// Proves that each widget type in VisageWidgets.h correctly routes
// gesture input through the real ParameterActionDispatcher.
//
// This test does NOT exercise AppKit mouse routing — that path is
// covered by real_dispatch_harness_test.mm.  Instead it calls the
// public handle* methods directly, which are the same code paths that
// the onMouseDown/Drag/Up lambdas invoke.  This keeps the test
// deterministic and independent of coordinate scaling.

#include "../../src/ui/VisageWidgets.h"
#include "../../src/ui/ParameterBinding.h"
#include "../../src/core/ParameterRegistry.h"

#import <Cocoa/Cocoa.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{

[[noreturn]] void fail(const char* expr, int line)
{
    std::fprintf(stderr, "ats_widget_dispatch_test check failed at line %d: %s\n", line, expr);
    std::abort();
}

#define REQUIRE(e) ((e) ? (void)0 : fail(#e, __LINE__))

// -----------------------------------------------------------------------
// Harness helpers
// -----------------------------------------------------------------------

struct Accepted
{
    std::vector<ats::ui::SetParameterAction> actions;
    void operator()(const ats::ui::SetParameterAction& a) { actions.push_back(a); }
};

ats::ui::ThemeTokens defaultTheme()
{
    return ats::ui::ThemeTokens::forTheme(ats::ui::ThemeId::ember);
}

struct Harness
{
    ats::ParameterStore store;
    Accepted accepted;
    ats::ui::ParameterActionDispatcher dispatcher { store, std::ref(accepted) };

    ats::ui::ParameterBinding makeBind(const char* id)
    {
        return { store, std::string(id), dispatcher };
    }
};

// -----------------------------------------------------------------------
// AtsSlider
// -----------------------------------------------------------------------

void testSlider()
{
    Harness h;
    auto binding = h.makeBind("analysisLowestFreq");
    REQUIRE(binding.valid());

    ats::ui::AtsSlider slider(binding, defaultTheme);
    // Width is used to convert absolute x → normalized; set a known value.
    slider.setBounds(0.0f, 0.0f, 200.0f, 24.0f);

    slider.handleDragBegin(0.25f);
    REQUIRE(binding.gestureActive());
    REQUIRE(h.accepted.actions.size() == 1);
    REQUIRE(h.accepted.actions[0].begin);
    REQUIRE(!h.accepted.actions[0].end);

    slider.handleDragMove(0.5f);
    REQUIRE(h.accepted.actions.size() == 2);

    slider.handleDragEnd(0.75f);
    REQUIRE(!binding.gestureActive());
    REQUIRE(h.accepted.actions.size() == 3);
    REQUIRE(h.accepted.actions[2].end);

    // Value is clamped to parameter range
    const auto* d = ats::ParameterRegistry::find("analysisLowestFreq");
    REQUIRE(d != nullptr);
    const float v = h.store.read("analysisLowestFreq");
    REQUIRE(v >= d->minimum && v <= d->maximum);

    // Out-of-range normalized is clamped to [0,1] before denorm
    h.accepted.actions.clear();
    slider.handleDragBegin(-0.5f);
    REQUIRE(h.accepted.actions.size() == 1);
    slider.handleDragEnd(1.5f);
    REQUIRE(h.accepted.actions.size() == 2);
}

// -----------------------------------------------------------------------
// AtsToggle
// -----------------------------------------------------------------------

void testToggle()
{
    Harness h;
    auto binding = h.makeBind("usePhase");
    REQUIRE(binding.valid());

    ats::ui::AtsToggle toggle(binding, "PHASE", defaultTheme);

    const float before = h.store.read("usePhase");

    toggle.handleClick();
    REQUIRE(h.accepted.actions.size() == 1);
    const float after = h.store.read("usePhase");
    // Value must have flipped
    REQUIRE(std::abs(after - before) > 0.4f);

    toggle.handleClick();
    REQUIRE(h.accepted.actions.size() == 2);
    const float restored = h.store.read("usePhase");
    REQUIRE(std::abs(restored - before) < 0.01f);
}

// -----------------------------------------------------------------------
// AtsCombo
// -----------------------------------------------------------------------

void testCombo()
{
    Harness h;
    auto binding = h.makeBind("freqShapeMode");
    REQUIRE(binding.valid());

    const auto* d = ats::ParameterRegistry::find("freqShapeMode");
    REQUIRE(d != nullptr && d->choiceCount >= 2);

    ats::ui::AtsCombo combo(binding, defaultTheme);

    combo.handleSelect(0);
    REQUIRE(h.accepted.actions.size() == 1);
    REQUIRE(h.store.read("freqShapeMode") == 0.0f);

    combo.handleSelect(1);
    REQUIRE(h.accepted.actions.size() == 2);
    REQUIRE(h.store.read("freqShapeMode") == 1.0f);

    // Selecting the same index again still emits an action (needed for
    // host automation acknowledgment)
    combo.handleSelect(1);
    REQUIRE(h.accepted.actions.size() == 3);
}

// -----------------------------------------------------------------------
// AtsSegmented
// -----------------------------------------------------------------------

void testSegmented()
{
    Harness h;
    auto binding = h.makeBind("morphGridType");
    REQUIRE(binding.valid());

    const auto* d = ats::ParameterRegistry::find("morphGridType");
    REQUIRE(d != nullptr && d->choiceCount >= 2);

    std::vector<std::string> labels;
    for (std::uint16_t i = 0; i < d->choiceCount; ++i)
        labels.push_back(d->choices ? std::string(d->choices[i]) : std::to_string(i));

    ats::ui::AtsSegmented seg(binding, labels, defaultTheme);
    seg.setBounds(0.0f, 0.0f, 300.0f, 24.0f);

    // Click at x=10 → first segment (300 / N segments, first segment starts at 0)
    seg.handleClick(10.0f);
    REQUIRE(h.accepted.actions.size() == 1);
    REQUIRE(h.store.read("morphGridType") == 0.0f);

    // Click near right end → last segment
    seg.handleClick(290.0f);
    REQUIRE(h.accepted.actions.size() == 2);
    REQUIRE(h.store.read("morphGridType") >= 1.0f);
}

// -----------------------------------------------------------------------
// AtsTextField
// -----------------------------------------------------------------------

void testTextField()
{
    std::vector<std::string> committed;
    ats::ui::AtsTextField field(
        [&committed](const std::string& text) { committed.push_back(text); },
        defaultTheme);

    field.handleCharInput('h');
    field.handleCharInput('i');
    REQUIRE(field.text() == "hi");

    field.handleBackspace();
    REQUIRE(field.text() == "h");

    field.handleCommit();
    REQUIRE(committed.size() == 1);
    REQUIRE(committed[0] == "h");

    field.setText("reset");
    REQUIRE(field.text() == "reset");

    field.handleCommit();
    REQUIRE(committed.size() == 2);
    REQUIRE(committed[1] == "reset");
}

// -----------------------------------------------------------------------
// AtsTooltip (pure show/hide, no dispatcher wiring)
// -----------------------------------------------------------------------

void testTooltip()
{
    ats::ui::AtsTooltip tooltip(defaultTheme);
    tooltip.showAt(10.0f, 20.0f, "hello tooltip");
    tooltip.hideTooltip();
    // Verify text is cleared — no double-free / crash
    tooltip.showAt(0.0f, 0.0f, "second show");
    tooltip.hideTooltip();
}

// -----------------------------------------------------------------------
// AtsPageContainer
// -----------------------------------------------------------------------

void testPageContainer()
{
    std::size_t lastPage = 999;
    ats::ui::AtsPageContainer container(
        { "Session", "Analysis", "Synthesis" },
        defaultTheme,
        [&lastPage](std::size_t p) { lastPage = p; });
    container.setBounds(0.0f, 0.0f, 300.0f, 120.0f);

    REQUIRE(container.currentPage() == 0);

    // Tab width = 300 / 3 = 100; tab 1 starts at x=100
    container.handlePageClick(110.0f);
    REQUIRE(container.currentPage() == 1);
    REQUIRE(lastPage == 1);

    container.handlePageClick(210.0f);
    REQUIRE(container.currentPage() == 2);
    REQUIRE(lastPage == 2);

    container.selectPage(0);
    REQUIRE(container.currentPage() == 0);
    REQUIRE(lastPage == 0);

    // Out-of-range selectPage is a no-op
    container.selectPage(99);
    REQUIRE(container.currentPage() == 0);
}

} // namespace

int main()
{
    @autoreleasepool
    {
        // Initialize AppKit so that Visage frame internals can be safely
        // constructed.  The application itself never runs its event loop;
        // we just need a valid Cocoa environment.
        NSApplication* application = [NSApplication sharedApplication];
        [application finishLaunching];

        testSlider();
        testToggle();
        testCombo();
        testSegmented();
        testTextField();
        testTooltip();
        testPageContainer();

        std::printf("ats_widget_dispatch_test: all checks passed\n");
    }
    return 0;
}
