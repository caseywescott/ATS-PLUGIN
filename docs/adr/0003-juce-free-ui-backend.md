# ADR 0003: JUCE-free UI backend selection gate

## Status

Accepted — Visage is pinned; full parity remains a follow-on implementation.

## Context

The ATS removal contract requires a real native dispatcher, popup and focus
ownership, text entry, dialogs, embedding, resize, and logical/device scaling.
The ATS editor still contains the legacy JUCE implementation, but the
replacement path now has a pinned JUCE-free backend and a native embedding
probe. Selecting a backend by appearance or API familiarity would make the UI
parity claim unverifiable.

## Decision

Visage commit `20de59464243447816d142e9d38e9723d068f755` is the selected
backend. Its revision is pinned by `ATS_VISAGE_VERSION` in CMake, and the
native lifecycle proof is recorded in
`docs/juce-removal-baseline/RMJ-010-visage-live-probe-report.md`. RMJ-020
headless primitives and their independent tests remain valid supplemental
coverage; real UI work must continue through the live native editor seam.

## Consequences

The backend gate is clear, but RMJ-080, RMJ-090, and RMJ-100 remain open until
the full widget set, popup/dialog/text matrix, host wrappers, and release
evidence pass. The probe is not a claim that JUCE behavior has already been
fully reproduced.
