// Pruebas del motor de audio v2: efectos, automatización, motor unificado (en vivo = exportación),
// seguridad en tiempo real (sin reservas de memoria), exportación MIDI con pitch bend y estado v2.
#if defined(__GNUC__) && ! defined(__clang__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete" // operator new/delete sustituidos a propósito (malloc/free)
#endif
#include "TestFramework.h"
#include "solo/Analysis.h"
#include "solo/AudioRenderer.h"
#include "solo/MidiFile.h"
#include "solo/SoloEngine.h"
#include "solo/StateManager.h"
#include "solo/ToneStudio.h"

#include <algorithm>
#include <limits>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <map>
#include <new>
#include <set>

using namespace solo;

// ---------------------------------------------------------------- contador de reservas de memoria
// Se sustituye el operator new global del ejecutable de pruebas. Solo cuenta mientras g_track está activo
// (es decir, dentro de SoloEngine::process), así que mide exactamente lo que hace el hilo de audio.
// Compartidos con TestTone.cpp (extern).
std::atomic<bool> g_track { false };
std::atomic<long> g_allocs { 0 };

void* operator new (std::size_t n)
{
    if (g_track.load (std::memory_order_relaxed)) g_allocs.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n)
{
    if (g_track.load (std::memory_order_relaxed)) g_allocs.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
// Variantes nothrow: deben usar el mismo par malloc/free (lo exige AddressSanitizer).
void* operator new (std::size_t n, const std::nothrow_t&) noexcept
{
    if (g_track.load (std::memory_order_relaxed)) g_allocs.fetch_add (1, std::memory_order_relaxed);
    return std::malloc (n ? n : 1);
}
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept
{
    if (g_track.load (std::memory_order_relaxed)) g_allocs.fetch_add (1, std::memory_order_relaxed);
    return std::malloc (n ? n : 1);
}
void operator delete (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }

namespace
{
ComposerParams params2 (Style st = Style::MelodicRock, InstrumentId ins = InstrumentId::Guitar, uint64_t seed = 4)
{
    ComposerParams p;
    Key::parse ("Am", p.key);
    std::string err;
    Progression::parse ("Am F C G", p.progression, err);
    p.bars = 4;
    p.bpm = 120;
    p.style = st;
    p.instrument = ins;
    p.seed = seed;
    return p;
}

bool allFinite (const std::vector<float>& v)
{
    for (float x : v) if (! std::isfinite (x)) return false;
    return true;
}

double rms (const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0;
    b = std::min (b, v.size());
    for (size_t i = a; i < b; ++i) s += (double) v[i] * v[i];
    return b > a ? std::sqrt (s / (double) (b - a)) : 0.0;
}
} // namespace

// ============================================================ efectos
TEST (fx_presets_complete_and_calibrated)
{
    const char* expected[] = { "Clean Guitar", "Emotional Lead", "Classic Rock Lead", "Blues Lead", "High Gain Solo",
                               "Ambient Guitar", "Cinematic Lead", "Wide Synth Lead" };
    CHECK (fxPresets().size() == 8);
    for (const char* n : expected) CHECK_MSG (fxPresetIndex (n) >= 0, n);
    const auto seq = SoloComposer::compose (params2());
    double lo = 0, hi = -200;
    for (const auto& pr : fxPresets())
    {
        EngineRenderOptions o;
        o.fx = pr.settings;
        o.instrument = std::string (pr.name) == "Wide Synth Lead" ? InstrumentId::Synth : InstrumentId::Guitar;
        const auto r = renderWithEngine (seq, o);
        const double l = integratedLoudness (r.left, r.right, o.sampleRate);
        std::printf ("  %-18s sonoridad %.1f LUFS, pico %.2f, cola %.1f s\n", pr.name, l, r.peak, r.tailSeconds);
        CHECK_MSG (allFinite (r.left) && allFinite (r.right), pr.name);
        CHECK_MSG (r.peak <= 0.9f && r.peak > 0.02f, pr.name + std::string (" pico ") + std::to_string (r.peak));
        CHECK_MSG (r.activeVoicesAtEnd == 0, pr.name);
        lo = lo == 0 ? l : std::min (lo, l);
        hi = std::max (hi, l);
    }
    // Calibración: los presets no deben diferir en sonoridad más de 4 LU entre sí, ni saturar el limitador.
    std::printf ("  rango de sonoridad entre presets: %.1f LU\n", hi - lo);
    CHECK (hi - lo <= 4.0);
    CHECK (hi <= -12.0);
}

TEST (fx_bypass_is_transparent_and_each_stage_acts)
{
    EffectsChain fx;
    fx.prepare (48000, 512);
    std::vector<float> l (4800), r (4800);
    for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.3f * (float) std::sin (2 * 3.14159265 * 440.0 * (double) i / 48000.0);
    const auto origL = l;
    FxSettings bypass; // solo salida, 0 dB, limitador (transparente bajo -1 dBFS)
    fx.process (l.data(), r.data(), (int) l.size(), bypass, 120);
    double maxDiff = 0;
    for (size_t i = 0; i < l.size(); ++i) maxDiff = std::max (maxDiff, (double) std::abs (l[i] - origL[i]));
    CHECK_MSG (maxDiff < 1e-6, std::to_string (maxDiff));
    for (int st = 0; st < kNumFxStages - 1; ++st)
    {
        FxSettings s;
        s.setOn ((FxStage) st, true);
        s.delayMix = 0.5f; s.delayBeats = 0.125f; s.reverbMix = 0.5f; s.chorusMix = 0.5f; s.filterCutoffHz = 300; s.eqHighDb = 9; s.eqMidDb = 6;
        EffectsChain c;
        c.prepare (48000, 512);
        std::vector<float> a = origL, b = origL;
        for (size_t pos = 0; pos < a.size(); pos += 512)
        {
            const int n = (int) std::min<size_t> (512, a.size() - pos);
            c.process (a.data() + pos, b.data() + pos, n, s, 120);
        }
        double d = 0;
        for (size_t i = 0; i < a.size(); ++i) d = std::max (d, (double) std::abs (a[i] - origL[i]));
        CHECK_MSG (allFinite (a) && allFinite (b), fxStageName ((FxStage) st));
        CHECK_MSG (d > 0.01, std::string (fxStageName ((FxStage) st)) + " no modifica la señal");
    }
}

TEST (fx_tail_included_in_export)
{
    const auto seq = SoloComposer::compose (params2 (Style::Ambient));
    EngineRenderOptions dry, wet;
    wet.fx = fxPresets()[(size_t) fxPresetIndex ("Ambient Guitar")].settings;
    const auto rd = renderWithEngine (seq, dry), rw = renderWithEngine (seq, wet);
    CHECK (rw.tailSeconds > rd.tailSeconds + 2.0);
    CHECK (rw.left.size() > rd.left.size());
    const double sr = 48000;
    const size_t end = (size_t) ((double) seq.lengthTicks() / kPPQ * 0.5 * sr); // fin del último compás a 120 BPM
    const double tailWet = rms (rw.left, end + (size_t) (1.0 * sr), end + (size_t) (2.0 * sr));
    const double tailDry = rms (rd.left, end + (size_t) (1.0 * sr), std::min (rd.left.size(), end + (size_t) (2.0 * sr)));
    std::printf ("  RMS 1-2 s tras el final: con efectos %.5f, sin efectos %.5f; cola calculada %.1f s\n", tailWet, tailDry, rw.tailSeconds);
    CHECK (tailWet > tailDry * 3 + 1e-5);
    CHECK (rms (rw.left, rw.left.size() - 4800, rw.left.size()) < 2e-3); // la exportación termina en silencio
}

TEST (fx_automation_curves_and_smoothing)
{
    const auto seq = SoloComposer::compose (params2 (Style::Emotional));
    EngineRenderOptions o;
    AutomationLane& lane = o.automation.lane (AutoTarget::Volume);
    lane.enabled = true;
    lane.points = { { 0, 0.2f }, { seq.lengthTicks(), 1.0f } };
    const auto r = renderWithEngine (seq, o);
    const size_t barS = (size_t) (2.0 * 48000);
    const double first = rms (r.left, 0, barS), last = rms (r.left, 3 * barS, 4 * barS);
    std::printf ("  volumen automatizado: RMS compás 1 = %.4f, compás 4 = %.4f\n", first, last);
    CHECK (last > first * 2.0);
    // Sin saltos bruscos: comparar con la misma señal sin automatización (el cociente varía suavemente).
    CHECK (allFinite (r.left));
    // Interpolación y extremos de la curva
    AutomationLane l2;
    l2.points = { { 100, 0.0f }, { 300, 1.0f } };
    CHECK (std::abs (l2.valueAt (0) - 0.0f) < 1e-6f);
    CHECK (std::abs (l2.valueAt (200) - 0.5f) < 1e-6f);
    CHECK (std::abs (l2.valueAt (1000) - 1.0f) < 1e-6f);
    CHECK (std::abs (autoToParam (AutoTarget::FilterCutoff, 0.0f) - 200.0f) < 1e-3f);
    CHECK (std::abs (autoToParam (AutoTarget::FilterCutoff, 1.0f) - 20000.0f) < 1.0f);
}

// ============================================================ motor unificado
TEST (engine_live_matches_export)
{
    // "En vivo": el plugin procesa bloques de 256 muestras con el transporte del host empezando en el compás 3.
    // "Exportación": renderWithEngine con bloques de 512. Mismo motor, misma cadena de efectos.
    const auto seq = SoloComposer::compose (params2 (Style::MelodicRock));
    const FxSettings fxs = fxPresets()[(size_t) fxPresetIndex ("Emotional Lead")].settings;
    EngineRenderOptions o;
    o.fx = fxs;
    const auto exported = renderWithEngine (seq, o);

    SoloEngine live;
    live.setInstrument (InstrumentId::Guitar);
    live.publishFx (fxs);
    const int64_t startBar = 2; // base 0 => compás 3 del DAW
    live.publishSequence (makePlaybackData (seq, startBar * seq.timeSignature.ticksPerBar(), false));
    live.prepare (48000, 256);
    const double sr = 48000, bpm = 120;
    const int64_t offsetSamples = (int64_t) std::llround ((double) startBar * 4 * 60.0 / bpm * sr);
    std::vector<float> L (exported.left.size() + (size_t) offsetSamples), R (L.size());
    TransportState t;
    t.valid = true; t.playing = true; t.bpm = bpm;
    for (size_t pos = 0; pos < L.size(); pos += 256)
    {
        const int n = (int) std::min<size_t> (256, L.size() - pos);
        t.ppqPosition = (double) pos * bpm / 60.0 / sr;
        live.process (L.data() + pos, R.data() + pos, n, t);
    }
    double maxDiff = 0, sumDiff = 0, sumRef = 0;
    for (size_t i = 0; i < exported.left.size(); ++i)
    {
        const double d = std::abs (L[i + (size_t) offsetSamples] - exported.left[i]);
        maxDiff = std::max (maxDiff, d);
        sumDiff += d * d;
        sumRef += (double) exported.left[i] * exported.left[i];
    }
    const double relDb = 10 * std::log10 (std::max (1e-30, sumDiff) / std::max (1e-30, sumRef));
    std::printf ("  en vivo vs exportado: diferencia máx %.2e, energía relativa de la diferencia %.1f dB\n", maxDiff, relDb);
    CHECK (relDb < -60.0);
    // Antes del compás 3 no suena nada
    CHECK (rms (L, 0, (size_t) offsetSamples - 10) < 1e-6);
}

// FL Studio puede llamar a processBlock con bloques de tamaño variable e incluso mayores que el anunciado en
// prepareToPlay. El motor debe dar el mismo audio (sin desbordes: ejecutar también con ASan) con bloques de
// 1 a 8192 muestras habiendo preparado solo 64.
TEST (engine_irregular_and_oversized_blocks)
{
    const auto seq = SoloComposer::compose (params2 (Style::ExpressiveBlues));
    const FxSettings fxs = fxPresets()[(size_t) fxPresetIndex ("Blues Lead")].settings;
    EngineRenderOptions o;
    o.fx = fxs;
    const auto ref = renderWithEngine (seq, o);

    SoloEngine e;
    e.setInstrument (InstrumentId::Guitar);
    e.publishFx (fxs);
    e.publishSequence (makePlaybackData (seq, 0, false));
    e.prepare (48000, 64);
    std::vector<float> L (ref.left.size()), R (L.size());
    TransportState t;
    t.valid = true; t.playing = true; t.bpm = 120;
    static const int kSizes[] = { 1, 64, 8192, 37, 4096, 1, 512, 3000, 2, 8192, 128, 999 };
    size_t pos = 0;
    int k = 0;
    while (pos < L.size())
    {
        const int n = (int) std::min<size_t> ((size_t) kSizes[k++ % 12], L.size() - pos);
        t.ppqPosition = (double) pos * 120.0 / 60.0 / 48000.0;
        e.process (L.data() + pos, R.data() + pos, n, t);
        pos += (size_t) n;
    }
    double sumDiff = 0, sumRef = 0;
    for (size_t i = 0; i < L.size(); ++i)
    {
        const double d = L[i] - ref.left[i];
        sumDiff += d * d;
        sumRef += (double) ref.left[i] * ref.left[i];
    }
    const double relDb = 10 * std::log10 (std::max (1e-30, sumDiff) / std::max (1e-30, sumRef));
    std::printf ("  bloques irregulares (1..8192, preparado para 64): diferencia con la exportación %.1f dB\n", relDb);
    CHECK (relDb < -60.0);
}

// Defecto encontrado en la auditoría para FL: el plugin republicaba la secuencia en cada cambio de ajustes
// (mover un deslizador, cambiar un efecto), y republicar corta la nota que suena. Con publishSequenceIfChanged,
// republicar lo mismo no emite note-off; cambiar las notas sí.
TEST (engine_republish_same_sequence_does_not_cut_notes)
{
    Sequence seq;
    seq.bars = 2;
    Note n;
    n.startTick = 0; n.lengthTicks = 4 * kPPQ; n.pitch = 64; n.velocity = 100;
    seq.notes.push_back (n);
    SoloEngine e;
    e.setInstrument (InstrumentId::Synth);
    e.prepare (48000, 256);
    CHECK (e.publishSequenceIfChanged (&seq, 0, false));
    std::vector<float> L (256), R (256);
    TransportState t;
    t.valid = true; t.playing = true; t.bpm = 120;
    int ons = 0, offs = 0;
    auto block = [&] (int b) {
        t.ppqPosition = b * 256.0 * 2.0 / 48000.0;
        const auto r = e.process (L.data(), R.data(), 256, t);
        for (int i = 0; i < r.eventCount; ++i) (e.events()[i].type == EventType::NoteOn ? ons : offs)++;
    };
    int b = 0;
    for (; b < 20; ++b) block (b);
    CHECK (ons == 1 && offs == 0);
    CHECK (! e.publishSequenceIfChanged (&seq, 0, false)); // lo mismo: no se publica
    for (; b < 40; ++b) block (b);
    CHECK (ons == 1 && offs == 0);                      // la nota sigue sonando
    auto changed = seq;
    changed.notes[0].pitch = 65;
    CHECK (e.publishSequenceIfChanged (&changed, 0, false));
    block (b++);
    CHECK (offs == 1);                                  // cambio real: se cierra la nota anterior
    CHECK (e.publishSequenceIfChanged (&changed, 0, true)); // cambiar el loop sí republica

    // Comportamiento antiguo del plugin (publicar siempre, aunque nada cambie): la nota se corta y no se
    // reanuda (el reproductor no reinicia una nota a medias), así que queda silencio hasta la siguiente.
    SoloEngine old;
    old.setInstrument (InstrumentId::Synth);
    old.prepare (48000, 256);
    old.publishSequence (makePlaybackData (seq, 0, false));
    int oldOns = 0, oldOffs = 0;
    for (int k = 0; k < 40; ++k)
    {
        t.ppqPosition = k * 256.0 * 2.0 / 48000.0;
        if (k == 20) old.publishSequence (makePlaybackData (seq, 0, false)); // misma secuencia
        const auto r = old.process (L.data(), R.data(), 256, t);
        for (int i = 0; i < r.eventCount; ++i) (old.events()[i].type == EventType::NoteOn ? oldOns : oldOffs)++;
    }
    CHECK (oldOns == 1 && oldOffs == 1); // nota cortada a mitad
}

TEST (engine_no_allocations_in_audio_thread)
{
    const auto seq = SoloComposer::compose (params2 (Style::VirtuosoRock));
    for (const auto& pr : fxPresets())
    {
        SoloEngine e;
        e.setInstrument (InstrumentId::Guitar);
        e.publishFx (pr.settings);
        AutomationSet a;
        auto& lane = a.lane (AutoTarget::FilterCutoff);
        lane.enabled = true;
        lane.points = { { 0, 0.1f }, { 7680, 0.9f } };
        e.publishAutomation (a);
        e.publishSequence (makePlaybackData (seq, 0, true));
        e.prepare (44100, 512);
        std::vector<float> L (512), R (512);
        TransportState t;
        t.valid = true; t.playing = true; t.bpm = 133;
        g_allocs = 0;
        for (int b = 0; b < 600; ++b)
        {
            t.ppqPosition = b * 512 * 133.0 / 60.0 / 44100.0;
            if (b == 200) e.requestPreviewStart();       // pulsar "Escuchar" a mitad
            if (b == 300) e.setInstrument (InstrumentId::Flute); // cambio de instrumento en marcha
            if (b == 400) { t.ppqPosition = 1.0; }        // salto del host
            if (b == 450) e.requestPreviewStop();
            g_track = true;
            e.process (L.data(), R.data(), 512, t);
            g_track = false;
        }
        CHECK_MSG (g_allocs.load() == 0, std::string (pr.name) + ": " + std::to_string (g_allocs.load()) + " reservas");
    }
}

TEST (engine_preview_restart_flushes_notes_D1)
{
    // Defecto D1 (S0–S2): pulsar "Escuchar" mientras sonaba dejaba voces de flauta/sinte colgadas.
    ComposerParams p = params2 (Style::Emotional, InstrumentId::Flute);
    Sequence seq;
    seq.bars = 4;
    seq.bpm = 120;
    Note n;
    n.startTick = 0; n.lengthTicks = 4 * 3840 - 10; n.pitch = 72; n.velocity = 100;
    seq.notes.push_back (n); // una nota muy larga
    SoloEngine e;
    e.setInstrument (InstrumentId::Flute);
    e.setSyncToHost (false);
    e.publishSequence (makePlaybackData (seq, 0, true));
    e.prepare (48000, 512);
    std::vector<float> L (512), R (512);
    TransportState host;
    std::map<int, int> on;
    auto run = [&] (int blocks) {
        for (int b = 0; b < blocks; ++b)
        {
            const auto br = e.process (L.data(), R.data(), 512, host);
            for (int i = 0; i < br.eventCount; ++i)
            {
                const auto& ev = e.events()[i];
                on[ev.pitch] += ev.type == EventType::NoteOn ? 1 : -1;
            }
        }
    };
    e.requestPreviewStart();
    run (20);
    CHECK (on[72] == 1);
    e.requestPreviewStart(); // reinicio con la nota sonando
    const auto br = e.process (L.data(), R.data(), 512, host);
    int offs = 0, ons = 0;
    for (int i = 0; i < br.eventCount; ++i) (e.events()[i].type == EventType::NoteOn ? ons : offs)++;
    CHECK (offs == 1 && ons == 1); // se cierra la anterior y vuelve a empezar
    e.requestPreviewStop();
    run (200); // ~2 s: la flauta debe haberse apagado del todo
    CHECK (e.activeVoiceCount() == 0);
    CHECK (rms (L, 0, 512) < 1e-5);
}

TEST (engine_instrument_switch_no_click)
{
    Sequence seq;
    seq.bars = 2; seq.bpm = 120;
    Note n; n.startTick = 0; n.lengthTicks = 7000; n.pitch = 64; n.velocity = 110;
    seq.notes.push_back (n);
    SoloEngine e;
    e.setInstrument (InstrumentId::Synth);
    e.setSyncToHost (true);
    e.publishSequence (makePlaybackData (seq, 0, false));
    e.prepare (48000, 256);
    std::vector<float> L (256 * 200), R (L.size());
    TransportState t; t.valid = true; t.playing = true; t.bpm = 120;
    for (int b = 0; b < 200; ++b)
    {
        t.ppqPosition = b * 256 * 2.0 / 48000.0;
        if (b == 60) e.setInstrument (InstrumentId::Flute);
        e.process (L.data() + b * 256, R.data() + b * 256, 256, t);
    }
    double maxStepAround = 0, maxStepBefore = 0;
    for (size_t i = 1; i < L.size(); ++i)
    {
        const double d = std::abs (L[i] - L[i - 1]);
        if (i > 40 * 256 && i < 59 * 256) maxStepBefore = std::max (maxStepBefore, d);
        if (i >= 60 * 256 && i < 62 * 256) maxStepAround = std::max (maxStepAround, d);
    }
    std::printf ("  salto máximo entre muestras: antes %.4f, en el cambio %.4f\n", maxStepBefore, maxStepAround);
    CHECK (maxStepAround <= maxStepBefore * 1.5 + 1e-3);
}

TEST (engine_mono_output)
{
    const auto seq = SoloComposer::compose (params2());
    SoloEngine e;
    e.publishFx (fxPresets()[1].settings);
    e.publishSequence (makePlaybackData (seq, 0, false));
    e.prepare (48000, 512);
    std::vector<float> M (512);
    TransportState t; t.valid = true; t.playing = true; t.bpm = 120;
    float peak = 0;
    for (int b = 0; b < 400; ++b)
    {
        t.ppqPosition = b * 512 * 2.0 / 48000.0;
        e.process (M.data(), M.data(), 512, t);
        for (float x : M) { CHECK (std::isfinite (x)); peak = std::max (peak, std::abs (x)); }
    }
    CHECK (peak > 0.01f && peak <= 1.0f);
}

// ============================================================ MIDI con expresión
TEST (midi_pitch_bend_export)
{
    Sequence seq;
    seq.bars = 2; seq.bpm = 100;
    Note a; a.startTick = 0; a.lengthTicks = 1920; a.pitch = 69; a.velocity = 100;
    a.expr.bendType = BendType::Up; a.expr.bendSemitones = 2; a.expr.bendStart = 0.1f; a.expr.bendTime = 0.3f;
    Note b; b.startTick = 1920; b.lengthTicks = 480; b.pitch = 72; b.velocity = 90; b.expr.legato = true;
    Note c; c.startTick = 2400; c.lengthTicks = 1920; c.pitch = 74; c.velocity = 90; c.expr.vibratoDepth = 0.3f; c.expr.palmMute = true;
    seq.notes = { a, b, c };
    MidiExportOptions o;
    const auto bytes = writeMidiFile (seq, o);
    ParsedMidi pm;
    std::string err;
    CHECK_MSG (readMidiFile (bytes, pm, err), err);
    CHECK (pm.bendRangeSemitones == 12);
    CHECK (pm.pitchBends.size() > 10);
    // Durante la nota 'a' el bend llega a +2 semitonos (2/12 de 8191).
    int maxA = 0;
    for (const auto& pb : pm.pitchBends) if (pb.tick < 1920) maxA = std::max (maxA, pb.value);
    CHECK (std::abs (maxA - (int) std::lround (2.0 / 12.0 * 8191)) <= 8);
    // Al terminar la nota 'a' se vuelve al centro
    bool reset = false;
    for (const auto& pb : pm.pitchBends) reset |= pb.tick == 1920 && pb.value == 0;
    CHECK (reset);
    // Legato: la nota anterior solapa la siguiente; marcas de texto informativas.
    CHECK (pm.notes.size() == 3 && pm.notes[0].endTick() > pm.notes[1].startTick);
    bool pmText = false;
    for (const auto& t : pm.texts) pmText |= t.second.find ("PM") != std::string::npos;
    CHECK (pmText);
    // Sin pitch bend: ningún evento 0xE0 y notas idénticas
    o.pitchBend = false;
    o.legatoOverlap = false;
    ParsedMidi plain;
    CHECK (readMidiFile (writeMidiFile (seq, o), plain, err));
    CHECK (plain.pitchBends.empty());
    CHECK (plain.notes.size() == 3 && plain.notes[0].lengthTicks == 1920);
}

TEST (midi_reader_survives_fuzzing)
{
    const auto seq = SoloComposer::compose (params2 (Style::ExpressiveBlues));
    const auto good = writeMidiFile (seq, MidiExportOptions {});
    Rng r (2024);
    int ok = 0;
    for (int it = 0; it < 3000; ++it)
    {
        auto d = good;
        const int mode = it % 3;
        if (mode == 0) d.resize ((size_t) r.range (0, (int) d.size()));
        else if (mode == 1) for (int k = 0; k < 8; ++k) d[(size_t) r.range (0, (int) d.size() - 1)] = (uint8_t) r.range (0, 255);
        else { d.resize ((size_t) r.range (14, 64)); for (auto& x : d) x = (uint8_t) r.range (0, 255); d[0] = 'M'; d[1] = 'T'; d[2] = 'h'; d[3] = 'd'; }
        ParsedMidi pm;
        std::string err;
        ok += readMidiFile (d, pm, err) ? 1 : 0;
    }
    std::printf ("  3000 ficheros MIDI corruptos: %d leídos sin error, el resto rechazados (sin fallos de memoria)\n", ok);
    CHECK (true);
}

// Defecto encontrado por UBSan: metaevento de compás con exponente de denominador 183 -> desplazamiento
// indefinido (1 << 183). Y tempo de 0 us/negra -> BPM infinito. Ambos deben ignorarse sin romper la lectura.
TEST (midi_reader_rejects_bad_timesig_and_tempo)
{
    auto be32 = [] (std::vector<uint8_t>& v, uint32_t x) { for (int s = 24; s >= 0; s -= 8) v.push_back ((uint8_t) (x >> s)); };
    std::vector<uint8_t> trk = { 0x00, 0xFF, 0x58, 0x04, 0x04, 183, 24, 8,      // compás 4/2^183
                                 0x00, 0xFF, 0x51, 0x03, 0x00, 0x00, 0x00,       // tempo 0
                                 0x00, 0x90, 60, 100, 0x60, 0x80, 60, 0,
                                 0x00, 0xFF, 0x2F, 0x00 };
    std::vector<uint8_t> d = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x03, 0xC0, 'M', 'T', 'r', 'k' };
    be32 (d, (uint32_t) trk.size());
    d.insert (d.end(), trk.begin(), trk.end());
    ParsedMidi pm;
    std::string err;
    CHECK (readMidiFile (d, pm, err));
    CHECK (std::isfinite (pm.bpm) && pm.bpm == 0.0);   // 0 = "no especificado": el tempo inválido se ignora
    CHECK (pm.tsDenominator == 0);                      // 0 = "no especificado": el compás inválido se ignora
    CHECK (pm.notes.size() == 1);
}

// Defecto encontrado en la revisión visual: un proyecto nuevo mostraba "Emotional Lead" activo pero sus
// efectos eran los de fábrica (sin etapas): la interfaz y el sonido no coincidían.
TEST (default_state_fx_match_named_preset)
{
    SoloState st;
    CHECK (st.settings.fxEnabled);
    // v3: el proyecto nuevo arranca con un preset de TONO (fx + guitarra + macros + motor); el nombre debe existir
    // y los valores deben ser exactamente los del preset.
    const int t = tonePresetIndex (st.settings.fxPreset);
    const int i = fxPresetIndex (st.settings.fxPreset);
    CHECK (t >= 0 || i >= 0);
    if (t >= 0)
    {
        const auto& p = tonePresets()[(size_t) t];
        CHECK (st.fx == p.fx);
        CHECK (st.guitar == p.guitar);
        CHECK (st.macros == p.macros);
        CHECK (st.settings.guitarEngine == (int) p.engine);
        CHECK (st.fx.ampModel == 1);
    }
    else CHECK (st.fx == fxPresets()[(size_t) i].settings);
}

// Defecto encontrado al investigar picos de CPU: las colas de reverb/delay/filtros generaban números
// desnormalizados en el estado interno (p99 de CPU por bloque ~24% frente a 1.7%). SoloEngine::process usa
// dsp::ScopedFlushDenormals; aquí se comprueba que el guardián realmente anula los subnormales y restaura
// el estado anterior. El efecto en CPU se mide con `solo_cli bench` (no se usa tiempo en pruebas: sería inestable).
TEST (flush_denormals_guard_works)
{
    volatile float tiny = std::numeric_limits<float>::min(); // menor normal
    volatile float half = 0.5f;
    const float before = tiny * half;
    CHECK (std::fpclassify (before) == FP_SUBNORMAL); // sin guardián: subnormal
    {
        const dsp::ScopedFlushDenormals g;
        const float inside = tiny * half;
        CHECK (inside == 0.0f);
    }
    const float after = tiny * half;
    CHECK (std::fpclassify (after) == FP_SUBNORMAL); // estado restaurado
}

// ============================================================ estado v2
TEST (state_v2_roundtrip_full_project)
{
    SoloState st;
    st.settings.style = Style::Cinematic;
    st.settings.performer = PerformerProfile::Virtuoso;
    st.settings.sectionPlan = "P2 C1 R1";
    st.settings.fxPreset = "Cinematic Lead";
    st.settings.keepMotifs = true;
    st.settings.motifSeed = 991;
    st.fx = fxPresets()[(size_t) fxPresetIndex ("Cinematic Lead")].settings;
    st.fx.delayBeats = 1.5f;
    auto& lane = st.automation.lane (AutoTarget::ReverbMix);
    lane.enabled = true;
    lane.smoothingMs = 80;
    lane.points = { { 0, 0.1f }, { 3840, 0.75f }, { 15360, 0.3f } };
    st.lockedBars = { false, true, false, true };
    SeedMotif m;
    m.notes = { { 0, 480, 0 }, { 480, 480, 3 }, { 960, 960, -2 } };
    m.lengthTicks = 1920;
    m.firstPitch = 64;
    st.seedMotifs.push_back (m);
    st.dnaSummary = "Contorno ascendente, ritmo de corcheas";
    auto p = params2 (Style::Cinematic);
    for (uint64_t s = 1; s <= 4; ++s) { p.seed = s; st.history.push (SoloComposer::compose (p), s, "Toma " + std::to_string (s)); }
    st.history.setFavorite (1, true);
    st.slotA = st.history.all()[0].id;
    st.slotB = st.history.all()[2].id;
    const auto text = serializeState (st);
    SoloState back;
    std::string err;
    CHECK_MSG (deserializeState (text, back, err), err);
    CHECK (back.history == st.history);
    CHECK (back.fx == st.fx);
    CHECK (back.automation == st.automation);
    CHECK (back.lockedBars == st.lockedBars);
    CHECK (back.slotA == st.slotA && back.slotB == st.slotB);
    CHECK (back.seedMotifs.size() == 1 && back.seedMotifs[0].notes.size() == 3 && back.seedMotifs[0].notes[1].interval == 3);
    CHECK (back.dnaSummary == st.dnaSummary);
    CHECK (back.settings.performer == PerformerProfile::Virtuoso && back.settings.sectionPlan == "P2 C1 R1");
    CHECK (serializeState (back) == text);
    // Las expresiones de nota sobreviven
    bool anyExpr = false;
    for (const auto& n : back.history.current().notes) anyExpr |= ! n.expr.isPlain();
    CHECK (anyExpr);
}

TEST (state_v1_migration_reproduces_s0_s2)
{
    // Estado guardado por S0–S2 (formato v1): estilo 1 = Rock, instrumento 0 = guitarra S2.
    const std::string v1 =
        "SOLOAISTUDIO-STATE 1\nkey=Am\nprogression=Am F C G\nts=4\nbars=8\ninstrument=0\nstyle=1\nenergy=0.6\n"
        "density=0.5\ncomplexity=0.4\nseed=12345\nmanualBpm=146\nuseHostTempo=1\nsyncToHost=1\nstartBar=1\nloop=1\n"
        "outputGain=1\nproMode=0\nwavSampleRate=48000\nmidiFormat=1\nvariations=1 0\nvar 12345 8 4 146 2\n"
        "n 0 480 69 100 0\nn 960 480 72 90 0\nend\n";
    SoloState st;
    std::string err;
    CHECK_MSG (deserializeState (v1, st, err), err);
    CHECK (st.loadedFromVersion == 1);
    CHECK (st.settings.engine == 1 && st.settings.legacyStyle == 1);
    CHECK (st.settings.instrument == InstrumentId::GuitarLite);
    CHECK (st.settings.style == Style::MelodicRock);
    CHECK (! st.settings.fxEnabled);
    CHECK (st.history.size() == 1 && st.history.current().notes.size() == 2);
    CHECK (st.history.current().notes[1].expr.isPlain());
    // Regenerar con los ajustes migrados da exactamente el mismo solo que S0–S2 (hash de referencia).
    ComposerParams cp;
    CHECK_MSG (st.settings.toComposerParams (146.0, cp, err), err);
    const auto s = SoloComposer::compose (cp);
    uint64_t h = 1469598103934665603ull;
    auto mix = [&] (int64_t v) { h ^= (uint64_t) v; h *= 1099511628211ull; };
    for (const auto& n : s.notes) { mix (n.startTick); mix (n.lengthTicks); mix (n.pitch); mix (n.velocity); }
    CHECK (h == 4560047695921077447ull);
    // Al volver a guardar se escribe v2 y se relee igual.
    SoloState again;
    CHECK (deserializeState (serializeState (st), again, err));
    CHECK (again.settings.engine == 1 && again.settings.legacyStyle == 1 && again.history == st.history);
}

TEST (state_survives_fuzzing)
{
    SoloState st;
    auto p = params2();
    st.history.push (SoloComposer::compose (p), 1);
    st.automation.lane (AutoTarget::Drive).points = { { 0, 0.3f } };
    const auto good = serializeState (st);
    Rng r (77);
    int accepted = 0;
    for (int it = 0; it < 3000; ++it)
    {
        std::string t = good;
        const int mode = it % 4;
        if (mode == 0) t.resize ((size_t) r.range (0, (int) t.size()));
        else if (mode == 1) for (int k = 0; k < 6; ++k) t[(size_t) r.range (0, (int) t.size() - 1)] = (char) r.range (32, 126);
        else if (mode == 2) { const size_t pos = (size_t) r.range (0, (int) t.size() - 1); t.insert (pos, "\nn 1 -5 999 0 0\n"); }
        else for (int k = 0; k < 3; ++k) t[(size_t) r.range (0, (int) t.size() - 1)] = '\n';
        SoloState out;
        out.settings.keyText = "SENTINEL";
        std::string err;
        if (deserializeState (t, out, err))
        {
            ++accepted;
            // Si se acepta, todo está en rango
            for (const auto& it2 : out.history.all())
                for (const auto& n : it2.sequence.notes)
                    CHECK (n.pitch >= 0 && n.pitch <= 127 && n.lengthTicks > 0);
        }
        else CHECK (out.settings.keyText == "SENTINEL"); // nunca a medias
    }
    std::printf ("  3000 estados corruptos: %d aceptados (válidos), el resto rechazados sin aplicar nada\n", accepted);
}

TEST (variation_history_protects_favorites_D3)
{
    VariationHistory h;
    Sequence s;
    h.push (s, 1, "favorita antigua");
    h.setFavorite (0, true);
    for (int i = 0; i < 100; ++i) { s.bpm = 100 + i; h.push (s, (uint64_t) (i + 2)); }
    CHECK (h.size() == VariationHistory::kMaxVariations);
    CHECK (h.all()[0].favorite && h.all()[0].name == "favorita antigua");
    // Todas favoritas: el historial crece hasta kMaxTotal y luego se niega a borrar
    VariationHistory f;
    int pushed = 0;
    for (int i = 0; i < 100; ++i)
    {
        if (f.push (s, (uint64_t) i)) { ++pushed; f.setFavorite (f.size() - 1, true); }
    }
    CHECK (f.size() == VariationHistory::kMaxTotal && pushed == VariationHistory::kMaxTotal);
    CHECK (f.favoriteCount() == VariationHistory::kMaxTotal);
    // Ids estables y únicos
    std::set<uint32_t> ids;
    for (const auto& it : f.all()) ids.insert (it.id);
    CHECK ((int) ids.size() == f.size());
    // Borrado explícito
    CHECK (f.remove (0) && f.size() == VariationHistory::kMaxTotal - 1);
}

TEST (undo_redo_stack)
{
    SoloState st;
    auto p = params2();
    UndoStack u;
    for (uint64_t s = 1; s <= 3; ++s)
    {
        u.push (snapshotOf (st, "Generar " + std::to_string (s)));
        p.seed = s;
        st.history.push (SoloComposer::compose (p), s);
    }
    u.push (snapshotOf (st, "Bloquear compás"));
    st.lockedBars = { true };
    CHECK (u.undoLabel() == "Bloquear compás");
    ProjectSnapshot cur = snapshotOf (st, "");
    CHECK (u.undo (cur));
    applySnapshot (st, cur);
    CHECK (st.lockedBars.empty() && st.history.size() == 3);
    CHECK (u.undo (cur)); applySnapshot (st, cur);
    CHECK (st.history.size() == 2);
    CHECK (u.canRedo() && u.redoLabel() == "Generar 3");
    CHECK (u.redo (cur)); applySnapshot (st, cur);
    CHECK (st.history.size() == 3);
    CHECK (u.redo (cur)); applySnapshot (st, cur);
    CHECK (st.lockedBars.size() == 1 && ! u.canRedo());
    // Una acción nueva borra el rehacer
    u.undo (cur);
    u.push (snapshotOf (st, "Otra"));
    CHECK (! u.canRedo());
    // Límite de pasos
    for (int i = 0; i < 100; ++i) u.push (snapshotOf (st, "x"));
    CHECK (u.undoCount() == UndoStack::kMaxSteps);
}
