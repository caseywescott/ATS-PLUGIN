find_package(Python3 COMPONENTS Interpreter REQUIRED)

option(ATS_ENABLE_EXTERNAL_FIXTURES
  "Enable the external real-audio acceptance gate (requires audio assets and ATSA references). Default OFF to keep ctest deterministic."
  OFF)

add_executable(ats_modern_model_test ats_modern_model_test.cpp ../src/core/AtsModernModel.cpp)
target_include_directories(ats_modern_model_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(ats_modern_model_test PRIVATE cxx_std_17)

add_executable(
  ats_modern_synth_test
  ats_modern_synth_test.cpp
  ../src/core/AtsModernModel.cpp
  ../src/core/AtsModernSynth.cpp
  ../src/core/AtsResidualBandNoise.cpp)
target_include_directories(ats_modern_synth_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(ats_modern_synth_test PRIVATE cxx_std_17)

add_executable(
  ats_modern_morph_test
  ats_modern_morph_test.cpp
  ../src/core/AtsModernModel.cpp
  ../src/core/AtsModernMorph.cpp
  ../src/core/AtsModernSynth.cpp
  ../src/core/AtsResidualBandNoise.cpp)
target_include_directories(ats_modern_morph_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(ats_modern_morph_test PRIVATE cxx_std_17)

add_executable(
  ats_analysis_pipeline_test
  ats_analysis_pipeline_test.cpp
  ../src/core/AtsAnalysisPipeline.cpp
  ../src/core/AtsModel.cpp
  ../src/core/AtsModernModel.cpp
  ../src/core/AtsParams.cpp
  ../src/core/AtsResidualBandNoise.cpp)
target_include_directories(ats_analysis_pipeline_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
                                                              "${ATS_ATSA_SOURCE_DIR}")
target_link_libraries(ats_analysis_pipeline_test PRIVATE atsa)
target_compile_features(ats_analysis_pipeline_test PRIVATE cxx_std_17)

add_executable(
  ats_modern_acceptance_test
  ats_modern_acceptance_test.cpp
  ../src/core/AtsAnalysisPipeline.cpp
  ../src/core/AtsModel.cpp
  ../src/core/AtsModernAcceptance.cpp
  ../src/core/AtsModernModel.cpp
  ../src/core/AtsModernMorph.cpp
  ../src/core/AtsModernSynth.cpp
  ../src/core/AtsParams.cpp
  ../src/core/AtsResidualBandNoise.cpp)
target_include_directories(ats_modern_acceptance_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
                                                              "${ATS_ATSA_SOURCE_DIR}")
target_link_libraries(ats_modern_acceptance_test PRIVATE atsa)
target_compile_features(ats_modern_acceptance_test PRIVATE cxx_std_17)

add_test(
  NAME golden_offline
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/golden_compare.py"
          "$<TARGET_FILE:ats_offline>" "${CMAKE_BINARY_DIR}"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME ats_roundtrip COMMAND $<TARGET_FILE:ats_roundtrip> WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(
  NAME ats_fixture_manifest
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/fixture_manifest_check.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ats_compat_manifest.json")

add_test(
  NAME modern_analysis_source_path
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/modern_analysis_source_path_check.py"
          "${CMAKE_SOURCE_DIR}")

add_test(
  NAME classic_compat_baseline
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/classic_compat_baseline.py"
          "$<TARGET_FILE:ats_offline>" "${CMAKE_BINARY_DIR}"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME ats_modern_model COMMAND $<TARGET_FILE:ats_modern_model_test>)
add_test(NAME ats_analysis_pipeline COMMAND $<TARGET_FILE:ats_analysis_pipeline_test>)
add_test(NAME ats_modern_synth COMMAND $<TARGET_FILE:ats_modern_synth_test>)
add_test(NAME ats_modern_morph COMMAND $<TARGET_FILE:ats_modern_morph_test>)
add_test(NAME ats_modern_acceptance COMMAND $<TARGET_FILE:ats_modern_acceptance_test>)

# ---------------------------------------------------------------------------
# External fixture acceptance gate (Phase 7)
#
# Always build the C++ gate-logic unit test (no audio assets required).
# The Python-driven real-audio CTest is only registered when
# ATS_ENABLE_EXTERNAL_FIXTURES=ON.
# ---------------------------------------------------------------------------

add_executable(
  ats_modern_acceptance_external_test
  ats_modern_acceptance_external_test.cpp
  ../src/core/AtsAnalysisPipeline.cpp
  ../src/core/AtsModel.cpp
  ../src/core/AtsModernAcceptance.cpp
  ../src/core/AtsModernModel.cpp
  ../src/core/AtsModernMorph.cpp
  ../src/core/AtsModernSynth.cpp
  ../src/core/AtsParams.cpp
  ../src/core/AtsResidualBandNoise.cpp)
target_include_directories(ats_modern_acceptance_external_test
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core" "${ATS_ATSA_SOURCE_DIR}")
target_link_libraries(ats_modern_acceptance_external_test PRIVATE atsa)
target_compile_features(ats_modern_acceptance_external_test PRIVATE cxx_std_17)

# Gate-logic unit test — always runs (deterministic, no external assets).
add_test(NAME ats_modern_acceptance_external_gates
  COMMAND $<TARGET_FILE:ats_modern_acceptance_external_test>)

add_test(
  NAME ats_external_acceptance_driver_contract
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/external_acceptance_driver_test.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/run_external_acceptance.py")

if(ATS_ENABLE_EXTERNAL_FIXTURES)
  # Real-audio parity test — only when option is ON and assets may be present.
  add_test(
    NAME ats_modern_acceptance_external
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/run_external_acceptance.py"
            "$<TARGET_FILE:ats_offline>"
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/ats_external_fixture_manifest.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/reference"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
