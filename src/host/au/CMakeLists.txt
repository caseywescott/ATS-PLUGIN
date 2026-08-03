cmake_minimum_required(VERSION 3.22)

include("${CMAKE_SOURCE_DIR}/cmake/ATSAudioUnitSdk.cmake")
ats_add_audio_unit_sdk()

add_library(ats_resynth_au MODULE AtsAudioUnit.mm)
target_compile_features(ats_resynth_au PRIVATE cxx_std_17)
target_include_directories(ats_resynth_au PRIVATE
  "${CMAKE_SOURCE_DIR}/src/core"
  "${CMAKE_SOURCE_DIR}/src/host"
  "${CMAKE_SOURCE_DIR}/src/ui")
target_link_libraries(ats_resynth_au PRIVATE
  ats_audio_unit_sdk ats_host_au ats_file_service ats_preset_service ats_visage_editor visage
  "-framework Cocoa" "-framework AudioToolbox" "-framework AudioUnit")
set_target_properties(ats_resynth_au PROPERTIES
  OUTPUT_NAME "ATS Resynth"
  PREFIX ""
  BUNDLE TRUE
  BUNDLE_EXTENSION component
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.atsplugin.resynth"
  MACOSX_BUNDLE_BUNDLE_NAME "ATS Resynth"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_INFO_PLIST
    "${CMAKE_CURRENT_SOURCE_DIR}/ATSResynthAUInfo.plist.in")

add_executable(ats_au_artifact_test
  "${CMAKE_SOURCE_DIR}/tests/host/au_artifact_test.mm")
target_compile_features(ats_au_artifact_test PRIVATE cxx_std_17)
target_link_libraries(ats_au_artifact_test PRIVATE
  "-framework AudioToolbox" "-framework CoreFoundation")
add_dependencies(ats_au_artifact_test ats_resynth_au)
enable_testing()
add_test(NAME ats_au_artifact_test
  COMMAND ats_au_artifact_test "$<TARGET_FILE:ats_resynth_au>")

add_executable(ats_au_editor_view_probe
  "${CMAKE_SOURCE_DIR}/tests/host/au_editor_view_probe.mm")
target_compile_features(ats_au_editor_view_probe PRIVATE cxx_std_17)
target_link_libraries(ats_au_editor_view_probe PRIVATE
  "-framework Cocoa" "-framework AudioToolbox" "-framework AudioUnit"
  "-framework CoreFoundation")
add_dependencies(ats_au_editor_view_probe ats_resynth_au)
add_test(NAME ats_au_editor_view_probe
  COMMAND ats_au_editor_view_probe "$<TARGET_FILE:ats_resynth_au>")
