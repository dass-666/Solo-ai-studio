#include "solo/MusicTheory.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace solo
{
namespace
{
std::string lower (std::string s)
{
    for (auto& c : s) c = (char) std::tolower ((unsigned char) c);
    return s;
}

std::string trim (const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace ((unsigned char) s[a])) ++a;
    while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
    return s.substr (a, b - a);
}

int mod12 (int v) { return ((v % 12) + 12) % 12; }

int parseAccidentals (const std::string& text, size_t& pos)
{
    int delta = 0;
    while (pos < text.size())
    {
        const char c = text[pos];
        if (c == '#') { ++delta; ++pos; continue; }
        if (c == 'b' && pos > 0) { --delta; ++pos; continue; }
        // "♯" y "♭" en UTF-8
        if (text.compare (pos, 3, "\xE2\x99\xAF") == 0) { ++delta; pos += 3; continue; }
        if (text.compare (pos, 3, "\xE2\x99\xAD") == 0) { --delta; pos += 3; continue; }
        break;
    }
    return delta;
}

int parsePitchClassImpl (const std::string& text, size_t* consumed, bool trySpanish)
{
    if (text.empty()) return -1;
    size_t pos = 0;
    int base = -1;

    if (trySpanish)
    {
        static const struct { const char* name; int pc; } spanish[] = {
            { "sol", 7 }, { "do", 0 }, { "re", 2 }, { "mi", 4 }, { "fa", 5 }, { "la", 9 }, { "si", 11 }
        };
        const auto l = lower (text);
        for (const auto& s : spanish)
        {
            const size_t n = std::char_traits<char>::length (s.name);
            if (l.compare (0, n, s.name) == 0) { base = s.pc; pos = n; break; }
        }
    }

    if (base < 0)
    {
        switch (std::toupper ((unsigned char) text[0]))
        {
            case 'C': base = 0; break;
            case 'D': base = 2; break;
            case 'E': base = 4; break;
            case 'F': base = 5; break;
            case 'G': base = 7; break;
            case 'A': base = 9; break;
            case 'B': base = 11; break;
            default: return -1;
        }
        pos = 1;
    }

    const int delta = parseAccidentals (text, pos);
    if (consumed) *consumed = pos;
    return mod12 (base + delta);
}

bool parseQuality (const std::string& suffixIn, ChordQuality& q)
{
    std::string suffix = suffixIn;
    // Ignora el bajo de acordes con barra: "C/E" -> "C"
    if (auto slash = suffix.find ('/'); slash != std::string::npos) suffix = suffix.substr (0, slash);

    struct Entry { const char* text; ChordQuality q; };
    // Comparación sensible a mayúsculas para distinguir "M7" de "m7".
    static const Entry exact[] = {
        { "", ChordQuality::Major }, { "M", ChordQuality::Major }, { "maj", ChordQuality::Major },
        { "6", ChordQuality::Major }, { "add9", ChordQuality::Major }, { "69", ChordQuality::Major },
        { "m", ChordQuality::Minor }, { "min", ChordQuality::Minor }, { "-", ChordQuality::Minor },
        { "m6", ChordQuality::Minor }, { "madd9", ChordQuality::Minor },
        { "dim", ChordQuality::Diminished }, { "o", ChordQuality::Diminished }, { "dim7", ChordQuality::Diminished },
        { "\xC2\xB0", ChordQuality::Diminished },
        { "aug", ChordQuality::Augmented }, { "+", ChordQuality::Augmented },
        { "7", ChordQuality::Dominant7 }, { "9", ChordQuality::Dominant7 }, { "11", ChordQuality::Dominant7 },
        { "13", ChordQuality::Dominant7 }, { "7sus4", ChordQuality::Dominant7 },
        { "maj7", ChordQuality::Major7 }, { "M7", ChordQuality::Major7 }, { "maj9", ChordQuality::Major7 },
        { "\xCE\x94", ChordQuality::Major7 }, { "\xCE\x94" "7", ChordQuality::Major7 },
        { "m7", ChordQuality::Minor7 }, { "min7", ChordQuality::Minor7 }, { "-7", ChordQuality::Minor7 },
        { "m9", ChordQuality::Minor7 }, { "m11", ChordQuality::Minor7 },
        { "m7b5", ChordQuality::HalfDiminished7 }, { "\xC3\xB8", ChordQuality::HalfDiminished7 },
        { "sus2", ChordQuality::Sus2 }, { "sus4", ChordQuality::Sus4 }, { "sus", ChordQuality::Sus4 },
        { "5", ChordQuality::Power },
    };
    for (const auto& e : exact)
        if (suffix == e.text) { q = e.q; return true; }
    return false;
}
} // namespace

int parsePitchClass (const std::string& text, size_t* consumed)
{
    return parsePitchClassImpl (text, consumed, true);
}

std::string pitchClassName (int pc, bool preferFlats)
{
    static const char* sharps[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const char* flats[] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
    return (preferFlats ? flats : sharps)[mod12 (pc)];
}

std::string midiNoteName (int n)
{
    return pitchClassName (n) + std::to_string (n / 12 - 1);
}

// ---------------------------------------------------------------- Key
std::array<int, 7> Key::scalePitchClasses() const
{
    static const int major[] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minor[] = { 0, 2, 3, 5, 7, 8, 10 };
    const int* steps = mode == Mode::Major ? major : minor;
    std::array<int, 7> out {};
    for (int i = 0; i < 7; ++i) out[(size_t) i] = mod12 (tonic + steps[i]);
    return out;
}

bool Key::containsPitchClass (int pc) const
{
    const auto s = scalePitchClasses();
    return std::find (s.begin(), s.end(), mod12 (pc)) != s.end();
}

bool Key::prefersFlats() const
{
    // Tonalidades con bemoles en la armadura (relativa mayor).
    const int relMajor = mode == Mode::Major ? tonic : mod12 (tonic + 3);
    return relMajor == 5 || relMajor == 10 || relMajor == 3 || relMajor == 8 || relMajor == 1;
}

std::string Key::toString() const
{
    return pitchClassName (tonic, prefersFlats()) + (mode == Mode::Minor ? "m" : "");
}

bool Key::parse (const std::string& textIn, Key& out)
{
    const auto text = trim (textIn);
    for (bool spanish : { true, false })
    {
        size_t used = 0;
        const int pc = parsePitchClassImpl (text, &used, spanish);
        if (pc < 0) continue;
        const auto restRaw = trim (text.substr (used));
        const auto rest = lower (restRaw);
        Mode m;
        if (restRaw.empty() || restRaw == "M" || rest == "maj" || rest == "major" || rest == "mayor" || rest == "dur")
            m = Mode::Major;
        else if (restRaw == "m" || rest == "min" || rest == "minor" || rest == "menor" || rest == "moll" || restRaw == "-")
            m = Mode::Minor;
        else
            continue;
        out.tonic = pc;
        out.mode = m;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- Chord
std::vector<int> Chord::intervals() const
{
    switch (quality)
    {
        case ChordQuality::Major: return { 0, 4, 7 };
        case ChordQuality::Minor: return { 0, 3, 7 };
        case ChordQuality::Diminished: return { 0, 3, 6 };
        case ChordQuality::Augmented: return { 0, 4, 8 };
        case ChordQuality::Dominant7: return { 0, 4, 7, 10 };
        case ChordQuality::Major7: return { 0, 4, 7, 11 };
        case ChordQuality::Minor7: return { 0, 3, 7, 10 };
        case ChordQuality::HalfDiminished7: return { 0, 3, 6, 10 };
        case ChordQuality::Sus2: return { 0, 2, 7 };
        case ChordQuality::Sus4: return { 0, 5, 7 };
        case ChordQuality::Power: return { 0, 7 };
    }
    return { 0, 4, 7 };
}

std::vector<int> Chord::pitchClasses() const
{
    std::vector<int> out;
    for (int i : intervals()) out.push_back (mod12 (root + i));
    return out;
}

bool Chord::containsPitchClass (int pc) const
{
    for (int i : intervals())
        if (mod12 (root + i) == mod12 (pc)) return true;
    return false;
}

std::vector<int> Chord::tensionPitchClasses (const Key& key) const
{
    std::vector<int> out;
    const auto tones = pitchClasses();
    for (int pc : key.scalePitchClasses())
    {
        if (containsPitchClass (pc)) continue;
        bool avoid = false;
        for (int t : tones)
            if (mod12 (pc - t) == 1) avoid = true; // semitono sobre nota del acorde
        if (! avoid) out.push_back (pc);
    }
    return out;
}

std::vector<int> Chord::stablePitchClasses() const
{
    const auto iv = intervals();
    std::vector<int> out;
    for (size_t i = 0; i < iv.size() && i < 3; ++i) out.push_back (mod12 (root + iv[i]));
    return out;
}

bool Chord::isMinorish() const
{
    return quality == ChordQuality::Minor || quality == ChordQuality::Minor7
        || quality == ChordQuality::Diminished || quality == ChordQuality::HalfDiminished7;
}

std::string Chord::toString (bool preferFlats) const
{
    static const char* suffix[] = { "", "m", "dim", "aug", "7", "maj7", "m7", "m7b5", "sus2", "sus4", "5" };
    return pitchClassName (root, preferFlats) + suffix[(int) quality];
}

bool Chord::parse (const std::string& textIn, Chord& out)
{
    const auto text = trim (textIn);
    if (text.empty()) return false;
    for (bool spanish : { true, false })
    {
        size_t used = 0;
        const int pc = parsePitchClassImpl (text, &used, spanish);
        if (pc < 0) continue;
        ChordQuality q;
        if (! parseQuality (text.substr (used), q)) continue;
        out.root = pc;
        out.quality = q;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- Progression
const Chord& Progression::chordAtBar (int bar) const
{
    static const Chord fallback {};
    if (bars.empty()) return fallback;
    const int n = (int) bars.size();
    return bars[(size_t) (((bar % n) + n) % n)];
}

std::string Progression::toString (bool preferFlats) const
{
    std::string s;
    for (size_t i = 0; i < bars.size(); ++i)
    {
        if (i) s += " ";
        s += bars[i].toString (preferFlats);
    }
    return s;
}

bool Progression::parse (const std::string& text, Progression& out, std::string& error)
{
    std::string normalized = text;
    for (auto& c : normalized)
        if (c == '|' || c == ',' || c == ';' || c == '\t' || c == '\n' || c == '\r') c = ' ';
    std::istringstream ss (normalized);
    std::string tok;
    Progression p;
    while (ss >> tok)
    {
        if (tok == "%" || tok == ".")
        {
            if (p.bars.empty()) { error = "'" + tok + "' no puede ir al principio"; return false; }
            p.bars.push_back (p.bars.back());
            continue;
        }
        Chord c;
        if (! Chord::parse (tok, c)) { error = "Acorde no reconocido: '" + tok + "'"; return false; }
        p.bars.push_back (c);
        if (p.bars.size() > 64) { error = "Máximo 64 compases en la progresión"; return false; }
    }
    if (p.bars.empty()) { error = "La progresión está vacía"; return false; }
    out = p;
    error.clear();
    return true;
}

std::vector<std::string> Progression::validate (const Key& key) const
{
    std::vector<std::string> warnings;
    for (size_t i = 0; i < bars.size(); ++i)
    {
        int outside = 0;
        for (int pc : bars[i].pitchClasses())
            if (! key.containsPitchClass (pc)) ++outside;
        if (outside > 0)
            warnings.push_back ("Compás " + std::to_string (i + 1) + ": " + bars[i].toString (key.prefersFlats())
                                + " tiene " + std::to_string (outside) + " nota(s) fuera de " + key.toString());
    }
    return warnings;
}

int nearestPitchWithClass (int target, const std::vector<int>& pcs, int lo, int hi)
{
    if (pcs.empty()) return std::clamp (target, lo, hi);
    target = std::clamp (target, lo, hi);
    for (int d = 0; d <= 24; ++d)
    {
        for (int sign : { 1, -1 })
        {
            const int p = target + sign * d;
            if (p < lo || p > hi) continue;
            for (int pc : pcs)
                if (mod12 (p) == mod12 (pc)) return p;
        }
    }
    return target;
}

} // namespace solo
