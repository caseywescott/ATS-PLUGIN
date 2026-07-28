#include "../src/audio/PlayheadPublisher.h"
#include "../src/core/ParameterRegistry.h"
#include "../src/diagnostics/ProgressPublisher.h"
#include "../src/parameters/ParameterSnapshot.h"

#include <cassert>
#include <cmath>

int main()
{
    ats::ParameterStore store;
    assert(store.write("outputTrim", 0.75f));
    const auto parameters = ats::parameters::ParameterSnapshot::capture(store, 4);
    assert(parameters.count == store.size() && parameters.revision == 4);
    assert(std::abs(parameters.read(ats::ParameterRegistry::indexOf("outputTrim")) - 0.75f) < 1.0e-6f);

    ats::diagnostics::ProgressPublisher progress;
    progress.begin(8);
    progress.update(0.5f);
    assert(progress.snapshot().running && progress.snapshot().fraction == 0.5f);
    progress.update(NAN);
    progress.cancel();
    assert(progress.snapshot().cancelled && !progress.snapshot().running);

    ats::audio::PlayheadPublisher playhead;
    playhead.publishAudioRt(2.0f, true, 9);
    const auto snapshot = playhead.snapshot();
    assert(snapshot.revision == 9 && snapshot.normalizedTime == 1.0f && snapshot.playing);
    return 0;
}
