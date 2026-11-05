#pragma once
// MidiEngine (parte 1): secuencia MIDI canónica, independiente del sintetizador.
#include "solo/Types.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace solo
{
struct Note
{
    int64_t startTick = 0;   // posición desde el inicio del solo (PPQ = kPPQ)
    int64_t lengthTicks = 0; // duración (> 0)
    int pitch = 60;          // 0..127
    int velocity = 100;      // 1..127
    int channel = 0;         // 0..15

    int64_t endTick() const { return startTick + lengthTicks; }
    bool operator== (const Note& o) const
    {
        return startTick == o.startTick && lengthTicks == o.lengthTicks && pitch == o.pitch
            && velocity == o.velocity && channel == o.channel;
    }
};

struct Sequence
{
    std::vector<Note> notes;
    TimeSignature timeSignature;
    int bars = 8;
    double bpm = 120.0; // tempo de referencia para exportación

    int64_t lengthTicks() const { return (int64_t) bars * timeSignature.ticksPerBar(); }

    // Ordena, recorta al rango válido, corrige valores fuera de rango y elimina
    // solapamientos de la misma altura (que provocarían notas colgadas en algunos hosts).
    void normalise();
    bool operator== (const Sequence& o) const
    {
        return notes == o.notes && timeSignature == o.timeSignature && bars == o.bars && std::abs (bpm - o.bpm) < 1e-9;
    }
};

enum class EventType : uint8_t
{
    NoteOff = 0, // se ordena antes que NoteOn en el mismo tick
    NoteOn = 1
};

struct MidiEvent
{
    int64_t tick = 0;
    EventType type = EventType::NoteOn;
    uint8_t channel = 0;
    uint8_t pitch = 60;
    uint8_t velocity = 100;
};

// Lista de eventos ordenada (note-off antes que note-on en el mismo tick).
std::vector<MidiEvent> buildEventList (const Sequence& seq);

// Historial de variaciones generadas: permite ir a la anterior/siguiente sin perderlas.
class VariationHistory
{
public:
    static constexpr int kMaxVariations = 32;

    void push (const Sequence& s, uint64_t seed);
    bool canGoBack() const { return index > 0; }
    bool canGoForward() const { return index + 1 < (int) items.size(); }
    void back() { if (canGoBack()) --index; }
    void forward() { if (canGoForward()) ++index; }
    bool empty() const { return items.empty(); }
    int size() const { return (int) items.size(); }
    int currentIndex() const { return index; }
    void setCurrentIndex (int i) { if (i >= 0 && i < size()) index = i; }
    Sequence& current() { return items[(size_t) index].sequence; }
    const Sequence& current() const { return items[(size_t) index].sequence; }
    uint64_t currentSeed() const { return items[(size_t) index].seed; }
    void clear() { items.clear(); index = -1; }

    struct Item { Sequence sequence; uint64_t seed = 0; };
    const std::vector<Item>& all() const { return items; }
    void restore (std::vector<Item> newItems, int newIndex);

private:
    std::vector<Item> items;
    int index = -1;
};

} // namespace solo
