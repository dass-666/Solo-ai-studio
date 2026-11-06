# Solo AI Studio — instalación y prueba en FL Studio (Windows 11)

> **Estado:** compilado y validado automáticamente en Windows (pruebas, host de prueba, pluginval nivel 5).
> **No se ha probado todavía dentro de FL Studio ni lo ha escuchado nadie.** Esta guía es para hacer esa prueba.

## 1. Contenido del paquete

| Archivo | Qué es |
|---|---|
| `Solo AI Studio.vst3\` | El plugin (es una **carpeta**; cópiala entera) |
| `Solo AI Studio.exe` | Versión independiente para probar sin DAW |
| `Instalar-SoloAIStudio.ps1` | Copia el VST3 a la carpeta estándar y verifica la copia |
| `PRUEBAS-FL.md` | Lista de pruebas manuales, con casillas para anotar resultados |
| `SHA256SUMS.txt` | Huellas de los binarios (para comprobar que no se han alterado) |
| `ejemplos\` | MIDI/WAV de referencia y una voz sintética para probar «Tararear» |

Requisitos: Windows 10/11 de 64 bits. El plugin no necesita el redistribuible de Visual C++ (va enlazado estáticamente).

## 2. Instalar

**Con el script (recomendado):**
1. Cierra FL Studio.
2. Abre PowerShell **como administrador** en la carpeta del paquete.
3. Ejecuta: `powershell -ExecutionPolicy Bypass -File .\Instalar-SoloAIStudio.ps1`
4. Debe terminar con «Instalado y verificado».

Para desinstalar: el mismo comando con `-Desinstalar`.

**A mano:** copia la carpeta `Solo AI Studio.vst3` en `C:\Program Files\Common Files\VST3\`. Windows pedirá permisos de administrador.

El binario no está firmado digitalmente. Si SmartScreen o el antivirus lo bloquean, es por eso (firma pendiente).

## 3. Cargarlo en FL Studio 24

1. **Options › Manage plugins › Find installed plugins** (o *Start scan*).
2. Busca «Solo AI Studio» y márcalo como favorito (estrella).
3. **Channel Rack › + › Solo AI Studio**. Es un instrumento: genera audio por sí mismo, sin notas de entrada.

## 4. Uso rápido

- **Modo Fácil:**
  - Escribe la tonalidad (`Am`) y los acordes, uno por compás (`Am F C G`).
  - Elige instrumento y estilo, y pulsa **GENERAR**.
  - **ESCUCHAR** usa el reloj interno. Con *Sonar con el DAW*, el solo suena con el Play de FL desde el compás 1.
  - `<` `>` recorren las variaciones; ☆ marca la favorita.
- **Modo Pro (pestañas):**
  - **Generación:** semilla, BPM, intérprete, motor, opciones de exportación.
  - **Notas:** bloquear y regenerar compases, transponer, articulaciones. Para elegir compases, arrastra sobre la regla del piano roll. Sin selección, se aplica a todo el solo.
  - **Efectos** y **Automatización**.
  - **Voz / ADN:** importar un WAV de voz.
  - **Historial A/B**.
  - **Comandos:** órdenes escritas en español.
- **Deshacer / Rehacer:** botones de la cabecera. FL puede quedarse con Ctrl+Z; los botones funcionan siempre.
- **Exportar:**
  - **MIDI:** notas, y bends/vibrato como pitch bend con rango ±12. Detalles en [MIDI.md](MIDI.md).
  - **WAV:** el mismo sonido que se oye, con efectos y colas, a 24 bits.
  - Carpeta por defecto: `Documentos\Solo AI Studio`.
- **Tararear:** graba la voz en FL, expórtala a WAV e impórtala con **TARAREAR / IMPORTAR VOZ**. Solo se ha validado con voces sintéticas.

El proyecto se guarda dentro del `.flp`: solo, variaciones, favoritas, bloqueos, efectos y automatización.

## 5. Problemas conocidos o sin verificar

- La salida MIDI en directo envía solo notas (sin pitch bend). Para tener la expresión, usa **Exportar MIDI**.
- *Arrastrar MIDI al DAW* depende del host y no está probado en FL.
- Si los bends del MIDI exportado suenan pequeños en otro sinte, sube su rango de pitch bend a 12 semitonos.
- La flauta tiene un ataque de soplo suave: medido, suena unos 30 ms más tarde que la guitarra.
- Guitar Tone Studio v3 (Tono, Guitarra y las perillas Fuerza / Expresión / Ambiente; Pro › Ampli / Samples): ver `GUITAR-TONE.md`. Con el ampli v3 el plugin declara 31 muestras de latencia (46 con oversampling 4x); FL la compensa automáticamente.

- Beat-to-Melody V4 (IMPORTAR BEAT, GENERAR (4 PROPUESTAS), VARIAR ESTA IDEA; Pro › Beat / Explorer): ver `BEAT-MELODY.md`. El análisis del beat está probado solo con beats sintéticos; comprueba BPM y tonalidad y confírmalos si hace falta.

## 6. Si algo falla

Anota la prueba (`PRUEBAS-FL.md`), la versión de FL, el tamaño de buffer de audio y, si puedes, guarda el `.flp`.
Para comprobar que el binario es el original: `Get-FileHash "...\Solo AI Studio.vst3\Contents\x86_64-win\Solo AI Studio.vst3"` y compáralo con `SHA256SUMS.txt`.
