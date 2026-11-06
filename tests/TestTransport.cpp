// Transporte: robustez del reproductor frente a hosts que cuantizan, redondean o reparten el PPQ con jitter
// (lo que se sospecha de FL Studio, cuya prueba manual de sincronía falló), y frente a loops, saltos y Stop.
#include "solo/SoloComposer.h"
#include "solo/SoloEngine.h"
#include "TestFramework.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace solo;

namespace
{
Sequence testSolo()
{
    ComposerParams p;
    Key::parse ("Am", p.key);
    std::string err;
    Progression::parse ("Am F C G", p.progression, err);
    p.bars = 4;
    p.bpm = 120;
    p.seed = 11;
    p.style = Style::MelodicRock;
    return SoloComposer::compose (p);
}

struct Logged { int64_t sample; TimedEvent e; };

// Simula un host: la posición "real" avanza en muestras; lo que el host DECLARA pasa por 'report'.
template <typename Report>
std::vector<Logged> run (const Sequence& s, SoloPlayer& pl, double sr, int block, double bpm, double seconds, bool loop,
                         bool sampleTime, Report report)
{
    auto data = makePlaybackData (s, 0, loop);
    std::vector<TimedEvent> ev (512);
    std::vector<Logged> log;
    const int64_t total = (int64_t) (seconds * sr);
    for (int64_t pos = 0; pos < total; pos += block)
    {
        TransportState t;
        t.valid = true; t.playing = true; t.bpm = bpm;
        const double truePpq = (double) pos * bpm / 60.0 / sr;
        t.ppqPosition = report (truePpq, pos);
        t.timeInSamplesValid = sampleTime;
        t.timeInSamples = pos;
        const int c = pl.process (t, sr, block, data.get(), ev.data(), 512);
        for (int i = 0; i < c; ++i) log.push_back ({ pos + ev[i].sampleOffset, ev[i] });
    }
    return log;
}

// Error de colocación de cada note-on respecto a la rejilla exacta (en muestras), y recuento.
struct Stats { int ons = 0, offs = 0; double maxErr = 0, meanErr = 0, minSigned = 1e9, maxSigned = -1e9; int cut = 0; };
Stats stats (const Sequence& s, const std::vector<Logged>& log, double sr, double bpm)
{
    Stats st;
    const double spt = sr * 60.0 / bpm / kPPQ; // muestras por tick
    std::vector<Note> notes = s.notes;
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.startTick < b.startTick; });
    size_t k = 0;
    for (const auto& l : log)
    {
        if (l.e.type == EventType::NoteOn)
        {
            ++st.ons;
            if (k < notes.size())
            {
                const double expect = (double) notes[k].startTick * spt;
                const double signedErr = (double) l.sample - expect;
                const double err = std::abs (signedErr);
                st.maxErr = std::max (st.maxErr, err);
                st.minSigned = std::min (st.minSigned, signedErr);
                st.maxSigned = std::max (st.maxSigned, signedErr);
                st.meanErr += err;
                ++k;
            }
        }
        else ++st.offs;
    }
    if (st.ons > 0) st.meanErr /= st.ons;
    return st;
}
} // namespace

TEST (transport_exact_host_is_sample_accurate)
{
    const auto s = testSolo();
    SoloPlayer pl;
    const auto log = run (s, pl, 48000, 512, 120, 8.5, false, false, [] (double p, int64_t) { return p; });
    const auto st = stats (s, log, 48000, 120);
    CHECK (st.ons == (int) s.notes.size());
    CHECK (st.offs == (int) s.notes.size());
    CHECK (st.maxErr <= 1.0);
    CHECK (pl.resyncCount() == 1); // solo el arranque
}

TEST (transport_quantised_ppq_like_fl_does_not_cut_notes)
{
    // Host que declara el PPQ cuantizado a 1/96 de negra (ticks de FL) y SIN posición en muestras.
    // Antes (tolerancia de 4 muestras) cada bloque parecía un salto: se cerraban todas las notas en cada bloque.
    const auto s = testSolo();
    for (int block : { 256, 512, 1024, 37 })
    {
        SoloPlayer pl;
        const auto log = run (s, pl, 48000, block, 120, 8.5, false, false,
                              [] (double p, int64_t) { return std::floor (p * 96.0) / 96.0; });
        const auto st = stats (s, log, 48000, 120);
        std::printf ("  bloque %d: %d on / %d off, error máx %.1f muestras, resync %u, correcciones %u\n", block, st.ons, st.offs,
                     st.maxErr, pl.resyncCount(), pl.nudgeCount());
        CHECK_MSG (st.ons == (int) s.notes.size(), "notas duplicadas o perdidas");
        CHECK (st.offs == (int) s.notes.size());
        CHECK (pl.resyncCount() == 1);
        // El reloj interno arranca en la posición declarada del primer bloque (0, exacta) y luego ignora la cuantización.
        CHECK (st.maxErr <= 1.0);
        CHECK (pl.nudgeCount() == 0);
    }
}

TEST (transport_jittered_ppq_does_not_cut_notes)
{
    // PPQ con jitter aleatorio de hasta ±1.5 ms (sin posición en muestras): tampoco debe cortar notas.
    const auto s = testSolo();
    std::mt19937 rng (5);
    std::uniform_real_distribution<double> j (-0.0015 * 2.0, 0.0015 * 2.0); // 2 ppq/s a 120 BPM
    SoloPlayer pl;
    const auto log = run (s, pl, 44100, 480, 120, 8.5, false, false, [&] (double p, int64_t) { return p + j (rng); });
    const auto st = stats (s, log, 44100, 120);
    std::printf ("  jitter: %d on / %d off, error máx %.1f muestras, resync %u\n", st.ons, st.offs, st.maxErr, pl.resyncCount());
    CHECK (st.ons == (int) s.notes.size());
    CHECK (st.offs == (int) s.notes.size());
    CHECK (pl.resyncCount() == 1);
    // El primer bloque fija el origen (con su jitter, <= 1.5 ms: inevitable sin posición en muestras); después el
    // jitter queda dentro de la banda muerta y no se transmite: todas las notas comparten el mismo desplazamiento.
    CHECK (st.maxErr <= 0.0015 * 44100 + 1.0);
    CHECK (st.maxSigned - st.minSigned <= 1.0);
}

TEST (transport_sample_time_continuity_survives_tempo_ramp)
{
    // Con posición en muestras del host, una rampa de tempo (el PPQ se aleja poco a poco del previsto) no corta notas:
    // el reloj interno corrige la deriva y sigue la nueva velocidad.
    Sequence s;
    s.bars = 4;
    s.bpm = 120;
    for (int b = 0; b < 4; ++b) s.notes.push_back ({ b * 4 * kPPQ, 3 * kPPQ, 60 + b, 100, 0 });
    auto data = makePlaybackData (s, 0, false);
    SoloPlayer pl;
    std::vector<TimedEvent> ev (64);
    const double sr = 48000;
    double ppq = 0, bpm = 120;
    int ons = 0, offs = 0;
    for (int64_t pos = 0; pos < 9 * 48000; pos += 512)
    {
        TransportState t;
        t.valid = true; t.playing = true; t.bpm = bpm; t.ppqPosition = ppq;
        t.timeInSamplesValid = true; t.timeInSamples = pos;
        const int c = pl.process (t, sr, 512, data.get(), ev.data(), 64);
        for (int i = 0; i < c; ++i) (ev[i].type == EventType::NoteOn ? ons : offs)++;
        ppq += 512 * bpm / 60.0 / sr;
        bpm = std::min (160.0, bpm + 0.5); // rampa: +0.5 BPM por bloque
    }
    CHECK (ons == 4);
    CHECK (offs == 4);
    CHECK (pl.resyncCount() == 1);
}

TEST (transport_real_jumps_loops_and_stop_still_flush)
{
    Sequence s;
    s.bars = 2;
    s.bpm = 120;
    s.notes = { { 0, 6 * kPPQ, 60, 100, 0 } }; // nota de 6 negras
    auto data = makePlaybackData (s, 0, true);
    SoloPlayer pl;
    std::vector<TimedEvent> ev (64);
    TransportState t;
    t.valid = true; t.playing = true; t.bpm = 120; t.ppqPosition = 0; t.timeInSamplesValid = true; t.timeInSamples = 0;
    int c = pl.process (t, 48000, 512, data.get(), ev.data(), 64);
    CHECK (c == 1 && pl.activeNoteCount() == 1);
    // Salto hacia delante de 1 negra con contador de muestras continuo (host que no reinicia muestras): es un salto.
    t.ppqPosition = 1.0 + 512 * 2.0 / 48000.0; t.timeInSamples = 512;
    c = pl.process (t, 48000, 512, data.get(), ev.data(), 64);
    CHECK (c >= 1 && ev[0].type == EventType::NoteOff);
    CHECK (pl.resyncCount() == 2);
    // Loop del host al compás 1 (ppq 0): reinicia la nota.
    t.ppqPosition = 0; t.timeInSamples = 0;
    c = pl.process (t, 48000, 512, data.get(), ev.data(), 64);
    CHECK (pl.activeNoteCount() == 1 && pl.resyncCount() == 3);
    // Stop: cierra.
    t.playing = false;
    pl.process (t, 48000, 512, data.get(), ev.data(), 64);
    CHECK (pl.activeNoteCount() == 0);
    // Salto hacia atrás pequeño pero mayor que la tolerancia (1/16 de negra) también es un salto.
    t.playing = true; t.ppqPosition = 2.0; t.timeInSamples = 100000;
    pl.process (t, 48000, 512, data.get(), ev.data(), 64);
    const auto r0 = pl.resyncCount();
    t.ppqPosition = 2.0 + 512 * 2.0 / 48000.0 - 0.1; t.timeInSamples = 100512; // muestras continuas, PPQ retrocede 0.1
    pl.process (t, 48000, 512, data.get(), ev.data(), 64);
    CHECK (pl.resyncCount() == r0 + 1);
}

TEST (transport_engine_live_with_quantised_host_matches_export)
{
    // El motor completo con un host que cuantiza el PPQ a 1/96 y no da muestras: el audio debe coincidir con la
    // exportación offline salvo por la colocación del primer bloque (misma rejilla de muestras en este caso).
    const auto seq = testSolo();
    EngineRenderOptions o;
    const auto exported = renderWithEngine (seq, o);
    SoloEngine live;
    live.setInstrument (InstrumentId::Guitar);
    live.publishSequence (makePlaybackData (seq, 0, false));
    live.prepare (48000, 256);
    std::vector<float> L (exported.left.size()), R (L.size());
    TransportState t;
    t.valid = true; t.playing = true; t.bpm = 120;
    for (size_t pos = 0; pos < L.size(); pos += 256)
    {
        const int n = (int) std::min<size_t> (256, L.size() - pos);
        t.ppqPosition = std::floor ((double) pos * 120.0 / 60.0 / 48000.0 * 96.0) / 96.0;
        live.process (L.data() + pos, R.data() + pos, n, t);
    }
    double sumDiff = 0, sumRef = 0;
    for (size_t i = 0; i < L.size(); ++i)
    {
        const double d = L[i] - exported.left[i];
        sumDiff += d * d;
        sumRef += (double) exported.left[i] * exported.left[i];
    }
    const double relDb = 10 * std::log10 (std::max (1e-30, sumDiff) / std::max (1e-30, sumRef));
    std::printf ("  host cuantizado vs exportado: %.1f dB (resync %u)\n", relDb, live.resyncCount());
    CHECK (relDb < -60.0);
    CHECK (live.resyncCount() == 1);
}
