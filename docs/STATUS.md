# ATS Resynth: current status

**This is the single live status document.** Older status sources
(`JUCE_REMOVAL_TASKS.md` §"Current execution status", `JUCE_REMOVAL_PARITY_AUDIT.md`,
`JUCELESS_PARITY_COMPLETION_SPEC.md` §1, `REPO_AUDIT_2026-09-18.md`) are
historical and carry a banner pointing here. Update this file in the same
commit as any change that moves a ledger row or closes a task.

**As of:** 2026-09-27, branch `remove-juce`. Evidence was produced at
`f2057a51` and ledgers promoted at `f708d883`.

## Verdict

JUCE is gone from the build and the product runs end to end on arm64, the
architecture Logic loads. The remaining gates need a person or an account
action:

1. a Logic session with the arm64 AU (T-204), and `auval` / the VST3
   validator on installed artifacts (ARCH-HOST-001);
2. GitHub Actions billing: CI jobs stopped starting after `9ace67e8` with
   "recent account payments have failed or your spending limit needs to be
   increased"; everything after that is verified locally only.

## Suites (local)

| Tree | Arch / config | Result |
|---|---|---|
| `build-arm64-ui` | arm64, wrappers + Visage UI probe + windowed | 108/108 (`-LE perf`) |
| `build-plugin` | x86_64 (Rosetta) wrappers | 101/101 |
| `build-review-ui` | x86_64 UI probe | 100/100 |
| `build-macos-arm64` | arm64 release artifacts | 98/98; `verify_macos_artifacts.sh` all gates |
| `build-asan` / `build-tsan` | arm64 core, `scripts/run_sanitizers.sh` | 93/93 each |

The `cmake` on this machine is an x86_64 binary: trees without
`CMAKE_OSX_ARCHITECTURES=arm64` build x86_64 under Rosetta.

## Ledgers

| Ledger | verified | implemented | deviated | blocked |
|---|---|---|---|---|
| Architecture (`tests/architecture/ats_juce_replacement_traceability.json`) | 30 | 2 (ARCH-HOST-001, DSP-METER-001) | 8 (DEV-SVCL) | 0 |
| UI (`tests/ui/ats_ui_parity_manifest.json`) | 19 | 31 | 0 | 3 (ADR-0004 P1 deferrals) |

## Audit tasks (`REPO_AUDIT_2026-09-18.md` §5)

| Task | Status | Where |
|---|---|---|
| T-001 fileMode + ignores | done | `47b7624f` |
| T-002 governing docs tracked | done | `43c96dc9` |
| T-003 commit the tree | done: one bulk commit (not sliced); it silently reverted one committed fix (`AnalyzeCurrent` routing), restored in `f41a0240` | `43c96dc9`, `39c82d22` |
| T-004 push | done | `origin/remove-juce` |
| T-005 CI covers branch + UI config | done (green through `9ace67e8`; now blocked by billing) | `fbdda346`, `ade20e1f` |
| T-006 AppleDouble cleanup | done | `scripts/clean_appledouble.sh` |
| T-101/102/103 red tests | done | `70538689`, `326d900b`, `438895dc` |
| T-104 CI gate | done (`-LE 'windowed\|perf'`) | `0cf0e136` |
| T-201 screenshot matrix | done | RMJ-031 |
| T-202 standalone live dispatch | done | RMJ-032 |
| T-203 arm64 windowed GUI | done except `auval` (needs install) | RMJ-031 §arm64 |
| T-204 Logic session | **open: needs a person** | — |
| T-205 differential wrapper matrix | done | `ats_vst3_differential_test` |
| T-301 release matrix | done | RMJ-100 |
| T-302 ledger promotion | done where evidence exists | `f708d883` |
| T-303 docs consolidation | done | this file |
| T-304 final packet / clean-clone build | see `docs/juce-removal-baseline/RMJ-110-final-evidence-packet.md` | — |
| T-401 theme persistence | done (per-user preference) | `4cb22409` |
| T-402 copy/focus/keyboard parity | partial: preset names, toggle captions, §7.2 widgets, legacy `synDur` FULL / pivot Auto readouts; spec quoted-copy scan clean except §8.2 timeline readout | see open items |
| T-403 factory preset values | done | `ff52db63` |
| T-404 resize negotiation | done | `6ab26a68` |
| T-405 keyboard / output spectrum / meter | out of parity scope (ADR-0004), not started | — |

## Open items

- Logic session (T-204) and plugin validation (ARCH-HOST-001).
- `DSP-METER-001`: `tests/ui/spectrum_real_dispatch_test.cpp` does not exist.
- State corpus from a legacy JUCE binary (the current corpus is produced by
  the byte-compatible codec).
- Standalone: device selection and MIDI input (legacy holder had both).
- `assets/` and `slider/` (UI design assets, ~80 MB, untracked and
  unreferenced): decide whether they belong in the repository.
- T-402 remaining: tooltip text and keyboard navigation audit against
  `ats-mearcstapa-ui-spec.md`.
- **SESSION morph timeline (spec §8.2) is a scrub track, not the node editor.**
  Handles, double-click add/remove, curve/fill, BOUNCE/RESET, axis labels and
  the `"{t}s  {amount}"` drag readout are not implemented. This is feature
  work, not copy.
