#include "solo/GuitarAmp.h"
#include <algorithm>
#include <cmath>

namespace solo
{
namespace
{
inline float flushDenormal (float v) { return std::abs (v) < 1.0e-15f ? 0.0f : v; }

double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    const double q = x * x / 4.0;
    for (int k = 1; k < 60; ++k)
    {
        term *= q / ((double) k * k);
        sum += term;
        if (term < 1e-12 * sum) break;
    }
    return sum;
}
} // namespace

bool AmpParams::operator== (const AmpParams& o) const
{
    auto eq = [] (float a, float b) { return std::abs (a - b) < 1.0e-6f; };
    return overdrive == o.overdrive && distortion == o.distortion && amp == o.amp && cabinet == o.cabinet
        && eq (odDrive, o.odDrive) && eq (odTone, o.odTone) && eq (distGain, o.distGain) && eq (distTone, o.distTone)
        && eq (preGainDb, o.preGainDb) && satType == o.satType && eq (master, o.master) && eq (sag, o.sag)
        && eq (compression, o.compression) && eq (bassDb, o.bassDb) && eq (midDb, o.midDb) && eq (trebleDb, o.trebleDb)
        && eq (presenceDb, o.presenceDb) && cabType == o.cabType && eq (outputLpHz, o.outputLpHz) && eq (trimDb, o.trimDb)
        && oversample == o.oversample;
}

// ---------------------------------------------------------------- IR
bool prepareCabImpulse (const std::vector<float>& mono, double sourceRate, double engineRate, CabImpulse& out, std::string& error)
{
    if (mono.empty()) { error = "La respuesta al impulso está vacía"; return false; }
    if (! (sourceRate > 1000.0) || ! (engineRate > 1000.0)) { error = "Frecuencia de muestreo inválida"; return false; }
    for (float v : mono) if (! std::isfinite (v)) { error = "La respuesta al impulso contiene valores no finitos"; return false; }
    // Remuestreo lineal (una IR de pantalla es muy suave en el tiempo; suficiente) y recorte.
    const double ratio = sourceRate / engineRate;
    const int n = std::min (CabImpulse::kMaxTaps, (int) std::floor ((double) mono.size() / ratio));
    if (n < 8) { error = "La respuesta al impulso es demasiado corta"; return false; }
    out.taps.assign ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double p = i * ratio;
        const size_t i0 = (size_t) p;
        const double f = p - (double) i0;
        const float a = mono[std::min (i0, mono.size() - 1)], b = mono[std::min (i0 + 1, mono.size() - 1)];
        out.taps[(size_t) i] = (float) (a + (b - a) * f);
    }
    // Fundido final (si se recortó) y normalización de energía.
    const int fade = std::min (n / 8, 256);
    for (int i = 0; i < fade; ++i) out.taps[(size_t) (n - 1 - i)] *= (float) i / (float) fade;
    double energy = 0.0;
    for (float v : out.taps) energy += (double) v * v;
    if (energy <= 1.0e-12) { error = "La respuesta al impulso es silencio"; return false; }
    const float g = (float) (1.0 / std::sqrt (energy));
    for (auto& v : out.taps) v *= g;
    out.sampleRate = engineRate;
    return true;
}

// ---------------------------------------------------------------- media banda
void Halfband2x::prepare()
{
    // Sinc enventanado (Kaiser, beta 8: ~-80 dB) de kTaps coeficientes, corte en fs/4 (de la frecuencia alta).
    constexpr int M = (kTaps - 1) / 2; // 31
    const double beta = 8.0, i0b = besselI0 (beta);
    for (int k = 0; k < (int) hOdd.size(); ++k)
    {
        const int n = 2 * k + 1; // distancia impar al centro
        if (n > M) { hOdd[(size_t) k] = 0.0f; continue; }
        const double x = (double) n;
        const double sinc = std::sin (3.141592653589793 * x / 2.0) / (3.141592653589793 * x / 2.0) * 0.5;
        const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - (x / (double) M) * (x / (double) M)))) / i0b;
        hOdd[(size_t) k] = (float) (sinc * w);
    }
    reset();
}

void Halfband2x::reset()
{
    histUp.fill (0.0f);
    histDnOdd.fill (0.0f);
    histDnEven.fill (0.0f);
    posUp = posDn = 0;
}

void Halfband2x::up (float x, float& even, float& odd)
{
    // Entrada con ceros intercalados y filtro de ganancia 2: la fase par solo ve el coeficiente central (0.5·2 = 1)
    // retrasado (M-1)/2 muestras; la fase impar es la FIR de los coeficientes impares.
    constexpr int M = (kTaps - 1) / 2;
    constexpr int half = (M + 1) / 2; // 16 coeficientes impares a cada lado
    histUp[(size_t) posUp] = x;
    auto at = [&] (int back) { return histUp[(size_t) ((posUp - back + 64) & 63)]; };
    even = at (half); // retardo de 16 muestras; la fase impar interpola entre at(16) y at(15)
    float acc = 0.0f;
    for (int k = 0; k < half; ++k)
        acc += hOdd[(size_t) k] * (at (half - 1 - k) + at (half + k)); // simetría: y[n] = sum h_k (x[n-15+k] + x[n-16-k])
    odd = 2.0f * acc;
    posUp = (posUp + 1) & 63;
}

float Halfband2x::down (float even, float odd)
{
    constexpr int M = (kTaps - 1) / 2;
    constexpr int half = (M + 1) / 2;
    histDnEven[(size_t) posDn] = even;
    histDnOdd[(size_t) posDn] = odd;
    auto atE = [&] (int back) { return histDnEven[(size_t) ((posDn - back + 64) & 63)]; };
    auto atO = [&] (int back) { return histDnOdd[(size_t) ((posDn - back + 64) & 63)]; };
    float acc = 0.5f * atE ((M - 1) / 2);
    for (int k = 0; k < half; ++k)
        acc += hOdd[(size_t) k] * (atO (half - 1 - k) + atO (half + k));
    posDn = (posDn + 1) & 63;
    return acc;
}

// ---------------------------------------------------------------- etapas no lineales
float GuitarAmp::saturate (float v, int type)
{
    switch (type)
    {
        case 1: // alta ganancia: recorte más duro y simétrico (armónicos impares)
        {
            const float y = v / (1.0f + std::abs (v));
            return std::tanh (1.8f * y) * 1.1f;
        }
        case 2: // limpio: casi lineal, solo redondea los picos
            return std::abs (v) < 0.5f ? v : (v > 0 ? 0.5f + 0.5f * std::tanh (2.0f * (v - 0.5f)) : -0.5f + 0.5f * std::tanh (2.0f * (v + 0.5f)));
        default: // válvula: asimétrica suave (armónicos pares), f(0) = 0 exacto
            return v >= 0.0f ? std::tanh (v) : 1.18f * std::tanh (v / 1.18f);
    }
}

void GuitarAmp::Stages::reset()
{
    for (auto* b : { &odPre, &odTone, &inter1Hp, &inter1Lp, &distTone, &inter2Hp }) b->reset();
    dcX = dcY = 0; envFast = envSlow = 0;
}

void GuitarAmp::Stages::design (double rate, const AmpParams& p)
{
    odPre.highpass (rate, 720.0, 0.6);
    odTone.lowpass (rate, 1800.0 + 5000.0 * p.odTone, 0.65);
    inter1Hp.highpass (rate, 90.0, 0.7);
    inter1Lp.lowpass (rate, p.satType == 2 ? 11000.0 : 7200.0, 0.6);
    distTone.lowpass (rate, 2400.0 + 5600.0 * p.distTone, 0.7);
    inter2Hp.highpass (rate, 40.0, 0.7);
}

float GuitarAmp::Stages::run (float x, const AmpParams& p, float preGain)
{
    // Overdrive opcional (pedal antes del ampli): realce de medios + recorte suave.
    if (p.overdrive)
    {
        const float in = 0.35f * x + 0.65f * odPre.process (x);
        const float g = 1.0f + p.odDrive * p.odDrive * 40.0f;
        const float y = in * g;
        x = odTone.process (y >= 0.0f ? std::tanh (y) : 1.25f * std::tanh (0.8f * y)) * 0.6f;
    }
    // Etapa 1 (previo). El "sag" desplaza el punto de polarización con la envolvente lenta: más compresión y
    // más armónicos pares cuanto más fuerte se toca.
    const float a = std::abs (x);
    envFast += (a > envFast ? 0.02f : 0.0008f) * (a - envFast);
    envSlow += (a > envSlow ? 0.004f : 0.0002f) * (a - envSlow);
    const float bias = 0.25f * p.sag * std::min (1.0f, envSlow * preGain);
    float y = saturate (x * preGain - bias, p.satType) + (p.satType == 2 ? 0.0f : saturate (-bias, p.satType) * -1.0f);
    y = inter1Lp.process (inter1Hp.process (y));
    // Distorsión opcional (segunda etapa de alta ganancia).
    if (p.distortion)
    {
        const float g = 1.0f + p.distGain * p.distGain * 120.0f;
        float d = y * g;
        d = d / (1.0f + std::abs (d));
        y = distTone.process (std::tanh (1.6f * d)) * 0.8f;
    }
    // Etapa de potencia: el sag reduce la ganancia disponible bajo carga.
    const float k = (1.0f + p.master * 2.5f) / (1.0f + 1.5f * p.sag * std::min (1.0f, envSlow * preGain));
    y = std::tanh (y * k) / std::tanh (std::max (0.3f, k * 0.6f));
    y = inter2Hp.process (y);
    // Bloqueo de continua.
    const float out = y - dcX + 0.995f * dcY;
    dcX = y;
    dcY = flushDenormal (out);
    return out;
}

// ---------------------------------------------------------------- amplificador
float GuitarAmp::preGainDbForForce (double forcePercent)
{
    const double f = std::clamp (forcePercent / 100.0, 0.0, 1.0);
    return (float) (2.0 + 22.0 * std::pow (f, 1.6));
}

float GuitarAmp::compensationGain (const AmpParams& p)
{
    // Señal de referencia: seno de 220 Hz a 0.2 de pico (nivel típico de la cuerda) a 96 kHz, 2048 muestras.
    Stages s;
    s.reset();
    s.design (96000.0, p);
    const float pre = dsp::dbToGain (p.preGainDb);
    double in2 = 0.0, out2 = 0.0;
    for (int i = 0; i < 4096; ++i)
    {
        const float x = 0.2f * (float) std::sin (2.0 * 3.141592653589793 * 220.0 * i / 96000.0);
        const float y = s.run (x, p, pre);
        if (i >= 2048) { in2 += (double) x * x; out2 += (double) y * y; }
    }
    double gain = std::sqrt (std::max (1e-12, out2) / std::max (1e-12, in2));
    // Ganancia estática del compresor sobre la misma señal de referencia (envolvente ~0.14 frente a umbral 0.05).
    if (p.compression > 0.001f)
    {
        const double ratio = 1.0 + 3.0 * p.compression;
        const double overDb = 20.0 * std::log10 (0.14 / 0.05);
        gain *= std::pow (10.0, (-overDb * (1.0 - 1.0 / ratio) + 5.0 * p.compression) / 20.0);
    }
    // Objetivo: la cadena no lineal deja el nivel RMS 2 veces (+6 dB) por encima de la entrada limpia, como el
    // ampli v2 (ampNorm·2) para que los presets antiguos y nuevos queden en rangos de nivel parecidos.
    // Ajuste derivado de las pruebas (tone_force_keeps_loudness_within_tolerance): con una nota real (que decae) la
    // saturación levanta la cola y la sonoridad sube ~0.26 dB por dB de ganancia previa aunque el pico quede igual.
    // Se compensa la mitad (-0.13 dB/dB): una nota sostenida baja algo con Fuerza alta y una nota que decae sube algo.
    const double trend = std::pow (10.0, -0.13 * (p.preGainDb - 2.0) / 20.0);
    return (float) std::clamp (2.0 / gain * trend, 0.02, 50.0);
}

void GuitarAmp::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    for (auto& o : os) o.prepare();
    irHist.assign ((size_t) CabImpulse::kMaxTaps * 2, 0.0f);
    haveParams = false;
    reset();
}

void GuitarAmp::reset()
{
    for (auto& o : os) o.reset();
    stages.reset();
    for (auto* b : { &inHp, &compHp, &tsLow, &tsMid, &tsHigh, &presence, &outLp }) b->reset();
    for (auto& b : cab) b.reset();
    compEnv = 0.0f;
    std::fill (irHist.begin(), irHist.end(), 0.0f);
    irPos = 0;
    haveParams = false;
}

int GuitarAmp::latencySamples() const
{
    const int o = std::clamp (params.oversample, 1, 4);
    if (o >= 4) return Halfband2x::latencyAtLowRate() + Halfband2x::latencyAtLowRate() / 2;
    if (o >= 2) return Halfband2x::latencyAtLowRate();
    return 0;
}

void GuitarAmp::setParams (const AmpParams& pIn)
{
    AmpParams p = pIn;
    p.oversample = p.oversample >= 4 ? 4 : (p.oversample >= 2 ? 2 : 1);
    const bool changed = ! haveParams || p != params;
    if (! changed) return;
    const bool osChanged = ! haveParams || p.oversample != params.oversample;
    const bool filtersChanged = ! haveParams || p.satType != params.satType || std::abs (p.odTone - params.odTone) > 1e-6f
                                || std::abs (p.distTone - params.distTone) > 1e-6f || osChanged
                                || std::abs (p.bassDb - params.bassDb) > 1e-6f || std::abs (p.midDb - params.midDb) > 1e-6f
                                || std::abs (p.trebleDb - params.trebleDb) > 1e-6f || std::abs (p.presenceDb - params.presenceDb) > 1e-6f
                                || p.cabType != params.cabType || std::abs (p.outputLpHz - params.outputLpHz) > 1e-6f;
    const float comp = compensationGain (p) * dsp::dbToGain (p.trimDb);
    const float pre = dsp::dbToGain (p.preGainDb);
    const int ramp = (int) (0.02 * sr);
    if (! haveParams) { sPreGain.reset (pre); sComp.reset (comp); sTrim.reset (1.0f); }
    else { sPreGain.setTarget (pre, ramp); sComp.setTarget (comp, ramp); }
    params = p;
    if (filtersChanged)
    {
        stageRate = sr * p.oversample;
        stages.design (stageRate, p);
        inHp.highpass (sr, 70.0, 0.7);
        compHp.highpass (sr, 120.0, 0.7);
        // Tonestack: graves 110 Hz, medios 750 Hz (con un ligero hueco en alta ganancia), agudos 2.8 kHz, presencia 4.5 kHz.
        tsLow.lowShelf (sr, 110.0, p.bassDb);
        tsMid.peak (sr, 750.0, 0.6, p.midDb + (p.satType == 1 ? -2.0 : 0.0));
        tsHigh.highShelf (sr, 2800.0, p.trebleDb);
        presence.highShelf (sr, 4500.0, p.presenceDb);
        switch (p.cabType)
        {
            case 0: // 1x12 abierta: más brillante y con menos graves
                cab[0].highpass (sr, 75.0, 0.7); cab[1].peak (sr, 130.0, 1.0, 2.5); cab[2].peak (sr, 2600.0, 1.2, 3.5);
                cab[3].lowpass (sr, 6200.0, 0.54); cab[4].lowpass (sr, 6200.0, 1.31);
                break;
            case 2: // 4x12 cerrada: pegada en 110 Hz, hueco en 450 Hz, corte más bajo
                cab[0].highpass (sr, 85.0, 0.7); cab[1].peak (sr, 110.0, 1.2, 4.0); cab[2].peak (sr, 450.0, 1.0, -3.0);
                cab[3].lowpass (sr, 5000.0, 0.54); cab[4].lowpass (sr, 5000.0, 1.31);
                break;
            default: // 2x12
                cab[0].highpass (sr, 80.0, 0.7); cab[1].peak (sr, 120.0, 1.0, 3.0); cab[2].peak (sr, 2200.0, 1.4, 2.5);
                cab[3].lowpass (sr, 5600.0, 0.54); cab[4].lowpass (sr, 5600.0, 1.31);
                break;
        }
        outLp.lowpass (sr, std::clamp ((double) p.outputLpHz, 2000.0, 20000.0), 0.7);
    }
    if (osChanged) for (auto& o : os) o.reset();
    haveParams = true;
}

float GuitarAmp::process (float x)
{
    if (! haveParams) return x;
    const auto& p = params;
    const float pre = sPreGain.next();
    const float comp = sComp.next();
    if (! p.amp && ! p.overdrive && ! p.distortion)
    {
        // Solo pantalla (o nada).
        if (p.cabinet)
        {
            if (impulse && ! impulse->taps.empty())
            {
                const int n = (int) impulse->taps.size();
                irHist[(size_t) irPos] = x;
                irHist[(size_t) irPos + (size_t) CabImpulse::kMaxTaps] = x; // copia espejo: lectura contigua
                float acc = 0.0f;
                const float* h = impulse->taps.data();
                const float* s = irHist.data() + irPos + CabImpulse::kMaxTaps;
                for (int i = 0; i < n; ++i) acc += h[i] * s[-i];
                irPos = (irPos + 1) % CabImpulse::kMaxTaps;
                x = acc;
            }
            else for (auto& b : cab) x = b.process (x);
        }
        return x;
    }
    // Filtro previo y compresor (feed-forward sobre la señal filtrada).
    x = inHp.process (x);
    if (p.compression > 0.001f)
    {
        const float a = std::abs (compHp.process (x));
        compEnv += (a > compEnv ? 0.015f : 0.0004f) * (a - compEnv);
        const float thr = 0.05f; // -26 dBFS
        const float ratio = 1.0f + 3.0f * p.compression;
        float g = 1.0f;
        if (compEnv > thr)
        {
            const float overDb = 20.0f * std::log10 (compEnv / thr);
            g = dsp::dbToGain (-overDb * (1.0f - 1.0f / ratio));
        }
        x *= g * dsp::dbToGain (5.0f * p.compression); // compensación fija del compresor
    }
    // Etapas no lineales con sobremuestreo.
    float y = 0.0f;
    if (p.oversample >= 4)
    {
        float e1, o1;
        os[0].up (x, e1, o1);
        float e2, o2, e3, o3;
        os[1].up (e1, e2, o2);
        os[1].up (o1, e3, o3);
        const float d1 = os[1].down (stages.run (e2, p, pre), stages.run (o2, p, pre));
        const float d2 = os[1].down (stages.run (e3, p, pre), stages.run (o3, p, pre));
        y = os[0].down (d1, d2);
    }
    else if (p.oversample >= 2)
    {
        float e, o;
        os[0].up (x, e, o);
        y = os[0].down (stages.run (e, p, pre), stages.run (o, p, pre));
    }
    else y = stages.run (x, p, pre);
    y *= comp;
    // Tonestack, presencia, pantalla y filtro de salida.
    y = presence.process (tsHigh.process (tsMid.process (tsLow.process (y))));
    if (p.cabinet)
    {
        if (impulse && ! impulse->taps.empty())
        {
            const int n = (int) impulse->taps.size();
            irHist[(size_t) irPos] = y;
            irHist[(size_t) irPos + (size_t) CabImpulse::kMaxTaps] = y;
            float acc = 0.0f;
            const float* h = impulse->taps.data();
            const float* s = irHist.data() + irPos + CabImpulse::kMaxTaps;
            for (int i = 0; i < n; ++i) acc += h[i] * s[-i];
            irPos = (irPos + 1) % CabImpulse::kMaxTaps;
            y = acc;
        }
        else for (auto& b : cab) y = b.process (y);
    }
    return outLp.process (y);
}

} // namespace solo
