cmake_minimum_required(VERSION 3.22)

project(SoloAIStudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(SOLO_BUILD_TESTS  "Compilar pruebas unitarias del núcleo"      ON)
option(SOLO_BUILD_TOOLS  "Compilar la herramienta de línea de comandos" ON)
option(SOLO_BUILD_PLUGIN "Compilar el plugin (requiere JUCE)"         ON)
set(SOLO_JUCE_DIR "" CACHE PATH "Ruta a una copia local de JUCE (vacío = descargar la versión fijada)")

if(MSVC)
    # Runtime estático: el .vst3 no depende de que el usuario tenga instalado el redistribuible de VC++.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------- núcleo (sin JUCE)
add_library(solo_core STATIC
    core/src/MusicTheory.cpp
    core/src/Sequence.cpp
    core/src/MidiFile.cpp
    core/src/LegacyComposer.cpp
    core/src/ComposerV2.cpp
    core/src/Performer.cpp
    core/src/InstrumentEngine.cpp
    core/src/Effects.cpp
    core/src/SoloEngine.cpp
    core/src/Playback.cpp
    core/src/AudioRenderer.cpp
    core/src/StateManager.cpp
    core/src/Analysis.cpp)
target_include_directories(solo_core PUBLIC core/include)
if(MSVC)
    target_compile_options(solo_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(solo_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Sanitizers (solo GCC/Clang): -DSOLO_SANITIZE=ON compila núcleo, pruebas y herramientas con ASan + UBSan.
option(SOLO_SANITIZE "Compilar con AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if(SOLO_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
    target_compile_options(solo_core PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
endif()

if(SOLO_BUILD_TESTS)
    enable_testing()
    add_executable(solo_tests tests/TestMain.cpp tests/TestComposerV2.cpp tests/TestAudioV2.cpp)
    target_link_libraries(solo_tests PRIVATE solo_core)
    if(MSVC)
        target_compile_options(solo_tests PRIVATE /utf-8)
    else()
        target_compile_options(solo_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME solo_core_tests COMMAND solo_tests)
endif()

if(SOLO_BUILD_TOOLS)
    add_executable(solo_cli tools/solo_cli.cpp)
    target_link_libraries(solo_cli PRIVATE solo_core)
    if(MSVC)
        target_compile_options(solo_cli PRIVATE /utf-8)
    else()
        target_compile_options(solo_cli PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

# ---------------------------------------------------------------- plugin JUCE
if(SOLO_BUILD_PLUGIN)
    if(SOLO_JUCE_DIR)
        add_subdirectory(${SOLO_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        8.0.15
            GIT_SHALLOW    ON)
        FetchContent_MakeAvailable(JUCE)
    endif()
    add_subdirectory(plugin)
endif()
