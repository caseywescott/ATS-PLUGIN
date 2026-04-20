cmake_minimum_required(VERSION 3.22)
project(ats_plugin VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_PLUGIN "Build JUCE VST3 + Standalone plugin" ON)

if(NOT DEFINED ATS_ATSA_SOURCE_DIR)
  set(ATS_ATSA_SOURCE_DIR "${CMAKE_SOURCE_DIR}/qatsh/src/atsa" CACHE PATH "Path to Qatsh ATSA sources (atsa.h and .c files)")
endif()

if(NOT EXISTS "${ATS_ATSA_SOURCE_DIR}/atsa.h")
  message(FATAL_ERROR "ATSA sources not found at ATS_ATSA_SOURCE_DIR='${ATS_ATSA_SOURCE_DIR}'. Clone Qatsh next to this project or set ATS_ATSA_SOURCE_DIR.")
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
