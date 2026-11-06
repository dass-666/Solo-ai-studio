#pragma once
// Melody Explorer (V4-B/C/D): generador de FAMILIAS de motivos condicionado por el beat.
//
//   contexto (armonía por compás + acentos del beat + energía)  ->  identidad musical (motivo principal)
//   -> cuatro propuestas con ESTRATEGIAS distintas (A sencilla, B emocional, C rítmica, D atrevida)
//   -> favorita -> variaciones relacionadas con un grado de libertad 0..100 -> edición por regiones.
//
// No duplica el motor: cada propuesta es una llamada a SoloComposer (motor 2) con parámetros, plan de
// secciones y motivo semilla derivados de la misma identidad; las diferencias entre A/B/C/D son
// musicales (estilo, densidad, plan, transformación del motivo, alineación rítmica), no solo semillas.
// Funciona con beat importado (HarmonyTimeline del análisis) o con acordes manuales.
#include "solo/BeatAnalyzer.h"
#include "solo/CreativeDNA.h"
#include "solo/SoloComposer.h"
#include <string>
#include <vector>

namespace solo
{
enum class MelodyType { Chorus = 0, Solo = 1, Interlude = 2, Intro = 3, Outro = 4, Background = 5 };
constexpr int kNumMelodyTypes = 6;
enum class ChorusCharacter { EasyToSing = 0, Emotional = 1, Energetic = 2 };
enum class ProposalStrategy { Simple = 0, Emotional = 1, Rhythmic = 2, Bold = 3 };
constexpr int kNumStrategies = 4;

const char* melodyTypeName (MelodyType t);
const char* chorusCharacterName (ChorusCharacter c);
const char* strategyName (ProposalStrategy s);
const char* strategyDescription (ProposalStrategy s);

// Lo que pide el usuario (modo Fácil: tipo, carácter, libertad).
struct MelodyBrief
{
    MelodyType type = MelodyType::Chorus;
    ChorusCharacter character = ChorusCharacter::EasyToSing;
    double freedom = 35.0;                      // 0..100 (ver bandas en MelodyExplorer::freedomBand)
    InstrumentId instrument = InstrumentId::Guitar; // instrumento guía
    Style baseStyle = Style::MelodicRock;       // estilo del proyecto (para Solo/Interludio)
    double expression = 65.0;
    uint64_t seed = 1;
};

// Armonía y beat sobre los que se compone. 'harmony' puede venir del análisis o ser manual.
struct MelodyContext
{
    HarmonyTimeline harmony;
    int fromBar = 0, toBar = -1;               // sección (-1 = hasta el final)
    const BeatAnalysisResult* beat = nullptr;  // opcional: acentos y regiones de energía
    double bpm = 120.0;

    int bars() const;                          // compases de la sección
    Progression sectionProgression() const;    // acordes de la sección (uno por compás)
    static MelodyContext manual (const Key& k, const Progression& p, double bpm);
};

// Identidad musical: el motivo principal del que nacen todas las propuestas, con rasgos.
struct MelodyIdentity
{
    SeedMotif motif;        // motivo principal ("hook")
    SeedMotif secondary;    // contraste (puede estar vacío)
    CreativeDNA dna;        // rasgos del material de identidad
    uint64_t motifSeed = 0; // semilla temática común a toda la familia
    std::string description;
    bool valid() const { return ! motif.notes.empty(); }
};

struct MelodyProposal
{
    ProposalStrategy strategy = ProposalStrategy::Simple;
    std::string name;             // "A · Sencilla y pegajosa"
    std::string description;      // qué hace distinta a esta propuesta
    Sequence sequence;
    ComposerParams params;        // parámetros exactos usados (para regenerar regiones / variar)
    double similarityToIdentity = 0; // 0..1 respecto a la realización directa de la identidad
};

struct MelodyFamily
{
    MelodyBrief brief;
    MelodyIdentity identity;
    std::vector<MelodyProposal> proposals; // 4 (A..D)
    double diversity = 0;                  // 0..1 media de (1 - parecido) entre pares
    std::string summary() const;
};

// Métricas de parecido (0..1). No miden calidad artística: solo cuánto se conserva.
struct MelodyMetrics
{
    double interval = 0;   // n-gramas de intervalos (melodicSimilarity)
    double rhythm = 0;     // posiciones de ataque dentro del compás
    double contour = 0;    // histograma de alturas (coseno)
    double overall = 0;    // combinación
};
MelodyMetrics melodySimilarity (const Sequence& a, const Sequence& b);
double familyDiversity (const std::vector<Sequence>& v); // media de (1 - overall) entre pares

// Coherencia armónica 0..1: notas en tiempo fuerte que son del acorde + notas dentro de la escala.
struct HarmonicFit { double strongChordTones = 0; double inScale = 0; int strongNotes = 0; int notes = 0; };
HarmonicFit harmonicFit (const Sequence& s, const Progression& p, const Key& k);

// Banda de libertad creativa (documentada en el encargo).
enum class FreedomBand { Close = 0, Identity = 1, NewPhrases = 2, Experimental = 3 };
const char* freedomBandName (FreedomBand b);

// Estado del explorador dentro del proyecto (se guarda con el estado; versiones anteriores lo ignoran).
// Las propuestas y variaciones viven en el historial de variaciones (favoritas, A/B, deshacer ya existen);
// aquí se guarda la familia: brief, identidad y qué variación nació de qué estrategia.
struct MelodyProjectState
{
    bool active = false;
    MelodyBrief brief;
    MelodyIdentity identity;                       // dna no se persiste (se recalcula si hace falta)
    std::vector<std::pair<uint32_t, int>> variationStrategy; // id de variación -> estrategia (0..3)
    double diversity = 0;
    int strategyOf (uint32_t variationId) const;   // -1 si no pertenece a la familia
};
std::string serializeMelodyState (const MelodyProjectState& m);
bool deserializeMelodyState (const std::string& text, MelodyProjectState& out, std::string& error);
std::string serializeSeedMotif (const SeedMotif& m);          // "len first n onset len interval ..."
bool parseSeedMotif (const std::string& text, SeedMotif& out);

class MelodyExplorer
{
public:
    static FreedomBand freedomBand (double freedom);

    // 1) Identidad: motivo principal coherente con la armonía y el tipo (determinista por semilla).
    static MelodyIdentity createIdentity (const MelodyContext& ctx, const MelodyBrief& brief);

    // 2) Cuatro propuestas A..D a partir de la identidad (misma familia, estrategias distintas).
    static MelodyFamily propose (const MelodyContext& ctx, const MelodyBrief& brief);
    static MelodyProposal proposeOne (const MelodyContext& ctx, const MelodyBrief& brief, const MelodyIdentity& id, ProposalStrategy s);

    // 3) Variaciones relacionadas de una favorita según la libertad del brief (respeta compases bloqueados).
    static std::vector<MelodyProposal> vary (const MelodyContext& ctx, const MelodyBrief& brief, const MelodyIdentity& id,
                                             const MelodyProposal& favorite, int count, uint64_t seed,
                                             const std::vector<bool>& lockedBars);

    // 4) Regenerar solo una región (compases base 0, inclusivos) conservando el resto y los bloqueos.
    static Sequence regenerateRegion (const MelodyContext& ctx, const MelodyProposal& p, int fromBar, int toBar,
                                      uint64_t seed, const std::vector<bool>& lockedBars);

    // Parámetros base del compositor para un tipo de melodía (sin motivo): lo que usa cada estrategia.
    static ComposerParams baseParams (const MelodyContext& ctx, const MelodyBrief& brief);
    // Plan de secciones derivado de las regiones de energía del beat (o del tipo si no hay beat).
    static std::string sectionPlanFor (const MelodyContext& ctx, const MelodyBrief& brief, ProposalStrategy s);
};
} // namespace solo
