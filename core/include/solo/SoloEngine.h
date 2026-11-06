#pragma once
// SoloEngine: motor de audio completo (transporte + reproductor + instrumento + efectos + automatización).
// Lo usan tanto el plugin (tiempo real) como el render offline (exportación WAV), de modo que lo que
// se exporta pasa por exactamente el mismo código que lo que se escucha.
#include "solo/Effects.h"
#include "solo/GuitarSoundEngine.h"
#include "solo/Playback.h"
#include "solo/Sampler.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace solo
{
struct AutomationSet
{
    std::vector<AutomationLane> lanes; // como mucho una por destino
    bool operator== (const AutomationSet& o) const { return lanes == o.lanes; }
    const AutomationLane* find (AutoTarget t) const
    {
        for (const auto& l : lanes)
            if (l.target == t) return &l;
        return nullptr;
    }
    AutomationLane& lane (AutoTarget t)
    {
        for (auto& l : lanes)
            if (l.target == t) return l;
        AutomationLane nl;
        nl.target = t;
        lanes.push_back (nl);
        return lanes.back();
    }
};

// Guitar Tone Studio v3: qué motor suena cuando el instrumento es la guitarra y con qué parámetros.
enum class GuitarEngineKind { LegacyV2 = 0, Modeled = 1, Samples = 2 };
constexpr int kNumGuitarEngines = 3;
const char* guitarEngineName (GuitarEngineKind k);

struct GuitarRigSettings
{
    GuitarEngineKind engine = GuitarEngineKind::LegacyV2;
    GuitarParams guitar;
    ExpressionScaling expression = expressionScaling (65.0);
};

using SharedBank = std::shared_ptr<const SampleBank>;
using SharedImpulse = std::shared_ptr<const CabImpulse>;

class SoloEngine
{
public:
    static constexpr int kMaxEvents = 4096;

    SoloEngine();

    // ------------------------------------------------ hilo de mensajes (pueden reservar memoria)
    void prepare (double sampleRate, int maxBlockSize);
    void publishSequence (std::unique_ptr<PlaybackData> data) { playback.publish (std::move (data)); havePublished = false; }
    // Publica solo si la secuencia, el inicio o el loop han cambiado. Publicar una secuencia nueva corta las notas que
    // suenan (es lo correcto si cambian las notas), así que mover un control que no toca notas NO debe republicar.
    // Devuelve true si publicó. seq == nullptr = sin solo.
    bool publishSequenceIfChanged (const Sequence* seq, int64_t startOffsetTicks, bool loop);
    void publishFx (const FxSettings& fx);
    void publishAutomation (const AutomationSet& a);
    // v3
    void publishRig (const GuitarRigSettings& r);
    void publishSampleBank (SharedBank bank);     // nullptr = sin banco
    void publishImpulse (SharedImpulse ir);       // nullptr = pantalla por modelo
    void collectGarbage();
    // Latencia (muestras) de los efectos publicados (sobremuestreo del ampli v3). Hilo de mensajes.
    int latencySamples() const { return publishedLatency; }

    // ------------------------------------------------ cualquier hilo (atómicos)
    void setInstrument (InstrumentId id) { instrument.setInstrument (id); instrumentSel.store ((int) id); }
    void setSyncToHost (bool b) { syncToHost.store (b); }
    void setUseHostTempo (bool b) { useHostTempo.store (b); }
    void setManualBpm (double b) { manualBpm.store (b); }
    void setOutputGain (float g) { outputGain.store (g); }
    void requestPreviewStart() { previewStopRequest.store (false); previewStartRequest.store (true); }
    void requestPreviewStop() { previewStartRequest.store (false); previewStopRequest.store (true); }
    bool isPreviewing() const { return previewRunning.load(); }
    int64_t playheadTick() const { return playhead.load(); }

    // ------------------------------------------------ hilo de audio (sin reservas ni bloqueos)
    struct BlockResult
    {
        int eventCount = 0;   // eventos MIDI de este bloque (ver events())
        double bpm = 120.0;   // tempo efectivo
        bool resynced = false;      // el reproductor arrancó o detectó un salto/loop (cerró notas)
        double deviationPpq = 0.0;  // host - reloj interno (0 si no hay tramo continuo)
        bool transportPlaying = false; // transporte efectivo (host o reloj interno) en marcha
    };
    // Escribe el audio (estéreo; si right == left, mono) sustituyendo el contenido del buffer.
    BlockResult process (float* left, float* right, int numSamples, const TransportState& host);
    const TimedEvent* events() const { return eventScratch.data(); }
    int activeVoiceCount() const { return instrument.activeVoiceCount() + modeled.activeVoiceCount() + sampler.activeVoiceCount(); }
    uint32_t resyncCount() const { return player.resyncCount(); }
    uint32_t nudgeCount() const { return player.nudgeCount(); }

private:
    InstrumentEngine instrument;
    ModeledGuitarEngine modeled;
    SampleGuitarEngine sampler;
    EffectsChain fx;
    SoloPlayer player;
    ManualClock previewClock;
    RealtimeSwap<PlaybackData> playback;
    RealtimeSwap<FxSettings> fxSwap;
    RealtimeSwap<AutomationSet> autoSwap;
    RealtimeSwap<GuitarRigSettings> rigSwap;
    RealtimeSwap<SharedBank> bankSwap;
    RealtimeSwap<SharedImpulse> irSwap;
    std::vector<TimedEvent> eventScratch;
    std::vector<float> monoScratch;
    double sampleRate = 48000.0;
    std::atomic<int> instrumentSel { 0 };
    int lastSource = -1; // 0 = InstrumentEngine, 1 = modelada v3, 2 = samples
    int publishedLatency = 0;

    std::atomic<bool> previewStartRequest { false }, previewStopRequest { false }, previewRunning { false };
    std::atomic<bool> syncToHost { true }, useHostTempo { true };
    std::atomic<double> manualBpm { 120.0 };
    std::atomic<float> outputGain { 1.0f };
    std::atomic<int64_t> playhead { -1 };
    dsp::Smoothed gainSmoothed;
    std::array<float, kNumAutoTargets> autoHold {};
    bool autoHoldValid = false;

    // Último publicado (solo hilo de mensajes)
    bool havePublished = false, lastHadSeq = false, lastLoop = false;
    Sequence lastSeq;
    int64_t lastOffset = 0;
};

// ---------------------------------------------------------------- render offline
struct EngineRenderOptions
{
    double sampleRate = 48000.0;
    int blockSize = 512;
    InstrumentId instrument = InstrumentId::Guitar;
    float gain = 1.0f;
    FxSettings fx;            // por defecto: solo salida (sin efectos)
    AutomationSet automation;
    double extraTailSeconds = 1.0; // release del instrumento
    double maxTailSeconds = 12.0;
    // v3
    GuitarRigSettings rig;
    SharedBank bank;
    SharedImpulse impulse;
};

struct EngineRenderResult
{
    std::vector<float> left, right;
    int noteOns = 0, noteOffs = 0;
    float peak = 0.0f;
    double tailSeconds = 0.0;
    int activeVoicesAtEnd = 0;
};

// Renderiza la secuencia con efectos y automatización, incluida la cola de delay/reverb.
EngineRenderResult renderWithEngine (const Sequence& seq, const EngineRenderOptions& opts);

} // namespace solo
