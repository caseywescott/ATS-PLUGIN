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

add_test(NAME ats_roundtrip COMMAND $<TARGET_FILE:ats_roundtrip> WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
