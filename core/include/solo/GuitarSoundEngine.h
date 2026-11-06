#pragma once
// Guitar Tone Studio v3: abstracción del motor de sonido de guitarra.
//   - GuitarSoundEngine: interfaz (mono, pre-amplificador). El compositor y la secuencia MIDI no saben qué motor suena.
//   - ModeledGuitarEngine: evolución del modelo físico (cuerda Karplus-Strong con rigidez, púa, pastilla y cuerpo).
//   - SampleGuitarEngine (Sampler.h): reproducción de muestras reales (SFZ mínimo).
// El motor v2 (InstrumentEngine::Guitar) sigue disponible sin cambios para compatibilidad y comparación A/B.
#include "solo/Expression.h"
#include "solo/Sequence.h"
#include <cstdint>

namespace solo
{
struct TimedEvent; // InstrumentEngine.h

// Parámetros del instrumento (fuente). Persisten como "gtr.clave=valor" y entran en deshacer.
struct GuitarParams
{
    int pickup = 1;                // 0 = simple (brillante), 1 = doble (humbucker: más medios y nivel)
    float pickupPosition = 0.18f;  // fracción de la cuerda desde el puente (0.08..0.45)
    float brightness = 0.55f;      // brillo del lazo (0..1)
    float pickHardness = 0.6f;     // dureza de la púa (0 suave .. 1 dura); la macro Fuerza la sube
    float sustain = 0.5f;          // 0..1 (T60 de la cuerda)
    float stiffness = 0.25f;       // rigidez (inarmonicidad) 0..1
    float bodyResonance = 0.3f;    // resonancia del cuerpo 0..1
    float palmMuteDamp = 0.5f;     // oscuridad/brevedad del palm mute
    float gainDb = 0.0f;           // nivel de la fuente
    bool operator== (const GuitarParams& o) const;
    void sanitise();
};

class GuitarSoundEngine
{
public:
    virtual ~GuitarSoundEngine() = default;
    virtual const char* name() const = 0;
    // Hilo de mensajes (puede reservar memoria).
    virtual void prepare (double sampleRate) = 0;
    // Hilo de audio: sin reservas ni bloqueos.
    virtual void setParams (const GuitarParams& p, const ExpressionScaling& e) = 0;
    virtual void noteOn (int pitch, int velocity, int lengthSamples, const NoteExpression& expr) = 0;
    virtual void noteOff (int pitch) = 0;
    virtual void allNotesOff (bool immediate) = 0; // immediate: fundido de ~6 ms
    virtual void render (float* mono, int numSamples) = 0; // SUMA al buffer
    virtual int activeVoiceCount() const = 0;
    // Procesa los eventos (ordenados por offset) sumando al buffer mono.
    void process (float* mono, int numSamples, const TimedEvent* events, int numEvents);
};

class ModeledGuitarEngine : public GuitarSoundEngine
{
public:
    static constexpr int kMaxVoices = 6;
    ModeledGuitarEngine();
    const char* name() const override { return "Guitarra modelada v3"; }
    void prepare (double sampleRate) override;
    void setParams (const GuitarParams& p, const ExpressionScaling& e) override;
    void noteOn (int pitch, int velocity, int lengthSamples, const NoteExpression& expr) override;
    void noteOff (int pitch) override;
    void allNotesOff (bool immediate) override;
    void render (float* mono, int numSamples) override;
    int activeVoiceCount() const override;

private:
    enum class Stage { Off, Playing, Release, FastRelease };
    struct Voice
    {
        Stage stage = Stage::Off;
        int pitch = -1;
        uint64_t age = 0;
        double baseSemis = 60.0, glideFrom = 0.0;
        int glideSamples = 0, glidePos = 0;
        int64_t pos = 0;
        NoteGestures g;
        float env = 0.0f, loopState = 0.0f, loopGain = 0.999f, loopDamp = 0.3f, apState = 0.0f, apCoef = 0.0f;
        float clickEnv = 0.0f, clickLp = 0.0f, fingerEnv = 0.0f;
        std::vector<float> delay;
        int writePos = 0;
        bool released = false, palmMute = false;
    };
    void configureString (Voice& v);
    void excite (Voice& v, float amount, float brightness, bool full);
    float renderVoice (Voice& v);
    float noise();
    Voice* findLegatoVoice();

    std::vector<Voice> voices;
    double sr = 48000.0;
    uint64_t ageCounter = 0;
    uint32_t noiseState = 0x2545F491u;
    GuitarParams params;
    ExpressionScaling expr;
    bool haveParams = false;
    // Pastilla y cuerpo
    float pickupRes1 = 0, pickupRes2 = 0; // SVF resonante
    float pickupF = 0, pickupQ = 1;
    float bodyLp = 0;
    std::vector<float> pickupComb; // comb de posición de pastilla (retardo corto)
    int combPos = 0;
    float hpState = 0.0f, hpPrev = 0.0f;
    float smoothedGain = 1.0f;
};

} // namespace solo
