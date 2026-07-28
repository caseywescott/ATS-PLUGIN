#pragma once

#include "EditorHost.h"
#include "EditorPageModels.h"
#include "SessionPageModel.h"
#include "SpectrumInteractionModel.h"
#include "ThemeTokens.h"

#include <array>
#include <cstddef>

namespace ats::ui
{

struct EditorShellState
{
    EditorPage page = EditorPage::session;
    ThemeId theme = ThemeId::ember;
    bool canUndo = false;
    bool canRedo = false;
    bool analysisRunning = false;
    bool renderRunning = false;
};

class EditorShellModel
{
public:
    static constexpr std::size_t kPageCount = 5;

    [[nodiscard]] const EditorShellState& state() const noexcept { return state_; }
    [[nodiscard]] const SessionPageModel& session() const noexcept { return session_; }
    [[nodiscard]] const AnalysisPageModel& analysis() const noexcept { return analysis_; }
    [[nodiscard]] const SynthesisPageModel& synthesis() const noexcept { return synthesis_; }
    [[nodiscard]] const PartialPageModel& partials() const noexcept { return partials_; }
    [[nodiscard]] const MathPageModel& math() const noexcept { return math_; }
    [[nodiscard]] const SpectrumInteractionModel& spectrum() const noexcept { return spectrum_; }

    bool selectPage(std::size_t index) noexcept
    {
        if (index >= kPageCount)
            return false;
        state_.page = static_cast<EditorPage>(index);
        return true;
    }

    bool selectPage(EditorPage page) noexcept
    {
        const auto index = static_cast<std::size_t>(page);
        return selectPage(index);
    }

    void setTheme(ThemeId theme) noexcept { state_.theme = theme; }
    void setHistoryAvailability(bool canUndo, bool canRedo) noexcept
    {
        state_.canUndo = canUndo;
        state_.canRedo = canRedo;
    }
    void setActivity(bool analysisRunning, bool renderRunning) noexcept
    {
        state_.analysisRunning = analysisRunning;
        state_.renderRunning = renderRunning;
    }

    [[nodiscard]] static constexpr std::array<const char*, kPageCount> pageLabels() noexcept
    {
        return { "SESSION", "ANALYSIS", "SYNTHESIS", "PARTIALS", "MATH" };
    }

private:
    EditorShellState state_{};
    SessionPageModel session_{};
    AnalysisPageModel analysis_{};
    SynthesisPageModel synthesis_{};
    PartialPageModel partials_{};
    MathPageModel math_{};
    SpectrumInteractionModel spectrum_{};
};

} // namespace ats::ui
