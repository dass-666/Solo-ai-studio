#pragma once
// MusicTheory: escalas, acordes, progresiones y registros instrumentales.
#include "solo/Types.h"
#include <array>
#include <string>
#include <vector>

namespace solo
{
enum class Mode
{
    Major,
    Minor // menor natural (eólico)
};

enum class ChordQuality
{
    Major,
    Minor,
    Diminished,
    Augmented,
    Dominant7,
    Major7,
    Minor7,
    HalfDiminished7,
    Sus2,
    Sus4,
    Power
};

// Devuelve la clase de altura (0 = C ... 11 = B) o -1 si el texto no es válido.
// Acepta "C", "c#", "Db", "Bb", "E#" ... y también nombres en español: Do, Re, Mi, Fa, Sol, La, Si.
int parsePitchClass (const std::string& text, size_t* consumed = nullptr);
std::string pitchClassName (int pc, bool preferFlats = false);
std::string midiNoteName (int midiNote); // "A4" = 69

struct Key
{
    int tonic = 9; // A
    Mode mode = Mode::Minor;

    // Escala diatónica como clases de altura (7 valores) ascendente desde la tónica.
    std::array<int, 7> scalePitchClasses() const;
    bool containsPitchClass (int pc) const;
    std::string toString() const;
    bool prefersFlats() const;

    // "Am", "A minor", "La menor", "C", "C major", "Do mayor", "F#m" ...
    static bool parse (const std::string& text, Key& out);
    bool operator== (const Key& o) const { return tonic == o.tonic && mode == o.mode; }
};

struct Chord
{
    int root = 0;
    ChordQuality quality = ChordQuality::Major;

    // Intervalos (semitonos desde la fundamental) de las notas del acorde.
    std::vector<int> intervals() const;
    std::vector<int> pitchClasses() const;
    bool containsPitchClass (int pc) const;
    // Notas de la escala que no son del acorde y no forman semitono contra una nota
    // del acorde (evita "avoid notes"): 9as, 11as, 13as utilizables.
    std::vector<int> tensionPitchClasses (const Key& key) const;
    // Fundamental, 3ª y 5ª (notas de reposo para resoluciones).
    std::vector<int> stablePitchClasses() const;
    bool isMinorish() const;
    std::string toString (bool preferFlats = false) const;

    static bool parse (const std::string& text, Chord& out);
    bool operator== (const Chord& o) const { return root == o.root && quality == o.quality; }
};

// Progresión: un acorde por compás. Admite "Am F C G", "Am | F | C | G",
// "%" o "." para repetir el compás anterior. Si la progresión es más corta que
// el solo, se repite cíclicamente.
struct Progression
{
    std::vector<Chord> bars;

    const Chord& chordAtBar (int bar) const;
    std::string toString (bool preferFlats = false) const;

    // Devuelve false y rellena 'error' si hay símbolos no válidos.
    static bool parse (const std::string& text, Progression& out, std::string& error);
    // Advertencias no bloqueantes (acordes fuera de la tonalidad, etc.).
    std::vector<std::string> validate (const Key& key) const;
};

struct InstrumentRange
{
    int lowest = 48;
    int highest = 96;
    int comfortLow = 55;
    int comfortHigh = 88;
};

// Altura MIDI más cercana a 'target' cuya clase de altura esté en 'pcs' dentro de [lo, hi].
int nearestPitchWithClass (int target, const std::vector<int>& pcs, int lo, int hi);

} // namespace solo
