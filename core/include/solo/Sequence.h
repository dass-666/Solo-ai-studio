#pragma once
// MidiEngine (parte 1): secuencia MIDI canónica, independiente del sintetizador.
#include "solo/Types.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace solo
{
// Expresión/articulación de una nota. La nota MIDI (pitch) es la altura "pisada";
// los bends y slides se expresan respecto a ella (como en una guitarra real).
enum class BendType : uint8_t
{
    None = 0,
    Up = 1,       // empieza en pitch y sube 'bendSemitones'
    Release = 2,  // pre-bend: empieza en pitch+bend y baja a pitch
    PreBend = 3,  // suena en pitch+bend todo el tiempo (subido antes de pulsar)
    UpRelease = 4 // sube y vuelve a bajar
};

struct NoteExpression
{
    BendType bendType = BendType::None;
    float bendSemitones = 0.0f; // 0.5 .. 3
    float bendStart = 0.0f;     // fracción de la nota donde empieza el movimiento
    float bendTime = 0.3f;      // fracción de la nota que dura el movimiento
    float vibratoDepth = 0.0f;  // semitonos (pico)
    float vibratoRate = 5.5f;   // Hz
    float vibratoDelay = 0.3f;  // fracción de la nota antes de que entre el vibrato
    int8_t slideFrom = 0;       // semitonos relativos desde los que se desliza al entrar (0 = no)
    bool legato = false;        // hammer-on / pull-off: sin nuevo ataque de púa
    bool palmMute = false;
    uint8_t attack = 64;        // 0 = suave ... 127 = agresivo

    bool isPlain() const
    {
        return bendType == BendType::None && vibratoDepth <= 0.0f && slideFrom == 0 && ! legato && ! palmMute && attack == 64;
    }
    bool operator== (const NoteExpression& o) const
    {
        return bendType == o.bendType && std::abs (bendSemitones - o.bendSemitones) < 1e-4f
            && std::abs (bendStart - o.bendStart) < 1e-4f && std::abs (bendTime - o.bendTime) < 1e-4f
            && std::abs (vibratoDepth - o.vibratoDepth) < 1e-4f && std::abs (vibratoRate - o.vibratoRate) < 1e-4f
            && std::abs (vibratoDelay - o.vibratoDelay) < 1e-4f && slideFrom == o.slideFrom && legato == o.legato
            && palmMute == o.palmMute && attack == o.attack;
    }
};

// Función musical que el compositor asigna a la nota (para el intérprete, el análisis y las pruebas).
enum class NoteRole : uint8_t
{
    Free = 0,
    Target = 1,       // nota estructural (nota del acorde en tiempo fuerte)
    Anticipation = 2, // anticipa el acorde del compás siguiente
    Passing = 3,
    Climax = 4,
    Cadence = 5,
    Run = 6           // parte de una escala/arpegio rápido
};

struct Note
{
    int64_t startTick = 0;   // posición desde el inicio del solo (PPQ = kPPQ)
    int64_t lengthTicks = 0; // duración (> 0)
    int pitch = 60;          // 0..127
    int velocity = 100;      // 1..127
    int channel = 0;         // 0..15
    NoteExpression expr;
    NoteRole role = NoteRole::Free;
    bool locked = false;     // protegida frente a regeneraciones

    int64_t endTick() const { return startTick + lengthTicks; }
    // Altura en la que termina la nota (tras bends): la que debe encajar con la armonía.
    int targetPitch() const
    {
        const int b = (int) std::lround (expr.bendSemitones);
        switch (expr.bendType)
        {
            case BendType::Up:
            case BendType::PreBend: return pitch + b;
            default: return pitch;
        }
    }
    bool operator== (const Note& o) const
    {
        return startTick == o.startTick && lengthTicks == o.lengthTicks && pitch == o.pitch
            && velocity == o.velocity && channel == o.channel && expr == o.expr && role == o.role && locked == o.locked;
    }
    bool sameMusic (const Note& o) const // ignora rol/bloqueo
    {
        return startTick == o.startTick && lengthTicks == o.lengthTicks && pitch == o.pitch && velocity == o.velocity
            && channel == o.channel && expr == o.expr;
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
    int64_t lengthTicks = 0; // solo note-on: duración (para articulaciones)
    NoteExpression expr;     // solo note-on
};

// Lista de eventos ordenada (note-off antes que note-on en el mismo tick).
std::vector<MidiEvent> buildEventList (const Sequence& seq);

// Historial de variaciones generadas: permite ir a la anterior/siguiente sin perderlas.
// Las favoritas NUNCA se eliminan automáticamente: al superar kMaxVariations se descarta la
// variación no favorita más antigua (que no sea la actual). Si todas son favoritas, el historial
// crece hasta kMaxTotal y a partir de ahí push() devuelve false (hay que quitar favoritas a mano).
class VariationHistory
{
public:
    static constexpr int kMaxVariations = 32;
    static constexpr int kMaxTotal = 64;

    struct Item
    {
        Sequence sequence;
        uint64_t seed = 0;
        bool favorite = false;
        std::string name;
        uint32_t id = 0; // identificador estable (A/B, referencias de la interfaz)
    };

    bool push (const Sequence& s, uint64_t seed, const std::string& name = {});
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
    Item& currentItem() { return items[(size_t) index]; }
    const Item& currentItem() const { return items[(size_t) index]; }
    void clear() { items.clear(); index = -1; }

    void setFavorite (int i, bool fav) { if (i >= 0 && i < size()) items[(size_t) i].favorite = fav; }
    void rename (int i, const std::string& n) { if (i >= 0 && i < size()) items[(size_t) i].name = n; }
    int indexOfId (uint32_t id) const;
    // Elimina una variación a petición del usuario (las favoritas también, porque es explícito).
    bool remove (int i);
    int favoriteCount() const;

    const std::vector<Item>& all() const { return items; }
    uint32_t nextIdValue() const { return nextId; }
    void restore (std::vector<Item> newItems, int newIndex, uint32_t nextIdValue = 0);
    bool operator== (const VariationHistory& o) const;

private:
    std::vector<Item> items;
    int index = -1;
    uint32_t nextId = 1;
};

} // namespace solo
