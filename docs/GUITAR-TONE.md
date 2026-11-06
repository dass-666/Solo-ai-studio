# Guitar Tone Studio v3

Rama `claude/solo-ai-studio-v3-guitar-tone` (sale de `claude/solo-ai-studio-fl-prep`, 6cf4ab6). Sin fusionar a `main`.

Objetivo: que la guitarra suene a guitarra eléctrica con carácter y que los perfiles se distingan de verdad,
con tres macroperillas (Fuerza, Expresión, Ambiente) que escriben valores DSP reales. **Nada de lo que sigue
ha pasado por una evaluación auditiva humana**: todas las comparaciones son medidas objetivas.

## Arquitectura (qué se reutiliza y qué es nuevo)

| Pieza | Estado | Dónde |
|---|---|---|
| Compositor, intérprete, secuencia MIDI, variaciones, bloqueos, deshacer, automatización, estado, exportación | **Se reutilizan sin cambios de formato**; el estado v2 se lee tal cual | `core/` |
| `GuitarSoundEngine` (interfaz mono pre-ampli) | Nuevo | `GuitarSoundEngine.h` |
| `ModeledGuitarEngine` (Karplus-Strong con rigidez, púa, pastilla, cuerpo) | Nuevo | `ModeledGuitarEngine.cpp` |
| `SampleGuitarEngine` + `loadSfz` (SFZ mínimo, sin dependencias) | Nuevo | `Sampler.h/.cpp` |
| Motor v2 (`InstrumentEngine::Guitar`) | **Intacto** (motor «v2 (anterior)» para A/B; bit a bit igual con los presets v2) | `InstrumentEngine.cpp` |
| `GuitarAmp` (cadena incremental) | Nuevo; sustituye overdrive/distorsión/ampli/pantalla v2 cuando `fx.ampModel = 1` | `GuitarAmp.h/.cpp`, `Effects.cpp` |
| Ambiente: predelay, filtros de cola, anchura | Nuevo en `EffectsChain`; valores neutros por defecto (los presets v2 no cambian) | `Effects.cpp` |
| Expresión explícita por nota (`NoteGestures`) | Nuevo; la misma función alimenta a los motores y a la exportación MIDI con pitch bend | `Expression.h/.cpp` |
| Macros y presets de tono | Nuevo | `ToneStudio.h/.cpp` |

Selección de motor: `settings.guitarEngine` (0 v2, 1 modelado v3, 2 samples). Los estados anteriores a v3 no
traen la clave y quedan en v2 con la cadena v2: **un proyecto antiguo suena como antes**. Un proyecto nuevo
arranca con el preset de tono «Rock Lead» (motor modelado v3, ampli v3).

### Cadena del ampli v3 (`GuitarAmp`)

Entrada (mono, pre-ampli) → paso alto 70 Hz → compresor (feed-forward, `ampCompression`) → previo
(`ampPreGainDb`) → overdrive opcional → etapa asimétrica tipo válvula con sag (`ampSag`) → filtros entre etapas →
distorsión opcional → etapa de potencia (tanh, `ampMaster`) → bloqueo de DC → tonestack (graves 110 Hz, medios
750 Hz, agudos 2,8 kHz, presencia 4,5 kHz) → pantalla (5 biquads por tipo, o IR del usuario de hasta 2048
muestras) → paso bajo de salida (`ampOutputLpHz`) → compensación de nivel.

Las etapas no lineales corren con oversampling 1x, 2x o 4x (FIR halfband polifásico de 63 coeficientes,
Kaiser β = 8). Latencia declarada al host: 0 / 31 / 46 muestras. Se investigó `juce::dsp::Oversampling` y
`juce::dsp::Convolution`; se descartaron para mantener el núcleo sin JUCE (así lo cubren las mismas pruebas que
la exportación y la CLI). Tres tipos de saturación: válvula (asimétrica), alta ganancia (dura), limpio.

Compensación de nivel: ganancia medida con un seno de referencia más la ganancia estática del compresor y una
corrección empírica de −0,13 dB/dB; el barrido de Fuerza 0..100 queda dentro de **3,0 dB** (prueba
`tone_force_keeps_loudness_within_tolerance`) y sin saltos entre bloques (0,01 dB).

Distorsión armónica medida (seno 187,5 Hz, entrada 0,3 de pico, THD hasta el 12.º armónico):

| Preset | Fuerza 35 % | Fuerza 70 % | Fuerza 100 % |
|---|---|---|---|
| Rock Lead (válvula) | 4,6 % | 17,1 % | 25,0 % |
| Blues Lead (válvula) | 2,8 % | 9,0 % | 17,2 % |
| Clean Electric (limpio) | 2,9 % | 10,1 % | 18,8 % |
| High Gain Solo (alta ganancia) | 31,7 % | 33,0 % | 33,3 % |

Aliasing (seno 2,5 kHz a Fuerza 100): energía no armónica −17 dB (1x), −28 dB (2x), −46 dB (4x).

## Las tres macros

Cada macro escribe campos concretos de `FxSettings` / `GuitarParams` (Fuerza, Ambiente) o la escala de
expresión del motor y del intérprete (Expresión). La pestaña Pro › Ampli / Samples muestra esos valores y
permite editarlos a mano; la edición manual convierte el preset en «Personalizado». Mover una perilla mientras
suena previsualiza sin pasos de deshacer; al soltarla se crea un único paso.

### Fuerza F (0..100 %)

| Campo | Curva | 70 % |
|---|---|---|
| `ampPreGainDb` | 2 + 22·(F/100)^1,6 | **14,4 dB** |
| `ampCompression` | 0,10 + 0,50·F/100 | 0,45 |
| `ampSag` | 0,10 + 0,40·F/100 | 0,38 |
| `ampPresenceDb` | −1 + 4·F/100 | +1,8 dB |
| `ampMaster` | 0,30 + 0,50·F/100 | 0,65 |
| `odDrive` / `distGain` (si la etapa está activa) | 0,20 + 0,60·F/100 / 0,30 + 0,50·F/100 | 0,62 / 0,65 |
| `guitar.pickHardness` (dureza de la púa, fuente) | 0,25 + 0,65·F/100 | 0,70 |

Ajuste derivado de las pruebas: la compensación de nivel incluye la ganancia estática del compresor y la
corrección −0,13 dB/dB; sin ellas el barrido variaba 5,8 dB.

### Expresión E (0..100 %)

`expressionScaling(E)`: 65 % es la escala neutra (1,0 exacto, el comportamiento anterior). 0 % deja los bends en
su altura final, sin vibrato ni slides; 100 % sube la profundidad de vibrato (×1,4), alarga bends (×1,25) y
slides (×1,3) y aumenta la tendencia del intérprete a escribir gestos (×1,3). No se añaden gestos aleatorios: la
macro escala los que decide el intérprete (y, al generar, cuántos decide). Con la misma semilla, 0 % y 65 %
producen las mismas notas; solo cambia la ejecución (prueba `tone_expression_macro_scales_generation`:
0 % = 0/0/0 vibrato/bends/slides; 65 % = 11/15/4; 100 % = 11/18/4 de 34 notas).

MIDI: la secuencia editable sigue siendo la intención musical (sin pitch bend). Al exportar con pitch bend se
usan **los mismos gestos que suenan** (`MidiExportOptions::expressionPercent`), con RPN 0 por canal; el pitch
bend afecta a todo el canal, así que solo es exacto con una voz a la vez (el solo es monofónico).

### Ambiente A (0..100 %)

| Campo | Curva | 35 % |
|---|---|---|
| `reverbMix` | 0,32·(A/100)^1,3 | **8,2 %** |
| `delayMix` | 0,25·(A/100)^1,5 | **5,2 %** |
| `delayFeedback` | 0,12 + 0,34·A/100 | **24 %** |
| `reverbPredelayMs` | 10 + 34·A/100 | **22 ms** |
| decay T60 → `reverbSize` | 0,6 + 2,3·A/100 s (inversa de la cola Freeverb) | **1,4 s** |
| `ambienceWidth` | 0,6 + 0,4·A/100 | 0,74 |

Reverb y delay van en paralelo sobre la señal del ampli; la cola se filtra (corte bajo/alto configurables por
preset), la anchura se controla con mid/side y el tiempo del delay se deriva del tempo del DAW. La cola
estimada con Ambiente 35 % es 1,46 s (1,40 + predelay), comprobada con `EffectsChain::tailSeconds`.

## Presets de tono

Seis presets (`tonePresets()`), cada uno con fuente, articulación de ampli, pantalla, ecualización, efectos y
macros propias, calibrados a −18 LUFS con el mismo solo (rango 0,3 dB):

| Preset | Fuente | Ampli | Pantalla | Macros F/E/A |
|---|---|---|---|---|
| Rock Lead | humbucker, púa dura, pastilla al puente | válvula + overdrive | 4x12 | 70/65/35 |
| Blues Lead | simple, pastilla central | válvula al borde, sin delay | 2x12 | 50/75/28 |
| Clean Electric | simple, pastilla al mástil | limpio + chorus leve | 1x12 | 22/55/30 |
| Emotional Lead | humbucker, mucho sustain | válvula + overdrive, delay con puntillo | 2x12 | 58/85/55 |
| High Gain Solo | humbucker, poco cuerpo | alta ganancia + distorsión, 4x | 4x12 | 90/60/32 |
| Ambient Guitar | simple, mucho sustain | limpio, chorus, delay largo, reverb enorme | 1x12 | 30/55/85 |

Diferenciación objetiva (`tone_presets_differ_objectively`, mismo MIDI, 4 compases): cada pareja difiere en
brillo ≥ 2 dB, cresta ≥ 1,5 dB o cola ≥ 3 dB; Rock frente a Blues: cresta 10,5 frente a 16,6 dB. Los demos
`solo_cli tone-ab` generan los WAV a −18 LUFS y una tabla (`metricas-tono.md`).

## Sampler (SFZ mínimo)

`loadSfz` admite `<control> default_path`, `<global>/<group>/<region>`, `sample`, `key/lokey/hikey`,
`pitch_keycenter`, `lovel/hivel` (capas), `seq_length/seq_position` (round-robin), `loop_mode/loop_start/loop_end`,
`ampeg_attack/ampeg_release`, `tune`, `volume`, `group_label/region_label` (articulación «mute» = palm mute). Solo
WAV PCM/float, mono o estéreo (se mezcla a mono); se ignoran filtros, LFO, CC, FLAC/OGG. Límite de memoria
512 MB; los samples que faltan se notifican sin impedir cargar el resto. La ruta se guarda en el estado
(`sfzPath`): al reabrir el proyecto se vuelve a cargar; si no existe, el motor queda en silencio y la pestaña
lo indica (sin fallar ni tocar el proyecto). Legato por glide de altura, pitch bend/vibrato por remuestreo
(hermite), 8 voces, publicación al audio por puntero compartido (sin bloqueos; comprobado sin reservas en el hilo
de audio).

**Licencias:** no se incluye ninguna biblioteca de guitarra. El banco de prueba es **sintético** (generado con el
modelo físico, etiquetado `[SINTÉTICO]` en el SFZ y en la interfaz) y existe solo para probar el motor y como
plantilla. El usuario importa su propia biblioteca SFZ con una licencia que lo permita. Tampoco se incluye ninguna
IR de pantalla: `Cargar IR (WAV)` acepta la del usuario.

## Rendimiento (Linux, contenedor compartido, 48 kHz, bloques de 256, 60 s)

| Rig | CPU media | p99 | p99,9 |
|---|---|---|---|
| Presets v2 (referencia) | 1,3–1,8 % | 1,8–3,0 % | 2,3–4,8 % |
| Presets v3 con 2x | 2,1–3,0 % | 3,0–4,8 % | 4,7–12,1 % |
| High Gain Solo (4x) | 6,0 % | 7,5 % | 17,5 % |
| Rock Lead con samples (banco sintético) | 2,8 % | 3,9 % | 6,2 % |

Bloques aislados llegan al 20–55 % (también en v2 hasta 97 % en este contenedor): atribuidos al reparto de CPU
del entorno, **sin confirmar**; en Windows con FL Studio hay que medirlo. Memoria: `SoloEngine` 7,1 KB + búferes;
banco sintético 20 MB; pico RSS del banco de pruebas 28 MB.

## Pruebas (106, 0 fallos; también con ASan + UBSan)

`tests/TestTone.cpp`: halfband transparente con latencia conocida; curva de Fuerza; nivel dentro de 3 dB;
barrido sin discontinuidades; oversampling reduce aliasing; latencia declarada = medida; parámetros extremos
finitos; escalado de expresión y gestos; motor modelado toca y libera; en vivo = exportado (−330 dB); cambio de
motor a mitad de nota sin colgar ni clics; banco sintético carga y toca (altura correcta), descarga con notas
sonando; estado sin SFZ hace fallback; IR carga y se usa; sin reservas en el hilo de audio; macros escriben
valores DSP reales; estado v3 ida y vuelta y fallback de estados antiguos; deshacer restaura macros y guitarra;
presets difieren objetivamente; cambio de preset/macros con nota sonando; la macro Expresión escala la
generación; MIDI con gestos. Defecto previo a v3 encontrado con ASan en esta fase: lectura fuera del búfer del
delay cuando la posición de lectura redondeaba exactamente al tamaño (corregido en `Effects.cpp`).

## Limitaciones y pendientes

- Sin evaluación auditiva humana. El "carácter eléctrico" se ha medido (THD, brillo, cresta), no escuchado.
- Oversampling y ampli solo en el núcleo propio (sin `juce::dsp`); la convolución de IR es FIR directa (≤ 2048
  muestras, ~43 ms a 48 kHz): IRs más largas se truncan con fundido.
- El sampler no implementa filtros, LFO ni CC del SFZ; pitch bend por remuestreo (sin capas de bend).
- Las macros no se automatizan por compases (la automatización v2 de Drive/Delay/Reverb sigue funcionando y, con
  `ampModel = 1`, Drive mueve el previo entre 2 y 24 dB).
- Sincronización con FL Studio: hay una corrección aislada (`d8162e5`) con diagnóstico exportable; **no está
  validada en FL**.
