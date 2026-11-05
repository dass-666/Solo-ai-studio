#pragma once
// Intérprete: convierte la intención musical del compositor en una ejecución instrumental
// (vibrato, bends, slides, legato, palm mute, ataque, duración y micro-tiempo).
// Es determinista: depende de la posición métrica, la función de la nota y la semilla,
// no de ruido aleatorio puro.
#include "solo/SoloComposer.h"

namespace solo
{
struct PerformerTraits
{
    double vibratoDepth;   // semitonos en notas largas
    double vibratoRate;    // Hz
    double vibratoDelay;   // fracción de la nota
    double slideIn;        // probabilidad de entrar deslizando al inicio de frase
    double legato;         // probabilidad de ligar notas rápidas por grado conjunto
    double palmMute;       // probabilidad en notas cortas graves repetidas
    double curl;           // probabilidad de "quarter bend" en notas largas sin bend
    int attackBase;        // 0..127
    double articulation;   // fracción de duración que suena (1 = legato total)
    double staccato;       // probabilidad de acortar notas cortas a contratiempo
    int laidBackTicks;     // retraso de las notas a contratiempo (feel)
    double dynamicRange;   // multiplicador de la variación de velocity
};

PerformerProfile resolvePerformer (Style style, PerformerProfile requested, InstrumentId instrument);
const PerformerTraits& traitsFor (PerformerProfile p);

// Aplica la interpretación in situ. Las notas bloqueadas no se modifican.
void performSequence (Sequence& seq, const ComposerParams& params, PerformerProfile profile, uint64_t seed);

} // namespace solo
