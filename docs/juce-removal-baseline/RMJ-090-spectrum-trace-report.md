# RMJ-090 pure spectrum trace contract

`SpectralTraceBuilder` and `SpectrumViewModel` now provide a renderer-neutral
spectrum snapshot boundary. The builder applies deterministic source/mode
selection, finite-value filtering, log-frequency and 72 dB amplitude
normalization, bounded partial/point/residual-column publication, and empty or
sparse input handling. The view model rejects stale source or display-shape
revisions before replacing its snapshot.

`tests/spectrum_trace_snapshot_test.cpp` covers source selection, combined and
residual-only modes, the 900/720/220-style limits through a smaller fixture,
inactive points, finite normalized output, and stale revision rejection. Real
backend drawing/dispatch and screenshot goldens remain separate gates.
