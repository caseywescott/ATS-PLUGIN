#pragma once

#include "PartialSelectionCriteria.h"
#include "ThemeRecipes.h"
#include "VisageWidgets.h"

#include <array>
#include <functional>
#include <memory>
#include <string>

namespace ats::ui
{

/**
    Select-by-criteria panel over the spectrum (toolbar SELECT...).  Six
    optional numeric fields - frequency range (Hz), minimum SMR (dB),
    minimum level (dB) and duration range (s) - and SELECT (replace the
    selection), ADD (unite with it) and CLOSE.  Matching runs in the owner,
    which reports the result line back through setStatus().
*/
class SpectrumSelectionPanel final : public visage::Frame
{
public:
    using ThemeGetter = std::function<ThemeTokens()>;
    /** criteria, add-to-selection */
    using Apply = std::function<void(const PartialCriteria&, bool)>;
    using Close = std::function<void()>;

    static constexpr float kWidth = 300.0f;
    static constexpr float kHeight = 184.0f;

    SpectrumSelectionPanel(Apply apply, Close close, ThemeGetter theme)
        : apply_(std::move(apply)), close_(std::move(close)), theme_(std::move(theme))
    {
        for (auto& field : fields_)
        {
            auto owned = std::make_unique<AtsTextField>([this](const std::string&) { applyFields(false); },
                                                        theme_);
            field = owned.get();
            addChild(std::move(owned));
        }
        onMouseDown() += [this](const visage::MouseEvent& event) {
            const auto p = event.position;
            for (std::size_t i = 0; i < kButtons.size(); ++i)
                if (contains(buttonRect(i), p.x, p.y))
                {
                    if (i == 2) { if (close_) close_(); }
                    else applyFields(i == 1);
                    return;
                }
        };
        layout();
    }

    void resized() override { layout(); }

    void setStatus(std::string status)
    {
        status_ = std::move(status);
        redraw();
    }

    void draw(visage::Canvas& canvas) override
    {
        const auto tokens = theme_();
        paintThemeOps(canvas, ThemeRecipes::raisedPanel(tokens, { 0.0f, 0.0f, width(), height() },
                                                        ThemeRadius::control));
        const visage::Font title(ThemeFont::sectionLabel, visage::fonts::Lato_Regular_ttf);
        const visage::Font label(ThemeFont::controlLabel, visage::fonts::Lato_Regular_ttf);
        const visage::Font button(ThemeFont::button, visage::fonts::Lato_Regular_ttf);
        canvas.setColor(tokens.textSecondary.argb);
        canvas.text("SELECT PARTIALS", title, visage::Font::kLeft, kPad, 6.0f, 200.0f, 14.0f);
        for (std::size_t row = 0; row < kRowLabels.size(); ++row)
        {
            const auto y = rowY(row);
            canvas.setColor(tokens.textPrimary.argb);
            canvas.text(kRowLabels[row], label, visage::Font::kLeft, kPad, y, kLabelWidth, kRowHeight);
            if (row == 0 || row == 3)
            {
                canvas.setColor(tokens.textSecondary.argb);
                canvas.text("to", label, visage::Font::kCenter, toX(), y, kToWidth, kRowHeight);
            }
        }
        for (std::size_t i = 0; i < kButtons.size(); ++i)
        {
            const auto r = buttonRect(i);
            paintThemeOps(canvas, ThemeRecipes::buttonBackground(tokens, r, false, false));
            canvas.setColor(tokens.textPrimary.argb);
            canvas.text(kButtons[i], button, visage::Font::kCenter, r.x, r.y, r.width, r.height);
        }
        canvas.setColor(statusIsError_ ? tokens.accent.argb : tokens.textSecondary.argb);
        canvas.text(status_, label, visage::Font::kLeft, kPad, height() - 22.0f, width() - 2.0f * kPad, 16.0f);
    }

    /** Parses the fields; on an invalid one, reports it and returns false. */
    bool readCriteria(PartialCriteria& criteria)
    {
        std::array<std::optional<double>*, 6> targets {
            &criteria.minFrequencyHz, &criteria.maxFrequencyHz, &criteria.minSmrDb,
            &criteria.minLevelDb, &criteria.minSeconds, &criteria.maxSeconds };
        static constexpr std::array<const char*, 6> kNames {
            "frequency from", "frequency to", "min SMR", "min level", "duration from", "duration to" };
        for (std::size_t i = 0; i < fields_.size(); ++i)
            if (!parseCriterion(fields_[i]->text(), *targets[i]))
            {
                status_ = std::string("not a number: ") + kNames[i];
                statusIsError_ = true;
                redraw();
                return false;
            }
        statusIsError_ = false;
        return true;
    }

    /** For tests: field order is freq from/to, SMR, level, duration from/to. */
    void setFieldText(std::size_t index, std::string text) { fields_.at(index)->setText(std::move(text)); }

private:
    static constexpr float kPad = 10.0f;
    static constexpr float kRowHeight = 22.0f;
    static constexpr float kRowGap = 4.0f;
    static constexpr float kLabelWidth = 96.0f;
    static constexpr float kToWidth = 22.0f;
    static constexpr float kFieldWidth = 72.0f;
    static constexpr std::array<const char*, 4> kRowLabels {
        "Frequency (Hz)", "Min SMR (dB)", "Min level (dB)", "Duration (s)" };
    static constexpr std::array<const char*, 3> kButtons { "SELECT", "ADD", "CLOSE" };

    static bool contains(UiRect r, float x, float y)
    { return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height; }

    [[nodiscard]] float rowY(std::size_t row) const
    { return 26.0f + static_cast<float>(row) * (kRowHeight + kRowGap); }
    [[nodiscard]] float fieldX() const { return kPad + kLabelWidth; }
    [[nodiscard]] float toX() const { return fieldX() + kFieldWidth; }

    [[nodiscard]] UiRect buttonRect(std::size_t i) const
    {
        const auto w = (width() - 2.0f * kPad - 2.0f * 6.0f) / 3.0f;
        return { kPad + static_cast<float>(i) * (w + 6.0f), rowY(4) + 2.0f, w, 24.0f };
    }

    void layout()
    {
        // fields_: freq from, freq to, SMR, level, duration from, duration to
        const std::array<std::pair<std::size_t, int>, 6> place {{
            { 0, 0 }, { 0, 1 }, { 1, 0 }, { 2, 0 }, { 3, 0 }, { 3, 1 } }};
        for (std::size_t i = 0; i < fields_.size(); ++i)
        {
            const auto [row, column] = place[i];
            const auto x = column == 0 ? fieldX() : toX() + kToWidth;
            fields_[i]->setBounds(x, rowY(row), kFieldWidth, kRowHeight);
        }
    }

    void applyFields(bool add)
    {
        PartialCriteria criteria;
        if (!readCriteria(criteria))
            return;
        if (criteria.empty())
        {
            status_ = "enter at least one value";
            statusIsError_ = true;
            redraw();
            return;
        }
        if (apply_)
            apply_(criteria, add);
    }

    Apply apply_;
    Close close_;
    ThemeGetter theme_;
    std::array<AtsTextField*, 6> fields_ {};
    std::string status_ { "blank fields are not used" };
    bool statusIsError_ = false;
};

} // namespace ats::ui
