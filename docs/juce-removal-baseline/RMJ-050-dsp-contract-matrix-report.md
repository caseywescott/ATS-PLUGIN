# RMJ-050 DSP contract matrix

The framework-free DSP services now have one executable contract matrix at
`tests/dsp/dsp_contract_matrix_test.cpp`. It exercises resampling, frequency
shaping, timbre-plan serialization/evaluation, Bark/Mel grids, recipe
serialization, deterministic morph evaluation, phase policy, residual and
transient policy, spectral-envelope transfer, correspondence strategies, and
MathExpr error handling through the neutral service headers.

Evidence:

```text
cmake -S . -B build-review -DBUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-review --target dsp_contract_matrix_test -j2
./build-review/tests/dsp_contract_matrix_test
```

The matrix is deliberately separate from the native plugin target. It proves
the algorithm/service boundary without implying that the still-blocked native
JUCE-free window backend or VST3/AU wrapper has been selected.
