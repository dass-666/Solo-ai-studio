// Pruebas unitarias del núcleo de Solo AI Studio (sin dependencias externas).
#include "solo/AudioRenderer.h"
#include "solo/MidiFile.h"
#include "solo/MusicTheory.h"
#include "solo/Playback.h"
#include "solo/SoloComposer.h"
#include "solo/StateManager.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace solo;

#ifndef SOLO_GOLDEN_HASH
#define SOLO_GOLDEN_HASH 4560047695921077447ull
#endif

#include "TestFramework.h"

static ComposerParams baseParams (const char* key = "Am", const char* prog = "Am F C G", double bpm = 146.0)
{
    ComposerParams p;
    CHECK (Key::parse (key, p.key));
    std::string err;
    CHECK (Progression::parse (prog, p.progression, err));
    p.bars = 8;
    p.bpm = bpm;
    // Estas pruebas son la regresión de S0–S2: usan el motor 1 tal como estaba (estilo "Rock").
    p.engine = 1;
    p.legacyStyle = 1;
    p.instrument = InstrumentId::GuitarLite;
    return p;
}

static uint64_t hashSequence (const Sequence& s)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&] (int64_t v) { h ^= (uint64_t) v; h *= 1099511628211ull; };
    for (const auto& n : s.notes) { mix (n.startTick); mix (n.lengthTicks); mix (n.pitch); mix (n.velocity); }
    return h;
}

// ============================================================ MusicTheory
TEST (theory_pitch_classes)
{
    CHECK (parsePitchClass ("C") == 0);
    CHECK (parsePitchClass ("c#") == 1);
    CHECK (parsePitchClass ("Db") == 1);
    CHECK (parsePitchClass ("Bb") == 10);
    CHECK (parsePitchClass ("B#") == 0);
    CHECK (parsePitchClass ("Cb") == 11);
    CHECK (parsePitchClass ("Sol") == 7);
    CHECK (parsePitchClass ("Sib") == 10);
    CHECK (parsePitchClass ("La") == 9);
    CHECK (parsePitchClass ("H") == -1);
    CHECK (midiNoteName (69) == "A4");
    CHECK (midiNoteName (60) == "C4");
}

TEST (theory_keys)
{
    Key k;
    CHECK (Key::parse ("Am", k) && k.tonic == 9 && k.mode == Mode::Minor);
    CHECK (Key::parse ("C", k) && k.tonic == 0 && k.mode == Mode::Major);
    CHECK (Key::parse ("F# minor", k) && k.tonic == 6 && k.mode == Mode::Minor);
    CHECK (Key::parse ("La menor", k) && k.tonic == 9 && k.mode == Mode::Minor);
    CHECK (Key::parse ("Do mayor", k) && k.tonic == 0 && k.mode == Mode::Major);
    CHECK (Key::parse ("Eb", k) && k.tonic == 3 && k.prefersFlats());
    CHECK (! Key::parse ("X", k));
    CHECK (! Key::parse ("Am7", k));
    Key am; Key::parse ("Am", am);
    auto s = am.scalePitchClasses();
    CHECK (s[0] == 9 && s[1] == 11 && s[2] == 0 && s[6] == 7);
    CHECK (am.containsPitchClass (0) && ! am.containsPitchClass (1));
}

TEST (theory_chords)
{
    Chord c;
    CHECK (Chord::parse ("Am", c) && c.root == 9 && c.quality == ChordQuality::Minor);
    CHECK (Chord::parse ("G7", c) && c.root == 7 && c.quality == ChordQuality::Dominant7);
    CHECK (Chord::parse ("Cmaj7", c) && c.quality == ChordQuality::Major7);
    CHECK (Chord::parse ("CM7", c) && c.quality == ChordQuality::Major7);
    CHECK (Chord::parse ("Cm7", c) && c.quality == ChordQuality::Minor7);
    CHECK (Chord::parse ("Bm7b5", c) && c.root == 11 && c.quality == ChordQuality::HalfDiminished7);
    CHECK (Chord::parse ("F#dim", c) && c.root == 6 && c.quality == ChordQuality::Diminished);
    CHECK (Chord::parse ("Dsus4", c) && c.quality == ChordQuality::Sus4);
    CHECK (Chord::parse ("E5", c) && c.quality == ChordQuality::Power);
    CHECK (Chord::parse ("C/E", c) && c.root == 0 && c.quality == ChordQuality::Major);
    CHECK (Chord::parse ("Fadd9", c) && c.root == 5 && c.quality == ChordQuality::Major);
    CHECK (Chord::parse ("Lam", c) && c.root == 9 && c.quality == ChordQuality::Minor);
    CHECK (Chord::parse ("Sol7", c) && c.root == 7 && c.quality == ChordQuality::Dominant7);
    CHECK (! Chord::parse ("Xm", c));
    CHECK (! Chord::parse ("Cfoo", c));
    Chord am; Chord::parse ("Am", am);
    auto pcs = am.pitchClasses();
    CHECK (pcs.size() == 3 && pcs[0] == 9 && pcs[1] == 0 && pcs[2] == 4);
    Key key; Key::parse ("Am", key);
    auto t = am.tensionPitchClasses (key); // B (9ª) y D (11ª) válidas; F es b6 (semitono sobre E) => evitada
    std::set<int> ts (t.begin(), t.end());
    CHECK (ts.count (11) && ts.count (2) && ! ts.count (5));
}

TEST (theory_progressions)
{
    Progression p;
    std::string err;
    CHECK (Progression::parse ("Am F C G", p, err) && p.bars.size() == 4);
    CHECK (Progression::parse ("Am | F | C | G |", p, err) && p.bars.size() == 4);
    CHECK (Progression::parse ("C % F %", p, err) && p.bars.size() == 4 && p.bars[1] == p.bars[0]);
    CHECK (p.chordAtBar (5) == p.bars[1]);
    CHECK (! Progression::parse ("Am Q C", p, err) && ! err.empty());
    CHECK (! Progression::parse ("   ", p, err));
    CHECK (! Progression::parse ("% C", p, err));
    Key key; Key::parse ("C", key);
    Progression::parse ("C F G7 Ab", p, err);
    auto w = p.validate (key);
    CHECK (w.size() == 1); // Ab fuera de Do mayor
}

TEST (theory_nearest_pitch)
{
    CHECK (nearestPitchWithClass (61, { 0, 4, 7 }, 48, 84) == 60);
    CHECK (nearestPitchWithClass (66, { 0, 4, 7 }, 48, 84) == 67);
    CHECK (nearestPitchWithClass (100, { 0 }, 48, 84) == 84);
}

// ============================================================ Composer
TEST (composer_deterministic)
{
    auto p = baseParams();
    p.seed = 42;
    const auto a = SoloComposer::compose (p);
    const auto b = SoloComposer::compose (p);
    CHECK (a == b);
    CHECK (! a.notes.empty());
}

TEST (composer_three_distinct_solos_same_progression)
{
    auto p = baseParams();
    std::set<uint64_t> hashes;
    for (uint64_t seed : { 1ull, 2ull, 3ull })
    {
        p.seed = seed;
        hashes.insert (hashSequence (SoloComposer::compose (p)));
    }
    CHECK (hashes.size() == 3);
}

TEST (composer_golden_hash_cross_platform)
{
    // Valor de referencia calculado en Linux/GCC. Si falla en Windows/MSVC, la semilla
    // no es reproducible entre plataformas y hay que investigarlo.
    auto p = baseParams();
    p.seed = 12345;
    const auto s = SoloComposer::compose (p);
    const uint64_t h = hashSequence (s);
    std::printf ("  hash(Am F C G, 146, seed 12345) = %llu, notas = %zu\n", (unsigned long long) h, s.notes.size());
    CHECK (h == SOLO_GOLDEN_HASH);
}

struct Scenario { const char* key; const char* prog; int ts; int bars; InstrumentId ins; Style st; double e, d, c; double bpm; };

static std::vector<Scenario> scenarios()
{
    std::vector<Scenario> v;
    const char* progs[][2] = { { "Am", "Am F C G" }, { "C", "C G Am F" }, { "A", "A7 D7 A7 E7" }, { "Fm", "Fm Db Ab Eb" },
                               { "Dm", "Dm Gm A7 Dm" }, { "G", "G Em C D" }, { "E", "Emaj7 C#m7 F#m7 B7" } };
    int i = 0;
    for (auto& pr : progs)
        for (int ins = 0; ins < 3; ++ins) // cobertura original de S0–S2 (ver legacy_composer_known_defects_report)
            for (int st = 0; st < 4; ++st) // los 4 estilos del motor 1
            {
                const int ts = (i % 3 == 0) ? 3 : 4;
                const int bars = (int[]) { 2, 4, 8, 16 }[i % 4];
                const double e = (i % 5) / 4.0, d = ((i + 2) % 5) / 4.0, c = ((i + 4) % 5) / 4.0;
                v.push_back ({ pr[0], pr[1], ts, bars, (InstrumentId) ins, (Style) st, e, d, c, 70.0 + (i * 13) % 120 });
                ++i;
            }
    return v;
}

TEST (composer_musical_invariants)
{
    int downbeatNotes = 0, downbeatChordTones = 0;
    int totalNotes = 0;
    for (const auto& sc : scenarios())
        for (uint64_t seed = 1; seed <= 6; ++seed)
        {
            ComposerParams p = baseParams (sc.key, sc.prog, sc.bpm);
            p.timeSignature = { sc.ts, 4 };
            p.bars = sc.bars;
            p.instrument = sc.ins;
            p.style = sc.st;
            p.legacyStyle = (int) sc.st;
            p.energy = sc.e; p.density = sc.d; p.complexity = sc.c;
            p.seed = seed;
            const auto s = SoloComposer::compose (p);
            const auto& prof = profileFor (sc.ins);
            const std::string tag = std::string (sc.prog) + " " + instrumentName (sc.ins) + " " + styleName (sc.st)
                                    + " seed " + std::to_string (seed);
            const int64_t barT = p.timeSignature.ticksPerBar();
            CHECK_MSG (s.lengthTicks() == (int64_t) sc.bars * barT, tag);
            CHECK_MSG (s.notes.size() >= (size_t) sc.bars, tag); // al menos ~1 nota por compás
            totalNotes += (int) s.notes.size();
            int64_t longestGap = 0;
            for (size_t i = 0; i < s.notes.size(); ++i)
            {
                const auto& n = s.notes[i];
                CHECK_MSG (n.startTick >= 0 && n.endTick() <= s.lengthTicks(), tag);
                CHECK_MSG (n.lengthTicks > 0, tag);
                CHECK_MSG (n.startTick % 80 == 0, tag + " rejilla");
                CHECK_MSG (n.pitch >= prof.range.lowest && n.pitch <= prof.range.highest, tag + " rango");
                CHECK_MSG (n.velocity >= 1 && n.velocity <= 127, tag);
                if (i > 0)
                {
                    const auto& prev = s.notes[i - 1];
                    CHECK_MSG (n.startTick >= prev.endTick(), tag + " monofónico");
                    CHECK_MSG (std::abs (n.pitch - prev.pitch) <= prof.maxLeapSemitones + 1, tag + " salto");
                    longestGap = std::max (longestGap, n.startTick - prev.endTick());
                }
                if (n.startTick % barT == 0)
                {
                    ++downbeatNotes;
                    const Chord& ch = p.progression.chordAtBar ((int) (n.startTick / barT));
                    downbeatChordTones += ch.containsPitchClass (n.pitch) ? 1 : 0;
                }
            }
            // Respiración: con 4+ compases debe haber al menos un silencio de media parte.
            if (sc.bars >= 4) CHECK_MSG (longestGap >= kPPQ / 2, tag + " respiración");
            // Resolución final sobre nota estable del último acorde.
            const Chord& last = p.progression.chordAtBar (sc.bars - 1);
            const auto st = last.stablePitchClasses();
            bool stable = false;
            for (int pc : st) stable |= (s.notes.back().pitch % 12) == pc;
            CHECK_MSG (stable, tag + " resolución");
            // Interpretabilidad: notas por segundo.
            const double seconds = (double) s.lengthTicks() / kPPQ * 60.0 / sc.bpm;
            CHECK_MSG ((double) s.notes.size() / seconds <= prof.maxNotesPerSecond * 1.05, tag + " notas/s");
        }
    std::printf ("  %d notas, %d en primer tiempo, %.1f%% notas del acorde\n", totalNotes, downbeatNotes,
                 100.0 * downbeatChordTones / std::max (1, downbeatNotes));
    CHECK (downbeatChordTones >= downbeatNotes * 95 / 100);
}

TEST (composer_density_increases_notes)
{
    auto p = baseParams();
    size_t lowTotal = 0, highTotal = 0;
    for (uint64_t seed = 1; seed <= 10; ++seed)
    {
        p.seed = seed;
        p.density = 0.1; lowTotal += SoloComposer::compose (p).notes.size();
        p.density = 0.9; highTotal += SoloComposer::compose (p).notes.size();
    }
    CHECK (highTotal > lowTotal * 13 / 10);
}

TEST (composer_regenerate_fragment_keeps_rest)
{
    auto p = baseParams();
    p.seed = 7;
    const auto s = SoloComposer::compose (p);
    const auto r = SoloComposer::regenerateBars (s, p, 2, 3, 999);
    const int64_t a = 2 * 3840, b = 4 * 3840;
    std::vector<Note> outsideA, outsideB, insideA, insideB;
    for (auto n : s.notes) { if (n.startTick >= b) outsideA.push_back (n); else if (n.startTick >= a) insideA.push_back (n); }
    for (auto n : r.notes) { if (n.startTick >= b) outsideB.push_back (n); else if (n.startTick >= a) insideB.push_back (n); }
    CHECK (outsideA == outsideB);
    CHECK (! insideB.empty());
    CHECK (insideA != insideB);
    for (auto n : r.notes) CHECK (! (n.startTick < a && n.endTick() > a));
}

TEST (composer_invalid_params_returns_empty)
{
    auto p = baseParams();
    p.timeSignature = { 5, 4 };
    CHECK (! SoloComposer::validate (p).empty());
    CHECK (SoloComposer::compose (p).notes.empty());
}

// ============================================================ MIDI
TEST (midi_roundtrip_format1_and_0)
{
    auto p = baseParams();
    p.seed = 3;
    auto s = SoloComposer::compose (p);
    for (int format : { 0, 1 })
    {
        MidiExportOptions o;
        o.format = format;
        o.key = p.key;
        o.gmProgram = 29;
        const auto bytes = writeMidiFile (s, o);
        ParsedMidi m;
        std::string err;
        CHECK_MSG (readMidiFile (bytes, m, err), err);
        CHECK (m.format == format);
        CHECK (m.numTracks == (format == 0 ? 1 : 2));
        CHECK (m.ppq == kPPQ);
        CHECK (std::abs (m.bpm - 146.0) < 0.01);
        CHECK (m.tsNumerator == 4 && m.tsDenominator == 4);
        CHECK (m.unmatchedNoteOffs == 0 && m.danglingNoteOns == 0);
        CHECK (m.program == 29);
        CHECK (m.notes == s.notes);
    }
}

TEST (midi_varlen_and_long_files)
{
    Sequence s;
    s.bars = 64;
    s.bpm = 90;
    s.timeSignature = { 3, 4 };
    for (int i = 0; i < 64; ++i) s.notes.push_back ({ (int64_t) i * 2880, 2880 - 10, 40 + i % 40, 1 + i, 0 });
    const auto bytes = writeMidiFile (s, {});
    ParsedMidi m;
    std::string err;
    CHECK (readMidiFile (bytes, m, err));
    CHECK (m.tsNumerator == 3);
    CHECK (m.notes == s.notes);
}

TEST (midi_reader_rejects_garbage)
{
    ParsedMidi m;
    std::string err;
    CHECK (! readMidiFile ({ 1, 2, 3 }, m, err));
    auto bytes = writeMidiFile (SoloComposer::compose (baseParams()), {});
    bytes.resize (bytes.size() - 10);
    CHECK (! readMidiFile (bytes, m, err));
}

TEST (sequence_normalise_removes_overlaps)
{
    Sequence s;
    s.bars = 1;
    s.notes = { { 0, 960, 60, 100, 0 }, { 480, 960, 60, 100, 0 }, { 3000, 2000, 62, 200, 0 }, { -10, 100, 64, 0, 0 } };
    s.normalise();
    CHECK (s.notes.size() == 4);
    CHECK (s.notes[0].startTick == 0 && s.notes[0].pitch == 60 && s.notes[0].lengthTicks == 480); // recortada por solape
    CHECK (s.notes[1].startTick == 0 && s.notes[1].pitch == 64 && s.notes[1].lengthTicks == 90 && s.notes[1].velocity == 1);
    CHECK (s.notes[3].endTick() == 3840 && s.notes[3].velocity == 127);
}

// ============================================================ Playback (tiempo real)
static int runPlayer (const Sequence& s, double sr, int block, double bpm, double seconds, bool loop,
                      std::vector<std::pair<int64_t, TimedEvent>>& log, SoloPlayer& player, int64_t offsetTicks = 0)
{
    auto data = makePlaybackData (s, offsetTicks, loop);
    ManualClock clock;
    clock.start();
    std::vector<TimedEvent> ev (2048);
    int64_t pos = 0;
    const int64_t total = (int64_t) (seconds * sr);
    int maxActive = 0;
    while (pos < total)
    {
        const int n = (int) std::min<int64_t> (block, total - pos);
        auto t = clock.next (bpm, n, sr);
        const int c = player.process (t, sr, n, data.get(), ev.data(), (int) ev.size());
        for (int i = 0; i < c; ++i)
        {
            CHECK (ev[(size_t) i].sampleOffset >= 0 && ev[(size_t) i].sampleOffset < n);
            log.push_back ({ pos + ev[(size_t) i].sampleOffset, ev[(size_t) i] });
        }
        maxActive = std::max (maxActive, player.activeNoteCount());
        pos += n;
    }
    return maxActive;
}

TEST (player_sample_accurate_all_block_sizes_and_rates)
{
    auto p = baseParams();
    p.seed = 11;
    const auto s = SoloComposer::compose (p);
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 1, 32, 64, 441, 512, 1024, 2048 })
        {
            SoloPlayer pl;
            std::vector<std::pair<int64_t, TimedEvent>> log;
            const double seconds = (double) s.lengthTicks() / kPPQ * 60.0 / 146.0 + 0.5;
            const int maxActive = runPlayer (s, sr, block, 146.0, seconds, false, log, pl);
            int ons = 0, offs = 0;
            size_t ni = 0;
            bool timingOk = true;
            for (auto& [samplePos, e] : log)
            {
                if (e.type == EventType::NoteOn)
                {
                    const auto& n = s.notes[ni++];
                    const double exact = (double) n.startTick / kPPQ * 60.0 / 146.0 * sr;
                    if (std::abs ((double) samplePos - exact) > 1.0) timingOk = false;
                    if (e.pitch != n.pitch || e.velocity != n.velocity) timingOk = false;
                    ++ons;
                }
                else ++offs;
            }
            const std::string tag = "sr " + std::to_string ((int) sr) + " block " + std::to_string (block);
            CHECK_MSG (ons == (int) s.notes.size(), tag);
            CHECK_MSG (offs == ons, tag);
            CHECK_MSG (timingOk, tag + " precisión de muestra");
            CHECK_MSG (pl.activeNoteCount() == 0, tag);
            CHECK_MSG (maxActive <= 1, tag + " solo monofónico");
        }
}

TEST (player_loop_and_stop_no_hanging_notes)
{
    auto p = baseParams();
    p.bars = 2;
    p.seed = 5;
    const auto s = SoloComposer::compose (p);
    SoloPlayer pl;
    std::vector<std::pair<int64_t, TimedEvent>> log;
    const double cycle = (double) s.lengthTicks() / kPPQ * 60.0 / 146.0;
    runPlayer (s, 48000, 512, 146.0, cycle * 3.0 + 0.01, true, log, pl);
    int ons = 0, offs = 0;
    for (auto& [pos, e] : log) (e.type == EventType::NoteOn ? ons : offs)++;
    CHECK (ons >= (int) s.notes.size() * 3);
    CHECK (ons - offs == pl.activeNoteCount());
    // Stop: el transporte se detiene => todas las notas se cierran.
    std::vector<TimedEvent> ev (256);
    TransportState stopped;
    stopped.valid = true;
    stopped.playing = false;
    auto data = makePlaybackData (s, 0, true);
    pl.process (stopped, 48000, 512, data.get(), ev.data(), 256);
    CHECK (pl.activeNoteCount() == 0);
}

TEST (player_jump_and_tempo_change)
{
    // Una nota larga; el host salta hacia atrás a mitad de la nota y luego cambia el tempo.
    Sequence s;
    s.bars = 2;
    s.notes = { { 0, 3000, 60, 100, 0 }, { 3840, 1920, 64, 90, 0 } };
    auto data = makePlaybackData (s, 0, false);
    SoloPlayer pl;
    std::vector<TimedEvent> ev (256);
    TransportState t;
    t.valid = true; t.playing = true; t.bpm = 120; t.ppqPosition = 0.0;
    int c = pl.process (t, 48000, 512, data.get(), ev.data(), 256);
    CHECK (c == 1 && ev[0].type == EventType::NoteOn && ev[0].sampleOffset == 0);
    CHECK (pl.activeNoteCount() == 1);
    t.ppqPosition = 1.0; // salto (no continúa en 0 + 512 muestras)
    c = pl.process (t, 48000, 512, data.get(), ev.data(), 256);
    CHECK (c == 1 && ev[0].type == EventType::NoteOff);
    CHECK (pl.activeNoteCount() == 0); // la nota iniciada antes del salto no se reinicia a medias
    // Bloques contiguos con cambio de tempo: la nota de compás 2 debe sonar justo en ppq 4.
    t.ppqPosition = 3.9;
    t.bpm = 60;
    pl.process (t, 48000, 512, data.get(), ev.data(), 256); // resincroniza (salto)
    double pos = 3.9 + 512 * 60.0 / 60.0 / 48000.0;
    bool found = false;
    for (int i = 0; i < 400 && ! found; ++i)
    {
        t.ppqPosition = pos;
        c = pl.process (t, 48000, 512, data.get(), ev.data(), 256);
        for (int k = 0; k < c; ++k)
            if (ev[k].type == EventType::NoteOn && ev[k].pitch == 64)
            {
                const double at = pos + ev[k].sampleOffset * 60.0 / 60.0 / 48000.0;
                CHECK (std::abs (at - 4.0) < 1.0 / 48000.0 * 2);
                found = true;
            }
        pos += 512 * 60.0 / 60.0 / 48000.0;
    }
    CHECK (found);
}

TEST (player_sequence_swap_flushes_notes)
{
    Sequence s;
    s.bars = 1;
    s.notes = { { 0, 3000, 60, 100, 0 } };
    auto a = makePlaybackData (s, 0, true);
    auto b = makePlaybackData (s, 0, true);
    SoloPlayer pl;
    std::vector<TimedEvent> ev (64);
    TransportState t;
    t.valid = true; t.playing = true; t.bpm = 120; t.ppqPosition = 0;
    pl.process (t, 48000, 256, a.get(), ev.data(), 64);
    CHECK (pl.activeNoteCount() == 1);
    t.ppqPosition += 256 * 2.0 / 48000.0;
    const int c = pl.process (t, 48000, 256, b.get(), ev.data(), 64);
    CHECK (c >= 1 && ev[0].type == EventType::NoteOff);
    CHECK (pl.activeNoteCount() == 0);
}

TEST (player_host_offset_start_bar)
{
    Sequence s;
    s.bars = 1;
    s.notes = { { 0, 480, 60, 100, 0 } };
    auto d = makePlaybackData (s, 4 * kPPQ * 2, true); // empieza en el compás 3
    SoloPlayer pl;
    std::vector<TimedEvent> ev (64);
    TransportState t;
    t.valid = true; t.playing = true; t.bpm = 120; t.ppqPosition = 7.999;
    const int c = pl.process (t, 48000, 512, d.get(), ev.data(), 64);
    CHECK (c == 1 && ev[0].type == EventType::NoteOn);
    CHECK (ev[0].sampleOffset == (int) std::floor (0.001 / (120.0 / 60.0 / 48000.0)));
    t.ppqPosition = 0.0;
    SoloPlayer pl2;
    CHECK (pl2.process (t, 48000, 512, d.get(), ev.data(), 64) == 0);
}

TEST (realtime_swap)
{
    RealtimeSwap<int> sw;
    CHECK (sw.acquire() == nullptr);
    sw.publish (std::make_unique<int> (1));
    CHECK (*sw.acquire() == 1);
    sw.publish (std::make_unique<int> (2));
    CHECK (*sw.acquire() == 2);
    sw.publish (std::make_unique<int> (3)); // retirado pendiente de GC: el audio espera
    sw.publish (std::make_unique<int> (4)); // reemplaza al pendiente
    CHECK (*sw.acquire() == 4);
}

// ============================================================ Audio
TEST (render_consistency_and_no_clipping)
{
    for (int ins = 0; ins < kNumInstruments; ++ins)
        for (double sr : { 44100.0, 48000.0 })
        {
            auto p = baseParams();
            p.seed = 21;
            p.instrument = (InstrumentId) ins;
            p.energy = 1.0;
            const auto s = SoloComposer::compose (p);
            RenderOptions o;
            o.sampleRate = sr;
            o.instrument = (InstrumentId) ins;
            const auto r = renderSequence (s, o);
            const std::string tag = std::string (instrumentName ((InstrumentId) ins)) + " " + std::to_string ((int) sr);
            CHECK_MSG (r.noteOnsRendered == (int) s.notes.size(), tag);
            CHECK_MSG (r.noteOffsRendered == r.noteOnsRendered, tag);
            CHECK_MSG (r.peak <= 1.0f && r.peak > 0.05f, tag + " pico " + std::to_string (r.peak));
            CHECK_MSG (r.activeVoicesAtEnd == 0, tag + " voces colgadas");
            bool finite = true;
            for (float x : r.left) finite &= std::isfinite (x);
            CHECK_MSG (finite, tag);
            const double expectedLen = (double) s.lengthTicks() / kPPQ * 60.0 / 146.0 + o.tailSeconds;
            CHECK_MSG (std::abs ((double) r.left.size() / sr - expectedLen) < 0.01, tag);
            // Antes de la primera nota: silencio. Justo después: sonido.
            const int64_t first = (int64_t) std::llround ((double) s.notes.front().startTick / kPPQ * 60.0 / 146.0 * sr);
            float before = 0.0f, after = 0.0f;
            for (int64_t i = 0; i < first; ++i) before = std::max (before, std::abs (r.left[(size_t) i]));
            for (int64_t i = first; i < first + (int64_t) (0.05 * sr); ++i) after = std::max (after, std::abs (r.left[(size_t) i]));
            CHECK_MSG (before == 0.0f, tag + " silencio previo");
            CHECK_MSG (after > 0.01f, tag + " ataque");
            // El final de la cola es silencio (sin notas colgadas).
            float tail = 0.0f;
            for (size_t i = r.left.size() - (size_t) (0.1 * sr); i < r.left.size(); ++i) tail = std::max (tail, std::abs (r.left[i]));
            CHECK_MSG (tail < 1e-3f, tag + " cola " + std::to_string (tail));
        }
}

TEST (render_block_size_independent)
{
    auto p = baseParams();
    p.seed = 8;
    p.instrument = InstrumentId::Synth;
    const auto s = SoloComposer::compose (p);
    RenderOptions a, b;
    a.instrument = b.instrument = InstrumentId::Synth;
    a.blockSize = 64;
    b.blockSize = 1024;
    const auto ra = renderSequence (s, a), rb = renderSequence (s, b);
    double maxDiff = 0;
    for (size_t i = 0; i < ra.left.size(); ++i) maxDiff = std::max (maxDiff, (double) std::abs (ra.left[i] - rb.left[i]));
    // Diferencia máxima admitida: cuantización de ±1 muestra en la posición de eventos.
    CHECK_MSG (maxDiff < 2e-3, std::to_string (maxDiff));
}

TEST (wav_header)
{
    std::vector<float> l (100, 0.5f), r (100, -0.5f);
    const auto b = encodeWav24 (l, r, 48000);
    CHECK (b.size() == 44 + 600);
    CHECK (b[0] == 'R' && b[8] == 'W' && b[36] == 'd');
    CHECK (b[22] == 2 && b[34] == 24);
    const uint32_t sr = b[24] | (b[25] << 8) | (b[26] << 16) | ((uint32_t) b[27] << 24);
    CHECK (sr == 48000);
    const int32_t first = (int32_t) ((uint32_t) b[44] | ((uint32_t) b[45] << 8) | ((uint32_t) b[46] << 16) | ((b[46] & 0x80) ? 0xFF000000u : 0));
    CHECK (std::abs (first - 4194304) <= 1);
}

// ============================================================ Estado
TEST (state_roundtrip)
{
    SoloState st;
    st.settings.keyText = "F#m";
    st.settings.progressionText = "F#m D A E";
    st.settings.bars = 16;
    st.settings.instrument = InstrumentId::Flute;
    st.settings.style = Style::ExpressiveBlues;
    st.settings.energy = 0.33;
    st.settings.seed = 18446744073709551615ull;
    st.settings.manualBpm = 146.5;
    st.settings.startBar = 9;
    st.settings.proMode = true;
    auto p = baseParams();
    for (uint64_t seed : { 1ull, 2ull, 3ull }) { p.seed = seed; st.history.push (SoloComposer::compose (p), seed); }
    st.history.back();
    const auto text = serializeState (st);
    SoloState back;
    std::string err;
    CHECK_MSG (deserializeState (text, back, err), err);
    CHECK (back.settings.keyText == "F#m" && back.settings.progressionText == "F#m D A E");
    CHECK (back.settings.bars == 16 && back.settings.instrument == InstrumentId::Flute && back.settings.style == Style::ExpressiveBlues);
    CHECK (std::abs (back.settings.energy - 0.33) < 1e-9 && back.settings.seed == 18446744073709551615ull);
    CHECK (back.settings.manualBpm == 146.5 && back.settings.startBar == 9 && back.settings.proMode);
    CHECK (back.history.size() == 3 && back.history.currentIndex() == 1);
    for (int i = 0; i < 3; ++i) CHECK (back.history.all()[(size_t) i].sequence == st.history.all()[(size_t) i].sequence);
    CHECK (serializeState (back) == text);
}

TEST (state_rejects_corrupt_and_future)
{
    SoloState st;
    auto p = baseParams();
    st.history.push (SoloComposer::compose (p), 1);
    const auto text = serializeState (st);
    SoloState out;
    out.settings.keyText = "SENTINEL";
    std::string err;
    CHECK (! deserializeState (text.substr (0, text.size() / 2), out, err));
    CHECK (out.settings.keyText == "SENTINEL"); // sin aplicar a medias
    auto future = text;
    future.replace (future.find (" 2\n"), 3, " 99\n");
    CHECK (! deserializeState (future, out, err));
    CHECK (! deserializeState ("basura", out, err));
    auto badNote = text;
    const auto pos = badNote.find ("\nn ");
    badNote.replace (pos, 3, "\nn -5 ");
    CHECK (! deserializeState (badNote, out, err));
    auto badRange = text;
    badRange.replace (badRange.find ("bars=8"), 6, "bars=999");
    CHECK (! deserializeState (badRange, out, err));
}

TEST (presets_roundtrip_and_factory_valid)
{
    for (const auto& fp : factoryPresets())
    {
        const auto txt = serializePreset (fp.settings, fp.name);
        SoloSettings s;
        std::string name, err;
        CHECK_MSG (deserializePreset (txt, s, name, err), err);
        CHECK (name == fp.name);
        ComposerParams cp;
        CHECK_MSG (s.toComposerParams (120.0, cp, err), std::string (fp.name) + ": " + err);
        CHECK (! SoloComposer::compose (cp).notes.empty());
    }
}

TEST (variation_history)
{
    VariationHistory h;
    Sequence s;
    for (int i = 0; i < 40; ++i) { s.bpm = 100 + i; h.push (s, (uint64_t) i); }
    CHECK (h.size() == VariationHistory::kMaxVariations);
    CHECK (h.currentSeed() == 39);
    h.back(); h.back();
    CHECK (h.currentSeed() == 37);
    h.push (s, 100); // añade al final sin perder las demás
    CHECK (h.size() == VariationHistory::kMaxVariations && h.currentSeed() == 100);
    CHECK (h.canGoBack() && ! h.canGoForward());
}

int main (int argc, char** argv)
{
    return solo_test::runAll (argc, argv);
}
