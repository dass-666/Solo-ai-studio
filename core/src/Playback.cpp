#include "solo/Playback.h"
#include <algorithm>
#include <cmath>

namespace solo
{
namespace
{
TimedEvent offEvent (int offset, uint8_t pitch)
{
    TimedEvent e;
    e.sampleOffset = offset;
    e.type = EventType::NoteOff;
    e.pitch = pitch;
    e.velocity = 0;
    return e;
}
} // namespace

std::unique_ptr<PlaybackData> makePlaybackData (const Sequence& seqIn, int64_t startOffsetTicks, bool loop)
{
    Sequence seq = seqIn;
    seq.normalise();
    auto d = std::make_unique<PlaybackData>();
    d->events = buildEventList (seq);
    d->lengthTicks = seq.lengthTicks();
    d->startOffsetTicks = startOffsetTicks;
    d->loop = loop;
    return d;
}

int SoloPlayer::flush (TimedEvent* out, int maxOut)
{
    int n = 0;
    for (int p = 0; p < 128; ++p)
    {
        while (active[(size_t) p] > 0 && n < maxOut)
        {
            out[n++] = offEvent (0, (uint8_t) p);
            --active[(size_t) p];
        }
    }
    return n;
}

int SoloPlayer::activeNoteCount() const
{
    int n = 0;
    for (auto a : active) n += a;
    return n;
}

void SoloPlayer::reset()
{
    active.fill (0);
    wasPlaying = false;
    haveSampleTime = false;
    lastData = nullptr;
    resyncs = nudges = 0;
    lastResynced = false;
    lastDeviation = 0.0;
}

int SoloPlayer::process (const TransportState& t, double sampleRate, int numSamples, const PlaybackData* data,
                         TimedEvent* out, int maxOut)
{
    int n = 0;
    lastResynced = false;
    if (data != lastData)
    {
        // Nueva secuencia publicada: cerrar las notas de la anterior.
        n += flush (out + n, maxOut - n);
        lastData = data;
        wasPlaying = false; // fuerza resincronización
    }
    if (! data || ! t.valid || ! t.playing || numSamples <= 0 || sampleRate <= 0.0 || t.bpm <= 0.0)
    {
        n += flush (out + n, maxOut - n);
        wasPlaying = false;
        haveSampleTime = false;
        return n;
    }

    const double ppqPerSample = t.bpm / 60.0 / sampleRate;
    // ¿Continúa este bloque al anterior?
    bool continuous = wasPlaying;
    if (continuous)
    {
        if (t.timeInSamplesValid && haveSampleTime)
            continuous = t.timeInSamples == lastSampleTime + lastNumSamples;
        // Aunque las muestras sigan, la posición musical debe seguir también (loop con contador de muestras
        // continuo, salto de un host sin posición en muestras, etc.).
        if (continuous) continuous = std::abs (t.ppqPosition - expectedPpq) <= kJumpTolerancePpq;
    }
    double p0;
    if (! continuous)
    {
        n += flush (out + n, maxOut - n);
        p0 = t.ppqPosition;
        ++resyncs;
        lastResynced = true;
        lastDeviation = 0.0;
    }
    else
    {
        // Reloj interno. Dentro de la banda muerta (cuantización o jitter del host) no se corrige nada; fuera de
        // ella la deriva se corrige poco a poco, como mucho kMaxNudgeSamples por bloque.
        const double dev = t.ppqPosition - expectedPpq;
        lastDeviation = dev;
        p0 = expectedPpq;
        if (std::abs (dev) > kDeadbandPpq)
        {
            const double maxNudge = ppqPerSample * kMaxNudgeSamples;
            const double excess = dev > 0 ? dev - kDeadbandPpq : dev + kDeadbandPpq;
            p0 += std::clamp (excess, -maxNudge, maxNudge);
            ++nudges;
        }
    }
    const double p1 = p0 + numSamples * ppqPerSample;
    wasPlaying = true;
    expectedPpq = p1;
    haveSampleTime = t.timeInSamplesValid;
    lastSampleTime = t.timeInSamples;
    lastNumSamples = numSamples;

    const double ticksPerSample = ppqPerSample * kPPQ;
    const double t0 = p0 * kPPQ - (double) data->startOffsetTicks;
    const double t1 = p1 * kPPQ - (double) data->startOffsetTicks;
    const int64_t L = data->lengthTicks;
    // En un tramo continuo se sigue exactamente donde se dejó: si el reloj se corrigió hacia atrás no se repiten
    // eventos ya emitidos, y si se corrigió hacia delante no se pierde ninguno (salen con offset 0).
    const double segFrom = continuous ? lastEmittedTick : t0;
    lastEmittedTick = t1;
    if (t1 <= 0.0 || L <= 0) return n;

    const auto& ev = data->events;
    // Tolerancia de 1e-6 muestras: un evento que cae (por redondeo de coma flotante) justo en el borde
    // entre dos bloques se asigna siempre igual, sea cual sea el tamaño de bloque. Así la exportación
    // offline y la reproducción en vivo colocan cada nota en la misma muestra.
    const double epsT = 1.0e-6 * ticksPerSample;
    auto emitRange = [&] (int64_t cycleBase, double segA, double segB, bool includeEnd) {
        // Eventos con tick local en [ceil(segA), segB) (o hasta L inclusive al cerrar ciclo).
        const int64_t a = (int64_t) std::ceil (segA - (double) cycleBase - epsT);
        auto it = std::lower_bound (ev.begin(), ev.end(), a, [] (const MidiEvent& e, int64_t v) { return e.tick < v; });
        for (; it != ev.end(); ++it)
        {
            const double global = (double) (it->tick + cycleBase);
            const bool inside = includeEnd ? it->tick <= L && global < segB + 1e-9 : global + epsT < segB;
            if (! inside) break;
            int offset = (int) std::floor ((global - t0) / ticksPerSample + 1.0e-6);
            offset = std::clamp (offset, 0, numSamples - 1);
            auto& act = active[it->pitch];
            if (it->type == EventType::NoteOn)
            {
                if (n + 2 > maxOut) continue; // sin espacio: no iniciar notas (nunca perder note-off)
                if (act > 0) { out[n++] = offEvent (offset, it->pitch); --act; }
                TimedEvent on;
                on.sampleOffset = offset;
                on.type = EventType::NoteOn;
                on.pitch = it->pitch;
                on.velocity = it->velocity;
                on.lengthSamples = (int32_t) std::min (2.0e9, (double) it->lengthTicks / ticksPerSample);
                on.expr = it->expr;
                out[n++] = on;
                ++act;
            }
            else if (act > 0 && n < maxOut)
            {
                out[n++] = offEvent (offset, it->pitch);
                --act;
            }
        }
    };

    const double start = std::max (0.0, segFrom);
    if (! data->loop)
    {
        if (start <= (double) L) emitRange (0, start, std::min (t1, (double) L), t1 >= (double) L);
        return n;
    }
    int64_t k = (int64_t) std::floor (start / (double) L);
    double segStart = start;
    while (segStart < t1 && n < maxOut)
    {
        const double cycleEnd = (double) ((k + 1) * L);
        const double segEnd = std::min (t1, cycleEnd);
        emitRange (k * L, segStart, segEnd, segEnd >= cycleEnd);
        segStart = segEnd;
        ++k;
    }
    return n;
}

} // namespace solo
