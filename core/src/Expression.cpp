#include "solo/Expression.h"
#include <algorithm>
#include <cmath>

namespace solo
{
namespace
{
constexpr double kNeutral = 0.65; // valor de la macro en el que la escala es 1.0
inline double smooth01 (double u) { u = std::clamp (u, 0.0, 1.0); return 0.5 - 0.5 * std::cos (3.141592653589793 * u); }

// 0 en 0 %, 1 en 65 %, 'top' en 100 %.
inline double ramp (double e, double top)
{
    if (e <= kNeutral) return e / kNeutral;
    return 1.0 + (e - kNeutral) / (1.0 - kNeutral) * (top - 1.0);
}
} // namespace

ExpressionScaling expressionScaling (double percent)
{
    const double e = std::clamp (percent / 100.0, 0.0, 1.0);
    ExpressionScaling s;
    s.amount = e;
    s.vibratoDepth = e < 0.03 ? 0.0 : ramp (e, 1.4);
    s.vibratoDelay = e <= kNeutral ? 1.3 - 0.3 * (e / kNeutral) : 1.0 - 0.25 * (e - kNeutral) / (1.0 - kNeutral);
    s.bendTime = e < 0.03 ? 0.0 : ramp (e, 1.25);
    s.slideTime = e < 0.03 ? 0.0 : ramp (e, 1.3);
    s.legatoGlide = 0.6 + 0.4 * ramp (e, 1.5);
    s.attackVariation = e;
    s.sustain = 0.8 + 0.4 * e;
    s.velocitySensitivity = 0.3 + 0.7 * e;
    s.generation = ramp (e, 1.3);
    return s;
}

NoteGestures gesturesFor (const NoteExpression& e, int velocity, double lengthSec, const ExpressionScaling& s, bool guitar)
{
    NoteGestures g;
    const double L = std::max (1.0e-3, lengthSec);
    g.lengthSec = L;
    const double vel = std::clamp (velocity / 127.0, 0.0, 1.0);

    // Bend: el movimiento dura bendTime × escala; con escala 0 la nota suena directamente en su altura final.
    if (e.bendType != BendType::None && e.bendSemitones > 0.0f)
    {
        g.bendSemis = e.bendSemitones;
        g.bendStartSec = e.bendStart * L;
        g.bendTimeSec = std::clamp ((double) e.bendTime * L, 0.03, 0.6) * s.bendTime;
        g.bendType = e.bendType;
        if (s.bendTime <= 0.0)
        {
            // Sin expresión: Up/UpRelease => altura final sin movimiento; Release => altura base.
            if (e.bendType == BendType::Up || e.bendType == BendType::PreBend) g.bendType = BendType::PreBend;
            else g.bendType = BendType::None;
            g.bendTimeSec = 0.0;
        }
        g.bendDownStartSec = std::max (g.bendStartSec + g.bendTimeSec, 0.62 * L);
    }
    // Vibrato: profundidad y entrada escaladas; en notas cortas no hay tiempo para que se oiga.
    if (e.vibratoDepth > 0.0f && s.vibratoDepth > 0.0)
    {
        g.vibratoDepth = e.vibratoDepth * s.vibratoDepth;
        g.vibratoRateHz = e.vibratoRate;
        g.vibratoStartSec = std::min (L, (double) e.vibratoDelay * L * s.vibratoDelay);
        g.vibratoRampSec = 0.18;
        g.vibratoUpward = guitar;
    }
    // Slide de entrada
    if (e.slideFrom != 0 && s.slideTime > 0.0)
    {
        g.slideFromSemis = e.slideFrom;
        g.slideTimeSec = std::min (0.09 * s.slideTime, 0.3 * L);
    }
    g.legato = e.legato;
    g.legatoGlideSec = 0.004 * s.legatoGlide + (e.slideFrom != 0 ? 0.06 * s.legatoGlide : 0.0);
    g.palmMute = e.palmMute;

    // Ataque: la articulación (0..127) decide; la macro decide cuánto se nota esa diferencia.
    const double art = e.attack / 127.0;
    g.attack = 0.5 + (art - 0.5) * (0.3 + 0.7 * s.attackVariation) + (vel - 0.6) * 0.3 * s.velocitySensitivity;
    g.attack = std::clamp (g.attack, 0.0, 1.0);
    g.sustainScale = s.sustain;
    // Velocity: nivel (curva suave) y brillo.
    g.level = std::pow (vel, 0.3 + 0.9 * s.velocitySensitivity) * (1.0 - 0.15 * s.velocitySensitivity) + 0.15 * s.velocitySensitivity * vel;
    g.level = std::clamp (g.level, 0.05, 1.0);
    g.brightness = std::clamp (0.35 + 0.4 * vel * s.velocitySensitivity + 0.3 * (g.attack - 0.5), 0.0, 1.0);
    return g;
}

double gesturePitchOffset (const NoteGestures& g, double t)
{
    double off = 0.0;
    if (g.bendType != BendType::None && g.bendSemis > 0.0)
    {
        const double B = g.bendSemis, bs = g.bendStartSec, bt = std::max (1.0e-6, g.bendTimeSec);
        switch (g.bendType)
        {
            case BendType::Up: off += B * smooth01 ((t - bs) / bt); break;
            case BendType::Release: off += B * (1.0 - smooth01 ((t - bs) / bt)); break;
            case BendType::PreBend: off += B; break;
            case BendType::UpRelease: off += B * (smooth01 ((t - bs) / bt) - smooth01 ((t - g.bendDownStartSec) / bt)); break;
            case BendType::None: break;
        }
    }
    if (g.vibratoDepth > 0.0)
    {
        const double ramp = std::clamp ((t - g.vibratoStartSec) / std::max (1.0e-3, g.vibratoRampSec), 0.0, 1.0);
        if (ramp > 0.0)
        {
            const double ph = 2.0 * 3.141592653589793 * g.vibratoRateHz * (t - g.vibratoStartSec);
            off += (g.vibratoUpward ? g.vibratoDepth * (0.5 - 0.5 * std::cos (ph)) : g.vibratoDepth * std::sin (ph)) * ramp;
        }
    }
    if (g.slideFromSemis != 0.0 && g.slideTimeSec > 0.0 && t < g.slideTimeSec)
        off += g.slideFromSemis * (1.0 - smooth01 (t / g.slideTimeSec));
    return off;
}

double gestureMaxAbsOffset (const NoteGestures& g)
{
    double m = 0.0;
    if (g.bendType != BendType::None) m += g.bendSemis;
    m += g.vibratoDepth;
    m = std::max (m, std::abs (g.slideFromSemis));
    return m;
}

} // namespace solo
