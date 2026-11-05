#!/usr/bin/env bash
# Compilación reproducible en Linux/macOS: núcleo, pruebas, plugin y host de prueba.
#   scripts/build.sh            (descarga JUCE 8.0.15 con CMake FetchContent)
#   JUCE_DIR=/ruta/JUCE scripts/build.sh
set -euo pipefail
cd "$(dirname "$0")/.."
extra=()
if [[ -n "${JUCE_DIR:-}" ]]; then extra+=("-DSOLO_JUCE_DIR=${JUCE_DIR}"); fi
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "${extra[@]}"
cmake --build build
ctest --test-dir build --output-on-failure
runner=()
if [[ "$(uname)" == "Linux" && -z "${DISPLAY:-}" ]] && command -v xvfb-run >/dev/null; then runner=(xvfb-run -a); fi
"${runner[@]}" build/plugin/SoloHostTest_artefacts/Release/SoloHostTest "build/plugin/SoloAIStudio_artefacts/Release/VST3/Solo AI Studio.vst3" --editor
