#include "solo/Sampler.h"
#include "solo/AudioRenderer.h"
#include "solo/Dsp.h"
#include "solo/InstrumentEngine.h"
#include "solo/MusicTheory.h"
#include "solo/VoiceAnalysis.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace solo
{
namespace
{
inline double semisToHz (double s) { return 440.0 * std::pow (2.0, (s - 69.0) / 12.0); }
inline double smoothStep (double u) { u = std::clamp (u, 0.0, 1.0); return 0.5 - 0.5 * std::cos (3.141592653589793 * u); }

std::string lower (std::string s) { for (auto& c : s) c = (char) std::tolower ((unsigned char) c); return s; }
std::string trim (const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace ((unsigned char) s[a])) ++a;
    while (b > a && std::isspace ((unsigned char) s[b - 1])) --b;
    return s.substr (a, b - a);
}

// Nota SFZ: número MIDI o nombre (c4, f#3, bb2). Devuelve -1 si no se entiende.
int parseSfzKey (const std::string& vIn)
{
    const std::string v = lower (trim (vIn));
    if (v.empty()) return -1;
    if (std::isdigit ((unsigned char) v[0]) || v[0] == '-') return std::clamp (std::atoi (v.c_str()), -1, 127);
    const int pc = parsePitchClass (v.substr (0, v.size() > 1 && (v[1] == '#' || v[1] == 'b') ? 2 : 1));
    if (pc < 0) return -1;
    const size_t octPos = v.size() > 1 && (v[1] == '#' || v[1] == 'b') ? 2 : 1;
    if (octPos >= v.size()) return -1;
    const int oct = std::atoi (v.c_str() + octPos);
    return std::clamp ((oct + 1) * 12 + pc, 0, 127);
}

std::string dirOf (const std::string& path)
{
    const auto p = path.find_last_of ("/\\");
    return p == std::string::npos ? std::string() : path.substr (0, p + 1);
}

bool isAbsolute (const std::string& p)
{
    return ! p.empty() && (p[0] == '/' || p[0] == '\\' || (p.size() > 1 && p[1] == ':'));
}

struct ZoneDef
{
    std::string sample;
    int key = -1, lokey = -1, hikey = -1, center = -1, lovel = 1, hivel = 127, seqLen = 1, seqPos = 1;
    std::string loopMode;
    int loopStart = -1, loopEnd = -1;
    float attack = 0.001f, release = 0.2f, tune = 0, volume = 0;
    std::string label;
    void apply (const std::string& k, const std::string& v)
    {
        if (k == "sample") sample = v;
        else if (k == "key") key = parseSfzKey (v);
        else if (k == "lokey") lokey = parseSfzKey (v);
        else if (k == "hikey") hikey = parseSfzKey (v);
        else if (k == "pitch_keycenter") center = parseSfzKey (v);
        else if (k == "lovel") lovel = std::clamp (std::atoi (v.c_str()), 0, 127);
        else if (k == "hivel") hivel = std::clamp (std::atoi (v.c_str()), 0, 127);
        else if (k == "seq_length") seqLen = std::clamp (std::atoi (v.c_str()), 1, 32);
        else if (k == "seq_position") seqPos = std::clamp (std::atoi (v.c_str()), 1, 32);
        else if (k == "loop_mode") loopMode = lower (v);
        else if (k == "loop_start") loopStart = std::atoi (v.c_str());
        else if (k == "loop_end") loopEnd = std::atoi (v.c_str());
        else if (k == "ampeg_attack") attack = (float) std::atof (v.c_str());
        else if (k == "ampeg_release") release = (float) std::atof (v.c_str());
        else if (k == "tune") tune = (float) std::atof (v.c_str());
        else if (k == "volume") volume = (float) std::atof (v.c_str());
        else if (k == "group_label" || k == "region_label" || k == "sw_label") label = lower (v);
    }
};
} // namespace

int SampleBank::findZone (int key, int velocity, int roundRobin, const std::string& articulation) const
{
    auto pick = [&] (const std::string& art) {
        int best = -1, bestScore = -1;
        for (size_t i = 0; i < zones.size(); ++i)
        {
            const auto& z = zones[i];
            if (key < z.loKey || key > z.hiKey || velocity < z.loVel || velocity > z.hiVel) continue;
            if (! art.empty() && z.articulation != art) continue;
            if (z.seqLength > 1 && (roundRobin % z.seqLength) + 1 != z.seqPosition) continue;
            // Preferir la zona cuya nota raíz esté más cerca (menos transposición).
            const int score = 1000 - std::abs (z.rootKey - key);
            if (score > bestScore) { bestScore = score; best = (int) i; }
        }
        return best;
    };
    int z = pick (articulation);
    if (z < 0 && articulation != "sustain") z = pick ("sustain");
    if (z < 0) z = pick ("");
    return z;
}

std::string SampleBank::summary() const
{
    int lo = 127, hi = 0, layers = 0, rr = 0;
    std::vector<std::string> arts;
    for (const auto& z : zones)
    {
        lo = std::min (lo, z.loKey); hi = std::max (hi, z.hiKey);
        layers = std::max (layers, z.loVel > 1 || z.hiVel < 127 ? 2 : 1);
        rr = std::max (rr, z.seqLength);
        if (std::find (arts.begin(), arts.end(), z.articulation) == arts.end()) arts.push_back (z.articulation);
    }
    std::ostringstream o;
    o << (synthetic ? "[SINTÉTICO] " : "") << name << ": " << zones.size() << " zonas, teclas " << (zones.empty() ? 0 : lo) << ".."
      << (zones.empty() ? 0 : hi) << ", round-robin " << rr << ", " << (bytes / (1024 * 1024)) << " MB, articulaciones:";
    for (const auto& a : arts) o << " " << a;
    return o.str();
}

bool loadSfz (const std::string& sfzPath, SampleBank& out, std::string& error, size_t maxBytes)
{
    std::ifstream f (sfzPath, std::ios::binary);
    if (! f) { error = "No se pudo abrir " + sfzPath; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string text = ss.str();
    if (text.size() > 8 * 1024 * 1024) { error = "SFZ demasiado grande"; return false; }
    // La marca de banco sintético vive en los comentarios: se evalúa antes de quitarlos.
    const bool markedSynthetic = text.find ("SINTETICO") != std::string::npos || text.find ("synthetic") != std::string::npos;
    // Quitar comentarios (// hasta fin de línea y /* */).
    {
        std::string clean;
        clean.reserve (text.size());
        for (size_t i = 0; i < text.size();)
        {
            if (text.compare (i, 2, "//") == 0) { while (i < text.size() && text[i] != '\n') ++i; continue; }
            if (text.compare (i, 2, "/*") == 0) { const auto e = text.find ("*/", i + 2); i = e == std::string::npos ? text.size() : e + 2; continue; }
            clean.push_back (text[i++]);
        }
        text.swap (clean);
    }
    // Tokenizar: cabeceras <x> y pares k=v (el valor dura hasta el siguiente token que contenga '=' o '<').
    struct Tok { bool header; std::string a, b; };
    std::vector<Tok> toks;
    {
        std::istringstream in (text);
        std::string w;
        while (in >> w)
        {
            if (w.size() >= 2 && w.front() == '<' && w.back() == '>') { toks.push_back ({ true, lower (w.substr (1, w.size() - 2)), {} }); continue; }
            const auto eq = w.find ('=');
            if (eq != std::string::npos) toks.push_back ({ false, lower (w.substr (0, eq)), w.substr (eq + 1) });
            else if (! toks.empty() && ! toks.back().header) toks.back().b += " " + w; // continuación de un valor con espacios
        }
    }
    SampleBank bank;
    bank.path = sfzPath;
    bank.name = sfzPath.substr (dirOf (sfzPath).size());
    std::string defaultPath = dirOf (sfzPath);
    ZoneDef global, group, region;
    std::string section;
    std::vector<ZoneDef> defs;
    auto flushRegion = [&] { if (section == "region") defs.push_back (region); };
    for (const auto& t : toks)
    {
        if (t.header)
        {
            flushRegion();
            section = t.a;
            if (section == "global") { global = ZoneDef(); group = global; region = group; }
            else if (section == "group" || section == "master") { group = global; region = group; }
            else if (section == "region") region = group;
            continue;
        }
        if (section == "control") { if (t.a == "default_path") defaultPath = (isAbsolute (t.b) ? std::string() : dirOf (sfzPath)) + t.b; continue; }
        if (section == "global") { global.apply (t.a, t.b); group.apply (t.a, t.b); region.apply (t.a, t.b); }
        else if (section == "group" || section == "master") { group.apply (t.a, t.b); region.apply (t.a, t.b); }
        else if (section == "region") region.apply (t.a, t.b);
    }
    flushRegion();
    if (defs.empty()) { error = "El SFZ no define ninguna <region>"; return false; }
    if (defs.size() > 4096) { error = "Demasiadas regiones (máximo 4096)"; return false; }
    std::string missing;
    int missingCount = 0;
    for (const auto& d : defs)
    {
        if (d.sample.empty()) continue;
        std::string path = d.sample;
        for (auto& c : path) if (c == '\\') c = '/';
        if (! isAbsolute (path)) path = defaultPath + path;
        MonoAudio audio;
        std::string err;
        if (! readWavFile (path, audio, err))
        {
            if (++missingCount <= 5) missing += (missing.empty() ? "" : "; ") + d.sample + " (" + err + ")";
            continue;
        }
        if (audio.samples.empty()) continue;
        if (bank.bytes + audio.samples.size() * sizeof (float) > maxBytes) { error = "Banco demasiado grande (límite " + std::to_string (maxBytes / (1024 * 1024)) + " MB)"; break; }
        SampleZone z;
        z.data = std::move (audio.samples);
        z.sampleRate = audio.sampleRate;
        z.file = d.sample;
        const int key = d.key >= 0 ? d.key : (d.center >= 0 ? d.center : 60);
        z.loKey = std::clamp (d.lokey >= 0 ? d.lokey : key, 0, 127);
        z.hiKey = std::clamp (d.hikey >= 0 ? d.hikey : key, 0, 127);
        z.rootKey = std::clamp (d.center >= 0 ? d.center : key, 0, 127);
        z.loVel = std::clamp (d.lovel, 0, 127);
        z.hiVel = std::clamp (d.hivel, 0, 127);
        z.seqLength = d.seqLen;
        z.seqPosition = std::clamp (d.seqPos, 1, d.seqLen);
        z.loop = d.loopMode == "loop_continuous" || d.loopMode == "loop_sustain";
        z.loopStart = std::clamp (d.loopStart, 0, (int) z.data.size() - 1);
        z.loopEnd = d.loopEnd >= 0 ? std::clamp (d.loopEnd, 0, (int) z.data.size() - 1) : (int) z.data.size() - 1;
        if (z.loopEnd <= z.loopStart + 16) z.loop = false;
        z.attackSec = std::clamp (d.attack, 0.0f, 5.0f);
        z.releaseSec = std::clamp (d.release, 0.005f, 10.0f);
        z.tuneCents = std::clamp (d.tune, -1200.0f, 1200.0f);
        z.volumeDb = std::clamp (d.volume, -60.0f, 24.0f);
        if (! d.label.empty()) z.articulation = d.label.find ("mute") != std::string::npos ? "mute" : d.label;
        bank.bytes += z.data.size() * sizeof (float);
        bank.zones.push_back (std::move (z));
    }
    if (bank.zones.empty()) { if (error.empty()) error = "No se pudo leer ningún sample: " + missing; return false; }
    if (missingCount > 0 && error.empty()) error = std::to_string (missingCount) + " samples no encontrados: " + missing;
    bank.synthetic = markedSynthetic;
    out = std::move (bank);
    return true;
}

std::string makeSyntheticBank (const std::string& dir, std::string& error, int sampleRate)
{
    {
        std::error_code ec;
        std::filesystem::create_directories (std::filesystem::u8path (dir), ec);
        if (ec) { error = "No se pudo crear la carpeta " + dir + ": " + ec.message(); return {}; }
    }
    // Notas E2..E6 cada 3 semitonos, 2 capas de velocity, 2 round-robin (semillas distintas), 1.6 s cada una.
    ModeledGuitarEngine eng;
    eng.prepare (sampleRate);
    GuitarParams gp;
    const auto es = expressionScaling (65);
    std::ostringstream sfz;
    sfz << "// Banco SINTETICO de prueba de Solo AI Studio: generado con el modelo fisico (ModeledGuitarEngine).\n"
           "// NO son samples de una guitarra real. Sirve para probar el motor de samples y como plantilla SFZ.\n"
           "<control> default_path=\n<global> ampeg_release=0.25\n";
    const int len = (int) (1.6 * sampleRate);
    std::vector<float> mono ((size_t) len);
    for (int key = 40; key <= 88; key += 3)
        for (int layer = 0; layer < 2; ++layer)
            for (int rr = 0; rr < 2; ++rr)
            {
                const int vel = layer == 0 ? 70 : 115;
                gp.pickHardness = layer == 0 ? 0.4f : 0.8f;
                gp.brightness = 0.5f + 0.1f * rr;
                eng.prepare (sampleRate);
                eng.setParams (gp, es);
                NoteExpression ex;
                ex.attack = (uint8_t) (60 + 20 * rr);
                std::fill (mono.begin(), mono.end(), 0.0f);
                eng.noteOn (key, vel, len, ex);
                eng.render (mono.data(), (int) (1.3 * sampleRate));
                eng.noteOff (key);
                eng.render (mono.data() + (int) (1.3 * sampleRate), len - (int) (1.3 * sampleRate));
                for (int i = 0; i < 200; ++i) mono[(size_t) (len - 1 - i)] *= (float) i / 200.0f;
                char name[64];
                std::snprintf (name, sizeof name, "sint_%03d_v%d_rr%d.wav", key, layer, rr);
                std::string err;
                if (! writeWav24 (dir + "/" + name, mono, mono, sampleRate, err)) { error = err; return {}; }
                sfz << "<region> sample=" << name << " pitch_keycenter=" << key << " lokey=" << std::max (0, key - 1) << " hikey=" << key + 1
                    << " lovel=" << (layer == 0 ? 1 : 96) << " hivel=" << (layer == 0 ? 95 : 127) << " seq_length=2 seq_position=" << rr + 1 << "\n";
            }
    // Articulación "mute" (palm mute) para las notas graves, una capa.
    for (int key = 40; key <= 64; key += 3)
    {
        gp.pickHardness = 0.7f;
        eng.prepare (sampleRate);
        eng.setParams (gp, es);
        NoteExpression ex;
        ex.palmMute = true;
        std::fill (mono.begin(), mono.end(), 0.0f);
        eng.noteOn (key, 100, len, ex);
        eng.render (mono.data(), len);
        for (int i = 0; i < 200; ++i) mono[(size_t) (len - 1 - i)] *= (float) i / 200.0f;
        char name[64];
        std::snprintf (name, sizeof name, "sint_%03d_mute.wav", key);
        std::string err;
        if (! writeWav24 (dir + "/" + name, mono, mono, sampleRate, err)) { error = err; return {}; }
        sfz << "<region> group_label=mute sample=" << name << " pitch_keycenter=" << key << " lokey=" << std::max (0, key - 1) << " hikey=" << key + 1 << "\n";
    }
    const std::string path = dir + "/guitarra-sintetica.sfz";
    std::ofstream out (path, std::ios::binary);
    if (! out) { error = "No se pudo escribir " + path; return {}; }
    out << sfz.str();
    return path;
}

// ---------------------------------------------------------------- motor
SampleGuitarEngine::SampleGuitarEngine()
{
    voices.resize ((size_t) kMaxVoices);
    rrCounter.assign (128, 0);
}

void SampleGuitarEngine::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    for (auto& v : voices) { v.stage = Stage::Off; v.zone = nullptr; v.env = 0; }
}

void SampleGuitarEngine::setBank (const SampleBank* b)
{
    if (b == current) return;
    // Las voces apuntan a zonas del banco anterior: se cortan de inmediato (carga de banco = acción del usuario).
    for (auto& v : voices) { v.stage = Stage::Off; v.zone = nullptr; v.env = 0; }
    current = b;
}

void SampleGuitarEngine::setParams (const GuitarParams& p, const ExpressionScaling& e)
{
    params = p;
    params.sanitise();
    expr = e;
}

SampleGuitarEngine::Voice* SampleGuitarEngine::findLegatoVoice()
{
    Voice* best = nullptr;
    for (auto& v : voices)
        if ((v.stage == Stage::Playing || v.stage == Stage::Attack) && v.env > 0.02f)
            if (! best || v.age > best->age) best = &v;
    return best;
}

void SampleGuitarEngine::startVoice (Voice& v, const SampleZone& z, int pitch, const NoteGestures& g)
{
    v.zone = &z;
    v.pitch = pitch;
    v.age = ++ageCounter;
    v.baseSemis = pitch;
    v.pos = 0.0;
    v.samples = 0;
    v.g = g;
    v.glideFrom = g.slideFromSemis;
    v.glidePos = 0;
    v.glideSamples = g.slideFromSemis != 0.0 ? std::max (1, (int) (g.slideTimeSec * sr)) : 0;
    v.gain = dsp::dbToGain (z.volumeDb) * (float) g.level;
    v.lp = 0.0f;
    v.stage = Stage::Attack;
}

void SampleGuitarEngine::noteOn (int pitch, int velocity, int lengthSamples, const NoteExpression& e)
{
    if (! current || current->zones.empty() || pitch < 0 || pitch > 127) return;
    if (velocity <= 0) { noteOff (pitch); return; }
    const NoteGestures g = gesturesFor (e, velocity, std::max (1, lengthSamples) / sr, expr, true);
    if (g.legato)
    {
        if (Voice* lv = findLegatoVoice())
        {
            // Legato: la muestra sigue sonando y se desliza a la nueva altura (sin nuevo ataque).
            Voice& v = *lv;
            const double from = v.baseSemis + gesturePitchOffset (v.g, (double) v.samples / sr);
            v.pitch = pitch;
            v.baseSemis = pitch;
            v.glideFrom = from - pitch;
            v.glideSamples = std::max (1, (int) (g.legatoGlideSec * sr));
            v.glidePos = 0;
            v.samples = 0;
            v.g = g;
            v.g.slideFromSemis = 0.0;
            v.age = ++ageCounter;
            v.gain = std::max (v.gain * 0.85f, dsp::dbToGain (v.zone->volumeDb) * (float) g.level);
            return;
        }
    }
    const int rr = rrCounter[(size_t) pitch]++;
    const int zi = current->findZone (pitch, velocity, rr, g.palmMute ? "mute" : "sustain");
    if (zi < 0) return;
    Voice* target = nullptr;
    for (auto& v : voices) if (v.stage != Stage::Off && v.pitch == pitch) { target = &v; break; }
    if (! target) for (auto& v : voices) if (v.stage == Stage::Off) { target = &v; break; }
    if (! target)
    {
        Voice* oldest = nullptr;
        for (auto& v : voices)
        {
            const bool rel = v.stage == Stage::Release || v.stage == Stage::FastRelease;
            if (! oldest) { oldest = &v; continue; }
            const bool oldestRel = oldest->stage == Stage::Release || oldest->stage == Stage::FastRelease;
            if ((rel && ! oldestRel) || (rel == oldestRel && v.age < oldest->age)) oldest = &v;
        }
        target = oldest;
    }
    // Si la voz estaba sonando, el ataque empieza desde su envolvente actual (sin clic).
    const float envBefore = target->stage == Stage::Off ? 0.0f : target->env;
    startVoice (*target, current->zones[(size_t) zi], pitch, g);
    target->env = envBefore;
}

void SampleGuitarEngine::noteOff (int pitch)
{
    for (auto& v : voices)
        if (v.pitch == pitch && (v.stage == Stage::Playing || v.stage == Stage::Attack)) v.stage = Stage::Release;
}

void SampleGuitarEngine::allNotesOff (bool immediate)
{
    for (auto& v : voices)
        if (v.stage != Stage::Off) v.stage = immediate ? Stage::FastRelease : Stage::Release;
}

int SampleGuitarEngine::activeVoiceCount() const
{
    int n = 0;
    for (const auto& v : voices) n += v.stage != Stage::Off ? 1 : 0;
    return n;
}

float SampleGuitarEngine::renderVoice (Voice& v)
{
    const SampleZone& z = *v.zone;
    double semis = v.baseSemis + gesturePitchOffset (v.g, (double) v.samples / sr);
    if (v.glideSamples > 0 && v.glidePos < v.glideSamples)
    {
        semis += v.glideFrom * (1.0 - smoothStep ((double) v.glidePos / v.glideSamples));
        ++v.glidePos;
    }
    ++v.samples;
    const double rate = std::pow (2.0, (semis - z.rootKey + z.tuneCents / 100.0) / 12.0) * z.sampleRate / sr;
    const int n = (int) z.data.size();
    // Interpolación de Hermite
    const int i1 = (int) v.pos;
    const float frac = (float) (v.pos - i1);
    auto at = [&] (int i) { return i >= 0 && i < n ? z.data[(size_t) i] : 0.0f; };
    float s = dsp::hermite (at (i1 - 1), at (i1), at (i1 + 1), at (i1 + 2), frac);
    v.pos += rate;
    if (z.loop && v.stage != Stage::Release && v.stage != Stage::FastRelease && v.pos >= z.loopEnd)
        v.pos -= (z.loopEnd - z.loopStart);
    // Brillo por velocity/ataque: paso bajo de un polo suave.
    const float lpc = (float) std::clamp (0.25 + 0.75 * v.g.brightness, 0.25, 1.0);
    v.lp += lpc * (s - v.lp);
    s = v.lp;
    switch (v.stage)
    {
        case Stage::Attack:
            v.env += 1.0f / (float) std::max (1.0, z.attackSec * sr + (v.g.attack < 0.2 ? 0.1 * sr : 0.0));
            if (v.env >= 1.0f) { v.env = 1.0f; v.stage = Stage::Playing; }
            break;
        case Stage::Playing: break;
        case Stage::Release:
            v.env *= (float) std::exp (-5.0 / std::max (1.0, z.releaseSec * v.g.sustainScale * sr));
            if (v.env < 1e-4f) { v.env = 0; v.stage = Stage::Off; }
            break;
        case Stage::FastRelease:
            v.env *= (float) std::exp (-5.0 / std::max (1.0, 0.006 * sr));
            if (v.env < 1e-4f) { v.env = 0; v.stage = Stage::Off; }
            break;
        case Stage::Off: break;
    }
    if (v.pos >= n + 2) { v.stage = Stage::Off; v.env = 0; }
    return s * v.env * v.gain;
}

void SampleGuitarEngine::render (float* mono, int numSamples)
{
    const float target = dsp::dbToGain (params.gainDb);
    for (int i = 0; i < numSamples; ++i)
    {
        float s = 0.0f;
        for (auto& v : voices)
            if (v.stage != Stage::Off && v.zone != nullptr) s += renderVoice (v);
        smoothedGain += 0.001f * (target - smoothedGain);
        mono[i] += s * smoothedGain;
    }
}

} // namespace solo
