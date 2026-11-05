#include "solo/MidiFile.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>

namespace solo
{
namespace
{
void put16 (std::vector<uint8_t>& v, uint32_t x) { v.push_back ((uint8_t) (x >> 8)); v.push_back ((uint8_t) x); }
void put32 (std::vector<uint8_t>& v, uint32_t x)
{
    for (int s = 24; s >= 0; s -= 8) v.push_back ((uint8_t) (x >> s));
}
void putVarLen (std::vector<uint8_t>& v, uint32_t x)
{
    uint8_t buf[5];
    int n = 0;
    buf[n++] = (uint8_t) (x & 0x7F);
    while ((x >>= 7) != 0) buf[n++] = (uint8_t) ((x & 0x7F) | 0x80);
    while (n > 0) v.push_back (buf[--n]);
}

struct RawEvent
{
    int64_t tick;
    int order; // meta antes que notas; note-off antes que note-on
    std::vector<uint8_t> bytes;
};

void appendTrack (std::vector<uint8_t>& file, std::vector<RawEvent> events, int64_t endTick)
{
    std::stable_sort (events.begin(), events.end(), [] (const RawEvent& a, const RawEvent& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return a.order < b.order;
    });
    std::vector<uint8_t> data;
    int64_t last = 0;
    for (const auto& e : events)
    {
        putVarLen (data, (uint32_t) (e.tick - last));
        data.insert (data.end(), e.bytes.begin(), e.bytes.end());
        last = e.tick;
    }
    putVarLen (data, (uint32_t) std::max<int64_t> (0, endTick - last));
    data.insert (data.end(), { 0xFF, 0x2F, 0x00 }); // End of Track
    file.insert (file.end(), { 'M', 'T', 'r', 'k' });
    put32 (file, (uint32_t) data.size());
    file.insert (file.end(), data.begin(), data.end());
}

RawEvent meta (int64_t tick, uint8_t type, const std::vector<uint8_t>& payload)
{
    RawEvent e { tick, 0, { 0xFF, type } };
    putVarLen (e.bytes, (uint32_t) payload.size());
    e.bytes.insert (e.bytes.end(), payload.begin(), payload.end());
    return e;
}

std::vector<RawEvent> conductorEvents (const Sequence& seq, const MidiExportOptions& opts)
{
    std::vector<RawEvent> ev;
    const uint32_t uspq = (uint32_t) std::lround (60000000.0 / std::clamp (seq.bpm, 20.0, 999.0));
    ev.push_back (meta (0, 0x51, { (uint8_t) (uspq >> 16), (uint8_t) (uspq >> 8), (uint8_t) uspq }));
    int denPow = 0;
    for (int d = seq.timeSignature.denominator; d > 1; d >>= 1) ++denPow;
    ev.push_back (meta (0, 0x58, { (uint8_t) seq.timeSignature.numerator, (uint8_t) denPow, 24, 8 }));
    if (opts.writeKeySignature)
    {
        // Número de alteraciones de la armadura (negativo = bemoles).
        static const int majorSf[12] = { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };
        const int relMajor = opts.key.mode == Mode::Major ? opts.key.tonic : (opts.key.tonic + 3) % 12;
        const int sf = majorSf[relMajor];
        ev.push_back (meta (0, 0x59, { (uint8_t) (int8_t) sf, (uint8_t) (opts.key.mode == Mode::Minor ? 1 : 0) }));
    }
    return ev;
}

std::vector<RawEvent> noteEvents (const Sequence& seq, const MidiExportOptions& opts)
{
    std::vector<RawEvent> ev;
    std::vector<uint8_t> name (opts.trackName.begin(), opts.trackName.end());
    ev.push_back (meta (0, 0x03, name));
    if (opts.gmProgram >= 0 && opts.gmProgram < 128)
        ev.push_back ({ 0, 1, { 0xC0, (uint8_t) opts.gmProgram } });
    for (const auto& e : buildEventList (seq))
    {
        if (e.type == EventType::NoteOn)
            ev.push_back ({ e.tick, 3, { (uint8_t) (0x90 | e.channel), e.pitch, e.velocity } });
        else // Note-off explícito (0x80) con velocity 64: máxima compatibilidad
            ev.push_back ({ e.tick, 2, { (uint8_t) (0x80 | e.channel), e.pitch, 64 } });
    }
    return ev;
}
} // namespace

std::vector<uint8_t> writeMidiFile (const Sequence& seqIn, const MidiExportOptions& opts)
{
    Sequence seq = seqIn;
    seq.normalise();
    std::vector<uint8_t> file { 'M', 'T', 'h', 'd' };
    put32 (file, 6);
    const int format = opts.format == 0 ? 0 : 1;
    put16 (file, (uint32_t) format);
    put16 (file, format == 0 ? 1 : 2);
    put16 (file, (uint32_t) kPPQ);
    const int64_t end = seq.lengthTicks();
    if (format == 0)
    {
        auto all = conductorEvents (seq, opts);
        auto notes = noteEvents (seq, opts);
        all.insert (all.end(), notes.begin(), notes.end());
        appendTrack (file, all, end);
    }
    else
    {
        auto cond = conductorEvents (seq, opts);
        cond.insert (cond.begin(), meta (0, 0x03, { 'T', 'e', 'm', 'p', 'o' }));
        appendTrack (file, cond, end);
        appendTrack (file, noteEvents (seq, opts), end);
    }
    return file;
}

bool saveMidiFile (const std::string& path, const Sequence& seq, const MidiExportOptions& opts, std::string& error)
{
    const auto bytes = writeMidiFile (seq, opts);
    std::ofstream f (path, std::ios::binary | std::ios::trunc);
    if (! f) { error = "No se puede escribir: " + path; return false; }
    f.write ((const char*) bytes.data(), (std::streamsize) bytes.size());
    if (! f) { error = "Error de escritura: " + path; return false; }
    return true;
}

// ------------------------------------------------------------------ lector
bool readMidiFile (const std::vector<uint8_t>& d, ParsedMidi& out, std::string& error)
{
    out = {};
    size_t p = 0;
    auto need = [&] (size_t n) { return p + n <= d.size(); };
    auto rd32 = [&] { uint32_t v = ((uint32_t) d[p] << 24) | ((uint32_t) d[p + 1] << 16) | ((uint32_t) d[p + 2] << 8) | d[p + 3]; p += 4; return v; };
    auto rd16 = [&] { uint32_t v = ((uint32_t) d[p] << 8) | d[p + 1]; p += 2; return v; };

    if (! need (14) || d[0] != 'M' || d[1] != 'T' || d[2] != 'h' || d[3] != 'd') { error = "Cabecera MThd ausente"; return false; }
    p = 4;
    if (rd32() != 6) { error = "Longitud MThd inválida"; return false; }
    out.format = (int) rd16();
    out.numTracks = (int) rd16();
    out.ppq = (int) rd16();
    if (out.ppq & 0x8000) { error = "Formato SMPTE no soportado"; return false; }

    for (int t = 0; t < out.numTracks; ++t)
    {
        if (! need (8) || d[p] != 'M' || d[p + 1] != 'T' || d[p + 2] != 'r' || d[p + 3] != 'k') { error = "Pista MTrk ausente"; return false; }
        p += 4;
        const uint32_t len = rd32();
        if (! need (len)) { error = "Pista truncada"; return false; }
        const size_t end = p + len;
        int64_t tick = 0;
        uint8_t running = 0;
        std::map<int, std::vector<std::pair<int64_t, int>>> open; // (canal*128+nota) -> pila (tick, vel)
        bool sawEnd = false;
        while (p < end)
        {
            uint32_t delta = 0;
            for (int i = 0; i < 4; ++i)
            {
                if (p >= end) { error = "VLQ truncado"; return false; }
                const uint8_t b = d[p++];
                delta = (delta << 7) | (b & 0x7F);
                if (! (b & 0x80)) break;
            }
            tick += delta;
            if (p >= end) { error = "Evento truncado"; return false; }
            uint8_t status = d[p];
            if (status & 0x80) ++p; else status = running;
            if (status == 0xFF)
            {
                if (p + 1 > end) { error = "Meta truncado"; return false; }
                const uint8_t type = d[p++];
                uint32_t l = 0;
                for (int i = 0; i < 4; ++i) { const uint8_t b = d[p++]; l = (l << 7) | (b & 0x7F); if (! (b & 0x80)) break; }
                if (p + l > end) { error = "Meta truncado"; return false; }
                if (type == 0x51 && l == 3) out.bpm = 60000000.0 / (double) ((d[p] << 16) | (d[p + 1] << 8) | d[p + 2]);
                if (type == 0x58 && l == 4) { out.tsNumerator = d[p]; out.tsDenominator = 1 << d[p + 1]; }
                if (type == 0x03 && out.trackName.empty()) out.trackName.assign ((const char*) &d[p], l);
                if (type == 0x2F) sawEnd = true;
                p += l;
                continue;
            }
            if (status == 0xF0 || status == 0xF7)
            {
                uint32_t l = 0;
                for (int i = 0; i < 4; ++i) { const uint8_t b = d[p++]; l = (l << 7) | (b & 0x7F); if (! (b & 0x80)) break; }
                p += l;
                continue;
            }
            if (! (status & 0x80)) { error = "Running status sin estado previo"; return false; }
            running = status;
            const int hi = status & 0xF0, ch = status & 0x0F;
            const int nData = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
            if (p + (size_t) nData > end) { error = "Datos truncados"; return false; }
            const int a = d[p], b = nData > 1 ? d[p + 1] : 0;
            p += (size_t) nData;
            if (hi == 0xC0) out.program = a;
            if (hi == 0x90 && b > 0) open[ch * 128 + a].push_back ({ tick, b });
            else if (hi == 0x80 || (hi == 0x90 && b == 0))
            {
                auto& st = open[ch * 128 + a];
                if (st.empty()) { ++out.unmatchedNoteOffs; continue; }
                auto [start, vel] = st.front();
                st.erase (st.begin());
                out.notes.push_back ({ start, tick - start, a, vel, ch });
            }
        }
        if (! sawEnd) { error = "Falta End of Track"; return false; }
        for (auto& [k, st] : open) out.danglingNoteOns += (int) st.size();
    }
    std::stable_sort (out.notes.begin(), out.notes.end(), [] (const Note& x, const Note& y) {
        return x.startTick != y.startTick ? x.startTick < y.startTick : x.pitch < y.pitch;
    });
    return true;
}

} // namespace solo
