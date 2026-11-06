#pragma once
// Expresión (Guitar Tone Studio v3): representación explícita de los gestos de interpretación de una nota.
//
// Separación de responsabilidades:
//   - La SECUENCIA (Sequence/Note) guarda la intención musical editable: altura, duración, velocity y la
//     articulación decidida por el intérprete (NoteExpression). Es lo que se ve en el piano roll y lo que se
//     exporta como MIDI "limpio" (sin pitch bend) cuando el usuario así lo pide.
//   - Los GESTOS (NoteGestures) son lo que el motor de sonido ejecuta de verdad: curvas de altura (bends, vibrato,
//     slides, glides de legato) y de ataque/sustain, ya escaladas por la macro Expresión. Los calcula
//     gesturesFor() a partir de NoteExpression + macro; la misma función alimenta a los motores de guitarra y a la
//     exportación MIDI "con gestos" (pitch bend), de modo que lo exportado coincide con lo que suena.
//
// La macro Expresión (0..100 %) NO añade gestos aleatorios: solo escala los que el intérprete decidió
// musicalmente (y, al generar, cuánta tendencia tiene el intérprete a decidirlos; ver Performer).
#include "solo/Sequence.h"
#include <cmath>

namespace solo
{
struct ExpressionScaling
{
    double amount = 0.65;          // macro 0..1
    double vibratoDepth = 1.0;     // × profundidad de vibrato (0 % => sin vibrato)
    double vibratoDelay = 1.0;     // × retardo de entrada del vibrato
    double bendTime = 1.0;         // × duración del movimiento de un bend (0 => la nota suena ya en la altura final)
    double slideTime = 1.0;        // × duración del slide de entrada (0 => sin slide)
    double legatoGlide = 1.0;      // × tiempo del glide entre notas ligadas
    double attackVariation = 0.65; // 0..1: cuánto cambia el ataque con la articulación y la velocity
    double sustain = 1.0;          // × tiempo de release / decaimiento
    double velocitySensitivity = 0.75; // 0..1: cuánto afecta la velocity al nivel y al brillo
    double generation = 1.0;       // × probabilidad de que el intérprete escriba gestos al generar (Performer)
};

// Curvas de la macro (documentadas en docs/GUITAR-TONE.md). 65 % => escala neutra (1.0) salvo attackVariation.
ExpressionScaling expressionScaling (double percent);

struct NoteGestures
{
    // Altura
    BendType bendType = BendType::None;
    double bendSemis = 0.0, bendStartSec = 0.0, bendTimeSec = 0.0, bendDownStartSec = 0.0;
    double vibratoDepth = 0.0, vibratoRateHz = 5.5, vibratoStartSec = 0.0, vibratoRampSec = 0.18;
    bool vibratoUpward = true; // guitarra: el vibrato tensa la cuerda (solo hacia arriba)
    double slideFromSemis = 0.0, slideTimeSec = 0.0;
    bool legato = false;
    double legatoGlideSec = 0.004;
    // Ataque, timbre y sustain
    double attack = 0.5;       // 0 = suave (swell) ... 1 = agresivo
    bool palmMute = false;
    double sustainScale = 1.0; // × release
    double level = 1.0;        // nivel relativo por velocity (0..1)
    double brightness = 0.5;   // 0..1 por velocity y ataque
    double lengthSec = 0.5;

    bool hasPitchGestures() const { return bendType != BendType::None || vibratoDepth > 0.0 || std::abs (slideFromSemis) > 1e-9; }
};

// Construye los gestos de una nota. 'guitar' = vibrato asimétrico y limitaciones de guitarra.
NoteGestures gesturesFor (const NoteExpression& e, int velocity, double lengthSec, const ExpressionScaling& s, bool guitar = true);

// Desplazamiento de altura (semitonos) en el instante t (segundos desde el note-on). Determinista.
double gesturePitchOffset (const NoteGestures& g, double tSec);

// Desplazamiento máximo absoluto que puede alcanzar la curva (para elegir el rango de pitch bend MIDI).
double gestureMaxAbsOffset (const NoteGestures& g);

} // namespace solo
