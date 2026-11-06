#pragma once
// StateManager: persistencia versionada del proyecto (ajustes, variaciones, efectos, automatización,
// bloqueos, A/B y material temático). Formato de texto legible, validado al cargar.
// Un estado inválido nunca se aplica a medias. Lee estados v1 (S0–S2) y los migra.
#include "solo/BeatAnalyzer.h"
#include "solo/MelodyExplorer.h"
#include "solo/Effects.h"
#include "solo/SoloComposer.h"
#include "solo/SoloEngine.h"
#include "solo/ToneStudio.h"
#include <string>
#include <vector>

namespace solo
{
struct SoloSettings
{
    std::string keyText = "Am";
    std::string progressionText = "Am F C G";
    int tsNumerator = 4;
    int bars = 8;
    InstrumentId instrument = InstrumentId::Guitar;
    Style style = Style::MelodicRock;
    double energy = 0.6, density = 0.5, complexity = 0.4;
    uint64_t seed = 1;
    double manualBpm = 120.0;
    bool useHostTempo = true;
    bool syncToHost = true; // true: suena con el play del DAW; false: solo con "Escuchar"
    int startBar = 1;       // compás del DAW donde empieza el solo (base 1)
    bool loop = true;
    double outputGain = 1.0;
    bool proMode = false;
    int proTab = 0;                 // última pestaña Pro abierta (solo interfaz)
    int wavSampleRate = 48000;
    int midiFormat = 1;
    bool midiPitchBend = true; // exportar bends/vibrato/slides como pitch bend

    // v2
    int engine = 2;
    int legacyStyle = -1;      // solo motor 1 (proyectos migrados de S0–S2)
    PerformerProfile performer = PerformerProfile::Auto;
    uint64_t motifSeed = 0;
    bool keepMotifs = false;   // al generar variaciones, conservar los motivos
    std::string sectionPlan;   // vacío = automático
    double fidelity = 0.5, freedom = 0.5;
    std::string fxPreset = "Rock Lead"; // preset de efectos (v2) o de tono (v3, ver tonePresets)
    bool fxEnabled = true;

    // v3 Guitar Tone Studio
    int guitarEngine = 1;      // GuitarEngineKind: 0 motor v2, 1 modelado v3, 2 samples (SFZ)
    std::string sfzPath;       // biblioteca SFZ del usuario (motor de samples); vacío = ninguna

    // Construye los parámetros del compositor; devuelve false con el error si tonalidad/progresión no son válidas.
    bool toComposerParams (double bpm, ComposerParams& out, std::string& error) const;
};

struct SoloState
{
    static constexpr int kVersion = 2;
    SoloState(); // efectos, guitarra y macros iniciales = preset de tono "Rock Lead"
    SoloSettings settings;
    GuitarParams guitar;   // fuente (motores v3); persiste como gtr.clave=valor
    ToneMacros macros;     // Fuerza / Expresión / Ambiente (última posición aplicada); tone.clave=valor
    VariationHistory history;
    FxSettings fx;
    AutomationSet automation;
    std::vector<bool> lockedBars;        // por compás (base 0)
    uint32_t slotA = 0, slotB = 0;       // ids de variación para comparar A/B (0 = vacío)
    std::vector<SeedMotif> seedMotifs;   // material temático (Creative DNA / voz)
    std::string dnaSummary;              // descripción legible del ADN extraído
    BeatProject beat;                    // V4: beat importado (análisis + armonía editable); vacío si no hay
    MelodyProjectState melody;           // V4: familia de propuestas del Melody Explorer
    int loadedFromVersion = kVersion;    // informativo (no se serializa)
};

std::string serializeState (const SoloState& s);
bool deserializeState (const std::string& text, SoloState& out, std::string& error);

// Migración de estilos de S0–S2 (0 Melódico, 1 Rock, 2 Blues, 3 Electrónico).
Style styleFromV1 (int v1Style);

// Presets: solo ajustes (sin secuencias).
std::string serializePreset (const SoloSettings& s, const std::string& name);
bool deserializePreset (const std::string& text, SoloSettings& out, std::string& name, std::string& error);

struct FactoryPreset { const char* name; SoloSettings settings; };
std::vector<FactoryPreset> factoryPresets();

// ---------------------------------------------------------------- deshacer / rehacer
// Instantánea de todo lo que el usuario puede editar (no incluye ajustes de interfaz).
struct ProjectSnapshot
{
    VariationHistory history;
    FxSettings fx;
    GuitarParams guitar;
    ToneMacros macros;
    AutomationSet automation;
    std::vector<bool> lockedBars;
    std::string label; // qué acción produjo el cambio (para la interfaz)
};

class UndoStack
{
public:
    static constexpr int kMaxSteps = 40;
    // Guardar el estado ANTES de una modificación. Borra el "rehacer".
    void push (ProjectSnapshot before);
    bool canUndo() const { return ! undoItems.empty(); }
    bool canRedo() const { return ! redoItems.empty(); }
    // Intercambia: devuelve el estado al que hay que volver y guarda 'current' para rehacer.
    bool undo (ProjectSnapshot& current);
    bool redo (ProjectSnapshot& current);
    std::string undoLabel() const { return undoItems.empty() ? std::string() : undoItems.back().label; }
    std::string redoLabel() const { return redoItems.empty() ? std::string() : redoItems.back().label; }
    void clear() { undoItems.clear(); redoItems.clear(); }
    int undoCount() const { return (int) undoItems.size(); }

private:
    std::vector<ProjectSnapshot> undoItems, redoItems;
};

ProjectSnapshot snapshotOf (const SoloState& s, const std::string& label);
void applySnapshot (SoloState& s, const ProjectSnapshot& snap);

} // namespace solo
