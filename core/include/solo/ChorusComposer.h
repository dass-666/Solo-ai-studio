#pragma once
// ChorusComposer (V4-C): da forma de CORO a una melodía generada por el motor:
//   - registro vocal humano (configurable por carácter),
//   - frases repetibles: la segunda mitad repite la primera adaptada a sus acordes (gancho reconocible),
//   - respiraciones entre frases,
//   - desarrollo y resolución: la última nota cae en una nota del acorde final (tónica si es posible),
//   - sin notas demasiado cortas para cantar (según carácter).
// No genera voz cantada: el resultado se escucha con el instrumento guía y se exporta a MIDI.
// Trabaja sobre una Sequence ya compuesta (reutiliza SoloComposer) y respeta compases bloqueados.
#include "solo/MelodyExplorer.h"

namespace solo
{
struct ChorusShapeOptions
{
    ChorusCharacter character = ChorusCharacter::EasyToSing;
    bool mirrorHalves = true;         // segunda mitad = primera mitad adaptada (solo si no hay bloqueos en ella)
    bool breathe = true;              // hueco de respiración al final de cada frase
    bool resolveEnding = true;        // última nota larga en nota del acorde final
    int64_t minNoteTicks = 0;         // 0 = según carácter (fácil de cantar: corchea)
};

// Registro vocal por carácter (MIDI): fácil de cantar ~G3-C5, emocional ~F3-D5, enérgico ~A3-E5.
void chorusRange (ChorusCharacter c, int& low, int& high, int& maxLeap);

// Aplica la forma de coro. 'lockedBars' (puede estar vacío) protege compases: no se tocan.
Sequence shapeChorus (const Sequence& in, const ComposerParams& params, const Progression& prog,
                      const ChorusShapeOptions& o, const std::vector<bool>& lockedBars);

// Métricas de "cantabilidad" para pruebas e interfaz (no miden belleza).
struct SingabilityReport
{
    int lowest = 0, highest = 0;
    int maxLeap = 0;                  // semitonos
    double shortNoteRatio = 0;        // notas más cortas que una corchea
    double breathGapsPerPhrase = 0;   // huecos >= medio pulso por cada 2 compases
    bool endsOnChordTone = false;
    double repeatedHalfSimilarity = 0; // parecido primera/segunda mitad
};
SingabilityReport singability (const Sequence& s, const Progression& prog, const Key& k);
} // namespace solo
