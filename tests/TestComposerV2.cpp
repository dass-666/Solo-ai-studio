// Pruebas del compositor jerárquico (motor 2) y del intérprete.
#include "TestFramework.h"
#include "solo/Analysis.h"
#include "solo/Performer.h"
#include "solo/SoloComposer.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

using namespace solo;

namespace
{
ComposerParams v2Params (const char* key = "Am", const char* prog = "Am F C G", double bpm = 120.0)
{
    ComposerParams p;
    CHECK (Key::parse (key, p.key));
    std::string err;
    CHECK (Progression::parse (prog, p.progression, err));
    p.bars = 8;
    p.bpm = bpm;
    p.engine = 2;
    return p;
}

struct Sc { const char* key; const char* prog; };
const Sc kProgs[] = { { "Am", "Am F C G" }, { "C", "C G Am F" }, { "A", "A7 D7 A7 E7" }, { "Fm", "Fm Db Ab Eb" },
                      { "Dm", "Dm Gm A7 Dm" }, { "G", "G Em C D" }, { "E", "Emaj7 C#m7 F#m7 B7" } };
} // namespace

TEST (v2_deterministic_and_distinct)
{
    for (int st = 0; st < kNumStyles; ++st)
    {
        auto p = v2Params();
        p.style = (Style) st;
        p.seed = 42;
        const auto a = SoloComposer::compose (p), b = SoloComposer::compose (p);
        CHECK_MSG (a == b, styleName (p.style));
        p.seed = 43;
        const auto c = SoloComposer::compose (p);
        CHECK_MSG (! (a == c), styleName (p.style));
    }
}

TEST (v2_musical_invariants_all_styles)
{
    int strong = 0, strongTone = 0, total = 0, resolved = 0, solos = 0;
    int i = 0;
    for (const auto& pr : kProgs)
        for (int ins = 0; ins < kNumInstruments; ++ins)
            for (int st = 0; st < kNumStyles; ++st)
                for (uint64_t seed = 1; seed <= 3; ++seed, ++i)
                {
                    auto p = v2Params (pr.key, pr.prog, 66.0 + (i * 17) % 110);
                    p.timeSignature = { (i % 3 == 0) ? 3 : 4, 4 };
                    p.bars = (int[]) { 2, 4, 8, 16 }[i % 4];
                    p.instrument = (InstrumentId) ins;
                    p.style = (Style) st;
                    p.energy = (i % 5) / 4.0;
                    p.density = ((i + 2) % 5) / 4.0;
                    p.complexity = ((i + 4) % 5) / 4.0;
                    p.seed = seed;
                    const auto s = SoloComposer::compose (p);
                    const auto& prof = profileFor (p.instrument);
                    const std::string tag = std::string (pr.prog) + " | " + instrumentName (p.instrument) + " | "
                                            + styleName (p.style) + " | " + std::to_string (p.bars) + "c "
                                            + std::to_string (p.timeSignature.numerator) + "/4 seed " + std::to_string (seed);
                    CHECK_MSG (s.lengthTicks() == (int64_t) p.bars * p.timeSignature.ticksPerBar(), tag);
                    CHECK_MSG ((int) s.notes.size() >= std::max (1, p.bars / 2), tag + " notas=" + std::to_string (s.notes.size()));
                    if (s.notes.empty()) continue;
                    ++solos;
                    int64_t longestGap = 0;
                    for (size_t k = 0; k < s.notes.size(); ++k)
                    {
                        const auto& n = s.notes[k];
                        CHECK_MSG (n.startTick >= 0 && n.endTick() <= s.lengthTicks() && n.lengthTicks > 0, tag);
                        CHECK_MSG (n.pitch >= prof.range.lowest && n.targetPitch() <= prof.range.highest, tag + " rango " + std::to_string (n.pitch));
                        CHECK_MSG (n.velocity >= 1 && n.velocity <= 127, tag);
                        if (p.instrument == InstrumentId::Flute)
                            CHECK_MSG (n.expr.bendType == BendType::None && ! n.expr.palmMute && n.expr.slideFrom == 0, tag + " flauta sin bends/pm");
                        if (k > 0)
                        {
                            const auto& prev = s.notes[k - 1];
                            CHECK_MSG (n.startTick >= prev.endTick(), tag + " monofónico");
                            CHECK_MSG (std::abs (n.pitch - prev.targetPitch()) <= prof.maxLeapSemitones + 2, tag + " salto");
                            longestGap = std::max (longestGap, n.startTick - prev.endTick());
                        }
                    }
                    if (p.bars >= 4) CHECK_MSG (longestGap >= kPPQ / 2, tag + " respiración");
                    const auto m = analyseSolo (s, p);
                    strong += (int) std::lround (m.strongChordTone * 1000);
                    strongTone += 1000;
                    resolved += m.resolvesToStable ? 1 : 0;
                    CHECK_MSG (m.resolvesToStable, tag + " resolución");
                    CHECK_MSG (m.notesPerSecond <= prof.maxNotesPerSecond * 1.05, tag + " notas/s " + std::to_string (m.notesPerSecond));
                    total += m.notes;
                }
    const double strongPct = 100.0 * strong / std::max (1, strongTone);
    std::printf ("  %d solos, %d notas, notas del acorde en tiempo fuerte (media) %.1f%%, resuelven %d/%d\n", solos, total,
                 strongPct, resolved, solos);
    CHECK (strongPct >= 90.0);
}

TEST (v2_styles_are_objectively_different)
{
    // Para cada estilo: media de métricas sobre varias semillas y progresiones.
    std::map<int, SoloMetrics> avg;
    for (int st = 0; st < kNumStyles; ++st)
    {
        SoloMetrics acc;
        int n = 0;
        for (const auto& pr : kProgs)
            for (uint64_t seed = 1; seed <= 4; ++seed)
            {
                auto p = v2Params (pr.key, pr.prog, 100);
                p.style = (Style) st;
                p.seed = seed;
                p.energy = 0.6; p.density = 0.5; p.complexity = 0.5;
                const auto m = analyseSolo (SoloComposer::compose (p), p);
                acc.notesPerSecond += m.notesPerSecond;
                acc.restRatio += m.restRatio;
                acc.bends += m.bends;
                acc.vibratos += m.vibratos;
                acc.legatos += m.legatos;
                acc.palmMutes += m.palmMutes;
                acc.meanPitch += m.meanPitch;
                acc.motifRecurrence += m.motifRecurrence;
                ++n;
            }
        acc.notesPerSecond /= n; acc.restRatio /= n; acc.meanPitch /= n; acc.motifRecurrence /= n;
        avg[st] = acc;
        std::printf ("  %-28s n/s=%.2f silencio=%.2f bends=%d vib=%d legato=%d pm=%d altura=%.1f repMotivo=%.2f\n",
                     styleName ((Style) st), acc.notesPerSecond, acc.restRatio, acc.bends, acc.vibratos, acc.legatos,
                     acc.palmMutes, acc.meanPitch, acc.motifRecurrence);
    }
    const auto& virt = avg[(int) Style::VirtuosoRock];
    const auto& amb = avg[(int) Style::Ambient];
    const auto& blues = avg[(int) Style::ExpressiveBlues];
    const auto& emo = avg[(int) Style::Emotional];
    const auto& vocal = avg[(int) Style::VocalResponse];
    CHECK (virt.notesPerSecond > 1.8 * amb.notesPerSecond);
    CHECK (virt.notesPerSecond > emo.notesPerSecond);
    CHECK (amb.restRatio > virt.restRatio);
    CHECK (vocal.restRatio > avg[(int) Style::MelodicRock].restRatio); // deja espacio a la voz
    CHECK (blues.bends > amb.bends);
    CHECK (virt.legatos > emo.legatos);
}

TEST (v2_plan_sections_and_climax)
{
    auto p = v2Params();
    p.bars = 8;
    const auto plan = SoloComposer::plan (p);
    CHECK (! plan.empty());
    int covered = 0;
    bool hasClimax = false;
    for (const auto& s : plan) { covered += s.bars; hasClimax |= s.role == SectionRole::Climax; }
    CHECK (covered == p.bars);
    CHECK (hasClimax);
    p.sectionPlan = "P2 D2 T2 C1 R1";
    const auto custom = SoloComposer::plan (p);
    CHECK (custom.size() == 5 && custom[3].role == SectionRole::Climax && custom[3].startBar == 6);
    // El clímax suena más alto e intenso que la presentación (media sobre semillas).
    double climaxPitch = 0, presPitch = 0, climaxVel = 0, presVel = 0;
    int nc = 0, np = 0;
    for (uint64_t seed = 1; seed <= 12; ++seed)
    {
        p.seed = seed;
        const auto s = SoloComposer::compose (p);
        for (const auto& n : s.notes)
        {
            const int bar = (int) (n.startTick / s.timeSignature.ticksPerBar());
            if (bar == 6) { climaxPitch += n.targetPitch(); climaxVel += n.velocity; ++nc; }
            if (bar < 2) { presPitch += n.targetPitch(); presVel += n.velocity; ++np; }
        }
    }
    climaxPitch /= std::max (1, nc); presPitch /= std::max (1, np);
    climaxVel /= std::max (1, nc); presVel /= std::max (1, np);
    std::printf ("  clímax: altura %.1f vel %.1f | presentación: altura %.1f vel %.1f\n", climaxPitch, climaxVel, presPitch, presVel);
    CHECK (climaxPitch > presPitch + 2.0);
    CHECK (climaxVel > presVel);
    // Plan inválido => error de validación explícito (la interfaz lo muestra) y no se compone nada.
    p.sectionPlan = "X9 garbage";
    CHECK (! SoloComposer::validate (p).empty());
    CHECK (SoloComposer::compose (p).notes.empty());
}

TEST (v2_motifs_recur_more_than_legacy)
{
    double v2 = 0, v1 = 0;
    int n = 0;
    for (const auto& pr : kProgs)
        for (uint64_t seed = 1; seed <= 5; ++seed)
        {
            auto p = v2Params (pr.key, pr.prog, 110);
            p.seed = seed;
            p.style = Style::MelodicRock;
            v2 += analyseSolo (SoloComposer::compose (p), p).motifRecurrence;
            p.engine = 1;
            p.legacyStyle = 1;
            v1 += analyseSolo (SoloComposer::compose (p), p).motifRecurrence;
            ++n;
        }
    std::printf ("  recurrencia de motivos (4-gramas de intervalos): motor 2 = %.3f, motor 1 = %.3f\n", v2 / n, v1 / n);
    CHECK (v2 > v1 * 1.3);
}

TEST (v2_keep_motifs_between_variations)
{
    // Con la misma semilla de motivos, dos variaciones comparten más material que con semillas distintas.
    auto shared = [] (const Sequence& a, const Sequence& b) {
        auto grams = [] (const Sequence& s) {
            std::set<std::vector<int>> g;
            for (size_t i = 3; i < s.notes.size(); ++i)
                g.insert ({ s.notes[i - 2].pitch - s.notes[i - 3].pitch, s.notes[i - 1].pitch - s.notes[i - 2].pitch,
                            s.notes[i].pitch - s.notes[i - 1].pitch });
            return g;
        };
        const auto ga = grams (a), gb = grams (b);
        int common = 0;
        for (const auto& x : ga) common += gb.count (x) ? 1 : 0;
        return (double) common / std::max<size_t> (1, std::min (ga.size(), gb.size()));
    };
    double keep = 0, free = 0;
    for (uint64_t s = 1; s <= 10; ++s)
    {
        auto p = v2Params();
        p.motifSeed = 777;
        p.seed = s;
        const auto a = SoloComposer::compose (p);
        p.seed = s + 100;
        const auto b = SoloComposer::compose (p);
        keep += shared (a, b);
        p.motifSeed = 0;
        const auto c = SoloComposer::compose (p);
        p.seed = s;
        const auto d = SoloComposer::compose (p);
        free += shared (c, d);
    }
    std::printf ("  material común: con motivos fijos %.2f, sin fijar %.2f\n", keep / 10, free / 10);
    CHECK (keep > free);
}

TEST (v2_locked_bars_and_notes_survive_regeneration)
{
    auto p = v2Params();
    p.seed = 9;
    auto s = SoloComposer::compose (p);
    const int64_t barT = s.timeSignature.ticksPerBar();
    std::vector<bool> locked ((size_t) p.bars, false);
    locked[1] = locked[5] = true;
    // Una nota bloqueada individualmente en el compás 3
    int lockedIdx = -1;
    for (size_t i = 0; i < s.notes.size(); ++i)
        if (s.notes[i].startTick / barT == 3) { s.notes[i].locked = true; lockedIdx = (int) i; break; }
    CHECK (lockedIdx >= 0);
    for (uint64_t ns : { 100ull, 200ull, 300ull })
    {
        const auto r = SoloComposer::regenerate (s, p, ns, locked);
        auto inBar = [&] (const Sequence& q, int b) {
            std::vector<Note> v;
            for (const auto& n : q.notes) if (n.startTick / barT == b) v.push_back (n);
            return v;
        };
        CHECK (inBar (r, 1) == inBar (s, 1));
        CHECK (inBar (r, 5) == inBar (s, 5));
        bool found = false;
        for (const auto& n : r.notes) found |= n == s.notes[(size_t) lockedIdx];
        CHECK (found);
        CHECK (! (r == s));
        for (size_t i = 1; i < r.notes.size(); ++i) CHECK (r.notes[i].startTick >= r.notes[i - 1].endTick());
    }
    // Regenerar solo un rango
    const auto rr = SoloComposer::regenerate (s, p, 55, std::vector<bool> (8, false), 2, 3);
    for (const auto& n : s.notes)
        if (n.startTick / barT < 2 || n.startTick / barT > 3)
        {
            bool same = false;
            for (const auto& m : rr.notes) same |= m.sameMusic (n) || (m.startTick == n.startTick && m.pitch == n.pitch);
            CHECK (same);
        }
}

TEST (v2_regenerate_selected_notes_only)
{
    auto p = v2Params();
    p.seed = 31;
    const auto s = SoloComposer::compose (p);
    CHECK (s.notes.size() > 10);
    const std::vector<int> idx { 4, 5, 6 };
    const auto r = SoloComposer::regenerateNotes (s, p, 1234, idx);
    const int64_t a = s.notes[4].startTick, b = s.notes[6].endTick();
    for (size_t i = 0; i < s.notes.size(); ++i)
    {
        if (i >= 4 && i <= 6) continue;
        const auto& n = s.notes[i];
        bool kept = false;
        for (const auto& m : r.notes) kept |= m.startTick == n.startTick && m.pitch == n.pitch;
        CHECK_MSG (kept, "nota " + std::to_string (i));
    }
    bool changed = false;
    for (const auto& m : r.notes)
        if (m.startTick >= a && m.startTick < b)
        {
            bool old = false;
            for (int i : idx) old |= m.sameMusic (s.notes[(size_t) i]);
            changed |= ! old;
        }
    CHECK (changed);
}

TEST (v2_seed_motif_is_used)
{
    // Un motivo semilla reconocible (5 notas con intervalos +2 +2 -4 +7) debe aparecer en el solo.
    auto p = v2Params ("C", "C Am F G", 100);
    SeedMotif m;
    const int ivs[] = { 0, 2, 2, -4, 7 };
    for (int i = 0; i < 5; ++i) m.notes.push_back ({ i * 480, 400, ivs[i] });
    m.lengthTicks = 2400;
    m.firstPitch = 67;
    p.seedMotifs.push_back (m);
    p.style = Style::VocalResponse;
    int hits = 0;
    for (double fid : { 0.9, 0.5 })
        for (uint64_t seed = 1; seed <= 6; ++seed)
        {
            p.fidelity = fid;
            p.seed = seed;
            const auto s = SoloComposer::compose (p);
            for (size_t i = 3; i < s.notes.size(); ++i)
            {
                const int a = s.notes[i - 2].pitch - s.notes[i - 3].pitch, b = s.notes[i - 1].pitch - s.notes[i - 2].pitch;
                const int c = s.notes[i].pitch - s.notes[i - 1].pitch;
                // contorno arriba-arriba-abajo con tamaños parecidos (la armonía puede ajustar ±1)
                if (a > 0 && a <= 3 && b > 0 && b <= 3 && c < 0 && c >= -5) { ++hits; break; }
            }
        }
    std::printf ("  solos con el contorno del motivo semilla: %d/12\n", hits);
    CHECK (hits >= 9);
}

TEST (performer_profiles_and_instrument_limits)
{
    auto p = v2Params();
    p.seed = 5;
    p.style = Style::MelodicRock;
    p.performer = PerformerProfile::None;
    const auto plain = SoloComposer::compose (p);
    for (const auto& n : plain.notes) CHECK (n.expr.isPlain());
    std::map<int, SoloMetrics> by;
    for (int prof = 1; prof < kNumPerformers - 1; ++prof)
    {
        SoloMetrics acc;
        for (uint64_t seed = 1; seed <= 8; ++seed)
        {
            p.performer = (PerformerProfile) prof;
            p.seed = seed;
            const auto m = analyseSolo (SoloComposer::compose (p), p);
            acc.vibratos += m.vibratos; acc.legatos += m.legatos; acc.palmMutes += m.palmMutes; acc.slides += m.slides;
            acc.bends += m.bends;
        }
        by[prof] = acc;
        std::printf ("  %-10s vib=%d legato=%d pm=%d slides=%d bends=%d\n", performerName ((PerformerProfile) prof),
                     acc.vibratos, acc.legatos, acc.palmMutes, acc.slides, acc.bends);
    }
    CHECK (by[(int) PerformerProfile::Rock].palmMutes > 0);
    CHECK (by[(int) PerformerProfile::Melodic].palmMutes == 0);
    CHECK (by[(int) PerformerProfile::Virtuoso].legatos > by[(int) PerformerProfile::Melodic].legatos);
    CHECK (by[(int) PerformerProfile::Blues].bends > by[(int) PerformerProfile::Melodic].bends);
    // Resolución automática del intérprete
    CHECK (resolvePerformer (Style::ExpressiveBlues, PerformerProfile::Auto, InstrumentId::Guitar) == PerformerProfile::Blues);
    CHECK (resolvePerformer (Style::VirtuosoRock, PerformerProfile::Auto, InstrumentId::Flute) == PerformerProfile::Melodic);
    CHECK (resolvePerformer (Style::Ambient, PerformerProfile::Rock, InstrumentId::Guitar) == PerformerProfile::Rock);
}

TEST (performer_keeps_locked_notes)
{
    auto p = v2Params();
    p.seed = 3;
    auto s = SoloComposer::compose (p);
    for (auto& n : s.notes) { n.locked = true; }
    const auto before = s;
    performSequence (s, p, PerformerProfile::Virtuoso, 99);
    CHECK (s == before);
}

TEST (legacy_composer_known_defects_report)
{
    // Informativo: el motor 1 (S0–S2) no siempre resuelve en nota estable con instrumentos/combos
    // no probados entonces. Se registra la tasa; el motor 2 debe resolver siempre (ver v2_musical_invariants).
    int fails = 0, total = 0;
    for (const auto& pr : kProgs)
        for (int st = 0; st < 4; ++st)
            for (uint64_t seed = 1; seed <= 6; ++seed)
            {
                auto p = v2Params (pr.key, pr.prog, 120);
                p.engine = 1;
                p.legacyStyle = st;
                p.instrument = InstrumentId::GuitarLite;
                p.seed = seed;
                for (int bars : { 2, 4, 8 })
                {
                    p.bars = bars;
                    const auto m = analyseSolo (SoloComposer::compose (p), p);
                    fails += m.resolvesToStable ? 0 : 1;
                    ++total;
                }
            }
    std::printf ("  motor 1: %d/%d solos sin resolución estable\n", fails, total);
    CHECK (true);
}
