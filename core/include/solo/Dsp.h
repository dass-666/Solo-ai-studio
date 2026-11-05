#pragma once
// Utilidades DSP sin dependencias: biquads (RBJ), suavizado de parámetros, interpolación.
#include <cmath>
#include <cstdint>
#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
 #include <xmmintrin.h>
 #define SOLO_HAS_SSE_CSR 1
#endif

namespace solo::dsp
{
// Activa "flush-to-zero" y "denormals-are-zero" mientras existe el objeto y restaura el estado al salir.
// Sin esto, las colas de reverb/delay/filtros que decaen hacia cero producen números desnormalizados,
// que en x86 son hasta ~100 veces más lentos (medido: p99 de CPU por bloque 24% -> 1.7%).
// Lo usa SoloEngine::process, de modo que tanto el plugin como la exportación offline quedan protegidos
// aunque el host no lo haga.
class ScopedFlushDenormals
{
public:
    ScopedFlushDenormals()
    {
#if defined(SOLO_HAS_SSE_CSR)
        saved = _mm_getcsr();
        _mm_setcsr (saved | 0x8040u); // FTZ (bit 15) | DAZ (bit 6)
#elif defined(__aarch64__)
        uint64_t v;
        __asm__ __volatile__ ("mrs %0, fpcr" : "=r"(v));
        saved = v;
        v |= (1ull << 24); // FZ
        __asm__ __volatile__ ("msr fpcr, %0" : : "r"(v));
#endif
    }
    ~ScopedFlushDenormals()
    {
#if defined(SOLO_HAS_SSE_CSR)
        _mm_setcsr ((unsigned int) saved);
#elif defined(__aarch64__)
        __asm__ __volatile__ ("msr fpcr, %0" : : "r"(saved));
#endif
    }
    ScopedFlushDenormals (const ScopedFlushDenormals&) = delete;
    ScopedFlushDenormals& operator= (const ScopedFlushDenormals&) = delete;

private:
    uint64_t saved = 0;
};

constexpr double kPi = 3.14159265358979323846;

struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    float process (float x)
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { z1 = z2 = 0; }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
    }
    void lowpass (double sr, double f, double q = 0.7071)
    {
        f = clampF (f, sr);
        const double w = 2 * kPi * f / sr, c = std::cos (w), a = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + a, -2 * c, 1 - a);
    }
    void highpass (double sr, double f, double q = 0.7071)
    {
        f = clampF (f, sr);
        const double w = 2 * kPi * f / sr, c = std::cos (w), a = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + a, -2 * c, 1 - a);
    }
    void peak (double sr, double f, double q, double gainDb)
    {
        f = clampF (f, sr);
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * kPi * f / sr, c = std::cos (w), a = std::sin (w) / (2 * q);
        set (1 + a * A, -2 * c, 1 - a * A, 1 + a / A, -2 * c, 1 - a / A);
    }
    void lowShelf (double sr, double f, double gainDb)
    {
        f = clampF (f, sr);
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * kPi * f / sr, c = std::cos (w), s = std::sin (w);
        const double a = s / 2 * std::sqrt (2.0), sa = 2 * std::sqrt (A) * a;
        set (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
             (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
    }
    void highShelf (double sr, double f, double gainDb)
    {
        f = clampF (f, sr);
        const double A = std::pow (10.0, gainDb / 40.0), w = 2 * kPi * f / sr, c = std::cos (w), s = std::sin (w);
        const double a = s / 2 * std::sqrt (2.0), sa = 2 * std::sqrt (A) * a;
        set (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
             (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }

private:
    static double clampF (double f, double sr) { return f < 10.0 ? 10.0 : (f > sr * 0.45 ? sr * 0.45 : f); }
};

// Suavizado lineal por bloque: evita clics al cambiar parámetros.
struct Smoothed
{
    float current = 0, target = 0, step = 0;
    int remaining = 0;
    void reset (float v) { current = target = v; remaining = 0; step = 0; }
    void setTarget (float v, int rampSamples)
    {
        if (std::abs (v - target) < 1e-9f && remaining > 0) return;
        target = v;
        if (rampSamples <= 0) { current = v; remaining = 0; return; }
        step = (target - current) / (float) rampSamples;
        remaining = rampSamples;
    }
    float next()
    {
        if (remaining > 0) { current += step; if (--remaining == 0) current = target; }
        return current;
    }
};

// Interpolación cúbica de Hermite sobre 4 puntos (x en [0,1) entre y1 e y2).
inline float hermite (float y0, float y1, float y2, float y3, float x)
{
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * x + c2) * x + c1) * x + y1;
}

inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

} // namespace solo::dsp
