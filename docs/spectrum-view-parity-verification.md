# Spectrum view parity verification

The parity implementation is verified with the following reproducible checks:

```text
cmake --build build-review-ui --target spectrum_chrome_test spectrum_renderer_test spectrum_renderer_perf_test -j2
ctest --test-dir build-review-ui --output-on-failure -R 'spectrum_(chrome|renderer|renderer_perf)_test'
python3 tests/architecture/traceability_manifest_check.py
python3 tests/ui/ui_parity_manifest_check.py
git diff --check
```

The renderer regression includes both sides of the zoom-culling defect: a point at
`timeCenter=0.25` is asserted at `plot.centerX` with `timeZoom=4`, and a point outside the
visible range is asserted absent. The chrome regression asserts the five-pixel header anchor,
the 204-pixel zoom bar plus six-pixel trailing gap, all row-one widths, every toolbar button
width, the residual dB controls, and the envelope smooth controls.

`spectrum_renderer_perf_test` renders the maximum 900-partial × 720-frame snapshot at
1600×900 and reports the five-run steady-state median. It fails if the median exceeds the
25 ms parity budget. Verification runs on 2026-08-27 reported `20.7874 ms` and
`16.8773 ms` medians.
