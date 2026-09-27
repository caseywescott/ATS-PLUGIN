# Repository audit — JUCE removal status and gap-closure task spec

> **Historical — not current status.** The live scoreboard is [`docs/STATUS.md`](STATUS.md). This document is kept for its reasoning and history; where it disagrees with STATUS.md, STATUS.md wins.

**Audit date:** 2026-09-18
**Branch:** `remove-juce` at `c598ea45` plus a dirty working tree
(65 content-modified files, ~7,075 insertions; 117 real untracked files)
**Method:** direct source/CMake/ledger inspection plus fresh incremental builds
and full CTest runs in `build-plugin` and `build-review-ui` on this machine.
This document audits *current* state; where it disagrees with
`docs/JUCE_REMOVAL_PARITY_AUDIT.md` (2026-08-05) or
`docs/JUCELESS_PARITY_COMPLETION_SPEC.md` (2026-08-22), current source
evidence wins.

Evidence commands run for this audit:

```text
cmake --build build-plugin -j8          # clean
cmake --build build-review-ui -j8       # clean
ctest --test-dir build-plugin           # 94/96 passed
ctest --test-dir build-review-ui        # 95/97 passed
grep -rIl juce src/  (case-insensitive) # 12 hits, all comments
git ls-files src/plugin | wc -l         # 0 — legacy JUCE tree deleted
grep FetchContent/juce_add/juce:: over CMakeLists.txt, cmake/, src/, tests/  # 0 hits
python3 -json inspection of tests/architecture/ats_juce_replacement_traceability.json
python3 -json inspection of tests/ui/ats_ui_parity_manifest.json
```

## 1. Verdict

**The JUCE removal has succeeded technically.** There is no JUCE in the build
graph, no `src/plugin`, no JUCE FetchContent, no `juce::` symbol anywhere in
`src/` or `tests/`, and the `ats_no_juce_native_scan` CTest gate enforces it.
The difficulty you are feeling is no longer *removal* — it is the last three
kinds of work the spec always said would be hardest:

1. **Evidence, not code.** The remaining RMJ cards (31–35) are almost entirely
   proof obligations: screenshot matrices, live-DAW dispatch, the release
   compatibility matrix, and ledger promotion. 31 of 40 architecture rows sit
   at `implemented` because nothing has generated the cross-layer evidence to
   promote them, not because the code is missing.
2. **Three red tests** (§3.2) that block any "clean suite" claim.
3. **Process risk that dwarfs both:** roughly 7,000 lines of parity-critical
   work — including the governing spec documents themselves — exist only as
   uncommitted files on one exFAT external drive, and CI is configured so that
   it never runs for this branch at all.

The P0 findings of the 2026-08-22 completion spec are now substantially
closed in source (verified this audit):

| 2026-08-22 finding | Status today | Evidence |
|---|---|---|
| P0-1 editor is a coordinate probe | **Fixed** | `VisageEditor.cpp` mounts `AtsPageContainer` + five page frames; no `ProbeFrame` reference remains |
| P0-2 UI disconnected from host | **Fixed** | `EditorSnapshot` defined and published through `HostAdapter`, VST3, AU, and standalone wrappers |
| P0-3 no spectrum publication path | **Fixed** | `EditorSnapshot.spectrum` → `setSpectrumSnapshot()` in the production refresh path |
| P0-4 state loses session content | **Fixed** | `captureState()` serializes `atsModel`, variations, morph sources, residual states |
| P0-4a source identity conflation | **Fixed** | `enum class SourceIdentity { current, sourceA, sourceB, morphTimeline }` at every boundary |
| P0-5 undo not connected | **Fixed** | `recordMutation()` transaction helper with ~13 production call sites |
| P0-6 spectrum editing reduced | **Largely fixed** | solo/freeze/prune/tilt in `PartialEditService`; `SpectrumInteractionModel` +699 lines; spectrum parity verified 2026-08-27 (20.8/16.9 ms vs 25 ms budget) |
| P0-7 truncated UI payloads | **Fixed per docs** | source-contract timeline/timbre/onset payloads; needs the differential matrix (T-301) for final proof |
| P0-8 evidence ahead of production | **Still true** | this is the remaining work — see §3.3 |

## 2. What is genuinely done (do not redo)

- JUCE-free build graph, pinned SDKs (VST3 `05c4a976…`, AudioUnitSDK
  `53a9a200…`, Visage `20de5946…`), configure-time pin enforcement.
- 81-parameter registry with independent golden manifest.
- `LegacyStateCodec` v2–v18 with full-state capture/restore and full-state
  `.atspreset` user presets.
- Framework-free engine/DSP in `src/core` with the §14.2 audio golden matrix.
- RT safety: dense-index parameter reads, `playback_rt_guard_test` allocation
  guard, `JobScheduler`-owned workers (no `std::async` in production).
- Mounted Visage editor: widgets, five pages, seven-mode spectrum renderer
  with LUT/glow/isometric layers, popup menus, native dialogs.
- Ledger: architecture 31 `implemented` / 1 `verified` / 8 `deviated` (all 8
  deviations are the recorded DEV-SVCL service-ownership decisions); UI 38
  `implemented` / 12 `verified` / 3 `blocked` (ADR-0004 P1 deferrals:
  keyboard, output spectrum, output meter — legitimately out of scope).

## 3. Findings

### 3.1 Process and hygiene (highest risk, cheapest to fix)

**F-1 — The work is not backed up.** `origin/remove-juce` ends at `c598ea45`
(2026-08-24). Everything since — the spectrum pixel-parity port, the theme
foundation (`ThemeCanvas/ThemeLayout/ThemeMetrics/ThemeRecipes`), 65 modified
files, 117 untracked files — lives only in this working tree on an exFAT
drive. A drive failure loses ~3.5 weeks of the hardest work.

**F-2 — The governing documents are untracked.** `docs/JUCE_REMOVAL_ATS_SPEC.md`
(the master spec), `docs/JUCELESS_PARITY_COMPLETION_SPEC.md`,
`docs/ats-mearcstapa-ui-spec.md`, the spectrum parity specs, and ~55 other
docs are untracked. The project's source of truth cannot be recovered from
git.

**F-3 — CI never runs for this branch.** `.github/workflows/ci.yml` triggers
only on push to `master`/`main` and on PRs. Every push to `remove-juce` skips
CI entirely. CI also builds neither the `ATS_BUILD_VISAGE_UI_PROBE=ON` config
nor the native-wrapper config with pinned SDK checkouts, so none of the UI or
host tests run anywhere but this machine.

**F-4 — 298 AppleDouble `._*` files** pollute `src/`, `tests/`, `docs/`,
`tools/` (none tracked, but they inflate `git status` to 735 entries and have
previously produced corrupt `._pack-*.idx` noise). `core.fileMode` is `true`,
so exFAT produces 237 phantom mode-only diffs (`100644 → 100755`) hiding the
65 real content changes.

**F-5 — Status documentation contradicts itself.** Three overlapping status
sources disagree: `JUCE_REMOVAL_TASKS.md` ("execution status 2026-07-29"),
`JUCE_REMOVAL_PARITY_AUDIT.md` (2026-08-05, says P0-7 open), and
`JUCELESS_PARITY_COMPLETION_SPEC.md` (2026-08-22, supersedes both, itself now
partially stale per §1 above). Anyone (human or agent) resuming work must
re-derive the truth, which is expensive and error-prone — this audit had to.

### 3.2 Red tests (block any release claim)

| Test | Config | Failure | Character |
|---|---|---|---|
| `host_lifecycle_test` | both | `left[0] > 0.49f && right[0] < -0.49f` (line 146, panning) | pre-existing since ≤2026-08-25 |
| `ats_vst3_artifact_test` | build-plugin | "processor publishes controller-imported analysis" | pre-existing since ≤2026-08-25 |
| `popup_focus_retention_test` | build-review-ui | `state.popupShown == 1` fails at line 148 — popup never opens | newer; test was written 2026-08-27 to catch a real popup-dismissal defect |

Per the repo's own rule these must be triaged by reinstating/reproducing the
defect, not assumed stale. Note the popup failure may be an environment issue
(the test needs a real run loop and window server), but "may be" is not
evidence.

### 3.3 Product gaps (the remaining RMJ 31–35 work)

**F-6 — No deterministic screenshot / layout-parity matrix (card 31).** Zero
1x/2x page/theme/mode captures exist as evidence. This is also the only way
to prove the Ember theme foundation and the SESSION-tab overlap fixes
actually render correctly — currently unproven.

**F-7 — No live-dispatch matrix in standalone + one DAW (card 32).** The
Steinberg validator passes (47/0) and `auval` passes, but no DAW has
exercised the release artifact. The §6 differential matrix (same action via
VST3 message / AU direct / standalone → byte-identical state) has never run.

**F-8 — arm64 GUI probes trap in headless AppKit/Visage font init.** All live
GUI evidence is x86_64; the artifact users run in Logic is arm64
(`build-macos-arm64`). Until an arm64 windowed run exists, arm64 GUI behavior
is unproven.

**F-9 — Release/compatibility matrix not run (card 33)** — the full ladder:
v2–v17 state corpus, 81-parameter edges, sample-rate/block/channel/MIDI
matrix, sanitizers, Qatsh patch probes at the release hash.

**F-10 — Ledger promotion + final evidence packet (cards 34–35).** 31
architecture rows and 38 UI rows await promotion to `verified`; each needs
named cross-layer evidence, then the final packet (hashes, manifests,
deviation closure, docs refresh).

**F-11 — P1 hardening backlog** (from completion-spec §4, still valid):
theme/session UI persistence without an 82nd parameter; exact tooltip/copy/
focus/keyboard parity; factory preset value-level verification; resize
negotiation tests; the three ADR-0004 deferrals.

## 4. Recommendations

1. **Stop feature work for one day and commit the tree** (Phase 0 below). The
   expected loss from F-1/F-2 exceeds the cost of every other finding
   combined. The repo's own convention (selective staging, one reviewable
   story per commit) makes this ~8–12 commits, not one.
2. **Make CI cover this branch and both configs.** Until then every "pass"
   claim is single-machine. A `push: branches: [remove-juce]` line is one
   commit; a macOS job with the UI-probe config is a second.
3. **Triage the three reds before new evidence work** — the fail-first rule
   exists here precisely because this repo has produced vacuous passes
   (asserts erased outside `tests/`, stale binaries on exFAT, grace-window
   popup tests).
4. **Collapse status truth into one file.** Mark the two older audit/status
   docs as historical, keep `JUCELESS_PARITY_COMPLETION_SPEC.md` (or this
   file) as the single live scoreboard, and update it per commit as the RMJ
   rules already require.
5. **Treat the remaining work as evidence engineering.** Nothing in §3.3
   requires new product code except small fixes fallout. Plan runs, capture
   artifacts, promote rows.

## 5. Gap-closure task spec

Conventions: every task = one commit (or a small stack) per repo convention;
each has acceptance evidence; dependencies are explicit. IDs are `T-<phase><nn>`.
Rules that always apply: stage by explicit path (never `git add -A`); run
`build-plugin` ctest after touching `src/host/**`, `build-review-ui` after
`src/ui/**`; new test targets outside `tests/` need
`target_compile_options(<t> PRIVATE -UNDEBUG)`; `touch` sources after any
out-of-band restore before rebuilding (exFAT mtime).

### Phase 0 — safeguard the work (no dependencies, do first)

**T-001 — Set `core.fileMode false`; add ignore rules.**
Scope: `git config core.fileMode false`; add `._*`, `__pycache__/`,
`.DS_Store` to `.gitignore` if absent.
Acceptance: `git status --porcelain | wc -l` drops from ~735 to ~185 (65
modified + ~117 untracked + few); zero mode-only diffs remain.

**T-002 — Commit the governing documents.**
Scope: stage every untracked file under `docs/` that is a spec, ADR, surface
record, or RMJ report (~60 files; exclude scratch). One
`docs(spec): track governing JUCE-removal and parity specifications` commit.
Acceptance: `git ls-files docs/JUCE_REMOVAL_ATS_SPEC.md` non-empty; no spec
referenced by another tracked doc is untracked.
Depends: T-001.

**T-003 — Commit the uncommitted implementation in reviewable slices.**
Scope: split the 65 modified + ~30 untracked source/test files into coherent
commits following the existing subject style, approximately:
(a) `feat(ui): theme foundation` — `ThemeCanvas/Layout/Metrics/Recipes.h` +
three theme spec tests; (b) `feat(ui): spectrum pixel parity` —
`SpectrumChrome/Geometry/PathBuilder/Rdc.h`, renderer/builder diffs, spectrum
tests + fixtures; (c) `feat(engine): partial edit parity ops` —
`PartialEditService` (+115), `MorphTrajectoryRenderer`, engine diffs;
(d) `feat(host): editor snapshot + state completeness` — `HostAdapter.h`
(+413), wrapper diffs; (e) `feat(ui): mounted pages` — `AtsEditorPages.h`
(+1599), `VisageEditor` diffs, dispatch tests; (f) scripts/build/docs
remainder. Each slice must build and pass its config's suite (modulo the
three known reds) before committing.
Acceptance: `git status --porcelain` empty except deliberate exclusions;
every commit message carries `Traceability:`/`Evidence:` trailers per RMJ
rules.
Depends: T-001, T-002.

**T-004 — Push and verify remote.**
Scope: push `remove-juce`; confirm `origin/remove-juce` == local HEAD.
Acceptance: `git log origin/remove-juce -1` shows today's head. (Ask before
pushing if anything in T-003 was ambiguous.)
Depends: T-003.

**T-005 — Make CI run for this branch and both configs.**
Scope: add `remove-juce` (or `branches: ['**']`) to the push trigger; add a
macOS matrix row with `ATS_BUILD_VISAGE_UI_PROBE=ON` (headless-safe tests
only — exclude the windowed probes via a ctest label or `-E` regex); cache
the pinned VST3/AudioUnitSDK/Visage checkouts.
Acceptance: a CI run visible on GitHub for a `remove-juce` push, green except
documented exclusions.
Depends: T-004.

**T-006 — Delete AppleDouble files and add a hygiene script.**
Scope: `find … -name '._*' -delete` outside `.git`; add
`scripts/clean_appledouble.sh`; document the exFAT quirks (fileMode, mtime,
AppleDouble) in `BUILDING.md`.
Acceptance: `find src tests docs tools cmake scripts -name '._*' | wc -l` = 0;
script is idempotent.
Depends: T-003 (so deletions don't race the staging work).

### Phase 1 — restore a truthful green suite

**T-101 — Triage `host_lifecycle_test` (panning assertion, line 146).**
Scope: determine whether constant-power panning regressed or the baseline
expectation is wrong, per the fail-first rule: find the commit where it went
red (`git bisect` against `build-plugin`), reinstate/fix accordingly.
Acceptance: test passes in both configs, or a recorded deviation explains the
expectation change with before/after audio evidence.

**T-102 — Triage `ats_vst3_artifact_test` ("processor publishes
controller-imported analysis").**
Scope: as T-101; this guards the VST3 controller→processor analysis
publication contract, adjacent to the P0-3 snapshot work — suspect a contract
drift when `EditorSnapshot` replaced the old telemetry path.
Acceptance: test passes in `build-plugin`; state/spectrum round-trip evidence
attached.
Depends: nothing, but coordinate with T-103 (same subsystem).

**T-103 — Triage `popup_focus_retention_test` (popup never shows).**
Scope: first rule out environment (needs `nextEventMatchingMask` +
`[NSRunLoop runMode:]` pumping ≥150 ms and a window server; run from a real
session). If it still fails, the popup-open regression is real — bisect the
uncommitted `VisageEditor`/`AtsEditorPages` diffs.
Acceptance: test passes when run in a windowed session; CI exclusion label
documented if it cannot run headless.

**T-104 — Add the red-test gate to CI.**
Scope: once T-101..103 resolve, remove any temporary exclusions so CI fails
on regression.
Acceptance: CI run with the full suite green.
Depends: T-005, T-101..T-103.

### Phase 2 — production parity evidence (cards 31–32)

**T-201 — Deterministic screenshot matrix (card 31).**
Scope: scripted capture (Quartz window-id, per the documented
`screencapture -o -l<id>` + pid-filter procedure) of five pages × both themes
× 1x/2x × min/design/max sizes, plus the seven spectrum modes with the
committed fixture; store under `docs/juce-removal-baseline/screenshots/` with
build hash + CMake cache + arch in a manifest. Kill any running standalone
first and verify process start time before capture (stale-window trap).
Acceptance: matrix complete; layout validator or eyeball checklist signed;
Ember theme and SESSION overlap fixes visibly correct; hashes recorded.
Depends: T-103 (popups must work), T-003.

**T-202 — Live dispatch matrix in standalone (card 32, part 1).**
Scope: run the real-dispatch harness against the standalone app in a windowed
session: every widget class, popup open/dismiss, text commit/focus loss,
undo/redo, source import, preview, cancellation, resize, teardown, with
action/result/telemetry logs.
Acceptance: checklist + logs stored as an RMJ report with build hash.
Depends: T-201.

**T-203 — arm64 windowed GUI evidence.**
Scope: rebuild `build-macos-arm64`, run the CocoaUI/standalone probes in a
real (non-headless) session on this machine — the SIGTRAP is specific to
headless font init, so a windowed arm64 run is expected to pass and is the
cheapest way to close F-8. Install via `scripts/install_logic_au.sh` (quit
Logic first, never kill it).
Acceptance: arm64 probe pass logs + `auval` at the same hash; receipt file
records the artifact.
Depends: T-202.

**T-204 — One-DAW live run (card 32, part 2).**
Scope: in Logic (arm64 AU): insert, all-81 automation write/read spot checks,
state save/reopen, preset load, editor open/close/reopen showing identical
state, bounce. Record the manual checklist + screen recording.
Acceptance: checklist committed as RMJ evidence; any defect becomes a new
Phase 1-style task before promotion proceeds.
Depends: T-203.

**T-205 — Differential wrapper matrix (completion-spec §6).**
Scope: automated test driving identical action sequences through the VST3
message bridge, AU direct path, and standalone path; assert byte-identical
captured state and equivalent audio.
Acceptance: new test green in both configs; covers timeline points, timbre
JSON, onset records (the former P0-7 payloads).
Depends: T-102.

### Phase 3 — release matrix, ledger, and the final packet (cards 33–35)

**T-301 — Release/compatibility matrix (card 33).**
Scope: one scripted run at a frozen hash: v2–v17 + v18 state corpus,
factory/user/ATS preset round-trips, 81-parameter edge goldens,
44.1/48/88.2/96 kHz × block sizes × channel × MIDI matrix,
ASan/TSan/UBSan configs, Qatsh patch probes (20/20), artifact identity
checks.
Acceptance: single `RMJ-100-release-matrix-report.md` with every command,
hash, and result.
Depends: T-104, T-205.

**T-302 — Ledger promotion sweep.**
Scope: for each of the 31 `implemented` architecture rows and 38 UI rows, cite
the T-201..T-301 evidence and promote to `verified`, or demote/record a
deviation where evidence stops short (the P0-8 discipline). Update both JSON
manifests and surface records together.
Acceptance: manifest validators pass; zero rows remain `implemented`;
deviation register has no unapproved entries.
Depends: T-201..T-205, T-301.

**T-303 — Documentation truth consolidation.**
Scope: mark `JUCE_REMOVAL_PARITY_AUDIT.md` and the status section of
`JUCE_REMOVAL_TASKS.md` as historical with a banner pointing at the live
scoreboard; refresh `JUCELESS_PARITY_COMPLETION_SPEC.md` §1 table (or replace
with this document's successor); update `BUILDING.md`/`RELEASE.md` release
hashes; final `NOTICE`/`THIRD_PARTY.md` re-check.
Acceptance: exactly one document claims to be current status, and it is.
Depends: T-302.

**T-304 — Final parity evidence packet (card 35).**
Scope: assemble hashes, manifests, screenshot/live/DAW evidence, deviation
approvals, clean-checkout configure/build/test log (fresh clone, not this
worktree — also proves T-002 captured everything).
Acceptance: release gate checklist in `JUCE_REMOVAL_TASKS.md` fully checked;
clean-clone build passes with zero local-only files needed.
Depends: T-301..T-303.

### Phase 4 — P1 hardening (post-parity, ordered by value)

**T-401 — Persist editor theme/session UI state** per ADR-0004 without adding
an automatable parameter. Acceptance: theme survives editor close/reopen and
DAW reopen; no change to the 81-parameter golden.

**T-402 — Copy/tooltip/focus/keyboard parity pass** against
`ats-mearcstapa-ui-spec.md` exact copy tables; screenshot-diff assisted.
Acceptance: copy audit checklist; discrepancies fixed or recorded.

**T-403 — Factory preset value-level verification** (names, descriptions, all
values vs the 12 legacy recipes, not just index validity).

**T-404 — Resize negotiation tests** for min/preferred/max host negotiation in
all three wrappers.

**T-405 — Reconsider ADR-0004 deferrals** (on-screen keyboard, output
spectrum, output meter) as feature work; requires FFT-on-output-bus for the
spectrum. Explicitly out of parity scope.

### Dependency graph (summary)

```
T-001 → T-002 → T-003 → T-004 → T-005 ─┐
                 │                      ├→ T-104 ─┐
                 └→ T-006   T-101..103 ─┘         │
T-003/T-103 → T-201 → T-202 → T-203 → T-204       ├→ T-301 → T-302 → T-303 → T-304 → Phase 4
T-102 → T-205 ────────────────────────────────────┘
```

The critical path is Phase 0 (a day), then the three test triages, then the
evidence chain T-201→T-204. Everything in Phase 3 is mechanical once the
evidence exists.
