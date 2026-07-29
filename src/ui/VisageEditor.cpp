#include "VisageEditor.h"
#include "EditorShell.h"
#include "VisageNativeMac.h"
#include "../core/ParameterRegistry.h"

#include <embedded/fonts.h>
#include <visage/app.h>
#include <visage_ui/popup_menu.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <string>

namespace ats::ui
{
namespace
{

class ProbeFrame final : public visage::Frame
{
public:
    explicit ProbeFrame(EditorEventSink sink) : sink_(sink)
    {
        for (std::size_t index = 0; index < analysisValues_.size(); ++index)
            if (const auto* descriptor = analysisDescriptor(index))
                analysisValues_[index] = descriptor->defaultValue;
        for (std::size_t index = 0; index < synthesisValues_.size(); ++index)
            if (const auto* descriptor = synthesisDescriptor(index))
                synthesisValues_[index] = descriptor->defaultValue;

        setAcceptsKeystrokes(true);
        onMouseDown() += [this](const visage::MouseEvent& event) {
            if (event.shouldTriggerPopup())
            {
                visage::PopupMenu popup("Probe menu");
                popup.addOption(1, "Probe option");
                popup.onSelection() += [this](int optionId) {
                    if (sink_.popupSelected)
                        sink_.popupSelected(sink_.context, optionId);
                    if (sink_.popupDismissed)
                        sink_.popupDismissed(sink_.context);
                };
                popup.onCancel() += [this] {
                    if (sink_.popupDismissed)
                        sink_.popupDismissed(sink_.context);
                };
                if (sink_.popupShown)
                    sink_.popupShown(sink_.context);
                // Keep the probe menu in a deterministic logical location so
                // the native harness can select it across window-system scale.
                popup.show(this, { 40, 40 });
                return;
            }
            const auto point = event.relativePosition();
            if (point.y < 48.0f && point.x >= 248.0f && point.x < 316.0f)
            {
                if (shell_.state().canUndo && sink_.historyAction)
                    sink_.historyAction(sink_.context, false);
                emitAction(HistoryAction { false });
                requestKeyboardFocus();
                return;
            }
            if (point.y < 48.0f && point.x >= 316.0f && point.x < 384.0f)
            {
                if (shell_.state().canRedo && sink_.historyAction)
                    sink_.historyAction(sink_.context, true);
                emitAction(HistoryAction { true });
                requestKeyboardFocus();
                return;
            }
            if (point.y < 48.0f && point.x >= width() - 220.0f)
            {
                visage::PopupMenu popup("Theme");
                popup.addOption(1, "Ember");
                popup.addOption(2, "Porcelain");
                popup.onSelection() += [this](int optionId) {
                    if (optionId == 1 || optionId == 2)
                    {
                        shell_.setTheme(optionId == 2 ? ThemeId::porcelain : ThemeId::ember);
                        if (sink_.themeSelected)
                            sink_.themeSelected(sink_.context, optionId - 1);
                        redraw();
                    }
                };
                popup.onCancel() += [this] {
                    if (sink_.popupDismissed)
                        sink_.popupDismissed(sink_.context);
                };
                if (sink_.popupShown)
                    sink_.popupShown(sink_.context);
                popup.show(this, { width() - 208.0f, 8.0f });
                requestKeyboardFocus();
                return;
            }
            if (shell_.state().page == EditorPage::session && point.y >= 212.0f &&
                point.y < 250.0f && point.x >= 18.0f && point.x < 182.0f)
            {
                visage::PopupMenu popup("Spectrum view");
                const auto items = shell_.spectrum().menu(true, false);
                for (const auto& item : items)
                    if (item.enabled)
                        popup.addOption(item.id, std::string(item.label));
                popup.onSelection() += [this, items](int optionId) {
                    if (optionId < 0 || optionId >= static_cast<int>(items.size()) ||
                        !items[static_cast<std::size_t>(optionId)].enabled)
                        return;
                    const auto action = shell_.spectrum().selectMenuItem(optionId, true, false);
                    if (action.mode < 0)
                        return;
                    shell_.spectrum().setMode(items[static_cast<std::size_t>(optionId)].mode);
                    if (sink_.spectrumModeSelected)
                        sink_.spectrumModeSelected(sink_.context, action.source, action.mode);
                    emitAction(action);
                    if (sink_.popupDismissed)
                        sink_.popupDismissed(sink_.context);
                    redraw();
                };
                popup.onCancel() += [this] {
                    if (sink_.popupDismissed)
                        sink_.popupDismissed(sink_.context);
                };
                if (sink_.popupShown)
                    sink_.popupShown(sink_.context);
                popup.show(this, { 18.0f, 244.0f });
                requestKeyboardFocus();
                return;
            }
            if (shell_.state().page == EditorPage::session && isSpectrumPoint(point))
            {
                spectrumDragging_ = true;
                spectrumLastX_ = point.x;
                spectrumLastY_ = point.y;
                shell_.spectrum().setPointerCaptured(true);
                requestKeyboardFocus();
                return;
            }
            if (shell_.state().page == EditorPage::session && point.y >= 112.0f &&
                point.y < 164.0f && point.x >= 18.0f && point.x < 238.0f)
            {
                const NativeFileDialogRequest request {
                    point.x < 128.0f ? NativeFileDialogKind::importWav
                                     : NativeFileDialogKind::importAts,
                    dialogParent_, {}};
                if (sink_.dialogRequested)
                    sink_.dialogRequested(sink_.context, static_cast<int>(request.kind));
                const auto result = detail::runNativeFileDialog(request);
                if (sink_.dialogResolved)
                    sink_.dialogResolved(sink_.context, static_cast<int>(request.kind),
                                         result.path.c_str(), result.accepted);
                requestKeyboardFocus();
                return;
            }
            if (point.y >= 56.0f && point.y < 92.0f && point.x >= 16.0f)
            {
                const auto page = static_cast<std::size_t>((point.x - 16.0f) / 126.0f);
                if (shell_.selectPage(page))
                {
                    if (sink_.pageSelected)
                        sink_.pageSelected(sink_.context, static_cast<int>(page));
                    redraw();
                }
                requestKeyboardFocus();
                return;
            }
            if (shell_.state().page == EditorPage::analysis)
            {
                if (const auto control = analysisControlAt(point))
                {
                    const auto* descriptor = analysisDescriptor(*control);
                    if (descriptor == nullptr)
                        return;

                    if (descriptor->type == ParameterType::boolean ||
                        descriptor->type == ParameterType::choice)
                    {
                        float value = analysisValues_[*control];
                        if (descriptor->type == ParameterType::boolean)
                            value = value >= 0.5f ? 0.0f : 1.0f;
                        else
                            value = std::fmod(value + 1.0f,
                                              static_cast<float>(descriptor->choiceCount));
                        setAnalysisValue(*control, value);
                        emitParameterGesture(*control, descriptor->normalize(value), true, true);
                    }
                    else
                    {
                        parameterDragging_ = true;
                        activeParameterSurface_ = ParameterSurface::analysis;
                        activeAnalysisControl_ = *control;
                        parameterValue_ = parameterAt(point.x, analysisControlBoundsFor(*control));
                        emitParameterGesture(*control, parameterValue_, true, false);
                    }
                    requestKeyboardFocus();
                    redraw();
                    return;
                }
            }
            if (shell_.state().page == EditorPage::synthesis)
            {
                if (const auto control = synthesisControlAt(point))
                {
                    const auto* descriptor = synthesisDescriptor(*control);
                    if (descriptor == nullptr)
                        return;
                    if (descriptor->type == ParameterType::boolean ||
                        descriptor->type == ParameterType::choice)
                    {
                        float value = synthesisValues_[*control];
                        if (descriptor->type == ParameterType::boolean)
                            value = value >= 0.5f ? 0.0f : 1.0f;
                        else
                            value = std::fmod(value + 1.0f,
                                              static_cast<float>(descriptor->choiceCount));
                        setSynthesisValue(*control, value);
                        emitSynthesisGesture(*control, descriptor->normalize(value), true, true);
                    }
                    else
                    {
                        parameterDragging_ = true;
                        activeParameterSurface_ = ParameterSurface::synthesis;
                        activeSynthesisControl_ = *control;
                        parameterValue_ = parameterAt(point.x, synthesisControlBoundsFor(*control));
                        emitSynthesisGesture(*control, parameterValue_, true, false);
                    }
                    requestKeyboardFocus();
                    redraw();
                    return;
                }
            }
            if (shell_.state().page == EditorPage::math)
            {
                if (point.x >= 18.0f && point.x <= 638.0f && point.y >= 180.0f && point.y < 410.0f)
                {
                    mathEditing_ = true;
                    if (shell_.math().draft().empty())
                        shell_.math().setDraft(std::string(MathPageModel::starterText()));
                    requestKeyboardFocus();
                    redraw();
                    return;
                }
                if (point.x >= 506.0f && point.x <= 638.0f && point.y >= 424.0f && point.y < 458.0f)
                {
                    if (const auto action = shell_.math().applyAction())
                    {
                        emitAction(*action);
                        shell_.math().beginApply();
                        redraw();
                    }
                    requestKeyboardFocus();
                    return;
                }
            }
            if (shell_.state().page == EditorPage::partials)
            {
                if (point.x >= 18.0f && point.x <= 638.0f && point.y >= 190.0f && point.y < 224.0f)
                {
                    partialIndex_ = static_cast<int>(std::round(parameterAt(
                        point.x, { 18.0f, 190.0f, 620.0f, 34.0f }) * 255.0f));
                    redraw();
                    requestKeyboardFocus();
                    return;
                }
                if (point.x >= 18.0f && point.x <= 638.0f && point.y >= 232.0f && point.y < 266.0f)
                {
                    partialFrame_ = static_cast<int>(std::round(parameterAt(
                        point.x, { 18.0f, 232.0f, 620.0f, 34.0f }) * 1023.0f));
                    redraw();
                    requestKeyboardFocus();
                    return;
                }
                if (point.x >= 18.0f && point.x <= 300.0f && point.y >= 274.0f && point.y < 308.0f)
                {
                    partialAmplitude_ = parameterAt(point.x, { 18.0f, 274.0f, 282.0f, 34.0f });
                    redraw();
                    requestKeyboardFocus();
                    return;
                }
                if (point.x >= 314.0f && point.x <= 638.0f && point.y >= 274.0f && point.y < 308.0f)
                {
                    partialFrequencyHz_ = 20.0f + parameterAt(
                        point.x, { 314.0f, 274.0f, 324.0f, 34.0f }) * 19980.0f;
                    redraw();
                    requestKeyboardFocus();
                    return;
                }
                if (point.x >= 18.0f && point.x <= 180.0f && point.y >= 326.0f && point.y < 360.0f)
                {
                    emitAction(PartialEditAction { partialIndex_, partialFrame_, true });
                    requestKeyboardFocus();
                    return;
                }
            }
            if (sink_.mouseDown)
                sink_.mouseDown(sink_.context);
            requestKeyboardFocus();
        };
        onMouseDrag() += [this](const visage::MouseEvent& event) {
            if (spectrumDragging_)
            {
                const auto point = event.relativePosition();
                shell_.spectrum().pan((spectrumLastX_ - point.x) / 608.0f,
                                       (spectrumLastY_ - point.y) / 184.0f);
                spectrumLastX_ = point.x;
                spectrumLastY_ = point.y;
                redraw();
                return;
            }
            if (!parameterDragging_)
                return;
            const auto bounds = activeParameterSurface_ == ParameterSurface::analysis
                ? analysisControlBoundsFor(activeAnalysisControl_)
                : synthesisControlBoundsFor(activeSynthesisControl_);
            parameterValue_ = parameterAt(event.relativePosition().x, bounds);
            if (activeParameterSurface_ == ParameterSurface::analysis)
                emitParameterGesture(activeAnalysisControl_, parameterValue_, false, false);
            else
                emitSynthesisGesture(activeSynthesisControl_, parameterValue_, false, false);
            redraw();
        };
        onMouseUp() += [this](const visage::MouseEvent&) {
            if (spectrumDragging_)
            {
                spectrumDragging_ = false;
                shell_.spectrum().setPointerCaptured(false);
                return;
            }
            if (parameterDragging_)
            {
                if (activeParameterSurface_ == ParameterSurface::analysis)
                    emitParameterGesture(activeAnalysisControl_, parameterValue_, false, true);
                else
                    emitSynthesisGesture(activeSynthesisControl_, parameterValue_, false, true);
                parameterDragging_ = false;
                return;
            }
            if (sink_.mouseUp)
                sink_.mouseUp(sink_.context);
        };
        onMouseWheel() += [this](const visage::MouseEvent& event) {
            const auto point = event.relativePosition();
            if (shell_.state().page == EditorPage::analysis && point.y >= 190.0f)
            {
                const auto delta = event.precise_wheel_delta_y != 0.0f
                    ? event.precise_wheel_delta_y : event.wheel_delta_y;
                if (delta != 0.0f)
                {
                    analysisScroll_ = std::clamp(analysisScroll_ - delta * 36.0f,
                                                 0.0f, analysisMaximumScroll());
                    redraw();
                }
                return true;
            }
            if (!isSpectrumPoint(point))
                return false;
            const auto delta = event.precise_wheel_delta_y != 0.0f
                ? event.precise_wheel_delta_y : event.wheel_delta_y;
            if (delta == 0.0f)
                return true;
            const auto factor = delta > 0.0f ? 1.15f : 1.0f / 1.15f;
            shell_.spectrum().zoom(factor, (point.x - 24.0f) / 608.0f,
                                   (point.y - 250.0f) / 184.0f);
            redraw();
            return true;
        };
    }

    void setDialogParent(void* nativeParent) noexcept { dialogParent_ = nativeParent; }

    void setTheme(ThemeId theme)
    {
        shell_.setTheme(theme);
        redraw();
    }

    void setHistoryAvailability(bool canUndo, bool canRedo)
    {
        shell_.setHistoryAvailability(canUndo, canRedo);
        redraw();
    }

    void setActivity(bool analysisRunning, bool renderRunning)
    {
        shell_.setActivity(analysisRunning, renderRunning);
        redraw();
    }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = visageTokens();
        canvas.setColor(tokens.background);
        canvas.fill(0.0f, 0.0f, width(), height());
        canvas.setColor(tokens.panel);
        canvas.rectangle(0.0f, 0.0f, width(), 48.0f);
        canvas.setColor(tokens.surface);
        canvas.rectangle(16.0f, 56.0f, std::min(width() - 32.0f, 640.0f), 36.0f);

        const visage::Font titleFont(17.0f, visage::fonts::Lato_Regular_ttf);
        const visage::Font labelFont(10.0f, visage::fonts::Lato_Regular_ttf);
        canvas.setColor(tokens.accent);
        canvas.text("ATS RESYNTH", titleFont, visage::Font::kLeft, 16.0f, 7.0f, 220.0f, 22.0f);
        canvas.setColor(tokens.secondaryText);
        canvas.text("SPECTRAL MODEL EDITOR", labelFont, visage::Font::kLeft,
                    16.0f, 28.0f, 220.0f, 14.0f);
        canvas.setColor(shell_.state().canUndo ? tokens.text : tokens.secondaryText);
        canvas.text("UNDO", labelFont, visage::Font::kCenter, 248.0f, 14.0f, 68.0f, 18.0f);
        canvas.setColor(shell_.state().canRedo ? tokens.text : tokens.secondaryText);
        canvas.text("REDO", labelFont, visage::Font::kCenter, 316.0f, 14.0f, 68.0f, 18.0f);
        canvas.setColor(shell_.state().analysisRunning ? tokens.accent : tokens.secondaryText);
        canvas.circle(402.0f, 17.0f, 8.0f);
        canvas.setColor(tokens.secondaryText);
        canvas.text("ANL", labelFont, visage::Font::kLeft, 414.0f, 14.0f, 30.0f, 18.0f);
        canvas.setColor(shell_.state().renderRunning ? tokens.accent : tokens.secondaryText);
        canvas.circle(448.0f, 17.0f, 8.0f);
        canvas.setColor(tokens.secondaryText);
        canvas.text("RND", labelFont, visage::Font::kLeft, 460.0f, 14.0f, 30.0f, 18.0f);
        canvas.setColor(tokens.secondaryText);
        canvas.text(shell_.state().theme == ThemeId::porcelain ? "PORCELAIN" : "EMBER",
                    labelFont, visage::Font::kCenter, width() - 220.0f, 14.0f, 200.0f, 18.0f);

        const auto labels = EditorShellModel::pageLabels();
        const auto selected = static_cast<std::size_t>(shell_.state().page);
        const float segmentWidth = 126.0f;
        for (std::size_t i = 0; i < labels.size(); ++i)
        {
            if (i == selected)
            {
                canvas.setColor(tokens.accent);
                canvas.roundedRectangle(18.0f + segmentWidth * static_cast<float>(i),
                                       58.0f, segmentWidth - 4.0f, 32.0f, 5.0f);
            }
            canvas.setColor(i == selected ? tokens.background : tokens.text);
            canvas.text(labels[i], labelFont, visage::Font::kCenter,
                        18.0f + segmentWidth * static_cast<float>(i), 58.0f,
                        segmentWidth - 4.0f, 32.0f);
        }

        if (selected == static_cast<std::size_t>(EditorPage::session))
        {
            const auto& session = shell_.session().state();
            canvas.setColor(tokens.secondaryText);
            canvas.text("SOURCE A", labelFont, visage::Font::kLeft,
                        18.0f, 112.0f, 170.0f, 18.0f);
            canvas.text(session.sources[0].hasModel ? "READY" : "DROP WAV / ATS",
                        labelFont, visage::Font::kLeft, 18.0f, 132.0f, 220.0f, 18.0f);
            canvas.text("SOURCE B", labelFont, visage::Font::kLeft,
                        258.0f, 112.0f, 170.0f, 18.0f);
            canvas.text(session.sources[1].hasModel ? "READY" : "DROP WAV / ATS",
                        labelFont, visage::Font::kLeft, 258.0f, 132.0f, 220.0f, 18.0f);
            canvas.text("PLAY  BOTH  |  SMEAR OFF  |  SPECTRUM  COMBINED  |  A -> B",
                        labelFont, visage::Font::kLeft, 18.0f, 176.0f, 520.0f, 18.0f);
            canvas.setColor(tokens.accent);
            canvas.rectangle(18.0f, 204.0f, 380.0f, 3.0f);
            canvas.setColor(tokens.secondaryText);
            canvas.text("SPECTRUM VIEW", labelFont, visage::Font::kLeft,
                        18.0f, 216.0f, 164.0f, 18.0f);
            canvas.setColor(tokens.surface);
            canvas.roundedRectangle(18.0f, 244.0f, 620.0f, 214.0f, 5.0f);
            const UiRect spectrumBounds { 24.0f, 250.0f, 608.0f, 184.0f };
            spectrumSnapshot_.mode = shell_.spectrum().mode();
            const auto commands = SpectrumRenderer::render(
                spectrumSnapshot_, spectrumBounds,
                { shell_.state().theme, shell_.spectrum().navigation().selectedPartial,
                  0.0f, false });
            for (const auto& command : commands)
            {
                if (command.kind == SpectrumDrawKind::background ||
                    command.kind == SpectrumDrawKind::grid ||
                    command.kind == SpectrumDrawKind::residualBand ||
                    command.kind == SpectrumDrawKind::playhead)
                {
                    canvas.setColor(command.color);
                    canvas.rectangle(command.bounds.x, command.bounds.y,
                                    command.bounds.width, command.bounds.height);
                }
            }
            canvas.setColor(tokens.secondaryText);
            canvas.text("VIEW", labelFont, visage::Font::kCenter,
                        18.0f, 216.0f, 164.0f, 18.0f);
            canvas.text(spectrumModeLabel(), labelFont, visage::Font::kLeft,
                        30.0f, 438.0f, 240.0f, 14.0f);
        }
        else if (selected == static_cast<std::size_t>(EditorPage::analysis))
        {
            canvas.setColor(tokens.secondaryText);
            canvas.text("ANALYSIS PROFILE  |  18 CONTROLS  |  RANGE / TRACKING / THRESHOLDS",
                        labelFont, visage::Font::kLeft, 18.0f, 112.0f, 600.0f, 18.0f);
            canvas.text(shell_.analysis().canCancel() ? "ANALYZING  |  CANCEL ENABLED" : "READY  |  APPLY / ANALYZE CURRENT",
                        labelFont, visage::Font::kLeft, 18.0f, 140.0f, 520.0f, 18.0f);
            canvas.setColor(tokens.accent);
            canvas.rectangle(18.0f, 172.0f, 620.0f, 3.0f);
            drawAnalysisControls(canvas, tokens, labelFont);
        }
        else if (selected == static_cast<std::size_t>(EditorPage::synthesis))
        {
            canvas.setColor(tokens.secondaryText);
            canvas.text("PITCH  |  SHAPE  |  TIME  |  RENDER",
                        labelFont, visage::Font::kLeft, 18.0f, 112.0f, 520.0f, 18.0f);
            canvas.text(shell_.synthesis().shapeControlsDimmed() ? "LINEAR  |  SHAPE AMOUNT DIMMED"
                                                                  : "SHAPE ACTIVE  |  BOTH PLAYBACK",
                        labelFont, visage::Font::kLeft, 18.0f, 140.0f, 520.0f, 18.0f);
            canvas.setColor(tokens.accent);
            canvas.rectangle(18.0f, 172.0f, 620.0f, 3.0f);
            drawSynthesisControls(canvas, tokens, labelFont);
        }
        else if (selected == static_cast<std::size_t>(EditorPage::partials))
        {
            canvas.setColor(tokens.secondaryText);
            canvas.text("CELL EDIT  |  AMPLITUDE / FREQUENCY  |  APPLY",
                        labelFont, visage::Font::kLeft, 18.0f, 112.0f, 600.0f, 18.0f);
            canvas.setColor(tokens.accent);
            canvas.rectangle(18.0f, 172.0f, 620.0f, 3.0f);
            drawPartialControl(canvas, tokens, labelFont, "PARTIAL", static_cast<float>(partialIndex_),
                               255.0f, { 18.0f, 190.0f, 620.0f, 34.0f });
            drawPartialControl(canvas, tokens, labelFont, "FRAME", static_cast<float>(partialFrame_),
                               1023.0f, { 18.0f, 232.0f, 620.0f, 34.0f });
            drawPartialControl(canvas, tokens, labelFont, "AMPLITUDE", partialAmplitude_, 1.0f,
                               { 18.0f, 274.0f, 282.0f, 34.0f });
            drawPartialControl(canvas, tokens, labelFont, "FREQUENCY HZ", partialFrequencyHz_ - 20.0f,
                               19980.0f, { 314.0f, 274.0f, 324.0f, 34.0f });
            canvas.setColor(tokens.surface);
            canvas.roundedRectangle(18.0f, 326.0f, 162.0f, 34.0f, 6.0f);
            canvas.setColor(tokens.accent);
            canvas.text("APPLY TO CELL", labelFont, visage::Font::kCenter,
                        22.0f, 334.0f, 154.0f, 18.0f);
        }
        else if (selected == static_cast<std::size_t>(EditorPage::math))
        {
            canvas.setColor(tokens.secondaryText);
            canvas.text("MATHEXPR  |  MULTILINE SCRIPT  |  APPLY",
                        labelFont, visage::Font::kLeft, 18.0f, 112.0f, 560.0f, 18.0f);
            canvas.text(MathPageModel::helpText().data(), labelFont, visage::Font::kLeft,
                        18.0f, 140.0f, 620.0f, 18.0f);
            canvas.setColor(tokens.accent);
            canvas.rectangle(18.0f, 172.0f, 620.0f, 3.0f);
            canvas.setColor(tokens.surface);
            canvas.roundedRectangle(18.0f, 180.0f, 620.0f, 230.0f, 8.0f);
            canvas.setColor(mathEditing_ ? tokens.accent : tokens.secondaryText);
            const auto& draft = shell_.math().draft();
            canvas.text(draft.empty() ? MathPageModel::starterText().data() : draft.c_str(),
                        labelFont, visage::Font::kLeft, 30.0f, 194.0f, 596.0f, 190.0f);
            canvas.setColor(tokens.surface);
            canvas.roundedRectangle(506.0f, 424.0f, 132.0f, 34.0f, 6.0f);
            canvas.setColor(shell_.math().canApply() ? tokens.accent : tokens.secondaryText);
            canvas.text("APPLY SCRIPT", labelFont, visage::Font::kCenter,
                        510.0f, 432.0f, 124.0f, 18.0f);
            canvas.setColor(tokens.secondaryText);
            canvas.text(shell_.math().status() == MathStatus::running ? "RUNNING" :
                        shell_.math().canApply() ? "READY" : "ENTER A SCRIPT",
                        labelFont, visage::Font::kLeft, 18.0f, 432.0f, 360.0f, 18.0f);
        }
    }

    bool receivesTextInput() override { return true; }

    void textInput(const std::string& text) override
    {
        if (shell_.state().page == EditorPage::math && mathEditing_)
        {
            auto draft = shell_.math().draft();
            if (text == "\b")
            {
                if (!draft.empty())
                    draft.pop_back();
            }
            else
                draft += text;
            shell_.math().setDraft(std::move(draft));
            redraw();
            return;
        }
        if (sink_.textInput)
            sink_.textInput(sink_.context, text.c_str());
    }

    void focusChanged(bool focused, bool) override
    {
        if (sink_.focusChanged)
            sink_.focusChanged(sink_.context, focused);
    }

private:
    [[nodiscard]] static bool isSpectrumPoint(const visage::Point& point) noexcept
    {
        return point.x >= 24.0f && point.x < 632.0f && point.y >= 250.0f && point.y < 434.0f;
    }

    struct AnalysisControlBounds
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
    };

    struct VisageTokens
    {
        std::uint32_t background;
        std::uint32_t panel;
        std::uint32_t surface;
        std::uint32_t text;
        std::uint32_t secondaryText;
        std::uint32_t accent;
    };

    static constexpr std::array<std::size_t, AnalysisPageModel::kControlCount>
        kAnalysisVisualOrder { 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 0, 1, 2 };
    static constexpr std::array<std::string_view, 12> kSynthesisControlIds {
        "freq", "freqShapeMode", "freqShapeAmount", "freqShapePivotHz", "timeStretch",
        "synStart", "synDur", "usePhase", "useAllPartials", "residualOnly", "useIfftSynth",
        "classicPreviewMode"
    };

    enum class ParameterSurface : std::uint8_t { analysis, synthesis };

    [[nodiscard]] static const ParameterDescriptor* analysisDescriptor(std::size_t index) noexcept
    {
        const auto ids = AnalysisPageModel::controlIds();
        return index < ids.size() ? ParameterRegistry::find(ids[index]) : nullptr;
    }

    [[nodiscard]] static const ParameterDescriptor* synthesisDescriptor(std::size_t index) noexcept
    {
        return index < kSynthesisControlIds.size() ? ParameterRegistry::find(kSynthesisControlIds[index])
                                                   : nullptr;
    }

    [[nodiscard]] AnalysisControlBounds analysisControlBounds(std::size_t visualIndex) const noexcept
    {
        if (visualIndex == 0)
            // Preserve the original native-probe hit target around the compact
            // slider while drawing the track inside that row.
            return { 18.0f, 190.0f - analysisScroll_, 420.0f, 38.0f };
        const auto gridIndex = visualIndex - 1;
        constexpr float columnWidth = 300.0f;
        constexpr float columnGap = 14.0f;
        return { 18.0f + static_cast<float>(gridIndex % 2) * (columnWidth + columnGap),
                 238.0f + static_cast<float>(gridIndex / 2) * 42.0f - analysisScroll_,
                 columnWidth, 34.0f };
    }

    [[nodiscard]] AnalysisControlBounds analysisControlBoundsFor(std::size_t control) const noexcept
    {
        for (std::size_t visualIndex = 0; visualIndex < kAnalysisVisualOrder.size(); ++visualIndex)
            if (kAnalysisVisualOrder[visualIndex] == control)
                return analysisControlBounds(visualIndex);
        return {};
    }

    [[nodiscard]] AnalysisControlBounds synthesisControlBounds(std::size_t control) const noexcept
    {
        constexpr float columnWidth = 300.0f;
        constexpr float columnGap = 14.0f;
        switch (control)
        {
            case 0: return { 18.0f, 190.0f, 620.0f, 34.0f };
            case 1: return { 18.0f, 232.0f, 620.0f, 28.0f };
            case 2: return { 18.0f, 274.0f, columnWidth, 34.0f };
            case 3: return { 18.0f + columnWidth + columnGap, 274.0f, columnWidth, 34.0f };
            case 4: return { 18.0f, 316.0f, 620.0f, 34.0f };
            case 5: return { 18.0f, 358.0f, columnWidth, 34.0f };
            case 6: return { 18.0f + columnWidth + columnGap, 358.0f, columnWidth, 34.0f };
            case 7: return { 18.0f, 400.0f, columnWidth, 28.0f };
            case 8: return { 18.0f + columnWidth + columnGap, 400.0f, columnWidth, 28.0f };
            case 9: return { 18.0f, 438.0f, columnWidth, 28.0f };
            case 10: return { 18.0f + columnWidth + columnGap, 438.0f, columnWidth, 28.0f };
            case 11: return { 18.0f, 480.0f, 620.0f, 34.0f };
            default: return {};
        }
    }

    [[nodiscard]] AnalysisControlBounds synthesisControlBoundsFor(std::size_t control) const noexcept
    {
        return synthesisControlBounds(control);
    }

    [[nodiscard]] std::optional<std::size_t> analysisControlAt(const visage::Point& point) const noexcept
    {
        for (std::size_t visualIndex = 0; visualIndex < kAnalysisVisualOrder.size(); ++visualIndex)
        {
            const auto bounds = analysisControlBounds(visualIndex);
            if (point.x >= bounds.x && point.x <= bounds.x + bounds.width &&
                point.y >= bounds.y && point.y <= bounds.y + bounds.height)
                return kAnalysisVisualOrder[visualIndex];
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::size_t> synthesisControlAt(const visage::Point& point) const noexcept
    {
        for (std::size_t control = 0; control < kSynthesisControlIds.size(); ++control)
        {
            if ((control == 2 || control == 3) && shell_.synthesis().shapeControlsDimmed())
                continue;
            const auto bounds = synthesisControlBounds(control);
            if (point.x >= bounds.x && point.x <= bounds.x + bounds.width &&
                point.y >= bounds.y && point.y <= bounds.y + bounds.height)
                return control;
        }
        return std::nullopt;
    }

    [[nodiscard]] static float parameterAt(float x, const AnalysisControlBounds& bounds) noexcept
    {
        return bounds.width <= 0.0f ? 0.0f
                                   : std::clamp((x - bounds.x) / bounds.width, 0.0f, 1.0f);
    }

    [[nodiscard]] float analysisMaximumScroll() const noexcept
    {
        const auto last = analysisControlBounds(kAnalysisVisualOrder.size() - 1);
        return std::max(0.0f, last.y + last.height + analysisScroll_ - height() + 16.0f);
    }

    void setAnalysisValue(std::size_t control, float value)
    {
        const auto* descriptor = analysisDescriptor(control);
        if (descriptor == nullptr || control >= analysisValues_.size())
            return;
        analysisValues_[control] = descriptor->snap(value);
        shell_.analysis().setControl(control, analysisValues_[control]);
    }

    void setSynthesisValue(std::size_t control, float value)
    {
        const auto* descriptor = synthesisDescriptor(control);
        if (descriptor == nullptr || control >= synthesisValues_.size())
            return;
        synthesisValues_[control] = descriptor->snap(value);
        if (control == 1)
            shell_.synthesis().setShapeMode(
                static_cast<FrequencyShapeMode>(static_cast<int>(synthesisValues_[control])));
    }

    void drawAnalysisControls(visage::Canvas& canvas, const VisageTokens& tokens,
                              const visage::Font& labelFont) const
    {
        for (std::size_t visualIndex = 0; visualIndex < kAnalysisVisualOrder.size(); ++visualIndex)
        {
            const auto control = kAnalysisVisualOrder[visualIndex];
            const auto* descriptor = analysisDescriptor(control);
            if (descriptor == nullptr)
                continue;
            const auto bounds = analysisControlBounds(visualIndex);
            if (bounds.y + bounds.height < 180.0f || bounds.y > height())
                continue;

            auto renderedBounds = bounds;
            if (visualIndex == 0)
            {
                renderedBounds.y += 8.0f;
                renderedBounds.height = 18.0f;
            }
            canvas.setColor(tokens.secondaryText);
            canvas.text(descriptor->name.data(), labelFont, visage::Font::kLeft,
                        renderedBounds.x, renderedBounds.y - 14.0f, renderedBounds.width, 13.0f);
            canvas.setColor(tokens.surface);
            canvas.roundedRectangle(renderedBounds.x, renderedBounds.y, renderedBounds.width,
                                    renderedBounds.height, 4.0f);

            const auto value = analysisValues_[control];
            if (descriptor->type == ParameterType::boolean || descriptor->type == ParameterType::choice)
            {
                const auto active = descriptor->type == ParameterType::boolean && value >= 0.5f;
                if (active)
                {
                    canvas.setColor(tokens.accent);
                    canvas.roundedRectangle(renderedBounds.x, renderedBounds.y, renderedBounds.width,
                                            renderedBounds.height, 4.0f);
                }
                canvas.setColor(active ? tokens.background : tokens.text);
                const auto text = descriptor->text(value);
                canvas.text(text.c_str(), labelFont, visage::Font::kCenter,
                            renderedBounds.x + 4.0f, renderedBounds.y + 3.0f,
                            renderedBounds.width - 8.0f, renderedBounds.height - 6.0f);
            }
            else
            {
                const auto normalized = descriptor->normalize(value);
                canvas.setColor(tokens.accent);
                canvas.roundedRectangle(renderedBounds.x, renderedBounds.y,
                                        renderedBounds.width * normalized,
                                        renderedBounds.height, 4.0f);
                canvas.setColor(tokens.background);
                const auto text = descriptor->text(value);
                canvas.text(text.c_str(), labelFont, visage::Font::kCenter,
                            renderedBounds.x + 4.0f, renderedBounds.y + 3.0f,
                            renderedBounds.width - 8.0f, renderedBounds.height - 6.0f);
            }
        }
    }

    void drawSynthesisControls(visage::Canvas& canvas, const VisageTokens& tokens,
                               const visage::Font& labelFont) const
    {
        for (std::size_t control = 0; control < kSynthesisControlIds.size(); ++control)
        {
            const auto* descriptor = synthesisDescriptor(control);
            if (descriptor == nullptr)
                continue;
            const auto bounds = synthesisControlBounds(control);
            const auto dimmed = (control == 2 || control == 3) &&
                shell_.synthesis().shapeControlsDimmed();
            canvas.setColor(dimmed ? tokens.secondaryText : tokens.secondaryText);
            canvas.text(descriptor->name.data(), labelFont, visage::Font::kLeft,
                        bounds.x, bounds.y - 14.0f, bounds.width, 13.0f);
            canvas.setColor(tokens.surface);
            canvas.roundedRectangle(bounds.x, bounds.y, bounds.width, bounds.height, 4.0f);

            const auto value = synthesisValues_[control];
            if (descriptor->type == ParameterType::boolean || descriptor->type == ParameterType::choice)
            {
                const auto active = descriptor->type == ParameterType::boolean && value >= 0.5f;
                if (active)
                {
                    canvas.setColor(tokens.accent);
                    canvas.roundedRectangle(bounds.x, bounds.y, bounds.width, bounds.height, 4.0f);
                }
                canvas.setColor(active ? tokens.background : tokens.text);
                const auto text = descriptor->text(value);
                canvas.text(text.c_str(), labelFont, visage::Font::kCenter,
                            bounds.x + 4.0f, bounds.y + 3.0f, bounds.width - 8.0f,
                            bounds.height - 6.0f);
            }
            else
            {
                const auto normalized = descriptor->normalize(value);
                canvas.setColor(dimmed ? tokens.secondaryText : tokens.accent);
                canvas.roundedRectangle(bounds.x, bounds.y, bounds.width * normalized,
                                        bounds.height, 4.0f);
                canvas.setColor(tokens.background);
                const auto text = descriptor->text(value);
                canvas.text(text.c_str(), labelFont, visage::Font::kCenter,
                            bounds.x + 4.0f, bounds.y + 3.0f, bounds.width - 8.0f,
                            bounds.height - 6.0f);
            }
        }
    }

    static void drawPartialControl(visage::Canvas& canvas, const VisageTokens& tokens,
                                   const visage::Font& labelFont, const char* label, float value,
                                   float maximum, const AnalysisControlBounds& bounds)
    {
        canvas.setColor(tokens.secondaryText);
        canvas.text(label, labelFont, visage::Font::kLeft,
                    bounds.x, bounds.y - 14.0f, bounds.width, 13.0f);
        canvas.setColor(tokens.surface);
        canvas.roundedRectangle(bounds.x, bounds.y, bounds.width, bounds.height, 4.0f);
        canvas.setColor(tokens.accent);
        canvas.roundedRectangle(bounds.x, bounds.y,
                                bounds.width * std::clamp(value / maximum, 0.0f, 1.0f),
                                bounds.height, 4.0f);
        canvas.setColor(tokens.background);
        const auto text = std::to_string(value);
        canvas.text(text.c_str(), labelFont, visage::Font::kCenter,
                    bounds.x + 4.0f, bounds.y + 3.0f, bounds.width - 8.0f, bounds.height - 6.0f);
    }

    [[nodiscard]] const char* spectrumModeLabel() const noexcept
    {
        const auto items = shell_.spectrum().menu(true, false);
        const auto mode = shell_.spectrum().mode();
        for (const auto& item : items)
            if (item.mode == mode)
                return item.label.data();
        return "Combined";
    }

    void emitParameterGesture(std::size_t control, float normalized, bool begin, bool end)
    {
        const auto* descriptor = analysisDescriptor(control);
        if (descriptor == nullptr || control >= analysisValues_.size())
            return;
        setAnalysisValue(control, descriptor->denormalize(normalized));
        const auto parameterValue = analysisValues_[control];
        const auto id = std::string(descriptor->id);
        emitAction(SetParameterAction { id, parameterValue, begin, end });
        if (sink_.parameterGesture)
            sink_.parameterGesture(sink_.context, id.c_str(), parameterValue, begin, end);
    }

    void emitSynthesisGesture(std::size_t control, float normalized, bool begin, bool end)
    {
        const auto* descriptor = synthesisDescriptor(control);
        if (descriptor == nullptr || control >= synthesisValues_.size())
            return;
        setSynthesisValue(control, descriptor->denormalize(normalized));
        const auto parameterValue = synthesisValues_[control];
        const auto id = std::string(descriptor->id);
        emitAction(SetParameterAction { id, parameterValue, begin, end });
        if (sink_.parameterGesture)
            sink_.parameterGesture(sink_.context, id.c_str(), parameterValue, begin, end);
    }

    void emitAction(const EditorAction& action)
    {
        if (sink_.action)
            sink_.action(sink_.context, &action);
    }

    [[nodiscard]] VisageTokens visageTokens() const noexcept
    {
        const auto tokens = ThemeTokens::forTheme(shell_.state().theme);
        return { tokens.background.argb, tokens.panel.argb, tokens.surface.argb,
                 tokens.text.argb, tokens.secondaryText.argb, tokens.accent.argb };
    }

    EditorEventSink sink_{};
    EditorShellModel shell_{};
    SpectralTraceSnapshot spectrumSnapshot_{};
    void* dialogParent_ = nullptr;
    bool parameterDragging_ = false;
    float parameterValue_ = 0.5f;
    ParameterSurface activeParameterSurface_ = ParameterSurface::analysis;
    std::size_t activeAnalysisControl_ = 3;
    std::array<float, AnalysisPageModel::kControlCount> analysisValues_ {};
    std::size_t activeSynthesisControl_ = 0;
    std::array<float, kSynthesisControlIds.size()> synthesisValues_ {};
    float analysisScroll_ = 0.0f;
    bool mathEditing_ = false;
    int partialIndex_ = 0;
    int partialFrame_ = 0;
    float partialAmplitude_ = 0.5f;
    float partialFrequencyHz_ = 440.0f;
    bool spectrumDragging_ = false;
    float spectrumLastX_ = 0.0f;
    float spectrumLastY_ = 0.0f;
};

} // namespace

struct VisageEditor::Impl
{
    explicit Impl(EditorEventSink eventSink) : sink(eventSink)
    {
        app.setWindowDimensions(static_cast<int>(width), static_cast<int>(height));
        app.onCloseRequested() = [] { return false; };
        app.onDraw() = [](visage::Canvas& canvas) {
            canvas.setColor(0xff20242a);
            canvas.fill(0, 0, canvas.width(), canvas.height());
        };
        app.onWindowContentsResized() = [this] {
            // Host resize requests update the logical dimensions before
            // reaching Visage. Do not replace them with native backing
            // dimensions from this callback; that would make the neutral
            // contract DPI-dependent.
            frame.setBounds(0.0f, 0.0f, static_cast<float>(width),
                            static_cast<float>(height));
        };
        app.addChild(&frame);
        frame.setBounds(0.0f, 0.0f, static_cast<float>(width),
                        static_cast<float>(height));
    }

    ~Impl()
    {
        app.close();
    }

    EditorEventSink sink{};
    visage::ApplicationWindow app;
    ProbeFrame frame { sink };
    std::uint32_t width = 1280;
    std::uint32_t height = 940;
    double scale = 1.0;
    bool attached = false;
    bool visible = false;
};

VisageEditor::VisageEditor(EditorEventSink sink) : impl_(std::make_unique<Impl>(sink)) {}

VisageEditor::~VisageEditor() = default;

void VisageEditor::setTheme(ThemeId theme)
{
    impl_->frame.setTheme(theme);
}

void VisageEditor::setHistoryAvailability(bool canUndo, bool canRedo)
{
    impl_->frame.setHistoryAvailability(canUndo, canRedo);
}

void VisageEditor::setActivity(bool analysisRunning, bool renderRunning)
{
    impl_->frame.setActivity(analysisRunning, renderRunning);
}

void VisageEditor::getSize(std::uint32_t& width, std::uint32_t& height) const
{
    width = impl_->width;
    height = impl_->height;
}

bool VisageEditor::setSize(std::uint32_t width, std::uint32_t height)
{
    if (width == 0 || height == 0)
        return false;

    impl_->width = width;
    impl_->height = height;
#if defined(__APPLE__)
    impl_->app.setWindowDimensions(static_cast<int>(width), static_cast<int>(height));
#else
    impl_->app.setNativeWindowDimensions(static_cast<int>(width), static_cast<int>(height));
#endif
    impl_->frame.setBounds(0.0f, 0.0f, static_cast<float>(width),
                           static_cast<float>(height));
    detail::setNativeViewFrame(nativeHandle(), static_cast<float>(width / impl_->scale),
                               static_cast<float>(height / impl_->scale));
    return true;
}

bool VisageEditor::canResize() const { return true; }

void VisageEditor::setScale(double scale)
{
    impl_->scale = std::clamp(scale, 0.5, 4.0);
    impl_->app.setDpiScale(static_cast<float>(impl_->scale));
    detail::setNativeViewFrame(nativeHandle(), static_cast<float>(impl_->width / impl_->scale),
                               static_cast<float>(impl_->height / impl_->scale));
}

bool VisageEditor::setParent(void* nativeParent)
{
    if (nativeParent == nullptr)
        return false;
    impl_->app.show(nativeParent);
    detail::setNativeViewFrame(nativeHandle(), static_cast<float>(impl_->width / impl_->scale),
                               static_cast<float>(impl_->height / impl_->scale));
    impl_->frame.setDialogParent(nativeHandle());
    impl_->attached = true;
    impl_->visible = true;
    detail::setNativeViewHidden(nativeHandle(), false);
    return impl_->app.window() != nullptr;
}

bool VisageEditor::showStandalone()
{
    if (!impl_->app.window())
        impl_->app.show();
    else
        impl_->app.window()->show();
    impl_->attached = impl_->app.window() != nullptr;
    detail::setNativeViewFrame(nativeHandle(), static_cast<float>(impl_->width / impl_->scale),
                               static_cast<float>(impl_->height / impl_->scale));
    impl_->frame.setDialogParent(nativeHandle());
    impl_->visible = impl_->attached && impl_->app.isShowing();
    detail::setNativeViewHidden(nativeHandle(), false);
    return impl_->visible;
}

void VisageEditor::show()
{
    if (impl_->app.window())
    {
        impl_->app.window()->show();
        impl_->visible = true;
        detail::setNativeViewHidden(nativeHandle(), false);
    }
}

void VisageEditor::hide()
{
    if (impl_->app.window())
    {
        impl_->app.window()->hide();
        impl_->visible = false;
        detail::setNativeViewHidden(nativeHandle(), true);
    }
}

bool VisageEditor::isShowing() const
{
    return impl_->attached && impl_->visible && impl_->app.isShowing()
        && !detail::nativeViewIsHidden(nativeHandle());
}

void* VisageEditor::nativeHandle() const
{
    return impl_->app.window() ? impl_->app.window()->nativeHandle() : nullptr;
}

} // namespace ats::ui
