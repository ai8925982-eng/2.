cmake_minimum_required(VERSION 3.22)

project(SpectrumScope VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- JUCE (ดึงอัตโนมัติตอน configure ครั้งแรก ต้องต่ออินเทอร์เน็ต) ----
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.8
    GIT_SHALLOW    ON
)
FetchContent_MakeAvailable(JUCE)

# ---- ปลั๊กอิน: VST3 + Standalone ----
juce_add_plugin(SpectrumScope
    COMPANY_NAME                "YourCompany"
    PLUGIN_MANUFACTURER_CODE    Ysco
    PLUGIN_CODE                 Sscp
    PRODUCT_NAME                "SpectrumScope"
    FORMATS                     VST3 Standalone
    VST3_CATEGORIES             Fx Analyzer
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE   # เปลี่ยนเป็น TRUE ถ้าต้องการคัดลอกไป C:\Program Files\Common Files\VST3 อัตโนมัติ (ต้องรันแบบ Administrator)
)

target_sources(SpectrumScope PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
)

target_compile_definitions(SpectrumScope PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(SpectrumScope
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
