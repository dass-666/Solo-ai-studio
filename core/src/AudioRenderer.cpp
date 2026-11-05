#include "solo/AudioRenderer.h"
#include <algorithm>
#include <cmath>
#include <fstream>

namespace solo
{
RenderResult renderSequence (const Sequence& seq, const RenderOptions& opts)
{
    RenderResult r;
    const double sr = opts.sampleRate;
    const int block = std::max (16, opts.blockSize);
    const double bpm = seq.bpm > 0 ? seq.bpm : 120.0;
    const double seconds = (double) seq.lengthTicks() / kPPQ * 60.0 / bpm + opts.tailSeconds;
    const int64_t total = (int64_t) std::ceil (seconds * sr);

    InstrumentEngine engine;
    engine.setInstrument (opts.instrument);
    engine.setOutputGain (opts.gain);
    engine.prepare (sr);

    const auto data = makePlaybackData (seq, 0, false);
    SoloPlayer player;
    ManualClock clock;
    clock.start();

    r.left.assign ((size_t) total, 0.0f);
    r.right.assign ((size_t) total, 0.0f);
    std::vector<TimedEvent> events (4096);

    for (int64_t pos = 0; pos < total; pos += block)
    {
        const int n = (int) std::min<int64_t> (block, total - pos);
        const auto t = clock.next (bpm, n, sr);
        const int count = player.process (t, sr, n, data.get(), events.data(), (int) events.size());
        for (int i = 0; i < count; ++i)
            (events[(size_t) i].type == EventType::NoteOn ? r.noteOnsRendered : r.noteOffsRendered)++;
        engine.process (r.left.data() + pos, r.right.data() + pos, n, events.data(), count);
    }
    for (size_t i = 0; i < r.left.size(); ++i)
        r.peak = std::max ({ r.peak, std::abs (r.left[i]), std::abs (r.right[i]) });
    r.activeVoicesAtEnd = engine.activeVoiceCount();
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
