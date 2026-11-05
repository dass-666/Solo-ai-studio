#pragma once
// InstrumentEngine: síntesis integrada, ligera y sin samples.
//  - Guitarra eléctrica (modelada): cuerda Karplus-Strong extendida con interpolación cúbica,
//    filtro de lazo dependiente de la altura, excitación con posición de púa, ataque variable,
//    palm mute, legato (hammer-on/pull-off sin nuevo ataque), bends, slides y vibrato.
//    Su carácter "eléctrico" depende sobre todo de la cadena de efectos (drive + ampli + pantalla).
//  - Flauta (sintetizada), Sintetizador lead y Guitarra ligera (el modelo S0–S2).
// NO son instrumentos acústicos realistas; son modelos sintéticos.
#include "solo/Dsp.h"
#include "solo/SoloComposer.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace solo
{
struct TimedEvent
{
    int sampleOffset = 0; // posición dentro del bloque
    EventType type = EventType::NoteOn;
    uint8_t pitch = 60;
    uint8_t velocity = 100;
    int32_t lengthSamples = 0; // note-on: duración prevista (para bends/vibrato)
    NoteExpression expr;       // note-on
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
    void noteOn (int pitch, int velocity, int lengthSamples = 0, const NoteExpression& expr = {});
    void noteOff (int pitch);
    void allNotesOff (bool immediate); // immediate=true corta con un fundido de ~6 ms (sin clic)
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
        int inst = 0;
        int pitch = -1;
        float velocity = 0.0f;
        uint64_t age = 0;
        double baseSemis = 60.0;   // altura base (MIDI, fraccional durante glides)
        double glideFrom = 0.0;    // semitonos de deslizamiento restantes al inicio
        int glideSamples = 0, glidePos = 0;
        float env = 0.0f;
        double phase1 = 0.0, phase2 = 0.0, vibPhase = 0.0;
        int64_t pos = 0;           // muestras desde el note-on
        int lengthSamples = 0;
        NoteExpression expr;
        float lp1 = 0.0f, lp2 = 0.0f;
        float svfLow = 0.0f, svfBand = 0.0f;
        // Cuerda
        std::vector<float> delay;
        int writePos = 0;
        float loopState = 0.0f;
        float loopGain = 0.999f, loopDamp = 0.3f;
        float clickEnv = 0.0f;
        bool released = false;
    };

    struct Preset { float attack, decay, sustain, release; };

    void renderVoices (float* left, float* right, int start, int count);
    float renderSample (Voice& v);
    double pitchOffset (Voice& v); // semitonos por bend/vibrato/slide en este instante
    void advanceEnvelope (Voice& v, const Preset& p);
    void exciteString (Voice& v, float amount, float brightness, bool fullBurst);
    void configureString (Voice& v);
    float noise();
    static Preset presetFor (int inst);
    Voice* findLegatoVoice();

    std::array<Voice, kMaxVoices> voices;
    double sampleRate = 44100.0;
    uint64_t ageCounter = 0;
    uint32_t noiseState = 0x12345678u;
    std::atomic<int> pendingInstrument { 0 };
    int currentInst = 0;
    std::atomic<float> outputGain { 1.0f };
    float smoothedGain = 1.0f;
    // Filtro de "cuerpo/pastilla" para la guitarra modelada.
    dsp::Biquad bodyHp, bodyPeak, bodyLp;
};

} // namespace solo
