// ChorusComposer (V4-C). Ver ChorusComposer.h.
#include "solo/ChorusComposer.h"
#include <algorithm>
#include <cmath>

namespace solo
{
namespace
{
int mod12 (int v) { return ((v % 12) + 12) % 12; }

bool barLocked (const std::vector<bool>& locks, int b) { return b >= 0 && b < (int) locks.size() && locks[(size_t) b]; }

// Altura del acorde/escala más cercana a 'pitch' dentro de [lo, hi].
int fitPitch (int pitch, const std::vector<int>& pcs, int lo, int hi)
{
    if (pcs.empty()) return std::clamp (pitch, lo, hi);
    return nearestPitchWithClass (std::clamp (pitch, lo, hi), pcs, lo, hi);
}
} // namespace

void chorusRange (ChorusCharacter c, int& low, int& high, int& maxLeap)
{
    switch (c)
    {
        case ChorusCharacter::EasyToSing: low = 55; high = 72; maxLeap = 5; break;  // G3-C5
        case ChorusCharacter::Emotional: low = 53; high = 74; maxLeap = 7; break;   // F3-D5
        case ChorusCharacter::Energetic: low = 57; high = 76; maxLeap = 7; break;   // A3-E5
    }
}

Sequence shapeChorus (const Sequence& in, const ComposerParams& params, const Progression& prog,
                      const ChorusShapeOptions& o, const std::vector<bool>& lockedBars)
{
    Sequence s = in;
    if (s.notes.empty() || prog.bars.empty()) return s;
    const int64_t bar = s.timeSignature.ticksPerBar(), beat = s.timeSignature.ticksPerBeat();
    const int bars = s.bars;
    int lo = params.rangeLow, hi = params.rangeHigh, leap = params.maxLeap;
    if (lo < 0 || hi <= lo) chorusRange (o.character, lo, hi, leap);
    const auto scale = params.key.scalePitchClasses();
    const std::vector<int> scalePcs (scale.begin(), scale.end());
    auto chordAt = [&] (int b) { return prog.chordAtBar (std::clamp (b, 0, (int) prog.bars.size() - 1)); };
    auto protectedNote = [&] (const Note& n) { return n.locked || barLocked (lockedBars, (int) (n.startTick / bar)); };

    // 2) Registro vocal y saltos (solo notas no protegidas). Se aplica ahora y otra vez al final.
    auto enforceRange = [&] () {
        int prev = -1;
        for (auto& n : s.notes)
        {
            if (! protectedNote (n))
            {
                while (n.pitch < lo) n.pitch += 12;
                while (n.pitch > hi) n.pitch -= 12;
                if (prev >= 0 && leap > 0 && std::abs (n.pitch - prev) > leap)
                {
                    // nota de la escala más cercana a la original dentro de [prev-leap, prev+leap] y del registro
                    const int wlo = std::max (lo, prev - leap), whi = std::min (hi, prev + leap);
                    n.pitch = whi >= wlo ? fitPitch (std::clamp (n.pitch, wlo, whi), scalePcs, wlo, whi) : std::clamp (prev, lo, hi);
                    if (std::abs (n.pitch - prev) > leap) n.pitch = std::clamp (prev, lo, hi);
                }
                const int pc = mod12 (n.pitch);
                if (std::find (scalePcs.begin(), scalePcs.end(), pc) == scalePcs.end() && ! chordAt ((int) (n.startTick / bar)).containsPitchClass (pc))
                {
                    const int wlo = prev >= 0 && leap > 0 ? std::max (lo, prev - leap) : lo, whi = prev >= 0 && leap > 0 ? std::min (hi, prev + leap) : hi;
                    if (whi >= wlo) n.pitch = fitPitch (n.pitch, scalePcs, wlo, whi);
                }
            }
            prev = n.pitch;
        }
    };
    enforceRange();

    // 3) Notas demasiado cortas para cantar: se funden con la siguiente (misma altura) o se alargan.
    int64_t minLen = o.minNoteTicks;
    if (minLen <= 0) minLen = o.character == ChorusCharacter::EasyToSing ? beat / 2 : beat / 4;
    if (minLen > 1)
    {
        std::vector<Note> out;
        for (size_t i = 0; i < s.notes.size(); ++i)
        {
            Note n = s.notes[i];
            if (! protectedNote (n) && n.lengthTicks < minLen)
            {
                if (! out.empty() && ! protectedNote (out.back()) && out.back().endTick() >= n.startTick - beat / 8 && (i % 2 == 1))
                { out.back().lengthTicks = std::max (out.back().lengthTicks, n.endTick() - out.back().startTick); continue; } // se funde con la anterior
                n.lengthTicks = minLen;
            }
            out.push_back (n);
        }
        s.notes = out;
        s.normalise();
    }

    // 4) Respiraciones: al final de cada frase de 2 compases, hueco de al menos medio pulso.
    if (o.breathe)
    {
        for (int ph = 2; ph <= bars; ph += 2)
        {
            const int64_t boundary = (int64_t) ph * bar;
            const int64_t gapStart = boundary - beat / 2;
            for (auto& n : s.notes)
            {
                if (protectedNote (n) || n.startTick >= boundary) continue;
                if (n.endTick() > gapStart && n.startTick < gapStart) n.lengthTicks = std::max<int64_t> (beat / 4, gapStart - n.startTick);
            }
            std::vector<Note> out;
            for (const auto& n : s.notes)
                if (protectedNote (n) || ! (n.startTick >= gapStart && n.startTick < boundary && n.lengthTicks < beat)) out.push_back (n);
            s.notes = out;
        }
        s.normalise();
    }

    // 1) (tras dar forma a la primera mitad) Frases repetibles: la segunda mitad repite la primera adaptada a sus acordes.
    if (o.mirrorHalves && bars >= 4 && bars % 2 == 0)
    {
        const int half = bars / 2;
        bool secondHalfFree = true;
        for (int b = half; b < bars; ++b) if (barLocked (lockedBars, b)) secondHalfFree = false;
        for (const auto& n : s.notes) if (n.locked && n.startTick >= half * bar) secondHalfFree = false;
        if (secondHalfFree)
        {
            std::vector<Note> out;
            for (const auto& n : s.notes) if (n.startTick < half * bar) out.push_back (n);
            const size_t firstCount = out.size();
            for (size_t i = 0; i < firstCount; ++i)
            {
                Note n = out[i];
                n.startTick += half * bar;
                const int b = (int) (n.startTick / bar);
                const Chord c = chordAt (b);
                const Chord c0 = chordAt (b - half);
                // Misma función: si la nota era del acorde original, pasa a la nota del acorde nuevo más cercana;
                // si era de paso, se mantiene (ajustada a la escala).
                if (c0.containsPitchClass (mod12 (n.pitch)) && ! (c == c0))
                    n.pitch = fitPitch (n.pitch, c.pitchClasses(), lo, hi);
                else
                    n.pitch = fitPitch (n.pitch, scalePcs, lo, hi);
                out.push_back (n);
            }
            s.notes = out;
        }
    }

    // 5) Resolución: la última nota cae en una nota del acorde final (tónica si está cerca) y dura al menos un pulso.
    if (o.resolveEnding && ! s.notes.empty())
    {
        auto& last = s.notes.back();
        if (! protectedNote (last))
        {
            const Chord c = chordAt (bars - 1);
            const int tonicPc = params.key.tonic;
            std::vector<int> pcs = c.pitchClasses();
            int target = fitPitch (last.pitch, pcs, lo, hi);
            if (c.containsPitchClass (tonicPc))
            {
                const int t2 = fitPitch (last.pitch, { tonicPc }, lo, hi);
                if (std::abs (t2 - last.pitch) <= 4) target = t2;
            }
            last.pitch = target;
            last.expr.bendType = BendType::None; last.expr.bendSemitones = 0; last.expr.slideFrom = 0;
            last.role = NoteRole::Cadence;
            const int64_t end = (int64_t) bars * bar - beat / 8;
            last.lengthTicks = std::max<int64_t> (beat, std::min<int64_t> (last.lengthTicks * 2, end - last.startTick));
            if (last.startTick + last.lengthTicks > (int64_t) bars * bar) last.lengthTicks = (int64_t) bars * bar - last.startTick;
        }
    }
    enforceRange();
    s.normalise();
    return s;
}

SingabilityReport singability (const Sequence& s, const Progression& prog, const Key& k)
{
    (void) k;
    SingabilityReport r;
    if (s.notes.empty()) return r;
    const int64_t bar = s.timeSignature.ticksPerBar(), beat = s.timeSignature.ticksPerBeat();
    r.lowest = 127; r.highest = 0;
    int prev = -1, shortN = 0;
    for (const auto& n : s.notes)
    {
        r.lowest = std::min (r.lowest, n.pitch); r.highest = std::max (r.highest, n.pitch);
        if (prev >= 0) r.maxLeap = std::max (r.maxLeap, std::abs (n.pitch - prev));
        prev = n.pitch;
        if (n.lengthTicks < beat / 2) ++shortN;
    }
    r.shortNoteRatio = (double) shortN / (double) s.notes.size();
    int gaps = 0, phrases = 0;
    for (int ph = 2; ph <= s.bars; ph += 2)
    {
        ++phrases;
        const int64_t boundary = (int64_t) ph * bar;
        int64_t lastEnd = 0, nextStart = boundary + bar;
        for (const auto& n : s.notes)
        {
            if (n.startTick < boundary) lastEnd = std::max (lastEnd, n.endTick());
            else nextStart = std::min (nextStart, n.startTick);
        }
        if (nextStart - lastEnd >= beat / 2) ++gaps;
    }
    r.breathGapsPerPhrase = phrases > 0 ? (double) gaps / phrases : 0;
    if (! prog.bars.empty())
        r.endsOnChordTone = prog.chordAtBar (std::min (s.bars - 1, (int) prog.bars.size() - 1)).containsPitchClass (mod12 (s.notes.back().targetPitch()));
    if (s.bars >= 4 && s.bars % 2 == 0)
    {
        Sequence a = s, b = s;
        a.notes.clear(); b.notes.clear();
        const int64_t half = (int64_t) (s.bars / 2) * bar;
        for (const auto& n : s.notes) { if (n.startTick < half) a.notes.push_back (n); else { Note m = n; m.startTick -= half; b.notes.push_back (m); } }
        r.repeatedHalfSimilarity = melodySimilarity (a, b).overall;
    }
    return r;
}
} // namespace solo
