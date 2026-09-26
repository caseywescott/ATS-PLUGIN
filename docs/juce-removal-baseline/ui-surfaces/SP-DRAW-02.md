# SP-DRAW-02 — Spectrum cache revision: snapshot staleness gate

Status: implemented

Replaces `SpectrogramPanel`'s display-revision bookkeeping that compared the
incoming snapshot generation against the last-rendered one.

## Ownership

`SpectrumViewModel::cacheRevision()` (in `src/ui/SpectrumViewModel.h`) derives a
single monotonic scalar from the accepted snapshot:

```cpp
snapshot_.sourceRevision * 1000003ULL + snapshot_.displayShapeRevision
```

Returns `0` when no snapshot has been accepted. The caller compares the previous
and current revision values to decide whether to issue a re-render; no timer,
dirty flag, or boolean is needed.

## Behaviour

- `cacheRevision()` returns `0` before `accept()` is called or after `clear()`.
- Any combination of `sourceRevision`/`displayShapeRevision` that changes at
  least one field produces a different `cacheRevision`.
- The hash function multiplier (1 000 003) is a prime chosen so that incrementing
  only `displayShapeRevision` by 1 does not alias a prior `sourceRevision`
  increment.

Evidence:

```text
arch -x86_64 cmake --build build-review --target spectrum_dispatch_test -j4
ctest --test-dir build-review --output-on-failure -R '^spectrum_dispatch_test$'
```

`spectrum_dispatch_test::test_cache_revision` verifies: zero before accept,
non-zero after, changes when either revision field changes.

Deviations: cache-miss performance measurement under rapid display-shape changes
requires live profiling against the Visage render loop.
