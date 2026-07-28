#pragma once

#include "../engine/PartialEditService.h"

namespace ats::ui
{

// UI-neutral state for the Partials page. It owns draft text and pointer
// capture state; domain mutation remains in PartialEditService.
class PartialEditView
{
public:
    void beginCellEdit(int partial, int frame, double amplitude, double frequencyHz) noexcept
    {
        focusedPartial_ = partial;
        focusedFrame_ = frame;
        draftAmplitude_ = amplitude;
        draftFrequencyHz_ = frequencyHz;
        editing_ = true;
    }

    void cancelCellEdit() noexcept { editing_ = false; }
    [[nodiscard]] bool editing() const noexcept { return editing_; }
    [[nodiscard]] int focusedPartial() const noexcept { return focusedPartial_; }
    [[nodiscard]] int focusedFrame() const noexcept { return focusedFrame_; }
    void setDraftAmplitude(double value) noexcept { draftAmplitude_ = value; }
    void setDraftFrequencyHz(double value) noexcept { draftFrequencyHz_ = value; }

    [[nodiscard]] ats::PartialEditCommand commitCellEdit() noexcept
    {
        ats::PartialEditCommand command;
        command.kind = ats::PartialEditKind::writeCell;
        command.partial = focusedPartial_;
        command.frame = focusedFrame_;
        command.amplitude = draftAmplitude_;
        command.frequencyHz = draftFrequencyHz_;
        editing_ = false;
        return command;
    }

    void setPointerCaptured(bool captured) noexcept { pointerCaptured_ = captured; }
    [[nodiscard]] bool pointerCaptured() const noexcept { return pointerCaptured_; }
    void restoreFocus() noexcept { focusRestored_ = true; }
    [[nodiscard]] bool focusRestored() const noexcept { return focusRestored_; }

private:
    int focusedPartial_ = -1;
    int focusedFrame_ = -1;
    double draftAmplitude_ = 0.0;
    double draftFrequencyHz_ = 1.0;
    bool editing_ = false;
    bool pointerCaptured_ = false;
    bool focusRestored_ = false;
};

} // namespace ats::ui
