#pragma once

#include "SpectralEnvelope.h"

#include <string>
#include <vector>

namespace ats
{

enum class CepstrumPenalty
{
    None,
    FirstDerivative,
    SecondDerivative
};

struct RdcFitPoint
{
    double frequencyHz = 0.0;
    double logAmplitudeDb = 0.0;
    double weight = 1.0;
};

struct RdcFitOptions
{
    EnvelopeAxisSpec frequencyAxis;
    int order = 16;
    double regularization = 1.0e-3;
    CepstrumPenalty penalty = CepstrumPenalty::SecondDerivative;
    double ridge = 1.0e-10;
};

struct RdcFitResult
{
    bool ok = false;
    FrameEnvelope envelope;
    EnvelopeDiagnostics diagnostics;
    int effectiveOrder = 0;
    std::string errorMessage;
};

[[nodiscard]] const char* toString(CepstrumPenalty penalty);
[[nodiscard]] bool parseCepstrumPenalty(const std::string& text, CepstrumPenalty& out);

bool fitRegularizedDiscreteCepstrum(const std::vector<RdcFitPoint>& points,
                                    const RdcFitOptions& options,
                                    RdcFitResult& result);

[[nodiscard]] double evaluateRegularizedDiscreteCepstrum(const FrameEnvelope& envelope,
                                                         double frequencyHz,
                                                         bool* ok = nullptr);

} // namespace ats
