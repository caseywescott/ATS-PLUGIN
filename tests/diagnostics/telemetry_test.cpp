#include "../../src/diagnostics/ProgressPublisher.h"
#include "../../src/ui/UiActionDispatcher.h"

#include <cassert>
#include <limits>

int main()
{
    ats::diagnostics::ProgressPublisher progress;
    progress.begin(42);
    progress.update(std::numeric_limits<float>::quiet_NaN());
    auto snapshot = progress.snapshot();
    assert(snapshot.generation == 42 && snapshot.running && snapshot.fraction == 0.0f);

    progress.update(2.0f);
    snapshot = progress.snapshot();
    assert(snapshot.fraction == 1.0f);
    progress.cancel();
    snapshot = progress.snapshot();
    assert(snapshot.cancelled && !snapshot.running);

    ats::ui::ActionResultDispatcher results(1);
    ats::ui::ActionResult failed;
    failed.generation = 42;
    failed.status = ats::ui::ActionStatus::failed;
    failed.code = "invalid_source";
    failed.text = "source is unavailable";
    assert(results.publish(failed));
    assert(!results.publish(ats::ui::ActionResult {}));
    const auto observed = results.tryPop();
    assert(observed.has_value());
    assert(observed->generation == 42
           && observed->status == ats::ui::ActionStatus::failed
           && observed->code == "invalid_source");
    assert(!results.tryPop().has_value());
    return 0;
}
