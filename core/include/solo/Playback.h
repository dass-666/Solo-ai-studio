#pragma once
// HostTransport + reproducción en tiempo real.
//  - TransportState: lo que el host (o el reloj manual) dice en cada bloque.
//  - SoloPlayer: convierte la secuencia en eventos con offset de muestra exacto,
//    gestiona loops, saltos, parada y cambios de secuencia sin notas colgadas.
//  - RealtimeSwap: publica datos del hilo de mensajes al de audio sin bloqueos.
#include "solo/InstrumentEngine.h"
#include "solo/Sequence.h"
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace solo
{
struct TransportState
{
    bool valid = false;     // false si el host no da posición/tempo utilizables
    bool playing = false;
    double bpm = 120.0;
    double ppqPosition = 0.0; // posición en negras al inicio del bloque
    int tsNumerator = 4, tsDenominator = 4;
};

// Elige la fuente de transporte. Si el host no da información válida se usa el BPM manual.
struct TransportResolver
{
    static double effectiveBpm (const TransportState& host, bool useHostTempo, double manualBpm)
    {
        if (useHostTempo && host.valid && host.bpm >= 20.0 && host.bpm <= 999.0) return host.bpm;
        return manualBpm;
    }
};

// Datos inmutables que consume el hilo de audio.
struct PlaybackData
{
    std::vector<MidiEvent> events; // ordenados
    int64_t lengthTicks = 0;
    int64_t startOffsetTicks = 0;  // dónde empieza el solo en la línea de tiempo del host
    bool loop = true;
};
std::unique_ptr<PlaybackData> makePlaybackData (const Sequence& seq, int64_t startOffsetTicks, bool loop);

// Reloj interno para escuchar el solo sin depender del play del host.
class ManualClock
{
public:
    void start() { position = 0.0; running = true; }
    void stop() { running = false; }
    bool isRunning() const { return running; }
    TransportState next (double bpm, int numSamples, double sampleRate)
    {
        TransportState t;
        t.valid = true;
        t.playing = running;
        t.bpm = bpm;
        t.ppqPosition = position;
        if (running) position += (double) numSamples * bpm / 60.0 / sampleRate;
        return t;
    }

private:
    double position = 0.0;
    bool running = false;
};

class SoloPlayer
{
public:
    // Escribe hasta maxOut eventos en 'out'. Devuelve cuántos. Seguro en tiempo real.
    int process (const TransportState& t, double sampleRate, int numSamples, const PlaybackData* data,
                 TimedEvent* out, int maxOut);
    // Corta todas las notas activas (offset 0). Devuelve eventos emitidos.
    int flush (TimedEvent* out, int maxOut);
    int activeNoteCount() const;
    void reset();

private:
    std::array<uint8_t, 128> active {};
    bool wasPlaying = false;
    double expectedPpq = 0.0;
    const PlaybackData* lastData = nullptr;
};

// Intercambio de punteros sin bloqueos (un productor = hilo de mensajes, un consumidor = audio).
// El hilo de audio nunca libera memoria: los objetos retirados se borran en collectGarbage().
template <typename T>
class RealtimeSwap
{
public:
    ~RealtimeSwap()
    {
        delete pending.exchange (nullptr);
        delete retired.exchange (nullptr);
        delete current;
    }

    // Hilo de mensajes.
    void publish (std::unique_ptr<T> obj)
    {
        collectGarbage();
        delete pending.exchange (obj.release());
    }
    void collectGarbage() { delete retired.exchange (nullptr); }

    // Hilo de audio: devuelve el objeto vigente (puede ser nullptr).
    const T* acquire()
    {
        if (retired.load (std::memory_order_acquire) == nullptr)
        {
            if (T* p = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                retired.store (current, std::memory_order_release);
                current = p;
            }
        }
        return current;
    }

private:
    std::atomic<T*> pending { nullptr };
    std::atomic<T*> retired { nullptr };
    T* current = nullptr; // propiedad del hilo de audio
};

} // namespace solo
