#include "VisageEditor.h"
#include "EditorShell.h"
#include "AtsEditorPages.h"
#include "FileDialogModel.h"
#include "SessionFileDrop.h"
#include "VisageNativeMac.h"
#include "../core/ParameterRegistry.h"
#include "../state/FactoryPresetState.h"

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
#include <utility>

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

class AtsProductionEditorShell final : public visage::Frame
{
public:
    explicit AtsProductionEditorShell(EditorEventSink sink) : sink_(sink)
    {
        setAcceptsKeystrokes(true);
        setReceiveChildMouseEvents(true);

        const auto theme = [this] {
            return ThemeTokens::forTheme(shell_.state().theme);
        };
        pageContainer_ = std::make_unique<AtsPageContainer>(
            std::vector<std::string> { "SESSION", "ANALYSIS", "SYNTHESIS", "PARTIALS", "MATH" },
            theme,
            [this](std::size_t page) {
                shell_.selectPage(page);
                updateSourceDropTargets();
                if (sink_.pageSelected)
                    sink_.pageSelected(sink_.context, static_cast<int>(page));
                redraw();
            });
        auto sessionPage = std::make_unique<AtsSessionPage>(
            shell_.session(), [this](const EditorAction& action) { emitPageAction(action); }, theme);
        sessionPageRaw_ = sessionPage.get();
        sessionPage->setChromeHandlers(
            [this](int slot) { runSourceImportDialog(NativeFileDialogKind::importWav, slot); },
            [this](int slot) { runSourceImportDialog(NativeFileDialogKind::importAts, slot); },
            [this](int slot) { runSourceImportDialog(NativeFileDialogKind::exportAts, slot); });
        sessionPage->setCentreHandlers({
            [this](visage::Canvas& canvas, UiRect well, UiRect timeline) {
                drawSessionCentre(canvas, well, timeline);
            },
            [this](visage::Point point, UiRect well, UiRect timeline,
                   bool shift, bool command, bool alt, bool middle, bool doubleClick) {
                return sessionCentreMouseDown(point, well, timeline, shift, command, alt, middle,
                                              doubleClick);
            },
            [this](visage::Point point, UiRect well) { sessionCentreMove(point, well); },
            [this] {
                spectrumHoverPartial_ = -1;
                shell_.spectrum().setHoveredBand(-1);
                redraw();
            },
            [this](visage::Point point, UiRect well) {
                sessionCentreDrag(point, well);
            },
            [this](visage::Point point, UiRect well) { sessionCentreUp(point, well); },
            [this](visage::Point point, float scaleFactor, UiRect well) {
                return sessionCentreMagnify(point, scaleFactor, well);
            },
            [this](visage::Point point, float deltaX, float deltaY, UiRect well,
                   bool shift, bool command, bool alt, bool precise) {
                return sessionCentreWheel(point, deltaX, deltaY, well, shift, command, alt, precise);
            },
        });
        pageContainer_->addPage(std::move(sessionPage));
        auto analysisPage = std::make_unique<AtsAnalysisPage>(
            shell_.analysis(), [this](const EditorAction& action) { emitPageAction(action); }, theme);
        analysisPageRaw_ = analysisPage.get();
        analysisPage->setChromeHandlers(
            [this](UiRect) { runSourceImportDialog(NativeFileDialogKind::importWav, 0); },
            [this](UiRect anchor) { showAnalysisProfileMenu(anchor); });
        pageContainer_->addPage(std::move(analysisPage));
        auto synthesisPage = std::make_unique<AtsSynthesisPage>(
            [this](const EditorAction& action) { emitPageAction(action); }, theme);
        synthesisPageRaw_ = synthesisPage.get();
        pageContainer_->addPage(std::move(synthesisPage));
        auto partialsPage = std::make_unique<AtsPartialsPage>(
            [this](const EditorAction& action) { emitPageAction(action); }, theme);
        partialsPageRaw_ = partialsPage.get();
        pageContainer_->addPage(std::move(partialsPage));
        pageContainer_->addPage(std::make_unique<AtsMathPage>(
            shell_.math(), [this](const EditorAction& action) { emitPageAction(action); }, theme));
        pageContainer_->setBounds(18.0f, 56.0f, 620.0f, 402.0f);
        pageContainerRaw_ = pageContainer_.get();
        addChild(std::move(pageContainer_));
        // The source cards are shell-owned drop zones. Re-add their native
        // targets after the page container so page widgets cannot swallow a
        // file drop that belongs to a card.
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
        // Drop targets for the Analysis page source card wells (right column).
        // Hidden by default; shown only while the analysis page is active.
        for (int slot = 0; slot < 2; ++slot)
        {
            auto target = std::make_unique<SourceDropTarget>(
                [this, slot](const std::vector<std::string>& paths) { acceptSourceDrop(slot, paths); },
                [this, slot](bool active) {
                    sourceDropActive_[static_cast<std::size_t>(slot)] = active;
                    redraw();
                });
            target->setVisible(false);
            analysisDropTargets_[static_cast<std::size_t>(slot)] = target.get();
            addChild(std::move(target));
        }
        onMouseDown() += [this](const visage::MouseEvent& event) {
            if (event.shouldTriggerPopup())
            {
                visage::PopupMenu popup("Context menu");
                popup.addOption(1, "Context option");
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
                // Keep the context menu in a deterministic logical location so
                // the native harness can select it across window-system scale.
                popup.show(this, { 40, 40 });
                return;
            }
            const auto point = localPoint(*this, event);
            if (point.y < 48.0f && point.x >= 120.0f && point.x < 236.0f)
            {
                visage::PopupMenu popup("Presets");
                for (int index = 0; index < 12; ++index)
                    popup.addOption(index + 1, std::string(state::kFactoryPresets[
                                                   static_cast<std::size_t>(index)].name));
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
                if ((result.status == NativeFileDialogStatus::initializationFailed ||
                     result.status == NativeFileDialogStatus::failed) && sink_.dialogError)
                    sink_.dialogError(sink_.context, static_cast<int>(kind),
                                      "native_dialog_failed", result.error.c_str());
                if (const auto action = FileDialogModel::resolve(kind, result))
                        emitAction(*action);
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
                        setTheme(optionId == 2 ? ThemeId::porcelain : ThemeId::ember);
                        if (sink_.themeSelected)
                            sink_.themeSelected(sink_.context, optionId - 1);
                    }
                };
                popup.onCancel() += [this] {
                    if (sink_.popupDismissed)
                        sink_.popupDismissed(sink_.context);
                };
                if (sink_.popupShown)
                    sink_.popupShown(sink_.context);
                popup.show(this, { width() - 208.0f, 8.0f });
                return;
            }
            // No requestKeyboardFocus() here. Visage bubbles every mouse-down to
            // the ancestors, so this runs *after* whichever nested frame handled
            // the click — including one that just opened a popup or focused a
            // text field. Claiming focus back kills them. The window's event
            // handler has already focused this frame if nothing deeper wanted it.
            if (sink_.mouseDown)
                sink_.mouseDown(sink_.context);
        };
        // Session drag/up/wheel now belong to AtsSessionPage's centre delegate;
        // the root only still reports the bare mouse-up to the C sink.
        onMouseUp() += [this](const visage::MouseEvent&) {
            if (sink_.mouseUp)
                sink_.mouseUp(sink_.context);
        };
    }

    void setDialogParent(void* nativeParent) noexcept { dialogParent_ = nativeParent; }

    bool handleMagnify(visage::Point point, float scaleFactor)
    {
        return sessionPageRaw_ != nullptr && sessionPageRaw_->dispatchMagnify(point, scaleFactor);
    }

    void resized() override
    {
        if (pageContainerRaw_ != nullptr)
        {
            const auto body = ThemeLayout::body(width(), height());
            pageContainerRaw_->setBounds(body.x, body.y, body.width, body.height);
            updateSourceDropTargetBounds(body);
        }
    }

    void selectPage(std::size_t page)
    {
        if (pageContainerRaw_ != nullptr)
            pageContainerRaw_->selectPage(page);
    }

    void setTheme(ThemeId theme)
    {
        shell_.setTheme(theme);
        // Widgets read the theme at draw time, but Visage keeps each frame's
        // last drawing until that frame is invalidated; redrawing only the
        // container left the visible page's sliders and segments in the old
        // theme (seen on ANALYSIS in the screenshot matrix).
        redrawAll();
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

    void setEditorSnapshot(EditorSnapshot snapshot)
    {
        telemetry_ = snapshot.telemetry;
        sourceNames_ = snapshot.sourceNames;
        sourceAvailable_ = snapshot.sourceAvailable;
        sourceResidualAvailable_ = snapshot.sourceResidualAvailable;
        shell_.setActivity(telemetry_.analysisRunning, telemetry_.renderRunning);
        shell_.setHistoryAvailability(telemetry_.canUndo, telemetry_.canRedo);
        shell_.analysis().setWorkerRunning(telemetry_.analysisRunning);
        shell_.analysis().setHasSource(telemetry_.hasSourceA || telemetry_.hasSourceB ||
                                       telemetry_.hasCurrentModel);

        // Source publication uses the stable snapshot order current, A, B;
        // the visible Session cards are A and B. Keep their labels and model
        // metadata on the same accepted publication as the parameters.
        for (std::size_t card = 0; card < 2; ++card)
        {
            const auto sourceIndex = card + 1;
            shell_.session().setSource(card, SessionSourceCard {
                snapshot.sourceAvailable[sourceIndex],
                snapshot.sourceAvailable[sourceIndex],
                !snapshot.sourceAvailable[sourceIndex],
                snapshot.sourceNames[sourceIndex],
                snapshot.sourceDurations[sourceIndex],
                static_cast<std::uint32_t>(std::max(0, snapshot.sourcePartialCounts[sourceIndex])),
                snapshot.sourceAvailable[sourceIndex]
            });
        }
        shell_.session().setPreset(snapshot.presetName, false);
        shell_.session().setOnsetOverlay(snapshot.onsets);
        if (partialsPageRaw_ != nullptr)
        {
            const auto partials = std::max(0, snapshot.sourcePartialCounts[0]);
            partialsPageRaw_->setDimensions(partials, 1024);
            partialsPageRaw_->setSelection(0, 0);
        }

        // Keep the compact page controls and the legacy probe-compatible
        // controls reconciled from the same accepted host snapshot. No page
        // performs a second parameter read from a wrapper.
        const auto analysisIds = AnalysisPageModel::controlIds();
        for (const auto& descriptor : ParameterRegistry::descriptors())
        {
            if (descriptor.denseIndex >= snapshot.parameters.size())
                continue;
            const auto value = snapshot.parameters[descriptor.denseIndex];
            if (sessionPageRaw_ != nullptr && std::find(AtsSessionPage::kControlIds.begin(),
                                                        AtsSessionPage::kControlIds.end(),
                                                        descriptor.id) != AtsSessionPage::kControlIds.end())
                sessionPageRaw_->setControlValue(descriptor.id, value);
            if (analysisPageRaw_ != nullptr && std::find(analysisIds.begin(),
                                                         analysisIds.end(),
                                                         descriptor.id) != analysisIds.end())
                analysisPageRaw_->setControlValue(descriptor.id, value);
            if (synthesisPageRaw_ != nullptr && std::find(AtsSynthesisPage::kControlIds.begin(),
                                                          AtsSynthesisPage::kControlIds.end(),
                                                          descriptor.id) != AtsSynthesisPage::kControlIds.end())
                synthesisPageRaw_->setControlValue(descriptor.id, value);
        }
        if (snapshot.timeline.size() >= 2)
            shell_.session().setTimeline(snapshot.timeline);
        const auto readParameter = [&snapshot](std::string_view id, float fallback) {
            const auto* descriptor = ParameterRegistry::find(id);
            return descriptor != nullptr && descriptor->denseIndex < snapshot.parameters.size()
                ? snapshot.parameters[descriptor->denseIndex] : fallback;
        };
        shell_.session().setTimbre(readParameter("timbreBrightnessDb", 0.5f),
                                   readParameter("timbreTiltDbPerOct", 0.5f),
                                   readParameter("timbreNoiseBalance", 0.5f),
                                   snapshot.timbrePlan.empty()
                                       ? shell_.session().state().timbre.curve
                                       : snapshot.timbrePlan);
        if (snapshot.spectrum)
            setSpectrumSnapshot(std::move(*snapshot.spectrum));
        spectrumOverlaySnapshot_ = snapshot.spectrumOverlay;
        redraw();
    }

    void setSpectrumSnapshot(SpectralTraceSnapshot snapshot)
    {
        spectrumSnapshot_ = std::move(snapshot);
        spectrumHoverPartial_ = -1;
        shell_.spectrum().setHoveredBand(-1);
        if (spectrumSnapshot_.onsets.empty() && spectrumSnapshot_.onsetNorms.empty())
            spectrumOnsetEditing_ = false;
        shell_.spectrum().setSource(spectrumSnapshot_.source);
        shell_.spectrum().setMode(spectrumSnapshot_.mode);
        redraw();
    }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = visageTokens();
        canvas.setColor(tokens.window.argb);
        canvas.fill(0.0f, 0.0f, width(), height());
        canvas.setColor(tokens.panel.argb);
        canvas.rectangle(0.0f, 0.0f, width(), 48.0f);

        const visage::Font titleFont(17.0f, visage::fonts::Lato_Regular_ttf);
        const visage::Font labelFont(10.0f, visage::fonts::Lato_Regular_ttf);
        canvas.setColor(tokens.accent.argb);
        canvas.text("ATS RESYNTH", titleFont, visage::Font::kLeft, 16.0f, 7.0f, 220.0f, 22.0f);
        canvas.setColor(tokens.textSecondary.argb);
        canvas.text("SPECTRAL MODEL EDITOR", labelFont, visage::Font::kLeft,
                    16.0f, 28.0f, 220.0f, 14.0f);
        canvas.setColor(tokens.textPrimary.argb);
        canvas.text("PRESETS", labelFont, visage::Font::kCenter, 120.0f, 14.0f, 116.0f, 18.0f);
        canvas.setColor(shell_.state().canUndo ? tokens.textPrimary.argb : tokens.textSecondary.argb);
        canvas.text("UNDO", labelFont, visage::Font::kCenter, 248.0f, 14.0f, 68.0f, 18.0f);
        canvas.setColor(shell_.state().canRedo ? tokens.textPrimary.argb : tokens.textSecondary.argb);
        canvas.text("REDO", labelFont, visage::Font::kCenter, 316.0f, 14.0f, 68.0f, 18.0f);
        canvas.setColor(shell_.state().analysisRunning ? tokens.accent.argb : tokens.textSecondary.argb);
        canvas.circle(402.0f, 17.0f, 8.0f);
        canvas.setColor(tokens.textSecondary.argb);
        canvas.text("ANL", labelFont, visage::Font::kLeft, 414.0f, 14.0f, 30.0f, 18.0f);
        canvas.setColor(shell_.state().renderRunning ? tokens.accent.argb : tokens.textSecondary.argb);
        canvas.circle(448.0f, 17.0f, 8.0f);
        canvas.setColor(tokens.textSecondary.argb);
        canvas.text("RND", labelFont, visage::Font::kLeft, 460.0f, 14.0f, 30.0f, 18.0f);
        canvas.setColor(tokens.textSecondary.argb);
        canvas.text(shell_.state().theme == ThemeId::porcelain ? "PORCELAIN" : "EMBER",
                    labelFont, visage::Font::kCenter, width() - 220.0f, 14.0f, 200.0f, 18.0f);

        // The tab bar and every page body belong to AtsPageContainer; drawing
        // either here too would double-render it and let the selections drift.
    }

    // Invoked by AtsSessionPage during its own draw, so the rects are page-local
    // and the spectrum lands inside the §10.4 centre column rather than behind it.
    void drawSessionCentre(visage::Canvas& canvas, UiRect well, UiRect timeline)
    {
        const auto tokens = visageTokens();
        const visage::Font labelFont(ThemeFont::controlLabel, visage::fonts::Lato_Regular_ttf);
        const visage::Font axisFont(8.5f, visage::fonts::Lato_Regular_ttf);
        const visage::Font titleFont(12.0f, visage::fonts::Lato_Regular_ttf);
        const visage::Font emptyFont(15.0f, visage::fonts::Lato_Regular_ttf);

        spectrumSnapshot_.mode = shell_.spectrum().mode();
        shell_.spectrum().setSnapshot(&spectrumSnapshot_, well);
        std::vector<int> partials;
        partials.reserve(spectrumSnapshot_.partials.size());
        for (const auto& trace : spectrumSnapshot_.partials)
            partials.push_back(trace.partialIndex);
        shell_.spectrum().setAvailablePartials(std::move(partials));
        std::vector<int> bands;
        bands.reserve(spectrumSnapshot_.residualBands.size());
        for (const auto& band : spectrumSnapshot_.residualBands)
            bands.push_back(band.bandIndex);
        shell_.spectrum().setAvailableBands(std::move(bands));
        const auto& session = shell_.session().state();
        const auto& navigation = shell_.spectrum().navigation();
        SpectrumRenderOptions renderOptions;
        renderOptions.theme = shell_.state().theme;
        renderOptions.selectedPartial = navigation.selectedPartial;
        renderOptions.selectedPartials = navigation.selectedPartials;
        renderOptions.playheadNorm = session.transport.normalizedPlayhead;
        renderOptions.showPlayhead = true;
        renderOptions.overlaySnapshot = spectrumOverlaySnapshot_ ? &*spectrumOverlaySnapshot_ : nullptr;
        renderOptions.hoveredPartial = spectrumHoverPartial_;
        renderOptions.lassoActive = navigation.lassoActive;
        renderOptions.lassoBounds = navigation.lassoBounds;
        renderOptions.showOnsetOverlay = spectrumOnsetEditing_;
        renderOptions.selectedBands = navigation.selectedBands;
        if (renderOptions.selectedBands.empty() && navigation.selectedBand >= 0)
            renderOptions.selectedBands.push_back(navigation.selectedBand);
        renderOptions.residualFirstFrame = navigation.residualFirstFrame;
        renderOptions.residualLastFrame = navigation.residualLastFrame;
        if (navigation.envelopeEditing)
        {
            const auto plot = SpectrumRenderer::getPlotBounds(well, spectrumSnapshot_);
            renderOptions.envelopeRange = {
                plot.x + plot.width * navigation.envelopeTimeStart,
                plot.y + plot.height * (1.0f - navigation.envelopeFrequencyEnd),
                plot.width * (navigation.envelopeTimeEnd - navigation.envelopeTimeStart),
                plot.height * (navigation.envelopeFrequencyEnd - navigation.envelopeFrequencyStart) };
        }
        renderOptions.viewport = { (navigation.timeStart + navigation.timeEnd) * 0.5f,
                                   1.0f / std::max(1.0e-6f, navigation.timeEnd - navigation.timeStart),
                                   (navigation.frequencyStart + navigation.frequencyEnd) * 0.5f,
                                   1.0f / std::max(1.0e-6f, navigation.frequencyEnd - navigation.frequencyStart) };
        const auto plot = SpectrumRenderer::getPlotBounds(well, spectrumSnapshot_);
        const auto commands = SpectrumRenderer::render(
            spectrumSnapshot_, well, renderOptions);
        const auto segmentStride = SpectrumRenderer::pathSegmentStride(commands);
        for (const auto& command : commands)
        {
            const auto clipToPlot = SpectrumRenderer::requiresPlotClip(command);
            if (clipToPlot)
            {
                canvas.saveState();
                canvas.trimClampBounds(plot.x, plot.y, plot.width, plot.height);
            }
            const auto restoreClip = [&] {
                if (clipToPlot)
                    canvas.restoreState();
            };
            const auto& b = command.bounds;
            const auto bakedAlpha = static_cast<std::uint32_t>(
                std::clamp(command.alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
            const auto color = (command.color & 0x00ffffffu) | (bakedAlpha << 24);
            if (command.kind == SpectrumDrawKind::background)
            {
                canvas.setColor(visage::Brush::radial(
                    SpectralCanvasTokens::fixed().background.argb,
                    SpectralCanvasTokens::fixed().bgVignetteEdge.argb,
                    visage::Point(b.x + b.width * 0.5f, b.y + b.height * 0.42f),
                    b.width * 0.72f, b.height * 0.86f));
                canvas.roundedRectangle(b.x, b.y, b.width, b.height, 5.0f);
                restoreClip();
                continue;
            }
            canvas.setColor(color);
            if (command.isPath())
            {
                const auto solidPath = command.flat
                    || command.kind == SpectrumDrawKind::selectedPartial
                    || command.kind == SpectrumDrawKind::glowHalo
                    || command.kind == SpectrumDrawKind::traceGlow
                    || command.kind == SpectrumDrawKind::envelopeTrace;
                const auto colorAt = [&](float proportion) {
                    if (solidPath || command.stops.empty())
                        return UiColor { color };
                    auto next = std::lower_bound(command.stops.begin(), command.stops.end(), proportion,
                        [](const TraceStop& stop, float value) { return stop.proportion < value; });
                    if (next == command.stops.begin())
                        return ampColour(next->amp).withAlpha(
                            std::clamp(command.alpha * next->fade, 0.0f, 1.0f));
                    if (next == command.stops.end())
                    {
                        const auto& stop = command.stops.back();
                        return ampColour(stop.amp).withAlpha(
                            std::clamp(command.alpha * stop.fade, 0.0f, 1.0f));
                    }
                    const auto& previous = *(next - 1);
                    const auto span = std::max(1.0e-6f, next->proportion - previous.proportion);
                    const auto amount = std::clamp(
                        (proportion - previous.proportion) / span, 0.0f, 1.0f);
                    const auto amp = previous.amp + amount * (next->amp - previous.amp);
                    const auto fade = previous.fade + amount * (next->fade - previous.fade);
                    return ampColour(amp).withAlpha(
                        std::clamp(command.alpha * fade, 0.0f, 1.0f));
                };
                const auto axisX = command.gradEnd.x - command.gradStart.x;
                const auto axisY = command.gradEnd.y - command.gradStart.y;
                const auto axisLength2 = axisX * axisX + axisY * axisY;
                const auto last = command.vertices.size() - 1;
                for (std::size_t i = 0; i < last;)
                {
                    const auto next = std::min(i + segmentStride, last);
                    const auto& a = command.vertices[i];
                    const auto& b = command.vertices[next];
                    i = next;
                    const auto midpointX = (a.x + b.x) * 0.5f - command.gradStart.x;
                    const auto midpointY = (a.y + b.y) * 0.5f - command.gradStart.y;
                    const auto proportion = axisLength2 > 1.0e-6f
                        ? std::clamp((midpointX * axisX + midpointY * axisY) / axisLength2,
                                     0.0f, 1.0f)
                        : 0.0f;
                    canvas.setColor(colorAt(proportion).argb);
                    canvas.segment(a.x, a.y, b.x, b.y, command.thickness, true);
                }
                restoreClip();
                continue;
            }
            switch (command.kind)
            {
            case SpectrumDrawKind::envelopeFill:
            {
                const auto top = (command.color & 0x00ffffffu) | (bakedAlpha << 24);
                const auto bottom = command.color & 0x00ffffffu;
                canvas.setColor(visage::Brush::vertical(top, bottom));
                canvas.triangle(b.x, b.y, command.x2, command.y2,
                                command.x2, command.floorY);
                canvas.triangle(b.x, b.y, command.x2, command.floorY,
                                b.x, command.floorY);
                break;
            }
            case SpectrumDrawKind::envelopeTrace:
            case SpectrumDrawKind::partialTrace:
                canvas.segment(b.x, b.y, b.x + b.width, b.y + b.height,
                               command.thickness, true);
                break;
            case SpectrumDrawKind::selectedPartial:
                canvas.segment(b.x, b.y, b.x + b.width, b.y + b.height,
                               command.thickness, true);
                break;
            case SpectrumDrawKind::selectionMarker:
                canvas.rectangleBorder(b.x, b.y, b.width, b.height, 1.0f);
                break;
            case SpectrumDrawKind::traceGlow:
                canvas.segment(b.x, b.y, b.x + b.width, b.y + b.height,
                               command.thickness, true);
                break;
            case SpectrumDrawKind::playheadWake:
                canvas.rectangle(b.x, b.y, b.width, b.height);
                break;
            case SpectrumDrawKind::playhead:
                canvas.rectangle(b.x, b.y, b.width, b.height);
                break;
            case SpectrumDrawKind::residualBand:
                canvas.segment(b.x, b.y, b.x + b.width, b.y + b.height,
                               command.thickness, true);
                break;
            case SpectrumDrawKind::glowHalo:
                canvas.roundedRectangle(b.x, b.y, b.width, b.height, 3.0f);
                break;
            case SpectrumDrawKind::axis:
                canvas.roundedRectangleBorder(b.x, b.y, b.width, b.height, 5.0f, 1.0f);
                break;
            case SpectrumDrawKind::axisHighlight:
                canvas.roundedRectangle(b.x, b.y, b.width, b.height, 4.0f);
                break;
            case SpectrumDrawKind::legend:
                // Reserved text slots, not fills; the legend is drawn below as
                // the clickable mode chip.
                break;
            case SpectrumDrawKind::axisLabel:
                canvas.setColor(color);
                canvas.text(command.label, axisFont, visage::Font::kCenter,
                            b.x, b.y, b.width, b.height);
                break;
            case SpectrumDrawKind::title:
                canvas.setColor(color);
                canvas.text("ATS TRACE DISPLAY", titleFont, visage::Font::kLeft,
                            b.x, b.y, b.width, b.height);
                break;
            case SpectrumDrawKind::emptyState:
                canvas.text("NO ATS MODEL", emptyFont, visage::Font::kCenter,
                            b.x, b.y + b.height * 0.45f, b.width, 18.0f);
                break;
            case SpectrumDrawKind::bandLabel:
            case SpectrumDrawKind::freqLabel:
                canvas.setColor(color);
                canvas.text(command.label, labelFont, visage::Font::kCenter,
                            b.x, b.y, b.width, b.height);
                break;
            case SpectrumDrawKind::ampLegend:
            {
                canvas.setColor(color);
                canvas.roundedRectangle(b.x, b.y, b.width, b.height, 4.0f);
                canvas.setColor((SpectralCanvasTokens::fixed().axisFrame.argb & 0x00ffffffu) |
                                (static_cast<std::uint32_t>(0.45f * 255.0f) << 24));
                canvas.roundedRectangleBorder(b.x, b.y, b.width, b.height, 4.0f, 1.0f);
                visage::Gradient ramp;
                ramp.addColorStop(visage::Color(0xff1b4fa0u), 0.0f);
                ramp.addColorStop(visage::Color(0xff22a7c4u), 0.25f);
                ramp.addColorStop(visage::Color(0xff3fe8b0u), 0.5f);
                ramp.addColorStop(visage::Color(0xffb8ff7au), 0.75f);
                ramp.addColorStop(visage::Color(0xffb8ff7au), 1.0f);
                canvas.setBrush(visage::Brush::linear(
                    std::move(ramp), visage::Point(b.x + 8.0f, b.y + 16.0f),
                    visage::Point(b.x + b.width - 8.0f, b.y + 16.0f)));
                canvas.roundedRectangle(b.x + 8.0f, b.y + 16.0f, b.width - 16.0f, 6.0f, 3.0f);
                canvas.setColor(0xff6fa3b0u);
                canvas.text((spectrumSnapshot_.mode == SpectralTraceMode::residual ||
                             spectrumSnapshot_.mode == SpectralTraceMode::isometricResidual)
                                ? "E N E R G Y" : "A M P", labelFont, visage::Font::kLeft,
                            b.x + 8.0f, b.y + 2.0f, b.width - 16.0f, 12.0f);
                const auto residualLegend = spectrumSnapshot_.mode == SpectralTraceMode::residual
                    || spectrumSnapshot_.mode == SpectralTraceMode::isometricResidual;
                canvas.text(residualLegend ? "lo" : "-72 dB", labelFont, visage::Font::kLeft,
                            b.x + 8.0f, b.y + 25.0f, 54.0f, 10.0f);
                canvas.text(residualLegend ? "hi" : "0", labelFont, visage::Font::kRight,
                            b.x + b.width - 22.0f, b.y + 25.0f, 14.0f, 10.0f);
                break;
            }
            case SpectrumDrawKind::toolbarButton:
                canvas.setColor(color);
                canvas.roundedRectangle(b.x, b.y, b.width, b.height, 3.0f);
                canvas.setColor((SpectralCanvasTokens::fixed().axisFrame.argb & 0x00ffffffu) |
                                (static_cast<std::uint32_t>(0.45f * 255.0f) << 24));
                canvas.roundedRectangleBorder(b.x, b.y, b.width, b.height, 3.0f, 1.0f);
                canvas.setColor(SpectralCanvasTokens::fixed().hover.argb);
                canvas.text(command.label, labelFont, visage::Font::kCenter,
                            b.x, b.y, b.width, b.height);
                break;
            case SpectrumDrawKind::zoomBar:
            case SpectrumDrawKind::timeScrollbar:
            case SpectrumDrawKind::frequencyScrollbar:
                canvas.setColor(color);
                canvas.roundedRectangle(b.x, b.y, b.width, b.height,
                                        command.index == 0 ? 2.0f : 3.0f);
                if (command.index == 0)
                {
                    canvas.setColor((SpectralCanvasTokens::fixed().axisFrame.argb & 0x00ffffffu) |
                                    (static_cast<std::uint32_t>(0.45f * 255.0f) << 24));
                    canvas.roundedRectangleBorder(b.x, b.y, b.width, b.height,
                                                  2.0f, 1.0f);
                }
                break;
            default:
                canvas.rectangle(b.x, b.y, b.width, b.height);
                break;
            }
            restoreClip();
        }
        for (const auto& control : SpectrumChromeLayout::toolbarControls(well,
                                                                           spectrumSnapshot_.mode))
        {
            const auto& b = control.bounds;
            if (control.kind == SpectrumChromeLayout::ToolbarControlKind::label)
            {
                canvas.setColor(tokens.textSecondary.argb);
                canvas.text(std::string(control.label), labelFont, visage::Font::kLeft,
                            b.x, b.y, b.width, b.height);
            }
            else if (control.kind == SpectrumChromeLayout::ToolbarControlKind::slider)
            {
                canvas.setColor(SpectralCanvasTokens::fixed().axisFrame.argb);
                canvas.roundedRectangle(b.x, b.y + b.height * 0.5f - 2.0f,
                                        b.width, 4.0f, 2.0f);
                canvas.setColor(tokens.accent.argb);
                canvas.circle(b.x + b.width * std::clamp(control.value, 0.0f, 1.0f),
                              b.y + b.height * 0.5f, 5.0f);
            }
            else
            {
                paintThemeOps(canvas, ThemeRecipes::buttonBackground(tokens, b, false, false));
                canvas.setColor(tokens.textSecondary.argb);
                canvas.text(std::string(control.label), labelFont, visage::Font::kCenter,
                            b.x, b.y, b.width, b.height);
            }
        }

        const bool isometric = spectrumSnapshot_.mode == SpectralTraceMode::isometric ||
                               spectrumSnapshot_.mode == SpectralTraceMode::isometricResidual ||
                               spectrumSnapshot_.mode == SpectralTraceMode::isometricEnvelope ||
                               spectrumSnapshot_.mode == SpectralTraceMode::isometricEnvelopeRdc;
        if (isometric)
        {
            const auto axisFont = visage::Font(8.0f, visage::fonts::Lato_Regular_ttf);
            canvas.setColor((SpectralCanvasTokens::fixed().axisFrame.argb & 0x00ffffffu) |
                            (static_cast<std::uint32_t>(0.70f * 255.0f) << 24));
            canvas.text("TIME", axisFont, visage::Font::kCenter,
                        well.x + well.width * 0.48f, well.y + well.height - 18.0f, 52.0f, 12.0f);
            canvas.text("FREQ", axisFont, visage::Font::kLeft,
                        well.x + 8.0f, well.y + well.height * 0.48f, 48.0f, 12.0f);
            canvas.text("AMP", axisFont, visage::Font::kRight,
                        well.x + well.width - 46.0f, well.y + 8.0f, 38.0f, 12.0f);
        }
        const auto residualMode = spectrumSnapshot_.mode == SpectralTraceMode::residual
            || spectrumSnapshot_.mode == SpectralTraceMode::isometricResidual;
        const auto hasSelection = residualMode
            ? (!navigation.selectedBands.empty() || navigation.selectedBand >= 0)
            : (!navigation.selectedPartials.empty() || navigation.selectedPartial >= 0);
        const auto hasOnsets = !spectrumSnapshot_.onsets.empty()
            || !spectrumSnapshot_.onsetNorms.empty();
        const auto chrome = SpectrumChromeLayout::layout(well, spectrumSnapshot_.mode,
                                                          hasOnsets, hasSelection);
        const UiRect modeChip = chrome.viewMenuButton;
        paintThemeOps(canvas, ThemeRecipes::buttonBackground(tokens, modeChip, false, false));
        canvas.setColor((SpectralCanvasTokens::fixed().axisFrame.argb & 0x00ffffffu) |
                        (static_cast<std::uint32_t>(0.45f * 255.0f) << 24));
        canvas.roundedRectangleBorder(modeChip.x, modeChip.y, modeChip.width, modeChip.height,
                                      4.0f, 1.0f);
        canvas.setColor(tokens.textSecondary.argb);
        canvas.text(spectrumModeLabel(), labelFont, visage::Font::kCenter, modeChip.x,
                    modeChip.y, modeChip.width, modeChip.height);
        if (hasSelection)
        {
            const auto deleteButton = chrome.deleteButton;
            paintThemeOps(canvas, ThemeRecipes::buttonBackground(tokens, deleteButton, false, false));
            canvas.setColor(tokens.textPrimary.argb);
            canvas.text("DELETE", labelFont, visage::Font::kCenter, deleteButton.x,
                        deleteButton.y, deleteButton.width, deleteButton.height);
        }
        if (hasOnsets)
        {
            const auto onsetButton = chrome.onsetEditButton;
            paintThemeOps(canvas, ThemeRecipes::buttonBackground(tokens, onsetButton,
                                                                 false, spectrumOnsetEditing_));
            canvas.setColor(spectrumOnsetEditing_ ? tokens.accent.argb : tokens.textSecondary.argb);
            canvas.text("ONSETS", labelFont, visage::Font::kCenter, onsetButton.x,
                        onsetButton.y, onsetButton.width, onsetButton.height);
        }
        if (chrome.envSourceAButton.width > 0.0f)
        {
            for (const auto& sourceButton : { std::pair<const char*, UiRect> { "A", chrome.envSourceAButton },
                                              std::pair<const char*, UiRect> { "B", chrome.envSourceBButton } })
            {
                paintThemeOps(canvas, ThemeRecipes::buttonBackground(tokens, sourceButton.second,
                                                                       false, false));
                canvas.setColor(tokens.textSecondary.argb);
                canvas.text(sourceButton.first, labelFont, visage::Font::kCenter,
                            sourceButton.second.x, sourceButton.second.y,
                            sourceButton.second.width, sourceButton.second.height);
            }
        }
        if (chrome.selectionLabel.width > 0.0f)
        {
            const auto selectedCount = residualMode ? navigation.selectedBands.size()
                                                     : navigation.selectedPartials.size();
            const auto selectionText = selectedCount > 0
                ? std::to_string(selectedCount) + " selected" : "No selection";
            canvas.setColor(tokens.textSecondary.argb);
            canvas.text(selectionText, labelFont, visage::Font::kLeft,
                        chrome.selectionLabel.x + 4.0f, chrome.selectionLabel.y,
                        chrome.selectionLabel.width - 8.0f, chrome.selectionLabel.height);
        }

        const UiRect track = timelineTrack(timeline);
        paintThemeOps(canvas, ThemeRecipes::well(tokens, track, ThemeRadius::sliderTrack));
        canvas.setColor(tokens.accent.argb);
        if (session.morph.timeline.size() >= 3)
            canvas.rectangle(track.x + track.width * session.morph.timeline[1], track.y - 6.0f,
                             3.0f, track.height + 12.0f);
        canvas.rectangle(track.x + track.width * session.transport.normalizedPlayhead,
                         track.y - 10.0f, 2.0f, track.height + 20.0f);
        canvas.setColor(tokens.textSecondary.argb);
        canvas.text("MORPH TIMELINE", labelFont, visage::Font::kLeft, timeline.x + 10.0f,
                    timeline.y + 6.0f, 180.0f, 14.0f);

        const visage::Font buttonFont(ThemeFont::cheekButton, visage::fonts::Lato_Regular_ttf);
        for (const auto& button : timelineButtons(timeline))
        {
            paintThemeOps(canvas,
                          ThemeRecipes::buttonBackground(tokens, button.second, false, false));
            canvas.setColor(tokens.textPrimary.argb);
            canvas.text(button.first, buttonFont, visage::Font::kCenter, button.second.x,
                        button.second.y, button.second.width, button.second.height);
        }
    }

    // The timeline panel is a scrub track with the two smearnov actions parked
    // on its right; both are derived so the hit test cannot drift from the paint.
    [[nodiscard]] static UiRect timelineTrack(UiRect timeline) noexcept
    {
        return { timeline.x + 10.0f, timeline.y + timeline.height * 0.5f - 3.0f,
                 std::max(0.0f, timeline.width - 200.0f), 6.0f };
    }

    [[nodiscard]] static std::array<std::pair<const char*, UiRect>, 2>
    timelineButtons(UiRect timeline) noexcept
    {
        const float y = timeline.y + timeline.height * 0.5f - 14.0f;
        const float x = timeline.x + timeline.width - 176.0f;
        return { std::pair<const char*, UiRect> { "DETECT", UiRect { x, y, 82.0f, 28.0f } },
                 std::pair<const char*, UiRect> { "APPLY",
                                                  UiRect { x + 90.0f, y, 82.0f, 28.0f } } };
    }

    // Shown against the session page, not the shell, because the rect handed to
    // the delegate is page-local.
    void showSpectrumModeMenu(visage::Point origin)
    {
        visage::PopupMenu popup("Spectrum view");
        const auto capabilities = spectrumCapabilities();
        const auto items = shell_.spectrum().menu(capabilities);
        const auto sourceLabels = shell_.spectrum().sourceMenuLabels(capabilities);
        const auto addSourceMenu = [&](int source, std::string_view label, bool enabled) {
            visage::PopupMenu submenu { std::string(label) };
            for (const auto& item : items)
            {
                const auto itemEnabled = enabled &&
                    (item.mode != SpectralTraceMode::residual || capabilities.hasResidualForSource(source));
                submenu.addOption((source + 1) * 100 + item.id + 1,
                                  std::string(item.label)).enable(itemEnabled).select(
                                      enabled && source == SpectrumInteractionModel::sourceIndex(
                                          shell_.spectrum().source())
                                      && item.mode == shell_.spectrum().mode());
            }
            popup.addSubMenu(std::move(submenu));
        };
        addSourceMenu(0, sourceLabels[0], capabilities.hasCurrentSource);
        addSourceMenu(1, sourceLabels[1].empty() ? std::string_view { "Source A" } : sourceLabels[1],
                      capabilities.hasSourceA);
        addSourceMenu(2, sourceLabels[2].empty() ? std::string_view { "Source B" } : sourceLabels[2],
                      capabilities.hasSourceB);
        popup.onSelection() += [this, capabilities](int optionId) {
            if (optionId < 101)
                return;
            const auto source = optionId / 100 - 1;
            const auto mode = optionId % 100 - 1;
            const auto action = shell_.spectrum().selectSourceMenuItem(source, mode, capabilities);
            if (action.mode < 0)
                return;
            shell_.spectrum().setSource(static_cast<SpectralTraceSource>(source));
            shell_.spectrum().setMode(static_cast<SpectralTraceMode>(mode));
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
        popup.show(sessionPageRaw_ != nullptr ? static_cast<visage::Frame*>(sessionPageRaw_) : this,
                   origin);
    }

    bool sessionCentreMouseDown(visage::Point point, UiRect well, UiRect timeline,
                                bool shift, bool command, bool alt, bool middle, bool doubleClick)
    {
        const auto mode = shell_.spectrum().mode();
        const auto& selection = shell_.spectrum().navigation();
        const auto residualSelection = mode == SpectralTraceMode::residual
            || mode == SpectralTraceMode::isometricResidual;
        const auto canDelete = residualSelection
            ? (!selection.selectedBands.empty() || selection.selectedBand >= 0)
            : (!selection.selectedPartials.empty() || selection.selectedPartial >= 0);
        const auto hasOnsets = !spectrumSnapshot_.onsets.empty()
            || !spectrumSnapshot_.onsetNorms.empty();
        const auto chrome = SpectrumChromeLayout::layout(well, mode, hasOnsets, canDelete);
        const UiRect modeChip = chrome.viewMenuButton;
        const auto deleteButton = chrome.deleteButton;
        if (canDelete && pointInRect(point, deleteButton))
        {
            auto ids = residualSelection ? selection.selectedBands : selection.selectedPartials;
            if (ids.empty())
            {
                const auto id = residualSelection ? selection.selectedBand : selection.selectedPartial;
                if (id >= 0)
                    ids.push_back(id);
            }
            emitAction(SpectrumEditAction {
                residualSelection ? SpectrumEditCommand::muteResidual
                                  : SpectrumEditCommand::deletePartials,
                std::move(ids), residualSelection ? selection.residualFirstFrame : 0,
                residualSelection ? selection.residualLastFrame
                                  : std::max(0, static_cast<int>(spectrumSnapshot_.frameCount) - 1),
                0.0f, {} });
            return true;
        }
        const auto onsetButton = chrome.onsetEditButton;
        if (hasOnsets
            && pointInRect(point, onsetButton))
        {
            spectrumOnsetEditing_ = !spectrumOnsetEditing_;
            redraw();
            return true;
        }
        if (pointInRect(point, modeChip))
        {
            showSpectrumModeMenu({ modeChip.x, modeChip.y + modeChip.height + 4.0f });
            return true;
        }
        if (pointInRect(point, chrome.envSourceAButton)
            || pointInRect(point, chrome.envSourceBButton))
        {
            const auto source = pointInRect(point, chrome.envSourceAButton) ? 1 : 2;
            const auto capabilities = spectrumCapabilities();
            const auto enabled = source == 1 ? capabilities.hasSourceA : capabilities.hasSourceB;
            if (!enabled)
                return true;
            shell_.spectrum().setSource(static_cast<SpectralTraceSource>(source));
            emitAction(SetSpectralViewAction { source, static_cast<int>(mode) });
            if (sink_.spectrumModeSelected)
                sink_.spectrumModeSelected(sink_.context, source, static_cast<int>(mode));
            redraw();
            return true;
        }
        if (pointInRect(point, chrome.zoomBar))
        {
            const auto position = std::clamp((point.x - chrome.zoomBar.x - 8.0f) / 188.0f,
                                              0.0f, 1.0f);
            const auto currentZoom = 1.0f / std::max(1.0e-6f,
                shell_.spectrum().navigation().timeEnd - shell_.spectrum().navigation().timeStart);
            const auto targetZoom = SpectrumGeometry::kMinZoom
                + position * (SpectrumGeometry::kMaxZoom - SpectrumGeometry::kMinZoom);
            shell_.spectrum().zoom(targetZoom / currentZoom, 0.5f, 0.5f);
            redraw();
            return true;
        }
        if (pointInRect(point, chrome.timeScrollbar))
        {
            const auto center = chrome.timeScrollbar.width > 0.0f
                ? (point.x - chrome.timeScrollbar.x) / chrome.timeScrollbar.width : 0.5f;
            shell_.spectrum().scrollTimeTo(center);
            redraw();
            return true;
        }
        if (pointInRect(point, chrome.frequencyScrollbar))
        {
            const auto center = chrome.frequencyScrollbar.height > 0.0f
                ? 1.0f - (point.y - chrome.frequencyScrollbar.y) / chrome.frequencyScrollbar.height : 0.5f;
            shell_.spectrum().scrollFrequencyTo(center);
            redraw();
            return true;
        }
        const auto& navigation = shell_.spectrum().navigation();
        const auto residual = mode == SpectralTraceMode::residual
                           || mode == SpectralTraceMode::isometricResidual;
        const auto envelope = mode == SpectralTraceMode::isometricEnvelope
                           || mode == SpectralTraceMode::isometricEnvelopeRdc;
        if (envelope && navigation.envelopeEditing)
        {
            const auto plot = SpectrumRenderer::getPlotBounds(well, spectrumSnapshot_);
            const UiRect range {
                plot.x + plot.width * navigation.envelopeTimeStart,
                plot.y + plot.height * (1.0f - navigation.envelopeFrequencyEnd),
                plot.width * (navigation.envelopeTimeEnd - navigation.envelopeTimeStart),
                plot.height * (navigation.envelopeFrequencyEnd - navigation.envelopeFrequencyStart) };
            if (pointInRect(point, range))
            {
                spectrumScaleDragging_ = true;
                spectrumScaleStartY_ = point.y;
                spectrumScaleFactor_ = 1.0f;
                spectrumMoved_ = false;
                shell_.spectrum().setPointerCaptured(true);
                requestKeyboardFocus();
                return true;
            }
        }
        const auto selectionAvailable = residual ? (!navigation.selectedBands.empty() || navigation.selectedBand >= 0)
                                                 : (!navigation.selectedPartials.empty() || navigation.selectedPartial >= 0);
        for (const auto& control : SpectrumChromeLayout::toolbarControls(well, mode))
        {
            if (!pointInRect(point, control.bounds))
                continue;
            if (control.kind == SpectrumChromeLayout::ToolbarControlKind::editor)
                return true; // the dB field is a readback/editor slot; edits use the draw path.
            auto ids = residual ? navigation.selectedBands : navigation.selectedPartials;
            if (ids.empty())
            {
                const auto selected = residual ? navigation.selectedBand : navigation.selectedPartial;
                if (selected >= 0)
                    ids.push_back(selected);
            }
            if (ids.empty())
                return true;
            const auto value = control.bounds.width > 0.0f
                ? std::clamp((point.x - control.bounds.x) / control.bounds.width, 0.0f, 1.0f)
                : 0.5f;
            const auto controlCommand = envelope
                ? SpectrumEditCommand::smoothPartialEnvelope
                : (residual ? SpectrumEditCommand::tiltResidual
                            : SpectrumEditCommand::tiltPartialEnvelope);
            emitAction(SpectrumEditAction { controlCommand, std::move(ids),
                                            residual ? navigation.residualFirstFrame : 0,
                                            residual ? navigation.residualLastFrame
                                                     : static_cast<int>(std::max(1u, spectrumSnapshot_.frameCount)) - 1,
                                            value, {} });
            return true;
        }
        for (const auto& item : SpectrumChromeLayout::toolbarItems(well, mode, selectionAvailable))
        {
            if (!pointInRect(point, item.bounds)) continue;
            if (!item.enabled) return true;
            if (item.command == SpectrumEditCommand::drawResidual
                || item.command == SpectrumEditCommand::drawPartialFrequency)
            {
                spectrumDrawTool_ = item.command;
                spectrumDrawing_ = false;
                spectrumDrawPoints_.clear();
                redraw();
                return true;
            }
            if (item.command == SpectrumEditCommand::prunePartials)
            {
                const auto prune = shell_.spectrum().runPartialPrune(10.0f);
                shell_.spectrum().setSelectedPartials(prune.bands);
                redraw();
                return true;
            }
            auto ids = residual ? navigation.selectedBands : navigation.selectedPartials;
            if (ids.empty())
            {
                const auto selected = residual ? navigation.selectedBand : navigation.selectedPartial;
                if (selected >= 0) ids.push_back(selected);
            }
            const auto frameCount = std::max(1u, spectrumSnapshot_.frameCount);
            const auto tilt = item.command == SpectrumEditCommand::tiltResidual
                           || item.command == SpectrumEditCommand::tiltPartialEnvelope;
            emitAction(SpectrumEditAction { item.command, std::move(ids),
                                            residual ? navigation.residualFirstFrame : 0,
                                            residual ? navigation.residualLastFrame
                                                     : static_cast<int>(frameCount) - 1,
                                            tilt ? 1.0f : 0.0f, {} });
            return true;
        }
        if (isSpectrumPoint(point, well) && (middle || alt))
        {
            shell_.spectrum().setGeometry(well);
            if (!pointInRect(point, shell_.spectrum().interactionPlotBounds()))
                return true;
            shell_.spectrum().beginPan({ point.x, point.y });
            spectrumPanning_ = true;
            spectrumMoved_ = false;
            shell_.spectrum().setPointerCaptured(true);
            requestKeyboardFocus();
            return true;
        }
        if (isSpectrumPoint(point, well))
        {
            shell_.spectrum().setGeometry(well);
            auto onset = spectrumOnsetEditing_
                ? shell_.spectrum().onsetHandleAtPoint({ point.x, point.y }, 7.0f)
                : OnsetHandle {};
            if (onset.kind != OnsetHandleKind::none)
            {
                if (spectrumSnapshot_.onsets.empty())
                    for (const auto value : spectrumSnapshot_.onsetNorms)
                        spectrumSnapshot_.onsets.push_back({ value, value, value, 0.0f });
                if (onset.index >= 0 && onset.index < static_cast<int>(spectrumSnapshot_.onsets.size()))
                {
                    if (doubleClick)
                    {
                        spectrumSnapshot_.onsets.erase(
                            spectrumSnapshot_.onsets.begin() + onset.index);
                        emitAction(EditSmearnovOnsetHandlesAction { spectrumSnapshot_.onsets });
                        shell_.spectrum().setPointerCaptured(false);
                        redraw();
                        return true;
                    }
                    spectrumOnsetDragging_ = true;
                    spectrumOnsetHandle_ = onset;
                    spectrumOnsetOriginal_ = spectrumSnapshot_.onsets;
                    shell_.spectrum().setPointerCaptured(true);
                    requestKeyboardFocus();
                    return true;
                }
            }
            if (doubleClick && (mode == SpectralTraceMode::residual
                                || mode == SpectralTraceMode::isometricResidual))
            {
                const auto band = shell_.spectrum().residualBandForPoint({ point.x, point.y });
                if (band >= 0)
                {
                    emitAction(SpectrumEditAction { SpectrumEditCommand::readResidual,
                                                    { band },
                                                    shell_.spectrum().frameForPoint(
                                                        { point.x, point.y }),
                                                    shell_.spectrum().frameForPoint(
                                                        { point.x, point.y }), 0.0f, {} });
                    return true;
                }
            }
            if (spectrumDrawTool_ == SpectrumEditCommand::drawResidual
                || spectrumDrawTool_ == SpectrumEditCommand::drawPartialFrequency)
            {
                const auto drawResidual = spectrumDrawTool_ == SpectrumEditCommand::drawResidual;
                const auto hit = drawResidual
                    ? shell_.spectrum().residualBandForPoint({ point.x, point.y })
                    : shell_.spectrum().findNearestPartial({ point.x, point.y }, 8.0f);
                if (hit < 0)
                    return true;
                if (drawResidual)
                    shell_.spectrum().selectBand(hit);
                else
                    shell_.spectrum().selectPartial(hit);
                spectrumDrawBand_ = hit;
                spectrumDrawFirstFrame_ = shell_.spectrum().frameForPoint({ point.x, point.y });
                spectrumDrawLastFrame_ = spectrumDrawFirstFrame_;
                spectrumDrawPoints_.push_back({ spectrumDrawFirstFrame_,
                                                 spectrumDrawValue(point, well, drawResidual) });
                spectrumDrawing_ = true;
                spectrumMoved_ = false;
                shell_.spectrum().setPointerCaptured(true);
                requestKeyboardFocus();
                return true;
            }
            const auto residualMode = mode == SpectralTraceMode::residual
                                   || mode == SpectralTraceMode::isometricResidual;
            const auto hit = residualMode
                ? shell_.spectrum().residualBandForPoint({ point.x, point.y })
                : shell_.spectrum().findNearestPartial({ point.x, point.y }, 8.0f);
            if (hit < 0)
            {
                shell_.spectrum().beginLasso({ point.x, point.y });
                spectrumLasso_ = true;
                spectrumResidualLasso_ = residualMode;
                spectrumEnvelopeLasso_ = mode == SpectralTraceMode::isometricEnvelope
                                      || mode == SpectralTraceMode::isometricEnvelopeRdc;
                spectrumLassoAdditive_ = shift || command;
            }
            else
            {
                if (residualMode)
                {
                    const auto& selected = shell_.spectrum().navigation().selectedBands;
                    const auto alreadySelected = std::find(selected.begin(), selected.end(), hit)
                                               != selected.end();
                    if (shift || command)
                    {
                        shell_.spectrum().toggleBandSelection(hit);
                        spectrumDragging_ = false;
                    }
                    else if (alreadySelected)
                    {
                        spectrumScaleDragging_ = true;
                        spectrumScaleStartY_ = point.y;
                        spectrumScaleFactor_ = 1.0f;
                    }
                    else
                        shell_.spectrum().selectBand(hit);
                }
                else
                {
                    const auto& selected = shell_.spectrum().navigation().selectedPartials;
                    const auto alreadySelected = std::find(selected.begin(), selected.end(), hit)
                                               != selected.end();
                    if (shift || command)
                    {
                        shell_.spectrum().togglePartialSelection(hit);
                        spectrumDragging_ = false;
                    }
                    else if (alreadySelected)
                    {
                        spectrumScaleDragging_ = true;
                        spectrumScaleStartY_ = point.y;
                        spectrumScaleFactor_ = 1.0f;
                    }
                    else
                        shell_.spectrum().selectPartial(hit);
                }
                spectrumDragging_ = !spectrumScaleDragging_;
            }
            spectrumMoved_ = false;
            spectrumLastX_ = point.x;
            spectrumLastY_ = point.y;
            shell_.spectrum().setPointerCaptured(true);
            requestKeyboardFocus();
            return true;
        }
        const auto buttons = timelineButtons(timeline);
        if (pointInRect(point, buttons[0].second))
        {
            emitAction(DetectSmearnovAction {});
            return true;
        }
        if (pointInRect(point, buttons[1].second))
        {
            emitAction(ApplySmearnovAction {});
            return true;
        }
        if (pointInRect(point, timeline))
        {
            const auto track = timelineTrack(timeline);
            const auto normalized =
                std::clamp(track.width > 0.0f ? (point.x - track.x) / track.width : 0.0f, 0.0f,
                           1.0f);
            shell_.session().setPlayhead(normalized);
            emitAction(shell_.session().seekAction());
            shell_.session().setTimeline({ 0.0f, normalized, 1.0f });
            emitAction(shell_.session().timelineAction());
            return true;
        }
        return false;
    }

    void sessionCentreMove(visage::Point point, UiRect well)
    {
        if (!isSpectrumPoint(point, well))
        {
            spectrumHoverPartial_ = -1;
            return;
        }
        shell_.spectrum().setGeometry(well);
        const auto residual = shell_.spectrum().mode() == SpectralTraceMode::residual
                           || shell_.spectrum().mode() == SpectralTraceMode::isometricResidual;
        const auto next = residual ? -1
                                   : shell_.spectrum().findNearestPartial({ point.x, point.y }, 8.0f);
        const auto nextBand = residual
            ? shell_.spectrum().residualBandForPoint({ point.x, point.y }) : -1;
        if (next != spectrumHoverPartial_ || nextBand != shell_.spectrum().navigation().hoveredBand)
        {
            spectrumHoverPartial_ = next;
            shell_.spectrum().setHoveredBand(nextBand);
            redraw();
        }
    }

    void sessionCentreDrag(visage::Point point, UiRect well)
    {
        if (well.width <= 0.0f || well.height <= 0.0f)
            return;
        if (spectrumOnsetDragging_)
        {
            const auto index = spectrumOnsetHandle_.index;
            if (index >= 0 && index < static_cast<int>(spectrumSnapshot_.onsets.size()))
            {
                auto& onset = spectrumSnapshot_.onsets[static_cast<std::size_t>(index)];
                const auto norm = shell_.spectrum().timeNormFromScreenPoint({ point.x, point.y });
                switch (spectrumOnsetHandle_.kind)
                {
                case OnsetHandleKind::marker:
                    onset.norm = norm;
                    onset.preNorm = std::min(onset.preNorm, onset.norm);
                    onset.peakNorm = std::max(onset.peakNorm, onset.norm);
                    break;
                case OnsetHandleKind::leftEdge:
                    onset.preNorm = std::min(norm, onset.norm);
                    break;
                case OnsetHandleKind::rightEdge:
                    onset.peakNorm = std::max(norm, onset.norm);
                    break;
                case OnsetHandleKind::none:
                    break;
                }
                spectrumMoved_ = true;
                redraw();
            }
            return;
        }
        if (spectrumScaleDragging_)
        {
            spectrumScaleFactor_ = std::clamp(
                std::exp((spectrumScaleStartY_ - point.y) / 100.0f), 0.05f, 20.0f);
            spectrumMoved_ = spectrumMoved_ || std::abs(spectrumScaleFactor_ - 1.0f) > 0.001f;
            redraw();
            return;
        }
        if (spectrumDrawing_)
        {
            const bool drawResidual = spectrumDrawTool_ == SpectrumEditCommand::drawResidual;
            const auto frame = shell_.spectrum().frameForPoint({ point.x, point.y });
            const auto value = spectrumDrawValue(point, well, drawResidual);
            if (spectrumDrawPoints_.empty()
                || frame != spectrumDrawPoints_.back().first
                || std::abs(value - spectrumDrawPoints_.back().second) > 0.001f)
                spectrumDrawPoints_.push_back({ frame, value });
            spectrumDrawFirstFrame_ = std::min(spectrumDrawFirstFrame_, frame);
            spectrumDrawLastFrame_ = std::max(spectrumDrawLastFrame_, frame);
            spectrumMoved_ = true;
            redraw();
            return;
        }
        if (spectrumLasso_)
        {
            shell_.spectrum().updateLasso({ point.x, point.y });
            spectrumMoved_ = true;
            redraw();
            return;
        }
        if (spectrumPanning_)
        {
            shell_.spectrum().setGeometry(well);
            spectrumMoved_ = spectrumMoved_ || std::abs(spectrumLastX_ - point.x) > 1.0f
                || std::abs(spectrumLastY_ - point.y) > 1.0f;
            shell_.spectrum().updatePan({ point.x, point.y });
            spectrumLastX_ = point.x;
            spectrumLastY_ = point.y;
            redraw();
            return;
        }
        if (!spectrumDragging_)
            return;
        spectrumMoved_ = spectrumMoved_ || std::abs(spectrumLastX_ - point.x) > 1.0f ||
                         std::abs(spectrumLastY_ - point.y) > 1.0f;
        shell_.spectrum().setGeometry(well);
        spectrumHoverPartial_ = shell_.spectrum().findNearestPartial({ point.x, point.y }, 8.0f);
        // Ordinary selection drags never pan. Panning is entered only by the
        // middle button or Alt-drag branch above.
        spectrumLastX_ = point.x;
        spectrumLastY_ = point.y;
        redraw();
    }

    void sessionCentreUp(visage::Point point, UiRect well)
    {
        if (spectrumScaleDragging_)
        {
            (void) point;
            const auto mode = shell_.spectrum().mode();
            const auto residual = mode == SpectralTraceMode::residual
                               || mode == SpectralTraceMode::isometricResidual;
            const auto envelope = mode == SpectralTraceMode::isometricEnvelope
                               || mode == SpectralTraceMode::isometricEnvelopeRdc;
            auto ids = residual ? shell_.spectrum().navigation().selectedBands
                                : shell_.spectrum().navigation().selectedPartials;
            const auto& navigation = shell_.spectrum().navigation();
            if (envelope && ids.empty())
                for (const auto& trace : spectrumSnapshot_.partials)
                    ids.push_back(trace.partialIndex);
            else if (ids.empty())
            {
                const auto id = residual ? navigation.selectedBand : navigation.selectedPartial;
                if (id >= 0) ids.push_back(id);
            }
            if (spectrumMoved_ && !ids.empty())
            {
                const auto frameCount = std::max(1u, spectrumSnapshot_.frameCount);
                const auto firstFrame = envelope
                    ? static_cast<int>(std::floor(std::clamp(navigation.envelopeTimeStart,
                                                              0.0f, 1.0f)
                                                   * static_cast<float>(frameCount - 1)))
                    : 0;
                const auto lastFrame = envelope
                    ? static_cast<int>(std::ceil(std::clamp(navigation.envelopeTimeEnd,
                                                             0.0f, 1.0f)
                                                  * static_cast<float>(frameCount - 1)))
                    : static_cast<int>(frameCount) - 1;
                emitAction(SpectrumEditAction {
                    envelope ? SpectrumEditCommand::gainPartialEnvelope
                             : residual ? SpectrumEditCommand::scaleResidual
                                        : SpectrumEditCommand::scalePartials,
                    std::move(ids), firstFrame, std::max(firstFrame, lastFrame),
                    spectrumScaleFactor_, {} });
            }
            spectrumScaleDragging_ = false;
            spectrumScaleFactor_ = 1.0f;
            spectrumMoved_ = false;
            shell_.spectrum().setPointerCaptured(false);
            redraw();
            return;
        }
        if (spectrumOnsetDragging_)
        {
            (void) point;
            (void) well;
            std::sort(spectrumSnapshot_.onsets.begin(), spectrumSnapshot_.onsets.end(),
                      [](const auto& a, const auto& b) { return a.norm < b.norm; });
            emitAction(EditSmearnovOnsetHandlesAction { spectrumSnapshot_.onsets });
            spectrumOnsetDragging_ = false;
            spectrumOnsetHandle_ = {};
            spectrumOnsetOriginal_.clear();
            shell_.spectrum().setPointerCaptured(false);
            redraw();
            return;
        }
        if (spectrumDrawing_)
        {
            (void) point;
            (void) well;
            const auto command = spectrumDrawTool_;
            std::vector<int> ids { spectrumDrawBand_ };
            if (spectrumDrawPoints_.size() >= 1 && spectrumDrawBand_ >= 0)
                emitAction(SpectrumEditAction { command, std::move(ids),
                                                spectrumDrawFirstFrame_, spectrumDrawLastFrame_,
                                                spectrumDrawPoints_.back().second,
                                                spectrumDrawPoints_ });
            spectrumDrawing_ = false;
            spectrumDrawPoints_.clear();
            spectrumDrawBand_ = -1;
            shell_.spectrum().setPointerCaptured(false);
            redraw();
            return;
        }
        if (spectrumLasso_)
        {
            spectrumLasso_ = false;
            if (spectrumResidualLasso_)
            {
                const auto rect = shell_.spectrum().navigation().lassoBounds;
                const auto bands = shell_.spectrum().residualBandsForRect(rect);
                const auto first = shell_.spectrum().frameForPoint(
                    { rect.x, rect.y + rect.height * 0.5f });
                const auto last = shell_.spectrum().frameForPoint(
                    { rect.x + rect.width, rect.y + rect.height * 0.5f });
                if (spectrumLassoAdditive_)
                {
                    auto selected = shell_.spectrum().navigation().selectedBands;
                    selected.insert(selected.end(), bands.begin(), bands.end());
                    shell_.spectrum().selectResidualRange(std::move(selected), std::min(first, last),
                                                          std::max(first, last));
                }
                else
                    shell_.spectrum().selectResidualRange(bands, std::min(first, last),
                                                          std::max(first, last));
            }
            else if (spectrumEnvelopeLasso_)
            {
                const auto rect = shell_.spectrum().navigation().lassoBounds;
                const auto plot = SpectrumRenderer::getPlotBounds(well, spectrumSnapshot_);
                const auto timeStart = plot.width > 0.0f
                    ? (rect.x - plot.x) / plot.width : 0.0f;
                const auto timeEnd = plot.width > 0.0f
                    ? (rect.x + rect.width - plot.x) / plot.width : 0.0f;
                const auto freqStart = plot.height > 0.0f
                    ? 1.0f - (rect.y + rect.height - plot.y) / plot.height : 0.0f;
                const auto freqEnd = plot.height > 0.0f
                    ? 1.0f - (rect.y - plot.y) / plot.height : 0.0f;
                shell_.spectrum().setEnvelopeRange(timeStart, timeEnd,
                                                   freqStart, freqEnd);
            }
            else
                shell_.spectrum().endLasso(spectrumLassoAdditive_);
            spectrumResidualLasso_ = false;
            spectrumEnvelopeLasso_ = false;
            spectrumLassoAdditive_ = false;
            shell_.spectrum().setPointerCaptured(false);
            redraw();
            return;
        }
        if (spectrumPanning_)
        {
            spectrumPanning_ = false;
            shell_.spectrum().endPan();
            shell_.spectrum().setPointerCaptured(false);
            redraw();
            return;
        }
        if (!spectrumDragging_)
            return;
        spectrumDragging_ = false;
        shell_.spectrum().setPointerCaptured(false);
        if (spectrumMoved_)
            return;
        shell_.spectrum().setGeometry(well);
        const auto frame = shell_.spectrum().frameForX(point.x);
        if (shell_.spectrum().mode() == SpectralTraceMode::residual ||
            shell_.spectrum().mode() == SpectralTraceMode::isometricResidual)
        {
            shell_.spectrum().setGeometry(well);
            const auto band = shell_.spectrum().residualBandForPoint({ point.x, point.y });
            shell_.spectrum().selectBand(band);
            // A click changes selection only. Residual editing is owned by the
            // toolbar/action path, so a hit cannot mutate the model.
        }
        else
        {
            shell_.spectrum().setGeometry(well);
            const auto partial = shell_.spectrum().findNearestPartial({ point.x, point.y }, 8.0f);
            shell_.spectrum().selectPartial(partial);
            (void) frame;
        }
        redraw();
    }

    bool sessionCentreWheel(visage::Point point, float deltaX, float deltaY, UiRect well,
                            bool shift, bool command, bool alt, bool precise)
    {
        (void) precise;
        shell_.spectrum().setGeometry(well);
        const auto plot = shell_.spectrum().interactionPlotBounds();
        if (!pointInRect(point, plot))
            return false;
        if (deltaX == 0.0f && deltaY == 0.0f)
            return true;
        if (command)
        {
            const auto zoomDelta = std::abs(deltaY) >= std::abs(deltaX) ? deltaY : deltaX;
            if (zoomDelta == 0.0f)
                return true;
            const auto factor = std::pow(1.12f, zoomDelta);
            if (shift)
                shell_.spectrum().zoomAtScreenPoint({ point.x, point.y }, factor, 1.0f);
            else if (alt)
                shell_.spectrum().zoomAtScreenPoint({ point.x, point.y }, 1.0f, factor);
            else
                shell_.spectrum().zoomAtScreenPoint({ point.x, point.y }, factor, factor);
            redraw();
            return true;
        }
        float scrollX = deltaX;
        float scrollY = deltaY;
        const auto& nav = shell_.spectrum().navigation();
        const auto iso = SpectrumGeometry::isIsometricDisplayMode(shell_.spectrum().mode());
        const auto canPanHorizontally = iso
            ? (nav.frequencyEnd - nav.frequencyStart) < 0.999f
            : (nav.timeEnd - nav.timeStart) < 0.999f;
        const auto canPanVertically = iso
            ? (nav.timeEnd - nav.timeStart) < 0.999f
            : (nav.frequencyEnd - nav.frequencyStart) < 0.999f;
        if (shift && scrollX == 0.0f)
        {
            scrollX = scrollY;
            scrollY = 0.0f;
        }
        else if (canPanHorizontally && !canPanVertically && scrollX == 0.0f)
        {
            scrollX = scrollY;
            scrollY = 0.0f;
        }
        else if (!canPanHorizontally && canPanVertically && scrollY == 0.0f)
        {
            scrollY = scrollX;
            scrollX = 0.0f;
        }
        const auto pixelsX = scrollX * SpectrumGeometry::kWheelPanPixelsPerDelta;
        const auto pixelsY = scrollY * SpectrumGeometry::kWheelPanPixelsPerDelta;
        const auto timeLength = nav.timeEnd - nav.timeStart;
        const auto freqLength = nav.frequencyEnd - nav.frequencyStart;
        if (iso)
            shell_.spectrum().pan(pixelsY / plot.height * timeLength,
                                   -pixelsX / plot.width * freqLength);
        else
            shell_.spectrum().pan(-pixelsX / plot.width * timeLength,
                                  pixelsY / plot.height * freqLength);
        redraw();
        return true;
    }

    bool sessionCentreMagnify(visage::Point point, float scaleFactor, UiRect well)
    {
        shell_.spectrum().setGeometry(well);
        if (!std::isfinite(scaleFactor) || scaleFactor <= 0.0f
            || spectrumSnapshot_.frameCount == 0)
            return false;
        if (!pointInRect(point, shell_.spectrum().interactionPlotBounds()))
            return false;
        if (!shell_.spectrum().zoomAtScreenPoint({ point.x, point.y }, scaleFactor, scaleFactor))
            return false;
        redraw();
        return true;
    }

    [[nodiscard]] static bool pointInRect(visage::Point point, UiRect r) noexcept
    {
        return point.x >= r.x && point.x < r.x + r.width && point.y >= r.y &&
               point.y < r.y + r.height;
    }

    bool receivesTextInput() override { return true; }

    bool keyPress(const visage::KeyEvent& event) override
    {
        if (!event.key_down || shell_.state().page != EditorPage::session)
            return false;
        int key = static_cast<int>(event.keyCode());
        if (event.keyCode() == visage::KeyCode::Delete)
            key = SpectrumInteractionModel::kKeyDelete;
        else if (event.keyCode() == visage::KeyCode::Backspace)
            key = SpectrumInteractionModel::kKeyBackspace;
        else if (event.keyCode() == visage::KeyCode::Escape)
            key = SpectrumInteractionModel::kKeyEscape;
        else if (event.keyCode() == visage::KeyCode::Home)
            key = SpectrumInteractionModel::kKeyHome;
        else if (event.keyCode() == visage::KeyCode::Return)
            key = SpectrumInteractionModel::kKeyReturn;
        const auto action = shell_.spectrum().handleKey(key, event.isShiftDown(),
                                                        event.isMainModifier());
        if (action)
            emitAction(*action);
        redraw();
        return action.has_value() || key == SpectrumInteractionModel::kKeyEscape
            || key == SpectrumInteractionModel::kKeyFit || key == SpectrumInteractionModel::kKeyHome;
    }

    void textInput(const std::string& text) override
    {
        if (sink_.textInput)
            sink_.textInput(sink_.context, text.c_str());
    }

    void focusChanged(bool focused, bool) override
    {
        if (sink_.focusChanged)
            sink_.focusChanged(sink_.context, focused);
    }

private:
    // The well's geometry now comes from AtsSessionPage's centre column, so
    // every spectrum coordinate is resolved against the rect it was drawn in.
    [[nodiscard]] static bool isSpectrumPoint(const visage::Point& point, UiRect well) noexcept
    {
        return point.x >= well.x && point.x < well.x + well.width && point.y >= well.y &&
               point.y < well.y + well.height;
    }

    [[nodiscard]] float spectrumDrawValue(const visage::Point& point, UiRect well,
                                           bool residual) const noexcept
    {
        const auto norm = shell_.spectrum().freqNormFromScreenPoint({ point.x, point.y });
        if (residual)
            return -72.0f + norm * 72.0f;
        const auto minHz = std::max(1.0, spectrumSnapshot_.minFrequencyHz);
        const auto maxHz = std::max(minHz * 1.2, spectrumSnapshot_.maxFrequencyHz);
        return minHz * std::pow(maxHz / minHz, norm);
    }

    void updateSourceDropTargets()
    {
        const auto page = shell_.state().page;
        const auto sessionVisible = page == EditorPage::session;
        for (auto* target : sourceDropTargets_)
            if (target != nullptr)
                target->setVisible(sessionVisible);
        const auto analysisVisible = page == EditorPage::analysis;
        for (auto* target : analysisDropTargets_)
            if (target != nullptr)
                target->setVisible(analysisVisible);
        if (!sessionVisible && !analysisVisible)
            sourceDropActive_.fill(false);
    }

    void updateSourceDropTargetBounds(UiRect body)
    {
        if (sessionPageRaw_ == nullptr)
            return;
        const auto pageOffset = ThemeLayout::pageArea({ 0.0f, 0.0f, body.width, body.height });
        for (int slot = 0; slot < 2; ++slot)
        {
            auto* target = sourceDropTargets_[static_cast<std::size_t>(slot)];
            if (target == nullptr)
                continue;
            const UiRect card = sessionPageRaw_->sourceCardBounds(slot);
            // Exclude the ANALYZE chip zone (bottom 26px of the card) so clicks
            // on that button pass through to the session page unobstructed.
            target->setBounds(body.x + pageOffset.x + card.x,
                              body.y + pageOffset.y + card.y,
                              card.width, card.height - 26.0f);
        }
        updateAnalysisDropTargetBounds(body);
    }

    void updateAnalysisDropTargetBounds(UiRect body)
    {
        if (analysisPageRaw_ == nullptr)
            return;
        const auto pageOffset = ThemeLayout::pageArea({ 0.0f, 0.0f, body.width, body.height });
        for (int slot = 0; slot < 2; ++slot)
        {
            auto* target = analysisDropTargets_[static_cast<std::size_t>(slot)];
            if (target == nullptr)
                continue;
            const UiRect card = analysisPageRaw_->sourceCardBounds(slot);
            if (card.width <= 0.0f || card.height <= 0.0f)
                continue;
            target->setBounds(body.x + pageOffset.x + card.x,
                              body.y + pageOffset.y + card.y,
                              card.width, card.height);
        }
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
        {
            emitAction(ImportWavAction { drop->path, drop->slot });
            emitAction(AnalyzeMorphSourceAction { drop->slot });
        }
        else
            emitAction(ImportAtsAction { drop->path, drop->slot });
        requestKeyboardFocus();
        redraw();
    }

    [[nodiscard]] const char* spectrumModeLabel() const noexcept
    {
        const auto items = shell_.spectrum().menu(spectrumCapabilities());
        const auto mode = shell_.spectrum().mode();
        for (const auto& item : items)
            if (item.mode == mode)
                return item.label.data();
        return "Trace + Residual";
    }

    [[nodiscard]] SpectrumCapabilities spectrumCapabilities() const noexcept
    {
        return { sourceAvailable_[0], spectrumSnapshot_.hasResidual(),
                 sourceAvailable_[1], sourceAvailable_[2],
                 sourceNames_[0].empty() ? std::string_view { "Current" } : std::string_view { sourceNames_[0] },
                 sourceNames_[1].empty() ? std::string_view { "Source A" } : std::string_view { sourceNames_[1] },
                 sourceNames_[2].empty() ? std::string_view { "Source B" } : std::string_view { sourceNames_[2] },
                 sourceResidualAvailable_ };
    }

    void runSourceImportDialog(NativeFileDialogKind kind, int slot)
    {
        const NativeFileDialogRequest request { kind, dialogParent_, {}};
        if (sink_.dialogRequested)
            sink_.dialogRequested(sink_.context, static_cast<int>(request.kind));
        const auto result = detail::runNativeFileDialog(request);
        if (sink_.dialogResolved)
            sink_.dialogResolved(sink_.context, static_cast<int>(request.kind),
                                 result.path.c_str(), result.accepted);
        if ((result.status == NativeFileDialogStatus::initializationFailed ||
             result.status == NativeFileDialogStatus::failed) && sink_.dialogError)
            sink_.dialogError(sink_.context, static_cast<int>(request.kind),
                              "native_dialog_failed", result.error.c_str());
        if (const auto action = FileDialogModel::resolve(kind, result, slot))
        {
            emitAction(*action);
            if (kind == NativeFileDialogKind::importWav)
                emitAction(AnalyzeMorphSourceAction { slot });
        }
    }

    // The anchor arrives in page-local coordinates; the popup is shown on the
    // editor root, so it has to be walked back up through the page container.
    void showAnalysisProfileMenu(UiRect anchor)
    {
        float x = anchor.x;
        float y = anchor.y + anchor.height;
        if (pageContainerRaw_ != nullptr)
        {
            const auto area = ThemeLayout::pageArea({ 0.0f, 0.0f, pageContainerRaw_->width(),
                                                      pageContainerRaw_->height() });
            x += pageContainerRaw_->x() + area.x;
            y += pageContainerRaw_->y() + area.y;
        }

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
                if (analysisPageRaw_ != nullptr)
                    analysisPageRaw_->setProfile(optionId);
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
        popup.show(this, { x, y });
    }

    void emitAction(const EditorAction& action)
    {
        if (sink_.action)
            sink_.action(sink_.context, &action);
    }

    // Page frames own their own gestures, so unlike the shell's emit*Gesture
    // helpers they arrive as a finished action. The C parameterGesture sink is
    // fed from here so a host sees one callback per gesture whichever surface
    // produced it.
    void emitPageAction(const EditorAction& action)
    {
        emitAction(action);
        const auto* parameter = std::get_if<SetParameterAction>(&action);
        if (parameter == nullptr)
            return;
        if (sink_.parameterGesture)
            sink_.parameterGesture(sink_.context, parameter->id.c_str(), parameter->value,
                                   parameter->begin, parameter->end);
        mirrorTimbre(parameter->id);
    }

    // The timbre curve overlay reads its three scalars from the session model,
    // not the registry, so a page edit has to be copied across.
    void mirrorTimbre(std::string_view id)
    {
        if (sessionPageRaw_ == nullptr || (id != "timbreBrightnessDb" &&
                                           id != "timbreTiltDbPerOct" && id != "timbreNoiseBalance"))
            return;
        shell_.session().setTimbre(sessionPageRaw_->controlValue("timbreBrightnessDb"),
                                   sessionPageRaw_->controlValue("timbreTiltDbPerOct"),
                                   sessionPageRaw_->controlValue("timbreNoiseBalance"),
                                   shell_.session().state().timbre.curve);
    }

    [[nodiscard]] ThemeTokens visageTokens() const noexcept
    {
        return ThemeTokens::forTheme(shell_.state().theme);
    }

    EditorEventSink sink_{};
    std::array<SourceDropTarget*, 2> sourceDropTargets_ {};
    std::array<SourceDropTarget*, 2> analysisDropTargets_ {};
    std::array<bool, 2> sourceDropActive_ {};
    EditorShellModel shell_{};
    std::unique_ptr<AtsPageContainer> pageContainer_;
    AtsPageContainer* pageContainerRaw_ = nullptr;
    AtsSessionPage* sessionPageRaw_ = nullptr;
    AtsAnalysisPage* analysisPageRaw_ = nullptr;
    AtsSynthesisPage* synthesisPageRaw_ = nullptr;
    AtsPartialsPage* partialsPageRaw_ = nullptr;
    SpectralTraceSnapshot spectrumSnapshot_{};
    std::optional<SpectralTraceSnapshot> spectrumOverlaySnapshot_;
    std::array<std::string, 3> sourceNames_ {};
    std::array<bool, 3> sourceAvailable_ {};
    std::array<bool, 3> sourceResidualAvailable_ {};
    int spectrumHoverPartial_ = -1;
    void* dialogParent_ = nullptr;
    int analysisProfile_ = 0;
    TelemetrySnapshot telemetry_ {};
    bool spectrumDragging_ = false;
    bool spectrumPanning_ = false;
    bool spectrumScaleDragging_ = false;
    float spectrumScaleStartY_ = 0.0f;
    float spectrumScaleFactor_ = 1.0f;
    bool spectrumLasso_ = false;
    bool spectrumLassoAdditive_ = false;
    bool spectrumResidualLasso_ = false;
    bool spectrumEnvelopeLasso_ = false;
    bool spectrumDrawing_ = false;
    bool spectrumOnsetEditing_ = false;
    bool spectrumOnsetDragging_ = false;
    OnsetHandle spectrumOnsetHandle_{};
    std::vector<SpectralOnset> spectrumOnsetOriginal_;
    SpectrumEditCommand spectrumDrawTool_ = SpectrumEditCommand::copyResidual;
    int spectrumDrawBand_ = -1;
    int spectrumDrawFirstFrame_ = 0;
    int spectrumDrawLastFrame_ = 0;
    std::vector<std::pair<std::int32_t, float>> spectrumDrawPoints_;
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
        // Returning true lets a native standalone window close and allows its
        // event loop to stop after the last window disappears. Embedded plugin
        // views do not own their host window, so accepting the callback is also
        // the correct response when the host itself is shutting down.
        app.onCloseRequested() = [] { return true; };
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
        detail::removeNativeMagnifyMonitor(magnifyMonitor);
        app.close();
    }

    static void nativeMagnify(void* context, float x, float y, float scaleFactor)
    {
        auto* self = static_cast<Impl*>(context);
        if (self != nullptr)
            self->frame.handleMagnify({ x, y }, scaleFactor);
    }

    void updateMagnifyMonitor()
    {
        detail::removeNativeMagnifyMonitor(magnifyMonitor);
        magnifyMonitor = nullptr;
        if (app.window() == nullptr)
            return;
        magnifyMonitor = detail::installNativeMagnifyMonitor(
            app.window()->nativeHandle(), this, &Impl::nativeMagnify,
            static_cast<float>(width), static_cast<float>(height));
    }

    EditorEventSink sink{};
    visage::ApplicationWindow app;
    AtsProductionEditorShell frame { sink };
    std::uint32_t width = 1280;
    std::uint32_t height = 940;
    double scale = 1.0;
    bool attached = false;
    bool visible = false;
    void* magnifyMonitor = nullptr;
};

VisageEditor::VisageEditor(EditorEventSink sink) : impl_(std::make_unique<Impl>(sink)) {}

VisageEditor::~VisageEditor() = default;

void VisageEditor::setTheme(ThemeId theme)
{
    impl_->frame.setTheme(theme);
}

void VisageEditor::selectPage(int page)
{
    if (page >= 0)
        impl_->frame.selectPage(static_cast<std::size_t>(page));
}

bool VisageEditor::captureScreenshot(const std::string& path)
{
    // Composites the canvas as last rendered; callers must let the window draw
    // (spin the run loop) after changing state and before capturing.
    if (impl_->app.window() == nullptr)
        return false;
    const auto& screenshot = impl_->app.takeScreenshot();
    if (screenshot.width() <= 0 || screenshot.height() <= 0)
        return false;
    screenshot.save(path);
    return true;
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

void VisageEditor::setEditorSnapshot(EditorSnapshot snapshot)
{
    impl_->frame.setEditorSnapshot(std::move(snapshot));
}

void VisageEditor::setSpectrumSnapshot(SpectralTraceSnapshot snapshot)
{
    impl_->frame.setSpectrumSnapshot(std::move(snapshot));
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
    impl_->updateMagnifyMonitor();
    return true;
}

bool VisageEditor::canResize() const { return true; }

EditorSizeLimits VisageEditor::sizeLimits() const { return {}; }

EditorSize VisageEditor::constrainSize(EditorSize requested) const
{
    const auto lim = sizeLimits();
    return {
        std::clamp(requested.width,  lim.minWidth,  lim.maxWidth),
        std::clamp(requested.height, lim.minHeight, lim.maxHeight)
    };
}

void VisageEditor::setScale(double scale)
{
    impl_->scale = std::clamp(scale, 0.5, 4.0);
    impl_->app.setDpiScale(static_cast<float>(impl_->scale));
    detail::setNativeViewFrame(nativeHandle(), static_cast<float>(impl_->width / impl_->scale),
                               static_cast<float>(impl_->height / impl_->scale));
    impl_->updateMagnifyMonitor();
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
    impl_->updateMagnifyMonitor();
    return impl_->app.window() != nullptr;
}

bool VisageEditor::showStandalone()
{
    // A standalone window is resized by the user directly, so the size limits
    // hosts negotiate through the wrappers are enforced on the window itself.
    const auto limits = sizeLimits();
    impl_->app.setMinimumDimensions(static_cast<float>(limits.minWidth),
                                    static_cast<float>(limits.minHeight));
    if (!impl_->app.window())
        impl_->app.show();
    else
        impl_->app.window()->show();
    detail::setNativeWindowContentLimits(nativeHandle(), static_cast<float>(limits.minWidth),
                                         static_cast<float>(limits.minHeight),
                                         static_cast<float>(limits.maxWidth),
                                         static_cast<float>(limits.maxHeight));
    impl_->attached = impl_->app.window() != nullptr;
    detail::setNativeViewFrame(nativeHandle(), static_cast<float>(impl_->width / impl_->scale),
                               static_cast<float>(impl_->height / impl_->scale));
    impl_->frame.setDialogParent(nativeHandle());
    impl_->visible = impl_->attached && impl_->app.isShowing();
    detail::setNativeViewHidden(nativeHandle(), false);
    impl_->updateMagnifyMonitor();
    return impl_->visible;
}

void VisageEditor::runStandaloneEventLoop()
{
    impl_->app.runEventLoop();
}

void VisageEditor::show()
{
    if (impl_->app.window())
    {
        impl_->app.window()->show();
        impl_->visible = true;
        detail::setNativeViewHidden(nativeHandle(), false);
        impl_->updateMagnifyMonitor();
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
