# Pruebas manuales en FL Studio 24

Anota ✓ / ✗ y un comentario breve. Las 5 primeras son las prioritarias (unos 20 minutos).
Versión de FL: ______ · Interfaz de audio y buffer: ______ · Fecha: ______

## Prioritarias

| # | Prueba | Qué debe pasar | Resultado |
|---|---|---|---|
| 1 | **Carga**: instalar, escanear y añadir al Channel Rack; abrir y redimensionar la ventana | Aparece como instrumento; la interfaz se ve completa | |
| 2 | **Sincronía**: proyecto a 120 BPM, GENERAR, Play en FL; luego saltar con el cursor, loop de patrón y Stop | Empieza alineado en el compás 1; sin notas colgadas ni clics al parar o saltar | |
| 3 | **Edición en reproducción**: con FL sonando, mover Intensidad y cambiar de preset en Pro › Efectos | El sonido cambia sin cortes; la nota que suena no se interrumpe | |
| 4 | **Exportación**: Exportar MIDI y arrastrarlo al Piano Roll de FL; Exportar WAV y compararlo con lo escuchado | Mismas notas y duración; el WAV suena igual, con la cola de reverb | |
| 5 | **Persistencia**: bloquear un compás, marcar favorita, elegir preset; guardar el `.flp`, cerrar FL y reabrir | Todo vuelve igual: solo, variaciones, favorita, bloqueo y efectos | |

## Complementarias

| # | Prueba | Qué debe pasar | Resultado |
|---|---|---|---|
| 6 | Pro › Comandos: «regenera los compases 3 a 4 y más delay» → Interpretar → Ejecutar → Deshacer | Vista previa correcta; deshacer vuelve atrás paso a paso | |
| 7 | Pro › Notas: bloquear compases 1-2, Regenerar compases | Los compases bloqueados no cambian | |
| 8 | TARAREAR / IMPORTAR VOZ con `ejemplos\04-voz-sintetica-Am-100.wav` | Unas 8 notas que siguen la melodía | |
| 9 | Igual que la 8, con tu propia voz tarareada (grabada en FL y exportada a WAV) | Anota cuántas notas acierta (sin validar con voces reales) | |
| 10 | Buffer de audio 128 / 512 / 1024 | Sin clics ni cortes | |
| 11 | CPU del canal con «High Gain Solo» (Mixer o herramienta de rendimiento de FL) | Anota el valor | |
| 12 | Escuchar `ejemplos\` y las demos A/B | Opinión musical: es la primera escucha humana | |

## Guitar Tone Studio v3

| # | Prueba | Qué debe pasar | Resultado |
|---|---|---|---|
| 13 | **Sincronía (de nuevo)**: proyecto a 120 BPM, GENERAR, Play en FL; saltar, loop, Stop. Si falla, pulsa Pro › Generación › «Diagnóstico transporte» y adjunta `Documentos\Solo AI Studio\diagnostico-transporte.txt` | Empieza alineado; el diagnóstico muestra «resync» solo al saltar/parar | |
| 14 | **Tono**: con FL sonando, cambiar Tono entre Rock Lead, Blues Lead y Ambient Guitar | Se oyen tres guitarras distintas (saturación, pantalla, cola) sin cortes | |
| 15 | **Fuerza**: girar de 0 a 100 % mientras suena | De limpio a saturado sin saltos de volumen bruscos; Pro › Ampli / Samples muestra «Previo dB» de 2 a 24 | |
| 16 | **Expresión**: 0 %, GENERAR; 100 %, GENERAR (misma semilla fijada en Pro) | Mismas notas; a 0 % sin vibrato/bends, a 100 % más gestos | |
| 17 | **Ambiente**: 0 % y 100 % mientras suena | Sin cola / cola larga con predelay; sin clics ni realimentación creciente | |
| 18 | **Guitarra = v2 (anterior)** con el mismo solo | Suena como la versión anterior (comparación A/B) | |
| 19 | **Samples**: Pro › Ampli / Samples › «Banco sintético de prueba» (y, si tienes una, «Cargar SFZ…» con tu biblioteca) | Suena; al guardar y reabrir el FLP se recarga; si borras el SFZ, aviso en la pestaña y silencio sin fallo | |
| 20 | **IR**: «Cargar IR (WAV)…» con una IR tuya; luego «Quitar» | Cambia la pantalla; el proyecto guarda la ruta | |
| 21 | **Latencia**: con un ampli v3 (2x) el canal declara 31 muestras (46 con 4x); el MIDI exportado y el audio siguen alineados | FL compensa (PDC) sin desplazar las notas | |
| 22 | **Exportar MIDI con pitch bend** y arrastrarlo a un sinte de FL | Los bends/vibratos del solo aparecen como pitch bend; sin curva a Expresión 0 % | |
| 23 | **CPU** con High Gain Solo (4x) y con Rock Lead (2x) | Anota los valores | |

## Beat-to-Melody + Melody Explorer V4

| # | Prueba | Qué debe pasar | Resultado |
|---|---|---|---|
| 24 | **Importar beat**: «IMPORTAR BEAT (WAV)…» con un beat tuyo de 8–16 compases (WAV, cualquier frecuencia) | Aparecen BPM, tonalidad y acordes con su confianza; FL no se congela más de 1–2 s | |
| 25 | **Comprobar el análisis**: compara el BPM con el del beat; si falla, Pro › Beat / Explorer › «Confirmar BPM» (mira «otros BPM») y «Confirmar tonalidad» | Tras confirmar, la franja marca el dato como confirmado y el compositor usa esos valores | |
| 26 | **Acordes a mano** con un beat solo percusivo | «Acordes a mano» acepta la progresión y GENERAR compone sobre ella | |
| 27 | **GENERAR (4 PROPUESTAS)** con tipo Coro, carácter Fácil de cantar | 4 variaciones nuevas, claramente distintas, en la tonalidad del beat; el piano roll muestra la actual | |
| 28 | **Escuchar con el beat**: pista del beat en FL + plugin sonando con el DAW | La melodía cae en el compás (si no, anota el desfase) | |
| 29 | **VARIAR ESTA IDEA** con libertad 15, 45 y 90 | A 15 casi igual; a 45 reconocible; a 90 muy distinta | |
| 30 | **Regenerar compases (familia)** tras seleccionar 2 compases en la regla del piano roll, con otro compás bloqueado | Solo cambian los compases elegidos; el bloqueado no | |
| 31 | **Proponer ritmo (WAV)** con un tarareo rítmico tuyo | Aparece el patrón en texto; «Aplicar patrón» genera notas sobre ese ritmo | |
| 32 | **Proponer melodía (WAV)** con una melodía cantada | La primera propuesta es la tuya sin corregir; las siguientes en la armonía del beat | |
| 33 | **Guardar y reabrir el FLP** con beat y propuestas | Se restauran beat (análisis), sección, familia y variaciones | |
| 34 | **Exportar MIDI/WAV** de una propuesta y abrirlo en FL | Mismas notas que el piano roll | |
