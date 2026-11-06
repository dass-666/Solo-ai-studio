// Melody Explorer (V4-B/C/D). Ver MelodyExplorer.h.
#include "solo/MelodyExplorer.h"
#include "solo/ChorusComposer.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace solo
{
namespace
{
double clamp01 (double v) { return std::max (0.0, std::min (1.0, v)); }
int mod12 (int v) { return ((v % 12) + 12) % 12; }

// ---------------------------------------------------------------- transformaciones del motivo semilla
SeedMotif simplifyMotif (const SeedMotif& m)
{
    // Menos notas y más largas, intervalos reducidos a grados conjuntos/terceras: pegajoso y fácil.
    SeedMotif out;
    out.firstPitch = m.firstPitch;
    for (size_t i = 0; i < m.notes.size(); ++i)
    {
        if (i % 3 == 2 && m.notes.size() > 3) continue; // quita una de cada tres (la débil)
        SeedMotifNote n = m.notes[i];
        n.interval = std::clamp (n.interval, -4, 4);
        out.notes.push_back (n);
    }
    // Alargar cada nota hasta la siguiente (legato cantable)
    for (size_t i = 0; i + 1 < out.notes.size(); ++i) out.notes[i].length = std::max<int64_t> (out.notes[i].length, out.notes[i + 1].onset - out.notes[i].onset);
    out.lengthTicks = m.lengthTicks;
    if (! out.notes.empty()) out.notes.back().length = std::max<int64_t> (out.notes.back().length, kPPQ);
    return out;
}

SeedMotif sustainMotif (const SeedMotif& m)
{
    // Emocional: primera y última nota más largas (notas tenidas), el resto igual.
    SeedMotif out = m;
    if (out.notes.size() >= 2)
    {
        out.notes.back().length = std::max<int64_t> (out.notes.back().length, kPPQ * 2);
        out.notes.front().length = std::max<int64_t> (out.notes.front().length, std::min<int64_t> (kPPQ, out.notes[1].onset - out.notes.front().onset));
    }
    out.lengthTicks = std::max (out.lengthTicks, out.notes.empty() ? 0 : out.notes.back().onset + out.notes.back().length);
    return out;
}

SeedMotif syncopateMotif (const SeedMotif& m, const std::vector<double>& offbeatStrength)
{
    // Rítmica: ataques desplazados al contratiempo (más donde el beat acentúa el contratiempo),
    // duraciones alternadas corta/larga y una pausa intencionada.
    SeedMotif out = m;
    const int64_t eighth = kPPQ / 2;
    double off = 0.5;
    if (! offbeatStrength.empty())
    {
        double s = 0; for (double v : offbeatStrength) s += v;
        off = clamp01 (0.3 + 0.7 * s / (double) offbeatStrength.size());
    }
    for (size_t i = 1; i < out.notes.size(); ++i)
    {
        auto& n = out.notes[i];
        const int64_t onBeat = (n.onset / kPPQ) * kPPQ;
        if (n.onset == onBeat && (i % 2 == 1 || off > 0.6)) n.onset = onBeat + eighth; // anticipa/retarda al contratiempo
        n.length = (i % 2 == 0) ? std::max<int64_t> (eighth / 2, n.length / 2) : std::max<int64_t> (eighth, n.length);
        if (n.onset <= out.notes[i - 1].onset) n.onset = out.notes[i - 1].onset + eighth / 2;
    }
    for (size_t i = 0; i + 1 < out.notes.size(); ++i) out.notes[i].length = std::min (out.notes[i].length, out.notes[i + 1].onset - out.notes[i].onset);
    if (out.notes.size() >= 3)
    {
        // pausa: la nota central se acorta y deja hueco
        auto& mid = out.notes[out.notes.size() / 2];
        mid.length = std::max<int64_t> (eighth / 2, mid.length / 2);
    }
    out.lengthTicks = std::max (m.lengthTicks, out.notes.empty() ? 0 : out.notes.back().onset + out.notes.back().length);
    out.lengthTicks = ((out.lengthTicks + kPPQ - 1) / kPPQ) * kPPQ;
    return out;
}

SeedMotif widenMotif (const SeedMotif& m, bool invert)
{
    // Atrevida: intervalos más amplios (x1.6, saltos de hasta una octava), opcionalmente invertidos.
    SeedMotif out = m;
    for (size_t i = 1; i < out.notes.size(); ++i)
    {
        int iv = (int) std::lround (out.notes[i].interval * 1.6);
        if (iv == 0 && out.notes[i].interval == 0 && i % 2 == 0) iv = (i % 4 == 0) ? 5 : -3; // nota repetida -> salto
        iv = std::clamp (iv, -12, 12);
        out.notes[i].interval = invert ? -iv : iv;
    }
    return out;
}

SeedMotif fragmentMotif (const SeedMotif& m)
{
    SeedMotif out; out.firstPitch = m.firstPitch;
    const size_t n = std::max<size_t> (2, m.notes.size() / 2);
    for (size_t i = 0; i < n && i < m.notes.size(); ++i) out.notes.push_back (m.notes[i]);
    out.lengthTicks = out.notes.empty() ? 0 : ((out.notes.back().onset + out.notes.back().length + kPPQ - 1) / kPPQ) * kPPQ;
    return out;
}

SeedMotif augmentMotif (const SeedMotif& m)
{
    SeedMotif out = m;
    for (auto& n : out.notes) { n.onset *= 2; n.length *= 2; }
    out.lengthTicks = m.lengthTicks * 2;
    return out;
}

// ---------------------------------------------------------------- utilidades sobre secuencias
int64_t barTicks (const Sequence& s) { return s.timeSignature.ticksPerBar(); }

// Mueve los ataques cercanos a acentos fuertes del beat hacia ellos (±1/8) y abre una pausa de un pulso
// cada dos compases donde el beat tiene su acento más fuerte (respuesta al golpe).
void alignToAccents (Sequence& s, const BeatAnalysisResult* beat, int fromBar, const std::vector<bool>& locked, uint64_t seed)
{
    if (beat == nullptr || beat->beatStrength.empty()) return;
    const int64_t bt = s.timeSignature.ticksPerBeat(), bar = barTicks (s);
    const int bpb = std::max (1, beat->beatsPerBar);
    Rng rng (seed ^ 0x51ull);
    for (auto& n : s.notes)
    {
        const int b = (int) (n.startTick / bar);
        if (n.locked || (b < (int) locked.size() && locked[(size_t) b])) continue;
        const int64_t inBar = n.startTick % bar;
        const int beatIdx = (int) (inBar / bt);
        const size_t gi = (size_t) ((fromBar + b) * bpb + beatIdx);
        if (gi >= beat->beatStrength.size()) continue;
        const int64_t beatPos = (int64_t) b * bar + (int64_t) beatIdx * bt;
        const int64_t offPos = beatPos + bt / 2;
        const bool strongBeat = beat->beatStrength[gi] > 0.6;
        const bool strongOff = gi < beat->offbeatStrength.size() && beat->offbeatStrength[gi] > 0.6;
        if (strongOff && std::llabs (n.startTick - offPos) <= bt / 4 && n.startTick != offPos && rng.chance (0.7)) { n.lengthTicks = std::max<int64_t> (bt / 4, n.lengthTicks - (offPos - n.startTick)); n.startTick = offPos; }
        else if (strongBeat && std::llabs (n.startTick - beatPos) <= bt / 4 && n.startTick != beatPos && rng.chance (0.6)) { n.lengthTicks = std::max<int64_t> (bt / 4, n.lengthTicks + (n.startTick - beatPos)); n.startTick = beatPos; }
    }
    // Pausas intencionadas: en compases impares no bloqueados, quitar las notas del último pulso (deja respirar al golpe).
    std::vector<Note> kept;
    for (const auto& n : s.notes)
    {
        const int b = (int) (n.startTick / bar);
        const bool prot = n.locked || (b < (int) locked.size() && locked[(size_t) b]);
        if (! prot && b % 2 == 1 && n.startTick % bar >= bar - bt && n.lengthTicks < bt && rng.chance (0.6)) continue;
        kept.push_back (n);
    }
    s.notes = kept;
    s.normalise();
}

// Devuelve a 'seq' exactamente las notas de 'ref' en los compases bloqueados y las notas bloqueadas
// (garantía final, independiente de lo que haga el motor).
void restoreLocked (Sequence& seq, const Sequence& ref, const std::vector<bool>& locks)
{
    const int64_t bar = ref.timeSignature.ticksPerBar();
    auto isLockedBar = [&] (int64_t tick) { const int b = (int) (tick / bar); return b >= 0 && b < (int) locks.size() && locks[(size_t) b]; };
    std::vector<Note> out;
    for (const auto& n : seq.notes) if (! isLockedBar (n.startTick) && ! n.locked) out.push_back (n);
    for (const auto& n : ref.notes) if (isLockedBar (n.startTick) || n.locked) out.push_back (n);
    seq.notes = out;
    seq.normalise();
}

Sequence composeWithFallback (ComposerParams p)
{
    Sequence s = SoloComposer::compose (p);
    if (! s.notes.empty()) return s;
    p.seedMotifs.clear();
    p.sectionPlan.clear();
    return SoloComposer::compose (p);
}

std::string planString (const std::vector<std::pair<char, int>>& parts)
{
    std::string s;
    for (const auto& pr : parts) if (pr.second > 0) { if (! s.empty()) s += ' '; s += pr.first; s += std::to_string (pr.second); }
    return s;
}
} // namespace

// ---------------------------------------------------------------- nombres
const char* melodyTypeName (MelodyType t)
{
    switch (t)
    {
        case MelodyType::Chorus: return "Coro";
        case MelodyType::Solo: return "Solo";
        case MelodyType::Interlude: return "Interludio";
        case MelodyType::Intro: return "Intro";
        case MelodyType::Outro: return "Final";
        case MelodyType::Background: return "Melodía de fondo";
    }
    return "?";
}
const char* chorusCharacterName (ChorusCharacter c)
{
    switch (c) { case ChorusCharacter::EasyToSing: return "Fácil de cantar"; case ChorusCharacter::Emotional: return "Emocional"; case ChorusCharacter::Energetic: return "Enérgico"; }
    return "?";
}
const char* strategyName (ProposalStrategy s)
{
    switch (s) { case ProposalStrategy::Simple: return "A · Sencilla y pegajosa"; case ProposalStrategy::Emotional: return "B · Emocional"; case ProposalStrategy::Rhythmic: return "C · Rítmica"; case ProposalStrategy::Bold: return "D · Atrevida"; }
    return "?";
}
const char* strategyDescription (ProposalStrategy s)
{
    switch (s)
    {
        case ProposalStrategy::Simple: return "Motivo reconocible, repetición controlada, registro cómodo, frases memorables";
        case ProposalStrategy::Emotional: return "Notas sostenidas, tensión armónica, resoluciones claras, desarrollo expresivo";
        case ProposalStrategy::Rhythmic: return "Síncopas, variedad de duraciones, respuestas a los golpes del beat, pausas intencionadas";
        case ProposalStrategy::Bold: return "Más exploración, tensiones controladas, intervalos amplios, variaciones inesperadas pero armónicas";
    }
    return "?";
}
const char* freedomBandName (FreedomBand b)
{
    switch (b) { case FreedomBand::Close: return "Variaciones cercanas"; case FreedomBand::Identity: return "Conserva la identidad"; case FreedomBand::NewPhrases: return "Frases nuevas con elementos del motivo"; case FreedomBand::Experimental: return "Experimentación amplia"; }
    return "?";
}

// ---------------------------------------------------------------- contexto
int MelodyContext::bars() const
{
    const int total = harmony.bars;
    if (total <= 0) return 0;
    const int from = std::max (0, std::min (fromBar, total - 1));
    const int to = toBar < 0 ? total - 1 : std::min (toBar, total - 1);
    return std::max (0, to - from + 1);
}
Progression MelodyContext::sectionProgression() const
{
    Progression p;
    const int total = harmony.bars;
    const int from = std::max (0, std::min (fromBar, std::max (0, total - 1)));
    const int n = bars();
    for (int b = 0; b < n; ++b) p.bars.push_back (harmony.chordAtBar (from + b));
    return p;
}
MelodyContext MelodyContext::manual (const Key& k, const Progression& p, double bpm)
{
    MelodyContext c;
    c.harmony = HarmonyTimeline::fromManual (k, p, bpm);
    c.bpm = bpm;
    return c;
}

// ---------------------------------------------------------------- métricas
MelodyMetrics melodySimilarity (const Sequence& a, const Sequence& b)
{
    MelodyMetrics m;
    m.interval = melodicSimilarity (a, b);
    // ritmo: conjunto de posiciones de ataque (resolución de semicorchea) dentro del compás, Jaccard ponderado por compás
    auto rhythmSet = [] (const Sequence& s) {
        std::map<int64_t, int> pos;
        const int64_t q = kPPQ / 4;
        for (const auto& n : s.notes) ++pos[(n.startTick / q) * q];
        return pos;
    };
    const auto ra = rhythmSet (a), rb = rhythmSet (b);
    if (! ra.empty() && ! rb.empty())
    {
        int inter = 0, uni = 0;
        for (const auto& [k, v] : ra) { auto it = rb.find (k); inter += it == rb.end() ? 0 : std::min (v, it->second); uni += it == rb.end() ? v : std::max (v, it->second); }
        for (const auto& [k, v] : rb) if (! ra.count (k)) uni += v;
        m.rhythm = uni > 0 ? (double) inter / uni : 0;
    }
    // contorno: histograma de alturas (coseno)
    std::array<double, 128> ha {}, hb {};
    for (const auto& n : a.notes) ha[(size_t) std::clamp (n.pitch, 0, 127)] += (double) n.lengthTicks;
    for (const auto& n : b.notes) hb[(size_t) std::clamp (n.pitch, 0, 127)] += (double) n.lengthTicks;
    double dot = 0, na = 0, nb = 0;
    for (int i = 0; i < 128; ++i) { dot += ha[(size_t) i] * hb[(size_t) i]; na += ha[(size_t) i] * ha[(size_t) i]; nb += hb[(size_t) i] * hb[(size_t) i]; }
    m.contour = (na > 0 && nb > 0) ? dot / std::sqrt (na * nb) : 0;
    m.overall = clamp01 (0.45 * m.interval + 0.35 * m.rhythm + 0.20 * m.contour);
    return m;
}

double familyDiversity (const std::vector<Sequence>& v)
{
    if (v.size() < 2) return 0;
    double s = 0; int n = 0;
    for (size_t i = 0; i < v.size(); ++i)
        for (size_t j = i + 1; j < v.size(); ++j) { s += 1.0 - melodySimilarity (v[i], v[j]).overall; ++n; }
    return n > 0 ? s / n : 0;
}

HarmonicFit harmonicFit (const Sequence& s, const Progression& p, const Key& k)
{
    HarmonicFit f;
    if (p.bars.empty()) return f;
    const int64_t bar = s.timeSignature.ticksPerBar(), beat = s.timeSignature.ticksPerBeat();
    const auto scale = k.scalePitchClasses();
    int strongOk = 0, scaleOk = 0;
    for (const auto& n : s.notes)
    {
        const int b = (int) (n.startTick / bar);
        const Chord c = p.chordAtBar (std::min (b, (int) p.bars.size() - 1));
        const int pc = mod12 (n.targetPitch());
        const bool inScale = std::find (scale.begin(), scale.end(), pc) != scale.end();
        const bool inChord = c.containsPitchClass (pc);
        ++f.notes;
        if (inScale || inChord) ++scaleOk;
        const bool strong = (n.startTick % bar) % beat == 0 && ((n.startTick % bar) / beat) % 2 == 0 && n.lengthTicks >= beat / 2;
        if (strong) { ++f.strongNotes; if (inChord) ++strongOk; }
    }
    f.inScale = f.notes > 0 ? (double) scaleOk / f.notes : 0;
    f.strongChordTones = f.strongNotes > 0 ? (double) strongOk / f.strongNotes : 1.0;
    return f;
}

std::string MelodyFamily::summary() const
{
    std::ostringstream o;
    o << melodyTypeName (brief.type);
    if (brief.type == MelodyType::Chorus) o << " (" << chorusCharacterName (brief.character) << ")";
    o << ", libertad " << (int) std::lround (brief.freedom) << "%, " << proposals.size() << " propuestas, diversidad " << (int) std::lround (diversity * 100) << "%";
    if (identity.valid()) o << ", motivo de " << identity.motif.notes.size() << " notas";
    return o.str();
}

// ---------------------------------------------------------------- explorer
FreedomBand MelodyExplorer::freedomBand (double f)
{
    return f <= 25 ? FreedomBand::Close : f <= 55 ? FreedomBand::Identity : f <= 80 ? FreedomBand::NewPhrases : FreedomBand::Experimental;
}

ComposerParams MelodyExplorer::baseParams (const MelodyContext& ctx, const MelodyBrief& brief)
{
    ComposerParams p;
    p.key = ctx.harmony.key;
    p.progression = ctx.sectionProgression();
    p.timeSignature.numerator = ctx.harmony.beatsPerBar == 3 ? 3 : 4;
    p.bars = std::clamp (ctx.bars(), 1, 64);
    p.instrument = brief.instrument;
    p.bpm = ctx.bpm > 0 ? ctx.bpm : ctx.harmony.bpm;
    p.seed = brief.seed;
    p.engine = 2;
    p.expression = brief.expression;
    p.style = brief.baseStyle;
    switch (brief.type)
    {
        case MelodyType::Chorus:
        {
            int lo, hi, leap; chorusRange (brief.character, lo, hi, leap);
            p.rangeLow = lo; p.rangeHigh = hi; p.maxLeap = leap;
            if (brief.character == ChorusCharacter::EasyToSing) { p.style = Style::SoftImprov; p.energy = 0.55; p.density = 0.4; p.complexity = 0.2; }
            else if (brief.character == ChorusCharacter::Emotional) { p.style = Style::Emotional; p.energy = 0.6; p.density = 0.35; p.complexity = 0.3; }
            else { p.style = Style::MelodicRock; p.energy = 0.85; p.density = 0.6; p.complexity = 0.35; }
            p.performer = PerformerProfile::Melodic;
            break;
        }
        case MelodyType::Solo: p.energy = 0.65; p.density = 0.55; p.complexity = 0.45; break;
        case MelodyType::Interlude: p.style = Style::SoftImprov; p.energy = 0.45; p.density = 0.45; p.complexity = 0.3; break;
        case MelodyType::Intro: p.style = Style::Cinematic; p.energy = 0.3; p.density = 0.3; p.complexity = 0.25; p.maxLeap = 7; break;
        case MelodyType::Outro: p.style = Style::Emotional; p.energy = 0.45; p.density = 0.35; p.complexity = 0.3; break;
        case MelodyType::Background: p.style = Style::Ambient; p.energy = 0.25; p.density = 0.15; p.complexity = 0.15; p.maxLeap = 5; break;
    }
    return p;
}

std::string MelodyExplorer::sectionPlanFor (const MelodyContext& ctx, const MelodyBrief& brief, ProposalStrategy s)
{
    const int n = std::clamp (ctx.bars(), 1, 64);
    if (n < 4) return "";
    // Con beat: la energía de cada compás de la sección marca la función (baja -> presentación/resolución, alta -> clímax).
    std::vector<int> level ((size_t) n, 1);
    bool haveEnergy = false;
    if (ctx.beat != nullptr && ! ctx.beat->energyRegions.empty())
    {
        const int from = std::max (0, ctx.fromBar);
        for (int b = 0; b < n; ++b)
            for (const auto& r : ctx.beat->energyRegions)
                if (from + b >= r.startBar && from + b < r.endBar) { level[(size_t) b] = (int) r.level; haveEnergy = true; }
    }
    auto byType = [&] () -> std::vector<std::pair<char, int>> {
        const int q = std::max (1, n / 4);
        switch (brief.type)
        {
            case MelodyType::Intro: return { { 'P', n - q }, { 'R', q } };
            case MelodyType::Outro: return { { 'D', std::max (1, n / 3) }, { 'R', n - std::max (1, n / 3) } };
            case MelodyType::Background: return { { 'P', n / 2 }, { 'D', n - n / 2 } };
            case MelodyType::Chorus: return { { 'P', q }, { 'D', q }, { 'C', n - 3 * q }, { 'R', q } };
            default: break;
        }
        switch (s)
        {
            case ProposalStrategy::Simple: return { { 'P', n / 2 }, { 'D', n / 4 }, { 'R', n - n / 2 - n / 4 } };
            case ProposalStrategy::Emotional: return { { 'P', q }, { 'D', q }, { 'T', q }, { 'C', std::max (1, (n - 3 * q) / 2) }, { 'R', n - 3 * q - std::max (1, (n - 3 * q) / 2) } };
            case ProposalStrategy::Rhythmic: return { { 'P', q }, { 'D', n - 2 * q - std::max (1, q / 2) }, { 'C', q }, { 'R', std::max (1, q / 2) } };
            case ProposalStrategy::Bold: return { { 'P', std::max (1, q / 2) }, { 'T', q }, { 'C', n - q - std::max (1, q / 2) - q }, { 'R', q } };
        }
        return { { 'P', n } };
    };
    std::vector<std::pair<char, int>> parts;
    if (haveEnergy)
    {
        // Agrupar compases por nivel: baja -> P (o R si es al final), media -> D, alta -> C (T justo antes de una alta).
        for (int b = 0; b < n; ++b)
        {
            char role = level[(size_t) b] == 0 ? 'P' : level[(size_t) b] == 2 ? 'C' : 'D';
            if (role == 'D' && b + 1 < n && level[(size_t) b + 1] == 2 && (parts.empty() || parts.back().first != 'C')) role = 'T';
            if (b == n - 1 && role != 'C') role = 'R';
            if (! parts.empty() && parts.back().first == role) ++parts.back().second; else parts.push_back ({ role, 1 });
        }
        if (parts.back().first == 'C' && parts.back().second > 1) { --parts.back().second; parts.push_back ({ 'R', 1 }); }
        else if (parts.back().first == 'C') parts.back().first = 'R';
    }
    else parts = byType();
    int sum = 0; for (const auto& pr : parts) sum += pr.second;
    if (sum != n) return "";
    return planString (parts);
}

MelodyIdentity MelodyExplorer::createIdentity (const MelodyContext& ctx, const MelodyBrief& brief)
{
    MelodyIdentity id;
    if (ctx.bars() <= 0 || ctx.harmony.segments.empty()) return id;
    id.motifSeed = brief.seed * 0x2545F491ull + 0x1234567ull;
    // Material de identidad: una frase corta (2 compases) compuesta por el motor con el carácter del tipo.
    ComposerParams p = baseParams (ctx, brief);
    p.density = std::max (p.density, 0.5); // el gancho necesita notas aunque el tipo sea escaso (fondo, intro)
    p.energy = std::max (p.energy, 0.5);
    if (p.style == Style::Ambient || p.style == Style::Cinematic) p.style = Style::SoftImprov; // estilos escasos no dan un gancho
    p.bars = std::min (2, p.bars);
    p.progression.bars.resize ((size_t) p.bars);
    p.sectionPlan = p.bars == 2 ? "P1 R1" : "";
    p.motifSeed = id.motifSeed;
    p.performer = PerformerProfile::None;
    for (int attempt = 0; attempt < 6 && ! id.valid(); ++attempt)
    {
        p.seed = brief.seed + (uint64_t) attempt * 7919ull;
        const Sequence s = SoloComposer::compose (p);
        const CreativeDNA dna = extractDNA (s);
        if (! dna.valid || dna.motifs.empty() || dna.motifs.front().notes.size() < 3) continue;
        id.dna = dna;
        id.motif = dna.motifs.front();
        if (dna.motifs.size() > 1) id.secondary = dna.motifs[1];
        // Motivo compacto (máximo 8 notas, 2 compases): un gancho, no una frase entera.
        if (id.motif.notes.size() > 8) { id.motif.notes.resize (8); id.motif.lengthTicks = ((id.motif.notes.back().onset + id.motif.notes.back().length + kPPQ - 1) / kPPQ) * kPPQ; }
        id.motif.lengthTicks = std::min<int64_t> (id.motif.lengthTicks, 2 * p.timeSignature.ticksPerBar());
    }
    if (id.valid())
    {
        std::ostringstream o;
        o << "Motivo de " << id.motif.notes.size() << " notas, " << id.dna.summary();
        id.description = o.str();
    }
    return id;
}

MelodyProposal MelodyExplorer::proposeOne (const MelodyContext& ctx, const MelodyBrief& brief, const MelodyIdentity& id, ProposalStrategy s)
{
    MelodyProposal out;
    out.strategy = s;
    out.name = strategyName (s);
    out.description = strategyDescription (s);
    ComposerParams p = baseParams (ctx, brief);
    p.motifSeed = id.motifSeed;
    p.sectionPlan = sectionPlanFor (ctx, brief, s);
    p.seed = brief.seed * 131ull + (uint64_t) s * 17ull + 3ull;
    const bool chorus = brief.type == MelodyType::Chorus;
    const bool calm = brief.type == MelodyType::Background || brief.type == MelodyType::Intro;
    switch (s)
    {
        case ProposalStrategy::Simple:
            p.seedMotifs = { simplifyMotif (id.motif) };
            p.fidelity = 0.9; p.freedom = 0.15;
            p.complexity = std::max (0.0, p.complexity - 0.15); p.density = std::max (0.1, p.density - 0.1);
            if (p.maxLeap < 0 || p.maxLeap > 7) p.maxLeap = 7;
            break;
        case ProposalStrategy::Emotional:
            p.seedMotifs = { sustainMotif (id.motif) };
            if (! calm) p.style = Style::Emotional;
            p.fidelity = 0.7; p.freedom = 0.45;
            p.density = std::max (0.1, p.density - 0.15); p.energy = std::min (1.0, p.energy + 0.05);
            if (! chorus) p.performer = PerformerProfile::Melodic;
            break;
        case ProposalStrategy::Rhythmic:
            p.seedMotifs = { syncopateMotif (id.motif, ctx.beat ? ctx.beat->offbeatStrength : std::vector<double> {}) };
            if (! id.secondary.notes.empty()) p.seedMotifs.push_back (id.secondary);
            if (p.style == Style::Emotional || p.style == Style::Ambient || p.style == Style::Cinematic) p.style = Style::MelodicRock;
            p.fidelity = 0.65; p.freedom = 0.5;
            p.density = std::min (1.0, p.density + 0.2); p.complexity = std::min (1.0, p.complexity + 0.1);
            break;
        case ProposalStrategy::Bold:
            p.seedMotifs = { widenMotif (id.motif, brief.seed % 2 == 1) };
            if (! calm && ! chorus) p.style = ctx.harmony.key.mode == Mode::Minor ? Style::Cinematic : Style::VirtuosoRock;
            p.fidelity = 0.45; p.freedom = 0.8;
            p.complexity = std::min (1.0, p.complexity + 0.35); p.energy = std::min (1.0, p.energy + 0.1);
            if (p.maxLeap > 0) p.maxLeap = std::min (12, p.maxLeap + (chorus ? 2 : 4)); // en un coro sigue siendo cantable
            break;
    }
    if (! SoloComposer::validate (p).empty()) p.sectionPlan.clear();
    out.sequence = composeWithFallback (p);
    const std::vector<bool> noLocks;
    if (s == ProposalStrategy::Rhythmic) alignToAccents (out.sequence, ctx.beat, ctx.fromBar, noLocks, p.seed);
    if (chorus)
    {
        ChorusShapeOptions o; o.character = brief.character;
        o.mirrorHalves = s != ProposalStrategy::Bold; // la atrevida no repite literalmente
        out.sequence = shapeChorus (out.sequence, p, p.progression, o, noLocks);
    }
    else if (brief.type == MelodyType::Outro)
    {
        ChorusShapeOptions o; o.mirrorHalves = false; o.breathe = false; o.resolveEnding = true; o.minNoteTicks = 1;
        out.sequence = shapeChorus (out.sequence, p, p.progression, o, noLocks);
    }
    out.params = p;
    // Parecido con la realización directa de la identidad (misma familia).
    ComposerParams ref = baseParams (ctx, brief);
    ref.motifSeed = id.motifSeed; ref.seedMotifs = { id.motif }; ref.fidelity = 0.9; ref.freedom = 0.2; ref.performer = PerformerProfile::None;
    const Sequence refSeq = composeWithFallback (ref);
    out.similarityToIdentity = melodySimilarity (out.sequence, refSeq).overall;
    return out;
}

MelodyFamily MelodyExplorer::propose (const MelodyContext& ctx, const MelodyBrief& brief)
{
    MelodyFamily f;
    f.brief = brief;
    f.identity = createIdentity (ctx, brief);
    if (! f.identity.valid()) return f;
    std::vector<Sequence> seqs;
    for (int s = 0; s < kNumStrategies; ++s)
    {
        f.proposals.push_back (proposeOne (ctx, brief, f.identity, (ProposalStrategy) s));
        seqs.push_back (f.proposals.back().sequence);
    }
    f.diversity = familyDiversity (seqs);
    return f;
}

std::vector<MelodyProposal> MelodyExplorer::vary (const MelodyContext& ctx, const MelodyBrief& brief, const MelodyIdentity& id,
                                                  const MelodyProposal& favorite, int count, uint64_t seed,
                                                  const std::vector<bool>& lockedBars)
{
    (void) ctx; // la armonía ya está en favorite.params (misma sección)
    std::vector<MelodyProposal> out;
    if (favorite.sequence.notes.empty() || count <= 0) return out;
    const double fr = std::clamp (brief.freedom, 0.0, 100.0);
    const FreedomBand band = freedomBand (fr);
    const int bars = favorite.sequence.bars;
    std::vector<bool> locks ((size_t) std::max (0, bars), false);
    for (size_t i = 0; i < locks.size() && i < lockedBars.size(); ++i) locks[i] = lockedBars[i];
    const int64_t bar = favorite.sequence.timeSignature.ticksPerBar();
    const bool chorus = brief.type == MelodyType::Chorus;

    for (int k = 0; k < count; ++k)
    {
        Rng rng (seed * 977ull + (uint64_t) k * 31ull + (uint64_t) fr);
        const uint64_t vseed = seed * 7ull + (uint64_t) k * 1013ull + 11ull;
        ComposerParams p = favorite.params;
        p.seed = vseed;
        MelodyProposal v;
        v.strategy = favorite.strategy;
        v.params = p;
        Sequence s;
        switch (band)
        {
            case FreedomBand::Close:
            {
                // 0-25: regenerar un puñado de notas no estructurales (5 % + libertad), mismo motivo y plan.
                std::vector<int> idx;
                const double share = 0.05 + 0.8 * (fr / 25.0) * 0.5;
                for (size_t i = 0; i < favorite.sequence.notes.size(); ++i)
                {
                    const auto& n = favorite.sequence.notes[i];
                    const int b = (int) (n.startTick / bar);
                    if (n.locked || (b < (int) locks.size() && locks[(size_t) b])) continue;
                    const bool structural = (n.startTick % bar) == 0 || n.role == NoteRole::Target || n.role == NoteRole::Cadence;
                    if (! structural && rng.chance (share)) idx.push_back ((int) i);
                }
                if (idx.empty() && ! favorite.sequence.notes.empty()) idx.push_back ((int) (favorite.sequence.notes.size() / 2));
                s = SoloComposer::regenerateNotes (favorite.sequence, p, vseed, idx);
                v.name = "Variación cercana " + std::to_string (k + 1);
                break;
            }
            case FreedomBand::Identity:
            {
                // 26-55: mismo motivo y semilla temática; se regenera una parte de los compases (más cuanta más libertad).
                p.fidelity = 0.8; p.freedom = 0.3 + 0.3 * (fr - 25) / 30.0;
                std::vector<bool> keep = locks;
                const double share = 0.3 + 0.6 * (fr - 25) / 30.0;
                int changed = 0;
                for (int b = 0; b < bars; ++b) if (! keep[(size_t) b]) { if (rng.chance (share)) ++changed; else keep[(size_t) b] = true; }
                if (changed == 0) for (int b = bars - 1; b >= 0; --b) if (! locks[(size_t) b]) { keep[(size_t) b] = false; break; }
                s = SoloComposer::regenerate (favorite.sequence, p, vseed, keep);
                v.name = "Variación " + std::to_string (k + 1) + " (misma identidad)";
                break;
            }
            case FreedomBand::NewPhrases:
            {
                // 56-80: frases nuevas con elementos del motivo (fragmento, aumentación, secuencia).
                const int mode = rng.range (0, 2);
                SeedMotif m = mode == 0 ? fragmentMotif (id.motif) : mode == 1 ? augmentMotif (fragmentMotif (id.motif)) : sustainMotif (id.motif);
                p.seedMotifs = { m };
                if (! id.secondary.notes.empty() && rng.chance (0.5)) p.seedMotifs.push_back (id.secondary);
                p.fidelity = 0.55; p.freedom = 0.6 + 0.25 * (fr - 55) / 25.0;
                p.complexity = std::min (1.0, p.complexity + 0.1);
                s = SoloComposer::regenerate (favorite.sequence, p, vseed, locks);
                v.name = "Variación " + std::to_string (k + 1) + " (frases nuevas)";
                break;
            }
            case FreedomBand::Experimental:
            {
                // 81-100: exploración amplia con restricciones (armonía, registro y bloqueos siguen mandando).
                p.motifSeed = vseed * 3ull + 5ull;
                p.seedMotifs = fr >= 92 ? std::vector<SeedMotif> {} : std::vector<SeedMotif> { widenMotif (id.motif, rng.chance (0.5)) };
                p.fidelity = 0.3; p.freedom = 0.9;
                p.complexity = std::min (1.0, p.complexity + 0.2 + 0.2 * (fr - 80) / 20.0);
                p.density = std::clamp (p.density + (rng.unit() - 0.5) * 0.3, 0.1, 1.0);
                if (! chorus && rng.chance (0.5)) p.style = (Style) rng.range (0, kNumStyles - 1);
                s = SoloComposer::regenerate (favorite.sequence, p, vseed, locks);
                v.name = "Variación " + std::to_string (k + 1) + " (experimental)";
                break;
            }
        }
        if (s.notes.empty()) s = favorite.sequence;
        restoreLocked (s, favorite.sequence, locks);
        if (chorus)
        {
            ChorusShapeOptions o; o.character = brief.character; o.mirrorHalves = false; // no se pisa la segunda mitad en variaciones
            s = shapeChorus (s, p, p.progression, o, locks);
        }
        v.sequence = s;
        v.description = std::string (freedomBandName (band)) + " (" + std::to_string ((int) std::lround (fr)) + " %)";
        v.similarityToIdentity = melodySimilarity (s, favorite.sequence).overall; // aquí: parecido con la favorita
        out.push_back (v);
    }
    return out;
}

// ---------------------------------------------------------------- estado del proyecto
int MelodyProjectState::strategyOf (uint32_t variationId) const
{
    for (const auto& pr : variationStrategy) if (pr.first == variationId) return pr.second;
    return -1;
}

std::string serializeSeedMotif (const SeedMotif& m)
{
    std::ostringstream o;
    o << m.lengthTicks << " " << m.firstPitch << " " << m.notes.size();
    for (const auto& n : m.notes) o << " " << n.onset << " " << n.length << " " << n.interval;
    return o.str();
}

bool parseSeedMotif (const std::string& text, SeedMotif& out)
{
    std::istringstream v (text);
    SeedMotif m; size_t count = 0;
    v >> m.lengthTicks >> m.firstPitch >> count;
    if (v.fail() || count > 256 || m.lengthTicks < 0 || m.firstPitch < -1 || m.firstPitch > 127) return false;
    for (size_t k = 0; k < count; ++k)
    {
        SeedMotifNote n;
        v >> n.onset >> n.length >> n.interval;
        if (v.fail() || n.onset < 0 || n.length <= 0 || std::abs (n.interval) > 48) return false;
        m.notes.push_back (n);
    }
    out = m;
    return true;
}

std::string serializeMelodyState (const MelodyProjectState& m)
{
    std::ostringstream o;
    o << "mel.active=" << (m.active ? 1 : 0) << "\n";
    o << "mel.brief=" << (int) m.brief.type << " " << (int) m.brief.character << " " << m.brief.freedom << " " << (int) m.brief.instrument << " "
      << (int) m.brief.baseStyle << " " << m.brief.expression << " " << m.brief.seed << "\n";
    o << "mel.motifSeed=" << m.identity.motifSeed << "\n";
    if (! m.identity.motif.notes.empty()) o << "mel.motif=" << serializeSeedMotif (m.identity.motif) << "\n";
    if (! m.identity.secondary.notes.empty()) o << "mel.motif2=" << serializeSeedMotif (m.identity.secondary) << "\n";
    std::string d = m.identity.description; for (auto& c : d) if (c == '\n' || c == '\r') c = ' ';
    o << "mel.desc=" << d << "\n";
    o << "mel.diversity=" << m.diversity << "\n";
    for (const auto& pr : m.variationStrategy) o << "mel.var=" << pr.first << " " << pr.second << "\n";
    return o.str();
}

bool deserializeMelodyState (const std::string& text, MelodyProjectState& out, std::string& error)
{
    MelodyProjectState m;
    std::istringstream is (text); std::string line; bool any = false;
    while (std::getline (is, line))
    {
        if (! line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find ('=');
        if (eq == std::string::npos || line.rfind ("mel.", 0) != 0) continue;
        const std::string k = line.substr (4, eq - 4), v = line.substr (eq + 1);
        any = true;
        std::istringstream vs (v);
        if (k == "active") m.active = std::atoi (v.c_str()) != 0;
        else if (k == "brief")
        {
            int t = 0, c = 0, inst = 0, st = 0; double f = 35, ex = 65; uint64_t seed = 1;
            vs >> t >> c >> f >> inst >> st >> ex >> seed;
            if (vs.fail()) { error = "Brief de melodía inválido"; return false; }
            m.brief.type = (MelodyType) std::clamp (t, 0, kNumMelodyTypes - 1);
            m.brief.character = (ChorusCharacter) std::clamp (c, 0, 2);
            m.brief.freedom = std::clamp (f, 0.0, 100.0);
            m.brief.instrument = (InstrumentId) std::clamp (inst, 0, kNumInstruments - 1);
            m.brief.baseStyle = (Style) std::clamp (st, 0, kNumStyles - 1);
            m.brief.expression = std::clamp (ex, 0.0, 100.0);
            m.brief.seed = seed;
        }
        else if (k == "motifSeed") m.identity.motifSeed = std::strtoull (v.c_str(), nullptr, 10);
        else if (k == "motif") { if (! parseSeedMotif (v, m.identity.motif)) { error = "Motivo de identidad inválido"; return false; } }
        else if (k == "motif2") { if (! parseSeedMotif (v, m.identity.secondary)) { error = "Motivo secundario inválido"; return false; } }
        else if (k == "desc") m.identity.description = v;
        else if (k == "diversity") m.diversity = std::clamp (std::atof (v.c_str()), 0.0, 1.0);
        else if (k == "var") { unsigned long id = 0; int st = 0; vs >> id >> st; if (! vs.fail() && m.variationStrategy.size() < 256) m.variationStrategy.push_back ({ (uint32_t) id, std::clamp (st, 0, kNumStrategies - 1) }); }
    }
    if (! any) { error = "No hay estado de melodía"; return false; }
    out = m;
    return true;
}

Sequence MelodyExplorer::regenerateRegion (const MelodyContext& ctx, const MelodyProposal& p, int fromBar, int toBar,
                                           uint64_t seed, const std::vector<bool>& lockedBars)
{
    (void) ctx;
    return SoloComposer::regenerate (p.sequence, p.params, seed, lockedBars, fromBar, toBar);
}
} // namespace solo
