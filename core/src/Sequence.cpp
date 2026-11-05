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
        ev.push_back ({ n.startTick, EventType::NoteOn, (uint8_t) n.channel, (uint8_t) n.pitch, (uint8_t) n.velocity, n.lengthTicks, n.expr });
        ev.push_back ({ n.endTick(), EventType::NoteOff, (uint8_t) n.channel, (uint8_t) n.pitch, 0, 0, {} });
    }
    std::stable_sort (ev.begin(), ev.end(), [] (const MidiEvent& a, const MidiEvent& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return (int) a.type < (int) b.type;
    });
    return ev;
}

bool VariationHistory::push (const Sequence& s, uint64_t seed, const std::string& name)
{
    // Se añade al final (no se descartan las variaciones posteriores a la actual).
    if ((int) items.size() >= kMaxVariations)
    {
        // Descartar la no favorita más antigua (nunca la actual).
        for (size_t i = 0; i < items.size() && (int) items.size() >= kMaxVariations; ++i)
        {
            if (items[i].favorite || (int) i == index) continue;
            items.erase (items.begin() + (std::ptrdiff_t) i);
            if ((int) i < index) --index;
            break;
        }
        if ((int) items.size() >= kMaxTotal) return false;
    }
    Item it;
    it.sequence = s;
    it.seed = seed;
    it.name = name;
    it.id = nextId++;
    items.push_back (std::move (it));
    index = (int) items.size() - 1;
    return true;
}

int VariationHistory::indexOfId (uint32_t id) const
{
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].id == id) return (int) i;
    return -1;
}

bool VariationHistory::remove (int i)
{
    if (i < 0 || i >= size()) return false;
    items.erase (items.begin() + i);
    if (items.empty()) index = -1;
    else if (i < index || index >= size()) index = std::max (0, index - 1);
    return true;
}

int VariationHistory::favoriteCount() const
{
    int n = 0;
    for (const auto& it : items) n += it.favorite ? 1 : 0;
    return n;
}

void VariationHistory::restore (std::vector<Item> newItems, int newIndex, uint32_t nextIdValue)
{
    items = std::move (newItems);
    if ((int) items.size() > kMaxTotal) items.resize ((size_t) kMaxTotal);
    uint32_t maxId = 0;
    for (const auto& it : items) maxId = std::max (maxId, it.id);
    for (auto& it : items)
        if (it.id == 0) it.id = ++maxId; // estados v1: sin id
    nextId = std::max (nextIdValue, maxId + 1);
    index = items.empty() ? -1 : std::clamp (newIndex, 0, (int) items.size() - 1);
}

bool VariationHistory::operator== (const VariationHistory& o) const
{
    if (index != o.index || items.size() != o.items.size()) return false;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const auto& a = items[i];
        const auto& b = o.items[i];
        if (! (a.sequence == b.sequence) || a.seed != b.seed || a.favorite != b.favorite || a.name != b.name || a.id != b.id)
            return false;
    }
    return true;
}

} // namespace solo
