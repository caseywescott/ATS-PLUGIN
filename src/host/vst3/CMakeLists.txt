cmake_minimum_required(VERSION 3.22)

include("${CMAKE_SOURCE_DIR}/cmake/ATSVst3Sdk.cmake")
ats_add_vst3_sdk()

add_library(ats_resynth_vst3 MODULE AtsVst3Plugin.cpp)
target_compile_features(ats_resynth_vst3 PRIVATE cxx_std_17)
target_include_directories(ats_resynth_vst3 PRIVATE
  "${CMAKE_SOURCE_DIR}/src/core"
  "${CMAKE_SOURCE_DIR}/src/host")
target_link_libraries(ats_resynth_vst3 PRIVATE ats_vst3_sdk ats_host_vst3 ats_visage_editor)
set_target_properties(ats_resynth_vst3 PROPERTIES
  OUTPUT_NAME "ATS Resynth"
  PREFIX "")

if(APPLE)
  target_link_libraries(ats_resynth_vst3 PRIVATE
    "-framework Foundation" "-framework CoreFoundation")
  set_target_properties(ats_resynth_vst3 PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION vst3
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.atsplugin.resynth"
    MACOSX_BUNDLE_BUNDLE_NAME "ATS Resynth"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_INFO_PLIST
      "${CMAKE_CURRENT_SOURCE_DIR}/ATSResynthVST3Info.plist.in")
elseif(UNIX)
  set_target_properties(ats_resynth_vst3 PROPERTIES
    SUFFIX ".so"
    LIBRARY_OUTPUT_DIRECTORY
      "${CMAKE_BINARY_DIR}/ATS Resynth.vst3/Contents/x86_64-linux")
else()
  set_target_properties(ats_resynth_vst3 PROPERTIES SUFFIX ".vst3")
endif()

add_executable(ats_vst3_artifact_test
  "${CMAKE_SOURCE_DIR}/tests/host/vst3_artifact_test.cpp")
target_compile_features(ats_vst3_artifact_test PRIVATE cxx_std_17)
target_link_libraries(ats_vst3_artifact_test PRIVATE ats_vst3_sdk)
add_dependencies(ats_vst3_artifact_test ats_resynth_vst3)
enable_testing()
add_test(NAME ats_vst3_artifact_test
  COMMAND ats_vst3_artifact_test "$<TARGET_FILE:ats_resynth_vst3>")

if(APPLE)
  add_executable(ats_vst3_editor_view_probe
    "${CMAKE_SOURCE_DIR}/tests/host/vst3_editor_view_probe.mm")
  target_compile_features(ats_vst3_editor_view_probe PRIVATE cxx_std_17)
  target_link_libraries(ats_vst3_editor_view_probe PRIVATE ats_vst3_sdk
    "-framework Cocoa")
  add_dependencies(ats_vst3_editor_view_probe ats_resynth_vst3)
  add_test(NAME ats_vst3_editor_view_probe
    COMMAND ats_vst3_editor_view_probe "$<TARGET_FILE:ats_resynth_vst3>")
endif()
