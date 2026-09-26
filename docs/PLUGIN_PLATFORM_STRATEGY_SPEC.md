# Reusable JUCE-Free Plugin Platform — Portfolio Specification

Status: proposed architecture and implementation backlog; not implemented by this document.  
Updated: 2026-08-31. Supersedes the ATS-only proposal dated 2026-08-30.  
Scope: 11 identified plugin products in nine codebases, plus reusable foundations for future products.
Three of the eleven — ATS Resynth, CompressedFeedback, CFSynth — are already JUCE-free and are the
platform's extraction consumers, not migrations; the JUCE-removal backlog is eight products (section 11).  
Operating context: single maintainer, GitHub source and CI, one hosted server (section 18).  
Revision: section 19 adds the platform implementation plan (milestones M0–M5, package build
order, per-package definition of done); section 14 converted from open questions to decisions
of record (D-01–D-17); section 18 right-sizes the model for solo operation. Twelve
future-product robustness requirements in section 15 (FP-01–FP-12), the risk register in
section 16 and the governance/traceability rules in section 17.

**Contents.** [1 Decision](#1-decision-in-brief) · [2 Evidence](#2-evidence-and-limits) ·
[3 Product requirements](#3-requirements-exposed-by-the-portfolio) ·
[4 Scope](#4-scope-and-boundaries) · [5 Architecture](#5-architecture-and-ownership) ·
[6 Module catalogue](#6-module-catalogue-replacing-the-work-juce-was-doing) ·
[7 Critical contracts](#7-critical-contracts) · [8 Reuse and backend](#8-reuse-plan-and-backend-selection) ·
[9 Product contract](#9-product-contract-instead-of-a-bespoke-migration-spec) ·
[10 Repository model](#10-repository-and-maintenance-model) ·
[11 Backlog and gates](#11-delivery-backlog-and-exit-gates) ·
[12 Validation matrix](#12-reusable-validation-matrix) ·
[13 Cost and licensing](#13-cost-licensing-and-industry-context) ·
[14 Decisions of record](#14-decisions-of-record) ·
[15 FP-01–FP-12](#15-future-product-robustness-requirements) ·
[16 Risk register](#16-risk-register-kill-criteria-and-review-triggers) ·
[17 Governance](#17-governance-traceability-and-document-control) ·
[18 Solo operating model](#18-single-maintainer-operating-model) ·
[19 Implementation plan](#19-platform-implementation-plan)

**Building the framework?** Start at [section 19](#19-platform-implementation-plan); it
gives the milestones, the package build order and the acceptance test for each step, and
cites the rest of this document for the contracts each step must satisfy.

## 1. Decision in brief

Build a reusable internal **audio-plugin platform**, not a source-compatible clone
of all JUCE. Own product-facing contracts and shared behavior; use existing
libraries for plugin formats, rendering, codecs, and operating-system integration.
The platform must support creating and evolving new products as well as migrating
existing ones. Section 15 makes that requirement concrete through executable
contracts, maintained starters, compatibility policies and measurable release gates.
Four of those requirements exist because a plugin is not a standalone program: it
is loaded into a foreign process alongside other plugins (FP-09), inherits a
floating-point environment it does not control (FP-10), consumes files and network
data it did not author (FP-11), and is operated by users with assistive technology
and non-English locales (FP-12).
The destination is JUCE-free production products, not a permanent mixture of JUCE
and non-JUCE products. Existing JUCE builds can remain temporary reference oracles
under their applicable licenses while replacements are validated.

The starting point is already present in this portfolio:

- CF's `KitCore`, `KitClap`, and `KitUi` serve CompressedFeedback and CFSynth.
- MIDI Collab has another neutral core, CLAP/wrapper integration, Visage UI,
  MIDI-file codecs, scheduling, native dialogs, and network services.
- ATS has useful parameter, state, file, job, publication, and editor-contract
  patterns, plus native VST3/AUv2/standalone adapters.
- Mirage separates its emulation engine and specialist file formats from its
  JUCE processors/editors.

Consolidate these into one versioned platform. Do not extract the whole ATS
`HostAdapter`, copy CF's whole DSP library into every plugin, or start a fourth
independent replacement stack. Existing code is a **reuse candidate**, not
automatically a production-quality framework implementation.

Proposed default: neutral C++ core, **CLAP adapter + clap-wrapper for
VST3/AUv2/standalone**, and a **Visage-based UI kit**. Keep ATS's native adapters
until equivalence is demonstrated. Use iPlug2 as a bounded alternative evaluation
if this stack fails required coverage, not another simultaneous portfolio rewrite.
CLAP is a plugin API, not a replacement for JUCE's UI, state, files, devices or DSP.

This document specifies and audits; it does not by itself migrate plugins, change
dependencies, approve sound changes, or declare any release complete. It is written to
be implementable: §5.1 and §5.2 fix the day-one engineering conventions, §7.9 and FP-02
make "preserve the sound" measurable, §10.1 gives the repository shape, and §11.1 orders
the immediately actionable work, §14 records the decisions those sections depend on, and
section 18 right-sizes the whole model for a single maintainer, and **section 19 is the
build order** — milestones M0–M5, the package dependency graph and an acceptance test per
step. Work starts at §19.3 (M0), whose prerequisites are classifying which products
actually shipped (§14.1) and capturing the compatibility baselines and oracles that expire
(§11.1, R-13).

## 2. Evidence and limits

Read the [portfolio audit](PLUGIN_PLATFORM_PORTFOLIO_AUDIT.md) for source anchors
and product gaps. The [generated inventory](PLUGIN_PLATFORM_DEPENDENCY_INVENTORY.md)
and [machine-readable inventory](PLUGIN_PLATFORM_DEPENDENCY_INVENTORY.json) record
qualified JUCE symbols, module mentions, source locations, working-source hashes,
and Git revisions. Regenerate with:

```sh
python3 scripts/audit_juce_portfolio.py --audit-date YYYY-MM-DD \
  --json-output docs/PLUGIN_PLATFORM_DEPENDENCY_INVENTORY.json \
  --markdown-output docs/PLUGIN_PLATFORM_DEPENDENCY_INVENTORY.md
```

The audit scanned 831 first-party source files in configured scopes and inspected
build definitions and selected implementations. It is a lexical inventory plus
architecture review, **not** a compiled reachability proof, exhaustive behavioral
specification, DAW test, or licensing clearance. Tests, generated files, vendors,
inactive code, aliases, and inherited behavior require separate build-level review.
MIDI's scope includes legacy and replacement source. No sibling-project tests
were executed during this audit.

Existing migration documents/post-mortems are historical evidence, not instructions
to execute their tasks, honor stale stop points, or accept completion claims.
Current source takes precedence when describing implementation. Later capture the
release baseline from the actual binaries intended for compatibility; a dirty
working tree is not automatically that baseline.

## 3. Requirements exposed by the portfolio

| Product(s) | Requirements beyond the common plugin shell |
|---|---|
| CompressedFeedback | Sidechain, feedback DSP, FFT/analysis, oversampling, modulation and visualizers |
| CFSynth | Polyphony, voice control, modulation matrix, keyboard, interactive/animated UI |
| MIR-8 | Emulation engine, sampling input, audio import, captured sample bytes, disk/preset/calibration formats, sequencer, MIDI input/output/thru, custom editing and undo |
| MIR-8 FX | Separate effect identity and bus/state contract sharing Mirage engine code; generic-editor support |
| Avedis E12G / GraphicEQ | Mono/stereo EQ, 2x/4x IIR oversampling, nonlinear transformer, latency-aligned bypass and metering |
| HeartSynth | JUCE voice/sound inheritance, ADSR/smoothing, FFT/table worker, custom 3D OpenGL renderer, 2D fallback, modulation UI |
| HydroSynth | Engine coupled to APVTS, custom voices, ADSR, seeded randomness, modulation, separate parameter/editor-preset state |
| MIDI Network Plugin | MIDI-only topology, input/output scheduling, host musical time, piano roll/CC/event editing, SMF import/export, clipboard, networking/TLS/account lifecycle |
| PunchGate | Optional sidechain bus, lookahead, 2x IIR oversampling, changing latency, channel linking, async host notification |
| SNDWARP | Audio/custom-window files, resampling, live input/freeze, smoothing, source lifetime and missing-asset restore |
| ATS Resynth | Analysis/render workers, cancellation, large spectral models, multiple editing modes, specialized binary state/assets, cached visualization |

These are capability profiles, not eleven independent framework specifications.
Some algorithms are already neutral; others still take JUCE types. Removing
includes is insufficient where JUCE supplies runtime behavior.

## 4. Scope and boundaries

### Required destination

1. One shared implementation of host lifecycle, parameters, state transport,
   editor binding, jobs/publication, packaging, and common tests.
2. Optional packages for DSP primitives, voices, assets, timeline editing,
   networking, GPU surfaces, and standalone devices.
3. No JUCE source, headers, modules, generated wrappers, or JUCE-dependent runtime
   libraries in production artifacts; test-oracle dependencies separately declared.
4. Preserve sound, automation, identity, state, and functional UI unless a
   deliberate deviation is approved and recorded.
5. Every exercised JUCE capability has an owner, implementation, compatibility
   test, and supported-platform status before removal.
6. Future products start from maintained templates and inherit the lifecycle,
   sound-compatibility, performance, diagnostics and upgrade contracts in section 15.
7. Two products built against different platform versions coexist in one host
   process without symbol, runtime, resource or teardown interference (FP-09).
8. Untrusted input (states, presets, assets, network frames) cannot corrupt memory,
   escape declared directories or execute code (FP-11); shipped UIs meet the declared
   accessibility and locale baseline (FP-12).

### Not required

- Reimplement all JUCE, emulate its namespace, or preserve its class hierarchy.
- Share Mirage's emulation, Hydro's synthesis, CF's feedback algorithms, ATS's
  analysis, or MIDI's collaboration protocol as generic platform behavior.
- Build a universal UI designer or generate arbitrary product artwork.
- Rewrite pure DSP, mandate one voice-stealing algorithm, or introduce a scripting
  language, Rust rewrite, universal DSP graph, or plugin host.
- Add AAX, AUv3, Linux, mobile, web, MPE, MIDI 2.0, or new codecs to every product
  merely because upstream supports them. These remain explicit extensions.

## 5. Architecture and ownership

Use a C++17-compatible public core initially, because CF and MIDI use C++17;
C++20 products can consume it. Raise the baseline only through a tested toolchain
decision. Ship source/static libraries with each plugin, not a globally installed
shared runtime that can invalidate older products. Because several such copies will
be loaded into one host process, the platform is designed for in-process coexistence
from the start: hidden default symbol visibility, version-tagged namespaces and no
process-global mutable state (FP-09). Retrofitting this after products ship is a
binary-compatibility break, not a refactor.

| Layer | Owns | Must not own |
|---|---|---|
| Product engine | DSP, voice/sample/model semantics, deterministic render behavior | Host SDK callbacks, windows, file/network access in processing |
| Product controller/document | Product actions, edits, schemas, asset meaning, job requests | Format streams or widget lifetime |
| Platform services | Parameters, block/event dispatch, codecs, transactions, jobs, publication, controls | Product parameter lists or algorithms |
| Format adapters | Host callbacks, events, buses, identity mapping, notifications, embedding | Independent DSP, alternate serializers or product edits |
| UI backend + product pages | Native events/rendering + product layout/visuals | Mutating audio-owned objects or calling host SDKs |
| OS/standalone services | Devices, dialogs, paths, clipboard, sockets/TLS | Implicit dependency of every DSP target |

The host adapter delivers audio/events to the engine. The editor sends typed
commands to the controller. Workers prepare immutable resources for safe audio
adoption and editor publication. Host and UI are separate clients of shared
contracts; the editor is not downstream of the audio callback.

There is one composition root per instance, not one giant universal runtime class.
A small FX instance must not allocate a document editor, network session, worker
pool, or sample database. Services are explicit and injectable. Only backend and
adapter targets include their SDK types. Product DSP compiles and renders
headlessly without any format/UI/OS SDK.

### 5.1 Language and API conventions

These are day-one decisions that cost nothing to make now and are expensive to
harmonize across nineteen packages later. Each is a proposed default to ratify in P0.

| Area | Convention |
|---|---|
| Errors | No exceptions in audio-domain or adapter-boundary code, and none across a native plugin ABI. Fallible operations return an explicit result type carrying either a value or a typed error enum plus context; C++17 means we own a small `Result<T, E>`, not `std::expected`. Errors are values, and ignoring one is a compile warning promoted to an error |
| Exceptions elsewhere | Permitted in worker and tooling code where the boundary catches them and converts to a result. Every catch site is at a declared boundary, never mid-algorithm |
| Ownership | Raw pointer and reference mean non-owning and non-null-unless-documented; `unique_ptr` means sole ownership; `shared_ptr` appears only where the publication protocol (§7.6) requires it and never as a default. Any borrowed pointer's lifetime is stated in the API contract (FP-01) |
| Views | Own a `Span<T>` and a UTF-8 `StringView` for the C++17 baseline; adopt the standard types when the baseline moves. Views never outlive the call unless documented |
| Strings | UTF-8 `std::string` internally; conversion to platform-native encodings happens only in OS/UI adapters. No custom string class |
| Identifiers | Strongly typed IDs (distinct types for parameter handle, parameter logical ID, voice ID, revision, asset ID) so they cannot be swapped at a call site. Integers are not identities |
| RTTI and virtuals | Virtual dispatch is fine at service seams and prohibited in per-sample inner loops. No `dynamic_cast` on audio |
| Allocation | Every allocating API says so in its contract. Audio-domain code allocates never (FP-01); worker code allocates against declared caps (FP-04) |
| Assertions | Debug assertions carry a category (domain, lifecycle, ownership, bounds, numeric). Release builds compile out the message formatting, not the check, for cheap invariants |
| Logging | Never on audio. Logging is a service, not a global; it is injectable so tests can capture it; every message is redaction-reviewed (FP-07) |
| Singletons | None. If something looks like it needs process scope, it is a lifetime question for §5.2 and FP-09, not a static |
| Namespaces and targets | One root namespace with a version-tagged inline namespace inside it (FP-09); one CMake target per package with a matching alias, and a package prefix on every public macro. Names are chosen once in P0 and are not renamed after the first release |
| Versioning | Platform releases carry three independent versions — API, state schema, DSP behavior (FP-02) — plus a build revision. A single marketing version is not one of them |

### 5.2 Thread and pool ownership

The platform owns few threads and names all of them. This section resolves a conflict
that would otherwise be discovered at scale: FP-09 forbids process-global mutable state,
while FP-04 forbids starting a worker pool per instance or per scan. With static linking
both constraints bind at once — each loaded copy of the platform has its own statics, and
a session with forty instances must not produce forty pools.

Proposed model, to be ratified in P0:

- **Per instance:** the audio domain (host-owned thread, we create nothing), a bounded
  lock-free command and telemetry queue pair, and the instance's own state. No threads.
- **Per platform copy:** one lazily created, reference-counted worker pool and one timer
  source, shared by every instance of every product that links that copy. Created on the
  first *activation* that requests background work, never during scanning or construction,
  and torn down when the last user releases it.
- **Pool sizing:** a small cap derived from hardware concurrency with a declared ceiling,
  because several copies may coexist. A copy's pool is sized for its own instances, not
  the machine.
- **UI dispatch:** the host's or the OS's main-thread mechanism, wrapped, never our own
  thread.
- Every thread carries a name identifying the product and the copy, which is what makes a
  crash report from a user's DAW readable.

Products that need an isolated pool — a heavy analysis engine that must not starve small
FX instances sharing the same copy — declare it, and fairness and cancellation follow
FP-04. The alternative of a truly process-wide pool shared across copies is rejected: it
requires cross-version ABI stability and shared global state, both of which FP-09 forbids.

## 6. Module catalogue: replacing the work JUCE was doing

Codes identify primary owners in the generated inventory. Some symbols span
modules: APVTS needs PARAM + STATE + UI; MIDI keyboard needs MIDI + UI; `Random`
is a BASE utility with a DSP compatibility requirement.

| ID / package | JUCE responsibilities covered | Required implementation and consumers |
|---|---|---|
| BASE / foundation | String, containers, IDs, listeners, helpers, random, assertions | STL behind small APIs; UTF-8 conversions; stable IDs; deterministic RNG profiles; helper edge-case tests. All products. |
| RT / realtime | AudioBuffer, AbstractFifo, vector operations, denormal guards | Non-owning views separate from storage; bounded queues; scratch arenas; safe publication/reclamation; copy/gain/mix/clear/meter operations. |
| HOST / formats | AudioProcessor, AudioPlayHead, channel sets, wrappers | Multi-bus/multi-event-port engine contract; lifecycle; host time; bypass/latency/tail; identity; editor attachment. |
| PARAM / parameters | APVTS store, ranges, ParameterID, attachments | One schema; typed/dense access; per-format frozen IDs; flags/units/groups; exact conversion; timestamped events; gestures. Zero parameters supported. |
| STATE / serialization | ValueTree, var/JSON, XML, MemoryBlock | Versioned document envelope, parameter serialization, legacy readers, bounded codecs, transactional apply. Not an arbitrary ValueTree clone. |
| EDIT / editing | UndoManager, observable edit models | Typed commands, grouped gestures, bounded history, revisions, dirty state, undo/redo, immutable views. Mirage/ATS/MIDI and preset workflows. |
| JOBS / scheduling | Threads, timers, AsyncUpdater, MessageManager, WaitableEvent, locks | UI dispatch; cancellable workers; generations; lifetime-safe callbacks; coalesced RT notifications. Heavy work never in process. |
| MIDI / events and files | MidiMessage/Buffer/Sequence/File, keyboard state/collector | Variable-length transport, notes/CC/pressure/program/bend, clock/thru/output, timestamp domains, SMF, local keyboard and panic. |
| VOICE / voice services | Synthesiser, SynthesiserVoice/Sound | Optional allocator/router/sustain/lifecycle with policy-based stealing, legato/retrigger and segmentation. Heart requires migration; custom engines need not adopt it. |
| DSP / primitives | ADSR, SmoothedValue, FFT/windows, oversampling, Decibels | Tested normalization, timing, interpolation, filter-family, latency and precision profiles; shared buffer/control-rate/level/metering conventions (§7.9); optional modulation subpackage (FP-05). Not a universal synth engine. |
| FILES / streams | File and binary streams | Paths, bounded reads, atomic saves, app/preset directories, errors; no file I/O from audio. |
| ASSETS / audio resources | AudioFormatManager/Reader and decoding | Codec registry, metadata, resampling, immutable sample/window tables, capture handoff, missing-resource policy. Mirage/SNDWARP/ATS. |
| UI / editor toolkit | Component, controls, LookAndFeel, attachments, menus, lists/tables, viewport, drag/drop, focus | Behavior-rich widgets, embedding, layout, accessibility semantics and platform bridges, locale-safe text/number formatting, string catalogue, generic editor, preset browser, timeline primitives. Product pages retain identity. |
| DRAW / 2D graphics | Graphics/Path, fonts/glyphs, images, gradients/transforms | Visage rendering, image/font pipeline, DPI-correct geometry/text, cached plots/scopes. Rendering separate from hit testing. |
| GPU / specialized surfaces | OpenGLContext/Renderer/ShaderProgram, GL resources | Render-surface ownership, lifecycle/resources, snapshot input and fallback; Heart 3D and CF feedback effects need feasibility tests. |
| OS / application services | Desktop, clipboard, PropertiesFile, SystemStats; native platform plumbing | Paths/settings, clipboard, Unicode, dialogs, URL launch and windows; per-OS capability reporting. |
| NET / networking | StreamingSocket, InterprocessConnection, URL, related async services | Bounded transport/framing, production TLS, timeouts/reconnect/cancellation, redacted diagnostics. MIDI protocol stays product-owned. |
| DEVICES / standalone | audio_devices and audio_utils standalone services | Audio/MIDI selection/routing, rates/blocks, permissions, hot-plug, settings, MIDI-only operation. Separate from host processing. |
| SEC / trust and integrity | Implicit in JUCE's parsers, streams, sockets and credential handling | Untrusted-input parsing rules, path containment, size/recursion limits, integrity verification, OS-keystore credentials, redaction. Shared by STATE, ASSETS, FILES, NET (FP-11). |
| BUILD / delivery | juce_add_plugin, generated headers/BinaryData, SDK setup, resources, packaging | Manifest-driven targets; dependency lock by hash; symbol visibility and namespace versioning; fonts/shaders/resources; metadata; signing/notarization/installers; SBOM. |
| TEST / conformance | JUCE test scaffolding and implicit guarantees | Shared unit/oracle/RT/host/state/UI/codec/security/artifact/release harnesses. |

### JUCE module coverage, including transitive services

| JUCE module | Destination |
|---|---|
| juce_core | BASE, FILES, OS, NET, STATE, JOBS |
| juce_events | JOBS, UI/OS dispatch |
| juce_data_structures | STATE, EDIT, PARAM |
| juce_audio_basics | RT, MIDI, VOICE, DSP |
| juce_audio_processors / juce_audio_plugin_client | HOST, PARAM, STATE, BUILD |
| juce_audio_devices | DEVICES (often standalone even without direct source calls) |
| juce_audio_formats | ASSETS and codec/format extensions |
| juce_audio_utils | Split HOST/DEVICES/UI/MIDI utilities; no catch-all library |
| juce_dsp | DSP and RT |
| juce_graphics | DRAW and image/font/resource decoders |
| juce_gui_basics / juce_gui_extra | UI, OS, DRAW, JOBS; exercised features, not entire modules |
| juce_opengl | GPU |

No direct scoped module mentions established a need for JUCE video, OSC or
cryptography modules. This is not proof of transitive absence. MIDI's native
TLS work continues; JUCE was not necessarily supplying its TLS implementation.

## 7. Critical contracts

### 7.1 Audio, topology, timing and lifecycle

Process views of a declared bus collection, not one assumed stereo buffer.
Preserve mono/stereo layouts, disabled optional sidechains, synth input for
sampling, independent routing, and MIDI-only plugins with zero audio buses.
Declare permitted layouts and validate negotiation before activation.

Separate prepare, reset, process, deactivate and destruction. Preparation can
allocate; processing/reset-on-audio cannot. Handle actual/max block lengths,
zero-frame parameter flush, suspend/resume, offline processing and buffer aliasing.
Oversize blocks need a safe policy, not hidden resizing. Float32 is required;
advertise float64 only with implementation/tests or a declared conversion policy.

Events have stable order and sample offsets. Segmentation preserves note/parameter
order, equal-timestamp behavior and output offsets. Validate malformed events,
bound work, and report overflow. Overflow policies prioritize note-off/panic and
never silently leave held notes.

Transport includes validity flags, sample/beat/PPQ position, tempo, time signature,
play/record/loop state, loop limits and discontinuity. Missing fields are not
guessed as valid. Network/wall clocks, host samples and UI timestamps are distinct
types with tested conversion policies.

Latency/tail/bypass are capabilities. EQ and PunchGate require dynamic latency;
coalesce updates to the target format's required thread. Apply latency-changing
DSP configurations safely; keep host reporting, wet/dry alignment and bypass
consistent. Do not copy one JUCE-era callback-thread behavior into all adapters.
In particular, CLAP latency and port configuration stay fixed while activated;
changes require the host-coordinated reconfiguration contract in FP-01, not just
a notification on a different thread.

Hosts deviate from their own specifications. Keep a single **host-quirk registry**
instead of scattered inline conditionals: each entry records the host and version
range, the observed deviation, the evidence (session, log or validator output), the
narrowest workaround, the products affected, and a re-test trigger. Quirks are data
consulted by adapters, never permanent silent behavior differences in shared code.
A workaround with no reproduction is a bug report, not a registry entry; a registry
entry with no re-test date becomes permanent technical debt. Never gate a workaround
on the host string alone when a capability query can answer the question.

### 7.2 Parameters and released identity

Declare each parameter once: stable logical ID, dense internal handle, per-format
host ID, version/order metadata, name/group/unit, type/default/range/interval,
skew/symmetric-skew or custom mapping, choices, automation/read-only/bypass flags,
text parse/format and smoothing policy. Generated bindings share one gesture
begin/change/end path. Host/UI updates must not create notification loops.

Freeze released VST3 processor/controller UIDs, AU type/subtype/manufacturer,
existing CLAP IDs, bundle IDs, parameter IDs/order/version hints and state roots.
CF's string-to-FNV CLAP IDs are not a universal substitute for historical JUCE
VST3/AU IDs. A new CLAP ID does not make an old VST3 session recognize a plugin.
Current CMake descriptors are leads; extract identities from reference binaries.

Identity spaces differ in width and shape: VST3 component/controller IDs are 128-bit
class IDs and its parameter IDs are 32-bit; AU identity is three four-character codes
with 32-bit parameter IDs; CLAP plugin IDs are strings while its parameter IDs are
32-bit. A logical ID scheme must therefore survive lossy projection into each space
without collision, and the projection is itself frozen once released. Where a wrapper
derives host IDs from a CLAP ID by hashing, that derivation is only acceptable for
products with no shipped history; for released products, verify the wrapper accepts
explicit historical overrides for every identity and parameter ID, and treat the
absence of such an override mechanism as a P0 backend-selection finding (section 16,
R-01). Record every assigned identity in the portfolio registry (FP-03, section 17)
so two products can never claim the same four-character code or class ID.

Use typed handles on audio, not string lookup or schema walks. Separate base
values, temporary modulation and persistent model edits. Sample-accurate automation
may improve a previously block-quantized path; label improvement versus parity.

### 7.3 State, presets and assets

One state service supports host and preset workflows with explicit scopes: full
sessions, sound-only presets, editor preferences and ephemeral state need not
serialize identical fields. Product codecs define scope; every format uses the
same validation/apply transaction.

Decode to a temporary document, validate product/schema, sizes, finite values,
IDs, nesting and resource bounds, prepare resources, then publish an accepted
revision. Failure leaves working state intact. Support save/restore before UI
creation and during audio. Serialize a coherent published snapshot, not a mutable
graph behind an audio callback lock.

Share bounded codecs and legacy-reader registration, but keep historical schemas
product-owned. Required cases include:

- JUCE binary XML envelopes and parameter XML, absent-field defaults and old
  numeric/text conversion behavior.
- Mirage's OsPrograms, Wavesamples including captured bytes, Sequences, specialist
  formats and exact legacy binary-to-text encoding. Do not assume
  `MemoryBlock::toBase64Encoding` equals arbitrary standard base64 without fixtures.
- Heart's parameter wrapper and editor/camera/preset fields; Hydro's parameter
  tree and separate PROCESSOR_STATE.
- SNDWARP's source/custom-window paths and preset selection. Path references are
  not portable embedded assets; missing-file relink and optional future embedding
  need explicit migration policy.
- MIDI's document/legacy JSON and ATS's versioned binary state/spectral assets.

Generic JSON/XML libraries sit behind this layer; reject truncated, oversized or
malformed input. Saved state is untrusted input from an unknown author: FP-11 sets
the parsing, containment and fuzzing rules that every codec here must satisfy. CF's
parameter-only scanner/parser is not a full-portfolio codec. Test locale/Unicode and
unknown-field behavior. New readers accept supported old states; reverse compatibility
requires explicit old-format export.

Audio resources declare codecs/containers/bit depths, channel policy, resampling,
loop/metadata handling, memory limits, cancellation and lifetime. Workers decode;
audio adopts prepared resources. Preserve capture/live/freeze semantics and defer
destruction of replaced large buffers away from audio.

### 7.4 DSP and voices: preserve sound deliberately

Replacement primitives need behavior profiles, not matching class names:

- ADSR: stage timing, zero-time stages, retrigger, sustain changes, note-off
  mid-attack, reset and sample-rate changes.
- Smoothing: linear/multiplicative law, ramp rounding, initial value, retargeting
  and block splitting.
- FFT/windows: real packing, complex layout, inverse normalization, magnitudes,
  symmetric/periodic windows, scratch ownership and precision.
- Oversampling/resampling: factor, FIR/IIR family, phase/stopband/transition
  response, nonlinear feedback behavior, fractional/integer latency and reset.
- Randomness: seed width, sequence and conversion where sound/presets or
  deterministic renders depend on them.
- Voices: allocation/stealing, note identity/channel, repeated notes,
  velocity-zero note-on, sustain, bend/pressure, retrigger, tails and panic.

CF's 2x windowed-sinc FIR is not interchangeable with EQ's 2x/4x or PunchGate's
2x JUCE polyphase-IIR oversampling. Heart's custom voice-stealing override needs
a policy, not CF's voice behavior. Mirage and Hydro can retain their own engines.

Capture oracle vectors from authorized reference builds. Compare IDs/events/encodings
exactly; establish justified numeric/audio tolerances before choosing replacements.
Do not rebaseline changed sound just to pass tests. Algorithm changes need a
recorded product decision. The metrics, comparison classes and procedure that make
"within tolerance" an executable claim are defined in FP-02; the code-level conventions
these profiles are written against are in §7.9.

### 7.5 MIDI, keyboard, files and output

Use a bounded event header plus payload arena for variable-size MIDI/SysEx;
three bytes cannot cover complete transport. Preserve native note IDs internally
where supported. Each format declares dialect and translation/loss policy. MIDI
2.0/MPE can remain extensions without claiming legacy-engine support.

Shared behavior covers CC, channel/poly pressure, program change, pitch bend,
sustain, all-notes/all-sound-off, input/thru/generated output, timestamped local
keyboard injection and teardown panic. Mirage needs MIDI output and pressure/program
behavior; MIDI Collab needs zero-audio routing and musical scheduling. SysEx is a
transport capability to verify per format/product, not a claim that Mirage has
SysEx patch editing. MIDI Collab's current standalone source explicitly drops SysEx.

SMF import/export is optional. Validate tracks/time divisions, PPQ/SMPTE, tempo
maps, variable-length quantities, running status, meta/SysEx bounds, note pairing,
malformed input and round trips. Reuse MIDI's codec only for its proven subset;
unsupported cases must be explicit. Editing/clipboard models are not RT queues.

### 7.6 Jobs, publication and editing

Provide cancellation, generation/revision IDs, bounded/coalesced requests, progress
and typed results. Stale work cannot replace newer presets, samples, tables,
network sessions or spectral models. Destruction cancels/joins outside audio;
callbacks cannot retain dead editors.

Audio publishes bounded telemetry and adopts prepared snapshots. Reference counting
alone is not RT safety: atomic shared-pointer operations may lock and final release
may destroy large resources on audio. Use a tested publication/reclamation protocol.
Cache large spectra/timelines/geometry by revision instead of copying every UI tick.
RT queues cannot enqueue allocating closures.

Commands return accepted/rejected/cancelled/stale/unsupported results. A clear,
panic or preset button cannot bind to a default no-op. Undo groups gestures,
restores coherent revisions and has memory limits; session-restore versus undo
semantics remain product policy.

### 7.7 UI and GPU

Shared controls own behavior and drawing: drag law, snapping, double-click reset,
typed values, menus/choices, focus traversal, shortcuts, wheel policy, disabled
state, tooltips, scrolling, popup lifetime, drag/drop and accessibility roles/actions.
Generic parameter editors serve MIR-8 FX and diagnostics, not as a substitute
for complex product interfaces.

Factor repeated Heart/Hydro widgets and preset/modulation patterns into controls;
keep themes/layouts product-owned. Extract timeline selection, zoom/scroll transforms,
history and viewport primitives from MIDI/ATS where contracts overlap; retain
their different document schemas.

Embedding must handle attach/detach/reopen, resizing, HiDPI, nonuniform resize
where supported, host keyboard ownership, transient windows, multiple instances
and teardown. CF's post-mortem explains why direct handler tests and screenshots
missed dead controls. Require the **real pinned backend dispatcher** and embedded
host interaction tests, not just hand-constructed mouse events.

Visage is a 2D/UI implementation, not proof of JUCE parity. Verify Unicode/IME,
text metrics, clipboard/drag-drop and accessibility on each OS. Missing capabilities
remain backlog items, not automatic feature deletions. Accessibility and locale
behavior are contracts with their own gates, not styling preferences; see FP-12.
If the chosen backend exposes no platform accessibility bridge, that is a platform
work item with an owner and a target release, and shipping without it requires a
recorded deviation naming the affected users.

Prototype Heart's mesh/shaders/camera/interaction and 2D fallback: resource creation,
context/device loss, resize, render threading and multiple instances. Probe CF's
feedback/ping-pong effects too. Choose a supported render extension or narrowly
isolated native surface after evidence; do not create a general 3D engine or
silently replace Heart with a static image.

### 7.8 Networking, OS and standalone

NET is optional and absent from EQ builds. Reuse MIDI transport seams; keep its
wire messages/auth/rooms product-owned. Production needs real TLS, verified
hostname/trust, bounded messages, timeouts/reconnect limits, secure credentials
and redacted logs. Test/dev passthrough cannot be selected by release configuration.
Existing self-signed/trust-disabled behavior requires an explicit compatibility
and security decision; do not copy it as the shared default. FP-11 fixes the release
rule: no trust-disabled or plaintext transport may be reachable in a release build,
and that is enforced by construction rather than by a runtime flag.

OS services return typed errors and advertise capabilities. Share dialogs,
clipboard and settings paths, rather than per-product Cocoa/Win32 implementations.
Validate actual code, not filenames: MIDI's `native_file_dialog_stub.cpp` includes
a Windows implementation.

Standalone is an application with audio/MIDI routing, settings, permissions,
hot-plug, rate/block changes and clean shutdown. MIDI-only operation must not
need an audio device. Prefer the selected wrapper's device stack when adequate;
supplement behind DEVICES if needed. Opening a window is not device parity.

### 7.9 DSP engineering contract

§7.4 says which sound is correct and FP-10 defines the numeric environment. This
section defines how DSP code is written against the platform, so that eleven products
do not each invent their own buffer, control-rate, latency, level and metering
conventions. Every value called a *default* below is a proposed platform default that
each product either accepts or overrides explicitly in its DSP profile (§7.9.10);
none may be left implicit, because each one is audible.

#### 7.9.1 Buffer, aliasing and scratch contract

Audio is planar float32: an array of per-channel pointers plus a frame count. Channels
are never assumed contiguous or interleaved, and channel count changes only at prepare.

| Rule | Requirement |
|---|---|
| Alignment | Host-supplied buffers are assumed aligned only to `alignof(float)`. Vector code uses unaligned loads/stores or copies into platform scratch. Platform-owned scratch and arenas are 64-byte aligned and may use aligned access |
| Aliasing | Input and output channel pointers may be identical or may overlap. Every primitive declares `InPlaceSafe` or `RequiresDistinct`; the platform supplies a wrapper that stages a `RequiresDistinct` primitive through scratch. `__restrict` appears only where the declaration guarantees it |
| Scratch | Each primitive answers `scratchBytes(maxFrames, channels)` at prepare; the instance allocates one arena covering the worst-case chain, then sub-allocates by bump pointer during process. No allocation, growth or free on audio |
| Frame counts | `maxFrames` is fixed at prepare. Process may receive any count from 0 to `maxFrames`. Zero-frame calls are legal parameter flushes and must not advance audio state |
| Oversize blocks | The host adapter splits blocks larger than `maxFrames` into `maxFrames` chunks with correctly re-based event offsets. Splitting must be inaudible and, for T1 paths, bit-exact (FP-10) |
| Silence and constant-block hints | Hints only. A primitive that skips work on a silent input must still advance envelopes, LFOs, delay lines and counters exactly as if it had processed zeros. Conformance runs every fixture twice — hints forced on and forced off — and requires bit-exact equality |
| Buffer reuse | Never retain a host buffer pointer beyond the call. Never publish a pointer into scratch to another thread |
| Precision | Internal state may be float64 where declared; the block interface stays float32 unless the product declares a float64 path (§7.1) |

#### 7.9.2 Control rate, smoothing and coefficient updates

This is the architectural decision most likely to be made accidentally, so it is made
here. Processing proceeds in **control sub-blocks**: the engine splits each host block
first at event offsets, then at control-block boundaries, and never processes more than
`kControlBlock` frames without re-evaluating modulated coefficients.

- Default `kControlBlock` is 32 frames. It is a per-product profile field, fixed once
  released, because changing it changes modulation timing and therefore the sound.
- **Control-block boundaries are anchored to the instance's absolute sample counter, not
  to the start of the host block.** Without this anchoring, a host that delivers 512
  frames and a host that delivers two blocks of 256 produce different coefficient update
  timing, and the T1 block-size invariance test in FP-10 fails by design. Reset the
  counter only at prepare/reset, and define its behavior across transport locates.
- Per-sample update is permitted where a primitive declares it (typically audio-rate
  modulation targets); per-block update is permitted only for parameters declared
  non-modulatable. Both are profile fields, not implementation freedom.

Smoothing laws are declared per parameter, with the convention spelled out because the
same word means different things in different codebases:

| Law | Definition to implement | Use |
|---|---|---|
| Linear ramp | Target reached in `N = round(rampSeconds × sampleRate)` steps with the rounding rule recorded; increment `(target − current) / remainingSteps` | Gains, mix, pan |
| Multiplicative ramp | Same step count, geometric increment; requires a non-zero floor | Frequencies, ratios |
| One-pole exponential | `y += (target − y) × a`, `a = 1 − exp(−1 / (τ × sampleRate))`; the profile records whether τ means the 63.2 % time constant or a time-to-percentage figure | Detectors, meters, slow controls |

Migrating a JUCE-era smoother means capturing its step-count derivation and rounding
from the reference build, not assuming ours matches. Recorded rules:

- State restore, preset load and prepare set smoothers **directly to target** with no
  ramp; automation and UI gestures ramp. A product wanting a ramped preset change
  declares it.
- Expensive coefficient recomputation may be gated on a change test, but the epsilon and
  the gating are part of the behavior profile, since they determine whether a slow
  automation ramp updates coefficients continuously or in steps.
- Smoothed values are read once per control block and held; a primitive must not read a
  smoother at two different points in the same sub-block and get two values.
- Modulation combination order, clamping and mapping domain follow FP-05.

#### 7.9.3 Latency, alignment and delay composition

- Every stage reports integer latency in samples **at its own operating rate** plus an
  optional fractional remainder. The engine composes total base-rate latency as the sum
  of each stage's latency divided by its rate factor.
- Hosts receive integer sample latency only. If the composed total is fractional, the
  engine reports the ceiling and absorbs the remainder in an internal fractional delay
  whose interpolation order is declared. Silently rounding is prohibited: it detunes
  wet/dry cancellation and breaks null tests against the oracle.
- One platform `LatencyAlignedDelay` serves every dry, sidechain, detector and metering
  path. Products must not hand-roll compensation delays; a mismatch between wet and dry
  alignment is comb filtering, not a small error.
- Lookahead: the profile states explicitly whether lookahead delays the **main** path
  (detector sees the future) or the **detector** path, and whether the sidechain input is
  delayed to match. Both conventions exist in the portfolio; neither is assumed.
- Oversampled regions: a detector or sidechain that controls an oversampled path runs at
  a declared rate, and its latency participates in the same composition.
- Latency changes only across a prepare boundary (FP-01, CLAP). A product needing a
  runtime-variable algorithmic latency either declares its maximum and pads to it, or
  uses the host-coordinated reconfiguration contract.

#### 7.9.4 Discontinuity and click policy

Every transition that can produce a step discontinuity has a declared handler and a test.
No transition may be left to chance.

| Event | Required handling (default) |
|---|---|
| Parameter change | Smoothed per §7.9.2; non-smoothable switches crossfade over 10 ms |
| Preset/state restore | Full reset, then a declared fade-in (default 5 ms) if the previous output was non-silent |
| Bypass toggle | Equal-gain (linear) crossfade over 10 ms between the latency-aligned dry path and the wet path. Equal-power is wrong here: the two signals are correlated and equal-power produces a level bump |
| Sample-rate or block-size change | Full reset at prepare; no attempt to carry state across rates unless the profile declares a resampling of state |
| Transport locate, loop jump, seek | Declared per product: either continue tails or reset with a fade. Reverb and delay tails continuing across a locate is a choice, not an accident |
| Oversampling or topology switch | Reset the switched region and crossfade over the declared duration |
| Voice steal | Forced release ramp of at least 2 ms before reallocation (FP-05, §7.4) |
| Engine reconfiguration | Old configuration continues until the new one is prepared, then crossfade or hard switch at a zero crossing, as declared |

Conformance: a click-detection suite drives each transition and measures maximum
sample-to-sample delta and short-window high-frequency energy against the same signal
without the transition. Thresholds live in the product profile.

#### 7.9.5 Level, gain and mixing conventions

- 0 dBFS is ±1.0. Conversions are `linear = 10^(dB/20)` and `dB = 20·log10(linear)`, with
  a declared minus-infinity floor below which gain is exactly 0. The default floor is
  −100 dB, matching the JUCE-era convention that migrating products inherit; a product
  that used a different floor records it.
- Wet/dry mixing law is declared. Default is **equal-gain** (`out = dry·(1−m) + wet·m`)
  because for most effects the wet signal is a processed version of the dry and the two
  are correlated. Equal-power is reserved for genuinely uncorrelated mixes and must be
  declared, since switching laws silently rebalances every existing preset.
- Pan law is declared with its formula and centre attenuation. Default is equal-power
  sine/cosine, `θ = (pan + 1)·π/4`, `gainL = cos θ`, `gainR = sin θ`, giving −3.01 dB at
  centre. Mono-to-stereo placement and stereo balance are different controls and may use
  different laws; both are recorded.
- Channel linking declares its detector combination (max, sum, mean or RMS across
  channels), whether detection is pre- or post-gain, and its behavior when a channel is
  silent.
- Mid/side conversion declares its scaling: `M = (L+R)/2, S = (L−R)/2` with the inverse
  `L = M+S, R = M−S`, or the √2-normalised variant. Round-trip must be exact within
  float32 and is tested as such.
- Summation order is fixed and serial. Parallel or reordered reduction drops the path to
  T2 at best (FP-10).

#### 7.9.6 Metering and analysis conventions

Meters are telemetry, never part of the signal path, and are published under §7.6 rules.

- The audio side accumulates; the UI side reads and clears. A meter reads every processed
  sample into a running maximum or accumulator; it must never sample the buffer at UI
  frame rate, which makes readings block-size and frame-rate dependent.
- Declared meter types and their parameters: **sample peak**; **true peak** with a declared
  oversampling factor (default 4×, the minimum in ITU-R BS.1770); **RMS** with a declared
  window length and whether it is a sliding window or a one-pole detector; **loudness**
  only where a product needs it, with the declared BS.1770 K-weighting and gating.
- Ballistics are declared: attack and release time constants, decay law, peak-hold
  duration and reset behavior. Two products whose meters differ silently look like a bug
  to the user; the shared default belongs in the platform and overrides in the profile.
- Meter values cross to the UI as pre-converted, bounded scalars, not as buffers.
- Analysis surfaces (spectra, spectrograms) declare FFT size, window, hop, normalization
  and dB reference, so that magnitudes are comparable across sizes and across products.
  Normalization convention is stated explicitly for both forward and inverse transforms
  (§7.4), because an unstated 1/N is the single most common spectral porting bug.

#### 7.9.7 Filters and numerical robustness

- Coefficients are computed in float64 and stored at the declared state precision.
- Topology is declared per filter. Default for anything modulated is a ZDF/TPT state
  variable form, which stays stable under fast coefficient modulation. Classic biquad
  direct forms are permitted only with float64 state or above a declared `fc/fs`
  threshold (default: direct forms require `fc/fs ≥ 0.01`), because coefficient
  sensitivity and state quantization degrade badly at low ratios and high Q.
- All frequency inputs are clamped before coefficient computation, to a declared fraction
  of the sample rate (default `fc ≤ 0.49·fs`) and a declared low bound. Bilinear designs
  state their prewarp: `ω = 2·fs·tan(π·fc/fs)`.
- Q, resonance, slope and gain mappings are recorded as formulas in the profile, not left
  to the implementation, because they define the control feel and the sound.
- Debug builds assert coefficient finiteness and pole radius; release builds clamp per the
  declared policy without logging on audio (FP-01).
- Recursive state resets to exact zero (FP-10), and every filter declares whether reset
  clears state only or also recomputes coefficients.

#### 7.9.8 Oversampling, resampling and interpolation

Each oversampled region declares, as a unit: factor; filter family (windowed-sinc FIR or
polyphase IIR halfband); order or tap count; transition band; stopband attenuation;
passband ripple; phase response (linear or minimum); resulting latency; and reset
behavior. These travel together — "2× oversampling" alone is not a specification, and the
portfolio already contains three mutually incompatible versions of it (§7.4).

- Non-linear stages sit between the declared up- and down-sampling filters; nothing else
  in the region may bypass them.
- Sample-rate conversion for assets declares its algorithm, quality and whether it happens
  at load (worker) or at playback.
- Playback and delay-line interpolation order is declared — linear, cubic Hermite,
  Lagrange, or windowed sinc with table size and oversampling — because it is audible and
  because Mirage, SNDWARP and ATS each depend on a specific character.
- Wavetable and lookup-table oscillators declare table size, interpolation and
  band-limiting strategy, plus the exact table generation procedure, so tables can be
  regenerated bit-identically rather than shipped as opaque data.

#### 7.9.9 Voice and polyphony mechanics

Extending §7.4 and FP-05 with the mechanics that affect reproducibility:

- Voice buffer accumulation order is deterministic and fixed (voice index order), so
  polyphonic renders are T1-reproducible.
- Each voice carries a monotonic voice ID and its originating note identity; stealing,
  release and reallocation are recorded against those IDs in the event trace (FP-07).
- Release tail truncation policy is declared: minimum ramp length, maximum tail duration
  and the behavior when polyphony is exhausted.
- Per-voice random streams are seeded deterministically from the instance seed and voice
  ID (FP-10), never from a shared global generator whose sequence depends on voice timing.

#### 7.9.10 The DSP profile artifact

Every product ships `dsp.profile.json` alongside its manifest. It is versioned by the DSP
behavior version in FP-02, and a change to any field is a sound-affecting change:

```text
behaviorVersion            DSP behavior version this profile defines
controlBlockFrames         control-rate granularity, anchored to the sample counter
supportedRates             rates and any rate-dependent behavior
maxFrames                  block ceiling and oversize policy
determinismTier            T1 / T2 / T3 per render path (FP-10)
fpFlags                    compiler and floating-point settings the profile assumes
smoothing[]                per-parameter law, time, rounding rule and restore behavior
latency                    per-stage latency, composition, fractional policy, lookahead convention
transitions[]              per-event handler and crossfade/fade durations (§7.9.4)
levels                     dB floor, wet/dry law, pan law, linking, mid/side scaling
meters[]                   type, window, ballistics, hold, oversampling factor
analysis[]                 FFT size, window, hop, normalization, dB reference
filters[]                  topology, precision, clamps, prewarp, Q/gain mappings
oversampling[]             factor, family, order, ripple, stopband, phase, latency, reset
interpolation[]            per-consumer order and table parameters
voices                     allocation, stealing, ramps, tails, seeding
tolerances                 comparison class and thresholds per render path (FP-02)
```

The starters in FP-03 ship complete example profiles; the generator emits a profile
skeleton whose required fields fail the build until filled in. An unfilled profile field
is an undeclared sound decision, which is exactly what this platform exists to prevent.

## 8. Reuse plan and backend selection

| Existing asset | Direction | Hardening before shared adoption |
|---|---|---|
| CF Parameters/registry/gestures | Seed PARAM and tests | Legacy IDs per format; custom conversions; no dense-index persistence assumptions |
| CF PluginModel/KitClap | Seed HOST and CLAP adapter | Buses, MIDI output/payloads, richer time, explicit capabilities/results |
| CF buffer/MIDI helpers | Keep useful operations; separate RT storage | Buffer wrapping constructs a vector; event insertion can grow one; prove allocation-free paths |
| CF state/presets | Reuse fixtures/workflows | Bounded transaction codecs; retain existing readable formats |
| CF FFT/oversampling/voice code | Optional primitive candidates | Distinct sound/voice semantics require separate profiles |
| CF controls + Heart/Hydro widgets | Shared controls/bindings | Real-dispatch/lifecycle tests; behavior as well as appearance |
| MIDI replacement services | Seed MIDI/files/edit/network/OS packages | Separate product protocol; verify production paths, SysEx limits, trust policy, Windows evidence |
| ATS jobs/snapshots/actions/files | Seed JOBS/EDIT/FILES | Remove spectral assumptions/duplicate ownership; bound payload/reclamation |
| ATS native adapters | Compatibility bridge/reference | Avoid maintaining two default implementations of every format indefinitely |
| Mirage engine/formats | Retain product libraries | Adapt controller/state/editor rather than rewrite emulation |

CF pins CLAP 1.2.2 / clap-wrapper v0.11.0; MIDI records newer replacement pins;
ATS/CF and MIDI also differ in Visage revisions. Select one tested lock after
comparing patches, not by copying the oldest pin or upstream HEAD. Support local
overrides while recording the actual dependency content used in evidence.

### iPlug2 and alternatives

iPlug2 itself is free for closed-source projects under its zlib-like license;
it supplies plugin/app integration and IGraphics. There is no iPlug2 license fee
to budget; SDK terms, dependencies, signing, support and engineering remain
separate. [Official iPlug2 overview](https://iplug2.github.io/).

If evaluated, use the same neutral probes: FX with legacy state, sidechain/dynamic
latency, synth MIDI output, MIDI-only topology and custom GPU/editor surface.
Measure validated coverage, adaptation effort, patches and ongoing maintenance.
Do not choose from a gain demo or invent a percentage-saving threshold. One adapter
strategy wins the default; exceptions need an owner and review/retirement date.

Current clap-wrapper advertises VST3, AUv2, AUv3, AAX and standalone; our initial
target is VST3/AUv2/CLAP/standalone. Upstream coverage does not prove behavior of
CF's older pin or preservation of MIDI-only behavior/legacy IDs. Its standalone
uses RtAudio/RtMidi. [Official clap-wrapper repository](https://github.com/free-audio/clap-wrapper).

Visage provides GPU-accelerated native UI and 2D graphics under MIT. Unresolved
3D/accessibility/host integration requirements remain our work.
[Official Visage repository](https://github.com/VitalAudio/visage).

## 9. Product contract instead of a bespoke migration spec

Each product contributes a small package:

```text
product.manifest.json       identities, platforms/formats, buses/ports, capabilities
identity.lock.json          frozen released IDs per format and the parameter-ID ledger
parameters.*               schema, conversions, automation and host-ID mappings
engine/                    product DSP and minimal platform adapter
dsp.profile.json           declared DSP behavior, conventions and tolerances (§7.9.10)
state/                     document schema and legacy migrations
editor/                    pages, themes, visuals and command bindings
strings/                   translatable UI/parameter text and accessibility labels
compatibility/             identities, saved states, audio/events/UI fixtures
contracts/                 thread/lifecycle declarations and capability versions
performance.profile.json   workloads, supported benchmark machines and budgets
assets.manifest.json       optional asset identities, revisions and packaging policy
deviations.md               only intentional differences and unresolved product gaps
```

`identity.lock.json` is append-only and validated against the portfolio registry
(section 17) at build time; a changed released identity fails the build rather than
producing an artifact that silently orphans user sessions.

The manifest is validated data, not a language. It selects `sidechain`,
`dynamic_latency`, `sample_assets`, `voice_services`, `midi_output`, `timeline_editor`,
`network_session`, `gpu_surface` and `standalone_devices` as needed. Custom codecs,
DSP and visuals remain ordinary C++ hooks.
Future optional selections include `modulation` and `asset_streaming`; selecting
them requires their capability and conformance contracts, not just a build flag.
Record framework API compatibility, state schema and DSP behavior versions
separately as specified in FP-02.

A generator supplies targets, metadata/resources, bindings, generic editor,
state envelope, host-test registration and release checklists. It cannot infer
historical IDs, arbitrary DSP equivalence or bespoke interaction. Unavoidable
per-plugin work is the domain adapter and fixtures—not re-specifying timers,
knobs, streams and wrappers eleven times.
The generator and its maintained starter products are release-tested together
(FP-03); generated wiring and hand-written product code have separate ownership.

## 10. Repository and maintenance model

Create a dedicated platform source repository when implementation is approved.
Start controlled extraction with CF's two consumers and a MIDI adapter probe;
do not move all nine repositories first. Products pin platform releases and upgrade
after conformance passes. Keep optional CMake packages out of unselected products;
headless DSP targets must not fetch host/UI SDKs.

Record dependency versions/licenses, patch hashes and resource provenance. Maintain
API versions, changelog and migration notes. Fix shared bugs once with regression
tests, then roll out to pinned consumers. New abstractions normally require two
consumers or a safety/format need. A one-product requirement such as Heart 3D is
still mandatory for its migration but can remain optional to the common core.
FP-08 defines extension/version negotiation, release qualification, deprecation,
support ownership and rollback requirements. A platform version change must not
silently change a product's DSP compatibility profile.

### 10.1 Repository layout and consumption

A concrete shape, so that the first commit does not have to invent one. §19.3 lists what
that commit actually contains and §19.2 gives the order the packages get built in:

```text
platform/
  packages/<code>/          one directory per module-catalogue code (base, rt, host, …)
    include/                public headers only; contracts documented per API (FP-01)
    src/                    implementation
    tests/                  unit and contract tests for this package
    CMakeLists.txt          one target, one alias, declared dependencies
  adapters/                 clap, vst3, auv2, standalone — the only SDK-including targets
  generator/                starter generation and manifest/profile validation (FP-03)
  starters/                 the five maintained starter products (FP-03)
  harness/                  oracle capture, replay, benchmarks, fuzzers, coexistence tests
  registry/                 portfolio identity registry (section 17)
  deps/                     pinned dependency locks, patches and their hashes (FP-11)
  docs/                     contracts, changelog, migration notes, support matrix
```

Products consume the platform by pinned source fetch — revision plus content hash —
built as part of the product's own build, with a documented local-override path for
platform development. No prebuilt binary distribution and no system-wide install; this
is what makes FP-09's per-copy isolation and FP-08's rollback both work.

During extraction a product may need to build against the platform and its current
implementation at once. That is permitted as a temporary, declared state with an end
condition recorded in the product's `deviations.md`; it is not a permanent dual runtime,
and §11's definition of done ends it. Keep the platform's own history separate from the
eleven product repositories so a platform bisect is possible.

A shipped product still needs servicing while its migration is in flight. State per
product which line receives user-facing fixes, and whether a fix must be ported to the
migration branch before that branch can ship. An unported fix that silently regresses in
the replacement is indistinguishable, to the user, from the migration having broken
something.

Optional packages are CMake options defaulting to off, driven by the product manifest.
A product that does not select `network_session` must not compile, link or fetch NET —
verified by the dependency-boundary test, not by inspection.

## 11. Delivery backlog and exit gates

These are dependency phases, not calendar estimates. No migration must wait for
unrelated ATS release work. Risky feasibility probes happen early.

**Three products are already JUCE-free** and are therefore not migrations: ATS
Resynth, CompressedFeedback and CFSynth all report zero qualified JUCE tokens in the
portfolio audit. They are the platform's *extraction consumers* — the code the
platform is factored out of, and the products that prove the factoring did not
regress anything. The actual JUCE-removal backlog is **eight products in seven
codebases** (P2–P5), which is the number to plan against rather than eleven.

| Phase | Work | Exit gate |
|---|---|---|
| P0 — Baselines/feasibility | Capture identities/parameters/state/audio; inspect compiled dependencies; probe legacy wrapper mapping, MIDI-only/output, latency, GPU, files/TLS; probe two-version coexistence and backend accessibility bridges; define lifecycle, numeric and benchmark profiles | Capability/deviation register; no unknown release-critical architecture requirement; backend decision; FP-01/02/04 baselines; FP-09/10/12 feasibility answered before the backend is fixed |
| P1 — Foundation | Extract/harden CF PARAM/RT/HOST; state transactions, editor seam, generator, starters, contract assertions and benchmark harness; visibility/namespace and FP-environment rules in the build | All three already-JUCE-free products (CF, CFSynth, ATS Resynth) consume one package with no regression; FP-01/02/04 enforced, FP-09/10/11 enforced in CI, FP-03 starter framework and FP-07/08 tooling/release foundations available |
| P2 — FX | GraphicEQ/PunchGate; buffers, oversampling profiles, metering, presets, sidechain, latency/bypass, controls, packaging | Audio oracles and host latency/sidechain/session/UI gates; conventional/sidechain starters pass performance, accessibility (FP-12) and compatibility tests |
| P3 — Assets/MIDI | Mirage instrument/FX + SNDWARP; codecs/assets/capture, legacy state, MIDI/output/actions; FP-06 asset identity/cache/recovery | Captured sounds/assets and sessions restore; sampler/MIDI starters qualified; streaming required only for products declaring it |
| P4 — Synth/GPU | Hydro/Heart; remove APVTS from HydroModel; voices/ADSR/RNG/FFT/tables, FP-05 modulation, controls, GPU solution | Sound/voice/table/preset tests, synth starter, modulation compatibility, 3D interaction/fallback and resource budgets pass |
| P5 — Rich documents | Consolidate MIDI editing/network/OS/standalone and ATS jobs/publication; expand FP-07 replay/diagnostics | Network reconnect/clock/state and ATS analysis/edit lifetimes tested; no duplicate generic runtimes; representative bugs replayable |
| P6 — Portfolio release | Per-product matrix, license/resources, packaging, sessions; starter regeneration, cross-product qualification, mixed-version coexistence and rollback | Declared products ship JUCE-free with reproducible evidence; all applicable FP-01–FP-12 gates pass |

Phases P0–P2 are expanded into buildable milestones in section 19: P0 becomes M0, P1
becomes M1–M4 (spine, editor, generator, consumers) and P2 begins at M5. Section 19 is
the one to work from; this table stays as the dependency and gate view.

P3/P4/P5 packages can proceed concurrently once contracts are agreed, but their
feasibility belongs in P0. CF/CFSynth already give two real consumers, and ATS a
third with a different shape; a small test FX helps conformance but does not prove
portfolio reuse. GraphicEQ has real oversampling/latency complexity and is not a
trivial gain-plugin pilot, which is why it leads P2: it is the first product the
platform was *not* extracted from, and therefore the first honest test of whether
the extraction produced a platform or just a rearrangement of CF.

### 11.1 P0 execution order

P0 is the phase that is actionable immediately, so it is broken down. Items 1 and 2 are
ordered first because they are the only ones that expire.

| # | Task | Artifact produced |
|---|---|---|
| 1 | Classify all eleven products `shipped` / `never-shipped` with evidence (§14.1); then archive and hash the shipped binary of each `shipped` product with its build revision and toolchain | Shipped-status classification; compatibility baseline manifest; the archived binaries themselves |
| 2 | **While the JUCE builds still build**, capture oracle renders and fixtures for every product against the §7.9 corpus and the FP-02 procedure | Oracle vector library with metadata and hashes |
| 3 | Extract identities from those binaries — VST3 class IDs, AU codes, CLAP IDs, bundle IDs, parameter IDs and order, state roots | Seeded portfolio identity registry (section 17) |
| 4 | Collect real user sessions and presets per product, in every format, as state fixtures | Compatibility fixture corpus per product |
| 5 | Run the neutral backend probes: legacy-state FX, sidechain and dynamic latency, synth MIDI output, MIDI-only topology, GPU surface, plus two-version coexistence (FP-09), accessibility bridge (FP-12) and explicit host-ID override (§7.2) | Probe reports; backend decision with evidence |
| 6 | Ratify the conventions: §5.1 language and API, §5.2 threading, §7.9 DSP defaults, FP-10 determinism tiers, FP-02 comparison classes and thresholds | Ratified convention record; profile skeletons that fail the build until filled |
| 7 | Define performance reference hardware, workloads and budgets; define lifecycle states and contract annotations | `performance.profile.json` templates; FP-01 contract vocabulary |
| 8 | Stand up the harness skeleton and CI tiers on the chosen runners, with the coexistence, numerics and fuzzing jobs stubbed but wired | Running CI with red-but-present gates |
| 9 | Close the two open section 14 decisions (D-03 wrapper identity preservation, D-16 surviving reference builds) and confirm or override each `Default` entry | Completed decision record; recorded deviations for any override |
| 10 | Produce the capability and deviation register across all eleven products | Capability/deviation register |

**Why items 1 and 2 expire.** An oracle can only be captured from a build that still
compiles and loads. Toolchains, SDKs and OS versions move; a JUCE-based product that
builds today may not build in a year, and a product already mid-migration may have lost
its reference build entirely. Once the oracle is gone, "preserve the sound" degrades from
a measurable claim to a memory of one, and every downstream gate in FP-02 becomes
unenforceable. Capture first, argue about architecture second. Where a reference build is
already unbuildable, record that in the deviation register immediately and decide
explicitly what compatibility means for that product — do not discover it in P4.

### Shared definition of done

- Every inventory match has an owner; aliases/inheritance/generated sources and
  target/link graphs are separately checked. Unknowns block removal; zero grep
  matches never suffice.
- Production builds with JUCE unavailable; compile dependencies/artifacts prove
  the boundary. Test oracles are isolated.
- Old states/sessions/identities survive; malformed input cannot partially mutate
  state. DSP/event deviations are explicitly reviewed.
- Every product ships a complete `dsp.profile.json` with no unfilled required field,
  and every render path declares a determinism tier and a comparison class that its
  fixtures actually meet.
- RT checks cover allocations, deallocations, locks/blocking and lifecycle callbacks.
- Real editor/host interaction passes, including multiple instances, resize/DPI,
  focus/popups, keyboard panic and teardown with active jobs.
- Optional packages stay absent from products that do not require them.
- Two products built against different platform versions load, process, open editors
  and unload in one host process without interference.
- Untrusted-input corpora cannot crash, hang, over-allocate or write outside declared
  directories; dependency locks, notices and artifact hashes are reproducible.
- The declared accessibility and locale baseline passes on each supported OS.
- All applicable future-product requirements in section 15 pass their acceptance
  gates; unimplemented optional capabilities are reported as unsupported.
- Every gate above cites the contract it enforces (section 7 clause or FP number),
  and every open deviation has an owner and a review date (section 17).

## 12. Reusable validation matrix

| Area | Shared tests and product fixture inputs |
|---|---|
| DSP | Supported 44.1/48/88.2/96/192 kHz; small/odd/large blocks; the §7.9 corpus of silence/impulses/sweeps/tones/noise/music; automation; aliasing/THD; seeds; reset/bypass/latency; in-place and aliased buffers; silence hints forced on and off |
| DSP conventions | Control-block anchoring across host block splits; latency composition and wet/dry alignment; click detection at every declared transition (§7.9.4); level/pan/mid-side round trips; meter ballistics and frame-rate independence; filter clamps and stability at extreme settings |
| Events | Timestamp order; dense automation; note/CC/pressure/program; malformed payloads; overflow/panic; tempo/loop/seek; output offsets; declared SysEx |
| State | Old → replacement, new round trips, defaults/unknowns, corruption/size limits, Unicode/locale, missing assets, save/restore without UI and during audio |
| Host | Selected VST3/AUv2/CLAP validators; actual DAW sessions; gestures, buses, suspend/resume, latency/tail, headless scan, multiple instances |
| UI/GPU | Real dispatcher; embedded/standalone; preset/file/menu/keyboard; text/IME/accessibility; DPI/resize; close/reopen during work; context loss; pixels plus behavior |
| Files/network | Codec corpus/fuzzing, cancellation/memory caps, atomic saves, bad paths/permissions, TLS trust/hostname errors, reconnect/partial frames/clock jumps, redaction |
| Lifecycle/contracts | Legal transitions and injected invalid sequences; thread/ownership assertions; failed/repeated activation; pending reconfiguration; outstanding jobs at teardown (FP-01) |
| Sound compatibility | Historical behavior profiles, reserved parameter IDs/choice mappings, offline/realtime policy, old-session renders after framework upgrades; declared comparison class and thresholds per render path, with cross-platform spread measured before the threshold is set (FP-02) |
| Starters/tooling | Clean generation, unique identities, reproducible metadata, supported-format builds, UI gallery, deterministic state/event replay and diagnostic redaction (FP-03/07) |
| Scale/resources | High-percentile and maximum processing times; deadline misses; many instances; memory/queue/cache/worker limits; UI closed; concurrent loading and bounce (FP-04) |
| Modulation/assets | Routing identity/order/voice lifecycle; unavailable host capabilities; relocated/missing/corrupt assets; prefetch/cache pressure and streaming underrun policies (FP-05/06) |
| Platform evolution | Capability negotiation, consumer source builds, dependency boundaries, supported-release upgrade/rollback and deprecation tests (FP-08) |
| Coexistence | Two platform versions in one process; exported-symbol and duplicate-class inspection; repeated load/unload; host scan cost; teardown with live threads (FP-09) |
| Numerics | Compiler/flag conformance, denormal and rounding-mode handling, non-finite input/output policy, cross-architecture and SIMD-path agreement within declared tolerance (FP-10) |
| Trust boundaries | Malformed/hostile state, preset, asset and frame corpora; path containment and archive traversal; resource-exhaustion limits; credential storage; dependency integrity and SBOM (FP-11) |
| Accessibility/locale | Screen-reader roles/values/actions, keyboard-only operation, focus visibility, text scaling and overflow, locale number/date parsing and formatting, IME and Unicode text (FP-12) |
| Delivery | Architecture slices, clean-machine load, dependency/resource notices, IDs, signing/notarization as applicable, artifact hash/revision manifest |

Proposed qualification baseline: macOS arm64/x86_64 and Windows x64 for selected
products, subject to product support decisions. This is not present Windows parity:
ATS's native-editor build currently rejects non-Apple builds. Other platforms need
explicit qualification. Select named DAWs/versions in P0, including AU instrument,
effect and MIDI-effect routing plus VST3/CLAP hosts; one host cannot prove all formats.

**Automated versus manual.** Most of this matrix runs in CI. Some of it cannot: licensed
DAWs, real session reload, screen-reader behavior, GPU driver variety, notarization on
signed artifacts and listening judgements. Splitting the matrix honestly is part of being
implementable — an unautomatable gate that is never written down simply does not happen.
Each row therefore declares whether it is automated, automated-with-manual-confirmation,
or manual. Manual rows have a written procedure with explicit steps and expected results,
a named signer, the exact product and platform build under test, and a dated record kept
with the release evidence. A manual gate signed without its procedure is not evidence
(section 17). Where a manual gate recurs every release, it is a candidate for automation
and carries an owner for that work; where it cannot be automated, that is stated once
rather than rediscovered each cycle.

## 13. Cost, licensing and industry context

Optimize total cost: extraction/migration + maintenance + host QA/support +
dependencies/signing + delayed product work. Do not count a shared framework
subscription as a separate saving per plugin. No prices, staffing or calendar
estimates are assumed. Measure pilot effort and defect/support load before budgeting
remaining profiles and recurring maintenance.

Removing JUCE does not clear other licenses. Record source revision, license text,
linkage, modifications, notices, resource/fonts ownership, update owner and exit
strategy. ATS's Qatsh/ATSA provenance and libsndfile obligations remain separate.
Obtain appropriate review before distribution; do not relabel copied JUCE
implementations as original code.

Current VST3 documentation describes MIT licensing. Review the exact revisions
bundled by these repos separately; old migration notes asserting a universal
GPL/commercial VST3 requirement are not current guidance.
[Steinberg licensing documentation](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Licensing.html).

Public examples establish several approaches, not one universal industry stack:

- JUCE lists customers including Arturia and Native Instruments; this proves use,
  not that every product or all their DSP is JUCE.
  [JUCE developer/customer stories](https://juce.com/made-with-juce/).
- u-he documents CLAP collaboration and credits framework/CLAP developers in
  its Diva manual. Framework expertise is ongoing engineering, not a one-off
  deletion task; these sources do not reveal its private architecture.
  [u-he CLAP](https://u-he.com/community/clap/),
  [Diva manual](https://dl.u-he.com/manuals/plugins/diva/Diva-user-guide.pdf).
- iPlug2 is a permissive framework route; Vital Audio's Visage is a specialist UI
  layer. Neither defines this portfolio's state, synthesis or collaboration logic.
  See official references in section 8.

The recommendation is an inference from this portfolio: reuse specialist libraries
and own a narrow, tested product platform. The win is fixing shared behavior once
and focusing product work on sound/interaction, not eliminating all dependencies.

## 14. Decisions of record

The seventeen kickoff questions are answered here. Each decision carries a status:
**Decided** — normative now; **Default** — the platform position, chosen as the
boutique-industry norm where no product-specific answer existed, overridable per
product only through a recorded deviation (section 17); **Open** — genuinely
undecided, with the P0 work item that closes it. Sections elsewhere in this
document that read "requires a decision from section 14" are satisfied by the
corresponding entry below.

The operating context these decisions assume: **one maintainer**, GitHub for source
and CI, and a hosted server for the network services. Section 18 right-sizes the
gates, reviews and owner tables in this document to that reality; nothing below
assumes a second engineer.

| # | Subject | Status | Decision |
|---|---|---|---|
| D-01 | Compatibility baselines | Decided | Per-product `shipped` / `never-shipped` classification is prerequisite work, not an assumption (§14.1) |
| D-02 | Platforms, formats, minimums | Default | macOS 11+ universal2, Windows 10+ x64; VST3, AUv2, CLAP, standalone; AAX and Linux deferred (§14.2) |
| D-03 | Wrapper identity preservation | Open | Closed by the P0 legacy-mapping probe (§11.1 item 5, R-01); scope depends on D-01 |
| D-04 | GPU approach | Default | 2D everywhere; no 3D capability in the platform; Heart's 3D is product-local or a recorded deviation (§14.3) |
| D-05 | Strict legacy sound | Default | Strict only for `shipped` products; `never-shipped` start at behavior version 1, frozen at first public release |
| D-06 | Codecs and assets | Default | WAV/AIFF/FLAC through one vetted decoder; no lossy codecs; embed small assets, reference large ones (§14.4) |
| D-07 | MIDI network security | Decided | Public-CA TLS on the hosted server; no trust-disabled path reachable in a release build (§14.5) |
| D-08 | Platform ownership | Decided | The maintainer owns releases, dependency and security updates, and shared QA (section 18) |
| D-09 | Historical profiles and tolerances | Default | FP-02 comparison classes: class A where only surrounding code moved, class B for reimplemented primitives (§14.6) |
| D-10 | Performance qualification | Decided | Low-end reference hardware is the budget target, not current hardware (§14.7) |
| D-11 | Accessibility and languages | Default | English-only at first release, but catalogued strings from day one; keyboard + OS accessibility API baseline (§14.8) |
| D-12 | Reproducibility promise | Decided | T1 within one binary on one machine; T2 across architectures; cross-machine bitwise is never promised (§14.9) |
| D-13 | Security sign-off and response | Decided | The maintainer signs; published triage and fix commitments (§14.10) |
| D-14 | Portfolio identity registry | Decided | `registry/portfolio.identity.json` in the platform repository, CI-validated, with an offline copy (§14.11) |
| D-15 | §5.1 / §5.2 conventions | Default | Accepted as proposed; revisit only if the P0 coexistence harness contradicts §5.2 |
| D-16 | Surviving reference builds | Open | ATS's JUCE sources survive on `master`; the other eight codebases are unaudited (§14.1, R-13) |
| D-17 | CI and qualification hosts | Decided | GitHub Actions with cost-tiered runners; free validators plus a low-cost host set; the maintainer signs (§14.12) |

### 14.1 Baselines, shipped status and surviving oracles (D-01, D-16)

This is the highest-value decision in the section because it is the largest available
scope reduction. A `never-shipped` product has **no** compatibility burden: no frozen
identity, no state-migration obligation, no oracle to capture, and FP-02's measurement
apparatus does not apply to it. A `shipped` product carries all of them permanently.
Classifying wrongly in the conservative direction costs weeks of unnecessary oracle
work per product; classifying wrongly in the permissive direction breaks users'
sessions and is unrecoverable.

The repository has **no git tags**, so "which version shipped" cannot currently be
answered from source control. Before any migration work:

1. Classify each of the eleven products as `shipped` (a binary reached users outside
   the maintainer's machines) or `never-shipped`. Record the classification and its
   evidence — a release page, a download, a customer, a signed installer — in the
   portfolio registry (D-14). Absence of evidence means `never-shipped`; do not
   preserve conservatively by default, because conservative preservation of eleven
   products is what makes this project multi-year.
2. For each `shipped` product, archive the actual released binary and hash it, tag
   the commit that produced it if identifiable, and record the toolchain.
3. For each product with any JUCE-era source, record whether that source still
   builds today. ATS's does survive: the JUCE-based sources are on the `master`
   branch, deleted from `remove-juce` in commit `1a64f030`. The remaining eight
   codebases are unaudited and this is the expiring half of R-13.

**The three already-JUCE-free products need this classification most, not least.**
For ATS Resynth, CompressedFeedback and CFSynth the baseline is whichever build
actually reached users last. If a JUCE-free build already shipped, *that* build is
the oracle and the identity source, and the older JUCE build is historical context
with no compatibility standing — capturing an oracle from it would freeze the wrong
behavior. If only the JUCE build ever shipped, the JUCE-free build is an unreleased
change that must still be measured against it under FP-02 before it ships. If
neither shipped, the product is `never-shipped` and none of this applies. These three
cases lead to opposite work and cannot be distinguished without the classification.

Only after this classification exists is the real scope of the migration known.

### 14.2 Platforms, formats and minimum versions (D-02)

| Axis | Release promise | Note |
|---|---|---|
| macOS | 11.0 Big Sur and later, universal2 (arm64 + x86_64) | Deployment target 11.0; hardened runtime, notarized |
| Windows | Windows 10 1809 and later, x64 | No 32-bit; no ARM64 until a user asks |
| Linux | Not a release promise | CLAP builds may exist unsupported; no qualification rows |
| Formats | VST3, AUv2, CLAP, standalone | One CLAP core, wrapped (section 8) |
| AAX | Deferred | Requires an Avid developer agreement and PACE signing — recurring cost and a per-release signing step; revisit only on demonstrated demand |
| Hosts | See D-17 | Qualification host set, not a compatibility promise per host |

The deferral of AAX is a solo-operation decision, not a technical one: it adds a
paid dependency and a manual signing step to every release of every product.

### 14.3 GPU (D-04)

The platform provides a 2D vector/raster surface and nothing else. No 3D API, no
shader pipeline, no scene graph enters the shared platform. Heart's 3D renderer is
either a product-local extension that owns its own surface and context-loss handling
behind the GPU package boundary, or Heart ships its 2D fallback with a recorded
deviation (R-03). A 3D capability maintained across macOS and Windows for one
product in eleven is not a defensible use of a single maintainer's time.

### 14.4 Codecs and assets (D-06)

Supported for read: WAV (PCM 16/24/32-bit integer, 32/64-bit float), AIFF, FLAC.
Supported for write: WAV and FLAC. No MP3, AAC, Ogg or other lossy codec is decoded
by any product — this removes a large untrusted-parser surface (FP-11) and a
licensing question at the same time. One decoder library serves the whole portfolio;
libsndfile is already present in ATS and dr_libs is the smaller-surface alternative,
and the choice is a P0 dependency decision recorded in `deps/`, not a per-product one.
The same codec set is available on every platform: a platform-conditional codec
matrix is a support burden with no product behind it.

Assets under 1 MiB are embedded in the binary and identified by content hash. Larger
assets are referenced by identity plus revision with a user-visible relink path
(§7.5). Every asset carries an entry in `assets.manifest.json` regardless of which
side of the threshold it falls on.

### 14.5 MIDI network security (D-07)

MIDI Collab's self-signed-certificate behavior does not migrate. The hosted server
presents a certificate from a public CA; clients perform normal chain and hostname
verification with the platform's default trust store. There is no
certificate-verification bypass, no "accept invalid certificate" preference, and no
plaintext fallback compiled into a release build; any such path exists only behind a
debug-build flag that cannot be set at runtime in a shipped binary. Credentials and
tokens live in the OS keystore (FP-11), never in preset or state documents. A
migration note tells existing users the old self-signed behavior is gone.

### 14.6 Sound-compatibility tolerances (D-09)

Default assignment of FP-02 comparison classes, per render path, overridable per
product in `dsp.profile.json`:

| Situation | Class | Rationale |
|---|---|---|
| DSP source unchanged, only host/state/UI plumbing replaced | A | Any deviation is a porting defect, not a numerical one |
| Primitive reimplemented (filter, oversampler, interpolator) | B | Reimplementation legitimately changes the last bits |
| Deliberate behavior change accepted by the maintainer | D, versioned | Ships as a new DSP behavior version; the old version stays selectable |
| Chaotic or feedback-dominated path past its divergence horizon | C, windowed | Compare statistics over windows, not samples (§7.9) |
| Bit-exact requirement (state round-trip, identity, ID mapping) | E | Not audio; exact equality or fail |

A tolerance that has not been measured against cross-platform spread is not a
tolerance; §7.9 requires the ≥10 dB margin before a threshold may be written down.

### 14.7 Performance qualification (D-10)

The budget target is deliberately old hardware. Products must run on machines their
users already own, not on the maintainer's development machine.

| Reference | Machine class | Role |
|---|---|---|
| REF-MAC-LOW | 2017 dual-core Intel MacBook Pro class, or base M1 | macOS budget target |
| REF-WIN-LOW | 4-core x64 laptop, ~2017 vintage, integrated graphics | Windows budget target |
| REF-DEV | The maintainer's current machine | Regression detection only; never the budget |

Baseline instruction set is SSE4.2 on x86_64 and the arm64 baseline on Apple silicon.
AVX2 is permitted only as a runtime-dispatched optional path that is declared in
`dsp.profile.json` as a sound-compatibility surface (FP-10); AVX-512 is not used. No
design may assume more than four cores, and the platform's worker pool (§5.2) sizes
itself accordingly.

Budgets are expressed as instance counts at 48 kHz with 128-frame blocks on the
low-end reference, at a stated CPU headroom, and recorded in
`performance.profile.json`. A product that only meets its budget on REF-DEV has not
met its budget.

### 14.8 Accessibility and localization (D-11)

First release ships English only. Every user-visible string nonetheless lives in
`strings/` from the first commit — retrofitting a string catalogue after eleven
products exist is the expensive path, while writing one from the start costs
approximately nothing. All serialization remains locale-invariant regardless of UI
language (FP-12).

Accessibility baseline for a release: every control reachable and operable from the
keyboard; typed numeric entry available for every parameter; and role, name, value
and value-change notification exposed through the OS accessibility API — VoiceOver
on macOS, UI Automation on Windows. Whether the selected UI backend already provides
those bridges is a P0 probe (R-04); if it does not, building them is owned platform
work, not a product's problem.

### 14.9 Reproducibility promise (D-12)

| Scope | Promise |
|---|---|
| Same binary, same machine, offline render, same input and state | T1 — bit-identical |
| Same binary, same machine, real-time render | T2 — tolerance-bounded (block-size and thread-timing variation) |
| Across architectures, compilers, SIMD paths or machines | T2 — tolerance-bounded; bitwise is **not** promised |
| Across DSP behavior versions | No promise; that is what versioning is for |

Offline-bounce determinism on one machine is the promise users actually depend on
and it is the one the platform guarantees. Anything stronger would forbid runtime
SIMD dispatch, which D-10 requires.

### 14.10 Security sign-off and response (D-13)

The maintainer signs security exceptions; there is no second approver, so the
compensating control is that every exception is a recorded deviation with an expiry
date (section 17), and an expired security deviation blocks the release. Published
commitments for a vulnerability reported in a shipped product or a pinned dependency:

| Stage | Commitment |
|---|---|
| Acknowledge receipt | 7 days |
| Triage and severity assessment | 14 days |
| Fix or documented mitigation for critical/high | 30 days |
| Advisory published with the release | At release |

A `SECURITY.md` in the platform repository states the reporting address and these
timelines. Dependencies are pinned by hash (FP-11) and reviewed on a fixed cadence
(section 18) rather than on notification, because no notification service is
guaranteed to reach a single maintainer.

### 14.11 Portfolio identity registry (D-14)

The registry is `registry/portfolio.identity.json` in the platform repository. Its
single writer is the maintainer; CI validates every product's `identity.lock.json`
against it and rejects collisions on behalf of the generator (FP-03). Because it is
the one artifact that cannot be reconstructed from source, it is protected three
ways: it lives in git history, it is mirrored to the hosted server on each platform
release, and a copy is kept offline. Released values are append-only — a released
identity is never edited or deleted, only superseded with both entries retained.

### 14.12 CI, validators and qualification hosts (D-17)

CI is GitHub Actions, tiered by cost because macOS runners bill at a multiplier of
Linux minutes and Windows at a smaller one. Section 18 defines which tier runs when.

| Purpose | Tool | Cost |
|---|---|---|
| AUv2 validation | `auval` | Free, macOS |
| VST3 validation | Steinberg `validator` | Free |
| CLAP validation | `clap-validator` | Free |
| Static/dynamic analysis | Sanitizers, `clang-tidy` | Free, Linux tier |
| Host qualification, primary | Reaper | Low cost, scriptable, all platforms |
| Host qualification, macOS | Logic Pro, GarageBand | Already available |
| Host qualification, CLAP | Bitwig demo or Waveform Free | Free or low cost |

Automated validators run on every push; host qualification is manual and signed by
the maintainer in the section 12 matrix, at a release gate rather than per commit.

Success: the next plugin supplies a manifest, parameters, engine/controller,
pages and fixtures—not another JUCE-removal spec or another copy of host/state/
event/UI/release plumbing.

## 15. Future-product robustness requirements

These twelve requirements extend the migration specification into a maintainable
product-development platform. They are planned work, not implemented features.
Lifecycle contracts, sound compatibility, maintained starters and performance
budgets are foundation priorities, together with the host-environment contracts
FP-09 through FP-12, which are cheap to establish now and expensive to retrofit
after products ship. Specialist features remain optional packages; their interfaces
are designed early and implemented when a real consumer needs them.

### FP-01 — Executable lifecycle, threading and ownership contracts

Owners: HOST, RT, JOBS, STATE and UI. Foundation priority; define in P0, enforce in P1.

Every public platform API must declare:

- Allowed execution domain and lifecycle states; possible concurrent/reentrant calls.
- Whether allocation, destruction, blocking or exceptions are permitted.
- Input/output ownership, borrowed-data lifetime and callback cancellation rules.
- Work/capacity bounds, failure result and any required host notification.

The declaration is machine-checkable, not a comment. Each public entry point carries an
annotation naming its execution domain — audio, UI, worker, any — which expands to a
debug-build domain assertion at entry and to nothing in release. A single per-instance
domain registry records which OS thread currently holds which domain, so an assertion
fires on the first violation with the offending call site rather than on a later
corruption. The same vocabulary drives the generated lifecycle tests, so an API whose
domain is undeclared has no tests generated and fails the package's own conformance
build. Declaring the domain is therefore the cheapest path, which is the point.

Represent instance lifecycle explicitly: created, initialized/inactive, active,
processing, stopped/active and destroyed, with failure paths. Editor attachment
has its own lifetime; closing the editor must not destroy an active audio engine.
Adapters map native lifecycles to these rules rather than imposing identical
callback ordering on every format. Track execution domain, not a permanent
assumption that one OS thread will always process the instance.

| Operation | Required contract |
|---|---|
| Prepare/activate | Allocate and validate before processing; leave a defined inactive state if preparation fails |
| Process/reset on audio | Bounded work; no allocation, resource destruction, blocking, file/network access or UI callbacks |
| State save/restore | Respect the native API's caller/synchronous-return requirements; use coherent snapshots and staged validation, not an assumption that the editor is open |
| Reconfiguration | Request, prepare, host-authorized transition, apply and acknowledge; retain the old valid configuration while pending |
| Editor/job completion | Validate owner lifetime and revision; discard stale work without touching retired objects |
| Deactivate/destroy | Stop callbacks, cancel/drain owned work and reclaim outside audio; prevent callbacks into a destroyed instance |

For CLAP, latency and port topology cannot change while activated. A requested
change requiring new latency/topology must wait for host deactivation and be
installed for the next activation. If the host delays the transition, expose
pending status and continue the previous valid configuration; do not spin or
block the callback. A product may instead maintain a fixed declared latency and
internally compensate, provided its sound/bypass contract allows it. Distinguish
parameter flush without audio from a fabricated zero-frame CLAP process call.
[CLAP lifecycle](https://github.com/free-audio/clap/blob/main/include/clap/plugin.h),
[parameter/restart rules](https://github.com/free-audio/clap/blob/main/include/clap/ext/params.h).

Debug builds and the test harness enforce thread/state/ownership assertions;
release builds have bounded error handling without formatting/logging on audio.
Contain recoverable failures at SDK boundaries; no exception may cross a native
plugin ABI. This does not promise isolation from memory corruption inside a DAW.

Acceptance: generated lifecycle tests cover valid and invalid sequences, failed
activation, repeated prepare/reset, delayed reconfiguration, UI reopen and teardown
with active jobs. RT allocation/deallocation/blocking checks exercise all audio-domain
callbacks, not only steady-state process. Race/lifetime sanitizer tests run separately
from timing benchmarks; passing assertions alone is not a realtime proof.

### FP-02 — Sound compatibility and independent versioning

Owners: PARAM, STATE, DSP and product engines. Foundation priority; baseline in P0.

| Version/identity | Meaning | Compatibility rule |
|---|---|---|
| Platform API version | Consumer source/interface compatibility | An API update alone must not select different DSP behavior |
| State schema version | Serialized structure and migration path | Decode old structure without silently changing its intended behavior profile |
| DSP behavior version | Sound-affecting algorithm and control semantics | Persist the profile; old states select their defined historical behavior |
| Product release/version and format IDs | Distribution and host identity | Release version is not a substitute for the above; released host IDs remain stable |

The DSP profile covers filter/envelope/oversampling laws, modulation order,
interpolation, random sequence, parameter mapping, latency, declared offline
quality behavior, and the numeric environment and determinism tier that make it
reproducible (FP-10). Missing behavior metadata in legacy state maps to an explicit
historical default, not automatically to the newest implementation. Reject an
unsupported profile clearly rather than rendering with an unrelated fallback.

For a sound-changing update, choose and document one of: preserve the old path,
offer an explicit new mode with opt-in conversion, or release a deliberately
incompatible product/major version with a session migration plan. Define retention
and support costs; this does not require preserving unlimited algorithm versions.
Security/correctness fixes that affect sound still require a documented exception.

Keep a permanent parameter-ID reservation ledger. Retired IDs cannot be reassigned;
retain compatible hidden/legacy mappings where old sessions require them. Choice
numbers/order and normalized mappings are compatibility data, not display-only
details. Defaults for newly created instances may evolve only without rewriting
the meaning of restored old state. Preset files and host sessions carry the same
behavior identity when they represent the same sound.

#### Measuring sound compatibility

"Within tolerance" is meaningless until the measurement is defined. This subsection
defines the metrics, the comparison classes and the procedure, so that every product
argues about the same numbers. Thresholds below are platform defaults ratified or
overridden per product in P0 and recorded in `dsp.profile.json` (§7.9.10).

**Alignment first.** Before comparing, cross-correlate the two renders to find the
integer sample offset. The offset must equal the difference in *declared* latency —
normally zero. Any other offset is a latency-contract failure (§7.9.3) reported as such;
it is never silently corrected by shifting one render to make the comparison pass. The
comparison window starts after the declared latency and includes the full tail.

**Metrics.** All computed on float64 copies of the float32 renders:

| Metric | Definition | Purpose |
|---|---|---|
| Peak error | `20·log10(max\|a[n] − b[n]\|)` in dBFS | Catches single-sample defects a mean hides |
| Segmented null depth | For each 4096-sample window at 50 % overlap where the reference RMS exceeds −80 dBFS: `20·log10(rms(a−b) / rms(a))`. Report the **worst** window and its start sample, plus the global figure | The primary metric. A global null hides a short transient defect inside a long clean tail |
| Spectral difference | STFT with declared size/window/hop; per-bin magnitude difference in dB over bins whose reference magnitude exceeds a declared floor; report maximum and 99.9th percentile | Compares implementations that differ legitimately in phase; the profile states whether phase is in scope |
| Silence integrity | Silent input must yield bit-exact silence unless the product declares idle noise | Catches denormal remnants, DC offset and leaking state |
| Aliasing / THD | Sine at 1 kHz and 7 kHz, −6 dBFS, through the non-linear stage; Blackman-Harris windowed FFT; sum energy in harmonic bins (THD) and in non-harmonic bins (aliasing), each in dB relative to the fundamental | Compared against the oracle's figures, not against an absolute ideal |

**Comparison classes.** Every render path declares one, per product:

| Class | Default thresholds | When it applies |
|---|---|---|
| **E** — exact | Bitwise identical | IDs, state encodings, event streams, integer data, and every T1 self-comparison such as the block-size sweep |
| **A** — arithmetic-equivalent | Peak error ≤ −120 dBFS and worst segmented null ≤ −100 dB | Reordered arithmetic, alternative SIMD paths, compiler or toolchain changes |
| **B** — transparent replacement | Worst segmented null ≤ −60 dB and maximum spectral difference ≤ 0.1 dB below 20 kHz | The default claim for a reimplemented primitive replacing a JUCE one |
| **C** — documented difference | Worst segmented null ≤ −40 dB, plus recorded listening sign-off and a deviation entry naming the audible effect | Where an exact match is infeasible and the difference was reviewed |
| **D** — deliberate change | No threshold; requires the FP-02 exception record, a DSP behavior version bump and a migration policy | Intentional algorithm changes |

A tolerance must exceed the observed cross-platform and cross-compiler spread for the
same path by a margin of at least 10 dB. If it does not, the gate is flaky by
construction: either the path is genuinely less reproducible than claimed and its class
drops, or the implementation needs fixing. Measure the spread before setting the number.

**Chaotic and self-oscillating systems.** Feedback, chaotic and self-oscillating paths —
CompressedFeedback most obviously, plus any resonator driven into instability — amplify a
one-LSB difference exponentially. A sample-domain null test on these is invalid beyond a
divergence horizon, and forcing one produces either a meaningless pass at a huge tolerance
or a permanent red test. Such paths declare instead: a **bounded comparison horizon** in
samples over which class A or B applies, and **statistical descriptors** compared over the
full render — banded spectral envelope, RMS and crest factor trajectories, and energy
distribution — each with its own thresholds. State the horizon; do not disguise it.

**Instrument renders** use an identical event trace with sample offsets, an identical
seed, and deterministic voice allocation (§7.9.9). Where voice allocation legitimately
differs between implementations, compare per-voice renders before the mix rather than
loosening the tolerance on the sum.

**Signal corpus.** Every product runs, at minimum: digital silence; DC; a unit impulse at
sample 0 and again at a mid-block offset; sine tones at 100 Hz, 1 kHz and 10 kHz at −60,
−20 and −0.1 dBFS; a 10-second logarithmic sweep from 20 Hz to Nyquist; a 19 + 20 kHz
dual-tone intermodulation probe; white and pink noise from a fixed seed; a step and a
naive square wave as aliasing probes; an exponential swept sine for non-linear impulse
extraction where a harmonic-order breakdown is wanted; worst-case automation ramps on
every smoothed parameter; note-on/off and sustain patterns for instruments; and the
product's own edge cases. Music excerpts are licensed, hashed and referenced by revision.

**Procedure.** For each supported sample rate: render the corpus through both the oracle
and the candidate, at block sizes {1, 7, 32, 64, 512, `maxFrames`} plus one randomized
split with a fixed seed, through both the realtime and offline paths, with bypass off and
on. Store renders as float32 raw plus a JSON sidecar recording product, behavior version,
platform, toolchain, flags, seed, corpus item and content hash. A failure reports the
metric, the threshold, the worst window's start sample and a difference spectrogram.

**Golden management.** Goldens are immutable artifacts keyed by product, behavior version,
render path, rate and corpus item. CI may never regenerate one. Regeneration requires a
change record naming the intended behavioral difference, its approver and its migration
policy — the same record FP-02 requires for a sound-affecting change. A golden that
changes without such a record is treated as a defect in the change, not in the golden.

Acceptance: historical state plus event/seed fixtures render against these classes and
thresholds on supported configurations after each shared DSP change. Keep exact tests for
IDs and encodings, and explicit tolerances for floating-point audio. Round-trip
serialization is not sound compatibility; changing a golden requires review of the
intended behavior change and its migration policy. The oracle capture procedure, corpus
and thresholds are themselves versioned artifacts: a comparison whose method is not
recorded is not evidence (section 17).

### FP-03 — Maintained starter products and generator contract

Owners: BUILD, TEST, HOST, UI and the relevant capability packages. Foundation
priority; generator starts in P1 and each profile qualifies with its first consumers.

| Starter | Minimum demonstration beyond shared parameters/state/editor |
|---|---|
| Conventional FX | Mono/stereo processing, smoothing, bypass, meters and preset workflow |
| Sidechain/lookahead FX | Optional auxiliary bus, routing, lookahead/oversampling, host-coordinated latency and dry/wet alignment |
| Polyphonic synth | Voice policy, keyboard/MIDI, sustain/pressure, modulation and release tails |
| Sample-based instrument | Asset identity, asynchronous loading, missing-file recovery, publication and sample playback; streaming variant only when supported |
| MIDI-only processor | Zero audio buses, timestamped input/output, transport discontinuities, state and a functioning editor |

Each starter is a real maintained consumer built in every format/OS combination
it declares supported. Unsupported cells are explicit, not silently skipped or
made to pass by changing product topology. Starters complement tests of existing
products; they do not replace legacy-session and specialized-UI evidence.

Generation requires explicit product identity input and rejects collisions in
the portfolio registry. Example/test identities must not escape into releases.
Given the same inputs, generated metadata/wiring is deterministic and records the
template/platform version. Regeneration cannot overwrite hand-written engine,
state-migration or page code; generated files are marked and kept separate.

Acceptance: CI generates each starter into a clean temporary workspace, builds
its declared targets, tests automation/state/editor behavior, and checks dependency
boundaries and packaging metadata. A second generation is unchanged. Documentation
is exercised by the same commands. A new compatible parameter can be added through
the schema without editing host glue, serializers or bindings by hand.

### FP-04 — Performance and resource budgets at portfolio scale

Owners: RT, JOBS, DSP, GPU, ASSETS and TEST. Foundation priority; define workloads
in P0 and add automated measurement in P1 before optimizing individual products.

Every product profile specifies reference hardware/OS/build configuration, sample
rates/block sizes, voice/route limits, instance counts, workloads and numeric
budgets. Set budgets from representative measurements and product goals; this
spec deliberately does not invent a universal CPU percentage or instance limit.
Budgets must be filled in before the profile is release-qualified.

Measure callback duration distributions and maximum observed duration, deadline
misses, activation/scan/editor-open time, memory high-water marks, queue/cache sizes,
worker concurrency and UI/GPU use. The block duration is a timing reference, not
the plugin's whole available CPU budget: the host must process other tracks too.
Average CPU and a no-allocation test cannot substitute for tail-latency evidence;
measured maxima are observations, not mathematical worst-case guarantees.

Required stress scenarios include many mixed plugin instances, all editors closed,
multiple editors visible, dense events, long tails, maximum declared polyphony,
simultaneous sample/table/analysis jobs, memory pressure, and offline bounce during
background work. Test repeated create/destroy as well as steady-state playback.

Bound worker creation and queue growth. Define fairness/cancellation so one heavy
instance cannot indefinitely starve others. Do not start a large pool or device
stack during every plugin scan; scanning also creates no sockets, windows or user
files and has its own measured time budget (FP-09). Shared immutable caches, if introduced, require
explicit lifetime/eviction/accounting and multi-version safety; a global singleton
is not assumed safe. Host-assisted realtime parallelism is distinct from background
analysis/loading and must have a serial fallback. CLAP's thread-pool request waits
for completion and its documentation warns about hard-realtime synchronization;
using it does not establish a lock-free audio path.
[CLAP thread-pool contract](https://github.com/free-audio/clap/blob/main/include/clap/ext/thread-pool.h).
Under the default no-blocking contract, this optional parallel path stays disabled;
adoption requires an explicit reviewed timing-contract exception and separate
qualification, not bypassing FP-01 checks or using background job workers on audio.

Define overload behavior per resource: drop/coalesce telemetry, cancel stale
jobs, cap voices through the selected policy, or report an asset underrun. Never
silently switch a sound algorithm to meet a budget. Define realtime versus offline
quality explicitly and include it in the sound contract. Stop or throttle invisible
editor work without affecting processing or host automation.

Acceptance: benchmark artifacts compare against the supported platform release
on comparable machines, with explicit regression thresholds. Release gates include
resource-cap and stress tests; noisy timing measurements get controlled reruns,
not automatic golden replacement. Keep timing instrumentation separate from
sanitizer-heavy runs and bounded within any instrumented callback.

### FP-05 — Optional reusable modulation subsystem

Owners: DSP/modulation with PARAM, MIDI, VOICE, STATE and UI integration. Define
with the foundation; implement and prove reuse with CF/CFSynth and the synth profile.

Share routing mechanisms, not product oscillator/filter algorithms. Declare
sources and destinations with stable IDs, scope (global/channel/note/voice), units,
range, polarity, depth/curve and supported control/audio update rate. Separate
saved base values, host modulation and internal modulation; define combination
order, mapping domain, clamping and smoothing as part of the DSP behavior profile.

Routes have durable identities and serialize logical endpoints, never pointers
or transient voice indices. Resolve them to dense bounded processing plans outside
audio; publish edits safely. Bound route count and work. Either reject cycles or
require an explicit supported delay/feedback policy; do not introduce a universal
dynamic DSP graph to implement a modulation matrix.

Define voice-start/retrigger/release/reset behavior and how per-note modulation
follows note identity. UI controls show base versus effective value without
writing modulation back into saved automation. Missing endpoints during migration
produce a diagnostic and a defined disabled-route policy, not a remap to another
parameter. MIDI-learn and host automation must not double-record the same gesture.

Keep global and per-note host capabilities distinct. CLAP exposes separate
per-note modulation flags; the selected adapter must advertise only what the
engine and destination format actually support. Lack of a host capability must
have an explicit fallback or unsupported result, not silent polyphonic-to-global
conversion. [CLAP parameter capabilities](https://github.com/free-audio/clap/blob/main/include/clap/ext/params.h).

Acceptance: at least two product consumers use the shared routing mechanism;
tests cover combination order, block splitting, sustained/repeated notes, voice
stealing, route edit/publication, preset restore and unsupported adapter features.
Products without modulation do not link or allocate the subsystem.

### FP-06 — Asset identity, content packs, streaming and bounded caches

Owners: ASSETS, FILES, JOBS, STATE and OS. Identity/recovery is required in P3;
streaming and pack installation become mandatory only for consumers declaring them.

Separate a stable logical asset ID from its content revision/hash and its current
filesystem location. Specify whether a hash covers encoded bytes or canonical
decoded content; a checksum identifies content, not publisher trust. Record type,
format, size, channel/rate metadata, provenance and packaging/embedding policy.
Resolve exact declared revisions; never substitute a same-named but different file.

Support relocatable references, user relink/search, embedded versus external assets
and missing/corrupt/incompatible-resource diagnostics. Optional content packs have
their own IDs/versions/manifests and can coexist; updating a pack must not silently
replace the revision referenced by an old session. Installation/import is bounded
and staged with integrity/path checks, then atomically published; cleanup must not
remove assets still referenced by sessions or active instances.

Streaming exposes prepared buffers and readiness, not disk handles, to the engine.
Define prefetch/seek/cancellation policy, per-instance and aggregate cache budgets,
eviction rules, in-use pinning and reclamation outside audio. Scope any shared cache
carefully; do not assume independently loaded plugin modules share one runtime.
Realtime processing never waits for disk/network or triggers an automatic download.

Each consumer declares an underrun policy such as a bounded fade to silence or
another explicitly approved behavior, plus diagnostics. Offline rendering has a
separate readiness/failure policy compatible with the host contract; it must not
silently export missing audio or an alternate sample. Existing non-streaming
products remain non-streaming unless their requirements change.

Acceptance: use fixture libraries moved/renamed across directories, missing and
corrupt assets, pack upgrades, simultaneous readers, cancellation at each load
stage, seek storms, cache pressure and simulated slow storage. Demonstrate bounded
memory and no disk waits in audio. Keep the complete package out of a simple FX
build that does not select assets.

### FP-07 — Developer workbench, replay and privacy-safe diagnostics

Owners: TEST, BUILD, UI, RT diagnostics and product controllers. A basic workbench
starts in P1; profile-specific replay expands with product adoption.

Provide an offline render runner, native UI component gallery, parameter/state
inspector and bounded event recorder/replayer. The gallery exercises actual backend
dispatch, focus, menus, resizing, keyboard/accessibility and supported GPU surfaces.
The workbench embeds the same production engine/controller/UI components with
injectable services; it is not a new DAW or arbitrary third-party plugin host.

A reproducibility record includes product/platform/dependency revisions, behavior
profile, render configuration, ordered parameter/MIDI/transport events and seeds.
Control clocks and worker scheduling in test mode; store required asset revisions.
Replay the same trace headlessly and through the relevant adapter. State the
determinism boundary: GPU screenshots, network timing and different CPU/compiler
floating-point results may require tolerance or simulated inputs, not bitwise claims.

Diagnostics use fixed-size/capped audio-side records and counters; formatting,
compression and file writes happen outside audio. Record overflows rather than
growing an unbounded trace. Support users can export a bundle they preview first.
Default export excludes credentials, personal paths, raw audio, sample content and
unreviewed full state. Use a minimal sanitized reproduction state; allow additional
user-selected content only with clear disclosure, and never include credentials.
There is no automatic upload or background telemetry requirement.

Acceptance: reproduce representative automation, state-restore, lifecycle and
worker-ordering defects from fixtures without a private DAW project. Tests verify
redaction with seeded sensitive fields, bounded recording and exact build/profile
identification. At least one interaction regression runs through real dispatch;
the workbench cannot replace actual supported-host qualification.

### FP-08 — Extension, support and controlled platform evolution

Owners: platform maintainer, BUILD/TEST and each capability owner. Define before
the first shared release; enforce for every platform upgrade.

Optional capabilities use stable names, explicit interface versions, dependency
requirements, lifecycle/thread contracts and conformance tests. Build-time selection
controls linkage; runtime queries negotiate what an adapter/host can actually do.
Unknown optional features may be omitted only under the declared fallback policy;
a required incompatible feature must fail clearly. Do not imply stable cross-compiler
C++ binary ABI: products continue to build against pinned source/static libraries.

New needs start as isolated product-local extensions behind the approved boundary.
Promote them when two real consumers share a contract or a demonstrated format/
safety requirement warrants it. Keep specialized GPU/network/streaming features
optional. This provides a path for future MPE/MIDI 2.0, multi-output/surround,
new formats or specialist engines without advertising unimplemented support.

Each package has an owner, supported versions/platforms, dependency/patch inventory,
changelog and deprecation policy. Set a stated support window and migration path
before deprecating APIs or behavior profiles. Unowned packages cannot become
required release dependencies. Version/signature drift is checked against pinned
consumers; downstream products opt into qualified releases rather than following HEAD.

CI tiers: focused unit/contracts and representative starter builds on each change;
broader cross-product builds for shared/API/dependency changes; the complete declared
support matrix and compatibility/performance gates before platform release.
Publish consumer impact notes, fixture results, dependency changes and rollback
instructions with the release. A framework API-compatible release must still pass
sound and state tests; semantic version labels alone are not evidence.

Rollback retains previous source locks, signed artifacts where applicable and
readable user data. Schema upgrades must not overwrite the only recoverable copy
of a project/preset. Test rollback before adoption; if older binaries cannot read
newly saved state, disclose that limit and preserve an export/backup path. Do not
claim that reinstalling an old binary makes every newer state backward-compatible.

Acceptance: upgrade at least two distinct product profiles from the previous
supported platform release, run their old-session/audio/UI fixtures, and exercise
the documented rollback path. Verify optional dependencies stay absent, required
capability mismatches fail clearly, and a product-local extension can be added
without edits across unrelated product engines or format adapters.

### FP-09 — In-process coexistence and binary hygiene

Owners: BUILD, BASE, JOBS and OS, with every package. Foundation priority; probe in
P0, enforce in the build from P1.

A plugin is a guest in someone else's process. Assume a host loads several of our
products at once, built against different platform versions, alongside third-party
plugins that vendor the same open-source libraries we do; assume it loads and unloads
bundles repeatedly, scans in-process or out-of-process, and calls us from threads it
owns. Static libraries do not isolate us from this: duplicate global names still
collide inside one address space.

| Hazard | Required rule |
|---|---|
| Symbol collision between two platform versions | Default hidden visibility for all platform and vendored code; export only the format entry points; verify exported symbol lists per artifact |
| ODR violation across versions | Version-tagged inline namespace derived from the platform release; header-inlined templates and types must differ between incompatible versions |
| Process-global Objective-C names on macOS | Every class, category and protocol we define carries a unique per-product/per-build suffix; duplicates silently resolve to whichever bundle loaded first |
| Vendored third-party globals | Hidden visibility and, where a version conflict is plausible, symbol prefixing; two plugins with different revisions of the same codec must not bind to each other |
| Static initialization order | No process-global mutable state; no static initializer that touches devices, files, network, UI or the OS locale; per-instance composition root only |
| Teardown during unload | Threads joined and callbacks stopped in the format's explicit teardown, never in `atexit`, static destructors or `DllMain`; no locking or thread joins on the unload path |
| Leaked execution | No detached threads or timers outliving the bundle; thread-local destructors run before unload; threads carry identifying names |
| Resource and settings collisions | Font, shader, window-class, settings-file, registry and shared-memory names namespaced per product and version |
| Scan cost and side effects | Scanning creates no devices, sockets, windows, worker pools or user files; bounded, deterministic and idempotent |

Platform types never cross a plugin-binary boundary; each product links its own copy.
Do not publish a shared dynamic library that multiple products resolve at runtime.

Acceptance: a coexistence harness loads two starters built against different platform
releases into one process, processes both, opens and closes both editors, then unloads
and reloads, under leak and address sanitizers. Artifact inspection asserts the exact
exported-symbol set and the absence of duplicate Objective-C class names across shipped
bundles. Repeated load/unload cycles show no growth and no crash on unload. Scan-time
cost and side effects are measured against the budget in FP-04.

### FP-10 — Numerical environment, denormals and determinism tiers

Owners: DSP, RT, BUILD and TEST, with product engines. Foundation priority; define in
P0, enforce in P1. FP-02 decides *which* sound is correct; FP-10 defines the environment
in which that decision is reproducible.

- **Compiler contract.** Fast-math and other value-changing float optimizations are
  prohibited in shipped DSP targets. Floating-point contraction is off unless a DSP
  behavior profile declares it. The exact flag set, compiler and standard-library
  revisions are part of the profile and are recorded in the reproducibility record
  (FP-07). A toolchain upgrade is a sound-affecting change until fixtures prove otherwise.
- **Denormals and modes.** The FPU/SIMD control state belongs to the host. Audio-domain
  entry points set the declared denormal mode and restore the caller's mode on exit;
  rounding mode is never left modified. Never assume the host flushed denormals: reset
  feedback structures to exact zero and keep the sound identical under both settings, or
  declare the dependency in the profile.
- **Non-finite values.** Upstream plugins deliver NaN and infinity. Each product declares
  an input policy and an output policy — for example, sanitize the block, reset the
  affected state and count the event in telemetry — so a single bad sample cannot poison
  a feedback path permanently. Test builds trap on non-finite output; release builds apply
  the declared bounded policy without logging on audio.
- **Determinism tiers.** Every render path declares one: **T1** bitwise identical for the
  same binary, machine and flags; **T2** equal within declared tolerance across supported
  compilers, architectures and SIMD paths; **T3** not reproducible (GPU, timing- or
  thread-order-dependent). Promise T1 for offline bounce where a product claims render
  reproducibility, and never for a path whose result depends on scheduling.
- **Block-size and rate invariance.** T1 paths produce identical output regardless of how
  the same timeline is split into blocks, including one-sample and maximum blocks and
  events landing on block boundaries. Rate-dependent behavior is declared, not incidental.
- **Multiple code paths.** Runtime SIMD dispatch, per-architecture kernels and scalar
  fallbacks are sound-compatibility surfaces: either all paths agree within the profile's
  tolerance and are compared pairwise in CI, or the profile pins one path. Parallel
  reductions use fixed partitioning or drop to T3.
- **Counters and clocks.** Accumulate musical and sample positions from integer sample
  counts, not by repeatedly adding a float increment; phase accumulators wrap exactly.
  Random streams are per-instance and per-voice, seeded deterministically (FP-02).

Acceptance: a block-size sweep and event-alignment sweep prove T1 claims; a build-flag
conformance test asserts the compiled DSP target's floating-point settings; a hostile-FPU
test enters callbacks with denormal, rounding and exception modes altered and verifies both
the result and the restored caller state; NaN/Inf injection runs per product; cross-platform
fixtures compare macOS arm64, macOS x86_64 and Windows x64 within declared tolerance.
Tolerance changes and golden updates go through the FP-02 review.

### FP-11 — Trust boundaries, untrusted input and supply chain

Owners: SEC with STATE, ASSETS, FILES, NET and BUILD. Foundation priority for the parsing
and build rules; network and pack rules apply to the consumers that declare them.

Threat model: our code runs with the user's full privileges inside a DAW, with no sandbox.
Its inputs — host session state, downloaded presets and content packs, sample and window
files, project paths, and for MIDI Collab remote peers — are authored elsewhere and must be
treated as attacker-controlled. Out of scope: defending against a malicious host, against
local software already running as the user, and DRM.

- **Parsers are the attack surface.** Every decoder validates before allocating: explicit
  sizes, allocation and element-count caps, recursion and nesting depth limits, checked
  arithmetic on all offsets and lengths, no reinterpretation of input bytes as structures
  or pointers, no trust in self-declared offsets. Reject and preserve the working state;
  never attempt to repair malformed input in place.
- **State never selects code.** Deserialization may not name a library, resolve a symbol,
  choose an executable path or evaluate arbitrary expressions. Where a product exposes an
  expression feature, it runs in a bounded interpreter with no file, network or host access
  and explicit step/time/memory limits, and its input is untrusted like any other field.
- **Path containment.** Canonicalize before use; reject traversal, symlink escape, absolute
  or device paths inside archives and packs, and reserved Windows names. Write only within
  declared directories, atomically, never destroying the user's only copy (FP-08).
- **Resource exhaustion is a bug, not a user error.** Cap decompression ratios and output
  sizes, per-load and aggregate memory, and load time; support cancellation at every stage.
  A hostile asset must fail cleanly rather than exhaust the host process.
- **Network and credentials.** TLS with verified chain and hostname, a pinned minimum
  version, bounded frames and timeouts. No release configuration may select a plaintext or
  trust-disabled path; test-only transports are excluded from release builds by construction,
  not by a runtime flag. Tokens live in the OS keystore, never in settings files or logs.
- **Supply chain.** Pin every dependency by revision and content hash and verify at fetch;
  store patches in-repo with hashes; forbid unpinned network fetches during build. Generate
  an SBOM and license inventory per release artifact and diff it against the previous
  release. Signing and notarization credentials are held by the release owner with a
  documented revocation and re-signing procedure.
- **Response.** Maintain a private report path, a triage owner and a severity rubric. A
  security fix that changes sound still requires the FP-02 exception record.

Acceptance: each decoder — host state, presets, SMF, audio and window assets, pack
manifests, network frames — is fuzzed under address and undefined-behavior sanitizers to a
declared corpus and time budget in CI, with the seed corpus and any reproducers retained.
Crashes, hangs, leaks and unbounded allocation block release. Path-traversal and archive-escape
fixtures, keystore round-trips, an assertion that no trust-disabled transport is reachable in
release configuration, SBOM generation and hash-verification gates all run in the release tier.

### FP-12 — Accessibility, input methods and internationalization

Owners: UI, DRAW and OS, with product pages. Foundation priority; the baseline is proven by
the starters in P1/P2 and inherited by every product afterwards.

Declare a named baseline per platform — screen-reader support through the OS accessibility
API on macOS and Windows — and state what "supported" means before the first release, because
retrofitting semantics into finished custom UIs is far more expensive than building them in.

- Every interactive control exposes role, name, value as both text and number, range and
  step, enabled/selected state and available actions, and notifies the platform on change.
- Full keyboard operability: defined focus order, a visible focus indicator, working
  escape/return semantics for menus and popups, no keyboard trap inside an embedded editor,
  and correct hand-back of unhandled keys to the host that owns the window.
- Typed value entry exists for every automatable parameter; it is the reliable accessible
  path to a rotary control and is also the fastest path for expert users.
- Honour OS reduced-motion, increased-contrast and text-size settings where declared. Never
  encode meaning in colour alone. Declare a minimum contrast for functional text.
- Data-dense surfaces such as spectra, waveforms and timelines are not exempt by default:
  they expose summary values, a keyboard-navigable selection model and readable numeric
  readouts. Any true exemption is recorded in `deviations.md` with its alternative path.
- Text handling is Unicode-complete: IME composition in every text field, grapheme-aware
  cursor and selection, no one-glyph-per-byte assumptions, correct measurement for the
  shipped scripts.
- Locale affects display only. Parameter text parsing and formatting are explicit about
  decimal separators and digit grouping; serialized state, IDs and fixtures always use the
  invariant form. No saved file may depend on the machine's locale.
- No hard-coded user-visible text in engine or platform code. Each product owns a string
  catalogue with stable keys; missing keys are visible in development builds and fall back
  to the base language in release. Layout tolerates length growth; right-to-left support is
  either implemented or explicitly out of scope.
- Verify the accessibility tree actually attaches through each format's window hierarchy;
  a wrapper or host container can interpose and silently break it.

Acceptance: an automated pass over the starter gallery asserts that every control exposes
role, name, value and actions and is reachable by keyboard alone; a scripted or recorded
screen-reader smoke test runs per supported OS and format; a pseudo-localization build with
lengthened accented strings shows no clipped or overlapping functional text; the state and
parameter-text suites run under at least one comma-decimal locale; text-scaling and
reduced-motion behavior are checked. Gaps are backlog items with owners, not silent omissions.

### 15.13 Priority and completion summary

Implement FP-01/02/03/04 first: lifecycle enforcement, sound versioning, maintained
starters and measurable resource budgets. Establish FP-07/08 tooling and release
ownership alongside them. Implement FP-05/06 specialist functionality with real
modulation/asset consumers and their declared capabilities. No existing product
must wait for an unused future streaming engine or format.

| Requirement | Cost if established now | Cost if retrofitted after release |
|---|---|---|
| FP-09 coexistence | Build flags, namespace macro, naming rules | Rebuild and re-release every shipped product; field crashes only reproducible with a specific set of installed plugins |
| FP-10 numerics | Flag policy and a fixture sweep | Every past render and preset becomes suspect; tolerance disputes without a baseline |
| FP-11 trust boundaries | Parser rules and CI fuzzing | Incident response against installed binaries, plus a security fix that may change sound |
| FP-12 accessibility/locale | Semantics in shared controls once | Re-instrumenting every finished product UI, per platform |

FP-09 through FP-12 are therefore foundation work despite being framed as
future-product requirements: each is cheap while the shared controls, build system and
DSP fixtures are still being written, and each becomes a re-release event afterwards.

A platform release is robust enough for new products when its applicable contracts
are executable, starter generation is tested, compatibility and performance budgets
pass, coexistence, numeric, trust-boundary and accessibility gates pass, failures can
be reproduced safely, and an upgrade can be supported or rolled back without silently
changing users' sessions or sound.

## 16. Risk register, kill criteria and review triggers

A strategy without a documented failure path becomes sunk cost. Each risk names the
early signal that would reveal it, the response, and the condition under which the
approach itself is abandoned rather than defended. All risks are owned by the single
maintainer (D-08); review dates are set at the phase gates in section 11, and the
substitutions in §18.1 apply to every "owner" and "review" in this table.

| ID | Risk | Early signal | Response | Abandon/fallback condition |
|---|---|---|---|---|
| R-01 | Wrapper cannot preserve released VST3/AU identities, state or MIDI-only topology | P0 legacy-mapping probe fails on one shipped product | Patch upstream with explicit ID overrides; contribute or maintain a bounded fork | Keep ATS-style native adapters for released products and use the wrapper only for new products; do not carry an unmaintainable fork |
| R-02 | Users' existing sessions or presets fail to reload after migration | Any compatibility fixture regression in P0/P1 | Freeze identities, extract from reference binaries, add fixtures before code | Release blocker, always; no product ships past a failing identity or state fixture |
| R-03 | No acceptable GPU path for Heart's 3D renderer or CF's feedback effects | P0 GPU probe cannot create/lose/resize context reliably across hosts | Narrowly isolated native surface behind the GPU package | Heart ships the 2D fallback with a recorded deviation, or stays on its current build, rather than blocking the portfolio |
| R-04 | UI backend lacks accessibility, IME or host-embedding capability | P0 backend probe and FP-12 baseline check | Build the missing platform bridge as owned platform work | Reconsider the UI backend before P2 locks products into it; the decision is cheapest here and near-irreversible later |
| R-05 | Sound divergence discovered late, after several products migrated | Oracle vector mismatch outside declared tolerance | Per-product profiles and tolerances captured in P0 before replacement | Preserve the old path or ship an explicit versioned mode (FP-02); never rebaseline a golden to pass |
| R-06 | Replacement is measurably slower or less stable than the JUCE builds | FP-04 benchmarks against the previous release | Optimize against the profile; cap or coalesce work | If a product cannot meet its budget, it does not migrate on that schedule |
| R-07 | The platform becomes a second full-time product and outweighs its savings | P1 extraction effort or regression count against the three already-JUCE-free products exceeds the estimate; or GraphicEQ needs platform changes disproportionate to its size | Narrow scope to the highest-leverage packages (PARAM, HOST, STATE, TEST) | Stop extracting optional packages; keep products self-contained above the narrow core |
| R-08 | In-process coexistence defects appear only in users' installed combinations | FP-09 harness or a field crash implicating another vendor's plugin | Enforce visibility, namespacing and teardown rules in the build from P1 | Not deferrable: any product shipped without FP-09 must be re-released to fix it |
| R-09 | An upstream dependency stalls, breaks or changes license | Pin drift, unmerged patches, unmaintained upstream | Record an exit strategy and patch inventory per dependency (section 13) | Vendor and own the fork with a named maintainer, or replace the dependency; do not follow HEAD to stay current |
| R-10 | Parallel migration of the eight remaining JUCE products starves all of them | More than one product in flight at a time under solo operation (§18.4) | Enforce phase discipline; extraction onto the three already-JUCE-free products first, then GraphicEQ | Pause new migrations until the current phase exits |
| R-11 | Replacement code is derived too directly from JUCE source | Implementations that mirror JUCE structure or naming | Implement from documented behavior and captured oracle vectors; record the derivation basis per primitive | Rewrite the affected primitive; obtain review before distribution (section 13) |
| R-12 | Shipped products outlive the single maintainer's availability | Not a signal but a standing condition: solo operation is the baseline (section 18), so the risk is continuity, not concentration | Registry, archived release binaries, deviations and identity locks stored redundantly and offline; operational record for signing and renewal paths (§18.5) | A product whose release cannot be reproduced from committed artifacts alone is not releasable |
| R-13 | Reference builds rot before oracles are captured, making sound compatibility unmeasurable | A JUCE-era product fails to build on a current toolchain, or a product is already mid-migration | Capture oracles and identities first (§11.1 items 1–3); archive the binaries, not just the source | Where the oracle is already lost, record it and redefine that product's compatibility claim explicitly rather than asserting parity that cannot be tested |
| R-14 | Contracts stay prose and are never enforced, leaving a well-specified platform with unchanged failure modes | Clauses accumulating with no corresponding test; coverage report not shrinking | Machine-checkable annotations (FP-01), generated tests, and the clause-citation rule in section 17 | If the prose-only list is not shrinking phase over phase, stop adding contracts and enforce the existing ones |

Review triggers: reopen the backend decision (section 8) if R-01, R-03 or R-04 fires;
reopen the scope decision (section 4) if R-07 or R-10 fires; reopen a released DSP
profile only through FP-02. Record each firing, the evidence and the decision taken;
a risk that fires and is not recorded is how a strategy quietly becomes a rewrite.

## 17. Governance, traceability and document control

**Reading this document.** Imperative statements are normative. "May", "optional" and
"proposed" mark choices resolved by the decisions of record in section 14; the two
entries still marked `Open` there are the only ones outstanding.
Status, scope and date are in the header; this document is reviewed at every phase gate
in section 11 and at every platform release, and changes are recorded with their date
and rationale rather than silently edited.

**Traceability.** Contracts are cited as section numbers (§7.1–§7.9) and requirement IDs
(FP-01–FP-12); risks as R-nn; decisions of record as D-nn. Every conformance test names the
clause it enforces, every deviation names the clause it departs from, and every exit gate in
section 11 names the clause it proves. A gate that cites nothing is not a gate. Coverage is reported as clauses
with executable tests versus clauses asserted only in prose; the second list is the real
backlog and shrinking it is the measure of platform maturity.

**Portfolio registry.** One artifact — `registry/portfolio.identity.json` (D-14) — records for every
product: product code and status, bundle IDs, VST3 class IDs, AU type/subtype/manufacturer,
CLAP IDs, state roots, the parameter-ID reservation ledger (FP-02) and the released binaries
that define its compatibility baseline. It is append-only for released values, validated in
CI against each product's `identity.lock.json`, and consulted by the generator to reject
collisions (FP-03). Losing this file must not be possible; it is the one artifact that cannot
be reconstructed from source alone.

**Deviations and exceptions.** A deviation records the clause, the reason, the user-visible
impact, the approver and a review date. Deviations are reviewed at each release and either
resolved, renewed with a new date, or promoted into the specification as a deliberate change.
An expired, unreviewed deviation blocks the release; accumulating them silently is how a
compatibility promise decays into folklore.

**Evidence.** Oracle vectors, fixtures, benchmark artifacts, fuzzing corpora, SBOMs and
release manifests are stored with their revision and content hash, retained for as long as
the release they qualify is supported. They live in `harness/` in the platform repository,
with large binary artifacts referenced by hash from the hosted server and mirrored offline
alongside the registry (§18.5); a fixture that cannot be located is not evidence.

**Terms.** *Profile* — a declared, versioned behavior specification (DSP, performance,
numeric) that state can reference. *Oracle* — captured output from an authorized reference
build used as a comparison baseline. *Starter* — a maintained, released-quality example
product that qualifies a capability profile (FP-03). *Capability* — a named optional feature
selected in the manifest and negotiated at runtime (FP-08). *Deviation* — an approved,
recorded departure from this specification. *Quirk* — a recorded host-specific deviation and
its narrowest workaround (§7.1). *Publication* — the protocol by which a prepared immutable
resource becomes visible to the audio domain (§7.6). *Revision/generation* — the identity
that lets stale work be discarded safely. *Tier* — a declared determinism level, T1–T3 (FP-10).
*Comparison class* — the declared strictness of a sound-compatibility check, E/A/B/C/D (FP-02).
*Segmented null depth* — the worst windowed difference-to-reference ratio, and the metric a
sound-compatibility claim is normally decided by. *Control block* — the anchored sub-block
granularity at which modulated coefficients are re-evaluated (§7.9.2).

## 18. Single-maintainer operating model

This document was written in the vocabulary of a team — owners, reviewers, sign-off.
Section 14 established that there is one maintainer. That does not weaken the
contracts; it changes what enforces them. With a team, review catches drift. With one
person, only automation catches drift, because the reviewer and the author are the
same person on the same day with the same blind spot. Every rule below follows from
that single fact.

### 18.1 Substitutions

| Where this document says | Read instead |
|---|---|
| Owner (module catalogue, section 6) | The area of the codebase this maintainer is responsible for keeping coherent; a label for grouping work and tests, not a person |
| "Name a second reviewer" | A required automated check that fails the build |
| Approval / sign-off | A recorded, dated, expiring deviation (section 17) |
| Phase gate review | A checklist run whose artifacts are committed; the gate passes when the artifacts exist and pass, not when someone declares it passed |
| Shared QA | The validation matrix in section 12, automated where possible and manually signed where not (D-17) |

### 18.2 What must be automated because no one else will check it

A single maintainer's failure mode is not writing bad code; it is silently skipping a
step under time pressure and not noticing for six months. These checks therefore run
without being invoked, and block:

- Domain annotations and the generated lifecycle tests (FP-01) — the substitute for
  a reviewer asking "can this be called from the audio thread?"
- Identity and state fixtures (FP-02, D-01) — the substitute for remembering which
  products shipped.
- Registry validation against every `identity.lock.json` (D-14).
- Manifest and profile schema validation, including that every declared clause in
  `dsp.profile.json` and `performance.profile.json` is present and typed (§7.9.10).
- Deviation expiry (section 17) — an expired deviation fails the release job. This is
  the mechanism that makes solo sign-off honest.
- Dependency pin verification by hash and SBOM generation (FP-11).
- Clause-citation coverage: the count of contract clauses with executable tests versus
  prose-only, reported on every run. If that number is not improving, R-14 is firing.

### 18.3 CI tiering

Runner minutes are a real constraint for one person paying for them. Three tiers:

| Tier | Trigger | Contents | Platforms |
|---|---|---|---|
| Fast | Every push | Build, unit tests, schema and registry validation, sanitizers | Linux only |
| Full | Merge to the default branch, and nightly if anything changed | All packages, oracle replay, format validators, coexistence harness | Linux + macOS + Windows |
| Release | Tag | Full, plus benchmarks on reference hardware, notarization, signing, SBOM, deviation-expiry check | All |

Benchmarks (D-10) do not run on shared CI runners — their timing is too noisy to
detect the regressions that matter. They run on the maintainer's pinned reference
machines at the release tier, and their artifacts are committed.

Two things run on a calendar rather than on a trigger, because nothing else will
prompt them: a **monthly** dependency review against the pinned hashes and published
advisories (D-13, FP-11), and a **quarterly** rebuild of every maintained starter
against current toolchains and OS versions, which is the early-warning system for
R-13-style rot in the platform's own code.

### 18.4 Scope discipline

Solo, the dominant risks in section 16 are R-07 (the platform becomes the product)
and R-10 (parallel migration starves everything). Enforce:

- **One product in flight at a time** (R-10), including during P1's extraction.
- **A package is extracted when a second product needs it**, never in anticipation.
  Optional packages in section 6 stay unbuilt until a real consumer exists.
- **`never-shipped` products are re-releases, not migrations** (§14.1). They skip the
  entire FP-02 oracle apparatus. Do this classification before committing to a
  schedule, because it may remove most of the work.
- **The starter count is a budget.** FP-03's five maintained starters are five
  products that must keep building forever. If maintaining them competes with
  shipping, reduce the count and record the deviation rather than letting them rot.

The eleven-product framing overstates the work. Three products — ATS Resynth,
CompressedFeedback and CFSynth — are already JUCE-free (section 11), so the real
backlog is eight products in seven codebases, and the first phase is an *extraction*
from working JUCE-free code rather than a migration. That is a materially better
starting position than the document's own risk register assumes.

The defensible solo commitment is therefore:

1. **M0 through M3** (§19.3–§19.6) — bootstrap, spine, editor, generator. This is the
   framework itself, and it is worth building even if no migration follows: it ends
   with new products being cheap to create, which is the stated goal. M3's measured
   time-to-new-plugin is the number that justifies or condemns the whole project.
2. **M4, the three already-JUCE-free products** — CF, CFSynth and ATS Resynth
   re-hosted with no regression. Bounded, and it consolidates three products that
   currently duplicate the same solved problems.
3. **M5, GraphicEQ**, as the honest test of reuse. It is the smallest remaining JUCE
   codebase and still has real oversampling and latency behavior.
4. **Re-decide the remaining seven after that**, with measured effort in hand, per
   R-07's review trigger — plus any product §14.1 clears as `never-shipped`, which
   can be re-released against the platform without the FP-02 oracle apparatus.

Committing to all eight migrations now, before P1 has measured what extraction
actually costs, is the decision R-07 exists to prevent.

### 18.5 Continuity

R-12 in a solo operation is not "knowledge is concentrated" — it is, unavoidably, and
no bus-factor mitigation is available. The addressable risk is that *shipped products
outlive the maintainer's availability*. The mitigations are artifacts, not people:

- The portfolio registry (D-14) and archived release binaries (§14.1) are stored in
  at least two places, one offline.
- Signing identities, notarization credentials and the server's certificate renewal
  path are documented in a sealed operational record — location and recovery
  procedure, not the secrets themselves.
- Every product's `deviations.md` and `identity.lock.json` are sufficient for another
  engineer to reproduce a compatible release without interviewing the author. That is
  the actual test of whether this specification did its job.

## 19. Platform implementation plan

Sections 1–18 say what the platform must be. This section says what to build, in what
order, and how to know each step is finished. It treats the **platform as the product**:
the eight remaining migrations and every future plugin are its consumers, and the
platform's success is measured by how cheaply a *new* product reaches a host, not only
by how faithfully an old one is reproduced.

Milestones are dependency-ordered, not dated. Each states its package set, its
acceptance test, and what it deliberately leaves unbuilt.

| Milestone | Goal | Proves |
|---|---|---|
| M0 — Bootstrap | Repository, toolchain, dependency locks, CI fast tier, feasibility spikes | The build and the risky assumptions, before any platform code exists |
| M1 — Spine | A headless two-parameter effect loads, processes and saves state in real hosts, in all four formats | The core contract stack end to end: HOST, PARAM, STATE, RT |
| M2 — Editor | The same effect gains a real, accessible, host-embedded editor | The UI seam, and that the editor is detachable from the engine |
| M3 — Generator | `generate` produces a new working product from a manifest, with a registered identity | The framework goal: new plugins are cheap. This is the milestone that pays for the project |
| M4 — Consumers | CF, CFSynth and ATS Resynth build on the platform with no regression | Reuse across three real, differently-shaped products |
| M5 — First migration | GraphicEQ ships JUCE-free on the platform | That the platform serves a product it was not extracted from |

M0–M3 are platform construction and are the actual subject of this section. M4 and M5
are section 11's P1 and P2 with their package prerequisites made explicit.

### 19.1 Decisions required before the first commit

These are cheap now and structural later. Record each in `deps/` with revision and
content hash (FP-11); the defaults are the platform position unless a spike in M0
contradicts them.

| Item | Default | Rationale | Blocks |
|---|---|---|---|
| C++ standard | C++20 | `std::span` for RT's non-owning views, concepts for the contract annotations, designated initializers for profile structs | Everything |
| Library-feature constraint | No `std::format`, no `std::chrono` time zones, no C++20 library feature with a dylib availability floor | macOS 11 deployment target (D-02) — these fail at link or runtime on older systems, and the failure is not visible on the development machine | BASE, all |
| CMake | 3.24 minimum | `FetchContent` with `OVERRIDE_FIND_PACKAGE`, dependency-provider hooks used by the pinned-fetch model (§10.1) | BUILD |
| Compilers | Apple Clang 14+, MSVC 19.36+ (VS 2022), Clang 16+ on the Linux CI tier | Three compilers from day one; a two-compiler platform acquires portability defects invisibly | BUILD |
| Warnings | Warnings-as-errors at a high level on all three; no per-file suppressions without a comment naming the reason | Solo operation has no reviewer to catch a warning (§18.2) | BUILD |
| Test framework | doctest, behind a thin harness header | Compile speed dominates a single maintainer's iteration loop; the harness wrapper keeps it swappable | TEST |
| JSON | One vetted library behind a SEC-owned wrapper that enforces depth, size and allocation caps | FP-11 requires bounded parsing of untrusted state; the caps belong to SEC, not to the library choice, so the library stays replaceable | STATE, SEC |
| CLAP SDK | Pinned by tag and hash | — | HOST, adapters |
| clap-wrapper | Pinned by revision and hash, with `deps/patches/` created empty at M0 | D-03 and R-01 anticipate an ID-override patch; the patch directory must pre-exist or the first patch gets committed somewhere ad hoc | adapters |
| UI backend | Visage, pinned; confirmed at M2 after the M0 probe | R-04 makes this near-irreversible after products adopt it | DRAW, UI |
| Audio codec | Deferred to M4 | Not needed before ASSETS has a consumer (§14.4) | ASSETS |

### 19.2 Package dependency order

Dependencies point strictly downward. The graph is acyclic and CI enforces it, because
a cycle between PARAM and UI, or between HOST and DSP, is the specific failure that
makes a framework untestable and unextractable later.

| Tier | Packages | Depends on |
|---|---|---|
| 0 | BUILD, TEST | Nothing; infrastructure |
| 1 | BASE | — |
| 2 | RT, SEC, OS | BASE |
| 3 | PARAM, JOBS, FILES | BASE; JOBS also RT; FILES also SEC, OS |
| 4 | STATE, MIDI | STATE: BASE, SEC, FILES, PARAM. MIDI: BASE, RT |
| 5 | HOST | BASE, RT, PARAM, STATE, JOBS, MIDI |
| 6 | `adapters/clap`, then `adapters/{vst3,auv2,standalone}` | HOST; the wrapper adapters also clap-wrapper |
| 7 | DRAW, DSP | DRAW: BASE, OS. DSP: BASE, RT |
| 8 | UI | DRAW, PARAM, JOBS, OS, BASE |
| 9 | EDIT, ASSETS, VOICE, NET, DEVICES, GPU | Optional; see section 6 |

Three structural rules, each enforced by the dependency-boundary test rather than by
inspection:

- **UI must never depend on HOST.** The editor binds to PARAM and a product controller
  interface, never to the plugin instance. This is what makes the editor testable
  headless and genuinely detachable (FP-01: closing the editor must not touch an active
  engine).
- **DSP must never depend on HOST or PARAM.** DSP primitives take plain values and
  sample rates. A DSP package that knows about parameters cannot be unit-tested against
  an oracle, which defeats FP-02.
- **Nothing depends on an adapter.** Adapters are leaves and the only targets that
  include a format SDK (§10.1).

Core packages, linked by every product: BASE, RT, HOST, PARAM, STATE, SEC, BUILD, TEST.
Everything else is selected by the product manifest and defaults to off.

### 19.3 M0 — Bootstrap

Build no platform code. Produce the repository shell from §10.1, the locks from §19.1,
and answers to the questions that would invalidate the design.

Contents of the initial commits:

1. The §10.1 directory skeleton, with `deps/`, `registry/` and `deps/patches/` present
   even where empty.
2. Root CMake defining the C++ standard, the three-compiler warning configuration,
   default-hidden symbol visibility, the version-tagged inline namespace macro and the
   package-target/alias helper — the §5.1 conventions expressed as build code rather
   than as prose, so a package cannot opt out by omission.
3. `registry/portfolio.identity.json`, seeded from the identity extraction in §11.1
   items 1–3, plus its CI validator.
4. The CI fast tier (§18.3): configure, build, unit tests, sanitizers, schema and
   registry validation, dependency-graph acyclicity, on Linux only.
5. `SECURITY.md` with the D-13 timelines.

M0 spikes, each a throwaway program in `harness/spikes/`, kept only for its report:

| Spike | Question | Fires |
|---|---|---|
| Accessibility bridge | Does Visage expose role, name, value and value-change through VoiceOver and UI Automation, or must the platform build that bridge? | R-04, D-11 |
| Host embedding | Does a Visage surface embed, resize and handle DPI change correctly under Reaper and Logic? | R-04 |
| Wrapper ID override | Can clap-wrapper emit chosen VST3 class IDs and AU codes without a substantial fork? | R-01, D-03 |
| Coexistence | Do two builds of the same platform, differing only in the version-tagged namespace, load in one host process without symbol interference? | R-08, FP-09 |

**M0 is done when** the fast tier is green on an empty platform, the four spike reports
are written, and D-03 and D-16 in section 14 are closed.

### 19.4 M1 — The spine

The single most important milestone: it proves the entire contract stack against real
hosts while the amount of code is still small enough to change freely.

Build `starters/minimal`: a stereo effect with exactly two parameters — gain in dB and
a bypass-style mute — and **no editor**. Packages: BASE, RT, PARAM, STATE, HOST, SEC,
plus BUILD, TEST and all four adapters. Do not build JOBS, UI, DRAW, MIDI or DSP.

M1 acceptance, all automated except the host rows:

| Check | Requirement |
|---|---|
| Format validation | `clap-validator`, Steinberg `validator` and `auval` pass with no warnings that are not recorded deviations |
| Host load | Loads, processes and unloads in Reaper on macOS and Windows, and in Logic on macOS |
| State round-trip | Host session save/reload restores both parameters exactly; comparison class E (§14.6) |
| Identity | Declared IDs in `identity.lock.json` match what each format actually exposes, verified by reading the built binary rather than the source |
| Parameter ledger | Removing or renumbering a parameter fails CI against the registry (D-14) |
| RT cleanliness | 10,000 process calls with no allocation, free, lock or blocking call, verified by an instrumented allocator, not by inspection |
| Domain assertions | Debug build asserts on the first FP-01 domain violation; a deliberately wrong call in a test triggers it |
| Determinism | Two identical offline renders are bit-identical on one machine (T1, D-12) |
| Coexistence | Two differently-versioned builds of `starters/minimal` load and process in one host process (FP-09) |
| Malformed state | A corpus of truncated and hostile state blobs cannot crash, hang or partially apply (FP-11) |

**M1 is done when** every row passes on macOS and Windows. Nothing further is built
until it does — a defect in HOST or STATE found at M4 costs far more than one found here.

### 19.5 M2 — The editor seam

Add DRAW, UI and JOBS. Give `starters/minimal` an editor with one slider bound to gain
and one toggle bound to mute, and nothing else.

The point is not the widgets. It is that the seam holds: the editor opens and closes
repeatedly while audio runs; two instances have independent editors; the engine survives
editor destruction; §5.2's single reference-counted worker pool per platform copy is
created on first activation and torn down with the last user, verified by a test that
counts threads. Accessibility (D-11) and typed numeric entry are part of this milestone,
not a later pass — retrofitting them after the widget set exists is the expensive order.

**M2 is done when** the M1 checks still pass, the editor rows of section 12 pass on both
OSes, the accessibility baseline passes under VoiceOver and UI Automation, and the
thread-count test shows one pool regardless of instance count.

### 19.6 M3 — The generator

This is the milestone that makes the framework worth building, and it is the one most
likely to be skipped under pressure. It converts the platform from "code CF happens to
share" into something a new product can be built on.

Deliver: the manifest and profile schemas as validating code; `generator/` producing a
complete buildable product directory from `product.manifest.json`; identity allocation
that reserves IDs in the registry and refuses collisions; and a second starter — a
minimal instrument, which forces MIDI input, note handling and the instrument identity
path that an effect never exercises.

**M3 is done when**, from a clean checkout, generating a new product from a manifest and
building it to a host-loadable, validator-passing plugin in all four formats takes one
command and under an hour of human time, with its identity registered and its
`dsp.profile.json` and `performance.profile.json` present as failing skeletons that block
release until filled. Measure and record that time; it is the platform's headline metric
and the number that R-07 is judged against.

### 19.7 M4 and M5 — Consumers and the first migration

M4 re-hosts CF, CFSynth and ATS Resynth on the platform. It adds the packages those
products actually need — DSP conventions (§7.9), EDIT and ASSETS for ATS, the codec
decision deferred from §19.1 — and nothing they do not. Each product's compatibility
obligation follows from its §14.1 classification, and the rule from §18.4 holds: one
product in flight at a time.

M5 migrates GraphicEQ. Its value is diagnostic: it is the first product the platform was
not extracted from, so every platform change it forces is a measurement of how much of
M1–M3 was actually general. Record those changes — they are the evidence for the R-07
review that decides the remaining seven products.

### 19.8 Definition of done for a package

Applies to every package in §19.2, and is the substitute for a reviewer (§18.1).

1. One CMake target and one alias, with correct `PUBLIC`/`PRIVATE` usage requirements
   and no transitive leakage of a private dependency's headers.
2. It configures, builds and passes its tests in an isolated build containing only its
   declared dependencies — verified by a CI job, since an undeclared dependency is
   invisible in a full build.
3. Every public entry point carries its FP-01 execution-domain annotation. An
   unannotated entry point generates no lifecycle tests and fails the package's own
   conformance build.
4. Unit and contract tests exist, and each names the clause it enforces (section 17).
5. Any function annotated audio-domain passes the allocation/lock/blocking check.
6. It compiles clean at the project warning level on all three compilers.
7. Public headers document ownership, lifetime and failure behavior per section 7.
8. Its clauses appear in the coverage report as tested rather than prose-only (R-14).

### 19.9 Not built yet, deliberately

Building these before a consumer exists is how R-07 fires. Each is designed in section 6
and implemented when a real product selects it: VOICE (needs Heart or Hydro), GPU (needs
Heart, and may never be built per D-04), NET and DEVICES beyond standalone basics (need
MIDI Collab), ASSETS streaming (only for products declaring it), and starters three
through five of FP-03's set (each is a permanent maintenance obligation, §18.4).

The rule is the same throughout: a package is extracted when the second consumer needs
it, never in anticipation of the first.
