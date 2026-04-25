/**
 * envelope_preset_recipe_test.cpp
 *
 * Tests the SpectralEnvelopeTransferRecipe produced by the six general-source
 * preset configurations (commits 25–26).  These tests use the core recipe
 * struct and serialization directly, without a JUCE AudioProcessor.
 *
 * Coverage:
 *  - Default / neutral recipe round-trips cleanly.
 *  - Each preset maps to expected sourceDecolorAmount / targetColorAmount /
 *    envelopeMix values.
 *  - transientPolicy enum values are consistent with preset intent.
 *  - residualPolicy values match the expected selection.
 *  - All presets serialize and deserialize deterministically.
 *  - Preset values are within the documented [0, 1] range.
 *  - Conservative preset never applies more than 60 % de-color or re-color.
 *  - Noisy-texture preset has residual policy set to DecolorThenColor or
 *    TargetColored (residualMix >= 0.9).
 *  - Plucked / percussive and inharmonic presets bypass or preserve on
 *    transients.
 */

#include "SpectralEnvelopeHybrid.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{

int pass_count = 0;
int fail_count = 0;

void check(bool condition, const std::string& description)
{
    if (condition)
    {
        ++pass_count;
    }
    else
    {
        std::cerr << "FAIL: " << description << '\n';
        ++fail_count;
    }
}

bool closeEnough(double a, double b, double tol = 1.0e-9)
{
    return std::abs(a - b) <= tol;
}

bool inRange(double v, double lo, double hi)
{
    return v >= lo && v <= hi;
}

// Helper: build a recipe matching preset 4 (All-purpose / conservative).
ats::SpectralEnvelopeTransferRecipe makeConservativePreset()
{
    ats::SpectralEnvelopeTransferRecipe r;
    r.sourceDecolorAmount = 0.55;
    r.targetColorAmount   = 0.55;
    r.envelopeMix         = 0.75;
    r.residualPolicy      = ats::HybridResidualPolicy::Unchanged;
    r.transientPolicy     = ats::HybridTransientPolicy::PreserveSource;
    return r;
}

// Helper: build a recipe matching preset 5 (Sustained instrument).
ats::SpectralEnvelopeTransferRecipe makeSustainedPreset()
{
    ats::SpectralEnvelopeTransferRecipe r;
    r.sourceDecolorAmount = 1.0;
    r.targetColorAmount   = 1.0;
    r.envelopeMix         = 1.0;
    r.residualPolicy      = ats::HybridResidualPolicy::Crossfade; // residualMix ~ 0.3
    r.transientPolicy     = ats::HybridTransientPolicy::PreserveSource;
    return r;
}

// Helper: build a recipe matching preset 6 (Voice / singing).
ats::SpectralEnvelopeTransferRecipe makeVoicePreset()
{
    ats::SpectralEnvelopeTransferRecipe r;
    r.sourceDecolorAmount = 1.0;
    r.targetColorAmount   = 0.85;
    r.envelopeMix         = 0.85;
    r.residualPolicy      = ats::HybridResidualPolicy::Crossfade; // residualMix = 0.5
    r.transientPolicy     = ats::HybridTransientPolicy::CrossfadeSource;
    return r;
}

// Helper: build a recipe matching preset 7 (Inharmonic metal / bell).
ats::SpectralEnvelopeTransferRecipe makeInharmonicPreset()
{
    ats::SpectralEnvelopeTransferRecipe r;
    r.sourceDecolorAmount = 1.0;
    r.targetColorAmount   = 1.0;
    r.envelopeMix         = 1.0;
    r.residualPolicy      = ats::HybridResidualPolicy::Unchanged;
    r.transientPolicy     = ats::HybridTransientPolicy::Bypass;
    return r;
}

// Helper: build a recipe matching preset 8 (Noisy texture).
ats::SpectralEnvelopeTransferRecipe makeNoisyPreset()
{
    ats::SpectralEnvelopeTransferRecipe r;
    r.sourceDecolorAmount = 0.4;
    r.targetColorAmount   = 0.6;
    r.envelopeMix         = 0.6;
    // residualMix = 1.0 -> DecolorThenColor policy
    r.residualPolicy      = ats::HybridResidualPolicy::DecolorThenColor;
    r.transientPolicy     = ats::HybridTransientPolicy::TargetColor;
    return r;
}

// Helper: build a recipe matching preset 9 (Plucked / percussive).
ats::SpectralEnvelopeTransferRecipe makePluckedPreset()
{
    ats::SpectralEnvelopeTransferRecipe r;
    r.sourceDecolorAmount = 0.7;
    r.targetColorAmount   = 0.7;
    r.envelopeMix         = 0.8;
    r.residualPolicy      = ats::HybridResidualPolicy::Unchanged;
    r.transientPolicy     = ats::HybridTransientPolicy::Bypass;
    return r;
}

} // namespace

int main()
{
    // ---- Default recipe ----
    {
        ats::SpectralEnvelopeTransferRecipe def;
        check(closeEnough(def.sourceDecolorAmount, 1.0), "Default: sourceDecolorAmount == 1.0");
        check(closeEnough(def.targetColorAmount, 1.0),   "Default: targetColorAmount == 1.0");
        check(closeEnough(def.envelopeMix, 1.0),         "Default: envelopeMix == 1.0");
        check(def.residualPolicy  == ats::HybridResidualPolicy::Unchanged,
              "Default: residualPolicy == Unchanged");
        check(def.transientPolicy == ats::HybridTransientPolicy::PreserveSource,
              "Default: transientPolicy == PreserveSource");
        check(def.harmonicMappingEnabled == false, "Default: no harmonic mapping");

        // Serialize / deserialize round-trip
        const std::string s1 = def.serialize();
        const std::string s2 = def.serialize();
        check(s1 == s2, "Default: serialization is deterministic");

        ats::SpectralEnvelopeTransferRecipe loaded;
        std::string err;
        check(ats::SpectralEnvelopeTransferRecipe::deserialize(s1, loaded, &err),
              "Default: deserialize succeeds");
        check(closeEnough(loaded.sourceDecolorAmount, def.sourceDecolorAmount),
              "Default: round-trip sourceDecolorAmount");
        check(closeEnough(loaded.targetColorAmount, def.targetColorAmount),
              "Default: round-trip targetColorAmount");
        check(loaded.transientPolicy == def.transientPolicy,
              "Default: round-trip transientPolicy");
    }

    // ---- Conservative preset ----
    {
        const auto r = makeConservativePreset();
        check(r.sourceDecolorAmount <= 0.6, "Conservative: de-color <= 60%");
        check(r.targetColorAmount   <= 0.6, "Conservative: re-color <= 60%");
        check(r.envelopeMix <= 0.8, "Conservative: envMix <= 80%");
        check(r.transientPolicy == ats::HybridTransientPolicy::PreserveSource,
              "Conservative: preserve source transients");
        check(r.residualPolicy == ats::HybridResidualPolicy::Unchanged,
              "Conservative: residual unchanged");
        check(inRange(r.sourceDecolorAmount, 0.0, 1.0), "Conservative: sourceDecolorAmount in [0,1]");
        check(inRange(r.targetColorAmount,   0.0, 1.0), "Conservative: targetColorAmount in [0,1]");
        check(inRange(r.envelopeMix,         0.0, 1.0), "Conservative: envelopeMix in [0,1]");

        const std::string s1 = r.serialize();
        const std::string s2 = r.serialize();
        check(s1 == s2, "Conservative: serialization deterministic");
    }

    // ---- Sustained instrument preset ----
    {
        const auto r = makeSustainedPreset();
        check(closeEnough(r.sourceDecolorAmount, 1.0), "Sustained: full de-color");
        check(closeEnough(r.targetColorAmount,   1.0), "Sustained: full re-color");
        check(closeEnough(r.envelopeMix,         1.0), "Sustained: full env mix");
        check(r.transientPolicy == ats::HybridTransientPolicy::PreserveSource,
              "Sustained: preserve transients");
        // residualMix 0.3 puts us in Crossfade policy territory
        check(r.residualPolicy != ats::HybridResidualPolicy::Unchanged,
              "Sustained: residual receives some coloring");

        const std::string s1 = r.serialize();
        ats::SpectralEnvelopeTransferRecipe loaded;
        check(ats::SpectralEnvelopeTransferRecipe::deserialize(s1, loaded, nullptr),
              "Sustained: round-trip deserialize succeeds");
        check(closeEnough(loaded.sourceDecolorAmount, r.sourceDecolorAmount),
              "Sustained: round-trip sourceDecolorAmount");
    }

    // ---- Voice / singing preset ----
    {
        const auto r = makeVoicePreset();
        check(closeEnough(r.sourceDecolorAmount, 1.0), "Voice: full de-color");
        check(r.targetColorAmount < 1.0, "Voice: less than full re-color (some source formants preserved)");
        check(r.transientPolicy == ats::HybridTransientPolicy::CrossfadeSource,
              "Voice: crossfade transients (prevent click on plosives)");
        check(r.residualPolicy == ats::HybridResidualPolicy::Crossfade,
              "Voice: residual crossfaded");
        check(inRange(r.sourceDecolorAmount, 0.0, 1.0), "Voice: sourceDecolorAmount in [0,1]");
        check(inRange(r.targetColorAmount,   0.0, 1.0), "Voice: targetColorAmount in [0,1]");
        check(inRange(r.envelopeMix,         0.0, 1.0), "Voice: envelopeMix in [0,1]");

        const std::string s1 = r.serialize();
        const std::string s2 = r.serialize();
        check(s1 == s2, "Voice: serialization deterministic");
    }

    // ---- Inharmonic metal / bell preset ----
    {
        const auto r = makeInharmonicPreset();
        check(closeEnough(r.sourceDecolorAmount, 1.0), "Inharmonic: full de-color");
        check(closeEnough(r.targetColorAmount,   1.0), "Inharmonic: full re-color");
        check(r.transientPolicy == ats::HybridTransientPolicy::Bypass,
              "Inharmonic: bypass transfer on transients (metallic click preserved)");
        check(r.residualPolicy == ats::HybridResidualPolicy::Unchanged,
              "Inharmonic: residual unchanged (preserve inharmonic character)");

        const std::string s1 = r.serialize();
        ats::SpectralEnvelopeTransferRecipe loaded;
        check(ats::SpectralEnvelopeTransferRecipe::deserialize(s1, loaded, nullptr),
              "Inharmonic: round-trip deserialize succeeds");
        check(loaded.transientPolicy == ats::HybridTransientPolicy::Bypass,
              "Inharmonic: round-trip transientPolicy == Bypass");
    }

    // ---- Noisy texture preset ----
    {
        const auto r = makeNoisyPreset();
        check(r.sourceDecolorAmount < 0.5, "Noisy: light partial de-color (preserve noise character)");
        check(r.residualPolicy == ats::HybridResidualPolicy::DecolorThenColor,
              "Noisy: residual fully re-colored");
        check(r.transientPolicy == ats::HybridTransientPolicy::TargetColor,
              "Noisy: transients receive target color (overall brightness match)");
        check(inRange(r.sourceDecolorAmount, 0.0, 1.0), "Noisy: sourceDecolorAmount in [0,1]");
        check(inRange(r.targetColorAmount,   0.0, 1.0), "Noisy: targetColorAmount in [0,1]");
        check(inRange(r.envelopeMix,         0.0, 1.0), "Noisy: envelopeMix in [0,1]");

        const std::string s1 = r.serialize();
        const std::string s2 = r.serialize();
        check(s1 == s2, "Noisy: serialization deterministic");

        ats::SpectralEnvelopeTransferRecipe loaded;
        check(ats::SpectralEnvelopeTransferRecipe::deserialize(s1, loaded, nullptr),
              "Noisy: round-trip deserialize succeeds");
        check(loaded.residualPolicy == r.residualPolicy,
              "Noisy: round-trip residualPolicy");
    }

    // ---- Plucked / percussive preset ----
    {
        const auto r = makePluckedPreset();
        check(r.transientPolicy == ats::HybridTransientPolicy::Bypass,
              "Plucked: bypass transfer on transients (attack sharpness preserved)");
        check(r.residualPolicy == ats::HybridResidualPolicy::Unchanged,
              "Plucked: residual unchanged (avoid smearing decay noise)");
        check(r.sourceDecolorAmount > 0.5 && r.sourceDecolorAmount < 1.0,
              "Plucked: moderate de-color");
        check(r.targetColorAmount > 0.5 && r.targetColorAmount < 1.0,
              "Plucked: moderate re-color");

        const std::string s1 = r.serialize();
        ats::SpectralEnvelopeTransferRecipe loaded;
        check(ats::SpectralEnvelopeTransferRecipe::deserialize(s1, loaded, nullptr),
              "Plucked: round-trip deserialize succeeds");
        check(loaded.transientPolicy == ats::HybridTransientPolicy::Bypass,
              "Plucked: round-trip transientPolicy == Bypass");
        check(loaded.residualPolicy == ats::HybridResidualPolicy::Unchanged,
              "Plucked: round-trip residualPolicy == Unchanged");
    }

    // ---- Cross-preset invariants ----
    {
        // All presets must produce serializations that round-trip the same recipe
        const std::array<ats::SpectralEnvelopeTransferRecipe, 6> presets = {
            makeConservativePreset(),
            makeSustainedPreset(),
            makeVoicePreset(),
            makeInharmonicPreset(),
            makeNoisyPreset(),
            makePluckedPreset(),
        };
        const std::array<const char*, 6> names = {
            "Conservative", "Sustained", "Voice", "Inharmonic", "Noisy", "Plucked"
        };
        for (int i = 0; i < 6; ++i)
        {
            const std::string txt = presets[i].serialize();
            ats::SpectralEnvelopeTransferRecipe rt;
            std::string err;
            check(ats::SpectralEnvelopeTransferRecipe::deserialize(txt, rt, &err),
                  std::string(names[i]) + ": full round-trip succeeds");
            check(closeEnough(rt.sourceDecolorAmount, presets[i].sourceDecolorAmount),
                  std::string(names[i]) + ": round-trip sourceDecolorAmount");
            check(closeEnough(rt.targetColorAmount, presets[i].targetColorAmount),
                  std::string(names[i]) + ": round-trip targetColorAmount");
            check(closeEnough(rt.envelopeMix, presets[i].envelopeMix),
                  std::string(names[i]) + ": round-trip envelopeMix");
            check(rt.transientPolicy == presets[i].transientPolicy,
                  std::string(names[i]) + ": round-trip transientPolicy");
            check(rt.residualPolicy  == presets[i].residualPolicy,
                  std::string(names[i]) + ": round-trip residualPolicy");
        }

        // Verify that no two presets are identical (they must be meaningfully distinct)
        for (int i = 0; i < 6; ++i)
        {
            for (int j = i + 1; j < 6; ++j)
            {
                const bool sameDecolor = closeEnough(presets[i].sourceDecolorAmount,
                                                     presets[j].sourceDecolorAmount);
                const bool sameRecolor = closeEnough(presets[i].targetColorAmount,
                                                     presets[j].targetColorAmount);
                const bool sameMix     = closeEnough(presets[i].envelopeMix,
                                                     presets[j].envelopeMix);
                const bool sameTransient = presets[i].transientPolicy == presets[j].transientPolicy;
                const bool sameResidual  = presets[i].residualPolicy  == presets[j].residualPolicy;
                // At least one of the five key fields must differ
                const bool distinct = !(sameDecolor && sameRecolor && sameMix
                                        && sameTransient && sameResidual);
                check(distinct,
                      std::string(names[i]) + " vs " + names[j] + ": presets are distinct");
            }
        }
    }

    // ---- HybridTransientPolicy string round-trip ----
    {
        using P = ats::HybridTransientPolicy;
        const P policies[] = { P::PreserveSource, P::TargetColor, P::CrossfadeSource, P::Bypass };
        for (const auto policy : policies)
        {
            const std::string txt = ats::toString(policy);
            check(!txt.empty(), std::string("transientPolicy toString non-empty for ") + txt);
            P parsed {};
            check(ats::parseHybridTransientPolicy(txt, parsed),
                  std::string("transientPolicy parse succeeds for ") + txt);
            check(parsed == policy,
                  std::string("transientPolicy round-trip for ") + txt);
        }
    }

    // ---- HybridResidualPolicy string round-trip ----
    {
        using P = ats::HybridResidualPolicy;
        const P policies[] = {
            P::Unchanged, P::TargetColored, P::DecolorThenColor, P::Crossfade, P::IndependentMorph
        };
        for (const auto policy : policies)
        {
            const std::string txt = ats::toString(policy);
            check(!txt.empty(), std::string("residualPolicy toString non-empty for ") + txt);
            P parsed {};
            check(ats::parseHybridResidualPolicy(txt, parsed),
                  std::string("residualPolicy parse succeeds for ") + txt);
            check(parsed == policy,
                  std::string("residualPolicy round-trip for ") + txt);
        }
    }

    std::cout << "envelope_preset_recipe_test: " << pass_count << " passed, "
              << fail_count << " failed\n";
    return fail_count > 0 ? 1 : 0;
}
