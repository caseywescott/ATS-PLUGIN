# SpectralEnvelope3DPanel — Isometric Waterfall Display Spec

## Goal
Add an isometric 3D waterfall view of the spectral envelope over all analysis frames to the
Envelope tab group. Each frame's frequency-vs-amplitude curve is rendered at its position along
the time (Z) axis, producing a "waterfall" that shows how the spectral shape evolves over time.
Two envelope sets (source / target, toggled via checkboxes) can be shown simultaneously.

## Layout

```
┌──────────────────────────────────────────────────────────────────────┐
│ [✓] Source   [✓] Target     Frames: 48   Preset: Default (RDC Bark) │  ← toolbar
├──────────────────────────────────────────────────────────────────────┤
│                                                                      │
│              isometric 3D waterfall                                  │
│              (X=freq, Y=amp dB, Z=time)                              │
│                                                                      │
│   time axis label  ↗     freq axis label  ↘                         │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

## Data model

### Processor additions
New method on `AtsAudioProcessor`:
```cpp
bool buildAllFrameEnvelopes(int gridSize,
                            int maxFrames,
                            std::vector<ats::EnvelopeOverlayCurve>& sourceFrames,
                            std::vector<ats::EnvelopeOverlayCurve>& targetFrames,
                            juce::String& status);
```
- `gridSize` — frequency grid resolution per frame (e.g. 128 points)
- `maxFrames` — cap the number of displayed frames; input frames are uniformly decimated
- `sourceFrames[i]` — envelope curve for the i-th selected frame (first half of model)
- `targetFrames[i]` — envelope curve for the i-th selected frame (second half of model)
- Uses the currently selected estimator preset (same as EnvelopeOverlayPanel)
- Lock-safe: reads `analyzedModel` under `modelLock`

### Panel cached state
`SpectralEnvelope3DPanel` keeps:
- `std::vector<ats::EnvelopeOverlayCurve> sourceFrames_`
- `std::vector<ats::EnvelopeOverlayCurve> targetFrames_`
- Rebuilt on model revision change (timer at 4 Hz)

## Isometric projection

World coordinates (all normalized to [0,1]):
- `freq`  — frequency axis (0 = min freq, 1 = max freq)
- `frame` — time axis (0 = oldest frame, 1 = newest frame)
- `amp`   — amplitude axis (0 = minDb, 1 = maxDb)

Screen mapping:
```
sx = cx + (freq - frame) * scaleX
sy = cy - amp * scaleY + (freq + frame) * scaleDepth
```
where `cx`, `cy` are the center of the drawing area and `scaleX`, `scaleY`, `scaleDepth`
are chosen to fill roughly 90 % of the panel.

Painter's algorithm: render frames in reverse order (frame N first, frame 0 last) so that
later frames appear on top of earlier ones.

## Rendering details

- **Curve per frame**: draw a filled polyline.  Fill area downward to the "floor" of that
  frame's iso depth to give a solid waterfall appearance.
- **Source colour**: `0xff4090d0` (blue) with alpha gradient across time (0.4 → 0.9).
- **Target colour**: `0xffd0a030` (amber) with same alpha range, overlaid.
- **Fill**: same hue, 25 % opacity, filled to the zero-dB floor of the frame.
- **Axis labels**: thin iso-projected lines for the freq and time edges; text labels at tips.
- **dB range**: −80 dB to +12 dB (same as EnvelopeOverlayPanel).

## UI controls (toolbar)

| Control | Type | Default |
|---|---|---|
| Source checkbox | `juce::ToggleButton` | checked |
| Target checkbox | `juce::ToggleButton` | checked |
| Frame count display | `juce::Label` (read-only) | shows actual frame count |

## Files

| File | Role |
|---|---|
| `src/plugin/SpectralEnvelope3DPanel.h` | New component header |
| `src/plugin/SpectralEnvelope3DPanel.cpp` | New component implementation |
| `src/plugin/PluginProcessor.h` | Add `buildAllFrameEnvelopes()` declaration |
| `src/plugin/PluginProcessor.cpp` | Add `buildAllFrameEnvelopes()` implementation |
| `src/plugin/PluginEditor.h` | Add `SpectralEnvelope3DPanel` member |
| `src/plugin/PluginEditor.cpp` | Add "3D Env" tab |
| `src/plugin/CMakeLists.txt` | Add `SpectralEnvelope3DPanel.cpp` to sources |
