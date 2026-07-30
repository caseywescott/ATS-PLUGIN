#include "VisageEditor.h"
#include "EditorShell.h"
#include "SessionFileDrop.h"
#include "VisageNativeMac.h"
#include "../core/ParameterRegistry.h"

#include <embedded/fonts.h>
#include <visage/app.h>
#include <visage_ui/popup_menu.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace ats::ui
{
namespace
{

class SourceDropTarget final : public visage::Frame
{
public:
    using PathsCallback = std::function<void(const std::vector<std::string>&)>;
    using StateCallback = std::function<void(bool)>;

    SourceDropTarget(PathsCallback dropped, StateCallback stateChanged)
        : dropped_(std::move(dropped)), stateChanged_(std::move(stateChanged))
    {
        setDrawing(false);
    }

    bool receivesDragDropFiles() override { return true; }
    std::string dragDropFileExtensionRegex() override
    {
        // WindowEventHandler applies this expression to the extension before
        // dispatching a native drop, which lets the OS reject unsupported
        // files and multi-file drops before the action boundary.
        return "\\.(wav|wave|aif|aiff|ats|WAV|WAVE|AIF|AIFF|ATS)";
    }

    void dragFilesEnter(const std::vector<std::string>&) override
    {
        if (stateChanged_)
            stateChanged_(true);
    }

    void dragFilesExit() override
    {
        if (stateChanged_)
            stateChanged_(false);
    }

    void dropFiles(const std::vector<std::string>& paths) override
    {
        if (stateChanged_)
            stateChanged_(false);
        if (dropped_)
            dropped_(paths);
    }

private:
    PathsCallback dropped_;
    StateCallback stateChanged_;
};

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
        for (std::size_t index = 0; index < sessionValues_.size(); ++index)
            if (const auto* descriptor = sessionDescriptor(index))
                sessionValues_[index] = descriptor->defaultValue;

        setAcceptsKeystrokes(true);
        setReceiveChildMouseEvents(true);
        for (int slot = 0; slot < 2; ++slot)
        {
            auto target = std::make_unique<SourceDropTarget>(
                [this, slot](const std::vector<std::string>& paths) { acceptSourceDrop(slot, paths); },
                [this, slot](bool active) {
                    sourceDropActive_[static_cast<std::size_t>(slot)] = active;
                    redraw();
                });
            target->setBounds(slot == 0 ? 18.0f : 330.0f, 112.0f, 300.0f, 68.0f);
            sourceDropTargets_[static_cast<std::size_t>(slot)] = target.get();
            addChild(std::move(target));
        }
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
            if (point.y < 48.0f && point.x >= 120.0f && point.x < 236.0f)
            {
                visage::PopupMenu popup("Presets");
                for (int index = 0; index < 12; ++index)
                    popup.addOption(index + 1, "Factory " + std::to_string(index + 1));
                popup.addOption(13, "Save preset...");
                popup.addOption(14, "Load preset...");
                popup.onSelection() += [this](int optionId) {
                    if (optionId >= 1 && optionId <= 12)
                        emitAction(ApplyFactoryPresetAction { optionId - 1 });
                    else if (optionId == 13 || optionId == 14)
                    {
                        const auto kind = optionId == 13 ? NativeFileDialogKind::savePreset
                                                         : NativeFileDialogKind::loadPreset;
                        const NativeFileDialogRequest request { kind, dialogParent_, "ATS Resynth" };
                        if (sink_.dialogRequested)
                            sink_.dialogRequested(sink_.context, static_cast<int>(kind));
                        const auto result = detail::runNativeFileDialog(request);
                        if (sink_.dialogResolved)
                            sink_.dialogResolved(sink_.context, static_cast<int>(kind),
                                                 result.path.c_str(), result.accepted);
                        if (result.accepted)
                        {
                            if (kind == NativeFileDialogKind::savePreset)
                                emitAction(SavePresetAction { ats::Path { result.path } });
                            else
                                emitAction(LoadPresetAction { ats::Path { result.path } });
                        }
                    }
                    if (sink_.popupDismissed)
                        sink_.popupDismissed(sink_.context);
                    requestKeyboardFocus();
                };
                popup.onCancel() += [this] {
                    if (sink_.popupDismissed)
                        sink_.popupDismissed(sink_.context);
                };
                if (sink_.popupShown)
                    sink_.popupShown(sink_.context);
                popup.show(this, { 120.0f, 40.0f });
                requestKeyboardFocus();
                return;
            }
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
                spectrumMoved_ = false;
                spectrumLastX_ = point.x;
                spectrumLastY_ = point.y;
                shell_.spectrum().setPointerCaptured(true);
                requestKeyboardFocus();
                return;
            }
            if (shell_.state().page == EditorPage::session && point.y >= 112.0f &&
                point.y < 180.0f && ((point.x >= 18.0f && point.x < 318.0f) ||
                                     (point.x >= 330.0f && point.x < 630.0f)))
            {
                const auto slot = point.x >= 330.0f ? 1 : 0;
                const auto localX = point.x - (slot == 0 ? 18.0f : 330.0f);
                if (point.y < 148.0f)
                {
                    const auto source = slot == 0 ? SessionSource::sourceA : SessionSource::sourceB;
                    const auto layer = shell_.session().state().transport.layer;
                    shell_.session().setTransport(source, layer);
                    if (shell_.session().state().transport.playing)
                        emitAction(shell_.session().previewAction(true));
                    redraw();
                    requestKeyboardFocus();
                    return;
                }
                if (point.y >= 148.0f && localX < 92.0f)
                {
                    emitAction(AnalyzeMorphSourceAction { slot });
                    requestKeyboardFocus();
                    return;
                }
                const auto kind = localX < 190.0f ? NativeFileDialogKind::importWav
                                                   : NativeFileDialogKind::importAts;
                const NativeFileDialogRequest request { kind, dialogParent_, {}};
                if (sink_.dialogRequested)
                    sink_.dialogRequested(sink_.context, static_cast<int>(request.kind));
                const auto result = detail::runNativeFileDialog(request);
                if (sink_.dialogResolved)
                    sink_.dialogResolved(sink_.context, static_cast<int>(request.kind),
                                         result.path.c_str(), result.accepted);
                if (result.accepted)
                {
                    if (kind == NativeFileDialogKind::importWav)
                        emitAction(ImportWavAction { ats::Path { result.path }, slot });
                    else
                        emitAction(ImportAtsAction { ats::Path { result.path }, slot });
                }
                requestKeyboardFocus();
                return;
            }
            if (shell_.state().page == EditorPage::session && point.y >= 190.0f &&
                point.y < 210.0f && point.x >= 18.0f && point.x < 146.0f)
            {
                const auto playing = !shell_.session().state().transport.playing;
                shell_.session().setPlaying(playing);
                emitAction(shell_.session().previewAction(playing));
                redraw();
                requestKeyboardFocus();
                return;
            }
            if (shell_.state().page == EditorPage::session && point.y >= 190.0f &&
                point.y < 210.0f && point.x >= 160.0f && point.x < 398.0f)
            {
                const auto normalized = std::clamp((point.x - 160.0f) / 238.0f, 0.0f, 1.0f);
                shell_.session().setPlayhead(normalized);
                emitAction(shell_.session().seekAction());
                redraw();
                requestKeyboardFocus();
                return;
            }
            if (shell_.state().page == EditorPage::session)
            {
                if (const auto control = sessionControlAt(point))
                {
                    const auto* descriptor = sessionDescriptor(*control);
                    if (descriptor == nullptr)
                        return;
                    const auto bounds = sessionControlBounds(*control);
                    if (descriptor->type == ParameterType::boolean ||
                        descriptor->type == ParameterType::choice)
                    {
                        float value = sessionValues_[*control];
                        if (descriptor->type == ParameterType::boolean)
                            value = value >= 0.5f ? 0.0f : 1.0f;
                        else
                            value = std::fmod(value + 1.0f,
                                              static_cast<float>(descriptor->choiceCount));
                        setSessionValue(*control, value);
                        emitSessionGesture(*control, descriptor->normalize(value), true, true);
                    }
                    else
                    {
                        parameterDragging_ = true;
                        activeParameterSurface_ = ParameterSurface::session;
                        activeSessionControl_ = *control;
                        parameterValue_ = parameterAt(point.x, bounds);
                        emitSessionGesture(*control, parameterValue_, true, false);
                    }
                    requestKeyboardFocus();
                    redraw();
                    return;
                }
                const auto transportY = sessionContentY(static_cast<float>((kSessionControlIds.size() + 1) / 2) * 42.0f + 16.0f);
                const auto variationY = transportY + 40.0f;
                const auto timelineY = variationY + 40.0f;
                const auto timbreCurveY = timelineY + 82.0f;
                if (point.y >= transportY && point.y < transportY + 28.0f &&
                    point.x >= 18.0f && point.x < 426.0f)
                {
                    const auto layer = static_cast<SessionLayer>(std::clamp(
                        static_cast<int>((point.x - 18.0f) / 102.0f), 0, 3));
                    const auto source = shell_.session().state().transport.source;
                    shell_.session().setTransport(source, layer);
                    if (shell_.session().state().transport.playing)
                        emitAction(shell_.session().previewAction(true));
                    redraw();
                    requestKeyboardFocus();
                    return;
                }
                if (point.y >= transportY && point.y < transportY + 28.0f &&
                    point.x >= 450.0f && point.x < 638.0f)
                {
                    shell_.session().setSmearEnabled(!shell_.session().state().transport.smearEnabled);
                    emitAction(shell_.session().smearAction());
                    redraw();
                    requestKeyboardFocus();
                    return;
                }
                if (point.y >= variationY && point.y < variationY + 28.0f &&
                    point.x >= 18.0f && point.x < 318.0f)
                {
                    const auto variation = static_cast<std::size_t>((point.x - 18.0f) / 100.0f);
                    const auto stored = shell_.session().state().morph.variationStored[variation];
                    shell_.session().setVariationStored(variation, !stored);
                    emitAction(shell_.session().variationAction(variation, !stored));
                    redraw();
                    requestKeyboardFocus();
                    return;
                }
                if (point.y >= timelineY && point.y < timelineY + 64.0f &&
                    point.x >= 18.0f && point.x < 426.0f)
                {
                    const auto normalized = std::clamp((point.x - 18.0f) / 408.0f, 0.0f, 1.0f);
                    shell_.session().setTimeline({ 0.0f, normalized, 1.0f });
                    emitAction(shell_.session().timelineAction());
                    redraw();
                    requestKeyboardFocus();
                    return;
                }
                if (point.y >= timelineY && point.y < timelineY + 32.0f &&
                    point.x >= 450.0f && point.x < 538.0f)
                {
                    emitAction(DetectSmearnovAction {});
                    requestKeyboardFocus();
                    return;
                }
                if (point.y >= timelineY && point.y < timelineY + 32.0f &&
                    point.x >= 550.0f && point.x < 638.0f)
                {
                    emitAction(ApplySmearnovAction {});
                    requestKeyboardFocus();
                    return;
                }
                if (point.y >= timbreCurveY && point.y < timbreCurveY + 64.0f &&
                    point.x >= 18.0f && point.x < 426.0f)
                {
                    const auto middle = std::clamp(1.0f - (point.y - timbreCurveY) / 64.0f,
                                                   0.0f, 1.0f);
                    const auto& timbre = shell_.session().state().timbre;
                    shell_.session().setTimbre(timbre.brightness, timbre.tilt, timbre.tonalNoise,
                                               { 0.0f, middle, 1.0f });
                    emitAction(SetTimbreMotionPlanAction { { 0.0f, middle, 1.0f } });
                    redraw();
                    requestKeyboardFocus();
                    return;
                }
            }
            if (shell_.state().page == EditorPage::analysis && point.y >= 112.0f &&
                point.y < 140.0f && point.x >= 450.0f && point.x < 638.0f)
            {
                visage::PopupMenu popup("Analysis profile");
                popup.addOption(0, "Default");
                popup.addOption(1, "Speech");
                popup.addOption(2, "Percussive");
                popup.addOption(3, "Custom");
                popup.onSelection() += [this](int optionId) {
                    if (optionId >= 0 && optionId <= 3)
                    {
                        analysisProfile_ = optionId;
                        shell_.analysis().setProfile(static_cast<AnalysisProfile>(optionId));
                        emitAction(shell_.analysis().profileAction());
                        redraw();
                    }
                    if (sink_.popupDismissed)
                        sink_.popupDismissed(sink_.context);
                };
                popup.onCancel() += [this] {
                    if (sink_.popupDismissed)
                        sink_.popupDismissed(sink_.context);
                };
                if (sink_.popupShown)
                    sink_.popupShown(sink_.context);
                popup.show(this, { 450.0f, 140.0f });
                requestKeyboardFocus();
                return;
            }
            if (shell_.state().page == EditorPage::analysis && point.y >= 148.0f &&
                point.y < 176.0f && point.x >= 450.0f && point.x < 638.0f)
            {
                if (point.x < 540.0f)
                    emitAction(AnalyzeCurrentAction {});
                else if (shell_.analysis().canCancel())
                    emitAction(CancelJobsAction {});
                requestKeyboardFocus();
                return;
            }
            if (point.y >= 56.0f && point.y < 92.0f && point.x >= 16.0f)
            {
                const auto page = static_cast<std::size_t>((point.x - 16.0f) / 126.0f);
                if (shell_.selectPage(page))
                {
                    updateSourceDropTargets();
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
                spectrumMoved_ = spectrumMoved_ || std::abs(spectrumLastX_ - point.x) > 1.0f ||
                    std::abs(spectrumLastY_ - point.y) > 1.0f;
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
                : activeParameterSurface_ == ParameterSurface::synthesis
                    ? synthesisControlBoundsFor(activeSynthesisControl_)
                    : sessionControlBounds(activeSessionControl_);
            parameterValue_ = parameterAt(event.relativePosition().x, bounds);
            if (activeParameterSurface_ == ParameterSurface::analysis)
                emitParameterGesture(activeAnalysisControl_, parameterValue_, false, false);
            else if (activeParameterSurface_ == ParameterSurface::synthesis)
                emitSynthesisGesture(activeSynthesisControl_, parameterValue_, false, false);
            else
                emitSessionGesture(activeSessionControl_, parameterValue_, false, false);
            redraw();
        };
        onMouseUp() += [this](const visage::MouseEvent& event) {
            if (spectrumDragging_)
            {
                spectrumDragging_ = false;
                shell_.spectrum().setPointerCaptured(false);
                if (!spectrumMoved_)
                {
                    const auto point = event.relativePosition();
                    const auto frame = spectrumFrameAt(point);
                    if (shell_.spectrum().mode() == SpectralTraceMode::residual)
                    {
                        const auto band = spectrumBandAt(point);
                        shell_.spectrum().selectBand(band);
                        emitAction(shell_.spectrum().applyResidualEdit(
                            band, frame, spectrumValueAt(point)));
                    }
                    else
                    {
                        const auto partial = spectrumPartialAt(point);
                        shell_.spectrum().selectPartial(partial);
                        emitAction(shell_.spectrum().applyPartialEdit(partial, frame, false));
                    }
                    redraw();
                }
                return;
            }
            if (parameterDragging_)
            {
                if (activeParameterSurface_ == ParameterSurface::analysis)
                    emitParameterGesture(activeAnalysisControl_, parameterValue_, false, true);
                else if (activeParameterSurface_ == ParameterSurface::synthesis)
                    emitSynthesisGesture(activeSynthesisControl_, parameterValue_, false, true);
                else
                    emitSessionGesture(activeSessionControl_, parameterValue_, false, true);
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
            if (shell_.state().page == EditorPage::session && point.y >= 474.0f)
            {
                const auto delta = event.precise_wheel_delta_y != 0.0f
                    ? event.precise_wheel_delta_y : event.wheel_delta_y;
                if (delta != 0.0f)
                {
                    sessionScroll_ = std::clamp(sessionScroll_ - delta * 36.0f,
                                                0.0f, sessionMaximumScroll());
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
        shell_.analysis().setWorkerRunning(analysisRunning);
        redraw();
    }

    void setTelemetry(TelemetrySnapshot snapshot)
    {
        telemetry_ = std::move(snapshot);
        shell_.setActivity(telemetry_.analysisRunning, telemetry_.renderRunning);
        shell_.analysis().setWorkerRunning(telemetry_.analysisRunning);
        shell_.analysis().setHasSource(telemetry_.hasSourceA || telemetry_.hasSourceB ||
                                       telemetry_.hasCurrentModel);
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
        canvas.setColor(tokens.text);
        canvas.text("PRESETS", labelFont, visage::Font::kCenter, 120.0f, 14.0f, 116.0f, 18.0f);
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
            drawSourceCard(canvas, tokens, labelFont, session.sources[0], "A", 18.0f,
                           sourceDropActive_[0]);
            drawSourceCard(canvas, tokens, labelFont, session.sources[1], "B", 330.0f,
                           sourceDropActive_[1]);
            canvas.setColor(tokens.surface);
            canvas.roundedRectangle(18.0f, 190.0f, 128.0f, 20.0f, 5.0f);
            canvas.setColor(session.transport.playing ? tokens.accent : tokens.text);
            canvas.text(session.transport.playing ? "STOP PREVIEW" : "PLAY PREVIEW",
                        labelFont, visage::Font::kCenter, 22.0f, 193.0f, 120.0f, 14.0f);
            canvas.setColor(tokens.secondaryText);
            const auto sourceLabel = session.transport.source == SessionSource::sourceA ? "A"
                : session.transport.source == SessionSource::sourceB ? "B" : "CURRENT";
            const auto layerLabel = session.transport.layer == SessionLayer::both ? "BOTH"
                : session.transport.layer == SessionLayer::partials ? "PARTIALS"
                : session.transport.layer == SessionLayer::residual ? "RESIDUAL" : "SMEAR";
            const auto transportLabel = std::string(sourceLabel) + "  |  " + layerLabel +
                (session.transport.smearEnabled ? "  |  SMEAR ON" : "  |  SMEAR OFF");
            canvas.text(transportLabel.c_str(), labelFont, visage::Font::kLeft,
                        160.0f, 193.0f, 360.0f, 14.0f);
            canvas.setColor(tokens.accent);
            canvas.rectangle(18.0f, 204.0f, 380.0f, 3.0f);
            canvas.rectangle(160.0f + 238.0f * session.transport.normalizedPlayhead,
                             201.0f, 3.0f, 9.0f);
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
            canvas.setColor(tokens.secondaryText);
            canvas.text("MORPH  |  TIMBRE  |  OUTPUT", labelFont, visage::Font::kLeft,
                        18.0f, 474.0f, 360.0f, 14.0f);
            drawSessionControls(canvas, tokens, labelFont);
            const auto& transport = session.transport;
            const auto transportY = sessionContentY(static_cast<float>((kSessionControlIds.size() + 1) / 2) * 42.0f + 16.0f);
            const auto variationY = transportY + 40.0f;
            const auto timelineY = variationY + 40.0f;
            const auto timbreCurveY = timelineY + 82.0f;
            constexpr std::array<const char*, 4> layerLabels { "BOTH", "PART", "RES", "SMEAR" };
            if (sessionContentVisible(transportY, 28.0f))
            {
                for (std::size_t index = 0; index < layerLabels.size(); ++index)
                {
                    const auto x = 18.0f + static_cast<float>(index) * 102.0f;
                    canvas.setColor(static_cast<int>(transport.layer) == static_cast<int>(index)
                        ? tokens.accent : tokens.surface);
                    canvas.roundedRectangle(x, transportY, 96.0f, 28.0f, 5.0f);
                    canvas.setColor(static_cast<int>(transport.layer) == static_cast<int>(index)
                        ? tokens.background : tokens.text);
                    canvas.text(layerLabels[index], labelFont, visage::Font::kCenter,
                                x, transportY + 6.0f, 96.0f, 16.0f);
                }
                canvas.setColor(transport.smearEnabled ? tokens.accent : tokens.surface);
                canvas.roundedRectangle(450.0f, transportY, 188.0f, 28.0f, 5.0f);
                canvas.setColor(transport.smearEnabled ? tokens.background : tokens.text);
                canvas.text(transport.smearEnabled ? "SMEAR FX ON" : "SMEAR FX OFF", labelFont,
                            visage::Font::kCenter, 454.0f, transportY + 6.0f, 180.0f, 16.0f);
            }
            if (sessionContentVisible(variationY, 28.0f))
            {
                for (std::size_t index = 0; index < session.morph.variationStored.size(); ++index)
                {
                    const auto x = 18.0f + static_cast<float>(index) * 100.0f;
                    canvas.setColor(session.morph.variationStored[index] ? tokens.accent : tokens.surface);
                    canvas.roundedRectangle(x, variationY, 92.0f, 28.0f, 5.0f);
                    canvas.setColor(session.morph.variationStored[index] ? tokens.background : tokens.text);
                    const auto label = std::string("VAR ") + std::to_string(index + 1);
                    canvas.text(label.c_str(), labelFont, visage::Font::kCenter,
                                x, variationY + 6.0f, 92.0f, 16.0f);
                }
            }
            if (sessionContentVisible(timelineY, 64.0f))
            {
                canvas.setColor(tokens.surface);
                canvas.roundedRectangle(18.0f, timelineY, 408.0f, 64.0f, 8.0f);
                canvas.setColor(tokens.accent);
                const auto& timeline = session.morph.timeline;
                if (timeline.size() >= 3)
                    canvas.rectangle(18.0f + 408.0f * timeline[1], timelineY + 8.0f, 3.0f, 48.0f);
                canvas.setColor(tokens.secondaryText);
                canvas.text("MORPH TIMELINE", labelFont, visage::Font::kLeft,
                            30.0f, timelineY + 10.0f, 180.0f, 14.0f);
                canvas.setColor(tokens.surface);
                canvas.roundedRectangle(450.0f, timelineY, 88.0f, 32.0f, 5.0f);
                canvas.roundedRectangle(550.0f, timelineY, 88.0f, 32.0f, 5.0f);
                canvas.setColor(tokens.accent);
                canvas.text("DETECT", labelFont, visage::Font::kCenter,
                            454.0f, timelineY + 8.0f, 80.0f, 16.0f);
                canvas.text("APPLY", labelFont, visage::Font::kCenter,
                            554.0f, timelineY + 8.0f, 80.0f, 16.0f);
            }
            if (sessionContentVisible(timbreCurveY, 64.0f))
            {
                canvas.setColor(tokens.surface);
                canvas.roundedRectangle(18.0f, timbreCurveY, 408.0f, 64.0f, 8.0f);
                canvas.setColor(tokens.accent);
                const auto& curve = session.timbre.curve;
                const auto middle = curve.size() >= 2 ? curve[1] : 0.5f;
                canvas.rectangle(18.0f + 204.0f, timbreCurveY + 8.0f,
                                 3.0f, 48.0f * (1.0f - middle));
                canvas.setColor(tokens.secondaryText);
                canvas.text("TIMBRE MOTION CURVE", labelFont, visage::Font::kLeft,
                            30.0f, timbreCurveY + 10.0f, 240.0f, 14.0f);
                canvas.text("CLICK TO SET MIDPOINT", labelFont, visage::Font::kLeft,
                            30.0f, timbreCurveY + 34.0f, 240.0f, 14.0f);
            }
        }
        else if (selected == static_cast<std::size_t>(EditorPage::analysis))
        {
            canvas.setColor(tokens.secondaryText);
            canvas.text("ANALYSIS PROFILE  |  18 CONTROLS  |  RANGE / TRACKING / THRESHOLDS",
                        labelFont, visage::Font::kLeft, 18.0f, 112.0f, 600.0f, 18.0f);
            const auto analysisStatus = telemetry_.status.empty()
                ? (shell_.analysis().canCancel() ? "ANALYZING  |  CANCEL ENABLED" : "READY  |  APPLY / ANALYZE CURRENT")
                : telemetry_.status;
            canvas.text(analysisStatus.c_str(), labelFont, visage::Font::kLeft,
                        18.0f, 140.0f, 520.0f, 18.0f);
            if (telemetry_.analysisRunning)
            {
                canvas.setColor(tokens.accent);
                canvas.rectangle(18.0f, 174.0f, 408.0f * std::clamp(telemetry_.progress, 0.0f, 1.0f), 3.0f);
            }
            canvas.setColor(tokens.surface);
            canvas.roundedRectangle(450.0f, 112.0f, 188.0f, 28.0f, 5.0f);
            canvas.roundedRectangle(450.0f, 148.0f, 88.0f, 28.0f, 5.0f);
            canvas.roundedRectangle(550.0f, 148.0f, 88.0f, 28.0f, 5.0f);
            constexpr std::array<const char*, 4> profileLabels {
                "DEFAULT", "SPEECH", "PERCUSSIVE", "CUSTOM"
            };
            canvas.setColor(tokens.text);
            canvas.text(profileLabels[static_cast<std::size_t>(analysisProfile_)], labelFont,
                        visage::Font::kCenter, 454.0f, 118.0f, 180.0f, 16.0f);
            canvas.setColor(tokens.accent);
            canvas.text("ANALYZE", labelFont, visage::Font::kCenter, 454.0f, 154.0f, 80.0f, 16.0f);
            canvas.setColor(shell_.analysis().canCancel() ? tokens.accent : tokens.secondaryText);
            canvas.text("CANCEL", labelFont, visage::Font::kCenter, 554.0f, 154.0f, 80.0f, 16.0f);
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

    [[nodiscard]] static int spectrumFrameAt(const visage::Point& point) noexcept
    {
        return std::clamp(static_cast<int>((point.x - 24.0f) / 608.0f * 255.0f), 0, 255);
    }

    [[nodiscard]] static int spectrumPartialAt(const visage::Point& point) noexcept
    {
        return std::clamp(static_cast<int>((1.0f - (point.y - 250.0f) / 184.0f) * 31.0f),
                          0, 31);
    }

    [[nodiscard]] static int spectrumBandAt(const visage::Point& point) noexcept
    {
        return std::clamp(static_cast<int>((1.0f - (point.y - 250.0f) / 184.0f) * 7.0f),
                          0, 7);
    }

    [[nodiscard]] static float spectrumValueAt(const visage::Point& point) noexcept
    {
        return std::clamp(1.0f - (point.y - 250.0f) / 184.0f, 0.0f, 1.0f);
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
    static constexpr std::array<std::string_view, 51> kSessionControlIds {
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

    enum class ParameterSurface : std::uint8_t { analysis, synthesis, session };

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

    [[nodiscard]] static const ParameterDescriptor* sessionDescriptor(std::size_t index) noexcept
    {
        return index < kSessionControlIds.size() ? ParameterRegistry::find(kSessionControlIds[index])
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

    [[nodiscard]] AnalysisControlBounds sessionControlBounds(std::size_t control) const noexcept
    {
        constexpr float width = 300.0f;
        constexpr float gap = 14.0f;
        const auto row = control / 2;
        const auto column = control % 2;
        return { 18.0f + static_cast<float>(column) * (width + gap),
                 490.0f + static_cast<float>(row) * 42.0f - sessionScroll_, width, 34.0f };
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

    [[nodiscard]] std::optional<std::size_t> sessionControlAt(const visage::Point& point) const noexcept
    {
        for (std::size_t control = 0; control < kSessionControlIds.size(); ++control)
        {
            const auto bounds = sessionControlBounds(control);
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

    [[nodiscard]] float sessionMaximumScroll() const noexcept
    {
        // Parameters occupy paired rows, followed by the Session-only
        // transport, variation, timeline, onset, and timbre-plan controls.
        const auto parameterRows = (kSessionControlIds.size() + 1) / 2;
        const auto contentBottom = 490.0f + static_cast<float>(parameterRows) * 42.0f + 268.0f;
        return std::max(0.0f, contentBottom - height() + 16.0f);
    }

    [[nodiscard]] bool sessionContentVisible(float y, float contentHeight) const noexcept
    {
        return y + contentHeight >= 474.0f && y <= height();
    }

    [[nodiscard]] float sessionContentY(float offset) const noexcept
    { return 490.0f + offset - sessionScroll_; }

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

    void setSessionValue(std::size_t control, float value)
    {
        const auto* descriptor = sessionDescriptor(control);
        if (descriptor != nullptr && control < sessionValues_.size())
        {
            sessionValues_[control] = descriptor->snap(value);
            if (descriptor->id == "timbreBrightnessDb" || descriptor->id == "timbreTiltDbPerOct" ||
                descriptor->id == "timbreNoiseBalance")
            {
                const auto* brightness = ParameterRegistry::find("timbreBrightnessDb");
                const auto* tilt = ParameterRegistry::find("timbreTiltDbPerOct");
                const auto* noise = ParameterRegistry::find("timbreNoiseBalance");
                if (brightness != nullptr && tilt != nullptr && noise != nullptr)
                {
                    const auto findValue = [this](std::string_view id) {
                        for (std::size_t i = 0; i < kSessionControlIds.size(); ++i)
                            if (kSessionControlIds[i] == id) return sessionValues_[i];
                        return 0.0f;
                    };
                    shell_.session().setTimbre(findValue("timbreBrightnessDb"),
                                               findValue("timbreTiltDbPerOct"),
                                               findValue("timbreNoiseBalance"),
                                               shell_.session().state().timbre.curve);
                }
            }
        }
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

    void drawSessionControls(visage::Canvas& canvas, const VisageTokens& tokens,
                             const visage::Font& labelFont) const
    {
        for (std::size_t control = 0; control < kSessionControlIds.size(); ++control)
        {
            const auto* descriptor = sessionDescriptor(control);
            if (descriptor == nullptr)
                continue;
            const auto bounds = sessionControlBounds(control);
            if (!sessionContentVisible(bounds.y - 14.0f, bounds.height + 14.0f))
                continue;
            canvas.setColor(tokens.secondaryText);
            canvas.text(descriptor->name.data(), labelFont, visage::Font::kLeft,
                        bounds.x, bounds.y - 14.0f, bounds.width, 13.0f);
            canvas.setColor(tokens.surface);
            canvas.roundedRectangle(bounds.x, bounds.y, bounds.width, bounds.height, 4.0f);
            const auto value = sessionValues_[control];
            if (descriptor->type == ParameterType::boolean || descriptor->type == ParameterType::choice)
            {
                const auto active = descriptor->type == ParameterType::boolean && value >= 0.5f;
                if (active)
                {
                    canvas.setColor(tokens.accent);
                    canvas.roundedRectangle(bounds.x, bounds.y, bounds.width, bounds.height, 4.0f);
                }
                canvas.setColor(active ? tokens.background : tokens.text);
            }
            else
            {
                canvas.setColor(tokens.accent);
                canvas.roundedRectangle(bounds.x, bounds.y,
                                        bounds.width * descriptor->normalize(value), bounds.height, 4.0f);
                canvas.setColor(tokens.background);
            }
            const auto text = descriptor->text(value);
            canvas.text(text.c_str(), labelFont, visage::Font::kCenter,
                        bounds.x + 4.0f, bounds.y + 3.0f, bounds.width - 8.0f,
                        bounds.height - 6.0f);
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

    static void drawSourceCard(visage::Canvas& canvas, const VisageTokens& tokens,
                               const visage::Font& labelFont, const SessionSourceCard& source,
                               const char* slot, float x, bool dropActive)
    {
        canvas.setColor(dropActive ? tokens.accent : tokens.surface);
        canvas.roundedRectangle(x, 112.0f, 300.0f, 68.0f, 8.0f);
        canvas.setColor(tokens.accent);
        canvas.roundedRectangle(x + 8.0f, 120.0f, 18.0f, 14.0f, 4.0f);
        canvas.setColor(tokens.background);
        canvas.text(slot, labelFont, visage::Font::kCenter, x + 8.0f, 120.0f, 18.0f, 14.0f);
        canvas.setColor(tokens.text);
        const auto& name = source.name.empty() ? std::string("DROP WAV / ATS") : source.name;
        canvas.text(name.c_str(), labelFont, visage::Font::kLeft, x + 34.0f, 120.0f, 254.0f, 14.0f);
        constexpr std::array<const char*, 3> labels { "ANALYZE", "WAV", "ATS" };
        constexpr std::array<float, 3> widths { 88.0f, 92.0f, 92.0f };
        float buttonX = x + 8.0f;
        for (std::size_t index = 0; index < labels.size(); ++index)
        {
            canvas.setColor(tokens.panel);
            canvas.roundedRectangle(buttonX, 148.0f, widths[index], 24.0f, 5.0f);
            canvas.setColor(tokens.secondaryText);
            canvas.text(labels[index], labelFont, visage::Font::kCenter,
                        buttonX, 153.0f, widths[index], 14.0f);
            buttonX += widths[index] + 4.0f;
        }
    }

    void updateSourceDropTargets()
    {
        const auto visible = shell_.state().page == EditorPage::session;
        for (auto* target : sourceDropTargets_)
            if (target != nullptr)
                target->setVisible(visible);
        if (!visible)
            sourceDropActive_.fill(false);
    }

    void acceptSourceDrop(int slot, const std::vector<std::string>& paths)
    {
        const auto drop = classifySessionSourceDrop(slot, paths);
        const auto path = paths.size() == 1 ? paths.front().c_str() : "";
        if (sink_.fileDrop)
            sink_.fileDrop(sink_.context, slot, path, drop.has_value());
        if (!drop)
            return;

        if (drop->kind == SessionDropKind::wav)
            emitAction(ImportWavAction { drop->path, drop->slot });
        else
            emitAction(ImportAtsAction { drop->path, drop->slot });
        requestKeyboardFocus();
        redraw();
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

    void emitSessionGesture(std::size_t control, float normalized, bool begin, bool end)
    {
        const auto* descriptor = sessionDescriptor(control);
        if (descriptor == nullptr || control >= sessionValues_.size())
            return;
        setSessionValue(control, descriptor->denormalize(normalized));
        const auto value = sessionValues_[control];
        const auto id = std::string(descriptor->id);
        emitAction(SetParameterAction { id, value, begin, end });
        if (sink_.parameterGesture)
            sink_.parameterGesture(sink_.context, id.c_str(), value, begin, end);
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
    std::array<SourceDropTarget*, 2> sourceDropTargets_ {};
    std::array<bool, 2> sourceDropActive_ {};
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
    std::size_t activeSessionControl_ = 0;
    std::array<float, kSessionControlIds.size()> sessionValues_ {};
    float analysisScroll_ = 0.0f;
    float sessionScroll_ = 0.0f;
    int analysisProfile_ = 0;
    TelemetrySnapshot telemetry_ {};
    bool mathEditing_ = false;
    int partialIndex_ = 0;
    int partialFrame_ = 0;
    float partialAmplitude_ = 0.5f;
    float partialFrequencyHz_ = 440.0f;
    bool spectrumDragging_ = false;
    bool spectrumMoved_ = false;
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

void VisageEditor::setTelemetry(TelemetrySnapshot snapshot)
{
    impl_->frame.setTelemetry(std::move(snapshot));
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
