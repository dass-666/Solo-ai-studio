# MIDI: qué se exporta y qué se pierde

El MIDI exportado es **independiente de los efectos**: contiene notas y expresión, nunca el sonido de la cadena de efectos.
Para tener el sonido completo hay que exportar el WAV (que incluye efectos, automatización y colas de delay/reverb).

## Qué contiene el archivo `.mid`

| Elemento | Cómo se escribe | Notas |
|---|---|---|
| Notas | Note On / Note Off, canal 1 | Velocity 1–127 |
| Tempo, compás, tonalidad | Metaeventos en la pista de tempo (tipo 1) o en la única pista (tipo 0) | |
| Programa | Program Change General MIDI según el instrumento | Solo orienta a sintes GM |
| Bends, vibrato, slides | **Pitch Bend** muestreado cada 20 ticks (≈ 1/48 de negra) | Rango ±12 semitonos fijado al inicio con **RPN 0** |
| Legato (hammer-on/pull-off) | La nota anterior se alarga unos ticks hasta solaparse con la siguiente | Funciona con sintes en modo mono/legato o portamento |
| Palm mute, legato, bend | Metaeventos de **texto** (`PM`, `H/P`, `BU`…) | Informativos: ningún sinte los interpreta |

La opción «MIDI con pitch bend» (pestaña Pro › Generación) permite exportar solo notas.

## Limitaciones (comprobadas o inherentes al formato)

1. **El pitch bend afecta a todo el canal.** Es exacto porque el solo es monofónico; si se fusiona con otra pista del mismo canal, los bends se mezclan.
2. **El rango de bend depende del sinte.** Se envía RPN 0 = ±12, pero hay sintes que ignoran RPN y usan ±2 fijo: los bends sonarán 6 veces más pequeños. Solución: ajustar el rango de pitch bend del sinte a 12.
3. **Palm mute y ataque de púa no tienen equivalente MIDI estándar.** Solo quedan como texto y en la velocity/duración.
4. **Cuerda y posición no se exportan** (no hay estándar común; MPE o canal por cuerda quedan como posible ampliación).
5. **La salida MIDI en directo del plugin envía solo Note On/Off**, sin pitch bend todavía. La expresión completa está en el MIDI exportado y en el audio.
6. **Arrastrar MIDI al DAW** usa un archivo temporal y depende del host. No se ha probado en FL Studio.

## Cómo se ha comprobado

- `midi_pitch_bend_export`: un bend de 2 semitonos se lee de vuelta como pitch bend con el valor esperado, con RPN 0 = 12 y reinicio al centro al acabar la nota.
- `midi_reader_survives_fuzzing`: 3000 archivos corruptos sin fallos de memoria (también con ASan/UBSan).
- `midi_reader_rejects_bad_timesig_and_tempo`: defecto encontrado por UBSan (compás con exponente 183, tempo 0) y corregido.
- **No comprobado:** importación en el Piano Roll de FL Studio ni la respuesta de sintes concretos al RPN.
