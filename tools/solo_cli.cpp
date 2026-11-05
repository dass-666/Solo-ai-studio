// solo_cli: genera solos desde la línea de comandos (MIDI + WAV) con el mismo núcleo que el plugin.
// Sirve para la batería de ejemplos, para escuchar resultados sin DAW y para depurar.
//
//   solo_cli generate --key Am --prog "Am F C G" --bpm 146 --bars 8 --instrument guitar \
//                     --style rock --seed 1 --energy 0.6 --density 0.5 --complexity 0.4 --out solo
//   solo_cli examples <carpeta>
#include "solo/AudioRenderer.h"
#include "solo/MidiFile.h"
#include "solo/SoloComposer.h"
#include "solo/StateManager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

using namespace solo;

namespace
{
bool parseInstrument (const std::string& s, InstrumentId& out)
{
    if (s == "guitar" || s == "guitarra") { out = InstrumentId::Guitar; return true; }
    if (s == "flute" || s == "flauta") { out = InstrumentId::Flute; return true; }
    if (s == "synth" || s == "sinte") { out = InstrumentId::Synth; return true; }
    return false;
}

bool parseStyle (const std::string& s, Style& out)
{
    if (s == "melodic" || s == "melodico") { out = Style::Melodic; return true; }
    if (s == "rock") { out = Style::Rock; return true; }
    if (s == "blues") { out = Style::Blues; return true; }
    if (s == "electronic" || s == "electronico") { out = Style::Electronic; return true; }
    return false;
}

void printSequence (const Sequence& s, const ComposerParams& p)
{
    const int barT = s.timeSignature.ticksPerBar(), beatT = s.timeSignature.ticksPerBeat();
    std::printf ("%zu notas, %d compases %d/4, %.1f BPM\n", s.notes.size(), s.bars, s.timeSignature.numerator, s.bpm);
    int lastBar = -1;
    for (const auto& n : s.notes)
    {
        const int bar = (int) (n.startTick / barT);
        if (bar != lastBar)
        {
            std::printf ("\n  Compás %2d [%s]: ", bar + 1, p.progression.chordAtBar (bar).toString (p.key.prefersFlats()).c_str());
            lastBar = bar;
        }
        const double beat = 1.0 + (double) (n.startTick % barT) / beatT;
        std::printf ("%s@%.2f(%.2f,v%d) ", midiNoteName (n.pitch).c_str(), beat, (double) n.lengthTicks / beatT, n.velocity);
    }
    std::printf ("\n");
}

int writeOutputs (const Sequence& s, const ComposerParams& p, const std::string& prefix, int sampleRate)
{
    std::string err;
    MidiExportOptions mo;
    mo.key = p.key;
    mo.gmProgram = profileFor (p.instrument).gmProgram;
    mo.trackName = std::string ("Solo AI Studio - ") + instrumentName (p.instrument);
    if (! saveMidiFile (prefix + ".mid", s, mo, err)) { std::fprintf (stderr, "%s\n", err.c_str()); return 1; }
    RenderOptions ro;
    ro.sampleRate = sampleRate;
    ro.instrument = p.instrument;
    const auto r = renderSequence (s, ro);
    if (! writeWav24 (prefix + ".wav", r.left, r.right, sampleRate, err)) { std::fprintf (stderr, "%s\n", err.c_str()); return 1; }
    std::printf ("-> %s.mid, %s.wav (pico %.3f, %d note-on renderizados, voces activas al final: %d)\n",
                 prefix.c_str(), prefix.c_str(), r.peak, r.noteOnsRendered, r.activeVoicesAtEnd);
    return 0;
}

int cmdGenerate (int argc, char** argv)
{
    std::map<std::string, std::string> a;
    for (int i = 2; i + 1 < argc; i += 2)
        if (std::strncmp (argv[i], "--", 2) == 0) a[argv[i] + 2] = argv[i + 1];
    auto get = [&] (const char* k, const char* def) { auto it = a.find (k); return it == a.end() ? std::string (def) : it->second; };

    SoloSettings st;
    st.keyText = get ("key", "Am");
    st.progressionText = get ("prog", "Am F C G");
    st.bars = std::atoi (get ("bars", "8").c_str());
    st.tsNumerator = std::atoi (get ("ts", "4").c_str());
    st.energy = std::atof (get ("energy", "0.6").c_str());
    st.density = std::atof (get ("density", "0.5").c_str());
    st.complexity = std::atof (get ("complexity", "0.4").c_str());
    st.seed = std::strtoull (get ("seed", "1").c_str(), nullptr, 10);
    if (! parseInstrument (get ("instrument", "guitar"), st.instrument)) { std::fprintf (stderr, "Instrumento desconocido\n"); return 2; }
    if (! parseStyle (get ("style", "rock"), st.style)) { std::fprintf (stderr, "Estilo desconocido\n"); return 2; }
    const double bpm = std::atof (get ("bpm", "120").c_str());

    ComposerParams p;
    std::string err;
    if (! st.toComposerParams (bpm, p, err)) { std::fprintf (stderr, "Error: %s\n", err.c_str()); return 2; }
    for (const auto& w : p.progression.validate (p.key)) std::printf ("Aviso: %s\n", w.c_str());
    const auto s = SoloComposer::compose (p);
    printSequence (s, p);
    return writeOutputs (s, p, get ("out", "solo"), std::atoi (get ("rate", "48000").c_str()));
}

int cmdExamples (const std::string& dir)
{
    struct Ex { const char* name; const char* key; const char* prog; double bpm; int ts; int bars; const char* ins; const char* style; uint64_t seed; };
    const Ex list[] = {
        { "01_am-f-c-g_146_guitarra_rock_s1", "Am", "Am F C G", 146, 4, 8, "guitar", "rock", 1 },
        { "02_am-f-c-g_146_guitarra_rock_s2", "Am", "Am F C G", 146, 4, 8, "guitar", "rock", 2 },
        { "03_am-f-c-g_146_guitarra_rock_s3", "Am", "Am F C G", 146, 4, 8, "guitar", "rock", 3 },
        { "04_am-f-c-g_146_flauta_melodico", "Am", "Am F C G", 146, 4, 8, "flute", "melodic", 4 },
        { "05_am-f-c-g_146_synth_electronico", "Am", "Am F C G", 146, 4, 8, "synth", "electronic", 5 },
        { "06_blues-a_96_guitarra_16c", "A", "A7 A7 A7 A7 D7 D7 A7 A7 E7 D7 A7 E7 A7 A7 A7 E7", 96, 4, 16, "guitar", "blues", 6 },
        { "07_c-g-am-f_80_flauta_4c", "C", "C G Am F", 80, 4, 4, "flute", "melodic", 7 },
        { "08_vals-dm_120_flauta_3-4", "Dm", "Dm Gm A7 Dm", 120, 3, 8, "flute", "melodic", 8 },
        { "09_fm_128_synth_2c", "Fm", "Fm Db", 128, 4, 2, "synth", "electronic", 9 },
    };
    int rc = 0;
    for (const auto& e : list)
    {
        std::printf ("\n=== %s ===\n", e.name);
        SoloSettings st;
        st.keyText = e.key; st.progressionText = e.prog; st.tsNumerator = e.ts; st.bars = e.bars; st.seed = e.seed;
        parseInstrument (e.ins, st.instrument);
        parseStyle (e.style, st.style);
        ComposerParams p;
        std::string err;
        if (! st.toComposerParams (e.bpm, p, err)) { std::fprintf (stderr, "%s\n", err.c_str()); rc = 1; continue; }
        const auto s = SoloComposer::compose (p);
        printSequence (s, p);
        rc |= writeOutputs (s, p, dir + "/" + e.name, 48000);
    }
    return rc;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc >= 2 && std::strcmp (argv[1], "generate") == 0) return cmdGenerate (argc, argv);
    if (argc >= 3 && std::strcmp (argv[1], "examples") == 0) return cmdExamples (argv[2]);
    std::printf ("Uso:\n  solo_cli generate [--key Am] [--prog \"Am F C G\"] [--bpm 146] [--bars 8] [--ts 4]\n"
                 "                   [--instrument guitar|flute|synth] [--style melodic|rock|blues|electronic]\n"
                 "                   [--seed 1] [--energy 0.6] [--density 0.5] [--complexity 0.4] [--rate 48000] [--out solo]\n"
                 "  solo_cli examples <carpeta>\n");
    return 1;
}
