#pragma once

#include "Editor.h"

#include <algorithm>

namespace ats::ui
{

// Supplemental contract test double. It deliberately does not claim that a
// native surface, focus, or platform event dispatch works.
class HeadlessEditor final : public IEditor
{
public:
    explicit HeadlessEditor(EditorEventSink sink = {}) : sink_(sink) {}

    void getSize(std::uint32_t& width, std::uint32_t& height) const override
    {
        width = width_;
        height = height_;
    }

    bool setSize(std::uint32_t width, std::uint32_t height) override
    {
        if (width == 0 || height == 0)
            return false;
        width_ = width;
        height_ = height;
        return true;
    }

    bool canResize() const override { return true; }

    void setScale(double scale) override
    {
        scale_ = std::clamp(scale, 0.5, 4.0);
    }

    bool setParent(void* nativeParent) override
    {
        parent_ = nativeParent;
        return nativeParent != nullptr;
    }

    void show() override { showing_ = parent_ != nullptr; }
    void hide() override { showing_ = false; }
    bool isShowing() const override { return showing_; }
    void* nativeHandle() const override { return nullptr; }

    [[nodiscard]] double scale() const noexcept { return scale_; }
    [[nodiscard]] void* parent() const noexcept { return parent_; }

private:
    EditorEventSink sink_{};
    std::uint32_t width_ = 1280;
    std::uint32_t height_ = 940;
    double scale_ = 1.0;
    void* parent_ = nullptr;
    bool showing_ = false;
};

} // namespace ats::ui
