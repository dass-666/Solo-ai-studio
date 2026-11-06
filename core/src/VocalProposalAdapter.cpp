// VocalProposalAdapter (V4-E). Ver VocalProposalAdapter.h.
#include "solo/VocalProposalAdapter.h"
#include "solo/ChorusComposer.h"
#include <algorithm>

namespace solo
{
namespace
{
HumToSoloOptions humOpts (const VocalProposalOptions& o, HumMode m, bool rhythmOnly)
{
    HumToSoloOptions h;
    h.mode = m; h.rhythmOnly = rhythmOnly; h.quantize = o.quantize; h.gridTicks = o.gridTicks;
    h.startBar = o.startBar; h.voiceStartSec = o.voiceStartSec; h.fidelity = o.fidelity; h.freedom = o.freedom;
    return h;
}
} // namespace

VocalProposalSet proposeFromVocal (const VoiceAnalysis& va, const MelodyContext& ctx, const MelodyBrief& brief, const VocalProposalOptions& o)
{
    VocalProposalSet set;
    set.pitchReliable = va.pitchReliable;
    if (va.notes.empty()) { set.warnings.push_back ("La toma no contiene notas"); return set; }
    if (ctx.bars() <= 0) { set.warnings.push_back ("No hay armonía sobre la que proponer"); return set; }
    ComposerParams base = MelodyExplorer::baseParams (ctx, brief);
    base.seed = o.seed;
    const bool rhythmOnly = ! va.pitchReliable;
    set.usedRhythmOnly = rhythmOnly;
    if (rhythmOnly) set.warnings.push_back ("Altura de la voz poco fiable: se usa solo su ritmo; las notas las pone la armonía");
    for (const auto& w : va.warnings) set.warnings.push_back (w);
    const bool chorus = brief.type == MelodyType::Chorus;
    const std::vector<bool> noLocks;

    // 1) Original: tal cual (Copy). No se toca la armonía ni se da forma de coro.
    {
        ComposerParams p = base;
        p.rangeLow = -1; p.rangeHigh = -1; p.maxLeap = -1; // el registro del instrumento, no el del coro: no altera intervalos
        const auto res = humToSolo (va, p, humOpts (o, HumMode::Copy, rhythmOnly));
        VocalProposal v; v.name = "Original (tal cual)"; v.description = rhythmOnly ? "Tu ritmo con notas de la armonía (altura no fiable)" : "Tu melodía cuantizada, sin corregir";
        v.mode = HumMode::Copy; v.original = true; v.sequence = res.sequence; v.similarityToOriginal = 1.0;
        set.notesFromVoice = res.notesFromVoice > 0 ? res.notesFromVoice : (int) res.sequence.notes.size();
        set.motif = res.motif;
        for (const auto& w : res.warnings) set.warnings.push_back (w);
        if (v.sequence.notes.empty()) { set.warnings.push_back ("La toma no cae dentro de los compases de la sección"); return set; }
        set.proposals.push_back (v);
    }
    const Sequence original = set.proposals.front().sequence; // copia: el vector se realoja al añadir propuestas

    // 2) Corregida con la armonía (Correct): mismo ritmo, alturas al acorde/escala.
    {
        const auto res = humToSolo (va, base, humOpts (o, HumMode::Correct, rhythmOnly));
        VocalProposal v; v.name = "Corregida con la armonía"; v.description = "Mismo ritmo; las notas fuera de la armonía del beat se ajustan";
        v.mode = HumMode::Correct; v.sequence = res.sequence;
        if (chorus) { ChorusShapeOptions c; c.character = brief.character; c.mirrorHalves = false; v.sequence = shapeChorus (v.sequence, base, base.progression, c, noLocks); }
        v.similarityToOriginal = melodySimilarity (v.sequence, original).overall;
        if (! v.sequence.notes.empty()) set.proposals.push_back (v);
    }
    // 3) Desarrollada (Develop): el compositor desarrolla el motivo cantado.
    {
        const auto res = humToSolo (va, base, humOpts (o, HumMode::Develop, rhythmOnly));
        VocalProposal v; v.name = "Desarrollada"; v.description = "El compositor desarrolla tu motivo a lo largo de la sección";
        v.mode = HumMode::Develop; v.sequence = res.sequence;
        if (chorus) { ChorusShapeOptions c; c.character = brief.character; v.sequence = shapeChorus (v.sequence, base, base.progression, c, noLocks); }
        v.similarityToOriginal = melodySimilarity (v.sequence, original).overall;
        if (! v.sequence.notes.empty()) set.proposals.push_back (v);
    }
    // 4) Combinada con el beat: propuesta del explorador con tu motivo como identidad (acentos, energía, plan).
    if (! set.motif.notes.empty())
    {
        MelodyIdentity id;
        id.motif = set.motif;
        id.motifSeed = o.seed * 0x9E37ull + 101ull;
        id.description = "Motivo cantado";
        const MelodyProposal mp = MelodyExplorer::proposeOne (ctx, brief, id, ProposalStrategy::Rhythmic);
        VocalProposal v; v.name = "Combinada con el beat"; v.description = "Tu motivo, desarrollado siguiendo los acentos y la energía del beat";
        v.mode = HumMode::Develop; v.sequence = mp.sequence; v.strategy = (int) ProposalStrategy::Rhythmic;
        v.similarityToOriginal = melodySimilarity (v.sequence, original).overall;
        if (! v.sequence.notes.empty()) set.proposals.push_back (v);
    }
    return set;
}

Sequence correctRegionWithVoice (const Sequence& current, const VoiceAnalysis& take, const ComposerParams& familyParams,
                                 const MelodyBrief& brief, int fromBar, int toBar, const std::vector<bool>& lockedBars,
                                 double quantize, HumMode mode)
{
    HumToSoloOptions o;
    o.mode = mode; o.quantize = quantize; o.rhythmOnly = ! take.pitchReliable;
    Sequence out = applyVocalCorrection (current, take, familyParams, o, fromBar, toBar, lockedBars);
    if (brief.type == MelodyType::Chorus)
    {
        ChorusShapeOptions c; c.character = brief.character; c.mirrorHalves = false;
        std::vector<bool> keep ((size_t) std::max (0, current.bars), false);
        for (size_t b = 0; b < keep.size(); ++b) keep[b] = ((int) b < fromBar || (int) b > toBar) || (b < lockedBars.size() && lockedBars[b]);
        out = shapeChorus (out, familyParams, familyParams.progression, c, keep);
    }
    return out;
}
} // namespace solo
