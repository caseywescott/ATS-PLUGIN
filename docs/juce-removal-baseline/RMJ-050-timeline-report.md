# RMJ-050 morph timeline contract

`MorphTimelineService` extracts normalized point sanitization, three-lane
evaluation, curve shaping, protected Morph endpoints, point editing, and
constant-endpoint detection. `MorphRenderService` evaluates the same lanes
into a generation-tagged, cancellation-aware render plan; it intentionally
does not claim WAV output or native preview integration.

The focused test covers endpoint and curve behavior, lane evaluation, edit
protection, cancellation, and deterministic plan sizing. Audio rendering,
preview parity, bounce I/O, and live popup/undo dispatch remain separate gates.
