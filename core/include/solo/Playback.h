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
    // Posición en muestras del host (opcional). Si el host la da, la continuidad entre bloques se decide con ella
    // (exacta, entera) y no con la posición musical, que algunos hosts cuantizan o redondean.
    bool timeInSamplesValid = false;
    int64_t timeInSamples = 0;
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
    void start() { position = 0.0; samples = 0; running = true; }
    void stop() { running = false; }
    bool isRunning() const { return running; }
    TransportState next (double bpm, int numSamples, double sampleRate)
    {
        TransportState t;
        t.valid = true;
        t.playing = running;
        t.bpm = bpm;
        t.ppqPosition = position;
        t.timeInSamplesValid = true;
        t.timeInSamples = samples;
        if (running) { position += (double) numSamples * bpm / 60.0 / sampleRate; samples += numSamples; }
        return t;
    }

private:
    double position = 0.0;
    int64_t samples = 0;
    bool running = false;
};

// Reproductor con reloj interno enganchado al host:
//  - La continuidad entre bloques se decide por la posición en muestras del host si la da; si no, por la
//    posición musical con una tolerancia de 1/16 de negra (los hosts que cuantizan o redondean el PPQ no
//    provocan cortes).
//  - Dentro de un tramo continuo, la posición de cada bloque es la del reloj interno (exacta en muestras).
//    Si el host se aleja más de la banda muerta (kDeadbandPpq), se corrige como mucho kMaxNudgeSamples por
//    bloque hacia la del host (deriva o cambio de tempo); dentro de la banda no se toca (cuantización, jitter).
//  - Un salto real (fuera de tolerancia), un loop, Stop o una secuencia nueva cierran todas las notas.
class SoloPlayer
{
public:
    static constexpr double kJumpTolerancePpq = 1.0 / 16.0; // 1/16 de negra: 31 ms a 120 BPM
    static constexpr double kDeadbandPpq = 1.0 / 64.0;      // 1/64 de negra: cubre un tick de FL (1/96) y jitter < 7 ms
    static constexpr double kMaxNudgeSamples = 2.0;

    // Escribe hasta maxOut eventos en 'out'. Devuelve cuántos. Seguro en tiempo real.
    int process (const TransportState& t, double sampleRate, int numSamples, const PlaybackData* data,
                 TimedEvent* out, int maxOut);
    // Corta todas las notas activas (offset 0). Devuelve eventos emitidos.
    int flush (TimedEvent* out, int maxOut);
    int activeNoteCount() const;
    void reset();

    // Diagnóstico (lo lee la interfaz; contadores desde reset()).
    uint32_t resyncCount() const { return resyncs; }        // arranques y saltos/loops (cierran notas)
    uint32_t nudgeCount() const { return nudges; }          // bloques en los que el reloj se corrigió < tolerancia
    bool lastBlockResynced() const { return lastResynced; }
    double lastDeviationPpq() const { return lastDeviation; } // host - reloj interno en el último bloque

private:
    std::array<uint8_t, 128> active {};
    bool wasPlaying = false;
    double expectedPpq = 0.0;     // posición interna prevista para el bloque siguiente
    double lastEmittedTick = 0.0; // tick global hasta el que se han emitido eventos (evita duplicados al corregir)
    int64_t lastSampleTime = 0;
    int lastNumSamples = 0;
    bool haveSampleTime = false;
    const PlaybackData* lastData = nullptr;
    uint32_t resyncs = 0, nudges = 0;
    bool lastResynced = false;
    double lastDeviation = 0.0;
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
