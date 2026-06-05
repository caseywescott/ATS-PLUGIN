find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_executable(morph_recipe_test morph_recipe_test.cpp ../src/core/AtsMorphRecipe.cpp)
target_include_directories(morph_recipe_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(morph_recipe_test PRIVATE cxx_std_17)

add_executable(frequency_shape_test frequency_shape_test.cpp ../src/core/AtsFrequencyShape.cpp)
target_include_directories(frequency_shape_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${ATS_ATSA_SOURCE_DIR}")
target_compile_features(frequency_shape_test PRIVATE cxx_std_17)

add_executable(spectral_envelope_transfer_test spectral_envelope_transfer_test.cpp
  ../src/core/AtsSpectralEnvelope.cpp)
target_include_directories(spectral_envelope_transfer_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(spectral_envelope_transfer_test PRIVATE cxx_std_17)

add_executable(ats_transient_smear_test ats_transient_smear_test.cpp
  ../src/core/AtsTransientSmear.cpp)
target_include_directories(ats_transient_smear_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${ATS_ATSA_SOURCE_DIR}")
target_compile_features(ats_transient_smear_test PRIVATE cxx_std_17)

add_executable(ats_transient_smear_accuracy_test ats_transient_smear_accuracy_test.cpp
  ../src/core/AtsTransientSmear.cpp)
target_include_directories(ats_transient_smear_accuracy_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${ATS_ATSA_SOURCE_DIR}")
target_compile_features(ats_transient_smear_accuracy_test PRIVATE cxx_std_17)

add_executable(ats_transient_layer_test ats_transient_layer_test.cpp
  ../src/core/AtsTransientLayer.cpp
  ../src/core/AtsTransientSmear.cpp)
target_include_directories(ats_transient_layer_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${ATS_ATSA_SOURCE_DIR}")
target_compile_features(ats_transient_layer_test PRIVATE cxx_std_17)

add_executable(atsa_residual_test atsa_residual_test.c)
target_link_libraries(atsa_residual_test PRIVATE atsa)
if(UNIX AND NOT APPLE)
  target_link_libraries(atsa_residual_test PRIVATE m)
endif()
if(MSVC)
  target_compile_definitions(atsa_residual_test PRIVATE _USE_MATH_DEFINES)
endif()

add_executable(estimator_accuracy_test estimator_accuracy_test.c)
target_link_libraries(estimator_accuracy_test PRIVATE atsa)
if(UNIX AND NOT APPLE)
  target_link_libraries(estimator_accuracy_test PRIVATE m)
endif()
if(MSVC)
  target_compile_definitions(estimator_accuracy_test PRIVATE _USE_MATH_DEFINES)
endif()

add_executable(multires_resolution_test multires_resolution_test.c)
target_link_libraries(multires_resolution_test PRIVATE atsa)
if(UNIX AND NOT APPLE)
  target_link_libraries(multires_resolution_test PRIVATE m)
endif()
if(MSVC)
  target_compile_definitions(multires_resolution_test PRIVATE _USE_MATH_DEFINES)
endif()

add_executable(ifft_synth_test ifft_synth_test.c)
target_link_libraries(ifft_synth_test PRIVATE atsa)
if(UNIX AND NOT APPLE)
  target_link_libraries(ifft_synth_test PRIVATE m)
endif()
if(MSVC)
  target_compile_definitions(ifft_synth_test PRIVATE _USE_MATH_DEFINES)
endif()

add_test(
  NAME golden_offline
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/golden_compare.py"
          "$<TARGET_FILE:ats_offline>" "${CMAKE_BINARY_DIR}"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(
  NAME morph_fixture_manifest
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/morph_fixture_manifest_check.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/morph_fixture_manifest.json"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(
  NAME morph_metric_harness
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/morph_metric_harness.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/morph_metric_tolerances.json"
          "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/morph_metric_smoke_report.json"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME ats_roundtrip COMMAND $<TARGET_FILE:ats_roundtrip> WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
add_test(NAME atsa_residual_test COMMAND $<TARGET_FILE:atsa_residual_test> WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
add_test(NAME estimator_accuracy_test COMMAND $<TARGET_FILE:estimator_accuracy_test> WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
add_test(NAME multires_resolution_test COMMAND $<TARGET_FILE:multires_resolution_test> WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
add_test(NAME ifft_synth_test COMMAND $<TARGET_FILE:ifft_synth_test> WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME morph_recipe_roundtrip COMMAND $<TARGET_FILE:morph_recipe_test>)
add_test(NAME frequency_shape_test COMMAND $<TARGET_FILE:frequency_shape_test>)
add_test(NAME spectral_envelope_transfer_test COMMAND $<TARGET_FILE:spectral_envelope_transfer_test>)
add_test(NAME ats_transient_smear_test COMMAND $<TARGET_FILE:ats_transient_smear_test>)
add_test(NAME ats_transient_smear_accuracy_test COMMAND $<TARGET_FILE:ats_transient_smear_accuracy_test>)
add_test(NAME ats_transient_layer_test COMMAND $<TARGET_FILE:ats_transient_layer_test>)
