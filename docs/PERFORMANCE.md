# Performance budgets and limitations (Phase 5)

This document states **informal engineering guidance** for **ATS Resynth** (JUCE + Qatsh ATSA). It is not a guarantee of behaviour on every host CPU, buffer size, or sample rate.

## Reference hardware (informal)

Budgets below assume something in the class of:

- **Apple Silicon** (M1 or newer) or **recent Intel quad-core** laptop/desktop  
- **16 GB RAM**  
- Host at **48 kHz**, **256 samples** per process block (typical DAW default)

Adjust expectations down for older CPUs, higher sample rates, or very small buffers.

## Analysis (offline / worker thread)

- Analysis (`tracker`) is **CPU-heavy** and runs **off the audio thread**. The UI should remain responsive; cancel is cooperative.
- **Live analysis of arbitrary live input** at **low latency** is **not** a supported goal for this MVP: analysis is oriented toward **imported WAV** (or future scoped capture), not continuous real-time tracking in an insert under 10 ms constraints.
- **Duration:** longer files scale roughly with audio length and analysis settings (window, hop, partial count). Very long sources can take minutes and large RAM.

## Model size and memory (in-RAM model)

The ATS model holds dense **double** arrays (amplitude, frequency, optional phase, residual-related data). Order-of-magnitude:

- Per active partial × frame, expect **on the order of tens of bytes** minimum for the core partial tracks (multiple `double**` grids), plus overhead and optional bands.
- A **rough** planning cap for comfortable editing on 16 GB machines: aim for **well under ~1–2 GB** working set for the model plus UI and host—not a hard limit in code.

The plug-in UI warns when **`partials × frames` > 2,000,000** (see `BUILDING.md`). That is a **soft** guardrail: large models may still load, but **analysis, resynthesis, MathExpr, and memory** become painful.

## Playback path (audio callback)

- The real-time path **plays a pre-rendered stereo buffer** after worker-side `do_synthesis`. It avoids calling ATSA on the audio thread.
- **Reported latency:** there is **no extra plug-in processing delay** beyond what the host assigns for this effect path; see `BUILDING.md` (Phase 4 MIDI) for MIDI transpose behaviour.

## Resynthesis (`do_synthesis` on worker)

- Triggered when synthesis parameters change (and after analysis/import). Cost grows with **model size** and **output length** implied by time window / stretch parameters.
- **256 samples @ 48 kHz** refers to **host block size**, not an internal ATSA block: it affects how often the host calls `processBlock`, not the size of the synthesis FFT.

## Recommended planning table (48 kHz, reference class CPU)

| Scenario | Guidance |
|----------|----------|
| **Comfortable analysis** | Prefer sources **≤ ~2–3 minutes** at 48 kHz for interactive iteration unless you accept long waits; use analysis presets that limit partial count where possible. |
| **Comfortable in-session edits** | Prefer **`partials × frames`** in the **low millions** or below for snappy resynth; above the UI soft cap, expect **multi-second** re-renders. |
| **MathExpr / batch transforms** | Same as model size; whole passes are **O(P × T × lines)** expression evaluations under a lock—large models can **freeze the UI** for seconds unless cancelled. |

## Live input stance (explicit)

- **Do not rely** on this plug-in for **continuous low-latency spectral analysis** of a live input bus in the same sense as a pitch tracker or gate.
- Future work (ring buffer, latency tradeoffs) would need a separate ADR and UX.

## See also

- `BUILDING.md` — build options, soft limit, formats.  
- `docs/DETERMINISM.md` — offline CLI determinism.  
- PRD §7, §11.3–11.4.
