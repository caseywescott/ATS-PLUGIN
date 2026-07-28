find_package(Python3 COMPONENTS Interpreter REQUIRED)

# These tests use assertions as executable contract checks. Keep them active
# even though the repository defaults the overall build to Release.
add_compile_options(-UNDEBUG)

add_executable(ats_primitives_test ats_primitives_test.cpp)
target_include_directories(ats_primitives_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(ats_primitives_test PRIVATE cxx_std_17)
add_test(NAME ats_primitives_test COMMAND ats_primitives_test)

add_executable(parameter_registry_test parameter_registry_test.cpp ../src/core/ParameterRegistry.cpp)
target_include_directories(parameter_registry_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(parameter_registry_test PRIVATE cxx_std_17)
add_test(NAME parameter_registry_test COMMAND parameter_registry_test)

add_executable(parameter_binding_dispatch_test parameter_binding_dispatch_test.cpp ../src/core/ParameterRegistry.cpp)
target_include_directories(parameter_binding_dispatch_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/ui")
target_compile_features(parameter_binding_dispatch_test PRIVATE cxx_std_17)
add_test(NAME parameter_binding_dispatch_test COMMAND parameter_binding_dispatch_test)

add_executable(playback_buffer_test playback_buffer_test.cpp)
target_include_directories(playback_buffer_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(playback_buffer_test PRIVATE cxx_std_17)
add_test(NAME playback_buffer_test COMMAND playback_buffer_test)

add_executable(midi_note_router_test midi_note_router_test.cpp)
target_include_directories(midi_note_router_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(midi_note_router_test PRIVATE cxx_std_17)
add_test(NAME midi_note_router_test COMMAND midi_note_router_test)

add_executable(undo_history_test undo_history_test.cpp)
target_include_directories(undo_history_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(undo_history_test PRIVATE cxx_std_17)
add_test(NAME undo_history_test COMMAND undo_history_test)

add_executable(state_header_test state_header_test.cpp)
target_include_directories(state_header_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(state_header_test PRIVATE cxx_std_17)
add_test(NAME state_header_test COMMAND state_header_test)

add_executable(job_scheduler_test job_scheduler_test.cpp)
target_include_directories(job_scheduler_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(job_scheduler_test PRIVATE cxx_std_17)
add_test(NAME job_scheduler_test COMMAND job_scheduler_test)

add_executable(ui_dispatcher_test ui_dispatcher_test.cpp)
target_include_directories(ui_dispatcher_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core" "${CMAKE_CURRENT_SOURCE_DIR}/../src/ui")
target_compile_features(ui_dispatcher_test PRIVATE cxx_std_17)
add_test(NAME ui_dispatcher_test COMMAND ui_dispatcher_test)

add_executable(ui_action_dispatcher_test ui_action_dispatcher_test.cpp)
target_include_directories(ui_action_dispatcher_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core" "${CMAKE_CURRENT_SOURCE_DIR}/../src/ui")
target_compile_features(ui_action_dispatcher_test PRIVATE cxx_std_17)
add_test(NAME ui_action_dispatcher_test COMMAND ui_action_dispatcher_test)

add_executable(plugin_model_test plugin_model_test.cpp)
target_include_directories(plugin_model_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/ui")
target_compile_features(plugin_model_test PRIVATE cxx_std_17)
target_link_libraries(plugin_model_test PRIVATE ats_plugin_model)
add_test(NAME plugin_model_test COMMAND plugin_model_test)

add_executable(ui_backend_contract_test ui_backend_contract_test.cpp)
target_include_directories(ui_backend_contract_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/ui")
target_compile_features(ui_backend_contract_test PRIVATE cxx_std_17)
add_test(NAME ui_backend_contract_test COMMAND ui_backend_contract_test)

add_executable(dsp_service_contract_test dsp_service_contract_test.cpp)
target_include_directories(dsp_service_contract_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/dsp"
  "${ATS_ATSA_SOURCE_DIR}")
target_compile_features(dsp_service_contract_test PRIVATE cxx_std_17)
target_link_libraries(dsp_service_contract_test PRIVATE ats_engine)
add_test(NAME dsp_service_contract_test COMMAND dsp_service_contract_test)

add_executable(spectral_envelope_service_test spectral_envelope_service_test.cpp
  ../src/core/AtsSpectralEnvelope.cpp)
target_include_directories(spectral_envelope_service_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/dsp")
target_compile_features(spectral_envelope_service_test PRIVATE cxx_std_17)
add_test(NAME spectral_envelope_service_test COMMAND spectral_envelope_service_test)

add_executable(telemetry_publishers_test telemetry_publishers_test.cpp)
target_include_directories(telemetry_publishers_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/audio")
target_compile_features(telemetry_publishers_test PRIVATE cxx_std_17)
add_test(NAME telemetry_publishers_test COMMAND telemetry_publishers_test)

add_executable(snapshot_publication_test snapshot_publication_test.cpp)
target_include_directories(snapshot_publication_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/audio"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine")
target_compile_features(snapshot_publication_test PRIVATE cxx_std_17)
add_test(NAME snapshot_publication_test COMMAND snapshot_publication_test)

add_executable(ui_scheduler_test ui_scheduler_test.cpp)
target_include_directories(ui_scheduler_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/ui")
target_compile_features(ui_scheduler_test PRIVATE cxx_std_17)
add_test(NAME ui_scheduler_test COMMAND ui_scheduler_test)

add_executable(ats_engine_test ats_engine_test.cpp)
target_include_directories(ats_engine_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core" "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine" "${ATS_ATSA_SOURCE_DIR}")
target_compile_features(ats_engine_test PRIVATE cxx_std_17)
target_link_libraries(ats_engine_test PRIVATE ats_engine)
add_test(NAME ats_engine_test COMMAND ats_engine_test WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(playback_engine_test playback_engine_test.cpp)
target_include_directories(playback_engine_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine")
target_compile_features(playback_engine_test PRIVATE cxx_std_17)
target_link_libraries(playback_engine_test PRIVATE ats_playback)
add_test(NAME playback_engine_test COMMAND playback_engine_test)

add_executable(legacy_state_codec_test legacy_state_codec_test.cpp)
target_include_directories(legacy_state_codec_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core" "${CMAKE_CURRENT_SOURCE_DIR}/../src/state" "${ATS_ATSA_SOURCE_DIR}")
target_compile_features(legacy_state_codec_test PRIVATE cxx_std_17)
target_link_libraries(legacy_state_codec_test PRIVATE ats_state)
add_test(NAME legacy_state_codec_test COMMAND legacy_state_codec_test)

add_executable(file_service_preset_test file_service_preset_test.cpp)
target_include_directories(file_service_preset_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/platform"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/state")
target_compile_features(file_service_preset_test PRIVATE cxx_std_17)
target_link_libraries(file_service_preset_test PRIVATE ats_preset_service)
add_test(NAME file_service_preset_test COMMAND file_service_preset_test)

add_executable(playback_cache_generation_test playback_cache_generation_test.cpp)
target_include_directories(playback_cache_generation_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/audio"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/engine")
target_compile_features(playback_cache_generation_test PRIVATE cxx_std_17)
add_test(NAME playback_cache_generation_test COMMAND playback_cache_generation_test)

add_executable(host_lifecycle_test host_lifecycle_test.cpp ../src/core/ParameterRegistry.cpp)
target_include_directories(host_lifecycle_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/host")
target_compile_features(host_lifecycle_test PRIVATE cxx_std_17)
add_test(NAME host_lifecycle_test COMMAND host_lifecycle_test)

add_executable(morph_recipe_test morph_recipe_test.cpp ../src/core/AtsMorphRecipe.cpp)
target_include_directories(morph_recipe_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(morph_recipe_test PRIVATE cxx_std_17)

add_executable(morph_transport_test morph_transport_test.cpp ../src/core/AtsMorphTransport.cpp)
target_include_directories(morph_transport_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(morph_transport_test PRIVATE cxx_std_17)

add_executable(morph_transport_correspondence_test morph_transport_correspondence_test.cpp
  ../src/core/AtsMorphTransportCorrespondence.cpp
  ../src/core/AtsMorphTransport.cpp)
target_include_directories(morph_transport_correspondence_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(morph_transport_correspondence_test PRIVATE cxx_std_17)

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

add_executable(ats_math_expr_test ats_math_expr_test.cpp ../src/core/AtsMathExpr.cpp)
target_include_directories(ats_math_expr_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${ATS_ATSA_SOURCE_DIR}")
target_link_libraries(ats_math_expr_test PRIVATE atsa)
target_compile_features(ats_math_expr_test PRIVATE cxx_std_17)

add_executable(timbre_motion_plan_test timbre_motion_plan_test.cpp
  ../src/core/TimbreMotionPlan.cpp)
target_include_directories(timbre_motion_plan_test PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_compile_features(timbre_motion_plan_test PRIVATE cxx_std_17)

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

add_test(
  NAME ats_traceability_manifest
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/architecture/traceability_manifest_check.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/architecture/ats_juce_replacement_traceability.json"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_test(
  NAME ats_qatsh_patch_manifest
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/architecture/qatsh_patch_manifest_check.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/architecture/qatsh_patch_manifest.json"
          "${ATS_ATSA_SOURCE_DIR}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_test(
  NAME ats_ui_parity_manifest
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/ui/ui_parity_manifest_check.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/ui/ats_ui_parity_manifest.json"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_test(NAME ats_roundtrip COMMAND $<TARGET_FILE:ats_roundtrip> WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
add_test(NAME atsa_residual_test COMMAND $<TARGET_FILE:atsa_residual_test> WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
add_test(NAME estimator_accuracy_test COMMAND $<TARGET_FILE:estimator_accuracy_test> WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
add_test(NAME multires_resolution_test COMMAND $<TARGET_FILE:multires_resolution_test> WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
add_test(NAME ifft_synth_test COMMAND $<TARGET_FILE:ifft_synth_test> WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(NAME morph_transport_test COMMAND $<TARGET_FILE:morph_transport_test>)
add_test(NAME morph_transport_correspondence_test COMMAND $<TARGET_FILE:morph_transport_correspondence_test>)
add_test(NAME morph_recipe_roundtrip COMMAND $<TARGET_FILE:morph_recipe_test>)
add_test(NAME frequency_shape_test COMMAND $<TARGET_FILE:frequency_shape_test>)
add_test(NAME spectral_envelope_transfer_test COMMAND $<TARGET_FILE:spectral_envelope_transfer_test>)
add_test(NAME ats_transient_smear_test COMMAND $<TARGET_FILE:ats_transient_smear_test>)
add_test(NAME ats_transient_smear_accuracy_test COMMAND $<TARGET_FILE:ats_transient_smear_accuracy_test>)
add_test(NAME ats_transient_layer_test COMMAND $<TARGET_FILE:ats_transient_layer_test>)
add_test(NAME ats_math_expr_test COMMAND $<TARGET_FILE:ats_math_expr_test>)
add_test(NAME timbre_motion_plan_test COMMAND $<TARGET_FILE:timbre_motion_plan_test>)
