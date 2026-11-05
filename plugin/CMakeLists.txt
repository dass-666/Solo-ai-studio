# Plugin Solo AI Studio (JUCE). Formatos: VST3 (prioritario) + Standalone para pruebas sin DAW.
set(SOLO_FORMATS VST3 Standalone)

juce_add_plugin(SoloAIStudio
    PRODUCT_NAME "Solo AI Studio"
    COMPANY_NAME "Solo AI Studio"
    COMPANY_WEBSITE "https://github.com/dass-666"
    BUNDLE_ID "com.soloaistudio.soloaistudio"
    # Identificadores únicos (4 caracteres; el de fabricante con al menos una mayúscula).
    # No cambiarlos tras publicar: los DAW los usan para reconocer el plugin en proyectos guardados.
    PLUGIN_MANUFACTURER_CODE Slai
    PLUGIN_CODE Sols
    VERSION ${PROJECT_VERSION}
    FORMATS ${SOLO_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Synth Generator
    DESCRIPTION "Generador de solos instrumentales con exportacion MIDI/WAV")

target_sources(SoloAIStudio PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/PianoRoll.cpp)

target_compile_definitions(SoloAIStudio PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(SoloAIStudio
    PRIVATE
        solo_core
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(SoloAIStudio PRIVATE /utf-8)
endif()

# Host de prueba sin interfaz: carga el .vst3 compilado y verifica E/S, audio, MIDI y estado.
option(SOLO_BUILD_TEST_HOST "Compilar el host de prueba del VST3" ON)
if(SOLO_BUILD_TEST_HOST)
    juce_add_console_app(SoloHostTest PRODUCT_NAME "SoloHostTest")
    target_sources(SoloHostTest PRIVATE ../tests/HostTest.cpp)
    target_compile_definitions(SoloHostTest PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        SOLO_VST3_PATH="$<TARGET_FILE_DIR:SoloAIStudio_VST3>/../..")
    target_link_libraries(SoloHostTest PRIVATE
        solo_core
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_recommended_config_flags)
    if(MSVC)
        target_compile_options(SoloHostTest PRIVATE /utf-8)
    endif()
    add_dependencies(SoloHostTest SoloAIStudio_VST3)
endif()
