// Prints per-frame timbre descriptors of an .ats model as CSV (see
// src/core/AtsDescriptors.h), followed by a summary footer.
//
// Usage: ats_describe input.ats

#include "AtsDescriptors.h"

#include <cstdio>
#include <cstdlib>

#if defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#define ATS_DESCRIBE_HAVE_DUP 1
#endif

namespace
{
// ats_load logs to stdout; route that to stderr so stdout is clean CSV.
int loadQuietly(ATS_SOUND* raw, const char* path, int* type)
{
#ifdef ATS_DESCRIBE_HAVE_DUP
    std::fflush(stdout);
    const int saved = dup(STDOUT_FILENO);
    if (saved >= 0)
        dup2(STDERR_FILENO, STDOUT_FILENO);
    const int rc = ats_load(raw, path, type);
    std::fflush(stdout);
    if (saved >= 0)
    {
        dup2(saved, STDOUT_FILENO);
        close(saved);
    }
    return rc;
#else
    return ats_load(raw, path, type);
#endif
}

void printSummary(const char* name, const ats::DescriptorSummary& s)
{
    std::printf("# %s: mean=%.6g stddev=%.6g min=%.6g max=%.6g\n", name, s.mean, s.stddev, s.min, s.max);
}
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s input.ats\n", argv[0]);
        return 2;
    }
    auto* raw = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    if (raw == nullptr)
        return 1;
    int type = 0;
    if (loadQuietly(raw, argv[1], &type) < 0)
    {
        std::free(raw);
        std::fprintf(stderr, "%s: not a valid ATS file\n", argv[1]);
        return 1;
    }
    const ats::AtsSoundPtr sound(raw);
    const auto track = ats::describeSound(sound.get());

    std::printf("frame,time,centroidHz,centroidBark,spreadHz,tilt,f0,hrc,noisiness,inharm,t1,t2,t3,oddEven\n");
    for (std::size_t f = 0; f < track.frames.size(); ++f)
    {
        const auto& d = track.frames[f];
        const auto time = raw->time != nullptr && raw->time[0] != nullptr ? raw->time[0][f] : 0.0;
        std::printf("%zu,%.6f,%.3f,%.4f,%.3f,%.3f,%.3f,%.4f,%.5f,%.6f,%.5f,%.5f,%.5f,%.4f\n",
                    f, time, d.centroidHz, d.centroidBark, d.spreadHz, d.tiltDbPerOctave, d.f0Hz,
                    d.harmonicRankCentroid, d.noisiness, d.inharmonicity, d.tristimulus[0], d.tristimulus[1],
                    d.tristimulus[2], d.oddEvenRatio);
    }
    std::printf("# file=%s type=%d frames=%d partials=%d srate=%d\n", argv[1], type, raw->frames, raw->partials, raw->srate);
    std::printf("# fundamentalHz=%.4f attackCentroidHz=%.3f\n", track.fundamentalHz, track.attackCentroidHz);
    printSummary("centroidHz", track.centroidHz);
    printSummary("centroidBark", track.centroidBark);
    printSummary("spreadHz", track.spreadHz);
    printSummary("tiltDbPerOctave", track.tiltDbPerOctave);
    printSummary("harmonicRankCentroid", track.harmonicRankCentroid);
    printSummary("noisiness", track.noisiness);
    printSummary("inharmonicity", track.inharmonicity);
    return 0;
}
