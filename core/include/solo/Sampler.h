#pragma once
// Guitar Tone Studio v3: motor de samples (SFZ mínimo, sin dependencias).
//
// Lo que se admite del formato SFZ (subconjunto): <control> default_path, <global>/<group>/<region> con
//   sample, key, lokey, hikey, pitch_keycenter, lovel, hivel, seq_length, seq_position, loop_mode, loop_start,
//   loop_end, ampeg_attack, ampeg_release, tune, volume, group_label/region_label (articulación).
// Solo WAV PCM (8/16/24/32 bits, flotante 32) mono o estéreo (se mezcla a mono: la guitarra entra mono al ampli).
// Lo que NO se admite: filtros, LFO, modulaciones, opcodes de CC, FLAC/OGG. Se ignoran sin error.
//
// NO se incluye ninguna biblioteca de guitarra real: el usuario importa la suya (con licencia que lo permita).
// Para probar el motor existe un banco sintético generado con el modelo físico (makeSyntheticBank), claramente
// etiquetado como tal.
#include "solo/GuitarSoundEngine.h"
#include <cstddef>
#include <string>
#include <vector>

namespace solo
{
struct SampleZone
{
    std::vector<float> data;    // mono
    double sampleRate = 44100.0;
    int rootKey = 60, loKey = 0, hiKey = 127, loVel = 1, hiVel = 127;
    int seqLength = 1, seqPosition = 1;
    bool loop = false;
    int loopStart = 0, loopEnd = 0;
    float attackSec = 0.001f, releaseSec = 0.2f, tuneCents = 0.0f, volumeDb = 0.0f;
    std::string articulation = "sustain"; // "sustain", "mute" (palm mute), "legato"...
    std::string file;
};

struct SampleBank
{
    std::vector<SampleZone> zones;
    std::string name, path;
    size_t bytes = 0;
    bool synthetic = false; // generado con el modelo (no son samples reales)

    // Zona para una nota: articulación preferida si existe (si no, "sustain"), capa de velocity y round-robin.
    int findZone (int key, int velocity, int roundRobin, const std::string& articulation) const;
    std::string summary() const;
};

// Carga un SFZ. 'maxBytes' limita la memoria (por seguridad); los samples que falten se notifican en 'error'
// pero no impiden cargar el resto. Devuelve false si no se pudo cargar ninguna zona.
bool loadSfz (const std::string& sfzPath, SampleBank& out, std::string& error, size_t maxBytes = (size_t) 512 * 1024 * 1024);

// Genera un banco de prueba SINTÉTICO (modelo físico) en 'dir': WAVs + "guitarra-sintetica.sfz". Devuelve la ruta del SFZ.
std::string makeSyntheticBank (const std::string& dir, std::string& error, int sampleRate = 44100);

class SampleGuitarEngine : public GuitarSoundEngine
{
public:
    static constexpr int kMaxVoices = 8;
    SampleGuitarEngine();
    const char* name() const override { return "Guitarra con samples"; }
    void prepare (double sampleRate) override;
    // Hilo de audio: el banco lo publica el hilo de mensajes (RealtimeSwap); un banco nuevo corta las voces.
    void setBank (const SampleBank* bank);
    const SampleBank* bank() const { return current; }
    void setParams (const GuitarParams& p, const ExpressionScaling& e) override;
    void noteOn (int pitch, int velocity, int lengthSamples, const NoteExpression& expr) override;
    void noteOff (int pitch) override;
    void allNotesOff (bool immediate) override;
    void render (float* mono, int numSamples) override;
    int activeVoiceCount() const override;

private:
    enum class Stage { Off, Attack, Playing, Release, FastRelease };
    struct Voice
    {
        Stage stage = Stage::Off;
        const SampleZone* zone = nullptr;
        int pitch = -1;
        uint64_t age = 0;
        double pos = 0.0, baseSemis = 60.0, glideFrom = 0.0;
        int glideSamples = 0, glidePos = 0;
        int64_t samples = 0;
        float env = 0.0f, gain = 1.0f, lp = 0.0f;
        NoteGestures g;
    };
    Voice* findLegatoVoice();
    void startVoice (Voice& v, const SampleZone& z, int pitch, const NoteGestures& g);
    float renderVoice (Voice& v);

    std::vector<Voice> voices;
    std::vector<uint8_t> rrCounter; // por tecla
    const SampleBank* current = nullptr;
    double sr = 48000.0;
    uint64_t ageCounter = 0;
    GuitarParams params;
    ExpressionScaling expr;
    float smoothedGain = 1.0f;
};

} // namespace solo
