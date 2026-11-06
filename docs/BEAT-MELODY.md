# Beat-to-Melody + Melody Explorer (V4)

Rama `claude/solo-ai-studio-v4-beat-melody` (sale de `claude/solo-ai-studio-v3-guitar-tone`, e171da5). Sin fusionar a `main`.

Objetivo: importar un beat (WAV), estimar su tempo, tonalidad, acordes y energía, y componer sobre esa armonía
cuatro propuestas con estrategias distintas, variarlas como familia con una «libertad creativa» medible, y aceptar
ritmo o melodía tarareados. **Todo el análisis está validado solo con beats sintéticos** (batería + pad + bajo
generados por el propio programa). No se afirma nada sobre mezclas comerciales ni sobre calidad artística: no ha
habido evaluación auditiva humana.

## Qué hay (y qué no)

| Pieza | Estado | Evidencia |
|---|---|---|
| `BeatAnalyzer`: tempo, rejilla de pulsos, compases, tonalidad, acordes por compás, energía, confianzas | Validado con señales sintéticas | 22 pruebas en `tests/TestBeat.cpp`; `solo_cli beat` |
| `HarmonyTimeline`: tonalidad/BPM/acordes con procedencia (estimado o confirmado) y edición manual | Validado | `harmony_timeline_manual_edit`, persistencia en el estado |
| `MelodyExplorer`: identidad (motivo), 4 estrategias, familias, bandas de libertad, métricas | Validado (objetivo) | 12 pruebas en `tests/TestMelody.cpp` |
| `ChorusComposer`: registro vocal, saltos, respiraciones, mitades espejo, final resuelto, informe de cantabilidad | Validado (objetivo), **sin escucha humana** | `chorus_is_singable` |
| `RhythmProposal`: patrón rítmico desde voz, texto editable, melodía desde patrón | Validado con tarareo sintético | `tests/TestHumV4.cpp` |
| `VocalProposalAdapter`: original intacta, corregida, desarrollada, combinada con el beat; corregir región | Validado con tarareo sintético; **no con voces reales** | `tests/TestHumV4.cpp` |
| Interfaz: franja Beat (Fácil y Pro) y pestaña Pro «Beat / Explorer» | Parcial | compila; prueba de host con 9 pestañas; capturas en Xvfb; **sin FL Studio** |
| Persistencia (estado v2 + bloques `beat.*`, `harm.*`, `mel.*`, `rhy.*`) | Validado | estados antiguos cargan; bloques corruptos no invalidan el proyecto |
| Exportación MIDI/WAV de propuestas | Validado | `solo_cli beatdemo`; mismas rutas que el resto del plugin |

## Flujo en el plugin

1. **IMPORTAR BEAT (WAV)…** (franja superior, modos Fácil y Pro). El análisis se hace fuera del hilo de audio al
   importar; con 128 s de audio tarda ~0,7 s en Linux. Se muestran BPM, tonalidad, acordes por compás con su
   confianza, y energía por compás. Nada se da por cierto: cada dato tiene confianza y se puede corregir en Pro.
2. **Sección**: clic en un compás de la vista del beat = inicio; mayús + clic = fin (o Pro › «Fijar sección»).
3. **Tipo** (coro, solo, interludio, intro, final, fondo), **carácter** del coro (fácil de cantar, emocional,
   enérgico) y **libertad** (0–100, inicial 35).
4. **GENERAR (4 PROPUESTAS)** crea cuatro variaciones en el historial con estrategias distintas:
   A sencilla y pegajosa, B emocional, C rítmica (sigue los acentos del beat), D atrevida. Comparten identidad
   (un motivo de 2 compases) y difieren en transformación del motivo, estilo, fidelidad, densidad, plan de
   sección y forma; no son cuatro semillas distintas del mismo generador.
5. Escuchar, marcar favorita (★) y **VARIAR ESTA IDEA**: 3 variaciones relacionadas con la actual, según la
   banda de libertad. Las notas bloqueadas y los compases bloqueados se respetan.
6. Pro › Beat / Explorer › «Regenerar compases (familia)» regenera solo la región elegida en el piano roll,
   con los parámetros de la familia; «Corregir compases con voz (WAV)…» sustituye solo esa región por la toma.
7. Exportar MIDI / WAV como siempre.

### Libertad creativa (bandas)

| Rango | Nombre | Qué cambia | Parecido medio medido con la favorita |
|---|---|---|---|
| 0–25 | Muy cercana | Notas sueltas (ornamentos, duraciones) sobre la misma estructura | 0,93 (libertad 10) |
| 26–55 | Conserva la identidad | Regenera algunos compases manteniendo el motivo | 0,49 (libertad 40) |
| 56–80 | Frases nuevas | Fragmenta o aumenta el motivo; frases nuevas | 0,32 (libertad 70) |
| 81–100 | Experimental | Motivo nuevo (o sin motivo) y estilo distinto | 0,22 (libertad 95) |

El parecido es `melodySimilarity` (0,45 n-gramas de intervalos + 0,35 Jaccard rítmico + 0,20 coseno de histograma
de alturas). Mide parecido, no calidad.

## Análisis del beat: método y límites

DSP ligero y determinista, sin modelos ni servicios externos: remuestreo a 22 050 Hz, FFT propia (2048/256),
flujo espectral con refuerzo de graves para los ataques, autocorrelación con preferencia por tempos humanos,
rejilla de pulsos ajustada por regresión, croma 60–4200 Hz con perfiles de Krumhansl-Kessler para la tonalidad,
plantillas de tríadas por compás para los acordes, y energía relativa al rango dinámico del propio beat.

Precisión medida con beats sintéticos (batería con caja 25 ms tarde, ruido −30 dB):

| BPM real | Estimado | Confianza |
|---|---|---|
| 70, 92, 105, 128, 140, 155 | correcto | 0,60–0,91 |
| 174, 185 | mitad del tempo (el real aparece como candidato) | ≤ 0,60 |

Tonalidad Em: 7/8 correcta; acordes Em | C | G | D: correctos; 3/4 soportado.

Límites conocidos:
- A partir de ~170 BPM con caja retrasada puede elegir la mitad del tempo. El tempo real queda en la lista de
  candidatos (Pro › Beat / Explorer › «otros BPM») y se confirma a mano.
- Sin batería (solo pad) no hay pulso fiable: se avisa y los acordes se estiman solo por tiempo.
- Beat solo percusivo: sin tonalidad ni acordes; se escriben a mano («Acordes a mano»).
- **No probado con mezclas reales** (voces, masterización, múltiples instrumentos). La confianza de BPM/tonalidad
  es una heurística, no una probabilidad calibrada.
- Duración máxima por defecto: 600 s; ficheros mayores se rechazan con mensaje.

## Tarareo (V4-E)

- **Proponer ritmo (WAV)…**: extrae solo los ataques (rejilla de semicorcheas, ajuste a rejilla medible, aviso si
  < 50 % de los ataques caen cerca de la rejilla). El patrón se ve y edita como texto (`x` ataque, `X` acento,
  `-` sostiene, `.` silencio; una línea por compás) y «Aplicar patrón» pone notas de la armonía sobre él. No se
  inventan alturas a partir del ritmo: las alturas las pone el compositor sobre los acordes.
- **Proponer melodía (WAV)…**: la toma entra como propuesta *Original (tal cual)* que nunca se corrige sola, más
  *Corregida con la armonía*, *Desarrollada* y *Combinada con el beat*. Si la altura no es fiable se usa solo el
  ritmo y se avisa.
- Probado únicamente con tarareo sintético; el analizador de voz une ataques consecutivos cuando la señal no tiene
  altura clara (documentado en la prueba).

## Rendimiento (Linux, contenedor)

| Operación | Medida |
|---|---|
| Análisis de 15,5 s de beat (48 kHz) | ~95 ms |
| Análisis de 128 s de beat | ~0,7 s |
| 4 propuestas | ~1 ms |
| Memoria del proceso `solo_cli beatdemo` | 12,6 MB → 31,4 MB (incluye renders) |

El análisis nunca corre en `processBlock`; el plugin lo ejecuta al importar, en el hilo de mensajes, con el
estado bloqueado solo al publicar el resultado.

## Demos

`solo_cli beatdemo <carpeta>` genera el beat sintético (`00-beat-sintetico.wav`), su análisis, 4 propuestas de
coro, solo e interludio (MIDI + WAV), 3 variaciones del coro a libertad 20/50/85, una mezcla del coro A con el
beat a −6 dB y `resumen.md` con las métricas. En la CI se publica como artefacto `SoloAIStudio-beat-melody-v4-demo`.
