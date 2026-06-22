set(ATS_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")

# ── Pure-C tools ─────────────────────────────────────────────────────────────

add_executable(ats_offline ats_offline.c)
target_link_libraries(ats_offline PRIVATE atsa)

add_executable(ats_synth ats_synth.c)
target_link_libraries(ats_synth PRIVATE atsa)

add_executable(ats_analyze ats_analyze.c)
target_link_libraries(ats_analyze PRIVATE atsa)

add_executable(ats_prune ats_prune.c)
target_link_libraries(ats_prune PRIVATE atsa)

add_executable(ats_roundtrip ats_roundtrip.c)
target_link_libraries(ats_roundtrip PRIVATE atsa)

# ── C++17 tools ──────────────────────────────────────────────────────────────

add_executable(ats_transform ats_transform.cpp
  ${ATS_CORE_DIR}/AtsFrequencyShape.cpp
  ${ATS_CORE_DIR}/AtsParams.cpp)
target_include_directories(ats_transform PRIVATE ${ATS_CORE_DIR})
target_link_libraries(ats_transform PRIVATE atsa)
target_compile_features(ats_transform PRIVATE cxx_std_17)

add_executable(ats_envelope ats_envelope.cpp
  ${ATS_CORE_DIR}/AtsSpectralEnvelope.cpp)
target_include_directories(ats_envelope PRIVATE ${ATS_CORE_DIR})
target_link_libraries(ats_envelope PRIVATE atsa)
target_compile_features(ats_envelope PRIVATE cxx_std_17)

add_executable(ats_transient ats_transient.cpp
  ${ATS_CORE_DIR}/AtsTransientLayer.cpp
  ${ATS_CORE_DIR}/AtsTransientSmear.cpp)
target_include_directories(ats_transient PRIVATE ${ATS_CORE_DIR})
target_link_libraries(ats_transient PRIVATE atsa)
target_compile_features(ats_transient PRIVATE cxx_std_17)

add_executable(ats_morph_plan ats_morph_plan.cpp
  ${ATS_CORE_DIR}/AtsMorphTransport.cpp
  ${ATS_CORE_DIR}/AtsSpectralEnvelope.cpp)
target_include_directories(ats_morph_plan PRIVATE ${ATS_CORE_DIR})
target_link_libraries(ats_morph_plan PRIVATE atsa)
target_compile_features(ats_morph_plan PRIVATE cxx_std_17)

add_executable(ats_transient_demo ats_transient_demo.cpp
  ${ATS_CORE_DIR}/AtsTransientLayer.cpp
  ${ATS_CORE_DIR}/AtsTransientSmear.cpp)
target_include_directories(ats_transient_demo PRIVATE ${ATS_CORE_DIR})
target_link_libraries(ats_transient_demo PRIVATE atsa)
target_compile_features(ats_transient_demo PRIVATE cxx_std_17)

# ── Platform flags ────────────────────────────────────────────────────────────

if(UNIX AND NOT APPLE)
  foreach(_t ats_offline ats_synth ats_analyze ats_prune ats_roundtrip
             ats_transform ats_envelope ats_transient ats_morph_plan ats_transient_demo)
    target_link_libraries(${_t} PRIVATE m)
  endforeach()
endif()

if(MSVC)
  foreach(_t ats_offline ats_synth ats_analyze ats_prune ats_roundtrip
             ats_transform ats_envelope ats_transient ats_morph_plan)
    target_compile_definitions(${_t} PRIVATE _USE_MATH_DEFINES)
  endforeach()
endif()
