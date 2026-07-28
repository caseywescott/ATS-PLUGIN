#pragma once

#include "Editor.h"

#include <memory>

namespace ats::ui
{

class VisageEditor final : public IEditor
{
public:
    explicit VisageEditor(EditorEventSink sink = {});
    ~VisageEditor() override;

    VisageEditor(const VisageEditor&) = delete;
    VisageEditor& operator=(const VisageEditor&) = delete;

    void getSize(std::uint32_t& width, std::uint32_t& height) const override;
    bool setSize(std::uint32_t width, std::uint32_t height) override;
    bool canResize() const override;
    void setScale(double scale) override;
    bool setParent(void* nativeParent) override;
    bool showStandalone() override;
    void show() override;
    void hide() override;
    bool isShowing() const override;
    void* nativeHandle() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ats::ui
