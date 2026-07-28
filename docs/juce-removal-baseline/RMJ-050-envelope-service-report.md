# RMJ-050 spectral envelope service

`SpectralEnvelopeService` names the JUCE-free envelope boundary used by the
morphing and frequency-shape paths. It delegates to the existing deterministic
Bark envelope and transfer implementation without importing host, UI, or
framework types. The focused service test covers envelope construction,
finite sampling, Bark conversion, and transfer output.

The broader morph-grid numerical matrix and audio goldens remain required for
promotion beyond `mapped`.
