cmake_minimum_required(VERSION 3.22)

include("${CMAKE_SOURCE_DIR}/cmake/ATSVst3Sdk.cmake")
ats_add_vst3_sdk()

add_library(ats_resynth_vst3 MODULE AtsVst3Plugin.cpp)
target_compile_features(ats_resynth_vst3 PRIVATE cxx_std_17)
target_include_directories(ats_resynth_vst3 PRIVATE
  "${CMAKE_SOURCE_DIR}/src/core"
  "${CMAKE_SOURCE_DIR}/src/host")
target_link_libraries(ats_resynth_vst3 PRIVATE ats_vst3_sdk ats_host_vst3 ats_file_service ats_preset_service ats_visage_editor)
set_target_properties(ats_resynth_vst3 PROPERTIES
  OUTPUT_NAME "ATS Resynth"
  PREFIX "")

if(APPLE)
  # macmain.cpp supplies the mandatory VST3 bundleEntry/bundleExit exports.
  # It cannot remain solely inside the static SDK library: the linker has no
  # referenced symbol that would pull that archive member into the bundle.
  get_property(_ats_vst3_sdk_root TARGET ats_vst3_sdk PROPERTY ATS_VST3_SDK_ROOT)
  target_sources(ats_resynth_vst3 PRIVATE
    "${_ats_vst3_sdk_root}/public.sdk/source/main/macmain.cpp")
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
  target_link_libraries(ats_vst3_artifact_test PRIVATE
    "-framework CoreFoundation")

  add_executable(ats_vst3_editor_view_probe MACOSX_BUNDLE
    "${CMAKE_SOURCE_DIR}/tests/host/vst3_editor_view_probe.mm")
  target_compile_features(ats_vst3_editor_view_probe PRIVATE cxx_std_17)
  target_link_libraries(ats_vst3_editor_view_probe PRIVATE ats_vst3_sdk
    "-framework Cocoa")
  set_target_properties(ats_vst3_editor_view_probe PROPERTIES
    OUTPUT_NAME "ATS VST3 Editor Smoke"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.atsplugin.vst3-editor-smoke"
    MACOSX_BUNDLE_BUNDLE_NAME "ATS VST3 Editor Smoke"
    MACOSX_BUNDLE_INFO_PLIST
      "${CMAKE_SOURCE_DIR}/tests/host/ATSVst3EditorSmokeInfo.plist.in")
  add_dependencies(ats_vst3_editor_view_probe ats_resynth_vst3)
  if(ATS_ENABLE_NATIVE_HOST_SMOKE)
    add_test(NAME ats_vst3_editor_view_probe
      COMMAND /bin/sh "${CMAKE_SOURCE_DIR}/tests/host/run_macos_app_smoke.sh"
        "$<TARGET_BUNDLE_DIR:ats_vst3_editor_view_probe>"
        "$<TARGET_FILE:ats_resynth_vst3>"
        "${CMAKE_CURRENT_BINARY_DIR}/ats_vst3_editor_view_probe.json")
    set_tests_properties(ats_vst3_editor_view_probe PROPERTIES LABELS windowed RUN_SERIAL TRUE)
  endif()
endif()

# Differential wrapper matrix: compiles the plugin TU into the test so the real
# controller encoder and processor decoder run in-process (audit T-205).
add_executable(ats_vst3_differential_test
  "${CMAKE_SOURCE_DIR}/tests/host/vst3_differential_test.cpp"
  "${_ats_vst3_sdk_root}/public.sdk/source/vst/hosting/hostclasses.cpp"
  "${_ats_vst3_sdk_root}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp")
target_compile_features(ats_vst3_differential_test PRIVATE cxx_std_17)
target_compile_options(ats_vst3_differential_test PRIVATE -UNDEBUG)
target_include_directories(ats_vst3_differential_test PRIVATE
  "${CMAKE_SOURCE_DIR}/src/core"
  "${CMAKE_SOURCE_DIR}/src/host"
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ats_vst3_differential_test PRIVATE
  ats_vst3_sdk ats_host_vst3 ats_file_service ats_preset_service ats_visage_editor)
if(APPLE)
  target_link_libraries(ats_vst3_differential_test PRIVATE
    "-framework Foundation" "-framework CoreFoundation")
endif()
add_test(NAME ats_vst3_differential_test COMMAND ats_vst3_differential_test)

# Editor size negotiation on the real plug view (audit T-404).
add_executable(ats_vst3_view_negotiation_test
  "${CMAKE_SOURCE_DIR}/tests/host/vst3_view_negotiation_test.cpp"
  "${_ats_vst3_sdk_root}/public.sdk/source/vst/hosting/hostclasses.cpp"
  "${_ats_vst3_sdk_root}/public.sdk/source/vst/hosting/pluginterfacesupport.cpp")
target_compile_features(ats_vst3_view_negotiation_test PRIVATE cxx_std_17)
target_compile_options(ats_vst3_view_negotiation_test PRIVATE -UNDEBUG)
target_include_directories(ats_vst3_view_negotiation_test PRIVATE
  "${CMAKE_SOURCE_DIR}/src/core"
  "${CMAKE_SOURCE_DIR}/src/host"
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ats_vst3_view_negotiation_test PRIVATE
  ats_vst3_sdk ats_host_vst3 ats_file_service ats_preset_service ats_visage_editor)
if(APPLE)
  target_link_libraries(ats_vst3_view_negotiation_test PRIVATE
    "-framework Foundation" "-framework CoreFoundation")
endif()
add_test(NAME ats_vst3_view_negotiation_test COMMAND ats_vst3_view_negotiation_test)
