cmake_minimum_required(VERSION 3.22)

add_executable(ats_resynth_standalone MACOSX_BUNDLE AtsStandaloneMain.mm)
target_compile_features(ats_resynth_standalone PRIVATE cxx_std_17)
target_include_directories(ats_resynth_standalone PRIVATE
  "${CMAKE_SOURCE_DIR}/src/core"
  "${CMAKE_SOURCE_DIR}/src/host"
  "${CMAKE_SOURCE_DIR}/src/ui")
target_link_libraries(ats_resynth_standalone PRIVATE
  ats_host_vst3 ats_file_service ats_visage_editor visage "-framework Cocoa")
set_target_properties(ats_resynth_standalone PROPERTIES
  OUTPUT_NAME "ATS Resynth"
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.atsplugin.resynth"
  MACOSX_BUNDLE_BUNDLE_NAME "ATS Resynth"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_INFO_PLIST
    "${CMAKE_CURRENT_SOURCE_DIR}/ATSResynthStandaloneInfo.plist.in")

add_executable(ats_resynth_standalone_smoke AtsStandaloneMain.mm)
target_compile_features(ats_resynth_standalone_smoke PRIVATE cxx_std_17)
target_include_directories(ats_resynth_standalone_smoke PRIVATE
  "${CMAKE_SOURCE_DIR}/src/core"
  "${CMAKE_SOURCE_DIR}/src/host"
  "${CMAKE_SOURCE_DIR}/src/ui")
target_link_libraries(ats_resynth_standalone_smoke PRIVATE
  ats_host_vst3 ats_file_service ats_visage_editor visage "-framework Cocoa")
enable_testing()
add_test(NAME ats_resynth_standalone_smoke
  COMMAND ats_resynth_standalone_smoke --smoke)
