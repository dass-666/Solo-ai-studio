// Intérprete: aplica articulaciones instrumentales sobre las notas del compositor.
// Determinista: cada nota usa un generador derivado de (semilla, posición, altura), de modo que
// regenerar unas notas no cambia la interpretación de las demás.
#include "solo/Performer.h"
#include <algorithm>
#include <cmath>

namespace solo
{
namespace
{
//                                vibD  vibR  vibDel slide legato pm    curl  att art   stac  laid dyn
const PerformerTraits kTraits[kNumPerformers] = {
    /* Auto (no se usa) */      { 0.30, 5.5, 0.30, 0.20, 0.30, 0.00, 0.00, 64, 0.95, 0.10, 0,  1.0 },
    /* Melodic */               { 0.32, 5.2, 0.35, 0.25, 0.35, 0.00, 0.05, 58, 0.97, 0.05, 6,  1.0 },
    /* Blues */                 { 0.42, 5.8, 0.25, 0.35, 0.30, 0.00, 0.35, 70, 0.92, 0.20, 18, 1.25 },
    /* Rock */                  { 0.36, 6.2, 0.25, 0.25, 0.30, 0.45, 0.10, 84, 0.93, 0.15, 6,  1.15 },
    /* Virtuoso */              { 0.30, 6.6, 0.20, 0.15, 0.65, 0.30, 0.05, 80, 0.96, 0.10, 0,  1.0 },
    /* Ambient */               { 0.18, 4.6, 0.45, 0.30, 0.50, 0.00, 0.00, 24, 1.00, 0.00, 0,  0.6 },
    /* None */                  { 0.00, 5.5, 0.30, 0.00, 0.00, 0.00, 0.00, 64, 1.00, 0.00, 0,  1.0 },
};

uint64_t mix (uint64_t a, uint64_t b)
{
    uint64_t z = a ^ (b + 0x9E3779B97F4A7C15ull + (a << 6) + (a >> 2));
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    return z ^ (z >> 27);
}

// Sustituye un bend por la nota final (para instrumentos que no lo admiten o el perfil "None").
void flattenBend (Note& n)
{
    if (n.expr.bendType == BendType::Up || n.expr.bendType == BendType::PreBend) n.pitch = n.targetPitch();
    n.expr.bendType = BendType::None;
    n.expr.bendSemitones = 0.0f;
}
} // namespace

PerformerProfile resolvePerformer (Style style, PerformerProfile requested, InstrumentId instrument)
{
    if (requested != PerformerProfile::Auto) return requested;
    PerformerProfile p = PerformerProfile::Melodic;
    switch (style)
    {
        case Style::MelodicRock:     p = PerformerProfile::Rock; break;
        case Style::ExpressiveBlues: p = PerformerProfile::Blues; break;
        case Style::Emotional:       p = PerformerProfile::Melodic; break;
        case Style::VirtuosoRock:    p = PerformerProfile::Virtuoso; break;
        case Style::Ambient:         p = PerformerProfile::Ambient; break;
        case Style::Cinematic:       p = PerformerProfile::Melodic; break;
        case Style::SoftImprov:      p = PerformerProfile::Melodic; break;
        case Style::VocalResponse:   p = PerformerProfile::Melodic; break;
    }
    // Un flautista no hace palm mute ni tiene un ataque "rock": usamos el perfil melódico/ambiental.
    if (instrument == InstrumentId::Flute && (p == PerformerProfile::Rock || p == PerformerProfile::Virtuoso))
        p = PerformerProfile::Melodic;
    return p;
}

const PerformerTraits& traitsFor (PerformerProfile p)
{
    const int i = std::clamp ((int) p, 0, kNumPerformers - 1);
    return kTraits[i];
}

void performSequence (Sequence& seq, const ComposerParams& params, PerformerProfile profile, uint64_t seed)
{
    if (seq.notes.empty()) return;
    std::sort (seq.notes.begin(), seq.notes.end(), [] (const Note& a, const Note& b) {
        return a.startTick != b.startTick ? a.startTick < b.startTick : a.pitch < b.pitch;
    });

    const InstrumentId inst = params.instrument;
    const bool isGuitar = inst == InstrumentId::Guitar || inst == InstrumentId::GuitarLite;
    const bool canBend = isGuitar || inst == InstrumentId::Synth;
    const bool canPalmMute = isGuitar;
    const bool canSlide = inst != InstrumentId::Flute;

    auto& notes = seq.notes;
    const size_t count = notes.size();

    if (profile == PerformerProfile::None)
    {
        for (auto& n : notes)
        {
            if (n.locked) continue;
            flattenBend (n);
            n.expr = NoteExpression {};
        }
        return;
    }

    const PerformerTraits& t = traitsFor (profile);
    const int64_t beat = seq.timeSignature.ticksPerBeat();
    const int64_t bar = seq.timeSignature.ticksPerBar();
    const double bpm = params.bpm > 1.0 ? params.bpm : 120.0;
    const double secPerTick = 60.0 / (bpm * kPPQ);
    const double energy = std::clamp (params.energy, 0.0, 1.0);
    // Registro grave del propio solo (el palm mute se usa en las notas más graves que se repiten).
    int lowPitch = 127;
    for (const auto& n : notes) lowPitch = std::min (lowPitch, n.pitch);

    // Velocidad media (para escalar el rango dinámico sin desplazar el nivel global).
    double meanVel = 0.0;
    for (const auto& n : notes) meanVel += n.velocity;
    meanVel /= (double) count;

    // Decisión de legato antes de tocar duraciones: la nota i se liga con la anterior.
    std::vector<char> legatoIn (count, 0);
    for (size_t i = 0; i < count; ++i)
    {
        Note& n = notes[i];
        if (n.locked || i == 0) { legatoIn[i] = n.expr.legato ? 1 : 0; continue; }
        const Note& p = notes[i - 1];
        Rng r (mix (seed, mix ((uint64_t) n.startTick, (uint64_t) n.pitch * 31u + 7u)));
        const int64_t gap = n.startTick - p.endTick();
        const int interval = std::abs (n.pitch - p.targetPitch());
        const bool contiguous = gap >= 0 && gap <= 24;
        bool leg = n.expr.legato && contiguous && interval <= 5;
        if (! leg && contiguous && interval >= 1 && interval <= 2 && n.lengthTicks <= beat / 2
            && n.expr.bendType == BendType::None)
            leg = r.chance (t.legato * (inst == InstrumentId::Flute ? 1.2 : 1.0));
        legatoIn[i] = leg ? 1 : 0;
    }

    for (size_t i = 0; i < count; ++i)
    {
        Note& n = notes[i];
        if (n.locked) continue;
        Rng r (mix (seed ^ 0x5A17ull, mix ((uint64_t) n.startTick, (uint64_t) n.pitch)));
        NoteExpression& e = n.expr;

        // --- Limitaciones físicas del instrumento ---
        if (! canBend && e.bendType != BendType::None) flattenBend (n);
        if (! canSlide) e.slideFrom = 0;
        if (! canPalmMute) e.palmMute = false;

        const double durSec = (double) n.lengthTicks * secPerTick;
        const int64_t posInBar = n.startTick % bar;
        const bool downbeat = posInBar == 0;
        const bool onBeat = posInBar % beat == 0;
        const bool offBeat = ! onBeat && (posInBar % (beat / 2)) == 0;
        const int64_t gapBefore = i == 0 ? bar : n.startTick - notes[i - 1].endTick();
        const bool phraseStart = gapBefore >= beat / 2;

        // --- Legato ---
        e.legato = legatoIn[i] != 0;

        // --- Slide de entrada al inicio de frase ---
        if (canSlide && phraseStart && e.slideFrom == 0 && e.bendType != BendType::Release
            && e.bendType != BendType::PreBend && n.lengthTicks >= beat / 2 && r.chance (t.slideIn))
            e.slideFrom = (int8_t) (r.chance (0.7) ? -2 : -1);
        if (e.slideFrom != 0) e.slideFrom = (int8_t) std::clamp ((int) e.slideFrom, -5, 5);

        // --- Vibrato: notas largas y las que el compositor marcó ---
        const bool longNote = durSec >= 0.38;
        if (longNote || e.vibratoDepth > 0.0f)
        {
            if (longNote)
            {
                const double depth = t.vibratoDepth * (0.75 + 0.35 * energy) * (0.9 + 0.2 * r.unit());
                e.vibratoDepth = (float) (inst == InstrumentId::Flute ? depth * 0.6 : depth);
                e.vibratoRate = (float) (t.vibratoRate * (0.94 + 0.12 * r.unit()));
                // En notas muy largas el vibrato entra más tarde (proporcionalmente).
                e.vibratoDelay = (float) std::clamp (t.vibratoDelay * (durSec > 1.2 ? 1.2 : 1.0), 0.05, 0.7);
            }
            else
            {
                e.vibratoDepth = 0.0f; // nota corta: no da tiempo a un vibrato audible
            }
        }

        // --- "Curl" de blues (cuarto de tono que vuelve) en notas largas sin bend ---
        if (canBend && e.bendType == BendType::None && longNote && t.curl > 0.0 && r.chance (t.curl))
        {
            e.bendType = BendType::UpRelease;
            e.bendSemitones = 0.3f;
            e.bendStart = 0.05f;
            e.bendTime = 0.35f;
        }

        // --- Palm mute: notas cortas y graves, repetidas o en ritmo constante ---
        if (canPalmMute && t.palmMute > 0.0 && n.lengthTicks <= beat / 2 && ! e.legato
            && e.bendType == BendType::None && n.pitch <= lowPitch + 5)
        {
            const bool repeated = (i > 0 && notes[i - 1].pitch == n.pitch) || (i + 1 < count && notes[i + 1].pitch == n.pitch);
            if (r.chance (t.palmMute * (repeated ? 1.0 : 0.35))) e.palmMute = true;
        }

        // --- Ataque de púa según posición métrica y función ---
        double att = t.attackBase + 18.0 * (energy - 0.5);
        if (downbeat) att += 12; else if (onBeat) att += 6; else if (offBeat) att -= 3; else att -= 8;
        switch (n.role)
        {
            case NoteRole::Climax: att += 16; break;
            case NoteRole::Cadence: att += 4; break;
            case NoteRole::Run: att -= 10; break;
            case NoteRole::Passing: att -= 6; break;
            default: break;
        }
        if (e.legato) att = std::min (att, 40.0);
        if (e.palmMute) att += 6;
        att += r.range (-4, 4);
        e.attack = (uint8_t) std::clamp ((int) std::lround (att), 0, 127);

        // --- Dinámica ---
        const double dv = ((double) n.velocity - meanVel) * t.dynamicRange;
        n.velocity = std::clamp ((int) std::lround (meanVel + dv + (e.legato ? -6 : 0)), 1, 127);
    }

    // --- Duraciones (articulación / staccato) y micro-tiempo ---
    for (size_t i = 0; i < count; ++i)
    {
        Note& n = notes[i];
        if (n.locked) continue;
        const bool nextLegato = i + 1 < count && legatoIn[i + 1] && ! notes[i + 1].locked;
        Rng r (mix (seed ^ 0xD0Dull, mix ((uint64_t) n.startTick, (uint64_t) n.pitch + 3u)));
        if (! nextLegato && n.expr.bendType == BendType::None)
        {
            double f = t.articulation;
            const int64_t posInBeat = n.startTick % beat;
            if (n.lengthTicks <= beat / 2 && posInBeat != 0 && r.chance (t.staccato)) f = 0.55;
            if (n.expr.palmMute) f = std::min (f, 0.8);
            n.lengthTicks = std::max<int64_t> (kPPQ / 32, (int64_t) std::lround ((double) n.lengthTicks * f));
        }
        // "Feel" retrasado en los contratiempos: se mueve el inicio y se conserva el final.
        if (t.laidBackTicks > 0 && ! n.expr.legato)
        {
            const int64_t posInBeat = n.startTick % beat;
            if (posInBeat == beat / 2 && n.lengthTicks > 4 * t.laidBackTicks)
            {
                const int64_t shift = (int64_t) std::lround (t.laidBackTicks * (0.5 + 0.5 * r.unit()));
                n.startTick += shift;
                n.lengthTicks -= shift;
            }
        }
    }
}

} // namespace solo
