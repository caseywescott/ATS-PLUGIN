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

option(BUILD_PLUGIN "Build JUCE VST3 + Standalone plugin" ON)
option(ATS_REQUIRE_QATSH_RESIDUAL_FIXES "Require local Qatsh ATSA residual fixes before building" ON)
option(ATS_REQUIRE_QATSH_PROGRESS "Require local Qatsh ATSA analysis-progress patch before building" ON)
option(ATS_REQUIRE_QATSH_REASSIGNMENT "Require local Qatsh ATSA frequency-reassignment patch before building" ON)
option(ATS_REQUIRE_QATSH_QIFFT "Require local Qatsh ATSA QIFFT amplitude patch before building" ON)
option(ATS_REQUIRE_QATSH_DDM "Require local Qatsh ATSA DDM within-frame slopes patch before building" ON)

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

add_subdirectory(tools)

if(BUILD_PLUGIN)
  add_subdirectory(src/plugin)
endif()

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
