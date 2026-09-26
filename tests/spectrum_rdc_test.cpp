#include "../src/ui/SpectrumRdc.h"

#include <cassert>
#include <cmath>

int main()
{
    const auto solved = ats::ui::rdcDisplay::solveGaussian(
        {{ 3.0, 2.0 }, { 1.0, 2.0 }}, { 8.0, 4.0 });
    assert(solved.has_value());
    assert(std::abs((*solved)[0] - 2.0) < 1.0e-6);
    assert(std::abs((*solved)[1] - 1.0) < 1.0e-6);

    const auto fit = ats::ui::rdcDisplay::fitRDC(
        { 200.0, 700.0, 1400.0, 3200.0, 8000.0 },
        { 0.25, 0.25, 0.25, 0.25, 0.25 }, 80.0, 12000.0, 3);
    assert(fit.valid);
    assert(std::isfinite(fit.evaluate(0.0)));
    assert(std::isfinite(fit.evaluate(1.0)));
    // RDC evaluates in the shipped dB domain: amplitude .25 == -72 dB.
    assert(fit.evaluate(0.5) > -120.0 && fit.evaluate(0.5) < 0.0);

    std::vector<float> blurred { 0.0f, 1.0f, 0.0f };
    ats::ui::rdcDisplay::boxBlurInPlace(blurred, 1);
    assert(blurred[0] > 0.0f && blurred[1] < 1.0f);
    assert(ats::ui::rdcDisplay::boxRadiiForGauss(2.0) == 3);
    assert(ats::ui::rdcDisplay::makeSliceTimes(32, 5).back() == 1.0f);
    return 0;
}
