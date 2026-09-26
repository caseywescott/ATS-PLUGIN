#include "../src/ui/SpectrumRenderer.h"

#include <cassert>
#include <cmath>

using namespace ats::ui;

int main()
{
    std::vector<TraceVertex> smooth {
        {{0.0f, 0.0f}, 0.0f, 1.0f}, {{1.0f, 3.0f}, 1.0f, 1.0f},
        {{2.0f, 0.0f}, 0.0f, 1.0f}, {{20.0f, 0.0f}, 1.0f, 1.0f} };
    const auto first = smooth.front(), last = smooth.back();
    smoothInPlace(smooth, 2);
    assert(smooth.front().pos.x == first.pos.x && smooth.front().amp == first.amp);
    assert(smooth.back().pos.x == last.pos.x && smooth.back().amp == last.amp);
    decimateInPlace(smooth);
    assert(smooth.front().pos.x == first.pos.x && smooth.back().pos.x == last.pos.x);

    const auto trace = buildGradientTrace(smooth);
    assert(trace.vertices.size() == smooth.size());
    assert(!trace.stops.empty());
    assert(trace.stops.front().fade >= 0.18f && trace.stops.back().fade >= 0.18f);
    assert(trace.maxAmp >= 0.0f);
    assert(ampColour(0.0f).argb == 0xff1b4fa0u);
    return 0;
}
