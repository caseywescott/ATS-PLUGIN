# UI-LIFECYCLE-01 — Host/editor lifecycle

Status: implemented

Replaces `PluginEditor` JUCE component construction / destruction lifecycle,
`AudioProcessorEditor` resize contract, and standalone window teardown.

## Ownership

`VisageEditor` (in `src/ui/VisageEditor.h`) is the top-level `visage::Frame`
subclass.  Its lifecycle is managed through `EditorSink` callbacks:

```cpp
struct EditorSink {
    void* context = nullptr;
    void (*created)(void*, int w, int h) = nullptr;
    void (*resized)(void*, int w, int h) = nullptr;
    void (*destroyed)(void*) = nullptr;
    void (*dialogRequested)(void*, int kind) = nullptr;
    void (*dialogResolved)(void*, int kind, const char* path) = nullptr;
    void (*dialogError)(void*, int kind, const char* code, const char* msg) = nullptr;
    bool (*dialogRequiresPrompt)(void*, int kind) = nullptr;
};
```

The host (`HostAdapter` / `PluginModel`) sets this sink before attaching the
editor to a window.  `VisageNativeMac` creates the Visage window and calls
`EditorSink::created`.

## Resize contract

`VisageEditor::resized()` is called by the Visage event loop when the host
changes the window size.  The minimum size is 1100×860; maximum is 1900×1400
(ADR-0004: promoted target geometry).  The editor reflows all layout at the
current size via `draw()`.

## Teardown

When the host closes the plugin editor window, `EditorSink::destroyed` is
called before the `VisageEditor` destructor.  All `visage::PopupMenu` objects
are closed and all pending worker callbacks are cancelled via `CancelJobsAction`
before returning from `destroyed`.

Evidence:

```text
host_lifecycle_test: exercises created/destroyed callbacks.
real_dispatch_harness_test: tests creation at standalone size with resize.
```

Deviation: AU host (Logic / GarageBand) resize behaviour differs from VST3
(Ableton); AU resizer not tested in headless CI.
