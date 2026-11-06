// Pruebas V4-A: Beat Analyzer con beats sintéticos de BPM y tonalidad conocidos.
// Lo que se valida aquí: señales sintéticas. NO demuestra precisión con mezclas comerciales.
#include "TestFramework.h"
#include "solo/BeatAnalyzer.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace solo;

namespace
{
std::string tempFile (const char* name) { return (std::filesystem::temp_directory_path() / name).string(); }

SyntheticBeatSpec spec (double bpm, const char* keyText, const char* prog, int bars = 8)
{
    SyntheticBeatSpec s;
    s.bpm = bpm; s.progression = prog; s.bars = bars;
    Key k; if (Key::parse (keyText, k)) s.key = k;
    return s;
}
bool near (double a, double b, double tol) { return std::abs (a - b) <= tol; }
bool bpmMatches (double got, double expected) { return near (got, expected, expected * 0.02); }
std::string showCands (const BeatAnalysisResult& r)
{
    std::string s;
    for (const auto& c : r.bpmCandidates) { char b[64]; std::snprintf (b, sizeof (b), "%.1f:%.2f ", c.bpm, c.score); s += b; }
    return s;
}
} // namespace

TEST (beat_fft_matches_dft_for_sine)
{
    std::vector<float> re (64), im (64, 0);
    for (int i = 0; i < 64; ++i) re[i] = (float) std::sin (2.0 * 3.14159265358979 * 5.0 * i / 64.0);
    fftRadix2 (re, im);
    double mag5 = std::sqrt (re[5] * re[5] + im[5] * im[5]);
    CHECK (near (mag5, 32.0, 0.01));
    double others = 0;
    for (int b = 0; b < 32; ++b) if (b != 5) others = std::max (others, (double) std::sqrt (re[b] * re[b] + im[b] * im[b]));
    CHECK (others < 1e-3);
}

TEST (beat_bpm_detection_across_tempos)
{
    for (double bpm : { 80.0, 100.0, 120.0, 146.0, 170.0 })
    {
        const auto a = synthesiseBeat (spec (bpm, "Am", "Am F C G"));
        const auto r = analyseBeat (a);
        CHECK_MSG (r.ok, r.error);
        CHECK_MSG (bpmMatches (r.bpm, bpm), "bpm " + std::to_string (bpm) + " -> " + std::to_string (r.bpm) + " cands " + showCands (r));
        CHECK_MSG (r.bpmConfidence >= 0.5, "confianza " + std::to_string (r.bpmConfidence) + " a " + std::to_string (bpm));
    }
}

TEST (beat_bpm_slow_tempo_half_octave_candidate_listed)
{
    // A 65 BPM el peine puede preferir 130 (preferencia por tempos humanos); exigimos que el BPM real
    // esté entre los candidatos y que el elegido sea él o su doble, con aviso de octava.
    const auto r = analyseBeat (synthesiseBeat (spec (65.0, "C", "C G Am F")));
    bool listed = false;
    for (const auto& c : r.bpmCandidates) if (bpmMatches (c.bpm, 65.0)) listed = true;
    CHECK_MSG (listed, showCands (r));
    CHECK_MSG (bpmMatches (r.bpm, 65.0) || bpmMatches (r.bpm, 130.0), std::to_string (r.bpm));
}

TEST (beat_beat_grid_positions_are_regular_and_on_time)
{
    const auto r = analyseBeat (synthesiseBeat (spec (120.0, "Am", "Am F C G")));
    CHECK (r.beatsSec.size() >= 30);
    CHECK (r.bars >= 7);
    // Pulsos cada 0.5 s (±15 ms) empezando cerca de 0.
    CHECK_MSG (near (r.firstBeatSec, 0.0, 0.03), std::to_string (r.firstBeatSec));
    for (size_t i = 1; i < r.beatsSec.size(); ++i)
        CHECK (near (r.beatsSec[i] - r.beatsSec[i - 1], 0.5, 0.015));
}

TEST (beat_lead_in_silence_shifts_first_beat)
{
    auto s = spec (120.0, "Am", "Am F C G"); s.leadInSec = 1.0;
    const auto r = analyseBeat (synthesiseBeat (s));
    CHECK_MSG (bpmMatches (r.bpm, 120.0), std::to_string (r.bpm));
    CHECK_MSG (near (r.firstBeatSec, 1.0, 0.05), std::to_string (r.firstBeatSec));
}

TEST (beat_key_detection_major_and_minor)
{
    struct Case { const char* key; const char* prog; };
    const Case cases[] = { { "Am", "Am F C G" }, { "C", "C F G C" }, { "E", "E A B E" }, { "Dm", "Dm Bb F C" }, { "G", "G D Em C" }, { "F#m", "F#m D A E" } };
    for (const auto& c : cases)
    {
        const auto r = analyseBeat (synthesiseBeat (spec (120.0, c.key, c.prog)));
        Key expected; Key::parse (c.key, expected);
        CHECK_MSG (r.tonal, std::string ("no tonal: ") + c.key);
        CHECK_MSG (r.key == expected, std::string (c.key) + " -> " + r.key.toString() + " conf " + std::to_string (r.keyConfidence));
        CHECK_MSG (r.keyConfidence >= 0.5, std::string (c.key) + " confianza " + std::to_string (r.keyConfidence));
    }
}

TEST (beat_chords_per_bar_simple_progression)
{
    const auto r = analyseBeat (synthesiseBeat (spec (120.0, "Am", "Am F C G")));
    CHECK (! r.harmony.empty());
    Progression expected; std::string err; Progression::parse ("Am F C G Am F C G", expected, err);
    int correct = 0;
    for (int b = 0; b < std::min (r.bars, 8); ++b)
        if (r.harmony.chordAtBar (b) == expected.chordAtBar (b)) ++correct;
    CHECK_MSG (correct >= 7, "acordes correctos " + std::to_string (correct) + "/8: " + r.harmony.toString());
    CHECK_MSG (r.chordConfidence >= 0.4, std::to_string (r.chordConfidence));
    // Cambios armónicos: cada compás cambia (1..7).
    CHECK_MSG (r.harmonicChanges.size() >= 6, std::to_string (r.harmonicChanges.size()));
}

TEST (beat_harmonic_changes_only_where_chord_changes)
{
    const auto r = analyseBeat (synthesiseBeat (spec (110.0, "C", "C C C G G Am Am F")));
    CHECK (r.bars >= 8);
    const auto ch = r.harmonicChanges;
    // Cambios esperados en 3, 5, 7. Toleramos un error de un compás aislado.
    int hits = 0;
    for (int b : { 3, 5, 7 }) for (int c : ch) if (c == b) ++hits;
    CHECK_MSG (hits >= 2, r.harmony.toString());
    CHECK_MSG (ch.size() <= 4, "demasiados cambios: " + std::to_string (ch.size()) + " " + r.harmony.toString());
}

TEST (beat_chords_without_drums_and_drums_without_chords)
{
    auto sh = spec (120.0, "Am", "Am F C G"); sh.drums = false;
    const auto rh = analyseBeat (synthesiseBeat (sh));
    CHECK (rh.tonal);
    Key am; Key::parse ("Am", am);
    CHECK_MSG (rh.key == am, rh.key.toString());

    auto sd = spec (120.0, "Am", "Am F C G"); sd.harmony = false;
    const auto rd = analyseBeat (synthesiseBeat (sd));
    CHECK_MSG (rd.ok && ! rd.silent, rd.error);
    CHECK_MSG (bpmMatches (rd.bpm, 120.0), std::to_string (rd.bpm));
    CHECK_MSG (rd.percussiveOnly, "un beat solo de batería no debe proponer tonalidad");
    CHECK (rd.harmony.empty());
    bool warned = false;
    for (const auto& w : rd.warnings) if (w.find ("percusivo") != std::string::npos) warned = true;
    CHECK (warned);
}

TEST (beat_silent_file_is_reported_not_analysed)
{
    MonoAudio a; a.sampleRate = 44100; a.samples.assign (44100 * 4, 0.0f);
    const auto r = analyseBeat (a);
    CHECK (r.ok);
    CHECK (r.silent);
    CHECK (r.bpm == 0);
    CHECK (! r.tonal);
    CHECK (! r.warnings.empty());
    CHECK (r.summary() == "Silencio");
}

TEST (beat_corrupt_and_empty_inputs_fail_cleanly)
{
    std::string err;
    MonoAudio out;
    CHECK (! decodeWav ({ 'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E' }, out, err));
    std::vector<uint8_t> garbage (1000, 0x5A);
    CHECK (! decodeWav (garbage, out, err));
    // WAV válido truncado a la mitad del bloque de datos: debe fallar o decodificar lo que hay, pero no romper.
    auto bytes = encodeWav16 (synthesiseBeat (spec (120.0, "Am", "Am", 2)));
    bytes.resize (bytes.size() / 2);
    MonoAudio part;
    const bool okPart = decodeWav (bytes, part, err);
    if (okPart) CHECK (analyseBeat (part).ok);
    const auto rf = analyseBeatFile (tempFile ("solo_no_existe_beat.wav"));
    CHECK (! rf.ok);
    CHECK (! rf.error.empty());
    MonoAudio empty; empty.sampleRate = 44100;
    CHECK (! analyseBeat (empty).ok);
    MonoAudio badRate = synthesiseBeat (spec (120.0, "Am", "Am", 2)); badRate.sampleRate = 0;
    CHECK (! analyseBeat (badRate).ok);
}

TEST (beat_wav_roundtrip_stereo_and_sample_rates)
{
    for (double sr : { 22050.0, 44100.0, 48000.0, 96000.0 })
    {
        auto s = spec (128.0, "Em", "Em C G D"); s.sampleRate = sr;
        const auto a = synthesiseBeat (s);
        const std::string path = tempFile ("solo_beat_rt.wav");
        CHECK (writeWavFile (path, a, sr == 48000.0 ? 2 : 1));
        const auto r = analyseBeatFile (path);
        CHECK_MSG (r.ok, r.error);
        CHECK_MSG (bpmMatches (r.bpm, 128.0), std::to_string (sr) + " -> " + std::to_string (r.bpm));
        Key em; Key::parse ("Em", em);
        CHECK_MSG (r.key == em, std::to_string (sr) + " -> " + r.key.toString());
        CHECK (near (r.sampleRate, sr, 0.5));
        std::remove (path.c_str());
    }
}

TEST (beat_analysis_is_deterministic)
{
    const auto a = synthesiseBeat (spec (146.0, "Am", "Am F C G"));
    const auto r1 = analyseBeat (a), r2 = analyseBeat (a);
    CHECK (r1.bpm == r2.bpm);
    CHECK (r1.bpmConfidence == r2.bpmConfidence);
    CHECK (r1.key == r2.key && r1.keyConfidence == r2.keyConfidence);
    CHECK (r1.beatsSec == r2.beatsSec);
    CHECK (serializeBeatAnalysis (r1) == serializeBeatAnalysis (r2));
}

TEST (beat_long_file_limits_and_timing)
{
    auto s = spec (120.0, "Am", "Am F C G", 64); // 2:08 min
    const auto a = synthesiseBeat (s);
    const auto t0 = std::chrono::steady_clock::now();
    const auto r = analyseBeat (a);
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    CHECK_MSG (r.ok, r.error);
    CHECK (r.bars >= 63);
    CHECK_MSG (ms < 8000.0, "tardó " + std::to_string (ms) + " ms");
    std::printf ("  análisis de %.0f s de audio: %.0f ms\n", a.durationSec(), ms);
    BeatAnalysisOptions o; o.maxDurationSec = 60.0;
    const auto rl = analyseBeat (a, o);
    CHECK (! rl.ok);
    CHECK (rl.error.find ("dura más") != std::string::npos);
}

TEST (beat_energy_regions_follow_dynamics)
{
    // Primera mitad sin batería (baja), segunda con batería (alta): concatenamos dos beats.
    auto quiet = spec (120.0, "Am", "Am F C G", 4); quiet.drums = false;
    auto loud = spec (120.0, "Am", "Am F C G", 4);
    auto a = synthesiseBeat (quiet);
    const auto b = synthesiseBeat (loud);
    a.samples.resize ((size_t) (8.0 * a.sampleRate));
    for (auto& v : a.samples) v *= 0.3f; // -10 dB: intro suave
    a.samples.insert (a.samples.end(), b.samples.begin(), b.samples.end());
    const auto r = analyseBeat (a);
    CHECK_MSG (r.ok && r.bars >= 7, std::to_string (r.bars));
    CHECK (! r.energyRegions.empty());
    // Debe haber al menos una región de menor nivel antes que una de mayor nivel.
    bool lowThenHigh = false;
    for (size_t i = 0; i + 1 < r.energyRegions.size(); ++i)
        if ((int) r.energyRegions[i].level < (int) r.energyRegions[i + 1].level) lowThenHigh = true;
    CHECK (lowThenHigh);
    CHECK (r.barRmsDb.size() == (size_t) r.bars);
}

TEST (beat_result_serialization_roundtrip)
{
    const auto r = analyseBeat (synthesiseBeat (spec (146.0, "Am", "Am F C G")));
    const std::string text = serializeBeatAnalysis (r);
    BeatAnalysisResult back; std::string err;
    CHECK_MSG (deserializeBeatAnalysis (text, back, err), err);
    CHECK (near (back.bpm, r.bpm, 1e-3));
    CHECK (near (back.bpmConfidence, r.bpmConfidence, 1e-4));
    CHECK (back.key == r.key);
    CHECK (back.tonal == r.tonal);
    CHECK (back.beatsSec.size() == r.beatsSec.size());
    CHECK (back.bars == r.bars);
    CHECK (back.harmony.toString() == r.harmony.toString());
    CHECK (back.energyRegions.size() == r.energyRegions.size());
    CHECK (back.warnings == r.warnings);
    CHECK (back.harmonicChanges == r.harmonicChanges);
    // Texto ajeno / vacío
    BeatAnalysisResult none;
    CHECK (! deserializeBeatAnalysis ("style=1\nbars=8\n", none, err));
    CHECK (! deserializeBeatAnalysis ("beat.version=99\n", none, err));
}

TEST (beat_harmony_timeline_manual_edit_and_provenance)
{
    Progression p; std::string err; Progression::parse ("Am F C G", p, err);
    Key am; Key::parse ("Am", am);
    auto h = HarmonyTimeline::fromManual (am, p, 120.0);
    CHECK (h.bars == 4 && h.segments.size() == 4);
    CHECK (h.toString() == "Am | F | C | G");
    Chord dm; Chord::parse ("Dm", dm);
    h.setChord (1, 2, dm);
    CHECK_MSG (h.toString() == "Am | Dm | Dm | G", h.toString());
    CHECK (h.segments.size() == 3);
    CHECK (h.chordConfidenceAtBar (1) == 1.0 && h.segments[1].provenance == Provenance::Confirmed);
    CHECK (h.toProgression().bars.size() == 4);
    CHECK (h.changeBars() == std::vector<int> ({ 1, 3 }));
    // Partir un segmento largo por el medio
    auto g = HarmonyTimeline::fromManual (am, p, 120.0);
    g.setChord (0, 3, dm);
    CHECK (g.segments.size() == 1);
    Chord e; Chord::parse ("E", e);
    g.setChord (1, 1, e, Provenance::Estimated);
    CHECK_MSG (g.toString() == "Am | E | Am | G" || g.toString() == "Dm | E | Dm | Dm", g.toString());
    CHECK (g.segments.size() == 3);
    const std::string text = serializeHarmony (g);
    HarmonyTimeline back;
    CHECK_MSG (deserializeHarmony (text, back, err), err);
    CHECK (back.toString() == g.toString());
    CHECK (back.segments[1].provenance == Provenance::Estimated);
    CHECK (back.key == am);
}

// ---------------------------------------------------------------- proyecto y estado
#include "solo/Project.h"

TEST (beat_project_import_confirm_and_apply_to_settings)
{
    ProjectController p;
    const auto audio = synthesiseBeat (spec (146.0, "Am", "Am F C G"));
    CHECK (! p.hasBeat());
    CHECK (! p.confirmBeatBpm (120).ok);
    const auto r = p.importBeat (audio, "beat_demo.wav");
    CHECK_MSG (r.ok, r.message);
    CHECK (p.hasBeat());
    CHECK (near (p.state.beat.harmony.bpm, 146.0, 3.0));
    CHECK (p.state.beat.harmony.bpmProvenance == Provenance::Estimated);
    // Confirmaciones del usuario
    CHECK (p.confirmBeatBpm (146.0).ok);
    CHECK (p.state.beat.harmony.bpm == 146.0 && p.state.beat.harmony.bpmProvenance == Provenance::Confirmed);
    Key am; Key::parse ("Am", am);
    CHECK (p.confirmBeatKey (am).ok);
    Chord dm; Chord::parse ("Dm", dm);
    CHECK (p.setBeatChord (1, 1, dm).ok);
    CHECK (p.state.beat.harmony.chordAtBar (1) == dm);
    CHECK (p.state.beat.harmony.chordConfidenceAtBar (1) == 1.0);
    CHECK (! p.setBeatChord (0, 99, dm).ok);
    // Sección y ajustes del compositor
    CHECK (p.setBeatSection (4, 7).ok);
    CHECK (p.state.beat.sectionBars() == 4);
    CHECK (! p.setBeatSection (-1, 2).ok);
    const auto a = p.applyBeatToSettings();
    CHECK_MSG (a.ok, a.message);
    CHECK (p.state.settings.bars == 4);
    CHECK (p.state.settings.keyText == "Am");
    CHECK_MSG (p.state.settings.progressionText == "Am F C G", p.state.settings.progressionText);
    CHECK (p.state.settings.manualBpm == 146.0 && ! p.state.settings.useHostTempo);
    // El compositor sigue funcionando con esos ajustes
    p.bpm = 146.0;
    CHECK (p.generate (7).ok);
    CHECK (p.current() != nullptr && p.current()->bars == 4);
    // Quitar el beat no borra lo generado
    CHECK (p.clearBeat().ok);
    CHECK (! p.hasBeat() && p.hasSequence());
}

TEST (beat_project_manual_chords_for_percussive_beat)
{
    ProjectController p;
    auto s = spec (100.0, "Am", "Am", 8); s.harmony = false;
    const auto r = p.importBeat (synthesiseBeat (s), "drums.wav");
    CHECK_MSG (r.ok, r.message);
    CHECK (p.state.beat.analysis.percussiveOnly);
    CHECK (p.state.beat.harmony.segments.empty());
    CHECK (! p.applyBeatToSettings().ok); // sin acordes no hay nada que aplicar
    Key em; Key::parse ("Em", em);
    CHECK (p.confirmBeatKey (em).ok);
    Progression prog; std::string err; Progression::parse ("Em C G D", prog, err);
    CHECK (p.setBeatChords (prog).ok);
    CHECK (p.state.beat.harmony.bars >= 7);
    CHECK (p.state.beat.harmony.toProgression().bars.size() == (size_t) p.state.beat.harmony.bars);
    CHECK (p.applyBeatToSettings().ok);
    CHECK (p.state.settings.keyText == "Em");
    CHECK (p.state.settings.progressionText.rfind ("Em C G D", 0) == 0);
    p.bpm = 100;
    CHECK (p.generate (3).ok);
}

TEST (beat_project_state_persists_and_old_states_still_load)
{
    ProjectController p;
    CHECK (p.importBeat (synthesiseBeat (spec (128.0, "C", "C G Am F")), "c:/beats/mi beat.wav").ok);
    CHECK (p.confirmBeatBpm (128.0).ok);
    CHECK (p.setBeatSection (2, 5).ok);
    p.bpm = 128; CHECK (p.generate (1).ok);
    const std::string text = serializeState (p.state);
    CHECK (text.find ("beatPath=c:/beats/mi beat.wav") != std::string::npos);
    CHECK (text.find ("beat.tempo=") != std::string::npos);
    CHECK (text.find ("harm.seg=") != std::string::npos);
    SoloState back; std::string err;
    CHECK_MSG (deserializeState (text, back, err), err);
    CHECK (back.beat.loaded());
    CHECK (back.beat.path == "c:/beats/mi beat.wav");
    CHECK (back.beat.harmony.bpm == 128.0 && back.beat.harmony.bpmProvenance == Provenance::Confirmed);
    CHECK (back.beat.sectionFromBar == 2 && back.beat.sectionToBar == 5);
    CHECK (back.beat.harmony.toString() == p.state.beat.harmony.toString());
    CHECK (back.beat.analysis.beatsSec.size() == p.state.beat.analysis.beatsSec.size());
    CHECK (back.history.size() == 1);
    // Un estado sin beat (v3 y anteriores) carga igual y sin beat.
    ProjectController q; q.bpm = 120; CHECK (q.generate (1).ok);
    const std::string old = serializeState (q.state);
    CHECK (old.find ("beat.") == std::string::npos);
    SoloState b2;
    CHECK (deserializeState (old, b2, err));
    CHECK (! b2.beat.loaded());
    // Un estado con líneas de beat corruptas carga el proyecto sin beat (no invalida todo).
    std::string corrupt = old;
    corrupt.insert (corrupt.find ("lockedBars="), "beat.version=1\nbeat.tempo=abc\n");
    SoloState b3;
    CHECK_MSG (deserializeState (corrupt, b3, err), err);
}

TEST (beat_robustness_swing_noise_and_tempo_sweep)
{
    // Tabla de precisión (señales sintéticas): caja atrasada 25 ms (backbeat "laid-back"), ruido a -30 dB,
    // barrido de tempos. Limitación conocida y documentada: por encima de ~170 BPM con la caja atrasada el
    // analizador puede elegir el medio tempo; en ese caso el tempo real debe figurar entre los candidatos
    // y la confianza no debe ser alta.
    int okBpm = 0, okKey = 0, total = 0;
    for (double bpm : { 70.0, 92.0, 105.0, 128.0, 140.0, 155.0, 174.0, 185.0 })
    {
        auto s = spec (bpm, "Em", "Em C G D"); s.swingSec = 0.025; s.noiseDb = -30.0; s.seed = (uint64_t) bpm;
        const auto r = analyseBeat (synthesiseBeat (s));
        Key em; Key::parse ("Em", em);
        const bool b = bpmMatches (r.bpm, bpm), k = r.key == em;
        okBpm += b; okKey += k; ++total;
        std::printf ("  %5.1f BPM -> %6.2f (conf %.2f) %s | tonalidad %s (conf %.2f) %s | acordes %s\n", bpm, r.bpm, r.bpmConfidence, b ? "ok" : "MAL",
                     r.key.toString().c_str(), r.keyConfidence, k ? "ok" : "MAL", r.harmony.toString().c_str());
        if (! b)
        {
            bool listed = false;
            for (const auto& c : r.bpmCandidates) if (bpmMatches (c.bpm, bpm)) listed = true;
            CHECK_MSG (listed, "el tempo real no está entre los candidatos a " + std::to_string (bpm));
            CHECK_MSG (r.bpmConfidence <= 0.6, "confianza alta con tempo equivocado a " + std::to_string (bpm));
            CHECK_MSG (bpmMatches (r.bpm, bpm / 2), "error que no es de octava a " + std::to_string (bpm));
        }
    }
    CHECK_MSG (okBpm >= 6, "BPM correctos " + std::to_string (okBpm) + "/" + std::to_string (total));
    CHECK_MSG (okKey >= total - 2, "tonalidades correctas " + std::to_string (okKey) + "/" + std::to_string (total));
}

TEST (beat_three_four_meter_option)
{
    auto s = spec (120.0, "G", "G C D G"); s.beatsPerBar = 3;
    BeatAnalysisOptions o; o.beatsPerBar = 3;
    const auto r = analyseBeat (synthesiseBeat (s), o);
    CHECK_MSG (bpmMatches (r.bpm, 120.0), std::to_string (r.bpm));
    CHECK (r.beatsPerBar == 3);
    CHECK_MSG (r.bars >= 7, std::to_string (r.bars));
    CHECK (r.harmony.beatsPerBar == 3);
}
