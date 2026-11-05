#pragma once
// StateManager: persistencia versionada de parámetros + secuencias + variaciones.
// Formato de texto legible, validado al cargar. Un estado inválido nunca se aplica a medias.
#include "solo/SoloComposer.h"
#include <string>
#include <vector>

namespace solo
{
struct SoloSettings
{
    std::string keyText = "Am";
    std::string progressionText = "Am F C G";
    int tsNumerator = 4;
    int bars = 8;
    InstrumentId instrument = InstrumentId::Guitar;
    Style style = Style::Rock;
    double energy = 0.6, density = 0.5, complexity = 0.4;
    uint64_t seed = 1;
    double manualBpm = 120.0;
    bool useHostTempo = true;
    bool syncToHost = true; // true: suena con el play del DAW; false: solo con "Escuchar"
    int startBar = 1;       // compás del DAW donde empieza el solo (base 1)
    bool loop = true;
    double outputGain = 1.0;
    bool proMode = false;
    int wavSampleRate = 48000;
    int midiFormat = 1;

    // Construye los parámetros del compositor; devuelve false con el error si tonalidad/progresión no son válidas.
    bool toComposerParams (double bpm, ComposerParams& out, std::string& error) const;
};

struct SoloState
{
    static constexpr int kVersion = 1;
    SoloSettings settings;
    VariationHistory history;
};

std::string serializeState (const SoloState& s);
bool deserializeState (const std::string& text, SoloState& out, std::string& error);

// Presets: solo ajustes (sin secuencias).
std::string serializePreset (const SoloSettings& s, const std::string& name);
bool deserializePreset (const std::string& text, SoloSettings& out, std::string& name, std::string& error);

struct FactoryPreset { const char* name; SoloSettings settings; };
std::vector<FactoryPreset> factoryPresets();

} // namespace solo
