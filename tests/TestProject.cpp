// Pruebas de Creative DNA, del controlador de proyecto (deshacer, A/B, favoritas) y de la dirección por voz (texto).
#include "TestFramework.h"
#include "solo/Commands.h"
#include "solo/CreativeDNA.h"
#include "solo/Project.h"

#include <cmath>

using namespace solo;

namespace
{
ComposerParams dp (uint64_t seed = 1)
{
    ComposerParams p;
    Key::parse ("Am", p.key);
    std::string e;
    Progression::parse ("Am F C G", p.progression, e);
    p.bars = 8;
    p.bpm = 110;
    p.seed = seed;
    p.style = Style::Emotional;
    return p;
}

ProjectController project()
{
    ProjectController pc;
    pc.bpm = 110;
    pc.state.settings.keyText = "Am";
    pc.state.settings.progressionText = "Am F C G";
    pc.state.settings.bars = 8;
    pc.state.settings.style = Style::MelodicRock;
    return pc;
}
} // namespace

// ============================================================ Creative DNA
TEST (dna_extraction_and_summary)
{
    const auto s = SoloComposer::compose (dp());
    const auto dna = extractDNA (s);
    CHECK (dna.valid);
    CHECK (! dna.motifs.empty() && dna.motifs[0].notes.size() >= 3);
    double h = 0;
    for (double x : dna.intervalHist) h += x;
    CHECK (std::abs (h - 1.0) < 1e-9);
    CHECK (dna.lowPitch <= dna.highPitch && dna.phraseBeats > 0);
    std::printf ("  ADN: %s\n", dna.summary().c_str());
    // Dirección: una escala ascendente tiene pendiente positiva
    Sequence up;
    up.bars = 2;
    for (int i = 0; i < 8; ++i) { Note n; n.startTick = i * 960; n.lengthTicks = 900; n.pitch = 60 + 2 * i; up.notes.push_back (n); }
    CHECK (extractDNA (up).direction > 1.0);
}

TEST (dna_derived_modes)
{
    const auto src = SoloComposer::compose (dp (11));
    const auto dna = extractDNA (src);
    double simImit = 0, simFree = 0;
    int answerOpp = 0;
    for (uint64_t seed = 1; seed <= 8; ++seed)
    {
        auto p = dp (100 + seed);
        DerivedOptions o;
        o.mode = DerivedMode::Imitate;
        const auto imit = generateFromDNA (dna, p, o);
        o.mode = DerivedMode::Free;
        const auto fr = generateFromDNA (dna, p, o);
        simImit += melodicSimilarity (src, imit);
        simFree += melodicSimilarity (src, fr);
        o.mode = DerivedMode::Answer;
        const auto ans = generateFromDNA (dna, p, o);
        CHECK (! ans.notes.empty());
        // Respuesta: el primer intervalo del motivo va en sentido contrario al original
        if (dna.motifs[0].notes.size() > 1 && ans.notes.size() > 2)
        {
            const int orig = dna.motifs[0].notes[1].interval;
            int first = 0;
            for (size_t i = 1; i < ans.notes.size() && first == 0; ++i) first = ans.notes[i].pitch - ans.notes[i - 1].targetPitch();
            if (orig == 0 || first == 0 || (orig > 0) != (first > 0)) ++answerOpp;
        }
        o.mode = DerivedMode::Continue;
        const auto cont = generateFromDNA (dna, p, o);
        CHECK (! cont.notes.empty());
        o.mode = DerivedMode::Transform;
        o.development = 0.9;
        const auto tr = generateFromDNA (dna, p, o);
        CHECK (! tr.notes.empty());
        for (const auto* s : { &imit, &fr, &ans, &cont, &tr })
            for (size_t i = 1; i < s->notes.size(); ++i) CHECK (s->notes[i].startTick >= s->notes[i - 1].endTick());
    }
    std::printf ("  parecido con el original: imitar %.2f, libre %.2f; respuestas en sentido contrario %d/8\n", simImit / 8, simFree / 8, answerOpp);
    CHECK (simImit > simFree);
    CHECK (answerOpp >= 6);
}

// ============================================================ proyecto
TEST (project_generate_lock_regenerate_undo)
{
    auto pc = project();
    CHECK (pc.generate (5).ok);
    CHECK (pc.generate (6).ok);
    CHECK (pc.state.history.size() == 2);
    CHECK (pc.setBarsLocked (2, 3, true).ok);
    const auto before = *pc.current();
    const auto r = pc.regenerateBars (0, 7, 99);
    CHECK (r.ok && r.sequenceChanged);
    const int64_t barT = 3840;
    auto bars = [&] (const Sequence& s, int a, int b) {
        std::vector<Note> v;
        for (const auto& n : s.notes) if (n.startTick / barT >= a && n.startTick / barT <= b) v.push_back (n);
        return v;
    };
    CHECK (bars (*pc.current(), 2, 3) == bars (before, 2, 3));
    CHECK (pc.state.history.size() == 3);
    // Deshacer x2 => vuelve a 2 variaciones y sin bloqueos
    CHECK (pc.undoLast().ok);
    CHECK (pc.state.history.size() == 2);
    CHECK (pc.undoLast().ok);
    CHECK (pc.state.lockedBars.empty() || ! pc.state.lockedBars[2]);
    CHECK (pc.redoLast().ok);
    CHECK (pc.state.lockedBars.size() == 8 && pc.state.lockedBars[2]);
}

TEST (project_ab_favorites_transpose_articulations)
{
    auto pc = project();
    pc.generate (1);
    pc.generate (2);
    pc.generate (3);
    CHECK (pc.assignSlot ('A', 0).ok && pc.assignSlot ('B', 2).ok);
    pc.selectVariation (0);
    CHECK (pc.switchAB().ok && pc.state.history.currentIndex() == 2);
    CHECK (pc.switchAB().ok && pc.state.history.currentIndex() == 0);
    CHECK (pc.setFavorite (1, true).ok && pc.state.history.all()[1].favorite);
    // Borrar es explícito y deshacible
    CHECK (pc.deleteVariation (1).ok && pc.state.history.size() == 2);
    CHECK (pc.undoLast().ok && pc.state.history.size() == 3 && pc.state.history.all()[1].favorite);
    // Transportar una octava solo el compás 1, respetando el registro
    const auto before = *pc.current();
    const auto r = pc.transpose (0, 0, 12);
    if (r.ok)
    {
        int moved = 0;
        for (size_t i = 0; i < before.notes.size(); ++i)
            if (before.notes[i].startTick < 3840 && pc.current()->notes[i].pitch == before.notes[i].pitch + 12) ++moved;
        CHECK (moved > 0);
        for (size_t i = 0; i < before.notes.size(); ++i)
            if (before.notes[i].startTick >= 3840) CHECK (pc.current()->notes[i].pitch == before.notes[i].pitch);
    }
    // Articulaciones: añadir vibrato en todo y quitar bends conservando la altura final
    CHECK (pc.setArticulation (0, 7, Articulation::Vibrato, true).ok);
    std::vector<int> targets;
    for (const auto& n : pc.current()->notes) targets.push_back (n.targetPitch());
    pc.setArticulation (0, 7, Articulation::Bend, false);
    for (size_t i = 0; i < targets.size(); ++i)
    {
        CHECK (pc.current()->notes[i].expr.bendType == BendType::None);
        CHECK (pc.current()->notes[i].targetPitch() == targets[i]);
    }
    pc.state.settings.instrument = InstrumentId::Flute;
    CHECK (! pc.setArticulation (0, 7, Articulation::PalmMute, true).ok);
}

TEST (project_fx_and_dna_ops)
{
    auto pc = project();
    pc.generate (4);
    CHECK (pc.setFxPreset ("Blues Lead").ok && pc.state.fx == fxPresets()[(size_t) fxPresetIndex ("Blues Lead")].settings);
    const float delayBefore = pc.state.fx.delayMix;
    CHECK (pc.adjustFx (AutoTarget::DelayMix, 0.2).ok);
    CHECK (pc.state.fx.isOn (FxStage::Delay) && pc.state.fx.delayMix > delayBefore);
    CHECK (pc.state.settings.fxPreset == "Personalizado");
    CHECK (pc.undoLast().ok && pc.state.settings.fxPreset == "Personalizado"); // el nombre es ajuste, el sonido vuelve
    CHECK (pc.state.fx == fxPresets()[(size_t) fxPresetIndex ("Blues Lead")].settings);
    CHECK (! pc.setFxPreset ("No existe").ok);
    CHECK (pc.extractDNAFromCurrent().ok && ! pc.state.seedMotifs.empty() && ! pc.state.dnaSummary.empty());
    DerivedOptions o;
    o.mode = DerivedMode::Continue;
    CHECK (pc.generateFromDNA (o, 77).ok);
    CHECK (pc.state.history.size() == 2);
}

// ============================================================ dirección por voz (texto)
TEST (commands_parse_spanish)
{
    struct Case { const char* text; OpKind kind; int from, to; };
    const Case cases[] = {
        { "Genera otro solo", OpKind::Generate, -1, -1 },
        { "regenera los compases 3 a 5", OpKind::RegenerateBars, 2, 4 },
        { "Cambia el compás cuatro", OpKind::RegenerateBars, 3, 3 },
        { "bloquea los compases 1 y 2", OpKind::LockBars, 0, 1 },
        { "desbloquea el compás 2", OpKind::UnlockBars, 1, 1 },
        { "más intenso", OpKind::Adjust, -1, -1 },
        { "menos notas", OpKind::Adjust, -1, -1 },
        { "estilo blues", OpKind::SetStyle, -1, -1 },
        { "cambia a flauta", OpKind::SetInstrument, -1, -1 },
        { "más delay", OpKind::FxAdjust, -1, -1 },
        { "menos reverb", OpKind::FxAdjust, -1, -1 },
        { "sonido limpio", OpKind::FxPreset, -1, -1 },
        { "sin efectos", OpKind::FxOff, -1, -1 },
        { "sube una octava el compás 2", OpKind::Transpose, 1, 1 },
        { "añade vibrato en el último compás", OpKind::AddArticulation, -2, -2 },
        { "quita los bends", OpKind::RemoveArticulation, -1, -1 },
        { "deshaz", OpKind::Undo, -1, -1 },
        { "rehacer", OpKind::Redo, -1, -1 },
        { "Escúchalo", OpKind::Play, -1, -1 },
        { "para", OpKind::Stop, -1, -1 },
        { "guárdalo como favorito", OpKind::Favorite, -1, -1 },
    };
    int ok = 0;
    for (const auto& c : cases)
    {
        const auto pc = parseCommand ({ c.text });
        const bool good = pc.ops.size() == 1 && pc.ops[0].kind == c.kind && pc.ops[0].fromBar == c.from && pc.ops[0].toBar == c.to;
        CHECK_MSG (good, std::string (c.text) + " -> " + pc.summary());
        ok += good;
    }
    std::printf ("  %d/%zu órdenes interpretadas correctamente\n", ok, sizeof (cases) / sizeof (cases[0]));
    const auto st = parseCommand ({ "estilo blues" });
    CHECK (st.ops[0].style == Style::ExpressiveBlues);
    const auto tr = parseCommand ({ "baja 3 semitonos" });
    CHECK (tr.ops.size() == 1 && tr.ops[0].amount == -3);
    // Varias órdenes en una frase, y fragmentos no entendidos
    const auto multi = parseCommand ({ "bloquea el compás 1, regenera los compases 2 a 4 y luego más reverb" });
    CHECK (multi.ops.size() == 3 && multi.unrecognised.empty());
    const auto partial = parseCommand ({ "haz que suene como una nave espacial y más delay" });
    CHECK (partial.ops.size() == 1 && partial.unrecognised.size() == 1 && partial.confidence < 1.0 && partial.needsConfirmation);
    CHECK (! parseCommand ({ "para" }).needsConfirmation);
    CHECK (parseCommand ({ "genera otro solo" }).needsConfirmation);
    CHECK (normaliseSpanish ("Añade VIBRATO en el compás dos") == "anade vibrato en el compas 2");
}

TEST (commands_preview_then_execute_with_undo)
{
    auto pc = project();
    pc.generate (9);
    const auto cmd = parseCommand ({ "bloquea el compás 1 y regenera los compases 1 a 4" });
    CHECK (cmd.ops.size() == 2);
    const auto before = pc.state.history;
    const auto pv = previewCommand (pc, cmd, 500);
    CHECK (pv.ok && pv.steps.size() == 2 && pv.variationsAdded == 1);
    CHECK (pc.state.history == before); // la vista previa no cambia nada
    const auto res = executeCommand (pc, cmd, 500);
    CHECK (res.ok && res.sequenceChanged);
    CHECK (pc.state.history.size() == 2);
    // Compás 1 intacto
    std::vector<Note> a, b;
    for (const auto& n : before.current().notes) if (n.startTick < 3840) a.push_back (n);
    for (const auto& n : pc.current()->notes) if (n.startTick < 3840) b.push_back (n);
    CHECK (a == b);
    // Deshacer por voz
    executeCommand (pc, parseCommand ({ "deshaz" }), 0);
    executeCommand (pc, parseCommand ({ "deshaz" }), 0);
    CHECK (pc.state.history == before);
}
