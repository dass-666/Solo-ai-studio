#include "solo/AudioRenderer.h"
#include "solo/SoloEngine.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace solo
{
RenderResult renderSequence (const Sequence& seq, const RenderOptions& opts)
{
    // Mismo motor que el plugin (SoloEngine), sin efectos: solo la etapa de salida.
    EngineRenderOptions eo;
    eo.sampleRate = opts.sampleRate;
    eo.blockSize = opts.blockSize;
    eo.instrument = opts.instrument;
    eo.gain = opts.gain;
    eo.extraTailSeconds = opts.tailSeconds;
    const auto er = renderWithEngine (seq, eo);
    RenderResult r;
    r.left = std::move (er.left);
    r.right = std::move (er.right);
    r.noteOnsRendered = er.noteOns;
    r.noteOffsRendered = er.noteOffs;
    r.peak = er.peak;
    r.activeVoicesAtEnd = er.activeVoicesAtEnd;
    return r;
}

std::vector<uint8_t> encodeWav24 (const std::vector<float>& left, const std::vector<float>& right, int sampleRate)
{
    const uint32_t frames = (uint32_t) std::min (left.size(), right.size());
    const uint32_t dataBytes = frames * 2 * 3;
    std::vector<uint8_t> b;
    b.reserve (44 + dataBytes);
    auto s = [&] (const char* t) { b.insert (b.end(), t, t + 4); };
    auto u32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) (v >> (8 * i))); };
    auto u16 = [&] (uint16_t v) { b.push_back ((uint8_t) v); b.push_back ((uint8_t) (v >> 8)); };
    s ("RIFF"); u32 (36 + dataBytes); s ("WAVE");
    s ("fmt "); u32 (16); u16 (1); u16 (2); u32 ((uint32_t) sampleRate);
    u32 ((uint32_t) sampleRate * 6); u16 (6); u16 (24);
    s ("data"); u32 (dataBytes);
    for (uint32_t i = 0; i < frames; ++i)
        for (float x : { left[i], right[i] })
        {
            const float c = std::clamp (x, -1.0f, 1.0f);
            const int32_t v = (int32_t) std::lround (c * 8388607.0f);
            b.push_back ((uint8_t) v);
            b.push_back ((uint8_t) (v >> 8));
            b.push_back ((uint8_t) (v >> 16));
        }
    return b;
}

bool writeWav24 (const std::string& path, const std::vector<float>& left, const std::vector<float>& right,
                 int sampleRate, std::string& error)
{
    const auto bytes = encodeWav24 (left, right, sampleRate);
    std::ofstream f (path, std::ios::binary | std::ios::trunc);
    if (! f) { error = "No se puede escribir: " + path; return false; }
    f.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
    if (! f) { error = "Error de escritura: " + path; return false; }
    return true;
}

} // namespace solo
