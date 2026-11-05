# Arquitectura y decisiones técnicas

## Principio: núcleo sin JUCE

Todo lo musical vive en `core/` (C++17 puro). El plugin JUCE es una capa fina encima. Ventajas:
se prueba sin DAW ni interfaz, se puede reutilizar en otro formato (AU, CLAP, app) y el CLI genera exactamente
lo mismo que el plugin.

| Módulo pedido | Implementación |
|---|---|
| A. HostTransport | `Playback.h`: `TransportState`, `TransportResolver` (BPM host/manual), `ManualClock` (reloj interno), detección de saltos/loops en `SoloPlayer` |
| B. MusicTheory | `MusicTheory.h/.cpp` |
| C. SoloComposer | `SoloComposer.h/.cpp` |
| D. InstrumentEngine | `InstrumentEngine.h/.cpp` |
| E. MidiEngine | `Sequence.h/.cpp` (secuencia canónica, eventos) + `MidiFile.h/.cpp` (SMF) + salida MIDI en `PluginProcessor` |
| F. AudioRenderer | `AudioRenderer.h/.cpp` |
| G. SoloEditor | `plugin/src/PianoRoll.*`, `plugin/src/PluginEditor.*` |
| H. StateManager | `StateManager.h/.cpp` |

## Decisiones (reversibles) y motivo

- **PPQ = 960**: representa semicorcheas, tresillos y fusas sin redondeo.
- **RNG propio (SplitMix64)** en lugar de `std::uniform_*_distribution`: la salida de esas distribuciones depende
  de la biblioteca estándar, así que la misma semilla daría otro solo en MSVC que en GCC. Hay una prueba
  "golden hash" que lo comprueba en el CI de Windows.
- **Un acorde por compás** en S0–S2 (lo pidió el alcance). Ampliable a cambios a mitad de compás.
- **Solo monofónico**: un solo instrumental es una línea; simplifica interpretación y evita solapamientos.
- **Reproducción = render**: el WAV se genera simulando bloques con `SoloPlayer` + `InstrumentEngine`, el mismo
  código que suena en el DAW. Por eso MIDI, audio en vivo y WAV coinciden (prueba `render_consistency`).
- **Tiempo real**: el hilo de audio no reserva memoria, no bloquea y no toca disco. Las secuencias nuevas se
  publican con `RealtimeSwap` (dos punteros atómicos; el audio nunca libera memoria, lo hace un timer del hilo
  de mensajes). Componer tarda microsegundos y se hace en el hilo de la interfaz.
- **Saltos/loops/stop del host**: si la posición de un bloque no continúa la del anterior, se cierran todas las
  notas activas. Una nota que empezó antes del punto de salto no se reinicia a medias.
- **Estado en texto versionado** (`SOLOAISTUDIO-STATE 1`): legible, validado por completo antes de aplicarse;
  un estado corrupto o de una versión futura se rechaza sin modificar nada.
- **Sin parámetros automatizables salvo Volumen**: los controles de composición no tienen sentido automatizados
  y reducir parámetros evita problemas de compatibilidad entre hosts.
- **Entrada MIDI ignorada** en esta versión (el plugin declara entrada para que FL lo trate como instrumento normal).

## Algoritmo del compositor (resumen)

1. **Forma**: frases de 1, 2 o 4 compases (según densidad/complejidad e instrumento), alternando llamada y respuesta.
   Siempre al menos dos frases. La flauta no pasa de 2 compases sin respirar.
2. **Ritmo**: un motivo de 2 pulsos (3 en 3/4) elegido de un vocabulario de células (negras, corcheas, síncopas,
   semicorcheas, tresillos, shuffle) ponderado por densidad, estilo y un límite de notas por segundo del instrumento.
   El motivo se repite con variaciones; la respuesta reutiliza el ritmo de la llamada.
3. **Entradas y respiración**: entrada en el 1, a contratiempo o tras un pulso; silencio deliberado al final de cada frase.
4. **Alturas**: contorno por grados de la escala del estilo (pentatónica en rock/blues + nota de blues; diatónica en
   melódico/electrónico). Tiempos fuertes y notas largas → notas del acorde (en el primer tiempo, fundamental/3ª/5ª);
   con complejidad alta, tensiones sin semitono contra el acorde. Relleno tras salto, atracción al centro del registro,
   límite de salto por instrumento, sin repeticiones mecánicas.
5. **Arco**: el centro de registro sube hasta el clímax (~75 % del solo) y se relaja.
6. **Cadencias**: la llamada termina en 3ª/5ª (abierta), la respuesta en la fundamental, el final en la tónica
   (si pertenece al último acorde) con nota larga.
7. **Cromatismo**: notas de aproximación a semitono antes de notas estructurales, según complejidad.
8. **Dinámica**: velocity por intensidad, acentos métricos, arco de frase y pequeña variación aleatoria.

Garantías comprobadas por pruebas (504 solos: 7 progresiones × 3 instrumentos × 4 estilos × 6 semillas, con 2/4/8/16
compases × 3/4 y 4/4): rango del instrumento, rejilla temporal, monofonía, saltos máximos, respiración,
resolución final en nota estable, notas por segundo, y 100 % de notas del acorde en el primer tiempo.
No se garantiza calidad artística profesional.

## Instrumentos integrados

Síntesis ligera, sin samples (nada que licenciar). **No son instrumentos acústicos realistas**:
- Guitarra (sintetizada): Karplus-Strong con saturación suave y paso bajo.
- Flauta (sintetizada): aditiva con soplo filtrado, "chiff" de ataque y vibrato retardado.
- Sintetizador lead: dos sierras PolyBLEP desafinadas + filtro de estado variable con envolvente.

Preparado para S3+: `InstrumentEngine` recibe eventos con offset; sustituirlo por un reproductor de samples no
cambia el resto. `Note` puede ampliarse con pitch bend / CC.
