# RMJ-031 — Deterministic screenshot matrix (audit T-201, T-203)

**Date:** 2026-09-27
**Revision:** `02e75928` (manifests record the full hash)
**Tool:** `tools/ui_screenshot_matrix.mm` (target `ui_screenshot_matrix`,
built when `ATS_BUILD_VISAGE_UI_PROBE=ON`)

## Method

The tool attaches the production `VisageEditor` to a real `NSWindow`, applies a
state, and saves Visage's own composited frame (`ApplicationEditor::takeScreenshot`),
not a screen grab. A capture is accepted only when two consecutive composites
250 ms apart are pixel-identical. Every PNG is re-encoded through ImageIO and
hashed over its decoded RGBA pixels, so digests do not depend on the PNG encoder.

State per capture: registry-default parameters, current + source A loaded
(`synthetic-sparse-decay`, 5 partials, residual), and the fixture spectrum
from `tests/fixtures/spectrum-parity` in the requested mode.

Matrix: 5 pages × 2 themes × {min 1100×860, design 1280×940, max 1900×1400}
(30) plus 7 spectrum modes × 2 themes at the design size (14) = 44 captures.

```sh
cmake -S . -B build-arm64-ui -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DBUILD_PLUGIN=ON -DATS_BUILD_VISAGE_UI_PROBE=ON -DATS_VISAGE_DIR=<visage@20de5946> \
  -DSNDFILE_INCLUDE_DIR=vendor/sndfile-arm64/include \
  -DSNDFILE_LIBRARY=vendor/sndfile-arm64/lib/libsndfile.dylib \
  -DATS_AU_INSTALL_DIR=build-arm64-ui/au-install
cmake --build build-arm64-ui --target ui_screenshot_matrix
build-arm64-ui/ui_screenshot_matrix <out> --revision $(git rev-parse HEAD) --screen-grab
```

Needs a logged-in session with the window on screen: Visage only renders
visible windows.

## Results

| Check | Result |
|---|---|
| Captures, arm64 and x86_64 | 44/44 each, 0 failures |
| Run-to-run determinism (same arch, two runs) | 44/44 pixel-identical |
| arm64 vs x86_64 | 26/44 identical; the rest differ only by anti-aliasing (no pixel differs by more than 16/255) |
| Composite vs `screencapture -l` of the window (arm64, all 44, title bar cropped) | Same geometry; mean absolute difference 0.07–1.46 / 255 (colour management); SESSION captures have up to 5,651 px (~0.1%) above 32 / 255, which fits animated well elements. Before the fixes, the grab showed the same missing widgets as the composite, so the composite reflects what is on screen |
| Backing scale | 2 (Retina); Visage follows the screen, so a 1x set needs a non-Retina display |

Committed evidence: `screenshots/manifest-arm64.json`,
`screenshots/manifest-x86_64.json` (all 44 digests each) and the 24 arm64
design-size page and spectrum PNGs under `screenshots/arm64/`. Min/max sizes are
reproducible from the tool and verified by digest.

## Defects found and fixed while building the matrix

| Defect | Where it showed | Fix |
|---|---|---|
| Most of the editor vanished in Iso 3D Envelope / RDC modes: bgfx's transient buffer overflowed (~13k-quad default; the envelope alone emits ~11.8k) and every later batch was dropped | on screen, both archs | `01919d82`: 24/6 MB bgfx transient buffers, plus a 12k-segment per-frame cap on spectrum paths (large isometric models emitted ~103k) |
| Theme switch left the visible page's widgets in the old theme (ANALYSIS in Porcelain: dark tracks, near-invisible values) | Porcelain captures | `b473f72f`: `setTheme` redraws the whole tree |
| ANALYSIS/SYNTHESIS used segmented controls for every boolean and choice: "Window type" overlapped, RENDER toggles had no captions (spec §7.2, §301) | both themes | `b473f72f`: shared `makeParameterWidget` (toggle pills, combos above 3 choices, inline labels) |
| TIMBRE Brightness/Tilt showed `2.2209e-07` / `1.105e-07` instead of `0` | arm64 only | `02e75928`: exact `ParameterDescriptor::snap` |

## Review checklist (arm64, design size, both themes)

- [x] All five pages render completely in Ember and Porcelain.
- [x] Porcelain uses its own tokens on every page (teal accents, readable values).
- [x] All seven spectrum modes render their traces, and every mode keeps the rest of the editor intact.
- [x] Source cards reflect the loaded source (name, partial count, `+RES`).
- [x] SESSION well chrome (view chip, onsets, A/B, toolbar) is present in every mode.

Open observations, not fixed here (the parity-verified spectrum chrome and
copy tables own them; see T-402):

- SESSION well: the "ATS TRACE DISPLAY" caption sits under the Draw/Solo/Freeze
  (or Smooth) toolbar row, and the lowest frequency label is clipped at the plot
  edge.
- SYNTHESIS: "Residual only (mute partials)" clips at the column edge.
- MATH: the script editor keeps a dark well in Porcelain; confirm this matches
  the intended code-editor style.

## arm64 GUI (T-203)

`build-arm64-ui` runs the whole suite natively on the M1 Pro: 101/101 pass
(`ctest -LE perf`), including all 9 `windowed` tests (Visage editor probe,
real-dispatch harness, popup focus retention, native dialog contract, page
column layout, widget dispatch, standalone smoke + close smoke, AU editor view
probe). The headless font-init SIGTRAP noted in the audit (F-8) does not occur
in a windowed session.

Not done here: `auval` and a Logic run on the arm64 component. Both need the
component installed into `~/Library/Audio/Plug-Ins/Components`
(`scripts/install_logic_au.sh`, Logic quit first), which replaces the copy
Logic currently loads, so that is left to the owner (T-204).
