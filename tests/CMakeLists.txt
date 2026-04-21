find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
  NAME golden_offline
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/golden_compare.py"
          "$<TARGET_FILE:ats_offline>" "${CMAKE_BINARY_DIR}"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(
  NAME spectral_envelope_baselines
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/spectral_envelope_baselines.py")

add_executable(spectral_envelope_model_test
  spectral_envelope_model_test.cpp
  ../src/core/SpectralEnvelope.cpp)
target_include_directories(spectral_envelope_model_test PRIVATE ../src/core)
target_compile_features(spectral_envelope_model_test PRIVATE cxx_std_17)
add_test(NAME spectral_envelope_model_test COMMAND spectral_envelope_model_test)

add_executable(spectral_envelope_estimator_test
  spectral_envelope_estimator_test.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/RegularizedCepstrum.cpp)
target_include_directories(spectral_envelope_estimator_test PRIVATE ../src/core)
target_compile_features(spectral_envelope_estimator_test PRIVATE cxx_std_17)
add_test(NAME spectral_envelope_estimator_test COMMAND spectral_envelope_estimator_test)

add_executable(frequency_warp_test
  frequency_warp_test.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/SpectralEnvelope.cpp)
target_include_directories(frequency_warp_test PRIVATE ../src/core)
target_compile_features(frequency_warp_test PRIVATE cxx_std_17)
add_test(NAME frequency_warp_test COMMAND frequency_warp_test)

add_executable(regularized_cepstrum_test
  regularized_cepstrum_test.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp)
target_include_directories(regularized_cepstrum_test PRIVATE ../src/core)
target_compile_features(regularized_cepstrum_test PRIVATE cxx_std_17)
add_test(NAME regularized_cepstrum_test COMMAND regularized_cepstrum_test)

add_executable(rdc_weighting_test
  rdc_weighting_test.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp)
target_include_directories(rdc_weighting_test PRIVATE ../src/core)
target_compile_features(rdc_weighting_test PRIVATE cxx_std_17)
add_test(NAME rdc_weighting_test COMMAND rdc_weighting_test)

add_test(NAME ats_roundtrip COMMAND $<TARGET_FILE:ats_roundtrip> WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
