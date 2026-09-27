#pragma once

#include "Editor.h"
#include "SpectralTraceBuilder.h"
#include "ThemeTokens.h"

#include <memory>
#include <string>

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
    void runStandaloneEventLoop();
    void show() override;
    void hide() override;
    bool isShowing() const override;
    void* nativeHandle() const override;
    EditorSizeLimits sizeLimits() const override;
    EditorSize constrainSize(EditorSize requested) const override;

    void setTheme(ThemeId theme);
    // Selects one of the five pages (SESSION..MATH) as a tab click would.
    void selectPage(int page);
    // Saves the most recently rendered frame as PNG; false without a window.
    bool captureScreenshot(const std::string& path);
    void setHistoryAvailability(bool canUndo, bool canRedo);
    void setActivity(bool analysisRunning, bool renderRunning);
    void setTelemetry(TelemetrySnapshot snapshot);
    void setEditorSnapshot(EditorSnapshot snapshot);
    void setSpectrumSnapshot(SpectralTraceSnapshot snapshot);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ats::ui
