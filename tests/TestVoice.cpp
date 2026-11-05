// Pruebas de Hum-to-Solo / Vocal-to-Solo con voces SINTÉTICAS (no se ha probado con voces reales).
#include "TestFramework.h"
#include "solo/AudioRenderer.h"
#include "solo/VoiceAnalysis.h"

#include <cmath>
#include <cstring>

using namespace solo;

namespace
{
// Melodía de referencia a 100 BPM (corchea = 0.3 s), con pausas, legato, glides y vibrato.
std::vector<SynthVoiceNote> melody (double transpose = 0)
{
    const double e = 0.3;
    std::vector<SynthVoiceNote> v = {
        { 0.5, 2 * e - 0.05, 57 },             // A3
        { 0.5 + 2 * e, e - 0.03, 60 },         // C4
        { 0.5 + 3 * e, e, 62 },                // D4 (ligada a la siguiente)
        { 0.5 + 4 * e, 3 * e, 64, -2.0, 0.3 }, // E4 con glide desde abajo y vibrato
        { 0.5 + 8 * e, e - 0.04, 62 },         // tras una pausa
        { 0.5 + 9 * e, e - 0.04, 60 },
        { 0.5 + 10 * e, 2 * e - 0.05, 59 },
        { 0.5 + 12 * e, 4 * e, 57, 0, 0.35 },  // nota larga final
    };
    for (auto& n : v) n.midi += transpose;
    return v;
}

struct Match { int matched = 0, total = 0; double medOnsetErr = 0, maxPitchErr = 0; };

Match compare (const std::vector<SynthVoiceNote>& truth, const VoiceAnalysis& va)
{
    Match m;
    std::vector<double> errs;
    for (const auto& t : truth)
    {
        ++m.total;
        for (const auto& n : va.notes)
        {
            if (std::abs (n.startSec - t.startSec) < 0.06 && n.pitched && std::abs (n.midi - t.midi) < 0.5)
            {
                ++m.matched;
                errs.push_back (std::abs (n.startSec - t.startSec));
                m.maxPitchErr = std::max (m.maxPitchErr, std::abs (n.midi - t.midi));
                break;
            }
        }
    }
    if (! errs.empty())
    {
        std::sort (errs.begin(), errs.end());
        m.medOnsetErr = errs[errs.size() / 2];
    }
    return m;
}

std::vector<uint8_t> encodeWav (const std::vector<float>& mono, int sr, int bits, int channels, bool isFloat)
{
    std::vector<uint8_t> b;
    auto s = [&] (const char* t) { b.insert (b.end(), t, t + 4); };
    auto u32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) (v >> (8 * i))); };
    auto u16 = [&] (uint16_t v) { b.push_back ((uint8_t) v); b.push_back ((uint8_t) (v >> 8)); };
    const uint32_t bytesPer = (uint32_t) bits / 8, dataBytes = (uint32_t) mono.size() * bytesPer * (uint32_t) channels;
    s ("RIFF"); u32 (36 + 12 + dataBytes); s ("WAVE");
    s ("LIST"); u32 (4); s ("INFO"); // chunk desconocido que hay que saltar
    s ("fmt "); u32 (16); u16 (isFloat ? 3 : 1); u16 ((uint16_t) channels); u32 ((uint32_t) sr);
    u32 ((uint32_t) sr * bytesPer * (uint32_t) channels); u16 ((uint16_t) (bytesPer * (uint32_t) channels)); u16 ((uint16_t) bits);
    s ("data"); u32 (dataBytes);
    for (float x : mono)
        for (int c = 0; c < channels; ++c)
        {
            if (isFloat) { uint8_t t[4]; std::memcpy (t, &x, 4); b.insert (b.end(), t, t + 4); }
            else if (bits == 16) { const int16_t v = (int16_t) std::lround (std::clamp (x, -1.0f, 1.0f) * 32767.0f); u16 ((uint16_t) v); }
            else { const int32_t v = (int32_t) std::lround (std::clamp (x, -1.0f, 1.0f) * 8388607.0f); b.push_back ((uint8_t) v); b.push_back ((uint8_t) (v >> 8)); b.push_back ((uint8_t) (v >> 16)); }
        }
    return b;
}

ComposerParams cParams()
{
    ComposerParams p;
    Key::parse ("Am", p.key);
    std::string err;
    Progression::parse ("Am F C G", p.progression, err);
    p.bars = 4;
    p.bpm = 100;
    p.instrument = InstrumentId::Guitar;
    p.style = Style::Emotional;
    p.seed = 3;
    return p;
}
} // namespace

TEST (voice_pitch_and_rhythm_on_synthetic_hum)
{
    for (double transpose : { 0.0, 12.0 }) // voz grave y voz aguda
        for (double sr : { 22050.0, 44100.0, 48000.0 })
        {
            const auto truth = melody (transpose);
            const auto audio = synthesiseVoice (truth, sr, -50.0, 5);
            const auto va = analyseVoice (audio);
            const auto m = compare (truth, va);
            const std::string tag = "transp " + std::to_string ((int) transpose) + " sr " + std::to_string ((int) sr);
            std::printf ("  %s: %d/%d notas, detectadas %zu, error de ataque mediano %.0f ms, error de altura máx %.2f st, confianza %.2f\n",
                         tag.c_str(), m.matched, m.total, va.notes.size(), m.medOnsetErr * 1000, m.maxPitchErr, va.meanConfidence);
            CHECK_MSG (m.matched >= 7, tag);
            CHECK_MSG (std::abs ((int) va.notes.size() - (int) truth.size()) <= 1, tag);
            CHECK_MSG (m.medOnsetErr < 0.03, tag);
            CHECK_MSG (va.pitchReliable, tag);
            // Glide de entrada en la 4.ª nota (desde 2 semitonos por debajo)
            bool glide = false;
            for (const auto& n : va.notes)
                if (std::abs (n.midi - truth[3].midi) < 0.5 && std::abs (n.startSec - truth[3].startSec) < 0.1) glide = n.glideInSemis <= -0.7;
            CHECK_MSG (glide, tag + " glide");
        }
}

TEST (voice_robust_to_noise)
{
    const auto truth = melody();
    for (double noiseDb : { -40.0, -30.0 })
    {
        const auto va = analyseVoice (synthesiseVoice (truth, 44100, noiseDb, 9));
        const auto m = compare (truth, va);
        std::printf ("  ruido %.0f dBFS: %d/%d notas (detectadas %zu)\n", noiseDb, m.matched, m.total, va.notes.size());
        CHECK (m.matched >= 6);
    }
}

TEST (voice_wav_formats)
{
    const auto truth = melody();
    const auto audio = synthesiseVoice (truth, 44100, -50.0, 2);
    const auto ref = analyseVoice (audio);
    struct F { int bits, ch; bool fl; };
    for (const F f : { F { 16, 1, false }, F { 16, 2, false }, F { 24, 2, false }, F { 32, 1, true } })
    {
        MonoAudio dec;
        std::string err;
        CHECK_MSG (decodeWav (encodeWav (audio.samples, 44100, f.bits, f.ch, f.fl), dec, err), err);
        CHECK (dec.sampleRate == 44100 && dec.samples.size() == audio.samples.size() && dec.sourceChannels == f.ch);
        const auto va = analyseVoice (dec);
        CHECK (va.notes.size() == ref.notes.size());
    }
    // Formatos rechazados con mensaje claro
    MonoAudio dec;
    std::string err;
    CHECK (! decodeWav ({ 'R', 'I', 'F', 'F' }, dec, err) && ! err.empty());
    auto bad = encodeWav (audio.samples, 44100, 16, 1, false);
    bad[20 + 12] = 2; // fmtTag = 2 (ADPCM)
    CHECK (! decodeWav (bad, dec, err));
    // Fuzzing: nunca falla de forma insegura
    Rng r (5);
    const auto good = encodeWav (std::vector<float> (2000, 0.1f), 44100, 16, 2, false);
    for (int i = 0; i < 2000; ++i)
    {
        auto d = good;
        if (i % 2) d.resize ((size_t) r.range (0, (int) d.size()));
        else for (int k = 0; k < 6; ++k) d[(size_t) r.range (0, (int) d.size() - 1)] = (uint8_t) r.range (0, 255);
        MonoAudio o;
        decodeWav (d, o, err);
    }
    CHECK (true);
}

TEST (voice_unpitched_falls_back_to_rhythm)
{
    const auto truth = melody();
    const auto va = analyseVoice (synthesiseVoice (truth, 44100, -55.0, 4, true));
    std::printf ("  sílabas sin altura: fiable=%d, confianza %.2f, ataques detectados %zu de %zu\n", (int) va.pitchReliable,
                 va.meanConfidence, va.notes.size(), truth.size());
    CHECK (! va.pitchReliable);
    int onsets = 0;
    for (const auto& t : truth)
        for (const auto& n : va.notes)
            if (std::abs (n.startSec - t.startSec) < 0.05) { ++onsets; break; }
    CHECK (onsets >= 7);
    const auto res = humToSolo (va, cParams(), HumToSoloOptions {});
    CHECK (res.usedRhythmOnly);
    CHECK (! res.sequence.notes.empty());
    CHECK (! res.warnings.empty());
}

TEST (voice_tempo_estimate)
{
    const auto va = analyseVoice (synthesiseVoice (melody(), 44100, -50.0, 3));
    std::printf ("  tempo estimado %.1f BPM (real 100)\n", va.estimatedBpm);
    const double r = va.estimatedBpm / 100.0;
    CHECK (std::abs (r - 1.0) < 0.04 || std::abs (r - 2.0) < 0.08 || std::abs (r - 0.5) < 0.02 || std::abs (r - 1.5) < 0.06);
}

TEST (hum_to_solo_modes)
{
    const auto truth = melody();
    const auto va = analyseVoice (synthesiseVoice (truth, 48000, -50.0, 6));
    auto p = cParams();
    HumToSoloOptions o;
    // Copiar: misma melodía (transportada por octavas al registro del instrumento), ritmo cuantizado.
    o.mode = HumMode::Copy;
    const auto copy = humToSolo (va, p, o);
    CHECK ((int) copy.sequence.notes.size() >= 7);
    int samePc = 0;
    for (size_t i = 0; i < copy.sequence.notes.size() && i < truth.size(); ++i)
        samePc += (copy.sequence.notes[i].pitch % 12) == ((int) truth[i].midi % 12) ? 1 : 0;
    CHECK (samePc >= 7);
    for (const auto& n : copy.sequence.notes) CHECK (n.startTick % (kPPQ / 4) == 0);
    // Intervalos conservados
    for (size_t i = 1; i < std::min<size_t> (copy.sequence.notes.size(), 4); ++i)
        CHECK (copy.sequence.notes[i].pitch - copy.sequence.notes[i - 1].pitch == (int) (truth[i].midi - truth[i - 1].midi));
    // Corregir: todas las notas en la escala de La menor
    o.mode = HumMode::Correct;
    const auto corr = humToSolo (va, p, o);
    const int am[] = { 9, 11, 0, 2, 4, 5, 7 };
    for (const auto& n : corr.sequence.notes) CHECK (std::find (std::begin (am), std::end (am), n.targetPitch() % 12) != std::end (am));
    // Interpretar: articulaciones derivadas de la voz (glide -> bend/slide, vibrato)
    o.mode = HumMode::Interpret;
    const auto interp = humToSolo (va, p, o);
    int bendsOrSlides = 0, vib = 0;
    for (const auto& n : interp.sequence.notes) { bendsOrSlides += n.expr.bendType != BendType::None || n.expr.slideFrom != 0; vib += n.expr.vibratoDepth > 0; }
    CHECK (bendsOrSlides >= 1 && vib >= 1);
    // Desarrollar: solo completo a partir del motivo de la voz
    o.mode = HumMode::Develop;
    p.bars = 8;
    const auto dev = humToSolo (va, p, o);
    CHECK (dev.motif.notes.size() >= 3);
    CHECK (dev.sequence.lengthTicks() == 8 * 3840 && dev.sequence.notes.size() > copy.sequence.notes.size());
    // Solo ritmo: ataques iguales a los de "Copiar", alturas elegidas por la armonía
    o.mode = HumMode::Correct;
    o.rhythmOnly = true;
    p.bars = 4;
    const auto rh = humToSolo (va, p, o);
    CHECK (rh.usedRhythmOnly && rh.sequence.notes.size() == copy.sequence.notes.size());
    for (size_t i = 0; i < std::min (rh.sequence.notes.size(), copy.sequence.notes.size()); ++i)
        CHECK (rh.sequence.notes[i].startTick == copy.sequence.notes[i].startTick);
}

TEST (vocal_localized_correction_preserves_rest)
{
    auto p = cParams();
    p.engine = 2;
    p.bars = 8;
    const auto solo = SoloComposer::compose (p);
    // Nueva toma para los compases 3-4 (base 0: 2-3)
    const auto va = analyseVoice (synthesiseVoice (melody(), 44100, -50.0, 8));
    std::vector<bool> locked (8, false);
    locked[3] = true;
    HumToSoloOptions o;
    o.mode = HumMode::Correct;
    const auto out = applyVocalCorrection (solo, va, p, o, 2, 3, locked);
    const int64_t barT = 3840;
    auto inBars = [&] (const Sequence& s, int a, int b) {
        std::vector<Note> v;
        for (const auto& n : s.notes) if (n.startTick / barT >= a && n.startTick / barT <= b) v.push_back (n);
        return v;
    };
    CHECK (inBars (out, 4, 7) == inBars (solo, 4, 7));
    CHECK (inBars (out, 3, 3) == inBars (solo, 3, 3)); // compás bloqueado
    const auto before = inBars (solo, 0, 1), after = inBars (out, 0, 1);
    CHECK (before.size() == after.size());
    for (size_t i = 0; i + 1 < before.size(); ++i) CHECK (before[i] == after[i]); // la última puede acortarse
    CHECK (! (inBars (out, 2, 2) == inBars (solo, 2, 2)));
    for (size_t i = 1; i < out.notes.size(); ++i) CHECK (out.notes[i].startTick >= out.notes[i - 1].endTick());
}
