#include "solo/Effects.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace solo
{
namespace
{
constexpr int kMaxDelaySeconds = 4;
constexpr int kChorusSize = 4096; // > 40 ms a 96 kHz

inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float flushDenormal (float v) { return std::abs (v) < 1.0e-15f ? 0.0f : v; }

// Limitador suave de salida: transparente por debajo de -1 dBFS, techo en ~-0.2 dBFS.
inline float softLimit (float x)
{
    const float knee = 0.89f, ceil = 0.977f;
    const float a = std::abs (x);
    if (a <= knee) return x;
    const float y = knee + (ceil - knee) * std::tanh ((a - knee) / (ceil - knee));
    return x < 0 ? -y : y;
}

inline bool differs (float a, float b) { return std::abs (a - b) > 1.0e-6f; }
} // namespace

const char* fxStageName (FxStage s)
{
    switch (s)
    {
        case FxStage::Overdrive: return "Overdrive";
        case FxStage::Distortion: return "Distorsión";
        case FxStage::Amp: return "Amplificador";
        case FxStage::Cabinet: return "Pantalla";
        case FxStage::Eq: return "Ecualizador";
        case FxStage::Filter: return "Filtro";
        case FxStage::Chorus: return "Chorus";
        case FxStage::Delay: return "Delay";
        case FxStage::Reverb: return "Reverb";
        case FxStage::Output: return "Salida";
    }
    return "?";
}

const char* autoTargetName (AutoTarget t)
{
    switch (t)
    {
        case AutoTarget::Drive: return "Saturación";
        case AutoTarget::DelayMix: return "Delay (mezcla)";
        case AutoTarget::ReverbMix: return "Reverb (mezcla)";
        case AutoTarget::FilterCutoff: return "Filtro (corte)";
        case AutoTarget::Volume: return "Volumen";
        case AutoTarget::ChorusMix: return "Chorus (mezcla)";
    }
    return "?";
}

void FxSettings::sanitise()
{
    auto c = [] (float& v, float lo, float hi) { v = std::isfinite (v) ? clampf (v, lo, hi) : lo; };
    c (odDrive, 0, 1); c (odTone, 0, 1); c (odLevelDb, -24, 12);
    c (distGain, 0, 1); c (distTone, 0, 1); c (distLevelDb, -24, 12);
    c (ampGain, 0, 1); c (ampBassDb, -12, 12); c (ampMidDb, -12, 12); c (ampTrebleDb, -12, 12); c (ampMaster, 0, 1);
    cabType = std::clamp (cabType, 0, 2);
    c (eqLowDb, -15, 15); c (eqMidDb, -15, 15); c (eqMidHz, 200, 5000); c (eqHighDb, -15, 15);
    filterMode = std::clamp (filterMode, 0, 1);
    c (filterCutoffHz, 40, 20000); c (filterResonance, 0, 0.95f);
    c (chorusRateHz, 0.05f, 5); c (chorusDepth, 0, 1); c (chorusMix, 0, 1);
    c (delayBeats, 0.125f, 4); c (delayFeedback, 0, 0.92f); c (delayMix, 0, 1); c (delayToneHz, 500, 16000);
    c (reverbSize, 0, 1); c (reverbDamping, 0, 1); c (reverbMix, 0, 1); c (reverbWidth, 0, 1);
    c (outputDb, -48, 12);
}

bool FxSettings::operator== (const FxSettings& o) const
{
    return serializeFx (*this) == serializeFx (o);
}

// ---------------------------------------------------------------- presets
// Niveles de salida calibrados para ~-18 LUFS (medidos con tools/solo_cli calibrate sobre 9 solos de guitarra/sinte).
const std::vector<FxPreset>& fxPresets()
{
    static const std::vector<FxPreset> presets = [] {
        std::vector<FxPreset> v;
        auto on = [] (FxSettings& s, std::initializer_list<FxStage> st) { for (auto x : st) s.setOn (x, true); };
        {
            FxSettings s; // Clean Guitar
            on (s, { FxStage::Amp, FxStage::Cabinet, FxStage::Eq, FxStage::Chorus, FxStage::Reverb });
            s.ampGain = 0.12f; s.ampMaster = 0.2f; s.ampTrebleDb = 2; s.cabType = 0;
            s.eqHighDb = 1.5f; s.chorusMix = 0.2f; s.chorusDepth = 0.3f; s.reverbMix = 0.15f; s.reverbSize = 0.5f;
            s.outputDb = -10.6f;
            v.push_back ({ "Clean Guitar", s });
        }
        {
            FxSettings s; // Emotional Lead
            on (s, { FxStage::Overdrive, FxStage::Amp, FxStage::Cabinet, FxStage::Eq, FxStage::Delay, FxStage::Reverb });
            s.odDrive = 0.45f; s.odTone = 0.45f; s.ampGain = 0.35f; s.ampMidDb = 2; s.cabType = 1;
            s.eqLowDb = -2; s.delayBeats = 0.75f; s.delayFeedback = 0.38f; s.delayMix = 0.22f; s.delayToneHz = 3500;
            s.reverbMix = 0.25f; s.reverbSize = 0.7f; s.outputDb = -16.6f;
            v.push_back ({ "Emotional Lead", s });
        }
        {
            FxSettings s; // Classic Rock Lead
            on (s, { FxStage::Overdrive, FxStage::Amp, FxStage::Cabinet, FxStage::Eq, FxStage::Delay, FxStage::Reverb });
            s.odDrive = 0.6f; s.odTone = 0.55f; s.ampGain = 0.6f; s.ampMidDb = 3; s.ampTrebleDb = 1; s.ampMaster = 0.6f;
            s.cabType = 2; s.delayBeats = 0.5f; s.delayFeedback = 0.25f; s.delayMix = 0.14f; s.reverbMix = 0.14f;
            s.reverbSize = 0.5f; s.outputDb = -12.6f;
            v.push_back ({ "Classic Rock Lead", s });
        }
        {
            FxSettings s; // Blues Lead
            on (s, { FxStage::Overdrive, FxStage::Amp, FxStage::Cabinet, FxStage::Eq, FxStage::Reverb });
            s.odDrive = 0.35f; s.odTone = 0.4f; s.ampGain = 0.45f; s.ampBassDb = 1; s.ampMidDb = 2; s.ampTrebleDb = -1;
            s.ampMaster = 0.55f; s.cabType = 1; s.reverbMix = 0.18f; s.reverbSize = 0.45f; s.reverbDamping = 0.6f;
            s.outputDb = -14.2f;
            v.push_back ({ "Blues Lead", s });
        }
        {
            FxSettings s; // High Gain Solo
            on (s, { FxStage::Overdrive, FxStage::Distortion, FxStage::Amp, FxStage::Cabinet, FxStage::Eq, FxStage::Delay, FxStage::Reverb });
            s.odDrive = 0.2f; s.odTone = 0.6f; s.distGain = 0.7f; s.distTone = 0.55f; s.ampGain = 0.5f;
            s.ampBassDb = 2; s.ampMidDb = -1; s.ampTrebleDb = 2; s.ampMaster = 0.7f; s.cabType = 2;
            s.eqLowDb = -3; s.eqMidDb = 2; s.eqMidHz = 1200; s.delayBeats = 0.75f; s.delayFeedback = 0.3f;
            s.delayMix = 0.16f; s.reverbMix = 0.15f; s.outputDb = -14.8f;
            v.push_back ({ "High Gain Solo", s });
        }
        {
            FxSettings s; // Ambient Guitar
            on (s, { FxStage::Amp, FxStage::Cabinet, FxStage::Eq, FxStage::Filter, FxStage::Chorus, FxStage::Delay, FxStage::Reverb });
            s.ampGain = 0.15f; s.ampMaster = 0.25f; s.cabType = 0; s.eqHighDb = -2; s.filterCutoffHz = 5000;
            s.chorusMix = 0.35f; s.chorusDepth = 0.5f; s.chorusRateHz = 0.3f;
            s.delayBeats = 1.5f; s.delayFeedback = 0.55f; s.delayMix = 0.35f; s.delayToneHz = 2500;
            s.reverbMix = 0.45f; s.reverbSize = 0.92f; s.reverbDamping = 0.3f; s.outputDb = -14.0f;
            v.push_back ({ "Ambient Guitar", s });
        }
        {
            FxSettings s; // Cinematic Lead
            on (s, { FxStage::Overdrive, FxStage::Amp, FxStage::Cabinet, FxStage::Eq, FxStage::Delay, FxStage::Reverb });
            s.odDrive = 0.3f; s.ampGain = 0.3f; s.ampMidDb = 1; s.cabType = 1; s.eqLowDb = -1; s.eqHighDb = 1;
            s.delayBeats = 1.0f; s.delayFeedback = 0.42f; s.delayMix = 0.25f; s.delayToneHz = 3000;
            s.reverbMix = 0.38f; s.reverbSize = 0.88f; s.reverbDamping = 0.35f; s.outputDb = -18.8f;
            v.push_back ({ "Cinematic Lead", s });
        }
        {
            FxSettings s; // Wide Synth Lead
            on (s, { FxStage::Eq, FxStage::Filter, FxStage::Chorus, FxStage::Delay, FxStage::Reverb });
            s.eqLowDb = -2; s.eqHighDb = 1; s.filterCutoffHz = 6000; s.filterResonance = 0.25f;
            s.chorusMix = 0.5f; s.chorusDepth = 0.6f; s.chorusRateHz = 0.8f;
            s.delayBeats = 0.75f; s.delayFeedback = 0.35f; s.delayMix = 0.22f; s.reverbMix = 0.25f;
            s.reverbSize = 0.7f; s.reverbWidth = 1.0f; s.outputDb = 2.9f;
            v.push_back ({ "Wide Synth Lead", s });
        }
        return v;
    }();
    return presets;
}

int fxPresetIndex (const std::string& name)
{
    const auto& p = fxPresets();
    for (size_t i = 0; i < p.size(); ++i)
        if (name == p[i].name) return (int) i;
    return -1;
}

// ---------------------------------------------------------------- persistencia
namespace
{
std::string f2s (float v)
{
    std::ostringstream o;
    o.imbue (std::locale::classic());
    o.precision (7);
    o << v;
    return o.str();
}
} // namespace

std::string serializeFx (const FxSettings& s)
{
    std::ostringstream o;
    o << "fx.on=";
    for (bool b : s.enabled) o << (b ? '1' : '0');
    o << "\n";
    auto w = [&] (const char* k, float v) { o << "fx." << k << "=" << f2s (v) << "\n"; };
    w ("odDrive", s.odDrive); w ("odTone", s.odTone); w ("odLevelDb", s.odLevelDb);
    w ("distGain", s.distGain); w ("distTone", s.distTone); w ("distLevelDb", s.distLevelDb);
    w ("ampGain", s.ampGain); w ("ampBassDb", s.ampBassDb); w ("ampMidDb", s.ampMidDb);
    w ("ampTrebleDb", s.ampTrebleDb); w ("ampMaster", s.ampMaster); w ("cabType", (float) s.cabType);
    w ("eqLowDb", s.eqLowDb); w ("eqMidDb", s.eqMidDb); w ("eqMidHz", s.eqMidHz); w ("eqHighDb", s.eqHighDb);
    w ("filterMode", (float) s.filterMode); w ("filterCutoffHz", s.filterCutoffHz); w ("filterResonance", s.filterResonance);
    w ("chorusRateHz", s.chorusRateHz); w ("chorusDepth", s.chorusDepth); w ("chorusMix", s.chorusMix);
    w ("delayBeats", s.delayBeats); w ("delayFeedback", s.delayFeedback); w ("delayMix", s.delayMix);
    w ("delayToneHz", s.delayToneHz); w ("delayPingPong", s.delayPingPong ? 1.0f : 0.0f);
    w ("reverbSize", s.reverbSize); w ("reverbDamping", s.reverbDamping); w ("reverbMix", s.reverbMix);
    w ("reverbWidth", s.reverbWidth); w ("outputDb", s.outputDb); w ("limiter", s.limiter ? 1.0f : 0.0f);
    return o.str();
}

bool applyFxSetting (FxSettings& s, const std::string& key, const std::string& value)
{
    if (key == "on")
    {
        if (value.size() != (size_t) kNumFxStages) return false;
        for (int i = 0; i < kNumFxStages; ++i)
        {
            if (value[(size_t) i] != '0' && value[(size_t) i] != '1') return false;
            s.enabled[(size_t) i] = value[(size_t) i] == '1';
        }
        return true;
    }
    std::istringstream in (value);
    in.imbue (std::locale::classic());
    float v = 0;
    in >> v;
    if (in.fail() || ! std::isfinite (v)) return false;
    struct F { const char* k; float* p; };
    const F fields[] = {
        { "odDrive", &s.odDrive }, { "odTone", &s.odTone }, { "odLevelDb", &s.odLevelDb },
        { "distGain", &s.distGain }, { "distTone", &s.distTone }, { "distLevelDb", &s.distLevelDb },
        { "ampGain", &s.ampGain }, { "ampBassDb", &s.ampBassDb }, { "ampMidDb", &s.ampMidDb },
        { "ampTrebleDb", &s.ampTrebleDb }, { "ampMaster", &s.ampMaster },
        { "eqLowDb", &s.eqLowDb }, { "eqMidDb", &s.eqMidDb }, { "eqMidHz", &s.eqMidHz }, { "eqHighDb", &s.eqHighDb },
        { "filterCutoffHz", &s.filterCutoffHz }, { "filterResonance", &s.filterResonance },
        { "chorusRateHz", &s.chorusRateHz }, { "chorusDepth", &s.chorusDepth }, { "chorusMix", &s.chorusMix },
        { "delayBeats", &s.delayBeats }, { "delayFeedback", &s.delayFeedback }, { "delayMix", &s.delayMix },
        { "delayToneHz", &s.delayToneHz }, { "reverbSize", &s.reverbSize }, { "reverbDamping", &s.reverbDamping },
        { "reverbMix", &s.reverbMix }, { "reverbWidth", &s.reverbWidth }, { "outputDb", &s.outputDb },
    };
    for (const auto& f : fields)
        if (key == f.k) { *f.p = v; return true; }
    if (key == "cabType") { s.cabType = (int) std::lround (v); return true; }
    if (key == "filterMode") { s.filterMode = (int) std::lround (v); return true; }
    if (key == "delayPingPong") { s.delayPingPong = v > 0.5f; return true; }
    if (key == "limiter") { s.limiter = v > 0.5f; return true; }
    return true; // clave desconocida: se ignora (compatibilidad futura)
}

// ---------------------------------------------------------------- automatización
float AutomationLane::valueAt (double tick) const
{
    if (points.empty()) return 0.5f;
    if (tick <= (double) points.front().tick) return points.front().value;
    if (tick >= (double) points.back().tick) return points.back().value;
    // búsqueda binaria del tramo
    size_t lo = 0, hi = points.size() - 1;
    while (hi - lo > 1)
    {
        const size_t mid = (lo + hi) / 2;
        if ((double) points[mid].tick <= tick) lo = mid; else hi = mid;
    }
    const auto& a = points[lo];
    const auto& b = points[hi];
    const double span = (double) (b.tick - a.tick);
    if (span <= 0) return b.value;
    const double u = (tick - (double) a.tick) / span;
    return (float) (a.value + (b.value - a.value) * u);
}

void AutomationLane::sortPoints()
{
    std::stable_sort (points.begin(), points.end(), [] (const AutoPoint& a, const AutoPoint& b) { return a.tick < b.tick; });
    for (auto& p : points) p.value = std::isfinite (p.value) ? clampf (p.value, 0.0f, 1.0f) : 0.5f;
}

float autoToParam (AutoTarget t, float n)
{
    n = clampf (n, 0.0f, 1.0f);
    switch (t)
    {
        case AutoTarget::Drive: return n;
        case AutoTarget::DelayMix: return 0.7f * n;
        case AutoTarget::ReverbMix: return 0.8f * n;
        case AutoTarget::FilterCutoff: return 200.0f * std::pow (100.0f, n);
        case AutoTarget::Volume: return -24.0f + 30.0f * n;
        case AutoTarget::ChorusMix: return n;
    }
    return n;
}

float paramToAuto (AutoTarget t, const FxSettings& s)
{
    switch (t)
    {
        case AutoTarget::Drive: return s.isOn (FxStage::Distortion) ? s.distGain : s.odDrive;
        case AutoTarget::DelayMix: return clampf (s.delayMix / 0.7f, 0, 1);
        case AutoTarget::ReverbMix: return clampf (s.reverbMix / 0.8f, 0, 1);
        case AutoTarget::FilterCutoff: return clampf (std::log (s.filterCutoffHz / 200.0f) / std::log (100.0f), 0, 1);
        case AutoTarget::Volume: return clampf ((s.outputDb + 24.0f) / 30.0f, 0, 1);
        case AutoTarget::ChorusMix: return s.chorusMix;
    }
    return 0.5f;
}

// ---------------------------------------------------------------- procesador
void EffectsChain::prepare (double sampleRate, int)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    const size_t dl = (size_t) (kMaxDelaySeconds * sr) + 8;
    delayL.assign (dl, 0.0f);
    delayR.assign (dl, 0.0f);
    chorusBuf.assign ((size_t) kChorusSize, 0.0f);
    const double scale = sr / 44100.0;
    const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    const int apT[4] = { 556, 441, 341, 225 };
    for (int i = 0; i < 8; ++i)
    {
        combL[(size_t) i].buf.assign ((size_t) std::max (8.0, combT[i] * scale), 0.0f);
        combR[(size_t) i].buf.assign ((size_t) std::max (8.0, (combT[i] + 23) * scale), 0.0f);
    }
    for (int i = 0; i < 4; ++i)
    {
        apL[(size_t) i].buf.assign ((size_t) std::max (8.0, apT[i] * scale), 0.0f);
        apR[(size_t) i].buf.assign ((size_t) std::max (8.0, (apT[i] + 23) * scale), 0.0f);
    }
    prepared = true;
    haveLast = false;
    reset();
}

void EffectsChain::reset()
{
    std::fill (delayL.begin(), delayL.end(), 0.0f);
    std::fill (delayR.begin(), delayR.end(), 0.0f);
    std::fill (chorusBuf.begin(), chorusBuf.end(), 0.0f);
    for (auto* arr : { &combL, &combR })
        for (auto& c : *arr) { std::fill (c.buf.begin(), c.buf.end(), 0.0f); c.pos = 0; c.store = 0; }
    for (auto* arr : { &apL, &apR })
        for (auto& a : *arr) { std::fill (a.buf.begin(), a.buf.end(), 0.0f); a.pos = 0; }
    for (auto* b : { &upLp1, &upLp2, &downLp1, &downLp2, &odPre, &odTone, &distPre, &distTone, &ampBass, &ampMid,
                     &ampTreble, &ampPostLp, &eqLow, &eqMid, &eqHigh, &delayToneL, &delayToneR })
        b->reset();
    for (auto& b : cab) b.reset();
    svfIc1 = svfIc2 = 0;
    dcX = dcY = 0;
    delayPos = chorusPos = 0;
    chorusPhase = 0;
    delaySmoothed = -1.0f;
    haveLast = false;
}

void EffectsChain::updateFilters (const FxSettings& s)
{
    // Sobremuestreo (overdrive/distorsión/ampli comparten diseño; estado separado por etapa).
    upLp1.lowpass (2.0 * sr, 0.42 * sr); upLp2.lowpass (2.0 * sr, 0.42 * sr);
    downLp1.lowpass (2.0 * sr, 0.42 * sr); downLp2.lowpass (2.0 * sr, 0.42 * sr);
    odPre.highpass (sr, 600.0, 0.6);
    odTone.lowpass (sr, 1500.0 + 5000.0 * s.odTone, 0.65);
    distPre.highpass (sr, 140.0, 0.7);
    distTone.lowpass (sr, 2200.0 + 5500.0 * s.distTone, 0.7);
    ampBass.lowShelf (sr, 120.0, s.ampBassDb);
    ampMid.peak (sr, 650.0, 0.7, s.ampMidDb);
    ampTreble.highShelf (sr, 2800.0, s.ampTrebleDb);
    ampPostLp.lowpass (sr, 9000.0, 0.7);
    switch (s.cabType)
    {
        case 0:
            cab[0].highpass (sr, 70.0, 0.7); cab[1].peak (sr, 110.0, 1.0, 3.0);
            cab[2].peak (sr, 2500.0, 1.0, 3.0); cab[3].lowpass (sr, 6500.0, 0.7);
            break;
        case 2:
            cab[0].highpass (sr, 90.0, 0.7); cab[1].peak (sr, 100.0, 1.2, 4.0);
            cab[2].peak (sr, 400.0, 1.0, -3.0); cab[3].lowpass (sr, 4000.0, 0.9);
            break;
        default:
            cab[0].highpass (sr, 80.0, 0.7); cab[1].peak (sr, 120.0, 1.0, 3.0);
            cab[2].peak (sr, 1800.0, 1.0, 2.0); cab[3].lowpass (sr, 5000.0, 0.8);
            break;
    }
    eqLow.lowShelf (sr, 150.0, s.eqLowDb);
    eqMid.peak (sr, s.eqMidHz, 0.8, s.eqMidDb);
    eqHigh.highShelf (sr, 4000.0, s.eqHighDb);
    delayToneL.lowpass (sr, s.delayToneHz, 0.6);
    delayToneR.lowpass (sr, s.delayToneHz, 0.6);
}

double EffectsChain::tailSeconds (const FxSettings& s, double bpm)
{
    double tail = 0.0;
    if (s.isOn (FxStage::Delay) && s.delayMix > 0.001f)
    {
        const double t = s.delayBeats * 60.0 / (bpm > 1 ? bpm : 120.0);
        const double fb = std::max (0.01, (double) s.delayFeedback);
        tail = std::max (tail, t * (1.0 + std::log (0.001) / std::log (fb)));
    }
    if (s.isOn (FxStage::Reverb) && s.reverbMix > 0.001f)
    {
        // Freeverb: realimentación 0.7..0.98 => T60 aproximado.
        const double fb = s.reverbSize * 0.28 + 0.7;
        const double avgDelay = 1356.0 / 44100.0;
        tail = std::max (tail, avgDelay * std::log (0.001) / std::log (fb));
    }
    if (s.isOn (FxStage::Chorus)) tail = std::max (tail, 0.05);
    return std::min (tail, 20.0);
}

float EffectsChain::overdrive (float x, float drive)
{
    // Estilo "tube screamer": realce de medios antes de saturar, recorte suave asimétrico.
    const float hp = odPre.process (x);
    const float in = 0.35f * x + 0.65f * hp;
    const float g = 1.0f + drive * drive * 60.0f;
    const float a = upLp2.process (upLp1.process (2.0f * in));
    const float b = upLp2.process (upLp1.process (0.0f));
    // Asimetría (armónicos pares) sin desplazamiento de continua: f(0) = 0 exacto.
    auto shape = [g] (float v) { const float y = v * g; return y >= 0.0f ? std::tanh (y) : 1.25f * std::tanh (0.8f * y); };
    const float ya = downLp2.process (downLp1.process (shape (a)));
    downLp2.process (downLp1.process (shape (b)));
    return odTone.process (ya) * 0.7f;
}

float EffectsChain::distortion (float x, float gain)
{
    // Alta ganancia: recorte más duro; sin sobremuestreo propio (el tono posterior filtra el aliasing alto).
    const float in = distPre.process (x);
    const float g = 1.0f + gain * gain * 400.0f;
    float y = in * g;
    y = y / (1.0f + std::abs (y)); // recorte suave rápido
    y = std::tanh (1.6f * y);
    return distTone.process (y) * 0.8f;
}

void EffectsChain::process (float* left, float* right, int numSamples, const FxSettings& st, double bpm,
                            const float* autoValues, float autoSmoothingMs, double soloTimeSec)
{
    if (soloTimeSec >= 0.0)
        chorusPhase = std::fmod (2.0 * dsp::kPi * st.chorusRateHz * soloTimeSec, 2.0 * dsp::kPi);
    if (! prepared || numSamples <= 0) return;
    // Coeficientes de filtros: solo se recalculan si cambian los parámetros que los definen.
    const bool filtersChanged = ! haveLast || differs (st.odTone, last.odTone) || differs (st.distTone, last.distTone)
                                || differs (st.ampBassDb, last.ampBassDb) || differs (st.ampMidDb, last.ampMidDb)
                                || differs (st.ampTrebleDb, last.ampTrebleDb) || st.cabType != last.cabType
                                || differs (st.eqLowDb, last.eqLowDb) || differs (st.eqMidDb, last.eqMidDb) || differs (st.eqMidHz, last.eqMidHz)
                                || differs (st.eqHighDb, last.eqHighDb) || differs (st.delayToneHz, last.delayToneHz);
    if (filtersChanged) updateFilters (st);

    const int ramp = std::max (1, (int) (autoSmoothingMs * 0.001 * sr));
    auto targetOf = [&] (AutoTarget t, float fallback) {
        if (autoValues != nullptr && autoValues[(int) t] >= 0.0f) return autoToParam (t, autoValues[(int) t]);
        return fallback;
    };
    const float driveTarget = targetOf (AutoTarget::Drive, st.odDrive);
    const float distTarget = targetOf (AutoTarget::Drive, st.distGain);
    const float delayMixT = targetOf (AutoTarget::DelayMix, st.delayMix);
    const float revMixT = targetOf (AutoTarget::ReverbMix, st.reverbMix);
    const float cutoffT = targetOf (AutoTarget::FilterCutoff, st.filterCutoffHz);
    const float volT = dsp::dbToGain (targetOf (AutoTarget::Volume, st.outputDb));
    const float chorusMixT = targetOf (AutoTarget::ChorusMix, st.chorusMix);
    if (! haveLast)
    {
        sDrive.reset (driveTarget); sDistGain.reset (distTarget); sDelayMix.reset (delayMixT); sReverbMix.reset (revMixT);
        sCutoff.reset (cutoffT); sVolume.reset (volT); sChorusMix.reset (chorusMixT);
    }
    else
    {
        sDrive.setTarget (driveTarget, ramp); sDistGain.setTarget (distTarget, ramp); sDelayMix.setTarget (delayMixT, ramp);
        sReverbMix.setTarget (revMixT, ramp); sCutoff.setTarget (cutoffT, ramp); sVolume.setTarget (volT, ramp);
        sChorusMix.setTarget (chorusMixT, ramp);
    }
    last = st;
    haveLast = true;

    const bool odOn = st.isOn (FxStage::Overdrive), distOn = st.isOn (FxStage::Distortion), ampOn = st.isOn (FxStage::Amp);
    const bool cabOn = st.isOn (FxStage::Cabinet), eqOn = st.isOn (FxStage::Eq), filtOn = st.isOn (FxStage::Filter);
    const bool chorusOn = st.isOn (FxStage::Chorus), delayOn = st.isOn (FxStage::Delay), revOn = st.isOn (FxStage::Reverb);
    const bool outOn = st.isOn (FxStage::Output);
    const float odLevel = dsp::dbToGain (st.odLevelDb), distLevel = dsp::dbToGain (st.distLevelDb);
    const float ampPre = 1.0f + st.ampGain * 12.0f;
    const float ampK = 1.0f + st.ampMaster * 3.0f;
    const float ampNorm = 1.0f / std::sqrt (ampPre * ampK);

    // Delay
    const double bpmSafe = bpm > 1.0 ? bpm : 120.0;
    const float delayTarget = (float) std::clamp (st.delayBeats * 60.0 / bpmSafe * sr, 1.0, (double) delayL.size() - 4.0);
    if (delaySmoothed < 0.0f) delaySmoothed = delayTarget;
    const float fb = st.delayFeedback;

    // Reverb
    const float roomFb = st.reverbSize * 0.28f + 0.7f;
    const float damp1 = st.reverbDamping * 0.4f, damp2 = 1.0f - damp1;
    const float wet1 = st.reverbWidth / 2.0f + 0.5f, wet2 = (1.0f - st.reverbWidth) / 2.0f;

    // Filtro SVF (TPT)
    const float k = 2.0f - 1.9f * st.filterResonance;

    const double chorusInc = 2.0 * dsp::kPi * st.chorusRateHz / sr;
    const float chorusBase = (float) (0.007 * sr), chorusSwing = (float) (0.0045 * sr * st.chorusDepth);

    for (int i = 0; i < numSamples; ++i)
    {
        float x = 0.5f * (left[i] + right[i]);
        const float drv = sDrive.next(), dg = sDistGain.next();
        if (odOn) x = overdrive (x, drv) * odLevel;
        if (distOn) x = distortion (x, dg) * distLevel;
        if (ampOn)
        {
            float y = std::tanh (x * ampPre);
            y = ampTreble.process (ampMid.process (ampBass.process (y)));
            y = std::tanh (y * ampK);
            x = ampPostLp.process (y) * ampNorm * 2.0f;
        }
        if (odOn || distOn || ampOn)
        {
            // bloqueo de continua (la saturación asimétrica genera DC)
            const float y = x - dcX + 0.995f * dcY;
            dcX = x;
            dcY = flushDenormal (y);
            x = y;
        }
        if (cabOn) for (auto& b : cab) x = b.process (x);
        if (eqOn) x = eqHigh.process (eqMid.process (eqLow.process (x)));
        const float cutoff = sCutoff.next();
        if (filtOn)
        {
            const float g = (float) std::tan (dsp::kPi * std::min ((double) cutoff, 0.45 * sr) / sr);
            const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const float v3 = x - svfIc2;
            const float v1 = a1 * svfIc1 + a2 * v3;
            const float v2 = svfIc2 + a2 * svfIc1 + a3 * v3;
            svfIc1 = flushDenormal (2 * v1 - svfIc1);
            svfIc2 = flushDenormal (2 * v2 - svfIc2);
            x = st.filterMode == 0 ? v2 : x - k * v1 - v2;
        }
        float l = x, r = x;

        const float cmix = sChorusMix.next();
        if (chorusOn)
        {
            chorusBuf[(size_t) chorusPos] = x;
            auto tap = [&] (double ph) {
                const float d = chorusBase + chorusSwing * (float) std::sin (ph);
                float rp = (float) chorusPos - d;
                while (rp < 0) rp += (float) kChorusSize;
                const int i1 = (int) rp;
                const float fr = rp - (float) i1;
                auto at = [&] (int j) { return chorusBuf[(size_t) ((j + kChorusSize) % kChorusSize)]; };
                return dsp::hermite (at (i1 - 1), at (i1), at (i1 + 1), at (i1 + 2), fr);
            };
            const float tl = tap (chorusPhase), tr = tap (chorusPhase + dsp::kPi * 0.5);
            chorusPhase += chorusInc;
            if (chorusPhase > 2 * dsp::kPi) chorusPhase -= 2 * dsp::kPi;
            chorusPos = (chorusPos + 1) % kChorusSize;
            l = x * (1.0f - 0.5f * cmix) + tl * cmix * 0.8f;
            r = x * (1.0f - 0.5f * cmix) + tr * cmix * 0.8f;
        }

        const float dmix = sDelayMix.next();
        if (delayOn)
        {
            delaySmoothed += 0.0005f * (delayTarget - delaySmoothed);
            const int size = (int) delayL.size();
            float rp = (float) delayPos - delaySmoothed;
            if (rp < 0) rp += (float) size;
            const int i1 = (int) rp;
            const float fr = rp - (float) i1;
            const int i2 = (i1 + 1) % size;
            const float dl = delayL[(size_t) i1] + fr * (delayL[(size_t) i2] - delayL[(size_t) i1]);
            const float dr = delayR[(size_t) i1] + fr * (delayR[(size_t) i2] - delayR[(size_t) i1]);
            const float fl = delayToneL.process (dl), fr2 = delayToneR.process (dr);
            if (st.delayPingPong)
            {
                delayL[(size_t) delayPos] = flushDenormal (0.5f * (l + r) + fb * fr2);
                delayR[(size_t) delayPos] = flushDenormal (fb * fl);
            }
            else
            {
                delayL[(size_t) delayPos] = flushDenormal (l + fb * fl);
                delayR[(size_t) delayPos] = flushDenormal (r + fb * fr2);
            }
            delayPos = (delayPos + 1) % size;
            l += dmix * dl;
            r += dmix * dr;
        }

        const float rmix = sReverbMix.next();
        if (revOn)
        {
            const float in = (l + r) * 0.015f;
            float outL = 0, outR = 0;
            for (size_t c = 0; c < 8; ++c)
            {
                for (auto* cb : { &combL[c], &combR[c] })
                {
                    auto& cm = *cb;
                    const float y = cm.buf[(size_t) cm.pos];
                    cm.store = flushDenormal (y * damp2 + cm.store * damp1);
                    cm.buf[(size_t) cm.pos] = in + cm.store * roomFb;
                    if (++cm.pos >= (int) cm.buf.size()) cm.pos = 0;
                    (cb == &combL[c] ? outL : outR) += y;
                }
            }
            for (size_t a = 0; a < 4; ++a)
            {
                for (auto* ap : { &apL[a], &apR[a] })
                {
                    float& o = ap == &apL[a] ? outL : outR;
                    const float b = ap->buf[(size_t) ap->pos];
                    ap->buf[(size_t) ap->pos] = flushDenormal (o + b * 0.5f);
                    o = b - o;
                    if (++ap->pos >= (int) ap->buf.size()) ap->pos = 0;
                }
            }
            const float wl = outL * wet1 + outR * wet2, wr = outR * wet1 + outL * wet2;
            l = l * (1.0f - 0.3f * rmix) + rmix * 3.0f * wl;
            r = r * (1.0f - 0.3f * rmix) + rmix * 3.0f * wr;
        }

        const float vol = sVolume.next();
        if (outOn)
        {
            l *= vol;
            r *= vol;
            if (st.limiter) { l = softLimit (l); r = softLimit (r); }
        }
        left[i] = l;
        if (right != left) right[i] = r;
    }
}

} // namespace solo
