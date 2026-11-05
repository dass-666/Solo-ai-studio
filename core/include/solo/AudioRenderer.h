#pragma once
// AudioRenderer: render offline de la secuencia con el MISMO camino de código que la
// reproducción en tiempo real (SoloPlayer + InstrumentEngine), para garantizar que
// el WAV exportado coincide con lo que se escucha y con el MIDI exportado.
#include "solo/Playback.h"
#include <string>
#include <vector>

namespace solo
{
struct RenderOptions
{
    double sampleRate = 48000.0;
    int blockSize = 512;
    double tailSeconds = 1.5; // cola tras la última nota (release)
    InstrumentId instrument = InstrumentId::Guitar;
    float gain = 1.0f;
};

struct RenderResult
{
    std::vector<float> left, right;
    int noteOnsRendered = 0;
    int noteOffsRendered = 0;
    float peak = 0.0f;
    int activeVoicesAtEnd = 0;
};

RenderResult renderSequence (const Sequence& seq, const RenderOptions& opts);

// WAV PCM 24 bits estéreo.
bool writeWav24 (const std::string& path, const std::vector<float>& left, const std::vector<float>& right,
                 int sampleRate, std::string& error);
std::vector<uint8_t> encodeWav24 (const std::vector<float>& left, const std::vector<float>& right, int sampleRate);

} // namespace solo
