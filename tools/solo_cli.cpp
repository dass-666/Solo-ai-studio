// solo_cli: herramienta de línea de comandos con el mismo núcleo que el plugin.
//   solo_cli generate [opciones]            genera un solo (MIDI + WAV con efectos)
//   solo_cli examples <carpeta>             batería de ejemplos
//   solo_cli demos <carpeta>                7 demos antes (S0–S2) / después (v2) a igual sonoridad + métricas
//   solo_cli hum <voz.wav> <carpeta> [...]  Hum-to-Solo en los 4 modos
//   solo_cli synthvoice <salida.wav>        voz sintética de prueba
//   solo_cli bench                          CPU y memoria del motor en tiempo real
//   solo_cli calibrate                      sonoridad de los presets de efectos
//   solo_cli command "<orden>"              interpreta una orden en español (vista previa)
#include "solo/Analysis.h"
#include "solo/AudioRenderer.h"
#include "solo/Commands.h"
#include "solo/MidiFile.h"
#include "solo/Project.h"
#include "solo/SoloEngine.h"
#include "solo/StateManager.h"
#include "solo/VoiceAnalysis.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>

using namespace solo;

namespace
{
std::map<std::string, std::string> parseArgs (int argc, char** argv, int first)
{
    std::map<std::string, std::string> a;
    for (int i = first; i + 1 < argc; i += 2)
        if (std::strncmp (argv[i], "--", 2) == 0) a[argv[i] + 2] = argv[i + 1];
    return a;
}

bool parseInstrument (const std::string& s, InstrumentId& out)
{
    if (s == "guitar" || s == "guitarra") { out = InstrumentId::Guitar; return true; }
    if (s == "guitarlite" || s == "s2") { out = InstrumentId::GuitarLite; return true; }
    if (s == "flute" || s == "flauta") { out = InstrumentId::Flute; return true; }
    if (s == "synth" || s == "sinte") { out = InstrumentId::Synth; return true; }
    return false;
}

bool parseStyle (const std::string& s, Style& out)
{
    const std::pair<const char*, Style> m[] = {
        { "rock", Style::MelodicRock }, { "melodicrock", Style::MelodicRock }, { "blues", Style::ExpressiveBlues },
        { "emotional", Style::Emotional }, { "emocional", Style::Emotional }, { "melodic", Style::Emotional },
        { "virtuoso", Style::VirtuosoRock }, { "electronic", Style::VirtuosoRock }, { "ambient", Style::Ambient },
        { "cinematic", Style::Cinematic }, { "soft", Style::SoftImprov }, { "vocal", Style::VocalResponse } };
    for (const auto& [k, v] : m) if (s == k) { out = v; return true; }
    return false;
}

void printSequence (const Sequence& s, const ComposerParams& p)
{
    const int barT = s.timeSignature.ticksPerBar(), beatT = s.timeSignature.ticksPerBeat();
    std::printf ("%zu notas, %d compases %d/4, %.1f BPM", s.notes.size(), s.bars, s.timeSignature.numerator, s.bpm);
    int lastBar = -1;
    for (const auto& n : s.notes)
    {
        const int bar = (int) (n.startTick / barT);
        if (bar != lastBar)
        {
            std::printf ("\n  Compás %2d [%s]: ", bar + 1, p.progression.chordAtBar (bar).toString (p.key.prefersFlats()).c_str());
            lastBar = bar;
        }
        std::string art;
        if (n.expr.bendType == BendType::Up) art += "b";
        if (n.expr.vibratoDepth > 0) art += "~";
        if (n.expr.slideFrom) art += "/";
        if (n.expr.legato) art += "h";
        if (n.expr.palmMute) art += "pm";
        std::printf ("%s%s@%.2f ", midiNoteName (n.pitch).c_str(), art.c_str(), 1.0 + (double) (n.startTick % barT) / beatT);
    }
    std::printf ("\n");
}


struct Rendered { EngineRenderResult r; double lufs = 0; };

Rendered renderOut (const Sequence& s, const ComposerParams& p, const FxSettings& fx, const std::string& prefix,
                    double normaliseTo = 0.0, int sampleRate = 48000)
{
    Rendered out;
    MidiExportOptions mo;
    mo.key = p.key;
    mo.gmProgram = profileFor (p.instrument).gmProgram;
    mo.trackName = std::string ("Solo AI Studio - ") + instrumentName (p.instrument);
    std::string err;
    if (! saveMidiFile (prefix + ".mid", s, mo, err)) std::fprintf (stderr, "%s\n", err.c_str());
    EngineRenderOptions eo;
    eo.sampleRate = sampleRate;
    eo.instrument = p.instrument;
    eo.fx = fx;
    out.r = renderWithEngine (s, eo);
    if (normaliseTo < 0) normaliseLoudness (out.r.left, out.r.right, sampleRate, normaliseTo, 0.89f);
    out.lufs = integratedLoudness (out.r.left, out.r.right, sampleRate);
    if (! writeWav24 (prefix + ".wav", out.r.left, out.r.right, sampleRate, err)) std::fprintf (stderr, "%s\n", err.c_str());
    return out;
}

FxSettings fxByName (const std::string& name)
{
    const int i = fxPresetIndex (name);
    return i >= 0 ? fxPresets()[(size_t) i].settings : FxSettings {};
}

int cmdGenerate (int argc, char** argv)
{
    auto a = parseArgs (argc, argv, 2);
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
    st.engine = std::atoi (get ("engine", "2").c_str());
    st.sectionPlan = get ("plan", "");
    if (! parseInstrument (get ("instrument", "guitar"), st.instrument)) { std::fprintf (stderr, "Instrumento desconocido\n"); return 2; }
    if (! parseStyle (get ("style", "rock"), st.style)) { std::fprintf (stderr, "Estilo desconocido\n"); return 2; }
    const double bpm = std::atof (get ("bpm", "120").c_str());
    ComposerParams p;
    std::string err;
    if (! st.toComposerParams (bpm, p, err)) { std::fprintf (stderr, "Error: %s\n", err.c_str()); return 2; }
    for (const auto& w : p.progression.validate (p.key)) std::printf ("Aviso: %s\n", w.c_str());
    const auto s = SoloComposer::compose (p);
    printSequence (s, p);
    std::printf ("Métricas: %s\n", analyseSolo (s, p).toString().c_str());
    const std::string fx = get ("fx", st.engine == 1 ? "none" : "Emotional Lead");
    const auto r = renderOut (s, p, fx == "none" ? FxSettings {} : fxByName (fx), get ("out", "solo"), 0.0, std::atoi (get ("rate", "48000").c_str()));
    std::printf ("-> %s.mid / .wav (pico %.3f, %.1f LUFS, cola %.1f s)\n", get ("out", "solo").c_str(), r.r.peak, r.lufs, r.r.tailSeconds);
    return 0;
}

struct Demo { const char* name; const char* key; const char* prog; double bpm; Style style; InstrumentId ins; int legacyStyle; const char* fx; uint64_t seed; };

const Demo kDemos[] = {
    { "1_rock_melodico", "Am", "Am F C G", 120, Style::MelodicRock, InstrumentId::Guitar, 1, "Classic Rock Lead", 3 },
    { "2_blues_expresivo", "A", "A7 D7 A7 A7 D7 D7 A7 A7 E7 D7 A7 E7", 92, Style::ExpressiveBlues, InstrumentId::Guitar, 2, "Blues Lead", 5 },
    { "3_solo_emocional", "C", "C G Am F", 76, Style::Emotional, InstrumentId::Guitar, 0, "Emotional Lead", 7 },
    { "4_rock_virtuoso", "Em", "Em C D B7", 132, Style::VirtuosoRock, InstrumentId::Guitar, 1, "High Gain Solo", 2 },
    { "5_ambiental", "D", "Dmaj7 Bm7 Gmaj7 A", 70, Style::Ambient, InstrumentId::Guitar, 0, "Ambient Guitar", 4 },
    { "6_cinematografico", "Dm", "Dm Bb F C", 84, Style::Cinematic, InstrumentId::Guitar, 0, "Cinematic Lead", 6 },
    { "7_lead_sinte", "Fm", "Fm Db Ab Eb", 124, Style::SoftImprov, InstrumentId::Synth, 3, "Wide Synth Lead", 8 },
};

int cmdDemos (const std::string& dir)
{
    std::ofstream md (dir + "/metricas.md");
    md << "# Demos antes (S0–S2) / después (v2)\n\n"
          "Antes: compositor S0–S2 (motor 1), guitarra ligera S2, sin efectos. Después: compositor jerárquico (motor 2), "
          "intérprete, guitarra modelada y preset de efectos. Ambos normalizados a -18 LUFS (BS.1770 aproximado), pico <= -1 dBFS.\n"
          "Métricas objetivas (no son una medida de calidad percibida; no ha habido evaluación de escucha humana).\n\n"
          "| Demo | Versión | Notas | Notas/s | Silencio | Notas del acorde (t. fuerte) | Rep. motivos | Rep. ritmo | Frases | Articulaciones (b/~/sl/h/pm) | LUFS |\n"
          "|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (const auto& d : kDemos)
    {
        ComposerParams p;
        Key::parse (d.key, p.key);
        std::string err;
        Progression::parse (d.prog, p.progression, err);
        p.bars = 8;
        p.bpm = d.bpm;
        p.seed = d.seed;
        p.energy = 0.6; p.density = 0.5; p.complexity = 0.45;
        p.style = d.style;
        // antes
        ComposerParams before = p;
        before.engine = 1;
        before.legacyStyle = d.legacyStyle;
        before.instrument = d.ins == InstrumentId::Guitar ? InstrumentId::GuitarLite : d.ins;
        const auto sb = SoloComposer::compose (before);
        const auto rb = renderOut (sb, before, FxSettings {}, dir + "/" + d.name + "_antes", -18.0);
        // después
        ComposerParams after = p;
        after.engine = 2;
        after.instrument = d.ins;
        const auto sa = SoloComposer::compose (after);
        const auto ra = renderOut (sa, after, fxByName (d.fx), dir + "/" + d.name + "_despues", -18.0);
        for (int v = 0; v < 2; ++v)
        {
            const auto& s = v == 0 ? sb : sa;
            const auto m = analyseSolo (s, v == 0 ? before : after);
            char row[512];
            std::snprintf (row, sizeof row, "| %s | %s | %d | %.2f | %.0f%% | %.0f%% | %.2f | %.2f | %d | %d/%d/%d/%d/%d | %.1f |\n", d.name,
                           v == 0 ? "antes" : "después", m.notes, m.notesPerSecond, m.restRatio * 100, m.strongChordTone * 100,
                           m.motifRecurrence, m.rhythmRecurrence, m.phrases, m.bends, m.vibratos, m.slides, m.legatos, m.palmMutes,
                           v == 0 ? rb.lufs : ra.lufs);
            md << row;
        }
        std::printf ("%s: antes %zu notas (%.1f LUFS) / después %zu notas (%.1f LUFS, %s)\n", d.name, sb.notes.size(), rb.lufs,
                     sa.notes.size(), ra.lufs, d.fx);
    }
    std::printf ("-> %s/metricas.md\n", dir.c_str());
    return 0;
}

int cmdHum (int argc, char** argv)
{
    if (argc < 4) return 2;
    MonoAudio audio;
    std::string err;
    if (! readWavFile (argv[2], audio, err)) { std::fprintf (stderr, "%s\n", err.c_str()); return 1; }
    const std::string dir = argv[3];
    auto a = parseArgs (argc, argv, 4);
    auto get = [&] (const char* k, const char* def) { auto it = a.find (k); return it == a.end() ? std::string (def) : it->second; };
    const auto va = analyseVoice (audio);
    std::printf ("Voz: %.1f s, %zu notas, confianza %.2f, altura fiable: %s, tempo estimado %.1f BPM\n", va.durationSec,
                 va.notes.size(), va.meanConfidence, va.pitchReliable ? "sí" : "no", va.estimatedBpm);
    for (const auto& w : va.warnings) std::printf ("Aviso: %s\n", w.c_str());
    for (const auto& n : va.notes)
        std::printf ("  %.2f-%.2f s  %s%s (conf %.2f, glide %.1f, vib %.2f)\n", n.startSec, n.endSec,
                     n.pitched ? midiNoteName ((int) std::lround (n.midi)).c_str() : "?", n.pitched ? "" : " (sin altura)",
                     n.confidence, n.glideInSemis, n.vibratoSemis);
    ComposerParams p;
    Key::parse (get ("key", "Am").c_str(), p.key);
    Progression::parse (get ("prog", "Am F C G"), p.progression, err);
    p.bpm = std::atof (get ("bpm", va.estimatedBpm > 0 ? std::to_string (va.estimatedBpm).c_str() : "100").c_str());
    p.bars = std::atoi (get ("bars", "8").c_str());
    p.style = Style::Emotional;
    p.instrument = InstrumentId::Guitar;
    for (int m = 0; m < 4; ++m)
    {
        HumToSoloOptions o;
        o.mode = (HumMode) m;
        const auto res = humToSolo (va, p, o);
        const std::string prefix = dir + "/voz_" + humModeName (o.mode);
        renderOut (res.sequence, p, fxByName ("Emotional Lead"), prefix, -18.0);
        std::printf ("%s: %zu notas%s -> %s.mid/.wav\n", humModeName (o.mode), res.sequence.notes.size(),
                     res.usedRhythmOnly ? " (solo ritmo)" : "", prefix.c_str());
    }
    return 0;
}

int cmdSynthVoice (const std::string& path)
{
    const double e = 0.3;
    const std::vector<SynthVoiceNote> mel = { { 0.5, 2 * e - 0.05, 57 }, { 0.5 + 2 * e, e - 0.03, 60 }, { 0.5 + 3 * e, e, 62 },
                                              { 0.5 + 4 * e, 3 * e, 64, -2.0, 0.3 }, { 0.5 + 8 * e, e - 0.04, 62 },
                                              { 0.5 + 9 * e, e - 0.04, 60 }, { 0.5 + 10 * e, 2 * e - 0.05, 59 }, { 0.5 + 12 * e, 4 * e, 57, 0, 0.35 } };
    const auto a = synthesiseVoice (mel, 44100, -50.0, 1);
    std::string err;
    if (! writeWav24 (path, a.samples, a.samples, 44100, err)) { std::fprintf (stderr, "%s\n", err.c_str()); return 1; }
    std::printf ("-> %s (voz sintética, 100 BPM, La menor)\n", path.c_str());
    return 0;
}

long peakRssKb()
{
#if defined(__linux__)
    std::ifstream f ("/proc/self/status");
    std::string line;
    while (std::getline (f, line))
        if (line.rfind ("VmHWM:", 0) == 0) return std::atol (line.c_str() + 6);
#endif
    return -1;
}

int cmdBench()
{
    ComposerParams p;
    Key::parse ("Em", p.key);
    std::string err;
    Progression::parse ("Em C D B7", p.progression, err);
    p.bars = 16;
    p.bpm = 140;
    p.style = Style::VirtuosoRock;
    p.seed = 1;
    const auto s = SoloComposer::compose (p);
    std::printf ("Solo de prueba: %zu notas (rock virtuoso, 140 BPM). Bloques de 256 muestras a 48 kHz, 60 s de audio.\n", s.notes.size());
    std::printf ("%-20s %12s %14s %14s\n", "Preset", "CPU media %", "bloque peor %", "x tiempo real");
    const long rss0 = peakRssKb();
    std::vector<std::pair<std::string, FxSettings>> sets { { "Sin efectos", FxSettings {} } };
    for (const auto& pr : fxPresets()) sets.push_back ({ pr.name, pr.settings });
    for (const auto& [name, fx] : sets)
    {
        SoloEngine e;
        e.setInstrument (InstrumentId::Guitar);
        e.publishFx (fx);
        e.publishSequence (makePlaybackData (s, 0, true));
        e.prepare (48000, 256);
        std::vector<float> L (256), R (256);
        TransportState t;
        t.valid = true; t.playing = true; t.bpm = 140;
        const int blocks = 60 * 48000 / 256;
        double total = 0, worst = 0;
        for (int b = 0; b < blocks; ++b)
        {
            t.ppqPosition = b * 256.0 * 140.0 / 60.0 / 48000.0;
            const auto t0 = std::chrono::steady_clock::now();
            e.process (L.data(), R.data(), 256, t);
            const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
            total += us;
            worst = std::max (worst, us);
        }
        const double blockUs = 256.0 / 48000.0 * 1e6;
        std::printf ("%-20s %11.2f%% %13.1f%% %13.0fx\n", name.c_str(), 100.0 * total / blocks / blockUs, 100.0 * worst / blockUs,
                     blocks * blockUs / total);
    }
    std::printf ("Memoria: tamaño de SoloEngine %zu bytes + buffers (delay 4 s estéreo, reverb, chorus, cuerdas)\n", sizeof (SoloEngine));
    std::printf ("Pico de memoria residente del proceso: %ld KB (antes de la prueba: %ld KB)\n", peakRssKb(), rss0);
    return 0;
}

int cmdCalibrate()
{
    ComposerParams p;
    Key::parse ("Am", p.key);
    std::string err;
    Progression::parse ("Am F C G", p.progression, err);
    p.bars = 8; p.bpm = 110; p.seed = 7;
    for (const auto& pr : fxPresets())
    {
        p.instrument = std::string (pr.name) == "Wide Synth Lead" ? InstrumentId::Synth : InstrumentId::Guitar;
        EngineRenderOptions o;
        o.fx = pr.settings;
        o.instrument = p.instrument;
        const auto r = renderWithEngine (SoloComposer::compose (p), o);
        std::printf ("%-18s %.1f LUFS, pico %.2f, cola %.1f s\n", pr.name, integratedLoudness (r.left, r.right, 48000), r.peak, r.tailSeconds);
    }
    return 0;
}

int cmdCommand (const std::string& text)
{
    ProjectController pc;
    pc.state.settings.bars = 8;
    pc.generate (1);
    const auto cmd = parseCommand ({ text });
    std::printf ("Entendido (confianza %.2f): %s\n", cmd.confidence, cmd.summary().c_str());
    const auto pv = previewCommand (pc, cmd, 42);
    for (const auto& s : pv.steps) std::printf ("  %s\n", s.c_str());
    std::printf ("Requiere confirmación: %s\n", cmd.needsConfirmation ? "sí" : "no");
    return cmd.ops.empty() ? 1 : 0;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc >= 2 && std::strcmp (argv[1], "generate") == 0) return cmdGenerate (argc, argv);
    if (argc >= 3 && std::strcmp (argv[1], "demos") == 0) return cmdDemos (argv[2]);
    if (argc >= 4 && std::strcmp (argv[1], "hum") == 0) return cmdHum (argc, argv);
    if (argc >= 3 && std::strcmp (argv[1], "synthvoice") == 0) return cmdSynthVoice (argv[2]);
    if (argc >= 2 && std::strcmp (argv[1], "bench") == 0) return cmdBench();
    if (argc >= 2 && std::strcmp (argv[1], "calibrate") == 0) return cmdCalibrate();
    if (argc >= 3 && std::strcmp (argv[1], "command") == 0) return cmdCommand (argv[2]);
    if (argc >= 3 && std::strcmp (argv[1], "examples") == 0)
    {
        // Batería de ejemplos = demos "después" con los mismos parámetros (compatibilidad con la CI de S0–S2).
        return cmdDemos (argv[2]);
    }
    std::printf ("Uso:\n"
                 "  solo_cli generate [--key Am] [--prog \"Am F C G\"] [--bpm 120] [--bars 8] [--ts 4] [--engine 1|2]\n"
                 "                   [--instrument guitar|guitarlite|flute|synth] [--style rock|blues|emotional|virtuoso|ambient|cinematic|soft|vocal]\n"
                 "                   [--plan \"P2 D2 T2 C1 R1\"] [--fx \"Emotional Lead\"|none] [--seed 1] [--out solo]\n"
                 "  solo_cli demos <carpeta>\n  solo_cli examples <carpeta>\n"
                 "  solo_cli hum <voz.wav> <carpeta> [--key Am] [--prog \"Am F C G\"] [--bpm 100] [--bars 8]\n"
                 "  solo_cli synthvoice <salida.wav>\n  solo_cli bench\n  solo_cli calibrate\n  solo_cli command \"<orden en español>\"\n");
    return 1;
}
