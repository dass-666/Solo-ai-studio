#include "solo/Playback.h"
#include <algorithm>
#include <cmath>

namespace solo
{
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
            out[n++] = { 0, EventType::NoteOff, (uint8_t) p, 0 };
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
    lastData = nullptr;
}

int SoloPlayer::process (const TransportState& t, double sampleRate, int numSamples, const PlaybackData* data,
                         TimedEvent* out, int maxOut)
{
    int n = 0;
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
        return n;
    }

    const double ppqPerSample = t.bpm / 60.0 / sampleRate;
    const double p0 = t.ppqPosition;
    const double p1 = p0 + numSamples * ppqPerSample;
    // Salto/loop/reubicación del host: si la posición no continúa la del bloque anterior.
    const double tolerance = std::max (1.0e-3, ppqPerSample * 4.0);
    if (! wasPlaying || std::abs (p0 - expectedPpq) > tolerance)
        n += flush (out + n, maxOut - n);
    wasPlaying = true;
    expectedPpq = p1;

    const double ticksPerSample = ppqPerSample * kPPQ;
    const double t0 = p0 * kPPQ - (double) data->startOffsetTicks;
    const double t1 = p1 * kPPQ - (double) data->startOffsetTicks;
    const int64_t L = data->lengthTicks;
    if (t1 <= 0.0 || L <= 0) return n;

    const auto& ev = data->events;
    auto emitRange = [&] (int64_t cycleBase, double segA, double segB, bool includeEnd) {
        // Eventos con tick local en [ceil(segA), segB) (o hasta L inclusive al cerrar ciclo).
        const int64_t a = (int64_t) std::ceil (segA - (double) cycleBase - 1e-9);
        auto it = std::lower_bound (ev.begin(), ev.end(), a, [] (const MidiEvent& e, int64_t v) { return e.tick < v; });
        for (; it != ev.end(); ++it)
        {
            const double global = (double) (it->tick + cycleBase);
            const bool inside = includeEnd ? it->tick <= L && global < segB + 1e-9 : global < segB;
            if (! inside) break;
            int offset = (int) std::floor ((global - t0) / ticksPerSample);
            offset = std::clamp (offset, 0, numSamples - 1);
            auto& act = active[it->pitch];
            if (it->type == EventType::NoteOn)
            {
                if (n + 2 > maxOut) continue; // sin espacio: no iniciar notas (nunca perder note-off)
                if (act > 0) { out[n++] = { offset, EventType::NoteOff, it->pitch, 0 }; --act; }
                out[n++] = { offset, EventType::NoteOn, it->pitch, it->velocity };
                ++act;
            }
            else if (act > 0 && n < maxOut)
            {
                out[n++] = { offset, EventType::NoteOff, it->pitch, 0 };
                --act;
            }
        }
    };

    const double start = std::max (0.0, t0);
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
