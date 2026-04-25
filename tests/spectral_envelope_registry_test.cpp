#include "SpectralEnvelopeEstimator.h"
#include "F0AdaptiveEnvelope.h"
#include "LpcEnvelope.h"
#include "RegularizedCepstrum.h"
#include "TrueEnvelope.h"

#include <cmath>
#include <iostream>
#include <string>

namespace
{

int fails = 0;

void check(const bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++fails;
    }
}

} // namespace

int main()
{
    // --- Empty registry -------------------------------------------------------
    {
        ats::EnvelopeEstimatorRegistry empty;
        check(!empty.contains("anything"), "empty registry should not contain any id");
        check(empty.ids().empty(), "empty registry ids() should be empty");
        check(!empty.create("anything"), "empty registry create should return nullptr");
    }

    // --- Duplicate rejection --------------------------------------------------
    {
        ats::EnvelopeEstimatorRegistry reg;
        const bool first = reg.registerFactory("my/estimator", [] {
            return std::make_unique<ats::NoOpEnvelopeEstimator>();
        });
        check(first, "first registration should succeed");

        const bool second = reg.registerFactory("my/estimator", [] {
            return std::make_unique<ats::NoOpEnvelopeEstimator>();
        });
        check(!second, "duplicate registration should be rejected");

        check(reg.ids().size() == 1, "duplicate registration must not change id count");
    }

    // --- Empty id and null factory rejection ----------------------------------
    {
        ats::EnvelopeEstimatorRegistry reg;
        check(!reg.registerFactory("", [] { return std::make_unique<ats::NoOpEnvelopeEstimator>(); }),
              "empty id must be rejected");
        check(!reg.registerFactory("valid/id", nullptr), "null factory must be rejected");
        check(reg.ids().empty(), "rejected factories must not appear in ids()");
    }

    // --- Insertion-order preservation -----------------------------------------
    {
        ats::EnvelopeEstimatorRegistry reg;
        const std::vector<std::string> insertOrder = {
            "first/estimator",
            "second/estimator",
            "third/estimator",
        };
        for (const auto& id : insertOrder)
            reg.registerFactory(id, [] { return std::make_unique<ats::NoOpEnvelopeEstimator>(); });

        const auto ids = reg.ids();
        check(ids.size() == insertOrder.size(), "ids() count should match registered factories");
        for (std::size_t i = 0; i < insertOrder.size(); ++i)
            check(ids[i] == insertOrder[i], "ids() should preserve insertion order: " + insertOrder[i]);
    }

    // --- Each factory creates a fresh instance --------------------------------
    {
        ats::EnvelopeEstimatorRegistry reg;
        reg.registerFactory("shared/estimator", [] { return std::make_unique<ats::NoOpEnvelopeEstimator>(); });

        auto a = reg.create("shared/estimator");
        auto b = reg.create("shared/estimator");
        check(a != nullptr, "first create should return non-null");
        check(b != nullptr, "second create should return non-null");
        check(a.get() != b.get(), "each create() call should return a distinct instance");
    }

    // --- Default registry has stable id ordering and all known estimators -----
    {
        auto reg = ats::createDefaultEnvelopeEstimatorRegistry();

        const std::vector<std::string> expected = {
            ats::NoOpEnvelopeEstimator::kId,
            ats::RegularizedCepstrumEnvelopeEstimator::kId,
            ats::TrueEnvelopeEstimator::kId,
            ats::LpcEnvelopeEstimator::kId,
            ats::F0AdaptiveEnvelopeEstimator::kId,
        };
        const auto ids = reg.ids();
        check(ids.size() == expected.size(), "default registry should have exactly 5 estimators");
        for (std::size_t i = 0; i < expected.size() && i < ids.size(); ++i)
            check(ids[i] == expected[i], "default registry id[" + std::to_string(i) + "] order mismatch: " + expected[i]);

        for (const auto& id : expected)
        {
            check(reg.contains(id), "default registry should contain: " + id);
            auto inst = reg.create(id);
            check(inst != nullptr, "default registry should create: " + id);
            if (inst)
                check(inst->id() == id, "created estimator id() should match registered id: " + id);
        }

        check(!reg.create("not/registered"), "unknown id must return nullptr from default registry");
    }

    // --- Prepared RDC estimator produces a valid envelope ---------------------
    {
        auto reg = ats::createDefaultEnvelopeEstimatorRegistry();
        auto estimator = reg.create(ats::RegularizedCepstrumEnvelopeEstimator::kId);
        check(estimator != nullptr, "RDC estimator must be creatable from default registry");
        if (estimator)
        {
            ats::EnvelopeEstimatorContext ctx;
            ctx.sampleRate = 44100.0;
            ctx.frequencyAxis.type = ats::EnvelopeFrequencyAxis::LinearHz;
            ctx.frequencyAxis.sampleRate = 44100.0;
            ctx.frequencyAxis.minFrequencyHz = 50.0;
            ctx.frequencyAxis.maxFrequencyHz = 10000.0;
            ctx.maxOrder = 12;

            std::string err;
            check(estimator->prepare(ctx, &err), "prepare should succeed: " + err);

            ats::EnvelopeFrameInput frame;
            frame.sourceModelId = "registry-test";
            frame.frameIndex = 0;
            frame.sampleRate = 44100.0;
            frame.partials = {
                { 110.0,  0.80, 10.0, 1.0, true },
                { 440.0,  0.50,  8.0, 1.0, true },
                { 880.0,  0.30,  5.0, 1.0, true },
                { 1760.0, 0.15,  3.0, 1.0, true },
                { 3520.0, 0.07,  1.0, 1.0, true },
                { 7040.0, 0.02,  0.5, 1.0, false },
            };

            ats::FrameEnvelope envelope;
            check(estimator->estimate(frame, envelope, &err), "estimate should succeed: " + err);
            check(envelope.estimatorId == ats::RegularizedCepstrumEnvelopeEstimator::kId,
                  "estimator id should be set on output envelope");
            check(!envelope.values.empty(), "estimate should produce non-empty values");

            bool ok = false;
            const double ev = estimator->evaluate(envelope, 440.0, &ok);
            check(ok, "evaluate should succeed at a known frequency");
            check(std::isfinite(ev), "evaluate should produce a finite value");
        }
    }

    if (fails == 0)
        std::cout << "ok: spectral envelope estimator registry\n";
    else
        std::cerr << fails << " check(s) failed\n";

    return fails > 0 ? 1 : 0;
}
