#include "solo/SoloEngine.h"
#include <algorithm>
#include <cmath>

namespace solo
{
SoloEngine::SoloEngine()
{
    eventScratch.resize ((size_t) kMaxEvents);
    fxSwap.publish (std::make_unique<FxSettings>());
    autoSwap.publish (std::make_unique<AutomationSet>());
    autoHold.fill (-1.0f);
}

void SoloEngine::prepare (double sr, int maxBlockSize)
{
    sampleRate = sr > 0 ? sr : 48000.0;
    instrument.prepare (sampleRate);
    instrument.setOutputGain (1.0f);
    fx.prepare (sampleRate, maxBlockSize);
    player.reset();
    gainSmoothed.reset (outputGain.load());
    autoHoldValid = false;
}

void SoloEngine::publishFx (const FxSettings& s)
{
    auto p = std::make_unique<FxSettings> (s);
    p->sanitise();
    fxSwap.publish (std::move (p));
}

void SoloEngine::publishAutomation (const AutomationSet& a)
{
    auto p = std::make_unique<AutomationSet> (a);
    for (auto& l : p->lanes) l.sortPoints();
    autoSwap.publish (std::move (p));
}

void SoloEngine::collectGarbage()
{
    playback.collectGarbage();
    fxSwap.collectGarbage();
    autoSwap.collectGarbage();
}

SoloEngine::BlockResult SoloEngine::process (float* left, float* right, int n, const TransportState& host)
{
    BlockResult res;
    if (left == nullptr || n <= 0) return res;
    std::fill (left, left + n, 0.0f);
    if (right != left && right != nullptr) std::fill (right, right + n, 0.0f);
    if (right == nullptr) right = left;

    const double bpm = TransportResolver::effectiveBpm (host, useHostTempo.load(), manualBpm.load());
    res.bpm = bpm;
    int count = 0;
    const int maxOut = (int) eventScratch.size();

    if (previewStartRequest.exchange (false))
    {
        // Antes de reiniciar el reproductor hay que cerrar las notas que sonaban (defecto D1:
        // sin esto, las voces de flauta/sinte quedaban colgadas al pulsar "Escuchar" durante la reproducción).
        count += player.flush (eventScratch.data() + count, maxOut - count);
        player.reset();
        previewClock.start();
        previewRunning.store (true);
    }
    if (previewStopRequest.exchange (false))
    {
        previewClock.stop();
        previewRunning.store (false);
    }

    const PlaybackData* data = playback.acquire();
    const FxSettings* fxs = fxSwap.acquire();
    const AutomationSet* autos = autoSwap.acquire();

    TransportState t;
    const double offsetPpq = data ? (double) data->startOffsetTicks / kPPQ : 0.0;
    if (previewClock.isRunning())
    {
        t = previewClock.next (bpm, n, sampleRate);
        t.ppqPosition += offsetPpq; // el reloj interno empieza en el inicio del solo
        if (data && ! data->loop && (t.ppqPosition - offsetPpq) * kPPQ > (double) data->lengthTicks + kPPQ)
        {
            previewClock.stop();
            previewRunning.store (false);
        }
    }
    else if (syncToHost.load())
    {
        t = host;
    }

    count += player.process (t, sampleRate, n, data, eventScratch.data() + count, maxOut - count);
    res.eventCount = count;

    // Posición local del solo (para interfaz y automatización)
    double localTick = -1.0;
    if (data && t.valid && t.playing && data->lengthTicks > 0)
    {
        const double local = t.ppqPosition * kPPQ - (double) data->startOffsetTicks;
        if (local >= 0 && (data->loop || local <= (double) data->lengthTicks))
            localTick = std::fmod (local, (double) data->lengthTicks);
    }
    playhead.store (localTick < 0 ? -1 : (int64_t) localTick);

    instrument.process (left, right, n, eventScratch.data(), count);

    // Automatización: se evalúa al inicio del bloque y se suaviza dentro del procesador de efectos.
    float smoothingMs = 30.0f;
    bool anyAuto = false;
    if (autos)
    {
        for (const auto& lane : autos->lanes)
        {
            if (! lane.enabled || lane.points.empty()) { autoHold[(size_t) lane.target] = -1.0f; continue; }
            anyAuto = true;
            smoothingMs = std::max (smoothingMs, lane.smoothingMs);
            if (localTick >= 0) autoHold[(size_t) lane.target] = lane.valueAt (localTick);
            else if (! autoHoldValid || autoHold[(size_t) lane.target] < 0.0f)
                autoHold[(size_t) lane.target] = lane.valueAt (0.0); // parado: mantiene el último valor
        }
        // Destinos sin curva: sin automatización
        for (int i = 0; i < kNumAutoTargets; ++i)
        {
            const auto* l = autos->find ((AutoTarget) i);
            if (! l || ! l->enabled || l->points.empty()) autoHold[(size_t) i] = -1.0f;
        }
        autoHoldValid = true;
    }

    const double soloTimeSec = localTick >= 0 ? localTick / kPPQ * 60.0 / (t.bpm > 0 ? t.bpm : bpm) : -1.0;
    if (fxs) fx.process (left, right, n, *fxs, bpm, anyAuto ? autoHold.data() : nullptr, smoothingMs, soloTimeSec);

    // Volumen general (parámetro del host), suavizado.
    gainSmoothed.setTarget (outputGain.load(), (int) (0.02 * sampleRate));
    for (int i = 0; i < n; ++i)
    {
        const float g = gainSmoothed.next();
        left[i] *= g;
        if (right != left) right[i] *= g;
    }
    return res;
}

EngineRenderResult renderWithEngine (const Sequence& seq, const EngineRenderOptions& opts)
{
    EngineRenderResult r;
    const double sr = opts.sampleRate > 0 ? opts.sampleRate : 48000.0;
    const int block = std::max (16, opts.blockSize);
    const double bpm = seq.bpm > 0 ? seq.bpm : 120.0;
    FxSettings fxs = opts.fx;
    fxs.sanitise();
    r.tailSeconds = std::min (opts.maxTailSeconds, EffectsChain::tailSeconds (fxs, bpm) + opts.extraTailSeconds);
    const double seconds = (double) seq.lengthTicks() / kPPQ * 60.0 / bpm + r.tailSeconds;
    const int64_t total = (int64_t) std::ceil (seconds * sr);

    auto engine = std::make_unique<SoloEngine>();
    engine->setInstrument (opts.instrument);
    engine->setOutputGain (opts.gain);
    engine->setUseHostTempo (true);
    engine->setSyncToHost (true);
    engine->publishFx (fxs);
    engine->publishAutomation (opts.automation);
    engine->publishSequence (makePlaybackData (seq, 0, false));
    engine->prepare (sr, block);

    r.left.assign ((size_t) total, 0.0f);
    r.right.assign ((size_t) total, 0.0f);
    TransportState t;
    t.valid = true;
    t.playing = true;
    t.bpm = bpm;
    t.tsNumerator = seq.timeSignature.numerator;
    for (int64_t pos = 0; pos < total; pos += block)
    {
        const int n = (int) std::min<int64_t> (block, total - pos);
        t.ppqPosition = (double) pos * bpm / 60.0 / sr;
        const auto br = engine->process (r.left.data() + pos, r.right.data() + pos, n, t);
        for (int i = 0; i < br.eventCount; ++i)
            (engine->events()[i].type == EventType::NoteOn ? r.noteOns : r.noteOffs)++;
    }
    for (size_t i = 0; i < r.left.size(); ++i)
        r.peak = std::max ({ r.peak, std::abs (r.left[i]), std::abs (r.right[i]) });
    r.activeVoicesAtEnd = engine->activeVoiceCount();
    return r;
}

} // namespace solo
