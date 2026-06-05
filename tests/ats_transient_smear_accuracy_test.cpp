#include "AtsTransientSmear.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
constexpr double kPi = 3.141592653589793238462643383279;

int fail(const std::string& message)
{
    std::cerr << "ats_transient_smear_accuracy_test: " << message << '\n';
    return 1;
}

struct ModelBuilder
{
    int srate = 48000;
    int frameSize = 256;
    int frames = 0;
    std::vector<std::vector<double>> ampRows;
    std::vector<std::vector<double>> frqRows;
    std::vector<std::vector<double>> smrRows;
    std::vector<std::vector<double>> phaRows;
    std::vector<std::vector<double>> bandRows;
    std::vector<double*> ampPtrs;
    std::vector<double*> frqPtrs;
    std::vector<double*> smrPtrs;
    std::vector<double*> phaPtrs;
    std::vector<double*> bandPtrs;
    ATS_SOUND sound {};

    explicit ModelBuilder(int numFrames = 0, int numPartials = 0, bool residual = false, bool phase = false)
        : frames(numFrames)
    {
        ampRows.assign((size_t) numPartials, std::vector<double>((size_t) frames, 0.0));
        frqRows.assign((size_t) numPartials, std::vector<double>((size_t) frames, 0.0));
        smrRows.assign((size_t) numPartials, std::vector<double>((size_t) frames, 60.0));
        if (phase)
            phaRows.assign((size_t) numPartials, std::vector<double>((size_t) frames, 0.0));
        if (residual)
            bandRows.assign(ATSA_CRITICAL_BANDS, std::vector<double>((size_t) frames, 0.0));
    }

    ATS_SOUND* build()
    {
        sound = ATS_SOUND {};
        sound.srate = srate;
        sound.frame_size = frameSize;
        sound.frames = frames;
        sound.partials = (int) ampRows.size();

        ampPtrs.clear();
        frqPtrs.clear();
        smrPtrs.clear();
        phaPtrs.clear();
        bandPtrs.clear();
        for (auto& row : ampRows) ampPtrs.push_back(row.data());
        for (auto& row : frqRows) frqPtrs.push_back(row.data());
        for (auto& row : smrRows) smrPtrs.push_back(row.data());
        for (auto& row : phaRows) phaPtrs.push_back(row.data());
        for (auto& row : bandRows) bandPtrs.push_back(row.data());

        sound.amp = ampPtrs.empty() ? nullptr : ampPtrs.data();
        sound.frq = frqPtrs.empty() ? nullptr : frqPtrs.data();
        sound.smr = smrPtrs.empty() ? nullptr : smrPtrs.data();
        sound.pha = phaPtrs.empty() ? nullptr : phaPtrs.data();
        sound.band_energy = bandPtrs.empty() ? nullptr : bandPtrs.data();
        return &sound;
    }
};

struct Fixture
{
    std::string name;
    ModelBuilder model;
    std::vector<int> referenceFrames;
    double minF1 = 0.8;
};

struct Score
{
    int matches = 0;
    int detected = 0;
    int reference = 0;
    double precision = 0.0;
    double recall = 0.0;
    double f1 = 0.0;
};

void addPartialEvent(ModelBuilder& model, int onsetFrame, double scale)
{
    static constexpr double weights[] = { 1.0, 0.74, 0.48, 0.31, 0.2 };
    static constexpr double attack[] = { 0.06, 0.38, 0.82, 1.0, 0.72, 0.48, 0.32, 0.22 };
    for (int p = 0; p < (int) model.ampRows.size(); ++p)
    {
        const double w = weights[std::min(p, (int) (sizeof(weights) / sizeof(weights[0])) - 1)];
        for (int i = -1; i < 20; ++i)
        {
            const int frame = onsetFrame + i;
            if (frame < 0 || frame >= model.frames)
                continue;
            const double env = i < 0 ? 0.02 : (i < (int) (sizeof(attack) / sizeof(attack[0]))
                                                    ? attack[i]
                                                    : 0.22 * std::exp(-0.22 * (double) (i - 7)));
            model.ampRows[(size_t) p][(size_t) frame] += scale * w * env;
        }
    }
}

void addResidualEvent(ModelBuilder& model, int onsetFrame, double scale, int firstBand, int lastBand)
{
    static constexpr double attack[] = { 0.03, 0.34, 0.95, 1.0, 0.62, 0.36, 0.22 };
    for (int band = std::max(firstBand, 0); band <= std::min(lastBand, ATSA_CRITICAL_BANDS - 1); ++band)
    {
        const double centre = 0.5 * (double) (firstBand + lastBand);
        const double spread = std::max(1.0, 0.5 * (double) (lastBand - firstBand + 1));
        const double bandWeight = std::exp(-0.5 * std::pow(((double) band - centre) / spread, 2.0));
        for (int i = -1; i < 18; ++i)
        {
            const int frame = onsetFrame + i;
            if (frame < 0 || frame >= model.frames)
                continue;
            const double env = i < 0 ? 0.01 : (i < (int) (sizeof(attack) / sizeof(attack[0]))
                                                    ? attack[i]
                                                    : 0.22 * std::exp(-0.28 * (double) (i - 6)));
            model.bandRows[(size_t) band][(size_t) frame] += scale * bandWeight * env;
        }
    }
}

void fillFrequenciesAndPhases(ModelBuilder& model)
{
    for (int p = 0; p < (int) model.frqRows.size(); ++p)
    {
        const double hz = 165.0 * (double) (p + 1);
        double phase = 0.17 * (double) p;
        for (int f = 0; f < model.frames; ++f)
        {
            model.frqRows[(size_t) p][(size_t) f] = hz;
            if (!model.phaRows.empty())
            {
                phase = std::fmod(phase + 2.0 * kPi * hz * (double) model.frameSize / (double) model.srate,
                                  2.0 * kPi);
                model.phaRows[(size_t) p][(size_t) f] = phase;
            }
        }
    }
}

Fixture makePluckedGuitarFixture()
{
    Fixture fixture { "plucked_guitar_model", ModelBuilder(96, 5, true, true), { 12, 43, 72 }, 0.8 };
    for (const auto frame : fixture.referenceFrames)
    {
        addPartialEvent(fixture.model, frame, 0.9);
        addResidualEvent(fixture.model, frame, 0.09, 8, 18);
    }
    fillFrequenciesAndPhases(fixture.model);
    return fixture;
}

Fixture makeCrotaleFixture()
{
    Fixture fixture { "crotale_hit_model", ModelBuilder(88, 4, true, true), { 18, 58 }, 0.8 };
    for (const auto frame : fixture.referenceFrames)
    {
        addPartialEvent(fixture.model, frame, 0.7);
        addResidualEvent(fixture.model, frame, 0.16, 14, 24);
    }
    fillFrequenciesAndPhases(fixture.model);
    return fixture;
}

Fixture makeDrumFixture()
{
    Fixture fixture { "drum_hit_model", ModelBuilder(90, 0, true, false), { 10, 32, 55, 76 }, 0.8 };
    for (const auto frame : fixture.referenceFrames)
        addResidualEvent(fixture.model, frame, 0.42, 5, 24);
    return fixture;
}

double hopSeconds(const ATS_SOUND* sound)
{
    return sound != nullptr && sound->srate > 0 && sound->frame_size > 0
               ? (double) sound->frame_size / (double) sound->srate
               : 0.0;
}

Score scoreOnsets(const std::vector<ats::AtsOnset>& detected,
                  const std::vector<int>& referenceFrames,
                  int toleranceFrames)
{
    std::vector<bool> used(detected.size(), false);
    int matches = 0;
    for (const int ref : referenceFrames)
    {
        int bestIndex = -1;
        int bestDistance = toleranceFrames + 1;
        for (int i = 0; i < (int) detected.size(); ++i)
        {
            if (used[(size_t) i])
                continue;
            const int distance = std::abs(detected[(size_t) i].frame - ref);
            if (distance <= toleranceFrames && distance < bestDistance)
            {
                bestDistance = distance;
                bestIndex = i;
            }
        }
        if (bestIndex >= 0)
        {
            used[(size_t) bestIndex] = true;
            ++matches;
        }
    }

    Score score;
    score.matches = matches;
    score.detected = (int) detected.size();
    score.reference = (int) referenceFrames.size();
    score.precision = score.detected > 0 ? (double) matches / (double) score.detected : 0.0;
    score.recall = score.reference > 0 ? (double) matches / (double) score.reference : 0.0;
    score.f1 = score.precision + score.recall > 0.0
                   ? 2.0 * score.precision * score.recall / (score.precision + score.recall)
                   : 0.0;
    return score;
}

std::vector<std::vector<double>> copyRows(double** rows, int rowCount, int frames)
{
    std::vector<std::vector<double>> copy((size_t) std::max(rowCount, 0));
    if (rows == nullptr)
        return copy;
    for (int r = 0; r < rowCount; ++r)
    {
        if (rows[r] == nullptr)
            continue;
        copy[(size_t) r].assign(rows[r], rows[r] + frames);
    }
    return copy;
}

// Mirror smearAttacks' blur-extended window so the invariants are checked against the
// frames the transform may actually touch (blur reaches ceil(3*sigma) past the peak).
std::vector<std::pair<int, int>> effectiveSmearWindows(const std::vector<ats::AtsOnset>& onsets,
                                                       double blurMs,
                                                       double hop,
                                                       int frames)
{
    const double sigmaFrames = (blurMs > 0.0 && hop > 0.0) ? (blurMs * 0.001) / hop : 0.0;
    const int blurExtend = sigmaFrames > 0.0 ? (int) std::ceil(3.0 * sigmaFrames) : 0;

    std::vector<std::pair<int, int>> windows;
    windows.reserve(onsets.size());
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        const int start = std::clamp(std::min(onsets[i].preFrame, onsets[i].peakFrame), 0, frames - 1);
        const int peak = std::clamp(std::max(onsets[i].preFrame, onsets[i].peakFrame), 0, frames - 1);
        int end = peak;
        if (blurExtend > 0)
        {
            int bound = frames - 1;
            if (i + 1 < onsets.size())
                bound = std::min(bound, std::min(onsets[i + 1].preFrame, onsets[i + 1].frame) - 1);
            end = std::clamp(peak + blurExtend, peak, std::max(peak, bound));
        }
        windows.emplace_back(start, end);
    }
    return windows;
}

bool inAnyWindow(int frame, const std::vector<std::pair<int, int>>& windows)
{
    for (const auto& w : windows)
        if (frame >= w.first && frame <= w.second)
            return true;
    return false;
}

double maxSlopeInWindow(const std::vector<double>& row, int start, int end)
{
    double maxSlope = 0.0;
    for (int frame = std::max(start + 1, 1); frame <= end; ++frame)
        maxSlope = std::max(maxSlope, std::abs(row[(size_t) frame] - row[(size_t) (frame - 1)]));
    return maxSlope;
}

double maxValueInWindow(const std::vector<double>& row, int start, int end)
{
    double peak = 0.0;
    for (int frame = start; frame <= end; ++frame)
        peak = std::max(peak, row[(size_t) frame]);
    return peak;
}

int checkRowsAfterSmear(const std::string& fixtureName,
                        const char* laneName,
                        const std::vector<std::vector<double>>& before,
                        double** afterRows,
                        int frames,
                        const std::vector<std::pair<int, int>>& windows,
                        double peakGain)
{
    if (before.empty() || afterRows == nullptr)
        return 0;

    for (int rowIndex = 0; rowIndex < (int) before.size(); ++rowIndex)
    {
        if (afterRows[rowIndex] == nullptr || before[(size_t) rowIndex].empty())
            continue;

        for (int frame = 0; frame < frames; ++frame)
        {
            const double value = afterRows[rowIndex][frame];
            if (!std::isfinite(value) || value < 0.0)
                return fail(fixtureName + ": " + laneName + " contains a non-finite or negative value");
            if (!inAnyWindow(frame, windows)
                && std::abs(value - before[(size_t) rowIndex][(size_t) frame]) > 1.0e-10)
                return fail(fixtureName + ": " + laneName + " changed outside an attack window");
        }

        for (const auto& window : windows)
        {
            const int start = window.first;
            const int end = window.second;
            const double beforePeak = maxValueInWindow(before[(size_t) rowIndex], start, end);
            const double afterPeak = maxValueInWindow(std::vector<double>(afterRows[rowIndex],
                                                                          afterRows[rowIndex] + frames),
                                                      start,
                                                      end);
            if (afterPeak > beforePeak * peakGain + 1.0e-9)
                return fail(fixtureName + ": " + laneName + " exceeded the smear gain bound");

            const double allowedJump = std::max(1.0e-9,
                                                maxSlopeInWindow(before[(size_t) rowIndex], start, end)
                                                    * peakGain
                                                    + 1.0e-9);
            if (start > 0)
            {
                const double jump = std::abs(afterRows[rowIndex][start] - afterRows[rowIndex][start - 1]);
                if (jump > allowedJump)
                    return fail(fixtureName + ": " + laneName + " introduced a start-edge discontinuity");
            }
            if (end + 1 < frames)
            {
                const double jump = std::abs(afterRows[rowIndex][end + 1] - afterRows[rowIndex][end]);
                if (jump > allowedJump)
                    return fail(fixtureName + ": " + laneName + " introduced an end-edge discontinuity");
            }
        }
    }

    return 0;
}

int checkPhaseUntouched(const std::string& fixtureName,
                        const std::vector<std::vector<double>>& before,
                        double** afterRows,
                        int frames)
{
    if (before.empty() || afterRows == nullptr)
        return 0;
    for (int row = 0; row < (int) before.size(); ++row)
        for (int frame = 0; frame < frames; ++frame)
            if (std::abs(afterRows[row][frame] - before[(size_t) row][(size_t) frame]) > 0.0)
                return fail(fixtureName + ": phase was edited by smearAttacks");
    return 0;
}
} // namespace

int main()
{
    std::vector<Fixture> fixtures;
    fixtures.push_back(makePluckedGuitarFixture());
    fixtures.push_back(makeCrotaleFixture());
    fixtures.push_back(makeDrumFixture());

    ats::SmearnovDetectParams detectParams;
    detectParams.maxAttackFrames = 8;

    for (auto& fixture : fixtures)
    {
        ATS_SOUND* sound = fixture.model.build();
        const auto onsets = ats::detectOnsets(sound, detectParams);
        const int toleranceFrames = std::max(1, (int) std::ceil(0.050 / hopSeconds(sound)));
        const auto score = scoreOnsets(onsets, fixture.referenceFrames, toleranceFrames);
        if (score.f1 < fixture.minF1)
        {
            std::string details = fixture.name + ": F1 " + std::to_string(score.f1)
                                  + " below target " + std::to_string(fixture.minF1)
                                  + " (matches=" + std::to_string(score.matches)
                                  + ", detected=" + std::to_string(score.detected)
                                  + ", reference=" + std::to_string(score.reference)
                                  + ")";
            return fail(details);
        }

        const auto ampBefore = copyRows(sound->amp, sound->partials, sound->frames);
        const auto bandBefore = copyRows(sound->band_energy, ATSA_CRITICAL_BANDS, sound->frames);
        const auto phaseBefore = copyRows(sound->pha, sound->partials, sound->frames);

        ats::SmearnovParams smearParams;
        smearParams.blurMs = 6.0;
        smearParams.sharpenAmount = 0.35;
        smearParams.exaggerateDb = 2.0;
        smearParams.maxDb = 6.0;
        smearParams.targets = ats::SmearnovTarget::Both;

        if (!ats::smearAttacks(sound, onsets, smearParams))
            return fail(fixture.name + ": smearAttacks should mutate fixture windows");

        const auto windows = effectiveSmearWindows(onsets, smearParams.blurMs,
                                                   hopSeconds(sound), sound->frames);
        const double amplitudeGain = std::pow(10.0, smearParams.exaggerateDb / 20.0);
        const double energyGain = std::pow(10.0, smearParams.exaggerateDb / 10.0);
        if (const int result = checkRowsAfterSmear(fixture.name,
                                                   "partials",
                                                   ampBefore,
                                                   sound->amp,
                                                   sound->frames,
                                                   windows,
                                                   amplitudeGain))
            return result;
        if (const int result = checkRowsAfterSmear(fixture.name,
                                                   "residual",
                                                   bandBefore,
                                                   sound->band_energy,
                                                   sound->frames,
                                                   windows,
                                                   energyGain))
            return result;
        if (const int result = checkPhaseUntouched(fixture.name, phaseBefore, sound->pha, sound->frames))
            return result;
    }

    return 0;
}
