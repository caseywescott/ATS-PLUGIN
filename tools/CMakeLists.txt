add_executable(ats_offline ats_offline.c)
target_link_libraries(ats_offline PRIVATE atsa)

add_executable(
  ats_modern_offline
  ats_modern_offline.cpp
  ../src/core/AtsAnalysisPipeline.cpp
  ../src/core/AtsModel.cpp
  ../src/core/AtsModernAcceptance.cpp
  ../src/core/AtsModernModel.cpp
  ../src/core/AtsModernSynth.cpp
  ../src/core/AtsParams.cpp
  ../src/core/AtsResidualBandNoise.cpp)
target_include_directories(ats_modern_offline
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core" "${ATS_ATSA_SOURCE_DIR}")
target_link_libraries(ats_modern_offline PRIVATE atsa)
target_compile_features(ats_modern_offline PRIVATE cxx_std_17)

add_executable(ats_roundtrip ats_roundtrip.c)
target_link_libraries(ats_roundtrip PRIVATE atsa)
if(UNIX AND NOT APPLE)
  target_link_libraries(ats_offline PRIVATE m)
endif()
if(MSVC)
  target_compile_definitions(ats_offline PRIVATE _USE_MATH_DEFINES)
  target_compile_definitions(ats_roundtrip PRIVATE _USE_MATH_DEFINES)
endif()
if(UNIX AND NOT APPLE)
  target_link_libraries(ats_roundtrip PRIVATE m)
endif()
