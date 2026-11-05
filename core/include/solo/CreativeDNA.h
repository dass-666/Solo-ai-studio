#pragma once
// Creative DNA: rasgos musicales de una idea (una melodía cantada, un solo o un fragmento) que se
// pueden reutilizar para generar material derivado: motivos, contorno, intervalos, ritmo, pausas,
// repeticiones, dirección e intensidad.
#include "solo/SoloComposer.h"
#include <array>
#include <string>
#include <vector>

namespace solo
{
struct CreativeDNA
{
    std::vector<SeedMotif> motifs;        // [0] = motivo principal (primera frase), [1] = el más recurrente
    std::array<double, 25> intervalHist {}; // intervalos -12..+12 normalizados
    std::array<double, 8> rhythmHist {};    // duraciones: 1/16, 1/8, 1/8., 1/4, 1/4., 1/2, 1/2., >=1
    double notesPerBeat = 0;
    double restRatio = 0;
    double repetitionRate = 0;   // notas repetidas consecutivas
    double stepwiseRatio = 0;
    double direction = 0;        // pendiente media (semitonos por pulso) > 0 asciende
    double meanVelocity = 0, velocityRange = 0;
    double meanPitch = 0;
    int lowPitch = 0, highPitch = 0;
    double phraseBeats = 0;      // longitud media de frase en pulsos
    int lastPitch = -1;
    bool valid = false;

    std::string summary() const; // descripción legible en español
};

CreativeDNA extractDNA (const Sequence& s);

enum class DerivedMode { Imitate = 0, Continue = 1, Answer = 2, Transform = 3, Free = 4 };
const char* derivedModeName (DerivedMode m);

struct DerivedOptions
{
    DerivedMode mode = DerivedMode::Imitate;
    double fidelity = 0.7;     // cuánto se respeta el material original
    double freedom = 0.4;      // libertad creativa
    double expression = 0.6;   // expresión instrumental (articulaciones)
    double development = 0.5;  // cuánto se desarrollan (transforman) los motivos
};

// Genera un solo derivado del ADN con el compositor jerárquico (motor 2). 'params' aporta armonía,
// compás, compases, instrumento, estilo, BPM y semilla.
Sequence generateFromDNA (const CreativeDNA& dna, const ComposerParams& params, const DerivedOptions& opts);

// Parecido melódico 0..1 entre dos solos (n-gramas de intervalos compartidos), para pruebas e interfaz.
double melodicSimilarity (const Sequence& a, const Sequence& b);

} // namespace solo
