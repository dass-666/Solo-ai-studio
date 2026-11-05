#include "solo/StateManager.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>

namespace solo
{
namespace
{
const char* kStateMagic = "SOLOAISTUDIO-STATE";
const char* kPresetMagic = "SOLOAISTUDIO-PRESET";

std::string fmt (double v)
{
    std::ostringstream o;
    o.imbue (std::locale::classic());
    o.precision (10);
    o << v;
    return o.str();
}

bool toDouble (const std::string& s, double& v)
{
    std::istringstream i (s);
    i.imbue (std::locale::classic());
    i >> v;
    return ! i.fail() && std::isfinite (v);
}

bool toInt64 (const std::string& s, int64_t& v)
{
    std::istringstream i (s);
    i.imbue (std::locale::classic());
    i >> v;
    return ! i.fail();
}

std::string oneLine (std::string v)
{
    for (auto& c : v)
        if (c == '\n' || c == '\r') c = ' ';
    return v;
}

void writeSettings (std::ostringstream& o, const SoloSettings& s)
{
    o << "key=" << oneLine (s.keyText) << "\n";
    o << "progression=" << oneLine (s.progressionText) << "\n";
    o << "ts=" << s.tsNumerator << "\n";
    o << "bars=" << s.bars << "\n";
    o << "instrument=" << (int) s.instrument << "\n";
    o << "style=" << (int) s.style << "\n";
    o << "energy=" << fmt (s.energy) << "\n";
    o << "density=" << fmt (s.density) << "\n";
    o << "complexity=" << fmt (s.complexity) << "\n";
    o << "seed=" << s.seed << "\n";
    o << "manualBpm=" << fmt (s.manualBpm) << "\n";
    o << "useHostTempo=" << (s.useHostTempo ? 1 : 0) << "\n";
    o << "syncToHost=" << (s.syncToHost ? 1 : 0) << "\n";
    o << "startBar=" << s.startBar << "\n";
    o << "loop=" << (s.loop ? 1 : 0) << "\n";
    o << "outputGain=" << fmt (s.outputGain) << "\n";
    o << "proMode=" << (s.proMode ? 1 : 0) << "\n";
    o << "proTab=" << s.proTab << "\n";
    o << "wavSampleRate=" << s.wavSampleRate << "\n";
    o << "midiFormat=" << s.midiFormat << "\n";
    o << "midiPitchBend=" << (s.midiPitchBend ? 1 : 0) << "\n";
    o << "engine=" << s.engine << "\n";
    o << "legacyStyle=" << s.legacyStyle << "\n";
    o << "performer=" << (int) s.performer << "\n";
    o << "motifSeed=" << s.motifSeed << "\n";
    o << "keepMotifs=" << (s.keepMotifs ? 1 : 0) << "\n";
    o << "sectionPlan=" << oneLine (s.sectionPlan) << "\n";
    o << "fidelity=" << fmt (s.fidelity) << "\n";
    o << "freedom=" << fmt (s.freedom) << "\n";
    o << "fxPreset=" << oneLine (s.fxPreset) << "\n";
    o << "fxEnabled=" << (s.fxEnabled ? 1 : 0) << "\n";
}

// Aplica pares clave=valor validando rangos. Las claves desconocidas se ignoran (compatibilidad futura).
bool applySetting (SoloSettings& s, const std::string& k, const std::string& v, std::string& error, int version = 2)
{
    double d = 0;
    int64_t i = 0;
    auto num = [&] { if (! toDouble (v, d)) { error = "Valor no numérico en " + k; return false; } return true; };
    auto integer = [&] (int64_t lo, int64_t hi) {
        if (! toInt64 (v, i) || i < lo || i > hi) { error = "Valor fuera de rango en " + k; return false; }
        return true;
    };
    if (k == "key") s.keyText = v;
    else if (k == "progression") s.progressionText = v;
    else if (k == "ts") { if (! integer (3, 4)) return false; s.tsNumerator = (int) i; }
    else if (k == "bars") { if (! integer (1, 64)) return false; s.bars = (int) i; }
    else if (k == "instrument")
    {
        if (version < 2)
        {
            // En S0–S2 la guitarra era el modelo ligero actual.
            if (! integer (0, 2)) return false;
            s.instrument = i == 0 ? InstrumentId::GuitarLite : (InstrumentId) i;
        }
        else { if (! integer (0, kNumInstruments - 1)) return false; s.instrument = (InstrumentId) i; }
    }
    else if (k == "legacyStyle") { if (! integer (-1, 3)) return false; s.legacyStyle = (int) i; }
    else if (k == "style")
    {
        if (version < 2) { if (! integer (0, 3)) return false; s.style = styleFromV1 ((int) i); s.legacyStyle = (int) i; }
        else { if (! integer (0, kNumStyles - 1)) return false; s.style = (Style) i; }
    }
    else if (k == "energy") { if (! num()) return false; s.energy = std::clamp (d, 0.0, 1.0); }
    else if (k == "density") { if (! num()) return false; s.density = std::clamp (d, 0.0, 1.0); }
    else if (k == "complexity") { if (! num()) return false; s.complexity = std::clamp (d, 0.0, 1.0); }
    else if (k == "seed") { s.seed = std::strtoull (v.c_str(), nullptr, 10); }
    else if (k == "manualBpm") { if (! num() || d < 20 || d > 400) { error = "BPM inválido"; return false; } s.manualBpm = d; }
    else if (k == "useHostTempo") { if (! integer (0, 1)) return false; s.useHostTempo = i == 1; }
    else if (k == "syncToHost") { if (! integer (0, 1)) return false; s.syncToHost = i == 1; }
    else if (k == "startBar") { if (! integer (1, 9999)) return false; s.startBar = (int) i; }
    else if (k == "loop") { if (! integer (0, 1)) return false; s.loop = i == 1; }
    else if (k == "outputGain") { if (! num()) return false; s.outputGain = std::clamp (d, 0.0, 2.0); }
    else if (k == "proMode") { if (! integer (0, 1)) return false; s.proMode = i == 1; }
    else if (k == "proTab") { if (! integer (0, 15)) return false; s.proTab = (int) i; }
    else if (k == "wavSampleRate") { if (! integer (8000, 192000)) return false; s.wavSampleRate = (int) i; }
    else if (k == "midiFormat") { if (! integer (0, 1)) return false; s.midiFormat = (int) i; }
    else if (k == "midiPitchBend") { if (! integer (0, 1)) return false; s.midiPitchBend = i == 1; }
    else if (k == "engine") { if (! integer (1, 2)) return false; s.engine = (int) i; }
    else if (k == "performer") { if (! integer (0, kNumPerformers - 1)) return false; s.performer = (PerformerProfile) i; }
    else if (k == "motifSeed") { s.motifSeed = std::strtoull (v.c_str(), nullptr, 10); }
    else if (k == "keepMotifs") { if (! integer (0, 1)) return false; s.keepMotifs = i == 1; }
    else if (k == "sectionPlan") { s.sectionPlan = v; }
    else if (k == "fidelity") { if (! num()) return false; s.fidelity = std::clamp (d, 0.0, 1.0); }
    else if (k == "freedom") { if (! num()) return false; s.freedom = std::clamp (d, 0.0, 1.0); }
    else if (k == "fxPreset") { s.fxPreset = v; }
    else if (k == "fxEnabled") { if (! integer (0, 1)) return false; s.fxEnabled = i == 1; }
    return true;
}

std::vector<std::string> lines (const std::string& text)
{
    std::vector<std::string> out;
    std::istringstream in (text);
    std::string l;
    while (std::getline (in, l))
    {
        if (! l.empty() && l.back() == '\r') l.pop_back();
        out.push_back (l);
    }
    return out;
}

void writeNote (std::ostringstream& o, const Note& n)
{
    const auto& e = n.expr;
    o << "n " << n.startTick << " " << n.lengthTicks << " " << n.pitch << " " << n.velocity << " " << n.channel << " "
      << (int) e.bendType << " " << fmt (e.bendSemitones) << " " << fmt (e.bendStart) << " " << fmt (e.bendTime) << " "
      << fmt (e.vibratoDepth) << " " << fmt (e.vibratoRate) << " " << fmt (e.vibratoDelay) << " " << (int) e.slideFrom << " "
      << (e.legato ? 1 : 0) << " " << (e.palmMute ? 1 : 0) << " " << (int) e.attack << " " << (int) n.role << " "
      << (n.locked ? 1 : 0) << "\n";
}

bool readNote (const std::string& line, int version, Note& n)
{
    std::istringstream nl (line);
    nl.imbue (std::locale::classic());
    std::string tag;
    nl >> tag >> n.startTick >> n.lengthTicks >> n.pitch >> n.velocity >> n.channel;
    if (nl.fail() || tag != "n" || n.lengthTicks <= 0 || n.startTick < 0 || n.pitch < 0 || n.pitch > 127
        || n.velocity < 1 || n.velocity > 127 || n.channel < 0 || n.channel > 15)
        return false;
    if (version < 2) return true;
    int bt = 0, slide = 0, leg = 0, pm = 0, att = 64, role = 0, lk = 0;
    float bs = 0, bst = 0, btm = 0, vd = 0, vr = 0, vdl = 0;
    nl >> bt >> bs >> bst >> btm >> vd >> vr >> vdl >> slide >> leg >> pm >> att >> role >> lk;
    if (nl.fail() || bt < 0 || bt > 4 || ! std::isfinite (bs) || bs < 0 || bs > 4 || ! (bst >= 0 && bst <= 1)
        || ! (btm >= 0 && btm <= 1) || ! (vd >= 0 && vd <= 2) || ! (vr >= 0 && vr <= 20) || ! (vdl >= 0 && vdl <= 1)
        || slide < -12 || slide > 12 || att < 0 || att > 127 || role < 0 || role > 6)
        return false;
    n.expr.bendType = (BendType) bt;
    n.expr.bendSemitones = bs;
    n.expr.bendStart = bst;
    n.expr.bendTime = btm;
    n.expr.vibratoDepth = vd;
    n.expr.vibratoRate = vr;
    n.expr.vibratoDelay = vdl;
    n.expr.slideFrom = (int8_t) slide;
    n.expr.legato = leg != 0;
    n.expr.palmMute = pm != 0;
    n.expr.attack = (uint8_t) att;
    n.role = (NoteRole) role;
    n.locked = lk != 0;
    return true;
}
} // namespace

Style styleFromV1 (int v1)
{
    switch (v1)
    {
        case 0: return Style::Emotional;       // Melódico
        case 1: return Style::MelodicRock;     // Rock
        case 2: return Style::ExpressiveBlues; // Blues
        case 3: return Style::VirtuosoRock;    // Electrónico (no hay equivalente directo)
        default: return Style::MelodicRock;
    }
}

bool SoloSettings::toComposerParams (double bpm, ComposerParams& p, std::string& error) const
{
    if (! Key::parse (keyText, p.key)) { error = "Tonalidad no válida: '" + keyText + "'"; return false; }
    if (! Progression::parse (progressionText, p.progression, error)) return false;
    p.timeSignature = { tsNumerator, 4 };
    p.bars = bars;
    p.instrument = instrument;
    p.style = style;
    p.energy = energy;
    p.density = density;
    p.complexity = complexity;
    p.seed = seed;
    p.bpm = bpm;
    p.engine = engine;
    p.legacyStyle = engine == 1 ? legacyStyle : -1;
    p.performer = performer;
    p.motifSeed = keepMotifs ? motifSeed : 0;
    p.sectionPlan = sectionPlan;
    p.fidelity = fidelity;
    p.freedom = freedom;
    const auto errs = SoloComposer::validate (p);
    if (! errs.empty()) { error = errs.front(); return false; }
    return true;
}

SoloState::SoloState()
{
    const int i = fxPresetIndex (settings.fxPreset);
    if (i >= 0) fx = fxPresets()[(size_t) i].settings;
}

std::string serializeState (const SoloState& s)
{
    std::ostringstream o;
    o.imbue (std::locale::classic());
    o << kStateMagic << " " << SoloState::kVersion << "\n";
    writeSettings (o, s.settings);
    o << serializeFx (s.fx);
    o << "lockedBars=";
    for (bool b : s.lockedBars) o << (b ? '1' : '0');
    o << "\n";
    o << "ab=" << s.slotA << " " << s.slotB << "\n";
    o << "dna=" << oneLine (s.dnaSummary) << "\n";
    for (const auto& lane : s.automation.lanes)
    {
        o << "auto " << (int) lane.target << " " << (lane.enabled ? 1 : 0) << " " << fmt (lane.smoothingMs) << " "
          << lane.points.size();
        for (const auto& p : lane.points) o << " " << p.tick << " " << fmt (p.value);
        o << "\n";
    }
    for (const auto& m : s.seedMotifs)
    {
        o << "motif " << m.lengthTicks << " " << m.firstPitch << " " << m.notes.size();
        for (const auto& n : m.notes) o << " " << n.onset << " " << n.length << " " << n.interval;
        o << "\n";
    }
    const auto& items = s.history.all();
    o << "variations=" << items.size() << " " << s.history.currentIndex() << " " << s.history.nextIdValue() << "\n";
    for (const auto& it : items)
    {
        const auto& q = it.sequence;
        o << "var " << it.seed << " " << q.bars << " " << q.timeSignature.numerator << " " << fmt (q.bpm) << " "
          << q.notes.size() << " " << it.id << " " << (it.favorite ? 1 : 0) << " " << oneLine (it.name) << "\n";
        for (const auto& n : q.notes) writeNote (o, n);
    }
    o << "end\n";
    return o.str();
}

bool deserializeState (const std::string& text, SoloState& out, std::string& error)
{
    const auto ls = lines (text);
    if (ls.empty()) { error = "Estado vacío"; return false; }
    int version = 0;
    {
        std::istringstream h (ls[0]);
        std::string magic;
        h >> magic >> version;
        if (magic != kStateMagic) { error = "No es un estado de Solo AI Studio"; return false; }
        if (version < 1) { error = "Versión de estado inválida"; return false; }
        if (version > SoloState::kVersion) { error = "Estado creado por una versión más nueva (" + std::to_string (version) + ")"; return false; }
    }
    SoloState s; // se construye aparte y solo se copia si todo es válido
    if (version < 2)
    {
        // S0–S2 no tenía efectos ni intérprete: se conserva el sonido "seco" y el compositor antiguo
        // para que el proyecto suene igual que cuando se guardó.
        s.settings.engine = 1;
        s.settings.fxEnabled = false;
        s.settings.fxPreset.clear();
        s.settings.instrument = InstrumentId::GuitarLite;
    }
    std::vector<VariationHistory::Item> items;
    int wantedVariations = 0, currentIdx = -1;
    uint32_t nextId = 0;
    bool sawEnd = false;
    for (size_t li = 1; li < ls.size(); ++li)
    {
        const auto& l = ls[li];
        if (l.empty()) continue;
        if (l == "end") { sawEnd = true; break; }
        if (l.rfind ("variations=", 0) == 0)
        {
            std::istringstream v (l.substr (11));
            v >> wantedVariations >> currentIdx;
            if (v.fail() || wantedVariations < 0 || wantedVariations > VariationHistory::kMaxTotal)
            { error = "Cabecera de variaciones inválida"; return false; }
            if (version >= 2) v >> nextId;
            continue;
        }
        if (l.rfind ("var ", 0) == 0)
        {
            std::istringstream v (l.substr (4));
            v.imbue (std::locale::classic());
            VariationHistory::Item it;
            int ts = 4;
            size_t count = 0;
            v >> it.seed >> it.sequence.bars >> ts >> it.sequence.bpm >> count;
            if (v.fail() || it.sequence.bars < 1 || it.sequence.bars > 64 || (ts != 3 && ts != 4) || count > 100000
                || ! (it.sequence.bpm >= 20 && it.sequence.bpm <= 400))
            { error = "Variación inválida en línea " + std::to_string (li + 1); return false; }
            if (version >= 2)
            {
                int fav = 0;
                v >> it.id >> fav;
                if (v.fail()) { error = "Variación inválida en línea " + std::to_string (li + 1); return false; }
                it.favorite = fav != 0;
                std::string rest;
                std::getline (v, rest);
                if (! rest.empty() && rest[0] == ' ') rest.erase (0, 1);
                it.name = rest;
            }
            it.sequence.timeSignature = { ts, 4 };
            for (size_t k = 0; k < count; ++k)
            {
                if (++li >= ls.size()) { error = "Notas truncadas"; return false; }
                Note n;
                if (! readNote (ls[li], version, n)) { error = "Nota inválida en línea " + std::to_string (li + 1); return false; }
                it.sequence.notes.push_back (n);
            }
            it.sequence.normalise();
            items.push_back (std::move (it));
            continue;
        }
        if (l.rfind ("auto ", 0) == 0)
        {
            std::istringstream v (l.substr (5));
            v.imbue (std::locale::classic());
            int target = 0, en = 0;
            size_t count = 0;
            AutomationLane lane;
            v >> target >> en >> lane.smoothingMs >> count;
            if (v.fail() || target < 0 || target >= kNumAutoTargets || count > 4096 || ! (lane.smoothingMs >= 0 && lane.smoothingMs <= 2000))
            { error = "Automatización inválida en línea " + std::to_string (li + 1); return false; }
            lane.target = (AutoTarget) target;
            lane.enabled = en != 0;
            for (size_t k = 0; k < count; ++k)
            {
                AutoPoint p;
                v >> p.tick >> p.value;
                if (v.fail() || p.tick < 0 || ! std::isfinite (p.value)) { error = "Punto de automatización inválido"; return false; }
                lane.points.push_back (p);
            }
            lane.sortPoints();
            if (s.automation.find (lane.target) == nullptr) s.automation.lanes.push_back (lane);
            continue;
        }
        if (l.rfind ("motif ", 0) == 0)
        {
            std::istringstream v (l.substr (6));
            SeedMotif m;
            size_t count = 0;
            v >> m.lengthTicks >> m.firstPitch >> count;
            if (v.fail() || count > 256 || m.lengthTicks < 0 || m.firstPitch < -1 || m.firstPitch > 127)
            { error = "Motivo inválido en línea " + std::to_string (li + 1); return false; }
            for (size_t k = 0; k < count; ++k)
            {
                SeedMotifNote n;
                v >> n.onset >> n.length >> n.interval;
                if (v.fail() || n.onset < 0 || n.length <= 0 || std::abs (n.interval) > 48) { error = "Motivo inválido"; return false; }
                m.notes.push_back (n);
            }
            if (s.seedMotifs.size() < 16) s.seedMotifs.push_back (std::move (m));
            continue;
        }
        const auto eq = l.find ('=');
        if (eq == std::string::npos) { error = "Línea no reconocida: " + l; return false; }
        const auto key = l.substr (0, eq), val = l.substr (eq + 1);
        if (key.rfind ("fx.", 0) == 0)
        {
            if (! applyFxSetting (s.fx, key.substr (3), val)) { error = "Valor de efecto inválido: " + key; return false; }
            continue;
        }
        if (key == "lockedBars")
        {
            if (val.size() > 64) { error = "Bloqueos inválidos"; return false; }
            s.lockedBars.clear();
            for (char c : val)
            {
                if (c != '0' && c != '1') { error = "Bloqueos inválidos"; return false; }
                s.lockedBars.push_back (c == '1');
            }
            continue;
        }
        if (key == "ab")
        {
            std::istringstream v (val);
            v >> s.slotA >> s.slotB;
            if (v.fail()) { error = "A/B inválido"; return false; }
            continue;
        }
        if (key == "dna") { s.dnaSummary = val; continue; }
        if (! applySetting (s.settings, key, val, error, version)) return false;
    }
    if (! sawEnd) { error = "Estado truncado (falta 'end')"; return false; }
    if ((int) items.size() != wantedVariations) { error = "Número de variaciones inconsistente"; return false; }
    s.fx.sanitise();
    s.history.restore (std::move (items), currentIdx, nextId);
    if (s.history.indexOfId (s.slotA) < 0) s.slotA = 0;
    if (s.history.indexOfId (s.slotB) < 0) s.slotB = 0;
    if (version < 2)
    {
        // v1: las variaciones se generaron con el motor 1; la semilla de motivos no existía.
        s.settings.keepMotifs = false;
    }
    else if (s.settings.fxEnabled && fxPresetIndex (s.settings.fxPreset) < 0 && ! s.settings.fxPreset.empty()
             && s.settings.fxPreset != "Personalizado")
    {
        s.settings.fxPreset = "Personalizado";
    }
    s.loadedFromVersion = version;
    out = std::move (s);
    return true;
}

std::string serializePreset (const SoloSettings& s, const std::string& name)
{
    std::ostringstream o;
    o.imbue (std::locale::classic());
    o << kPresetMagic << " 2\n";
    o << "name=" << oneLine (name) << "\n";
    writeSettings (o, s);
    o << "end\n";
    return o.str();
}

bool deserializePreset (const std::string& text, SoloSettings& out, std::string& name, std::string& error)
{
    const auto ls = lines (text);
    if (ls.empty() || ls[0].rfind (kPresetMagic, 0) != 0) { error = "No es un preset de Solo AI Studio"; return false; }
    int version = 1;
    {
        std::istringstream h (ls[0]);
        std::string magic;
        h >> magic >> version;
        if (h.fail()) version = 1;
        if (version > 2) { error = "Preset de una versión más nueva"; return false; }
    }
    SoloSettings s;
    std::string nm;
    bool sawEnd = false;
    for (size_t i = 1; i < ls.size(); ++i)
    {
        const auto& l = ls[i];
        if (l.empty()) continue;
        if (l == "end") { sawEnd = true; break; }
        const auto eq = l.find ('=');
        if (eq == std::string::npos) { error = "Línea no reconocida: " + l; return false; }
        if (l.substr (0, eq) == "name") { nm = l.substr (eq + 1); continue; }
        if (! applySetting (s, l.substr (0, eq), l.substr (eq + 1), error, version)) return false;
    }
    if (! sawEnd) { error = "Preset truncado"; return false; }
    out = s;
    name = nm;
    return true;
}

std::vector<FactoryPreset> factoryPresets()
{
    std::vector<FactoryPreset> v;
    auto make = [] (const char* key, const char* prog, InstrumentId ins, Style st, double e, double d, double c,
                    const char* fx, int ts = 4, double bpm = 120.0) {
        SoloSettings s;
        s.keyText = key; s.progressionText = prog; s.instrument = ins; s.style = st;
        s.energy = e; s.density = d; s.complexity = c; s.tsNumerator = ts; s.fxPreset = fx; s.manualBpm = bpm;
        return s;
    };
    v.push_back ({ "Rock melódico en La menor", make ("Am", "Am F C G", InstrumentId::Guitar, Style::MelodicRock, 0.65, 0.5, 0.4, "Classic Rock Lead") });
    v.push_back ({ "Blues en La (12 compases)", make ("A", "A7 D7 A7 A7 D7 D7 A7 A7 E7 D7 A7 E7", InstrumentId::Guitar, Style::ExpressiveBlues, 0.55, 0.45, 0.5, "Blues Lead", 4, 92) });
    v.push_back ({ "Solo emocional (C G Am F)", make ("C", "C G Am F", InstrumentId::Guitar, Style::Emotional, 0.45, 0.35, 0.35, "Emotional Lead", 4, 76) });
    v.push_back ({ "Rock virtuoso en Mi menor", make ("Em", "Em C D B7", InstrumentId::Guitar, Style::VirtuosoRock, 0.85, 0.8, 0.7, "High Gain Solo", 4, 132) });
    v.push_back ({ "Ambiental en Re", make ("D", "Dmaj7 Bm7 Gmaj7 A", InstrumentId::Guitar, Style::Ambient, 0.3, 0.2, 0.3, "Ambient Guitar", 4, 70) });
    v.push_back ({ "Cinematográfico en Re menor", make ("Dm", "Dm Bb F C", InstrumentId::Guitar, Style::Cinematic, 0.6, 0.4, 0.45, "Cinematic Lead", 4, 84) });
    v.push_back ({ "Improvisación suave (flauta)", make ("G", "G Em C D", InstrumentId::Flute, Style::SoftImprov, 0.4, 0.4, 0.35, "Clean Guitar", 4, 96) });
    v.push_back ({ "Lead de sinte en Fa menor", make ("Fm", "Fm Db Ab Eb", InstrumentId::Synth, Style::VirtuosoRock, 0.75, 0.65, 0.5, "Wide Synth Lead") });
    v.push_back ({ "Vals en Re menor (3/4)", make ("Dm", "Dm Gm A7 Dm", InstrumentId::Flute, Style::Emotional, 0.5, 0.45, 0.35, "Clean Guitar", 3) });
    return v;
}

// ---------------------------------------------------------------- deshacer / rehacer
void UndoStack::push (ProjectSnapshot before)
{
    undoItems.push_back (std::move (before));
    if ((int) undoItems.size() > kMaxSteps) undoItems.erase (undoItems.begin());
    redoItems.clear();
}

bool UndoStack::undo (ProjectSnapshot& current)
{
    if (undoItems.empty()) return false;
    ProjectSnapshot target = std::move (undoItems.back());
    undoItems.pop_back();
    current.label = target.label;
    redoItems.push_back (std::move (current));
    current = std::move (target);
    return true;
}

bool UndoStack::redo (ProjectSnapshot& current)
{
    if (redoItems.empty()) return false;
    ProjectSnapshot target = std::move (redoItems.back());
    redoItems.pop_back();
    current.label = target.label;
    undoItems.push_back (std::move (current));
    current = std::move (target);
    return true;
}

ProjectSnapshot snapshotOf (const SoloState& s, const std::string& label)
{
    ProjectSnapshot p;
    p.history = s.history;
    p.fx = s.fx;
    p.automation = s.automation;
    p.lockedBars = s.lockedBars;
    p.label = label;
    return p;
}

void applySnapshot (SoloState& s, const ProjectSnapshot& p)
{
    s.history = p.history;
    s.fx = p.fx;
    s.automation = p.automation;
    s.lockedBars = p.lockedBars;
}

} // namespace solo
