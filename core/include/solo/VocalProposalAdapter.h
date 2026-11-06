#pragma once
// VocalProposalAdapter (V4-E): una melodía cantada/tarareada (WAV) entra en el flujo del Melody Explorer.
// Reutiliza Hum-to-Solo (analyseVoice, humToSolo, applyVocalCorrection) y el explorador:
//   - Original: la melodía tal cual (solo cuantizada y llevada al registro del instrumento). NUNCA se corrige sola.
//   - Corregida: alturas ajustadas a la armonía del beat (o manual), mismo ritmo.
//   - Desarrollada: el compositor desarrolla el motivo cantado.
//   - Combinada con el beat: propuesta del explorador que nace del motivo cantado y sigue acentos/energía del beat.
// Si la altura no es fiable, se usa solo el ritmo (y se avisa). No se afirma nada sobre voces reales que no se hayan probado.
#include "solo/MelodyExplorer.h"
#include "solo/VoiceAnalysis.h"

namespace solo
{
struct VocalProposalOptions
{
    int startBar = 0;            // compás del solo donde cae la toma (base 0)
    double voiceStartSec = -1;   // -1 = primer ataque
    double quantize = 0.85;
    int gridTicks = kPPQ / 4;
    uint64_t seed = 1;
    double fidelity = 0.6, freedom = 0.45; // para "Desarrollada"
};

struct VocalProposal
{
    std::string name, description;
    HumMode mode = HumMode::Copy;
    bool original = false;       // true = la melodía cantada sin corrección
    Sequence sequence;
    double similarityToOriginal = 0; // 0..1
    int strategy = -1;           // estrategia del explorador si procede (Combinada)
};

struct VocalProposalSet
{
    bool pitchReliable = false;
    bool usedRhythmOnly = false;
    int notesFromVoice = 0;
    SeedMotif motif;             // motivo cantado (identidad para variar)
    std::vector<VocalProposal> proposals; // [0] = original
    std::vector<std::string> warnings;
};

VocalProposalSet proposeFromVocal (const VoiceAnalysis& va, const MelodyContext& ctx, const MelodyBrief& brief, const VocalProposalOptions& o);

// Corregir una región [fromBar, toBar] tarareando: sustituye solo esos compases (respeta bloqueos),
// con el registro/forma del tipo de melodía (coro: forma de coro solo en la región). El resto queda idéntico.
Sequence correctRegionWithVoice (const Sequence& current, const VoiceAnalysis& take, const ComposerParams& familyParams,
                                 const MelodyBrief& brief, int fromBar, int toBar, const std::vector<bool>& lockedBars,
                                 double quantize = 0.85, HumMode mode = HumMode::Correct);
} // namespace solo
