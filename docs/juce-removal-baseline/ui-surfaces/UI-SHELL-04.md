# UI-SHELL-04 — Telemetry renderer (activity dots)

Status: implemented

Replaces the `VoiceDots`-derived activity LED pair in `PluginEditor.cpp`.
Two filled circles indicate whether analysis and render threads are active.

## Ownership

`TelemetrySnapshot::analysisRunning` and `renderRunning` are the authority.
`VisageEditor::setTelemetry(snapshot)` is the update path; `ProbeFrame`
stores the snapshot and re-draws.  No widget polls or re-samples the state.

## Geometry (§10.2 right cluster, §8.4)

| Dot | Purpose | Diameter | Label width |
|---|---|---|---|
| ANL dot | Analysis worker active | 8 px circle | ~30 px |
| RND dot | Render worker active | 8 px circle | ~30 px |

The dot pair occupies a 92 px slot in the right cluster, vertically centered
on the top bar.  Active state renders in `tokens.accent`; idle in
`tokens.secondaryText`.

## Interaction

No mouse interaction.  The dots are indicators only; they do not emit actions
or change state.

## State rules

| Snapshot field | Dot color |
|---|---|
| `analysisRunning == true` | `tokens.accent` (ANL) |
| `analysisRunning == false` | `tokens.secondaryText` (ANL) |
| `renderRunning == true` | `tokens.accent` (RND) |
| `renderRunning == false` | `tokens.secondaryText` (RND) |

Evidence:

```text
editor_shell_test: confirms setActivity / state.analysisRunning wiring.
real_dispatch_harness_test: setTelemetry drives the frame; no dedicated
dot-render test (offscreen paint).
```

Deviation: The `SourceInventory` meter described in `UI-TELEMETRY-ENGINE` is
deferred (no live meter widget in the current source); see D-OUTPUT-METER.
