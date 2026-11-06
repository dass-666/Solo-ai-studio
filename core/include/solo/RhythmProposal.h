#pragma once
// RhythmProposal (V4-E): "Din, din, daaan..." -> patrón rítmico EDITABLE -> notas compatibles con la armonía.
// Solo usa ataques, duraciones, pausas y acentos de la voz. Nunca inventa una transcripción exacta de
// alturas: las alturas las decide la armonía (reutiliza humToSolo en modo solo-ritmo).
#include "solo/SoloComposer.h"
#include "solo/VoiceAnalysis.h"
#include <string>
#include <vector>

namespace solo
{
struct RhythmEvent
{
    int64_t onset = 0;      // ticks desde el inicio del patrón (compás startBar)
    int64_t length = 0;     // ticks
    double accent = 0.7;    // 0..1 (nivel relativo de la voz)
    bool pitched = false;   // la voz tenía altura fiable en ese ataque (informativo; no se usa para elegir notas)
};

struct RhythmPattern
{
    std::vector<RhythmEvent> events;  // ordenados por onset
    int bars = 0;                     // compases que cubre
    TimeSignature timeSignature;
    double bpm = 120.0;
    int startBar = 0;                 // compás del solo donde empieza (base 0)
    int gridTicks = kPPQ / 4;         // rejilla de cuantización usada
    double gridFit = 0;               // 0..1 fracción de ataques que ya caían cerca de la rejilla (alineación con el beat)
    double meanDeviationMs = 0;       // desviación media de los ataques respecto a la rejilla antes de cuantizar
    std::vector<std::string> warnings;

    bool empty() const { return events.empty(); }
    int64_t lengthTicks() const { return (int64_t) bars * timeSignature.ticksPerBar(); }
    // Texto editable, una línea por compás, un carácter por semicorchea: 'x' ataque, '-' sostiene, '.' silencio, 'X' ataque acentuado.
    std::string toText() const;
    static bool fromText (const std::string& text, RhythmPattern& inOut, std::string& error); // conserva bpm/compás/startBar de inOut
};

struct RhythmExtractOptions
{
    int gridTicks = kPPQ / 4;   // semicorchea
    double quantize = 1.0;      // 0..1
    int startBar = 0;
    double voiceStartSec = -1;  // -1 = el primer ataque cae en startBar
    int maxBars = 16;
};

// Extrae el patrón rítmico de una toma (con o sin altura fiable) al tempo dado.
RhythmPattern extractRhythm (const VoiceAnalysis& va, double bpm, TimeSignature ts, const RhythmExtractOptions& o = {});

struct RhythmProposalResult
{
    Sequence sequence;              // notas con alturas elegidas por la armonía (vacía si falla)
    int notesFromPattern = 0;
    std::vector<std::string> warnings;
};
// Propone notas para el patrón sobre la armonía/compases de 'params' (bars = compases del solo completo).
RhythmProposalResult proposeFromRhythm (const RhythmPattern& pattern, const ComposerParams& params, uint64_t seed);

std::string serializeRhythmPattern (const RhythmPattern& p);
bool deserializeRhythmPattern (const std::string& text, RhythmPattern& out, std::string& error);
} // namespace solo
