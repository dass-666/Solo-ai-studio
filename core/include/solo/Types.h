#pragma once
// Tipos básicos compartidos por todo el núcleo. Sin dependencias de JUCE.
#include <cstdint>
#include <string>

namespace solo
{
// Resolución temporal canónica: ticks por negra (PPQ). 960 permite semicorcheas,
// tresillos y fusas exactas sin redondeo.
constexpr int kPPQ = 960;

struct TimeSignature
{
    int numerator = 4;
    int denominator = 4;

    int ticksPerBeat() const { return kPPQ * 4 / denominator; }
    int ticksPerBar() const { return ticksPerBeat() * numerator; }
    bool isSupported() const { return denominator == 4 && (numerator == 4 || numerator == 3); }
    bool operator== (const TimeSignature& o) const { return numerator == o.numerator && denominator == o.denominator; }
};

// Generador pseudoaleatorio determinista e idéntico en todas las plataformas.
// No usamos std::uniform_*_distribution porque su salida depende de la
// implementación de la biblioteca estándar (MSVC vs libstdc++), lo que rompería
// la reproducibilidad de las semillas entre Windows y Linux.
class Rng
{
public:
    explicit Rng (uint64_t seed) : state (seed ^ 0x9E3779B97F4A7C15ull) {}

    uint64_t next()
    {
        // SplitMix64
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    // Entero uniforme en [lo, hi] (ambos incluidos).
    int range (int lo, int hi)
    {
        if (hi <= lo) return lo;
        const uint64_t span = (uint64_t) (hi - lo) + 1;
        return lo + (int) (next() % span);
    }

    // Real uniforme en [0, 1).
    double unit() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }

    bool chance (double p) { return unit() < p; }

private:
    uint64_t state;
};

} // namespace solo
