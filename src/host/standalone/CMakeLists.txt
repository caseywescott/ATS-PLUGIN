cmake_minimum_required(VERSION 3.22)

add_executable(ats_resynth_standalone MACOSX_BUNDLE AtsStandaloneMain.mm)
target_compile_features(ats_resynth_standalone PRIVATE cxx_std_17)
target_include_directories(ats_resynth_standalone PRIVATE
  "${CMAKE_SOURCE_DIR}/src/core"
  "${CMAKE_SOURCE_DIR}/src/host"
  "${CMAKE_SOURCE_DIR}/src/ui")
target_link_libraries(ats_resynth_standalone PRIVATE
  ats_host_vst3 ats_file_service ats_preset_service ats_visage_editor visage "-framework Cocoa")
set_target_properties(ats_resynth_standalone PROPERTIES
  OUTPUT_NAME "ATS Resynth"
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.atsplugin.resynth"
  MACOSX_BUNDLE_BUNDLE_NAME "ATS Resynth"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_INFO_PLIST
    "${CMAKE_CURRENT_SOURCE_DIR}/ATSResynthStandaloneInfo.plist.in"
  INSTALL_RPATH "@loader_path/../Frameworks"
  BUILD_WITH_INSTALL_RPATH TRUE)

set(ATS_CODESIGN_IDENTITY "-" CACHE STRING
  "codesign identity for ATS macOS bundles (use - for ad-hoc)")
set(_standalone_bundle_dir "$<TARGET_BUNDLE_DIR:ats_resynth_standalone>")
set(_standalone_sndfile_src
  "${SNDFILE_LIBRARY}")
set(_standalone_sndfile_dst
  "${_standalone_bundle_dir}/Contents/Frameworks/libsndfile.1.dylib")

add_custom_command(TARGET ats_resynth_standalone POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
    "${_standalone_bundle_dir}/Contents/Frameworks"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${_standalone_sndfile_src}" "${_standalone_sndfile_dst}"
  COMMAND ${CMAKE_COMMAND} -E rm -f
    "${_standalone_bundle_dir}/Contents/._Info.plist"
    "${_standalone_bundle_dir}/Contents/._Frameworks"
    "${_standalone_bundle_dir}/Contents/.__CodeSignature"
    "${_standalone_bundle_dir}/Contents/MacOS/._ATS Resynth"
    "${_standalone_bundle_dir}/Contents/Frameworks/._libsndfile.1.dylib"
  COMMAND codesign --force --sign "${ATS_CODESIGN_IDENTITY}"
    "${_standalone_sndfile_dst}"
  COMMAND ${CMAKE_COMMAND} -E rm -f
    "${_standalone_bundle_dir}/Contents/._Frameworks"
    "${_standalone_bundle_dir}/Contents/.__CodeSignature"
    "${_standalone_bundle_dir}/Contents/Frameworks/._libsndfile.1.dylib"
  COMMAND codesign --force --sign "${ATS_CODESIGN_IDENTITY}"
    "${_standalone_bundle_dir}/Contents/MacOS/ATS Resynth"
  COMMAND ${CMAKE_COMMAND} -E rm -f
    "${_standalone_bundle_dir}/Contents/._Info.plist"
    "${_standalone_bundle_dir}/Contents/._Frameworks"
    "${_standalone_bundle_dir}/Contents/.__CodeSignature"
    "${_standalone_bundle_dir}/Contents/MacOS/._ATS Resynth"
    "${_standalone_bundle_dir}/Contents/Frameworks/._libsndfile.1.dylib"
  COMMAND codesign --force --sign "${ATS_CODESIGN_IDENTITY}"
    "${_standalone_bundle_dir}"
  COMMENT "Embedding dependencies and signing ATS Resynth.app"
  VERBATIM)

add_executable(ats_resynth_standalone_smoke AtsStandaloneMain.mm)
target_compile_features(ats_resynth_standalone_smoke PRIVATE cxx_std_17)
target_include_directories(ats_resynth_standalone_smoke PRIVATE
  "${CMAKE_SOURCE_DIR}/src/core"
  "${CMAKE_SOURCE_DIR}/src/host"
  "${CMAKE_SOURCE_DIR}/src/ui")
target_link_libraries(ats_resynth_standalone_smoke PRIVATE
  ats_host_vst3 ats_file_service ats_preset_service ats_visage_editor visage "-framework Cocoa")
enable_testing()
add_test(NAME ats_resynth_standalone_smoke
  COMMAND ats_resynth_standalone_smoke --smoke)
add_test(NAME ats_resynth_standalone_close_smoke
  COMMAND ats_resynth_standalone_smoke --close-smoke)
# Both smokes launch the standalone window; they need a window-server session.
set_tests_properties(ats_resynth_standalone_smoke ats_resynth_standalone_close_smoke
  PROPERTIES LABELS windowed)
