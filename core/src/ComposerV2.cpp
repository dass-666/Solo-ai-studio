// Motor 2: composición jerárquica (plan -> secciones -> frases -> motivos -> notas) + intérprete.
#include "solo/Performer.h"
#include "solo/SoloComposer.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace solo
{
// ======================================================================= nombres y perfiles
const char* instrumentName (InstrumentId id)
{
    switch (id)
    {
        case InstrumentId::Guitar: return "Guitarra eléctrica (modelada)";
        case InstrumentId::Flute: return "Flauta (sintetizada)";
        case InstrumentId::Synth: return "Sintetizador lead";
        case InstrumentId::GuitarLite: return "Guitarra ligera (S2)";
    }
    return "?";
}

const char* styleName (Style s)
{
    switch (s)
    {
        case Style::MelodicRock: return "Rock melódico";
        case Style::ExpressiveBlues: return "Blues expresivo";
        case Style::Emotional: return "Solo emocional";
        case Style::VirtuosoRock: return "Rock virtuoso";
        case Style::Ambient: return "Solo ambiental";
        case Style::Cinematic: return "Solo cinematográfico";
        case Style::SoftImprov: return "Improvisación suave";
        case Style::VocalResponse: return "Respuesta a la voz";
    }
    return "?";
}

const char* performerName (PerformerProfile p)
{
    switch (p)
    {
        case PerformerProfile::Auto: return "Automático (según estilo)";
        case PerformerProfile::Melodic: return "Guitarrista melódico";
        case PerformerProfile::Blues: return "Guitarrista blues";
        case PerformerProfile::Rock: return "Guitarrista rock";
        case PerformerProfile::Virtuoso: return "Guitarrista virtuoso";
        case PerformerProfile::Ambient: return "Guitarrista ambiental";
        case PerformerProfile::None: return "Sin articulaciones";
    }
    return "?";
}

const InstrumentProfile& profileFor (InstrumentId id)
{
    // Rangos en notas MIDI (C4 = 60). Guitarra: E3..D6 (registro de solo); flauta: C4..C7.
    static const InstrumentProfile guitar { InstrumentId::Guitar, { 52, 86, 57, 81 }, 12, 0.92, 1.0, 4, 12.0, 0.18, 29 };
    static const InstrumentProfile flute { InstrumentId::Flute, { 60, 96, 64, 91 }, 12, 0.95, 0.8, 2, 10.0, 0.14, 73 };
    static const InstrumentProfile synth { InstrumentId::Synth, { 48, 96, 57, 86 }, 15, 0.85, 1.15, 4, 16.0, 0.22, 81 };
    static const InstrumentProfile guitarLite { InstrumentId::GuitarLite, { 52, 86, 57, 81 }, 12, 0.92, 1.0, 4, 12.0, 0.18, 29 };
    switch (id)
    {
        case InstrumentId::Flute: return flute;
        case InstrumentId::Synth: return synth;
        case InstrumentId::GuitarLite: return guitarLite;
        case InstrumentId::Guitar: break;
    }
    return guitar;
}

namespace
{
int mod12 (int v) { return ((v % 12) + 12) % 12; }

// ======================================================================= estilos
enum class ScaleKind { Pent, PentBlues, PentPlus, Diatonic, DiatonicHarm };

enum : uint8_t { F_TARGET = 1, F_BEND = 2, F_VIB = 4, F_SLIDE = 8, F_LEGATO = 16, F_REST = 32, F_ACCENT = 64, F_LITERAL = 128 };

struct LickNote { double units16; int step; uint8_t flags; };
using Lick = std::vector<LickNote>;

struct StyleProfile
{
    ScaleKind scale;
    double npbMin, npbMax;           // notas por pulso con densidad 0 y 1
    int phraseBeatsMin, phraseBeatsMax;
    double restBeatsMin, restBeatsMax;
    double leapProb;
    int maxLeapSteps;
    double longNote;                 // prob. de nota larga expresiva dentro de la frase
    double syncopation, anticipation, triplet, run, repeat;
    double bend, chromatic, swing;
    double lickUse;
    int motifBeats;
    double velBase, velRange;
    bool vocalSpace;
    int lickFamily;
    double registerLow, registerHigh; // posición del centro de registro (0..1 del rango cómodo) en intensidad 0 y 1
};

const StyleProfile& styleProfile (Style s)
{
    //                         scale                  npb       phrase   rest        leap mxL long  sync antic trip  run  rep   bend chrom swing lick mB vel  rng  vocal fam regLo regHi
    static const StyleProfile melodicRock { ScaleKind::PentPlus, 1.2, 2.8, 4, 8, 1.0, 2.0, 0.14, 4, 0.30, 0.35, 0.30, 0.05, 0.10, 0.55, 0.45, 0.10, 0.0, 0.55, 2, 84, 26, false, 0, 0.20, 0.95 };
    static const StyleProfile blues { ScaleKind::PentBlues, 1.0, 2.6, 3, 7, 1.0, 2.5, 0.12, 3, 0.35, 0.30, 0.25, 0.35, 0.05, 0.65, 0.65, 0.20, 0.55, 0.65, 2, 82, 34, false, 1, 0.20, 0.85 };
    static const StyleProfile emotional { ScaleKind::Diatonic, 0.6, 1.6, 6, 12, 1.0, 2.5, 0.22, 5, 0.50, 0.20, 0.35, 0.05, 0.03, 0.40, 0.40, 0.05, 0.0, 0.50, 4, 74, 34, false, 2, 0.15, 1.00 };
    static const StyleProfile virtuoso { ScaleKind::DiatonicHarm, 2.0, 5.0, 4, 10, 0.5, 1.5, 0.20, 5, 0.12, 0.30, 0.25, 0.15, 0.55, 0.40, 0.35, 0.15, 0.0, 0.60, 2, 92, 20, false, 3, 0.25, 1.00 };
    static const StyleProfile ambient { ScaleKind::Pent, 0.35, 0.9, 6, 12, 2.0, 4.0, 0.15, 4, 0.60, 0.10, 0.15, 0.00, 0.00, 0.50, 0.10, 0.00, 0.0, 0.60, 4, 60, 24, false, 4, 0.20, 0.65 };
    static const StyleProfile cinematic { ScaleKind::DiatonicHarm, 0.7, 1.9, 6, 12, 1.0, 2.0, 0.22, 7, 0.45, 0.20, 0.30, 0.08, 0.10, 0.55, 0.25, 0.05, 0.0, 0.55, 4, 76, 36, false, 5, 0.10, 1.00 };
    static const StyleProfile softImprov { ScaleKind::Diatonic, 1.2, 2.4, 4, 8, 1.0, 2.0, 0.10, 3, 0.20, 0.40, 0.20, 0.25, 0.03, 0.30, 0.10, 0.45, 0.35, 0.45, 2, 70, 22, false, 6, 0.25, 0.70 };
    static const StyleProfile vocal { ScaleKind::Diatonic, 0.9, 2.0, 3, 5, 2.0, 4.0, 0.10, 3, 0.25, 0.25, 0.20, 0.05, 0.02, 0.50, 0.25, 0.05, 0.0, 0.55, 2, 76, 24, true, 7, 0.20, 0.75 };
    switch (s)
    {
        case Style::MelodicRock: return melodicRock;
        case Style::ExpressiveBlues: return blues;
        case Style::Emotional: return emotional;
        case Style::VirtuosoRock: return virtuoso;
        case Style::Ambient: return ambient;
        case Style::Cinematic: return cinematic;
        case Style::SoftImprov: return softImprov;
        case Style::VocalResponse: return vocal;
    }
    return melodicRock;
}

// Vocabulario de frases idiomáticas (pasos en grados de la escala del estilo).
const std::vector<Lick>& lickFamily (int family)
{
    constexpr double T = 4.0 / 3.0; // corchea de tresillo en semicorcheas
    constexpr double X = 2.0 / 3.0; // seisillo de semicorchea
    static const std::vector<std::vector<Lick>> fam = {
        { // 0 rock melódico
          { { 2, 0, 0 }, { 2, 1, 0 }, { 4, 1, F_TARGET }, { 2, -1, 0 }, { 2, 1, 0 }, { 4, 1, F_VIB | F_TARGET } },
          { { 4, 0, 0 }, { 2, 1, 0 }, { 2, 1, 0 }, { 8, 1, F_BEND | F_VIB | F_TARGET } },
          { { 1, 0, 0 }, { 1, 1, F_LEGATO }, { 1, 1, 0 }, { 1, -1, F_LEGATO }, { 4, -1, F_TARGET }, { 4, 0, F_REST }, { 2, 0, 0 }, { 2, -1, 0 } },
          { { 3, 0, 0 }, { 1, 1, F_LEGATO }, { 4, 1, F_BEND | F_TARGET }, { 4, -1, 0 }, { 4, 0, F_VIB } },
          { { 2, 0, F_ACCENT }, { 2, 0, 0 }, { 2, 1, 0 }, { 2, -1, 0 }, { 8, -1, F_VIB | F_TARGET } } },
        { // 1 blues
          { { T, 0, 0 }, { T, -1, 0 }, { T, -1, 0 }, { 4, 1, F_BEND | F_VIB | F_TARGET }, { 4, 0, F_REST } },
          { { 2, 0, F_BEND | F_TARGET }, { 2, 0, 0 }, { 2, -1, 0 }, { 2, -1, 0 }, { 4, -1, F_VIB } },
          { { 6, 0, F_SLIDE | F_VIB }, { 2, -1, 0 }, { 4, -1, 0 }, { 4, 0, F_REST } },
          { { 1, 0, 0 }, { 1, 1, F_LEGATO }, { 2, -1, 0 }, { 2, -1, 0 }, { 2, -1, 0 }, { 4, 1, F_VIB | F_TARGET } },
          { { T, 0, F_ACCENT }, { T, 0, 0 }, { T, 0, 0 }, { 8, 1, F_BEND | F_VIB | F_TARGET } } },
        { // 2 emocional
          { { 8, 0, F_VIB | F_TARGET }, { 4, 1, 0 }, { 4, -1, 0 }, { 12, 2, F_BEND | F_VIB | F_TARGET } },
          { { 6, 0, F_SLIDE }, { 2, 1, 0 }, { 8, 2, F_VIB | F_TARGET } },
          { { 4, 0, 0 }, { 4, 4, 0 }, { 8, -1, F_VIB | F_TARGET } },
          { { 3, 0, 0 }, { 1, 1, F_LEGATO }, { 4, 1, 0 }, { 8, -2, F_VIB | F_TARGET } } },
        { // 3 virtuoso
          { { X, 0, 0 }, { X, 1, F_LEGATO }, { X, 1, F_LEGATO }, { X, 1, 0 }, { X, 1, F_LEGATO }, { X, 1, F_LEGATO }, { 4, 1, F_BEND | F_VIB | F_TARGET } },
          { { 1, 0, 0 }, { 1, 1, 0 }, { 1, 1, 0 }, { 1, -2, 0 }, { 1, 1, 0 }, { 1, 1, 0 }, { 1, 1, 0 }, { 1, -2, 0 }, { 4, 2, F_TARGET | F_VIB } },
          { { X, 0, 0 }, { X, 1, 0 }, { X, -1, 0 }, { X, 2, 0 }, { X, 1, 0 }, { X, -1, 0 }, { X, 2, 0 }, { X, 1, 0 }, { X, -1, 0 }, { 6, 2, F_TARGET | F_VIB } },
          { { 1, 0, 0 }, { 1, -1, F_LEGATO }, { 1, -1, F_LEGATO }, { 1, -1, 0 }, { 1, -1, F_LEGATO }, { 1, -1, F_LEGATO }, { 2, -1, 0 }, { 8, 3, F_BEND | F_VIB | F_TARGET } } },
        { // 4 ambiental
          { { 16, 0, F_SLIDE | F_VIB | F_TARGET }, { 8, 2, F_VIB }, { 8, 0, F_REST } },
          { { 8, 0, F_TARGET }, { 8, 1, F_SLIDE | F_VIB }, { 16, 0, F_REST } },
          { { 12, 0, F_VIB | F_TARGET }, { 4, -1, 0 }, { 16, -1, F_VIB } } },
        { // 5 cinematográfico
          { { 6, 0, 0 }, { 2, 1, 0 }, { 8, 2, F_TARGET | F_VIB } },
          { { 4, 0, 0 }, { 4, 4, F_TARGET }, { 6, -1, 0 }, { 2, -1, 0 }, { 8, 0, F_VIB } },
          { { 3, 0, 0 }, { 1, 1, 0 }, { 12, 2, F_TARGET | F_VIB }, { 4, 1, 0 }, { 4, -1, 0 }, { 8, -1, F_VIB } } },
        { // 6 improvisación suave
          { { 2, 0, 0 }, { 2, 1, 0 }, { 2, 2, 0 }, { 2, -1, 0 }, { 4, 0, F_TARGET }, { 4, 0, F_REST } },
          { { T, 0, 0 }, { T, 1, 0 }, { T, 1, 0 }, { 4, -1, F_TARGET }, { 2, 0, F_REST } },
          { { 2, 0, 0 }, { 2, -1, 0 }, { 2, -1, 0 }, { 2, 2, 0 }, { 8, 1, F_TARGET | F_VIB } } },
        { // 7 respuesta a la voz
          { { 2, 0, 0 }, { 2, -1, 0 }, { 4, -1, F_TARGET }, { 8, 0, F_REST } },
          { { 2, 0, 0 }, { 2, 1, 0 }, { 2, 1, 0 }, { 2, -1, 0 }, { 8, 0, F_VIB | F_TARGET } },
          { { 4, 0, F_SLIDE }, { 2, -1, 0 }, { 2, -1, 0 }, { 8, -1, F_VIB | F_TARGET } } },
    };
    return fam[(size_t) std::clamp (family, 0, (int) fam.size() - 1)];
}

// ======================================================================= estructuras internas
struct MNote
{
    int64_t onset;  // relativo al motivo
    int64_t dur;
    int step;       // grados respecto a la nota anterior
    uint8_t flags;
    int semis = 0;  // intervalo literal en semitonos (motivos semilla, F_LITERAL)
};

struct Motif
{
    std::vector<MNote> notes;
    int64_t length = 0;
};

struct PhraseNote
{
    int64_t start, length;
    int step;
    uint8_t flags;
    NoteRole role;
    int semis = 0;
};

class ComposerV2
{
public:
    explicit ComposerV2 (const ComposerParams& p)
        : params (p), style (styleProfile (p.style)), inst (profileFor (p.instrument)),
          rng (p.seed * 0x9E37ull + 17), motifRng (p.motifSeed != 0 ? p.motifSeed : p.seed * 31 + 7),
          beat (p.timeSignature.ticksPerBeat()), bar (p.timeSignature.ticksPerBar()), total ((int64_t) p.bars * bar)
    {
        const double maxNpb = inst.maxNotesPerSecond * 60.0 / std::max (20.0, p.bpm);
        npbCap = maxNpb;
    }

    Sequence run()
    {
        Sequence seq;
        seq.timeSignature = params.timeSignature;
        seq.bars = params.bars;
        seq.bpm = params.bpm;
        const auto sections = SoloComposer::plan (params);
        buildMotifs();
        int prevPitch = -1;
        int phraseIndex = 0;
        for (size_t si = 0; si < sections.size(); ++si)
        {
            const auto& sec = sections[si];
            const bool lastSection = si + 1 == sections.size();
            const int64_t secStart = (int64_t) sec.startBar * bar, secEnd = (int64_t) (sec.startBar + sec.bars) * bar;
            int64_t cursor = secStart;
            bool answer = false;
            while (cursor < secEnd)
            {
                // Longitud de frase (en pulsos) según estilo; nunca cruza el final de la sección.
                int beats = rng.range (style.phraseBeatsMin, style.phraseBeatsMax);
                if (sec.role == SectionRole::Climax) beats = std::max (beats, params.timeSignature.numerator * sec.bars);
                // Con material de la voz, la frase debe poder contener el motivo completo (más respiración).
                if (primaryFromSeed) beats = std::max (beats, (int) ((primary.length + (style.vocalSpace ? 3 : 1) * beat) / beat));
                int64_t len = std::min<int64_t> ((int64_t) beats * beat, secEnd - cursor);
                // Ajusta a medio compás para que las frases respiren en lugares naturales.
                const int64_t half = bar / (params.timeSignature.numerator == 4 ? 2 : 1);
                len = std::max<int64_t> (half, (len / half) * half);
                len = std::min<int64_t> (len, secEnd - cursor);
                if (secEnd - (cursor + len) < half) len = secEnd - cursor; // no dejar restos minúsculos
                const bool finalPhrase = lastSection && cursor + len >= secEnd;
                realizePhrase (seq, cursor, cursor + len, sec, answer, finalPhrase, phraseIndex, prevPitch);
                answer = ! answer;
                ++phraseIndex;
                cursor += len;
            }
        }
        applyAnticipations (seq);
        applySwing (seq);
        seq.normalise();
        enforceLeaps (seq);
        return seq;
    }

private:
    const ComposerParams& params;
    const StyleProfile& style;
    const InstrumentProfile& inst;
    Rng rng, motifRng;
    const int64_t beat, bar, total;
    double npbCap = 4.0;
    Motif primary, secondary;
    bool primaryFromSeed = false;

    // ------------------------------------------------------------------ escalas
    std::vector<int> scalePcsForChord (const Chord& chord) const
    {
        const Key& key = params.key;
        const bool minor = key.mode == Mode::Minor;
        std::vector<int> pcs;
        auto add = [&] (int iv) { pcs.push_back (mod12 (key.tonic + iv)); };
        switch (style.scale)
        {
            case ScaleKind::Pent:
                for (int iv : (minor ? std::vector<int> { 0, 3, 5, 7, 10 } : std::vector<int> { 0, 2, 4, 7, 9 })) add (iv);
                break;
            case ScaleKind::PentBlues:
                for (int iv : (minor ? std::vector<int> { 0, 3, 5, 6, 7, 10 } : std::vector<int> { 0, 2, 3, 4, 7, 9 })) add (iv);
                break;
            case ScaleKind::PentPlus:
                for (int iv : (minor ? std::vector<int> { 0, 2, 3, 5, 7, 10 } : std::vector<int> { 0, 2, 4, 7, 9, 11 })) add (iv);
                break;
            case ScaleKind::Diatonic:
            case ScaleKind::DiatonicHarm:
                for (int pc : key.scalePitchClasses()) pcs.push_back (pc);
                // Sobre la dominante mayor en tonalidad menor: 7ª elevada (menor armónica).
                if (style.scale == ScaleKind::DiatonicHarm && minor && mod12 (chord.root - key.tonic) == 7 && ! chord.isMinorish())
                {
                    for (auto& pc : pcs)
                        if (pc == mod12 (key.tonic + 10)) pc = mod12 (key.tonic + 11);
                }
                break;
        }
        // Las notas del acorde siempre están disponibles.
        for (int pc : chord.pitchClasses()) pcs.push_back (pc);
        std::sort (pcs.begin(), pcs.end());
        pcs.erase (std::unique (pcs.begin(), pcs.end()), pcs.end());
        return pcs;
    }

    std::vector<int> poolFor (const Chord& chord) const
    {
        const auto pcs = scalePcsForChord (chord);
        std::vector<int> pool;
        for (int n = inst.range.lowest; n <= inst.range.highest; ++n)
            if (std::find (pcs.begin(), pcs.end(), mod12 (n)) != pcs.end()) pool.push_back (n);
        return pool;
    }

    static int indexNear (const std::vector<int>& pool, int pitch)
    {
        auto it = std::lower_bound (pool.begin(), pool.end(), pitch);
        if (it == pool.end()) return (int) pool.size() - 1;
        if (it != pool.begin() && (pitch - *(it - 1)) < (*it - pitch)) --it;
        return (int) (it - pool.begin());
    }

    // ------------------------------------------------------------------ motivos
    double targetNpb (double intensity) const
    {
        const double d = std::clamp (params.density * 0.75 + intensity * 0.35 - 0.1, 0.0, 1.0);
        return std::min (npbCap, (style.npbMin + (style.npbMax - style.npbMin) * d) * inst.densityBias);
    }

    Motif motifFromLick (const Lick& lick) const
    {
        Motif m;
        int64_t t = 0;
        for (const auto& ln : lick)
        {
            const int64_t d = (int64_t) std::llround (ln.units16 * kPPQ / 4.0);
            m.notes.push_back ({ t, d, ln.step, ln.flags });
            t += d;
        }
        m.length = t;
        return m;
    }

    Motif proceduralMotif (Rng& r, double npb) const
    {
        // Ritmo: duraciones del vocabulario del estilo, ponderadas por la densidad deseada.
        struct D { int64_t ticks; int group; double npb; bool triplet; };
        const std::vector<D> vocab = { { 240, 1, 4, false }, { 480, 1, 2, false }, { 720, 1, 1.33, false }, { 960, 1, 1, false },
                                       { 1440, 1, 0.67, false }, { 1920, 1, 0.5, false }, { 320, 3, 3, true }, { 160, 6, 6, true } };
        Motif m;
        const int64_t length = (int64_t) style.motifBeats * beat;
        int64_t t = 0;
        while (t < length)
        {
            double w[8], sum = 0;
            for (size_t i = 0; i < vocab.size(); ++i)
            {
                const auto& v = vocab[i];
                const int64_t groupLen = v.ticks * v.group;
                double wi = std::exp (-std::pow (v.npb - npb, 2.0) / 1.0);
                if (v.triplet) wi *= v.group == 3 ? style.triplet * 2.0 : style.run * 1.5;
                if (groupLen > length - t) wi = 0;
                if (v.triplet && t % beat != 0) wi = 0; // los grupos de tresillo empiezan en el pulso
                if (v.npb > npbCap + 0.01) wi = 0;
                w[i] = wi;
                sum += wi;
            }
            size_t pick = 1;
            if (sum > 0)
            {
                double x = r.unit() * sum;
                for (size_t i = 0; i < vocab.size(); ++i) { x -= w[i]; if (x <= 0 && w[i] > 0) { pick = i; break; } }
            }
            const auto& v = vocab[pick];
            if (v.ticks > length - t) { m.notes.push_back ({ t, length - t, 0, 0 }); t = length; break; }
            for (int g = 0; g < v.group; ++g) { m.notes.push_back ({ t, v.ticks, 0, 0 }); t += v.ticks; }
        }
        // Síncopa: una nota a contratiempo se alarga ligándose sobre el pulso.
        if (r.chance (style.syncopation) && m.notes.size() >= 3)
        {
            for (size_t i = 0; i + 1 < m.notes.size(); ++i)
                if (m.notes[i].onset % beat == beat / 2 && m.notes[i].dur == beat / 2 && m.notes[i + 1].onset % beat == 0)
                {
                    m.notes[i].dur += m.notes[i + 1].dur;
                    m.notes.erase (m.notes.begin() + (long) i + 1);
                    break;
                }
        }
        // Contorno: grados conjuntos con saltos ocasionales y dirección dominante.
        int dir = r.chance (0.55) ? 1 : -1;
        for (size_t i = 0; i < m.notes.size(); ++i)
        {
            if (i == 0) { m.notes[i].step = 0; continue; }
            int s = r.chance (style.leapProb) ? r.range (2, std::max (2, style.maxLeapSteps)) : (r.chance (0.7) ? 1 : 0);
            if (r.chance (0.3)) dir = -dir;
            m.notes[i].step = s * dir;
        }
        // Nota final del motivo: objetivo armónico; larga => vibrato.
        if (! m.notes.empty())
        {
            auto& last = m.notes.back();
            last.flags |= F_TARGET;
            if (last.dur >= beat) last.flags |= F_VIB;
            if (last.dur >= beat && r.chance (style.bend)) last.flags |= F_BEND;
        }
        m.length = length;
        return m;
    }

    Motif motifFromSeed (const SeedMotif& sm) const
    {
        // Convierte intervalos en semitonos a pasos aproximados de escala (2 semitonos ~ 1 grado).
        Motif m;
        for (size_t i = 0; i < sm.notes.size(); ++i)
        {
            const auto& n = sm.notes[i];
            int step = 0;
            if (i > 0) step = (int) std::lround (n.interval / 1.75);
            if (i > 0 && step == 0 && n.interval != 0) step = n.interval > 0 ? 1 : -1;
            m.notes.push_back ({ n.onset, std::max<int64_t> (120, n.length), step, (uint8_t) (i > 0 ? F_LITERAL : 0), n.interval });
        }
        if (! m.notes.empty()) m.notes.back().flags |= F_TARGET;
        m.length = std::max<int64_t> (sm.lengthTicks, m.notes.empty() ? beat : m.notes.back().onset + m.notes.back().dur);
        // Cuantiza la longitud a pulsos
        m.length = ((m.length + beat - 1) / beat) * beat;
        return m;
    }

    void buildMotifs()
    {
        const auto& licks = lickFamily (style.lickFamily);
        const double npb = targetNpb (0.5);
        if (! params.seedMotifs.empty() && ! params.seedMotifs.front().notes.empty())
        {
            primary = motifFromSeed (params.seedMotifs.front());
            primaryFromSeed = true;
        }
        else if (motifRng.chance (style.lickUse))
            primary = motifFromLick (licks[(size_t) motifRng.range (0, (int) licks.size() - 1)]);
        else
            primary = proceduralMotif (motifRng, npb);

        if (params.seedMotifs.size() > 1 && ! params.seedMotifs[1].notes.empty())
            secondary = motifFromSeed (params.seedMotifs[1]);
        else if (motifRng.chance (style.lickUse * 0.8))
            secondary = motifFromLick (licks[(size_t) motifRng.range (0, (int) licks.size() - 1)]);
        else
            secondary = proceduralMotif (motifRng, npb * 1.15);
    }

    // Transformaciones temáticas
    static Motif invert (Motif m) { for (auto& n : m.notes) { n.step = -n.step; n.semis = -n.semis; } return m; }
    static Motif sequenceBy (Motif m, int steps) { if (! m.notes.empty()) m.notes.front().step += steps; return m; }
    Motif fragment (const Motif& m) const
    {
        Motif f;
        const int64_t half = std::max<int64_t> (beat, (m.length / 2 / beat) * beat);
        for (const auto& n : m.notes)
            if (n.onset < half) f.notes.push_back (n);
        if (! f.notes.empty())
        {
            auto& l = f.notes.back();
            l.dur = std::min (l.dur, half - l.onset);
            l.flags |= F_TARGET;
        }
        f.length = half;
        return f;
    }
    Motif augment (const Motif& m) const
    {
        Motif a = m;
        for (auto& n : a.notes) { n.onset *= 2; n.dur *= 2; }
        a.length *= 2;
        return a;
    }
    Motif diminish (const Motif& m) const
    {
        for (const auto& n : m.notes)
            if (n.dur < 240 || n.onset % 2) return m;
        Motif d = m;
        for (auto& n : d.notes) { n.onset /= 2; n.dur /= 2; }
        d.length = std::max<int64_t> (beat, d.length / 2);
        return d;
    }
    Motif varyRhythm (const Motif& m, Rng& r) const
    {
        // Variación ligera: parte una nota larga en dos o une dos cortas.
        Motif v = m;
        if (v.notes.size() < 2) return v;
        const size_t i = (size_t) r.range (0, (int) v.notes.size() - 1);
        auto& n = v.notes[i];
        if (n.dur >= beat && ! (n.flags & F_REST))
        {
            MNote extra = n;
            n.dur /= 2;
            extra.onset = n.onset + n.dur;
            extra.dur = n.dur;
            extra.step = r.chance (0.5) ? 1 : -1;
            extra.flags = (uint8_t) (n.flags & ~F_LITERAL);
            n.flags &= (uint8_t) ~(F_TARGET | F_VIB | F_BEND);
            v.notes.insert (v.notes.begin() + (long) i + 1, extra);
        }
        else if (i + 1 < v.notes.size() && ! (v.notes[i + 1].flags & F_REST))
        {
            n.dur += v.notes[i + 1].dur;
            n.flags |= v.notes[i + 1].flags & (F_TARGET | F_VIB);
            v.notes.erase (v.notes.begin() + (long) i + 1);
        }
        return v;
    }

    Motif developFor (SectionRole role, bool answer, int phraseIndex, Rng& r) const
    {
        const double freedom = primaryFromSeed ? params.freedom : 0.3 + 0.5 * params.complexity;
        const bool keepSeed = primaryFromSeed && r.chance (params.fidelity);
        switch (role)
        {
            case SectionRole::Presentation:
                if (! answer) return phraseIndex == 0 || keepSeed ? primary : varyRhythm (primary, r);
                return r.chance (0.5) ? invert (primary) : secondary;
            case SectionRole::Development:
                if (keepSeed) return sequenceBy (primary, answer ? -1 : 1);
                if (r.chance (freedom)) return answer ? invert (varyRhythm (secondary, r)) : sequenceBy (varyRhythm (primary, r), r.range (-2, 2));
                return answer ? secondary : sequenceBy (primary, 1);
            case SectionRole::Tension:
                return fragment (r.chance (0.6) ? primary : secondary);
            case SectionRole::Climax:
                return r.chance (style.run + 0.2) ? diminish (primary) : augment (fragment (primary));
            case SectionRole::Resolution:
                return keepSeed || r.chance (0.6) ? primary : fragment (primary);
        }
        return primary;
    }

    // ------------------------------------------------------------------ realización de frases
    void realizePhrase (Sequence& seq, int64_t ps, int64_t pe, const PlanSection& sec, bool answer, bool finalPhrase,
                        int phraseIndex, int& prevPitch)
    {
        const double intensity = sec.intensity;
        // Entrada: en el 1, anacrusa o desplazada (respiración y síncopa).
        int64_t cursor = ps;
        if (style.vocalSpace)
        {
            // Deja hueco para la voz; con material de la voz, el hueco se reduce para que quepa el motivo.
            int64_t gap = std::max<int64_t> (beat, ((pe - ps) / 2 / beat) * beat);
            if (primaryFromSeed) gap = std::max<int64_t> (beat, std::min (gap, ((pe - ps - primary.length - beat) / beat) * beat));
            cursor = ps + gap;
        }
        else if (phraseIndex > 0)
        {
            const double r = rng.unit();
            if (r < 0.25 + 0.25 * (1.0 - params.density)) cursor += beat / 2;
            else if (r < 0.40 + 0.25 * (1.0 - params.density)) cursor += beat;
        }
        // Silencio final de la frase (respiración), salvo en la última.
        int64_t rest = 0;
        if (! finalPhrase)
        {
            const double rb = style.restBeatsMin + (style.restBeatsMax - style.restBeatsMin) * rng.unit() * (1.2 - intensity);
            rest = std::clamp<int64_t> ((int64_t) std::llround (rb * 2.0) * (beat / 2), beat / 2, (pe - ps) / 2);
        }
        const int64_t soundEnd = finalPhrase ? pe - beat / 4 : pe - rest;
        if (soundEnd - cursor < beat) cursor = std::max (ps, soundEnd - beat);

        // Encadenar motivos hasta llenar la frase; la última nota se alarga como cadencia.
        std::vector<PhraseNote> notes;
        int64_t t = cursor;
        int guard = 0;
        bool first = true;
        while (t < soundEnd && guard++ < 16)
        {
            Motif m = first ? developFor (sec.role, answer, phraseIndex, rng)
                            : (rng.chance (style.repeat) ? varyRhythm (developFor (sec.role, answer, phraseIndex, rng), rng)
                                                         : sequenceBy (developFor (sec.role, answer, phraseIndex, rng), rng.range (-1, 2)));
            if (m.notes.empty() || m.length <= 0) break;
            for (const auto& n : m.notes)
            {
                const int64_t s = t + n.onset;
                if (s >= soundEnd) break;
                if (n.flags & F_REST) continue;
                NoteRole role = NoteRole::Free;
                if (n.dur <= kPPQ / 6) role = NoteRole::Run;
                notes.push_back ({ s, std::min (n.dur, soundEnd - s), n.step, n.flags, role, n.semis });
            }
            t += m.length;
            // Pequeña respiración entre repeticiones del motivo en frases largas.
            if (t < soundEnd && rng.chance (0.35 * (1.0 - intensity))) t += beat / 2;
            first = false;
        }
        if (notes.empty()) notes.push_back ({ cursor, std::max<int64_t> (beat, soundEnd - cursor), 0, F_TARGET, NoteRole::Free });

        // Clímax: carrera ascendente hacia la nota más aguda (si el estilo lo permite).
        if (sec.role == SectionRole::Climax && rng.chance (style.run + 0.15) && notes.size() >= 2)
        {
            for (size_t i = 0; i + 1 < notes.size(); ++i)
                if (notes[i].length <= beat / 2) { notes[i].step = std::abs (notes[i].step) == 0 ? 1 : std::abs (notes[i].step); }
        }

        // Cadencia: la última nota se prolonga hasta el final de la frase y cae en pulso si es posible.
        auto& last = notes.back();
        int64_t cadStart = last.start;
        if (cadStart % beat != 0 && (cadStart / beat) * beat > (notes.size() > 1 ? notes[notes.size() - 2].start : ps))
            cadStart = (cadStart / beat) * beat; // adelanta a pulso (anticipación de cadencia)
        last.start = cadStart;
        last.length = std::max<int64_t> (beat / 2, soundEnd - cadStart);
        last.flags |= F_TARGET;
        last.role = NoteRole::Cadence;
        if (last.length >= beat) last.flags |= F_VIB;
        if (sec.role == SectionRole::Climax || (sec.role == SectionRole::Tension && rng.chance (style.bend)))
            last.flags |= F_BEND;
        // Notas largas expresivas dentro de la frase.
        for (size_t i = 0; i + 1 < notes.size(); ++i)
            if ((notes[i].flags & F_TARGET) && rng.chance (style.longNote) && notes[i + 1].start - notes[i].start >= beat)
            {
                notes[i].length = notes[i + 1].start - notes[i].start;
                notes[i].flags |= F_VIB;
            }
        // Monofonía dentro de la frase.
        for (size_t i = 0; i + 1 < notes.size(); ++i)
            notes[i].length = std::max<int64_t> (60, std::min (notes[i].length, notes[i + 1].start - notes[i].start));

        realizePitches (seq, notes, sec, answer, finalPhrase, prevPitch);
    }

    int registerCenter (double intensity) const
    {
        const double lo = inst.range.comfortLow, hi = inst.range.comfortHigh;
        const double pos = style.registerLow + (style.registerHigh - style.registerLow) * std::clamp (intensity, 0.0, 1.0);
        return (int) std::lround (lo + (hi - lo) * pos);
    }

    void realizePitches (Sequence& seq, const std::vector<PhraseNote>& notes, const PlanSection& sec, bool answer,
                         bool finalPhrase, int& prevPitch)
    {
        const Key& key = params.key;
        const int lo = inst.range.lowest, hi = inst.range.highest;
        const int center = registerCenter (sec.intensity);
        int lastInterval = 0;
        int repeats = 0;
        const bool canBend = params.instrument == InstrumentId::Guitar || params.instrument == InstrumentId::GuitarLite
                             || params.instrument == InstrumentId::Synth;

        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& pn = notes[i];
            const int barIdx = (int) (pn.start / bar);
            const Chord& chord = params.progression.chordAtBar (barIdx);
            const auto pool = poolFor (chord);
            const auto chordPcs = chord.pitchClasses();
            int pitch;
            NoteRole role = pn.role;

            auto cadenceTargets = [&]() {
                std::vector<int> targets;
                if (finalPhrase) targets = { chord.containsPitchClass (key.tonic) ? key.tonic : chord.root };
                else if (answer || sec.role == SectionRole::Resolution) targets = { chord.root };
                else
                {
                    auto st2 = chord.stablePitchClasses();
                    targets.assign (st2.begin() + (st2.size() > 1 ? 1 : 0), st2.end());
                    // Tensión: termina en 3ª o 7ª si existe
                    if (sec.role == SectionRole::Tension && chord.intervals().size() > 3)
                        targets = { mod12 (chord.root + chord.intervals()[3]), mod12 (chord.root + chord.intervals()[1]) };
                }
                return targets;
            };
            const bool strongBeat = pn.start % bar == 0 || (params.timeSignature.numerator == 4 && pn.start % bar == 2 * beat);

            if (i == 0)
            {
                int target = center + rng.range (-2, 2);
                if (prevPitch > 0)
                {
                    // Continuidad con la frase anterior, con cambio de registro según la sección.
                    target = (prevPitch * 2 + target) / 3;
                    if (answer) target = prevPitch + (rng.chance (0.5) ? -2 : 2);
                }
                else if (primaryFromSeed && params.fidelity > 0.6 && ! params.seedMotifs.empty()
                         && params.seedMotifs.front().firstPitch > 0)
                {
                    // Material de la voz: empezar cerca de la altura cantada (en el registro del instrumento).
                    target = params.seedMotifs.front().firstPitch;
                    while (target < inst.range.comfortLow) target += 12;
                    while (target > inst.range.comfortHigh) target -= 12;
                }
                if (pn.role == NoteRole::Cadence)
                {
                    // Frase de una sola nota: es a la vez inicio y cadencia.
                    pitch = nearestPitchWithClass (target, cadenceTargets(), lo, hi);
                    role = NoteRole::Cadence;
                }
                else
                {
                    pitch = nearestPitchWithClass (target, chordPcs, lo, hi);
                    role = NoteRole::Target;
                }
            }
            else if ((pn.flags & F_LITERAL) && pn.role != NoteRole::Cadence && prevPitch > 0
                     && rng.chance (0.35 + 0.65 * params.fidelity))
            {
                // Intervalo literal del motivo semilla, ajustado a la escala del acorde.
                pitch = nearestPitchWithClass (prevPitch + pn.semis, scalePcsForChord (chord), lo, hi);
                if (strongBeat && ! chord.containsPitchClass (pitch) && rng.chance (1.0 - 0.7 * params.fidelity))
                    pitch = nearestPitchWithClass (pitch, chordPcs, lo, hi);
                role = NoteRole::Target;
            }
            else
            {
                int st = pn.step;
                if (std::abs (lastInterval) > 5 && std::abs (st) > 1) st = lastInterval > 0 ? -1 : 1; // relleno tras salto
                if (prevPitch - center > 8 && st > 0 && rng.chance (0.6)) st = -st;
                if (center - prevPitch > 8 && st < 0 && rng.chance (0.6)) st = -st;
                if (st == 0 && repeats >= 2) st = rng.chance (0.5) ? 1 : -1; // sin repeticiones mecánicas
                const int idx = std::clamp (indexNear (pool, prevPitch) + st, 0, (int) pool.size() - 1);
                pitch = pool[(size_t) idx];
                if ((pn.flags & F_LITERAL) && prevPitch > 0) pitch = prevPitch + pn.semis; // referencia: contorno de la voz

                const bool structural = (pn.flags & F_TARGET) || (strongBeat && pn.length >= beat / 2 && pn.role != NoteRole::Run);
                if (structural || pn.role == NoteRole::Cadence)
                {
                    std::vector<int> targets;
                    if (pn.role == NoteRole::Cadence) targets = cadenceTargets();
                    else if (pn.start % bar == 0) targets = chord.stablePitchClasses();
                    else targets = chordPcs;
                    pitch = nearestPitchWithClass (pitch, targets, lo, hi);
                    if (role == NoteRole::Free) role = NoteRole::Target;
                }
                else if (pn.role != NoteRole::Run && pn.length <= beat / 2 && rng.chance (style.chromatic * params.complexity))
                {
                    role = NoteRole::Passing; // posible aproximación cromática (se resuelve después)
                }
            }

            // Límite de salto del instrumento.
            if (prevPitch > 0)
            {
                while (pitch - prevPitch > inst.maxLeapSemitones) pitch -= 12;
                while (prevPitch - pitch > inst.maxLeapSemitones) pitch += 12;
                pitch = std::clamp (pitch, lo, hi);
            }
            if (sec.role == SectionRole::Climax && pn.role == NoteRole::Cadence) role = NoteRole::Climax;

            Note n;
            n.startTick = pn.start;
            n.lengthTicks = pn.length;
            n.pitch = pitch;
            n.role = role;

            // Bend hacia la nota objetivo: se pisa una nota más grave y se sube hasta ella.
            if ((pn.flags & F_BEND) && canBend && pn.length >= beat / 2)
            {
                const int targetPitch = pitch;
                int amount = 2;
                const auto scale = scalePcsForChord (chord);
                const bool belowInScale = std::find (scale.begin(), scale.end(), mod12 (targetPitch - 2)) != scale.end();
                if (! belowInScale) amount = 1;
                if (targetPitch - amount >= lo)
                {
                    n.pitch = targetPitch - amount;
                    n.expr.bendType = BendType::Up;
                    n.expr.bendSemitones = (float) amount;
                    n.expr.bendStart = 0.0f;
                    n.expr.bendTime = pn.length >= 2 * beat ? 0.18f : 0.32f;
                }
            }
            if (pn.flags & F_SLIDE) n.expr.slideFrom = -2;
            if (pn.flags & F_LEGATO) n.expr.legato = true;
            if (pn.flags & F_VIB) n.expr.vibratoDepth = 0.25f; // el intérprete ajusta profundidad y velocidad
            n.velocity = velocityFor (pn, sec, i, notes.size());

            lastInterval = prevPitch > 0 ? n.targetPitch() - prevPitch : 0;
            repeats = (n.targetPitch() == prevPitch) ? repeats + 1 : 0;
            prevPitch = n.targetPitch();
            seq.notes.push_back (n);
        }
        resolvePassing (seq);
    }

    void resolvePassing (Sequence& seq)
    {
        // Las notas "Passing" se convierten en aproximación cromática a la nota siguiente cuando es cercana.
        auto& v = seq.notes;
        for (size_t i = 0; i + 1 < v.size(); ++i)
        {
            if (v[i].role != NoteRole::Passing) continue;
            const int target = v[i + 1].pitch;
            const int approach = target + (v[i].pitch < target ? -1 : 1);
            if (v[i].endTick() + 10 >= v[i + 1].startTick && std::abs (approach - v[i].pitch) <= 3
                && approach >= inst.range.lowest && approach <= inst.range.highest)
                v[i].pitch = approach;
        }
    }

    int velocityFor (const PhraseNote& pn, const PlanSection& sec, size_t i, size_t count)
    {
        double v = style.velBase - style.velRange * 0.4 + style.velRange * 0.8 * sec.intensity * (0.6 + 0.4 * params.energy);
        const int64_t inBar = pn.start % bar;
        if (inBar == 0) v += 7;
        else if (inBar % beat != 0) v -= 6;
        if (pn.flags & F_ACCENT) v += 10;
        if (pn.role == NoteRole::Run) v -= 8 - 10.0 * (double) i / std::max<size_t> (1, count);
        const double pos = count > 1 ? (double) i / (double) (count - 1) : 0.0;
        v += 6.0 * std::sin (3.14159265 * std::min (1.0, pos * 1.25));
        v += rng.range (-3, 3);
        return std::clamp ((int) std::lround (v), 25, 127);
    }

    // ------------------------------------------------------------------ ajustes globales
    void applyAnticipations (Sequence& seq)
    {
        // Notas objetivo en el 1 que se adelantan una corchea (o semicorchea) y se ligan sobre la barra.
        auto& v = seq.notes;
        for (size_t i = 1; i < v.size(); ++i)
        {
            auto& n = v[i];
            if (n.startTick % bar != 0 || n.startTick == 0 || n.locked) continue;
            if (n.role != NoteRole::Target && n.role != NoteRole::Cadence) continue;
            if (! rng.chance (style.anticipation * (0.5 + params.complexity))) continue;
            const int64_t shift = rng.chance (0.7) ? beat / 2 : beat / 4;
            const int64_t gap = n.startTick - v[i - 1].endTick();
            if (gap >= beat / 2)
            {
                if (gap - shift < beat / 2) continue; // no comerse la respiración entre frases
            }
            else
            {
                // Dentro de la frase: se acorta la nota anterior para dejar sitio.
                const int64_t need = v[i - 1].endTick() - (n.startTick - shift);
                if (need > 0)
                {
                    if (v[i - 1].locked || v[i - 1].lengthTicks - need < 120) continue;
                    v[i - 1].lengthTicks -= need;
                }
            }
            n.startTick -= shift;
            n.lengthTicks += shift;
            n.role = NoteRole::Anticipation;
        }
    }

    void applySwing (Sequence& seq)
    {
        if (style.swing <= 0.0) return;
        const int64_t shift = (int64_t) std::llround (style.swing * kPPQ / 6.0); // hasta 160 ticks (2:1)
        auto& v = seq.notes;
        for (size_t i = 0; i < v.size(); ++i)
        {
            auto& n = v[i];
            if (n.startTick % beat != beat / 2 || n.lengthTicks > beat / 2) continue;
            const int64_t grid = (shift / 80) * 80; // mantiene la rejilla de 80 ticks
            if (grid <= 0) continue;
            if (i > 0 && v[i - 1].endTick() > n.startTick) continue;
            n.startTick += grid;
            n.lengthTicks = std::max<int64_t> (80, n.lengthTicks - grid);
            if (i > 0 && v[i - 1].endTick() == n.startTick - grid) v[i - 1].lengthTicks += grid;
        }
    }

    void enforceLeaps (Sequence& seq)
    {
        auto& v = seq.notes;
        for (size_t i = 1; i < v.size(); ++i)
        {
            if (v[i].locked) continue;
            const int prev = v[i - 1].targetPitch();
            while (v[i].pitch - prev > inst.maxLeapSemitones + 2 && v[i].pitch - 12 >= inst.range.lowest) v[i].pitch -= 12;
            while (prev - v[i].pitch > inst.maxLeapSemitones + 2 && v[i].pitch + 12 <= inst.range.highest) v[i].pitch += 12;
        }
    }
};

SectionRole roleFromChar (char c)
{
    switch (c)
    {
        case 'P': case 'p': return SectionRole::Presentation;
        case 'D': case 'd': return SectionRole::Development;
        case 'T': case 't': return SectionRole::Tension;
        case 'C': case 'c': return SectionRole::Climax;
        default: return SectionRole::Resolution;
    }
}

double baseIntensity (SectionRole r)
{
    switch (r)
    {
        case SectionRole::Presentation: return 0.35;
        case SectionRole::Development: return 0.55;
        case SectionRole::Tension: return 0.75;
        case SectionRole::Climax: return 1.0;
        case SectionRole::Resolution: return 0.45;
    }
    return 0.5;
}

bool parsePlan (const std::string& text, int bars, std::vector<PlanSection>& out)
{
    std::istringstream ss (text);
    std::string tok;
    int b = 0;
    std::vector<PlanSection> v;
    while (ss >> tok)
    {
        if (tok.size() < 2 || std::string ("PDTCRpdtcr").find (tok[0]) == std::string::npos) return false;
        const int n = std::atoi (tok.c_str() + 1);
        if (n <= 0) return false;
        v.push_back ({ b, n, roleFromChar (tok[0]), baseIntensity (roleFromChar (tok[0])) });
        b += n;
    }
    if (b != bars || v.empty()) return false;
    out = v;
    return true;
}
} // namespace

// ======================================================================= API
std::vector<PlanSection> SoloComposer::plan (const ComposerParams& p)
{
    std::vector<PlanSection> s;
    if (! p.sectionPlan.empty() && parsePlan (p.sectionPlan, p.bars, s)) {}
    else
    {
        Rng r (p.seed * 0x51ull + 3);
        std::vector<std::string> options;
        switch (p.bars)
        {
            case 1: options = { "R1" }; break;
            case 2: options = { "P1 R1" }; break;
            case 3: options = { "P1 C1 R1" }; break;
            case 4: options = { "P1 D1 C1 R1", "P2 C1 R1", "P1 T1 C1 R1" }; break;
            case 8: options = { "P2 D2 T2 C1 R1", "P2 D2 C2 R2", "P2 T2 D1 C2 R1", "P2 D1 T2 C2 R1" }; break;
            case 16: options = { "P4 D4 T4 C2 R2", "P4 D4 T2 C4 R2", "P4 D2 T4 C4 R2" }; break;
            default:
            {
                // Proporcional: 25% presentación, 25% desarrollo, 20% tensión, 15% clímax, resto resolución.
                const int n = p.bars;
                int pB = std::max (1, n / 4), dB = std::max (1, n / 4), tB = std::max (0, n / 5), cB = std::max (1, n * 15 / 100);
                int rB = n - pB - dB - tB - cB;
                while (rB < 1) { if (dB > 1) --dB; else if (tB > 0) --tB; else if (pB > 1) --pB; else break; rB = n - pB - dB - tB - cB; }
                std::string plan = "P" + std::to_string (pB) + " D" + std::to_string (dB);
                if (tB > 0) plan += " T" + std::to_string (tB);
                plan += " C" + std::to_string (cB) + " R" + std::to_string (std::max (1, rB));
                options = { plan };
            }
        }
        const auto& choice = options[(size_t) r.range (0, (int) options.size() - 1)];
        if (! parsePlan (choice, p.bars, s)) s = { { 0, p.bars, SectionRole::Presentation, 0.5 } };
    }
    const double e = std::clamp (p.energy, 0.0, 1.0);
    for (auto& sec : s) sec.intensity = std::clamp (sec.intensity * (0.65 + 0.7 * e), 0.05, 1.0);
    return s;
}

std::vector<std::string> SoloComposer::validate (const ComposerParams& p)
{
    std::vector<std::string> errors;
    if (! p.timeSignature.isSupported()) errors.push_back ("Compás no soportado (solo 4/4 y 3/4)");
    if (p.bars < 1 || p.bars > 64) errors.push_back ("Duración fuera de rango (1..64 compases)");
    if (p.progression.bars.empty()) errors.push_back ("La progresión está vacía");
    if (! (p.bpm >= 20.0 && p.bpm <= 400.0)) errors.push_back ("BPM fuera de rango (20..400)");
    if (! p.sectionPlan.empty())
    {
        std::vector<PlanSection> tmp;
        if (! parsePlan (p.sectionPlan, p.bars, tmp)) errors.push_back ("Plan de secciones no válido (ej.: P2 D2 T2 C1 R1, debe sumar la duración)");
    }
    return errors;
}

Sequence SoloComposer::compose (const ComposerParams& params)
{
    if (! validate (params).empty())
    {
        Sequence empty;
        empty.timeSignature = params.timeSignature;
        empty.bars = std::clamp (params.bars, 1, 64);
        empty.bpm = params.bpm;
        return empty;
    }
    ComposerParams p = params;
    p.energy = std::clamp (p.energy, 0.0, 1.0);
    p.density = std::clamp (p.density, 0.0, 1.0);
    p.complexity = std::clamp (p.complexity, 0.0, 1.0);
    p.fidelity = std::clamp (p.fidelity, 0.0, 1.0);
    p.freedom = std::clamp (p.freedom, 0.0, 1.0);
    if (p.engine == 1) return legacyCompose (p);
    ComposerV2 c (p);
    Sequence s = c.run();
    performSequence (s, p, resolvePerformer (p.style, p.performer, p.instrument), p.seed ^ 0xA5A5ull);
    return s;
}

namespace
{
// Combina: conserva de 'current' lo protegido y toma de 'fresh' el resto, sin solapamientos.
Sequence mergeProtected (const Sequence& current, const Sequence& fresh, const std::vector<bool>& keepBar,
                         const std::vector<int>& replaceIdx, bool useIdx)
{
    const int64_t barT = current.timeSignature.ticksPerBar();
    auto barOf = [&] (const Note& n) { return (int) (n.startTick / barT); };
    auto barKept = [&] (int b) { return b >= 0 && b < (int) keepBar.size() && keepBar[(size_t) b]; };

    Sequence out = current;
    out.notes.clear();
    std::vector<Note> kept;
    for (size_t i = 0; i < current.notes.size(); ++i)
    {
        const auto& n = current.notes[i];
        bool keep;
        if (useIdx) keep = n.locked || std::find (replaceIdx.begin(), replaceIdx.end(), (int) i) == replaceIdx.end();
        else keep = n.locked || barKept (barOf (n));
        if (keep) kept.push_back (n);
    }
    std::vector<Note> added;
    if (useIdx)
    {
        // Ventana temporal de las notas a sustituir
        for (int idx : replaceIdx)
        {
            if (idx < 0 || idx >= (int) current.notes.size()) continue;
            const auto& old = current.notes[(size_t) idx];
            if (old.locked) continue;
            for (const auto& n : fresh.notes)
                if (n.startTick >= old.startTick && n.startTick < old.endTick()) added.push_back (n);
        }
    }
    else
        for (const auto& n : fresh.notes)
            if (! barKept (barOf (n))) added.push_back (n);
    // Las notas nuevas no pueden solapar a las conservadas: se recortan o descartan.
    std::vector<Note> filtered;
    for (auto n : added)
    {
        bool drop = false;
        for (const auto& k : kept)
        {
            if (n.startTick < k.endTick() && k.startTick < n.endTick())
            {
                if (n.startTick < k.startTick) n.lengthTicks = k.startTick - n.startTick;
                else { drop = true; break; }
            }
        }
        if (! drop && n.lengthTicks >= 40)
        {
            n.locked = false;
            filtered.push_back (n);
        }
    }
    out.notes = kept;
    out.notes.insert (out.notes.end(), filtered.begin(), filtered.end());
    std::stable_sort (out.notes.begin(), out.notes.end(), [] (const Note& a, const Note& b) { return a.startTick < b.startTick; });
    // Recorta solo notas NO protegidas para garantizar monofonía.
    for (size_t i = 0; i + 1 < out.notes.size(); ++i)
    {
        auto& a = out.notes[i];
        const auto& b = out.notes[i + 1];
        if (a.endTick() > b.startTick && ! a.locked && ! (useIdx ? false : barKept (barOf (a))))
            a.lengthTicks = std::max<int64_t> (1, b.startTick - a.startTick);
    }
    return out;
}
} // namespace

Sequence SoloComposer::regenerate (const Sequence& current, const ComposerParams& params, uint64_t newSeed,
                                   const std::vector<bool>& lockedBars, int fromBar, int toBar)
{
    ComposerParams p = params;
    p.seed = newSeed;
    p.bars = current.bars;
    p.timeSignature = current.timeSignature;
    const Sequence fresh = compose (p);
    std::vector<bool> keep ((size_t) current.bars, false);
    for (int b = 0; b < current.bars; ++b)
    {
        const bool locked = b < (int) lockedBars.size() && lockedBars[(size_t) b];
        const bool outside = fromBar >= 0 && (b < fromBar || b > (toBar < 0 ? fromBar : toBar));
        keep[(size_t) b] = locked || outside;
    }
    return mergeProtected (current, fresh, keep, {}, false);
}

Sequence SoloComposer::regenerateNotes (const Sequence& current, const ComposerParams& params, uint64_t newSeed,
                                        const std::vector<int>& noteIndices)
{
    ComposerParams p = params;
    p.seed = newSeed;
    p.bars = current.bars;
    p.timeSignature = current.timeSignature;
    const Sequence fresh = compose (p);
    return mergeProtected (current, fresh, {}, noteIndices, true);
}

Sequence SoloComposer::regenerateBars (const Sequence& current, const ComposerParams& params, int fromBar, int toBar,
                                       uint64_t newSeed)
{
    return regenerate (current, params, newSeed, {}, fromBar, toBar);
}

} // namespace solo
