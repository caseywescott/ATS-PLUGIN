find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_executable(ats_modern_model_test ats_modern_model_test.cpp ../src/core/AtsModernModel.cpp)
target_include_directories(ats_modern_model_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(ats_modern_model_test PRIVATE cxx_std_17)

add_executable(
  ats_modern_synth_test
  ats_modern_synth_test.cpp
  ../src/core/AtsModernModel.cpp
  ../src/core/AtsModernSynth.cpp)
target_include_directories(ats_modern_synth_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(ats_modern_synth_test PRIVATE cxx_std_17)

add_executable(
  ats_modern_morph_test
  ats_modern_morph_test.cpp
  ../src/core/AtsModernModel.cpp
  ../src/core/AtsModernMorph.cpp
  ../src/core/AtsModernSynth.cpp)
target_include_directories(ats_modern_morph_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(ats_modern_morph_test PRIVATE cxx_std_17)

add_executable(
  ats_analysis_pipeline_test
  ats_analysis_pipeline_test.cpp
  ../src/core/AtsAnalysisPipeline.cpp
  ../src/core/AtsModel.cpp
  ../src/core/AtsModernModel.cpp
  ../src/core/AtsParams.cpp)
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
  ../src/core/AtsParams.cpp)
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
  NAME classic_compat_baseline
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/classic_compat_baseline.py"
          "$<TARGET_FILE:ats_offline>" "${CMAKE_BINARY_DIR}"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME ats_modern_model COMMAND $<TARGET_FILE:ats_modern_model_test>)
add_test(NAME ats_analysis_pipeline COMMAND $<TARGET_FILE:ats_analysis_pipeline_test>)
add_test(NAME ats_modern_synth COMMAND $<TARGET_FILE:ats_modern_synth_test>)
add_test(NAME ats_modern_morph COMMAND $<TARGET_FILE:ats_modern_morph_test>)
add_test(NAME ats_modern_acceptance COMMAND $<TARGET_FILE:ats_modern_acceptance_test>)
