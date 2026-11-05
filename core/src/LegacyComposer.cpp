// Compositor S0–S2 ("motor clásico"). Se conserva sin cambios de comportamiento para
// comparaciones A/B y para reproducir semillas antiguas.
#include "solo/SoloComposer.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace solo
{
namespace
{
enum class LStyle { Melodic = 0, Rock = 1, Blues = 2, Electronic = 3 };
int mod12 (int v) { return ((v % 12) + 12) % 12; }

struct Cell
{
    // Duraciones en fracciones de pulso (1 pulso = ticksPerBeat). Negativo = silencio.
    std::vector<double> parts;
    int beats;
    double notesPerBeat;
    bool triplet;
};

const std::vector<Cell>& cellLibrary()
{
    static const std::vector<Cell> cells = {
        { { 1.0 }, 1, 1.0, false },                       // negra
        { { -1.0 }, 1, 0.0, false },                      // silencio de negra
        { { 0.5, 0.5 }, 1, 2.0, false },                  // dos corcheas
        { { -0.5, 0.5 }, 1, 1.0, false },                 // contratiempo
        { { 0.75, 0.25 }, 1, 2.0, false },                // corchea con puntillo + semicorchea
        { { 0.5, 0.25, 0.25 }, 1, 3.0, false },           // corchea + 2 semicorcheas
        { { 0.25, 0.25, 0.5 }, 1, 3.0, false },           // 2 semicorcheas + corchea
        { { 0.25, 0.25, 0.25, 0.25 }, 1, 4.0, false },    // 4 semicorcheas
        { { 1.0 / 3, 1.0 / 3, 1.0 / 3 }, 1, 3.0, true },  // tresillo de corcheas
        { { 2.0 / 3, 1.0 / 3 }, 1, 2.0, true },           // shuffle
        { { 2.0 }, 2, 0.5, false },                       // blanca
        { { 1.5, 0.5 }, 2, 1.0, false },                  // negra con puntillo + corchea
        { { 0.5, 1.0, 0.5 }, 2, 1.5, false },             // síncopa
    };
    return cells;
}

double styleCellWeight (LStyle s, const Cell& c)
{
    switch (s)
    {
        case LStyle::Blues: return c.triplet ? 2.2 : (c.notesPerBeat >= 4.0 ? 0.4 : 1.0);
        case LStyle::Rock: return c.triplet ? 0.4 : (c.parts.size() == 3 && ! c.triplet && c.beats == 2 ? 1.6 : 1.0);
        case LStyle::Electronic: return c.triplet ? 0.1 : (c.notesPerBeat >= 2.0 ? 1.4 : 0.8);
        case LStyle::Melodic: return c.triplet ? 0.5 : (c.notesPerBeat <= 2.0 ? 1.3 : 0.6);
    }
    return 1.0;
}

struct Slot
{
    int64_t start;
    int64_t length;
    bool strong;    // tiempo fuerte (1 y 3 en 4/4, 1 en 3/4)
    bool onBeat;
    bool cadence;   // última nota de la frase
};

struct PhrasePlan
{
    int64_t start, end; // límites de la frase (compases completos)
    bool response;
    bool final;
};

class Composer
{
public:
    Composer (const ComposerParams& p, LStyle st)
        : params (p), lstyle (st), profile (profileFor (p.instrument)), rng (p.seed),
          beat (p.timeSignature.ticksPerBeat()), bar (p.timeSignature.ticksPerBar()),
          total ((int64_t) p.bars * bar)
    {
        buildPitchPool();
        const double maxNotesPerBeat = profile.maxNotesPerSecond * 60.0 / std::max (20.0, p.bpm);
        const double wanted = (0.6 + 2.8 * p.density) * profile.densityBias * (0.8 + 0.4 * p.energy);
        targetNotesPerBeat = std::min (wanted, maxNotesPerBeat);
    }

    Sequence run()
    {
        Sequence seq;
        seq.timeSignature = params.timeSignature;
        seq.bars = params.bars;
        seq.bpm = params.bpm;

        const auto phrases = planPhrases();
        std::vector<Cell> callMotif;
        std::vector<int> callContour;
        int prevPitch = -1;

        for (size_t i = 0; i < phrases.size(); ++i)
        {
            const auto& ph = phrases[i];
            std::vector<Cell> motif = ph.response && ! callMotif.empty() ? callMotif : makeMotif();
            std::vector<int> contour = ph.response && ! callContour.empty() ? invert (callContour) : makeContour (ph);
            if (! ph.response) { callMotif = motif; callContour = contour; }

            const auto slots = buildRhythm (ph, motif);
            placePitches (seq, ph, slots, contour, prevPitch);
        }
        addApproachNotes (seq);
        seq.normalise();
        return seq;
    }

private:
    const ComposerParams& params;
    const LStyle lstyle;
    const InstrumentProfile& profile;
    Rng rng;
    const int64_t beat, bar, total;
    double targetNotesPerBeat = 2.0;
    std::vector<int> pool; // alturas MIDI disponibles (escala del estilo) dentro del rango

    // ------------------------------------------------------------ escalas
    std::vector<int> stylePitchClasses() const
    {
        const auto& key = params.key;
        std::vector<int> pcs;
        const bool minor = key.mode == Mode::Minor;
        switch (lstyle)
        {
            case LStyle::Rock:
            case LStyle::Blues:
            {
                // Pentatónica (menor o mayor) + nota de blues en el estilo Blues.
                const std::vector<int> minorPent { 0, 3, 5, 7, 10 }, majorPent { 0, 2, 4, 7, 9 };
                for (int iv : (minor ? minorPent : majorPent)) pcs.push_back (mod12 (key.tonic + iv));
                if (lstyle == LStyle::Blues) pcs.push_back (mod12 (key.tonic + (minor ? 6 : 3)));
                // Con complejidad alta se completan los grados diatónicos.
                if (params.complexity > 0.65)
                    for (int pc : key.scalePitchClasses()) pcs.push_back (pc);
                break;
            }
            case LStyle::Melodic:
            case LStyle::Electronic:
                for (int pc : key.scalePitchClasses()) pcs.push_back (pc);
                break;
        }
        std::sort (pcs.begin(), pcs.end());
        pcs.erase (std::unique (pcs.begin(), pcs.end()), pcs.end());
        return pcs;
    }

    void buildPitchPool()
    {
        const auto pcs = stylePitchClasses();
        for (int n = profile.range.lowest; n <= profile.range.highest; ++n)
            if (std::find (pcs.begin(), pcs.end(), mod12 (n)) != pcs.end()) pool.push_back (n);
    }

    int poolIndexNear (int pitch) const
    {
        auto it = std::lower_bound (pool.begin(), pool.end(), pitch);
        if (it == pool.end()) return (int) pool.size() - 1;
        if (it != pool.begin() && (pitch - *(it - 1)) < (*it - pitch)) --it;
        return (int) (it - pool.begin());
    }

    // ------------------------------------------------------------ forma
    std::vector<PhrasePlan> planPhrases()
    {
        std::vector<PhrasePlan> out;
        int b = 0;
        bool nextIsResponse = false;
        while (b < params.bars)
        {
            const int left = params.bars - b;
            int len;
            if (params.bars <= 2) len = 1;
            else
            {
                const double w1 = 0.25 + 0.2 * params.density, w2 = 0.6, w4 = 0.1 + 0.3 * params.complexity;
                const double r = rng.unit() * (w1 + w2 + w4);
                len = r < w1 ? 1 : (r < w1 + w2 ? 2 : 4);
                // Llamada y respuesta con la misma longitud.
                if (nextIsResponse && ! out.empty()) len = (int) ((out.back().end - out.back().start) / bar);
            }
            // Siempre al menos dos frases (llamada y respuesta) y nunca más de 'maxPhraseBars'
            // ni de 2 compases sin respirar en solos cortos.
            const int cap = params.bars >= 8 ? profile.maxPhraseBars : std::max (1, params.bars / 2);
            len = std::max (1, std::min ({ len, left, cap }));
            out.push_back ({ (int64_t) b * bar, (int64_t) (b + len) * bar, nextIsResponse, false });
            nextIsResponse = ! nextIsResponse;
            b += len;
        }
        out.back().final = true;
        return out;
    }

    // ------------------------------------------------------------ ritmo
    const Cell& pickCell (int maxBeats)
    {
        const auto& lib = cellLibrary();
        double weights[32] {};
        double sum = 0.0;
        for (size_t i = 0; i < lib.size(); ++i)
        {
            const auto& c = lib[i];
            if (c.beats > maxBeats) continue;
            const double d = c.notesPerBeat - targetNotesPerBeat;
            double w = std::exp (-(d * d) / 1.2) * styleCellWeight (lstyle, c);
            if (c.notesPerBeat == 0.0) w = 0.08 * (1.0 - params.density); // silencios dentro de la frase
            if (c.notesPerBeat > targetNotesPerBeat + 1.01) w = 0.0;     // límite de interpretabilidad
            weights[i] = w;
            sum += w;
        }
        if (sum <= 0.0) return lib[0];
        double r = rng.unit() * sum;
        for (size_t i = 0; i < lib.size(); ++i)
        {
            r -= weights[i];
            if (r <= 0.0 && weights[i] > 0.0) return lib[i];
        }
        return lib[0];
    }

    std::vector<Cell> makeMotif()
    {
        // Motivo de 2 pulsos (4/4) o 3 pulsos (3/4).
        const int motifBeats = params.timeSignature.numerator == 3 ? 3 : 2;
        std::vector<Cell> m;
        int filled = 0;
        while (filled < motifBeats)
        {
            const auto& c = pickCell (motifBeats - filled);
            if (filled == 0 && c.notesPerBeat == 0.0) continue; // el motivo empieza con sonido
            m.push_back (c);
            filled += c.beats;
        }
        return m;
    }

    std::vector<Slot> buildRhythm (const PhrasePlan& ph, const std::vector<Cell>& motif)
    {
        std::vector<Slot> slots;
        // Entrada: en el 1, anacrusa tras una corchea o tras un pulso.
        int64_t cursor = ph.start;
        {
            const double r = rng.unit();
            const double late = (ph.response ? 0.45 : 0.25) * (1.2 - params.density);
            if (r < late * 0.5) cursor += beat;
            else if (r < late) cursor += beat / 2;
        }
        // Respiración al final de la frase.
        int64_t endRest = 0;
        if (! ph.final)
        {
            const double breath = 0.5 + 1.5 * (1.0 - params.density) * rng.unit();
            endRest = (int64_t) std::llround (breath * 2.0) * (beat / 2);
            endRest = std::clamp<int64_t> (endRest, beat / 2, 2 * beat);
        }
        const int64_t soundEnd = ph.final ? ph.end - beat / 4 : ph.end - endRest;
        // Nota de cadencia: 1-2 pulsos, empezando en un pulso.
        const int64_t cadenceLen = ph.final ? std::max<int64_t> (beat * 2, bar / 2) : (rng.chance (0.5) ? beat : beat * 2);
        int64_t cadenceStart = ((soundEnd - cadenceLen) / beat) * beat;
        if (cadenceStart <= cursor) cadenceStart = std::max (cursor, ((soundEnd - beat) / beat) * beat);

        auto addSlot = [&] (int64_t start, int64_t len, bool cadence) {
            if (len <= 0 || start >= soundEnd) return;
            len = std::min (len, soundEnd - start);
            const int64_t inBar = start % bar;
            const bool onBeat = inBar % beat == 0;
            const int beatIdx = (int) (inBar / beat);
            const bool strong = onBeat && (beatIdx == 0 || (params.timeSignature.numerator == 4 && beatIdx == 2));
            slots.push_back ({ start, len, strong, onBeat, cadence });
        };

        // Relleno hasta el siguiente pulso si la frase empieza a contratiempo.
        if (cursor % beat != 0 && cursor < cadenceStart)
        {
            const int64_t toBeat = beat - cursor % beat;
            if (targetNotesPerBeat > 2.5 && toBeat >= beat / 2) { addSlot (cursor, toBeat / 2, false); addSlot (cursor + toBeat / 2, toBeat / 2, false); }
            else addSlot (cursor, toBeat, false);
            cursor += toBeat;
        }

        size_t motifPos = 0;
        int repetition = 0;
        while (cursor < cadenceStart)
        {
            const int beatsLeft = (int) ((cadenceStart - cursor) / beat);
            if (beatsLeft <= 0) break;
            Cell cell = motif[motifPos % motif.size()];
            // Variación: con la complejidad sube la probabilidad de cambiar la célula repetida.
            const bool vary = repetition > 0 && rng.chance (0.15 + 0.5 * params.complexity
                                                               - (lstyle == LStyle::Electronic ? 0.15 : 0.0));
            if (vary || cell.beats > beatsLeft) cell = pickCell (beatsLeft);
            double offset = 0.0;
            for (double part : cell.parts)
            {
                const int64_t len = (int64_t) std::llround (std::abs (part) * (double) beat);
                if (part > 0) addSlot (cursor + (int64_t) std::llround (offset * (double) beat), len, false);
                offset += std::abs (part);
            }
            cursor += (int64_t) cell.beats * beat;
            if (++motifPos % motif.size() == 0) ++repetition;
        }
        addSlot (cadenceStart, soundEnd - cadenceStart, true);
        return slots;
    }

    // ------------------------------------------------------------ melodía
    std::vector<int> makeContour (const PhrasePlan& ph)
    {
        // Pasos en grados de la escala para hasta 24 notas; se recorre cíclicamente.
        std::vector<int> steps;
        const int dir = ph.response ? -1 : 1;
        int run = 0;
        for (int i = 0; i < 24; ++i)
        {
            int s;
            const double leapP = profile.leapProbability * (0.6 + 0.8 * params.complexity);
            if (rng.chance (leapP)) s = rng.range (3, 4 + (int) (params.complexity * 2));
            else s = rng.chance (0.75) ? 1 : 2;
            // Tendencia direccional con cambios ocasionales (arco melódico).
            int sign = rng.chance (0.62) ? dir : -dir;
            if (++run > 4) { sign = -sign; run = 0; }
            if (rng.chance (0.08 + 0.1 * (1.0 - params.complexity))) s = 0; // nota repetida ocasional
            steps.push_back (sign * s);
        }
        return steps;
    }

    static std::vector<int> invert (const std::vector<int>& c)
    {
        std::vector<int> out (c.size());
        for (size_t i = 0; i < c.size(); ++i) out[i] = -c[i];
        return out;
    }

    // Centro de registro según la posición en el solo: sube hasta el clímax (~75%) y se relaja al final.
    int registerCenter (int64_t tick) const
    {
        const double t = (double) tick / (double) std::max<int64_t> (1, total);
        const double arc = t < 0.75 ? t / 0.75 : 1.0 - (t - 0.75) / 0.25 * 0.45;
        const double lo = profile.range.comfortLow, hi = profile.range.comfortHigh;
        const double pos = 0.25 + 0.45 * arc * (0.6 + 0.4 * params.energy);
        return (int) std::lround (lo + (hi - lo) * pos);
    }

    void placePitches (Sequence& seq, const PhrasePlan& ph, const std::vector<Slot>& slots,
                       const std::vector<int>& contour, int& prevPitch)
    {
        if (slots.empty() || pool.empty()) return;
        const Key& key = params.key;
        int lastInterval = 0;
        int repeatCount = 0;
        size_t step = 0;
        const int lo = profile.range.lowest, hi = profile.range.highest;

        for (size_t i = 0; i < slots.size(); ++i)
        {
            const auto& s = slots[i];
            const int barIdx = (int) (s.start / bar);
            const Chord& chord = params.progression.chordAtBar (barIdx);
            const auto chordPcs = chord.pitchClasses();
            int pitch;

            if (i == 0)
            {
                // Inicio de frase: nota del acorde cerca del centro de registro (o cerca de la frase anterior).
                int target = registerCenter (s.start) + rng.range (-3, 3);
                if (prevPitch > 0 && std::abs (prevPitch - target) > 7) target = (prevPitch + target) / 2;
                pitch = nearestPitchWithClass (target, chordPcs, lo, hi);
            }
            else if (s.cadence)
            {
                // Resolución: frase final -> tónica si pertenece al acorde, si no fundamental;
                // respuesta -> fundamental; llamada -> 3ª o 5ª (final "abierto").
                std::vector<int> targets;
                if (ph.final) targets = { chord.containsPitchClass (key.tonic) ? key.tonic : chord.root };
                else if (ph.response) targets = { chord.root };
                else { auto st = chord.stablePitchClasses(); targets.assign (st.begin() + (st.size() > 1 ? 1 : 0), st.end()); }
                pitch = nearestPitchWithClass (prevPitch, targets, lo, hi);
                // Evitar un salto final mayor que el permitido.
                if (std::abs (pitch - prevPitch) > profile.maxLeapSemitones)
                    pitch += pitch > prevPitch ? -12 : 12;
            }
            else
            {
                int st = contour[step++ % contour.size()];
                // Relleno tras salto: después de un salto grande, movimiento conjunto en sentido contrario.
                if (std::abs (lastInterval) > 5) st = lastInterval > 0 ? -1 : 1;
                // Atracción al centro de registro.
                const int center = registerCenter (s.start);
                if (prevPitch - center > 7 && st > 0 && rng.chance (0.7)) st = -st;
                if (center - prevPitch > 7 && st < 0 && rng.chance (0.7)) st = -st;
                // Evitar repeticiones mecánicas de la misma altura.
                if (st == 0 && repeatCount >= 1) st = rng.chance (0.5) ? 1 : -1;

                int idx = std::clamp (poolIndexNear (prevPitch) + st, 0, (int) pool.size() - 1);
                pitch = pool[(size_t) idx];

                const bool longNote = s.length >= beat;
                if (s.strong || longNote)
                {
                    // Notas estructurales: nota del acorde (estable en el primer tiempo).
                    const bool downbeat = s.start % bar == 0;
                    auto pcs = downbeat ? chord.stablePitchClasses() : chordPcs;
                    if (! downbeat && params.complexity > 0.5 && rng.chance (params.complexity - 0.4))
                        for (int t : chord.tensionPitchClasses (key)) pcs.push_back (t);
                    pitch = nearestPitchWithClass (pitch, pcs, lo, hi);
                }
                else if (s.onBeat && ! key.containsPitchClass (pitch) && ! chord.containsPitchClass (pitch))
                {
                    // Nota de blues en parte fuerte de pulso: se resuelve a la escala.
                    const auto sc = key.scalePitchClasses();
                    pitch = nearestPitchWithClass (pitch, std::vector<int> (sc.begin(), sc.end()), lo, hi);
                }
            }

            // Limitar el salto máximo del instrumento.
            if (prevPitch > 0)
            {
                while (pitch - prevPitch > profile.maxLeapSemitones) pitch -= 12;
                while (prevPitch - pitch > profile.maxLeapSemitones) pitch += 12;
                pitch = std::clamp (pitch, lo, hi);
            }

            lastInterval = prevPitch > 0 ? pitch - prevPitch : 0;
            repeatCount = (pitch == prevPitch) ? repeatCount + 1 : 0;
            prevPitch = pitch;

            Note n;
            n.startTick = s.start;
            const double legato = s.cadence ? 1.0 : profile.legato * (lstyle == LStyle::Rock && rng.chance (0.2 * params.energy) ? 0.6 : 1.0);
            n.lengthTicks = std::max<int64_t> (kPPQ / 16, (int64_t) std::llround ((double) s.length * legato));
            n.pitch = pitch;
            n.velocity = velocityFor (s, ph, i, slots.size());
            seq.notes.push_back (n);
        }
    }

    int velocityFor (const Slot& s, const PhrasePlan& ph, size_t i, size_t count)
    {
        double v = 58.0 + 45.0 * params.energy;
        if (s.strong) v += 8.0;
        else if (! s.onBeat) v -= 6.0;
        const double pos = count > 1 ? (double) i / (double) (count - 1) : 0.0;
        v += 9.0 * std::sin (3.14159265 * std::min (1.0, pos * 1.3)); // arco de frase
        if (i == 0) v += 4.0;
        if (s.cadence && ph.final) v -= 4.0;
        if (s.length <= beat / 4 && ! s.onBeat) v -= 5.0; // semicorcheas más ligeras
        v += rng.range (-5, 5);
        return std::clamp ((int) std::lround (v), 20, 127);
    }

    void addApproachNotes (Sequence& seq)
    {
        // Notas de aproximación cromática hacia notas estructurales (solo con complejidad).
        if (params.complexity < 0.3) return;
        auto& n = seq.notes;
        for (size_t i = 1; i < n.size(); ++i)
        {
            const bool strongTarget = n[i].startTick % bar == 0 || n[i].lengthTicks >= beat;
            const bool adjacent = n[i - 1].startTick + (int64_t) (n[i - 1].lengthTicks / profile.legato) + 2 >= n[i].startTick;
            const bool shortPrev = n[i - 1].lengthTicks <= beat / 2;
            if (! strongTarget || ! adjacent || ! shortPrev) continue;
            if (! rng.chance (0.35 * params.complexity)) continue;
            const int from = n[i - 1].pitch, target = n[i].pitch;
            const int approach = target + (from < target ? -1 : 1);
            if (std::abs (approach - from) <= 4 && approach != target
                && approach >= profile.range.lowest && approach <= profile.range.highest)
                n[i - 1].pitch = approach;
        }
    }
};
} // namespace

Sequence legacyCompose (const ComposerParams& p)
{
    int ls = p.legacyStyle;
    if (ls < 0)
    {
        switch (p.style)
        {
            case Style::MelodicRock: case Style::VirtuosoRock: ls = 1; break;
            case Style::ExpressiveBlues: ls = 2; break;
            default: ls = 0; break;
        }
    }
    Composer c (p, (LStyle) std::clamp (ls, 0, 3));
    return c.run();
}

} // namespace solo
