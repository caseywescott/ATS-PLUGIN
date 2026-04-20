# ADR 0001: Effect-first MVP and plugin formats

## Status

Accepted (Phase 0)

## Context

The PRD targets a DAW plugin with ATS analysis and resynthesis. Hosts differ in supported formats (VST3, AU, CLAP).

## Decision

1. **MVP plugin category:** **Audio effect** (stereo or mono I/O TBD in implementation) with **offline-capable analysis** on imported or captured buffers. **Instrument / MIDI** playback of frozen models is explicitly **out of scope for MVP** and may be revisited in Phase 4.
2. **First shipping binary format:** **VST3** on macOS and Windows. **AU** (Logic) and **CLAP** are deferred unless product requirements change.
3. **Live, low-latency analysis** of arbitrary live input is **not guaranteed** in MVP; ring-buffer capture with large latency may be phased later.

## Consequences

- Simpler bus layout and parameter model than a full MIDI instrument.
- Logic users may need a wrapper host or converter until AU is implemented.
