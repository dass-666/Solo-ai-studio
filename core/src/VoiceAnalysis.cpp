#include "solo/VoiceAnalysis.h"
#include "solo/Dsp.h"
#include "solo/Performer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <numeric>

namespace solo
{
namespace
{
constexpr double kAnalysisRate = 16000.0;
constexpr int kHop = 160;     // 10 ms
constexpr int kWindow = 512;  // 32 ms

uint32_t rd32le (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
uint16_t rd16le (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

double median (std::vector<double> v)
{
    if (v.empty()) return 0.0;
    const size_t m = v.size() / 2;
    std::nth_element (v.begin(), v.begin() + (long) m, v.end());
    double r = v[m];
    if (v.size() % 2 == 0)
    {
        std::nth_element (v.begin(), v.begin() + (long) m - 1, v.end());
        r = 0.5 * (r + v[m - 1]);
    }
    return r;
}

double hzToMidi (double hz) { return 69.0 + 12.0 * std::log2 (hz / 440.0); }
double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
int mod12 (int v) { return ((v % 12) + 12) % 12; }

std::vector<float> resampleTo16k (const std::vector<float>& in, double sr)
{
    if (std::abs (sr - kAnalysisRate) < 1e-6) return in;
    std::vector<float> filtered = in;
    if (sr > kAnalysisRate)
    {
        // Antialias: 4.º orden a 6.5 kHz antes de diezmar.
        dsp::Biquad a, b;
        a.lowpass (sr, 6500.0, 0.54);
        b.lowpass (sr, 6500.0, 1.31);
        for (auto& x : filtered) x = b.process (a.process (x));
    }
    const double ratio = sr / kAnalysisRate;
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

// YIN (de Cheveigné & Kawahara, 2002) sobre una ventana.
void yin (const float* x, int maxTau, int minTau, double threshold, double& tauOut, double& confidence, std::vector<double>& d)
{
    d.assign ((size_t) maxTau + 2, 0.0);
    for (int tau = 1; tau <= maxTau + 1; ++tau)
    {
        double s = 0;
        for (int j = 0; j < kWindow; ++j)
        {
            const double diff = (double) x[j] - (double) x[j + tau];
            s += diff * diff;
        }
        d[(size_t) tau] = s;
    }
    // Diferencia normalizada acumulada
    double running = 0;
    d[0] = 1.0;
    for (int tau = 1; tau <= maxTau + 1; ++tau)
    {
        running += d[(size_t) tau];
        d[(size_t) tau] = running > 0 ? d[(size_t) tau] * tau / running : 1.0;
    }
    int best = -1;
    for (int tau = minTau; tau <= maxTau; ++tau)
    {
        if (d[(size_t) tau] < threshold)
        {
            while (tau + 1 <= maxTau && d[(size_t) tau + 1] < d[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0)
    {
        best = minTau;
        for (int tau = minTau; tau <= maxTau; ++tau)
            if (d[(size_t) tau] < d[(size_t) best]) best = tau;
    }
    double t = best;
    if (best > minTau && best < maxTau)
    {
        const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1];
        const double den = a - 2 * b + c;
        if (std::abs (den) > 1e-12) t = best + 0.5 * (a - c) / den;
    }
    tauOut = t;
    confidence = std::clamp (1.0 - d[(size_t) best], 0.0, 1.0);
}
} // namespace

// ---------------------------------------------------------------- WAV
bool decodeWav (const std::vector<uint8_t>& b, MonoAudio& out, std::string& error)
{
    if (b.size() < 12 || std::memcmp (b.data(), "RIFF", 4) != 0 || std::memcmp (b.data() + 8, "WAVE", 4) != 0)
    { error = "No es un fichero WAV (falta RIFF/WAVE)"; return false; }
    size_t p = 12;
    int fmtTag = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* data = nullptr;
    size_t dataSize = 0;
    while (p + 8 <= b.size())
    {
        const uint32_t sz = rd32le (&b[p + 4]);
        const size_t body = p + 8;
        if (sz > b.size() - body)
        {
            // Algunos programas escriben un tamaño de 'data' incorrecto: se acepta hasta el final del fichero.
            if (std::memcmp (&b[p], "data", 4) == 0) { data = &b[body]; dataSize = b.size() - body; break; }
            error = "Chunk WAV truncado";
            return false;
        }
        if (std::memcmp (&b[p], "fmt ", 4) == 0)
        {
            if (sz < 16) { error = "Chunk fmt inválido"; return false; }
            fmtTag = rd16le (&b[body]);
            channels = rd16le (&b[body + 2]);
            rate = rd32le (&b[body + 4]);
            bits = rd16le (&b[body + 14]);
            if (fmtTag == 0xFFFE && sz >= 40) fmtTag = rd16le (&b[body + 24]); // subformato
        }
        else if (std::memcmp (&b[p], "data", 4) == 0)
        {
            data = &b[body];
            dataSize = sz;
        }
        p = body + sz + (sz & 1);
    }
    if (channels <= 0 || channels > 8 || rate < 4000 || rate > 384000) { error = "Formato WAV no válido o falta 'fmt '"; return false; }
    if (! data) { error = "Falta el chunk 'data'"; return false; }
    const bool isFloat = fmtTag == 3;
    if (! ((fmtTag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (isFloat && (bits == 32 || bits == 64))))
    { error = "Formato WAV no soportado (usa PCM 16/24/32 bits o float 32)"; return false; }
    const size_t bytesPer = (size_t) bits / 8, frameBytes = bytesPer * (size_t) channels;
    const size_t frames = dataSize / frameBytes;
    if ((double) frames / rate > 600.0) { error = "Grabación demasiado larga (máximo 10 minutos)"; return false; }
    out.samples.assign (frames, 0.0f);
    out.sampleRate = rate;
    out.sourceChannels = channels;
    out.sourceBits = bits;
    for (size_t f = 0; f < frames; ++f)
    {
        double acc = 0;
        for (int c = 0; c < channels; ++c)
        {
            const uint8_t* s = data + f * frameBytes + (size_t) c * bytesPer;
            double v = 0;
            if (isFloat)
            {
                if (bits == 32) { float x; std::memcpy (&x, s, 4); v = x; }
                else { double x; std::memcpy (&x, s, 8); v = x; }
                if (! std::isfinite (v)) v = 0;
            }
            else if (bits == 8) v = ((int) s[0] - 128) / 128.0;
            else if (bits == 16) v = (int16_t) rd16le (s) / 32768.0;
            else if (bits == 24) v = (double) ((int32_t) (((uint32_t) s[0] << 8) | ((uint32_t) s[1] << 16) | ((uint32_t) s[2] << 24)) >> 8) / 8388608.0;
            else v = (int32_t) rd32le (s) / 2147483648.0;
            acc += v;
        }
        out.samples[f] = (float) std::clamp (acc / channels, -1.0, 1.0);
    }
    return true;
}

bool readWavFile (const std::string& path, MonoAudio& out, std::string& error)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) { error = "No se puede abrir: " + path; return false; }
    f.seekg (0, std::ios::end);
    const auto size = (size_t) f.tellg();
    if (size > 400u * 1024u * 1024u) { error = "Fichero demasiado grande"; return false; }
    f.seekg (0);
    std::vector<uint8_t> bytes (size);
    f.read ((char*) bytes.data(), (std::streamsize) size);
    return decodeWav (bytes, out, error);
}

// ---------------------------------------------------------------- análisis
VoiceAnalysis analyseVoice (const MonoAudio& audio, const VoiceAnalysisOptions& o)
{
    VoiceAnalysis va;
    va.sampleRate = audio.sampleRate;
    va.durationSec = audio.durationSec();
    if (audio.samples.empty() || audio.sampleRate <= 0) { va.warnings.push_back ("Audio vacío"); return va; }

    auto x = resampleTo16k (audio.samples, audio.sampleRate);
    // Quitar continua y graves (< 60 Hz): ruido de manejo del micrófono.
    {
        dsp::Biquad hp;
        hp.highpass (kAnalysisRate, 60.0, 0.7);
        for (auto& v : x) v = hp.process (v);
    }
    const int maxTau = (int) std::ceil (kAnalysisRate / o.minF0);
    const int minTau = std::max (2, (int) std::floor (kAnalysisRate / o.maxF0));
    const size_t need = (size_t) (kWindow + maxTau + 2);
    x.resize (std::max (x.size(), need), 0.0f);
    std::vector<double> d;
    for (size_t start = 0; start + need <= x.size(); start += kHop)
    {
        PitchFrame fr;
        fr.timeSec = ((double) start + kWindow * 0.5) / kAnalysisRate;
        double e = 0;
        for (int j = 0; j < kWindow; ++j) e += (double) x[start + (size_t) j] * x[start + (size_t) j];
        fr.rmsDb = 10.0 * std::log10 (e / kWindow + 1e-12);
        double tau = 0, conf = 0;
        yin (&x[start], maxTau, minTau, 0.15, tau, conf, d);
        fr.confidence = conf;
        if (tau > 0)
        {
            fr.f0Hz = kAnalysisRate / tau;
            fr.midi = hzToMidi (fr.f0Hz);
        }
        va.frames.push_back (fr);
    }
    if (va.frames.empty()) { va.warnings.push_back ("Audio demasiado corto"); return va; }

    double peakDb = -120;
    for (const auto& f : va.frames) peakDb = std::max (peakDb, f.rmsDb);
    const double gate = peakDb + o.silenceDb;
    if (peakDb < -60) va.warnings.push_back ("Nivel de la grabación muy bajo");
    const size_t N = va.frames.size();
    std::vector<char> sound (N), voiced (N);
    int soundCount = 0, voicedCount = 0;
    double confSum = 0;
    for (size_t i = 0; i < N; ++i)
    {
        const auto& f = va.frames[i];
        sound[i] = f.rmsDb > gate;
        voiced[i] = sound[i] && f.confidence >= o.minConfidence && f.f0Hz >= o.minF0 && f.f0Hz <= o.maxF0;
        soundCount += sound[i];
        voicedCount += voiced[i];
        if (sound[i]) confSum += f.confidence;
    }
    va.voicedRatio = soundCount > 0 ? (double) voicedCount / soundCount : 0;
    va.meanConfidence = soundCount > 0 ? confSum / soundCount : 0;
    va.pitchReliable = va.voicedRatio >= 0.5 && va.meanConfidence >= 0.6;
    if (! va.pitchReliable)
        va.warnings.push_back ("Altura poco fiable (voz con ruido, susurro o percusiva): se recomienda usar solo el ritmo");

    // Altura suavizada (mediana de 5) y corrección de saltos de octava aislados.
    std::vector<double> mid (N, 0.0);
    for (size_t i = 0; i < N; ++i)
    {
        if (! voiced[i]) continue;
        std::vector<double> w;
        for (size_t k = (i >= 2 ? i - 2 : 0); k <= std::min (N - 1, i + 2); ++k)
            if (voiced[k]) w.push_back (va.frames[k].midi);
        const double m = median (w);
        double v = va.frames[i].midi;
        while (v - m > 9) v -= 12;
        while (m - v > 9) v += 12;
        mid[i] = v;
    }

    // Segmentación: regiones con sonido -> cortes por re-ataque de energía o por cambio sostenido de altura.
    struct Seg { size_t a, b; };
    std::vector<Seg> segs;
    size_t i = 0;
    while (i < N)
    {
        if (! sound[i]) { ++i; continue; }
        size_t j = i;
        int gap = 0;
        while (j + 1 < N && (sound[j + 1] || (gap < 2 && j + 2 < N && sound[j + 2])))
        {
            gap = sound[j + 1] ? 0 : gap + 1;
            ++j;
        }
        // cortes dentro de la región
        size_t segStart = i;
        double refPitch = 0;
        int refCount = 0;
        for (size_t k = i + 1; k <= j; ++k)
        {
            bool cut = false;
            // re-ataque: subida de >= 6 dB respecto al mínimo de las 4 tramas anteriores, tras una caída
            if (k >= segStart + 4)
            {
                double lo = 1e9, hi = -1e9;
                for (size_t q = k - 4; q < k; ++q) { lo = std::min (lo, va.frames[q].rmsDb); hi = std::max (hi, va.frames[q].rmsDb); }
                double before = -1e9;
                for (size_t q = (k >= 10 ? k - 10 : segStart); q + 4 < k; ++q) before = std::max (before, va.frames[q].rmsDb);
                if (va.frames[k].rmsDb - lo >= 6.0 && before - lo >= 4.0) cut = true;
            }
            // cambio de altura sostenido
            if (! cut && voiced[k] && refCount >= 3)
            {
                bool sustained = true;
                int seen = 0;
                for (size_t q = k; q <= std::min (j, k + 3); ++q)
                {
                    if (! voiced[q]) continue;
                    ++seen;
                    if (std::abs (mid[q] - refPitch) < o.splitSemis) { sustained = false; break; }
                }
                if (sustained && seen >= 3) cut = true;
            }
            if (cut)
            {
                // Si el cambio de altura es un deslizamiento, el corte va al principio del deslizamiento.
                size_t c = k;
                while (c > segStart + 2 && voiced[c - 1] && std::abs (mid[c - 1] - refPitch) >= 0.35) --c;
                segs.push_back ({ segStart, c - 1 });
                segStart = c;
                refPitch = 0;
                refCount = 0;
                k = c;
                continue;
            }
            if (voiced[k] && k >= segStart + 2) // la referencia ignora el ataque
            {
                refPitch = refCount == 0 ? mid[k] : (refPitch * refCount + mid[k]) / (refCount + 1);
                refCount = std::min (refCount + 1, 12);
            }
        }
        segs.push_back ({ segStart, j });
        i = j + 1;
    }

    // Envolvente fina (1 ms) para precisar el instante de ataque dentro de la trama.
    std::vector<float> env (x.size());
    {
        float e = 0;
        const float att = 1.0f - std::exp (-1.0f / (0.001f * (float) kAnalysisRate));
        const float rel = 1.0f - std::exp (-1.0f / (0.010f * (float) kAnalysisRate));
        for (size_t k = 0; k < x.size(); ++k)
        {
            const float a = std::abs (x[k]);
            e += (a > e ? att : rel) * (a - e);
            env[k] = e;
        }
    }
    auto refineOnset = [&] (double t) {
        const long a = std::max (0L, (long) ((t - 0.045) * kAnalysisRate));
        const long b = std::min ((long) env.size() - 1, (long) ((t + 0.08) * kAnalysisRate));
        if (b <= a) return t;
        float peak = 0, floorV = 1e9f;
        for (long k = a; k <= b; ++k) peak = std::max (peak, env[(size_t) k]);
        for (long k = a; k <= std::min (b, a + (long) (0.01 * kAnalysisRate)); ++k) floorV = std::min (floorV, env[(size_t) k]);
        if (peak <= 0 || floorV > 0.3f * peak) return t; // no hay subida de energía (cambio de nota ligado)
        for (long k = a; k <= b; ++k)
            if (env[(size_t) k] >= 0.2f * peak) return (double) k / kAnalysisRate;
        return t;
    };

    const double frameSec = (double) kHop / kAnalysisRate;
    for (const auto& s : segs)
    {
        VoiceNote n;
        n.startSec = va.frames[s.a].timeSec - 0.5 * frameSec;
        n.endSec = va.frames[s.b].timeSec + 0.5 * frameSec;
        if (n.endSec - n.startSec < o.minNoteSec)
        {
            // demasiado corta: se une a la anterior si es contigua
            if (! va.notes.empty() && n.startSec - va.notes.back().endSec < 0.03) va.notes.back().endSec = n.endSec;
            continue;
        }
        n.startSec = refineOnset (n.startSec);
        if (! va.notes.empty())
        {
            // El final de la nota anterior está "manchado" por la ventana de análisis: se recorta al nuevo ataque.
            auto& prev = va.notes.back();
            n.startSec = std::max (n.startSec, prev.startSec + 0.03);
            if (prev.endSec > n.startSec) prev.endSec = n.startSec;
        }
        std::vector<double> ps, all;
        double conf = 0, lvl = 0;
        int cnt = 0;
        const size_t len = s.b - s.a + 1;
        const size_t skip = std::min (len / 3, (size_t) 6);
        for (size_t k = s.a; k <= s.b; ++k)
        {
            lvl += va.frames[k].rmsDb;
            if (! voiced[k]) continue;
            conf += va.frames[k].confidence;
            ++cnt;
            all.push_back (mid[k]);
            if (k >= s.a + skip) ps.push_back (mid[k]);
        }
        n.levelDb = lvl / (double) len;
        n.pitched = cnt >= (int) std::max<size_t> (3, len / 2);
        if (n.pitched)
        {
            n.midi = median (ps.empty() ? all : ps);
            n.confidence = conf / cnt;
            const double startP = median (std::vector<double> (all.begin(), all.begin() + (long) std::min<size_t> (3, all.size())));
            if (std::abs (startP - n.midi) >= 0.7) n.glideInSemis = startP - n.midi;
            double dev = 0;
            for (double v : ps) dev = std::max (dev, std::abs (v - n.midi));
            n.vibratoSemis = ps.size() >= 8 ? std::min (1.5, dev) : 0.0;
        }
        va.notes.push_back (n);
    }

    // Tempo aproximado a partir de los intervalos entre ataques (puntuación por coincidencia con corcheas).
    if (va.notes.size() >= 4)
    {
        double bestScore = -1, bestBpm = 0;
        for (double bpm = 60; bpm <= 180; bpm += 0.5)
        {
            const double eighth = 30.0 / bpm;
            double score = 0;
            for (size_t k = 1; k < va.notes.size(); ++k)
            {
                const double ioi = va.notes[k].startSec - va.notes[k - 1].startSec;
                const double ph = ioi / eighth;
                score += std::cos (2 * dsp::kPi * ph);
            }
            // ligera preferencia por tempos centrales
            score -= 0.002 * std::abs (bpm - 110);
            if (score > bestScore) { bestScore = score; bestBpm = bpm; }
        }
        va.estimatedBpm = bestBpm;
    }
    if (va.notes.empty()) va.warnings.push_back ("No se detectaron notas");
    return va;
}

// ---------------------------------------------------------------- de voz a solo
const char* humModeName (HumMode m)
{
    switch (m)
    {
        case HumMode::Copy: return "Copiar";
        case HumMode::Correct: return "Corregir";
        case HumMode::Interpret: return "Interpretar";
        case HumMode::Develop: return "Desarrollar";
    }
    return "?";
}

SeedMotif motifFromVoice (const std::vector<VoiceNote>& notes, double bpm, int grid, int maxNotes)
{
    SeedMotif m;
    if (notes.empty()) return m;
    // Primera frase (hasta una pausa >= 0.35 s) si tiene al menos 3 notas.
    size_t end = notes.size();
    for (size_t k = 1; k < notes.size(); ++k)
        if (notes[k].startSec - notes[k - 1].endSec >= 0.35) { end = k; break; }
    if (end < 3) end = notes.size();
    end = std::min (end, (size_t) std::max (1, maxNotes));
    const double ticksPerSec = bpm / 60.0 * kPPQ;
    const double t0 = notes[0].startSec;
    int prev = -1;
    for (size_t k = 0; k < end; ++k)
    {
        const auto& n = notes[k];
        SeedMotifNote sn;
        sn.onset = (int64_t) std::llround ((n.startSec - t0) * ticksPerSec / grid) * grid;
        sn.length = std::max<int64_t> (grid / 2, (int64_t) std::llround ((n.endSec - n.startSec) * ticksPerSec / grid) * grid);
        const int p = n.pitched ? (int) std::lround (n.midi) : (prev < 0 ? 64 : prev);
        sn.interval = prev < 0 ? 0 : std::clamp (p - prev, -24, 24);
        if (k == 0) m.firstPitch = p;
        prev = p;
        if (! m.notes.empty() && sn.onset <= m.notes.back().onset) sn.onset = m.notes.back().onset + grid / 2;
        m.notes.push_back (sn);
    }
    m.lengthTicks = m.notes.back().onset + m.notes.back().length;
    return m;
}

namespace
{
std::vector<int> scalePcs (const Key& k)
{
    static const int major[7] = { 0, 2, 4, 5, 7, 9, 11 }, minor[7] = { 0, 2, 3, 5, 7, 8, 10 };
    std::vector<int> v;
    for (int s : (k.mode == Mode::Major ? major : minor)) v.push_back (mod12 (k.tonic + s));
    return v;
}
} // namespace

HumToSoloResult humToSolo (const VoiceAnalysis& va, const ComposerParams& params, const HumToSoloOptions& o)
{
    HumToSoloResult res;
    Sequence& seq = res.sequence;
    seq.timeSignature = params.timeSignature;
    seq.bars = std::clamp (params.bars, 1, 64);
    seq.bpm = params.bpm;
    if (va.notes.empty()) { res.warnings.push_back ("La toma no contiene notas"); return res; }
    const auto& prof = profileFor (params.instrument);
    const bool rhythmOnly = o.rhythmOnly || ! va.pitchReliable;
    res.usedRhythmOnly = rhythmOnly;
    if (rhythmOnly && ! o.rhythmOnly) res.warnings.push_back ("Altura no fiable: se ha usado solo el ritmo de la voz");

    const double ticksPerSec = params.bpm / 60.0 * kPPQ;
    const double v0 = o.voiceStartSec >= 0 ? o.voiceStartSec : va.notes.front().startSec;
    const int64_t barT = seq.timeSignature.ticksPerBar(), beatT = seq.timeSignature.ticksPerBeat();
    const int64_t base = (int64_t) o.startBar * barT, total = seq.lengthTicks();
    const int64_t grid = std::max (40, o.gridTicks);
    auto q = [&] (double t) {
        const double tick = (double) base + (t - v0) * ticksPerSec;
        const double qt = std::round (tick / (double) grid) * (double) grid;
        return (int64_t) std::llround (tick + std::clamp (o.quantize, 0.0, 1.0) * (qt - tick));
    };

    // Transposición por octavas al registro cómodo del instrumento.
    std::vector<double> vm;
    for (const auto& n : va.notes) if (n.pitched) vm.push_back (n.midi);
    const double center = 0.5 * (prof.range.comfortLow + prof.range.comfortHigh);
    int shift = 0;
    if (! vm.empty())
    {
        const double med = median (vm);
        shift = (int) std::lround ((center - med) / 12.0) * 12;
    }
    double maxLevel = -120;
    for (const auto& n : va.notes) maxLevel = std::max (maxLevel, n.levelDb);

    const auto scale = scalePcs (params.key);
    Rng rng (params.seed ^ 0x6A09E667ull);
    int prevPitch = -1;
    for (size_t k = 0; k < va.notes.size(); ++k)
    {
        const auto& vn = va.notes[k];
        Note n;
        n.startTick = q (vn.startSec);
        const int64_t end = q (vn.endSec);
        n.lengthTicks = std::max<int64_t> (grid / 2, end - n.startTick);
        if (n.startTick < 0 || n.startTick >= total) continue;
        n.velocity = std::clamp ((int) std::lround (100 + 1.5 * (vn.levelDb - maxLevel)), 40, 120);
        const Chord& chord = params.progression.chordAtBar ((int) (n.startTick / barT));
        const bool strong = n.startTick % barT == 0 || (seq.timeSignature.numerator == 4 && n.startTick % barT == 2 * beatT);
        int pitch;
        if (rhythmOnly || ! vn.pitched)
        {
            // Alturas elegidas por la armonía: paso a paso por la escala, notas del acorde en tiempos fuertes.
            if (prevPitch < 0) pitch = nearestPitchWithClass ((int) center, chord.pitchClasses(), prof.range.lowest, prof.range.highest);
            else
            {
                const int step = rng.chance (0.6) ? (rng.chance (0.5) ? 2 : -2) : (rng.chance (0.5) ? 3 : -4);
                int cand = prevPitch + step;
                if (cand > prof.range.comfortHigh) cand -= 5;
                if (cand < prof.range.comfortLow) cand += 5;
                pitch = nearestPitchWithClass (cand, strong ? chord.pitchClasses() : scale, prof.range.lowest, prof.range.highest);
            }
        }
        else
        {
            const double m = vn.midi + shift;
            if (o.mode == HumMode::Copy) pitch = (int) std::lround (m);
            else
            {
                pitch = nearestPitchWithClass ((int) std::lround (m), scale, prof.range.lowest, prof.range.highest);
                // En tiempo fuerte, si la nota cantada está a un semitono de una nota del acorde, se ajusta a ella.
                if (strong && ! chord.containsPitchClass (pitch))
                {
                    const int c = nearestPitchWithClass (pitch, chord.pitchClasses(), prof.range.lowest, prof.range.highest);
                    if (std::abs (c - pitch) <= 1) pitch = c;
                }
            }
            pitch = std::clamp (pitch, 0, 127);
            while (pitch < prof.range.lowest) pitch += 12;
            while (pitch > prof.range.highest) pitch -= 12;
        }
        n.pitch = pitch;
        if (o.mode == HumMode::Interpret && vn.pitched && ! rhythmOnly)
        {
            const bool canBend = params.instrument == InstrumentId::Guitar || params.instrument == InstrumentId::Synth;
            if (vn.glideInSemis <= -0.7 && vn.glideInSemis >= -2.6 && canBend && n.pitch - 2 >= prof.range.lowest)
            {
                const int amount = vn.glideInSemis < -1.5 ? 2 : 1;
                n.pitch -= amount;
                n.expr.bendType = BendType::Up;
                n.expr.bendSemitones = (float) amount;
                n.expr.bendTime = 0.25f;
            }
            else if (std::abs (vn.glideInSemis) >= 0.7 && params.instrument != InstrumentId::Flute)
                n.expr.slideFrom = (int8_t) std::clamp ((int) std::lround (vn.glideInSemis), -5, 5);
            if (vn.vibratoSemis >= 0.15) n.expr.vibratoDepth = (float) std::min (0.6, vn.vibratoSemis);
        }
        n.role = strong ? NoteRole::Target : NoteRole::Free;
        prevPitch = n.targetPitch();
        seq.notes.push_back (n);
        ++res.notesFromVoice;
    }
    // Monofonía y final
    std::sort (seq.notes.begin(), seq.notes.end(), [] (const Note& a, const Note& b) { return a.startTick < b.startTick; });
    for (size_t k = 0; k + 1 < seq.notes.size(); ++k)
        seq.notes[k].lengthTicks = std::max<int64_t> (20, std::min (seq.notes[k].lengthTicks, seq.notes[k + 1].startTick - seq.notes[k].startTick));
    seq.notes.erase (std::remove_if (seq.notes.begin(), seq.notes.end(), [] (const Note& n) { return n.lengthTicks < 20; }), seq.notes.end());
    if (o.mode != HumMode::Copy && ! seq.notes.empty())
    {
        auto& last = seq.notes.back();
        const Chord& ch = params.progression.chordAtBar ((int) (last.startTick / barT));
        const auto st = ch.stablePitchClasses();
        if (std::find (st.begin(), st.end(), mod12 (last.targetPitch())) == st.end())
        {
            last.pitch = nearestPitchWithClass (last.pitch, st, prof.range.lowest, prof.range.highest);
            last.expr.bendType = BendType::None;
            last.expr.bendSemitones = 0;
        }
        last.role = NoteRole::Cadence;
    }
    seq.normalise();

    // Motivo para Creative DNA / Desarrollar (con las alturas ya decididas si solo se usó el ritmo).
    {
        std::vector<VoiceNote> mv;
        for (const auto& n : seq.notes)
        {
            VoiceNote v;
            v.startSec = (double) n.startTick / ticksPerSec;
            v.endSec = (double) n.endTick() / ticksPerSec;
            v.midi = n.targetPitch();
            mv.push_back (v);
        }
        res.motif = motifFromVoice (mv, params.bpm, (int) grid);
    }

    if (o.mode == HumMode::Interpret && ! rhythmOnly)
    {
        // El intérprete añade lo que la voz no indica (ataque, palm mute, legato), sin tocar lo detectado.
        Sequence perf = seq;
        performSequence (perf, params, resolvePerformer (params.style, params.performer, params.instrument), params.seed);
        if (perf.notes.size() == seq.notes.size())
            for (size_t k = 0; k < seq.notes.size(); ++k)
            {
                auto& n = seq.notes[k];
                const auto& p = perf.notes[k];
                n.expr.attack = p.expr.attack;
                n.expr.legato = p.expr.legato;
                n.expr.palmMute = p.expr.palmMute;
                if (n.expr.vibratoDepth <= 0) n.expr.vibratoDepth = p.expr.vibratoDepth;
                n.expr.vibratoRate = p.expr.vibratoRate;
                n.expr.vibratoDelay = p.expr.vibratoDelay;
                n.velocity = p.velocity;
            }
    }
    if (o.mode == HumMode::Develop)
    {
        ComposerParams p = params;
        p.engine = 2;
        p.seedMotifs = { res.motif };
        p.fidelity = o.fidelity;
        p.freedom = o.freedom;
        res.sequence = SoloComposer::compose (p);
        if (res.sequence.notes.empty()) res.warnings.push_back ("No se pudo desarrollar el motivo (parámetros no válidos)");
    }
    return res;
}

Sequence applyVocalCorrection (const Sequence& current, const VoiceAnalysis& take, const ComposerParams& params,
                               HumToSoloOptions opts, int fromBar, int toBar, const std::vector<bool>& lockedBars)
{
    const int64_t barT = current.timeSignature.ticksPerBar();
    fromBar = std::clamp (fromBar, 0, current.bars - 1);
    toBar = std::clamp (toBar, fromBar, current.bars - 1);
    if (opts.mode == HumMode::Develop) opts.mode = HumMode::Interpret; // una corrección local no reescribe el solo
    opts.startBar = fromBar;
    ComposerParams p = params;
    p.bars = current.bars;
    p.timeSignature = current.timeSignature;
    const auto fresh = humToSolo (take, p, opts).sequence;
    const int64_t a = (int64_t) fromBar * barT, b = (int64_t) (toBar + 1) * barT;
    auto barLocked = [&] (int64_t tick) {
        const size_t bi = (size_t) (tick / barT);
        return bi < lockedBars.size() && lockedBars[bi];
    };
    Sequence out = current;
    out.notes.clear();
    std::vector<Note> kept;
    for (const auto& n : current.notes)
    {
        const bool inRange = n.startTick >= a && n.startTick < b;
        if (! inRange || n.locked || barLocked (n.startTick)) kept.push_back (n);
    }
    for (const auto& n : fresh.notes)
    {
        if (n.startTick < a || n.startTick >= b || barLocked (n.startTick)) continue;
        bool clash = false;
        for (const auto& k : kept) clash |= n.startTick < k.endTick() && k.startTick < n.endTick();
        if (! clash) out.notes.push_back (n);
    }
    // Las notas conservadas justo antes del rango no pueden invadir las nuevas.
    for (auto k : kept)
    {
        if (! k.locked && ! barLocked (k.startTick))
            for (const auto& n : out.notes)
                if (k.startTick < n.startTick && k.endTick() > n.startTick) k.lengthTicks = n.startTick - k.startTick;
        out.notes.push_back (k);
    }
    std::sort (out.notes.begin(), out.notes.end(), [] (const Note& x, const Note& y) { return x.startTick < y.startTick; });
    for (size_t k = 0; k + 1 < out.notes.size(); ++k)
        if (! out.notes[k].locked && out.notes[k].endTick() > out.notes[k + 1].startTick)
            out.notes[k].lengthTicks = std::max<int64_t> (1, out.notes[k + 1].startTick - out.notes[k].startTick);
    out.normalise();
    return out;
}

// ---------------------------------------------------------------- voz sintética
MonoAudio synthesiseVoice (const std::vector<SynthVoiceNote>& notes, double sr, double noiseDb, uint64_t seed, bool unpitched)
{
    MonoAudio a;
    a.sampleRate = sr;
    double end = 0;
    for (const auto& n : notes) end = std::max (end, n.startSec + n.durSec);
    a.samples.assign ((size_t) ((end + 0.5) * sr), 0.0f);
    Rng rng (seed);
    auto white = [&] { return (float) (rng.unit() * 2.0 - 1.0); };
    for (const auto& n : notes)
    {
        const size_t s0 = (size_t) (n.startSec * sr), len = (size_t) (n.durSec * sr);
        double phase = 0;
        dsp::Biquad f1, f2, f3;
        f1.peak (sr, 350, 2.0, 12); // formantes de un tarareo ("mmm"/"aa")
        f2.peak (sr, 1100, 3.0, 6);
        f3.lowpass (sr, 3500, 0.7);
        dsp::Biquad nb;
        nb.peak (sr, 2500, 1.0, 10);
        for (size_t k = 0; k < len && s0 + k < a.samples.size(); ++k)
        {
            const double t = (double) k / sr;
            const double att = std::min (1.0, t / 0.035), rel = std::min (1.0, (n.durSec - t) / 0.05);
            const double env = std::max (0.0, std::min (att, rel));
            double y;
            if (unpitched)
            {
                // Sílabas percutidas tipo "ta": ruido filtrado con caída rápida.
                y = nb.process (white()) * 0.5 * std::exp (-t / 0.06) * att;
            }
            else
            {
                double m = n.midi;
                if (n.glideFromSemis != 0) m += n.glideFromSemis * (1.0 - std::min (1.0, t / 0.09));
                if (t > 0.15) m += n.vibratoSemis * std::sin (2 * dsp::kPi * 5.3 * t) * std::min (1.0, (t - 0.15) / 0.2);
                phase += 2 * dsp::kPi * midiToHz (m) / sr;
                double h = 0;
                for (int k2 = 1; k2 <= 16; ++k2) h += std::sin (k2 * phase) / std::pow ((double) k2, 1.3);
                y = f3.process (f2.process (f1.process ((float) (0.12 * h)))) + 0.01 * white(); // + respiración
            }
            a.samples[s0 + k] += (float) (y * env * 0.5);
        }
    }
    const double nl = std::pow (10.0, noiseDb / 20.0);
    for (auto& x : a.samples) x = (float) std::clamp ((double) x + nl * white(), -1.0, 1.0);
    return a;
}

} // namespace solo
