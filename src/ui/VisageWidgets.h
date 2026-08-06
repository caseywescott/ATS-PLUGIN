#pragma once

#include "ParameterBinding.h"
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
            handleDragBegin(normAt(event.relativePosition().x));
        };
        onMouseDrag() += [this](const visage::MouseEvent& event) {
            handleDragMove(normAt(event.relativePosition().x));
        };
        onMouseUp() += [this](const visage::MouseEvent& event) {
            handleDragEnd(normAt(event.relativePosition().x));
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
        const visage::Font font(10.0f, visage::fonts::Lato_Regular_ttf);
        canvas.setColor(tokens.surface.argb);
        canvas.roundedRectangle(0.0f, 0.0f, width(), height(), 4.0f);
        if (norm > 0.0f)
        {
            canvas.setColor(tokens.accent.argb);
            canvas.roundedRectangle(0.0f, 0.0f, width() * norm, height(), 4.0f);
        }
        canvas.setColor(tokens.background.argb);
        if (descriptor)
        {
            const auto label = descriptor->text(binding_.value());
            canvas.text(label.c_str(), font, visage::Font::kCenter,
                        4.0f, 3.0f, width() - 8.0f, height() - 6.0f);
        }
    }

private:
    float normAt(float x) const { return width() > 0.0f ? x / width() : 0.0f; }

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
        const visage::Font font(10.0f, visage::fonts::Lato_Regular_ttf);
        canvas.setColor(active ? tokens.accent.argb : tokens.surface.argb);
        canvas.roundedRectangle(0.0f, 0.0f, width(), height(), 4.0f);
        canvas.setColor(active ? tokens.background.argb : tokens.text.argb);
        canvas.text(label_.c_str(), font, visage::Font::kCenter,
                    4.0f, 3.0f, width() - 8.0f, height() - 6.0f);
    }

private:
    ParameterBinding& binding_;
    std::string label_;
    ThemeGetter theme_;
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
            popup.show(this, { event.relativePosition().x, height() });
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
        const visage::Font font(10.0f, visage::fonts::Lato_Regular_ttf);
        canvas.setColor(tokens.surface.argb);
        canvas.roundedRectangle(0.0f, 0.0f, width(), height(), 4.0f);
        canvas.setColor(tokens.text.argb);
        const auto label = descriptor ? descriptor->text(binding_.value()) : std::string();
        canvas.text(label.c_str(), font, visage::Font::kLeft,
                    6.0f, 3.0f, width() - 18.0f, height() - 6.0f);
        // Dropdown indicator triangle area
        canvas.setColor(tokens.secondaryText.argb);
        canvas.rectangle(width() - 14.0f, height() * 0.5f - 2.0f, 8.0f, 4.0f);
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
            handleClick(event.relativePosition().x);
        };
    }

    void handleClick(float x)
    {
        if (labels_.empty() || width() <= 0.0f) return;
        const float segW = width() / static_cast<float>(labels_.size());
        const auto index = static_cast<int>(x / segW);
        const int clamped = std::clamp(index, 0, static_cast<int>(labels_.size()) - 1);
        binding_.setValue(static_cast<float>(clamped));
        redraw();
    }

    void draw(visage::Canvas& canvas) override
    {
        if (labels_.empty()) return;
        const auto tokens = theme_();
        const visage::Font font(10.0f, visage::fonts::Lato_Regular_ttf);
        const float segW = width() / static_cast<float>(labels_.size());
        const int selected = static_cast<int>(std::round(binding_.value()));
        for (std::size_t i = 0; i < labels_.size(); ++i)
        {
            const float x = static_cast<float>(i) * segW;
            const bool active = static_cast<int>(i) == selected;
            canvas.setColor(active ? tokens.accent.argb : tokens.surface.argb);
            canvas.roundedRectangle(x + 1.0f, 0.0f, segW - 2.0f, height(), 4.0f);
            canvas.setColor(active ? tokens.background.argb : tokens.text.argb);
            canvas.text(labels_[i].c_str(), font, visage::Font::kCenter,
                        x + 1.0f, 3.0f, segW - 2.0f, height() - 6.0f);
        }
    }

private:
    ParameterBinding& binding_;
    std::vector<std::string> labels_;
    ThemeGetter theme_;
};


// Single-line text field. Characters accumulate until the user commits
// (Enter key or explicit handleCommit() call).
class AtsTextField final : public visage::Frame
{
public:
    using ThemeGetter = std::function<ThemeTokens()>;
    using Commit = std::function<void(const std::string&)>;

    AtsTextField(Commit onCommit, ThemeGetter theme)
        : onCommit_(std::move(onCommit)), theme_(std::move(theme))
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
        redraw();
    }

    void handleBackspace()
    {
        if (!text_.empty())
        {
            text_.pop_back();
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
        const visage::Font font(10.0f, visage::fonts::Lato_Regular_ttf);
        canvas.setColor(tokens.surface.argb);
        canvas.roundedRectangle(0.0f, 0.0f, width(), height(), 4.0f);
        if (focused_)
        {
            canvas.setColor(tokens.accent.argb);
            canvas.roundedRectangle(0.0f, 0.0f, width(), height(), 4.0f);
        }
        canvas.setColor(tokens.text.argb);
        const auto display = text_ + (focused_ ? "|" : "");
        canvas.text(display.c_str(), font, visage::Font::kLeft,
                    6.0f, 3.0f, width() - 8.0f, height() - 6.0f);
    }

private:
    Commit onCommit_;
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
        const visage::Font font(9.0f, visage::fonts::Lato_Regular_ttf);
        canvas.setColor(tokens.panel.argb);
        canvas.roundedRectangle(0.0f, 0.0f, width(), height(), 4.0f);
        canvas.setColor(tokens.text.argb);
        canvas.text(text_.c_str(), font, visage::Font::kCenter,
                    4.0f, 4.0f, width() - 8.0f, height() - 8.0f);
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


// Tab bar + page container. Child frames are added via addPage(); only the
// selected page is visible at a time. The tab bar is drawn at the top of
// this frame; pages are positioned below it.
class AtsPageContainer final : public visage::Frame
{
public:
    using ThemeGetter = std::function<ThemeTokens()>;
    using PageChange = std::function<void(std::size_t)>;

    static constexpr float kTabHeight = 28.0f;

    AtsPageContainer(std::vector<std::string> labels, ThemeGetter theme,
                     PageChange onPageChange = {})
        : labels_(std::move(labels))
        , theme_(std::move(theme))
        , onPageChange_(std::move(onPageChange))
    {
        onMouseDown() += [this](const visage::MouseEvent& event) {
            const auto& p = event.relativePosition();
            if (p.y < kTabHeight)
                handlePageClick(p.x);
        };
    }

    void handlePageClick(float x)
    {
        if (labels_.empty() || width() <= 0.0f) return;
        const float tabW = width() / static_cast<float>(labels_.size());
        const auto index = static_cast<std::size_t>(std::clamp(
            static_cast<int>(x / tabW), 0, static_cast<int>(labels_.size()) - 1));
        selectPage(index);
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
        page->setBounds(0.0f, kTabHeight, width(), height() - kTabHeight);
        page->setVisible(visible);
        pages_.push_back(page.get());
        addChild(std::move(page));
    }

    [[nodiscard]] std::size_t currentPage() const noexcept { return currentPage_; }

    void draw(visage::Canvas& canvas) override
    {
        if (labels_.empty()) return;
        const auto tokens = theme_();
        const visage::Font font(10.0f, visage::fonts::Lato_Regular_ttf);
        const float tabW = width() / static_cast<float>(labels_.size());
        for (std::size_t i = 0; i < labels_.size(); ++i)
        {
            const float x = static_cast<float>(i) * tabW;
            const bool active = i == currentPage_;
            canvas.setColor(active ? tokens.accent.argb : tokens.panel.argb);
            canvas.roundedRectangle(x + 1.0f, 1.0f, tabW - 2.0f, kTabHeight - 2.0f, 4.0f);
            canvas.setColor(active ? tokens.background.argb : tokens.text.argb);
            canvas.text(labels_[i].c_str(), font, visage::Font::kCenter,
                        x + 1.0f, 5.0f, tabW - 2.0f, kTabHeight - 10.0f);
        }
    }

private:
    std::vector<std::string> labels_;
    ThemeGetter theme_;
    PageChange onPageChange_;
    std::vector<visage::Frame*> pages_;
    std::size_t currentPage_ = 0;
};

} // namespace ats::ui
