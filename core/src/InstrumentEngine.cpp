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
}

InstrumentEngine::Preset InstrumentEngine::presetFor (int inst)
{
    switch ((InstrumentId) inst)
    {
        case InstrumentId::Guitar: return { 0.002f, 0.05f, 1.0f, 0.09f };
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

void InstrumentEngine::noteOn (int pitch, int velocity)
{
    if (pitch < 0 || pitch > 127) return;
    if (velocity <= 0) { noteOff (pitch); return; }

    Voice* target = nullptr;
    for (auto& v : voices)
        if (v.stage != Stage::Off && v.pitch == pitch) { target = &v; break; } // reutilizar misma altura
    if (! target)
        for (auto& v : voices)
            if (v.stage == Stage::Off) { target = &v; break; }
    if (! target)
    {
        // Robo de voz: la más antigua, preferentemente en release.
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
    v.pitch = pitch;
    v.velocity = (float) velocity / 127.0f;
    v.age = ++ageCounter;
    v.freq = 440.0 * std::pow (2.0, (pitch - 69) / 12.0);
    v.stage = Stage::Attack;
    v.timeOn = 0.0;
    v.vibPhase = 0.0;
    if (currentInst != (int) InstrumentId::Flute) v.env = 0.0f; // la flauta liga suavemente

    if (currentInst == (int) InstrumentId::Guitar)
    {
        // Excitación Karplus-Strong: ráfaga de ruido filtrado (más brillante con más velocity).
        v.delaySamples = std::clamp (sampleRate / v.freq - 0.5, 2.0, (double) kDelaySize - 4.0);
        const int n = (int) std::ceil (v.delaySamples) + 2;
        const float bright = 0.25f + 0.65f * v.velocity;
        float lp = 0.0f;
        for (int i = n; i > 0; --i)
        {
            lp += bright * (noise() - lp);
            v.delay[(size_t) ((v.writePos - i + kDelaySize) % kDelaySize)] = lp * 0.9f;
        }
        v.ksPrev = 0.0f;
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
            v.stage = Stage::Release;
}

void InstrumentEngine::allNotesOff (bool immediate)
{
    for (auto& v : voices)
        if (v.stage != Stage::Off) v.stage = immediate ? Stage::FastRelease : Stage::Release;
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
            v.env += 1.0f / std::max (1.0f, p.attack * sr);
            if (v.env >= 1.0f) { v.env = 1.0f; v.stage = Stage::Decay; }
            break;
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

float InstrumentEngine::renderSample (Voice& v)
{
    const double sr = sampleRate;
    v.timeOn += 1.0 / sr;
    float out = 0.0f;
    switch ((InstrumentId) currentInst)
    {
        case InstrumentId::Guitar:
        {
            double readPos = (double) v.writePos - v.delaySamples;
            while (readPos < 0) readPos += kDelaySize;
            const int i0 = (int) readPos;
            const float frac = (float) (readPos - i0);
            const float a = v.delay[(size_t) (i0 % kDelaySize)], b = v.delay[(size_t) ((i0 + 1) % kDelaySize)];
            const float x = a + (b - a) * frac;
            const float fb = 0.5f * (x + v.ksPrev) * 0.9965f; // filtro de lazo + pérdida (sustain de guitarra eléctrica)
            v.ksPrev = x;
            v.delay[(size_t) v.writePos] = fb;
            v.writePos = (v.writePos + 1) % kDelaySize;
            // Saturación suave tipo "lead" y paso bajo de pantalla (~4 kHz).
            const float driven = std::tanh (3.0f * x) * 0.55f;
            v.lp1 += 0.45f * (driven - v.lp1);
            out = v.lp1 * (0.5f + 0.5f * v.velocity);
            break;
        }
        case InstrumentId::Flute:
        {
            const double vibRamp = std::clamp ((v.timeOn - 0.25) / 0.3, 0.0, 1.0);
            v.vibPhase += kTwoPi * 5.2 / sr;
            const double f = v.freq * (1.0 + 0.0045 * vibRamp * std::sin (v.vibPhase));
            v.phase1 += f / sr;
            if (v.phase1 >= 1.0) v.phase1 -= 1.0;
            const double ph = kTwoPi * v.phase1;
            const float tone = (float) (std::sin (ph) + 0.22 * std::sin (2 * ph) + 0.07 * std::sin (3 * ph));
            const float n = noise();
            v.lp1 += 0.30f * (n - v.lp1);
            v.lp2 += 0.04f * (v.lp1 - v.lp2);
            const float breath = v.lp1 - v.lp2;
            const float chiff = (float) std::max (0.0, 1.0 - v.timeOn / 0.05);
            out = 0.55f * tone + breath * (0.18f + 0.6f * chiff);
            out *= 0.4f + 0.6f * v.velocity;
            break;
        }
        case InstrumentId::Synth:
        {
            const double dt1 = v.freq * 1.004 / sr, dt2 = v.freq * 0.996 / sr;
            v.phase1 += dt1; if (v.phase1 >= 1.0) v.phase1 -= 1.0;
            v.phase2 += dt2; if (v.phase2 >= 1.0) v.phase2 -= 1.0;
            const double s1 = 2.0 * v.phase1 - 1.0 - polyBlep (v.phase1, dt1);
            const double s2 = 2.0 * v.phase2 - 1.0 - polyBlep (v.phase2, dt2);
            const float in = (float) (0.5 * (s1 + s2));
            const double fc = std::min (sr / 6.0, 700.0 + 2200.0 * v.velocity + 3500.0 * v.env * v.env);
            const float f = (float) (2.0 * std::sin (3.141592653589793 * fc / sr));
            v.svfLow += f * v.svfBand;
            const float high = in - v.svfLow - 1.1f * v.svfBand;
            v.svfBand += f * high;
            out = v.svfLow * 0.5f * (0.45f + 0.55f * v.velocity);
            break;
        }
    }
    return out;
}

void InstrumentEngine::renderVoices (float* left, float* right, int start, int count)
{
    const Preset p = presetFor (currentInst);
    const float target = outputGain.load();
    for (int i = start; i < start + count; ++i)
    {
        float mix = 0.0f;
        for (auto& v : voices)
        {
            if (v.stage == Stage::Off) continue;
            advanceEnvelope (v, p);
            mix += renderSample (v) * v.env;
        }
        smoothedGain += 0.001f * (target - smoothedGain);
        const float s = softLimit (mix * 0.55f * smoothedGain);
        left[i] += s;
        right[i] += s;
    }
}

void InstrumentEngine::process (float* left, float* right, int numSamples, const TimedEvent* events, int numEvents)
{
    const int pending = pendingInstrument.load();
    if (pending != currentInst)
    {
        // Cambio de instrumento: silenciar voces del anterior sin clic y cambiar.
        for (auto& v : voices) { v.stage = Stage::Off; v.env = 0.0f; }
        currentInst = pending;
    }
    int pos = 0;
    for (int e = 0; e < numEvents; ++e)
    {
        const int at = std::clamp (events[e].sampleOffset, 0, numSamples);
        if (at > pos) { renderVoices (left, right, pos, at - pos); pos = at; }
        if (events[e].type == EventType::NoteOn) noteOn (events[e].pitch, events[e].velocity);
        else noteOff (events[e].pitch);
    }
    if (pos < numSamples) renderVoices (left, right, pos, numSamples - pos);
}

} // namespace solo
