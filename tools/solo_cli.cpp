// solo_cli: herramienta de línea de comandos con el mismo núcleo que el plugin.
//   solo_cli generate [opciones]            genera un solo (MIDI + WAV con efectos; --tone "Rock Lead" --force 70 --expression 65 --ambience 35)
//   solo_cli examples <carpeta>             batería de ejemplos
//   solo_cli demos <carpeta>                7 demos antes (S0–S2) / después (v2) a igual sonoridad + métricas
//   solo_cli hum <voz.wav> <carpeta> [...]  Hum-to-Solo en los 4 modos
//   solo_cli synthvoice <salida.wav>        voz sintética de prueba
//   solo_cli bench                          CPU y memoria del motor en tiempo real (v2 y v3)
//   solo_cli tone-ab <carpeta>              Guitar Tone Studio v3: mismo MIDI con cada rig, a -18 LUFS, + métricas
//   solo_cli makebank <carpeta>             banco SFZ sintético de prueba (etiquetado; no son samples reales)
//   solo_cli calibrate                      sonoridad de los presets de efectos
//   solo_cli command "<orden>"              interpreta una orden en español (vista previa)
#include "solo/Analysis.h"
#include "solo/AudioRenderer.h"
#include "solo/BeatAnalyzer.h"
#include "solo/Commands.h"
#include "solo/MidiFile.h"
#include "solo/Project.h"
#include "solo/Sampler.h"
#include "solo/SoloEngine.h"
#include "solo/ToneStudio.h"
#include "solo/StateManager.h"
#include "solo/VoiceAnalysis.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
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
                    double normaliseTo = 0.0, int sampleRate = 48000, const GuitarRigSettings* rig = nullptr)
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
    if (rig != nullptr) { eo.rig = *rig; mo.expressionPercent = rig->engine == GuitarEngineKind::LegacyV2 ? -1.0 : rig->expression.amount * 100.0; }
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
    // Guitar Tone Studio v3: --tone "<preset>" [--force F --expression E --ambience A] [--guitar modelada|samples|v2 --sfz ruta]
    ProjectController pc;
    pc.bpm = bpm;
    pc.state.settings = st;
    bool v3 = false;
    if (a.count ("tone")) { const auto r = pc.setTonePreset (a.at ("tone")); if (! r.ok) { std::fprintf (stderr, "%s\n", r.message.c_str()); return 2; } v3 = true; }
    if (a.count ("force") || a.count ("expression") || a.count ("ambience"))
    {
        if (! v3) { pc.setTonePreset ("Rock Lead"); v3 = true; }
        ToneMacros m = pc.state.macros;
        if (a.count ("force")) m.force = std::atof (a.at ("force").c_str());
        if (a.count ("expression")) m.expression = std::atof (a.at ("expression").c_str());
        if (a.count ("ambience")) m.ambience = std::atof (a.at ("ambience").c_str());
        pc.setMacros (m);
    }
    if (a.count ("guitar"))
    {
        const auto g = a.at ("guitar");
        v3 = true;
        pc.setGuitarEngine (g == "samples" ? GuitarEngineKind::Samples : g == "v2" ? GuitarEngineKind::LegacyV2 : GuitarEngineKind::Modeled);
    }
    SharedBank bank;
    if (a.count ("sfz"))
    {
        auto b = std::make_shared<SampleBank>(); std::string e;
        if (! loadSfz (a.at ("sfz"), *b, e)) { std::fprintf (stderr, "SFZ: %s\n", e.c_str()); return 2; }
        if (! e.empty()) std::printf ("Aviso SFZ: %s\n", e.c_str());
        std::printf ("Banco: %s\n", b->summary().c_str());
        bank = b;
        pc.setGuitarEngine (GuitarEngineKind::Samples);
        v3 = true;
    }
    ComposerParams p;
    std::string err;
    if (! pc.params (p, err)) { std::fprintf (stderr, "Error: %s\n", err.c_str()); return 2; }
    for (const auto& w : p.progression.validate (p.key)) std::printf ("Aviso: %s\n", w.c_str());
    const auto s = SoloComposer::compose (p);
    printSequence (s, p);
    std::printf ("Métricas: %s\n", analyseSolo (s, p).toString().c_str());
    const std::string fx = get ("fx", st.engine == 1 ? "none" : "Emotional Lead");
    GuitarRigSettings rig = pc.rigSettings();
    if (v3)
    {
        std::printf ("Tono v3: %s · motor %s · Fuerza %.0f %% (previo %.1f dB) · Expresión %.0f %% · Ambiente %.0f %% (reverb %.1f %%, delay %.1f %%)\n",
                     pc.state.settings.fxPreset.c_str(), guitarEngineName (rig.engine), pc.state.macros.force, pc.state.fx.ampPreGainDb,
                     pc.state.macros.expression, pc.state.macros.ambience, pc.state.fx.reverbMix * 100, pc.state.fx.delayMix * 100);
    }
    const auto r = v3 ? [&] { Rendered out; MidiExportOptions mo; mo.key = p.key; mo.gmProgram = profileFor (p.instrument).gmProgram;
                                mo.trackName = std::string ("Solo AI Studio - ") + instrumentName (p.instrument); mo.expressionPercent = pc.state.macros.expression;
                                std::string e; if (! saveMidiFile (get ("out", "solo") + ".mid", s, mo, e)) std::fprintf (stderr, "%s\n", e.c_str());
                                EngineRenderOptions eo; eo.sampleRate = std::atoi (get ("rate", "48000").c_str()); eo.instrument = p.instrument; eo.fx = pc.state.fx;
                                eo.rig = rig; eo.bank = bank; out.r = renderWithEngine (s, eo);
                                out.lufs = integratedLoudness (out.r.left, out.r.right, eo.sampleRate);
                                if (! writeWav24 (get ("out", "solo") + ".wav", out.r.left, out.r.right, (int) eo.sampleRate, e)) std::fprintf (stderr, "%s\n", e.c_str());
                                return out; }()
                      : renderOut (s, p, fx == "none" ? FxSettings {} : fxByName (fx), get ("out", "solo"), 0.0, std::atoi (get ("rate", "48000").c_str()));
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

// V4-A: analizar un beat WAV (offline) e imprimir tempo, tonalidad, acordes y regiones con sus confianzas.
int cmdBeat (int argc, char** argv)
{
    if (argc < 3) return 2;
    auto a = parseArgs (argc, argv, 3);
    auto get = [&] (const char* k, const char* def) { auto it = a.find (k); return it == a.end() ? std::string (def) : it->second; };
    BeatAnalysisOptions o;
    o.beatsPerBar = std::atoi (get ("ts", "4").c_str());
    o.minBpm = std::atof (get ("min", "60").c_str());
    o.maxBpm = std::atof (get ("max", "200").c_str());
    const auto t0 = std::chrono::steady_clock::now();
    const auto r = analyseBeatFile (argv[2], o);
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    if (! r.ok) { std::fprintf (stderr, "Error: %s\n", r.error.c_str()); return 1; }
    std::printf ("Archivo: %.1f s, %.0f Hz, %d canal(es), pico %.1f dBFS, RMS %.1f dBFS (análisis: %.0f ms)\n",
                 r.durationSec, r.sampleRate, r.sourceChannels, r.peakDb, r.rmsDb, ms);
    std::printf ("Resumen: %s\n", r.summary().c_str());
    if (r.silent) return 0;
    std::printf ("Tempo (estimado): %.2f BPM, confianza %.2f, primer pulso %.3f s, %zu pulsos, %d compases de %d\n",
                 r.bpm, r.bpmConfidence, r.firstBeatSec, r.beatsSec.size(), r.bars, r.beatsPerBar);
    for (const auto& c : r.bpmCandidates) std::printf ("  candidato %.2f BPM (puntuación %.2f)\n", c.bpm, c.score);
    if (r.tonal)
    {
        std::printf ("Tonalidad (estimada): %s, confianza %.2f\n", r.key.toString().c_str(), r.keyConfidence);
        for (const auto& k : r.keyCandidates) std::printf ("  candidata %s (correlación %.3f)\n", k.key.toString().c_str(), k.score);
        if (! r.harmony.empty())
        {
            std::printf ("Acordes por compás (estimados, confianza media %.2f):\n", r.chordConfidence);
            for (const auto& seg : r.harmony.segments)
                std::printf ("  compás %d-%d: %s (confianza %.2f)\n", seg.startBar + 1, seg.startBar + seg.lengthBars, seg.chord.toString (r.key.prefersFlats()).c_str(), seg.confidence);
            std::printf ("Cambios armónicos en compases:");
            for (int b : r.harmonicChanges) std::printf (" %d", b + 1);
            std::printf ("\n");
        }
        if (! r.chordEvents.empty())
        {
            std::printf ("Acordes por tiempo:\n");
            for (const auto& e : r.chordEvents) std::printf ("  %.2f-%.2f s: %s (confianza %.2f)\n", e.startSec, e.endSec, e.chord.toString (r.key.prefersFlats()).c_str(), e.confidence);
        }
    }
    else std::printf ("Sin contenido tonal fiable (beat percusivo): tonalidad y acordes a mano.\n");
    std::printf ("Regiones de energía:\n");
    for (const auto& e : r.energyRegions)
        std::printf ("  compás %d-%d (%.1f-%.1f s): %s, %.1f dBFS\n", e.startBar + 1, e.endBar, e.startSec, e.endSec,
                     e.level == EnergyLevel::Low ? "baja" : e.level == EnergyLevel::High ? "alta" : "media", e.rmsDb);
    for (const auto& w : r.warnings) std::printf ("Aviso: %s\n", w.c_str());
    const std::string out = get ("out", "");
    if (! out.empty())
    {
        std::ofstream f (out);
        f << serializeBeatAnalysis (r);
        std::printf ("-> %s\n", out.c_str());
    }
    return 0;
}

// V4-A: beat sintético con BPM y tonalidad conocidos (pruebas manuales y demos).
int cmdSynthBeat (int argc, char** argv)
{
    if (argc < 3) return 2;
    auto a = parseArgs (argc, argv, 3);
    auto get = [&] (const char* k, const char* def) { auto it = a.find (k); return it == a.end() ? std::string (def) : it->second; };
    SyntheticBeatSpec s;
    s.bpm = std::atof (get ("bpm", "120").c_str());
    Key::parse (get ("key", "Am"), s.key);
    s.progression = get ("prog", "Am F C G");
    s.bars = std::atoi (get ("bars", "8").c_str());
    s.beatsPerBar = std::atoi (get ("ts", "4").c_str());
    s.sampleRate = std::atof (get ("sr", "44100").c_str());
    s.drums = get ("drums", "1") != "0";
    s.harmony = get ("harmony", "1") != "0";
    s.noiseDb = std::atof (get ("noise", "-70").c_str());
    s.swingSec = std::atof (get ("swing", "0").c_str());
    s.leadInSec = std::atof (get ("leadin", "0").c_str());
    s.seed = std::strtoull (get ("seed", "1").c_str(), nullptr, 10);
    const auto audio = synthesiseBeat (s);
    if (! writeWavFile (argv[2], audio, std::atoi (get ("channels", "1").c_str()))) { std::fprintf (stderr, "No se pudo escribir %s\n", argv[2]); return 1; }
    std::printf ("Beat sintético: %.1f BPM, %s, %s, %d compases, %.1f s -> %s\n", s.bpm, s.key.toString().c_str(), s.progression.c_str(), s.bars, audio.durationSec(), argv[2]);
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


// ============================================================ Guitar Tone Studio v3
struct ToneProfile { double lufs = -70, brightDb = 0, crestDb = 0, tailDb = -90, widthDb = -90; };

// Métricas baratas en el dominio del tiempo (las mismas que la prueba tone_presets_differ_objectively).
ToneProfile profileOf (const EngineRenderResult& r, double sr, double bodyEndSec)
{
    ToneProfile p;
    const auto& L = r.left; const auto& R = r.right;
    if (L.empty()) return p;
    p.lufs = integratedLoudness (L, R, sr);
    const double kPi = 3.141592653589793;
    const double aLo = std::exp (-2 * kPi * 400.0 / sr), aHi = std::exp (-2 * kPi * 2500.0 / sr);
    double lo = 0, hi = 0, hiState = 0, loState = 0, sum2 = 0, peak = 0, body = 0, tail = 0, mid = 0, side = 0;
    const size_t bodyEnd = std::min (L.size(), (size_t) (bodyEndSec * sr));
    for (size_t i = 0; i < L.size(); ++i)
    {
        const double x = L[i];
        loState = aLo * loState + (1 - aLo) * x; lo += loState * loState;
        hiState = aHi * hiState + (1 - aHi) * x; const double h = x - hiState; hi += h * h;
        sum2 += x * x; peak = std::max (peak, std::abs (x));
        (i < bodyEnd ? body : tail) += x * x;
        const double m = 0.5 * (L[i] + R[i]), s = 0.5 * (L[i] - R[i]);
        mid += m * m; side += s * s;
    }
    p.brightDb = 10 * std::log10 (std::max (1e-30, hi) / std::max (1e-30, lo));
    p.crestDb = 20 * std::log10 (std::max (1e-30, peak) / std::sqrt (std::max (1e-30, sum2 / (double) L.size())));
    p.tailDb = 10 * std::log10 (std::max (1e-30, tail) / std::max (1e-30, body));
    p.widthDb = 10 * std::log10 (std::max (1e-30, side) / std::max (1e-30, mid));
    return p;
}

Sequence toneDemoSolo (ComposerParams& p, double expression = 65.0, uint64_t seed = 4242)
{
    Key::parse ("Am", p.key);
    std::string err;
    Progression::parse ("Am F C G", p.progression, err);
    p.bars = 8; p.bpm = 110; p.seed = seed; p.style = Style::MelodicRock; p.energy = 0.65; p.density = 0.5; p.complexity = 0.45;
    p.expression = expression;
    return SoloComposer::compose (p);
}

// Renderiza el MISMO solo con un rig v3 (o v2) y lo normaliza a -18 LUFS; devuelve el perfil medido ANTES de normalizar.
ToneProfile renderToneDemo (const Sequence& s, const FxSettings& fx, const GuitarParams& g, GuitarEngineKind k, double expression,
                            const std::string& path, SharedBank bank = {}, double* lufsAfter = nullptr)
{
    EngineRenderOptions eo;
    eo.sampleRate = 48000; eo.instrument = InstrumentId::Guitar; eo.fx = fx;
    eo.rig.engine = k; eo.rig.guitar = g; eo.rig.expression = expressionScaling (expression); eo.bank = std::move (bank);
    eo.extraTailSeconds = 1.0; eo.maxTailSeconds = 6.0;
    auto r = renderWithEngine (s, eo);
    const double bodyEnd = (double) s.lengthTicks() / kPPQ * 60.0 / (s.bpm > 0 ? s.bpm : 110.0);
    const auto prof = profileOf (r, 48000, bodyEnd);
    normaliseLoudness (r.left, r.right, 48000, -18.0, 0.89f);
    if (lufsAfter) *lufsAfter = integratedLoudness (r.left, r.right, 48000);
    std::string err;
    if (! writeWav24 (path, r.left, r.right, 48000, err)) std::fprintf (stderr, "%s\n", err.c_str());
    return prof;
}

int cmdMakeBank (const std::string& dir)
{
    std::string err;
    const auto sfz = makeSyntheticBank (dir, err);
    if (sfz.empty()) { std::fprintf (stderr, "No se pudo generar el banco: %s\n", err.c_str()); return 1; }
    SampleBank b;
    if (! loadSfz (sfz, b, err)) { std::fprintf (stderr, "El banco generado no carga: %s\n", err.c_str()); return 1; }
    std::printf ("Banco SINTÉTICO (modelo físico, no samples reales) escrito en %s\n  %s\n", sfz.c_str(), b.summary().c_str());
    return 0;
}

int cmdToneAb (int argc, char** argv)
{
    const std::string dir = argv[2];
    const auto args = parseArgs (argc, argv, 3);
    const double expression = args.count ("expression") ? std::atof (args.at ("expression").c_str()) : 65.0;
    ComposerParams p;
    const auto s = toneDemoSolo (p, expression);
    // MIDI editable (sin pitch bend) y MIDI con los mismos gestos que suenan.
    {
        MidiExportOptions mo; mo.key = p.key; mo.trackName = "Solo AI Studio - demo tono"; std::string err;
        mo.pitchBend = false; saveMidiFile (dir + "/demo_tono_limpio.mid", s, mo, err);
        mo.pitchBend = true; mo.expressionPercent = expression; saveMidiFile (dir + "/demo_tono_gestos.mid", s, mo, err);
    }
    std::ofstream md (dir + "/metricas-tono.md");
    md << "# Guitar Tone Studio v3: mismo MIDI, distintos rigs\n\n"
          "Solo: Am | Am F C G | 8 compases | 110 BPM | semilla 4242 | Expresión " << expression << " %. "
          "Todos los WAV normalizados a -18 LUFS (pico <= -1 dBFS) para comparar a igual sonoridad.\n"
          "Métricas ANTES de normalizar (brillo = energía >2.5 kHz / <400 Hz; cresta = pico/RMS; cola = energía tras la última nota / cuerpo; "
          "anchura = lado/centro). Son medidas objetivas, no una evaluación auditiva humana.\n\n"
          "| Archivo | Motor | Preset / macros | LUFS | Brillo dB | Cresta dB | Cola dB | Anchura dB |\n|---|---|---|---|---|---|---|---|\n";
    auto rowOf = [&] (const std::string& file, const std::string& engine, const std::string& preset, const ToneProfile& pr) {
        char row[400];
        std::snprintf (row, sizeof row, "| %s | %s | %s | %.1f | %.1f | %.1f | %.1f | %.1f |\n", file.c_str(), engine.c_str(), preset.c_str(),
                       pr.lufs, pr.brightDb, pr.crestDb, pr.tailDb, pr.widthDb);
        md << row;
        std::printf ("%-34s %-22s %-34s %6.1f LUFS  brillo %5.1f  cresta %5.1f  cola %6.1f\n", file.c_str(), engine.c_str(), preset.c_str(), pr.lufs, pr.brightDb, pr.crestDb, pr.tailDb);
    };
    // v2 (referencia A/B): motor anterior + presets de efectos v2
    for (const char* name : { "Classic Rock Lead", "Blues Lead", "Ambient Guitar" })
    {
        const std::string file = std::string ("v2_") + name + ".wav";
        std::replace_if (const_cast<std::string&> (file).begin(), const_cast<std::string&> (file).end(), [] (char c) { return c == ' '; }, '_');
        const auto pr = renderToneDemo (s, fxByName (name), GuitarParams {}, GuitarEngineKind::LegacyV2, 65.0, dir + "/" + file);
        rowOf (file, "v2 (anterior)", name, pr);
    }
    // v3: los seis presets de tono con sus macros de fábrica
    for (const auto& tp : tonePresets())
    {
        std::string file = std::string ("v3_") + tp.name + ".wav";
        std::replace (file.begin(), file.end(), ' ', '_');
        const auto pr = renderToneDemo (s, tp.fx, tp.guitar, tp.engine, tp.macros.expression, dir + "/" + file);
        char m[96]; std::snprintf (m, sizeof m, "%s (F %.0f / E %.0f / A %.0f)", tp.name, tp.macros.force, tp.macros.expression, tp.macros.ambience);
        rowOf (file, guitarEngineName (tp.engine), m, pr);
    }
    // Barrido de Fuerza y Ambiente sobre Rock Lead (las macros escriben valores DSP reales)
    {
        const auto& rock = tonePresets()[(size_t) tonePresetIndex ("Rock Lead")];
        for (double F : { 0.0, 35.0, 70.0, 100.0 })
        {
            FxSettings fx = rock.fx; GuitarParams g = rock.guitar; ToneMacros m = rock.macros; m.force = F; applyMacros (m, fx, g);
            char file[64]; std::snprintf (file, sizeof file, "v3_Rock_Lead_fuerza_%03d.wav", (int) F);
            char label[96]; std::snprintf (label, sizeof label, "Rock Lead, Fuerza %.0f %% (previo %.1f dB)", F, fx.ampPreGainDb);
            rowOf (file, "Guitarra modelada v3", label, renderToneDemo (s, fx, g, rock.engine, m.expression, dir + "/" + file));
        }
        for (double A : { 0.0, 35.0, 100.0 })
        {
            FxSettings fx = rock.fx; GuitarParams g = rock.guitar; ToneMacros m = rock.macros; m.ambience = A; applyMacros (m, fx, g);
            char file[64]; std::snprintf (file, sizeof file, "v3_Rock_Lead_ambiente_%03d.wav", (int) A);
            char label[128]; std::snprintf (label, sizeof label, "Rock Lead, Ambiente %.0f %% (reverb %.1f %%, delay %.1f %%)", A, fx.reverbMix * 100, fx.delayMix * 100);
            rowOf (file, "Guitarra modelada v3", label, renderToneDemo (s, fx, g, rock.engine, m.expression, dir + "/" + file));
        }
        // Expresión: 0 % (sin gestos) frente a 100 % (más gestos), regenerando el solo con la misma semilla
        for (double E : { 0.0, 100.0 })
        {
            ComposerParams pe; const auto se = toneDemoSolo (pe, E);
            FxSettings fx = rock.fx; GuitarParams g = rock.guitar; ToneMacros m = rock.macros; m.expression = E; applyMacros (m, fx, g);
            char file[64]; std::snprintf (file, sizeof file, "v3_Rock_Lead_expresion_%03d.wav", (int) E);
            char label[96]; std::snprintf (label, sizeof label, "Rock Lead, Expresión %.0f %% (mismo solo regenerado)", E);
            rowOf (file, "Guitarra modelada v3", label, renderToneDemo (se, fx, g, rock.engine, E, dir + "/" + file));
        }
    }
    // Motor de samples con el banco sintético (claramente etiquetado): se genera en <dir>/banco-sintetico
    {
        std::string err;
        const auto sfz = makeSyntheticBank (dir + "/banco-sintetico", err);
        auto bank = std::make_shared<SampleBank>();
        if (! sfz.empty() && loadSfz (sfz, *bank, err))
        {
            const auto& rock = tonePresets()[(size_t) tonePresetIndex ("Rock Lead")];
            const auto pr = renderToneDemo (s, rock.fx, rock.guitar, GuitarEngineKind::Samples, rock.macros.expression, dir + "/v3_Rock_Lead_samples_sinteticos.wav", bank);
            rowOf ("v3_Rock_Lead_samples_sinteticos.wav", "Guitarra con samples", "Rock Lead + banco SINTÉTICO de prueba", pr);
        }
        else std::fprintf (stderr, "Banco sintético no disponible: %s\n", err.c_str());
    }
    md << "\nMIDI: demo_tono_limpio.mid (notas sin pitch bend) y demo_tono_gestos.mid (pitch bend = los gestos que suenan).\n";
    std::printf ("-> %s/metricas-tono.md\n", dir.c_str());
    return 0;
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
    std::printf ("%-20s %10s %9s %9s %10s %8s %10s\n", "Preset", "media %", "p99 %", "p99.9 %", "peor %", "(bloque)", "x t.real");
    const long rss0 = peakRssKb();
    struct Set { std::string name; FxSettings fx; GuitarRigSettings rig; SharedBank bank; };
    std::vector<Set> sets { { "Sin efectos (v2)", FxSettings {}, {}, {} } };
    for (const auto& pr : fxPresets()) sets.push_back ({ std::string (pr.name) + " (v2)", pr.settings, {}, {} });
    for (const auto& tp : tonePresets())
    {
        GuitarRigSettings rig; rig.engine = tp.engine; rig.guitar = tp.guitar; rig.expression = expressionScaling (tp.macros.expression);
        sets.push_back ({ std::string (tp.name) + " (v3 " + std::to_string (tp.fx.ampOversample) + "x)", tp.fx, rig, {} });
    }
    {
        std::string err;
        const auto sfz = makeSyntheticBank ((std::filesystem::temp_directory_path() / "solo_bench_bank").string(), err);
        auto bank = std::make_shared<SampleBank>();
        if (! sfz.empty() && loadSfz (sfz, *bank, err))
        {
            const auto& tp = tonePresets()[(size_t) tonePresetIndex ("Rock Lead")];
            GuitarRigSettings rig; rig.engine = GuitarEngineKind::Samples; rig.guitar = tp.guitar;
            sets.push_back ({ "Rock Lead (v3 samples)", tp.fx, rig, bank });
        }
    }
    for (const auto& [name, fx, rig, bank] : sets)
    {
        SoloEngine e;
        e.setInstrument (InstrumentId::Guitar);
        e.publishFx (fx);
        e.publishRig (rig);
        e.publishSampleBank (bank);
        e.publishSequence (makePlaybackData (s, 0, true));
        e.prepare (48000, 256);
        std::vector<float> L (256), R (256);
        TransportState t;
        t.valid = true; t.playing = true; t.bpm = 140;
        const int blocks = 60 * 48000 / 256;
        double total = 0, worst = 0;
        int worstBlock = 0;
        std::vector<double> times ((size_t) blocks);
        for (int b = 0; b < blocks; ++b)
        {
            t.ppqPosition = b * 256.0 * 140.0 / 60.0 / 48000.0;
            const auto t0 = std::chrono::steady_clock::now();
            e.process (L.data(), R.data(), 256, t);
            const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
            total += us;
            times[(size_t) b] = us;
            if (us > worst) { worst = us; worstBlock = b; }
        }
        std::sort (times.begin(), times.end());
        auto pct = [&] (double q) { return times[(size_t) std::min ((double) blocks - 1, q * blocks)]; };
        const double blockUs = 256.0 / 48000.0 * 1e6;
        std::printf ("%-20s %9.2f%% %8.1f%% %8.1f%% %9.1f%% %8d %9.0fx\n", name.c_str(), 100.0 * total / blocks / blockUs,
                     100.0 * pct (0.99) / blockUs, 100.0 * pct (0.999) / blockUs, 100.0 * worst / blockUs, worstBlock,
                     blocks * blockUs / total);
    }
    std::printf ("Memoria: tamaño de SoloEngine %zu bytes + buffers (delay 4 s estéreo, reverb, chorus, cuerdas)\n", sizeof (SoloEngine));
    std::printf ("Latencia declarada: v2 0 muestras; v3 ampli 2x %d muestras; 4x %d muestras (a cualquier frecuencia de muestreo)\n",
                 Halfband2x::latencyAtLowRate(), Halfband2x::latencyAtLowRate() * 3 / 2);
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
    if (argc >= 3 && std::strcmp (argv[1], "beat") == 0) return cmdBeat (argc, argv);
    if (argc >= 3 && std::strcmp (argv[1], "synthbeat") == 0) return cmdSynthBeat (argc, argv);
    if (argc >= 2 && std::strcmp (argv[1], "bench") == 0) return cmdBench();
    if (argc >= 3 && std::strcmp (argv[1], "tone-ab") == 0) return cmdToneAb (argc, argv);
    if (argc >= 3 && std::strcmp (argv[1], "makebank") == 0) return cmdMakeBank (argv[2]);
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
                 "  solo_cli synthvoice <salida.wav>\n"
                 "  solo_cli beat <beat.wav> [--ts 4] [--min 60] [--max 200] [--out analisis.txt]   análisis offline del beat (V4)\n"
                 "  solo_cli synthbeat <salida.wav> [--bpm 120] [--key Am] [--prog \"Am F C G\"] [--bars 8] [--drums 0|1] [--harmony 0|1] [--noise -70] [--swing 0.02] [--leadin 0] [--sr 44100] [--channels 1|2]\n"
                 "  solo_cli bench\n  solo_cli tone-ab <carpeta> [--expression 65]   demos v2/v3 con el mismo MIDI a -18 LUFS\n  solo_cli makebank <carpeta>                    banco SFZ sintético de prueba\n  solo_cli calibrate\n  solo_cli command \"<orden en español>\"\n");
    return 1;
}
