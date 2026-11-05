# Hoja de ruta tras v2 (priorizada)

Criterio: primero lo que convierte el trabajo hecho en uso real; después calidad; al final lo caro.

## 1. Validación en FL Studio 24 (máxima prioridad, coste bajo, requiere a Diego)
- Ejecutar los pasos de prueba del informe v2: carga, transporte, `.flp`, exportación, MIDI en el Piano Roll, CPU real.
- Probar Hum-to-Solo con 3–5 grabaciones reales de voz (tarareo, silbido, «la-la») y anotar aciertos.
- Escucha comparativa de las 7 demos A/B (es la primera evaluación humana del proyecto).
- **Sin esto, cualquier nueva función añade riesgo sin evidencia de valor.**

## 2. Correcciones según lo que salga de FL (coste según hallazgos)
- Ventana/redimensionado en el wrapper de FL, foco del teclado (atajos de deshacer), diálogos de archivo.

## 3. Calidad musical (valor alto, coste medio)
- Más recurrencia de motivos (hoy 0,033): reutilizar motivos de forma explícita por sección.
- Acordes a mitad de compás y escalas por acorde (modos).
- Pitch bend en la salida MIDI en directo.

## 4. Voz (valor alto, coste medio, riesgo de calidad)
- Grabación desde el micrófono dentro del plugin (hoy: grabar en el DAW e importar el WAV).
- Reconocimiento de órdenes habladas con un modelo local pequeño (p. ej. whisper.cpp «tiny/base», ~75–150 MB).
  **Decisión de Diego:** tamaño de descarga, licencia del modelo (MIT en whisper.cpp; revisar pesos) y si aporta
  valor frente a escribir la orden.

## 5. Sonido (valor alto para vender, coste medio-alto)
- Samples con licencia redistribuible verificada o grabación propia; mantener la síntesis como opción ligera.

## 6. Distribución (bloqueante para vender)
- Licencia JUCE (comercial o AGPLv3), firma de código, instalador. Decisión de negocio de Diego.
