# Phase D Validation Report

Date: 2026-08-22  
Repository revision: `a8b9357c224c987a5c42927e5d14765a6bcbd092`  
Build tree: `build-plugin`  
Configuration: `Release`, native AU/VST3/standalone enabled  
Visage revision: `20de59464243447816d142e9d38e9723d068f755`  
VST3 SDK revision: `05c4a9763f311a58fe3f34b68a199e5eea92e2b4`

## Repository-local gates

Passed:

- `cmake --build build-plugin -j4`
- `ctest --test-dir build-plugin --output-on-failure` — 84/84
- `ctest --test-dir build-review-ui --output-on-failure` — 84/84
- VST3 artifact test
- AU artifact test
- AU editor view probe
- standalone smoke executable
- `git diff --check`

The native build compiled and linked the VST3, AU, standalone, artifact-test,
and editor-probe targets. The AU artifact test exercises class-info state
round-trip, 81 parameters, automation, MIDI, format negotiation, rendering,
and lifecycle.

## External/native gates

`auval -v aufx Atr1 AtPl -o` passed component registration and identity, then
failed during cold component open with `OpenAComponent: result: -1` in the
current runtime. The x86_64 retry could not find the component. This is an
environment/runtime limitation; the repository AU artifact and editor probes
pass.

No `pluginval`, Steinberg validator executable, or DAW executable was found in
the current environment. No legacy pinned artifact or second legacy checkout is
present in the workspace, so the legacy-vs-JUCE-less differential matrix cannot
be truthfully executed here.

The screenshot matrix and `tests/host/run_macos_app_smoke.sh` were not marked
as passed because they require a window-capable GUI session and the external
host artifacts above.

## Phase D status

Repository-local implementation and native wrapper evidence: **passed**.  
External legacy differential, DAW/pluginval stress, and complete screenshot
evidence: **blocked by unavailable external artifacts/runtime**.
