# Solo AI Studio

Plugin VST3 (JUCE + CMake, C++17) que compone solos instrumentales sobre una progresión de acordes,
los reproduce con sonidos integrados, permite editarlos en un piano roll y exportarlos como MIDI estándar y WAV.
Funciona sin conexión. Proyecto independiente de Spatial Echo Studio y AI Choir Studio.

> Estado: prototipo S0–S2. **No validado todavía en FL Studio.** Ver [docs/STATUS.md](docs/STATUS.md).

![Modo Pro](docs/img/modo-pro.png)

## Estructura

```
core/            Núcleo musical sin dependencias (probado por separado)
  MusicTheory    escalas, acordes, progresiones, registros
  SoloComposer   compositor algorítmico por motivos y frases (semillas reproducibles)
  Sequence       secuencia MIDI canónica + historial de variaciones
  MidiFile       exportación/lectura Standard MIDI File (tipo 0 y 1)
  InstrumentEngine  síntesis integrada (guitarra pluck, flauta, synth lead)
  Playback       transporte del host, reproductor con precisión de muestra, intercambio lock-free
  AudioRenderer  render WAV offline con el mismo código que la reproducción en tiempo real
  StateManager   estado versionado (parámetros + secuencias + variaciones) y presets
plugin/          Plugin JUCE: procesador, editor (modo Fácil/Pro), piano roll
tests/           Pruebas unitarias del núcleo + host de prueba que carga el .vst3
tools/solo_cli   Generador por línea de comandos (MIDI + WAV) y batería de ejemplos
examples/        MIDI de ejemplo (incluye Am–F–C–G a 146 BPM)
docs/            Instalación, arquitectura, estado, licencias, hoja de ruta
```

## Compilar

Windows (Visual Studio 2022 + CMake + Git):

```powershell
.\scripts\build.ps1            # compila, pruebas, host de prueba
.\scripts\build.ps1 -Install   # además instala el VST3 (PowerShell como administrador)
```

Linux/macOS: `scripts/build.sh`. JUCE 8.0.15 se descarga automáticamente (o `JUCE_DIR=/ruta`).

El CI de GitHub Actions (`.github/workflows/build.yml`) compila el VST3 para Windows x64 y lo publica como artefacto.

Más detalles: [docs/INSTALL.md](docs/INSTALL.md) · [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) ·
[docs/LICENSES.md](docs/LICENSES.md) · [docs/ROADMAP.md](docs/ROADMAP.md)
