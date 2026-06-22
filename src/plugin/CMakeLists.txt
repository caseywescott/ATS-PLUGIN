include(FetchContent)

set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 7.0.12
)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
  set(_ATS_RESYNTH_FORMATS VST3 Standalone AU)
else()
  set(_ATS_RESYNTH_FORMATS VST3 Standalone)
endif()

juce_add_plugin(
  AtsResynth
  COMPANY_NAME "ATSPlugin"
  BUNDLE_ID com.atsplugin.resynth
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  PLUGIN_MANUFACTURER_CODE AtPl
  PLUGIN_CODE Atr1
  FORMATS ${_ATS_RESYNTH_FORMATS}
  PRODUCT_NAME "ATS Resynth"
  COPY_PLUGIN_AFTER_BUILD TRUE
  MICROPHONE_PERMISSION_ENABLED FALSE
  HARDENED_RUNTIME_ENABLED TRUE)

juce_generate_juce_header(AtsResynth)

juce_add_binary_data(
  AtsResynthAssets
  SOURCES
    "${CMAKE_SOURCE_DIR}/slider/manifest.json"
    "${CMAKE_SOURCE_DIR}/slider/static/track_recess_1x.png"
    "${CMAKE_SOURCE_DIR}/slider/static/track_recess_2x.png"
    "${CMAKE_SOURCE_DIR}/slider/static/active_fill_1x.png"
    "${CMAKE_SOURCE_DIR}/slider/static/active_fill_2x.png"
    "${CMAKE_SOURCE_DIR}/slider/static/handle_1x.png"
    "${CMAKE_SOURCE_DIR}/slider/static/handle_2x.png"
    # Checkbox neumorphic assets (assets/checkbox/static/)
    "${CMAKE_SOURCE_DIR}/assets/checkbox/static/outer_bevel_1x.png"
    "${CMAKE_SOURCE_DIR}/assets/checkbox/static/outer_bevel_2x.png"
    "${CMAKE_SOURCE_DIR}/assets/checkbox/static/main_body_1x.png"
    "${CMAKE_SOURCE_DIR}/assets/checkbox/static/main_body_2x.png"
    "${CMAKE_SOURCE_DIR}/assets/checkbox/static/inner_cap_1x.png"
    "${CMAKE_SOURCE_DIR}/assets/checkbox/static/inner_cap_2x.png"
    "${CMAKE_SOURCE_DIR}/assets/checkbox/static/checkmark_1x.png"
    "${CMAKE_SOURCE_DIR}/assets/checkbox/static/checkmark_2x.png"
    # Embedded fonts
    "${CMAKE_SOURCE_DIR}/assets/fonts/Outfit-SemiBold.ttf"
    "${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Medium.ttf")

target_sources(
  AtsResynth
  PRIVATE UiFonts.h
          PluginEntry.cpp
          PluginProcessor.cpp
          PluginEditor.cpp
          SessionTabPanel.cpp
          NeumorphicSliderLookAndFeel.cpp
          NeumorphicCheckboxLookAndFeel.cpp
          AnalysisTabPanel.cpp
          SynthTabPanel.cpp
          PartialEditPanel.cpp
          MathExprPanel.cpp
          PresetLibrary.cpp
          SpectrogramPanel.cpp
          TimbreMotionPanel.cpp
          ../core/AtsFrequencyShape.cpp
          ../core/AtsModel.cpp
          ../core/AtsMorphRecipe.cpp
          ../core/AtsMorphTransport.cpp
          ../core/AtsMorphTransportCorrespondence.cpp
          ../core/AtsParams.cpp
          ../core/AtsSpectralEnvelope.cpp
          ../core/AtsTransientSmear.cpp
          ../core/ResampleLinear.cpp
          ../core/AtsMathExpr.cpp
          ../core/TimbreMotionPlan.cpp)

target_include_directories(AtsResynth PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../core"
                                              "${ATS_ATSA_SOURCE_DIR}")

target_link_libraries(AtsResynth PRIVATE juce::juce_audio_utils atsa AtsResynthAssets)

target_compile_definitions(AtsResynth PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
                                             JUCE_MODAL_LOOPS_PERMITTED=1
                                             JUCE_VST3_CAN_REPLACE_VST2=0)

target_compile_features(AtsResynth PRIVATE cxx_std_17)

if(MSVC)
  target_compile_definitions(AtsResynth PRIVATE _USE_MATH_DEFINES)
endif()
