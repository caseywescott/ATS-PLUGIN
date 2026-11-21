#pragma once

// Separate visage::Frame subclasses for each editor page.
// Each page is independent: it owns its child widget frames, manages
// the parameter-value cache it displays, and emits EditorActions through
// the supplied Emitter callback.
//
// Page frames intentionally do not depend on HostAdapter or VisageEditor;
// the production editor shell creates each page,
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


// A heading occupies a slot in the column just like a control, so its height
// has to be known before the walk. Pairs with paintSectionHeading below.
inline LayoutItem headingItem(bool withTopDivider)
{
    return { ThemeRecipes::sectionHeadingHeight(withTopDivider), 0.0f,
             LayoutItemKind::heading, false };
}

inline void paintSectionHeading(visage::Canvas& canvas, const ThemeTokens& tokens,
                                const char* text, UiRect slot, bool withTopDivider)
{
    if (withTopDivider)
        paintThemeOps(canvas, ThemeRecipes::engravedDivider(tokens, slot,
                                                            ThemeRecipes::kSectionLabelTopPad));
    const visage::Font font(ThemeFont::sectionLabel, visage::fonts::Lato_Regular_ttf);
    const float top = slot.y + ThemeRecipes::sectionLabelTop(withTopDivider);
    const float textWidth = font.stringWidth(visage::String(text).toUtf32());
    canvas.setColor(tokens.textMuted.argb);
    canvas.text(text, font, visage::Font::kLeft, slot.x, top, slot.width,
                ThemeRecipes::kSectionLabelH);
    paintThemeOps(canvas, ThemeRecipes::sectionRule(
        tokens, { slot.x, top, slot.width, ThemeRecipes::kSectionLabelH }, textWidth));
}

// Every control row is a label above the widget; the widget frame draws itself,
// so the page only has to place the label in the top strip of the slot.
inline void paintControlLabel(visage::Canvas& canvas, const ThemeTokens& tokens,
                              const char* text, UiRect slot)
{
    const visage::Font font(ThemeFont::sectionLabel, visage::fonts::Lato_Regular_ttf);
    canvas.setColor(tokens.textSecondary.argb);
    canvas.text(text, font, visage::Font::kLeft, slot.x, slot.y, slot.width,
                ThemeRecipes::kControlLabelH);
}

inline void paintPagePanel(visage::Canvas& canvas, const ThemeTokens& tokens, UiRect r)
{
    paintThemeOps(canvas, ThemeRecipes::raisedPanel(tokens, r, ThemeRadius::bodyColumnPanel));
}


// -----------------------------------------------------------------------
// Page row tables
// -----------------------------------------------------------------------
// Section 10 orders each column by hand rather than by registry order, so
// every page declares its column as a table of rows and hands that to the
// §10.7 walk. Headings and captions are drawn from the same table the widgets
// are placed from, which is what keeps paint and hit-testing in step.
// -----------------------------------------------------------------------

enum class PageRowKind : std::uint8_t
{
    control,         // a child widget frame; droppable by the overflow guard
    heading,         // section label
    dividedHeading,  // section label under a §7.8 engraved divider
    custom           // drawn and hit-tested by the page itself
};

struct PageRow
{
    PageRowKind kind = PageRowKind::control;
    int index = -1;          // widget index for controls, page-defined id for custom
    const char* label = nullptr;
    float height = 0.0f;     // headings take theirs from the recipe
    float gapAbove = 0.0f;
    bool pairNext = false;
    bool labelled = false;   // reserve a caption strip above the widget
};

inline std::vector<LayoutItem> pageLayoutItems(const std::vector<PageRow>& rows)
{
    std::vector<LayoutItem> items;
    items.reserve(rows.size());
    for (const auto& row : rows)
    {
        if (row.kind == PageRowKind::heading || row.kind == PageRowKind::dividedHeading)
            items.push_back(headingItem(row.kind == PageRowKind::dividedHeading));
        else
            items.push_back({ row.height, row.gapAbove, LayoutItemKind::control, row.pairNext });
    }
    return items;
}

inline std::vector<LayoutSlot> pageRowSlots(const std::vector<PageRow>& rows, UiRect area)
{
    return ThemeLayout::column(pageLayoutItems(rows), area);
}

// A labelled row is a caption strip above the widget; an unlabelled one (a
// segmented or pill row) gives the widget the whole slot.
inline UiRect pageRowWidgetArea(const PageRow& row, UiRect slot)
{
    if (!row.labelled)
        return slot;
    const float pad = ThemeRecipes::kControlLabelH;
    return { slot.x, slot.y + pad, slot.width, std::max(0.0f, slot.height - pad) };
}

inline bool rectContains(UiRect r, float x, float y)
{
    return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
}

inline void placePageWidgets(const std::vector<PageRow>& rows,
                             const std::vector<LayoutSlot>& slots,
                             const std::vector<visage::Frame*>& widgets)
{
    for (std::size_t i = 0; i < rows.size() && i < slots.size(); ++i)
    {
        if (rows[i].kind != PageRowKind::control)
            continue;
        const auto index = static_cast<std::size_t>(rows[i].index);
        if (index >= widgets.size())
            continue;

        widgets[index]->setVisible(slots[i].visible);
        if (!slots[i].visible)
            continue;
        const UiRect b = pageRowWidgetArea(rows[i], slots[i].bounds);
        widgets[index]->setBounds(b.x, b.y, b.width, b.height);
    }
}

// Draws the heading rows and the caption above each labelled control. Custom
// rows are left to the page, which knows what they are.
inline void paintPageRows(visage::Canvas& canvas, const ThemeTokens& tokens,
                          const std::vector<PageRow>& rows,
                          const std::vector<LayoutSlot>& slots,
                          const std::function<const char*(int)>& controlName)
{
    for (std::size_t i = 0; i < rows.size() && i < slots.size(); ++i)
    {
        if (!slots[i].visible)
            continue;
        const auto& row = rows[i];
        if (row.kind == PageRowKind::heading || row.kind == PageRowKind::dividedHeading)
        {
            paintSectionHeading(canvas, tokens, row.label, slots[i].bounds,
                                row.kind == PageRowKind::dividedHeading);
        }
        else if (row.kind == PageRowKind::control && row.labelled && controlName)
        {
            if (const char* name = controlName(row.index))
                paintControlLabel(canvas, tokens, name, slots[i].bounds);
        }
    }
}


// -----------------------------------------------------------------------
// AtsAnalysisPage — §10.5 Analysis page, left + right columns
// -----------------------------------------------------------------------
// Renders 18 analysis parameter controls as AtsSlider (continuous/integer)
// or AtsSegmented (choice/bool) child frames.  Profile selector and
// Analyze/Cancel action buttons are drawn inline.
// -----------------------------------------------------------------------

// Section 7.2 widget choice, shared by every page: booleans are toggle pills,
// choices with more than three options are combos (a segmented radio only
// stays legible up to three short segments), everything else is a slider.
// Pages whose rows carry no caption for a toggle pass its inline label.
inline std::unique_ptr<visage::Frame> makeParameterWidget(
    ParameterBinding& binding, const ParameterDescriptor* descriptor,
    const std::function<ThemeTokens()>& theme, bool labelToggle = false)
{
    if (descriptor == nullptr)
        return std::make_unique<AtsSlider>(binding, theme);
    if (descriptor->type == ParameterType::boolean)
        return std::make_unique<AtsToggle>(
            binding, labelToggle ? std::string(descriptor->name) : std::string(), theme);
    if (descriptor->type != ParameterType::choice)
        return std::make_unique<AtsSlider>(binding, theme);
    if (descriptor->choiceCount > 3)
        return std::make_unique<AtsCombo>(binding, theme);
    std::vector<std::string> segs;
    for (std::uint16_t c = 0; c < descriptor->choiceCount; ++c)
        segs.push_back(descriptor->choices ? std::string(descriptor->choices[c])
                                           : std::to_string(c));
    return std::make_unique<AtsSegmented>(binding, std::move(segs), theme);
}

class AtsAnalysisPage final : public visage::Frame
{
public:
    using ThemeGetter = std::function<ThemeTokens()>;
    using Emitter = std::function<void(const EditorAction&)>;
    // The page owns the button geometry, but the native file dialog and the
    // popup menu need the shell's window handle and event sink, so those two
    // rows call back instead of emitting an action.
    using ChromeRequest = std::function<void(UiRect anchor)>;

    AtsAnalysisPage(AnalysisPageModel& model, Emitter emit, ThemeGetter theme)
        : model_(model), bridge_(emit), dispatcher_(store_, [this](const SetParameterAction& a) {
              bridge_.dispatch(a);
          })
        , emit_(emit)
        , theme_(std::move(theme))
    {
        onMouseDown() += [this](const visage::MouseEvent& event) {
            const auto p = localPoint(*this, event);
            handleClick({ p.x, p.y });
        };
        const auto ids = AnalysisPageModel::controlIds();
        for (std::size_t i = 0; i < ids.size(); ++i)
        {
            const auto* descriptor = ParameterRegistry::find(ids[i]);
            if (descriptor)
                store_.write(ids[i], descriptor->defaultValue);

            auto binding = std::make_unique<ParameterBinding>(store_, std::string(ids[i]),
                                                               dispatcher_);
            bindings_.push_back(std::move(binding));

            auto widget = makeParameterWidget(*bindings_.back(), descriptor, theme_, true);
            widgets_.push_back(widget.get());
            addChild(std::move(widget));
        }
    }

    void setControlValue(std::string_view id, float value)
    {
        store_.write(id, value);
        redraw();
    }

    [[nodiscard]] float controlValue(std::string_view id) const noexcept
    { return store_.read(id); }

    void setProfile(int profile)
    {
        profile_ = profile;
        redraw();
    }

    // Worker state lives in AnalysisPageModel, which the editor updates from
    // telemetry. The page used to keep its own copy that nothing set, so
    // CANCEL never fired and ANALYZE stayed enabled during analysis.
    void setWorkerRunning(bool running)
    {
        model_.setWorkerRunning(running);
        redraw();
    }

    void setChromeHandlers(ChromeRequest importWav, ChromeRequest profileMenu)
    {
        onImportWav_ = std::move(importWav);
        onProfileMenu_ = std::move(profileMenu);
    }

    [[nodiscard]] UiRect sourceCardBounds(int slot) const
    {
        if (slot < 0 || slot > 1)
            return {};
        const auto cols = columnRects();
        if (cols.size() < 3)
            return {};
        const auto& rows = rightRows();
        const auto slots = pageRowSlots(rows, ThemeLayout::panelContent(cols[2]));
        const int target = slot == 0 ? kSourceCardA : kSourceCardB;
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].index == target && slots[i].visible)
                return slots[i].bounds;
        }
        return {};
    }

    void resized() override { layoutWidgets(); }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = theme_();
        const auto cols = columnRects();
        if (cols.empty())
            return;

        for (const auto& col : cols)
            paintPagePanel(canvas, tokens, col);

        // Centre column is the §8.1 spectrogram well, inset like any panel body.
        paintThemeOps(canvas, ThemeRecipes::well(tokens, ThemeLayout::panelContent(cols[1]),
                                                 ThemeRadius::spectrumWell));

        drawLeftColumn(canvas, tokens, ThemeLayout::panelContent(cols[0]));
        drawRightColumn(canvas, tokens, ThemeLayout::panelContent(cols[2]));
    }

private:
    // Ids for the rows the page draws itself. Buttons keep 0..3 so the click
    // switch reads in spec order; the cards sit above them out of the way.
    static constexpr int kProfileCombo = -2;
    static constexpr int kSourceCardA = 10;
    static constexpr int kSourceCardB = 11;

    // Section 10.5's row order is not the registry's, so the walk is driven by
    // an explicit table rather than by iterating controlIds().
public:
    // Public so tests/page_column_layout_test can walk the real tables.
    static const std::vector<PageRow>& leftRows()
    {
        static const std::vector<PageRow> rows {
            { PageRowKind::custom, kProfileCombo, "PROFILE", 42.0f, 0.0f, false, true },
            { PageRowKind::control,          0, nullptr,       26.0f, 0.0f, false, false },
            { PageRowKind::heading,         -1, "RANGE",        0.0f, 0.0f, false, false },
            { PageRowKind::control,          1, nullptr,       42.0f, 0.0f, true,  true },
            { PageRowKind::control,          2, nullptr,       42.0f, 0.0f, false, true },
            { PageRowKind::control,          3, nullptr,       42.0f, 0.0f, true,  true },
            { PageRowKind::control,          4, nullptr,       42.0f, 0.0f, false, true },
            { PageRowKind::dividedHeading,  -1, "TRACKING",     0.0f, 0.0f, false, false },
            { PageRowKind::control,          5, nullptr,       42.0f, 0.0f, false, true },
            { PageRowKind::control,          6, nullptr,       42.0f, 0.0f, true,  true },
            { PageRowKind::control,          7, nullptr,       42.0f, 0.0f, false, true },
            { PageRowKind::control,          8, nullptr,       42.0f, 0.0f, false, true },
            { PageRowKind::control,          9, nullptr,       42.0f, 0.0f, false, true },
            { PageRowKind::control,         10, nullptr,       42.0f, 0.0f, true,  true },
            { PageRowKind::control,         11, nullptr,       42.0f, 0.0f, false, true },
            { PageRowKind::control,         12, nullptr,       42.0f, 0.0f, false, true },
            { PageRowKind::dividedHeading,  -1, "THRESHOLDS",   0.0f, 0.0f, false, false },
            { PageRowKind::control,         13, nullptr,       42.0f, 0.0f, true,  true },
            { PageRowKind::control,         14, nullptr,       42.0f, 0.0f, false, true },
            { PageRowKind::control,         15, nullptr,       42.0f, 0.0f, true,  true },
            { PageRowKind::control,         16, nullptr,       42.0f, 0.0f, false, true },
            { PageRowKind::control,         17, nullptr,       26.0f, 0.0f, false, false },
        };
        return rows;
    }

    static const std::vector<PageRow>& rightRows()
    {
        static const std::vector<PageRow> rows {
            { PageRowKind::heading,        -1, "ACTIONS",         0.0f, 0.0f, false, false },
            { PageRowKind::custom,          0, "IMPORT WAV",     30.0f, 0.0f, false, false },
            { PageRowKind::custom,          1, "ANALYZE CURRENT", 30.0f, 6.0f, false, false },
            // Searches lowest/highest frequency, window cycles and hop for the
            // closest resynthesis, applies them and re-analyses.
            { PageRowKind::custom,          4, "AUTO-TUNE",       30.0f, 6.0f, false, false },
            { PageRowKind::custom,          2, "CANCEL",         30.0f, 6.0f, false, false },
            { PageRowKind::custom,          3, "STOP PREVIEW",   30.0f, 6.0f, false, false },
            { PageRowKind::dividedHeading, -1, "SOURCE A",        0.0f, 0.0f, false, false },
            { PageRowKind::custom, kSourceCardA, nullptr,        84.0f, 0.0f, false, false },
            { PageRowKind::dividedHeading, -1, "SOURCE B",        0.0f, 0.0f, false, false },
            { PageRowKind::custom, kSourceCardB, nullptr,        84.0f, 0.0f, false, false },
        };
        return rows;
    }
private:


    std::vector<UiRect> columnRects() const
    {
        return ThemeLayout::columns({ 0.0f, 0.0f, width(), height() },
                                    { 300.0f, 0.0f, 320.0f });
    }

    static const char* controlName(int index)
    {
        const auto ids = AnalysisPageModel::controlIds();
        const auto* descriptor = ParameterRegistry::find(ids[static_cast<std::size_t>(index)]);
        return descriptor ? descriptor->name.data() : nullptr;
    }

    void drawLeftColumn(visage::Canvas& canvas, const ThemeTokens& tokens, UiRect area)
    {
        const auto& rows = leftRows();
        const auto slots = pageRowSlots(rows, area);
        paintPageRows(canvas, tokens, rows, slots, controlName);

        if (!slots[0].visible)
            return;
        paintControlLabel(canvas, tokens, rows[0].label, slots[0].bounds);
        const UiRect box = pageRowWidgetArea(rows[0], slots[0].bounds);
        paintThemeOps(canvas, ThemeRecipes::well(tokens, box, ThemeRadius::control));
        static constexpr std::array<const char*, 4> kProfiles {
            "Default", "Speech", "Percussive", "Custom" };
        const visage::Font font(ThemeFont::controlLabel, visage::fonts::Lato_Regular_ttf);
        canvas.setColor(tokens.textPrimary.argb);
        canvas.text(kProfiles[static_cast<std::size_t>(std::clamp(profile_, 0, 3))], font,
                    visage::Font::kLeft, box.x + 8.0f, box.y, box.width - 16.0f, box.height);
    }

    void drawRightColumn(visage::Canvas& canvas, const ThemeTokens& tokens, UiRect area)
    {
        const auto& rows = rightRows();
        const auto slots = pageRowSlots(rows, area);
        paintPageRows(canvas, tokens, rows, slots, nullptr);
        const visage::Font font(ThemeFont::button, visage::fonts::Lato_Regular_ttf);

        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].kind != PageRowKind::custom || !slots[i].visible)
                continue;
            const UiRect slot = slots[i].bounds;
            if (rows[i].index >= kSourceCardA)
            {
                paintThemeOps(canvas, ThemeRecipes::well(tokens, slot, ThemeRadius::control));
                continue;
            }
            // CANCEL is live only while the worker runs; the others are the
            // inverse.
            const bool enabled = rows[i].index == 2 ? model_.workerRunning() : !model_.workerRunning();
            paintThemeOps(canvas, ThemeRecipes::buttonBackground(tokens, slot, false, false));
            canvas.setColor(enabled ? tokens.textPrimary.argb : tokens.textFaint.argb);
            canvas.text(rows[i].label, font, visage::Font::kCenter, slot.x, slot.y, slot.width,
                        slot.height);
        }
    }

    // Only the rows the page draws itself are hit-tested here; parameter rows
    // are child frames and were already offered the event.
    void handleClick(UiRect point)
    {
        const auto cols = columnRects();
        if (cols.empty())
            return;

        const auto leftSlots = pageRowSlots(leftRows(), ThemeLayout::panelContent(cols[0]));
        if (leftSlots[0].visible && rectContains(leftSlots[0].bounds, point.x, point.y) &&
            onProfileMenu_)
        {
            onProfileMenu_(pageRowWidgetArea(leftRows()[0], leftSlots[0].bounds));
            return;
        }

        const auto& rows = rightRows();
        const auto rightSlots = pageRowSlots(rows, ThemeLayout::panelContent(cols[2]));
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].kind != PageRowKind::custom || rows[i].index >= kSourceCardA)
                continue;
            if (!rightSlots[i].visible ||
                !rectContains(rightSlots[i].bounds, point.x, point.y))
                continue;
            switch (rows[i].index)
            {
            case 0:
                if (onImportWav_) onImportWav_(rightSlots[i].bounds);
                break;
            case 1:
                if (!model_.workerRunning() && emit_) emit_(AnalyzeCurrentAction {});
                break;
            case 2:
                if (model_.workerRunning() && emit_) emit_(CancelJobsAction {});
                break;
            case 4:
                if (!model_.workerRunning() && emit_) emit_(AutoTuneAnalysisAction {});
                break;
            default:
                if (emit_) emit_(StopPreviewAction {});
                break;
            }
            return;
        }
    }

    void layoutWidgets()
    {
        const auto cols = columnRects();
        if (cols.empty())
            return;
        const auto& rows = leftRows();
        placePageWidgets(rows, pageRowSlots(rows, ThemeLayout::panelContent(cols[0])), widgets_);
    }

    AnalysisPageModel& model_;
    ParameterStore store_;
    PageActionBridge bridge_;
    ParameterActionDispatcher dispatcher_;
    Emitter emit_;
    ChromeRequest onImportWav_;
    ChromeRequest onProfileMenu_;
    ThemeGetter theme_;
    std::vector<std::unique_ptr<ParameterBinding>> bindings_;
    std::vector<visage::Frame*> widgets_;
    int profile_ = 0;
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

            auto widget = makeParameterWidget(*bindings_.back(), descriptor, theme_, true);
            widgets_.push_back(widget.get());
            addChild(std::move(widget));
        }
    }

    void setControlValue(std::string_view id, float value)
    {
        store_.write(id, value);
        redraw();
    }

    [[nodiscard]] float controlValue(std::string_view id) const noexcept
    { return store_.read(id); }

    void resized() override { layoutWidgets(); }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = theme_();
        const auto cols = columnRects();
        if (cols.empty())
            return;

        for (const auto& col : cols)
            paintPagePanel(canvas, tokens, col);

        // Right column is the §8.3 output spectrum well, full height.
        paintThemeOps(canvas, ThemeRecipes::well(tokens, ThemeLayout::panelContent(cols[1]),
                                                 ThemeRadius::spectrumWell));

        const auto& rows = leftRows();
        paintPageRows(canvas, tokens, rows,
                      pageRowSlots(rows, ThemeLayout::panelContent(cols[0])), controlName);
    }

public:
    // Public so tests/page_column_layout_test can walk the real tables.
    static const std::vector<PageRow>& leftRows()
    {
        static const std::vector<PageRow> rows {
            { PageRowKind::heading,        -1, "PITCH",   0.0f, 0.0f, false, false },
            { PageRowKind::control,         0, nullptr,  42.0f, 0.0f, false, true },
            { PageRowKind::control,         1, nullptr,  26.0f, 0.0f, false, false },
            { PageRowKind::control,         2, nullptr,  42.0f, 0.0f, true,  true },
            { PageRowKind::control,         3, nullptr,  42.0f, 0.0f, false, true },
            { PageRowKind::dividedHeading, -1, "TIME",    0.0f, 0.0f, false, false },
            { PageRowKind::control,         4, nullptr,  42.0f, 0.0f, false, true },
            { PageRowKind::control,         5, nullptr,  42.0f, 0.0f, true,  true },
            { PageRowKind::control,         6, nullptr,  42.0f, 0.0f, false, true },
            { PageRowKind::dividedHeading, -1, "RENDER",  0.0f, 0.0f, false, false },
            { PageRowKind::control,         7, nullptr,  26.0f, 0.0f, true,  false },
            { PageRowKind::control,         8, nullptr,  26.0f, 0.0f, false, false },
            { PageRowKind::control,         9, nullptr,  26.0f, 0.0f, true,  false },
            { PageRowKind::control,        10, nullptr,  26.0f, 0.0f, false, false },
            { PageRowKind::control,        11, nullptr,  42.0f, 0.0f, false, true },
        };
        return rows;
    }
private:


    static const char* controlName(int index)
    {
        const auto* descriptor = ParameterRegistry::find(
            kControlIds[static_cast<std::size_t>(index)]);
        return descriptor ? descriptor->name.data() : nullptr;
    }

    std::vector<UiRect> columnRects() const
    {
        return ThemeLayout::columns({ 0.0f, 0.0f, width(), height() }, { 380.0f, 0.0f });
    }

    void layoutWidgets()
    {
        const auto cols = columnRects();
        if (cols.empty())
            return;
        const auto& rows = leftRows();
        placePageWidgets(rows, pageRowSlots(rows, ThemeLayout::panelContent(cols[0])), widgets_);
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
// The production shell coordinates non-parameter session actions.
// -----------------------------------------------------------------------

class AtsSessionPage final : public visage::Frame
{
public:
    using ThemeGetter = std::function<ThemeTokens()>;
    using Emitter = std::function<void(const EditorAction&)>;
    // The FILE rows need the shell's window handle for the native dialog, so
    // they call back rather than emitting an action.
    using ChromeRequest = std::function<void(int slot)>;

    // The spectrogram well and the morph timeline are driven by the shell's
    // spectrum model, which the page cannot reach. The page still owns their
    // geometry, so it hands the rects back and lets the shell render and
    // hit-test inside them.
    struct CentreHandlers
    {
        std::function<void(visage::Canvas&, UiRect well, UiRect timeline)> draw;
        std::function<bool(visage::Point, UiRect well, UiRect timeline,
                           bool shift, bool command, bool alt, bool middle, bool doubleClick)> mouseDown;
        std::function<void(visage::Point, UiRect well)> mouseMove;
        std::function<void()> mouseExit;
        std::function<void(visage::Point, UiRect well)> mouseDrag;
        std::function<void(visage::Point, UiRect well)> mouseUp;
        std::function<bool(visage::Point, float scaleFactor, UiRect well)> magnify;
        std::function<bool(visage::Point, float deltaX, float deltaY, UiRect well,
                           bool shift, bool command, bool alt, bool precise)> wheel;
    };

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

    AtsSessionPage(SessionPageModel& model, Emitter emit, ThemeGetter theme)
        : model_(model)
        , emit_(std::move(emit))
        , bridge_(emit_)
        , dispatcher_(store_, [this](const SetParameterAction& a) { bridge_.dispatch(a); })
        , theme_(std::move(theme))
    {
        onMouseDown() += [this](const visage::MouseEvent& event) {
            handleInteractionClick(localPoint(*this, event), event.isShiftDown(),
                                   event.isMainModifier(), event.isAltDown(), event.isMiddleButton(),
                                   event.repeatClickCount() > 0);
        };
        onMouseMove() += [this](const visage::MouseEvent& event) {
            if (centre_.mouseMove)
                centre_.mouseMove(localPoint(*this, event), centreWell());
        };
        onMouseExit() += [this](const visage::MouseEvent&) {
            if (centre_.mouseExit)
                centre_.mouseExit();
        };
        onMouseDrag() += [this](const visage::MouseEvent& event) {
            if (centre_.mouseDrag)
                centre_.mouseDrag(localPoint(*this, event), centreWell());
        };
        onMouseUp() += [this](const visage::MouseEvent& event) {
            if (centre_.mouseUp)
                centre_.mouseUp(localPoint(*this, event), centreWell());
        };
        onMouseWheel() += [this](const visage::MouseEvent& event) {
            if (!centre_.wheel)
                return false;
            const auto deltaX = event.precise_wheel_delta_x != 0.0f ? event.precise_wheel_delta_x
                                                                     : event.wheel_delta_x;
            const auto deltaY = event.precise_wheel_delta_y != 0.0f ? event.precise_wheel_delta_y
                                                                     : event.wheel_delta_y;
            const auto precise = event.precise_wheel_delta_x != 0.0f
                              || event.precise_wheel_delta_y != 0.0f;
            return centre_.wheel(localPoint(*this, event), deltaX, deltaY, centreWell(),
                                 event.isShiftDown(), event.isMainModifier(), event.isAltDown(), precise);
        };
        for (const auto id : kControlIds)
        {
            const auto* descriptor = ParameterRegistry::find(id);
            if (descriptor)
                store_.write(id, descriptor->defaultValue);

            auto binding = std::make_unique<ParameterBinding>(store_, std::string(id), dispatcher_);
            bindings_.push_back(std::move(binding));

            // The context grid's cells are far too narrow for five segments.
            auto widget = makeParameterWidget(*bindings_.back(), descriptor, theme_);
            widgets_.push_back(widget.get());
            addChild(std::move(widget));
        }
    }

    void setControlValue(std::string_view id, float value)
    {
        store_.write(id, value);
        redraw();
    }

    [[nodiscard]] float controlValue(std::string_view id) const noexcept
    { return store_.read(id); }

    void setChromeHandlers(ChromeRequest importWav, ChromeRequest importAts,
                           ChromeRequest exportAts)
    {
        onImportWav_ = std::move(importWav);
        onImportAts_ = std::move(importAts);
        onExportAts_ = std::move(exportAts);
    }

    void setCentreHandlers(CentreHandlers handlers) { centre_ = std::move(handlers); }

    bool dispatchMagnify(visage::Point point, float scaleFactor)
    {
        return centre_.magnify != nullptr
            && centre_.magnify(point - positionInWindow(), scaleFactor, centreWell());
    }

    [[nodiscard]] UiRect centreWell() const
    {
        return centreRects(panels()).well;
    }

    // Returns the source card bounding rect in page-local coordinates for
    // slot 0 (A) or 1 (B). Used by the shell to position native drop targets
    // after every resize.
    [[nodiscard]] UiRect sourceCardBounds(int slot) const
    {
        if (slot < 0 || slot > 1)
            return {};
        const auto cols = columnRects();
        if (cols.empty())
            return {};
        const auto& rows = leftRows();
        const auto slots = pageRowSlots(rows, ThemeLayout::panelContent(cols[0]));
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].index == kSourceCard + slot && slots[i].visible)
                return slots[i].bounds;
        }
        return {};
    }

    void resized() override { layoutWidgets(); }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = theme_();
        const auto cols = columnRects();
        if (cols.empty())
            return;

        const auto p = panels();
        for (const auto& panel : { p.left, p.spectrum, p.centre, p.right })
            paintPagePanel(canvas, tokens, panel);

        drawCentreColumn(canvas, tokens, p);
        drawLeftColumn(canvas, tokens, ThemeLayout::panelContent(cols[0]));

        const auto rightLayout = rightColumn(p.right, rightTab_);
        paintSegments(canvas, tokens, rightLayout.tabStrip,
                      { kRightTabLabels.begin(), kRightTabLabels.end() }, rightTab_);
        const auto& top = *rightLayout.rows;
        const auto topSlots = pageRowSlots(top, rightLayout.rowArea);
        paintPageRows(canvas, tokens, top, topSlots, controlName);
        paintRightCustomRows(canvas, tokens, top, topSlots);

        const auto& bottom = rightBottomRows();
        const auto bottomSlots = ThemeLayout::columnFromBottom(pageLayoutItems(bottom),
                                                               rightLayout.content);
        paintPageRows(canvas, tokens, bottom, bottomSlots, controlName);
    }

private:
    // Page-defined ids for the two rows the right column draws itself.
    static constexpr int kSnapshotStrip = 100;
    static constexpr int kLinkLanes = 101;

    // ... and for the left column, which is entirely page-drawn.
    static constexpr int kSourceCard = 200;      // +slot
    static constexpr int kModeSegments = 202;
    static constexpr int kAuditionRow = 203;
    static constexpr int kLayerSegments = 204;
    static constexpr int kSmearPill = 205;
    static constexpr int kVariationSlot = 206;   // +index
    static constexpr int kImportWav = 209;
    static constexpr int kImportAts = 210;
    static constexpr int kExportAts = 211;

public:
    // Public so tests/page_column_layout_test can walk the real tables.
    static const std::vector<PageRow>& leftRows()
    {
        static const std::vector<PageRow> rows {
            { PageRowKind::heading,        -1, "SOURCES",       0.0f, 0.0f, false, false },
            { PageRowKind::custom, kSourceCard,     nullptr,   56.0f, 0.0f, false, false },
            { PageRowKind::custom, kSourceCard + 1, nullptr,   56.0f, 8.0f, false, false },
            { PageRowKind::dividedHeading, -1, "MODE",          0.0f, 0.0f, false, false },
            { PageRowKind::custom, kModeSegments,   nullptr,   28.0f, 0.0f, false, false },
            { PageRowKind::dividedHeading, -1, "AUDITION",      0.0f, 0.0f, false, false },
            { PageRowKind::custom, kAuditionRow,    nullptr,   30.0f, 0.0f, false, false },
            { PageRowKind::custom, kLayerSegments,  nullptr,   26.0f, 4.0f, false, false },
            { PageRowKind::custom, kSmearPill,      nullptr,   26.0f, 4.0f, false, false },
            { PageRowKind::dividedHeading, -1, "VARIATIONS",    0.0f, 0.0f, false, false },
            { PageRowKind::custom, kVariationSlot,     nullptr, 28.0f, 0.0f, false, false },
            { PageRowKind::custom, kVariationSlot + 1, nullptr, 28.0f, 5.0f, false, false },
            { PageRowKind::custom, kVariationSlot + 2, nullptr, 28.0f, 5.0f, false, false },
            { PageRowKind::dividedHeading, -1, "FILE",          0.0f, 0.0f, false, false },
            { PageRowKind::custom, kImportWav, "IMPORT WAV",   28.0f, 0.0f, false, false },
            { PageRowKind::custom, kImportAts, "IMPORT ATS",   28.0f, 6.0f, false, false },
            { PageRowKind::custom, kExportAts, "EXPORT ATS",   28.0f, 6.0f, false, false },
        };
        return rows;
    }
private:


    static constexpr std::array<const char*, 3> kModeLabels { "MORPH", "HYBRID", "ENVELOPE" };
    static constexpr std::array<const char*, 4> kLayerLabels { "BOTH", "PART", "RES", "SMEAR" };
    static constexpr std::array<const char*, 4> kSourceLabels { "A", "B", "MORPH", "CURRENT" };

    void drawLeftColumn(visage::Canvas& canvas, const ThemeTokens& tokens, UiRect area)
    {
        const auto& rows = leftRows();
        const auto slots = pageRowSlots(rows, area);
        paintPageRows(canvas, tokens, rows, slots, nullptr);

        const visage::Font font(ThemeFont::controlLabel, visage::fonts::Lato_Regular_ttf);
        const visage::Font buttonFont(ThemeFont::button, visage::fonts::Lato_Regular_ttf);
        const auto& session = model_.state();

        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].kind != PageRowKind::custom || !slots[i].visible)
                continue;
            const UiRect slot = slots[i].bounds;
            const int id = rows[i].index;

            if (id == kSourceCard || id == kSourceCard + 1)
            {
                paintSourceCard(canvas, tokens, font, slot,
                                static_cast<std::size_t>(id - kSourceCard));
            }
            else if (id == kModeSegments)
            {
                paintSegments(canvas, tokens, slot, { kModeLabels.begin(), kModeLabels.end() },
                              static_cast<int>(session.morph.selectedLane));
            }
            else if (id == kAuditionRow)
            {
                paintAuditionRow(canvas, tokens, font, slot);
            }
            else if (id == kLayerSegments)
            {
                paintSegments(canvas, tokens, slot, { kLayerLabels.begin(), kLayerLabels.end() },
                              static_cast<int>(session.transport.layer));
            }
            else if (id == kSmearPill)
            {
                paintThemeOps(canvas, ThemeRecipes::togglePill(
                    tokens, { slot.x, slot.y, 48.0f, slot.height },
                    session.transport.smearEnabled, false));
                canvas.setColor(tokens.textPrimary.argb);
                canvas.text("SMEAR FX", font, visage::Font::kLeft, slot.x + 58.0f, slot.y,
                            slot.width - 58.0f, slot.height);
            }
            else if (id >= kVariationSlot && id < kVariationSlot + 3)
            {
                paintVariationRow(canvas, tokens, font, slot,
                                  static_cast<std::size_t>(id - kVariationSlot));
            }
            else
            {
                paintThemeOps(canvas, ThemeRecipes::buttonBackground(tokens, slot, false, false));
                canvas.setColor(tokens.textPrimary.argb);
                canvas.text(rows[i].label, buttonFont, visage::Font::kCenter, slot.x, slot.y,
                            slot.width, slot.height);
            }
        }
    }

    static void paintSegments(visage::Canvas& canvas, const ThemeTokens& tokens, UiRect slot,
                              const std::vector<const char*>& labels, int selected)
    {
        const float trackRadius = ThemeRadius::segmentedTrack(slot.height);
        paintThemeOps(canvas, ThemeRecipes::well(tokens, slot, trackRadius));
        const visage::Font font(ThemeFont::segment(static_cast<int>(labels.size())),
                                visage::fonts::Lato_Regular_ttf);
        const float segW = slot.width / static_cast<float>(labels.size());
        for (std::size_t i = 0; i < labels.size(); ++i)
        {
            const UiRect seg { slot.x + static_cast<float>(i) * segW, slot.y, segW, slot.height };
            const bool active = static_cast<int>(i) == selected;
            paintThemeOps(canvas,
                          ThemeRecipes::segmentedSegment(tokens, seg, trackRadius, active, false));
            canvas.setColor(ThemeRecipes::segmentedText(tokens, active, false).argb);
            canvas.text(labels[i], font, visage::Font::kCenter, seg.x, seg.y, seg.width,
                        seg.height);
        }
    }

    // The ANALYZE chip sits on the card's own bottom-right so the rest of the
    // card stays a single click target for source selection.
    [[nodiscard]] static UiRect analyzeChip(UiRect card) noexcept
    {
        return { card.x + card.width - 76.0f, card.y + card.height - 26.0f, 66.0f, 20.0f };
    }

    void paintSourceCard(visage::Canvas& canvas, const ThemeTokens& tokens,
                         const visage::Font& font, UiRect slot, std::size_t index) const
    {
        const auto& card = model_.state().sources[index];
        const auto wanted = index == 0 ? SessionSource::sourceA : SessionSource::sourceB;
        const bool selected = model_.state().transport.source == wanted;

        auto ops = ThemeRecipes::well(tokens, slot, ThemeRadius::control);
        if (selected)
            ops.push_back({ ThemeShape::roundedRectBorder, slot, ThemeRadius::control, 1.2f,
                            tokens.accent.withAlpha(0.8f) });
        paintThemeOps(canvas, ops);

        canvas.setColor(selected ? tokens.accent.argb : tokens.textPrimary.argb);
        canvas.text(index == 0 ? "A" : "B", font, visage::Font::kLeft, slot.x + 10.0f,
                    slot.y + 6.0f, 16.0f, 16.0f);
        canvas.setColor(tokens.textSecondary.argb);
        canvas.text(card.name.empty() ? "EMPTY" : card.name.c_str(), font, visage::Font::kLeft,
                    slot.x + 28.0f, slot.y + 6.0f, slot.width - 38.0f, 16.0f);

        const auto stats = card.hasModel
            ? std::to_string(card.partialCount) + " PARTIALS  " +
              std::to_string(static_cast<int>(card.durationSeconds * 100.0f) / 100) + "S" +
              (card.hasResidual ? "  +RES" : "")
            : std::string("DROP A FILE");
        canvas.setColor(tokens.textMuted.argb);
        canvas.text(stats.c_str(), font, visage::Font::kLeft, slot.x + 10.0f, slot.y + 26.0f,
                    slot.width - 90.0f, 16.0f);

        const UiRect chip = analyzeChip(slot);
        paintThemeOps(canvas, ThemeRecipes::buttonBackground(tokens, chip, false, false));
        canvas.setColor(card.analyzed ? tokens.accent.argb
                                      : (card.hasModel ? tokens.textPrimary.argb
                                                       : tokens.textFaint.argb));
        canvas.text("ANALYZE", font, visage::Font::kCenter, chip.x, chip.y, chip.width,
                    chip.height);
    }

    void paintAuditionRow(visage::Canvas& canvas, const ThemeTokens& tokens,
                          const visage::Font& font, UiRect slot) const
    {
        const UiRect combo = auditionCombo(slot);
        paintThemeOps(canvas, ThemeRecipes::well(tokens, combo, ThemeRadius::control));
        canvas.setColor(tokens.textPrimary.argb);
        canvas.text(kSourceLabels[static_cast<std::size_t>(model_.state().transport.source)], font,
                    visage::Font::kLeft, combo.x + 8.0f, combo.y, combo.width - 16.0f,
                    combo.height);

        const UiRect play = auditionPlay(slot);
        const bool playing = model_.state().transport.playing;
        paintThemeOps(canvas, ThemeRecipes::buttonBackground(tokens, play, false, false));
        canvas.setColor(playing ? tokens.accent.argb : tokens.textPrimary.argb);
        canvas.text(playing ? "STOP" : "PLAY", font, visage::Font::kCenter, play.x, play.y,
                    play.width, play.height);
    }

    [[nodiscard]] static UiRect auditionCombo(UiRect slot) noexcept
    {
        return { slot.x, slot.y, std::max(0.0f, slot.width - 78.0f), slot.height };
    }

    [[nodiscard]] static UiRect auditionPlay(UiRect slot) noexcept
    {
        return { slot.x + slot.width - 68.0f, slot.y, 68.0f, slot.height };
    }

    void paintVariationRow(visage::Canvas& canvas, const ThemeTokens& tokens,
                           const visage::Font& font, UiRect slot, std::size_t index) const
    {
        const bool stored = model_.state().morph.variationStored[index];
        auto ops = ThemeRecipes::buttonBackground(tokens, slot, false, false);
        if (stored)
            ops.push_back({ ThemeShape::roundedRectBorder, slot, ThemeRadius::control, 1.2f,
                            tokens.accent.withAlpha(0.8f) });
        paintThemeOps(canvas, ops);
        canvas.setColor(stored ? tokens.accent.argb : tokens.textPrimary.argb);
        canvas.text((std::string("VARIATION ") + std::to_string(index + 1)).c_str(), font,
                    visage::Font::kLeft, slot.x + 10.0f, slot.y, slot.width - 76.0f, slot.height);
        canvas.setColor(tokens.textMuted.argb);
        canvas.text(stored ? "STORED" : "EMPTY", font, visage::Font::kRight, slot.x + 10.0f,
                    slot.y, slot.width - 20.0f, slot.height);
    }

    void handleLeftClick(const std::vector<PageRow>& rows, const std::vector<LayoutSlot>& slots,
                         std::size_t i, visage::Point point)
    {
        const UiRect slot = slots[i].bounds;
        const int id = rows[i].index;
        const auto& transport = model_.state().transport;

        if (id == kSourceCard || id == kSourceCard + 1)
        {
            const auto index = static_cast<std::size_t>(id - kSourceCard);
            if (rectContains(analyzeChip(slot), point.x, point.y))
            {
                if (emit_)
                    emit_(AnalyzeMorphSourceAction { static_cast<int>(index) });
                return;
            }
            model_.setTransport(index == 0 ? SessionSource::sourceA : SessionSource::sourceB,
                                transport.layer);
            if (transport.playing && emit_)
                emit_(model_.previewAction(true));
        }
        else if (id == kModeSegments)
        {
            model_.setSelectedLane(static_cast<MorphLane>(
                segmentIndexAt(slot, point.x, kModeLabels.size())));
        }
        else if (id == kAuditionRow)
        {
            if (rectContains(auditionPlay(slot), point.x, point.y))
            {
                const bool playing = !transport.playing;
                model_.setPlaying(playing);
                if (emit_)
                    emit_(model_.previewAction(playing));
            }
            else
            {
                const auto next = static_cast<SessionSource>(
                    (static_cast<int>(transport.source) + 1) %
                    static_cast<int>(kSourceLabels.size()));
                model_.setTransport(next, transport.layer);
                if (transport.playing && emit_)
                    emit_(model_.previewAction(true));
            }
        }
        else if (id == kLayerSegments)
        {
            model_.setTransport(transport.source, static_cast<SessionLayer>(
                segmentIndexAt(slot, point.x, kLayerLabels.size())));
            if (transport.playing && emit_)
                emit_(model_.previewAction(true));
        }
        else if (id == kSmearPill)
        {
            model_.setSmearEnabled(!transport.smearEnabled);
            if (emit_)
                emit_(model_.smearAction());
        }
        else if (id >= kVariationSlot && id < kVariationSlot + 3)
        {
            const auto index = static_cast<std::size_t>(id - kVariationSlot);
            const bool stored = !model_.state().morph.variationStored[index];
            model_.setVariationStored(index, stored);
            if (emit_)
                emit_(model_.variationAction(index, stored));
        }
        else if (id == kImportWav && onImportWav_)
        {
            onImportWav_(static_cast<int>(transport.source == SessionSource::sourceB));
        }
        else if (id == kImportAts && onImportAts_)
        {
            onImportAts_(static_cast<int>(transport.source == SessionSource::sourceB));
        }
        else if (id == kExportAts && onExportAts_)
        {
            onExportAts_(static_cast<int>(transport.source == SessionSource::sourceB));
        }
        redraw();
    }

public:
    // The left column runs the full height.  The spectrum panel spans the
    // centre and right columns at the top; the centre (timeline, engine
    // context) and right (morph, lanes, timbre, output) panels sit under it.
    struct SessionPanels
    {
        UiRect left {};
        UiRect spectrum {};
        UiRect centre {};
        UiRect right {};
    };

    // The spectrogram well, then the centre column's morph timeline and the
    // §7.2 engine context panel taking whatever is left.  Public so
    // tests/page_column_layout_test can check the real geometry.
    struct CentreRects
    {
        UiRect well {};
        UiRect timeline {};
        UiRect context {};
    };

    // The spectrogram well fills its own panel, which spans the centre and
    // right columns; the timeline and the engine context panel sit under it
    // in the centre column.
    [[nodiscard]] static CentreRects centreRects(const SessionPanels& p) noexcept
    {
        CentreRects r;
        r.well = spectrumWell(p.spectrum);
        const auto c = ThemeLayout::panelContent(p.centre);
        const float bottom = c.y + c.height;
        r.timeline = { c.x, c.y, c.width, std::clamp(c.height, 0.0f, kTimelineH) };
        const float afterTimeline = r.timeline.y + r.timeline.height + 14.0f;
        r.context = { c.x, afterTimeline, c.width, std::max(0.0f, bottom - afterTimeline) };
        return r;
    }

    static constexpr float kContextModeStripH = 30.0f;

    [[nodiscard]] static UiRect contextModeStrip(UiRect context) noexcept
    {
        return { context.x, context.y, context.width,
                 std::min(kContextModeStripH, context.height) };
    }

    [[nodiscard]] static UiRect contextGridArea(UiRect context) noexcept
    {
        const float top = context.y + kContextModeStripH + 10.0f;
        return { context.x, top, context.width,
                 std::max(0.0f, context.y + context.height - top) };
    }

private:
    // §7.2's MORPH grid is not in registry order, so the cell walk is explicit.
    [[nodiscard]] std::vector<int> contextCells() const
    {
        if (contextMorph_)
            return { 3, 5, 4, 6, 7, 8, 9, 10, 11, 12 };
        std::vector<int> cells;
        for (int i = 13; i <= 32; ++i)
            cells.push_back(i);
        return cells;
    }

public:
    // Three columns, or four when three rows short of the cell count would
    // otherwise drop cells (SMEARNOV's twenty under the wide spectrum).
    [[nodiscard]] static std::vector<UiRect> contextGrid(UiRect area, std::size_t count)
    {
        std::vector<UiRect> out;
        constexpr float colGap = 14.0f;
        constexpr float rowGap = 8.0f;
        constexpr float cellH = 42.0f;
        if (count == 0 || area.width <= 0.0f)
            return out;
        const auto rowsFit = static_cast<std::size_t>(
            std::max(0.0f, (area.height + rowGap) / (cellH + rowGap)));
        const std::size_t columns = rowsFit * 3 >= count ? 3 : 4;
        const auto colCount = static_cast<float>(columns);
        const float colW = (area.width - (colCount - 1.0f) * colGap) / colCount;
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto col = static_cast<float>(i % columns);
            const auto row = static_cast<float>(i / columns);
            const float y = area.y + row * (cellH + rowGap);
            if (y + cellH > area.y + area.height)
                break;
            out.push_back({ area.x + col * (colW + colGap), y, colW, cellH });
        }
        return out;
    }
private:

    void drawCentreColumn(visage::Canvas& canvas, const ThemeTokens& tokens,
                          const SessionPanels& p)
    {
        const auto rects = centreRects(p);
        paintThemeOps(canvas,
                      ThemeRecipes::well(tokens, rects.well, ThemeRadius::spectrumWell));
        paintThemeOps(canvas,
                      ThemeRecipes::well(tokens, rects.timeline, ThemeRadius::spectrumWell));
        if (centre_.draw)
            centre_.draw(canvas, rects.well, rects.timeline);

        static const std::vector<const char*> kEngines { "MORPH", "SMEARNOV" };
        paintSegments(canvas, tokens, contextModeStrip(rects.context), kEngines,
                      contextMorph_ ? 0 : 1);

        const auto cells = contextCells();
        const auto grid = contextGrid(contextGridArea(rects.context), cells.size());
        for (std::size_t i = 0; i < grid.size(); ++i)
            paintControlLabel(canvas, tokens, controlName(cells[i]), grid[i]);
    }

    [[nodiscard]] static int segmentIndexAt(UiRect slot, float x, std::size_t count) noexcept
    {
        if (count == 0 || slot.width <= 0.0f)
            return 0;
        const float segW = slot.width / static_cast<float>(count);
        return std::clamp(static_cast<int>((x - slot.x) / segW), 0, static_cast<int>(count) - 1);
    }

    // Section 10.4's right column is anchored from both ends: the output group
    // sits on the floor of the panel and everything else walks down from the
    // top pad into whatever is left above it.
public:
    // Public so tests/page_column_layout_test can walk the real tables.
    static const std::vector<PageRow>& rightBottomRows()
    {
        static const std::vector<PageRow> rows {
            { PageRowKind::control,        50, nullptr, 42.0f,  0.0f, false, true },
            { PageRowKind::control,        48, nullptr, 42.0f, 10.0f, true,  true },
            { PageRowKind::control,        49, nullptr, 42.0f,  0.0f, false, true },
        };
        return rows;
    }

private:


    static const char* controlName(int index)
    {
        const auto* descriptor = ParameterRegistry::find(
            kControlIds[static_cast<std::size_t>(index)]);
        return descriptor ? descriptor->name.data() : nullptr;
    }

    std::vector<UiRect> columnRects() const
    {
        return ThemeLayout::columns({ 0.0f, 0.0f, width(), height() }, kColumnWidths);
    }

    [[nodiscard]] SessionPanels panels() const
    {
        return sessionPanels({ 0.0f, 0.0f, width(), height() });
    }

public:
    static inline const std::vector<float> kColumnWidths { 260.0f, 0.0f, 360.0f };
    static constexpr float kTimelineH = 96.0f;
    static constexpr float kSpectrumShare = 0.44f;
    static constexpr float kMinSpectrumPanelH = 260.0f;
    static constexpr float kRightTabStripH = 30.0f;
    static constexpr float kRightTabGap = 10.0f;
    static constexpr std::array<const char*, 3> kRightTabLabels { "MORPH", "LANES", "TIMBRE" };

    [[nodiscard]] static SessionPanels sessionPanels(UiRect area)
    {
        SessionPanels p;
        const auto cols = ThemeLayout::columns(area, kColumnWidths);
        if (cols.size() < 3)
            return p;
        p.left = cols[0];
        const float x = cols[1].x;
        const float top = cols[1].y;
        const float height = cols[1].height;
        // The spectrum takes its share, but never so much that the right
        // panel cannot hold its widest tab plus the output group.
        const float wanted = std::max(kMinSpectrumPanelH, height * kSpectrumShare);
        const float spectrumH = std::max(0.0f, std::min(wanted, height - ThemeLayout::kColumnGap
                                                                - minRightPanelHeight()));
        p.spectrum = { x, top, cols[2].x + cols[2].width - x, spectrumH };
        const float lowerY = top + spectrumH + ThemeLayout::kColumnGap;
        const float lowerH = std::max(0.0f, top + height - lowerY);
        p.centre = { cols[1].x, lowerY, cols[1].width, lowerH };
        p.right = { cols[2].x, lowerY, cols[2].width, lowerH };
        return p;
    }

    // The well is inset by the panel pad on every side: the panel has no
    // caption, so the well takes the whole body.
    [[nodiscard]] static UiRect spectrumWell(UiRect panel) noexcept
    {
        const auto pad = ThemeLayout::kPanelPad;
        return { panel.x + pad, panel.y + pad, std::max(0.0f, panel.width - 2.0f * pad),
                 std::max(0.0f, panel.height - 2.0f * pad) };
    }

    struct RightColumn
    {
        UiRect content {};
        UiRect tabStrip {};
        UiRect rowArea {};
        const std::vector<PageRow>* rows = nullptr;
    };

    // A MORPH | LANES | TIMBRE strip and the chosen group above the output
    // group.  Under the wide spectrum the three groups do not fit together
    // even at the largest window size.
    [[nodiscard]] static RightColumn rightColumn(UiRect panel, int tab)
    {
        RightColumn r;
        r.content = ThemeLayout::panelContent(panel);
        const auto top = rightTopArea(r.content);
        r.tabStrip = { top.x, top.y, top.width, std::min(kRightTabStripH, top.height) };
        const float rowsY = top.y + kRightTabStripH + kRightTabGap;
        r.rowArea = { top.x, rowsY, top.width, std::max(0.0f, top.y + top.height - rowsY) };
        r.rows = &rightGroupRows(std::clamp(tab, 0, 2));
        return r;
    }

    // Each right-column group without its heading (the tab names it).
    static const std::vector<PageRow>& rightGroupRows(int tab)
    {
        static const std::vector<PageRow> morph {
            { PageRowKind::control,         0, nullptr,     42.0f, 0.0f, false, true },
            { PageRowKind::custom, kSnapshotStrip, nullptr, 24.0f, 4.0f, false, false },
            { PageRowKind::control,         1, nullptr,     42.0f, 0.0f, true,  true },
            { PageRowKind::control,         2, nullptr,     42.0f, 0.0f, false, true },
        };
        static const std::vector<PageRow> lanes {
            { PageRowKind::control,        36, nullptr,     30.0f, 0.0f, true,  true },
            { PageRowKind::control,        37, nullptr,     30.0f, 0.0f, false, true },
            { PageRowKind::control,        38, nullptr,     30.0f, 4.0f, true,  true },
            { PageRowKind::control,        39, nullptr,     30.0f, 0.0f, false, true },
            { PageRowKind::control,        40, nullptr,     30.0f, 4.0f, true,  true },
            { PageRowKind::control,        41, nullptr,     30.0f, 0.0f, false, true },
            { PageRowKind::control,        42, nullptr,     30.0f, 4.0f, true,  true },
            { PageRowKind::control,        43, nullptr,     30.0f, 0.0f, false, true },
            { PageRowKind::control,        44, nullptr,     30.0f, 4.0f, true,  true },
            { PageRowKind::control,        45, nullptr,     30.0f, 0.0f, false, true },
            { PageRowKind::control,        46, nullptr,     30.0f, 4.0f, true,  true },
            { PageRowKind::control,        47, nullptr,     30.0f, 0.0f, false, true },
            { PageRowKind::custom, kLinkLanes, "LINK LANES", 26.0f, 4.0f, false, false },
        };
        static const std::vector<PageRow> timbre {
            { PageRowKind::control,        33, nullptr,     42.0f, 0.0f, true,  true },
            { PageRowKind::control,        34, nullptr,     42.0f, 0.0f, false, true },
            { PageRowKind::control,        35, nullptr,     42.0f, 0.0f, false, true },
        };
        return tab == 1 ? lanes : tab == 2 ? timbre : morph;
    }

    // The right panel's least height in tabbed form: pads, strip, the tallest
    // group and the bottom-anchored output group with its divider.
    // The height a row table needs to show every row: its own height, plus
    // what the overflow guard wants left for a final control shorter than
    // ThemeLayout::kMinCompactControlH (LINK LANES).
    [[nodiscard]] static float rowsNeed(const std::vector<PageRow>& rows) noexcept
    {
        const auto items = pageLayoutItems(rows);
        const auto height = ThemeLayout::columnHeight(items);
        if (items.empty() || items.back().kind != LayoutItemKind::control)
            return height;
        return height + std::max(0.0f, ThemeLayout::kMinCompactControlH - items.back().height);
    }

    [[nodiscard]] static float minRightPanelHeight() noexcept
    {
        float tallest = 0.0f;
        for (int tab = 0; tab < 3; ++tab)
            tallest = std::max(tallest, rowsNeed(rightGroupRows(tab)));
        return 3.0f * ThemeLayout::kPanelPad + kRightTabStripH + kRightTabGap + tallest
            + ThemeLayout::columnHeight(pageLayoutItems(rightBottomRows()))
            + ThemeRecipes::kDividerHeight;
    }

    // The top group may not run past the bottom group's engraved divider.
    [[nodiscard]] static UiRect rightTopArea(UiRect right)
    {
        const float reserved = ThemeLayout::columnHeight(pageLayoutItems(rightBottomRows())) +
                               ThemeRecipes::kDividerHeight;
        return { right.x, right.y, right.width, std::max(0.0f, right.height - reserved) };
    }
private:

    void paintRightCustomRows(visage::Canvas& canvas, const ThemeTokens& tokens,
                              const std::vector<PageRow>& rows,
                              const std::vector<LayoutSlot>& slots) const
    {
        const visage::Font font(ThemeFont::controlLabel, visage::fonts::Lato_Regular_ttf);
        for (std::size_t i = 0; i < rows.size() && i < slots.size(); ++i)
        {
            if (rows[i].kind != PageRowKind::custom || !slots[i].visible)
                continue;
            const UiRect slot = slots[i].bounds;
            if (rows[i].index == kLinkLanes)
            {
                paintThemeOps(canvas, ThemeRecipes::buttonBackground(tokens, slot, false, false));
                canvas.setColor(tokens.textPrimary.argb);
                canvas.text(rows[i].label, font, visage::Font::kCenter, slot.x, slot.y,
                            slot.width, slot.height);
                continue;
            }
            paintSnapshotStrip(canvas, tokens, font, slot);
        }
    }

    void paintSnapshotStrip(visage::Canvas& canvas, const ThemeTokens& tokens,
                            const visage::Font& font, UiRect slot) const
    {
        constexpr auto count = SessionPageModel::kVariationCount;
        const float gap = 6.0f;
        const float each = (slot.width - gap * static_cast<float>(count - 1)) /
                           static_cast<float>(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            const UiRect cell { slot.x + static_cast<float>(i) * (each + gap), slot.y, each,
                                slot.height };
            const bool stored = model_.state().morph.variationStored[i];
            auto ops = ThemeRecipes::well(tokens, cell, ThemeRadius::control);
            if (stored)
            {
                ops.push_back({ ThemeShape::roundedRect, cell, ThemeRadius::control, 1.0f,
                                tokens.accent.withAlpha(tokens.isDark() ? 0.22f : 0.14f) });
                ops.push_back({ ThemeShape::roundedRectBorder, cell, ThemeRadius::control, 1.2f,
                                tokens.accent.withAlpha(0.8f) });
            }
            paintThemeOps(canvas, ops);
            canvas.setColor(stored ? tokens.accent.argb : tokens.textSecondary.argb);
            canvas.text((std::string("VAR ") + std::to_string(i + 1)).c_str(), font,
                        visage::Font::kCenter, cell.x, cell.y, cell.width, cell.height);
        }
    }

    void layoutWidgets()
    {
        const auto cols = columnRects();
        if (cols.empty())
            return;
        // The context panel shows one engine's widgets at a time, so everything
        // starts hidden and only the placed rows are shown again.
        for (auto* widget : widgets_)
            widget->setVisible(false);

        const auto p = panels();
        const auto centre = centreRects(p);
        const auto cells = contextCells();
        const auto grid = contextGrid(contextGridArea(centre.context), cells.size());
        for (std::size_t i = 0; i < grid.size(); ++i)
        {
            auto* widget = widgets_[static_cast<std::size_t>(cells[i])];
            const UiRect box { grid[i].x, grid[i].y + ThemeRecipes::kControlLabelH, grid[i].width,
                               grid[i].height - ThemeRecipes::kControlLabelH };
            widget->setBounds(box.x, box.y, box.width, box.height);
            widget->setVisible(true);
        }

        const auto rightLayout = rightColumn(p.right, rightTab_);
        const auto& top = *rightLayout.rows;
        placePageWidgets(top, pageRowSlots(top, rightLayout.rowArea), widgets_);
        const auto& bottom = rightBottomRows();
        placePageWidgets(bottom, ThemeLayout::columnFromBottom(pageLayoutItems(bottom),
                                                               rightLayout.content),
                         widgets_);
    }

        void handleInteractionClick(visage::Point point, bool shift = false,
                                bool command = false, bool alt = false, bool middle = false,
                                bool doubleClick = false)
    {
        const auto cols = columnRects();
        if (cols.empty())
            return;

        const auto& left = leftRows();
        const auto leftSlots = pageRowSlots(left, ThemeLayout::panelContent(cols[0]));
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            if (left[i].kind != PageRowKind::custom || !leftSlots[i].visible)
                continue;
            if (!rectContains(leftSlots[i].bounds, point.x, point.y))
                continue;
            handleLeftClick(left, leftSlots, i, point);
            return;
        }

        const auto p = panels();
        const auto centre = centreRects(p);
        const UiRect strip = contextModeStrip(centre.context);
        if (rectContains(strip, point.x, point.y))
        {
            contextMorph_ = segmentIndexAt(strip, point.x, 2) == 0;
            layoutWidgets();
            redraw();
            return;
        }
        if (centre_.mouseDown && centre_.mouseDown(point, centre.well, centre.timeline,
                                                   shift, command, alt, middle, doubleClick))
        {
            redraw();
            return;
        }

        const auto rightLayout = rightColumn(p.right, rightTab_);
        if (rectContains(rightLayout.tabStrip, point.x, point.y))
        {
            rightTab_ = segmentIndexAt(rightLayout.tabStrip, point.x, kRightTabLabels.size());
            layoutWidgets();
            redraw();
            return;
        }
        const auto& rows = *rightLayout.rows;
        const auto slots = pageRowSlots(rows, rightLayout.rowArea);

        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].kind != PageRowKind::custom || !slots[i].visible)
                continue;
            if (!rectContains(slots[i].bounds, point.x, point.y))
                continue;
            if (rows[i].index == kSnapshotStrip)
                toggleVariationAt(slots[i].bounds, point.x);
            return;
        }
    }

    void toggleVariationAt(UiRect strip, float x)
    {
        constexpr auto count = SessionPageModel::kVariationCount;
        const float gap = 6.0f;
        const float each = (strip.width - gap * static_cast<float>(count - 1)) /
                           static_cast<float>(count);
        const auto index = static_cast<std::size_t>((x - strip.x) / (each + gap));
        if (index >= count)
            return;
        const bool stored = !model_.state().morph.variationStored[index];
        model_.setVariationStored(index, stored);
        if (emit_)
            emit_(model_.variationAction(index, stored));
        redraw();
    }

    SessionPageModel& model_;
    Emitter emit_;
    ParameterStore store_;
    PageActionBridge bridge_;
    ParameterActionDispatcher dispatcher_;
    ThemeGetter theme_;
    ChromeRequest onImportWav_;
    ChromeRequest onImportAts_;
    ChromeRequest onExportAts_;
    CentreHandlers centre_;
    std::vector<std::unique_ptr<ParameterBinding>> bindings_;
    std::vector<visage::Frame*> widgets_;
    bool contextMorph_ = true;
    int rightTab_ = 0;
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
            handleClick(localPoint(*this, event));
        };
    }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = theme_();
        const auto cols = columnRects();
        if (cols.empty())
            return;

        for (const auto& col : cols)
            paintPagePanel(canvas, tokens, col);

        // Left column is the §8.1 spectrogram well, click-to-select.
        paintThemeOps(canvas, ThemeRecipes::well(tokens, ThemeLayout::panelContent(cols[0]),
                                                 ThemeRadius::spectrumWell));

        const auto& rows = cellRows();
        const auto slots = pageRowSlots(rows, ThemeLayout::panelContent(cols[1]));
        paintPageRows(canvas, tokens, rows, slots, nullptr);

        const visage::Font font(ThemeFont::controlLabel, visage::fonts::Lato_Regular_ttf);
        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].kind != PageRowKind::custom || !slots[i].visible)
                continue;
            const UiRect slot = slots[i].bounds;
            if (rows[i].index == kApply)
            {
                paintThemeOps(canvas, ThemeRecipes::buttonBackground(tokens, slot, false, false));
                canvas.setColor(tokens.textPrimary.argb);
                canvas.text("APPLY", font, visage::Font::kCenter, slot.x, slot.y, slot.width,
                            slot.height);
                continue;
            }
            paintControlLabel(canvas, tokens, rows[i].label, slot);
            paintScalarRow(canvas, tokens, font, pageRowWidgetArea(rows[i], slot),
                           rows[i].index);
        }
    }

    [[nodiscard]] int partialIndex() const noexcept { return partialIndex_; }
    [[nodiscard]] int partialFrame() const noexcept { return partialFrame_; }

    void setDimensions(int partials, int frames) noexcept
    {
        partialCount_ = std::max(0, partials);
        frameCount_ = std::max(0, frames);
        partialIndex_ = std::clamp(partialIndex_, 0, std::max(0, partialCount_ - 1));
        partialFrame_ = std::clamp(partialFrame_, 0, std::max(0, frameCount_ - 1));
        redraw();
    }

    void setSelection(int partial, int frame) noexcept
    {
        partialIndex_ = std::max(0, partial);
        partialFrame_ = std::max(0, frame);
        redraw();
    }

    void handleClick(visage::Point point)
    {
        const auto cols = columnRects();
        if (cols.empty())
            return;
        const auto& rows = cellRows();
        const auto slots = pageRowSlots(rows, ThemeLayout::panelContent(cols[1]));

        for (std::size_t i = 0; i < rows.size(); ++i)
        {
            if (rows[i].kind != PageRowKind::custom || !slots[i].visible)
                continue;
            const UiRect slot = slots[i].bounds;
            if (!rectContains(slot, point.x, point.y))
                continue;

            if (rows[i].index == kApply)
            {
                if (emit_)
                    emit_(PartialEditAction { partialIndex_, partialFrame_, true,
                                              amplitude_, frequencyHz_ });
                return;
            }
            const UiRect track = pageRowWidgetArea(rows[i], slot);
            const float norm = std::clamp((point.x - track.x) / std::max(1.0f, track.width),
                                          0.0f, 1.0f);
            setScalar(rows[i].index, norm);
            redraw();
            return;
        }
    }

private:
    // Page-defined ids for the four scalar rows and the button, in §10.6 order.
    static constexpr int kPartial = 0;
    static constexpr int kFrame = 1;
    static constexpr int kAmplitude = 2;
    static constexpr int kFrequency = 3;
    static constexpr int kApply = 4;

    static constexpr float kMaxFrequencyHz = 20000.0f;

public:
    // Public so tests/page_column_layout_test can walk the real tables.
    static const std::vector<PageRow>& cellRows()
    {
        static const std::vector<PageRow> rows {
            { PageRowKind::heading, -1, "CELL",       0.0f, 0.0f, false, false },
            { PageRowKind::custom, kPartial,   "PARTIAL",  42.0f, 0.0f, false, true },
            { PageRowKind::custom, kFrame,     "FRAME",    42.0f, 0.0f, false, true },
            { PageRowKind::custom, kAmplitude, "AMPLITUDE", 42.0f, 0.0f, false, true },
            { PageRowKind::custom, kFrequency, "FREQUENCY", 42.0f, 0.0f, false, true },
            { PageRowKind::custom, kApply,     "APPLY",    30.0f, 6.0f, false, false },
        };
        return rows;
    }
private:


    std::vector<UiRect> columnRects() const
    {
        return ThemeLayout::columns({ 0.0f, 0.0f, width(), height() }, { 0.0f, 320.0f });
    }

    [[nodiscard]] float scalarNorm(int index) const noexcept
    {
        switch (index)
        {
        case kPartial:
            return static_cast<float>(partialIndex_) / std::max(1.0f, maxPartial());
        case kFrame:
            return static_cast<float>(partialFrame_) / std::max(1.0f, maxFrame());
        case kAmplitude:
            return static_cast<float>(amplitude_);
        default:
            return static_cast<float>(frequencyHz_) / kMaxFrequencyHz;
        }
    }

    void setScalar(int index, float norm) noexcept
    {
        switch (index)
        {
        case kPartial:
            partialIndex_ = static_cast<int>(std::round(norm * maxPartial()));
            break;
        case kFrame:
            partialFrame_ = static_cast<int>(std::round(norm * maxFrame()));
            break;
        case kAmplitude:
            amplitude_ = static_cast<double>(norm);
            break;
        default:
            frequencyHz_ = static_cast<double>(norm) * kMaxFrequencyHz;
            break;
        }
    }

    [[nodiscard]] float maxPartial() const noexcept
    { return static_cast<float>(std::max(0, partialCount_ - 1)); }

    [[nodiscard]] float maxFrame() const noexcept
    { return static_cast<float>(std::max(0, frameCount_ - 1)); }

    void paintScalarRow(visage::Canvas& canvas, const ThemeTokens& tokens,
                        const visage::Font& font, UiRect track, int index) const
    {
        paintThemeOps(canvas, ThemeRecipes::well(tokens, track, ThemeRadius::control));
        canvas.setColor(tokens.accent.argb);
        canvas.roundedRectangle(track.x, track.y, track.width * scalarNorm(index), track.height,
                                ThemeRadius::control);
        canvas.setColor(tokens.textPrimary.argb);
        canvas.text(scalarText(index).c_str(), font, visage::Font::kCenter, track.x, track.y,
                    track.width, track.height);
    }

    [[nodiscard]] std::string scalarText(int index) const
    {
        switch (index)
        {
        case kPartial: return std::to_string(partialIndex_);
        case kFrame:   return std::to_string(partialFrame_);
        case kAmplitude:
            return std::to_string(static_cast<int>(std::round(amplitude_ * 100.0))) + "%";
        default:
            return std::to_string(static_cast<int>(std::round(frequencyHz_))) + " Hz";
        }
    }

    Emitter emit_;
    ThemeGetter theme_;
    int partialIndex_ = 0;
    int partialFrame_ = 0;
    int partialCount_ = 0;
    int frameCount_ = 0;
    double amplitude_ = 1.0;
    double frequencyHz_ = 440.0;
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
        : model_(model), emit_(std::move(emit)), theme_(std::move(theme))
    {
        // Every edit updates the draft so APPLY SCRIPT sends what is on screen;
        // Return is a shortcut that commits and applies.
        auto field = std::make_unique<AtsTextField>(
            [this](const std::string& text) {
                model_.setDraft(text);
                apply();
            },
            theme_,
            [this](const std::string& text) {
                model_.setDraft(text);
                redraw();
            });
        textField_ = field.get();
        addChild(std::move(field));

        onMouseDown() += [this](const visage::MouseEvent& event) {
            const auto p = localPoint(*this, event);
            if (rectContains(applyRect(), p.x, p.y))
                apply();
        };
    }

    void resized() override
    {
        if (textField_)
        {
            const UiRect e = editorRect();
            textField_->setBounds(e.x, e.y, e.width, e.height);
        }
    }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = theme_();
        const UiRect panel = panelRect();
        paintPagePanel(canvas, tokens, panel);
        paintThemeOps(canvas, ThemeRecipes::well(tokens, editorRect(), ThemeRadius::spectrumWell));

        const UiRect status = statusRect();
        const visage::Font mono(ThemeFont::controlLabel, visage::fonts::Lato_Regular_ttf);
        canvas.setColor(tokens.textSecondary.argb);
        canvas.text(statusText(), mono, visage::Font::kLeft, status.x, status.y,
                    status.width - kApplyW - 10.0f, status.height);

        const UiRect apply = applyRect();
        paintThemeOps(canvas, ThemeRecipes::buttonBackground(tokens, apply, false, false));
        canvas.setColor(model_.canApply() ? tokens.textPrimary.argb : tokens.textFaint.argb);
        canvas.text("APPLY SCRIPT", mono, visage::Font::kCenter, apply.x, apply.y, apply.width,
                    apply.height);
    }

    AtsTextField* textField() const noexcept { return textField_; }

private:
    static constexpr float kStatusRowH = 40.0f;
    static constexpr float kApplyW = 110.0f;
    static constexpr float kApplyH = 30.0f;

    [[nodiscard]] const char* statusText() const
    {
        switch (model_.status())
        {
        case MathStatus::running:   return "RUNNING";
        case MathStatus::success:   return "APPLIED";
        case MathStatus::error:     return model_.error().c_str();
        case MathStatus::cancelled: return "CANCELLED";
        default:                    return "READY";
        }
    }

    [[nodiscard]] UiRect panelRect() const
    {
        const auto cols = ThemeLayout::columns({ 0.0f, 0.0f, width(), height() }, { 0.0f });
        return cols.empty() ? UiRect { 0.0f, 0.0f, width(), height() } : cols[0];
    }

    // The well fills the panel body less the bottom status row.
    [[nodiscard]] UiRect editorRect() const
    {
        const UiRect body = ThemeLayout::panelContent(panelRect());
        return { body.x, body.y, body.width, std::max(0.0f, body.height - kStatusRowH) };
    }

    [[nodiscard]] UiRect statusRect() const
    {
        const UiRect body = ThemeLayout::panelContent(panelRect());
        return { body.x, body.y + std::max(0.0f, body.height - kStatusRowH), body.width,
                 kStatusRowH };
    }

    [[nodiscard]] UiRect applyRect() const
    {
        const UiRect status = statusRect();
        return { status.x + status.width - kApplyW,
                 status.y + (kStatusRowH - kApplyH) * 0.5f, kApplyW, kApplyH };
    }

    void apply()
    {
        const auto action = model_.applyAction();
        if (!action)
            return;
        if (emit_)
            emit_(*action);
        model_.beginApply();
        redraw();
    }

    MathPageModel& model_;
    Emitter emit_;
    ThemeGetter theme_;
    AtsTextField* textField_ = nullptr;
};

} // namespace ats::ui
