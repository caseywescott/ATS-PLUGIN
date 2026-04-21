#include "SpectralEnvelopeEstimator.h"

#include "RegularizedCepstrum.h"

#include <algorithm>
#include <utility>

namespace ats
{

std::string SpectralEnvelopeEstimator::serialize(const FrameEnvelope& envelope) const
{
    return envelope.serialize();
}

bool EnvelopeEstimatorRegistry::registerFactory(std::string id, Factory factory)
{
    if (id.empty() || !factory || contains(id))
        return false;
    factories.emplace_back(std::move(id), std::move(factory));
    return true;
}

std::unique_ptr<SpectralEnvelopeEstimator> EnvelopeEstimatorRegistry::create(const std::string& id) const
{
    const auto it = std::find_if(factories.begin(), factories.end(), [&id](const auto& item) {
        return item.first == id;
    });
    if (it == factories.end())
        return nullptr;
    return it->second();
}

bool EnvelopeEstimatorRegistry::contains(const std::string& id) const
{
    return std::any_of(factories.begin(), factories.end(), [&id](const auto& item) {
        return item.first == id;
    });
}

std::vector<std::string> EnvelopeEstimatorRegistry::ids() const
{
    std::vector<std::string> result;
    result.reserve(factories.size());
    for (const auto& item : factories)
        result.push_back(item.first);
    return result;
}

std::string NoOpEnvelopeEstimator::id() const
{
    return kId;
}

std::uint32_t NoOpEnvelopeEstimator::version() const
{
    return kVersion;
}

bool NoOpEnvelopeEstimator::prepare(const EnvelopeEstimatorContext& nextContext, std::string* errorMessage)
{
    if (nextContext.sampleRate <= 0.0)
    {
        if (errorMessage)
            *errorMessage = "sample rate must be positive";
        prepared = false;
        return false;
    }

    context = nextContext;
    if (context.frequencyAxis.sampleRate <= 0.0)
        context.frequencyAxis.sampleRate = nextContext.sampleRate;

    lastDiagnostics = {};
    prepared = true;
    return true;
}

bool NoOpEnvelopeEstimator::estimate(const EnvelopeFrameInput& frame, FrameEnvelope& out, std::string* errorMessage)
{
    if (!prepared)
    {
        if (errorMessage)
            *errorMessage = "estimator is not prepared";
        return false;
    }

    lastDiagnostics = {};
    for (const auto& partial : frame.partials)
    {
        if (partial.selected)
            ++lastDiagnostics.fitPointCount;
    }

    out = {};
    out.estimatorId = id();
    out.estimatorVersion = version();
    out.sourceModelId = frame.sourceModelId;
    out.frameIndex = frame.frameIndex;
    out.sampleRate = frame.sampleRate > 0.0 ? frame.sampleRate : context.sampleRate;
    out.frequencyAxis = context.frequencyAxis;
    out.envelopeOrder = 0;
    out.regularization = 0.0;
    out.penaltyType = "none";
    out.storage = EnvelopeValueStorage::Samples;
    out.values = { 0.0 };
    out.diagnostics = lastDiagnostics;
    return true;
}

double NoOpEnvelopeEstimator::evaluate(const FrameEnvelope& envelope, double frequencyHz, bool* ok) const
{
    (void)frequencyHz;
    const bool valid = envelope.estimatorId == kId && !envelope.values.empty();
    if (ok)
        *ok = valid;
    return valid ? envelope.values.front() : 0.0;
}

EnvelopeDiagnostics NoOpEnvelopeEstimator::diagnostics() const
{
    return lastDiagnostics;
}

EnvelopeEstimatorRegistry createDefaultEnvelopeEstimatorRegistry()
{
    EnvelopeEstimatorRegistry registry;
    registry.registerFactory(NoOpEnvelopeEstimator::kId, [] {
        return std::make_unique<NoOpEnvelopeEstimator>();
    });
    registry.registerFactory(RegularizedCepstrumEnvelopeEstimator::kId, [] {
        return std::make_unique<RegularizedCepstrumEnvelopeEstimator>();
    });
    return registry;
}

} // namespace ats
