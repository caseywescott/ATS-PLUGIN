#include "VisageEditor.h"
#include "EditorShell.h"
#include "AtsEditorPages.h"
#include "FileDialogModel.h"
#include "SessionFileDrop.h"
#include "SpectrumSelectionPanel.h"
#include "CurveEditorModel.h"
#include "VisageNativeMac.h"
#include "EditorPreferences.h"
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
        // Select-by-criteria panel: a child of the SESSION page so it shares
        // the well's page-local coordinates and hides with the page.
        auto selectionPanel = std::make_unique<SpectrumSelectionPanel>(
            [this](const PartialCriteria& criteria, bool add) { applyPartialCriteria(criteria, add); },
            [this] { setSelectionPanelVisible(false); }, theme);
        selectionPanelRaw_ = selectionPanel.get();
        selectionPanel->setVisible(false);
        sessionPage->addChild(std::move(selectionPanel));
        pageContainer_->addPage(std::move(sessionPage));
        auto analysisPage = std::make_unique<AtsAnalysisPage>(
            shell_.analysis(), [this](const EditorAction& action) { emitPageAction(action); }, theme);
        analysisPageRaw_ = analysisPage.get();
        analysisPage->setChromeHandlers(
            // ANALYSIS works on the current model (slot 3) — the one SESSION's
            // CURRENT audition plays and PARTIALS/MATH edit — not morph source A.
            [this](UiRect) { runSourceImportDialog(NativeFileDialogKind::importWav, 3); },
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
                        if (onThemeChosen_)
                            onThemeChosen_(shell_.state().theme);
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
        if (selectionPanelRaw_ != nullptr && selectionPanelRaw_->isVisible())
            positionSelectionPanel();
    }

    void selectTimelineCurve(int tab)
    {
        curveEditor_.setTab(static_cast<CurveTab>(std::clamp(tab, 0, 3)));
        redraw();
    }

    void setSelectionPanelVisible(bool visible)
    {
        if (selectionPanelRaw_ == nullptr)
            return;
        if (visible)
            positionSelectionPanel();
        selectionPanelRaw_->setVisible(visible);
        redraw();
    }

    // Top-left of the spectrum plot, inside the well.
    void positionSelectionPanel()
    {
        if (selectionPanelRaw_ == nullptr || sessionPageRaw_ == nullptr)
            return;
        const auto well = sessionPageRaw_->centreWell();
        const auto plot = SpectrumRenderer::getPlotBounds(well, spectrumSnapshot_);
        selectionPanelRaw_->setBounds(plot.x + 8.0f, plot.y + 8.0f,
                                      std::min(SpectrumSelectionPanel::kWidth, std::max(0.0f, plot.width - 16.0f)),
                                      SpectrumSelectionPanel::kHeight);
    }

    void applyPartialCriteria(const PartialCriteria& criteria, bool add)
    {
        const auto matched = matchPartials(spectrumSnapshot_, criteria);
        const auto& navigation = shell_.spectrum().navigation();
        auto current = navigation.selectedPartials;
        if (current.empty() && navigation.selectedPartial >= 0)
            current.push_back(navigation.selectedPartial);
        const auto selection = add ? unitePartials(std::move(current), matched) : matched;
        shell_.spectrum().setSelectedPartials(selection);
        std::string status = std::to_string(matched.size()) + " matched, "
            + std::to_string(selection.size()) + " selected";
        if (criteria.minSmrDb)
        {
            const auto unknown = std::count_if(spectrumSnapshot_.partials.begin(),
                                               spectrumSnapshot_.partials.end(),
                                               [](const SpectralTrace& t) { return std::isnan(t.meanSmrDb); });
            if (unknown > 0)
                status += " (" + std::to_string(unknown) + " without SMR)";
        }
        if (selectionPanelRaw_ != nullptr)
            selectionPanelRaw_->setStatus(std::move(status));
        redraw();
    }

    // Called when the user picks a theme from the menu (not for programmatic
    // setTheme), so preferences record only deliberate choices.
    [[nodiscard]] ThemeId theme() const noexcept { return shell_.state().theme; }

    void setThemeChosenHandler(std::function<void(ThemeId)> handler)
    { onThemeChosen_ = std::move(handler); }

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
        if (analysisPageRaw_ != nullptr)
            analysisPageRaw_->redraw();
        redraw();
    }

    void setTelemetry(TelemetrySnapshot snapshot)
    {
        telemetry_ = std::move(snapshot);
        shell_.setActivity(telemetry_.analysisRunning, telemetry_.renderRunning);
        shell_.analysis().setWorkerRunning(telemetry_.analysisRunning);
        if (analysisPageRaw_ != nullptr)
            analysisPageRaw_->redraw();
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
        if (analysisPageRaw_ != nullptr)
            analysisPageRaw_->redraw();
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
        {
            auto morph = CurveEditorModel::unflatten(snapshot.morphCurve);
            for (std::size_t i = 0; i < morph.size() && i < snapshot.morphCurveShapes.size(); ++i)
                morph[i].shape = static_cast<std::uint8_t>(std::min<int>(snapshot.morphCurveShapes[i], 3));
            curveEditor_.setPoints(CurveTab::morph, std::move(morph));
        }
        curveEditor_.setPoints(CurveTab::timeWarp, CurveEditorModel::unflatten(snapshot.soundCurves[0]));
        curveEditor_.setPoints(CurveTab::pitch, CurveEditorModel::unflatten(snapshot.soundCurves[1]));
        curveEditor_.setPoints(CurveTab::spectralWarp, CurveEditorModel::unflatten(snapshot.soundCurves[2]));
        curveDurationSeconds_ = snapshot.sourceDurations[0];
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
        // Snapshots arrive on every UI tick.  The view mode is the editor's
        // own state: take the host's only when the host's value changes
        // (state restore, another editor instance), never on each poll, so a
        // selection is not undone by a snapshot published before the host
        // applied it.
        if (!hostSpectrumMode_ || *hostSpectrumMode_ != spectrumSnapshot_.mode)
        {
            hostSpectrumMode_ = spectrumSnapshot_.mode;
            shell_.spectrum().setMode(spectrumSnapshot_.mode);
        }
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
        renderOptions.timeSelectionFirstFrame = navigation.timeSelectionFirstFrame;
        renderOptions.timeSelectionLastFrame = navigation.timeSelectionLastFrame;
        renderOptions.showOnsetOverlay = spectrumOnsetEditing_;
        renderOptions.selectedBands = navigation.selectedBands;
        if (renderOptions.selectedBands.empty() && navigation.selectedBand >= 0)
            renderOptions.selectedBands.push_back(navigation.selectedBand);
        renderOptions.residualFirstFrame = navigation.residualFirstFrame;
        renderOptions.residualLastFrame = navigation.residualLastFrame;
        renderOptions.hoveredBand = navigation.hoveredBand;
        // The range is absolute normalised time/frequency; the renderer maps
        // it through the viewport (a slab in the isometric envelope views).
        renderOptions.envelopeEditing = navigation.envelopeEditing;
        renderOptions.envelopeTimeStart = navigation.envelopeTimeStart;
        renderOptions.envelopeTimeEnd = navigation.envelopeTimeEnd;
        renderOptions.envelopeFrequencyStart = navigation.envelopeFrequencyStart;
        renderOptions.envelopeFrequencyEnd = navigation.envelopeFrequencyEnd;
        renderOptions.viewport = { (navigation.timeStart + navigation.timeEnd) * 0.5f,
                                   1.0f / std::max(1.0e-6f, navigation.timeEnd - navigation.timeStart),
                                   (navigation.frequencyStart + navigation.frequencyEnd) * 0.5f,
                                   1.0f / std::max(1.0e-6f, navigation.frequencyEnd - navigation.frequencyStart) };
        const auto plot = SpectrumRenderer::getPlotBounds(well, spectrumSnapshot_);
        const auto commands = SpectrumRenderer::render(
            spectrumSnapshot_, well, renderOptions);
        const visage::Font font8_5(8.5f, visage::fonts::Lato_Regular_ttf);
        const visage::Font font9(9.0f, visage::fonts::Lato_Regular_ttf);
        const visage::Font font10(10.0f, visage::fonts::Lato_Regular_ttf);
        const visage::Font font11(11.0f, visage::fonts::Lato_Regular_ttf);
        const auto fontFor = [&](const SpectrumDrawCommand& command,
                                 const visage::Font& fallback) -> const visage::Font& {
            if (command.fontSize <= 0.0f) return fallback;
            if (command.fontSize < 8.8f) return font8_5;
            if (command.fontSize < 9.5f) return font9;
            if (command.fontSize < 10.5f) return font10;
            return font11;
        };
        const auto justification = [](const SpectrumDrawCommand& command) {
            return command.justify == 2 ? visage::Font::kRight
                : command.justify == 1 ? visage::Font::kCenter : visage::Font::kLeft;
        };
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
            // Fills fade from `color` at gradStart to transparent at gradEnd
            // when those points differ (absolute canvas coordinates).
            const auto fillBrush = [&](bool verticalFallback) {
                const auto clear = color & 0x00ffffffu;
                const auto gx = command.gradEnd.x - command.gradStart.x;
                const auto gy = command.gradEnd.y - command.gradStart.y;
                if (gx * gx + gy * gy > 1.0e-6f)
                    canvas.setColor(visage::Brush::linear(
                        visage::Color(color), visage::Color(clear),
                        visage::Point(command.gradStart.x, command.gradStart.y),
                        visage::Point(command.gradEnd.x, command.gradEnd.y)));
                else if (verticalFallback)
                    canvas.setColor(visage::Brush::vertical(color, clear));
            };
            // Fills are one path each: per-triangle fills left anti-aliased
            // seams along every shared edge, visible as vertical hatching.
            if (command.filled && command.vertices.size() >= 3)
            {
                fillBrush(false);
                visage::Path path;
                path.moveTo(command.vertices.front().x, command.vertices.front().y);
                for (std::size_t i = 1; i < command.vertices.size(); ++i)
                    path.lineTo(command.vertices[i].x, command.vertices[i].y);
                path.close();
                canvas.fill(path);
                restoreClip();
                continue;
            }
            if (command.isStrip())
            {
                // Top vertices, then down to the floor line through
                // (front.x, floorY) and (back.x, floorY2).
                fillBrush(true);
                const auto& v = command.vertices;
                visage::Path path;
                path.moveTo(v.front().x, v.front().y);
                for (std::size_t i = segmentStride; i < v.size(); i += segmentStride)
                    path.lineTo(v[i].x, v[i].y);
                path.lineTo(v.back().x, v.back().y);
                path.lineTo(v.back().x, command.floorY2);
                path.lineTo(v.front().x, command.floorY);
                path.close();
                canvas.fill(path);
                restoreClip();
                continue;
            }
            if (command.isPath())
            {
                const auto solidPath = command.flat
                    || command.kind == SpectrumDrawKind::selectedPartial
                    || command.kind == SpectrumDrawKind::glowHalo
                    || command.kind == SpectrumDrawKind::traceGlow;
                const auto rampColour = [&](float amp) {
                    return command.colorRamp == 1 ? overlayAmpColour(amp) : ampColour(amp);
                };
                const auto colorAt = [&](float proportion) {
                    if (solidPath || command.stops.empty())
                        return UiColor { color };
                    const auto shaded = [&](float amp, float fade, float stopAlpha) {
                        return rampColour(amp).withAlpha(
                            std::clamp(command.alpha * fade * stopAlpha, 0.0f, 1.0f));
                    };
                    auto next = std::lower_bound(command.stops.begin(), command.stops.end(), proportion,
                        [](const TraceStop& stop, float value) { return stop.proportion < value; });
                    if (next == command.stops.begin())
                        return shaded(next->amp, next->fade, next->alpha);
                    if (next == command.stops.end())
                    {
                        const auto& stop = command.stops.back();
                        return shaded(stop.amp, stop.fade, stop.alpha);
                    }
                    const auto& previous = *(next - 1);
                    const auto span = std::max(1.0e-6f, next->proportion - previous.proportion);
                    const auto amount = std::clamp(
                        (proportion - previous.proportion) / span, 0.0f, 1.0f);
                    return shaded(previous.amp + amount * (next->amp - previous.amp),
                                  previous.fade + amount * (next->fade - previous.fade),
                                  previous.alpha + amount * (next->alpha - previous.alpha));
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
                    const auto endSegment = i == 0 || next == last;
                    i = next;
                    const auto midpointX = (a.x + b.x) * 0.5f - command.gradStart.x;
                    const auto midpointY = (a.y + b.y) * 0.5f - command.gradStart.y;
                    const auto proportion = axisLength2 > 1.0e-6f
                        ? std::clamp((midpointX * axisX + midpointY * axisY) / axisLength2,
                                     0.0f, 1.0f)
                        : 0.0f;
                    canvas.setColor(colorAt(proportion).argb);
                    // Round caps only at the two ends: translucent round caps
                    // overlap at every interior joint and read as beads.
                    canvas.segment(a.x, a.y, b.x, b.y, command.thickness, endSegment);
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
                canvas.rectangleBorder(b.x, b.y, b.width, b.height, command.thickness);
                break;
            case SpectrumDrawKind::traceGlow:
                canvas.segment(b.x, b.y, b.x + b.width, b.y + b.height,
                               command.thickness, true);
                break;
            case SpectrumDrawKind::playheadWake:
                canvas.rectangle(b.x, b.y, b.width, b.height);
                break;
            case SpectrumDrawKind::playhead:
                // The isometric playhead marks its front floor point with a dot.
                if (command.index == 1)
                    canvas.circle(b.x, b.y, b.width);
                else
                    canvas.rectangle(b.x, b.y, b.width, b.height);
                break;
            case SpectrumDrawKind::smearnovCurve:
                canvas.roundedRectangle(b.x, b.y, b.width, b.height, 3.0f);
                break;
            case SpectrumDrawKind::infoText:
                canvas.text(command.label, fontFor(command, labelFont), justification(command),
                            b.x, b.y, b.width, b.height);
                break;
            case SpectrumDrawKind::badge:
            {
                // x2 = background alpha, y2 = 1 for a border.
                const auto backgroundAlpha = static_cast<std::uint32_t>(
                    std::clamp(command.x2, 0.0f, 1.0f) * 255.0f + 0.5f);
                canvas.setColor((backgroundAlpha << 24) | 0x0007110fu);
                canvas.roundedRectangle(b.x, b.y, b.width, b.height, 3.0f);
                canvas.setColor(color);
                if (command.y2 > 0.0f)
                    canvas.roundedRectangleBorder(b.x + 0.5f, b.y + 0.5f, b.width - 1.0f,
                                                  b.height - 1.0f, 4.0f, 0.8f);
                canvas.text(command.label, fontFor(command, labelFont), justification(command),
                            b.x + 6.0f, b.y, std::max(0.0f, b.width - 12.0f), b.height);
                break;
            }
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
                canvas.text(command.label, fontFor(command, axisFont),
                            command.fontSize > 0.0f ? justification(command) : visage::Font::kCenter,
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
                canvas.text(command.label, fontFor(command, labelFont), visage::Font::kLeft,
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
                    const auto borderAlpha = command.kind == SpectrumDrawKind::zoomBar ? 0.45f : 0.22f;
                    canvas.setColor((SpectralCanvasTokens::fixed().axisFrame.argb & 0x00ffffffu) |
                                    (static_cast<std::uint32_t>(borderAlpha * 255.0f) << 24));
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

        // Isometric axis captions are placed by the renderer at the projected
        // axis ends, so they follow the floor rather than the panel corners.
        const auto residualMode = spectrumSnapshot_.mode == SpectralTraceMode::residual
            || spectrumSnapshot_.mode == SpectralTraceMode::isometricResidual;
        const auto hasSelection = residualMode
            ? (!navigation.selectedBands.empty() || navigation.selectedBand >= 0)
            : (!navigation.selectedPartials.empty() || navigation.selectedPartial >= 0);
        const auto hasOnsets = !spectrumSnapshot_.onsets.empty()
            || !spectrumSnapshot_.onsetNorms.empty();
        const auto chrome = SpectrumChromeLayout::layout(well, spectrumSnapshot_.mode,
                                                          hasOnsets, hasSelection);
        const auto chipBorder = (SpectralCanvasTokens::fixed().axisFrame.argb & 0x00ffffffu) |
                                (static_cast<std::uint32_t>(0.45f * 255.0f) << 24);
        const auto paintChip = [&](UiRect chip, const std::string& text, float rightInset) {
            paintThemeOps(canvas, ThemeRecipes::buttonBackground(tokens, chip, false, false));
            canvas.setColor(chipBorder);
            canvas.roundedRectangleBorder(chip.x, chip.y, chip.width, chip.height, 4.0f, 1.0f);
            canvas.setColor(tokens.textSecondary.argb);
            // A long source name ("Current: <file>") is cut with an ellipsis
            // and left-aligned; centred, it was clipped at both ends.
            const auto room = std::max(0.0f, chip.width - 10.0f - rightInset);
            auto shown = visage::String(text).toUtf32();
            if (labelFont.stringWidth(shown) > room)
            {
                const std::u32string ellipsis = U"\u2026";
                while (!shown.empty() && labelFont.stringWidth(shown + ellipsis) > room)
                    shown.pop_back();
                shown += ellipsis;
            }
            canvas.text(visage::String(shown), labelFont,
                        shown.size() < visage::String(text).toUtf32().size()
                            ? visage::Font::kLeft : visage::Font::kCenter,
                        chip.x + 6.0f, chip.y, chip.width - 6.0f - rightInset, chip.height);
        };
        if (chrome.modeStrip)
        {
            const auto capabilities = spectrumCapabilities();
            const auto source = SpectrumInteractionModel::sourceIndex(shell_.spectrum().source());
            const auto& sourceChip = chrome.sourceMenuButton;
            paintChip(sourceChip, spectrumSourceLabel(source, capabilities), 16.0f);
            canvas.setColor(tokens.textSecondary.argb);
            canvas.triangleDown(sourceChip.x + sourceChip.width - 14.0f,
                                sourceChip.y + sourceChip.height * 0.5f - 2.0f, 4.0f);
            const auto& first = chrome.modeButtons.front();
            const auto& last = chrome.modeButtons.back();
            const UiRect strip { first.x, first.y, last.x + last.width - first.x, first.height };
            const auto trackRadius = ThemeRadius::segmentedTrack(strip.height);
            paintThemeOps(canvas, ThemeRecipes::well(tokens, strip, trackRadius));
            const auto activeMode = static_cast<std::size_t>(shell_.spectrum().mode());
            for (std::size_t i = 0; i < chrome.modeButtons.size(); ++i)
            {
                const auto& button = chrome.modeButtons[i];
                const auto active = i == activeMode;
                const auto enabled = spectrumModeEnabled(source, static_cast<int>(i), capabilities);
                paintThemeOps(canvas, ThemeRecipes::segmentedSegment(tokens, button, trackRadius,
                                                                     active, false));
                const auto colour = ThemeRecipes::segmentedText(tokens, active, false);
                canvas.setColor(enabled ? colour.argb : colour.withAlpha(0.35f).argb);
                canvas.text(std::string(SpectrumChromeLayout::kModeButtonLabels[i]), labelFont,
                            visage::Font::kCenter, button.x, button.y, button.width, button.height);
            }
        }
        else
        {
            paintChip(chrome.viewMenuButton, spectrumModeLabel(), 4.0f);
        }
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

        drawCurveEditor(canvas, tokens, timeline, session.transport.normalizedPlayhead);
    }

    // SESSION timeline (spec §8.2): a node editor for the morph curve and the
    // render-time automation curves, with DETECT / APPLY on the header row.
    void drawCurveEditor(visage::Canvas& canvas, const ThemeTokens& tokens, UiRect timeline, float playhead)
    {
        const auto layout = curveEditorLayout(timeline);
        curveEditor_.setPlot(layout.plot);
        const visage::Font buttonFont(ThemeFont::cheekButton, visage::fonts::Lato_Regular_ttf);
        const visage::Font axisFont(8.5f, visage::fonts::Lato_Regular_ttf);

        for (std::size_t i = 0; i < layout.tabs.size(); ++i)
        {
            const auto tab = static_cast<CurveTab>(i);
            const auto active = curveEditor_.tab() == tab;
            paintThemeOps(canvas, ThemeRecipes::buttonBackground(tokens, layout.tabs[i], false, active));
            canvas.setColor(active ? tokens.accent.argb : tokens.textSecondary.argb);
            canvas.text(CurveEditorModel::range(tab).label, buttonFont, visage::Font::kCenter,
                        layout.tabs[i].x, layout.tabs[i].y, layout.tabs[i].width, layout.tabs[i].height);
        }
        const std::array<std::pair<const char*, UiRect>, 4> buttons {{
            { "RESET", layout.reset }, { "BOUNCE", layout.bounce },
            { "DETECT", layout.detect }, { "APPLY", layout.apply } }};
        for (const auto& [label, rect] : buttons)
        {
            paintThemeOps(canvas, ThemeRecipes::buttonBackground(tokens, rect, false, false));
            canvas.setColor(tokens.textPrimary.argb);
            canvas.text(label, buttonFont, visage::Font::kCenter, rect.x, rect.y, rect.width, rect.height);
        }

        const auto plot = layout.plot;
        paintThemeOps(canvas, ThemeRecipes::well(tokens, plot, ThemeRadius::sliderTrack));
        const auto range = CurveEditorModel::range(curveEditor_.tab());
        // Identity reference: dashed horizontal (or the time-warp diagonal).
        canvas.setColor((tokens.textFaint.argb & 0x00ffffffu) | 0x80000000u);
        const auto identityAt = [&](float x) {
            return curveEditor_.toScreen({ x, range.diagonal ? range.minY + x * (range.maxY - range.minY)
                                                             : range.identity });
        };
        for (int dash = 0; dash < 24; dash += 2)
        {
            const auto a = identityAt(dash / 24.0f), b = identityAt((dash + 1) / 24.0f);
            canvas.segment(a.x, a.y, b.x, b.y, 1.0f, false);
        }
        // Axis range labels.
        char text[32];
        canvas.setColor(tokens.textSecondary.argb);
        std::snprintf(text, sizeof text, range.diagonal ? "end" : "%.3g%s", range.maxY, range.unit);
        canvas.text(text, axisFont, visage::Font::kRight, timeline.x + 2.0f, plot.y - 4.0f, plot.x - timeline.x - 6.0f, 10.0f);
        std::snprintf(text, sizeof text, range.diagonal ? "start" : "%.3g%s", range.minY, range.unit);
        canvas.text(text, axisFont, visage::Font::kRight, timeline.x + 2.0f, plot.y + plot.height - 6.0f,
                    plot.x - timeline.x - 6.0f, 10.0f);
        // Time labels under the plot (also the seek strip).
        const auto duration = curveDurationSeconds_;
        for (int i = 0; i <= 4; ++i)
        {
            const auto x = plot.x + plot.width * i / 4.0f;
            std::snprintf(text, sizeof text, duration > 0.0f ? "%.1fs" : "%.0f%%",
                          duration > 0.0f ? duration * i / 4.0 : 25.0 * i);
            canvas.text(text, axisFont, i == 0 ? visage::Font::kLeft : i == 4 ? visage::Font::kRight
                                                                               : visage::Font::kCenter,
                        i == 0 ? x : i == 4 ? x - 40.0f : x - 20.0f, layout.seek.y, 40.0f, layout.seek.height);
        }

        // The curve, its fill (MORPH) and points.
        const auto& points = curveEditor_.points();
        if (!points.empty())
        {
            const auto line = curveEditor_.polyline();
            if (curveEditor_.tab() == CurveTab::morph)
            {
                canvas.setColor((tokens.accent.argb & 0x00ffffffu) | 0x22000000u);
                const auto floor = plot.y + plot.height;
                for (std::size_t i = 1; i < line.size(); ++i)
                {
                    canvas.triangle(line[i - 1].x, line[i - 1].y, line[i].x, line[i].y, line[i].x, floor);
                    canvas.triangle(line[i - 1].x, line[i - 1].y, line[i].x, floor, line[i - 1].x, floor);
                }
            }
            canvas.setColor(tokens.accent.argb);
            for (std::size_t i = 1; i < line.size(); ++i)
                canvas.segment(line[i - 1].x, line[i - 1].y, line[i].x, line[i].y, 1.5f, true);
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                const auto p = curveEditor_.toScreen(points[i]);
                const auto dragged = static_cast<int>(i) == curveEditor_.dragging();
                canvas.setColor(dragged ? tokens.textPrimary.argb : tokens.accent.argb);
                canvas.circle(p.x - 3.5f, p.y - 3.5f, 7.0f);
            }
        }
        // Playhead.
        canvas.setColor((tokens.textPrimary.argb & 0x00ffffffu) | 0xb0000000u);
        canvas.rectangle(plot.x + plot.width * std::clamp(playhead, 0.0f, 1.0f), plot.y, 1.0f, plot.height);
        // Drag readout: "{t}s  {value}".
        const auto readout = curveEditor_.readout(duration);
        if (!readout.empty())
        {
            const auto p = curveEditor_.toScreen(points[static_cast<std::size_t>(curveEditor_.dragging())]);
            const auto x = std::clamp(p.x + 8.0f, plot.x, plot.x + plot.width - 110.0f);
            canvas.setColor((tokens.panel.argb & 0x00ffffffu) | 0xe0000000u);
            canvas.roundedRectangle(x, plot.y + 2.0f, 110.0f, 14.0f, 3.0f);
            canvas.setColor(tokens.textPrimary.argb);
            canvas.text(readout, axisFont, visage::Font::kCenter, x, plot.y + 2.0f, 110.0f, 14.0f);
        }
    }

    struct CurveEditorLayout
    {
        std::array<UiRect, 4> tabs {};
        UiRect reset {}, bounce {}, detect {}, apply {}, plot {}, seek {};
    };

    // Every timeline rect is derived here so paint and hit tests agree.
    [[nodiscard]] static CurveEditorLayout curveEditorLayout(UiRect timeline) noexcept
    {
        CurveEditorLayout l;
        const auto y = timeline.y + 6.0f;
        constexpr float h = 20.0f, tabW = 52.0f;
        for (std::size_t i = 0; i < l.tabs.size(); ++i)
            l.tabs[i] = { timeline.x + 10.0f + static_cast<float>(i) * (tabW + 2.0f), y, tabW, h };
        l.reset = { l.tabs.back().x + tabW + 8.0f, y, 50.0f, h };
        l.bounce = { l.reset.x + l.reset.width + 6.0f, y, 58.0f, h };
        l.apply = { timeline.x + timeline.width - 10.0f - 64.0f, y, 64.0f, h };
        l.detect = { l.apply.x - 6.0f - 64.0f, y, 64.0f, h };
        l.plot = { timeline.x + 42.0f, y + h + 6.0f, std::max(0.0f, timeline.width - 52.0f),
                   std::max(10.0f, timeline.height - (h + 12.0f) - 18.0f) };
        l.seek = { l.plot.x, l.plot.y + l.plot.height + 2.0f, l.plot.width, 12.0f };
        return l;
    }

    // Sends the edited curve to the host.
    void commitCurve()
    {
        const auto& points = curveEditor_.points();
        switch (curveEditor_.tab())
        {
        case CurveTab::morph:
        {
            SetMorphTimelineAction action;
            for (const auto& p : points)
                action.points.push_back({ p.x, std::clamp(p.y, 0.0f, 1.0f), 0, p.shape });
            if (action.points.empty())
                action.points = { { 0.0f, 0.0f, 0, 0 }, { 1.0f, 1.0f, 0, 0 } };
            emitAction(action);
            break;
        }
        case CurveTab::pitch:
            emitAction(SetSoundCurveAction { SoundCurveKind::pitch, CurveEditorModel::flatten(points) });
            break;
        case CurveTab::timeWarp:
            emitAction(SetSoundCurveAction { SoundCurveKind::timeWarp, CurveEditorModel::flatten(points) });
            break;
        case CurveTab::spectralWarp:
            emitAction(SetSoundCurveAction { SoundCurveKind::spectralWarp, CurveEditorModel::flatten(points) });
            break;
        }
    }

    // Applies a view chosen from the header buttons or either menu.  Returns
    // false when that source/view pair is unavailable (no residual, empty slot).
    bool applySpectrumView(int source, int mode)
    {
        const auto capabilities = spectrumCapabilities();
        const auto action = shell_.spectrum().selectSourceMenuItem(source, mode, capabilities);
        if (action.mode < 0)
            return false;
        shell_.spectrum().setSource(static_cast<SpectralTraceSource>(action.source));
        shell_.spectrum().setMode(static_cast<SpectralTraceMode>(action.mode));
        // The criteria panel belongs to the partial views' toolbar.
        const auto partialView = action.mode == static_cast<int>(SpectralTraceMode::combined)
            || action.mode == static_cast<int>(SpectralTraceMode::partials);
        if (!partialView)
            setSelectionPanelVisible(false);
        if (sink_.spectrumModeSelected)
            sink_.spectrumModeSelected(sink_.context, action.source, action.mode);
        emitAction(action);
        redraw();
        return true;
    }

    [[nodiscard]] bool spectrumSourceEnabled(int source,
                                             const SpectrumCapabilities& capabilities) const noexcept
    {
        return source == 0 ? capabilities.hasCurrentSource
             : source == 1 ? capabilities.hasSourceA
             : source == 2 && capabilities.hasSourceB;
    }

    [[nodiscard]] bool spectrumModeEnabled(int source, int mode,
                                           const SpectrumCapabilities& capabilities) const noexcept
    {
        return spectrumSourceEnabled(source, capabilities)
            && (mode != static_cast<int>(SpectralTraceMode::residual)
                || capabilities.hasResidualForSource(source));
    }

    [[nodiscard]] std::string spectrumSourceLabel(int source,
                                                  const SpectrumCapabilities& capabilities) const
    {
        const auto name = source == 0 ? capabilities.currentName
                        : source == 1 ? capabilities.sourceAName : capabilities.sourceBName;
        const auto prefix = source == 0 ? "Current" : source == 1 ? "A" : "B";
        if (source == 0 && name == "Current")
            return "Current";
        return std::string(prefix) + ": " + std::string(name);
    }

    // Menu ids: 1..7 pick a view on the current source, 11..13 pick a source
    // and keep the view (falling back to Trace + Residual where it is missing).
    static constexpr int kMenuModeBase = 1;
    static constexpr int kMenuSourceBase = 11;

    void selectSpectrumMenuOption(int optionId)
    {
        const auto source = SpectrumInteractionModel::sourceIndex(shell_.spectrum().source());
        const auto mode = static_cast<int>(shell_.spectrum().mode());
        if (optionId >= kMenuSourceBase && optionId < kMenuSourceBase + 3)
        {
            const auto wanted = optionId - kMenuSourceBase;
            if (!applySpectrumView(wanted, mode))
                (void) applySpectrumView(wanted, static_cast<int>(SpectralTraceMode::combined));
        }
        else if (optionId >= kMenuModeBase
                 && optionId < kMenuModeBase + static_cast<int>(SpectrumChromeLayout::kModeButtonCount))
        {
            (void) applySpectrumView(source, optionId - kMenuModeBase);
        }
    }

    void showSpectrumPopup(visage::PopupMenu popup, visage::Point origin)
    {
        popup.onSelection() += [this](int optionId) {
            selectSpectrumMenuOption(optionId);
            if (sink_.popupDismissed)
                sink_.popupDismissed(sink_.context);
        };
        popup.onCancel() += [this] {
            if (sink_.popupDismissed)
                sink_.popupDismissed(sink_.context);
        };
        if (sink_.popupShown)
            sink_.popupShown(sink_.context);
        // Shown against the session page, not the shell, because the rect
        // handed to the delegate is page-local.
        popup.show(sessionPageRaw_ != nullptr ? static_cast<visage::Frame*>(sessionPageRaw_) : this,
                   origin);
    }

    void addSpectrumSourceOptions(visage::PopupMenu& popup,
                                  const SpectrumCapabilities& capabilities) const
    {
        const auto current = SpectrumInteractionModel::sourceIndex(shell_.spectrum().source());
        for (int source = 0; source < 3; ++source)
            popup.addOption(kMenuSourceBase + source, spectrumSourceLabel(source, capabilities))
                .enable(spectrumSourceEnabled(source, capabilities))
                .select(source == current);
    }

    // Narrow wells: every view, then the sources, in one flat list.  (Views
    // used to sit in a submenu per source, which was hard to reach.)
    void showSpectrumModeMenu(visage::Point origin)
    {
        visage::PopupMenu popup("Spectrum view");
        const auto capabilities = spectrumCapabilities();
        const auto source = SpectrumInteractionModel::sourceIndex(shell_.spectrum().source());
        for (const auto& item : shell_.spectrum().menu(capabilities))
            popup.addOption(kMenuModeBase + item.id, std::string(item.label))
                .enable(spectrumModeEnabled(source, static_cast<int>(item.mode), capabilities))
                .select(item.mode == shell_.spectrum().mode());
        popup.addBreak();
        addSpectrumSourceOptions(popup, capabilities);
        showSpectrumPopup(std::move(popup), origin);
    }

    // Wide wells: the views are buttons, so the chip only picks the source.
    // Time... menu: structural edits on the selected time range (drag a box
    // in a planar view), and paste at the playhead.
    void showTimeEditMenu(visage::Point origin)
    {
        enum : int { kCopy = 1, kCut, kDelete, kCrop, kPasteFade, kPasteHard };
        const auto hasRange = shell_.spectrum().hasTimeSelection();
        const auto hasModel = spectrumSnapshot_.frameCount > 0;
        visage::PopupMenu popup("Time edit");
        popup.addOption(kCopy, "Copy time range").enable(hasRange);
        popup.addOption(kCut, "Cut time range").enable(hasRange);
        popup.addOption(kDelete, "Delete time range").enable(hasRange);
        popup.addOption(kCrop, "Crop to time range").enable(hasRange);
        popup.addBreak();
        popup.addOption(kPasteFade, "Paste at playhead (crossfade)").enable(hasModel);
        popup.addOption(kPasteHard, "Paste at playhead (no crossfade)").enable(hasModel);
        popup.onSelection() += [this](int option) {
            const auto& navigation = shell_.spectrum().navigation();
            TimeEditAction edit;
            edit.firstFrame = navigation.timeSelectionFirstFrame;
            edit.lastFrame = navigation.timeSelectionLastFrame;
            switch (option)
            {
            case kCopy: edit.kind = TimeEditKind::copy; break;
            case kCut: edit.kind = TimeEditKind::cut; break;
            case kDelete: edit.kind = TimeEditKind::remove; break;
            case kCrop: edit.kind = TimeEditKind::crop; break;
            case kPasteFade:
            case kPasteHard:
            {
                edit.kind = TimeEditKind::paste;
                const auto frames = std::max(1u, spectrumSnapshot_.frameCount);
                edit.firstFrame = static_cast<int>(std::lround(
                    std::clamp(shell_.session().state().transport.normalizedPlayhead, 0.0f, 1.0f)
                    * static_cast<float>(frames)));
                edit.lastFrame = edit.firstFrame;
                edit.crossfadeFrames = option == kPasteFade ? kTimeEditCrossfadeFrames : 0;
                break;
            }
            default: return;
            }
            emitAction(edit);
            // The model's frame grid changed (except copy); the old range is stale.
            if (edit.kind != TimeEditKind::copy)
                shell_.spectrum().clearTimeSelection();
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

    // Seam crossfade for "Paste at playhead (crossfade)": two frames each side.
    static constexpr int kTimeEditCrossfadeFrames = 2;

    void showSpectrumSourceMenu(visage::Point origin)
    {
        visage::PopupMenu popup("Spectrum source");
        addSpectrumSourceOptions(popup, spectrumCapabilities());
        showSpectrumPopup(std::move(popup), origin);
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
        if (modeChip.width > 0.0f && pointInRect(point, modeChip))
        {
            showSpectrumModeMenu({ modeChip.x, modeChip.y + modeChip.height + 4.0f });
            return true;
        }
        if (chrome.modeStrip)
        {
            const auto& sourceChip = chrome.sourceMenuButton;
            if (pointInRect(point, sourceChip))
            {
                showSpectrumSourceMenu({ sourceChip.x, sourceChip.y + sourceChip.height + 4.0f });
                return true;
            }
            for (std::size_t i = 0; i < chrome.modeButtons.size(); ++i)
                if (pointInRect(point, chrome.modeButtons[i]))
                {
                    (void) applySpectrumView(
                        SpectrumInteractionModel::sourceIndex(shell_.spectrum().source()),
                        static_cast<int>(i));
                    return true;
                }
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
            const auto position = std::clamp((point.x - chrome.zoomBar.x - 8.0f)
                                                  / SpectrumRenderer::zoomBarTrackWidth(chrome.zoomBar),
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
            if (item.command == SpectrumEditCommand::selectPartialsByCriteria)
            {
                setSelectionPanelVisible(selectionPanelRaw_ != nullptr && !selectionPanelRaw_->isVisible());
                return true;
            }
            if (item.command == SpectrumEditCommand::timeEditMenu)
            {
                showTimeEditMenu({ item.bounds.x, item.bounds.y + item.bounds.height + 4.0f });
                return true;
            }
            if (item.command == SpectrumEditCommand::invertPartialSelection)
            {
                auto current = navigation.selectedPartials;
                if (current.empty() && navigation.selectedPartial >= 0)
                    current.push_back(navigation.selectedPartial);
                shell_.spectrum().setSelectedPartials(invertPartials(spectrumSnapshot_, current));
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
        const auto layout = curveEditorLayout(timeline);
        if (pointInRect(point, layout.detect))
        {
            emitAction(DetectSmearnovAction {});
            return true;
        }
        if (pointInRect(point, layout.apply))
        {
            emitAction(ApplySmearnovAction {});
            return true;
        }
        for (std::size_t i = 0; i < layout.tabs.size(); ++i)
            if (pointInRect(point, layout.tabs[i]))
            {
                curveEditor_.setTab(static_cast<CurveTab>(i));
                redraw();
                return true;
            }
        if (pointInRect(point, layout.reset))
        {
            curveEditor_.reset();
            commitCurve();
            redraw();
            return true;
        }
        if (pointInRect(point, layout.bounce))
        {
            // The morph render of the current audition layer, saved as WAV.
            runSourceImportDialog(NativeFileDialogKind::bounceWav,
                                  shell_.session().previewAction(false).layer);
            return true;
        }
        if (pointInRect(point, layout.seek))
        {
            const auto normalized = std::clamp(
                layout.seek.width > 0.0f ? (point.x - layout.seek.x) / layout.seek.width : 0.0f, 0.0f, 1.0f);
            shell_.session().setPlayhead(normalized);
            emitAction(shell_.session().seekAction());
            redraw();
            return true;
        }
        curveEditor_.setPlot(layout.plot);
        // Shift-click a MORPH point: cycle the easing of the segment it starts.
        if (shift && curveEditor_.cycleShape({ point.x, point.y }))
        {
            commitCurve();
            redraw();
            return true;
        }
        if (curveEditor_.inPlot({ point.x, point.y }))
        {
            // A removal commits at once; an added or grabbed point commits on
            // release (if it was added or moved).
            const auto changed = curveEditor_.press({ point.x, point.y }, doubleClick);
            curvePressAdded_ = changed && curveEditor_.dragging() >= 0;
            if (changed && curveEditor_.dragging() < 0)
                commitCurve();
            redraw();
            return true;
        }
        return pointInRect(point, timeline);
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
        if (curveEditor_.dragging() >= 0)
        {
            curveEditor_.drag({ point.x, point.y });
            redraw();
            return;
        }
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
        if (curveEditor_.dragging() >= 0)
        {
            const auto moved = curveEditor_.release();
            if (moved || curvePressAdded_)
                commitCurve();
            curvePressAdded_ = false;
            redraw();
            return;
        }
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
    std::function<void(ThemeId)> onThemeChosen_;
    std::unique_ptr<AtsPageContainer> pageContainer_;
    SpectrumSelectionPanel* selectionPanelRaw_ = nullptr;
    CurveEditorModel curveEditor_;
    float curveDurationSeconds_ = 0.0f;
    bool curvePressAdded_ = false;
    AtsPageContainer* pageContainerRaw_ = nullptr;
    AtsSessionPage* sessionPageRaw_ = nullptr;
    AtsAnalysisPage* analysisPageRaw_ = nullptr;
    AtsSynthesisPage* synthesisPageRaw_ = nullptr;
    AtsPartialsPage* partialsPageRaw_ = nullptr;
    SpectralTraceSnapshot spectrumSnapshot_{};
    // The last mode the host published; see setSpectrumSnapshot().
    std::optional<SpectralTraceMode> hostSpectrumMode_;
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

ThemeId VisageEditor::theme() const
{
    return impl_->frame.theme();
}

void VisageEditor::enableUserPreferences(const std::string& path)
{
    const auto file = path.empty() ? detail::defaultEditorPreferencesPath() : path;
    if (file.empty())
        return;
    impl_->frame.setTheme(EditorPreferences::load(file).theme);
    impl_->frame.setThemeChosenHandler([file](ThemeId theme) {
        auto preferences = EditorPreferences::load(file);
        preferences.theme = theme;
        (void) preferences.save(file);
    });
}

void VisageEditor::selectPage(int page)
{
    if (page >= 0)
        impl_->frame.selectPage(static_cast<std::size_t>(page));
}

void VisageEditor::selectTimelineCurve(int tab)
{
    impl_->frame.selectTimelineCurve(tab);
}

void VisageEditor::showPartialSelectionPanel(bool visible)
{
    impl_->frame.setSelectionPanelVisible(visible);
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
