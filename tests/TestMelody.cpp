// Pruebas V4-B/C/D: Melody Explorer (identidad, 4 propuestas, coro, libertad creativa, variaciones, bloqueos).
// Miden coherencia, parecido y diversidad; NO miden calidad artística (eso requiere evaluación humana).
#include "TestFramework.h"
#include "solo/ChorusComposer.h"
#include "solo/MelodyExplorer.h"
#include <cmath>
#include <cstdio>

using namespace solo;

namespace
{
MelodyContext manualCtx (const char* key = "Am", const char* prog = "Am F C G Am F C G", double bpm = 120)
{
    Key k; Key::parse (key, k);
    Progression p; std::string err; Progression::parse (prog, p, err);
    return MelodyContext::manual (k, p, bpm);
}
MelodyBrief brief (MelodyType t, double freedom = 35, uint64_t seed = 1)
{
    MelodyBrief b; b.type = t; b.freedom = freedom; b.seed = seed; return b;
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
} // namespace

TEST (melody_identity_is_valid_and_deterministic)
{
    const auto ctx = manualCtx();
    const auto id1 = MelodyExplorer::createIdentity (ctx, brief (MelodyType::Chorus));
    const auto id2 = MelodyExplorer::createIdentity (ctx, brief (MelodyType::Chorus));
    CHECK (id1.valid());
    CHECK (id1.motif.notes.size() >= 3 && id1.motif.notes.size() <= 8);
    CHECK (id1.motif.notes.size() == id2.motif.notes.size());
    for (size_t i = 0; i < id1.motif.notes.size(); ++i)
        CHECK (id1.motif.notes[i].interval == id2.motif.notes[i].interval && id1.motif.notes[i].onset == id2.motif.notes[i].onset);
    const auto id3 = MelodyExplorer::createIdentity (ctx, brief (MelodyType::Chorus, 35, 2));
    bool differs = id3.motif.notes.size() != id1.motif.notes.size();
    for (size_t i = 0; ! differs && i < id1.motif.notes.size(); ++i) differs = id3.motif.notes[i].interval != id1.motif.notes[i].interval || id3.motif.notes[i].onset != id1.motif.notes[i].onset;
    CHECK_MSG (differs, "otra semilla debería dar otra identidad");
    CHECK (! id1.description.empty());
}

TEST (melody_four_proposals_use_distinct_strategies_not_only_seeds)
{
    const auto ctx = manualCtx();
    const auto fam = MelodyExplorer::propose (ctx, brief (MelodyType::Solo));
    CHECK (fam.proposals.size() == 4);
    for (const auto& p : fam.proposals) CHECK_MSG (! p.sequence.notes.empty(), p.name);
    // Parámetros musicales distintos entre estrategias (no solo la semilla)
    const auto& A = fam.proposals[0].params; const auto& B = fam.proposals[1].params; const auto& C = fam.proposals[2].params; const auto& D = fam.proposals[3].params;
    CHECK (A.fidelity > C.fidelity && C.fidelity > D.fidelity);
    CHECK (A.complexity < D.complexity);
    CHECK (B.style == Style::Emotional);
    CHECK (C.density > A.density);
    CHECK (A.sectionPlan != B.sectionPlan || A.sectionPlan.empty());
    // Mismo motivo semilla de partida (familia) pero transformado por estrategia
    CHECK (A.seedMotifs.size() >= 1 && B.seedMotifs.size() >= 1 && C.seedMotifs.size() >= 1 && D.seedMotifs.size() >= 1);
    CHECK (A.motifSeed == D.motifSeed);
    CHECK_MSG (fam.diversity >= 0.3, "diversidad " + std::to_string (fam.diversity));
    // Rasgos esperables: B tiene notas más largas que C; D tiene saltos mayores que A
    auto meanLen = [] (const Sequence& s) { double t = 0; for (const auto& n : s.notes) t += (double) n.lengthTicks; return s.notes.empty() ? 0 : t / s.notes.size(); };
    auto maxLeap = [] (const Sequence& s) { int m = 0; for (size_t i = 1; i < s.notes.size(); ++i) m = std::max (m, std::abs (s.notes[i].pitch - s.notes[i - 1].pitch)); return m; };
    CHECK_MSG (meanLen (fam.proposals[1].sequence) > meanLen (fam.proposals[2].sequence), "B media " + std::to_string (meanLen (fam.proposals[1].sequence)) + " C " + std::to_string (meanLen (fam.proposals[2].sequence)));
    CHECK_MSG (maxLeap (fam.proposals[3].sequence) >= maxLeap (fam.proposals[0].sequence), "D salto " + std::to_string (maxLeap (fam.proposals[3].sequence)) + " A " + std::to_string (maxLeap (fam.proposals[0].sequence)));
    std::printf ("  diversidad familia solo: %.2f; parecido con identidad A %.2f B %.2f C %.2f D %.2f\n", fam.diversity,
                 fam.proposals[0].similarityToIdentity, fam.proposals[1].similarityToIdentity, fam.proposals[2].similarityToIdentity, fam.proposals[3].similarityToIdentity);
    CHECK (! fam.summary().empty());
}

TEST (melody_proposals_are_harmonically_coherent_for_all_types)
{
    const auto ctx = manualCtx ("C", "C G Am F C G Am F");
    for (int t = 0; t < kNumMelodyTypes; ++t)
    {
        const auto fam = MelodyExplorer::propose (ctx, brief ((MelodyType) t));
        CHECK_MSG (fam.proposals.size() == 4, melodyTypeName ((MelodyType) t));
        for (const auto& p : fam.proposals)
        {
            const auto fit = harmonicFit (p.sequence, ctx.sectionProgression(), ctx.harmony.key);
            CHECK_MSG (p.sequence.bars == 8, p.name);
            CHECK_MSG (fit.inScale >= 0.85, std::string (melodyTypeName ((MelodyType) t)) + " " + p.name + " escala " + std::to_string (fit.inScale));
            CHECK_MSG (fit.strongChordTones >= 0.6, std::string (melodyTypeName ((MelodyType) t)) + " " + p.name + " acordes en tiempo fuerte " + std::to_string (fit.strongChordTones));
        }
    }
}

TEST (melody_chorus_is_singable_repeatable_and_resolves)
{
    const auto ctx = manualCtx();
    for (int c = 0; c < 3; ++c)
    {
        auto b = brief (MelodyType::Chorus); b.character = (ChorusCharacter) c;
        const auto fam = MelodyExplorer::propose (ctx, b);
        int lo, hi, leap; chorusRange (b.character, lo, hi, leap);
        for (size_t i = 0; i < fam.proposals.size(); ++i)
        {
            const auto& p = fam.proposals[i];
            const auto rep = singability (p.sequence, ctx.sectionProgression(), ctx.harmony.key);
            const std::string tag = std::string (chorusCharacterName (b.character)) + " / " + p.name;
            CHECK_MSG (rep.lowest >= lo && rep.highest <= hi, tag + " registro " + std::to_string (rep.lowest) + "-" + std::to_string (rep.highest));
            CHECK_MSG (rep.maxLeap <= (i == 3 ? leap + 2 : leap), tag + " salto " + std::to_string (rep.maxLeap)); // la atrevida admite 2 semitonos más
            CHECK_MSG (rep.endsOnChordTone, tag + " no acaba en nota del acorde");
            if (c == 0) CHECK_MSG (rep.shortNoteRatio < 0.05, tag + " notas cortas " + std::to_string (rep.shortNoteRatio));
            CHECK_MSG (rep.breathGapsPerPhrase >= 0.5, tag + " respiraciones " + std::to_string (rep.breathGapsPerPhrase));
            if (i != 3) CHECK_MSG (rep.repeatedHalfSimilarity >= 0.5, tag + " repetición mitades " + std::to_string (rep.repeatedHalfSimilarity));
        }
    }
}

TEST (melody_freedom_bands_change_similarity_monotonically)
{
    const auto ctx = manualCtx();
    const auto fam = MelodyExplorer::propose (ctx, brief (MelodyType::Solo));
    const auto& fav = fam.proposals[0];
    double mean[4] = { 0, 0, 0, 0 };
    const double levels[4] = { 10, 40, 70, 95 };
    for (int band = 0; band < 4; ++band)
    {
        auto b = brief (MelodyType::Solo, levels[band]);
        double s = 0; int n = 0;
        for (uint64_t seed = 1; seed <= 6; ++seed)
        {
            const auto vars = MelodyExplorer::vary (ctx, b, fam.identity, fav, 2, seed, {});
            CHECK (vars.size() == 2);
            for (const auto& v : vars)
            {
                CHECK (! v.sequence.notes.empty());
                s += melodySimilarity (v.sequence, fav.sequence).overall; ++n;
                const auto fit = harmonicFit (v.sequence, ctx.sectionProgression(), ctx.harmony.key);
                CHECK_MSG (fit.inScale >= 0.8, "libertad " + std::to_string (levels[band]) + " escala " + std::to_string (fit.inScale));
            }
        }
        mean[band] = s / n;
        std::printf ("  libertad %3.0f%%: parecido medio con la favorita %.2f (%s)\n", levels[band], mean[band], freedomBandName (MelodyExplorer::freedomBand (levels[band])));
    }
    CHECK_MSG (mean[0] > mean[1] && mean[1] > mean[2] && mean[2] >= mean[3] - 0.05, "no monótono");
    CHECK_MSG (mean[0] >= 0.7, "las variaciones cercanas deben parecerse mucho: " + std::to_string (mean[0]));
    CHECK_MSG (mean[3] <= 0.5, "las experimentales deben alejarse: " + std::to_string (mean[3]));
}

TEST (melody_variations_respect_locked_bars_and_notes)
{
    const auto ctx = manualCtx();
    const auto fam = MelodyExplorer::propose (ctx, brief (MelodyType::Chorus));
    const auto& fav = fam.proposals[1];
    const int64_t bar = fav.sequence.timeSignature.ticksPerBar();
    std::vector<bool> locks (8, false); locks[0] = locks[1] = locks[4] = true;
    for (double f : { 10.0, 40.0, 70.0, 95.0 })
    {
        auto b = brief (MelodyType::Chorus, f);
        const auto vars = MelodyExplorer::vary (ctx, b, fam.identity, fav, 3, 5, locks);
        for (const auto& v : vars)
        {
            CHECK_MSG (sameNotes (v.sequence, fav.sequence, 0, 2 * bar), "libertad " + std::to_string (f) + ": compases 1-2 bloqueados cambiaron");
            CHECK_MSG (sameNotes (v.sequence, fav.sequence, 4 * bar, 5 * bar), "libertad " + std::to_string (f) + ": compás 5 bloqueado cambió");
            CHECK (v.sequence.bars == 8);
        }
    }
    // Regenerar solo una región (compases 6-7) conserva el resto
    const auto region = MelodyExplorer::regenerateRegion (ctx, fav, 5, 6, 99, {});
    CHECK (sameNotes (region, fav.sequence, 0, 5 * bar));
    CHECK (sameNotes (region, fav.sequence, 7 * bar, 8 * bar));
}

TEST (melody_beat_context_conditions_plan_and_rhythm)
{
    // Beat sintético con intro suave (4 compases sin batería) y después batería: el plan debe reflejar la energía.
    auto quiet = SyntheticBeatSpec {}; quiet.bars = 4; quiet.drums = false; quiet.bpm = 110;
    auto loud = SyntheticBeatSpec {}; loud.bars = 4; loud.bpm = 110;
    auto a = synthesiseBeat (quiet);
    const auto b = synthesiseBeat (loud);
    a.samples.resize ((size_t) (4 * 4 * 60.0 / 110.0 * a.sampleRate));
    for (auto& v : a.samples) v *= 0.3f;
    a.samples.insert (a.samples.end(), b.samples.begin(), b.samples.end());
    const auto analysis = analyseBeat (a);
    CHECK_MSG (analysis.ok && analysis.bars >= 7 && ! analysis.harmony.empty(), analysis.summary());
    MelodyContext ctx;
    ctx.harmony = analysis.harmony; ctx.beat = &analysis; ctx.bpm = analysis.bpm; ctx.fromBar = 0; ctx.toBar = 7;
    const std::string plan = MelodyExplorer::sectionPlanFor (ctx, brief (MelodyType::Solo), ProposalStrategy::Simple);
    std::printf ("  plan derivado de la energía del beat: '%s' (regiones %zu)\n", plan.c_str(), analysis.energyRegions.size());
    CHECK_MSG (plan.rfind ("P", 0) == 0, "la sección empieza con presentación (energía baja): " + plan);
    CHECK_MSG (plan.find ('C') != std::string::npos || plan.find ('T') != std::string::npos, "la parte alta debería ser clímax/tensión: " + plan);
    const auto fam = MelodyExplorer::propose (ctx, brief (MelodyType::Solo));
    CHECK (fam.proposals.size() == 4);
    for (const auto& p : fam.proposals) CHECK_MSG (p.sequence.bars == 8 && ! p.sequence.notes.empty(), p.name);
    CHECK (std::abs (fam.proposals[0].params.bpm - analysis.bpm) < 1e-9);
    CHECK (fam.proposals[0].params.key == analysis.key);
    // Sin beat (acordes manuales) el mismo compositor sigue funcionando.
    const auto fam2 = MelodyExplorer::propose (manualCtx(), brief (MelodyType::Solo));
    CHECK (fam2.proposals.size() == 4);
}

TEST (melody_proposals_are_deterministic_and_section_subrange_works)
{
    auto ctx = manualCtx ("Em", "Em C G D Em C G D Em C G D Em C G D");
    ctx.fromBar = 4; ctx.toBar = 11;
    CHECK (ctx.bars() == 8);
    CHECK (ctx.sectionProgression().bars.size() == 8);
    const auto f1 = MelodyExplorer::propose (ctx, brief (MelodyType::Interlude, 35, 9));
    const auto f2 = MelodyExplorer::propose (ctx, brief (MelodyType::Interlude, 35, 9));
    CHECK (f1.proposals.size() == 4 && f2.proposals.size() == 4);
    for (size_t i = 0; i < 4; ++i)
    {
        CHECK (f1.proposals[i].sequence.notes.size() == f2.proposals[i].sequence.notes.size());
        CHECK (f1.proposals[i].sequence.bars == 8);
        for (size_t k = 0; k < f1.proposals[i].sequence.notes.size(); ++k) CHECK (f1.proposals[i].sequence.notes[k] == f2.proposals[i].sequence.notes[k]);
    }
    CHECK (std::abs (f1.diversity - f2.diversity) < 1e-12);
}

TEST (melody_metrics_sanity)
{
    const auto ctx = manualCtx();
    const auto fam = MelodyExplorer::propose (ctx, brief (MelodyType::Solo));
    const auto& a = fam.proposals[0].sequence;
    const auto self = melodySimilarity (a, a);
    CHECK (self.overall > 0.99 && self.rhythm > 0.99 && self.contour > 0.99);
    Sequence empty = a; empty.notes.clear();
    CHECK (melodySimilarity (a, empty).overall == 0);
    CHECK (familyDiversity ({ a, a, a }) < 1e-9);
    Sequence shifted = a; for (auto& n : shifted.notes) n.pitch += 12;
    const auto m = melodySimilarity (a, shifted);
    CHECK (m.interval > 0.99 && m.rhythm > 0.99 && m.contour < 0.5); // el histograma de alturas solapa en octavas comunes
}

// ---------------------------------------------------------------- integración con el proyecto
#include "solo/Project.h"
#include "solo/StateManager.h"

TEST (melody_project_proposals_favorite_vary_region_undo)
{
    ProjectController p; p.bpm = 120;
    MelodyBrief b = p.defaultBrief();
    b.type = MelodyType::Chorus; b.freedom = 35; b.seed = 4;
    CHECK (! p.varyCurrent (2, 1).ok); // sin propuestas
    const auto r = p.generateProposals (b);
    CHECK_MSG (r.ok, r.message);
    CHECK (p.state.history.size() == 4);
    CHECK (p.state.history.currentIndex() == 0);
    CHECK (p.state.melody.active && p.state.melody.identity.valid());
    CHECK (p.state.history.all()[0].name.rfind ("A", 0) == 0 && p.state.history.all()[3].name.rfind ("D", 0) == 0);
    for (const auto& it : p.state.history.all()) CHECK (p.state.melody.strategyOf (it.id) >= 0);
    // Favorita = B
    CHECK (p.selectVariation (1).ok && p.setFavorite (1, true).ok);
    const Sequence favSeq = p.state.history.current();
    CHECK (p.setBarsLocked (0, 1, true).ok);
    // Variar esta idea (libertad 35 -> conserva identidad)
    const auto v = p.varyCurrent (3, 7);
    CHECK_MSG (v.ok, v.message);
    CHECK (p.state.history.size() == 7);
    const int64_t bar = favSeq.timeSignature.ticksPerBar();
    for (int i = 4; i < 7; ++i)
    {
        const auto& s = p.state.history.all()[(size_t) i].sequence;
        CHECK (sameNotes (s, favSeq, 0, 2 * bar));
        CHECK (p.state.melody.strategyOf (p.state.history.all()[(size_t) i].id) == 1);
        CHECK_MSG (melodySimilarity (s, favSeq).overall >= 0.25, std::to_string (melodySimilarity (s, favSeq).overall));
    }
    // Regenerar región 5-6 de la variación actual sin destruir el resto (y respetando bloqueos 1-2)
    const Sequence before = p.state.history.current();
    const auto rr = p.regenerateMelodyRegion (4, 5, 11);
    CHECK_MSG (rr.ok, rr.message);
    const Sequence after = p.state.history.current();
    CHECK (sameNotes (after, before, 0, 4 * bar));
    CHECK (sameNotes (after, before, 6 * bar, 8 * bar));
    // Deshacer devuelve la región; deshacer x4 quita las variaciones
    CHECK (p.undoLast().ok);
    CHECK (sameNotes (p.state.history.current(), before, 0, 8 * bar));
    CHECK (p.undoLast().ok); // variar
    CHECK (p.state.history.size() == 4);
    CHECK (p.setMelodyFreedom (90).ok);
    CHECK (MelodyExplorer::freedomBand (p.state.melody.brief.freedom) == FreedomBand::Experimental);
}

TEST (melody_project_uses_beat_harmony_and_persists)
{
    ProjectController p; p.bpm = 120;
    SyntheticBeatSpec s; s.bpm = 100; Key::parse ("Em", s.key); s.progression = "Em C G D"; s.bars = 8;
    CHECK (p.importBeat (synthesiseBeat (s), "em.wav").ok);
    CHECK (p.confirmBeatBpm (100).ok);
    MelodyContext ctx; std::string err;
    CHECK_MSG (p.melodyContext (ctx, err), err);
    CHECK (ctx.beat != nullptr && ctx.bars() >= 7);
    Key em; Key::parse ("Em", em);
    CHECK (ctx.harmony.key == em);
    MelodyBrief b = p.defaultBrief(); b.type = MelodyType::Solo; b.seed = 2;
    CHECK (p.generateProposals (b).ok);
    CHECK (std::abs (p.state.history.current().bpm - 100.0) < 1e-9);
    CHECK (p.state.history.current().bars == ctx.bars());
    // Persistencia: brief, identidad y mapa de estrategias sobreviven a guardar/cargar
    const std::string text = serializeState (p.state);
    CHECK (text.find ("mel.motif=") != std::string::npos && text.find ("mel.var=") != std::string::npos);
    SoloState back;
    CHECK_MSG (deserializeState (text, back, err), err);
    CHECK (back.melody.active);
    CHECK (back.melody.brief.type == MelodyType::Solo && back.melody.brief.seed == 2);
    CHECK (back.melody.identity.motif.notes.size() == p.state.melody.identity.motif.notes.size());
    CHECK (back.melody.identity.motifSeed == p.state.melody.identity.motifSeed);
    CHECK (back.melody.variationStrategy == p.state.melody.variationStrategy);
    // Un proyecto cargado puede seguir variando (recupera los parámetros de familia de forma determinista)
    ProjectController q; q.bpm = 120; q.state = back;
    CHECK (q.selectVariation (2).ok);
    const auto v = q.varyCurrent (2, 3);
    CHECK_MSG (v.ok, v.message);
    CHECK (q.state.history.size() == 6);
    // Estado sin explorador (v3) carga sin melodía activa
    ProjectController old; old.bpm = 120; CHECK (old.generate (1).ok);
    SoloState b2; CHECK (deserializeState (serializeState (old.state), b2, err));
    CHECK (! b2.melody.active);
}

TEST (melody_project_manual_chords_when_no_beat_and_percussive_beat_fallback)
{
    ProjectController p; p.bpm = 95;
    p.state.settings.keyText = "D"; p.state.settings.progressionText = "D A Bm G"; p.state.settings.bars = 4;
    MelodyContext ctx; std::string err;
    CHECK_MSG (p.melodyContext (ctx, err), err);
    CHECK (ctx.beat == nullptr && ctx.bars() == 4 && std::abs (ctx.bpm - 95.0) < 1e-9);
    MelodyBrief b = p.defaultBrief(); b.type = MelodyType::Intro;
    CHECK (p.generateProposals (b).ok);
    CHECK (p.state.history.current().bars == 4);
    // Beat percusivo sin acordes: el explorador cae a los acordes manuales de los ajustes
    SyntheticBeatSpec s; s.harmony = false; s.bars = 8; s.bpm = 95;
    CHECK (p.importBeat (synthesiseBeat (s), "drums.wav").ok);
    CHECK (p.state.beat.harmony.segments.empty());
    CHECK_MSG (p.melodyContext (ctx, err), err);
    CHECK (ctx.beat == nullptr && ctx.bars() == 4);
    // Y si el usuario escribe los acordes del beat, pasa a usar el beat
    Progression prog; Progression::parse ("D A Bm G", prog, err);
    CHECK (p.setBeatChords (prog).ok);
    CHECK_MSG (p.melodyContext (ctx, err), err);
    CHECK (ctx.beat != nullptr && ctx.bars() >= 7);
}
