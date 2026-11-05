#include "solo/Analysis.h"
#include "solo/Dsp.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace solo
{
std::string SoloMetrics::toString() const
{
    std::ostringstream o;
    o.imbue (std::locale::classic());
    o.precision (3);
    o << "notas=" << notes << " n/s=" << notesPerSecond << " silencio=" << restRatio << " acordeFuerte=" << strongChordTone
      << " grados=" << stepwiseRatio << " saltos=" << leapRatio << " repMotivo=" << motifRecurrence
      << " repRitmo=" << rhythmRecurrence << " frases=" << phrases << " rango=" << pitchMin << "-" << pitchMax
      << " resuelve=" << (resolvesToStable ? "sí" : "no") << " bends=" << bends << " vib=" << vibratos
      << " slides=" << slides << " legato=" << legatos << " pm=" << palmMutes << " desvVel=" << velocityStdDev;
    return o.str();
}

SoloMetrics analyseSolo (const Sequence& s, const ComposerParams& p)
{
    SoloMetrics m;
    m.notes = (int) s.notes.size();
    if (s.notes.empty()) return m;
    const int64_t beat = s.timeSignature.ticksPerBeat(), bar = s.timeSignature.ticksPerBar();
    const double bpm = p.bpm > 0 ? p.bpm : 120.0;
    const double seconds = (double) s.lengthTicks() / kPPQ * 60.0 / bpm;
    m.notesPerSecond = (double) m.notes / std::max (1e-6, seconds);

    int64_t sounding = 0;
    int strong = 0, strongTone = 0, steps = 0, leaps = 0, repeats = 0, intervals = 0, offGrid = 0;
    double sumPitch = 0, sumVel = 0, sumVel2 = 0;
    m.pitchMin = 127;
    m.pitchMax = 0;
    m.phrases = 1;
    const int64_t strongStep = s.timeSignature.numerator == 4 ? 2 * beat : bar;
    std::vector<int> iv;
    for (size_t i = 0; i < s.notes.size(); ++i)
    {
        const auto& n = s.notes[i];
        sounding += n.lengthTicks;
        const int tp = n.targetPitch();
        m.pitchMin = std::min (m.pitchMin, n.pitch);
        m.pitchMax = std::max (m.pitchMax, tp);
        sumPitch += tp;
        sumVel += n.velocity;
        sumVel2 += (double) n.velocity * n.velocity;
        if (n.startTick % 80 != 0) ++offGrid;
        if (n.startTick % strongStep == 0)
        {
            ++strong;
            const auto& ch = p.progression.chordAtBar ((int) (n.startTick / bar));
            strongTone += ch.containsPitchClass (tp) ? 1 : 0;
        }
        if (n.expr.bendType != BendType::None) ++m.bends;
        if (n.expr.vibratoDepth > 0) ++m.vibratos;
        if (n.expr.slideFrom != 0) ++m.slides;
        if (n.expr.legato) ++m.legatos;
        if (n.expr.palmMute) ++m.palmMutes;
        if (i > 0)
        {
            const auto& pr = s.notes[i - 1];
            const int d = n.pitch - pr.targetPitch();
            iv.push_back (d);
            ++intervals;
            const int a = std::abs (d);
            if (a == 0) ++repeats;
            else if (a <= 2) ++steps;
            else if (a > 5) ++leaps;
            if (n.startTick - pr.endTick() >= beat / 2) ++m.phrases;
        }
    }
    m.restRatio = 1.0 - std::min (1.0, (double) sounding / (double) std::max<int64_t> (1, s.lengthTicks()));
    m.strongChordTone = strong > 0 ? (double) strongTone / strong : 1.0;
    m.stepwiseRatio = intervals > 0 ? (double) steps / intervals : 0;
    m.leapRatio = intervals > 0 ? (double) leaps / intervals : 0;
    m.repeatedPitchRatio = intervals > 0 ? (double) repeats / intervals : 0;
    m.meanPitch = sumPitch / m.notes;
    const double mv = sumVel / m.notes;
    m.velocityStdDev = std::sqrt (std::max (0.0, sumVel2 / m.notes - mv * mv));
    m.offGridRatio = (double) offGrid / m.notes;

    // Recurrencia de motivos: 4-gramas de intervalos.
    if (iv.size() >= 4)
    {
        std::map<std::vector<int>, int> grams;
        for (size_t i = 0; i + 4 <= iv.size(); ++i) grams[std::vector<int> (iv.begin() + (long) i, iv.begin() + (long) i + 4)]++;
        int recurring = 0, total = 0;
        for (const auto& [g, c] : grams) { total += c; if (c >= 2) recurring += c; }
        m.motifRecurrence = total > 0 ? (double) recurring / total : 0;
    }
    // Recurrencia rítmica por compás.
    {
        std::map<std::string, int> pats;
        std::vector<std::string> perBar ((size_t) s.bars);
        for (const auto& n : s.notes)
        {
            const int b = (int) (n.startTick / bar);
            if (b >= 0 && b < s.bars) perBar[(size_t) b] += std::to_string ((n.startTick % bar) / 80) + ",";
        }
        int nonEmpty = 0, rep = 0;
        for (const auto& pt : perBar) if (! pt.empty()) { pats[pt]++; ++nonEmpty; }
        for (const auto& pt : perBar) if (! pt.empty() && pats[pt] >= 2) ++rep;
        m.rhythmRecurrence = nonEmpty > 0 ? (double) rep / nonEmpty : 0;
    }
    const auto& last = p.progression.chordAtBar (s.bars - 1);
    for (int pc : last.stablePitchClasses()) m.resolvesToStable |= (s.notes.back().targetPitch() % 12) == pc;
    return m;
}

// ---------------------------------------------------------------- sonoridad (BS.1770 aproximada)
double integratedLoudness (const std::vector<float>& left, const std::vector<float>& right, double sr)
{
    const size_t n = std::min (left.size(), right.size());
    if (n == 0 || sr <= 0) return -120.0;
    dsp::Biquad shL, shR, hpL, hpR;
    shL.highShelf (sr, 1500.0, 4.0); shR = shL;
    hpL.highpass (sr, 38.0, 0.5); hpR = hpL;
    const size_t block = (size_t) (0.4 * sr), hop = (size_t) (0.1 * sr);
    std::vector<double> sq (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double l = hpL.process (shL.process (left[i])), r = hpR.process (shR.process (right[i]));
        sq[i] = l * l + r * r;
    }
    // sumas acumuladas para los bloques
    std::vector<double> cum (n + 1, 0.0);
    for (size_t i = 0; i < n; ++i) cum[i + 1] = cum[i] + sq[i];
    std::vector<double> z;
    for (size_t s = 0; s + block <= n; s += hop) z.push_back ((cum[s + block] - cum[s]) / (double) block);
    if (z.empty()) z.push_back (cum[n] / (double) n);
    auto lufs = [] (double ms) { return -0.691 + 10.0 * std::log10 (std::max (ms, 1e-12)); };
    double sum = 0;
    int cnt = 0;
    for (double v : z) if (lufs (v) > -70.0) { sum += v; ++cnt; }
    if (cnt == 0) return -120.0;
    const double rel = lufs (sum / cnt) - 10.0;
    sum = 0;
    cnt = 0;
    for (double v : z) if (lufs (v) > -70.0 && lufs (v) > rel) { sum += v; ++cnt; }
    return cnt > 0 ? lufs (sum / cnt) : -120.0;
}

double normaliseLoudness (std::vector<float>& left, std::vector<float>& right, double sr, double target, float peakCeiling)
{
    const double l = integratedLoudness (left, right, sr);
    if (l <= -100.0) return 1.0;
    double g = std::pow (10.0, (target - l) / 20.0);
    float peak = 0;
    for (size_t i = 0; i < left.size(); ++i) peak = std::max ({ peak, std::abs (left[i]), std::abs (right[i]) });
    if (peak * g > peakCeiling) g = peakCeiling / std::max (1e-9f, peak); // nunca saturar: queda por debajo del objetivo
    for (auto& x : left) x = (float) (x * g);
    for (auto& x : right) x = (float) (x * g);
    return g;
}

} // namespace solo
