#pragma once
// SoloComposer: compositor algorítmico condicionado por la armonía.
// No es aprendizaje automático: reglas musicales + aleatoriedad controlada por semilla.
//
// Motor 2 (por defecto) — composición jerárquica:
//   plan del solo (secciones con función) -> frases (llamada/respuesta) -> motivos
//   (principal y secundario, desarrollados) -> notas -> articulaciones (intérprete).
// Motor 1 — el compositor S0–S2, conservado para comparaciones A/B.
#include "solo/MusicTheory.h"
#include "solo/Sequence.h"
#include <cstdint>
#include <string>
#include <vector>

namespace solo
{
enum class InstrumentId
{
    Guitar = 0,     // guitarra eléctrica modelada (expresiva)
    Flute = 1,
    Synth = 2,
    GuitarLite = 3  // modelo de guitarra S0–S2 (ligero)
};
constexpr int kNumInstruments = 4;

enum class Style
{
    MelodicRock = 0,
    ExpressiveBlues = 1,
    Emotional = 2,
    VirtuosoRock = 3,
    Ambient = 4,
    Cinematic = 5,
    SoftImprov = 6,
    VocalResponse = 7
};
constexpr int kNumStyles = 8;

enum class PerformerProfile
{
    Auto = 0,     // según el estilo
    Melodic = 1,
    Blues = 2,
    Rock = 3,
    Virtuoso = 4,
    Ambient = 5,
    None = 6      // sin articulaciones (notas "planas")
};
constexpr int kNumPerformers = 7;

const char* instrumentName (InstrumentId id);
const char* styleName (Style s);
const char* performerName (PerformerProfile p);

// Perfil físico del instrumento: registro, saltos, respiración y articulación.
struct InstrumentProfile
{
    InstrumentId id;
    InstrumentRange range;
    int maxLeapSemitones;      // salto máximo entre notas consecutivas
    double legato;             // fracción de la duración nominal que suena (motor 1)
    double densityBias;        // multiplicador de densidad rítmica
    int maxPhraseBars;         // longitud máxima de frase sin respirar
    double maxNotesPerSecond;  // límite de interpretabilidad
    double leapProbability;    // tendencia a saltos frente a grados conjuntos
    int gmProgram;             // programa General MIDI sugerido al exportar
};
const InstrumentProfile& profileFor (InstrumentId id);

// Función de una sección dentro del solo.
enum class SectionRole { Presentation, Development, Tension, Climax, Resolution };

struct PlanSection
{
    int startBar = 0;
    int bars = 1;
    SectionRole role = SectionRole::Presentation;
    double intensity = 0.5; // 0..1
};

// Motivo semilla (p. ej. extraído de una melodía cantada): notas relativas.
struct SeedMotifNote
{
    int64_t onset = 0;     // ticks desde el inicio del motivo
    int64_t length = 0;
    int interval = 0;      // semitonos respecto a la nota anterior (la primera: 0)
};
struct SeedMotif
{
    std::vector<SeedMotifNote> notes;
    int64_t lengthTicks = 0;
    int firstPitch = -1;   // altura original de la primera nota (opcional)
};

struct ComposerParams
{
    Key key;
    Progression progression;
    TimeSignature timeSignature;
    int bars = 8;
    InstrumentId instrument = InstrumentId::Guitar;
    Style style = Style::MelodicRock;
    double energy = 0.6;     // 0..1 intensidad (dinámica, registro, actividad)
    double density = 0.5;    // 0..1 cantidad de notas
    double complexity = 0.4; // 0..1 variación, cromatismo, saltos
    double bpm = 120.0;      // para limitar notas por segundo
    uint64_t seed = 1;

    int engine = 2;          // 1 = compositor S0–S2, 2 = jerárquico
    int legacyStyle = -1;    // solo motor 1: estilo antiguo explícito (0..3)
    uint64_t motifSeed = 0;  // 0 = derivada de 'seed'. Fijarla conserva los motivos entre variaciones.
    std::string sectionPlan; // vacío = automático; o p. ej. "P2 D2 T2 C1 R1" (P,D,T,C,R + compases)
    PerformerProfile performer = PerformerProfile::Auto;
    double expression = 65.0; // macro Expresión (0..100): cuánta tendencia tiene el intérprete a escribir gestos

    // Material temático externo (Creative DNA / voz).
    std::vector<SeedMotif> seedMotifs;
    double fidelity = 0.5;   // 0..1 cuánto se respeta el motivo semilla
    double freedom = 0.5;    // 0..1 cuánto se aleja/desarrolla
};

class SoloComposer
{
public:
    // Genera un solo completo. Misma entrada (incluida la semilla) => misma salida.
    static Sequence compose (const ComposerParams& params);

    // Plan de secciones que usará el motor 2 (también útil para la interfaz).
    static std::vector<PlanSection> plan (const ComposerParams& params);

    // Regenera respetando lo protegido: compases bloqueados (lockedBars[b] == true) y notas con
    // locked == true quedan idénticos. 'onlyRange' limita el cambio a [fromBar, toBar] (base 0, inclusivo).
    static Sequence regenerate (const Sequence& current, const ComposerParams& params, uint64_t newSeed,
                                const std::vector<bool>& lockedBars, int fromBar = -1, int toBar = -1);

    // Regenera solo las notas indicadas (índices en current.notes); el resto queda idéntico.
    static Sequence regenerateNotes (const Sequence& current, const ComposerParams& params, uint64_t newSeed,
                                     const std::vector<int>& noteIndices);

    // Compatibilidad S0–S2: regenera [fromBar, toBar] sin bloqueos.
    static Sequence regenerateBars (const Sequence& current, const ComposerParams& params,
                                    int fromBar, int toBar, uint64_t newSeed);

    // Errores que impiden componer (vacío si todo es correcto).
    static std::vector<std::string> validate (const ComposerParams& params);
};

// Motor 1 (definido en LegacyComposer.cpp).
Sequence legacyCompose (const ComposerParams& params);

} // namespace solo
