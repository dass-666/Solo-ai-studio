// Beat Analyzer (V4-A). Ver BeatAnalyzer.h para las reglas de diseño.
#include "solo/BeatAnalyzer.h"
#include "solo/Dsp.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <numeric>
#include <sstream>

namespace solo
{
namespace
{
constexpr double kRate = 22050.0; // tasa interna de análisis
constexpr int kFrame = 2048;      // ventana FFT (~93 ms a 22.05 kHz, resolución ~10.8 Hz)
constexpr int kHop = 256;         // salto (~11.6 ms)
constexpr double kPi = 3.14159265358979323846;

double clamp01 (double v) { return std::max (0.0, std::min (1.0, v)); }
int mod12 (int v) { return ((v % 12) + 12) % 12; }
double dbOf (double lin) { return 20.0 * std::log10 (std::max (lin, 1e-9)); }

std::vector<float> resampleTo (const std::vector<float>& in, double srIn, double srOut)
{
    if (std::abs (srIn - srOut) < 1e-6 || in.empty()) return in;
    std::vector<float> filtered = in;
    if (srIn > srOut)
    {
        dsp::Biquad a, b;
        a.lowpass (srIn, srOut * 0.42, 0.54);
        b.lowpass (srIn, srOut * 0.42, 1.31);
        for (auto& x : filtered) x = b.process (a.process (x));
    }
    const double ratio = srIn / srOut;
    const size_t n = (size_t) std::floor ((double) in.size() / ratio);
    std::vector<float> out (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double pos = (double) i * ratio;
        const size_t k = (size_t) pos;
        const double fr = pos - (double) k;
        const float x0 = filtered[std::min (k, filtered.size() - 1)];
        const float x1 = filtered[std::min (k + 1, filtered.size() - 1)];
        out[i] = (float) (x0 + (x1 - x0) * fr);
    }
    return out;
}

struct Frames
{
    int count = 0;
    std::vector<float> flux;          // flujo espectral (ataques), por frame
    std::vector<float> lowFlux;       // flujo en graves (< 200 Hz): bombo / pulso fuerte
    std::vector<float> rms;           // energía lineal por frame
    std::vector<float> chroma;        // 12 x count (normalizado por frame, 0 si no hay energía)
    std::vector<float> chromaEnergy;  // energía armónica por frame (picos espectrales)
    std::vector<float> peakRatio;     // energía en picos espectrales / energía total
};

Frames computeFrames (const std::vector<float>& x)
{
    Frames f;
    if ((int) x.size() < kFrame) return f;
    f.count = (int) ((x.size() - kFrame) / kHop) + 1;
    f.flux.assign (f.count, 0); f.lowFlux.assign (f.count, 0); f.rms.assign (f.count, 0);
    f.chroma.assign (12 * f.count, 0); f.chromaEnergy.assign (f.count, 0); f.peakRatio.assign (f.count, 0);

    std::vector<float> win (kFrame);
    for (int i = 0; i < kFrame; ++i) win[i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / (kFrame - 1)));
    const int bins = kFrame / 2;
    std::vector<float> re (kFrame), im (kFrame), mag (bins), prevLog (bins, 0.0f), logMag (bins);
    std::vector<int> binPc (bins, -1);
    const int lowBin = (int) (200.0 * kFrame / kRate);
    for (int b = 1; b < bins; ++b)
    {
        const double hz = (double) b * kRate / kFrame;
        if (hz >= 60.0 && hz <= 4200.0) binPc[b] = mod12 ((int) std::lround (12.0 * std::log2 (hz / 440.0)) + 69);
    }
    const int maxFluxBin = std::min (bins - 1, (int) (10000.0 * kFrame / kRate));

    for (int t = 0; t < f.count; ++t)
    {
        const float* src = x.data() + (size_t) t * kHop;
        double e = 0;
        for (int i = 0; i < kFrame; ++i) { re[i] = src[i] * win[i]; im[i] = 0; e += (double) src[i] * src[i]; }
        f.rms[t] = (float) std::sqrt (e / kFrame);
        fftRadix2 (re, im);
        for (int b = 0; b < bins; ++b) { mag[b] = std::sqrt (re[b] * re[b] + im[b] * im[b]) / (kFrame * 0.25f); logMag[b] = std::log1p (mag[b] * 40.0f); }
        double flux = 0, lowF = 0;
        for (int b = 1; b <= maxFluxBin; ++b)
        {
            const float d = logMag[b] - prevLog[b];
            if (d > 0) { flux += d; if (b <= lowBin) lowF += d; }
        }
        f.flux[t] = (float) flux; f.lowFlux[t] = (float) lowF;
        prevLog = logMag;
        // Croma solo con picos espectrales (tonos sostenidos); el ruido ancho (caja, charles) apenas aporta.
        double total = 0, peaks = 0;
        float ch[12] = { 0 };
        for (int b = 2; b < bins - 2; ++b)
        {
            const double m2 = (double) mag[b] * mag[b];
            if ((double) b * kRate / kFrame > 60.0) total += m2;
            if (binPc[b] < 0) continue;
            if (mag[b] > mag[b - 1] && mag[b] >= mag[b + 1] && mag[b] > 2.0f * (mag[b - 2] + mag[b + 2]) * 0.5f + 1e-6f)
            {
                // Interpolación parabólica del pico para afinar la clase de altura.
                const float a = logMag[b - 1], c = logMag[b], d = logMag[b + 1];
                const float den = a - 2 * c + d;
                const double off = std::abs (den) > 1e-9f ? 0.5 * (a - d) / den : 0.0;
                const double hz = ((double) b + off) * kRate / kFrame;
                const int pc = mod12 ((int) std::lround (12.0 * std::log2 (hz / 440.0)) + 69);
                ch[pc] += (float) m2;
                peaks += m2;
            }
        }
        f.chromaEnergy[t] = (float) peaks;
        f.peakRatio[t] = total > 1e-12 ? (float) (peaks / total) : 0.0f;
        double s = 0;
        for (int k = 0; k < 12; ++k) s += ch[k];
        if (s > 1e-12)
            for (int k = 0; k < 12; ++k) f.chroma[(size_t) t * 12 + k] = (float) (ch[k] / s);
    }
    return f;
}

// Envolvente de ataques: flujo menos su media local, rectificado y normalizado.
std::vector<float> onsetEnvelope (const std::vector<float>& flux)
{
    const int n = (int) flux.size();
    std::vector<float> env (n, 0);
    const int half = (int) std::lround (0.5 * kRate / kHop); // ~0.5 s
    double mx = 0;
    for (int t = 0; t < n; ++t)
    {
        const int a = std::max (0, t - half), b = std::min (n - 1, t + half);
        double m = 0;
        for (int k = a; k <= b; ++k) m += flux[k];
        m /= (b - a + 1);
        env[t] = std::max (0.0f, flux[t] - (float) m);
        mx = std::max (mx, (double) env[t]);
    }
    if (mx > 0) for (auto& v : env) v = (float) (v / mx);
    return env;
}

double acfAt (const std::vector<double>& acf, double lag)
{
    const int k = (int) lag;
    if (k < 0 || k + 1 >= (int) acf.size()) return 0;
    const double fr = lag - k;
    return acf[k] + (acf[k + 1] - acf[k]) * fr;
}

struct TempoEstimate { double bpm = 0; double confidence = 0; std::vector<BpmCandidate> candidates; double periodFrames = 0; };

TempoEstimate estimateTempo (const std::vector<float>& envIn, const BeatAnalysisOptions& o)
{
    TempoEstimate te;
    const int n = (int) envIn.size();
    const double framesPerSec = kRate / kHop;
    // Suavizado triangular ±4 frames (±46 ms): una caja "atrasada" o una humanización no deben romper la
    // autocorrelación (la precisión del BPM se recupera después por regresión sobre los picos reales).
    std::vector<float> env (envIn.size(), 0.0f);
    {
        for (int t = 0; t < n; ++t)
        {
            float acc = 0, wsum = 0;
            for (int d = -4; d <= 4; ++d)
            {
                const float w = (float) (5 - std::abs (d));
                if (t + d >= 0 && t + d < n) { acc += w * envIn[t + d]; }
                wsum += w;
            }
            env[t] = acc / wsum;
        }
    }
    const int minLag = std::max (2, (int) std::floor (60.0 / o.maxBpm * framesPerSec));
    const int maxLag = (int) std::ceil (60.0 / o.minBpm * framesPerSec);
    if (n < 4 * minLag + 8) return te;
    const int maxNeeded = std::min (n - 2, maxLag * 4 + 2);
    std::vector<double> acf (maxNeeded + 1, 0.0);
    double e0 = 0;
    for (int t = 0; t < n; ++t) e0 += (double) env[t] * env[t];
    if (e0 <= 1e-12) return te;
    for (int lag = 0; lag <= maxNeeded; ++lag)
    {
        double s = 0;
        for (int t = 0; t + lag < n; ++t) s += (double) env[t] * env[t + lag];
        acf[lag] = s / e0;
    }
    // Puntuación en peine (lag, 2lag, 3lag, 4lag) para cada BPM candidato con paso fino.
    std::vector<std::pair<double, double>> scores; // bpm, score
    for (double bpm = o.minBpm; bpm <= o.maxBpm + 1e-9; bpm += 0.25)
    {
        const double lag = 60.0 / bpm * framesPerSec;
        double s = acfAt (acf, lag) + 0.6 * acfAt (acf, 2 * lag) + 0.4 * acfAt (acf, 3 * lag) + 0.3 * acfAt (acf, 4 * lag);
        // Si la mitad del periodo también es un pico fuerte, lo más probable es que el pulso sea el doble
        // de rápido (bombo en cada pulso y caja cada dos): se penaliza el "medio tempo".
        if (lag / 2 >= minLag) s -= 0.35 * acfAt (acf, lag / 2);
        // Preferencia suave por tempos "humanos" (90–150) para resolver la ambigüedad de octava.
        const double pref = std::exp (-0.5 * std::pow ((std::log2 (bpm / 120.0)) / 0.5, 2.0));
        s *= 0.75 + 0.25 * pref;
        scores.push_back ({ bpm, s });
    }
    // Máximos locales.
    std::vector<BpmCandidate> cands;
    for (size_t i = 1; i + 1 < scores.size(); ++i)
        if (scores[i].second > scores[i - 1].second && scores[i].second >= scores[i + 1].second && scores[i].second > 0)
            cands.push_back ({ scores[i].first, scores[i].second });
    if (cands.empty()) return te;
    std::sort (cands.begin(), cands.end(), [] (const BpmCandidate& a, const BpmCandidate& b) { return a.score > b.score; });
    // Agrupar candidatos casi iguales (±2 %).
    std::vector<BpmCandidate> merged;
    for (const auto& c : cands)
    {
        bool dup = false;
        for (const auto& m : merged) if (std::abs (c.bpm - m.bpm) / m.bpm < 0.02) { dup = true; break; }
        if (! dup) merged.push_back (c);
        if (merged.size() >= 6) break;
    }
    te.candidates = merged;
    te.bpm = merged.front().bpm;
    te.periodFrames = 60.0 / te.bpm * framesPerSec;
    // Confianza: prominencia del mejor frente al mejor candidato NO relacionado (ni octava ni 3:2),
    // y fuerza absoluta del pico de autocorrelación.
    double unrelated = 0;
    for (size_t i = 1; i < merged.size(); ++i)
    {
        const double r = merged[i].bpm / te.bpm;
        const double rel[] = { 0.5, 2.0, 1.5, 2.0 / 3.0, 3.0, 1.0 / 3.0, 4.0, 0.25 };
        bool related = false;
        for (double q : rel) if (std::abs (r / q - 1.0) < 0.06) related = true;
        if (! related) { unrelated = merged[i].score; break; }
    }
    const double prominence = merged.front().score > 0 ? clamp01 ((merged.front().score - unrelated) / merged.front().score) : 0;
    const double strength = clamp01 (acfAt (acf, te.periodFrames) / 0.35);
    te.confidence = clamp01 (0.6 * prominence + 0.4 * strength);
    // Octava casi empatada (mitad/doble con puntuación >= 85 % de la mejor): no afirmar confianza alta.
    for (size_t i = 1; i < merged.size(); ++i)
    {
        const double q = merged[i].bpm / te.bpm;
        if ((std::abs (q - 2.0) < 0.06 || std::abs (q - 0.5) < 0.03) && merged[i].score >= 0.85 * merged.front().score)
            te.confidence = std::min (te.confidence, 0.6);
    }
    return te;
}

std::vector<double> beatGrid (const std::vector<float>& env, const std::vector<float>& rms, double& periodFrames, int n)
{
    // Fase: desplazamiento (0..periodo) que maximiza la suma de ataques sobre la rejilla (tolerancia ±1 frame).
    auto at = [&] (double pos) {
        const int k = (int) std::lround (pos);
        double best = 0;
        for (int d = -1; d <= 1; ++d) if (k + d >= 0 && k + d < n) best = std::max (best, (double) env[k + d]);
        return best;
    };
    double bestScore = -1, bestPhase = 0;
    for (double ph = 0; ph < periodFrames; ph += 0.5)
    {
        double s = 0;
        for (double p = ph; p < n; p += periodFrames) s += at (p);
        if (s > bestScore) { bestScore = s; bestPhase = ph; }
    }
    // Afinar periodo y fase por regresión lineal sobre los picos de ataque cercanos a la rejilla
    // (el paso de 0.25 BPM de la búsqueda acumula deriva en archivos largos).
    for (int iter = 0; iter < 2; ++iter)
    {
        double sx = 0, sy = 0, sxx = 0, sxy = 0; int m = 0;
        double mx = 0;
        for (double p = bestPhase; p < n; p += periodFrames) mx = std::max (mx, at (p));
        int idx = 0;
        for (double p = bestPhase; p < n; p += periodFrames, ++idx)
        {
            const int k = (int) std::lround (p);
            int bestK = -1; double bestV = 0.2 * mx;
            for (int d = -3; d <= 3; ++d)
                if (k + d >= 0 && k + d < n && env[k + d] > bestV) { bestV = env[k + d]; bestK = k + d; }
            if (bestK < 0) continue;
            sx += idx; sy += bestK; sxx += (double) idx * idx; sxy += (double) idx * bestK; ++m;
        }
        if (m >= 4)
        {
            const double den = m * sxx - sx * sx;
            if (std::abs (den) > 1e-9)
            {
                const double slope = (m * sxy - sx * sy) / den;
                const double icpt = (sy - slope * sx) / m;
                if (std::abs (slope / periodFrames - 1.0) < 0.05) { periodFrames = slope; bestPhase = icpt; }
            }
        }
        while (bestPhase >= periodFrames) bestPhase -= periodFrames;
        // El flujo espectral adelanta el ataque 1-2 frames: un pulso hasta 3 frames (~35 ms) antes del inicio
        // es el golpe de la muestra 0 y se conserva (recortado a 0) en vez de perderse.
        while (bestPhase < -3.0) bestPhase += periodFrames;
        if (bestPhase >= periodFrames - 3.0) bestPhase -= periodFrames;
    }
    std::vector<double> beatsFrames;
    for (double p = bestPhase; p < n; p += periodFrames)
    {
        // Ajustar cada pulso al pico de ataque real cercano (±3 frames) si lo hay: tiempos exactos con swing.
        const int k = (int) std::lround (p);
        int bestK = -1; double bestV = 0;
        for (int d = -3; d <= 3; ++d) if (k + d >= 0 && k + d < n && env[k + d] > bestV) { bestV = env[k + d]; bestK = k + d; }
        double q = p;
        if (bestK >= 0 && bestV > 0.3 && (bestK == 0 || bestK == n - 1 || (env[bestK] >= env[bestK - 1] && env[bestK] >= env[bestK + 1]))) q = bestK;
        beatsFrames.push_back (std::max (0.0, q));
    }
    // Recortar pulsos iniciales en silencio (entrada sin señal): nivel por debajo de -60 dBFS alrededor del pulso.
    auto silentAround = [&] (double p) {
        const int a = std::max (0, (int) std::lround (p - periodFrames / 2)), b = std::min (n - 1, (int) std::lround (p + periodFrames / 2));
        float mxr = 0;
        for (int k = a; k <= b; ++k) mxr = std::max (mxr, rms[k]);
        return mxr < 1e-3f;
    };
    size_t first = 0;
    while (first + 1 < beatsFrames.size() && silentAround (beatsFrames[first])) ++first;
    if (first > 0) beatsFrames.erase (beatsFrames.begin(), beatsFrames.begin() + (long) first);
    std::vector<double> sec;
    for (double p : beatsFrames) sec.push_back (p * kHop / kRate);
    return sec;
}

// Perfiles de Krumhansl-Kessler.
const double kMajorProfile[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
const double kMinorProfile[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

double correlation (const double* a, const double* b, int n)
{
    double ma = 0, mb = 0;
    for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    double num = 0, da = 0, db = 0;
    for (int i = 0; i < n; ++i) { num += (a[i] - ma) * (b[i] - mb); da += (a[i] - ma) * (a[i] - ma); db += (b[i] - mb) * (b[i] - mb); }
    return (da > 1e-12 && db > 1e-12) ? num / std::sqrt (da * db) : 0.0;
}

std::vector<KeyCandidate> estimateKey (const double chroma[12])
{
    std::vector<KeyCandidate> out;
    for (int tonic = 0; tonic < 12; ++tonic)
        for (int m = 0; m < 2; ++m)
        {
            double prof[12];
            for (int i = 0; i < 12; ++i) prof[mod12 (i + tonic)] = (m == 0 ? kMajorProfile : kMinorProfile)[i];
            KeyCandidate kc;
            kc.key = Key { tonic, m == 0 ? Mode::Major : Mode::Minor };
            kc.score = correlation (chroma, prof, 12);
            out.push_back (kc);
        }
    std::sort (out.begin(), out.end(), [] (const KeyCandidate& a, const KeyCandidate& b) { return a.score > b.score; });
    return out;
}

struct ChordGuess { Chord chord; double confidence = 0; };

ChordGuess guessChord (const double chroma[12], const Key& key)
{
    const auto scale = key.scalePitchClasses();
    auto diatonic = [&] (const Chord& c) {
        for (int pc : c.pitchClasses()) if (std::find (scale.begin(), scale.end(), pc) == scale.end()) return false;
        return true;
    };
    double sum = 0;
    for (int i = 0; i < 12; ++i) sum += chroma[i];
    ChordGuess best, second;
    if (sum <= 1e-12) return best;
    for (int root = 0; root < 12; ++root)
        for (ChordQuality q : { ChordQuality::Major, ChordQuality::Minor })
        {
            Chord c { root, q };
            const int third = q == ChordQuality::Major ? 4 : 3;
            const double inChord = chroma[root] * 1.0 + chroma[mod12 (root + third)] * 0.9 + chroma[mod12 (root + 7)] * 0.8;
            const double wrongThird = chroma[mod12 (root + (q == ChordQuality::Major ? 3 : 4))];
            double s = (inChord - 0.6 * wrongThird) / sum;
            if (diatonic (c)) s += 0.05;
            if (s > best.confidence) { second = best; best = { c, s }; }
            else if (s > second.confidence) second = { c, s };
        }
    // Confianza: margen sobre la segunda opción y proporción de energía dentro del acorde.
    const double margin = clamp01 ((best.confidence - second.confidence) / 0.25);
    const double fit = clamp01 ((best.confidence - 0.35) / 0.5);
    best.confidence = clamp01 (0.55 * margin + 0.45 * fit);
    return best;
}

std::string confName (double c) { return c >= 0.7 ? "alta" : c >= 0.4 ? "media" : "baja"; }
} // namespace

// ---------------------------------------------------------------- FFT
void fftRadix2 (std::vector<float>& re, std::vector<float>& im)
{
    const size_t n = re.size();
    if (n < 2 || (n & (n - 1)) != 0 || im.size() != n) return;
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap (re[i], re[j]); std::swap (im[i], im[j]); }
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPi / (double) len;
        const float wr = (float) std::cos (ang), wi = (float) std::sin (ang);
        for (size_t i = 0; i < n; i += len)
        {
            float cr = 1, ci = 0;
            for (size_t k = 0; k < len / 2; ++k)
            {
                const size_t a = i + k, b = i + k + len / 2;
                const float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;
                const float ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
}

// ---------------------------------------------------------------- HarmonyTimeline
Chord HarmonyTimeline::chordAtBar (int bar) const
{
    Chord c { key.tonic, key.mode == Mode::Major ? ChordQuality::Major : ChordQuality::Minor };
    for (const auto& s : segments) if (s.startBar <= bar) c = s.chord;
    return c;
}
double HarmonyTimeline::chordConfidenceAtBar (int bar) const
{
    double c = 0;
    for (const auto& s : segments) if (s.startBar <= bar) c = s.confidence;
    return c;
}
void HarmonyTimeline::setChord (int fromBar, int toBar, const Chord& c, Provenance p)
{
    fromBar = std::max (0, fromBar); toBar = std::min (bars - 1, toBar);
    if (toBar < fromBar) return;
    std::vector<ChordSegment> out;
    for (const auto& s : segments)
    {
        const int a = s.startBar, b = s.startBar + s.lengthBars - 1;
        if (b < fromBar || a > toBar) { out.push_back (s); continue; }
        if (a < fromBar) { ChordSegment l = s; l.lengthBars = fromBar - a; out.push_back (l); }
        if (b > toBar) { ChordSegment r = s; r.startBar = toBar + 1; r.lengthBars = b - toBar; out.push_back (r); }
    }
    ChordSegment n; n.startBar = fromBar; n.lengthBars = toBar - fromBar + 1; n.chord = c; n.confidence = p == Provenance::Confirmed ? 1.0 : 0.5; n.provenance = p;
    out.push_back (n);
    std::sort (out.begin(), out.end(), [] (const ChordSegment& x, const ChordSegment& y) { return x.startBar < y.startBar; });
    segments = out;
}
void HarmonyTimeline::setKey (const Key& k, Provenance p) { key = k; keyProvenance = p; if (p == Provenance::Confirmed) keyConfidence = 1.0; }
void HarmonyTimeline::setBpm (double b, Provenance p) { bpm = b; bpmProvenance = p; if (p == Provenance::Confirmed) bpmConfidence = 1.0; }
Progression HarmonyTimeline::toProgression() const
{
    Progression p;
    for (int b = 0; b < bars; ++b) p.bars.push_back (chordAtBar (b));
    return p;
}
std::vector<int> HarmonyTimeline::changeBars() const
{
    std::vector<int> out;
    for (int b = 1; b < bars; ++b) if (! (chordAtBar (b) == chordAtBar (b - 1))) out.push_back (b);
    return out;
}
std::string HarmonyTimeline::toString() const
{
    std::string s;
    for (int b = 0; b < bars; ++b) { if (b) s += " | "; s += chordAtBar (b).toString (key.prefersFlats()); }
    return s;
}
HarmonyTimeline HarmonyTimeline::fromManual (const Key& k, const Progression& p, double bpm, int beatsPerBar)
{
    HarmonyTimeline h;
    h.key = k; h.keyConfidence = 1.0; h.keyProvenance = Provenance::Confirmed;
    h.bpm = bpm; h.bpmConfidence = 1.0; h.bpmProvenance = Provenance::Confirmed;
    h.beatsPerBar = beatsPerBar; h.bars = (int) p.bars.size();
    for (int b = 0; b < h.bars; ++b)
    {
        if (! h.segments.empty() && h.segments.back().chord == p.bars[(size_t) b]) { ++h.segments.back().lengthBars; continue; }
        ChordSegment s; s.startBar = b; s.chord = p.bars[(size_t) b]; s.confidence = 1.0; s.provenance = Provenance::Confirmed;
        h.segments.push_back (s);
    }
    return h;
}

// ---------------------------------------------------------------- análisis
BeatAnalysisResult analyseBeat (const MonoAudio& audio, const BeatAnalysisOptions& o)
{
    BeatAnalysisResult r;
    r.beatsPerBar = std::max (1, o.beatsPerBar);
    if (audio.sampleRate < 8000.0 || audio.sampleRate > 384000.0) { r.error = "Frecuencia de muestreo no válida"; return r; }
    if (audio.samples.empty()) { r.error = "El archivo no contiene audio"; return r; }
    r.sampleRate = audio.sampleRate; r.durationSec = audio.durationSec(); r.sourceChannels = audio.sourceChannels;
    if (r.durationSec > o.maxDurationSec) { r.error = "El beat dura más de " + std::to_string ((int) o.maxDurationSec) + " s; recórtalo antes de importarlo"; return r; }
    if (o.minBpm < 20 || o.maxBpm > 400 || o.minBpm >= o.maxBpm) { r.error = "Rango de BPM no válido"; return r; }

    double peak = 0, e = 0;
    for (float v : audio.samples) { peak = std::max (peak, (double) std::abs (v)); e += (double) v * v; }
    r.peakDb = dbOf (peak); r.rmsDb = dbOf (std::sqrt (e / (double) audio.samples.size()));
    r.ok = true;
    if (r.peakDb < -60.0)
    {
        r.silent = true;
        r.warnings.push_back ("El archivo está en silencio (pico por debajo de -60 dBFS): no hay nada que analizar");
        return r;
    }
    if (r.durationSec < 2.0) r.warnings.push_back ("El beat es muy corto (< 2 s): el tempo y la tonalidad serán poco fiables");

    auto x = resampleTo (audio.samples, audio.sampleRate, kRate);
    // Media ventana de ceros al principio: así el frame t queda centrado en la muestra t*hop (los tiempos de
    // ataque salen exactos y un golpe en la muestra 0 también se ve).
    x.insert (x.begin(), (size_t) (kFrame / 2), 0.0f);
    const Frames f = computeFrames (x);
    if (f.count < 8) { r.warnings.push_back ("Demasiado corto para estimar el tempo"); return r; }

    // ---- tempo y pulsos
    // Envolvente de ataques: flujo de toda la banda + flujo grave reforzado (bombo/bajo marcan el pulso;
    // el charles en corcheas, de banda ancha, no debe decidir el tempo).
    const auto envAll = onsetEnvelope (f.flux);
    const auto envLow = onsetEnvelope (f.lowFlux);
    std::vector<float> env (envAll.size());
    for (size_t i = 0; i < env.size(); ++i) env[i] = (envAll[i] + 1.5f * envLow[i]) / 2.5f;
    const TempoEstimate te = estimateTempo (env, o);
    if (te.bpm > 0)
    {
        r.bpmConfidence = te.confidence; r.bpmCandidates = te.candidates;
        double period = te.periodFrames;
        r.beatsSec = beatGrid (env, f.rms, period, f.count);
        r.bpm = 60.0 * (kRate / kHop) / period;
        if (! r.bpmCandidates.empty()) r.bpmCandidates.front().bpm = r.bpm;
        if (! r.beatsSec.empty()) r.firstBeatSec = r.beatsSec.front();
        if (te.confidence < 0.4) r.warnings.push_back ("Tempo ambiguo: revisa el BPM (candidatos: " + std::to_string ((int) std::lround (te.candidates.size())) + ")");
        bool octave = false;
        for (size_t i = 1; i < te.candidates.size() && i < 3; ++i)
        {
            const double q = te.candidates[i].bpm / te.bpm;
            if ((std::abs (q - 2.0) < 0.06 || std::abs (q - 0.5) < 0.03) && te.candidates[i].score > 0.8 * te.candidates.front().score) octave = true;
        }
        if (octave) r.warnings.push_back ("Posible ambigüedad de octava en el tempo (mitad/doble): confírmalo escuchando");
    }
    else
    {
        r.warnings.push_back ("No se detectó un pulso regular");
    }

    // ---- compases: elegir el pulso inicial (downbeat) por mayor energía grave + cambios de croma.
    const double framesPerSec = kRate / kHop;
    int downbeatOffset = 0;
    if (r.beatsSec.size() >= (size_t) r.beatsPerBar)
    {
        std::vector<double> lowScore ((size_t) r.beatsPerBar, 0.0), novScore ((size_t) r.beatsPerBar, 0.0);
        for (int d = 0; d < r.beatsPerBar; ++d)
        {
            double& sLow = lowScore[(size_t) d];
            double& s = novScore[(size_t) d];
            for (size_t i = (size_t) d; i < r.beatsSec.size(); i += (size_t) r.beatsPerBar)
            {
                const int k = (int) std::lround (r.beatsSec[i] * framesPerSec);
                for (int dd = -1; dd <= 1; ++dd) if (k + dd >= 0 && k + dd < f.count) sLow += f.lowFlux[k + dd];
                // novedad de croma: distancia entre el compás anterior y el que empieza aquí
                if (i >= (size_t) r.beatsPerBar && i + r.beatsPerBar <= r.beatsSec.size())
                {
                    double a[12] = { 0 }, b[12] = { 0 };
                    const int k0 = (int) std::lround (r.beatsSec[i - r.beatsPerBar] * framesPerSec), k1 = (int) std::lround (r.beatsSec[i] * framesPerSec);
                    const int k2 = (int) std::lround (r.beatsSec[std::min (i + r.beatsPerBar, r.beatsSec.size() - 1)] * framesPerSec);
                    for (int t = std::max (0, k0); t < std::min (k1, f.count); ++t) for (int c = 0; c < 12; ++c) a[c] += f.chroma[(size_t) t * 12 + c];
                    for (int t = std::max (0, k1); t < std::min (k2, f.count); ++t) for (int c = 0; c < 12; ++c) b[c] += f.chroma[(size_t) t * 12 + c];
                    double sa = 0, sb = 0; for (int c = 0; c < 12; ++c) { sa += a[c]; sb += b[c]; }
                    if (sa > 0 && sb > 0) { double dist = 0; for (int c = 0; c < 12; ++c) dist += std::abs (a[c] / sa - b[c] / sb); s += dist; }
                }
            }
        }
        // Ambos criterios normalizados (suma 1 entre desplazamientos): energía grave en el pulso 1 y cambios de acorde en el pulso 1.
        double lowTot = 0, novTot = 0;
        for (int d = 0; d < r.beatsPerBar; ++d) { lowTot += lowScore[(size_t) d]; novTot += novScore[(size_t) d]; }
        double bestS = -1;
        for (int d = 0; d < r.beatsPerBar; ++d)
        {
            const double sc = (lowTot > 0 ? lowScore[(size_t) d] / lowTot : 0) + (novTot > 0 ? 1.5 * novScore[(size_t) d] / novTot : 0);
            if (sc > bestS) { bestS = sc; downbeatOffset = d; }
        }
        if (downbeatOffset > 0) r.beatsSec.erase (r.beatsSec.begin(), r.beatsSec.begin() + downbeatOffset);
        r.firstBeatSec = r.beatsSec.empty() ? 0 : r.beatsSec.front();
    }
    r.bars = (int) (r.beatsSec.size() / (size_t) r.beatsPerBar);
    auto barFrameRange = [&] (int bar, int& t0, int& t1) {
        const size_t i0 = (size_t) bar * r.beatsPerBar, i1 = i0 + r.beatsPerBar;
        t0 = std::max (0, (int) std::lround (r.beatsSec[i0] * framesPerSec));
        t1 = i1 < r.beatsSec.size() ? (int) std::lround (r.beatsSec[i1] * framesPerSec) : (int) std::lround ((r.beatsSec[i1 - 1] + 60.0 / r.bpm) * framesPerSec);
        t1 = std::min (t1, f.count);
    };

    // ---- contenido tonal
    double chroma[12] = { 0 };
    double peakRatioMean = 0, peakW = 0;
    for (int t = 0; t < f.count; ++t)
    {
        const double w = f.chromaEnergy[t];
        for (int c = 0; c < 12; ++c) chroma[c] += w * f.chroma[(size_t) t * 12 + c];
        peakRatioMean += f.peakRatio[t] * f.rms[t]; peakW += f.rms[t];
    }
    peakRatioMean = peakW > 0 ? peakRatioMean / peakW : 0;
    double csum = 0; for (double c : chroma) csum += c;
    double flatness = 1.0;
    if (csum > 0)
    {
        double geo = 0, ari = 0;
        for (double c : chroma) { geo += std::log (c / csum + 1e-6); ari += c / csum; }
        flatness = std::exp (geo / 12.0) / (ari / 12.0);
    }
    r.tonal = csum > 0 && peakRatioMean > 0.25 && flatness < 0.75;
    r.percussiveOnly = ! r.tonal;
    if (r.percussiveOnly)
    {
        r.warnings.push_back ("No se detecta contenido armónico fiable (beat percusivo): elige la tonalidad y los acordes a mano");
    }
    else
    {
        r.keyCandidates = estimateKey (chroma);
        r.keyCandidates.resize (std::min<size_t> (4, r.keyCandidates.size()));
        r.key = r.keyCandidates.front().key;
        const double r1 = r.keyCandidates.front().score, r2 = r.keyCandidates.size() > 1 ? r.keyCandidates[1].score : 0;
        r.keyConfidence = clamp01 (0.55 * clamp01 ((r1 - r2) / 0.12) + 0.45 * clamp01 ((r1 - 0.3) / 0.6));

        // ---- acordes por tiempo: segmentos de croma estable (no dependen de la rejilla)
        if (o.detectChords)
        {
            const int smooth = std::max (1, (int) std::lround (0.12 * framesPerSec));
            std::vector<double> sm ((size_t) f.count * 12, 0.0), segSum (12, 0.0);
            for (int t = 0; t < f.count; ++t)
            {
                double acc[12] = { 0 }; double w = 0;
                for (int k = std::max (0, t - smooth); k <= std::min (f.count - 1, t + smooth); ++k)
                { w += f.chromaEnergy[k]; for (int c = 0; c < 12; ++c) acc[c] += f.chromaEnergy[k] * f.chroma[(size_t) k * 12 + c]; }
                if (w > 1e-12) for (int c = 0; c < 12; ++c) sm[(size_t) t * 12 + c] = acc[c] / w;
            }
            int segStart = -1, segLen = 0, away = 0;
            const int minLen = std::max (3, (int) std::lround (0.3 * framesPerSec));
            auto flush = [&] (int endT) {
                if (segStart < 0 || segLen < minLen) return;
                double bc[12];
                for (int c = 0; c < 12; ++c) bc[c] = segSum[(size_t) c];
                const ChordGuess g = guessChord (bc, r.key);
                ChordEvent ev; ev.startSec = segStart * kHop / kRate; ev.endSec = endT * kHop / kRate; ev.chord = g.chord; ev.confidence = g.confidence;
                if (! r.chordEvents.empty() && r.chordEvents.back().chord == ev.chord && ev.startSec - r.chordEvents.back().endSec < 0.25)
                { r.chordEvents.back().endSec = ev.endSec; r.chordEvents.back().confidence = std::max (r.chordEvents.back().confidence, ev.confidence); }
                else r.chordEvents.push_back (ev);
            };
            for (int t = 0; t < f.count; ++t)
            {
                const double* cur = &sm[(size_t) t * 12];
                double csumT = 0; for (int c = 0; c < 12; ++c) csumT += cur[c];
                const bool tonalFrame = csumT > 1e-9 && f.peakRatio[t] > 0.15 && f.chromaEnergy[t] > 1e-7f;
                if (segStart < 0)
                {
                    if (tonalFrame) { segStart = t; segLen = 1; away = 0; for (int c = 0; c < 12; ++c) segSum[(size_t) c] = cur[c]; }
                    continue;
                }
                double segTot = 0; for (double v : segSum) segTot += v;
                double dist = 0;
                if (tonalFrame && segTot > 1e-12) for (int c = 0; c < 12; ++c) dist += std::abs (segSum[(size_t) c] / segTot - cur[c]);
                const bool changed = ! tonalFrame || dist > 0.6;
                if (changed) ++away; else away = 0;
                if (away >= 3)
                {
                    flush (t - away + 1);
                    segStart = -1; segLen = 0; away = 0;
                    if (tonalFrame) { segStart = t; segLen = 1; for (int c = 0; c < 12; ++c) segSum[(size_t) c] = cur[c]; }
                    continue;
                }
                if (tonalFrame) { for (int c = 0; c < 12; ++c) segSum[(size_t) c] += cur[c]; ++segLen; }
            }
            flush (f.count);

            // Relativa mayor/menor: los perfiles de croma no la distinguen bien (misma escala). Decide el acorde
            // de tónica con el que empieza y acaba la música, y su presencia total.
            for (size_t i = 1; i < r.keyCandidates.size(); ++i)
            {
                const Key k1 = r.keyCandidates[0].key, k2 = r.keyCandidates[i].key;
                const bool relative = (k1.mode == Mode::Minor && k2.mode == Mode::Major && mod12 (k1.tonic + 3) == k2.tonic)
                                   || (k1.mode == Mode::Major && k2.mode == Mode::Minor && mod12 (k1.tonic - 3) == k2.tonic);
                if (! relative) continue;
                if (r.keyCandidates[0].score - r.keyCandidates[i].score < 0.2 && ! r.chordEvents.empty())
                {
                    auto tonicWeight = [&] (const Key& k) {
                        const Chord tonic { k.tonic, k.mode == Mode::Major ? ChordQuality::Major : ChordQuality::Minor };
                        double w = 0;
                        for (size_t e = 0; e < r.chordEvents.size(); ++e)
                            if (r.chordEvents[e].chord == tonic)
                                w += (e == 0 ? 2.0 : e + 1 == r.chordEvents.size() ? 1.5 : 1.0) * (0.5 + r.chordEvents[e].confidence) * std::min (4.0, r.chordEvents[e].endSec - r.chordEvents[e].startSec);
                        return w;
                    };
                    const double w1 = tonicWeight (k1), w2 = tonicWeight (k2);
                    if (w2 > w1 * 1.2 && w2 > 0)
                    {
                        std::swap (r.keyCandidates[0], r.keyCandidates[i]);
                        r.key = r.keyCandidates[0].key;
                        r.keyConfidence = std::max (r.keyConfidence, 0.6);
                        r.warnings.push_back ("Tonalidad decidida por el acorde de tónica de la progresión (" + r.key.toString() + " frente a " + k1.toString() + ")");
                    }
                    else if (w1 > 0 && w1 >= w2) r.keyConfidence = std::max (r.keyConfidence, 0.6);
                }
                break;
            }
        }

        // ---- acordes por compás (solo si la rejilla de pulsos es fiable)
        if (o.detectChords && r.bars > 0 && r.bpmConfidence < 0.3)
            r.warnings.push_back ("Sin pulso fiable: los acordes se estiman por tiempo, no por compás");
        if (o.detectChords && r.bars > 0 && r.bpmConfidence >= 0.3)
        {
            r.harmony.key = r.key; r.harmony.keyConfidence = r.keyConfidence;
            r.harmony.bpm = r.bpm; r.harmony.bpmConfidence = r.bpmConfidence;
            r.harmony.beatsPerBar = r.beatsPerBar; r.harmony.bars = r.bars;
            double confSum = 0;
            std::vector<ChordGuess> guesses;
            for (int b = 0; b < r.bars; ++b)
            {
                int t0, t1; barFrameRange (b, t0, t1);
                double bc[12] = { 0 };
                for (int t = t0; t < t1; ++t) for (int c = 0; c < 12; ++c) bc[c] += f.chromaEnergy[t] * f.chroma[(size_t) t * 12 + c];
                guesses.push_back (guessChord (bc, r.key));
            }
            // Suavizado: un compás aislado con poca confianza entre dos iguales se asimila a sus vecinos.
            for (int b = 1; b + 1 < r.bars; ++b)
                if (guesses[b].confidence < 0.35 && guesses[b - 1].chord == guesses[b + 1].chord && ! (guesses[b].chord == guesses[b - 1].chord))
                    guesses[b].chord = guesses[b - 1].chord;
            for (int b = 0; b < r.bars; ++b)
            {
                confSum += guesses[b].confidence;
                if (! r.harmony.segments.empty() && r.harmony.segments.back().chord == guesses[b].chord)
                {
                    auto& s = r.harmony.segments.back();
                    s.confidence = (s.confidence * s.lengthBars + guesses[b].confidence) / (s.lengthBars + 1);
                    ++s.lengthBars;
                    continue;
                }
                ChordSegment s; s.startBar = b; s.chord = guesses[b].chord; s.confidence = guesses[b].confidence;
                r.harmony.segments.push_back (s);
            }
            r.chordConfidence = confSum / r.bars;
            r.harmonicChanges = r.harmony.changeBars();
        }
    }

    if (r.tonal)
    {
        if (r.keyCandidates.size() > 1)
        {
            const Key& k2 = r.keyCandidates[1].key;
            const bool relative = (r.key.mode == Mode::Minor && k2.mode == Mode::Major && mod12 (r.key.tonic + 3) == k2.tonic)
                               || (r.key.mode == Mode::Major && k2.mode == Mode::Minor && mod12 (r.key.tonic - 3) == k2.tonic);
            if (relative && r.keyConfidence < 0.6) r.warnings.push_back ("Tonalidad ambigua entre " + r.key.toString() + " y su relativa " + k2.toString());
        }
        if (r.keyConfidence < 0.4) r.warnings.push_back ("Tonalidad poco clara: confírmala a mano");
    }

    // ---- energía por compás
    if (r.bars > 0)
    {
        std::vector<double> db (r.bars);
        for (int b = 0; b < r.bars; ++b)
        {
            int t0, t1; barFrameRange (b, t0, t1);
            double s = 0; int n = 0;
            for (int t = t0; t < t1; ++t) { s += (double) f.rms[t] * f.rms[t]; ++n; }
            db[b] = dbOf (std::sqrt (n > 0 ? s / n : 0));
        }
        r.barRmsDb = db;
        std::vector<double> sorted = db; std::sort (sorted.begin(), sorted.end());
        const double med = sorted[sorted.size() / 2];
        for (int b = 0; b < r.bars; ++b)
        {
            const EnergyLevel l = db[b] < med - 3.0 ? EnergyLevel::Low : db[b] > med + 2.0 ? EnergyLevel::High : EnergyLevel::Mid;
            if (! r.energyRegions.empty() && r.energyRegions.back().level == l)
            {
                auto& reg = r.energyRegions.back();
                reg.rmsDb = (reg.rmsDb * (reg.endBar - reg.startBar) + db[b]) / (reg.endBar - reg.startBar + 1);
                reg.endBar = b + 1;
                reg.endSec = (size_t) (b + 1) * r.beatsPerBar < r.beatsSec.size() ? r.beatsSec[(size_t) (b + 1) * r.beatsPerBar] : r.beatsSec.back() + 60.0 / r.bpm;
                continue;
            }
            EnergyRegion reg; reg.startBar = b; reg.endBar = b + 1; reg.level = l; reg.rmsDb = db[b];
            reg.startSec = r.beatsSec[(size_t) b * r.beatsPerBar];
            reg.endSec = (size_t) (b + 1) * r.beatsPerBar < r.beatsSec.size() ? r.beatsSec[(size_t) (b + 1) * r.beatsPerBar] : r.beatsSec.back() + 60.0 / r.bpm;
            r.energyRegions.push_back (reg);
        }
    }
    return r;
}

BeatAnalysisResult analyseBeatFile (const std::string& wavPath, const BeatAnalysisOptions& o)
{
    MonoAudio a; std::string err;
    if (! readWavFile (wavPath, a, err)) { BeatAnalysisResult r; r.error = err; return r; }
    return analyseBeat (a, o);
}

std::string BeatAnalysisResult::summary() const
{
    if (! ok) return "Error: " + error;
    if (silent) return "Silencio";
    char buf[256];
    std::string s;
    if (bpm > 0) { std::snprintf (buf, sizeof (buf), "%.1f BPM (confianza %s)", bpm, confName (bpmConfidence).c_str()); s += buf; }
    else s += "Sin pulso claro";
    if (tonal) { std::snprintf (buf, sizeof (buf), ", %s (confianza %s)", key.toString().c_str(), confName (keyConfidence).c_str()); s += buf; }
    else s += ", percusivo (sin tonalidad)";
    if (bars > 0) { std::snprintf (buf, sizeof (buf), ", %d compases", bars); s += buf; }
    if (! harmony.empty()) s += ", acordes: " + harmony.toString();
    return s;
}

// ---------------------------------------------------------------- persistencia
namespace
{
std::string fmt (double v) { char b[64]; std::snprintf (b, sizeof (b), "%.6g", v); return b; }
std::vector<std::string> splitWs (const std::string& s)
{
    std::vector<std::string> out; std::istringstream is (s); std::string t;
    while (is >> t) out.push_back (t);
    return out;
}
} // namespace

std::string serializeHarmony (const HarmonyTimeline& h, const std::string& prefix)
{
    std::ostringstream o;
    o << prefix << "key=" << h.key.tonic << " " << (h.key.mode == Mode::Major ? 0 : 1) << " " << fmt (h.keyConfidence) << " " << (int) h.keyProvenance << "\n";
    o << prefix << "bpm=" << fmt (h.bpm) << " " << fmt (h.bpmConfidence) << " " << (int) h.bpmProvenance << "\n";
    o << prefix << "bars=" << h.bars << " " << h.beatsPerBar << "\n";
    for (const auto& s : h.segments)
        o << prefix << "seg=" << s.startBar << " " << s.lengthBars << " " << s.chord.root << " " << (int) s.chord.quality << " " << fmt (s.confidence) << " " << (int) s.provenance << "\n";
    return o.str();
}

bool deserializeHarmony (const std::string& text, HarmonyTimeline& out, std::string& error, const std::string& prefix)
{
    HarmonyTimeline h;
    std::istringstream is (text); std::string line;
    bool any = false;
    while (std::getline (is, line))
    {
        if (! line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find ('=');
        if (eq == std::string::npos || line.rfind (prefix, 0) != 0 || eq < prefix.size()) continue;
        const std::string k = line.substr (prefix.size(), eq - prefix.size());
        const auto v = splitWs (line.substr (eq + 1));
        any = true;
        if (k == "key" && v.size() >= 4) { h.key = Key { mod12 (std::atoi (v[0].c_str())), std::atoi (v[1].c_str()) == 0 ? Mode::Major : Mode::Minor }; h.keyConfidence = std::atof (v[2].c_str()); h.keyProvenance = (Provenance) std::atoi (v[3].c_str()); }
        else if (k == "bpm" && v.size() >= 3) { h.bpm = std::atof (v[0].c_str()); h.bpmConfidence = std::atof (v[1].c_str()); h.bpmProvenance = (Provenance) std::atoi (v[2].c_str()); }
        else if (k == "bars" && v.size() >= 2) { h.bars = std::atoi (v[0].c_str()); h.beatsPerBar = std::atoi (v[1].c_str()); }
        else if (k == "seg" && v.size() >= 6)
        {
            ChordSegment s; s.startBar = std::atoi (v[0].c_str()); s.lengthBars = std::atoi (v[1].c_str());
            s.chord = Chord { mod12 (std::atoi (v[2].c_str())), (ChordQuality) std::clamp (std::atoi (v[3].c_str()), 0, 10) };
            s.confidence = std::atof (v[4].c_str()); s.provenance = (Provenance) std::atoi (v[5].c_str());
            if (s.lengthBars > 0 && s.startBar >= 0 && h.segments.size() < 1024) h.segments.push_back (s);
        }
    }
    if (! any) { error = "No hay datos de armonía"; return false; }
    if (h.bars < 0 || h.bars > 4096 || h.beatsPerBar < 1 || h.beatsPerBar > 16) { error = "Armonía no válida"; return false; }
    if (! (h.bpm >= 20 && h.bpm <= 400)) h.bpm = 120;
    out = h;
    return true;
}

std::string serializeBeatAnalysis (const BeatAnalysisResult& r)
{
    std::ostringstream o;
    o << "beat.version=" << BeatAnalysisResult::kVersion << "\n";
    o << "beat.ok=" << (r.ok ? 1 : 0) << "\n";
    o << "beat.signal=" << fmt (r.sampleRate) << " " << fmt (r.durationSec) << " " << r.sourceChannels << " " << fmt (r.peakDb) << " " << fmt (r.rmsDb) << " " << (r.silent ? 1 : 0) << " " << (r.percussiveOnly ? 1 : 0) << "\n";
    o << "beat.tempo=" << fmt (r.bpm) << " " << fmt (r.bpmConfidence) << " " << fmt (r.firstBeatSec) << " " << r.beatsPerBar << " " << r.bars << "\n";
    for (const auto& c : r.bpmCandidates) o << "beat.bpmcand=" << fmt (c.bpm) << " " << fmt (c.score) << "\n";
    o << "beat.tonal=" << (r.tonal ? 1 : 0) << " " << r.key.tonic << " " << (r.key.mode == Mode::Major ? 0 : 1) << " " << fmt (r.keyConfidence) << " " << fmt (r.chordConfidence) << "\n";
    for (const auto& c : r.keyCandidates) o << "beat.keycand=" << c.key.tonic << " " << (c.key.mode == Mode::Major ? 0 : 1) << " " << fmt (c.score) << "\n";
    o << "beat.beats=";
    for (size_t i = 0; i < r.beatsSec.size(); ++i) { if (i) o << " "; o << fmt (r.beatsSec[i]); }
    o << "\n";
    o << "beat.barrms=";
    for (size_t i = 0; i < r.barRmsDb.size(); ++i) { if (i) o << " "; o << fmt (r.barRmsDb[i]); }
    o << "\n";
    for (const auto& e : r.energyRegions) o << "beat.energy=" << e.startBar << " " << e.endBar << " " << (int) e.level << " " << fmt (e.rmsDb) << " " << fmt (e.startSec) << " " << fmt (e.endSec) << "\n";
    for (const auto& w : r.warnings) { std::string w1 = w; for (auto& ch : w1) if (ch == '\n') ch = ' '; o << "beat.warn=" << w1 << "\n"; }
    for (const auto& e : r.chordEvents) o << "beat.chordev=" << fmt (e.startSec) << " " << fmt (e.endSec) << " " << e.chord.root << " " << (int) e.chord.quality << " " << fmt (e.confidence) << "\n";
    if (! r.harmony.empty()) o << serializeHarmony (r.harmony, "beat.harm.");
    return o.str();
}

bool deserializeBeatAnalysis (const std::string& text, BeatAnalysisResult& out, std::string& error)
{
    BeatAnalysisResult r;
    std::istringstream is (text); std::string line;
    bool any = false;
    while (std::getline (is, line))
    {
        if (! line.empty() && line.back() == '\r') line.pop_back();
        const auto eq = line.find ('=');
        if (eq == std::string::npos || line.rfind ("beat.", 0) != 0) continue;
        const std::string k = line.substr (5, eq - 5);
        const std::string raw = line.substr (eq + 1);
        const auto v = splitWs (raw);
        any = true;
        if (k == "version") { if (std::atoi (raw.c_str()) > BeatAnalysisResult::kVersion) { error = "Análisis de beat de una versión más reciente"; return false; } }
        else if (k == "ok") r.ok = std::atoi (raw.c_str()) != 0;
        else if (k == "signal" && v.size() >= 7) { r.sampleRate = std::atof (v[0].c_str()); r.durationSec = std::atof (v[1].c_str()); r.sourceChannels = std::atoi (v[2].c_str()); r.peakDb = std::atof (v[3].c_str()); r.rmsDb = std::atof (v[4].c_str()); r.silent = v[5] == "1"; r.percussiveOnly = v[6] == "1"; }
        else if (k == "tempo" && v.size() >= 5) { r.bpm = std::atof (v[0].c_str()); r.bpmConfidence = std::atof (v[1].c_str()); r.firstBeatSec = std::atof (v[2].c_str()); r.beatsPerBar = std::clamp (std::atoi (v[3].c_str()), 1, 16); r.bars = std::clamp (std::atoi (v[4].c_str()), 0, 4096); }
        else if (k == "bpmcand" && v.size() >= 2) { if (r.bpmCandidates.size() < 16) r.bpmCandidates.push_back ({ std::atof (v[0].c_str()), std::atof (v[1].c_str()) }); }
        else if (k == "tonal" && v.size() >= 5) { r.tonal = v[0] == "1"; r.key = Key { mod12 (std::atoi (v[1].c_str())), std::atoi (v[2].c_str()) == 0 ? Mode::Major : Mode::Minor }; r.keyConfidence = std::atof (v[3].c_str()); r.chordConfidence = std::atof (v[4].c_str()); }
        else if (k == "keycand" && v.size() >= 3) { if (r.keyCandidates.size() < 8) { KeyCandidate c; c.key = Key { mod12 (std::atoi (v[0].c_str())), std::atoi (v[1].c_str()) == 0 ? Mode::Major : Mode::Minor }; c.score = std::atof (v[2].c_str()); r.keyCandidates.push_back (c); } }
        else if (k == "beats") { for (const auto& s : v) if (r.beatsSec.size() < 65536) r.beatsSec.push_back (std::atof (s.c_str())); }
        else if (k == "barrms") { for (const auto& s : v) if (r.barRmsDb.size() < 4096) r.barRmsDb.push_back (std::atof (s.c_str())); }
        else if (k == "energy" && v.size() >= 6) { EnergyRegion e; e.startBar = std::atoi (v[0].c_str()); e.endBar = std::atoi (v[1].c_str()); e.level = (EnergyLevel) std::clamp (std::atoi (v[2].c_str()), 0, 2); e.rmsDb = std::atof (v[3].c_str()); e.startSec = std::atof (v[4].c_str()); e.endSec = std::atof (v[5].c_str()); if (r.energyRegions.size() < 4096) r.energyRegions.push_back (e); }
        else if (k == "warn") { if (r.warnings.size() < 32) r.warnings.push_back (raw); }
        else if (k == "chordev" && v.size() >= 5) { ChordEvent e; e.startSec = std::atof (v[0].c_str()); e.endSec = std::atof (v[1].c_str()); e.chord = Chord { mod12 (std::atoi (v[2].c_str())), (ChordQuality) std::clamp (std::atoi (v[3].c_str()), 0, 10) }; e.confidence = std::atof (v[4].c_str()); if (r.chordEvents.size() < 4096) r.chordEvents.push_back (e); }
    }
    if (! any) { error = "No hay análisis de beat"; return false; }
    std::string herr;
    HarmonyTimeline h;
    if (deserializeHarmony (text, h, herr, "beat.harm.")) r.harmony = h;
    r.harmonicChanges = r.harmony.changeBars();
    out = r;
    return true;
}

int BeatProject::sectionBars() const
{
    if (! loaded()) return 0;
    const int total = std::max (harmony.bars, analysis.bars);
    const int to = sectionToBar < 0 ? total - 1 : std::min (sectionToBar, total - 1);
    return std::max (1, to - std::max (0, sectionFromBar) + 1);
}

std::string serializeBeatProject (const BeatProject& b)
{
    std::ostringstream o;
    std::string path = b.path; for (auto& c : path) if (c == '\n' || c == '\r') c = ' ';
    o << "beatPath=" << path << "\n";
    o << "beatSection=" << b.sectionFromBar << " " << b.sectionToBar << "\n";
    o << serializeBeatAnalysis (b.analysis);
    if (! b.harmony.empty()) o << serializeHarmony (b.harmony, "harm.");
    return o.str();
}

bool deserializeBeatProject (const std::string& text, BeatProject& out, std::string& error)
{
    BeatProject b;
    if (! deserializeBeatAnalysis (text, b.analysis, error)) return false;
    std::string herr;
    if (! deserializeHarmony (text, b.harmony, herr, "harm.")) b.harmony = b.analysis.harmony;
    std::istringstream is (text); std::string line;
    while (std::getline (is, line))
    {
        if (! line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind ("beatPath=", 0) == 0) b.path = line.substr (9);
        else if (line.rfind ("beatSection=", 0) == 0) { const auto v = splitWs (line.substr (12)); if (v.size() >= 2) { b.sectionFromBar = std::atoi (v[0].c_str()); b.sectionToBar = std::atoi (v[1].c_str()); } }
    }
    if (b.sectionFromBar < 0) b.sectionFromBar = 0;
    out = b;
    return true;
}

// ---------------------------------------------------------------- beats sintéticos
MonoAudio synthesiseBeat (const SyntheticBeatSpec& spec)
{
    MonoAudio a;
    a.sampleRate = spec.sampleRate; a.sourceChannels = 1; a.sourceBits = 16;
    const double sr = spec.sampleRate;
    const double beatSec = 60.0 / std::max (20.0, spec.bpm);
    const int totalBeats = spec.bars * spec.beatsPerBar;
    const size_t lead = (size_t) std::llround (spec.leadInSec * sr);
    const size_t n = lead + (size_t) std::llround (totalBeats * beatSec * sr) + (size_t) (0.5 * sr);
    std::vector<double> mix (n, 0.0);
    uint64_t rng = spec.seed * 0x9E3779B97F4A7C15ull + 12345;
    auto urand = [&] { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (double) (rng >> 11) / 9007199254740992.0 * 2.0 - 1.0; };

    if (spec.drums)
    {
        for (int b = 0; b < totalBeats; ++b)
        {
            const double swing = (b % 2 == 1) ? spec.swingSec : 0.0;
            const size_t start = lead + (size_t) std::llround ((b * beatSec + swing) * sr);
            // bombo en cada pulso (más fuerte en el 1)
            const double kickAmp = (b % spec.beatsPerBar == 0) ? 0.9 : 0.7;
            double ph = 0;
            for (size_t i = 0; i < (size_t) (0.25 * sr) && start + i < n; ++i)
            {
                const double t = (double) i / sr;
                const double fq = 50.0 + 100.0 * std::exp (-t * 30.0);
                ph += 2.0 * kPi * fq / sr;
                mix[start + i] += kickAmp * std::sin (ph) * std::exp (-t * 12.0);
            }
            // caja en 2 y 4
            if (spec.beatsPerBar >= 2 && (b % spec.beatsPerBar) % 2 == 1)
                for (size_t i = 0; i < (size_t) (0.18 * sr) && start + i < n; ++i)
                {
                    const double t = (double) i / sr;
                    mix[start + i] += 0.5 * (0.7 * urand() + 0.3 * std::sin (2.0 * kPi * 190.0 * t)) * std::exp (-t * 22.0);
                }
            // charles en corcheas
            for (int h = 0; h < 2; ++h)
            {
                const size_t hs = start + (size_t) std::llround (h * beatSec * 0.5 * sr);
                double hp = 0;
                for (size_t i = 0; i < (size_t) (0.04 * sr) && hs + i < n; ++i)
                {
                    const double t = (double) i / sr;
                    const double w = urand();
                    const double y = w - hp; hp = w; // diferencia: ruido con más agudos
                    mix[hs + i] += 0.18 * y * std::exp (-t * 90.0);
                }
            }
        }
    }

    if (spec.harmony)
    {
        Progression prog; std::string err;
        if (! Progression::parse (spec.progression, prog, err) || prog.bars.empty())
            prog.bars = { Chord { spec.key.tonic, spec.key.mode == Mode::Major ? ChordQuality::Major : ChordQuality::Minor } };
        for (int bar = 0; bar < spec.bars; ++bar)
        {
            const Chord c = prog.bars[(size_t) bar % prog.bars.size()];
            const size_t start = lead + (size_t) std::llround (bar * spec.beatsPerBar * beatSec * sr);
            const size_t len = (size_t) std::llround (spec.beatsPerBar * beatSec * sr);
            std::vector<int> midis;
            midis.push_back (36 + c.root);           // bajo (octava 2)
            for (int pc : c.pitchClasses()) midis.push_back (60 + mod12 (pc - c.root) + c.root - (c.root > 6 ? 12 : 0)); // voicing cerrado ~C4
            for (size_t k = 0; k < midis.size(); ++k)
            {
                const double hz = 440.0 * std::pow (2.0, (midis[k] - 69) / 12.0);
                const double amp = k == 0 ? 0.22 : 0.11;
                for (size_t i = 0; i < len && start + i < n; ++i)
                {
                    const double t = (double) i / sr;
                    const double envA = std::min (1.0, t / 0.02) * std::min (1.0, ((double) (len - i) / sr) / 0.05);
                    const double y = std::sin (2.0 * kPi * hz * t) + 0.45 * std::sin (2.0 * kPi * 2 * hz * t) + 0.2 * std::sin (2.0 * kPi * 3 * hz * t);
                    mix[start + i] += amp * envA * y;
                }
            }
        }
    }
    const double noiseLin = std::pow (10.0, spec.noiseDb / 20.0);
    for (auto& v : mix) v += noiseLin * urand();
    double peak = 0;
    for (double v : mix) peak = std::max (peak, std::abs (v));
    const double g = peak > 0.95 ? 0.95 / peak : 1.0;
    a.samples.resize (n);
    for (size_t i = 0; i < n; ++i) a.samples[i] = (float) (mix[i] * g);
    return a;
}

std::vector<uint8_t> encodeWav16 (const MonoAudio& audio, int channels)
{
    channels = std::clamp (channels, 1, 2);
    const uint32_t sr = (uint32_t) std::lround (audio.sampleRate);
    const uint32_t frames = (uint32_t) audio.samples.size();
    const uint32_t dataBytes = frames * (uint32_t) channels * 2u;
    std::vector<uint8_t> b;
    b.reserve (44 + dataBytes);
    auto w32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) (v >> (8 * i))); };
    auto w16 = [&] (uint16_t v) { b.push_back ((uint8_t) v); b.push_back ((uint8_t) (v >> 8)); };
    auto tag = [&] (const char* t) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) t[i]); };
    tag ("RIFF"); w32 (36 + dataBytes); tag ("WAVE");
    tag ("fmt "); w32 (16); w16 (1); w16 ((uint16_t) channels); w32 (sr); w32 (sr * (uint32_t) channels * 2u); w16 ((uint16_t) (channels * 2)); w16 (16);
    tag ("data"); w32 (dataBytes);
    for (float s : audio.samples)
    {
        const int v = (int) std::lround (std::max (-1.0f, std::min (1.0f, s)) * 32767.0f);
        for (int c = 0; c < channels; ++c) w16 ((uint16_t) (int16_t) v);
    }
    return b;
}

bool writeWavFile (const std::string& path, const MonoAudio& audio, int channels)
{
    const auto bytes = encodeWav16 (audio, channels);
    std::FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return false;
    const size_t written = std::fwrite (bytes.data(), 1, bytes.size(), f);
    std::fclose (f);
    return written == bytes.size();
}
} // namespace solo
