# SP-DRAW-03 — Spectrum view model: stale-snapshot rejection

Status: implemented

Replaces `SpectrogramPanel`'s inline generation-check that discarded out-of-order
render results arriving on the message thread.

## Ownership

`SpectrumViewModel::accept()` (in `src/ui/SpectrumViewModel.h`) enforces a
monotonic-revision contract:

- Rejects any snapshot whose `sourceRevision < current.sourceRevision`.
- Rejects any snapshot with the same `sourceRevision` but a lower
  `displayShapeRevision`.
- Accepts equal revisions (idempotent update).

When rejected, `accept()` returns `false`, the stored snapshot is unchanged, and
`cacheRevision()` is unchanged — the renderer will not re-draw for a stale result.

Evidence:

```text
arch -x86_64 cmake --build build-review --target spectrum_dispatch_test -j4
ctest --test-dir build-review --output-on-failure -R '^spectrum_dispatch_test$'
```

`spectrum_dispatch_test::test_stale_rejection` verifies: stale `sourceRevision`
rejected; stale `displayShapeRevision` at same `sourceRevision` rejected; equal
revisions accepted; snapshot unchanged after rejection.

Deviations: race-condition evidence (concurrent worker produce / UI consume) is
covered by the separate `snapshot_publication_test` thread-safety harness.
