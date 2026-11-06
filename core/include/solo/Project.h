#pragma once
// ProjectController: todas las operaciones de edición del proyecto, en el núcleo (sin JUCE ni hilos).
// El plugin, la línea de comandos y la dirección por voz usan las mismas operaciones, con deshacer/rehacer.
// No es seguro para tiempo real: se usa desde el hilo de mensajes.
#include "solo/CreativeDNA.h"
#include "solo/StateManager.h"
#include "solo/VoiceAnalysis.h"
#include <string>
#include <vector>

namespace solo
{
struct OpResult
{
    bool ok = false;
    std::string message;
    bool sequenceChanged = false; // hay que volver a publicar la reproducción
    bool fxChanged = false;
    bool settingsChanged = false;

    static OpResult fail (std::string m) { OpResult r; r.message = std::move (m); return r; }
};

enum class Articulation { Vibrato, Bend, Slide, Legato, PalmMute };
const char* articulationName (Articulation a);

// Rangos de compases en base 0; fromBar = -1 significa «todo el solo».
class ProjectController
{
public:
    SoloState state;
    UndoStack undo;
    double bpm = 120.0; // tempo efectivo (host o manual), lo fija quien llama

    // ---- generación
    OpResult generate (uint64_t seed);                       // variación nueva
    OpResult regenerateBars (int fromBar, int toBar, uint64_t seed); // respeta bloqueos
    OpResult regenerateNotes (const std::vector<int>& indices, uint64_t seed);
    // ---- bloqueos
    OpResult setBarsLocked (int fromBar, int toBar, bool locked);
    OpResult setNotesLocked (const std::vector<int>& indices, bool locked);
    // ---- variaciones, favoritas, A/B
    OpResult selectVariation (int index);
    OpResult setFavorite (int index, bool fav);
    OpResult renameVariation (int index, const std::string& name);
    OpResult deleteVariation (int index);
    OpResult assignSlot (char slot, int index); // 'A' o 'B'
    OpResult switchAB();                        // alterna entre A y B
    // ---- edición
    OpResult replaceSequence (const Sequence& s, const std::string& label = "Editar notas");
    OpResult transpose (int fromBar, int toBar, int semitones);
    OpResult setArticulation (int fromBar, int toBar, Articulation a, bool on);
    // ---- ajustes que no tocan notas (no entran en deshacer)
    OpResult adjust (const std::string& what, double delta); // "energy" | "density" | "complexity"
    OpResult setStyle (Style s);
    OpResult setInstrument (InstrumentId id);
    // ---- efectos y automatización (entran en deshacer)
    OpResult setFxPreset (const std::string& name);
    OpResult setFx (const FxSettings& fx, const std::string& label = "Editar efectos");
    OpResult adjustFx (AutoTarget target, double delta);  // ± sobre el valor normalizado
    OpResult setFxStage (FxStage stage, bool on);
    OpResult setAutomation (const AutomationSet& a);
    // ---- voz y ADN
    OpResult applyHum (const VoiceAnalysis& va, const HumToSoloOptions& o);
    OpResult applyVocalCorrection (const VoiceAnalysis& take, const HumToSoloOptions& o, int fromBar, int toBar);
    OpResult extractDNAFromCurrent();
    OpResult generateFromDNA (const DerivedOptions& o, uint64_t seed);
    // ---- deshacer
    OpResult undoLast();
    OpResult redoLast();

    bool hasSequence() const { return ! state.history.empty(); }
    const Sequence* current() const { return state.history.empty() ? nullptr : &state.history.current(); }
    bool params (ComposerParams& out, std::string& error) const;
    std::vector<bool> lockedBarsFor (int bars) const;

private:
    void remember (const std::string& label) { undo.push (snapshotOf (state, label)); }
    OpResult pushVariation (const Sequence& s, uint64_t seed, const std::string& label, const std::string& name = {});
};

} // namespace solo
