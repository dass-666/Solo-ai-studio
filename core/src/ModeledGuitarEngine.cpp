#include "solo/GuitarSoundEngine.h"
#include "solo/InstrumentEngine.h"
#include <algorithm>
#include <cmath>

namespace solo
{
namespace
{
constexpr int kDelaySize = 8192;
constexpr double kTwoPi = 6.283185307179586;
inline double semisToHz (double s) { return 440.0 * std::pow (2.0, (s - 69.0) / 12.0); }
inline double smoothStep (double u) { u = std::clamp (u, 0.0, 1.0); return 0.5 - 0.5 * std::cos (3.141592653589793 * u); }
inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

bool GuitarParams::operator== (const GuitarParams& o) const
{
    auto eq = [] (float a, float b) { return std::abs (a - b) < 1e-6f; };
    return pickup == o.pickup && eq (pickupPosition, o.pickupPosition) && eq (brightness, o.brightness) && eq (pickHardness, o.pickHardness)
        && eq (sustain, o.sustain) && eq (stiffness, o.stiffness) && eq (bodyResonance, o.bodyResonance) && eq (palmMuteDamp, o.palmMuteDamp)
        && eq (gainDb, o.gainDb);
}

void GuitarParams::sanitise()
{
    auto c = [] (float& v, float lo, float hi) { v = std::isfinite (v) ? clampf (v, lo, hi) : lo; };
    pickup = std::clamp (pickup, 0, 1);
    c (pickupPosition, 0.08f, 0.45f); c (brightness, 0, 1); c (pickHardness, 0, 1); c (sustain, 0, 1);
    c (stiffness, 0, 1); c (bodyResonance, 0, 1); c (palmMuteDamp, 0, 1); c (gainDb, -24, 24);
}

void GuitarSoundEngine::process (float* mono, int numSamples, const TimedEvent* events, int numEvents)
{
    int pos = 0;
    for (int e = 0; e < numEvents; ++e)
    {
        const int at = std::clamp (events[e].sampleOffset, 0, numSamples);
        if (at > pos) { render (mono + pos, at - pos); pos = at; }
        if (events[e].type == EventType::NoteOn) noteOn (events[e].pitch, events[e].velocity, events[e].lengthSamples, events[e].expr);
        else noteOff (events[e].pitch);
    }
    if (pos < numSamples) render (mono + pos, numSamples - pos);
}

// ---------------------------------------------------------------- modelo
ModeledGuitarEngine::ModeledGuitarEngine()
{
    voices.resize ((size_t) kMaxVoices);
    for (auto& v : voices) v.delay.assign (kDelaySize, 0.0f);
    pickupComb.assign (512, 0.0f);
}

void ModeledGuitarEngine::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    for (auto& v : voices)
    {
        v.stage = Stage::Off;
        v.env = 0.0f;
        std::fill (v.delay.begin(), v.delay.end(), 0.0f);
    }
    std::fill (pickupComb.begin(), pickupComb.end(), 0.0f);
    combPos = 0;
    pickupRes1 = pickupRes2 = 0; bodyLp = 0; hpState = hpPrev = 0;
    haveParams = false;
}

float ModeledGuitarEngine::noise()
{
    noiseState = noiseState * 1664525u + 1013904223u;
    return (float) ((int32_t) noiseState) * (1.0f / 2147483648.0f);
}

void ModeledGuitarEngine::setParams (const GuitarParams& pIn, const ExpressionScaling& e)
{
    GuitarParams p = pIn;
    p.sanitise();
    expr = e;
    if (haveParams && p == params) return;
    params = p;
    haveParams = true;
    // Pastilla: resonancia típica (simple ~4.5 kHz, Q alto; doble ~3.2 kHz, Q medio).
    const double f = p.pickup == 0 ? 4600.0 : 3200.0;
    pickupF = (float) (2.0 * std::sin (3.141592653589793 * std::min (f, 0.4 * sr) / sr));
    pickupQ = p.pickup == 0 ? 0.45f : 0.7f; // "k" del SVF: menor = más resonante
}

ModeledGuitarEngine::Voice* ModeledGuitarEngine::findLegatoVoice()
{
    Voice* best = nullptr;
    for (auto& v : voices)
        if (v.stage == Stage::Playing && v.env > 0.02f)
            if (! best || v.age > best->age) best = &v;
    return best;
}

void ModeledGuitarEngine::configureString (Voice& v)
{
    const double pitchNorm = std::clamp ((v.baseSemis - 52.0) / 36.0, 0.0, 1.0);
    // T60: 3..9 s según sustain, menos en agudos; palm mute y release lo acortan.
    double t60 = (3.0 + 6.0 * params.sustain) * (1.0 - 0.45 * pitchNorm) * v.g.sustainScale;
    double damp = 0.5 + 0.4 * params.brightness + 0.15 * (v.g.brightness - 0.5);
    if (v.palmMute) { t60 = 0.35 - 0.2 * params.palmMuteDamp; damp = 0.35 - 0.15 * params.palmMuteDamp; }
    if (v.released) { t60 = 0.08 * v.g.sustainScale; damp = std::min (damp, 0.35); }
    const double f = semisToHz (v.baseSemis);
    v.loopGain = (float) std::exp (-6.907755 / (t60 * f));
    v.loopDamp = (float) std::clamp (damp, 0.1, 0.95);
    // Rigidez: allpass de primer orden en el lazo (dispersión => ligera inarmonicidad, más "metálico").
    v.apCoef = (float) (0.05 + 0.35 * params.stiffness) * (float) (0.4 + 0.6 * pitchNorm);
}

void ModeledGuitarEngine::excite (Voice& v, float amount, float brightness, bool full)
{
    const double f = semisToHz (v.baseSemis + v.glideFrom);
    const double d = std::clamp (sr / f, 4.0, (double) kDelaySize - 8.0);
    const int n = (int) std::ceil (d) + 2;
    const int burst = full ? n : std::max (8, n / 5);
    // Púa: posición (comb) y dureza (la púa dura excita con un pulso más corto y brillante).
    const int pick = std::max (1, (int) std::lround ((0.08 + 0.1 * params.pickupPosition * 2.0) * d));
    float lp = 0.0f;
    float prev[512] {};
    int prevCount = 0;
    const float hard = clampf (params.pickHardness, 0, 1);
    const float lpCoef = clampf (brightness * (0.5f + 0.5f * hard), 0.05f, 0.98f);
    for (int i = 0; i < burst; ++i)
    {
        // Pulso de púa: ruido filtrado + una rampa corta (desplazamiento de la cuerda al soltarla)
        const float ramp = full && i < burst / 2 ? 1.0f - (float) i / (float) (burst / 2) : 0.0f;
        lp += lpCoef * (noise() * (0.7f + 0.3f * hard) + 0.6f * ramp * (1.0f - hard) - lp);
        float e = lp;
        if (prevCount >= pick && pick < 512) e -= prev[(prevCount - pick) % 512];
        prev[prevCount % 512] = lp;
        ++prevCount;
        const size_t idx = (size_t) ((v.writePos - burst + i + kDelaySize) % kDelaySize);
        if (full) v.delay[idx] = 0.2f * v.delay[idx] + amount * e;
        else v.delay[idx] += amount * e;
    }
}

void ModeledGuitarEngine::noteOn (int pitch, int velocity, int lengthSamples, const NoteExpression& e)
{
    if (pitch < 0 || pitch > 127) return;
    if (velocity <= 0) { noteOff (pitch); return; }
    const NoteGestures g = gesturesFor (e, velocity, std::max (1, lengthSamples) / sr, expr, true);

    if (g.legato)
    {
        if (Voice* lv = findLegatoVoice())
        {
            Voice& v = *lv;
            const double from = v.baseSemis + gesturePitchOffset (v.g, (double) v.pos / sr)
                                + (v.glideSamples > 0 && v.glidePos < v.glideSamples ? v.glideFrom * (1.0 - smoothStep ((double) v.glidePos / v.glideSamples)) : 0.0);
            v.pitch = pitch;
            v.baseSemis = pitch;
            v.glideFrom = from - pitch;
            v.glideSamples = std::max (1, (int) (g.legatoGlideSec * sr));
            v.glidePos = 0;
            v.pos = 0;
            v.g = g;
            v.g.slideFromSemis = 0.0; // el deslizamiento lo hace el glide desde la nota anterior
            v.age = ++ageCounter;
            v.released = false;
            v.palmMute = g.palmMute;
            configureString (v);
            if (e.slideFrom == 0) excite (v, 0.12f * (float) g.level, 0.45f, false); // golpe del dedo (hammer-on / pull-off)
            v.fingerEnv = 0.08f * (float) g.level;
            return;
        }
    }

    Voice* target = nullptr;
    for (auto& v : voices)
        if (v.stage != Stage::Off && v.pitch == pitch) { target = &v; break; }
    if (! target)
        for (auto& v : voices)
            if (v.stage == Stage::Off) { target = &v; break; }
    if (! target)
    {
        Voice* oldest = nullptr;
        for (auto& v : voices)
        {
            const bool rel = v.stage != Stage::Playing;
            if (! oldest) { oldest = &v; continue; }
            const bool oldestRel = oldest->stage != Stage::Playing;
            if ((rel && ! oldestRel) || (rel == oldestRel && v.age < oldest->age)) oldest = &v;
        }
        target = oldest;
    }
    Voice& v = *target;
    v.pitch = pitch;
    v.age = ++ageCounter;
    v.baseSemis = pitch;
    v.g = g;
    v.pos = 0;
    v.released = false;
    v.palmMute = g.palmMute;
    v.glideFrom = g.slideFromSemis;
    v.glidePos = 0;
    v.glideSamples = g.slideFromSemis != 0.0 ? std::max (1, (int) (g.slideTimeSec * sr)) : 0;
    v.stage = Stage::Playing;
    // Un nuevo ataque sobre una cuerda que ya sonaba: la envolvente no se reinicia a 0 (no hay clic), la excitación sí.
    if (v.env <= 0.0f) { v.loopState = 0.0f; v.apState = 0.0f; }
    v.env = std::max (v.env, 0.0f);
    configureString (v);
    const float att = (float) g.attack;
    const float bright = g.palmMute ? 0.2f + 0.25f * att : 0.3f + 0.45f * (float) g.brightness + 0.25f * att;
    excite (v, (0.5f + 0.5f * (float) g.level) * (g.palmMute ? 0.7f : 1.0f), std::min (0.97f, bright), true);
    v.clickEnv = (0.15f + 0.35f * params.pickHardness) * att * (float) g.level;
    v.fingerEnv = 0.0f;
    // Ataque suave (swell) para articulaciones muy blandas: la envolvente sube desde 0.
    if (att < 0.2f) v.env = 0.0f; else v.env = std::max (v.env, 1.0f);
}

void ModeledGuitarEngine::noteOff (int pitch)
{
    for (auto& v : voices)
        if (v.pitch == pitch && v.stage == Stage::Playing)
        {
            v.stage = Stage::Release;
            v.released = true;
            configureString (v);
        }
}

void ModeledGuitarEngine::allNotesOff (bool immediate)
{
    for (auto& v : voices)
        if (v.stage != Stage::Off)
        {
            v.stage = immediate ? Stage::FastRelease : Stage::Release;
            v.released = true;
            if (! immediate) configureString (v);
        }
}

int ModeledGuitarEngine::activeVoiceCount() const
{
    int n = 0;
    for (const auto& v : voices) n += v.stage != Stage::Off ? 1 : 0;
    return n;
}

float ModeledGuitarEngine::renderVoice (Voice& v)
{
    double semis = v.baseSemis + gesturePitchOffset (v.g, (double) v.pos / sr);
    if (v.glideSamples > 0 && v.glidePos < v.glideSamples)
    {
        semis += v.glideFrom * (1.0 - smoothStep ((double) v.glidePos / v.glideSamples));
        ++v.glidePos;
    }
    ++v.pos;
    const double f = semisToHz (semis);
    const float a = v.loopDamp;
    // Retardo total = D + retardo del paso bajo ((1-a)/a) + retardo del allpass (~ (1-c)/(1+c)).
    const double apDelay = (1.0 - v.apCoef) / (1.0 + v.apCoef);
    const double d = std::clamp (sr / f - (1.0 - a) / a - apDelay, 4.0, (double) kDelaySize - 8.0);
    double rp = (double) v.writePos - d;
    while (rp < 1.0) rp += kDelaySize;
    const int i1 = (int) rp;
    const float frac = (float) (rp - i1);
    auto at = [&v] (int i) { return v.delay[(size_t) ((i + kDelaySize) % kDelaySize)]; };
    const float x = dsp::hermite (at (i1 - 1), at (i1), at (i1 + 1), at (i1 + 2), frac);
    v.loopState += a * (x - v.loopState);
    // Allpass de rigidez: y = c·x + x1 - c·y1
    const float apIn = v.loopState;
    const float apOut = v.apCoef * apIn + v.apState;
    v.apState = apIn - v.apCoef * apOut;
    v.delay[(size_t) v.writePos] = apOut * v.loopGain;
    v.writePos = (v.writePos + 1) % kDelaySize;
    float out = x;
    if (v.clickEnv > 1e-4f)
    {
        const float n = noise();
        v.clickLp += 0.6f * (n - v.clickLp);
        out += (n - v.clickLp) * v.clickEnv;
        v.clickEnv *= 0.990f;
    }
    if (v.fingerEnv > 1e-4f) { out += noise() * v.fingerEnv * 0.3f; v.fingerEnv *= 0.985f; }

    // Envolvente: swell en ataques blandos; release/fast release.
    switch (v.stage)
    {
        case Stage::Playing:
            if (v.env < 1.0f) { v.env += 1.0f / (float) std::max (1.0, (0.03 + 0.12 * (1.0 - v.g.attack)) * sr); if (v.env > 1.0f) v.env = 1.0f; }
            break;
        case Stage::Release:
            v.env *= (float) std::exp (-5.0 / std::max (1.0, 0.09 * v.g.sustainScale * sr));
            if (v.env < 1e-4f) { v.env = 0.0f; v.stage = Stage::Off; }
            break;
        case Stage::FastRelease:
            v.env *= (float) std::exp (-5.0 / std::max (1.0, 0.006 * sr));
            if (v.env < 1e-4f) { v.env = 0.0f; v.stage = Stage::Off; }
            break;
        case Stage::Off: break;
    }
    return out * v.env * (float) v.g.level;
}

void ModeledGuitarEngine::render (float* mono, int n)
{
    const float target = dsp::dbToGain (params.gainDb);
    const int combSize = (int) pickupComb.size();
    for (int i = 0; i < n; ++i)
    {
        float s = 0.0f;
        for (auto& v : voices)
            if (v.stage != Stage::Off) s += renderVoice (v);
        // Pastilla: comb de posición (un retardo corto, ~posición en la cuerda a 110 Hz) y resonancia.
        pickupComb[(size_t) combPos] = s;
        const int combDelay = std::clamp ((int) (params.pickupPosition * sr / 110.0 * 0.5), 1, combSize - 1);
        const float delayed = pickupComb[(size_t) ((combPos - combDelay + combSize) % combSize)];
        combPos = (combPos + 1) % combSize;
        float p = s - 0.5f * delayed;
        // SVF resonante (paso bajo 2º orden con pico)
        pickupRes1 += pickupF * pickupRes2;
        const float high = p - pickupRes1 - pickupQ * pickupRes2;
        pickupRes2 += pickupF * high;
        p = pickupRes1 * (params.pickup == 0 ? 1.0f : 1.3f) + 0.15f * p;
        // Cuerpo: un poco de resonancia grave y paso alto de 70 Hz.
        bodyLp += 0.02f * (p - bodyLp);
        p += params.bodyResonance * 0.5f * bodyLp;
        const float hp = p - hpPrev + 0.991f * hpState;
        hpPrev = p;
        hpState = std::abs (hp) < 1e-15f ? 0.0f : hp;
        smoothedGain += 0.001f * (target - smoothedGain);
        mono[i] += hp * 0.5f * smoothedGain;
    }
}

} // namespace solo
