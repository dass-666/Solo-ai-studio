#include "solo/Sequence.h"
#include <algorithm>

namespace solo
{
void Sequence::normalise()
{
    const int64_t total = lengthTicks();
    std::vector<Note> out;
    out.reserve (notes.size());
    for (auto n : notes)
    {
        n.pitch = std::clamp (n.pitch, 0, 127);
        n.velocity = std::clamp (n.velocity, 1, 127);
        n.channel = std::clamp (n.channel, 0, 15);
        if (n.startTick < 0) { n.lengthTicks += n.startTick; n.startTick = 0; }
        if (n.startTick >= total) continue;
        if (n.endTick() > total) n.lengthTicks = total - n.startTick;
        if (n.lengthTicks <= 0) continue;
        out.push_back (n);
    }
    std::stable_sort (out.begin(), out.end(), [] (const Note& a, const Note& b) {
        if (a.startTick != b.startTick) return a.startTick < b.startTick;
        return a.pitch < b.pitch;
    });
    // Misma altura + mismo canal: si se solapan, acortar la anterior; si empiezan a la vez, quedarse con una.
    for (size_t i = 0; i < out.size(); ++i)
    {
        for (size_t j = i + 1; j < out.size(); ++j)
        {
            if (out[j].startTick >= out[i].endTick()) break;
            if (out[j].pitch == out[i].pitch && out[j].channel == out[i].channel)
                out[i].lengthTicks = out[j].startTick - out[i].startTick;
        }
    }
    out.erase (std::remove_if (out.begin(), out.end(), [] (const Note& n) { return n.lengthTicks <= 0; }), out.end());
    notes = std::move (out);
}

std::vector<MidiEvent> buildEventList (const Sequence& seq)
{
    std::vector<MidiEvent> ev;
    ev.reserve (seq.notes.size() * 2);
    for (const auto& n : seq.notes)
    {
        ev.push_back ({ n.startTick, EventType::NoteOn, (uint8_t) n.channel, (uint8_t) n.pitch, (uint8_t) n.velocity });
        ev.push_back ({ n.endTick(), EventType::NoteOff, (uint8_t) n.channel, (uint8_t) n.pitch, 0 });
    }
    std::stable_sort (ev.begin(), ev.end(), [] (const MidiEvent& a, const MidiEvent& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return (int) a.type < (int) b.type;
    });
    return ev;
}

void VariationHistory::push (const Sequence& s, uint64_t seed)
{
    // Se añade al final (no se descartan las variaciones posteriores a la actual).
    items.push_back ({ s, seed });
    if ((int) items.size() > kMaxVariations) items.erase (items.begin());
    index = (int) items.size() - 1;
}

void VariationHistory::restore (std::vector<Item> newItems, int newIndex)
{
    items = std::move (newItems);
    if ((int) items.size() > kMaxVariations) items.resize ((size_t) kMaxVariations);
    index = items.empty() ? -1 : std::clamp (newIndex, 0, (int) items.size() - 1);
}

} // namespace solo
