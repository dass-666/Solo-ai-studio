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
    o << "wavSampleRate=" << s.wavSampleRate << "\n";
    o << "midiFormat=" << s.midiFormat << "\n";
}

// Aplica pares clave=valor validando rangos. Las claves desconocidas se ignoran (compatibilidad futura).
bool applySetting (SoloSettings& s, const std::string& k, const std::string& v, std::string& error)
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
    else if (k == "instrument") { if (! integer (0, kNumInstruments - 1)) return false; s.instrument = (InstrumentId) i; }
    else if (k == "style") { if (! integer (0, kNumStyles - 1)) return false; s.style = (Style) i; }
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
    else if (k == "wavSampleRate") { if (! integer (8000, 192000)) return false; s.wavSampleRate = (int) i; }
    else if (k == "midiFormat") { if (! integer (0, 1)) return false; s.midiFormat = (int) i; }
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
} // namespace

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
    const auto errs = SoloComposer::validate (p);
    if (! errs.empty()) { error = errs.front(); return false; }
    return true;
}

std::string serializeState (const SoloState& s)
{
    std::ostringstream o;
    o.imbue (std::locale::classic());
    o << kStateMagic << " " << SoloState::kVersion << "\n";
    writeSettings (o, s.settings);
    const auto& items = s.history.all();
    o << "variations=" << items.size() << " " << s.history.currentIndex() << "\n";
    for (const auto& it : items)
    {
        const auto& q = it.sequence;
        o << "var " << it.seed << " " << q.bars << " " << q.timeSignature.numerator << " " << fmt (q.bpm) << " "
          << q.notes.size() << "\n";
        for (const auto& n : q.notes)
            o << "n " << n.startTick << " " << n.lengthTicks << " " << n.pitch << " " << n.velocity << " " << n.channel << "\n";
    }
    o << "end\n";
    return o.str();
}

bool deserializeState (const std::string& text, SoloState& out, std::string& error)
{
    const auto ls = lines (text);
    if (ls.empty()) { error = "Estado vacío"; return false; }
    {
        std::istringstream h (ls[0]);
        std::string magic;
        int version = 0;
        h >> magic >> version;
        if (magic != kStateMagic) { error = "No es un estado de Solo AI Studio"; return false; }
        if (version < 1) { error = "Versión de estado inválida"; return false; }
        if (version > SoloState::kVersion) { error = "Estado creado por una versión más nueva (" + std::to_string (version) + ")"; return false; }
    }
    SoloState s; // se construye aparte y solo se copia si todo es válido
    std::vector<VariationHistory::Item> items;
    int wantedVariations = 0, currentIdx = -1;
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
            if (v.fail() || wantedVariations < 0 || wantedVariations > VariationHistory::kMaxVariations)
            { error = "Cabecera de variaciones inválida"; return false; }
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
            it.sequence.timeSignature = { ts, 4 };
            for (size_t k = 0; k < count; ++k)
            {
                if (++li >= ls.size()) { error = "Notas truncadas"; return false; }
                std::istringstream nl (ls[li]);
                std::string tag;
                Note n;
                nl >> tag >> n.startTick >> n.lengthTicks >> n.pitch >> n.velocity >> n.channel;
                if (nl.fail() || tag != "n" || n.lengthTicks <= 0 || n.startTick < 0 || n.pitch < 0 || n.pitch > 127
                    || n.velocity < 1 || n.velocity > 127 || n.channel < 0 || n.channel > 15)
                { error = "Nota inválida en línea " + std::to_string (li + 1); return false; }
                it.sequence.notes.push_back (n);
            }
            it.sequence.normalise();
            items.push_back (std::move (it));
            continue;
        }
        const auto eq = l.find ('=');
        if (eq == std::string::npos) { error = "Línea no reconocida: " + l; return false; }
        if (! applySetting (s.settings, l.substr (0, eq), l.substr (eq + 1), error)) return false;
    }
    if (! sawEnd) { error = "Estado truncado (falta 'end')"; return false; }
    if ((int) items.size() != wantedVariations) { error = "Número de variaciones inconsistente"; return false; }
    s.history.restore (std::move (items), currentIdx);
    out = std::move (s);
    return true;
}

std::string serializePreset (const SoloSettings& s, const std::string& name)
{
    std::ostringstream o;
    o.imbue (std::locale::classic());
    o << kPresetMagic << " 1\n";
    o << "name=" << oneLine (name) << "\n";
    writeSettings (o, s);
    o << "end\n";
    return o.str();
}

bool deserializePreset (const std::string& text, SoloSettings& out, std::string& name, std::string& error)
{
    const auto ls = lines (text);
    if (ls.empty() || ls[0].rfind (kPresetMagic, 0) != 0) { error = "No es un preset de Solo AI Studio"; return false; }
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
        if (! applySetting (s, l.substr (0, eq), l.substr (eq + 1), error)) return false;
    }
    if (! sawEnd) { error = "Preset truncado"; return false; }
    out = s;
    name = nm;
    return true;
}

std::vector<FactoryPreset> factoryPresets()
{
    std::vector<FactoryPreset> v;
    auto make = [] (const char* key, const char* prog, InstrumentId ins, Style st, double e, double d, double c, int ts = 4) {
        SoloSettings s;
        s.keyText = key; s.progressionText = prog; s.instrument = ins; s.style = st;
        s.energy = e; s.density = d; s.complexity = c; s.tsNumerator = ts;
        return s;
    };
    v.push_back ({ "Rock en La menor (Am F C G)", make ("Am", "Am F C G", InstrumentId::Guitar, Style::Rock, 0.7, 0.55, 0.4) });
    v.push_back ({ "Blues en La (A7 D7 E7)", make ("A", "A7 A7 D7 A7 E7 D7 A7 E7", InstrumentId::Guitar, Style::Blues, 0.6, 0.5, 0.5) });
    v.push_back ({ "Balada de flauta (C G Am F)", make ("C", "C G Am F", InstrumentId::Flute, Style::Melodic, 0.4, 0.35, 0.3) });
    v.push_back ({ "Lead electrónico (Fm Db Ab Eb)", make ("Fm", "Fm Db Ab Eb", InstrumentId::Synth, Style::Electronic, 0.75, 0.7, 0.45) });
    v.push_back ({ "Vals en Re menor (3/4)", make ("Dm", "Dm Gm A7 Dm", InstrumentId::Flute, Style::Melodic, 0.5, 0.45, 0.35, 3) });
    return v;
}

} // namespace solo
