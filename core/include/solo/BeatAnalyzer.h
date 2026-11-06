#pragma once
// Beat Analyzer (V4-A): análisis OFFLINE de un beat en WAV -> tempo, rejilla de pulsos, tonalidad,
// acordes por compás, cambios armónicos y regiones de energía.
//
// Reglas de diseño:
//  - Nunca se ejecuta en el hilo de audio (processBlock). Es una función pura sobre un MonoAudio ya cargado.
//  - Solo DSP ligero (FFT radix-2 propia, flujo espectral, autocorrelación, croma + perfiles de tonalidad).
//    Sin modelos descargados, sin servicios externos, sin dependencias nuevas.
//  - Cada estimación lleva una confianza 0..1 y la interfaz distingue "confirmado" (lo que el usuario fija)
//    de "estimado" (lo que calcula el análisis). Cuando el análisis es ambiguo lo dice (warnings) en vez de
//    inventar certeza.
//  - Determinista: la misma señal produce el mismo resultado.
#include "solo/MusicTheory.h"
#include "solo/VoiceAnalysis.h" // MonoAudio, readWavFile
#include <cstdint>
#include <string>
#include <vector>

namespace solo
{
// Lo que el usuario puede confirmar a mano sobre el resultado del análisis.
enum class Provenance : uint8_t { Estimated = 0, Confirmed = 1 };

struct BpmCandidate { double bpm = 0; double score = 0; };
struct KeyCandidate { Key key; double score = 0; };

// Acorde estimado por tiempo (independiente de la rejilla de compases): segmentos donde el croma es estable.
struct ChordEvent
{
    double startSec = 0, endSec = 0;
    Chord chord;
    double confidence = 0;
};

struct ChordSegment
{
    int startBar = 0;      // base 0
    int lengthBars = 1;
    Chord chord;
    double confidence = 0; // 0..1
    Provenance provenance = Provenance::Estimated;
};

// Línea temporal armónica: tonalidad + acordes por compás + rejilla. Es la representación EDITABLE
// que consume el compositor (V4-B). Puede construirse sin beat (acordes manuales).
struct HarmonyTimeline
{
    Key key;
    double keyConfidence = 0;
    Provenance keyProvenance = Provenance::Estimated;
    double bpm = 120.0;
    double bpmConfidence = 0;
    Provenance bpmProvenance = Provenance::Estimated;
    int beatsPerBar = 4;
    int bars = 0;
    std::vector<ChordSegment> segments; // ordenados, sin huecos (cubren 0..bars)

    bool empty() const { return bars <= 0 || segments.empty(); }
    Chord chordAtBar (int bar) const;                 // último segmento que empieza <= bar
    double chordConfidenceAtBar (int bar) const;
    void setChord (int fromBar, int toBar, const Chord& c, Provenance p = Provenance::Confirmed); // parte segmentos
    void setKey (const Key& k, Provenance p = Provenance::Confirmed);
    void setBpm (double bpm, Provenance p = Provenance::Confirmed);
    Progression toProgression() const;                // un acorde por compás (lo que entiende SoloComposer)
    std::vector<int> changeBars() const;              // compases donde cambia el acorde (sin el 0)
    std::string toString() const;                     // "Am | F | C | G" ...
    static HarmonyTimeline fromManual (const Key& k, const Progression& p, double bpm, int beatsPerBar = 4);
};

enum class EnergyLevel : uint8_t { Low = 0, Mid = 1, High = 2 };

struct EnergyRegion
{
    double startSec = 0, endSec = 0;
    int startBar = 0, endBar = 0; // [startBar, endBar)
    EnergyLevel level = EnergyLevel::Mid;
    double rmsDb = -60;
};

struct BeatAnalysisOptions
{
    double minBpm = 60.0;
    double maxBpm = 200.0;
    double maxDurationSec = 600.0;   // más largo -> error (memoria/tiempo acotados)
    int beatsPerBar = 4;
    bool detectChords = true;
};

struct BeatAnalysisResult
{
    static constexpr int kVersion = 1;
    bool ok = false;
    std::string error;                 // vacío si ok
    std::vector<std::string> warnings; // ambigüedades (en español, para la interfaz)

    // ---- hechos de la señal (confirmados: se leen, no se estiman)
    double sampleRate = 0;
    double durationSec = 0;
    int sourceChannels = 0;
    double peakDb = -120, rmsDb = -120;
    bool silent = false;               // nada que analizar
    bool percussiveOnly = false;       // sin contenido tonal fiable: no se propone tonalidad ni acordes

    // ---- tempo (estimado)
    double bpm = 0;
    double bpmConfidence = 0;
    std::vector<BpmCandidate> bpmCandidates; // mejor primero, incluye octavas (x2, /2)
    std::vector<double> beatsSec;            // posiciones aproximadas de los pulsos
    double firstBeatSec = 0;
    int beatsPerBar = 4;
    int bars = 0;

    // ---- tonalidad y acordes (estimados)
    bool tonal = false;
    Key key;
    double keyConfidence = 0;
    std::vector<KeyCandidate> keyCandidates; // mejor primero (hasta 4)
    std::vector<ChordEvent> chordEvents;     // acordes por tiempo (aunque no haya pulso fiable)
    HarmonyTimeline harmony;                 // acordes por compás (vacío si !tonal, !detectChords o pulso poco fiable)
    double chordConfidence = 0;              // media por compás
    std::vector<int> harmonicChanges;        // compases donde cambia el acorde

    // ---- energía
    std::vector<EnergyRegion> energyRegions; // por compás, fusionadas
    std::vector<double> barRmsDb;

    std::string summary() const;             // texto corto para la interfaz ("146 BPM (alta), Am (media)...")
};

// Análisis completo. Pura y determinista. No llamar desde el hilo de audio.
BeatAnalysisResult analyseBeat (const MonoAudio& audio, const BeatAnalysisOptions& o = {});
BeatAnalysisResult analyseBeatFile (const std::string& wavPath, const BeatAnalysisOptions& o = {});

// Persistencia (formato de líneas "clave=valor", compatible con StateManager; versiones antiguas ignoran estas claves).
std::string serializeBeatAnalysis (const BeatAnalysisResult& r);
bool deserializeBeatAnalysis (const std::string& text, BeatAnalysisResult& out, std::string& error);
std::string serializeHarmony (const HarmonyTimeline& h, const std::string& prefix = "harm.");
bool deserializeHarmony (const std::string& text, HarmonyTimeline& out, std::string& error, const std::string& prefix = "harm.");

// Beat del proyecto: análisis crudo (estimado) + línea armónica editable (lo que el usuario confirma)
// + sección elegida. Se guarda con el estado; las versiones anteriores ignoran estas claves.
struct BeatProject
{
    std::string path;              // WAV de origen (informativo; no se vuelve a leer al cargar)
    BeatAnalysisResult analysis;   // estimación (no se edita)
    HarmonyTimeline harmony;       // editable: BPM, tonalidad y acordes confirmados o estimados
    int sectionFromBar = 0, sectionToBar = -1; // sección del beat sobre la que se compone (-1 = hasta el final)
    bool loaded() const { return analysis.ok && ! analysis.silent; }
    int sectionBars() const;       // compases de la sección (>= 1 si loaded)
};
std::string serializeBeatProject (const BeatProject& b);
bool deserializeBeatProject (const std::string& text, BeatProject& out, std::string& error);

// ---- beats sintéticos (pruebas, CLI y demos): tonalidad y BPM conocidos.
struct SyntheticBeatSpec
{
    double bpm = 120.0;
    Key key { 9, Mode::Minor };            // Am
    std::string progression = "Am F C G";  // un acorde por compás (texto de Progression::parse); se repite
    int bars = 8;
    int beatsPerBar = 4;
    double sampleRate = 44100.0;
    bool drums = true;
    bool harmony = true;                   // acordes (pad + bajo)
    double noiseDb = -70.0;                // ruido de fondo
    double swingSec = 0.0;                 // desplazamiento de los pulsos pares (humanización)
    uint64_t seed = 1;
    double leadInSec = 0.0;                // silencio inicial
};
MonoAudio synthesiseBeat (const SyntheticBeatSpec& spec);
std::vector<uint8_t> encodeWav16 (const MonoAudio& audio, int channels = 1); // PCM 16 bits (mono duplicado si channels=2)
bool writeWavFile (const std::string& path, const MonoAudio& audio, int channels = 1);

// FFT radix-2 (potencia de 2) in-place, utilidad compartida para análisis offline.
void fftRadix2 (std::vector<float>& re, std::vector<float>& im);
} // namespace solo
