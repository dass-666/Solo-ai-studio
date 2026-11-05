# Estado S0–S2 (2026-11-05)

Entorno de desarrollo: contenedor Linux (Ubuntu 24.04, GCC 13.3, CMake 3.28, JUCE 8.0.15).
No hay Windows ni FL Studio en este entorno: la compilación Windows la hace el CI de GitHub Actions.

## Implementado y probado (evidencia reproducible en Linux)

| Función | Evidencia |
|---|---|
| Teoría musical: tonalidades (también en español), acordes (triadas, 7ª, sus, 5, slash), progresiones con `%`, avisos fuera de tonalidad | `theory_*` |
| Compositor determinista por semilla; 3 solos distintos sobre la misma progresión | `composer_deterministic`, `composer_three_distinct_solos_same_progression` |
| Invariantes musicales en 504 solos (4/4 y 3/4; 2/4/8/16 compases; 3 instrumentos; 4 estilos) | `composer_musical_invariants` |
| Densidad controla el número de notas; regeneración parcial conserva el resto | `composer_density_increases_notes`, `composer_regenerate_fragment_keeps_rest` |
| MIDI SMF tipo 0 y 1: ida y vuelta exacta (ticks, notas, velocity, tempo, compás, programa) | `midi_roundtrip_format1_and_0`, `midi_varlen_and_long_files` |
| Reproductor: precisión de ±1 muestra con bloques 1–2048 y 44,1/48/96 kHz; loops; stop; saltos; cambio de tempo; cambio de secuencia sin notas colgadas | `player_*` |
| Render WAV: mismas notas que el MIDI, sin clipping (pico < 1), sin NaN, silencio antes y después, independiente del tamaño de bloque | `render_*`, `wav_header` |
| Estado: ida y vuelta completa; rechazo de estados truncados, corruptos o de versión futura sin aplicar nada | `state_*`, `presets_*`, `variation_history` |
| VST3 compila y **lo carga un host** (VST3 de JUCE): se declara instrumento, MIDI out 4/4 notas en la muestra exacta, stop cierra notas, audio válido, getState conserva la secuencia, editor se crea. 12 combinaciones sample rate × bloque | `SoloHostTest` (78 comprobaciones) |
| Interfaz Modo Fácil y Pro funcionando en la versión Standalone (capturas reales) | `docs/img/*.png` |

Resultados: `solo_tests` → 29 pruebas, 123.210 comprobaciones, 0 fallos. `SoloHostTest` → 0 fallos.

## Implementado pero no probado en FL Studio (ni en Windows hasta que pase el CI)

- Carga y escaneo en FL Studio 24; tamaño/redimensionado de la ventana dentro del wrapper de FL.
- Sincronización con el transporte de FL (play, stop, saltos, loop de patrón, cambios de tempo).
- Persistencia dentro del `.flp`.
- Exportación MIDI/WAV desde el diálogo de archivos de Windows e importación en el Piano Roll de FL.
- Salida MIDI VST3 hacia otro canal de FL (FL debe soportarla; no se ha verificado).
- Arrastrar MIDI al DAW (`performExternalDragDropOfFiles`).
- Reproducibilidad de semillas entre MSVC y GCC (la prueba "golden hash" lo comprobará en el CI de Windows).
- Rendimiento de CPU real (estimación: 8 voces de síntesis simple, muy por debajo del 1 % de un núcleo; sin medir).

## Pendiente

- Validación real en FL Studio 24 (checklist en `docs/INSTALL.md`).
- pluginval: se ejecuta en el CI de Windows como paso informativo; no se pudo ejecutar en este entorno.
- Licencia del código propio y licencia comercial de JUCE antes de distribuir; firma de código.
- Deshacer/rehacer en el piano roll; selección múltiple.
- Cambios de acorde dentro del compás; compases distintos de 4/4 y 3/4.
- Sonidos más realistas (samples con licencia, articulaciones, bends, vibrato por nota).
