# Estado v2 — desarrollo avanzado (2026-11-05)

Rama: `claude/solo-ai-studio-avanzado` (sin fusionar a `main`). Entorno: Linux (Ubuntu 24.04, GCC 13.3, JUCE 8.0.15).
**No hay Windows ni FL Studio en este entorno.** Windows se compila y prueba en el CI (windows-2022).
**No se ha hecho ninguna evaluación de escucha humana:** todas las métricas musicales son objetivas, no de calidad percibida.

Clasificación usada:
- **Validado:** implementado, con pruebas automáticas reproducibles que pasan.
- **Parcial:** implementado; probado solo en parte (p. ej. sin FL Studio o solo con datos sintéticos).
- **Experimental:** funciona, pero su calidad es dudosa o los resultados son modestos.
- **Pendiente:** no implementado.

## Núcleo y motor

| Función | Estado | Evidencia |
|---|---|---|
| Compositor jerárquico (plan de secciones, frases, motivos, clímax, cadencias), 8 estilos | Validado | `v2_*`: 672 solos, 97,3 % de notas del acorde en tiempo fuerte, 672/672 resuelven; los estilos difieren en métricas |
| Recurrencia de motivos | Experimental | Motor 2 = 0,033 frente a 0,006 del motor 1: mejora, pero baja en términos absolutos |
| Mantener motivos, bloqueos de compases y notas, regenerar notas, A/B, favoritas, historial de 64 | Validado | `v2_keep_motifs…`, `v2_locked_bars…`, `variation_history_protects_favorites_D3` |
| Intérprete separado del compositor, 5 perfiles, límites por instrumento | Validado | `performer_*` |
| Motor 1 (S0–S2) conservado; estados v1 se migran y reproducen el mismo solo | Validado | `state_v1_migration_reproduces_s0_s2` (golden hash) |
| Efectos: 10 etapas con bypass, 8 presets, automatización suavizada | Validado | `fx_*`; presets calibrados (rango 2,6 LU con señal de prueba, -18,0 LUFS en demos) |
| En vivo = exportación (mismo `SoloEngine`) | Validado | `engine_live_matches_export`: diferencia -333 dB (bit a bit) |
| Hilo de audio sin reservas de memoria ni bloqueos | Validado | `engine_no_allocations_in_audio_thread` (contador de `operator new`) |
| Denormales anulados dentro del motor | Validado | `flush_denormals_guard_works`; p99 de CPU por bloque 24 % → 1,7 % (`solo_cli bench`) |
| MIDI con pitch bend, legato y marcas de texto | Parcial | `midi_pitch_bend_export`; sin probar en el Piano Roll de FL. Ver [MIDI.md](MIDI.md) |
| Hum-to-Solo (WAV → notas, 4 modos + solo ritmo) | Parcial | `voice_*`, `hum_to_solo_modes`: **solo con voces sintéticas** (8/8 notas, ataque ±11 ms). **No probado con voces humanas reales** |
| Detección de voz sin altura (susurros, sílabas) | Experimental | Detecta 11 ataques de 8 reales; cae a «solo ritmo» como se espera |
| Creative DNA (extraer, imitar/continuar/responder/transformar/libre) | Parcial | `dna_*`; el parecido «imitar» es 0,19 (bajo): imita el carácter más que la melodía |
| Corrección vocal localizada (respeta bloqueos, deshacible) | Validado (sintético) | `vocal_localized_correction_preserves_rest` |
| Dirección por texto en español: interpretar → vista previa → ejecutar → deshacer | Validado | `commands_*`: 21/21 órdenes; probado también en la interfaz |
| Reconocimiento de voz (micrófono → texto) | Pendiente | Solo existe la interfaz `SpeechRecognizer`; no hay motor local incluido |
| Estado v2, robustez ante estados y MIDI corruptos | Validado | `state_*` (3000 estados corruptos), `midi_reader_*` (3000 MIDI corruptos) |
| ASan + UBSan sobre las 74 pruebas | Validado | 0 errores en local; job `sanitizers` en CI |

## Plugin

| Función | Estado | Evidencia |
|---|---|---|
| Procesador sobre `SoloEngine` + `ProjectController` | Validado (Linux) | `SoloHostTest`: 12 combinaciones de frecuencia × bloque, MIDI exacto, sin notas colgadas, estado conservado |
| Modo fácil: instrumento, estilo, generar, escuchar, tararear/importar voz, variaciones, favorita, intensidad, exportar | Parcial | Capturas reales del standalone; sin FL Studio |
| Modo Pro: pestañas Generación, Notas (bloqueos, articulaciones, transponer), Efectos, Automatización (curva), Voz/ADN, Historial A/B, Comandos | Parcial | Capturas reales y flujo de comando probado con clics simulados (xdotool) en el standalone |
| Deshacer/rehacer (40 pasos) | Validado | `undo_redo_stack`, `project_*`; probado en la interfaz |
| Grabar voz desde el micrófono dentro del plugin | Pendiente | Hoy se graba en el DAW y se importa el WAV |
| Pitch bend en la salida MIDI en directo | Pendiente | Solo notas on/off en directo; la expresión va en el MIDI exportado |
| Carga, transporte, persistencia y exportación dentro de FL Studio 24 | **No probado** | Requiere prueba manual (pasos en el informe) |

## Defectos encontrados en esta fase (todos con prueba que los reproduce)

D1 notas colgadas al reiniciar la vista previa · D3 el historial podía descartar favoritas · cadencia final sin resolver en frases de una nota ·
anticipaciones que eliminaban la respiración · palm mute que nunca se activaba · diferencia en vivo/exportación (-14,6 dB) por bordes de bloque, DC del overdrive y fase del chorus ·
presets con picos de +12 dBFS · ataques de voz con 31 ms de retraso · compound literal que MSVC no acepta · `<algorithm>` ausente (MSVC) ·
**UBSan:** desplazamiento indefinido en el lector MIDI (compás con exponente 183) · desajuste `new`/`free` en el contador de pruebas ·
proyecto nuevo con preset «Emotional Lead» mostrado pero efectos vacíos · **denormales** que multiplicaban el coste de algunos bloques.

## Rendimiento (Linux, 48 kHz, bloques de 256, guitarra modelada, rock virtuoso a 140 BPM)

| Preset | CPU media | p99 | p99,9 |
|---|---|---|---|
| Sin efectos | 0,27 % | 0,6 % | 0,8 % |
| Presets (rango) | 0,7–1,2 % | 1,1–1,7 % | 1,4–7,7 % |

Picos aislados (un bloque de 11 250, en una posición distinta en cada ejecución) llegan al 5–85 %. Atribuidos al
reparto de CPU de este contenedor compartido, **sin confirmar**: en Windows con FL Studio hay que medirlo.
Memoria: `SoloEngine` 3,7 KB + búferes (delay de 4 s, reverb, chorus); pico RSS del proceso de prueba 6,6 MB.
