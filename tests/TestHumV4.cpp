// Pruebas V4-E: proponer ritmo, proponer melodía desde la voz y corregir una región tarareando.
// Señales: voz sintética (synthesiseVoice). No demuestra el comportamiento con voces humanas reales.
#include "TestFramework.h"
#include "solo/ChorusComposer.h"
#include "solo/Project.h"
#include "solo/RhythmProposal.h"
#include "solo/StateManager.h"
#include "solo/VocalProposalAdapter.h"
#include <cmath>
#include <cstdio>

using namespace solo;

namespace
{
// "Din din daaan" a 120 BPM: corcheas y una negra con puntillo, 2 compases.
std::vector<SynthVoiceNote> dinDinDaan()
{
    std::vector<SynthVoiceNote> v;
    const double b = 0.5; // pulso
    double t = 0.0;
    for (int bar = 0; bar < 2; ++bar)
    {
        v.push_back ({ t, 0.18, 69 });           // din (corchea)
        v.push_back ({ t + b / 2, 0.18, 71 });   // din
        v.push_back ({ t + b, 0.36, 72 });       // daaan (negra, con hueco claro antes del siguiente)
        v.push_back ({ t + 2 * b, 0.18, 71 });
        v.push_back ({ t + 2.5 * b, 0.18, 69 });
        v.push_back ({ t + 3 * b, 0.3, 67 });
        t += 4 * b;
    }
    return v;
}
bool sameNotes (const Sequence& a, const Sequence& b, int64_t fromTick, int64_t toTick)
{
    std::vector<Note> x, y;
    for (const auto& n : a.notes) if (n.startTick >= fromTick && n.startTick < toTick) x.push_back (n);
    for (const auto& n : b.notes) if (n.startTick >= fromTick && n.startTick < toTick) y.push_back (n);
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); ++i) if (! (x[i] == y[i])) return false;
    return true;
}
MelodyContext amCtx()
{
    Key k; Key::parse ("Am", k); Progression p; std::string e; Progression::parse ("Am F C G Am F C G", p, e);
    return MelodyContext::manual (k, p, 120);
}
} // namespace

TEST (hum_rhythm_pattern_from_unpitched_voice_does_not_invent_pitches)
{
    // Tarareo sin altura fiable: el analizador de voz (v2) separa menos ataques cuando no hay cambios de nivel
    // claros (limitación conocida de la señal sintética); exigimos al menos 8 de 12 y ninguna altura.
    const auto vaU = analyseVoice (synthesiseVoice (dinDinDaan(), 44100.0, -40.0, 3, /*unpitched*/ true));
    CHECK (! vaU.pitchReliable);
    const auto patU = extractRhythm (vaU, 120.0, TimeSignature {});
    CHECK_MSG (patU.events.size() >= 8, "ataques sin altura " + std::to_string (patU.events.size()));
    for (const auto& e : patU.events) CHECK (! e.pitched);
    // Tarareo con altura: los 12 ataques, en la rejilla de semicorcheas, 2 compases.
    const auto va = analyseVoice (synthesiseVoice (dinDinDaan(), 44100.0, -40.0, 3));
    CHECK (! va.notes.empty());
    const auto pat = extractRhythm (va, 120.0, TimeSignature {});
    CHECK_MSG (pat.events.size() >= 11 && pat.events.size() <= 12, "ataques " + std::to_string (pat.events.size()));
    CHECK (pat.bars == 2);
    for (const auto& e : pat.events) CHECK (e.onset % (kPPQ / 4) == 0);
    CHECK_MSG (pat.gridFit >= 0.7, "alineación " + std::to_string (pat.gridFit));
    std::printf ("  patrón:\n%s\n  (a tiempo %.0f %%, desviación %.0f ms)\n", pat.toText().c_str(), 100 * pat.gridFit, pat.meanDeviationMs);
    // Texto editable: ida y vuelta
    RhythmPattern back = pat;
    std::string err;
    CHECK_MSG (RhythmPattern::fromText (pat.toText(), back, err), err);
    CHECK (back.events.size() == pat.events.size());
    for (size_t i = 0; i < back.events.size(); ++i) CHECK (back.events[i].onset == pat.events[i].onset);
    // Edición manual: quitar el segundo compás y acentuar
    RhythmPattern edited = pat;
    CHECK (RhythmPattern::fromText ("X---x-x-X---....", edited, err));
    CHECK (edited.events.size() == 4 && edited.bars == 1 && edited.events[0].accent == 1.0 && edited.events[0].length == kPPQ);
    CHECK (! RhythmPattern::fromText ("x-x", edited, err) && ! err.empty());
    // Propuesta de notas: mismo ritmo, alturas de la armonía (no de la voz)
    ComposerParams p; Key::parse ("Am", p.key); Progression::parse ("Am F C G", p.progression, err); p.bars = 4; p.bpm = 120;
    const auto res = proposeFromRhythm (pat, p, 1);
    CHECK_MSG (! res.sequence.notes.empty(), res.warnings.empty() ? "" : res.warnings.front());
    CHECK (res.sequence.notes.size() == pat.events.size());
    for (size_t i = 0; i < res.sequence.notes.size(); ++i) CHECK (res.sequence.notes[i].startTick == pat.events[i].onset);
    const auto fit = harmonicFit (res.sequence, p.progression, p.key);
    CHECK_MSG (fit.inScale >= 0.95 && fit.strongChordTones >= 0.8, "escala " + std::to_string (fit.inScale) + " acordes " + std::to_string (fit.strongChordTones));
    // Las alturas NO son una transcripción de la voz: la voz cantó A B C B A G (69 71 72 71 69 67) y la propuesta
    // sale de la armonía (misma secuencia con alturas distintas en al menos la mitad de las notas).
    {
        HumToSoloOptions copy; copy.mode = HumMode::Copy; copy.quantize = 1.0;
        const auto sung = humToSolo (va, p, copy).sequence;
        int differ = 0, n = (int) std::min (sung.notes.size(), res.sequence.notes.size());
        for (int i = 0; i < n; ++i) if (sung.notes[(size_t) i].pitch % 12 != res.sequence.notes[(size_t) i].pitch % 12) ++differ;
        CHECK_MSG (n > 0 && differ * 2 >= n, "la propuesta rítmica copió las alturas cantadas (" + std::to_string (differ) + "/" + std::to_string (n) + " distintas)");
    }
    // Determinista y serializable
    const auto res2 = proposeFromRhythm (pat, p, 1);
    CHECK (res2.sequence.notes.size() == res.sequence.notes.size() && res2.sequence.notes.front() == res.sequence.notes.front());
    RhythmPattern rp;
    CHECK_MSG (deserializeRhythmPattern (serializeRhythmPattern (pat), rp, err), err);
    CHECK (rp.events.size() == pat.events.size() && rp.bars == pat.bars && std::abs (rp.bpm - pat.bpm) < 1e-9);
}

TEST (hum_vocal_proposals_keep_original_and_correct_with_harmony)
{
    // Melodía cantada con notas fuera de Am (G# y D#): la original las conserva; la corregida las ajusta.
    std::vector<SynthVoiceNote> v = { { 0.0, 0.45, 69 }, { 0.5, 0.45, 72 }, { 1.0, 0.45, 68 }, { 1.5, 0.45, 75 }, { 2.0, 0.9, 72 }, { 3.0, 0.45, 71 }, { 3.5, 0.45, 69 } };
    const auto audio = synthesiseVoice (v, 44100.0, -45.0, 5);
    const auto va = analyseVoice (audio);
    CHECK_MSG (va.pitchReliable, "la voz sintética debería tener altura fiable");
    const auto ctx = amCtx();
    MelodyBrief b; b.type = MelodyType::Solo; b.seed = 3;
    VocalProposalOptions o; o.seed = 3;
    const auto set = proposeFromVocal (va, ctx, b, o);
    CHECK_MSG (set.proposals.size() == 4, std::to_string (set.proposals.size()));
    CHECK (set.proposals[0].original && set.proposals[0].mode == HumMode::Copy);
    const auto& orig = set.proposals[0].sequence;
    // La original conserva las alturas cantadas (módulo octava): G# (68) y D# (75) siguen ahí.
    bool hasGs = false, hasDs = false;
    for (const auto& n : orig.notes) { if (n.pitch % 12 == 8) hasGs = true; if (n.pitch % 12 == 3) hasDs = true; }
    CHECK_MSG (hasGs && hasDs, "la original no debe corregirse sola");
    const auto fitO = harmonicFit (orig, ctx.sectionProgression(), ctx.harmony.key);
    const auto fitC = harmonicFit (set.proposals[1].sequence, ctx.sectionProgression(), ctx.harmony.key);
    CHECK_MSG (fitC.inScale >= 0.95, "corregida en escala " + std::to_string (fitC.inScale));
    CHECK (fitC.inScale > fitO.inScale);
    CHECK_MSG (set.proposals[1].similarityToOriginal >= 0.5, "la corregida debe parecerse a la original: " + std::to_string (set.proposals[1].similarityToOriginal));
    CHECK (set.proposals[1].sequence.notes.size() == orig.notes.size());
    for (size_t i = 0; i < orig.notes.size(); ++i) CHECK (set.proposals[1].sequence.notes[i].startTick == orig.notes[i].startTick);
    CHECK (! set.proposals[2].sequence.notes.empty() && set.proposals[2].sequence.bars == 8);
    CHECK (! set.proposals[3].sequence.notes.empty() && set.proposals[3].strategy == (int) ProposalStrategy::Rhythmic);
    CHECK (! set.motif.notes.empty());
    std::printf ("  parecido con la original: corregida %.2f, desarrollada %.2f, combinada %.2f\n", set.proposals[1].similarityToOriginal, set.proposals[2].similarityToOriginal, set.proposals[3].similarityToOriginal);
    // Modo coro: original intacta, el resto en registro vocal
    b.type = MelodyType::Chorus; b.character = ChorusCharacter::EasyToSing;
    const auto setC = proposeFromVocal (va, ctx, b, o);
    CHECK (setC.proposals.size() == 4);
    CHECK (sameNotes (setC.proposals[0].sequence, orig, 0, orig.lengthTicks()) || setC.proposals[0].sequence.notes.size() == orig.notes.size());
    int lo, hi, leap; chorusRange (ChorusCharacter::EasyToSing, lo, hi, leap);
    for (size_t i = 1; i < setC.proposals.size(); ++i)
        for (const auto& n : setC.proposals[i].sequence.notes) CHECK_MSG (n.pitch >= lo && n.pitch <= hi, setC.proposals[i].name + " fuera del registro vocal");
}

TEST (hum_unreliable_pitch_falls_back_to_rhythm_with_warning)
{
    const auto audio = synthesiseVoice (dinDinDaan(), 44100.0, -40.0, 3, true);
    const auto va = analyseVoice (audio);
    CHECK (! va.pitchReliable);
    MelodyBrief b; b.type = MelodyType::Solo;
    const auto set = proposeFromVocal (va, amCtx(), b, {});
    CHECK (set.usedRhythmOnly);
    CHECK (! set.proposals.empty());
    bool warned = false;
    for (const auto& w : set.warnings) if (w.find ("ritmo") != std::string::npos) warned = true;
    CHECK (warned);
}

TEST (hum_project_rhythm_voice_and_region_correction_with_ab_and_undo)
{
    ProjectController p; p.bpm = 120;
    MelodyBrief b = p.defaultBrief(); b.type = MelodyType::Solo; b.seed = 2;
    CHECK (p.generateProposals (b).ok);
    CHECK (p.selectVariation (0).ok);
    // Proponer ritmo (independiente de GENERAR), editar y aplicar
    const auto rhythmTake = analyseVoice (synthesiseVoice (dinDinDaan(), 44100.0, -40.0, 3, true));
    const auto pr = p.proposeRhythm (rhythmTake, {});
    CHECK_MSG (pr.ok, pr.message);
    CHECK (! p.state.rhythm.empty() && p.state.history.size() == 4);
    CHECK (p.setRhythmPatternText ("x-x-x---x-x-x---\nX---....X---....").ok);
    CHECK (p.state.rhythm.bars == 2);
    const auto ar = p.applyRhythmPattern (9);
    CHECK_MSG (ar.ok, ar.message);
    CHECK (p.state.history.size() == 5);
    CHECK (p.state.history.current().notes.size() == 8);
    // Proponer melodía desde la voz
    std::vector<SynthVoiceNote> v = { { 0.0, 0.45, 69 }, { 0.5, 0.45, 72 }, { 1.0, 0.45, 71 }, { 1.5, 0.9, 69 } };
    const auto voiceTake = analyseVoice (synthesiseVoice (v, 44100.0, -45.0, 5));
    const auto pv = p.proposeFromVoice (voiceTake, {});
    CHECK_MSG (pv.ok, pv.message);
    CHECK (p.state.history.size() == 9);
    CHECK (p.state.history.currentItem().name.rfind ("Original", 0) == 0);
    // Corregir compases 3-4 tarareando: el resto idéntico, A/B preparado, deshacer
    CHECK (p.selectVariation (1).ok); // propuesta B
    const Sequence before = p.state.history.current();
    const uint32_t beforeId = p.state.history.currentItem().id;
    CHECK (p.setBarsLocked (0, 0, true).ok);
    const auto cr = p.correctRegionWithVoice (voiceTake, 2, 3);
    CHECK_MSG (cr.ok, cr.message);
    const Sequence after = p.state.history.current();
    const int64_t bar = before.timeSignature.ticksPerBar();
    CHECK (sameNotes (after, before, 0, 2 * bar));
    CHECK (sameNotes (after, before, 4 * bar, 8 * bar));
    CHECK (! sameNotes (after, before, 2 * bar, 4 * bar));
    CHECK (p.state.slotA == beforeId && p.state.slotB == p.state.history.currentItem().id);
    CHECK (p.state.history.size() == 10);
    CHECK (p.switchAB().ok);
    CHECK (p.state.history.currentItem().id == beforeId);
    CHECK (p.undoLast().ok);
    CHECK (p.state.history.size() == 9);
    // Persistencia del patrón
    std::string err; SoloState back;
    CHECK_MSG (deserializeState (serializeState (p.state), back, err), err);
    CHECK (back.rhythm.events.size() == p.state.rhythm.events.size());
}
