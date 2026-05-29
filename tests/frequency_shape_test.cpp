#include "AtsFrequencyShape.h"

#include <cmath>
#include <iostream>

namespace
{
int fail(const char* message)
{
    std::cerr << message << '\n';
    return 1;
}

bool approx(double a, double b, double tolerance = 1.0e-9)
{
    return std::abs(a - b) <= tolerance;
}
} // namespace

int main()
{
    ats::FrequencyShapeSettings stretch;
    stretch.mode = ats::FrequencyShapeMode::Stretch;
    stretch.amount = 1.0;
    stretch.resolvedPivotHz = 200.0;
    if (!approx(ats::applyFrequencyShape(440.0, 1, stretch, 24000.0), 440.0))
        return fail("stretch amount=1 should be identity");

    stretch.amount = 2.0;
    if (!approx(ats::applyFrequencyShape(400.0, 1, stretch, 24000.0), 800.0))
        return fail("stretch log-pivot warp is incorrect");

    ats::FrequencyShapeSettings compress;
    compress.mode = ats::FrequencyShapeMode::Compress;
    compress.amount = 0.5;
    compress.resolvedPivotHz = 200.0;
    if (!approx(ats::applyFrequencyShape(800.0, 1, compress, 24000.0), 400.0))
        return fail("compress log-pivot warp is incorrect");

    ats::FrequencyShapeSettings harmonic;
    harmonic.mode = ats::FrequencyShapeMode::Harmonic;
    harmonic.amount = 2.0;
    harmonic.resolvedPivotHz = 100.0;
    if (!approx(ats::applyFrequencyShape(250.0, 2, harmonic, 24000.0), 312.5))
        return fail("harmonic rank warp is incorrect");

    double frq0[] = { 400.0, 400.0, 400.0 };
    double frq1[] = { 100.0, 100.0, 100.0 };
    double amp0[] = { 1.0, 2.0, 1.0 };
    double amp1[] = { 1.0, 1.0, 1.0 };
    double* frq[] = { frq0, frq1 };
    double* amp[] = { amp0, amp1 };
    ATS_SOUND sound {};
    sound.srate = 48000;
    sound.partials = 2;
    sound.frames = 3;
    sound.frq = frq;
    sound.amp = amp;

    const auto ranks = ats::buildFrequencyShapeRanks(&sound);
    if (ranks.size() != 2 || ranks[0] != 2 || ranks[1] != 1)
        return fail("harmonic ranks should sort partials by mean active frequency");

    frq0[1] = 200.0;
    frq1[1] = 400.0;
    ats::FrequencyShapeSettings autoPivot;
    autoPivot.mode = ats::FrequencyShapeMode::Stretch;
    autoPivot.pivotHz = 0.0;
    if (!approx(ats::resolveFrequencyShapePivotHz(&sound, autoPivot), (200.0 * 2.0 + 400.0) / 3.0))
        return fail("auto pivot should use amplitude-weighted mid-frame frequency");

    return 0;
}
