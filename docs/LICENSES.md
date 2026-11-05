# Licencias de dependencias y recursos

Revisado el 2026-11-05 sobre JUCE 8.0.15 (commit `91ad83ae34a81e0833b1a2b0866f54846370ae53`).

| Componente | Uso | Licencia | Evidencia |
|---|---|---|---|
| JUCE 8.0.15 | Framework del plugin y la interfaz | **AGPLv3 o licencia comercial JUCE 8** (dual) | `JUCE/LICENSE.md` |
| VST3 SDK (incluido en JUCE 8.0.15) | Formato VST3 | **MIT** (Steinberg, 2025) | `modules/juce_audio_processors_headless/format_types/VST3_SDK/LICENSE.txt` |
| Código de Solo AI Studio (`core/`, `plugin/`, `tests/`, `tools/`) | Propio | Pendiente de decidir por el titular | — |
| Samples de audio | Ninguno | — | Todo el sonido es síntesis generada por código |
| Modelos de IA | Ninguno | — | Composición algorítmica, sin descargas |
| pluginval (solo en CI, no se distribuye) | Validación | GPLv3 (Tracktion) | Se descarga en el runner de CI |

## Lo que implica antes de distribuir

1. **JUCE**: para distribuir el plugin con código cerrado hace falta una licencia comercial de JUCE (consultar
   el nivel y el límite de ingresos vigentes en https://juce.com/get-juce/). La alternativa es publicar todo el
   código bajo AGPLv3. **Es una decisión de negocio pendiente**; no afecta al desarrollo, sí a la publicación.
2. **VST3**: con el SDK bajo MIT ya no es necesario el acuerdo de licencia de Steinberg para el SDK. El uso del
   logotipo/marca "VST" sigue sus propias directrices (`VST3_Usage_Guidelines.pdf`).
3. **Firma de código** (Authenticode) para evitar avisos de SmartScreen: pendiente.
4. No se incluye ningún recurso de terceros (samples, fuentes, imágenes). Las fuentes de la interfaz son las del sistema.
