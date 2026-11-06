// RhythmProposal (V4-E). Ver RhythmProposal.h.
#include "solo/RhythmProposal.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace solo
{
RhythmPattern extractRhythm (const VoiceAnalysis& va, double bpm, TimeSignature ts, const RhythmExtractOptions& o)
{
    RhythmPattern p;
    p.timeSignature = ts;
    p.bpm = std::clamp (bpm, 20.0, 400.0);
    p.startBar = std::max (0, o.startBar);
    p.gridTicks = std::max (40, o.gridTicks);
    if (va.notes.empty()) { p.warnings.push_back ("La toma no contiene ataques"); return p; }
    const double ticksPerSec = p.bpm / 60.0 * kPPQ;
    const double v0 = o.voiceStartSec >= 0 ? o.voiceStartSec : va.notes.front().startSec;
    const int64_t barT = ts.ticksPerBar();
    const int64_t maxTicks = (int64_t) std::max (1, o.maxBars) * barT;
    double maxLevel = -120, devSum = 0; int nearGrid = 0, n = 0;
    for (const auto& vn : va.notes) maxLevel = std::max (maxLevel, vn.levelDb);
    for (const auto& vn : va.notes)
    {
        const double tick = (vn.startSec - v0) * ticksPerSec;
        if (tick < -p.gridTicks / 2.0) continue;
        const double qt = std::round (tick / (double) p.gridTicks) * (double) p.gridTicks;
        const double devTicks = std::abs (tick - qt);
        devSum += devTicks / ticksPerSec * 1000.0; ++n;
        if (devTicks <= p.gridTicks / 4.0) ++nearGrid;
        RhythmEvent e;
        e.onset = (int64_t) std::llround (tick + std::clamp (o.quantize, 0.0, 1.0) * (qt - tick));
        if (e.onset < 0) e.onset = 0;
        if (e.onset >= maxTicks) { p.warnings.push_back ("La toma es más larga que el máximo de compases; se recorta"); break; }
        const double endTick = (vn.endSec - v0) * ticksPerSec;
        const double qe = std::round (endTick / (double) p.gridTicks) * (double) p.gridTicks;
        e.length = std::max<int64_t> (p.gridTicks / 2, (int64_t) std::llround (endTick + std::clamp (o.quantize, 0.0, 1.0) * (qe - endTick)) - e.onset);
        e.accent = std::clamp (1.0 + (vn.levelDb - maxLevel) / 24.0, 0.2, 1.0);
        e.pitched = vn.pitched;
        if (! p.events.empty() && e.onset <= p.events.back().onset) continue; // dos ataques en la misma casilla: uno
        p.events.push_back (e);
    }
    for (size_t i = 0; i + 1 < p.events.size(); ++i) p.events[i].length = std::min (p.events[i].length, p.events[i + 1].onset - p.events[i].onset);
    if (p.events.empty()) { p.warnings.push_back ("No quedó ningún ataque dentro del rango"); return p; }
    p.bars = std::clamp ((int) (p.events.back().onset / barT) + 1, 1, std::max (1, o.maxBars));
    p.events.back().length = std::min (p.events.back().length, (int64_t) p.bars * barT - p.events.back().onset);
    p.gridFit = n > 0 ? (double) nearGrid / n : 0;
    p.meanDeviationMs = n > 0 ? devSum / n : 0;
    if (p.gridFit < 0.5) p.warnings.push_back ("El tarareo no va muy a tiempo con el beat: revisa el patrón antes de usarlo");
    return p;
}

std::string RhythmPattern::toText() const
{
    const int64_t barT = timeSignature.ticksPerBar(), cell = kPPQ / 4;
    const int cellsPerBar = (int) (barT / cell);
    std::string out;
    for (int b = 0; b < bars; ++b)
    {
        std::string line ((size_t) cellsPerBar, '.');
        for (const auto& e : events)
        {
            const int64_t s = e.onset - (int64_t) b * barT;
            if (s < 0 || s >= barT) continue;
            const int c0 = (int) (s / cell);
            line[(size_t) c0] = e.accent >= 0.85 ? 'X' : 'x';
            const int c1 = (int) std::min<int64_t> (cellsPerBar, (s + e.length + cell / 2) / cell);
            for (int c = c0 + 1; c < c1; ++c) if (line[(size_t) c] == '.') line[(size_t) c] = '-';
        }
        if (b) out += '\n';
        out += line;
    }
    return out;
}

bool RhythmPattern::fromText (const std::string& text, RhythmPattern& io, std::string& error)
{
    const int64_t barT = io.timeSignature.ticksPerBar(), cell = kPPQ / 4;
    const int cellsPerBar = (int) (barT / cell);
    std::vector<RhythmEvent> events;
    std::istringstream is (text); std::string line; int bar = 0;
    while (std::getline (is, line))
    {
        std::string clean;
        for (char c : line) if (c == 'x' || c == 'X' || c == '-' || c == '.') clean += c;
        if (clean.empty()) continue;
        if ((int) clean.size() != cellsPerBar) { error = "Cada línea debe tener " + std::to_string (cellsPerBar) + " casillas (compás " + std::to_string (bar + 1) + ")"; return false; }
        for (int c = 0; c < cellsPerBar; ++c)
        {
            if (clean[(size_t) c] != 'x' && clean[(size_t) c] != 'X') continue;
            RhythmEvent e;
            e.onset = (int64_t) bar * barT + (int64_t) c * cell;
            int len = 1;
            while (c + len < cellsPerBar && clean[(size_t) (c + len)] == '-') ++len;
            e.length = (int64_t) len * cell;
            e.accent = clean[(size_t) c] == 'X' ? 1.0 : 0.7;
            events.push_back (e);
        }
        ++bar;
        if (bar > 64) { error = "Demasiados compases"; return false; }
    }
    if (events.empty()) { error = "El patrón no tiene ataques"; return false; }
    io.events = events;
    io.bars = bar;
    io.gridTicks = (int) cell;
    io.gridFit = 1.0;
    io.warnings.clear();
    return true;
}

RhythmProposalResult proposeFromRhythm (const RhythmPattern& pattern, const ComposerParams& params, uint64_t seed)
{
    RhythmProposalResult r;
    if (pattern.empty()) { r.warnings.push_back ("El patrón está vacío"); return r; }
    // Reconstruye una "toma" sin altura a partir del patrón y deja que humToSolo (solo ritmo) elija las notas.
    VoiceAnalysis va;
    const double ticksPerSec = params.bpm / 60.0 * kPPQ;
    va.pitchReliable = false;
    for (const auto& e : pattern.events)
    {
        VoiceNote vn;
        vn.startSec = (double) e.onset / ticksPerSec;
        vn.endSec = (double) (e.onset + e.length) / ticksPerSec;
        vn.pitched = false;
        vn.levelDb = -24.0 * (1.0 - e.accent);
        vn.confidence = 0;
        va.notes.push_back (vn);
    }
    va.durationSec = va.notes.back().endSec;
    HumToSoloOptions o;
    o.mode = HumMode::Correct;
    o.rhythmOnly = true;
    o.quantize = 0.0; // ya está en la rejilla
    o.startBar = pattern.startBar;
    o.voiceStartSec = 0.0;
    ComposerParams p = params;
    p.seed = seed;
    const auto res = humToSolo (va, p, o);
    r.sequence = res.sequence;
    r.notesFromPattern = (int) res.sequence.notes.size();
    r.warnings = res.warnings;
    if (r.sequence.notes.empty()) r.warnings.push_back ("El patrón no cabe en los compases del solo (revisa el compás de inicio)");
    return r;
}

std::string serializeRhythmPattern (const RhythmPattern& p)
{
    std::ostringstream o;
    o << "rhy.meta=" << p.bars << " " << p.timeSignature.numerator << " " << p.bpm << " " << p.startBar << " " << p.gridTicks << " " << p.gridFit << "\n";
    for (const auto& e : p.events) o << "rhy.ev=" << e.onset << " " << e.length << " " << e.accent << " " << (e.pitched ? 1 : 0) << "\n";
    return o.str();
}

bool deserializeRhythmPattern (const std::string& text, RhythmPattern& out, std::string& error)
{
    RhythmPattern p; bool any = false;
    std::istringstream is (text); std::string line;
    while (std::getline (is, line))
    {
        if (! line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find ('=');
        if (eq == std::string::npos || line.rfind ("rhy.", 0) != 0) continue;
        const std::string k = line.substr (4, eq - 4);
        std::istringstream v (line.substr (eq + 1));
        any = true;
        if (k == "meta")
        {
            int num = 4; v >> p.bars >> num >> p.bpm >> p.startBar >> p.gridTicks >> p.gridFit;
            if (v.fail() || p.bars < 0 || p.bars > 64 || (num != 3 && num != 4)) { error = "Patrón rítmico inválido"; return false; }
            p.timeSignature.numerator = num;
        }
        else if (k == "ev")
        {
            RhythmEvent e; int pitched = 0; v >> e.onset >> e.length >> e.accent >> pitched;
            if (v.fail() || e.onset < 0 || e.length <= 0) { error = "Evento rítmico inválido"; return false; }
            e.pitched = pitched != 0;
            if (p.events.size() < 4096) p.events.push_back (e);
        }
    }
    if (! any) { error = "No hay patrón rítmico"; return false; }
    out = p;
    return true;
}
} // namespace solo
