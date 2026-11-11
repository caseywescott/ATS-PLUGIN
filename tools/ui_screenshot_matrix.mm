// Deterministic editor screenshot matrix (JUCE removal card 31 / audit T-201).
//
// Renders the production VisageEditor in a real window and saves Visage's own
// composited frame (not a screen grab) for:
//   - five pages x two themes x min/design/max sizes, and
//   - the seven spectrum modes x two themes at the design size,
// using the synthetic-sparse-decay spectrum fixture and registry-default
// parameters. A frame is accepted only once two consecutive composites are
// pixel-identical. The manifest records build revision, arch, the display's
// backing scale (Visage follows the screen; 1x needs a non-Retina display) and
// each capture's pixel size and SHA-256 of the decoded RGBA pixels.
//
// Usage: ui_screenshot_matrix <output-dir> [--revision <git-hash>] [--screen-grab]
//                             [--ats <file.ats>]
// --ats loads a real model through HostAdapter::importSource (the drop/import
// path) as the current model and source A, and captures that instead of the
// fixture.  Not deterministic across revisions of the file; use for review.
// --screen-grab also saves `<name>.screen.png` via `screencapture -l` of the
// tool's own window, to confirm the composite matches what is on screen.
// Needs a logged-in session with the window on screen (the render loop only
// advances for visible windows).

#include "../src/ui/VisageEditor.h"
#include "../src/ui/Editor.h"
#include "../src/ui/SpectralTraceBuilder.h"
#include "../src/core/ParameterRegistry.h"
#include "../src/host/HostAdapter.h"

#import <Cocoa/Cocoa.h>
#include <CommonCrypto/CommonDigest.h>

#include <array>
#include <optional>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
using namespace ats::ui;

constexpr std::array<const char*, 5> kPages { "session", "analysis", "synthesis", "partials", "math" };
constexpr std::array<const char*, 2> kThemes { "ember", "porcelain" };
constexpr std::array<const char*, 7> kModes {
    "combined", "partials", "residual", "isometric",
    "isometricResidual", "isometricEnvelope", "isometricEnvelopeRdc" };

struct SizeCase
{
    const char* name;
    std::uint32_t width;
    std::uint32_t height;
};

void runFor(NSApplication* application, double seconds)
{
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:seconds];
    while ([deadline timeIntervalSinceNow] > 0.0)
    {
        NSEvent* event = [application nextEventMatchingMask:NSEventMaskAny
                                                  untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
                                                     inMode:NSDefaultRunLoopMode
                                                    dequeue:YES];
        if (event != nil)
            [application sendEvent:event];
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
}

struct Capture
{
    std::string sha256;
    long width = 0;
    long height = 0;
};

// Re-encodes Visage's uncompressed PNG through ImageIO (compressed) and hashes
// the decoded RGBA pixels, so the digest is independent of the PNG encoder.
bool recompressAndHash(const std::filesystem::path& path, Capture& out)
{
    NSString* file = [NSString stringWithUTF8String:path.c_str()];
    NSData* data = [NSData dataWithContentsOfFile:file];
    NSBitmapImageRep* rep = data != nil ? [NSBitmapImageRep imageRepWithData:data] : nil;
    if (rep == nil)
        return false;
    NSBitmapImageRep* rgba = [[NSBitmapImageRep alloc]
        initWithBitmapDataPlanes:nullptr pixelsWide:rep.pixelsWide pixelsHigh:rep.pixelsHigh
                   bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO
                  colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:rep.pixelsWide * 4
                    bitsPerPixel:32];
    [NSGraphicsContext saveGraphicsState];
    [NSGraphicsContext setCurrentContext:[NSGraphicsContext graphicsContextWithBitmapImageRep:rgba]];
    [rep drawInRect:NSMakeRect(0, 0, rep.pixelsWide, rep.pixelsHigh)];
    [NSGraphicsContext restoreGraphicsState];
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(rgba.bitmapData, static_cast<CC_LONG>(rgba.bytesPerRow * rgba.pixelsHigh), digest);
    std::ostringstream hex;
    for (const auto byte : digest)
        hex << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    out = { hex.str(), static_cast<long>(rep.pixelsWide), static_cast<long>(rep.pixelsHigh) };
    NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    return png != nil && [png writeToFile:file atomically:YES];
}

// Same construction as tools/spectrum_candidate_export.cpp (fixture
// synthetic-sparse-decay in tests/fixtures/spectrum-parity).
SpectralTraceSnapshot fixture(SpectralTraceMode mode)
{
    SpectralTraceInput input;
    input.source = SpectralTraceSource::current;
    input.sourceRevision = 1;
    input.displayShapeRevision = 1;
    input.durationSeconds = 0.37;
    input.partialCount = 5;
    input.frameCount = 17;
    input.nyquistHz = 24000.0;
    input.displayAmpPeak = 1.0;
    input.displayResidualPeak = 1.0;
    input.onsets = { { 0.78, 0.9, 0.70, 0.82 }, { 0.22, 0.4, 0.18, 0.25 } };
    input.smearnovDetectionCurve = { 0.0, 0.2, 0.4, 0.3, 0.1 };
    input.smearnovThresholdCurve = { 0.1, 0.1, 0.15, 0.2, 0.2 };
    for (int partial = 0; partial < 5; ++partial)
    {
        SpectralPartialInput trace;
        trace.partialIndex = partial;
        trace.frequencyShapeRank = partial + 1;
        for (int frame = 0; frame < 17; ++frame)
        {
            const auto frequency = 220.0 + partial * 180.0 + frame * (partial + 1) * 0.7;
            const auto amplitude = 0.08 + 0.12 * partial + 0.02 * (frame % 4);
            trace.frames.push_back({ frequency, amplitude });
        }
        if (partial == 4)
        {
            trace.frames[0] = { 0.0, 0.0 };
            trace.frames[1] = { 0.0, 0.0 };
            trace.frames[15] = { 0.0, 0.0 };
            trace.frames[16] = { 0.0, 0.0 };
        }
        input.partials.push_back(std::move(trace));
    }
    for (int band = 0; band < 3; ++band)
    {
        SpectralResidualBandInput trace;
        trace.bandIndex = band;
        trace.lowHz = 100.0 + band * 100.0;
        trace.highHz = trace.lowHz + 100.0;
        for (int frame = 0; frame < 17; ++frame)
            trace.energy.push_back(0.01 * (band + 1) * (1 + frame % 5));
        input.residualBands.push_back(std::move(trace));
    }
    return SpectralTraceBuilder::build(input, mode);
}

EditorSnapshot loadedSession()
{
    EditorSnapshot snapshot;
    snapshot.revision = 1;
    for (const auto& descriptor : ats::ParameterRegistry::descriptors())
        if (descriptor.denseIndex < snapshot.parameters.size())
            snapshot.parameters[descriptor.denseIndex] = descriptor.defaultValue;
    // Index 0 is the current (working) model, 1 and 2 are sources A and B.
    snapshot.sourceAvailable = { true, true, false };
    snapshot.sourceResidualAvailable = { true, true, false };
    snapshot.sourceNames = { "synthetic-sparse-decay", "synthetic-sparse-decay", "" };
    snapshot.sourceDurations = { 0.37f, 0.37f, 0.0f };
    snapshot.sourcePartialCounts = { 5, 5, 0 };
    return snapshot;
}
}

int main(int argc, char** argv)
{
    @autoreleasepool
    {
        if (argc < 2)
        {
            std::cerr << "usage: ui_screenshot_matrix <output-dir> [--revision <git-hash>]\n";
            return 2;
        }
        const std::filesystem::path output = argv[1];
        std::string revision = "unknown";
        bool screenGrab = false;
        std::string atsPath;
        for (int i = 2; i < argc; ++i)
        {
            if (std::string(argv[i]) == "--revision" && i + 1 < argc)
                revision = argv[++i];
            else if (std::string(argv[i]) == "--screen-grab")
                screenGrab = true;
            else if (std::string(argv[i]) == "--ats" && i + 1 < argc)
                atsPath = argv[++i];
        }
        std::optional<EditorSnapshot> realSession;
        if (!atsPath.empty())
        {
            static ats::host::HostAdapter host;
            ats::Error error;
            for (const auto slot : { 3, 0 })
                if (!host.importSource({ atsPath }, slot, ats::engine::SourceImportKind::ats, error))
                {
                    std::cerr << "could not import " << atsPath << ": " << error.text << '\n';
                    return 1;
                }
            realSession = host.editorSnapshot();
            if (!realSession->spectrum)
            {
                std::cerr << "import published no spectrum\n";
                return 1;
            }
        }
        std::filesystem::create_directories(output);

        NSApplication* application = [NSApplication sharedApplication];
        [application finishLaunching];
        [application setActivationPolicy:NSApplicationActivationPolicyAccessory];

        const EditorSizeLimits limits;
        const std::array<SizeCase, 3> sizes { {
            { "min", limits.minWidth, limits.minHeight },
            { "design", limits.defaultWidth, limits.defaultHeight },
            { "max", limits.maxWidth, limits.maxHeight } } };

        std::ostringstream entries;
        bool firstEntry = true;
        int failures = 0;
        double backingScale = 0.0;
        long windowNumber = 0;

        // One window + editor per size; pages, themes and modes are switched in
        // place and each capture waits for two identical consecutive frames.
        auto runSize = [&](const SizeCase& size, auto&& body) {
            NSWindow* window = [[NSWindow alloc]
                initWithContentRect:NSMakeRect(40.0, 40.0, size.width, size.height)
                          styleMask:NSWindowStyleMaskTitled
                            backing:NSBackingStoreBuffered
                              defer:NO];
            [window makeKeyAndOrderFront:nil];
            [application activateIgnoringOtherApps:YES];
            backingScale = window.backingScaleFactor;
            windowNumber = static_cast<long>(window.windowNumber);
            NSView* host = [[NSView alloc] initWithFrame:NSMakeRect(0.0, 0.0, size.width, size.height)];
            [[window contentView] addSubview:host];
            auto editor = std::make_unique<VisageEditor>();
            editor->setSize(size.width, size.height);
            if (!editor->setParent((__bridge void*) host))
            {
                ++failures;
                std::cerr << "editor did not attach at " << size.name << '\n';
                return;
            }
            editor->show();
            editor->setEditorSnapshot(realSession ? *realSession : loadedSession());
            runFor(application, 0.5);
            body(*editor);
            editor.reset();
            [window orderOut:nil];
            runFor(application, 0.1);
        };

        auto capture = [&](VisageEditor& editor, const std::string& name, const SizeCase& size,
                           int theme, int page, int mode) {
            editor.setTheme(theme == 0 ? ThemeId::ember : ThemeId::porcelain);
            if (realSession)
            {
                auto spectrum = *realSession->spectrum;
                spectrum.mode = static_cast<SpectralTraceMode>(mode);
                editor.setSpectrumSnapshot(std::move(spectrum));
            }
            else
                editor.setSpectrumSnapshot(fixture(static_cast<SpectralTraceMode>(mode)));
            editor.selectPage(page);
            const auto file = output / (name + ".png");
            Capture previous;
            Capture current;
            bool stable = false;
            for (int attempt = 0; attempt < 12 && !stable; ++attempt)
            {
                runFor(application, 0.25);
                if (!editor.captureScreenshot(file.string()) || !recompressAndHash(file, current))
                    break;
                stable = attempt > 0 && current.sha256 == previous.sha256;
                previous = current;
            }
            if (!stable)
            {
                ++failures;
                std::cerr << "capture did not settle: " << name << '\n';
                return;
            }
            if (screenGrab)
            {
                const auto grab = output / (name + ".screen.png");
                const auto command = "/usr/sbin/screencapture -x -o -l" + std::to_string(windowNumber)
                                   + " '" + grab.string() + "'";
                if (std::system(command.c_str()) != 0)
                    std::cerr << "screen grab failed: " << name << '\n';
            }
            entries << (firstEntry ? "" : ",\n") << "    {\"name\":\"" << name
                    << "\",\"page\":\"" << kPages[static_cast<std::size_t>(page)]
                    << "\",\"theme\":\"" << kThemes[static_cast<std::size_t>(theme)]
                    << "\",\"mode\":\"" << kModes[static_cast<std::size_t>(mode)]
                    << "\",\"size\":\"" << size.name << "\",\"logical\":[" << size.width << ','
                    << size.height << "],\"pixels\":[" << current.width << ',' << current.height
                    << "],\"sha256\":\"" << current.sha256 << "\"}";
            firstEntry = false;
            std::cout << "captured " << name << '\n';
        };

        for (const auto& size : sizes)
            runSize(size, [&](VisageEditor& editor) {
                for (int page = 0; page < static_cast<int>(kPages.size()); ++page)
                    for (int theme = 0; theme < static_cast<int>(kThemes.size()); ++theme)
                        capture(editor, std::string("page-") + kPages[static_cast<std::size_t>(page)]
                                            + '-' + kThemes[static_cast<std::size_t>(theme)] + '-'
                                            + size.name,
                                size, theme, page, 0);
                if (std::string(size.name) != "design")
                    return;
                for (int mode = 0; mode < static_cast<int>(kModes.size()); ++mode)
                    for (int theme = 0; theme < static_cast<int>(kThemes.size()); ++theme)
                        capture(editor, std::string("spectrum-") + kModes[static_cast<std::size_t>(mode)]
                                            + '-' + kThemes[static_cast<std::size_t>(theme)],
                                size, theme, 0, mode);
            });

#if defined(__arm64__)
        const char* arch = "arm64";
#else
        const char* arch = "x86_64";
#endif
        std::ofstream manifest(output / "manifest.json");
        manifest << "{\n  \"schema\": 1,\n  \"revision\": \"" << revision << "\",\n  \"arch\": \""
                 << arch << "\",\n  \"backingScale\": " << backingScale
                 << ",\n  \"fixture\": \"synthetic-sparse-decay\",\n  \"captures\": [\n"
                 << entries.str() << "\n  ]\n}\n";
        std::cout << "failures: " << failures << '\n';
        return failures == 0 ? 0 : 1;
    }
}
