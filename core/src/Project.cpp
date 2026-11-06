#include "solo/Project.h"
#include "solo/ChorusComposer.h"
#include <algorithm>
#include <cstdio>
#include <cmath>

namespace solo
{
const char* articulationName (Articulation a)
{
    switch (a)
    {
        case Articulation::Vibrato: return "vibrato";
        case Articulation::Bend: return "bends";
        case Articulation::Slide: return "slides";
        case Articulation::Legato: return "legato";
        case Articulation::PalmMute: return "palm mute";
    }
    return "?";
}

namespace
{
// Rango de compases: -1 (o negativo) = todo el solo. Se recorta a [0, bars-1].
void normaliseBarRange (int& fromBar, int& toBar, int bars)
{
    const int last = std::max (0, bars - 1);
    if (fromBar < 0) { fromBar = 0; if (toBar < 0) toBar = last; }
    if (toBar < 0) toBar = last;
    fromBar = std::clamp (fromBar, 0, last);
    toBar = std::clamp (toBar, fromBar, last);
}
} // namespace

bool ProjectController::params (ComposerParams& out, std::string& error) const
{
    if (! state.settings.toComposerParams (bpm, out, error)) return false;
    out.expression = state.macros.expression;
    return true;
}

std::vector<bool> ProjectController::lockedBarsFor (int bars) const
{
    std::vector<bool> v ((size_t) std::max (0, bars), false);
    for (size_t i = 0; i < v.size() && i < state.lockedBars.size(); ++i) v[i] = state.lockedBars[i];
    return v;
}

OpResult ProjectController::pushVariation (const Sequence& s, uint64_t seed, const std::string& label, const std::string& name)
{
    if (s.notes.empty()) return OpResult::fail ("No se generaron notas");
    if (state.history.size() >= VariationHistory::kMaxTotal && state.history.favoriteCount() >= state.history.size())
        return OpResult::fail ("Historial lleno de favoritas: quita alguna favorita para seguir generando");
    remember (label);
    if (! state.history.push (s, seed, name)) return OpResult::fail ("No se pudo guardar la variación");
    OpResult r;
    r.ok = true;
    r.sequenceChanged = true;
    r.message = label + ": variación " + std::to_string (state.history.currentIndex() + 1) + " (" + std::to_string (s.notes.size()) + " notas)";
    return r;
}

OpResult ProjectController::generate (uint64_t seed)
{
    ComposerParams p;
    std::string err;
    if (! params (p, err)) return OpResult::fail (err);
    if (state.settings.keepMotifs)
    {
        if (state.settings.motifSeed == 0) state.settings.motifSeed = seed ? seed : 1;
        p.motifSeed = state.settings.motifSeed;
    }
    p.seed = seed;
    state.settings.seed = seed;
    return pushVariation (SoloComposer::compose (p), seed, "Generar");
}

OpResult ProjectController::regenerateBars (int fromBar, int toBar, uint64_t seed)
{
    if (! hasSequence()) return OpResult::fail ("Primero genera un solo");
    ComposerParams p;
    std::string err;
    if (! params (p, err)) return OpResult::fail (err);
    if (state.settings.keepMotifs && state.settings.motifSeed) p.motifSeed = state.settings.motifSeed;
    const auto& cur = state.history.current();
    normaliseBarRange (fromBar, toBar, cur.bars);
    const auto out = SoloComposer::regenerate (cur, p, seed, lockedBarsFor (cur.bars), fromBar, toBar);
    return pushVariation (out, state.history.currentSeed(),
                          "Regenerar compases " + std::to_string (fromBar + 1) + "-" + std::to_string (toBar + 1));
}

OpResult ProjectController::regenerateNotes (const std::vector<int>& indices, uint64_t seed)
{
    if (! hasSequence()) return OpResult::fail ("Primero genera un solo");
    if (indices.empty()) return OpResult::fail ("No hay notas seleccionadas");
    ComposerParams p;
    std::string err;
    if (! params (p, err)) return OpResult::fail (err);
    const auto out = SoloComposer::regenerateNotes (state.history.current(), p, seed, indices);
    return pushVariation (out, state.history.currentSeed(), "Regenerar " + std::to_string (indices.size()) + " notas");
}

OpResult ProjectController::setBarsLocked (int fromBar, int toBar, bool locked)
{
    const int bars = hasSequence() ? state.history.current().bars : state.settings.bars;
    normaliseBarRange (fromBar, toBar, bars);
    remember (locked ? "Bloquear compases" : "Desbloquear compases");
    state.lockedBars = lockedBarsFor (bars);
    for (int b = fromBar; b <= toBar; ++b) state.lockedBars[(size_t) b] = locked;
    OpResult r;
    r.ok = true;
    r.message = std::string (locked ? "Bloqueados" : "Desbloqueados") + " los compases " + std::to_string (fromBar + 1) + "-" + std::to_string (toBar + 1);
    return r;
}

OpResult ProjectController::setNotesLocked (const std::vector<int>& indices, bool locked)
{
    if (! hasSequence()) return OpResult::fail ("No hay solo");
    remember (locked ? "Bloquear notas" : "Desbloquear notas");
    auto& s = state.history.current();
    int n = 0;
    for (int i : indices)
        if (i >= 0 && i < (int) s.notes.size()) { s.notes[(size_t) i].locked = locked; ++n; }
    OpResult r;
    r.ok = true;
    r.message = std::to_string (n) + (locked ? " notas bloqueadas" : " notas desbloqueadas");
    return r;
}

OpResult ProjectController::selectVariation (int index)
{
    if (index < 0 || index >= state.history.size()) return OpResult::fail ("Variación inexistente");
    state.history.setCurrentIndex (index);
    OpResult r;
    r.ok = true;
    r.sequenceChanged = true;
    r.message = "Variación " + std::to_string (index + 1);
    return r;
}

OpResult ProjectController::setFavorite (int index, bool fav)
{
    if (index < 0 || index >= state.history.size()) return OpResult::fail ("Variación inexistente");
    remember (fav ? "Marcar favorita" : "Quitar favorita");
    state.history.setFavorite (index, fav);
    OpResult r;
    r.ok = true;
    r.message = std::string ("Variación ") + std::to_string (index + 1) + (fav ? " marcada como favorita" : " ya no es favorita");
    return r;
}

OpResult ProjectController::renameVariation (int index, const std::string& name)
{
    if (index < 0 || index >= state.history.size()) return OpResult::fail ("Variación inexistente");
    remember ("Renombrar variación");
    state.history.rename (index, name);
    OpResult r;
    r.ok = true;
    r.message = "Variación renombrada";
    return r;
}

OpResult ProjectController::deleteVariation (int index)
{
    if (index < 0 || index >= state.history.size()) return OpResult::fail ("Variación inexistente");
    remember ("Borrar variación");
    state.history.remove (index);
    OpResult r;
    r.ok = true;
    r.sequenceChanged = true;
    r.message = "Variación borrada (se puede deshacer)";
    return r;
}

OpResult ProjectController::assignSlot (char slot, int index)
{
    if (index < 0 || index >= state.history.size()) return OpResult::fail ("Variación inexistente");
    const uint32_t id = state.history.all()[(size_t) index].id;
    (slot == 'B' || slot == 'b' ? state.slotB : state.slotA) = id;
    OpResult r;
    r.ok = true;
    r.message = std::string ("Variación ") + std::to_string (index + 1) + " en " + (slot == 'B' || slot == 'b' ? "B" : "A");
    return r;
}

OpResult ProjectController::switchAB()
{
    const int a = state.history.indexOfId (state.slotA), b = state.history.indexOfId (state.slotB);
    if (a < 0 || b < 0) return OpResult::fail ("Asigna primero las variaciones A y B");
    const bool onA = state.history.currentIndex() == a;
    auto r = selectVariation (onA ? b : a);
    r.message = onA ? "Escuchando B" : "Escuchando A";
    return r;
}

OpResult ProjectController::replaceSequence (const Sequence& s, const std::string& label)
{
    if (! hasSequence()) return OpResult::fail ("No hay solo");
    remember (label);
    auto& cur = state.history.current();
    cur = s;
    cur.normalise();
    OpResult r;
    r.ok = true;
    r.sequenceChanged = true;
    r.message = label;
    return r;
}

OpResult ProjectController::transpose (int fromBar, int toBar, int semis)
{
    if (! hasSequence()) return OpResult::fail ("No hay solo");
    auto s = state.history.current();
    normaliseBarRange (fromBar, toBar, s.bars);
    const int64_t barT = s.timeSignature.ticksPerBar();
    const auto& prof = profileFor (state.settings.instrument);
    const auto locked = lockedBarsFor (s.bars);
    int changed = 0, clipped = 0;
    for (auto& n : s.notes)
    {
        const int b = (int) (n.startTick / barT);
        if (b < fromBar || b > toBar || n.locked || locked[(size_t) b]) continue;
        const int np = n.pitch + semis;
        if (np < prof.range.lowest || n.targetPitch() + semis > prof.range.highest) { ++clipped; continue; }
        n.pitch = np;
        ++changed;
    }
    if (changed == 0) return OpResult::fail ("Ninguna nota se puede transportar (bloqueadas o fuera del registro)");
    auto r = replaceSequence (s, "Transportar " + std::to_string (semis) + (std::abs (semis) == 1 ? " semitono" : " semitonos"));
    if (clipped) r.message += " (" + std::to_string (clipped) + " notas fuera del registro sin cambiar)";
    return r;
}

OpResult ProjectController::setArticulation (int fromBar, int toBar, Articulation a, bool on)
{
    if (! hasSequence()) return OpResult::fail ("No hay solo");
    auto s = state.history.current();
    normaliseBarRange (fromBar, toBar, s.bars);
    const int64_t barT = s.timeSignature.ticksPerBar(), beat = s.timeSignature.ticksPerBeat();
    const auto locked = lockedBarsFor (s.bars);
    const auto inst = state.settings.instrument;
    const bool guitar = inst == InstrumentId::Guitar || inst == InstrumentId::GuitarLite;
    if (on && a == Articulation::PalmMute && ! guitar) return OpResult::fail ("El palm mute solo existe en la guitarra");
    if (on && a == Articulation::Bend && inst == InstrumentId::Flute) return OpResult::fail ("La flauta no hace bends");
    const int low = profileFor (inst).range.lowest;
    int changed = 0;
    for (size_t i = 0; i < s.notes.size(); ++i)
    {
        auto& n = s.notes[i];
        const int b = (int) (n.startTick / barT);
        if (b < fromBar || b > toBar || n.locked || locked[(size_t) b]) continue;
        auto& e = n.expr;
        const bool longN = n.lengthTicks >= beat / 2;
        switch (a)
        {
            case Articulation::Vibrato:
                if (on && longN) { e.vibratoDepth = 0.3f; ++changed; }
                else if (! on && e.vibratoDepth > 0) { e.vibratoDepth = 0; ++changed; }
                break;
            case Articulation::Bend:
                if (on && longN && e.bendType == BendType::None && n.pitch - 2 >= low)
                {
                    n.pitch -= 2; // se pisa 2 semitonos abajo y se sube a la nota original
                    e.bendType = BendType::Up; e.bendSemitones = 2; e.bendStart = 0.05f; e.bendTime = 0.3f;
                    ++changed;
                }
                else if (! on && e.bendType != BendType::None)
                {
                    n.pitch = n.targetPitch();
                    e.bendType = BendType::None; e.bendSemitones = 0;
                    ++changed;
                }
                break;
            case Articulation::Slide:
                if (on && longN && e.slideFrom == 0) { e.slideFrom = -2; ++changed; }
                else if (! on && e.slideFrom != 0) { e.slideFrom = 0; ++changed; }
                break;
            case Articulation::Legato:
                if (on && i > 0 && n.startTick - s.notes[i - 1].endTick() <= 30 && n.pitch != s.notes[i - 1].pitch) { e.legato = true; ++changed; }
                else if (! on && e.legato) { e.legato = false; ++changed; }
                break;
            case Articulation::PalmMute:
                if (on && n.lengthTicks <= beat / 2) { e.palmMute = true; ++changed; }
                else if (! on && e.palmMute) { e.palmMute = false; ++changed; }
                break;
        }
    }
    if (changed == 0) return OpResult::fail (std::string ("No hay notas adecuadas para ") + (on ? "añadir " : "quitar ") + articulationName (a));
    auto r = replaceSequence (s, std::string (on ? "Añadir " : "Quitar ") + articulationName (a));
    r.message += " (" + std::to_string (changed) + " notas)";
    return r;
}

OpResult ProjectController::adjust (const std::string& what, double delta)
{
    double* v = what == "energy" ? &state.settings.energy : what == "density" ? &state.settings.density
              : what == "complexity" ? &state.settings.complexity : nullptr;
    if (! v) return OpResult::fail ("Ajuste desconocido: " + what);
    *v = std::clamp (*v + delta, 0.0, 1.0);
    OpResult r;
    r.ok = true;
    r.settingsChanged = true;
    const char* nm = what == "energy" ? "Intensidad" : what == "density" ? "Densidad" : "Complejidad";
    r.message = std::string (nm) + " " + std::to_string ((int) std::lround (*v * 100)) + "% (se aplica al generar)";
    return r;
}

OpResult ProjectController::setStyle (Style s)
{
    state.settings.style = s;
    state.settings.engine = 2;
    OpResult r;
    r.ok = true;
    r.settingsChanged = true;
    r.message = std::string ("Estilo: ") + styleName (s) + " (se aplica al generar)";
    return r;
}

OpResult ProjectController::setInstrument (InstrumentId id)
{
    state.settings.instrument = id;
    OpResult r;
    r.ok = true;
    r.settingsChanged = true;
    r.message = std::string ("Instrumento: ") + instrumentName (id);
    return r;
}

OpResult ProjectController::setFxPreset (const std::string& name)
{
    const int i = fxPresetIndex (name);
    if (i < 0 && name != "Sin efectos") return OpResult::fail ("Preset de efectos desconocido: " + name);
    remember ("Preset de efectos");
    if (i < 0)
    {
        state.settings.fxEnabled = false;
        state.settings.fxPreset = "Sin efectos";
        state.fx = FxSettings {};
    }
    else
    {
        state.settings.fxEnabled = true;
        state.settings.fxPreset = name;
        state.fx = fxPresets()[(size_t) i].settings;
    }
    OpResult r;
    r.ok = true;
    r.fxChanged = true;
    r.message = "Efectos: " + state.settings.fxPreset;
    return r;
}

OpResult ProjectController::setFx (const FxSettings& fx, const std::string& label)
{
    remember (label);
    state.fx = fx;
    state.fx.sanitise();
    state.settings.fxEnabled = true;
    if (fxPresetIndex (state.settings.fxPreset) >= 0 && ! (fxPresets()[(size_t) fxPresetIndex (state.settings.fxPreset)].settings == state.fx))
        state.settings.fxPreset = "Personalizado";
    markToneCustomIfChanged();
    OpResult r;
    r.ok = true;
    r.fxChanged = true;
    r.message = label;
    return r;
}

OpResult ProjectController::adjustFx (AutoTarget t, double delta)
{
    FxSettings fx = state.fx;
    const float v = (float) std::clamp ((double) paramToAuto (t, fx) + delta, 0.0, 1.0);
    setAutoParam (fx, t, v);
    auto r = setFx (fx, std::string (delta > 0 ? "Más " : "Menos ") + autoTargetName (t));
    r.message += " (" + std::to_string ((int) std::lround (v * 100)) + "%)";
    return r;
}

OpResult ProjectController::setFxStage (FxStage stage, bool on)
{
    FxSettings fx = state.fx;
    fx.setOn (stage, on);
    return setFx (fx, std::string (on ? "Activar " : "Desactivar ") + fxStageName (stage));
}

OpResult ProjectController::setAutomation (const AutomationSet& a)
{
    remember ("Editar automatización");
    state.automation = a;
    for (auto& l : state.automation.lanes) l.sortPoints();
    OpResult r;
    r.ok = true;
    r.fxChanged = true;
    r.message = "Automatización actualizada";
    return r;
}

OpResult ProjectController::applyHum (const VoiceAnalysis& va, const HumToSoloOptions& o)
{
    ComposerParams p;
    std::string err;
    if (! params (p, err)) return OpResult::fail (err);
    const auto res = humToSolo (va, p, o);
    if (res.sequence.notes.empty()) return OpResult::fail (res.warnings.empty() ? "La toma no produjo notas" : res.warnings.front());
    auto r = pushVariation (res.sequence, p.seed, std::string ("Voz: ") + humModeName (o.mode), std::string ("Voz (") + humModeName (o.mode) + ")");
    if (! r.ok) return r;
    state.seedMotifs = { res.motif };
    state.dnaSummary = extractDNA (res.sequence).summary();
    for (const auto& w : res.warnings) r.message += " · " + w;
    return r;
}

OpResult ProjectController::applyVocalCorrection (const VoiceAnalysis& take, const HumToSoloOptions& o, int fromBar, int toBar)
{
    if (! hasSequence()) return OpResult::fail ("Primero genera o importa un solo");
    ComposerParams p;
    std::string err;
    if (! params (p, err)) return OpResult::fail (err);
    const auto& cur = state.history.current();
    const auto out = solo::applyVocalCorrection (cur, take, p, o, fromBar, toBar, lockedBarsFor (cur.bars));
    return pushVariation (out, state.history.currentSeed(),
                          "Corregir con voz compases " + std::to_string (fromBar + 1) + "-" + std::to_string (toBar + 1));
}

OpResult ProjectController::extractDNAFromCurrent()
{
    if (! hasSequence()) return OpResult::fail ("No hay solo del que extraer ADN");
    const auto dna = extractDNA (state.history.current());
    if (! dna.valid) return OpResult::fail ("El solo es demasiado corto para extraer ADN");
    state.seedMotifs = dna.motifs;
    state.dnaSummary = dna.summary();
    OpResult r;
    r.ok = true;
    r.message = "ADN extraído: " + state.dnaSummary;
    return r;
}

OpResult ProjectController::generateFromDNA (const DerivedOptions& o, uint64_t seed)
{
    if (! hasSequence() && state.seedMotifs.empty()) return OpResult::fail ("No hay ADN: importa una voz o extrae el ADN de un solo");
    ComposerParams p;
    std::string err;
    if (! params (p, err)) return OpResult::fail (err);
    p.seed = seed;
    CreativeDNA dna = hasSequence() ? extractDNA (state.history.current()) : CreativeDNA {};
    if (! state.seedMotifs.empty()) { dna.motifs = state.seedMotifs; dna.valid = true; if (dna.notesPerBeat <= 0) dna.notesPerBeat = 1.0; }
    const auto s = solo::generateFromDNA (dna, p, o);
    return pushVariation (s, seed, std::string ("ADN: ") + derivedModeName (o.mode), std::string ("ADN (") + derivedModeName (o.mode) + ")");
}

// ======================================================================== Guitar Tone Studio v3
// Un preset de tono sigue "siendo" el preset mientras solo cambien las macros: se compara con el preset de fábrica
// con las macros actuales aplicadas. Cualquier otra edición (Pro) lo convierte en "Personalizado".
void ProjectController::markToneCustomIfChanged()
{
    const int t = tonePresetIndex (state.settings.fxPreset);
    if (t < 0) return;
    FxSettings f = tonePresets()[(size_t) t].fx; GuitarParams g = tonePresets()[(size_t) t].guitar;
    applyMacros (state.macros, f, g);
    if (! (f == state.fx) || ! (g == state.guitar)) state.settings.fxPreset = "Personalizado";
}

OpResult ProjectController::setMacros (const ToneMacros& mIn, bool rememberStep)
{
    ToneMacros m = mIn;
    m.sanitise();
    if (rememberStep)
    {
        if (macroPreviewPending) { macroPreviewBefore.label = "Macros de tono"; undo.push (macroPreviewBefore); macroPreviewPending = false; }
        else remember ("Macros de tono");
    }
    else if (! macroPreviewPending) { macroPreviewBefore = snapshotOf (state, "Macros de tono"); macroPreviewPending = true; }
    state.macros = m;
    applyMacros (m, state.fx, state.guitar);
    state.settings.fxEnabled = true;
    OpResult r;
    r.ok = true;
    r.fxChanged = r.rigChanged = true;
    r.message = "Fuerza " + std::to_string ((int) std::lround (m.force)) + " % · Expresión " + std::to_string ((int) std::lround (m.expression))
                + " % · Ambiente " + std::to_string ((int) std::lround (m.ambience)) + " %";
    return r;
}

OpResult ProjectController::setTonePreset (const std::string& name)
{
    const int i = tonePresetIndex (name);
    if (i < 0) return OpResult::fail ("Preset de tono desconocido: " + name);
    remember ("Preset de tono");
    const auto& p = tonePresets()[(size_t) i];
    state.fx = p.fx;
    state.guitar = p.guitar;
    state.macros = p.macros;
    state.settings.guitarEngine = (int) p.engine;
    state.settings.fxEnabled = true;
    state.settings.fxPreset = name;
    if (p.performer != PerformerProfile::Auto) state.settings.performer = p.performer;
    OpResult r;
    r.ok = true;
    r.fxChanged = r.rigChanged = r.settingsChanged = true;
    r.message = std::string ("Tono: ") + name + " (" + p.description + ")";
    return r;
}

OpResult ProjectController::setGuitarParams (const GuitarParams& g, const std::string& label)
{
    remember (label);
    state.guitar = g;
    state.guitar.sanitise();
    markToneCustomIfChanged();
    OpResult r;
    r.ok = true;
    r.rigChanged = true;
    r.message = label;
    return r;
}

OpResult ProjectController::setGuitarEngine (GuitarEngineKind k)
{
    state.settings.guitarEngine = std::clamp ((int) k, 0, kNumGuitarEngines - 1);
    OpResult r;
    r.ok = true;
    r.rigChanged = r.settingsChanged = true;
    r.message = std::string ("Motor de guitarra: ") + guitarEngineName ((GuitarEngineKind) state.settings.guitarEngine);
    return r;
}

OpResult ProjectController::setSfzPath (const std::string& path)
{
    state.settings.sfzPath = path;
    OpResult r;
    r.ok = true;
    r.rigChanged = r.settingsChanged = true;
    r.message = path.empty() ? "Sin biblioteca de samples" : "Biblioteca SFZ: " + path;
    return r;
}

// ---------------------------------------------------------------- beat (V4-A)
OpResult ProjectController::importBeat (const MonoAudio& audio, const std::string& path, const BeatAnalysisOptions& o)
{
    BeatProject b;
    b.path = path;
    b.analysis = analyseBeat (audio, o);
    if (! b.analysis.ok) return OpResult::fail ("No se pudo analizar el beat: " + b.analysis.error);
    if (b.analysis.silent) return OpResult::fail ("El archivo está en silencio: no hay beat que analizar");
    b.harmony = b.analysis.harmony;
    if (b.harmony.empty())
    {
        // Sin acordes fiables: línea armónica vacía de acordes pero con tempo/compases, para que el usuario los escriba.
        b.harmony.bpm = b.analysis.bpm > 0 ? b.analysis.bpm : state.settings.manualBpm;
        b.harmony.bpmConfidence = b.analysis.bpmConfidence;
        b.harmony.beatsPerBar = b.analysis.beatsPerBar;
        b.harmony.bars = b.analysis.bars;
        if (b.analysis.tonal) { b.harmony.key = b.analysis.key; b.harmony.keyConfidence = b.analysis.keyConfidence; }
    }
    b.sectionFromBar = 0; b.sectionToBar = -1;
    state.beat = b;
    OpResult r;
    r.ok = true;
    r.settingsChanged = true;
    r.message = "Beat importado: " + b.analysis.summary();
    for (const auto& w : b.analysis.warnings) r.message += "\n· " + w;
    return r;
}

OpResult ProjectController::clearBeat()
{
    state.beat = BeatProject {};
    OpResult r; r.ok = true; r.settingsChanged = true; r.message = "Beat quitado: el compositor usa los acordes manuales";
    return r;
}

OpResult ProjectController::confirmBeatBpm (double bpm)
{
    if (! hasBeat()) return OpResult::fail ("No hay beat importado");
    if (! (bpm >= 20 && bpm <= 400)) return OpResult::fail ("BPM fuera de rango (20-400)");
    state.beat.harmony.setBpm (bpm, Provenance::Confirmed);
    OpResult r; r.ok = true; r.settingsChanged = true;
    char buf[64]; std::snprintf (buf, sizeof (buf), "BPM confirmado: %.1f", bpm); r.message = buf;
    return r;
}

OpResult ProjectController::confirmBeatKey (const Key& k)
{
    if (! hasBeat()) return OpResult::fail ("No hay beat importado");
    state.beat.harmony.setKey (k, Provenance::Confirmed);
    OpResult r; r.ok = true; r.settingsChanged = true; r.message = "Tonalidad confirmada: " + k.toString();
    return r;
}

OpResult ProjectController::setBeatChord (int fromBar, int toBar, const Chord& c)
{
    if (! hasBeat()) return OpResult::fail ("No hay beat importado");
    auto& h = state.beat.harmony;
    if (h.bars <= 0) return OpResult::fail ("El beat no tiene compases detectados: escribe la progresión completa");
    if (fromBar < 0 || toBar < fromBar || toBar >= h.bars) return OpResult::fail ("Compases fuera del beat");
    h.setChord (fromBar, toBar, c, Provenance::Confirmed);
    OpResult r; r.ok = true; r.settingsChanged = true;
    r.message = "Acorde " + c.toString (h.key.prefersFlats()) + " en compases " + std::to_string (fromBar + 1) + "-" + std::to_string (toBar + 1);
    return r;
}

OpResult ProjectController::setBeatChords (const Progression& p)
{
    if (! hasBeat()) return OpResult::fail ("No hay beat importado");
    if (p.bars.empty()) return OpResult::fail ("Progresión vacía");
    auto& h = state.beat.harmony;
    const int bars = h.bars > 0 ? h.bars : std::max (state.beat.analysis.bars, (int) p.bars.size());
    Progression full;
    for (int b = 0; b < bars; ++b) full.bars.push_back (p.bars[(size_t) b % p.bars.size()]); // se repite hasta cubrir el beat
    const HarmonyTimeline manual = HarmonyTimeline::fromManual (h.key, full, h.bpm, h.beatsPerBar);
    h.segments = manual.segments; h.bars = bars;
    OpResult r; r.ok = true; r.settingsChanged = true; r.message = "Acordes manuales: " + p.toString (h.key.prefersFlats());
    return r;
}

OpResult ProjectController::setBeatSection (int fromBar, int toBar)
{
    if (! hasBeat()) return OpResult::fail ("No hay beat importado");
    const int total = std::max (state.beat.harmony.bars, state.beat.analysis.bars);
    if (fromBar < 0 || fromBar >= std::max (1, total) || (toBar >= 0 && toBar < fromBar)) return OpResult::fail ("Sección fuera del beat");
    state.beat.sectionFromBar = fromBar; state.beat.sectionToBar = toBar;
    OpResult r; r.ok = true; r.settingsChanged = true;
    r.message = "Sección: compases " + std::to_string (fromBar + 1) + "-" + std::to_string ((toBar < 0 ? total : toBar + 1));
    return r;
}

OpResult ProjectController::applyBeatToSettings()
{
    if (! hasBeat()) return OpResult::fail ("No hay beat importado");
    const auto& b = state.beat;
    const auto& h = b.harmony;
    if (h.segments.empty()) return OpResult::fail ("El beat no tiene acordes (ni detectados ni manuales): escríbelos primero");
    const int total = h.bars;
    const int from = std::max (0, std::min (b.sectionFromBar, total - 1));
    const int to = b.sectionToBar < 0 ? total - 1 : std::min (b.sectionToBar, total - 1);
    Progression p;
    for (int bar = from; bar <= to; ++bar) p.bars.push_back (h.chordAtBar (bar));
    if (p.bars.empty()) return OpResult::fail ("La sección no tiene compases");
    state.settings.keyText = h.key.toString();
    state.settings.progressionText = p.toString (h.key.prefersFlats());
    state.settings.bars = (int) p.bars.size();
    state.settings.tsNumerator = h.beatsPerBar;
    state.settings.manualBpm = h.bpm;
    state.settings.useHostTempo = false;
    OpResult r; r.ok = true; r.settingsChanged = true;
    char buf[96]; std::snprintf (buf, sizeof (buf), "Compositor ajustado al beat: %s, %.1f BPM, %d compases", h.key.toString().c_str(), h.bpm, (int) p.bars.size());
    r.message = buf;
    return r;
}

// ---------------------------------------------------------------- Melody Explorer (V4-B/C/D)
bool ProjectController::melodyContext (MelodyContext& out, std::string& error) const
{
    MelodyContext c;
    if (hasBeat() && ! state.beat.harmony.segments.empty())
    {
        c.harmony = state.beat.harmony;
        c.fromBar = state.beat.sectionFromBar;
        c.toBar = state.beat.sectionToBar;
        c.beat = &state.beat.analysis;
        c.bpm = state.beat.harmony.bpm;
    }
    else
    {
        ComposerParams p;
        if (! params (p, error)) return false;
        Progression full;
        for (int b = 0; b < p.bars; ++b) full.bars.push_back (p.progression.chordAtBar (b));
        c = MelodyContext::manual (p.key, full, p.bpm);
        c.harmony.beatsPerBar = p.timeSignature.numerator;
    }
    if (c.bars() <= 0) { error = "No hay compases sobre los que componer"; return false; }
    out = c;
    return true;
}

MelodyBrief ProjectController::defaultBrief() const
{
    MelodyBrief b = state.melody.brief;
    b.instrument = state.settings.instrument;
    b.baseStyle = state.settings.style;
    b.expression = state.macros.expression;
    return b;
}

OpResult ProjectController::generateProposals (const MelodyBrief& brief)
{
    MelodyContext ctx; std::string err;
    if (! melodyContext (ctx, err)) return OpResult::fail (err);
    if (state.history.size() + 4 > VariationHistory::kMaxTotal && state.history.favoriteCount() + 4 > VariationHistory::kMaxTotal)
        return OpResult::fail ("Historial lleno de favoritas: quita alguna favorita para seguir generando");
    const MelodyFamily fam = MelodyExplorer::propose (ctx, brief);
    if (fam.proposals.size() != 4) return OpResult::fail ("No se pudo crear la identidad musical (¿armonía vacía?)");
    remember ("Generar 4 propuestas");
    MelodyProjectState ms;
    ms.active = true; ms.brief = brief; ms.identity = fam.identity; ms.diversity = fam.diversity;
    ms.variationStrategy = state.melody.variationStrategy; // familias anteriores siguen reconocibles
    for (const auto& p : fam.proposals)
    {
        if (! state.history.push (p.sequence, p.params.seed, p.name)) continue;
        ms.variationStrategy.push_back ({ state.history.currentItem().id, (int) p.strategy });
    }
    state.history.setCurrentIndex (state.history.size() - 4);
    state.melody = ms;
    state.settings.seed = brief.seed;
    OpResult r; r.ok = true; r.sequenceChanged = true; r.settingsChanged = true;
    r.message = "4 propuestas (" + fam.summary() + ")";
    return r;
}

bool ProjectController::melodyProposalFor (const VariationHistory::Item& item, MelodyProposal& out) const
{
    if (! state.melody.active || ! state.melody.identity.valid()) return false;
    int strategy = state.melody.strategyOf (item.id);
    if (strategy < 0) strategy = 0;
    MelodyContext ctx; std::string err;
    if (! melodyContext (ctx, err)) return false;
    // Parámetros exactos de la estrategia (determinista) + la secuencia tal como está ahora.
    MelodyProposal p = MelodyExplorer::proposeOne (ctx, state.melody.brief, state.melody.identity, (ProposalStrategy) strategy);
    p.sequence = item.sequence;
    p.params.bars = item.sequence.bars;
    if ((int) p.params.progression.bars.size() != item.sequence.bars)
    {
        Progression full;
        for (int b = 0; b < item.sequence.bars; ++b) full.bars.push_back (p.params.progression.chordAtBar (b));
        p.params.progression = full;
    }
    out = p;
    return true;
}

OpResult ProjectController::varyCurrent (int count, uint64_t seed)
{
    if (! hasSequence()) return OpResult::fail ("No hay variación de la que partir");
    if (! state.melody.active) return OpResult::fail ("Primero genera propuestas con el explorador");
    MelodyContext ctx; std::string err;
    if (! melodyContext (ctx, err)) return OpResult::fail (err);
    MelodyProposal fav;
    if (! melodyProposalFor (state.history.currentItem(), fav)) return OpResult::fail ("No se pudo recuperar la familia de esta variación");
    count = std::clamp (count, 1, 8);
    const auto vars = MelodyExplorer::vary (ctx, state.melody.brief, state.melody.identity, fav, count, seed, lockedBarsFor (fav.sequence.bars));
    if (vars.empty()) return OpResult::fail ("No se generaron variaciones");
    remember ("Variar esta idea");
    const int strategy = std::max (0, state.melody.strategyOf (state.history.currentItem().id));
    int added = 0; double simSum = 0;
    for (const auto& v : vars)
    {
        if (! state.history.push (v.sequence, v.params.seed, v.name)) continue;
        state.melody.variationStrategy.push_back ({ state.history.currentItem().id, strategy });
        simSum += v.similarityToIdentity; ++added;
    }
    if (added == 0) return OpResult::fail ("Historial lleno");
    OpResult r; r.ok = true; r.sequenceChanged = true;
    char buf[160];
    std::snprintf (buf, sizeof (buf), "%d variaciones (%s, parecido medio con la favorita %d %%)", added,
                   freedomBandName (MelodyExplorer::freedomBand (state.melody.brief.freedom)), (int) std::lround (100.0 * simSum / added));
    r.message = buf;
    return r;
}

OpResult ProjectController::regenerateMelodyRegion (int fromBar, int toBar, uint64_t seed)
{
    if (! hasSequence()) return OpResult::fail ("No hay solo");
    if (! state.melody.active) return regenerateBars (fromBar, toBar, seed);
    MelodyContext ctx; std::string err;
    if (! melodyContext (ctx, err)) return OpResult::fail (err);
    MelodyProposal cur;
    if (! melodyProposalFor (state.history.currentItem(), cur)) return regenerateBars (fromBar, toBar, seed);
    normaliseBarRange (fromBar, toBar, cur.sequence.bars);
    Sequence out = MelodyExplorer::regenerateRegion (ctx, cur, fromBar, toBar, seed, lockedBarsFor (cur.sequence.bars));
    if (state.melody.brief.type == MelodyType::Chorus)
    {
        ChorusShapeOptions o; o.character = state.melody.brief.character; o.mirrorHalves = false;
        std::vector<bool> keep = lockedBarsFor (cur.sequence.bars);
        for (int b = 0; b < cur.sequence.bars; ++b) if (b < fromBar || b > toBar) keep[(size_t) b] = true; // fuera de la región no se toca
        out = shapeChorus (out, cur.params, cur.params.progression, o, keep);
    }
    return replaceSequence (out, "Regenerar compases " + std::to_string (fromBar + 1) + "-" + std::to_string (toBar + 1) + " (familia)");
}

OpResult ProjectController::setMelodyFreedom (double freedom)
{
    state.melody.brief.freedom = std::clamp (freedom, 0.0, 100.0);
    OpResult r; r.ok = true; r.settingsChanged = true;
    r.message = "Libertad creativa " + std::to_string ((int) std::lround (state.melody.brief.freedom)) + " %: " + freedomBandName (MelodyExplorer::freedomBand (state.melody.brief.freedom));
    return r;
}

GuitarRigSettings ProjectController::rigSettings() const
{
    GuitarRigSettings r;
    r.engine = (GuitarEngineKind) std::clamp (state.settings.guitarEngine, 0, kNumGuitarEngines - 1);
    r.guitar = state.guitar;
    r.expression = expressionScaling (state.macros.expression);
    return r;
}

OpResult ProjectController::undoLast()
{
    ProjectSnapshot cur = snapshotOf (state, "");
    if (! undo.undo (cur)) return OpResult::fail ("Nada que deshacer");
    applySnapshot (state, cur);
    OpResult r;
    r.ok = true;
    r.sequenceChanged = r.fxChanged = r.rigChanged = true;
    r.message = "Deshecho: " + cur.label;
    return r;
}

OpResult ProjectController::redoLast()
{
    ProjectSnapshot cur = snapshotOf (state, "");
    if (! undo.redo (cur)) return OpResult::fail ("Nada que rehacer");
    applySnapshot (state, cur);
    OpResult r;
    r.ok = true;
    r.sequenceChanged = r.fxChanged = r.rigChanged = true;
    r.message = "Rehecho: " + cur.label;
    return r;
}

} // namespace solo
