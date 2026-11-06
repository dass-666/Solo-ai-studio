#pragma once
// Guitar Tone Studio v3: las tres macroperillas (Fuerza, Expresión, Ambiente) y los presets de tono.
//
// Las macros NO son parámetros de audio propios: cada una escribe valores DSP reales en FxSettings / GuitarParams
// (Fuerza y Ambiente) o en la escala de expresión del motor y del intérprete (Expresión). Así la interfaz Pro
// muestra siempre los valores efectivos y editarlos a mano sigue siendo posible. Curvas en docs/GUITAR-TONE.md.
#include "solo/Effects.h"
#include "solo/GuitarSoundEngine.h"
#include "solo/SoloEngine.h"
#include <string>
#include <vector>

namespace solo
{
struct ToneMacros
{
    double force = 70.0, expression = 65.0, ambience = 35.0; // 0..100 %
    void sanitise();
    bool operator== (const ToneMacros& o) const;
};

// ---- Curvas de las macros (F, A en 0..100)
double forcePreGainDb (double F);        // 2 + 22·(F/100)^1.6  => +14.4 dB en 70 %
double ambienceReverbWet (double A);     // 0.32·(A/100)^1.3    => 8.2 % en 35 %
double ambienceDelayWet (double A);      // 0.25·(A/100)^1.5    => 5.2 % en 35 %
double ambienceFeedback (double A);      // 0.12 + 0.34·(A/100) => 24 % en 35 %
double ambiencePredelayMs (double A);    // 10 + 34·(A/100)     => 21.9 ms en 35 %
double ambienceDecaySec (double A);      // 0.6 + 2.3·(A/100)   => 1.4 s en 35 %
float reverbSizeForDecay (double t60Sec); // inversa de EffectsChain::tailSeconds (Freeverb)

// Escriben SOLO los campos que gobierna cada macro; el resto de fx/guitar (preset, edición Pro) se conserva.
void applyForce (double F, FxSettings& fx, GuitarParams& guitar);
void applyAmbience (double A, FxSettings& fx);
void applyMacros (const ToneMacros& m, FxSettings& fx, GuitarParams& guitar);

struct TonePreset
{
    const char* name;
    const char* description;
    FxSettings fx;
    GuitarParams guitar;
    GuitarEngineKind engine;
    ToneMacros macros;
    PerformerProfile performer; // intérprete sugerido (Auto = según el estilo)
};
// Rock Lead, Blues Lead, Clean Electric, Emotional Lead, High Gain Solo, Ambient Guitar (ya con sus macros aplicadas).
const std::vector<TonePreset>& tonePresets();
int tonePresetIndex (const std::string& name); // -1 si no existe

} // namespace solo
