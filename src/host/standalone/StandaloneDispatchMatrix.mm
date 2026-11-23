// Live dispatch matrix for the standalone app (card 32 part 1, audit T-202).
//
// `ats_resynth_standalone --dispatch-matrix <log.jsonl>` drives the real
// standalone composition (HostAdapter + VisageEditor in a native window) with
// native mouse and keyboard events, and records every step as one JSON line:
// the editor actions emitted, the host result, and telemetry afterwards. Click
// targets are the 1280x940 design-size layout measured from the RMJ-031
// screenshot matrix; each step states and checks its expected effect.

#include "../../host/HostAdapter.h"
#include "../../core/ParameterRegistry.h"
#include "../../ui/VisageEditor.h"
#include "StandaloneAudioOutput.h"

#import <Cocoa/Cocoa.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace ats::standalone
{
namespace
{
constexpr const char* kActionNames[] {
    "SetParameter", "ImportWav", "ImportAts", "ExportAts", "AnalyzeCurrent", "AnalyzeMorphSource",
    "ApplyAnalysisProfile", "CancelJobs", "Preview", "StopPreview", "SeekPreview", "SetPreviewSmear",
    "MorphVariation", "SetMorphTimeline", "BounceMorphTimeline", "SetSpectralView",
    "ApplyFactoryPreset", "SavePreset", "LoadPreset", "Note", "History", "PartialEdit",
    "ResidualEdit", "SpectrumEdit", "ApplyMath", "DetectSmearnov", "ApplySmearnov",
    "EditSmearnovOnsets", "EditSmearnovOnsetHandles", "SetTimbreMotionPlan", "AutoTuneAnalysis", "TimeEdit", "SetSoundCurve" };
static_assert(std::size(kActionNames) == std::variant_size_v<ui::EditorAction>,
              "action name table out of sync with EditorAction");

std::string escape(const std::string& text)
{
    std::string out;
    for (const char c : text)
    {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (static_cast<unsigned char>(c) >= 0x20) out += c;
    }
    return out;
}

const char* uiStateName(ui::UiState state)
{
    switch (state)
    {
    case ui::UiState::noSource: return "noSource";
    case ui::UiState::readyToAnalyze: return "readyToAnalyze";
    case ui::UiState::analyzing: return "analyzing";
    case ui::UiState::analyzed: return "analyzed";
    case ui::UiState::resynthesizing: return "resynthesizing";
    case ui::UiState::applyingMath: return "applyingMath";
    case ui::UiState::error: return "error";
    }
    return "unknown";
}

struct Recorder
{
    host::HostAdapter* host = nullptr;
    std::vector<std::string> actions;
    std::vector<std::string> events;
    std::string lastError;
    int page = -1;
    int theme = -1;
    int popupsShown = 0;

    static void onAction(void* context, const ui::EditorAction* action)
    {
        auto* self = static_cast<Recorder*>(context);
        if (self == nullptr || action == nullptr)
            return;
        std::string name = kActionNames[action->index()];
        if (const auto* preview = std::get_if<ui::PreviewAction>(action))
            name += "(source=" + std::to_string(preview->source) + ",layer=" +
                    std::to_string(preview->layer) + (preview->start ? ",start" : ",stop") + ")";
        if (const auto* parameter = std::get_if<ui::SetParameterAction>(action))
            name += "(" + parameter->id + (parameter->begin ? ",begin" : "") +
                    (parameter->end ? ",end" : "") + ")";
        self->actions.push_back(name);
        Error error;
        (void) self->host->applyEditorAction(*action, error);
        if (error)
            self->lastError = error.code;
    }
    static void onPage(void* context, int page)
    { auto* self = static_cast<Recorder*>(context); self->page = page; self->events.push_back("page:" + std::to_string(page)); }
    static void onTheme(void* context, int theme)
    { auto* self = static_cast<Recorder*>(context); self->theme = theme; self->events.push_back("theme:" + std::to_string(theme)); }
    static void onPopupShown(void* context)
    { auto* self = static_cast<Recorder*>(context); ++self->popupsShown; self->events.push_back("popupShown"); }
    static void onPopupDismissed(void* context)
    { static_cast<Recorder*>(context)->events.push_back("popupDismissed"); }
    static void onHistory(void* context, bool redo)
    { static_cast<Recorder*>(context)->events.push_back(redo ? "history:redo" : "history:undo"); }
    static void onFocus(void* context, bool focused)
    { static_cast<Recorder*>(context)->events.push_back(focused ? "focus:in" : "focus:out"); }
};

// The standalone app polls analysis and republishes the editor snapshot every
// 20 ms; the matrix does the same while it waits, so the editor sees history,
// source and preview state exactly as it would in the app.
std::function<void()> gPump;

void drain(NSApplication* application, double seconds)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    auto nextPump = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (gPump && std::chrono::steady_clock::now() >= nextPump)
        {
            gPump();
            nextPump = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
        }
        NSEvent* event = [application nextEventMatchingMask:NSEventMaskAny
                                                  untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
                                                     inMode:NSDefaultRunLoopMode
                                                    dequeue:YES];
        if (event != nil)
            [application sendEvent:event];
    }
}

void writeWave(const std::filesystem::path& path)
{
    constexpr int rate = 44100;
    // Six seconds, so an analysis is still running when CANCEL is clicked.
    std::vector<std::int16_t> samples(rate * 6);
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = static_cast<std::int16_t>(
            9000.0 * std::sin(2.0 * 3.141592653589793 * 440.0 * static_cast<double>(i) / rate));
    std::ofstream out(path, std::ios::binary);
    auto u32 = [&out](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&out](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const auto bytes = static_cast<std::uint32_t>(samples.size() * 2);
    out.write("RIFF", 4); u32(36 + bytes); out.write("WAVEfmt ", 8); u32(16); u16(1); u16(1);
    u32(rate); u32(rate * 2); u16(2); u16(16); out.write("data", 4); u32(bytes);
    out.write(reinterpret_cast<const char*>(samples.data()), bytes);
}
}

int runDispatchMatrix(NSApplication* application, const char* logPath, const char* revision)
{
    std::ofstream log(logPath, std::ios::trunc);
    if (!log)
        return 40;

    host::HostAdapter host;
    Recorder recorder;
    recorder.host = &host;
    ui::EditorEventSink sink;
    sink.context = &recorder;
    sink.action = &Recorder::onAction;
    sink.pageSelected = &Recorder::onPage;
    sink.themeSelected = &Recorder::onTheme;
    sink.popupShown = &Recorder::onPopupShown;
    sink.popupDismissed = &Recorder::onPopupDismissed;
    sink.historyAction = &Recorder::onHistory;
    sink.focusChanged = &Recorder::onFocus;
    ui::VisageEditor editor(sink);
    if (!host.initialize(host::HostFormat::standalone) ||
        !host.attachStandaloneEditor(editor, 1.0f, 1280.0f, 940.0f))
        return 41;
    // Real default-output device, silent: the callback renders the host but
    // the speakers receive zeros.
    StandaloneAudioOutput audio;
    std::string audioError;
    const bool audioRunning = audio.start(host, true, audioError);
    gPump = [&host, &editor] {
        Error ignored;
        (void) host.pollAnalysis(ignored);
        editor.setEditorSnapshot(host.editorSnapshot());
    };
    drain(application, 0.3);
    NSView* view = (__bridge NSView*) editor.nativeHandle();
    if (view == nil || view.window == nil || ![view.window makeFirstResponder:view])
        return 42;
    // Visage renders (and finishes popup fades) only for an on-screen window.
    // With the session locked or the window occluded nothing here can be
    // observed, so report a ctest skip (77) rather than a pass or a failure.
    bool locked = false;
    if (CFDictionaryRef session = CGSessionCopyCurrentDictionary())
    {
        const auto value = static_cast<CFBooleanRef>(
            CFDictionaryGetValue(session, CFSTR("CGSSessionScreenIsLocked")));
        locked = value != nullptr && CFBooleanGetValue(value);
        CFRelease(session);
    }
    if (locked || !(view.window.occlusionState & NSWindowOcclusionStateVisible))
    {
        log << "{\"skipped\":\"" << (locked ? "screen locked" : "window not visible") << "\"}\n";
        std::cout << "dispatch matrix: skipped (" << (locked ? "screen locked" : "window not visible")
                  << ")\n";
        return 77;
    }

    int eventNumber = 1;
    auto mouse = [&](NSEventType type, CGFloat x, CGFloat y) {
        const NSPoint point = [view convertPoint:NSMakePoint(x, view.bounds.size.height - y) toView:nil];
        NSEvent* event = [NSEvent mouseEventWithType:type location:point modifierFlags:0 timestamp:0.0
                                        windowNumber:view.window.windowNumber context:nil
                                         eventNumber:eventNumber++ clickCount:1
                                            pressure:type == NSEventTypeLeftMouseUp ? 0.0 : 1.0];
        switch (type)
        {
        case NSEventTypeLeftMouseDown: [view mouseDown:event]; break;
        case NSEventTypeLeftMouseDragged: [view mouseDragged:event]; break;
        case NSEventTypeMouseMoved: [view mouseMoved:event]; break;
        default: [view mouseUp:event]; break;
        }
    };
    auto click = [&](CGFloat x, CGFloat y) {
        mouse(NSEventTypeMouseMoved, x, y);
        mouse(NSEventTypeLeftMouseDown, x, y);
        mouse(NSEventTypeLeftMouseUp, x, y);
        drain(application, 0.08);
    };
    auto drag = [&](CGFloat x0, CGFloat y0, CGFloat x1, CGFloat y1) {
        mouse(NSEventTypeLeftMouseDown, x0, y0);
        mouse(NSEventTypeLeftMouseDragged, (x0 + x1) * 0.5, (y0 + y1) * 0.5);
        mouse(NSEventTypeLeftMouseDragged, x1, y1);
        mouse(NSEventTypeLeftMouseUp, x1, y1);
        drain(application, 0.08);
    };
    auto type = [&](const std::string& text) {
        for (const char c : text)
        {
            NSString* characters = [NSString stringWithFormat:@"%c", c];
            for (NSEventType kind : { NSEventTypeKeyDown, NSEventTypeKeyUp })
            {
                NSEvent* event = [NSEvent keyEventWithType:kind location:NSZeroPoint modifierFlags:0
                                                 timestamp:0.0 windowNumber:view.window.windowNumber
                                                   context:nil characters:characters
                                  charactersIgnoringModifiers:characters isARepeat:NO keyCode:0];
                if (kind == NSEventTypeKeyDown) [view keyDown:event];
                else [view keyUp:event];
            }
        }
        drain(application, 0.08);
    };
    auto parameter = [&host](const char* id) {
        return host.parameters().read(ParameterRegistry::find(id)->denseIndex);
    };

    int failures = 0;
    int stepIndex = 0;
    auto step = [&](const std::string& name, const std::string& expectation,
                    const std::function<bool()>& run) {
        recorder.actions.clear();
        recorder.events.clear();
        recorder.lastError.clear();
        const bool ok = run();
        drain(application, 0.05);
        const auto telemetry = host.editorSnapshot().telemetry;
        std::ostringstream line;
        line << "{\"step\":" << stepIndex++ << ",\"name\":\"" << escape(name) << "\",\"expect\":\""
             << escape(expectation) << "\",\"pass\":" << (ok ? "true" : "false") << ",\"actions\":[";
        for (std::size_t i = 0; i < recorder.actions.size(); ++i)
            line << (i ? "," : "") << '"' << escape(recorder.actions[i]) << '"';
        line << "],\"events\":[";
        for (std::size_t i = 0; i < recorder.events.size(); ++i)
            line << (i ? "," : "") << '"' << escape(recorder.events[i]) << '"';
        line << "],\"error\":\"" << escape(recorder.lastError) << "\",\"telemetry\":{\"state\":\""
             << uiStateName(telemetry.state) << "\",\"analysisRunning\":" << telemetry.analysisRunning
             << ",\"previewPlaying\":" << telemetry.previewPlaying << ",\"canUndo\":" << telemetry.canUndo
             << ",\"canRedo\":" << telemetry.canRedo << ",\"hasSourceA\":" << telemetry.hasSourceA
             << ",\"status\":\"" << escape(telemetry.status) << "\",\"error\":\""
             << escape(telemetry.error) << "\"}}";
        log << line.str() << '\n';
        log.flush();
        std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
        if (!ok)
            ++failures;
    };

    log << "{\"run\":\"dispatch-matrix\",\"revision\":\"" << escape(revision)
#if defined(__arm64__)
        << "\",\"arch\":\"arm64\""
#else
        << "\",\"arch\":\"x86_64\""
#endif
        << ",\"logicalSize\":[1280,940],\"backingScale\":" << view.window.backingScaleFactor
        << ",\"audio\":{\"running\":" << (audioRunning ? "true" : "false") << ",\"sampleRate\":"
        << audio.sampleRate() << ",\"silent\":true,\"error\":\"" << escape(audioError) << "\"}}\n";

    // Pages: the five tabs are 128 px wide, centred at y 92.
    const CGFloat tabX[] { 384.0, 512.0, 640.0, 768.0, 896.0 };
    for (int page = 0; page < 5; ++page)
        step("page tab " + std::to_string(page), "pageSelected == " + std::to_string(page), [&] {
            click(tabX[page], 92.0);
            return recorder.page == page;
        });

    // Slider (ANALYSIS: Lowest frequency).
    step("slider drag analysisLowestFreq", "begin/end gesture and a new value", [&] {
        click(tabX[1], 92.0);
        const auto before = parameter("analysisLowestFreq");
        drag(40.0, 313.0, 100.0, 313.0);
        return parameter("analysisLowestFreq") != before && recorder.actions.size() >= 2;
    });

    // Combo (ANALYSIS: Window type) — popup opens under the click; pick row 2.
    step("combo analysisWinType -> Hanning", "popup shown, value 2", [&] {
        const int shownBefore = recorder.popupsShown;
        click(240.0, 439.0);
        const bool shown = recorder.popupsShown > shownBefore || !recorder.events.empty();
        for (int attempt = 0; attempt < 3 && std::lround(parameter("analysisWinType")) != 2; ++attempt)
            click(290.0, 511.0);
        return shown && std::lround(parameter("analysisWinType")) == 2;
    });

    // Toggle (SYNTHESIS: Use partial phases).
    step("toggle usePhase", "value flips", [&] {
        click(tabX[2], 92.0);
        const auto before = parameter("usePhase");
        click(180.0, 468.0);
        return parameter("usePhase") != before;
    });

    // Segmented (SYNTHESIS: Both playback mode -> Layer mix).
    step("segmented classicPreviewMode -> Layer mix", "value 1", [&] {
        click(295.0, 535.0);
        return std::lround(parameter("classicPreviewMode")) == 1;
    });

    // Theme popup: open from the header chip, choose Porcelain, then Ember.
    step("theme popup -> Porcelain -> Ember", "themeSelected 1 then 0", [&] {
        click(1180.0, 22.0);
        for (int attempt = 0; attempt < 3 && recorder.theme != 1; ++attempt)
            click(1120.0, 45.0);
        const bool porcelain = recorder.theme == 1 && editor.theme() == ui::ThemeId::porcelain;
        click(1180.0, 22.0);
        for (int attempt = 0; attempt < 3 && recorder.theme != 0; ++attempt)
            click(1120.0, 23.0);
        return porcelain && recorder.theme == 0 && editor.theme() == ui::ThemeId::ember;
    });

    // Presets popup: factory "Bright partials" (row 1 of the list at y 40).
    step("presets popup -> Bright partials", "ApplyFactoryPreset(1), partGain 1.6", [&] {
        click(178.0, 22.0);
        for (int attempt = 0; attempt < 3 && std::abs(parameter("partGain") - 1.6f) > 1.0e-3f; ++attempt)
            click(180.0, 77.0);
        return std::abs(parameter("partGain") - 1.6f) < 1.0e-3f;
    });

    // Undo / redo from the header buttons (enabled by the pumped snapshot).
    step("undo", "HistoryAction(undo); preset reverted", [&] {
        drain(application, 0.1);
        const bool couldUndo = host.editorSnapshot().telemetry.canUndo;
        click(282.0, 22.0);
        return couldUndo && std::abs(parameter("partGain") - 1.6f) > 1.0e-3f;
    });
    step("redo", "HistoryAction(redo); preset re-applied", [&] {
        drain(application, 0.1);
        click(350.0, 22.0);
        return std::abs(parameter("partGain") - 1.6f) < 1.0e-3f;
    });

    // Source import into the current model: the ANALYSIS page's IMPORT WAV
    // opens a native panel; its accepted result is the typed action below,
    // delivered through the same editor action path.
    const auto wav = std::filesystem::temp_directory_path() / "ats-dispatch-matrix.wav";
    writeWave(wav);
    step("import current source", "ImportWav(slot 3) accepted", [&] {
        const ui::EditorAction import = ui::ImportWavAction { { wav.u8string() }, 3 };
        Recorder::onAction(&recorder, &import);
        drain(application, 0.1);
        return recorder.lastError.empty() && host.editorSnapshot().telemetry.state == ui::UiState::readyToAnalyze;
    });
    // ATSA cannot abort a frame loop, so CANCEL discards the result when the
    // worker returns; the step waits for that and records the latency.
    step("ANALYZE CURRENT then CANCEL", "analysis starts; CANCEL discards it (no current model)", [&] {
        click(tabX[1], 92.0);
        click(1103.0, 226.0);
        const bool started = host.telemetry().analysisRunning;
        const auto cancelledAt = std::chrono::steady_clock::now();
        click(1103.0, 298.0); // CANCEL (AUTO-TUNE sits between it and ANALYZE)
        for (int i = 0; i < 1500 && host.telemetry().analysisRunning; ++i)
            drain(application, 0.01);
        const auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - cancelledAt).count();
        recorder.events.push_back("cancelLatencyMs:" + std::to_string(latency));
        return started && !host.telemetry().analysisRunning &&
               !host.editorSnapshot().telemetry.hasCurrentModel;
    });
    // AUTO-TUNE runs on the analysis worker and shares its CANCEL; a full
    // search of this six-second clip is too slow for the matrix, so the step
    // checks the button starts it and CANCEL leaves the settings untouched.
    step("AUTO-TUNE then CANCEL", "auto-tune starts; CANCEL leaves analysis settings unchanged", [&] {
        const auto lowest = parameter("analysisLowestFreq");
        const auto cycles = parameter("analysisWinCycles");
        click(1103.0, 262.0);
        const bool started = host.telemetry().analysisRunning;
        click(1103.0, 298.0);
        for (int i = 0; i < 1500 && host.telemetry().analysisRunning; ++i)
            drain(application, 0.01);
        return started && !host.telemetry().analysisRunning && recorder.lastError.empty()
            && parameter("analysisLowestFreq") == lowest && parameter("analysisWinCycles") == cycles
            && !host.editorSnapshot().telemetry.hasCurrentModel;
    });
    step("ANALYZE CURRENT to completion", "current model analysed", [&] {
        click(1103.0, 226.0);
        for (int i = 0; i < 300 && host.telemetry().analysisRunning; ++i)
            drain(application, 0.02);
        return !host.telemetry().analysisRunning && host.editorSnapshot().telemetry.hasCurrentModel &&
               recorder.lastError.empty();
    });

    // Preview CURRENT from the SESSION transport button.
    step("preview play button", "Preview start; device callbacks render audio", [&] {
        click(tabX[0], 92.0);
        drain(application, 0.1);
        const auto callbacksBefore = audio.callbacks();
        click(228.0, 423.0);
        drain(application, 0.4);
        return audioRunning && host.telemetry().previewPlaying &&
               audio.callbacks() > callbacksBefore && audio.peak() > 1.0e-4f;
    });
    step("preview stop button", "second click stops a playing preview", [&] {
        const bool wasPlaying = host.telemetry().previewPlaying;
        click(228.0, 423.0);
        return wasPlaying && !host.telemetry().previewPlaying;
    });

    // Text commit (MATH): focus the script well, type, apply.
    step("math script text commit", "ApplyMath carries the typed script", [&] {
        click(tabX[4], 92.0);
        click(680.0, 500.0);
        type("a=a*0.5");
        click(1195.0, 889.0);
        for (const auto& action : recorder.actions)
            if (action == "ApplyMath")
                return true;
        return false;
    });

    // Resize through the host seam to the negotiated extremes.
    step("resize min/max", "editor adopts 1100x860 and 1900x1400", [&] {
        std::uint32_t w = 0, h = 0;
        const bool small = host.setEditorLogicalSize(1100.0f, 860.0f);
        editor.getSize(w, h);
        const bool smallOk = small && w == 1100 && h == 860;
        const bool large = host.setEditorLogicalSize(1900.0f, 1400.0f);
        editor.getSize(w, h);
        return smallOk && large && w == 1900 && h == 1400;
    });

    step("teardown", "audio stopped, detached, not showing", [&] {
        audio.stop();
        host.detachEditor();
        return !host.editorAttached() && !editor.isShowing();
    });
    std::error_code ignored;
    std::filesystem::remove(wav, ignored);
    log << "{\"summary\":{\"steps\":" << stepIndex << ",\"failures\":" << failures << "}}\n";
    std::cout << "dispatch matrix: " << stepIndex - failures << "/" << stepIndex << " steps passed\n";
    return failures == 0 ? 0 : 43;
}

} // namespace ats::standalone
