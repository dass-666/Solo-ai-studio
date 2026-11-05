#pragma once
// SoloComposer: compositor algorítmico condicionado por la armonía.
// No es aprendizaje automático: reglas musicales + aleatoriedad controlada por semilla.
#include "solo/MusicTheory.h"
#include "solo/Sequence.h"
#include <cstdint>
#include <string>
#include <vector>

namespace solo
{
enum class InstrumentId
{
    Guitar = 0,
    Flute = 1,
    Synth = 2
};
constexpr int kNumInstruments = 3;

enum class Style
{
    Melodic = 0,
    Rock = 1,
    Blues = 2,
    Electronic = 3
};
constexpr int kNumStyles = 4;

const char* instrumentName (InstrumentId id);
const char* styleName (Style s);

// Perfil de interpretación: condiciona registro, saltos, respiración y articulación.
struct InstrumentProfile
{
    InstrumentId id;
    InstrumentRange range;
    int maxLeapSemitones;      // salto máximo entre notas consecutivas
    double legato;             // fracción de la duración nominal que suena
    double densityBias;        // multiplicador de densidad rítmica
    int maxPhraseBars;         // longitud máxima de frase sin respirar
    double maxNotesPerSecond;  // límite de interpretabilidad
    double leapProbability;    // tendencia a saltos frente a grados conjuntos
    int gmProgram;             // programa General MIDI sugerido al exportar
};
const InstrumentProfile& profileFor (InstrumentId id);

struct ComposerParams
{
    Key key;
    Progression progression;
    TimeSignature timeSignature;
    int bars = 8;
    InstrumentId instrument = InstrumentId::Guitar;
    Style style = Style::Rock;
    double energy = 0.6;     // 0..1 intensidad (dinámica, registro, actividad)
    double density = 0.5;    // 0..1 cantidad de notas
    double complexity = 0.4; // 0..1 variación, cromatismo, saltos
    double bpm = 120.0;      // para limitar notas por segundo
    uint64_t seed = 1;
};

class SoloComposer
{
public:
    // Genera un solo completo. Misma entrada (incluida la semilla) => misma salida.
    static Sequence compose (const ComposerParams& params);

    // Regenera los compases [fromBar, toBar] (base 0, inclusivo) con otra semilla,
    // conservando el resto de notas del solo actual.
    static Sequence regenerateBars (const Sequence& current, const ComposerParams& params,
                                    int fromBar, int toBar, uint64_t newSeed);

    // Errores que impiden componer (vacío si todo es correcto).
    static std::vector<std::string> validate (const ComposerParams& params);
};

} // namespace solo
