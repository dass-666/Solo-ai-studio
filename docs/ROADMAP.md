# Propuesta S3–S6 (priorizada)

Criterio: primero lo que desbloquea el uso real en FL Studio; después calidad musical; al final lo caro.

## S3 — Validación real y robustez (prioridad máxima, coste bajo)
- Ejecutar la checklist de `INSTALL.md` en FL Studio 24 y corregir lo que falle.
- Deshacer/rehacer, selección múltiple, copiar/pegar en el piano roll.
- Medir CPU y latencia de interfaz; pluginval en nivel 10 sin fallos.
- Decidir licencia (JUCE comercial vs AGPL) y firmar el binario. **Sin esto no se debe distribuir.**

## S4 — Expresividad y armonía (valor alto, coste medio)
- Bends, slides, vibrato por nota y hammer-on/pull-off en guitarra (pitch bend + CC en `Note` y en la exportación).
- Acordes a mitad de compás, acordes extendidos, modos (dórico, mixolidio) y escalas por acorde.
- Perfiles de estilo más ricos (frases-tipo de blues, rock melódico, fusión) y control de "contorno" dibujable.

## S5 — Sonido (valor alto para vender, coste medio-alto)
- Motor de samples con bibliotecas de licencia redistribuible verificada (o grabación propia).
- Articulaciones y round-robin. Mantener la síntesis actual como opción ligera.

## S6 — IA entrenada (solo si S3–S5 demuestran demanda; coste alto)
- Modelo pequeño, local y offline (p. ej. transformer de pocos MB vía ONNX Runtime) que proponga motivos,
  manteniendo el compositor de reglas como "guardarraíl" armónico y rítmico.
- Requiere un dataset con derechos claros: es el mayor riesgo legal y de coste. No empezar sin resolverlo.
- Alternativa más barata: aprender estilo a partir de MIDI que importe el propio usuario.

Recomendación: no empezar S4 hasta cerrar S3 en FL Studio real.
