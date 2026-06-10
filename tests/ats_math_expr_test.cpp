#include "AtsMathExpr.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
void destroySound(ATS_SOUND& sound)
{
    free_sound(&sound);
}

bool require(bool condition, const char* message)
{
    if (!condition)
        std::cerr << "FAIL: " << message << "\n";
    return condition;
}
} // namespace

int main()
{
    ATS_SOUND sound {};
    init_sound(&sound, 48000, 256, 1024, 3, 3.0 / 48000.0, 2, 0);

    for (int p = 0; p < sound.partials; ++p)
    {
        for (int t = 0; t < sound.frames; ++t)
        {
            sound.amp[p][t] = 0.25 + 0.1 * p + 0.01 * t;
            sound.frq[p][t] = 100.0 + 10.0 * p + (double) t;
            sound.pha[p][t] = 0.2 * p + 0.03 * t;
        }
    }

    std::string error;
    const std::string script =
        "a=((a + 0.125) * 1.5 - 0.25) / 1.25\n"
        "f=pow(f, 1.0) + (p + t) * 0.5\n"
        "h=atan2(sin(h), cos(h)) + A(p, t) + F(p, t) * 0.0\n";
    if (!require(ats::applyMathExprScript(&sound, script, error), error.c_str()))
    {
        destroySound(sound);
        return 1;
    }

    bool ok = true;
    for (int p = 0; p < sound.partials; ++p)
    {
        for (int t = 0; t < sound.frames; ++t)
        {
            const double originalAmp = 0.25 + 0.1 * p + 0.01 * t;
            const double expectedAmp = ((originalAmp + 0.125) * 1.5 - 0.25) / 1.25;
            const double originalFreq = 100.0 + 10.0 * p + (double) t;
            const double expectedFreq = std::pow(originalFreq, 1.0) + (p + t) * 0.5;
            const double originalPhase = 0.2 * p + 0.03 * t;
            const double expectedPhase =
                std::atan2(std::sin(originalPhase), std::cos(originalPhase))
                + expectedAmp + expectedFreq * 0.0;
            ok = require(sound.amp[p][t] == expectedAmp, "amplitude evaluation order changed") && ok;
            ok = require(sound.frq[p][t] == expectedFreq, "frequency evaluation order changed") && ok;
            ok = require(sound.pha[p][t] == expectedPhase, "phase evaluation order changed") && ok;
        }
    }

    error.clear();
    ok = require(ats::applyMathExprScript(&sound, "a=2 ^ 3 ^ 2\nf=-2 ^ 2 + 10\n", error),
                 error.c_str()) && ok;
    for (int p = 0; p < sound.partials; ++p)
    {
        for (int t = 0; t < sound.frames; ++t)
        {
            ok = require(sound.amp[p][t] == 64.0, "power associativity changed") && ok;
            ok = require(sound.frq[p][t] == 6.0, "unary precedence changed") && ok;
        }
    }

    sound.amp[0][0] = 1.0;
    sound.amp[0][1] = 2.0;
    sound.amp[0][2] = 3.0;
    error.clear();
    ok = require(ats::applyMathExprScript(&sound, "a=a + 1 + A(p, t - 1)\n", error),
                 error.c_str()) && ok;
    ok = require(sound.amp[0][0] == 2.0, "first in-pass read changed") && ok;
    ok = require(sound.amp[0][1] == 5.0, "in-pass reads no longer see prior edited cells") && ok;
    ok = require(sound.amp[0][2] == 9.0, "in-pass read accumulation changed") && ok;

    sound.amp[0][0] = 4.0;
    sound.amp[0][1] = 5.0;
    error.clear();
    ok = require(!ats::applyMathExprScript(&sound, "a=1 / (t - 1)\n", error),
                 "division by zero should fail") && ok;
    ok = require(sound.amp[0][0] == 0.0, "cells before division failure should remain applied") && ok;
    ok = require(sound.amp[0][1] == 5.0, "division failure should occur before writing the failing cell") && ok;
    ok = require(error.find("partial 0, frame 1") != std::string::npos,
                 "division failure moved to a different cell") && ok;

    destroySound(sound);
    if (!ok)
        return 1;

    std::cout << "ok: compiled MathExpr preserves evaluation behavior\n";
    return 0;
}
