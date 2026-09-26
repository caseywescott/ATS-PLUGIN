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
# Derive packed AU version from the root project version so the plist is never
# a literal. With 0.1.0: (0 << 16) | (1 << 8) | 0 = 256 = 0x100.
math(EXPR ATS_AU_VERSION_PACKED
  "(${PROJECT_VERSION_MAJOR} << 16) | (${PROJECT_VERSION_MINOR} << 8) | ${PROJECT_VERSION_PATCH}")

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
    "${CMAKE_CURRENT_SOURCE_DIR}/ATSResynthAUInfo.plist.in"
  # The AU is distributed with libsndfile inside its bundle.  Do not leave
  # the build-machine/vendor path in the installed binary: Logic validates
  # Audio Units from a separate process which cannot rely on that path.
  BUILD_RPATH "@loader_path/../Frameworks"
  INSTALL_RPATH "@loader_path/../Frameworks"
  BUILD_WITH_INSTALL_RPATH TRUE)

# Post-build: embed libsndfile and sign the build-tree AU artifact only.
# Installation is exclusively owned by scripts/install_logic_au.sh so building
# one architecture can never silently overwrite Logic's active component.
# Identity defaults to ad-hoc signing for repeatable local development.
# Override with -DATS_CODESIGN_IDENTITY="Developer ID Application: ..." for release.
set(ATS_CODESIGN_IDENTITY "-" CACHE STRING
  "codesign identity for the AU component (use - for ad-hoc)")

set(_au_bundle_dir "$<TARGET_BUNDLE_DIR:ats_resynth_au>")
set(_sndfile_src "${SNDFILE_LIBRARY}")
set(_sndfile_dst "${_au_bundle_dir}/Contents/Frameworks/libsndfile.1.dylib")

add_custom_command(TARGET ats_resynth_au POST_BUILD
  # 1. Embed libsndfile into the build-tree bundle so copy_directory picks it up.
  COMMAND ${CMAKE_COMMAND} -E make_directory "${_au_bundle_dir}/Contents/Frameworks"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_sndfile_src}" "${_sndfile_dst}"
  # Finder/AppleDouble sidecars are not bundle resources and make codesign
  # reject the component as containing unsigned nested code.
  COMMAND ${CMAKE_COMMAND} -E rm -f
    "${_au_bundle_dir}/Contents/._Info.plist"
    "${_au_bundle_dir}/Contents/.__CodeSignature"
    "${_au_bundle_dir}/Contents/MacOS/._ATS Resynth"
    "${_au_bundle_dir}/Contents/Frameworks/._libsndfile.1.dylib"
  # External volumes can preserve owner-only source modes (0700/0600). The
  # AudioComponentRegistrar and Logic's AUHostingService must be able to read
  # the plist and embedded library, and execute the plug-in binary.
  COMMAND /bin/chmod 644
    "${_au_bundle_dir}/Contents/Info.plist"
    "${_sndfile_dst}"
  COMMAND /bin/chmod 755 "${_au_bundle_dir}/Contents/MacOS/ATS Resynth"
  # chmod can itself create AppleDouble metadata on non-APFS build volumes.
  COMMAND ${CMAKE_COMMAND} -E rm -f
    "${_au_bundle_dir}/Contents/._Info.plist"
    "${_au_bundle_dir}/Contents/MacOS/._ATS Resynth"
    "${_au_bundle_dir}/Contents/Frameworks/._libsndfile.1.dylib"
  # 2. Pre-sign libsndfile and the main binary inside the build-tree bundle.
  COMMAND codesign --force --sign "${ATS_CODESIGN_IDENTITY}" "${_sndfile_dst}"
  COMMAND ${CMAKE_COMMAND} -E rm -f
    "${_au_bundle_dir}/Contents/Frameworks/._libsndfile.1.dylib"
  COMMAND codesign --force --sign "${ATS_CODESIGN_IDENTITY}"
    "${_au_bundle_dir}/Contents/MacOS/ATS Resynth"
  # Signing on this volume can recreate AppleDouble sidecars; remove them
  # once more before signing the outer bundle.
  COMMAND ${CMAKE_COMMAND} -E rm -f
    "${_au_bundle_dir}/Contents/._Info.plist"
    "${_au_bundle_dir}/Contents/MacOS/._ATS Resynth"
    "${_au_bundle_dir}/Contents/Frameworks/._libsndfile.1.dylib"
  # Sign the outer build-tree bundle so build-tree verification gates pass.
  COMMAND codesign --force --sign "${ATS_CODESIGN_IDENTITY}" "${_au_bundle_dir}"
  COMMAND ${CMAKE_COMMAND} -E rm -f
    "${_au_bundle_dir}/Contents/._Info.plist"
    "${_au_bundle_dir}/Contents/MacOS/._ATS Resynth"
    "${_au_bundle_dir}/Contents/Frameworks/._libsndfile.1.dylib"
  COMMENT "Embedding dependencies and signing build-tree ATS Resynth.component"
  VERBATIM)

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
