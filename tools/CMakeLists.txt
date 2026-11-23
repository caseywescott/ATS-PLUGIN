add_executable(ats_offline ats_offline.c)
target_link_libraries(ats_offline PRIVATE atsa)

add_executable(ats_roundtrip ats_roundtrip.c)
target_link_libraries(ats_roundtrip PRIVATE atsa)

add_executable(spectrum_candidate_export spectrum_candidate_export.cpp)
target_include_directories(spectrum_candidate_export PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/core"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/ui")
target_compile_features(spectrum_candidate_export PRIVATE cxx_std_17)

add_executable(ats_transient_demo ats_transient_demo.cpp
  ../src/core/AtsTransientLayer.cpp
  ../src/core/AtsTransientSmear.cpp)
target_include_directories(ats_transient_demo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src/core")
target_link_libraries(ats_transient_demo PRIVATE atsa)
target_compile_features(ats_transient_demo PRIVATE cxx_std_17)
if(UNIX AND NOT APPLE)
  target_link_libraries(ats_transient_demo PRIVATE m)
endif()
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

add_executable(ats_autotune ats_autotune.cpp)
target_link_libraries(ats_autotune PRIVATE ats_engine)
target_compile_features(ats_autotune PRIVATE cxx_std_17)

add_executable(ats_describe ats_describe.cpp)
target_link_libraries(ats_describe PRIVATE ats_engine)
target_compile_features(ats_describe PRIVATE cxx_std_17)
