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
    bool rigChanged = false;      // motor de guitarra, parámetros de la fuente, escala de expresión o banco SFZ

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
    // ---- Guitar Tone Studio v3 (entran en deshacer salvo el motor y la ruta SFZ)
    // Macros: escriben valores DSP reales en fx/guitar y la escala de expresión. remember=false para previsualizar
    // mientras se arrastra la perilla (sin paso de deshacer); al soltar, remember=true crea el paso.
    // Durante la previsualización (remember=false) se guarda aparte el estado anterior al primer cambio; el commit
    // (remember=true) crea un único paso de deshacer que vuelve a ese estado.
    OpResult setMacros (const ToneMacros& m, bool remember = true);
    OpResult setTonePreset (const std::string& name);  // fx + guitarra + macros + motor
    OpResult setGuitarParams (const GuitarParams& g, const std::string& label = "Editar guitarra");
    OpResult setGuitarEngine (GuitarEngineKind k);
    OpResult setSfzPath (const std::string& path);      // solo guarda la ruta (la carga la hace quien llama)
    GuitarRigSettings rigSettings() const;              // lo que hay que publicar al motor de audio
    // ---- voz y ADN
    OpResult applyHum (const VoiceAnalysis& va, const HumToSoloOptions& o);
    OpResult applyVocalCorrection (const VoiceAnalysis& take, const HumToSoloOptions& o, int fromBar, int toBar);
    OpResult extractDNAFromCurrent();
    OpResult generateFromDNA (const DerivedOptions& o, uint64_t seed);
    // ---- beat (V4-A). importBeat ejecuta el análisis: nunca desde el hilo de audio.
    OpResult importBeat (const MonoAudio& audio, const std::string& path, const BeatAnalysisOptions& o = {});
    OpResult clearBeat();
    OpResult confirmBeatBpm (double bpm);                  // el usuario fija el tempo
    OpResult confirmBeatKey (const Key& k);                // el usuario fija la tonalidad
    OpResult setBeatChord (int fromBar, int toBar, const Chord& c); // acorde confirmado a mano
    OpResult setBeatChords (const Progression& p);         // toda la progresión a mano (beat percusivo o análisis dudoso)
    OpResult setBeatSection (int fromBar, int toBar);      // sección sobre la que se compone (-1 = hasta el final)
    OpResult applyBeatToSettings();                        // copia BPM/tonalidad/acordes de la sección a los ajustes del compositor
    bool hasBeat() const { return state.beat.loaded(); }
    // ---- Melody Explorer (V4-B/C/D). Contexto = beat importado (sección) o acordes manuales de los ajustes.
    bool melodyContext (MelodyContext& out, std::string& error) const;   // out.beat apunta a state.beat.analysis (válido mientras no cambie el estado)
    MelodyBrief defaultBrief() const;                                    // instrumento/estilo/expresión del proyecto + brief guardado
    OpResult generateProposals (const MelodyBrief& brief);               // 4 propuestas -> 4 variaciones (una entrada de deshacer)
    OpResult varyCurrent (int count, uint64_t seed);                     // variaciones relacionadas de la variación actual (libertad del brief)
    OpResult regenerateMelodyRegion (int fromBar, int toBar, uint64_t seed); // regenera compases de la variación actual con sus parámetros de familia
    OpResult setMelodyFreedom (double freedom);                          // 0..100 (no toca notas)
    bool melodyProposalFor (const VariationHistory::Item& item, MelodyProposal& out) const; // parámetros de familia de una variación
    // ---- deshacer
    OpResult undoLast();
    OpResult redoLast();

    bool hasSequence() const { return ! state.history.empty(); }
    const Sequence* current() const { return state.history.empty() ? nullptr : &state.history.current(); }
    bool params (ComposerParams& out, std::string& error) const;
    std::vector<bool> lockedBarsFor (int bars) const;

private:
    void remember (const std::string& label) { undo.push (snapshotOf (state, label)); }
    void markToneCustomIfChanged();
    bool macroPreviewPending = false;
    ProjectSnapshot macroPreviewBefore;
    OpResult pushVariation (const Sequence& s, uint64_t seed, const std::string& label, const std::string& name = {});
};

} // namespace solo
