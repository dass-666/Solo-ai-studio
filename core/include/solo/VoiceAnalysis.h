#pragma once
// Hum-to-Solo: análisis de una voz aislada (tarareo/canto) y conversión en notas.
//  - Lectura de WAV (PCM 16/24/32 bits, float 32, mono/estéreo, WAVE_FORMAT_EXTENSIBLE).
//  - Altura (F0) con YIN a 16 kHz, confianza por trama, energía, ataques y pausas.
//  - Segmentación en notas con detección de deslizamientos (glides).
//  - Modos: Copiar / Corregir / Interpretar / Desarrollar, y "solo ritmo".
// Validado únicamente con señales vocales SINTÉTICAS (ver pruebas). No se ha comprobado con voces reales:
// la reverberación, el ruido, la respiración o la polifonía de una grabación real pueden degradar el resultado.
#include "solo/SoloComposer.h"
#include <string>
#include <vector>

namespace solo
{
struct MonoAudio
{
    std::vector<float> samples;
    double sampleRate = 48000.0;
    int sourceChannels = 1;
    int sourceBits = 16;
    double durationSec() const { return sampleRate > 0 ? (double) samples.size() / sampleRate : 0.0; }
};

// Decodifica un WAV completo (mezcla a mono). Rechaza formatos no soportados con un error claro.
bool decodeWav (const std::vector<uint8_t>& bytes, MonoAudio& out, std::string& error);
bool readWavFile (const std::string& path, MonoAudio& out, std::string& error);

struct PitchFrame
{
    double timeSec = 0;
    double f0Hz = 0;        // 0 = sin altura
    double midi = 0;        // altura MIDI fraccional (0 = sin altura)
    double confidence = 0;  // 0..1 (1 - mínimo de la diferencia normalizada de YIN)
    double rmsDb = -120;
};

struct VoiceNote
{
    double startSec = 0, endSec = 0;
    double midi = 0;          // altura representativa (mediana de la parte estable)
    double confidence = 0;    // media de las tramas
    double glideInSemis = 0;  // deslizamiento de entrada (altura inicial - altura de la nota)
    double vibratoSemis = 0;  // amplitud de oscilación detectada (pico)
    double levelDb = -120;    // nivel medio
    bool pitched = true;      // false = ataque detectado pero sin altura fiable (solo ritmo)
};

struct VoiceAnalysisOptions
{
    double minNoteSec = 0.07;
    double minConfidence = 0.55;  // YIN: tramas por debajo se consideran sin altura
    double silenceDb = -42.0;     // relativo al pico de la grabación
    double splitSemis = 0.8;      // cambio sostenido de altura que separa dos notas
    double minF0 = 70.0, maxF0 = 1000.0;
};

struct VoiceAnalysis
{
    double sampleRate = 0;
    double durationSec = 0;
    std::vector<PitchFrame> frames; // cada 10 ms
    std::vector<VoiceNote> notes;
    double voicedRatio = 0;          // fracción de tramas con sonido que tienen altura fiable
    double meanConfidence = 0;
    bool pitchReliable = false;      // si es false, se recomienda usar solo el ritmo
    double estimatedBpm = 0;         // estimación aproximada a partir de los ataques (0 = desconocido)
    std::vector<std::string> warnings;
};

VoiceAnalysis analyseVoice (const MonoAudio& audio, const VoiceAnalysisOptions& opts = {});

enum class HumMode { Copy = 0, Correct = 1, Interpret = 2, Develop = 3 };
const char* humModeName (HumMode m);

struct HumToSoloOptions
{
    HumMode mode = HumMode::Correct;
    bool rhythmOnly = false;     // usar solo ataques/duraciones; las alturas las elige el compositor
    double quantize = 1.0;       // 0..1 fuerza de cuantización a la rejilla
    int gridTicks = kPPQ / 4;    // semicorchea
    int startBar = 0;            // dónde empieza la toma dentro del solo
    double voiceStartSec = -1;   // segundo de la toma que cae en startBar (-1 = primer ataque)
    // Desarrollar
    double fidelity = 0.6, freedom = 0.5;
};

struct HumToSoloResult
{
    Sequence sequence;
    SeedMotif motif;               // material temático extraído (para Creative DNA / Desarrollar)
    int notesFromVoice = 0;
    bool usedRhythmOnly = false;
    std::vector<std::string> warnings;
};

// 'params' define tonalidad, progresión, compás, compases, instrumento, estilo, BPM y semilla.
HumToSoloResult humToSolo (const VoiceAnalysis& va, const ComposerParams& params, const HumToSoloOptions& opts);

// Convierte notas de voz en un motivo semilla relativo (intervalos + ritmo) cuantizado al BPM dado.
SeedMotif motifFromVoice (const std::vector<VoiceNote>& notes, double bpm, int gridTicks = kPPQ / 4, int maxNotes = 12);

// Corrección localizada: sustituye los compases [fromBar, toBar] de 'current' por lo que dice una toma
// nueva de voz (modo indicado), respetando compases y notas bloqueados. El resto del solo queda idéntico.
Sequence applyVocalCorrection (const Sequence& current, const VoiceAnalysis& take, const ComposerParams& params,
                               HumToSoloOptions opts, int fromBar, int toBar, const std::vector<bool>& lockedBars);

// ---------------------------------------------------------------- señales de prueba
// Voz sintética (tarareo) a partir de notas conocidas: armónicos con formantes, vibrato, glides,
// ataques suaves, respiración y ruido. Sirve para probar el análisis de forma reproducible.
struct SynthVoiceNote { double startSec, durSec, midi; double glideFromSemis = 0; double vibratoSemis = 0.25; };
MonoAudio synthesiseVoice (const std::vector<SynthVoiceNote>& notes, double sampleRate, double noiseDb = -40.0,
                           uint64_t seed = 1, bool unpitched = false);

} // namespace solo
