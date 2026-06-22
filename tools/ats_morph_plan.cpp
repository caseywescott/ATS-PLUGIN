/*
 * ats_morph_plan: compute optimal-transport correspondence between two ATS models.
 *
 * Uses each model's mean partial frequencies (converted to Bark) as positions
 * and mean partial amplitudes as masses, then runs buildOptimalTransportPlan()
 * from AtsMorphTransport.h.
 *
 * Usage: ats_morph_plan <source.ats> <target.ats>
 *
 * Output (JSON to stdout):
 *   {
 *     "source_partials": int,
 *     "target_partials": int,
 *     "source_mass": float,
 *     "target_mass": float,
 *     "transport_cost": float,
 *     "algorithm": "optimal_transport",
 *     "coupling": [[src_idx, tgt_idx, mass, src_bark, tgt_bark], ...]
 *   }
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

extern "C" {
#include "atsa.h"
}
#include "AtsMorphTransport.h"
#include "AtsSpectralEnvelope.h"   // for hzToBark

static std::string jd(double v) { std::ostringstream o; o << v; return o.str(); }
static std::string ji(int v)    { return std::to_string(v); }

// Build TransportPoints from a loaded ATS_SOUND using av[] partial averages.
// Position = hzToBark(mean_freq), mass = mean_amp.
static std::vector<ats::TransportPoint> soundToPoints(const ATS_SOUND* s)
{
    std::vector<ats::TransportPoint> pts;
    for (int p = 0; p < s->partials; ++p) {
        double frq = s->av[p].frq;
        double amp = s->av[p].amp;
        if (frq > 0.0 && amp > 0.0) {
            double bark = ats::BarkSpectralEnvelope::hzToBark(frq);
            pts.push_back({bark, amp, p});
        }
    }
    return pts;
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "Usage: ats_morph_plan <source.ats> <target.ats>\n");
        return 1;
    }

    const char* path_a = argv[1];
    const char* path_b = argv[2];

    ATS_SOUND* sa = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    ATS_SOUND* sb = static_cast<ATS_SOUND*>(std::calloc(1, sizeof(ATS_SOUND)));
    if (!sa || !sb) { std::fprintf(stderr, "Out of memory\n"); return 1; }

    int st_a = 0, st_b = 0;
    if (ats_load(sa, path_a, &st_a) < 0) { std::free(sa); std::free(sb); return 1; }
    if (ats_load(sb, path_b, &st_b) < 0) {
        free_sound(sa); std::free(sa); std::free(sb); return 1;
    }

    auto src = soundToPoints(sa);
    auto tgt = soundToPoints(sb);

    ats::TransportOptions opts;
    opts.normalizeMass = true;
    auto plan = ats::buildOptimalTransportPlan(src, tgt, opts);

    // Serialize
    std::ostringstream out;
    out << "{";
    out << "\"source_partials\":" << ji(sa->partials) << ",";
    out << "\"target_partials\":" << ji(sb->partials) << ",";
    out << "\"source_active_partials\":" << ji(static_cast<int>(src.size())) << ",";
    out << "\"target_active_partials\":" << ji(static_cast<int>(tgt.size())) << ",";
    out << "\"source_mass\":" << jd(plan.sourceMass) << ",";
    out << "\"target_mass\":" << jd(plan.targetMass) << ",";
    out << "\"transport_cost\":" << jd(plan.transportCost) << ",";
    out << "\"algorithm\":\"optimal_transport\",";
    out << "\"coupling\":[";
    for (size_t i = 0; i < plan.couplings.size(); ++i) {
        const auto& c = plan.couplings[i];
        if (i) out << ",";
        out << "[" << ji(c.sourceIndex) << "," << ji(c.targetIndex)
            << "," << jd(c.mass)
            << "," << jd(c.sourcePosition)
            << "," << jd(c.targetPosition) << "]";
    }
    out << "]}";

    std::printf("%s\n", out.str().c_str());

    free_sound(sa); std::free(sa);
    free_sound(sb); std::free(sb);
    return 0;
}
