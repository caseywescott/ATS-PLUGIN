include(FetchContent)

function(ats_add_audio_unit_sdk)
  if(TARGET ats_audio_unit_sdk)
    return()
  endif()

  if(ATS_AUDIO_UNIT_SDK_DIR AND
     EXISTS "${ATS_AUDIO_UNIT_SDK_DIR}/include/AudioUnitSDK/AUBase.h")
    set(_ats_audio_unit_sdk_root "${ATS_AUDIO_UNIT_SDK_DIR}")
    ats_assert_git_revision("${_ats_audio_unit_sdk_root}" "${ATS_AUDIO_UNIT_SDK_VERSION}" "Apple AudioUnitSDK")
  else()
    FetchContent_Declare(ats_audio_unit_sdk
      GIT_REPOSITORY https://github.com/apple/AudioUnitSDK.git
      GIT_TAG ${ATS_AUDIO_UNIT_SDK_VERSION})
    FetchContent_GetProperties(ats_audio_unit_sdk)
    if(NOT ats_audio_unit_sdk_POPULATED)
      FetchContent_Populate(ats_audio_unit_sdk)
    endif()
    set(_ats_audio_unit_sdk_root "${ats_audio_unit_sdk_SOURCE_DIR}")
  endif()

  if(NOT EXISTS "${_ats_audio_unit_sdk_root}/include/AudioUnitSDK/AUBase.h")
    message(FATAL_ERROR
      "Apple AudioUnitSDK is incomplete: ${_ats_audio_unit_sdk_root}. "
      "Use ATS_AUDIO_UNIT_SDK_DIR or allow the pinned download to complete.")
  endif()

  file(GLOB _ats_audio_unit_sources CONFIGURE_DEPENDS
    "${_ats_audio_unit_sdk_root}/src/AudioUnitSDK/*.cpp")
  add_library(ats_audio_unit_sdk STATIC ${_ats_audio_unit_sources})
  target_include_directories(ats_audio_unit_sdk PUBLIC
    "${_ats_audio_unit_sdk_root}/include")
  target_compile_features(ats_audio_unit_sdk PUBLIC cxx_std_17)
  target_link_libraries(ats_audio_unit_sdk PUBLIC
    "-framework AudioToolbox" "-framework CoreFoundation")
  set_property(TARGET ats_audio_unit_sdk PROPERTY ATS_AUDIO_UNIT_SDK_ROOT
    "${_ats_audio_unit_sdk_root}")
endfunction()
