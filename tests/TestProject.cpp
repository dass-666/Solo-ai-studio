// Pruebas de Creative DNA, del controlador de proyecto (deshacer, A/B, favoritas) y de la dirección por voz (texto).
#include "TestFramework.h"
#include "solo/Commands.h"
#include "solo/CreativeDNA.h"
#include "solo/MidiFile.h"
#include "solo/Project.h"
#include "solo/SoloEngine.h"
#include <algorithm>

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

// ============================================================ flujo completo (lo que se probará a mano en FL)
// Para cada estilo (8) y cada instrumento (4) se recorre el flujo de la prueba manual con invariantes en cada paso.
namespace
{
bool sameSnapshot (const ProjectSnapshot& a, const ProjectSnapshot& b)
{
    return a.history == b.history && a.fx == b.fx && a.automation == b.automation && a.lockedBars == b.lockedBars;
}
std::vector<Note> notesInBars (const Sequence& s, int a, int b)
{
    std::vector<Note> out;
    const int64_t bt = s.timeSignature.ticksPerBar();
    for (const auto& n : s.notes)
        if (n.startTick >= a * bt && n.startTick < (b + 1) * bt) out.push_back (n);
    return out;
}
} // namespace

TEST (manual_flow_all_styles_and_instruments)
{
    int runs = 0, undoSteps = 0;
    for (int st = 0; st < kNumStyles; ++st)
        for (int in = 0; in < kNumInstruments; ++in)
        {
            ProjectController pc = project();
            pc.state.settings.style = (Style) st;
            pc.state.settings.instrument = (InstrumentId) in;
            const auto initial = snapshotOf (pc.state, "inicio");
            const uint64_t seed = 1000 + (uint64_t) (st * 10 + in);

            // 1. Generar dos variaciones
            CHECK (pc.generate (seed).ok);
            CHECK (pc.generate (seed + 1).ok);
            CHECK (pc.current() && ! pc.current()->notes.empty());

            // 2. Bloquear compases 1-2 y regenerar 1-4: los compases 1-2 no cambian
            CHECK (pc.setBarsLocked (0, 1, true).ok);
            const auto keep = notesInBars (*pc.current(), 0, 1);
            CHECK (pc.regenerateBars (0, 3, seed + 2).ok);
            const auto kept = notesInBars (*pc.current(), 0, 1);
            bool same = keep.size() == kept.size();
            for (size_t i = 0; same && i < keep.size(); ++i) same = keep[i].sameMusic (kept[i]);
            CHECK (same);

            // 3. Editar notas: borrar la última, cambiar velocity de la primera
            auto edited = *pc.current();
            if (edited.notes.size() > 2)
            {
                edited.notes.pop_back();
                edited.notes.front().velocity = 33;
                CHECK (pc.replaceSequence (edited).ok);
                CHECK (pc.current()->notes.size() == edited.notes.size());
                CHECK (pc.current()->notes.front().velocity == 33);
            }

            // 4. Transponer y articulaciones (el palm mute puede no existir en flauta/sinte: se acepta el rechazo)
            const auto before = pc.current()->notes.size();
            CHECK (pc.transpose (4, 5, 2).ok);
            CHECK (pc.current()->notes.size() == before);
            CHECK (pc.setArticulation (-1, -1, Articulation::Vibrato, true).ok);
            pc.setArticulation (-1, -1, Articulation::PalmMute, true);
            for (const auto& n : pc.current()->notes) { CHECK (n.pitch >= 0 && n.pitch <= 127); CHECK (n.lengthTicks > 0); }

            // 5. Efectos: todos los presets, luego uno fijo
            for (const auto& pr : fxPresets()) CHECK (pc.setFxPreset (pr.name).ok);
            CHECK (pc.setFxPreset ("Blues Lead").ok);
            CHECK (pc.adjustFx (AutoTarget::DelayMix, 0.2).ok);

            // 6. Orden en español
            RecognizedText t;
            t.text = "regenera los compases 3 a 4 y luego más reverb";
            const auto cmd = parseCommand (t);
            CHECK (cmd.ops.size() == 2);
            CHECK (executeCommand (pc, cmd, seed + 3).ok);

            // 7. Exportar MIDI y releerlo
            const auto& seq = *pc.current();
            MidiExportOptions mo;
            ParsedMidi pm;
            std::string err;
            CHECK (readMidiFile (writeMidiFile (seq, mo), pm, err));
            bool midiOk = pm.notes.size() == seq.notes.size() && pm.danglingNoteOns == 0;
            for (size_t i = 0; midiOk && i < seq.notes.size(); ++i)
                midiOk = pm.notes[i].pitch == seq.notes[i].pitch && pm.notes[i].startTick == seq.notes[i].startTick;
            CHECK (midiOk);

            // 8. Exportar WAV con efectos: todas las notas suenan, sin NaN ni clipping
            EngineRenderOptions ro;
            ro.instrument = (InstrumentId) in;
            ro.fx = pc.state.fx;
            ro.automation = pc.state.automation;
            const auto r = renderWithEngine (seq, ro);
            CHECK (r.noteOns == (int) seq.notes.size() && r.noteOffs == r.noteOns);
            bool finite = true;
            for (float x : r.left) finite &= std::isfinite (x);
            CHECK (finite && r.peak > 0.01f && r.peak <= 1.0f);

            // 9. Guardar y restaurar el proyecto
            SoloState back;
            CHECK (deserializeState (serializeState (pc.state), back, err));
            CHECK (serializeState (back) == serializeState (pc.state));

            // 10. Deshacer hasta el principio y rehacer hasta el final
            const auto finalSnap = snapshotOf (pc.state, "final");
            int n = 0;
            while (pc.undo.canUndo()) { CHECK (pc.undoLast().ok); ++n; }
            CHECK (sameSnapshot (snapshotOf (pc.state, ""), initial));
            for (int k = 0; k < n; ++k) CHECK (pc.redoLast().ok);
            CHECK (sameSnapshot (snapshotOf (pc.state, ""), finalSnap));
            undoSteps += n;
            ++runs;
        }
    std::printf ("  %d flujos completos (8 estilos x 4 instrumentos), %d pasos de deshacer/rehacer verificados\n", runs, undoSteps);
}
