#include "solo/InstrumentEngine.h"
#include <algorithm>
#include <cmath>

namespace solo
{
namespace
{
constexpr double kTwoPi = 6.283185307179586;
constexpr int kDelaySize = 8192;

inline double polyBlep (double t, double dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

inline float softLimit (float x)
{
    const float a = std::abs (x);
    if (a <= 0.7f) return x;
    const float y = 0.7f + 0.3f * std::tanh ((a - 0.7f) / 0.3f);
    return x < 0 ? -y : y;
}

inline double smoothStep (double u) { u = std::clamp (u, 0.0, 1.0); return 0.5 - 0.5 * std::cos (3.141592653589793 * u); }
inline double semisToHz (double s) { return 440.0 * std::pow (2.0, (s - 69.0) / 12.0); }

} // namespace

InstrumentEngine::InstrumentEngine()
{
    for (auto& v : voices) v.delay.assign (kDelaySize, 0.0f);
}

void InstrumentEngine::prepare (double sr)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    for (auto& v : voices)
    {
        v.stage = Stage::Off;
        v.env = 0.0f;
        std::fill (v.delay.begin(), v.delay.end(), 0.0f);
    }
    currentInst = pendingInstrument.load();
    smoothedGain = outputGain.load();
    bodyHp.highpass (sampleRate, 85.0);
    bodyPeak.peak (sampleRate, 2600.0, 0.9, 3.5);
    bodyLp.lowpass (sampleRate, 7500.0, 0.6);
    bodyHp.reset(); bodyPeak.reset(); bodyLp.reset();
}

InstrumentEngine::Preset InstrumentEngine::presetFor (int inst)
{
    switch ((InstrumentId) inst)
    {
        case InstrumentId::Guitar: return { 0.001f, 0.05f, 1.0f, 0.07f };
        case InstrumentId::GuitarLite: return { 0.002f, 0.05f, 1.0f, 0.09f };
        case InstrumentId::Flute: return { 0.06f, 0.12f, 0.85f, 0.14f };
        case InstrumentId::Synth: return { 0.004f, 0.25f, 0.7f, 0.12f };
    }
    return { 0.01f, 0.1f, 0.8f, 0.1f };
}

float InstrumentEngine::noise()
{
    noiseState = noiseState * 1664525u + 1013904223u;
    return (float) ((int32_t) noiseState) * (1.0f / 2147483648.0f);
}

InstrumentEngine::Voice* InstrumentEngine::findLegatoVoice()
{
    Voice* best = nullptr;
    for (auto& v : voices)
        if (v.stage != Stage::Off && v.stage != Stage::FastRelease && v.env > 0.02f && v.inst == currentInst)
            if (! best || v.age > best->age) best = &v;
    return best;
}

void InstrumentEngine::configureString (Voice& v)
{
    // Tiempo de decaimiento (T60) según altura, palm mute y estado de la nota.
    const double pitchNorm = std::clamp ((v.baseSemis - 52.0) / 36.0, 0.0, 1.0);
    double t60 = 5.0 - 2.4 * pitchNorm; // sustain de guitarra eléctrica (cuerdas agudas decaen antes)
    double damp = 0.62 + 0.25 * v.velocity; // brillo del lazo (más púa = más armónicos)
    if (v.expr.palmMute) { t60 = 0.22; damp = 0.24; }
    if (v.released) { t60 = 0.07; damp = 0.3; }
    const double f = semisToHz (v.baseSemis);
    v.loopGain = (float) std::exp (-6.907755 / (t60 * f));
    v.loopDamp = (float) damp;
}

void InstrumentEngine::exciteString (Voice& v, float amount, float brightness, bool fullBurst)
{
    const double f = semisToHz (v.baseSemis + v.glideFrom);
    const double d = std::clamp (sampleRate / f, 4.0, (double) kDelaySize - 8.0);
    const int n = (int) std::ceil (d) + 2;
    const int burst = fullBurst ? n : std::max (8, n / 5);
    const int pick = std::max (1, (int) std::lround (0.13 * d)); // posición de púa cerca del puente
    float lp = 0.0f;
    float prev[512] {};
    int prevCount = 0;
    for (int i = 0; i < burst; ++i)
    {
        lp += brightness * (noise() - lp);
        // filtro peine de posición de púa: e[i] - e[i - pick]
        float e = lp;
        if (prevCount >= pick && pick < 512) e -= prev[(prevCount - pick) % 512];
        prev[prevCount % 512] = lp;
        ++prevCount;
        const size_t idx = (size_t) ((v.writePos - burst + i + kDelaySize) % kDelaySize);
        if (fullBurst) v.delay[idx] = 0.25f * v.delay[idx] + amount * e;
        else v.delay[idx] += amount * e;
    }
}

void InstrumentEngine::noteOn (int pitch, int velocity, int lengthSamples, const NoteExpression& expr)
{
    if (pitch < 0 || pitch > 127) return;
    if (velocity <= 0) { noteOff (pitch); return; }

    // Legato (hammer-on, pull-off, slide ligado): reutiliza la voz que está sonando sin nuevo ataque.
    if (expr.legato)
    {
        if (Voice* lv = findLegatoVoice())
        {
            Voice& v = *lv;
            const double from = v.baseSemis + pitchOffset (v);
            v.pitch = pitch;
            v.baseSemis = pitch;
            v.glideFrom = from - pitch;
            const bool slide = expr.slideFrom != 0;
            const double glideSec = slide ? 0.07 : (v.inst == (int) InstrumentId::Synth ? 0.05 : 0.004);
            v.glideSamples = std::max (1, (int) (glideSec * sampleRate));
            v.glidePos = 0;
            v.pos = 0;
            v.lengthSamples = lengthSamples;
            v.expr = expr;
            v.expr.slideFrom = 0; // el deslizamiento ya lo hace el glide desde la nota anterior
            v.velocity = std::max (v.velocity * 0.85f, (float) velocity / 127.0f);
            v.age = ++ageCounter;
            v.released = false;
            if (v.stage == Stage::Release) v.stage = Stage::Sustain;
            if (v.inst == (int) InstrumentId::Guitar)
            {
                configureString (v);
                if (! slide) exciteString (v, 0.18f * v.velocity, 0.5f, false); // golpe del dedo
            }
            return;
        }
    }

    Voice* target = nullptr;
    for (auto& v : voices)
        if (v.stage != Stage::Off && v.pitch == pitch && v.inst == currentInst) { target = &v; break; }
    if (! target)
        for (auto& v : voices)
            if (v.stage == Stage::Off) { target = &v; break; }
    if (! target)
    {
        Voice* oldest = nullptr;
        for (auto& v : voices)
        {
            const bool released = v.stage == Stage::Release || v.stage == Stage::FastRelease;
            if (! oldest) { oldest = &v; continue; }
            const bool oldestReleased = oldest->stage == Stage::Release || oldest->stage == Stage::FastRelease;
            if ((released && ! oldestReleased) || (released == oldestReleased && v.age < oldest->age)) oldest = &v;
        }
        target = oldest;
    }

    Voice& v = *target;
    const bool wasSounding = v.stage != Stage::Off && v.inst == currentInst;
    v.inst = currentInst;
    v.pitch = pitch;
    v.velocity = (float) velocity / 127.0f;
    v.age = ++ageCounter;
    v.baseSemis = pitch;
    v.expr = expr;
    v.lengthSamples = lengthSamples;
    v.pos = 0;
    v.released = false;
    v.vibPhase = 0.0;
    v.glideFrom = expr.slideFrom;
    v.glidePos = 0;
    v.glideSamples = expr.slideFrom != 0 ? std::max (1, (int) std::min (0.09 * sampleRate, 0.3 * std::max (1, lengthSamples))) : 0;
    v.stage = Stage::Attack;
    if (v.inst != (int) InstrumentId::Flute || ! wasSounding) v.env = 0.0f;

    if (v.inst == (int) InstrumentId::Guitar)
    {
        configureString (v);
        const float att = expr.attack / 127.0f;
        const float bright = expr.palmMute ? 0.25f + 0.2f * att : 0.3f + 0.35f * v.velocity + 0.3f * att;
        exciteString (v, 0.55f + 0.45f * v.velocity, std::min (0.97f, bright), true);
        v.clickEnv = 0.25f * att * v.velocity;
        v.loopState = 0.0f;
        v.lp1 = 0.0f;
    }
    else if (v.inst == (int) InstrumentId::GuitarLite)
    {
        // Modelo S0–S2 (Karplus-Strong básico)
        const double d = std::clamp (sampleRate / semisToHz (pitch) - 0.5, 2.0, (double) kDelaySize - 4.0);
        const int n = (int) std::ceil (d) + 2;
        const float bright = 0.25f + 0.65f * v.velocity;
        float lp = 0.0f;
        for (int i = n; i > 0; --i)
        {
            lp += bright * (noise() - lp);
            v.delay[(size_t) ((v.writePos - i + kDelaySize) % kDelaySize)] = lp * 0.9f;
        }
        v.loopState = 0.0f;
        v.lp1 = 0.0f;
    }
    else
    {
        v.phase1 = 0.0;
        v.phase2 = 0.37;
    }
}

void InstrumentEngine::noteOff (int pitch)
{
    for (auto& v : voices)
        if (v.pitch == pitch && v.stage != Stage::Off && v.stage != Stage::Release && v.stage != Stage::FastRelease)
        {
            v.stage = Stage::Release;
            v.released = true;
            if (v.inst == (int) InstrumentId::Guitar) configureString (v);
        }
}

void InstrumentEngine::allNotesOff (bool immediate)
{
    for (auto& v : voices)
        if (v.stage != Stage::Off)
        {
            v.stage = immediate ? Stage::FastRelease : Stage::Release;
            v.released = true;
        }
}

int InstrumentEngine::activeVoiceCount() const
{
    int n = 0;
    for (const auto& v : voices) n += v.stage != Stage::Off ? 1 : 0;
    return n;
}

void InstrumentEngine::advanceEnvelope (Voice& v, const Preset& p)
{
    const float sr = (float) sampleRate;
    switch (v.stage)
    {
        case Stage::Attack:
        {
            // Ataque suave (swell) cuando attack < 30: entrada de volumen tipo "violín".
            float a = p.attack;
            if (v.expr.attack < 30) a = std::max (a, 0.12f * (1.0f - v.expr.attack / 30.0f));
            v.env += 1.0f / std::max (1.0f, a * sr);
            if (v.env >= 1.0f) { v.env = 1.0f; v.stage = Stage::Decay; }
            break;
        }
        case Stage::Decay:
        {
            const float c = std::exp (-5.0f / std::max (1.0f, p.decay * sr));
            v.env = p.sustain + (v.env - p.sustain) * c;
            if (std::abs (v.env - p.sustain) < 1e-4f) { v.env = p.sustain; v.stage = Stage::Sustain; }
            break;
        }
        case Stage::Sustain: break;
        case Stage::Release:
        {
            v.env *= std::exp (-5.0f / std::max (1.0f, p.release * sr));
            if (v.env < 1e-4f) { v.env = 0.0f; v.stage = Stage::Off; }
            break;
        }
        case Stage::FastRelease:
        {
            v.env *= std::exp (-5.0f / std::max (1.0f, 0.006f * sr));
            if (v.env < 1e-4f) { v.env = 0.0f; v.stage = Stage::Off; }
            break;
        }
        case Stage::Off: break;
    }
}

double InstrumentEngine::pitchOffset (Voice& v)
{
    const auto& e = v.expr;
    const double t = (double) v.pos;
    const double L = (double) std::max (1, v.lengthSamples);
    double off = 0.0;

    if (e.bendType != BendType::None && e.bendSemitones > 0.0f)
    {
        const double B = e.bendSemitones;
        const double bs = e.bendStart * L;
        const double bt = std::clamp ((double) e.bendTime * L, 0.03 * sampleRate, 0.6 * sampleRate);
        switch (e.bendType)
        {
            case BendType::Up: off += B * smoothStep ((t - bs) / bt); break;
            case BendType::Release: off += B * (1.0 - smoothStep ((t - bs) / bt)); break;
            case BendType::PreBend: off += B; break;
            case BendType::UpRelease:
            {
                const double downStart = std::max (bs + bt, 0.62 * L);
                off += B * (smoothStep ((t - bs) / bt) - smoothStep ((t - downStart) / bt));
                break;
            }
            case BendType::None: break;
        }
    }
    if (e.vibratoDepth > 0.0f)
    {
        const double start = e.vibratoDelay * L;
        const double ramp = std::clamp ((t - start) / (0.18 * sampleRate), 0.0, 1.0);
        v.vibPhase += kTwoPi * e.vibratoRate / sampleRate;
        if (v.vibPhase > kTwoPi) v.vibPhase -= kTwoPi;
        if (ramp > 0.0)
        {
            // La guitarra hace vibrato tensando la cuerda (solo hacia arriba); los demás, simétrico.
            off += (v.inst == (int) InstrumentId::Guitar ? e.vibratoDepth * (0.5 - 0.5 * std::cos (v.vibPhase))
                                                          : e.vibratoDepth * std::sin (v.vibPhase))
                   * ramp;
        }
    }
    if (v.glideSamples > 0 && v.glidePos < v.glideSamples)
        off += v.glideFrom * (1.0 - smoothStep ((double) v.glidePos / v.glideSamples));
    return off;
}

float InstrumentEngine::renderSample (Voice& v)
{
    const double sr = sampleRate;
    const double semis = v.baseSemis + pitchOffset (v);
    if (v.glidePos < v.glideSamples) ++v.glidePos;
    ++v.pos;
    float out = 0.0f;
    switch ((InstrumentId) v.inst)
    {
        case InstrumentId::Guitar:
        {
            const double f = semisToHz (semis);
            const float a = v.loopDamp;
            // retardo total = D + retardo de grupo del paso bajo de un polo (≈ (1-a)/a muestras)
            const double d = std::clamp (sr / f - (1.0 - a) / a, 4.0, (double) kDelaySize - 8.0);
            double rp = (double) v.writePos - d;
            while (rp < 1.0) rp += kDelaySize;
            const int i1 = (int) rp;
            const float frac = (float) (rp - i1);
            auto at = [&v] (int i) { return v.delay[(size_t) ((i + kDelaySize) % kDelaySize)]; };
            const float x = dsp::hermite (at (i1 - 1), at (i1), at (i1 + 1), at (i1 + 2), frac);
            v.loopState += a * (x - v.loopState);
            v.delay[(size_t) v.writePos] = v.loopState * v.loopGain;
            v.writePos = (v.writePos + 1) % kDelaySize;
            out = x * 0.9f;
            if (v.clickEnv > 1e-4f)
            {
                // clic de púa: ruido brillante muy corto (ataques agresivos)
                const float n = noise();
                v.lp2 += 0.5f * (n - v.lp2);
                out += (n - v.lp2) * v.clickEnv;
                v.clickEnv *= 0.992f;
            }
            break;
        }
        case InstrumentId::GuitarLite:
        {
            double readPos = (double) v.writePos - std::clamp (sr / semisToHz (semis) - 0.5, 2.0, (double) kDelaySize - 4.0);
            while (readPos < 0) readPos += kDelaySize;
            const int i0 = (int) readPos;
            const float frac = (float) (readPos - i0);
            const float a = v.delay[(size_t) (i0 % kDelaySize)], b = v.delay[(size_t) ((i0 + 1) % kDelaySize)];
            const float x = a + (b - a) * frac;
            const float fb = 0.5f * (x + v.loopState) * (v.released ? 0.97f : 0.9965f);
            v.loopState = x;
            v.delay[(size_t) v.writePos] = fb;
            v.writePos = (v.writePos + 1) % kDelaySize;
            const float driven = std::tanh (3.0f * x) * 0.55f;
            v.lp1 += 0.45f * (driven - v.lp1);
            out = v.lp1 * (0.5f + 0.5f * v.velocity);
            break;
        }
        case InstrumentId::Flute:
        {
            const double baseVib = std::clamp (((double) v.pos / sr - 0.25) / 0.3, 0.0, 1.0);
            v.phase2 += kTwoPi * 5.2 / sr;
            const double f = semisToHz (semis) * (1.0 + (v.expr.vibratoDepth > 0 ? 0.0 : 0.0045 * baseVib * std::sin (v.phase2)));
            v.phase1 += f / sr;
            if (v.phase1 >= 1.0) v.phase1 -= 1.0;
            const double ph = kTwoPi * v.phase1;
            const float tone = (float) (std::sin (ph) + 0.22 * std::sin (2 * ph) + 0.07 * std::sin (3 * ph));
            const float n = noise();
            v.lp1 += 0.30f * (n - v.lp1);
            v.lp2 += 0.04f * (v.lp1 - v.lp2);
            const float breath = v.lp1 - v.lp2;
            const float chiff = (float) std::max (0.0, 1.0 - (double) v.pos / sr / 0.05) * (v.expr.legato ? 0.0f : 1.0f);
            out = 0.55f * tone + breath * (0.18f + 0.6f * chiff);
            out *= 0.4f + 0.6f * v.velocity;
            break;
        }
        case InstrumentId::Synth:
        {
            const double f = semisToHz (semis);
            const double dt1 = f * 1.004 / sr, dt2 = f * 0.996 / sr;
            v.phase1 += dt1; if (v.phase1 >= 1.0) v.phase1 -= 1.0;
            v.phase2 += dt2; if (v.phase2 >= 1.0) v.phase2 -= 1.0;
            const double s1 = 2.0 * v.phase1 - 1.0 - polyBlep (v.phase1, dt1);
            const double s2 = 2.0 * v.phase2 - 1.0 - polyBlep (v.phase2, dt2);
            const float in = (float) (0.5 * (s1 + s2));
            const double fc = std::min (sr / 6.0, 700.0 + 2200.0 * v.velocity + 3500.0 * v.env * v.env);
            const float fco = (float) (2.0 * std::sin (3.141592653589793 * fc / sr));
            v.svfLow += fco * v.svfBand;
            const float high = in - v.svfLow - 1.1f * v.svfBand;
            v.svfBand += fco * high;
            out = v.svfLow * 0.5f * (0.45f + 0.55f * v.velocity);
            break;
        }
    }
    return out;
}

void InstrumentEngine::renderVoices (float* left, float* right, int start, int count)
{
    const float target = outputGain.load();
    for (int i = start; i < start + count; ++i)
    {
        float mix = 0.0f, gtr = 0.0f;
        for (auto& v : voices)
        {
            if (v.stage == Stage::Off) continue;
            advanceEnvelope (v, presetFor (v.inst));
            const float s = renderSample (v) * v.env;
            if (v.inst == (int) InstrumentId::Guitar) gtr += s; else mix += s;
        }
        // "Pastilla y cuerpo": paso alto, resonancia de presencia y paso bajo suave.
        mix += bodyLp.process (bodyPeak.process (bodyHp.process (gtr)));
        smoothedGain += 0.001f * (target - smoothedGain);
        const float s = softLimit (mix * 0.55f * smoothedGain);
        left[i] += s;
        if (right != left) right[i] += s;
    }
}

void InstrumentEngine::process (float* left, float* right, int numSamples, const TimedEvent* events, int numEvents)
{
    const int pending = pendingInstrument.load();
    if (pending != currentInst)
    {
        // Cambio de instrumento: las voces del anterior se apagan con un fundido corto (sin clic)
        // y las notas nuevas usan el instrumento nuevo.
        for (auto& v : voices)
            if (v.stage != Stage::Off) v.stage = Stage::FastRelease;
        currentInst = pending;
    }
    int pos = 0;
    for (int e = 0; e < numEvents; ++e)
    {
        const int at = std::clamp (events[e].sampleOffset, 0, numSamples);
        if (at > pos) { renderVoices (left, right, pos, at - pos); pos = at; }
        if (events[e].type == EventType::NoteOn) noteOn (events[e].pitch, events[e].velocity, events[e].lengthSamples, events[e].expr);
        else noteOff (events[e].pitch);
    }
    if (pos < numSamples) renderVoices (left, right, pos, numSamples - pos);
}

} // namespace solo
