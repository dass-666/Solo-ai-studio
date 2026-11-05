#include "solo/CreativeDNA.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace solo
{
namespace
{
SeedMotif motifFromNotes (const std::vector<Note>& v, size_t a, size_t b)
{
    SeedMotif m;
    if (a >= v.size() || b <= a) return m;
    b = std::min (b, v.size());
    const int64_t t0 = v[a].startTick;
    for (size_t i = a; i < b; ++i)
    {
        SeedMotifNote n;
        n.onset = v[i].startTick - t0;
        n.length = std::max<int64_t> (60, v[i].lengthTicks);
        n.interval = i == a ? 0 : v[i].pitch - v[i - 1].targetPitch();
        m.notes.push_back (n);
    }
    m.firstPitch = v[a].pitch;
    m.lengthTicks = m.notes.back().onset + m.notes.back().length;
    return m;
}

SeedMotif transformMotif (SeedMotif m, bool invert, bool retro, double timeScale)
{
    if (retro && m.notes.size() > 1)
    {
        // Retrogradación: mismos intervalos en orden inverso y con signo cambiado; ritmo invertido.
        std::vector<SeedMotifNote> r;
        const int64_t L = m.lengthTicks;
        for (size_t i = m.notes.size(); i-- > 0;)
        {
            SeedMotifNote n = m.notes[i];
            n.onset = L - (m.notes[i].onset + m.notes[i].length);
            n.interval = (i + 1 < m.notes.size()) ? -m.notes[i + 1].interval : 0;
            r.push_back (n);
        }
        r.front().interval = 0;
        int64_t minOn = r.front().onset;
        for (auto& n : r) { n.onset -= minOn; }
        m.notes = r;
    }
    for (auto& n : m.notes)
    {
        if (invert) n.interval = -n.interval;
        n.onset = (int64_t) std::llround ((double) n.onset * timeScale / 80.0) * 80;
        n.length = std::max<int64_t> (80, (int64_t) std::llround ((double) n.length * timeScale / 80.0) * 80);
    }
    for (size_t i = 1; i < m.notes.size(); ++i)
        if (m.notes[i].onset <= m.notes[i - 1].onset) m.notes[i].onset = m.notes[i - 1].onset + 80;
    m.lengthTicks = m.notes.empty() ? 0 : m.notes.back().onset + m.notes.back().length;
    return m;
}
} // namespace

std::string CreativeDNA::summary() const
{
    if (! valid) return "Sin ADN (no hay material suficiente)";
    std::ostringstream o;
    o.imbue (std::locale::classic());
    o.precision (2);
    o << (direction > 0.15 ? "Contorno ascendente" : direction < -0.15 ? "Contorno descendente" : "Contorno en arco/estable");
    o << ", " << (stepwiseRatio > 0.55 ? "por grados conjuntos" : "con saltos");
    o << ", " << (notesPerBeat > 2.0 ? "ritmo rápido" : notesPerBeat > 1.0 ? "ritmo medio" : "ritmo pausado");
    o << ", silencios " << (int) std::lround (restRatio * 100) << "%";
    o << ", repeticiones " << (int) std::lround (repetitionRate * 100) << "%";
    o << ", registro " << lowPitch << "-" << highPitch;
    o << ", frases de ~" << std::fixed << phraseBeats << " pulsos";
    o << ", " << motifs.size() << " motivo(s)";
    return o.str();
}

CreativeDNA extractDNA (const Sequence& s)
{
    CreativeDNA d;
    const auto& v = s.notes;
    if (v.size() < 2) return d;
    const int64_t beat = s.timeSignature.ticksPerBeat();
    d.valid = true;
    int64_t sounding = 0;
    int steps = 0, reps = 0;
    double vsum = 0, vmin = 127, vmax = 0, psum = 0;
    d.lowPitch = 127;
    d.highPitch = 0;
    std::vector<size_t> phraseStarts { 0 };
    for (size_t i = 0; i < v.size(); ++i)
    {
        const auto& n = v[i];
        sounding += n.lengthTicks;
        vsum += n.velocity;
        vmin = std::min (vmin, (double) n.velocity);
        vmax = std::max (vmax, (double) n.velocity);
        psum += n.targetPitch();
        d.lowPitch = std::min (d.lowPitch, n.pitch);
        d.highPitch = std::max (d.highPitch, n.targetPitch());
        const double units = (double) n.lengthTicks / (beat / 4.0);
        const int rb = units < 1.5 ? 0 : units < 2.5 ? 1 : units < 3.5 ? 2 : units < 5 ? 3 : units < 7 ? 4 : units < 10 ? 5 : units < 14 ? 6 : 7;
        d.rhythmHist[(size_t) rb] += 1;
        if (i > 0)
        {
            const int iv = n.pitch - v[i - 1].targetPitch();
            d.intervalHist[(size_t) std::clamp (iv + 12, 0, 24)] += 1;
            if (iv == 0) ++reps;
            else if (std::abs (iv) <= 2) ++steps;
            if (n.startTick - v[i - 1].endTick() >= beat / 2) phraseStarts.push_back (i);
        }
    }
    const double nn = (double) v.size();
    for (auto& x : d.intervalHist) x /= std::max (1.0, nn - 1);
    for (auto& x : d.rhythmHist) x /= nn;
    const double beats = (double) s.lengthTicks() / beat;
    d.notesPerBeat = nn / std::max (1.0, beats);
    d.restRatio = 1.0 - std::min (1.0, (double) sounding / (double) std::max<int64_t> (1, s.lengthTicks()));
    d.repetitionRate = reps / std::max (1.0, nn - 1);
    d.stepwiseRatio = steps / std::max (1.0, nn - 1);
    d.meanVelocity = vsum / nn;
    d.velocityRange = vmax - vmin;
    d.meanPitch = psum / nn;
    d.lastPitch = v.back().targetPitch();
    // Dirección: regresión lineal altura ~ tiempo (en pulsos)
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0;
        for (const auto& n : v)
        {
            const double x = (double) n.startTick / beat, y = n.targetPitch();
            sx += x; sy += y; sxx += x * x; sxy += x * y;
        }
        const double den = nn * sxx - sx * sx;
        d.direction = std::abs (den) > 1e-9 ? (nn * sxy - sx * sy) / den : 0.0;
    }
    // Longitud media de frase
    {
        double total = 0;
        for (size_t k = 0; k < phraseStarts.size(); ++k)
        {
            const size_t a = phraseStarts[k], b = k + 1 < phraseStarts.size() ? phraseStarts[k + 1] - 1 : v.size() - 1;
            total += (double) (v[b].endTick() - v[a].startTick) / beat;
        }
        d.phraseBeats = total / (double) phraseStarts.size();
    }
    // Motivo principal: la primera frase (máx. 8 notas, mín. 3)
    {
        size_t end = phraseStarts.size() > 1 ? phraseStarts[1] : v.size();
        if (end < 3) end = std::min<size_t> (v.size(), 6);
        d.motifs.push_back (motifFromNotes (v, 0, std::min<size_t> (end, 8)));
    }
    // Motivo recurrente: el 3-grama de intervalos más frecuente (si aparece al menos 2 veces)
    if (v.size() >= 5)
    {
        std::map<std::vector<int>, std::vector<size_t>> grams;
        for (size_t i = 3; i < v.size(); ++i)
            grams[{ v[i - 2].pitch - v[i - 3].pitch, v[i - 1].pitch - v[i - 2].pitch, v[i].pitch - v[i - 1].pitch }].push_back (i - 3);
        size_t bestCount = 1, bestPos = 0;
        for (const auto& [g, pos] : grams)
            if (pos.size() > bestCount) { bestCount = pos.size(); bestPos = pos.front(); }
        if (bestCount >= 2 && bestPos != 0) d.motifs.push_back (motifFromNotes (v, bestPos, std::min (v.size(), bestPos + 5)));
    }
    return d;
}

const char* derivedModeName (DerivedMode m)
{
    switch (m)
    {
        case DerivedMode::Imitate: return "Imitar";
        case DerivedMode::Continue: return "Continuar";
        case DerivedMode::Answer: return "Responder";
        case DerivedMode::Transform: return "Transformar";
        case DerivedMode::Free: return "Solo libre";
    }
    return "?";
}

Sequence generateFromDNA (const CreativeDNA& dna, const ComposerParams& params, const DerivedOptions& o)
{
    ComposerParams p = params;
    p.engine = 2;
    if (! dna.valid) return SoloComposer::compose (p);
    // Carácter del ADN -> controles del compositor
    p.density = std::clamp (0.15 + 0.3 * dna.notesPerBeat, 0.05, 1.0);
    p.complexity = std::clamp (0.25 + 0.6 * (1.0 - dna.stepwiseRatio) * (0.5 + o.development), 0.0, 1.0);
    p.energy = std::clamp (dna.meanVelocity / 127.0 * (0.8 + 0.4 * o.expression), 0.0, 1.0);
    p.fidelity = std::clamp (o.fidelity, 0.0, 1.0);
    p.freedom = std::clamp (0.5 * o.freedom + 0.5 * o.development, 0.0, 1.0);
    if (o.expression < 0.15) p.performer = PerformerProfile::None;
    else if (o.expression > 0.8 && p.performer == PerformerProfile::Auto)
        p.performer = p.style == Style::ExpressiveBlues ? PerformerProfile::Blues : PerformerProfile::Virtuoso;

    const SeedMotif main = dna.motifs.empty() ? SeedMotif {} : dna.motifs.front();
    switch (o.mode)
    {
        case DerivedMode::Imitate:
            p.seedMotifs = dna.motifs;
            p.fidelity = std::max (p.fidelity, 0.75);
            break;
        case DerivedMode::Continue:
        {
            // Empieza donde terminó la idea original y sigue desarrollando su último gesto.
            SeedMotif m = main;
            if (dna.lastPitch > 0) m.firstPitch = dna.lastPitch;
            p.seedMotifs = { m };
            if (p.sectionPlan.empty() && p.bars >= 4)
            {
                const int c = std::max (1, p.bars / 4), r = std::max (1, p.bars / 4);
                const int dd = std::max (1, p.bars - 2 * c - r);
                p.sectionPlan = "D" + std::to_string (dd) + " T" + std::to_string (c) + " C" + std::to_string (p.bars - dd - c - r) + " R" + std::to_string (r);
                if (p.bars - dd - c - r <= 0) p.sectionPlan.clear();
            }
            break;
        }
        case DerivedMode::Answer:
            p.seedMotifs = { transformMotif (main, true, false, 1.0) };
            if (p.style == Style::MelodicRock) p.style = Style::VocalResponse;
            break;
        case DerivedMode::Transform:
            p.seedMotifs = { transformMotif (main, o.development > 0.5, true, o.development > 0.7 ? 2.0 : (o.development < 0.3 ? 0.5 : 1.0)) };
            p.fidelity = std::min (p.fidelity, 0.6);
            break;
        case DerivedMode::Free:
            p.seedMotifs.clear();
            break;
    }
    // Plan "Continuar" inválido => automático
    if (! p.sectionPlan.empty() && ! SoloComposer::validate (p).empty()) p.sectionPlan.clear();
    return SoloComposer::compose (p);
}

double melodicSimilarity (const Sequence& a, const Sequence& b)
{
    auto grams = [] (const Sequence& s) {
        std::multiset<std::vector<int>> g;
        for (size_t i = 3; i < s.notes.size(); ++i)
            g.insert ({ s.notes[i - 2].pitch - s.notes[i - 3].pitch, s.notes[i - 1].pitch - s.notes[i - 2].pitch,
                        s.notes[i].pitch - s.notes[i - 1].pitch });
        return g;
    };
    const auto ga = grams (a), gb = grams (b);
    if (ga.empty() || gb.empty()) return 0.0;
    int common = 0;
    std::set<std::vector<int>> seen;
    for (const auto& x : ga)
        if (seen.insert (x).second && gb.count (x)) common += (int) std::min (ga.count (x), gb.count (x));
    return (double) common / (double) std::min (ga.size(), gb.size());
}

} // namespace solo
