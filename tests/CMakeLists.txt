find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
  NAME golden_offline
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/golden_compare.py"
          "$<TARGET_FILE:ats_offline>" "${CMAKE_BINARY_DIR}"
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

add_test(
  NAME spectral_envelope_baselines
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/spectral_envelope_baselines.py")

add_test(
  NAME timbre_descriptor_manifest_check
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/timbre_descriptor_manifest_check.py")

add_test(
  NAME timbre_descriptor_analytical_fixtures_check
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/timbre_descriptor_analytical_fixtures.py")

add_test(
  NAME timbre_descriptor_report_schema_check
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/timbre_descriptor_report_check.py")

add_executable(timbre_descriptors_model_test
  timbre_descriptors_model_test.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/TimbreDescriptors.cpp)
target_include_directories(timbre_descriptors_model_test PRIVATE ../src/core)
target_compile_features(timbre_descriptors_model_test PRIVATE cxx_std_17)
add_test(NAME timbre_descriptors_model_test COMMAND timbre_descriptors_model_test)

add_executable(timbre_frame_adapter_test
  timbre_frame_adapter_test.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/TrueEnvelope.cpp
  ../src/core/TimbreDescriptors.cpp
  ../src/core/TimbreFrameAdapter.cpp)
target_include_directories(timbre_frame_adapter_test PRIVATE ../src/core)
target_compile_features(timbre_frame_adapter_test PRIVATE cxx_std_17)
add_test(NAME timbre_frame_adapter_test COMMAND timbre_frame_adapter_test)

add_executable(timbre_energy_distribution_test
  timbre_energy_distribution_test.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/TimbreDescriptors.cpp
  ../src/core/TimbreFrameAdapter.cpp
  ../src/core/TimbreEnergyDistribution.cpp)
target_include_directories(timbre_energy_distribution_test PRIVATE ../src/core)
target_compile_features(timbre_energy_distribution_test PRIVATE cxx_std_17)
add_test(NAME timbre_energy_distribution_test COMMAND timbre_energy_distribution_test)

add_executable(timbre_energy_noisiness_test
  timbre_energy_noisiness_test.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/TimbreDescriptors.cpp
  ../src/core/TimbreFrameAdapter.cpp
  ../src/core/TimbreEnergyDistribution.cpp
  ../src/core/TimbreFrameAnalysis.cpp)
target_include_directories(timbre_energy_noisiness_test PRIVATE ../src/core)
target_compile_features(timbre_energy_noisiness_test PRIVATE cxx_std_17)
add_test(NAME timbre_energy_noisiness_test COMMAND timbre_energy_noisiness_test)

add_executable(timbre_centroid_moments_test
  timbre_centroid_moments_test.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/TimbreDescriptors.cpp
  ../src/core/TimbreFrameAdapter.cpp
  ../src/core/TimbreEnergyDistribution.cpp
  ../src/core/TimbreFrameAnalysis.cpp)
target_include_directories(timbre_centroid_moments_test PRIVATE ../src/core)
target_compile_features(timbre_centroid_moments_test PRIVATE cxx_std_17)
add_test(NAME timbre_centroid_moments_test COMMAND timbre_centroid_moments_test)

add_executable(timbre_grid_shape_test
  timbre_grid_shape_test.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/TimbreDescriptors.cpp
  ../src/core/TimbreFrameAdapter.cpp
  ../src/core/TimbreEnergyDistribution.cpp
  ../src/core/TimbreFrameAnalysis.cpp)
target_include_directories(timbre_grid_shape_test PRIVATE ../src/core)
target_compile_features(timbre_grid_shape_test PRIVATE cxx_std_17)
add_test(NAME timbre_grid_shape_test COMMAND timbre_grid_shape_test)

add_executable(timbre_descriptor_serialization_test
  timbre_descriptor_serialization_test.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/TimbreDescriptors.cpp
  ../src/core/TimbreDescriptorSerialization.cpp)
target_include_directories(timbre_descriptor_serialization_test PRIVATE ../src/core)
target_compile_features(timbre_descriptor_serialization_test PRIVATE cxx_std_17)
target_compile_definitions(timbre_descriptor_serialization_test PRIVATE
  CHECKER_PATH="${CMAKE_CURRENT_SOURCE_DIR}/timbre_descriptor_report_check.py"
  CMAKE_BINARY_DIR_VALUE="${CMAKE_BINARY_DIR}")
add_test(NAME timbre_descriptor_serialization_test COMMAND timbre_descriptor_serialization_test)

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
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/TrueEnvelope.cpp)
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
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/TrueEnvelope.cpp)
target_include_directories(regularized_cepstrum_test PRIVATE ../src/core)
target_compile_features(regularized_cepstrum_test PRIVATE cxx_std_17)
add_test(NAME regularized_cepstrum_test COMMAND regularized_cepstrum_test)

add_executable(rdc_weighting_test
  rdc_weighting_test.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/TrueEnvelope.cpp)
target_include_directories(rdc_weighting_test PRIVATE ../src/core)
target_compile_features(rdc_weighting_test PRIVATE cxx_std_17)
add_test(NAME rdc_weighting_test COMMAND rdc_weighting_test)

add_executable(rdc_sparse_fallback_test
  rdc_sparse_fallback_test.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/TrueEnvelope.cpp)
target_include_directories(rdc_sparse_fallback_test PRIVATE ../src/core)
target_compile_features(rdc_sparse_fallback_test PRIVATE cxx_std_17)
add_test(NAME rdc_sparse_fallback_test COMMAND rdc_sparse_fallback_test)

add_executable(true_envelope_test
  true_envelope_test.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/TrueEnvelope.cpp)
target_include_directories(true_envelope_test PRIVATE ../src/core)
target_compile_features(true_envelope_test PRIVATE cxx_std_17)
add_test(NAME true_envelope_test COMMAND true_envelope_test)

add_executable(lpc_envelope_test
  lpc_envelope_test.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/TrueEnvelope.cpp)
target_include_directories(lpc_envelope_test PRIVATE ../src/core)
target_compile_features(lpc_envelope_test PRIVATE cxx_std_17)
add_test(NAME lpc_envelope_test COMMAND lpc_envelope_test)

add_executable(f0_adaptive_envelope_test
  f0_adaptive_envelope_test.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/TrueEnvelope.cpp)
target_include_directories(f0_adaptive_envelope_test PRIVATE ../src/core)
target_compile_features(f0_adaptive_envelope_test PRIVATE cxx_std_17)
add_test(NAME f0_adaptive_envelope_test COMMAND f0_adaptive_envelope_test)

add_executable(envelope_smoothing_test
  envelope_smoothing_test.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/SpectralEnvelopeSmoothing.cpp
  ../src/core/TrueEnvelope.cpp)
target_include_directories(envelope_smoothing_test PRIVATE ../src/core)
target_compile_features(envelope_smoothing_test PRIVATE cxx_std_17)
add_test(NAME envelope_smoothing_test COMMAND envelope_smoothing_test)

add_executable(envelope_evaluation_test
  envelope_evaluation_test.cpp
  ../src/core/EnvelopeEvaluation.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/TrueEnvelope.cpp)
target_include_directories(envelope_evaluation_test PRIVATE ../src/core)
target_compile_features(envelope_evaluation_test PRIVATE cxx_std_17)
add_test(NAME envelope_evaluation_test COMMAND envelope_evaluation_test)

add_executable(sampled_envelope_cache_test
  sampled_envelope_cache_test.cpp
  ../src/core/EnvelopeEvaluation.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SampledEnvelopeCache.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/TrueEnvelope.cpp)
target_include_directories(sampled_envelope_cache_test PRIVATE ../src/core)
target_compile_features(sampled_envelope_cache_test PRIVATE cxx_std_17)
add_test(NAME sampled_envelope_cache_test COMMAND sampled_envelope_cache_test)

add_executable(envelope_overlay_test
  envelope_overlay_test.cpp
  ../src/core/EnvelopeEvaluation.cpp
  ../src/core/EnvelopeOverlay.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/TrueEnvelope.cpp
  ../src/core/SpectralEnvelopeHybrid.cpp)
target_include_directories(envelope_overlay_test PRIVATE ../src/core)
target_compile_features(envelope_overlay_test PRIVATE cxx_std_17)
add_test(NAME envelope_overlay_test COMMAND envelope_overlay_test)

add_executable(hybrid_recipe_test
  hybrid_recipe_test.cpp
  ../src/core/EnvelopeEvaluation.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/TrueEnvelope.cpp
  ../src/core/SpectralEnvelopeHybrid.cpp)
target_include_directories(hybrid_recipe_test PRIVATE ../src/core)
target_compile_features(hybrid_recipe_test PRIVATE cxx_std_17)
add_test(NAME hybrid_recipe_test COMMAND hybrid_recipe_test)

add_executable(hybrid_transfer_test
  hybrid_transfer_test.cpp
  ../src/core/EnvelopeEvaluation.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/TrueEnvelope.cpp
  ../src/core/SpectralEnvelopeHybrid.cpp)
target_include_directories(hybrid_transfer_test PRIVATE ../src/core)
target_compile_features(hybrid_transfer_test PRIVATE cxx_std_17)
add_test(NAME hybrid_transfer_test COMMAND hybrid_transfer_test)

add_executable(hybrid_mapping_test
  hybrid_mapping_test.cpp
  ../src/core/EnvelopeEvaluation.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/TrueEnvelope.cpp
  ../src/core/SpectralEnvelopeHybrid.cpp)
target_include_directories(hybrid_mapping_test PRIVATE ../src/core)
target_compile_features(hybrid_mapping_test PRIVATE cxx_std_17)
add_test(NAME hybrid_mapping_test COMMAND hybrid_mapping_test)

add_executable(hybrid_normalization_test
  hybrid_normalization_test.cpp
  ../src/core/EnvelopeEvaluation.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/TrueEnvelope.cpp
  ../src/core/SpectralEnvelopeHybrid.cpp)
target_include_directories(hybrid_normalization_test PRIVATE ../src/core)
target_compile_features(hybrid_normalization_test PRIVATE cxx_std_17)
add_test(NAME hybrid_normalization_test COMMAND hybrid_normalization_test)

add_executable(hybrid_residual_transient_test
  hybrid_residual_transient_test.cpp
  ../src/core/EnvelopeEvaluation.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/TrueEnvelope.cpp
  ../src/core/SpectralEnvelopeHybrid.cpp)
target_include_directories(hybrid_residual_transient_test PRIVATE ../src/core)
target_compile_features(hybrid_residual_transient_test PRIVATE cxx_std_17)
add_test(NAME hybrid_residual_transient_test COMMAND hybrid_residual_transient_test)

add_executable(spectral_envelope_fixture_metrics_test
  spectral_envelope_fixture_metrics_test.cpp
  ../src/core/EnvelopeEvaluation.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/TrueEnvelope.cpp
  ../src/core/SpectralEnvelopeHybrid.cpp
  ../src/core/SpectralEnvelopeSmoothing.cpp)
target_include_directories(spectral_envelope_fixture_metrics_test PRIVATE ../src/core)
target_compile_features(spectral_envelope_fixture_metrics_test PRIVATE cxx_std_17)
add_test(NAME spectral_envelope_fixture_metrics_test COMMAND spectral_envelope_fixture_metrics_test)

add_executable(envelope_preset_recipe_test
  envelope_preset_recipe_test.cpp
  ../src/core/EnvelopeEvaluation.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/TrueEnvelope.cpp
  ../src/core/SpectralEnvelopeHybrid.cpp)
target_include_directories(envelope_preset_recipe_test PRIVATE ../src/core)
target_compile_features(envelope_preset_recipe_test PRIVATE cxx_std_17)
add_test(NAME envelope_preset_recipe_test COMMAND envelope_preset_recipe_test)

add_executable(spectral_envelope_registry_test
  spectral_envelope_registry_test.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/TrueEnvelope.cpp)
target_include_directories(spectral_envelope_registry_test PRIVATE ../src/core)
target_compile_features(spectral_envelope_registry_test PRIVATE cxx_std_17)
add_test(NAME spectral_envelope_registry_test COMMAND spectral_envelope_registry_test)

add_executable(spectral_envelope_improvements_test
  spectral_envelope_improvements_test.cpp
  ../src/core/FrequencyWarp.cpp
  ../src/core/F0AdaptiveEnvelope.cpp
  ../src/core/LpcEnvelope.cpp
  ../src/core/RegularizedCepstrum.cpp
  ../src/core/SpectralEnvelope.cpp
  ../src/core/SpectralEnvelopeEstimator.cpp
  ../src/core/TrueEnvelope.cpp)
target_include_directories(spectral_envelope_improvements_test PRIVATE ../src/core)
target_compile_features(spectral_envelope_improvements_test PRIVATE cxx_std_17)
add_test(NAME spectral_envelope_improvements_test COMMAND spectral_envelope_improvements_test)

add_test(NAME ats_roundtrip COMMAND $<TARGET_FILE:ats_roundtrip> WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")
