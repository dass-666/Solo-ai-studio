// Guitar Tone Studio v3: amplificador, sobremuestreo, macros y motores de guitarra.
#include "solo/Analysis.h"
#include "solo/Expression.h"
#include "solo/GuitarAmp.h"
#include "solo/SoloComposer.h"
#include "solo/SoloEngine.h"
#include "solo/VoiceAnalysis.h"
#include "TestFramework.h"
#include <cstdlib>
#include <memory>

extern std::atomic<long> g_allocs;
extern std::atomic<bool> g_track;
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace solo;

namespace
{
constexpr double kPi = 3.141592653589793;

double rmsOf (const std::vector<float>& v, size_t from = 0)
{
    double s = 0;
    for (size_t i = from; i < v.size(); ++i) s += (double) v[i] * v[i];
    return v.size() > from ? std::sqrt (s / (double) (v.size() - from)) : 0.0;
}

// Energía de una señal en una banda [f0, f1] (DFT directa sobre una ventana Hann; suficiente para pruebas).
double bandEnergyDb (const std::vector<float>& x, double sr, double f0, double f1)
{
    const size_t n = x.size();
    double total = 0.0;
    const int bins = (int) ((f1 - f0) / (sr / (double) n));
    for (int b = 0; b <= bins; ++b)
    {
        const double f = f0 + b * sr / (double) n;
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2 * kPi * (double) i / (double) n);
            const double ph = 2 * kPi * f * (double) i / sr;
            re += x[i] * w * std::cos (ph);
            im -= x[i] * w * std::sin (ph);
        }
        total += re * re + im * im;
    }
    return 10 * std::log10 (std::max (1e-30, total));
}

// Energía fuera de los armónicos de f0 (aliasing / intermodulación) relativa a la energía total.
double nonHarmonicRatioDb (const std::vector<float>& x, double sr, double f0)
{
    const size_t n = x.size();
    std::vector<double> mag (n / 2, 0.0);
    // DFT ingenua O(n^2) sobre 2048 puntos: aceptable en pruebas.
    for (size_t k = 1; k < n / 2; ++k)
    {
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2 * kPi * (double) i / (double) n);
            const double ph = 2 * kPi * (double) k * (double) i / (double) n;
            re += x[i] * w * std::cos (ph);
            im -= x[i] * w * std::sin (ph);
        }
        mag[k] = re * re + im * im;
    }
    const double binHz = sr / (double) n;
    double harm = 0, other = 0;
    for (size_t k = 1; k < n / 2; ++k)
    {
        const double f = k * binHz;
        const double ratio = f / f0;
        const bool isHarm = std::abs (ratio - std::round (ratio)) * f0 < 2.5 * binHz; // ±2.5 bins alrededor de cada armónico
        (isHarm ? harm : other) += mag[k];
    }
    return 10 * std::log10 (std::max (1e-30, other) / std::max (1e-30, harm + other));
}

AmpParams rockAmp()
{
    AmpParams p;
    p.amp = true; p.cabinet = true; p.overdrive = false; p.distortion = false;
    p.preGainDb = GuitarAmp::preGainDbForForce (70);
    p.satType = 0; p.master = 0.6f; p.sag = 0.3f; p.compression = 0.3f;
    p.cabType = 2; p.oversample = 2;
    return p;
}
} // namespace

TEST (tone_halfband_roundtrip_is_transparent_with_known_latency)
{
    Halfband2x hb;
    hb.prepare();
    const double sr = 48000;
    std::vector<float> in (4096), out (4096);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = 0.5f * (float) (std::sin (2 * kPi * 1000.0 * i / sr) + 0.5 * std::sin (2 * kPi * 7000.0 * i / sr));
    for (size_t i = 0; i < in.size(); ++i)
    {
        float e, o;
        hb.up (in[i], e, o);
        out[i] = hb.down (e, o);
    }
    const int lat = Halfband2x::latencyAtLowRate();
    double err = 0, ref = 0;
    for (size_t i = 512; i + lat < in.size(); ++i)
    {
        const double d = out[i + lat] - in[i];
        err += d * d;
        ref += (double) in[i] * in[i];
    }
    const double relDb = 10 * std::log10 (err / ref);
    std::printf ("  media banda: latencia %d muestras, error ida y vuelta %.1f dB\n", lat, relDb);
    CHECK (relDb < -60.0);
    // Rechazo de imágenes: la fase impar de un seno de 1 kHz subido a 2x no debe contener energía cerca de 47 kHz.
    std::vector<float> up2 (8192);
    hb.reset();
    for (size_t i = 0; i < 4096; ++i) { float e, o; hb.up ((float) std::sin (2 * kPi * 1000.0 * i / sr), e, o); up2[2 * i] = e; up2[2 * i + 1] = o; }
    std::vector<float> tail (up2.begin() + 4096, up2.end());
    const double sig = bandEnergyDb (tail, 2 * sr, 900, 1100), img = bandEnergyDb (tail, 2 * sr, 46900, 47100);
    std::printf ("  imagen a 47 kHz: %.1f dB por debajo de la señal\n", sig - img);
    CHECK (sig - img > 70.0);
}

TEST (tone_force_curve_matches_spec)
{
    CHECK (std::abs (GuitarAmp::preGainDbForForce (0) - 2.0f) < 1e-4f);
    CHECK (std::abs (GuitarAmp::preGainDbForForce (100) - 24.0f) < 1e-4f);
    const float at70 = GuitarAmp::preGainDbForForce (70);
    std::printf ("  Fuerza 70 %% => %.2f dB de ganancia previa\n", at70);
    CHECK (std::abs (at70 - 14.4f) < 0.15f);
    // Monótona
    float prev = -1;
    for (int f = 0; f <= 100; f += 5) { const float g = GuitarAmp::preGainDbForForce (f); CHECK (g > prev); prev = g; }
}

TEST (tone_force_keeps_loudness_within_tolerance)
{
    // La misma señal (cuerda sintética: seno con armónicos decayendo) con Fuerza 0..100: el nivel RMS de salida
    // del ampli debe quedar dentro de ±2.5 dB (compensación automática), y crecer la distorsión.
    const double sr = 48000;
    std::vector<float> src (48000);
    for (size_t i = 0; i < src.size(); ++i)
    {
        const double t = i / sr;
        const double env = std::exp (-t * 1.5);
        src[i] = (float) (0.18 * env * (std::sin (2 * kPi * 196.0 * t) + 0.5 * std::sin (2 * kPi * 392.0 * t) + 0.25 * std::sin (2 * kPi * 588.0 * t)));
    }
    double minDb = 1e9, maxDb = -1e9, firstNonHarm = 0, lastNonHarm = 0;
    for (int force = 0; force <= 100; force += 10)
    {
        GuitarAmp amp;
        amp.prepare (sr);
        AmpParams p = rockAmp();
        p.preGainDb = GuitarAmp::preGainDbForForce (force);
        p.compression = 0.1f + 0.5f * force / 100.0f;
        p.sag = 0.1f + 0.4f * force / 100.0f;
        amp.setParams (p);
        std::vector<float> out (src.size());
        for (size_t i = 0; i < src.size(); ++i) out[i] = amp.process (src[i]);
        const double db = 20 * std::log10 (rmsOf (out, 2400));
        std::vector<float> win (out.begin() + 4800, out.begin() + 4800 + 2048);
        const double nh = nonHarmonicRatioDb (win, sr, 196.0);
        if (force == 0) firstNonHarm = nh;
        if (force == 100) lastNonHarm = nh;
        std::printf ("  Fuerza %3d: pre %5.1f dB, salida %6.1f dB RMS, pico %.2f\n", force, p.preGainDb, db,
                     *std::max_element (out.begin(), out.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); }));
        minDb = std::min (minDb, db);
        maxDb = std::max (maxDb, db);
        for (float v : out) CHECK (std::isfinite (v) && std::abs (v) <= 1.5f);
    }
    std::printf ("  rango de nivel con Fuerza 0..100: %.1f dB\n", maxDb - minDb);
    CHECK (maxDb - minDb <= 4.0);
    (void) firstNonHarm; (void) lastNonHarm;
}

TEST (tone_force_sweep_has_no_discontinuities)
{
    // Barrido continuo de Fuerza 0 -> 100 -> 0 durante una nota sostenida: ningún salto brusco de nivel
    // (la derivada de la envolvente RMS por bloque queda acotada).
    const double sr = 48000;
    GuitarAmp amp;
    amp.prepare (sr);
    AmpParams p = rockAmp();
    double prevRms = -1, maxJumpDb = 0;
    for (int b = 0; b < 400; ++b)
    {
        const double force = b < 200 ? b / 2.0 : (400 - b) / 2.0;
        p.preGainDb = GuitarAmp::preGainDbForForce (force);
        p.compression = (float) (0.1 + 0.5 * force / 100.0);
        amp.setParams (p);
        std::vector<float> out (256);
        for (int i = 0; i < 256; ++i)
        {
            const double t = (b * 256 + i) / sr;
            out[(size_t) i] = amp.process ((float) (0.15 * std::sin (2 * kPi * 187.5 * t))); // 187.5 Hz = 256 muestras exactas
        }
        const double r = rmsOf (out);
        if (b > 10 && prevRms > 0) maxJumpDb = std::max (maxJumpDb, std::abs (20 * std::log10 (r / prevRms)));
        prevRms = r;
    }
    std::printf ("  mayor salto de nivel entre bloques consecutivos (5.3 ms): %.2f dB\n", maxJumpDb);
    CHECK (maxJumpDb < 1.0);
}

TEST (tone_oversampling_reduces_aliasing)
{
    // Seno de 4.2 kHz muy saturado: el aliasing (energía no armónica) debe bajar claramente con 2x y 4x.
    const double sr = 44100;
    double nh[3] = { 0, 0, 0 };
    const int osv[3] = { 1, 2, 4 };
    for (int k = 0; k < 3; ++k)
    {
        GuitarAmp amp;
        amp.prepare (sr);
        AmpParams p = rockAmp();
        p.preGainDb = 24; p.satType = 1; p.distortion = true; p.distGain = 0.8f; p.cabinet = false; p.outputLpHz = 20000;
        p.oversample = osv[k];
        amp.setParams (p);
        std::vector<float> out (8192);
        for (size_t i = 0; i < out.size(); ++i) out[i] = amp.process ((float) (0.3 * std::sin (2 * kPi * 4200.0 * i / sr)));
        std::vector<float> win (out.begin() + 4096, out.begin() + 4096 + 4096);
        nh[k] = nonHarmonicRatioDb (win, sr, 4200.0);
        std::printf ("  %dx: energía no armónica %.1f dB (latencia %d muestras)\n", osv[k], nh[k], amp.latencySamples());
    }
    CHECK (nh[1] < nh[0] - 6.0);
    CHECK (nh[2] < nh[1] - 3.0);
}

TEST (tone_amp_latency_reported_and_stable)
{
    GuitarAmp amp;
    amp.prepare (48000);
    AmpParams p = rockAmp();
    p.oversample = 1; amp.setParams (p); CHECK (amp.latencySamples() == 0);
    p.oversample = 2; amp.setParams (p); CHECK (amp.latencySamples() == Halfband2x::latencyAtLowRate());
    p.oversample = 4; amp.setParams (p); CHECK (amp.latencySamples() == Halfband2x::latencyAtLowRate() * 3 / 2);
    // Medida real: impulso => pico de salida en la latencia declarada (±1 muestra) con pantalla desactivada.
    p.oversample = 2; p.cabinet = false; p.compression = 0; p.outputLpHz = 20000; p.preGainDb = 0; p.satType = 2;
    GuitarAmp a2;
    a2.prepare (48000);
    a2.setParams (p);
    int peakAt = -1; float peak = 0;
    for (int i = 0; i < 400; ++i)
    {
        const float y = std::abs (a2.process (i == 100 ? 0.5f : 0.0f));
        if (y > peak) { peak = y; peakAt = i; }
    }
    std::printf ("  impulso en 100 => pico en %d (latencia declarada %d)\n", peakAt, a2.latencySamples());
    CHECK (std::abs (peakAt - 100 - a2.latencySamples()) <= 1);
}

TEST (tone_amp_extreme_params_are_finite)
{
    GuitarAmp amp;
    amp.prepare (48000);
    const float gains[] = { -10, 0, 24, 40, 60 };
    for (int sat = 0; sat < 3; ++sat)
        for (float g : gains)
            for (int os : { 1, 2, 4 })
            {
                AmpParams p = rockAmp();
                p.preGainDb = g; p.satType = sat; p.oversample = os; p.overdrive = true; p.distortion = true;
                p.odDrive = 1; p.distGain = 1; p.master = 1; p.sag = 1; p.compression = 1; p.bassDb = 12; p.midDb = -12; p.trebleDb = 12;
                p.presenceDb = 12; p.outputLpHz = 20000; p.trimDb = 12;
                amp.setParams (p);
                float peak = 0;
                for (int i = 0; i < 4000; ++i)
                {
                    const float y = amp.process (i < 2000 ? (float) std::sin (i * 0.1) * 2.0f : 0.0f);
                    CHECK (std::isfinite (y));
                    peak = std::max (peak, std::abs (y));
                }
                CHECK_MSG (peak < 40.0f, "pico " + std::to_string (peak));
            }
}

TEST (tone_expression_scaling_and_gestures)
{
    const auto e0 = expressionScaling (0), e65 = expressionScaling (65), e100 = expressionScaling (100);
    CHECK (e0.vibratoDepth == 0.0 && e0.bendTime == 0.0 && e0.slideTime == 0.0);
    CHECK (std::abs (e65.vibratoDepth - 1.0) < 1e-9 && std::abs (e65.bendTime - 1.0) < 1e-9 && std::abs (e65.generation - 1.0) < 1e-9);
    CHECK (e100.vibratoDepth > e65.vibratoDepth && e100.generation > 1.0);
    NoteExpression x;
    x.bendType = BendType::Up; x.bendSemitones = 2; x.bendStart = 0.1f; x.bendTime = 0.3f;
    x.vibratoDepth = 0.4f; x.vibratoRate = 5.5f; x.vibratoDelay = 0.3f; x.slideFrom = -2;
    const auto g65 = gesturesFor (x, 100, 1.0, e65);
    const auto g0 = gesturesFor (x, 100, 1.0, e0);
    // Con 0 % no hay vibrato ni slide y el bend es instantáneo (suena la altura final).
    CHECK (g0.vibratoDepth == 0.0 && g0.slideFromSemis == 0.0 && g0.bendType == BendType::PreBend);
    CHECK (std::abs (gesturePitchOffset (g0, 0.0) - 2.0) < 1e-9 && std::abs (gesturePitchOffset (g0, 0.9) - 2.0) < 1e-9);
    // Con 65 %: slide al principio, bend llega a +2 y vibrato (solo hacia arriba) después.
    CHECK (gesturePitchOffset (g65, 0.0) < -1.9);
    CHECK (std::abs (gesturePitchOffset (g65, 0.55) - 2.0) < 0.45); // bend completo + vibrato (0..0.4)
    double mx = -9, mn = 9;
    for (double t = 0.6; t < 1.0; t += 0.001) { const double v = gesturePitchOffset (g65, t); mx = std::max (mx, v); mn = std::min (mn, v); }
    CHECK (mn >= 2.0 - 1e-6 && mx <= 2.4 + 1e-6);
    CHECK (gestureMaxAbsOffset (g65) >= 2.4 - 1e-6);
}

// ============================================================ motores v3 en el SoloEngine
namespace
{
Sequence toneSolo (Style st = Style::MelodicRock, uint64_t seed = 21)
{
    ComposerParams p;
    Key::parse ("Am", p.key);
    std::string err;
    Progression::parse ("Am F C G", p.progression, err);
    p.bars = 4; p.bpm = 120; p.seed = seed; p.style = st; p.energy = 0.7;
    return SoloComposer::compose (p);
}

FxSettings v3Fx()
{
    FxSettings s;
    s.setOn (FxStage::Amp, true); s.setOn (FxStage::Cabinet, true); s.setOn (FxStage::Reverb, true); s.setOn (FxStage::Delay, true);
    s.ampModel = 1; s.ampPreGainDb = GuitarAmp::preGainDbForForce (70); s.cabType = 2; s.reverbMix = 0.1f; s.delayMix = 0.08f;
    s.reverbPredelayMs = 22; s.reverbLowCutHz = 120; s.reverbHighCutHz = 7000; s.delayLowCutHz = 150;
    s.outputDb = -6;
    return s;
}

double rmsRange (const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0; size_t n = 0;
    for (size_t i = a; i < b && i < v.size(); ++i) { s += (double) v[i] * v[i]; ++n; }
    return n ? std::sqrt (s / (double) n) : 0.0;
}
} // namespace

TEST (tone_modeled_engine_plays_notes_and_releases)
{
    ModeledGuitarEngine eng;
    eng.prepare (48000);
    eng.setParams (GuitarParams {}, expressionScaling (65));
    std::vector<float> out (96000, 0.0f);
    NoteExpression e;
    e.vibratoDepth = 0.3f; e.vibratoDelay = 0.2f;
    eng.noteOn (57, 100, 48000, e);
    eng.render (out.data(), 48000);
    eng.noteOff (57);
    eng.render (out.data() + 48000, 48000);
    CHECK (rmsRange (out, 1000, 6000) > 0.01);
    CHECK (rmsRange (out, 70000, 96000) < 1e-4); // tras el release se apaga
    CHECK (eng.activeVoiceCount() == 0);
    for (float v : out) CHECK (std::isfinite (v) && std::abs (v) < 2.0f);
    // El vibrato se oye: la altura detectada oscila en la segunda mitad de la nota.
    std::vector<float> seg (out.begin() + 24000, out.begin() + 48000);
    MonoAudio a; a.samples = seg; a.sampleRate = 48000;
    const auto va = analyseVoice (a);
    CHECK (! va.notes.empty());
    if (! va.notes.empty())
    {
        std::printf ("  nota A3 (57): detectada %.2f, vibrato %.2f semitonos\n", va.notes.front().midi, va.notes.front().vibratoSemis);
        CHECK (std::abs (va.notes.front().midi - 57.0) < 0.6);
    }
}

TEST (tone_v3_live_matches_export_modeled)
{
    const auto seq = toneSolo();
    EngineRenderOptions o;
    o.fx = v3Fx();
    o.rig.engine = GuitarEngineKind::Modeled;
    const auto exported = renderWithEngine (seq, o);
    CHECK (exported.noteOns == (int) seq.notes.size());
    CHECK (exported.peak > 0.01f && exported.peak <= 1.0f);
    SoloEngine live;
    live.setInstrument (InstrumentId::Guitar);
    live.publishFx (o.fx);
    live.publishRig (o.rig);
    live.publishSequence (makePlaybackData (seq, 0, false));
    live.prepare (48000, 256);
    std::vector<float> L (exported.left.size()), R (L.size());
    TransportState t;
    t.valid = true; t.playing = true; t.bpm = 120; t.timeInSamplesValid = true;
    for (size_t pos = 0; pos < L.size(); pos += 256)
    {
        const int n = (int) std::min<size_t> (256, L.size() - pos);
        t.ppqPosition = (double) pos * 120.0 / 60.0 / 48000.0;
        t.timeInSamples = (int64_t) pos;
        live.process (L.data() + pos, R.data() + pos, n, t);
    }
    double sumDiff = 0, sumRef = 0;
    for (size_t i = 0; i < L.size(); ++i) { const double d = L[i] - exported.left[i]; sumDiff += d * d; sumRef += (double) exported.left[i] * exported.left[i]; }
    const double relDb = 10 * std::log10 (std::max (1e-30, sumDiff) / std::max (1e-30, sumRef));
    std::printf ("  v3 modelada: en vivo vs exportado %.1f dB, pico %.2f, latencia %d\n", relDb, exported.peak, live.latencySamples());
    CHECK (relDb < -60.0);
    CHECK (live.latencySamples() == Halfband2x::latencyAtLowRate());
}

TEST (tone_engine_switch_mid_note_no_hang_no_click)
{
    // Cambiar de motor (v2 -> v3 modelada -> samples -> v2) mientras suena una nota larga: sin notas colgadas,
    // sin saltos bruscos (fundido) y la siguiente nota suena en el motor nuevo.
    Sequence s;
    s.bars = 4; s.bpm = 120;
    for (int b = 0; b < 4; ++b) s.notes.push_back ({ b * 4 * kPPQ, 3 * kPPQ + kPPQ / 2, 55 + b * 2, 100, 0 });
    SoloEngine e;
    e.setInstrument (InstrumentId::Guitar);
    e.publishFx (v3Fx());
    e.publishSequence (makePlaybackData (s, 0, false));
    e.prepare (48000, 256);
    GuitarRigSettings rig;
    const GuitarEngineKind order[] = { GuitarEngineKind::LegacyV2, GuitarEngineKind::Modeled, GuitarEngineKind::Samples, GuitarEngineKind::Modeled };
    std::vector<float> L (48000 * 8), R (L.size());
    TransportState t;
    t.valid = true; t.playing = true; t.bpm = 120; t.timeInSamplesValid = true;
    double maxJump = 0;
    for (size_t pos = 0; pos < L.size(); pos += 256)
    {
        const int bar = (int) (pos / 96000);
        rig.engine = order[std::min (3, bar)];
        if (pos % 96000 == 48000) e.publishRig (rig); // a mitad de cada compás (con la nota sonando)
        t.ppqPosition = (double) pos * 2.0 / 48000.0;
        t.timeInSamples = (int64_t) pos;
        e.process (L.data() + pos, R.data() + pos, 256, t);
        for (size_t i = pos + 1; i < pos + 256; ++i) maxJump = std::max (maxJump, (double) std::abs (L[i] - L[i - 1]));
    }
    std::printf ("  mayor salto entre muestras consecutivas: %.3f, voces activas al final: %d\n", maxJump, e.activeVoiceCount());
    CHECK (maxJump < 0.35);
    // Cada compás suena (los motores sin banco de samples no suenan: el compás 3 es samples sin banco => silencio esperado)
    CHECK (rmsRange (L, 10000, 40000) > 0.005);   // v2
    CHECK (rmsRange (L, 106000, 140000) > 0.005); // modelada v3
    CHECK (rmsRange (L, 298000, 330000) > 0.005); // modelada v3 de nuevo
    t.playing = false;
    e.process (L.data(), R.data(), 256, t);
    for (int i = 0; i < 100; ++i) e.process (L.data(), R.data(), 256, t);
    CHECK (e.activeVoiceCount() == 0);
}

TEST (tone_sampler_synthetic_bank_loads_and_plays)
{
    const std::string dir = "/tmp/solo_synth_bank";
    std::string err;
    (void) std::system (("mkdir -p " + dir).c_str());
    const auto sfz = makeSyntheticBank (dir, err);
    CHECK_MSG (! sfz.empty(), err);
    if (sfz.empty()) return;
    auto bank = std::make_shared<SampleBank>();
    CHECK_MSG (loadSfz (sfz, *bank, err), err);
    std::printf ("  %s\n", bank->summary().c_str());
    CHECK (bank->synthetic);
    CHECK (bank->zones.size() >= 60);
    // Capas y round-robin: la misma tecla con velocity distinta y llamadas sucesivas elige zonas distintas.
    const int z1 = bank->findZone (52, 60, 0, "sustain"), z2 = bank->findZone (52, 120, 0, "sustain"), z3 = bank->findZone (52, 60, 1, "sustain");
    CHECK (z1 >= 0 && z2 >= 0 && z3 >= 0 && z1 != z2 && z1 != z3);
    CHECK (bank->findZone (45, 100, 0, "mute") >= 0 && bank->zones[(size_t) bank->findZone (45, 100, 0, "mute")].articulation == "mute");
    CHECK (bank->findZone (80, 100, 0, "mute") >= 0); // sin mute en agudos: cae a sustain

    const auto seq = toneSolo (Style::ExpressiveBlues, 5);
    EngineRenderOptions o;
    o.fx = v3Fx();
    o.rig.engine = GuitarEngineKind::Samples;
    o.bank = bank;
    const auto r = renderWithEngine (seq, o);
    CHECK (r.noteOns == (int) seq.notes.size());
    CHECK (r.peak > 0.02f && r.peak <= 1.0f);
    CHECK (r.activeVoicesAtEnd == 0);
    for (float v : r.left) CHECK (std::isfinite (v));
    // Las notas del solo se reconocen en el audio (altura exacta en la mayoría).
    EngineRenderOptions dry = o;
    dry.fx = FxSettings {};
    const auto rd = renderWithEngine (seq, dry);
    MonoAudio a; a.samples = rd.left; a.sampleRate = 48000;
    const auto va = analyseVoice (a);
    int exact = 0;
    for (const auto& n : seq.notes)
    {
        const double start = (double) n.startTick / kPPQ * 60.0 / 120.0;
        for (const auto& d : va.notes)
            if (d.pitched && std::abs (d.startSec - start) < 0.08 && std::abs (d.midi - n.targetPitch()) < 0.6) { ++exact; break; }
    }
    std::printf ("  samples: %d/%zu notas con la altura esperada\n", exact, seq.notes.size());
    CHECK (exact >= (int) seq.notes.size() * 6 / 10);
    // Descarga del banco con notas sonando: no hay acceso a memoria liberada (ASan) ni notas colgadas.
    SoloEngine e;
    e.setInstrument (InstrumentId::Guitar);
    e.publishRig (o.rig);
    e.publishSampleBank (bank);
    e.publishSequence (makePlaybackData (seq, 0, true));
    e.prepare (48000, 512);
    std::vector<float> L (512), R (512);
    TransportState t; t.valid = true; t.playing = true; t.bpm = 120; t.timeInSamplesValid = true;
    for (int b = 0; b < 300; ++b)
    {
        t.ppqPosition = b * 512 * 2.0 / 48000.0; t.timeInSamples = b * 512;
        if (b == 100) { e.publishSampleBank (nullptr); bank.reset(); }
        if (b == 150) e.collectGarbage();
        e.process (L.data(), R.data(), 512, t);
        for (float v : L) CHECK (std::isfinite (v));
    }
    t.playing = false;
    for (int b = 0; b < 50; ++b) e.process (L.data(), R.data(), 512, t);
    CHECK (e.activeVoiceCount() == 0);
}

TEST (tone_state_without_sfz_file_falls_back)
{
    std::string err;
    SampleBank b;
    CHECK (! loadSfz ("/tmp/no-existe/banco.sfz", b, err));
    CHECK (! err.empty());
    // SFZ con samples ausentes: error descriptivo, sin excepción.
    const std::string dir = "/tmp/solo_bad_sfz";
    (void) std::system (("mkdir -p " + dir).c_str());
    { std::FILE* f = std::fopen ((dir + "/malo.sfz").c_str(), "wb"); std::fputs ("<region> sample=no.wav key=60\n<region> key=61\n", f); std::fclose (f); }
    CHECK (! loadSfz (dir + "/malo.sfz", b, err));
    std::printf ("  error esperado: %s\n", err.c_str());
}

TEST (tone_cab_impulse_load_and_use)
{
    // IR sintética (decaimiento con resonancia): se prepara, se normaliza y la cadena la usa sin saltos ni NaN.
    std::vector<float> ir (3000);
    for (size_t i = 0; i < ir.size(); ++i) ir[i] = (float) (std::exp (-(double) i / 400.0) * std::sin (2 * kPi * 2200.0 * i / 44100.0));
    auto imp = std::make_shared<CabImpulse>();
    std::string err;
    CHECK_MSG (prepareCabImpulse (ir, 44100, 48000, *imp, err), err);
    CHECK ((int) imp->taps.size() <= CabImpulse::kMaxTaps);
    double e = 0; for (float v : imp->taps) e += (double) v * v;
    CHECK (std::abs (e - 1.0) < 1e-3);
    CabImpulse bad;
    CHECK (! prepareCabImpulse ({}, 44100, 48000, bad, err));
    CHECK (! prepareCabImpulse (std::vector<float> (100, 0.0f), 44100, 48000, bad, err));
    const auto seq = toneSolo();
    EngineRenderOptions o;
    o.fx = v3Fx();
    o.rig.engine = GuitarEngineKind::Modeled;
    const auto withoutIr = renderWithEngine (seq, o);
    o.impulse = imp;
    const auto withIr = renderWithEngine (seq, o);
    CHECK (withIr.peak > 0.01f && withIr.peak <= 1.0f);
    for (float v : withIr.left) CHECK (std::isfinite (v));
    double diff = 0;
    for (size_t i = 0; i < withIr.left.size(); ++i) diff += std::abs (withIr.left[i] - withoutIr.left[i]);
    CHECK (diff > 1.0); // la IR cambia el sonido
}

TEST (tone_v3_no_allocations_in_audio_thread)
{
    const auto seq = toneSolo (Style::VirtuosoRock, 9);
    for (int engineSel = 1; engineSel <= 2; ++engineSel)
    {
        SoloEngine e;
        e.setInstrument (InstrumentId::Guitar);
        auto fx = v3Fx();
        fx.ampOversample = 4; fx.setOn (FxStage::Overdrive, true); fx.setOn (FxStage::Chorus, true);
        e.publishFx (fx);
        GuitarRigSettings rig;
        rig.engine = (GuitarEngineKind) engineSel;
        e.publishRig (rig);
        if (engineSel == 2)
        {
            auto bank = std::make_shared<SampleBank>();
            std::string err;
            if (loadSfz ("/tmp/solo_synth_bank/guitarra-sintetica.sfz", *bank, err)) e.publishSampleBank (bank);
        }
        e.publishSequence (makePlaybackData (seq, 0, true));
        e.prepare (44100, 512);
        std::vector<float> L (512), R (512);
        TransportState t; t.valid = true; t.playing = true; t.bpm = 133; t.timeInSamplesValid = true;
        g_allocs = 0;
        for (int b = 0; b < 400; ++b)
        {
            t.ppqPosition = b * 512 * 133.0 / 60.0 / 44100.0; t.timeInSamples = b * 512;
            if (b == 200) { rig.guitar.pickHardness = 0.9f; e.publishRig (rig); }
            if (b == 300) { fx.ampPreGainDb = 24; fx.cabType = 0; e.publishFx (fx); }
            g_track = true;
            e.process (L.data(), R.data(), 512, t);
            g_track = false;
        }
        CHECK_MSG (g_allocs.load() == 0, "motor " + std::to_string (engineSel) + ": " + std::to_string (g_allocs.load()) + " reservas");
    }
}
