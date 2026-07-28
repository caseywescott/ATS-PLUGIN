#include "VisageEditor.h"
#include "EditorShell.h"
#include "VisageNativeMac.h"

#include <embedded/fonts.h>
#include <visage/app.h>
#include <visage_ui/popup_menu.h>

#include <algorithm>
#include <memory>

namespace ats::ui
{
namespace
{

class ProbeFrame final : public visage::Frame
{
public:
    explicit ProbeFrame(EditorEventSink sink) : sink_(sink)
    {
        setAcceptsKeystrokes(true);
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
            if (point.y >= 56.0f && point.y < 92.0f && point.x >= 16.0f)
            {
                const auto page = static_cast<std::size_t>((point.x - 16.0f) / 126.0f);
                if (shell_.selectPage(page))
                {
                    if (sink_.pageSelected)
                        sink_.pageSelected(sink_.context, static_cast<int>(page));
                    redraw();
                }
                requestKeyboardFocus();
                return;
            }
            if (shell_.state().page == EditorPage::analysis && point.y >= 190.0f &&
                point.y < 228.0f && point.x >= 18.0f && point.x <= 438.0f)
            {
                parameterDragging_ = true;
                parameterValue_ = parameterAt(point.x);
                emitParameterGesture(parameterValue_, true, false);
                requestKeyboardFocus();
                return;
            }
            if (sink_.mouseDown)
                sink_.mouseDown(sink_.context);
            requestKeyboardFocus();
        };
        onMouseDrag() += [this](const visage::MouseEvent& event) {
            if (!parameterDragging_)
                return;
            parameterValue_ = parameterAt(event.relativePosition().x);
            emitParameterGesture(parameterValue_, false, false);
            redraw();
        };
        onMouseUp() += [this](const visage::MouseEvent&) {
            if (parameterDragging_)
            {
                emitParameterGesture(parameterValue_, false, true);
                parameterDragging_ = false;
                return;
            }
            if (sink_.mouseUp)
                sink_.mouseUp(sink_.context);
        };
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
            canvas.setColor(tokens.secondaryText);
            canvas.text("SOURCE A", labelFont, visage::Font::kLeft,
                        18.0f, 112.0f, 170.0f, 18.0f);
            canvas.text(session.sources[0].hasModel ? "READY" : "DROP WAV / ATS",
                        labelFont, visage::Font::kLeft, 18.0f, 132.0f, 220.0f, 18.0f);
            canvas.text("SOURCE B", labelFont, visage::Font::kLeft,
                        258.0f, 112.0f, 170.0f, 18.0f);
            canvas.text(session.sources[1].hasModel ? "READY" : "DROP WAV / ATS",
                        labelFont, visage::Font::kLeft, 258.0f, 132.0f, 220.0f, 18.0f);
            canvas.text("PLAY  BOTH  |  SMEAR OFF  |  SPECTRUM  COMBINED  |  A -> B",
                        labelFont, visage::Font::kLeft, 18.0f, 176.0f, 520.0f, 18.0f);
            canvas.setColor(tokens.accent);
            canvas.rectangle(18.0f, 204.0f, 380.0f, 3.0f);
        }
        else if (selected == static_cast<std::size_t>(EditorPage::analysis))
        {
            canvas.setColor(tokens.secondaryText);
            canvas.text("ANALYSIS PROFILE  |  18 CONTROLS  |  RANGE / TRACKING / THRESHOLDS",
                        labelFont, visage::Font::kLeft, 18.0f, 112.0f, 600.0f, 18.0f);
            canvas.text(shell_.analysis().canCancel() ? "ANALYZING  |  CANCEL ENABLED" : "READY  |  APPLY / ANALYZE CURRENT",
                        labelFont, visage::Font::kLeft, 18.0f, 140.0f, 520.0f, 18.0f);
            canvas.text("FREQUENCY", labelFont, visage::Font::kLeft,
                        18.0f, 184.0f, 140.0f, 14.0f);
            canvas.setColor(tokens.surface);
            canvas.roundedRectangle(18.0f, 198.0f, 420.0f, 18.0f, 4.0f);
            canvas.setColor(tokens.accent);
            canvas.roundedRectangle(18.0f, 198.0f, 420.0f * parameterValue_, 18.0f, 4.0f);
            canvas.setColor(tokens.accent);
            canvas.rectangle(18.0f, 172.0f, 420.0f, 3.0f);
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
            canvas.rectangle(18.0f, 172.0f, 320.0f, 3.0f);
        }
        else if (selected == static_cast<std::size_t>(EditorPage::partials))
        {
            canvas.setColor(tokens.secondaryText);
            canvas.text(shell_.partials().view().editing() ? "CELL EDIT  |  AMPLITUDE / FREQUENCY  |  APPLY"
                                                            : "PARTIAL / FRAME  |  SELECT A CELL",
                        labelFont, visage::Font::kLeft, 18.0f, 112.0f, 600.0f, 18.0f);
            canvas.setColor(tokens.accent);
            canvas.rectangle(18.0f, 144.0f, 260.0f, 3.0f);
        }
        else if (selected == static_cast<std::size_t>(EditorPage::math))
        {
            canvas.setColor(tokens.secondaryText);
            canvas.text("MATHEXPR  |  MULTILINE SCRIPT  |  APPLY",
                        labelFont, visage::Font::kLeft, 18.0f, 112.0f, 560.0f, 18.0f);
            canvas.text(MathPageModel::helpText().data(), labelFont, visage::Font::kLeft,
                        18.0f, 140.0f, 620.0f, 18.0f);
            canvas.setColor(tokens.accent);
            canvas.rectangle(18.0f, 172.0f, 280.0f, 3.0f);
        }
    }

    bool receivesTextInput() override { return true; }

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
    [[nodiscard]] float parameterAt(float x) const noexcept
    {
        return std::clamp((x - 18.0f) / 420.0f, 0.0f, 1.0f);
    }

    void emitParameterGesture(float value, bool begin, bool end)
    {
        if (sink_.parameterGesture)
            sink_.parameterGesture(sink_.context, "analysis.frequency", value, begin, end);
    }

    struct VisageTokens
    {
        std::uint32_t background;
        std::uint32_t panel;
        std::uint32_t surface;
        std::uint32_t text;
        std::uint32_t secondaryText;
        std::uint32_t accent;
    };

    [[nodiscard]] VisageTokens visageTokens() const noexcept
    {
        const auto tokens = ThemeTokens::forTheme(shell_.state().theme);
        return { tokens.background.argb, tokens.panel.argb, tokens.surface.argb,
                 tokens.text.argb, tokens.secondaryText.argb, tokens.accent.argb };
    }

    EditorEventSink sink_{};
    EditorShellModel shell_{};
    bool parameterDragging_ = false;
    float parameterValue_ = 0.5f;
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
            if (app.width() > 0 && app.height() > 0)
            {
                width = static_cast<std::uint32_t>(app.width());
                height = static_cast<std::uint32_t>(app.height());
            }
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
    bool attached = false;
    bool visible = false;
};

VisageEditor::VisageEditor(EditorEventSink sink) : impl_(std::make_unique<Impl>(sink)) {}

VisageEditor::~VisageEditor() = default;

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
    return true;
}

bool VisageEditor::canResize() const { return true; }

void VisageEditor::setScale(double scale)
{
    impl_->app.setDpiScale(static_cast<float>(std::clamp(scale, 0.5, 4.0)));
}

bool VisageEditor::setParent(void* nativeParent)
{
    if (nativeParent == nullptr)
        return false;
    impl_->app.show(nativeParent);
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
