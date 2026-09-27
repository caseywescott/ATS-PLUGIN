#pragma once

#include "ParameterDisplay.h"
#include "ParameterBinding.h"
#include "ThemeCanvas.h"
#include "ThemeLayout.h"
#include "ThemeMetrics.h"
#include "ThemeTokens.h"
#include "../core/ParameterRegistry.h"

#include <embedded/fonts.h>
#include <visage_ui/popup_menu.h>
#include <visage/app.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace ats::ui
{

// MouseEvent::relativePosition() is the pointer *delta* since the last move,
// not a point in the frame. MouseEvent::position is frame-local but stays
// relative to the frame the event was dispatched to as it propagates up to the
// ancestors, so a parent reading it gets its child's coordinates. Only the
// window position is unambiguous, so every handler derives its own local point
// from it.
[[nodiscard]] inline visage::Point localPoint(const visage::Frame& frame,
                                              const visage::MouseEvent& event)
{
    return event.windowPosition() - frame.positionInWindow();
}

// Horizontal fill-bar slider for continuous and integer parameters.
// Drag maps x position to the normalized [0,1] parameter range.
class AtsSlider final : public visage::Frame
{
public:
    using ThemeGetter = std::function<ThemeTokens()>;

    AtsSlider(ParameterBinding& binding, ThemeGetter theme)
        : binding_(binding), theme_(std::move(theme))
    {
        onMouseDown() += [this](const visage::MouseEvent& event) {
            handleDragBegin(normAt(localPoint(*this, event).x));
        };
        onMouseDrag() += [this](const visage::MouseEvent& event) {
            handleDragMove(normAt(localPoint(*this, event).x));
        };
        onMouseUp() += [this](const visage::MouseEvent& event) {
            handleDragEnd(normAt(localPoint(*this, event).x));
        };
    }

    void handleDragBegin(float normalized)
    {
        dragging_ = true;
        dragNorm_ = std::clamp(normalized, 0.0f, 1.0f);
        binding_.beginGesture(denorm(dragNorm_));
        redraw();
    }

    void handleDragMove(float normalized)
    {
        if (!dragging_) return;
        dragNorm_ = std::clamp(normalized, 0.0f, 1.0f);
        binding_.updateGesture(denorm(dragNorm_));
        redraw();
    }

    void handleDragEnd(float normalized)
    {
        if (!dragging_) return;
        dragNorm_ = std::clamp(normalized, 0.0f, 1.0f);
        binding_.endGesture(denorm(dragNorm_));
        dragging_ = false;
        redraw();
    }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = theme_();
        const auto* descriptor = ats::ParameterRegistry::find(binding_.id());
        const float norm = dragging_ ? dragNorm_
            : (descriptor ? descriptor->normalize(binding_.value()) : 0.0f);

        const float trackW = trackWidth();
        const float trackY = std::round((height() - kTrackHeight) * 0.5f);
        const UiRect track { kThumbRadius, trackY, trackW, kTrackHeight };
        paintWell(canvas, tokens, track, ThemeRadius::sliderTrack);

        const float fillW = trackW * std::clamp(norm, 0.0f, 1.0f);
        if (fillW > 1.0f)
        {
            const UiRect fill { track.x, track.y, fillW, kTrackHeight };
            paintThemeOps(canvas, ThemeRecipes::accentHalo(tokens, fill, ThemeRadius::sliderTrack,
                                                           0.25f, 0.0f, 2.0f));
            canvas.setBrush(visage::Brush::horizontal(visage::Color(tokens.accentDim.argb),
                                                      visage::Color(tokens.accent.argb)));
            canvas.roundedRectangle(fill.x, fill.y, fill.width, fill.height,
                                    ThemeRadius::sliderTrack);
        }

        paintThemeOps(canvas, ThemeRecipes::sliderThumb(tokens, track.x + fillW,
                                                        track.y + kTrackHeight * 0.5f,
                                                        kThumbRadius));

        if (descriptor)
        {
            const visage::Font mono(ThemeFont::monoValue, visage::fonts::DroidSansMono_ttf);
            canvas.setColor(tokens.textPrimary.argb);
            canvas.text(sliderValueText(*descriptor, binding_.value()).c_str(), mono, visage::Font::kRight,
                        width() - valueBoxWidth(), 0.0f, valueBoxWidth(), height());
        }
    }

private:
    // Spec 7.1: a fixed 52px right-hand value box, mono and right-justified so
    // numbers share a straight right edge down a column.
    static constexpr float kValueBoxWidth = 52.0f;
    static constexpr float kTrackHeight = 6.0f;
    static constexpr float kThumbRadius = 7.0f;

    float valueBoxWidth() const { return std::min(kValueBoxWidth, width() * 0.4f); }
    float trackWidth() const
    {
        return std::max(1.0f, width() - valueBoxWidth() - 2.0f * kThumbRadius);
    }

    // Drag maps over the track, not the whole frame, or the thumb lags the cursor.
    float normAt(float x) const { return (x - kThumbRadius) / trackWidth(); }

    float denorm(float normalized) const
    {
        const auto* descriptor = ats::ParameterRegistry::find(binding_.id());
        return descriptor ? descriptor->denormalize(normalized) : normalized;
    }

    ParameterBinding& binding_;
    ThemeGetter theme_;
    bool dragging_ = false;
    float dragNorm_ = 0.0f;
};


// Boolean toggle button. A click flips the parameter value between 0.0 and 1.0.
class AtsToggle final : public visage::Frame
{
public:
    using ThemeGetter = std::function<ThemeTokens()>;

    AtsToggle(ParameterBinding& binding, std::string label, ThemeGetter theme)
        : binding_(binding), label_(std::move(label)), theme_(std::move(theme))
    {
        onMouseDown() += [this](const visage::MouseEvent&) { handleClick(); };
        onMouseEnter() += [this](const visage::MouseEvent&) { hovered_ = true; redraw(); };
        onMouseExit() += [this](const visage::MouseEvent&) { hovered_ = false; redraw(); };
    }

    void handleClick()
    {
        const float next = binding_.value() >= 0.5f ? 0.0f : 1.0f;
        binding_.setValue(next);
        redraw();
    }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = theme_();
        const bool active = binding_.value() >= 0.5f;

        // Spec 7.3 inline layout: label left, pill hard right at 42x21 max.
        const float pillH = std::min(kPillHeight, height());
        const float pillW = std::min(kPillWidth, width());
        const UiRect pill { width() - pillW, std::round((height() - pillH) * 0.5f), pillW, pillH };
        paintThemeOps(canvas, ThemeRecipes::togglePill(tokens, pill, active, hovered_));

        const visage::Font font(ThemeFont::controlLabel, visage::fonts::Lato_Regular_ttf);
        canvas.setColor(tokens.textSecondary.argb);
        canvas.text(label_.c_str(), font, visage::Font::kLeft, 0.0f, 0.0f,
                    std::max(0.0f, pill.x - 6.0f), height());
    }

private:
    static constexpr float kPillWidth = 42.0f;
    static constexpr float kPillHeight = 21.0f;

    ParameterBinding& binding_;
    std::string label_;
    ThemeGetter theme_;
    bool hovered_ = false;
};


// Choice parameter control with popup menu.
// Click shows all choices; a selection fires SetParameterAction.
class AtsCombo final : public visage::Frame
{
public:
    using ThemeGetter = std::function<ThemeTokens()>;

    AtsCombo(ParameterBinding& binding, ThemeGetter theme)
        : binding_(binding), theme_(std::move(theme))
    {
        onMouseDown() += [this](const visage::MouseEvent& event) {
            const auto* descriptor = ats::ParameterRegistry::find(binding_.id());
            if (descriptor == nullptr || descriptor->choiceCount == 0)
                return;
            visage::PopupMenu popup("choice");
            for (std::uint16_t i = 0; i < descriptor->choiceCount; ++i)
            {
                const auto label = i < descriptor->choiceCount && descriptor->choices
                    ? std::string(descriptor->choices[i]) : std::to_string(i);
                popup.addOption(static_cast<int>(i), label);
            }
            popup.onSelection() += [this](int choiceId) { handleSelect(choiceId); };
            popup.show(this, { localPoint(*this, event).x, height() });
        };
    }

    void handleSelect(int choiceIndex)
    {
        binding_.setValue(static_cast<float>(choiceIndex));
        redraw();
    }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = theme_();
        const auto* descriptor = ats::ParameterRegistry::find(binding_.id());

        paintWell(canvas, tokens, reducedRect({ 0.0f, 0.0f, width(), height() }, 0.5f),
                  ThemeRadius::control);

        const visage::Font font(ThemeFont::listComboPopup, visage::fonts::Lato_Regular_ttf);
        canvas.setColor(tokens.textPrimary.argb);
        const auto label = descriptor ? descriptor->text(binding_.value()) : std::string();
        canvas.text(label.c_str(), font, visage::Font::kLeft,
                    8.0f, 1.0f, std::max(0.0f, width() - 30.0f), height() - 2.0f);

        // Spec 7.6: a stroked chevron, not a filled triangle -- 4px arms meeting
        // 2.5px below the arm ends, centered 13px in from the right edge.
        const float cx = width() - 13.0f;
        const float cy = height() * 0.5f;
        canvas.setColor(tokens.accent.argb);
        canvas.segment(cx - 4.0f, cy - 1.25f, cx, cy + 1.25f, 1.6f, true);
        canvas.segment(cx, cy + 1.25f, cx + 4.0f, cy - 1.25f, 1.6f, true);
    }

private:
    ParameterBinding& binding_;
    ThemeGetter theme_;
};


// Horizontal segmented selector for choice parameters.
// N equal-width segments drawn inline; click selects the touched segment.
class AtsSegmented final : public visage::Frame
{
public:
    using ThemeGetter = std::function<ThemeTokens()>;

    AtsSegmented(ParameterBinding& binding, std::vector<std::string> labels, ThemeGetter theme)
        : binding_(binding), labels_(std::move(labels)), theme_(std::move(theme))
    {
        onMouseDown() += [this](const visage::MouseEvent& event) {
            handleClick(localPoint(*this, event).x);
        };
        onMouseMove() += [this](const visage::MouseEvent& event) {
            handleHover(localPoint(*this, event).x);
        };
        onMouseExit() += [this](const visage::MouseEvent&) { hovered_ = -1; redraw(); };
    }

    [[nodiscard]] int segmentAt(float x) const
    {
        if (labels_.empty() || width() <= 0.0f) return -1;
        const float segW = width() / static_cast<float>(labels_.size());
        return std::clamp(static_cast<int>(x / segW), 0, static_cast<int>(labels_.size()) - 1);
    }

    void handleClick(float x)
    {
        const int index = segmentAt(x);
        if (index < 0) return;
        binding_.setValue(static_cast<float>(index));
        redraw();
    }

    void handleHover(float x)
    {
        const int index = segmentAt(x);
        if (index == hovered_) return;
        hovered_ = index;
        redraw();
    }

    void draw(visage::Canvas& canvas) override
    {
        if (labels_.empty()) return;
        const auto tokens = theme_();
        const float trackRadius = ThemeRadius::segmentedTrack(height());
        paintWell(canvas, tokens, { 0.0f, 0.0f, width(), height() }, trackRadius);

        const visage::Font font(ThemeFont::segment(static_cast<int>(labels_.size())),
                                visage::fonts::Lato_Regular_ttf);
        const float segW = width() / static_cast<float>(labels_.size());
        const int selected = static_cast<int>(std::round(binding_.value()));
        for (std::size_t i = 0; i < labels_.size(); ++i)
        {
            const UiRect seg { static_cast<float>(i) * segW, 0.0f, segW, height() };
            const bool active = static_cast<int>(i) == selected;
            const bool hovered = static_cast<int>(i) == hovered_;
            paintThemeOps(canvas, ThemeRecipes::segmentedSegment(tokens, seg, trackRadius, active,
                                                                 hovered));
            canvas.setColor(ThemeRecipes::segmentedText(tokens, active, hovered).argb);
            canvas.text(labels_[i].c_str(), font, visage::Font::kCenter,
                        seg.x, seg.y, seg.width, seg.height);
        }
    }

private:
    ParameterBinding& binding_;
    std::vector<std::string> labels_;
    ThemeGetter theme_;
    int hovered_ = -1;
};


// Single-line text field. Characters accumulate until the user commits
// (Enter key or explicit handleCommit() call).
class AtsTextField final : public visage::Frame
{
public:
    using ThemeGetter = std::function<ThemeTokens()>;
    using Commit = std::function<void(const std::string&)>;
    using Change = std::function<void(const std::string&)>;

    AtsTextField(Commit onCommit, ThemeGetter theme, Change onChange = {})
        : onCommit_(std::move(onCommit)), onChange_(std::move(onChange)), theme_(std::move(theme))
    {
        onMouseDown() += [this](const visage::MouseEvent&) {
            focused_ = true;
            requestKeyboardFocus();
            redraw();
        };
    }

    bool receivesTextInput() override { return focused_; }

    void textInput(const std::string& text) override
    {
        for (char c : text)
        {
            if (c == '\b')
                handleBackspace();
            else if (c >= 0x20 && c < 0x7f)
                handleCharInput(c);
        }
    }

    bool keyPress(const visage::KeyEvent& event) override
    {
        if (!event.key_down) return false;
        if (event.keyCode() == visage::KeyCode::Return)
        {
            handleCommit();
            return true;
        }
        if (event.keyCode() == visage::KeyCode::Escape)
        {
            focused_ = false;
            redraw();
            return true;
        }
        return false;
    }

    void handleCharInput(char c)
    {
        text_ += c;
        if (onChange_)
            onChange_(text_);
        redraw();
    }

    void handleBackspace()
    {
        if (!text_.empty())
        {
            text_.pop_back();
            if (onChange_)
                onChange_(text_);
            redraw();
        }
    }

    void handleCommit()
    {
        if (onCommit_)
            onCommit_(text_);
        focused_ = false;
        redraw();
    }

    void setText(std::string text) { text_ = std::move(text); redraw(); }
    [[nodiscard]] const std::string& text() const noexcept { return text_; }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = theme_();
        const auto body = reducedRect({ 0.0f, 0.0f, width(), height() }, 0.5f);
        paintWell(canvas, tokens, body, ThemeRadius::control);
        if (focused_)
        {
            // Outline only. Filling with accent would hide the text underneath.
            canvas.setColor(tokens.accent.argb);
            canvas.roundedRectangleBorder(body.x, body.y, body.width, body.height,
                                          ThemeRadius::control, 1.2f);
        }
        const visage::Font font(ThemeFont::listComboPopup, visage::fonts::Lato_Regular_ttf);
        canvas.setColor(tokens.textPrimary.argb);
        const auto display = text_ + (focused_ ? "|" : "");
        canvas.text(display.c_str(), font, visage::Font::kLeft,
                    8.0f, 1.0f, std::max(0.0f, width() - 16.0f), height() - 2.0f);
    }

private:
    Commit onCommit_;
    Change onChange_;
    ThemeGetter theme_;
    std::string text_;
    bool focused_ = false;
};


// Tooltip overlay. show/hide are called by the parent frame;
// the tooltip draws on top of its siblings.
class AtsTooltip final : public visage::Frame
{
public:
    using ThemeGetter = std::function<ThemeTokens()>;

    explicit AtsTooltip(ThemeGetter theme) : theme_(std::move(theme))
    {
        setVisible(false);
    }

    void showAt(float x, float y, std::string text)
    {
        text_ = std::move(text);
        setBounds(x, y, measureWidth(), 22.0f);
        setVisible(true);
        redraw();
    }

    void hideTooltip()
    {
        setVisible(false);
        text_.clear();
    }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = theme_();
        paintThemeOps(canvas, ThemeRecipes::tooltip(tokens, { 0.0f, 0.0f, width(), height() }));
        const visage::Font font(ThemeFont::button, visage::fonts::Lato_Regular_ttf);
        canvas.setColor(tokens.textPrimary.argb);
        canvas.text(text_.c_str(), font, visage::Font::kLeft,
                    8.0f, 4.0f, width() - 16.0f, height() - 8.0f);
    }

private:
    float measureWidth() const
    {
        // Approximate: 7px per character, minimum 60, maximum 240
        const float estimated = static_cast<float>(text_.size()) * 7.0f + 16.0f;
        return std::clamp(estimated, 60.0f, 240.0f);
    }

    ThemeGetter theme_;
    std::string text_;
};


// Page strip + page container, spec 10.3. This frame is given the body rect;
// the strip is centred within it and every page fills the area below.
class AtsPageContainer final : public visage::Frame
{
public:
    using ThemeGetter = std::function<ThemeTokens()>;
    using PageChange = std::function<void(std::size_t)>;

    static constexpr float kTabHeight = ThemeLayout::kPageStripHeight;

    AtsPageContainer(std::vector<std::string> labels, ThemeGetter theme,
                     PageChange onPageChange = {})
        : labels_(std::move(labels))
        , theme_(std::move(theme))
        , onPageChange_(std::move(onPageChange))
    {
        onMouseDown() += [this](const visage::MouseEvent& event) {
            const auto& p = localPoint(*this, event);
            if (p.y < kTabHeight)
                handlePageClick(p.x);
        };
        onMouseMove() += [this](const visage::MouseEvent& event) {
            const auto& p = localPoint(*this, event);
            setHoveredTab(p.y < kTabHeight ? tabAt(p.x) : -1);
        };
        onMouseExit() += [this](const visage::MouseEvent&) { setHoveredTab(-1); };
    }

    [[nodiscard]] UiRect stripBounds() const
    {
        return ThemeLayout::pageStrip({ 0.0f, 0.0f, width(), height() });
    }

    [[nodiscard]] int tabAt(float x) const
    {
        const auto strip = stripBounds();
        if (labels_.empty() || strip.width <= 0.0f) return -1;
        if (x < strip.x || x >= strip.x + strip.width) return -1;
        const float tabW = strip.width / static_cast<float>(labels_.size());
        return std::clamp(static_cast<int>((x - strip.x) / tabW), 0,
                          static_cast<int>(labels_.size()) - 1);
    }

    void handlePageClick(float x)
    {
        const int index = tabAt(x);
        if (index >= 0)
            selectPage(static_cast<std::size_t>(index));
    }

    void setHoveredTab(int index)
    {
        if (index == hoveredTab_) return;
        hoveredTab_ = index;
        redraw();
    }

    void selectPage(std::size_t index)
    {
        if (index >= labels_.size()) return;
        currentPage_ = index;
        for (std::size_t i = 0; i < pages_.size(); ++i)
            if (pages_[i]) pages_[i]->setVisible(i == currentPage_);
        if (onPageChange_) onPageChange_(currentPage_);
        redraw();
    }

    void addPage(std::unique_ptr<visage::Frame> page)
    {
        const bool visible = pages_.size() == currentPage_;
        page->setVisible(visible);
        pages_.push_back(page.get());
        addChild(std::move(page));
        layoutPages();
    }

    // Pages are normally added before the container itself has been sized, so
    // without this they keep the zero-width bounds they were given and never
    // draw anything.
    void resized() override { layoutPages(); }

    [[nodiscard]] std::size_t currentPage() const noexcept { return currentPage_; }

    template <typename Page>
    [[nodiscard]] Page* pageAt(std::size_t index) const noexcept
    {
        if (index >= pages_.size()) return nullptr;
        return dynamic_cast<Page*>(pages_[index]);
    }

    void draw(visage::Canvas& canvas) override
    {
        if (labels_.empty()) return;
        const auto tokens = theme_();

        // The style reference has no tab bar; a one-of-N row is a segmented
        // selector (7.4), so the tab bar is drawn as one rather than inventing
        // a shape the rest of the theme does not use.
        const auto strip = stripBounds();
        const float trackRadius = ThemeRadius::segmentedTrack(strip.height);
        paintWell(canvas, tokens, strip, trackRadius);

        const visage::Font font(ThemeFont::segment(static_cast<int>(labels_.size())),
                                visage::fonts::Lato_Regular_ttf);
        const float tabW = strip.width / static_cast<float>(labels_.size());
        for (std::size_t i = 0; i < labels_.size(); ++i)
        {
            const UiRect tab { strip.x + static_cast<float>(i) * tabW, strip.y, tabW,
                               strip.height };
            const bool active = i == currentPage_;
            const bool hovered = static_cast<int>(i) == hoveredTab_;
            paintThemeOps(canvas, ThemeRecipes::segmentedSegment(tokens, tab, trackRadius, active,
                                                                 hovered));
            canvas.setColor(ThemeRecipes::segmentedText(tokens, active, hovered).argb);
            canvas.text(labels_[i].c_str(), font, visage::Font::kCenter,
                        tab.x, tab.y, tab.width, tab.height);
        }
    }

private:
    void layoutPages()
    {
        const auto area = ThemeLayout::pageArea({ 0.0f, 0.0f, width(), height() });
        for (auto* page : pages_)
            if (page != nullptr)
                page->setBounds(area.x, area.y, area.width, area.height);
    }

    std::vector<std::string> labels_;
    ThemeGetter theme_;
    PageChange onPageChange_;
    std::vector<visage::Frame*> pages_;
    std::size_t currentPage_ = 0;
    int hoveredTab_ = -1;
};

} // namespace ats::ui
