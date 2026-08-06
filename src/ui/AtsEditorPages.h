#pragma once

// Separate visage::Frame subclasses for each editor page.
// Each page is independent: it owns its child widget frames, manages
// the parameter-value cache it displays, and emits EditorActions through
// the supplied Emitter callback.
//
// Page frames intentionally do not depend on HostAdapter or VisageEditor;
// the root frame (ProbeFrame or a future replacement) creates each page,
// sizes it, and routes parameter updates via setControlValue().

#include "EditorHost.h"
#include "EditorPageModels.h"
#include "ParameterBinding.h"
#include "SessionPageModel.h"
#include "ThemeTokens.h"
#include "VisageWidgets.h"
#include "../core/ParameterRegistry.h"

#include <embedded/fonts.h>
#include <visage_ui/popup_menu.h>
#include <visage/app.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ats::ui
{

// -----------------------------------------------------------------------
// Shared helpers
// -----------------------------------------------------------------------

// A minimal ActionSink bridge: stores the latest accepted SetParameterAction
// so page frames can observe what was accepted by the dispatcher.
class PageActionBridge final : public ActionSink
{
public:
    using Emitter = std::function<void(const EditorAction&)>;

    explicit PageActionBridge(Emitter emit) : emit_(std::move(emit)) {}

    void dispatch(EditorAction action) override
    {
        if (emit_) emit_(action);
    }

private:
    Emitter emit_;
};


// -----------------------------------------------------------------------
// AtsAnalysisPage — §10.5 Analysis page, left + right columns
// -----------------------------------------------------------------------
// Renders 18 analysis parameter controls as AtsSlider (continuous/integer)
// or AtsSegmented (choice/bool) child frames.  Profile selector and
// Analyze/Cancel action buttons are drawn inline.
// -----------------------------------------------------------------------

class AtsAnalysisPage final : public visage::Frame
{
public:
    using ThemeGetter = std::function<ThemeTokens()>;
    using Emitter = std::function<void(const EditorAction&)>;

    AtsAnalysisPage(AnalysisPageModel& model, Emitter emit, ThemeGetter theme)
        : model_(model), bridge_(emit), dispatcher_(store_, [this](const SetParameterAction& a) {
              bridge_.dispatch(a);
          })
        , theme_(std::move(theme))
    {
        const auto ids = AnalysisPageModel::controlIds();
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            const auto* descriptor = ParameterRegistry::find(ids[i]);
            if (descriptor)
                store_.write(ids[i], descriptor->defaultValue);

            auto binding = std::make_unique<ParameterBinding>(store_, std::string(ids[i]),
                                                               dispatcher_);
            bindings_.push_back(std::move(binding));

            if (descriptor && (descriptor->type == ParameterType::boolean ||
                               descriptor->type == ParameterType::choice))
            {
                std::vector<std::string> segs;
                if (descriptor->type == ParameterType::boolean)
                    segs = { "OFF", "ON" };
                else
                    for (std::uint16_t c = 0; c < descriptor->choiceCount; ++c)
                        segs.push_back(descriptor->choices
                            ? std::string(descriptor->choices[c]) : std::to_string(c));
                auto widget = std::make_unique<AtsSegmented>(*bindings_.back(), std::move(segs),
                                                              theme_);
                widgets_.push_back(widget.get());
                addChild(std::move(widget));
            }
            else
            {
                auto widget = std::make_unique<AtsSlider>(*bindings_.back(), theme_);
                widgets_.push_back(widget.get());
                addChild(std::move(widget));
            }
        }
    }

    void setControlValue(std::string_view id, float value)
    {
        store_.write(id, value);
        redraw();
    }

    void setProfile(int profile)
    {
        profile_ = profile;
        redraw();
    }

    void setWorkerRunning(bool running)
    {
        workerRunning_ = running;
        redraw();
    }

    void resized() override { layoutWidgets(); }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = theme_();
        const visage::Font labelFont(10.0f, visage::fonts::Lato_Regular_ttf);
        const auto ids = AnalysisPageModel::controlIds();

        // Profile row header
        canvas.setColor(tokens.secondaryText.argb);
        canvas.text("PROFILE", labelFont, visage::Font::kLeft,
                    18.0f, 0.0f, 120.0f, 14.0f);
        canvas.setColor(tokens.surface.argb);
        canvas.roundedRectangle(18.0f, 14.0f, 190.0f, 24.0f, 4.0f);
        const char* profileLabels[] { "Default", "Speech", "Percussive", "Custom" };
        canvas.setColor(tokens.text.argb);
        canvas.text(profileLabels[std::clamp(profile_, 0, 3)], labelFont, visage::Font::kLeft,
                    22.0f, 17.0f, 182.0f, 18.0f);

        // Analyze / Cancel buttons
        canvas.setColor(workerRunning_ ? tokens.secondaryText.argb : tokens.surface.argb);
        canvas.roundedRectangle(220.0f, 14.0f, 90.0f, 24.0f, 4.0f);
        canvas.setColor(tokens.text.argb);
        canvas.text("ANALYZE", labelFont, visage::Font::kCenter, 220.0f, 17.0f, 90.0f, 18.0f);
        canvas.setColor(workerRunning_ ? tokens.surface.argb : tokens.secondaryText.argb);
        canvas.roundedRectangle(316.0f, 14.0f, 90.0f, 24.0f, 4.0f);
        canvas.setColor(tokens.text.argb);
        canvas.text("CANCEL", labelFont, visage::Font::kCenter, 316.0f, 17.0f, 90.0f, 18.0f);

        // Control labels (widget frames draw themselves)
        for (std::size_t i = 0; i < ids.size() && i < widgets_.size(); ++i)
        {
            const auto* descriptor = ParameterRegistry::find(ids[i]);
            if (descriptor == nullptr) continue;
            const auto b = widgets_[i]->bounds();
            canvas.setColor(tokens.secondaryText.argb);
            canvas.text(descriptor->name.data(), labelFont, visage::Font::kLeft,
                        b.x(), b.y() - 14.0f, b.width(), 13.0f);
        }
    }

private:
    struct Bounds { float x, y, w, h; };

    Bounds controlBounds(std::size_t index) const
    {
        const float col0x = 18.0f;
        const float col1x = 18.0f + (width() - 36.0f) * 0.5f + 8.0f;
        const float colW = (width() - 36.0f) * 0.5f - 8.0f;
        const float startY = 56.0f;
        const float rowH = 42.0f;
        const float row = static_cast<float>(index / 2);
        const float col = index % 2 == 0 ? col0x : col1x;
        return { col, startY + row * rowH, colW, 24.0f };
    }

    void layoutWidgets()
    {
        for (std::size_t i = 0; i < widgets_.size(); ++i)
        {
            const auto b = controlBounds(i);
            widgets_[i]->setBounds(b.x, b.y, b.w, b.h);
        }
    }

    AnalysisPageModel& model_;
    ParameterStore store_;
    PageActionBridge bridge_;
    ParameterActionDispatcher dispatcher_;
    ThemeGetter theme_;
    std::vector<std::unique_ptr<ParameterBinding>> bindings_;
    std::vector<visage::Frame*> widgets_;
    int profile_ = 0;
    bool workerRunning_ = false;
};


// -----------------------------------------------------------------------
// AtsSynthesisPage — §10.6 Synthesis page, left column parameters
// -----------------------------------------------------------------------

class AtsSynthesisPage final : public visage::Frame
{
public:
    using ThemeGetter = std::function<ThemeTokens()>;
    using Emitter = std::function<void(const EditorAction&)>;

    static constexpr std::array<std::string_view, 12> kControlIds {
        "freq", "freqShapeMode", "freqShapeAmount", "freqShapePivotHz", "timeStretch",
        "synStart", "synDur", "usePhase", "useAllPartials", "residualOnly",
        "useIfftSynth", "classicPreviewMode"
    };

    AtsSynthesisPage(Emitter emit, ThemeGetter theme)
        : bridge_(emit)
        , dispatcher_(store_, [this](const SetParameterAction& a) { bridge_.dispatch(a); })
        , theme_(std::move(theme))
    {
        for (const auto id : kControlIds)
        {
            const auto* descriptor = ParameterRegistry::find(id);
            if (descriptor)
                store_.write(id, descriptor->defaultValue);

            auto binding = std::make_unique<ParameterBinding>(store_, std::string(id), dispatcher_);
            bindings_.push_back(std::move(binding));

            if (descriptor && (descriptor->type == ParameterType::boolean ||
                               descriptor->type == ParameterType::choice))
            {
                std::vector<std::string> segs;
                if (descriptor->type == ParameterType::boolean)
                    segs = { "OFF", "ON" };
                else
                    for (std::uint16_t c = 0; c < descriptor->choiceCount; ++c)
                        segs.push_back(descriptor->choices
                            ? std::string(descriptor->choices[c]) : std::to_string(c));
                auto widget = std::make_unique<AtsSegmented>(*bindings_.back(), std::move(segs),
                                                              theme_);
                widgets_.push_back(widget.get());
                addChild(std::move(widget));
            }
            else
            {
                auto widget = std::make_unique<AtsSlider>(*bindings_.back(), theme_);
                widgets_.push_back(widget.get());
                addChild(std::move(widget));
            }
        }
    }

    void setControlValue(std::string_view id, float value)
    {
        store_.write(id, value);
        redraw();
    }

    void resized() override { layoutWidgets(); }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = theme_();
        const visage::Font labelFont(10.0f, visage::fonts::Lato_Regular_ttf);
        // Section headings
        canvas.setColor(tokens.secondaryText.argb);
        canvas.text("PITCH", labelFont, visage::Font::kLeft, 18.0f, 0.0f, 80.0f, 14.0f);
        canvas.text("TIME", labelFont, visage::Font::kLeft, 18.0f, 140.0f, 80.0f, 14.0f);
        canvas.text("RENDER", labelFont, visage::Font::kLeft, 18.0f, 238.0f, 80.0f, 14.0f);
        // Control labels
        for (std::size_t i = 0; i < kControlIds.size() && i < widgets_.size(); ++i)
        {
            const auto* descriptor = ParameterRegistry::find(kControlIds[i]);
            if (descriptor == nullptr) continue;
            const auto b = widgets_[i]->bounds();
            canvas.setColor(tokens.secondaryText.argb);
            canvas.text(descriptor->name.data(), labelFont, visage::Font::kLeft,
                        b.x(), b.y() - 14.0f, b.width(), 13.0f);
        }
    }

private:
    void layoutWidgets()
    {
        // Layout two columns: freq/shape on left, time/render on right
        const float leftX = 18.0f;
        const float rightX = width() * 0.5f + 8.0f;
        const float colW = width() * 0.5f - 26.0f;
        const float heights[] { 24.0f, 20.0f, 24.0f, 24.0f, 24.0f, 24.0f, 24.0f,
                                 20.0f, 20.0f, 20.0f, 20.0f, 20.0f };
        float y = 18.0f;
        for (std::size_t i = 0; i < widgets_.size() && i < 12; ++i)
        {
            const float x = (i % 2 == 0) ? leftX : rightX;
            if (i % 2 == 0 && i > 0) y += heights[i - 1] + 18.0f;
            const float h = i < 12 ? heights[i] : 24.0f;
            widgets_[i]->setBounds(x, y, colW, h);
        }
    }

    ParameterStore store_;
    PageActionBridge bridge_;
    ParameterActionDispatcher dispatcher_;
    ThemeGetter theme_;
    std::vector<std::unique_ptr<ParameterBinding>> bindings_;
    std::vector<visage::Frame*> widgets_;
};


// -----------------------------------------------------------------------
// AtsSessionPage — §10.4 Session page right column (morph parameters)
// -----------------------------------------------------------------------
// Handles the 51 session-parameter sliders for the morph/smear/timbre/
// lane/gain controls.  Transport and source-card controls remain in
// ProbeFrame for now (they emit non-SetParameterAction actions).
// -----------------------------------------------------------------------

class AtsSessionPage final : public visage::Frame
{
public:
    using ThemeGetter = std::function<ThemeTokens()>;
    using Emitter = std::function<void(const EditorAction&)>;

    static constexpr std::array<std::string_view, 51> kControlIds {
        "morphAmount", "morphPerformanceMacro", "morphSmoothingMs",
        "morphGridType", "morphCorrespondenceStrategy", "morphQuality",
        "morphUnmatchedSourcePolicy", "morphUnmatchedTargetPolicy", "morphDiscardOptIn",
        "morphFrequencyInterpolationDomain", "morphAmplitudeInterpolationDomain",
        "morphPhaseStrategy", "morphTransientPolicy",
        "smearnovBlurMs", "smearnovSharpenAmount", "smearnovExaggerateDb", "smearnovMix",
        "smearnovMaxDb", "smearnovTargets", "smearnovScope", "smearnovWeightDetFlux",
        "smearnovWeightResFlux", "smearnovWeightHfcFlux", "smearnovWeightBirth",
        "smearnovWeightPhaseDev", "smearnovThresholdLambda", "smearnovThresholdAutoScale",
        "smearnovPeakWindowFrames", "smearnovMinGapFrames", "smearnovPreMarginMs",
        "smearnovMaxAttackFrames", "smearnovUsePhaseDeviation", "smearnovUseRpcaFallback",
        "timbreBrightnessDb", "timbreTiltDbPerOct", "timbreNoiseBalance",
        "morphLaneFrequencyAmount", "morphLaneFrequencyLinked", "morphLaneAmplitudeAmount",
        "morphLaneAmplitudeLinked", "morphLaneResidualAmount", "morphLaneResidualLinked",
        "morphLaneTimeAmount", "morphLaneTimeLinked", "morphLaneTransientAmount",
        "morphLaneTransientLinked", "morphLanePhaseAmount", "morphLanePhaseLinked",
        "partGain", "resGain", "outputTrim"
    };

    AtsSessionPage(Emitter emit, ThemeGetter theme)
        : bridge_(emit)
        , dispatcher_(store_, [this](const SetParameterAction& a) { bridge_.dispatch(a); })
        , theme_(std::move(theme))
    {
        for (const auto id : kControlIds)
        {
            const auto* descriptor = ParameterRegistry::find(id);
            if (descriptor)
                store_.write(id, descriptor->defaultValue);

            auto binding = std::make_unique<ParameterBinding>(store_, std::string(id), dispatcher_);
            bindings_.push_back(std::move(binding));

            if (descriptor && (descriptor->type == ParameterType::boolean ||
                               descriptor->type == ParameterType::choice))
            {
                std::vector<std::string> segs;
                if (descriptor->type == ParameterType::boolean)
                    segs = { "OFF", "ON" };
                else
                    for (std::uint16_t c = 0; c < descriptor->choiceCount; ++c)
                        segs.push_back(descriptor->choices
                            ? std::string(descriptor->choices[c]) : std::to_string(c));
                auto widget = std::make_unique<AtsSegmented>(*bindings_.back(), std::move(segs),
                                                              theme_);
                widgets_.push_back(widget.get());
                addChild(std::move(widget));
            }
            else
            {
                auto widget = std::make_unique<AtsSlider>(*bindings_.back(), theme_);
                widgets_.push_back(widget.get());
                addChild(std::move(widget));
            }
        }
    }

    void setControlValue(std::string_view id, float value)
    {
        store_.write(id, value);
        redraw();
    }

    void resized() override { layoutWidgets(); }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = theme_();
        const visage::Font labelFont(10.0f, visage::fonts::Lato_Regular_ttf);
        // Section labels
        canvas.setColor(tokens.secondaryText.argb);
        canvas.text("MORPH  |  TIMBRE  |  OUTPUT", labelFont, visage::Font::kLeft,
                    18.0f, 0.0f, 360.0f, 14.0f);
        for (std::size_t i = 0; i < kControlIds.size() && i < widgets_.size(); ++i)
        {
            const auto* descriptor = ParameterRegistry::find(kControlIds[i]);
            if (descriptor == nullptr) continue;
            const auto b = widgets_[i]->bounds();
            canvas.setColor(tokens.secondaryText.argb);
            canvas.text(descriptor->name.data(), labelFont, visage::Font::kLeft,
                        b.x(), b.y() - 14.0f, b.width(), 13.0f);
        }
    }

private:
    void layoutWidgets()
    {
        const float col0x = 18.0f;
        const float col1x = width() * 0.5f + 8.0f;
        const float colW = width() * 0.5f - 26.0f;
        const float startY = 20.0f;
        const float rowH = 42.0f;
        for (std::size_t i = 0; i < widgets_.size(); ++i)
        {
            const float row = static_cast<float>(i / 2);
            const float x = (i % 2 == 0) ? col0x : col1x;
            const auto* descriptor = ParameterRegistry::find(kControlIds[i]);
            const float h = (descriptor && (descriptor->type == ParameterType::boolean ||
                             descriptor->type == ParameterType::choice)) ? 20.0f : 24.0f;
            widgets_[i]->setBounds(x, startY + row * rowH, colW, h);
        }
    }

    ParameterStore store_;
    PageActionBridge bridge_;
    ParameterActionDispatcher dispatcher_;
    ThemeGetter theme_;
    std::vector<std::unique_ptr<ParameterBinding>> bindings_;
    std::vector<visage::Frame*> widgets_;
};


// -----------------------------------------------------------------------
// AtsPartialsPage — §10.6 Partials page
// -----------------------------------------------------------------------
// Two horizontal scrubber sliders (partial index 0–255, frame index 0–1023)
// and amplitude/frequency edit fields.
// -----------------------------------------------------------------------

class AtsPartialsPage final : public visage::Frame
{
public:
    using ThemeGetter = std::function<ThemeTokens()>;
    using Emitter = std::function<void(const EditorAction&)>;

    AtsPartialsPage(Emitter emit, ThemeGetter theme)
        : emit_(std::move(emit)), theme_(std::move(theme))
    {
        onMouseDown() += [this](const visage::MouseEvent& event) {
            handleClick(event.relativePosition());
        };
    }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = theme_();
        const visage::Font labelFont(10.0f, visage::fonts::Lato_Regular_ttf);
        canvas.setColor(tokens.secondaryText.argb);
        canvas.text("PARTIAL INDEX", labelFont, visage::Font::kLeft, 18.0f, 176.0f, 200.0f, 14.0f);
        canvas.setColor(tokens.surface.argb);
        canvas.roundedRectangle(18.0f, 190.0f, 620.0f, 34.0f, 4.0f);
        const float partialNorm = partialIndex_ / 255.0f;
        canvas.setColor(tokens.accent.argb);
        canvas.roundedRectangle(18.0f, 190.0f, 620.0f * partialNorm, 34.0f, 4.0f);
        canvas.setColor(tokens.background.argb);
        canvas.text(std::to_string(partialIndex_).c_str(), labelFont, visage::Font::kCenter,
                    18.0f, 198.0f, 620.0f, 18.0f);
        canvas.setColor(tokens.secondaryText.argb);
        canvas.text("FRAME", labelFont, visage::Font::kLeft, 18.0f, 228.0f, 200.0f, 14.0f);
        canvas.setColor(tokens.surface.argb);
        canvas.roundedRectangle(18.0f, 232.0f, 620.0f, 34.0f, 4.0f);
        const float frameNorm = partialFrame_ / 1023.0f;
        canvas.setColor(tokens.accent.argb);
        canvas.roundedRectangle(18.0f, 232.0f, 620.0f * frameNorm, 34.0f, 4.0f);
        canvas.setColor(tokens.background.argb);
        canvas.text(std::to_string(partialFrame_).c_str(), labelFont, visage::Font::kCenter,
                    18.0f, 240.0f, 620.0f, 18.0f);
    }

    [[nodiscard]] int partialIndex() const noexcept { return partialIndex_; }
    [[nodiscard]] int partialFrame() const noexcept { return partialFrame_; }

    void handleClick(visage::Point point)
    {
        if (point.y >= 190.0f && point.y < 224.0f && point.x >= 18.0f && point.x <= 638.0f)
        {
            partialIndex_ = static_cast<int>(std::round(
                std::clamp((point.x - 18.0f) / 620.0f, 0.0f, 1.0f) * 255.0f));
            redraw();
        }
        else if (point.y >= 232.0f && point.y < 266.0f && point.x >= 18.0f && point.x <= 638.0f)
        {
            partialFrame_ = static_cast<int>(std::round(
                std::clamp((point.x - 18.0f) / 620.0f, 0.0f, 1.0f) * 1023.0f));
            redraw();
        }
        else if (point.y >= 274.0f && point.y < 308.0f)
        {
            const auto amplitude = std::clamp((point.x - 18.0f) / 282.0f, 0.0f, 1.0f);
            if (emit_)
                emit_(PartialEditAction { partialIndex_, partialFrame_, true,
                                          static_cast<double>(amplitude), 440.0 });
        }
    }

private:
    Emitter emit_;
    ThemeGetter theme_;
    int partialIndex_ = 0;
    int partialFrame_ = 0;
};


// -----------------------------------------------------------------------
// AtsMathPage — §10.6 Math page, AtsTextField script editor
// -----------------------------------------------------------------------

class AtsMathPage final : public visage::Frame
{
public:
    using ThemeGetter = std::function<ThemeTokens()>;
    using Emitter = std::function<void(const EditorAction&)>;

    AtsMathPage(MathPageModel& model, Emitter emit, ThemeGetter theme)
        : model_(model), theme_(std::move(theme))
    {
        auto field = std::make_unique<AtsTextField>(
            [this, emit](const std::string& text) {
                model_.setDraft(text);
                if (const auto action = model_.applyAction())
                    emit(*action);
            },
            theme_);
        textField_ = field.get();
        addChild(std::move(field));
    }

    void resized() override
    {
        if (textField_)
            textField_->setBounds(18.0f, 180.0f, width() - 36.0f, 230.0f);
    }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = theme_();
        const visage::Font labelFont(10.0f, visage::fonts::Lato_Regular_ttf);
        canvas.setColor(tokens.secondaryText.argb);
        canvas.text("MATH EXPRESSION", labelFont, visage::Font::kLeft,
                    18.0f, 166.0f, 240.0f, 14.0f);
        canvas.setColor(tokens.surface.argb);
        canvas.roundedRectangle(506.0f, 424.0f, 132.0f, 34.0f, 4.0f);
        canvas.setColor(model_.canApply() ? tokens.accent.argb : tokens.secondaryText.argb);
        canvas.text("APPLY", labelFont, visage::Font::kCenter, 506.0f, 432.0f, 132.0f, 18.0f);
    }

    AtsTextField* textField() const noexcept { return textField_; }

private:
    MathPageModel& model_;
    ThemeGetter theme_;
    AtsTextField* textField_ = nullptr;
};

} // namespace ats::ui
