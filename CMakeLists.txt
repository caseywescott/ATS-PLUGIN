cmake_minimum_required(VERSION 3.22)
project(ats_plugin VERSION 0.1.0 LANGUAGES C CXX)

# Default to an optimized build. Without this, a bare `cmake -S . -B build`
# produces an empty build type (-O0), which makes the CPU-heavy ATSA analysis
# and morph/resynth rendering 5-20x slower. Single-config generators only;
# multi-config generators (Xcode/MSVC) pick the type at build time.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  message(STATUS "No CMAKE_BUILD_TYPE specified; defaulting to Release.")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_PLUGIN "Build the JUCE-free native plugin artifacts" ON)
option(ATS_BUILD_NATIVE_VST3 "Build the JUCE-free ATS VST3 wrapper" OFF)
option(ATS_BUILD_NATIVE_STANDALONE "Build the JUCE-free ATS standalone wrapper" OFF)
option(ATS_BUILD_NATIVE_AU "Build the JUCE-free ATS AUv2 wrapper" OFF)
set(ATS_AUDIO_UNIT_SDK_VERSION "53a9a2008aae7fb1b0a9f093dd523b9b12f6c0d9"
    CACHE STRING "Pinned Apple AudioUnitSDK commit for the native AUv2 wrapper")
set(ATS_AUDIO_UNIT_SDK_DIR "" CACHE PATH
    "Path to a standalone Apple AudioUnitSDK checkout (overrides download)")
set(ATS_VST3_SDK_VERSION "05c4a9763f311a58fe3f34b68a199e5eea92e2b4"
    CACHE STRING "Pinned Steinberg VST3 SDK commit for the native wrapper")
set(ATS_VST3_SDK_DIR "" CACHE PATH
    "Path to a standalone Steinberg VST3 SDK checkout (overrides download)")
option(ATS_BUILD_VISAGE_UI_PROBE "Build the pinned Visage native editor probe" OFF)
option(ATS_ENABLE_NATIVE_HOST_SMOKE
    "Run launched native-host editor smoke tests (requires a GUI-capable runner)" OFF)
set(ATS_VISAGE_VERSION "20de59464243447816d142e9d38e9723d068f755"
    CACHE STRING "Pinned Visage commit for the native editor backend")
set(ATS_VISAGE_DIR "" CACHE PATH "Path to a local Visage checkout")

# BUILD_PLUGIN is retained as the public configure switch used by existing
# scripts, but it no longer enters the retired src/plugin JUCE target. It
# selects the native wrapper graph instead. Individual ATS_BUILD_NATIVE_*
# options remain available for focused builds and CI jobs that set
# BUILD_PLUGIN=OFF.
if(BUILD_PLUGIN)
  set(ATS_BUILD_NATIVE_VST3 ON CACHE BOOL "Build the JUCE-free ATS VST3 wrapper" FORCE)
  set(ATS_BUILD_NATIVE_STANDALONE ON CACHE BOOL "Build the JUCE-free ATS standalone wrapper" FORCE)
  if(APPLE)
    set(ATS_BUILD_NATIVE_AU ON CACHE BOOL "Build the JUCE-free ATS AUv2 wrapper" FORCE)
  endif()
endif()

function(ats_assert_git_revision checkout expected name)
  execute_process(
    COMMAND git -C "${checkout}" rev-parse HEAD
    RESULT_VARIABLE _ats_git_result
    OUTPUT_VARIABLE _ats_git_revision
    ERROR_VARIABLE _ats_git_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _ats_git_result EQUAL 0)
    message(FATAL_ERROR
      "${name} checkout is not a readable Git repository: ${checkout}. "
      "Expected pinned revision ${expected}. ${_ats_git_error}")
  endif()
  if(NOT "${_ats_git_revision}" STREQUAL "${expected}")
    message(FATAL_ERROR
      "${name} checkout revision mismatch: ${checkout} is ${_ats_git_revision}, "
      "but the build requires ${expected}.")
  endif()
endfunction()

if(APPLE AND (ATS_BUILD_VISAGE_UI_PROBE OR ATS_BUILD_NATIVE_VST3 OR ATS_BUILD_NATIVE_STANDALONE OR ATS_BUILD_NATIVE_AU))
  # Visage's native macOS windowing implementation contains Objective-C++
  # sources. Enable the language at the top-level project so CMake also
  # initializes its compile rule variables for the nested dependency.
  enable_language(OBJC)
  enable_language(OBJCXX)
endif()
option(ATS_REQUIRE_QATSH_RESIDUAL_FIXES "Require local Qatsh ATSA residual fixes before building" ON)
option(ATS_REQUIRE_QATSH_PROGRESS "Require local Qatsh ATSA analysis-progress patch before building" ON)
option(ATS_REQUIRE_QATSH_REASSIGNMENT "Require local Qatsh ATSA frequency-reassignment patch before building" ON)
option(ATS_REQUIRE_QATSH_QIFFT "Require local Qatsh ATSA QIFFT amplitude patch before building" ON)
option(ATS_REQUIRE_QATSH_DDM "Require local Qatsh ATSA DDM within-frame slopes patch before building" ON)
option(ATS_REQUIRE_QATSH_MULTIRES "Require local Qatsh ATSA multi-resolution analysis patch before building" ON)
option(ATS_REQUIRE_QATSH_IFFT "Require local Qatsh ATSA overlap-add IFFT synthesis patch before building" ON)
option(ATS_REQUIRE_QATSH_RFFT_PACK "Require local Qatsh ATSA real-FFT packing patch before building" ON)
option(ATS_REQUIRE_QATSH_VDSP_FFT "Require local Qatsh ATSA vDSP FFT backend patch before building" ON)
option(ATS_REQUIRE_QATSH_PARALLEL "Require local Qatsh ATSA parallel frame-loop patch before building" ON)
option(ATS_REQUIRE_QATSH_RESIDUAL_PARALLEL "Require local Qatsh ATSA parallel residual-stage patch before building" ON)
option(ATS_REQUIRE_QATSH_MAINLOBE_PARALLEL "Require local Qatsh ATSA parallel main-lobe-gain patch before building" ON)
option(ATS_REQUIRE_QATSH_IFFT_SRAND "Require Qatsh ATSA IFFT srand-before-dispatch fix (C-002)" ON)
option(ATS_REQUIRE_QATSH_DDM_SLOPES_MAINT "Require Qatsh ATSA DDM slope maintenance in utilities (C-003)" ON)
option(ATS_REQUIRE_QATSH_TRACKSTORE "Require Qatsh ATSA O(P*T) track-store loop (A-001)" ON)
option(ATS_REQUIRE_QATSH_SMR_PRECOMPUTE "Require Qatsh ATSA SMR bark/dB precompute (A-003)" ON)
option(ATS_REQUIRE_QATSH_TRACKING_DS "Require Qatsh ATSA peak-tracking data structures (A-002)" ON)
option(ATS_REQUIRE_QATSH_RESIDUAL_POW2 "Require Qatsh ATSA pow2 residual FFT size (C-004)" ON)
option(ATS_REQUIRE_QATSH_SCRATCH "Require Qatsh ATSA frame scratch reuse (A-004)" ON)
option(ATS_REQUIRE_QATSH_SYNTH_STATE "Require Qatsh ATSA thread-local synthesis state (C-001 Phase 2.7)" ON)

if(NOT DEFINED ATS_ATSA_SOURCE_DIR)
  set(ATS_ATSA_SOURCE_DIR "${CMAKE_SOURCE_DIR}/qatsh/src/atsa" CACHE PATH "Path to Qatsh ATSA sources (atsa.h and .c files)")
endif()

if(NOT EXISTS "${ATS_ATSA_SOURCE_DIR}/atsa.h")
  message(FATAL_ERROR "ATSA sources not found at ATS_ATSA_SOURCE_DIR='${ATS_ATSA_SOURCE_DIR}'. Clone Qatsh next to this project or set ATS_ATSA_SOURCE_DIR.")
endif()

if(ATS_REQUIRE_QATSH_RESIDUAL_FIXES)
  file(READ "${ATS_ATSA_SOURCE_DIR}/synthesis.c" _atsa_synthesis_source)
  file(READ "${ATS_ATSA_SOURCE_DIR}/residual-analysis.c" _atsa_residual_source)
  string(FIND "${_atsa_synthesis_source}" "ATSA_SYNTH_RANDOM_SEED" _atsa_seed_marker)
  string(FIND "${_atsa_synthesis_source}" "has_residual = ats_sound->band_energy != NULL" _atsa_residual_guard_marker)
  string(FIND "${_atsa_residual_source}" "DB_AMP(ATSA_NOISE_THRESHOLD) * DB_AMP(ATSA_NOISE_THRESHOLD)" _atsa_threshold_marker)
  if(_atsa_seed_marker EQUAL -1 OR _atsa_residual_guard_marker EQUAL -1 OR _atsa_threshold_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA residual fixes are not applied. Run scripts/apply_qatsh_residual_fixes.sh apply, or configure with -DATS_REQUIRE_QATSH_RESIDUAL_FIXES=OFF to bypass.")
  endif()
endif()
unset(_atsa_synthesis_source)
unset(_atsa_residual_source)
unset(_atsa_seed_marker)
unset(_atsa_residual_guard_marker)
unset(_atsa_threshold_marker)

if(ATS_REQUIRE_QATSH_PROGRESS)
  file(READ "${ATS_ATSA_SOURCE_DIR}/atsa.h" _atsa_header_source)
  string(FIND "${_atsa_header_source}" "void (*progress_cb)(void *user, double fraction)" _atsa_progress_marker)
  if(_atsa_progress_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA analysis-progress patch is not applied. Run scripts/apply_qatsh_progress.sh apply, or configure with -DATS_REQUIRE_QATSH_PROGRESS=OFF to bypass.")
  endif()
  unset(_atsa_header_source)
  unset(_atsa_progress_marker)
endif()

if(ATS_REQUIRE_QATSH_REASSIGNMENT)
  file(READ "${ATS_ATSA_SOURCE_DIR}/peak-detection.c" _atsa_peak_source)
  file(READ "${ATS_ATSA_SOURCE_DIR}/tracker.c" _atsa_tracker_source)
  string(FIND "${_atsa_peak_source}" "ATSA_REASSIGN_FREQ" _atsa_reassign_marker)
  string(FIND "${_atsa_tracker_source}" "ATSA_REASSIGN_FREQ" _atsa_dwindow_marker)
  if(_atsa_reassign_marker EQUAL -1 OR _atsa_dwindow_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA frequency-reassignment patch is not applied. Run scripts/apply_qatsh_reassignment.sh apply, or configure with -DATS_REQUIRE_QATSH_REASSIGNMENT=OFF to bypass.")
  endif()
  unset(_atsa_peak_source)
  unset(_atsa_tracker_source)
  unset(_atsa_reassign_marker)
  unset(_atsa_dwindow_marker)
endif()

if(ATS_REQUIRE_QATSH_QIFFT)
  file(READ "${ATS_ATSA_SOURCE_DIR}/peak-detection.c" _atsa_qifft_peak_source)
  file(READ "${ATS_ATSA_SOURCE_DIR}/tracker.c" _atsa_qifft_tracker_source)
  string(FIND "${_atsa_qifft_peak_source}" "ATSA_QIFFT_AMP" _atsa_qifft_peak_marker)
  string(FIND "${_atsa_qifft_tracker_source}" "ATSA_QIFFT_AMP" _atsa_qifft_tracker_marker)
  if(_atsa_qifft_peak_marker EQUAL -1 OR _atsa_qifft_tracker_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA QIFFT amplitude patch is not applied. Run scripts/apply_qatsh_qifft.sh apply, or configure with -DATS_REQUIRE_QATSH_QIFFT=OFF to bypass.")
  endif()
  unset(_atsa_qifft_peak_source)
  unset(_atsa_qifft_tracker_source)
  unset(_atsa_qifft_peak_marker)
  unset(_atsa_qifft_tracker_marker)
endif()

if(ATS_REQUIRE_QATSH_DDM)
  file(READ "${ATS_ATSA_SOURCE_DIR}/tracker.c" _atsa_ddm_tracker_source)
  file(READ "${ATS_ATSA_SOURCE_DIR}/synthesis.c" _atsa_ddm_synth_source)
  string(FIND "${_atsa_ddm_tracker_source}" "ATSA_DDM_SLOPES" _atsa_ddm_tracker_marker)
  string(FIND "${_atsa_ddm_synth_source}" "ATSA_DDM_SLOPES" _atsa_ddm_synth_marker)
  if(_atsa_ddm_tracker_marker EQUAL -1 OR _atsa_ddm_synth_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA DDM within-frame slopes patch is not applied. Run scripts/apply_qatsh_ddm.sh apply, or configure with -DATS_REQUIRE_QATSH_DDM=OFF to bypass.")
  endif()
  unset(_atsa_ddm_tracker_source)
  unset(_atsa_ddm_synth_source)
  unset(_atsa_ddm_tracker_marker)
  unset(_atsa_ddm_synth_marker)
endif()

if(ATS_REQUIRE_QATSH_MULTIRES)
  file(READ "${ATS_ATSA_SOURCE_DIR}/tracker.c" _atsa_mr_tracker_source)
  file(READ "${ATS_ATSA_SOURCE_DIR}/atsa.h" _atsa_mr_header_source)
  string(FIND "${_atsa_mr_tracker_source}" "ATSA_MULTIRES" _atsa_mr_tracker_marker)
  string(FIND "${_atsa_mr_header_source}" "ATSA_MULTIRES" _atsa_mr_header_marker)
  if(_atsa_mr_tracker_marker EQUAL -1 OR _atsa_mr_header_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA multi-resolution analysis patch is not applied. Run scripts/apply_qatsh_multires.sh apply, or configure with -DATS_REQUIRE_QATSH_MULTIRES=OFF to bypass.")
  endif()
  unset(_atsa_mr_tracker_source)
  unset(_atsa_mr_header_source)
  unset(_atsa_mr_tracker_marker)
  unset(_atsa_mr_header_marker)
endif()

if(ATS_REQUIRE_QATSH_IFFT)
  file(READ "${ATS_ATSA_SOURCE_DIR}/synthesis.c" _atsa_ifft_synth_source)
  file(READ "${ATS_ATSA_SOURCE_DIR}/atsa.h" _atsa_ifft_header_source)
  string(FIND "${_atsa_ifft_synth_source}" "ATSA_IFFT_SYNTH" _atsa_ifft_synth_marker)
  string(FIND "${_atsa_ifft_header_source}" "ATSA_IFFT_SYNTH" _atsa_ifft_header_marker)
  if(_atsa_ifft_synth_marker EQUAL -1 OR _atsa_ifft_header_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA overlap-add IFFT synthesis patch is not applied. Run scripts/apply_qatsh_ifft.sh apply, or configure with -DATS_REQUIRE_QATSH_IFFT=OFF to bypass.")
  endif()
  unset(_atsa_ifft_synth_source)
  unset(_atsa_ifft_header_source)
  unset(_atsa_ifft_synth_marker)
  unset(_atsa_ifft_header_marker)
endif()

if(ATS_REQUIRE_QATSH_RFFT_PACK)
  file(READ "${ATS_ATSA_SOURCE_DIR}/tracker.c" _atsa_rfft_tracker_source)
  string(FIND "${_atsa_rfft_tracker_source}" "ATSA_RFFT_PACK" _atsa_rfft_tracker_marker)
  if(_atsa_rfft_tracker_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA real-FFT packing patch is not applied. Run scripts/apply_qatsh_rfft_pack.sh apply, or configure with -DATS_REQUIRE_QATSH_RFFT_PACK=OFF to bypass.")
  endif()
  unset(_atsa_rfft_tracker_source)
  unset(_atsa_rfft_tracker_marker)
endif()

if(ATS_REQUIRE_QATSH_VDSP_FFT)
  file(READ "${ATS_ATSA_SOURCE_DIR}/other-utils.c" _atsa_vdsp_source)
  string(FIND "${_atsa_vdsp_source}" "ATSA_VDSP_FFT" _atsa_vdsp_marker)
  if(_atsa_vdsp_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA vDSP FFT backend patch is not applied. Run scripts/apply_qatsh_vdsp_fft.sh apply, or configure with -DATS_REQUIRE_QATSH_VDSP_FFT=OFF to bypass.")
  endif()
  unset(_atsa_vdsp_source)
  unset(_atsa_vdsp_marker)
endif()

if(ATS_REQUIRE_QATSH_PARALLEL)
  file(READ "${ATS_ATSA_SOURCE_DIR}/tracker.c" _atsa_par_tracker_source)
  file(READ "${ATS_ATSA_SOURCE_DIR}/peak-detection.c" _atsa_par_peak_source)
  string(FIND "${_atsa_par_tracker_source}" "ATSA_PARALLEL" _atsa_par_tracker_marker)
  string(FIND "${_atsa_par_peak_source}" "ATSA_PARALLEL" _atsa_par_peak_marker)
  if(_atsa_par_tracker_marker EQUAL -1 OR _atsa_par_peak_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA parallel frame-loop patch is not applied. Run scripts/apply_qatsh_parallel.sh apply, or configure with -DATS_REQUIRE_QATSH_PARALLEL=OFF to bypass.")
  endif()
  unset(_atsa_par_tracker_source)
  unset(_atsa_par_peak_source)
  unset(_atsa_par_tracker_marker)
  unset(_atsa_par_peak_marker)
endif()

if(ATS_REQUIRE_QATSH_RESIDUAL_PARALLEL)
  file(READ "${ATS_ATSA_SOURCE_DIR}/residual.c" _atsa_rpar_res_source)
  file(READ "${ATS_ATSA_SOURCE_DIR}/residual-analysis.c" _atsa_rpar_ana_source)
  string(FIND "${_atsa_rpar_res_source}" "ATSA_PARALLEL" _atsa_rpar_res_marker)
  string(FIND "${_atsa_rpar_ana_source}" "ATSA_PARALLEL" _atsa_rpar_ana_marker)
  if(_atsa_rpar_res_marker EQUAL -1 OR _atsa_rpar_ana_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA parallel residual-stage patch is not applied. Run scripts/apply_qatsh_residual_parallel.sh apply, or configure with -DATS_REQUIRE_QATSH_RESIDUAL_PARALLEL=OFF to bypass.")
  endif()
  unset(_atsa_rpar_res_source)
  unset(_atsa_rpar_ana_source)
  unset(_atsa_rpar_res_marker)
  unset(_atsa_rpar_ana_marker)
endif()

if(ATS_REQUIRE_QATSH_MAINLOBE_PARALLEL)
  file(READ "${ATS_ATSA_SOURCE_DIR}/peak-detection.c" _atsa_mlobe_source)
  string(FIND "${_atsa_mlobe_source}" "mainlobe_worker" _atsa_mlobe_marker)
  if(_atsa_mlobe_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA parallel main-lobe-gain patch is not applied. Run scripts/apply_qatsh_mainlobe_parallel.sh apply, or configure with -DATS_REQUIRE_QATSH_MAINLOBE_PARALLEL=OFF to bypass.")
  endif()
  unset(_atsa_mlobe_source)
  unset(_atsa_mlobe_marker)
endif()

if(ATS_REQUIRE_QATSH_IFFT_SRAND)
  file(READ "${ATS_ATSA_SOURCE_DIR}/synthesis.c" _atsa_ifft_srand_source)
  string(FIND "${_atsa_ifft_srand_source}" "atsa_synth_seed_rng(ATSA_SYNTH_RANDOM_SEED)" _atsa_ifft_srand_pos)
  if(_atsa_ifft_srand_pos EQUAL -1)
    string(FIND "${_atsa_ifft_srand_source}" "srand(ATSA_SYNTH_RANDOM_SEED)" _atsa_ifft_srand_pos)
  endif()
  string(FIND "${_atsa_ifft_srand_source}" "sparams->use_ifft" _atsa_ifft_dispatch_pos)
  if(_atsa_ifft_srand_pos EQUAL -1 OR _atsa_ifft_dispatch_pos EQUAL -1 OR _atsa_ifft_srand_pos GREATER _atsa_ifft_dispatch_pos)
    message(FATAL_ERROR "Required Qatsh ATSA IFFT srand-before-dispatch patch is not applied. Run scripts/apply_qatsh_ifft_srand.sh apply, or configure with -DATS_REQUIRE_QATSH_IFFT_SRAND=OFF to bypass.")
  endif()
  unset(_atsa_ifft_srand_source)
  unset(_atsa_ifft_srand_pos)
  unset(_atsa_ifft_dispatch_pos)
endif()

if(ATS_REQUIRE_QATSH_DDM_SLOPES_MAINT)
  file(READ "${ATS_ATSA_SOURCE_DIR}/utilities.c" _atsa_ddm_maint_source)
  string(FIND "${_atsa_ddm_maint_source}" "swap(double*, sound->frq_slope[part], sound->frq_slope[kept])" _atsa_ddm_maint_marker)
  if(_atsa_ddm_maint_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA DDM slope maintenance patch is not applied. Run scripts/apply_qatsh_ddm_slopes_maint.sh apply, or configure with -DATS_REQUIRE_QATSH_DDM_SLOPES_MAINT=OFF to bypass.")
  endif()
  unset(_atsa_ddm_maint_source)
  unset(_atsa_ddm_maint_marker)
endif()

if(ATS_REQUIRE_QATSH_TRACKSTORE)
  file(READ "${ATS_ATSA_SOURCE_DIR}/tracker.c" _atsa_trackstore_source)
  string(FIND "${_atsa_trackstore_source}" "ATSA_TRACKSTORE" _atsa_trackstore_marker)
  if(_atsa_trackstore_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA track-store loop patch is not applied. Run scripts/apply_qatsh_trackstore.sh apply, or configure with -DATS_REQUIRE_QATSH_TRACKSTORE=OFF to bypass.")
  endif()
  unset(_atsa_trackstore_source)
  unset(_atsa_trackstore_marker)
endif()

if(ATS_REQUIRE_QATSH_SMR_PRECOMPUTE)
  file(READ "${ATS_ATSA_SOURCE_DIR}/critical-bands.c" _atsa_smr_pre_source)
  string(FIND "${_atsa_smr_pre_source}" "ATSA_SMR_PRECOMPUTE" _atsa_smr_pre_marker)
  if(_atsa_smr_pre_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA SMR precompute patch is not applied. Run scripts/apply_qatsh_smr_precompute.sh apply, or configure with -DATS_REQUIRE_QATSH_SMR_PRECOMPUTE=OFF to bypass.")
  endif()
  unset(_atsa_smr_pre_source)
  unset(_atsa_smr_pre_marker)
endif()

if(ATS_REQUIRE_QATSH_TRACKING_DS)
  file(READ "${ATS_ATSA_SOURCE_DIR}/peak-tracking.c" _atsa_tracking_ds_source)
  string(FIND "${_atsa_tracking_ds_source}" "ATSA_TRACKING_DS" _atsa_tracking_ds_marker)
  if(_atsa_tracking_ds_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA peak-tracking data structures patch is not applied. Run scripts/apply_qatsh_tracking_ds.sh apply, or configure with -DATS_REQUIRE_QATSH_TRACKING_DS=OFF to bypass.")
  endif()
  unset(_atsa_tracking_ds_source)
  unset(_atsa_tracking_ds_marker)
endif()

if(ATS_REQUIRE_QATSH_RESIDUAL_POW2)
  file(READ "${ATS_ATSA_SOURCE_DIR}/residual-analysis.c" _atsa_res_pow2_source)
  string(FIND "${_atsa_res_pow2_source}" "ATSA_RESIDUAL_POW2" _atsa_res_pow2_marker)
  if(_atsa_res_pow2_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA residual pow2 FFT patch is not applied. Run scripts/apply_qatsh_residual_pow2.sh apply, or configure with -DATS_REQUIRE_QATSH_RESIDUAL_POW2=OFF to bypass.")
  endif()
  unset(_atsa_res_pow2_source)
  unset(_atsa_res_pow2_marker)
endif()

if(ATS_REQUIRE_QATSH_SCRATCH)
  file(READ "${ATS_ATSA_SOURCE_DIR}/other-utils.c" _atsa_scratch_source)
  file(READ "${ATS_ATSA_SOURCE_DIR}/tracker.c" _atsa_scratch_tracker_source)
  string(FIND "${_atsa_scratch_source}" "ATSA_SCRATCH_PEAKS" _atsa_scratch_marker)
  string(FIND "${_atsa_scratch_tracker_source}" "polar_mags" _atsa_scratch_polar_marker)
  if(_atsa_scratch_marker EQUAL -1 OR _atsa_scratch_polar_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA frame scratch reuse patch is not applied. Run scripts/apply_qatsh_scratch.sh apply, or configure with -DATS_REQUIRE_QATSH_SCRATCH=OFF to bypass.")
  endif()
  unset(_atsa_scratch_source)
  unset(_atsa_scratch_tracker_source)
  unset(_atsa_scratch_marker)
  unset(_atsa_scratch_polar_marker)
endif()

if(ATS_REQUIRE_QATSH_SYNTH_STATE)
  file(READ "${ATS_ATSA_SOURCE_DIR}/synthesis.c" _atsa_synth_state_source)
  file(READ "${ATS_ATSA_SOURCE_DIR}/residual-analysis.c" _atsa_synth_state_res_source)
  string(FIND "${_atsa_synth_state_source}" "ATSA_SYNTH_STATE" _atsa_synth_state_marker)
  string(FIND "${_atsa_synth_state_res_source}" "atsa_prefill_partial_residual" _atsa_synth_state_prefill_marker)
  if(_atsa_synth_state_marker EQUAL -1 OR _atsa_synth_state_prefill_marker EQUAL -1)
    message(FATAL_ERROR "Required Qatsh ATSA synthesis state patch is not applied. Run scripts/apply_qatsh_synth_state.sh apply, or configure with -DATS_REQUIRE_QATSH_SYNTH_STATE=OFF to bypass.")
  endif()
  unset(_atsa_synth_state_source)
  unset(_atsa_synth_state_res_source)
  unset(_atsa_synth_state_marker)
  unset(_atsa_synth_state_prefill_marker)
endif()

find_path(SNDFILE_INCLUDE_DIR sndfile.h
  PATHS
    "${CMAKE_PREFIX_PATH}"
  PATH_SUFFIXES include)
find_library(SNDFILE_LIBRARY NAMES sndfile libsndfile
  PATHS
    "${CMAKE_PREFIX_PATH}"
  PATH_SUFFIXES lib)

if((NOT SNDFILE_INCLUDE_DIR OR NOT SNDFILE_LIBRARY) AND APPLE)
  execute_process(
    COMMAND brew --prefix libsndfile
    OUTPUT_VARIABLE _brew_sndfile
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE _brew_st)
  if(_brew_st EQUAL 0 AND EXISTS "${_brew_sndfile}/include/sndfile.h")
    set(SNDFILE_INCLUDE_DIR "${_brew_sndfile}/include")
    find_library(SNDFILE_LIBRARY NAMES sndfile libsndfile PATHS "${_brew_sndfile}/lib" NO_DEFAULT_PATH)
  endif()
endif()

if(NOT SNDFILE_INCLUDE_DIR OR NOT SNDFILE_LIBRARY)
  message(FATAL_ERROR "libsndfile not found. Install it (e.g. brew install libsndfile), set CMAKE_PREFIX_PATH to the prefix, or pass -DSNDFILE_INCLUDE_DIR=... -DSNDFILE_LIBRARY=...")
endif()

add_library(SndFile::sndfile UNKNOWN IMPORTED)
set_target_properties(SndFile::sndfile PROPERTIES
  IMPORTED_LOCATION "${SNDFILE_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${SNDFILE_INCLUDE_DIR}")
set(_ATSA_SRCS
  peak-tracking.c peak-detection.c other-utils.c critical-bands.c utilities.c
  tracker.c save-load-sound.c residual-analysis.c residual.c
  curves.c synthesis.c synthesis.atsh.c atsa.c
)
set(_ATSA_FULL "")
foreach(_f IN LISTS _ATSA_SRCS)
  list(APPEND _ATSA_FULL "${ATS_ATSA_SOURCE_DIR}/${_f}")
endforeach()

add_library(atsa STATIC ${_ATSA_FULL})
target_include_directories(atsa PUBLIC "${ATS_ATSA_SOURCE_DIR}")
if(MSVC)
  target_compile_definitions(atsa PRIVATE _USE_MATH_DEFINES)
endif()
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(atsa PRIVATE -Wno-unused-parameter -Wno-sign-compare)
endif()
target_link_libraries(atsa PUBLIC SndFile::sndfile)
if(APPLE)
  # ATSA_VDSP_FFT: Accelerate provides the vDSP double-precision FFT backend
  target_link_libraries(atsa PUBLIC "-framework Accelerate")
endif()

# Framework-free ATS ownership boundary. This target is intentionally built
# before the native wrapper targets so core contracts can be compiled and
# tested with BUILD_PLUGIN=OFF without a plugin SDK in the include graph.
add_library(ats_core STATIC
  src/core/AtsFrequencyShape.cpp
  src/core/AtsMathExpr.cpp
  src/core/AtsModel.cpp
  src/core/AtsMorphRecipe.cpp
  src/core/AtsMorphTransport.cpp
  src/core/AtsMorphTransportCorrespondence.cpp
  src/core/AtsParams.cpp
  src/core/AtsSpectralEnvelope.cpp
  src/core/AtsTransientLayer.cpp
  src/core/AtsTransientSmear.cpp
  src/core/ParameterRegistry.cpp
  src/core/ResampleLinear.cpp
  src/core/TimbreMotionPlan.cpp)
target_include_directories(ats_core PUBLIC "${CMAKE_SOURCE_DIR}/src/core" "${ATS_ATSA_SOURCE_DIR}")
target_compile_features(ats_core PUBLIC cxx_std_17)
target_link_libraries(ats_core PUBLIC atsa)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ats_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(ats_engine STATIC src/engine/AtsEngine.cpp)
target_include_directories(ats_engine PUBLIC "${CMAKE_SOURCE_DIR}/src/core" "${CMAKE_SOURCE_DIR}/src/engine" "${ATS_ATSA_SOURCE_DIR}")
target_compile_features(ats_engine PUBLIC cxx_std_17)
target_link_libraries(ats_engine PUBLIC ats_core atsa)

add_library(ats_playback STATIC src/engine/PlaybackEngine.cpp)
target_include_directories(ats_playback PUBLIC
  "${CMAKE_SOURCE_DIR}/src/core"
  "${CMAKE_SOURCE_DIR}/src/engine")
target_compile_features(ats_playback PUBLIC cxx_std_17)
target_link_libraries(ats_playback PUBLIC ats_core)

add_library(ats_plugin_model STATIC src/engine/PluginModel.cpp)
target_include_directories(ats_plugin_model PUBLIC
  "${CMAKE_SOURCE_DIR}/src/core"
  "${CMAKE_SOURCE_DIR}/src/engine"
  "${CMAKE_SOURCE_DIR}/src/ui")
target_compile_features(ats_plugin_model PUBLIC cxx_std_17)
target_link_libraries(ats_plugin_model PUBLIC ats_core)

add_library(ats_state STATIC src/state/LegacyStateCodec.cpp)
target_include_directories(ats_state PUBLIC "${CMAKE_SOURCE_DIR}/src/core" "${CMAKE_SOURCE_DIR}/src/state" "${ATS_ATSA_SOURCE_DIR}")
target_compile_features(ats_state PUBLIC cxx_std_17)
target_link_libraries(ats_state PUBLIC ats_core)

add_library(ats_file_service STATIC src/platform/FileService.cpp)
target_include_directories(ats_file_service PUBLIC "${CMAKE_SOURCE_DIR}/src/core" "${CMAKE_SOURCE_DIR}/src/platform")
target_compile_features(ats_file_service PUBLIC cxx_std_17)
target_link_libraries(ats_file_service PUBLIC ats_core)

add_library(ats_preset_service STATIC src/state/PresetService.cpp)
target_include_directories(ats_preset_service PUBLIC
  "${CMAKE_SOURCE_DIR}/src/core"
  "${CMAKE_SOURCE_DIR}/src/platform"
  "${CMAKE_SOURCE_DIR}/src/state")
target_compile_features(ats_preset_service PUBLIC cxx_std_17)
target_link_libraries(ats_preset_service PUBLIC ats_file_service)

add_library(ats_editor INTERFACE)
target_include_directories(ats_editor INTERFACE "${CMAKE_SOURCE_DIR}/src/core" "${CMAKE_SOURCE_DIR}/src/ui")
target_link_libraries(ats_editor INTERFACE ats_core)

# Format wrappers are deliberately interface-only until their SDK entry points
# are available. Keeping the three neutral seams in the graph now prevents
# wrapper code from reaching around HostAdapter and makes the no-JUCE build
# boundary testable in every configuration.
foreach(host_target IN ITEMS ats_host_vst3 ats_host_au ats_standalone)
  add_library(${host_target} INTERFACE)
  target_include_directories(${host_target} INTERFACE
    "${CMAKE_SOURCE_DIR}/src/core"
    "${CMAKE_SOURCE_DIR}/src/host"
    "${CMAKE_SOURCE_DIR}/src/ui")
  target_link_libraries(${host_target} INTERFACE
    ats_core ats_playback ats_state ats_editor)
endforeach()

if(ATS_BUILD_VISAGE_UI_PROBE OR ATS_BUILD_NATIVE_VST3 OR ATS_BUILD_NATIVE_STANDALONE OR ATS_BUILD_NATIVE_AU)
  if(NOT APPLE)
    message(FATAL_ERROR "ATS Visage editor backends currently require macOS native host embedding")
  endif()

  include(FetchContent)
  set(VISAGE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(VISAGE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(VISAGE_ENABLE_WIDGETS ON CACHE BOOL "" FORCE)
  if(ATS_VISAGE_DIR AND EXISTS "${ATS_VISAGE_DIR}/CMakeLists.txt")
    ats_assert_git_revision("${ATS_VISAGE_DIR}" "${ATS_VISAGE_VERSION}" "Visage")
    add_subdirectory("${ATS_VISAGE_DIR}" "${CMAKE_BINARY_DIR}/visage")
  else()
    FetchContent_Declare(visage
      GIT_REPOSITORY https://github.com/VitalAudio/visage.git
      GIT_TAG ${ATS_VISAGE_VERSION})
    FetchContent_MakeAvailable(visage)
  endif()

  add_library(ats_visage_editor STATIC src/ui/VisageEditor.cpp src/ui/VisageNativeMac.mm)
  target_include_directories(ats_visage_editor PUBLIC
    "${CMAKE_SOURCE_DIR}/src/ui" "${CMAKE_SOURCE_DIR}/src/core")
  target_include_directories(ats_visage_editor PRIVATE
    $<TARGET_PROPERTY:VisageEmbeddedFonts,INTERFACE_INCLUDE_DIRECTORIES>)
  target_compile_features(ats_visage_editor PUBLIC cxx_std_17)
  target_link_libraries(ats_visage_editor PUBLIC visage)

  if(ATS_BUILD_VISAGE_UI_PROBE)
    add_executable(visage_editor_probe tests/ui/visage_editor_probe.mm)
    target_include_directories(visage_editor_probe PRIVATE "${CMAKE_SOURCE_DIR}/src/ui")
    target_compile_features(visage_editor_probe PRIVATE cxx_std_17)
    target_link_libraries(visage_editor_probe PRIVATE ats_visage_editor ats_core visage "-framework Cocoa")
    add_test(NAME visage_editor_probe COMMAND visage_editor_probe)

    add_executable(real_dispatch_harness_test tests/ui/real_dispatch_harness_test.mm)
    target_include_directories(real_dispatch_harness_test PRIVATE
      "${CMAKE_SOURCE_DIR}/src/core" "${CMAKE_SOURCE_DIR}/src/ui")
    target_compile_features(real_dispatch_harness_test PRIVATE cxx_std_17)
    target_link_libraries(real_dispatch_harness_test PRIVATE ats_visage_editor ats_core visage "-framework Cocoa")
    add_test(NAME real_dispatch_harness_test COMMAND real_dispatch_harness_test)
  endif()
endif()

if(ATS_BUILD_NATIVE_VST3)
  add_subdirectory(src/host/vst3)
endif()

if(ATS_BUILD_NATIVE_STANDALONE)
  add_subdirectory(src/host/standalone)
endif()

if(ATS_BUILD_NATIVE_AU)
  add_subdirectory(src/host/au)
endif()

add_subdirectory(tools)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
