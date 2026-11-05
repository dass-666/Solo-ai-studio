#pragma once
// InstrumentEngine: síntesis integrada, ligera y sin samples.
// Son sonidos sintéticos básicos (pluck Karplus-Strong, flauta aditiva + soplo,
// lead de sierras filtradas). NO pretenden ser instrumentos acústicos realistas.
#include "solo/SoloComposer.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace solo
{
struct TimedEvent
{
    int sampleOffset; // posición dentro del bloque
    EventType type;
    uint8_t pitch;
    uint8_t velocity;
};

class InstrumentEngine
{
public:
    static constexpr int kMaxVoices = 8;

    InstrumentEngine();

    // Hilo de mensajes / antes de reproducir. Puede reservar memoria.
    void prepare (double sampleRate);

    // Seguros en tiempo real (sin bloqueos ni reservas de memoria).
    void setInstrument (InstrumentId id) { pendingInstrument.store ((int) id); }
    void noteOn (int pitch, int velocity);
    void noteOff (int pitch);
    void allNotesOff (bool immediate); // immediate=true corta sin release (con micro-fundido)
    // Suma el audio de 'numSamples' a L/R procesando los eventos (ordenados por offset).
    void process (float* left, float* right, int numSamples, const TimedEvent* events, int numEvents);
    int activeVoiceCount() const;
    void setOutputGain (float g) { outputGain.store (g); }

    InstrumentId currentInstrument() const { return (InstrumentId) currentInst; }

private:
    enum class Stage { Off, Attack, Decay, Sustain, Release, FastRelease };

    struct Voice
    {
        Stage stage = Stage::Off;
        int pitch = -1;
        float velocity = 0.0f;
        uint64_t age = 0;
        double freq = 440.0;
        float env = 0.0f;
        double phase1 = 0.0, phase2 = 0.0, vibPhase = 0.0;
        double timeOn = 0.0;
        float lp1 = 0.0f, lp2 = 0.0f, bp = 0.0f;   // filtros
        float svfLow = 0.0f, svfBand = 0.0f;
        // Karplus-Strong
        std::vector<float> delay;
        int writePos = 0;
        double delaySamples = 100.0;
        float ksPrev = 0.0f;
    };

    struct Preset { float attack, decay, sustain, release; };

    void renderVoices (float* left, float* right, int start, int count);
    float renderSample (Voice& v);
    void advanceEnvelope (Voice& v, const Preset& p);
    float noise();
    static Preset presetFor (int inst);

    std::array<Voice, kMaxVoices> voices;
    double sampleRate = 44100.0;
    uint64_t ageCounter = 0;
    uint32_t noiseState = 0x12345678u;
    std::atomic<int> pendingInstrument { 0 };
    int currentInst = 0;
    std::atomic<float> outputGain { 1.0f };
    float smoothedGain = 1.0f;
};

} // namespace solo
