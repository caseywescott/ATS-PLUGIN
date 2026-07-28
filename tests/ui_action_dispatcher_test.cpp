#include "../src/ui/UiActionDispatcher.h"

#include <cassert>

int main()
{
    ats::ui::UiActionDispatcher queue(2);
    assert(queue.submit(ats::ui::AnalyzeCurrentAction {}));
    assert(queue.submit(ats::ui::SetParameterAction { "freq", 2.0f, true, false }));
    assert(!queue.submit(ats::ui::StopPreviewAction {}));

    auto first = queue.tryPop();
    assert(first.has_value() && std::holds_alternative<ats::ui::AnalyzeCurrentAction>(*first));
    auto second = queue.tryPop();
    assert(second.has_value() && std::holds_alternative<ats::ui::SetParameterAction>(*second));
    assert(!queue.tryPop().has_value());

    ats::ui::TelemetryPublisher publisher;
    ats::ui::TelemetrySnapshot snapshot;
    snapshot.state = ats::ui::UiState::resynthesizing;
    snapshot.renderRunning = true;
    snapshot.modelRevision = 7;
    snapshot.previewPlaying = true;
    snapshot.normalizedPlayhead = 0.25f;
    publisher.publish(snapshot);
    const auto observed = publisher.snapshot();
    assert(observed.state == ats::ui::UiState::resynthesizing);
    assert(observed.renderRunning && observed.previewPlaying);
    assert(observed.modelRevision == 7 && observed.normalizedPlayhead == 0.25f);
    return 0;
}
