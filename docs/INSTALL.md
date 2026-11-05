# Instalación y uso en FL Studio (Windows 11)

> Estas instrucciones están escritas para FL Studio 24, pero **todavía no se han comprobado en FL Studio real**.
> Si algún paso no coincide, anótalo: es justo lo que falta validar.

## 1. Obtener el plugin

Opción A, desde el CI: en GitHub, pestaña **Actions** → ejecución más reciente de `build` en verde →
artefacto **SoloAIStudio-Windows-x64** (zip). Contiene la carpeta `Solo AI Studio.vst3`, una versión
Standalone (`Solo AI Studio.exe`) para probar sin DAW, y este documento.

Opción B, compilar: `.\scripts\build.ps1` (ver README).

## 2. Instalar el VST3

1. Cierra FL Studio.
2. Copia la carpeta completa `Solo AI Studio.vst3` en `C:\Program Files\Common Files\VST3\`
   (pedirá permisos de administrador). Es una carpeta, no un único archivo: cópiala entera.
3. Si Windows SmartScreen o el antivirus bloquea el plugin: el binario no está firmado digitalmente (pendiente).

## 3. Cargarlo en FL Studio

1. Abre FL Studio → **Options → Manage plugins** → **Find installed plugins** (o *Start scan*).
2. Busca "Solo AI Studio" en la lista y marca la estrella para añadirlo a favoritos.
3. En el **Channel Rack**, botón **+** → *Solo AI Studio*. Es un instrumento (genera audio).

## 4. Generar un solo de 8 compases

1. **Tonalidad**: por ejemplo `Am`.
2. **Acordes**: un acorde por compás, por ejemplo `Am F C G` (se repite si el solo es más largo).
3. **Instrumento**, **Estilo**, **Duración = 8 compases**, **Compás = 4/4**.
4. Ajusta **Intensidad** y **Densidad**.
5. Pulsa **GENERAR**. Cada pulsación crea una variación nueva; con `<` y `>` vuelves a las anteriores.
6. **ESCUCHAR** reproduce el solo con el reloj interno al BPM del proyecto (o el BPM manual en modo Pro).
7. Con **Sonar con el DAW** activado (modo Pro, activado por defecto), al pulsar Play en FL Studio el solo suena
   sincronizado empezando en el compás indicado en **Empieza en compás** (por defecto, 1) y se repite si **Loop** está activo.

Edición (modo Pro o Fácil, en el piano roll): clic para seleccionar, arrastrar para mover, borde derecho para alargar,
doble clic en vacío para añadir y sobre una nota para borrar, flechas para transponer/mover, `Supr` para borrar.
Para regenerar un fragmento: arrastra sobre la regla de compases (franja superior) y pulsa **Regenerar compases**.

## 5. Exportar

- **Exportar MIDI**: guarda un `.mid` (tipo 1 por defecto, tipo 0 en modo Pro) con tempo, compás, tonalidad y velocity.
  En FL Studio: arrástralo al Channel Rack / Playlist, o en el Piano Roll de cualquier instrumento usa
  *File → Import MIDI file*. Carpeta por defecto: `Documentos\Solo AI Studio`.
- **Exportar WAV**: render offline a 24 bits, 48 kHz (o 44,1 kHz en modo Pro), con el mismo motor que se escucha.
- **Arrastrar MIDI al DAW (experimental)**: arrastra desde ese recuadro. No verificado en FL Studio.

## 6. Guardado del proyecto

El estado completo (parámetros, solo actual, todas las variaciones y ediciones) se guarda dentro del proyecto `.flp`
y se restaura al reabrirlo. Pendiente de verificar en FL Studio real.

## 7. Salida MIDI hacia FL Studio (no verificado)

El plugin declara una salida MIDI VST3 y emite las notas del solo mientras suena. Que FL Studio permita
encaminarla a otro instrumento depende de su soporte de MIDI out en VST3 (en FL se configura en el
*Wrapper* → ajustes → *MIDI output port*). **No está comprobado**; la vía garantizada es **Exportar MIDI**.

## Prueba sugerida en FL Studio (checklist)

| # | Prueba | Resultado |
|---|--------|-----------|
| 1 | El escaneo encuentra "Solo AI Studio" como instrumento | |
| 2 | Se abre la interfaz sin errores y se puede redimensionar | |
| 3 | El indicador muestra el BPM del proyecto (p. ej. 146) | |
| 4 | GENERAR × 3 da tres solos distintos; ESCUCHAR suena sin cortes ni notas colgadas | |
| 5 | Play en FL: el solo suena alineado al compás 1 y sigue los cambios de tempo | |
| 6 | Saltar con el cursor / loop de patrón / Stop: no quedan notas colgadas | |
| 7 | Exportar MIDI e importarlo en el Piano Roll: notas, duraciones y velocity correctas | |
| 8 | Exportar WAV: duración correcta, sin clipping | |
| 9 | Guardar el .flp, cerrarlo, reabrirlo: misma secuencia y variaciones | |
| 10 | Tamaños de buffer distintos (ASIO 128 / 512 / 1024) sin clics | |
| 11 | (Opcional) MIDI out hacia otro instrumento | |
| 12 | (Opcional) Arrastrar MIDI al Playlist | |
