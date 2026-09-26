# Modern ATSH Engine — PRD Summary

**Sources:**  
- Main: [docs/MODERN_ATSH_PRD.md](file:///Users/caseywescott/Documents/Projects/ATS-PLUGIN/docs/MODERN_ATSH_PRD.md)  
- Worktree (adds link to spectral envelope doc): [spectral-envelope/docs/MODERN_ATSH_PRD.md](file:///Users/caseywescott/Documents/Projects/ATS-PLUGIN/.worktrees/spectral-envelope/docs/MODERN_ATSH_PRD.md)

Engine content is the **same product intent** in both trees; the worktree ties this PRD to [[Spectral Envelope - PRD Summary]].

## One sentence

A **versioned analysis / morph / synthesis engine** that keeps **classic ATS** reproducible while adding **modern tracking, orphan residual, transients, and correspondence-first morphing** — all **off the audio thread** for heavy work.

## Design corrections (remember these)

1. **Orphan residual** — do not force all residual onto partials; store **per-partial noise** + **orphan band** energy.
2. **Trackers** — **classic ATS tracker** stays for parity; **assignment-style tracker** (e.g. Neri/Depalle-style) is an **option** until benchmarks say otherwise.
3. **Transients** — **preserve first** (onsets, short buffers, break/reset partials, crossfades) before a full S+T+N lane.
4. **Morphing** — **correspondence maps** and labels are first-class, not an afterthought.
5. **Neural / DDSP** — optional **assist** later, not the core MVP representation.

## Analysis modes (named)

| Mode | Role |
| --- | --- |
| **Classic ATS** | STFT peaks, SMR, recursive tracking, gaps/prune, residual, Bark noise |
| **Modern ATS** | Classic + better peaks, assignment tracker option, onsets, orphans, diagnostics |
| **Experimental** | Reassignment / BWE / learned helpers — **never** default for compatibility workflows |

Every run records: mode, params, engine version, source hash, SR, channels, warnings.

## Data model (headline fields)

- **Header:** version, mode, SR, hop/window, metadata, param hash  
- **Frames:** time, active partial ids, transient markers, residual bands  
- **Partials:** id, label, amp/freq/phase, SMR, confidence, per-frame noise  
- **Orphan residual:** band energy not tied to a partial  
- **Transients:** onset, window, gain, optional buffer/grain metadata  
- **Morph:** source ids, correspondence, envelopes, unmatched policies  

## Synthesis core (RT)

Oscillator bank + noise-modulated partials + **orphan band noise** + **transient buffers**; smoothed automation; **no** analysis/alloc/locks/I/O on audio thread.

## QA bar (acceptance themes)

- Classic golden tests vs ATSA/Qatsh within tolerance  
- Morph + plugin state **reproducible**  
- Modern tracker **matches or improves** on crossing-partial fixtures without wrecking monophonic sustained material  
- Transient mode **sharpens** plucked/percussive fixtures without clicks  
- Stress tests stay RT-safe  

## Current release-candidate snapshot

**Updated:** 2026-05-20  
**Worktree:** `/Users/caseywescott/Documents/Projects/ATS-PLUGIN/.worktrees/improvements/`  
**Baseline:** Modern ATSH task ledger is complete through Commit 76.

- Verification target: `ctest --test-dir build --output-on-failure` passes 14/14 tests.
- Real-audio target: `tests/run_external_acceptance.py` passes 54/54 checks across Casey's first-party plucked guitar, crotale, vocal chop, and vowel phrase fixtures.
- Plugin target: `cmake --build build_plugin --target AtsResynth` builds; current JUCE `ranlib` no-symbol warnings are expected for ARA/LV2 translation units.
- Modern transient handoff: source buffers protect 180 ms after detected onsets; captured buffers are capped at 250 ms.
- Plugin UI: the Session tab now shows a dedicated "Analysis engine for next Analyze" card with explicit Classic ATS, Modern ATS, and Experimental mode copy.
- Quality gates now include source-normalized RMS and spectral ceilings, residual highpass shape, transient sharpness, decay-envelope preservation, and timing/phase stability.
- Current source NRMS ceilings: plucked guitar `0.085`, crotale `0.13`, vocal chop `0.001`, vowel phrase `0.14`.

Stopping point: this is a sensible release-readiness baseline for the current fixture set. The next larger campaign would be expanding the sample corpus rather than closing unfinished tasks in the Modern ATSH plan.

## Risks called out in PRD

Licensing, **default changes** vs legacy user expectations, transient **state size**, tracker **CPU** at dense frames, morph **UI scope**, **stereo** analysis strategy.

## Related

- [[Plugin (VST) - PRD Summary]]
- [[Spectral Envelope - PRD Summary]]
- [[Worktrees and canonical paths]]
