include(FetchContent)

function(ats_add_vst3_sdk)
  if(TARGET ats_vst3_sdk)
    return()
  endif()

  if(ATS_VST3_SDK_DIR AND EXISTS "${ATS_VST3_SDK_DIR}/public.sdk/source/main/pluginfactory.cpp")
    set(_ats_vst3_sdk_root "${ATS_VST3_SDK_DIR}")
  else()
    FetchContent_Declare(ats_vst3_sdk
      GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
      GIT_TAG ${ATS_VST3_SDK_VERSION}
      GIT_SUBMODULES base public.sdk pluginterfaces)
    FetchContent_GetProperties(ats_vst3_sdk)
    if(NOT ats_vst3_sdk_POPULATED)
      FetchContent_Populate(ats_vst3_sdk)
    endif()
    set(_ats_vst3_sdk_root "${ats_vst3_sdk_SOURCE_DIR}")
  endif()

  if(NOT EXISTS "${_ats_vst3_sdk_root}/public.sdk/source/main/pluginfactory.cpp")
    message(FATAL_ERROR
      "ATS VST3 SDK is incomplete: ${_ats_vst3_sdk_root}. "
      "Use ATS_VST3_SDK_DIR or allow the pinned download to complete.")
  endif()

  file(GLOB _ats_vst3_base_sources CONFIGURE_DEPENDS
    "${_ats_vst3_sdk_root}/base/source/*.cpp"
    "${_ats_vst3_sdk_root}/base/thread/source/*.cpp"
    "${_ats_vst3_sdk_root}/pluginterfaces/base/*.cpp"
    "${_ats_vst3_sdk_root}/public.sdk/source/common/*.cpp")

  set(_ats_vst3_sources
    ${_ats_vst3_base_sources}
    "${_ats_vst3_sdk_root}/public.sdk/source/main/pluginfactory.cpp"
    "${_ats_vst3_sdk_root}/public.sdk/source/main/moduleinit.cpp"
    "${_ats_vst3_sdk_root}/public.sdk/source/vst/vstinitiids.cpp"
    "${_ats_vst3_sdk_root}/public.sdk/source/vst/vstnoteexpressiontypes.cpp"
    "${_ats_vst3_sdk_root}/public.sdk/source/vst/vstaudioeffect.cpp"
    "${_ats_vst3_sdk_root}/public.sdk/source/vst/vstcomponent.cpp"
    "${_ats_vst3_sdk_root}/public.sdk/source/vst/vstcomponentbase.cpp"
    "${_ats_vst3_sdk_root}/public.sdk/source/vst/vstbus.cpp"
    "${_ats_vst3_sdk_root}/public.sdk/source/vst/vstparameters.cpp"
    "${_ats_vst3_sdk_root}/public.sdk/source/vst/vsteditcontroller.cpp"
    "${_ats_vst3_sdk_root}/public.sdk/source/vst/utility/stringconvert.cpp")

  if(APPLE)
    list(APPEND _ats_vst3_sources
      "${_ats_vst3_sdk_root}/public.sdk/source/main/macmain.cpp")
  elseif(UNIX)
    list(APPEND _ats_vst3_sources
      "${_ats_vst3_sdk_root}/public.sdk/source/main/linuxmain.cpp")
  else()
    list(APPEND _ats_vst3_sources
      "${_ats_vst3_sdk_root}/public.sdk/source/main/dllmain.cpp")
  endif()

  add_library(ats_vst3_sdk STATIC ${_ats_vst3_sources})
  target_include_directories(ats_vst3_sdk PUBLIC
    "${_ats_vst3_sdk_root}"
    "${_ats_vst3_sdk_root}/public.sdk"
    "${_ats_vst3_sdk_root}/pluginterfaces")
  target_compile_features(ats_vst3_sdk PUBLIC cxx_std_17)
  target_compile_definitions(ats_vst3_sdk PUBLIC RELEASE=1)
  set_target_properties(ats_vst3_sdk PROPERTIES UNITY_BUILD FALSE)
  if(APPLE)
    target_compile_options(ats_vst3_sdk PUBLIC -Wno-deprecated-declarations)
  endif()
  set_property(TARGET ats_vst3_sdk PROPERTY ATS_VST3_SDK_ROOT
    "${_ats_vst3_sdk_root}")
endfunction()
