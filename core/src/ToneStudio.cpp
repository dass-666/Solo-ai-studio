#include "solo/ToneStudio.h"
#include <algorithm>
#include <cmath>

namespace solo
{
void ToneMacros::sanitise()
{
    auto c = [] (double& v) { if (! std::isfinite (v)) v = 50.0; v = std::clamp (v, 0.0, 100.0); };
    c (force); c (expression); c (ambience);
}

bool ToneMacros::operator== (const ToneMacros& o) const
{
    return std::abs (force - o.force) < 1e-6 && std::abs (expression - o.expression) < 1e-6 && std::abs (ambience - o.ambience) < 1e-6;
}

static double unit (double pct) { return std::clamp (pct / 100.0, 0.0, 1.0); }

double forcePreGainDb (double F) { return GuitarAmp::preGainDbForForce ((float) F); }
double ambienceReverbWet (double A) { return 0.32 * std::pow (unit (A), 1.3); }
double ambienceDelayWet (double A) { return 0.25 * std::pow (unit (A), 1.5); }
double ambienceFeedback (double A) { return 0.12 + 0.34 * unit (A); }
double ambiencePredelayMs (double A) { return 10.0 + 34.0 * unit (A); }
double ambienceDecaySec (double A) { return 0.6 + 2.3 * unit (A); }

float reverbSizeForDecay (double t60)
{
    // EffectsChain::tailSeconds: T60 = avgDelay·ln(0.001)/ln(fb), fb = size·0.28 + 0.7
    const double avgDelay = 1356.0 / 44100.0;
    const double fb = std::exp (avgDelay * std::log (0.001) / std::max (0.05, t60));
    return (float) std::clamp ((fb - 0.7) / 0.28, 0.0, 1.0);
}

void applyForce (double F, FxSettings& fx, GuitarParams& guitar)
{
    const double f = unit (F);
    fx.ampPreGainDb = (float) forcePreGainDb (F);
    fx.ampCompression = (float) (0.10 + 0.50 * f);
    fx.ampSag = (float) (0.10 + 0.40 * f);
    fx.ampPresenceDb = (float) (-1.0 + 4.0 * f);
    fx.ampMaster = (float) (0.30 + 0.50 * f);
    fx.odDrive = (float) (0.20 + 0.60 * f);
    fx.distGain = (float) (0.30 + 0.50 * f);
    guitar.pickHardness = (float) (0.25 + 0.65 * f);
    fx.sanitise();
    guitar.sanitise();
}

void applyAmbience (double A, FxSettings& fx)
{
    const double a = unit (A);
    fx.reverbMix = (float) ambienceReverbWet (A);
    fx.delayMix = (float) ambienceDelayWet (A);
    fx.delayFeedback = (float) ambienceFeedback (A);
    fx.reverbPredelayMs = (float) ambiencePredelayMs (A);
    fx.reverbSize = reverbSizeForDecay (ambienceDecaySec (A));
    fx.ambienceWidth = (float) (0.6 + 0.4 * a);
    fx.sanitise();
}

void applyMacros (const ToneMacros& m, FxSettings& fx, GuitarParams& guitar)
{
    applyForce (m.force, fx, guitar);
    applyAmbience (m.ambience, fx);
}

const std::vector<TonePreset>& tonePresets()
{
    static const std::vector<TonePreset> presets = [] {
        std::vector<TonePreset> v;
        auto on = [] (FxSettings& s, std::initializer_list<FxStage> st) { for (auto x : st) s.setOn (x, true); };
        auto base = [] {
            FxSettings s;
            s.ampModel = 1; s.ampOversample = 2; s.ampOutputLpHz = 8000;
            s.reverbLowCutHz = 180; s.reverbHighCutHz = 6500; s.delayLowCutHz = 220; s.delayToneHz = 3800;
            s.delayPingPong = true; s.delayBeats = 0.75f; s.reverbDamping = 0.45f;
            return s;
        };
        auto add = [&] (const char* name, const char* desc, FxSettings s, GuitarParams g, GuitarEngineKind e, ToneMacros m, PerformerProfile p) {
            m.sanitise();
            applyMacros (m, s, g);
            v.push_back ({ name, desc, s, g, e, m, p });
        };
        {
            FxSettings s = base(); // Rock Lead: humbucker, válvula empujada, 4x12, delay corto
            on (s, { FxStage::Overdrive, FxStage::Amp, FxStage::Cabinet, FxStage::Eq, FxStage::Delay, FxStage::Reverb });
            s.ampSatType = 0; s.odTone = 0.55f; s.ampBassDb = 0; s.ampMidDb = 3; s.ampTrebleDb = 1.5f; s.cabType = 2;
            s.eqLowDb = -1; s.delayBeats = 0.5f; s.outputDb = -13.0f;
            GuitarParams g; g.pickup = 1; g.pickupPosition = 0.16f; g.brightness = 0.6f; g.sustain = 0.6f; g.stiffness = 0.25f; g.bodyResonance = 0.25f;
            add ("Rock Lead", "Humbucker, válvula empujada, pantalla 4x12, delay corto", s, g, GuitarEngineKind::Modeled, { 70, 65, 35 }, PerformerProfile::Auto);
        }
        {
            FxSettings s = base(); // Blues Lead: pastilla simple, ampli al borde, 2x12, sin delay, reverb de muelles
            on (s, { FxStage::Amp, FxStage::Cabinet, FxStage::Eq, FxStage::Reverb });
            s.ampSatType = 0; s.ampBassDb = 1.5f; s.ampMidDb = 1; s.ampTrebleDb = -1; s.cabType = 1;
            s.eqMidDb = 1.5f; s.eqMidHz = 900; s.reverbDamping = 0.6f; s.reverbHighCutHz = 5000; s.outputDb = -6.0f;
            GuitarParams g; g.pickup = 0; g.pickupPosition = 0.24f; g.brightness = 0.5f; g.sustain = 0.45f; g.stiffness = 0.2f; g.bodyResonance = 0.4f;
            add ("Blues Lead", "Pastilla simple, ampli al borde de la saturación, 2x12, reverb corta", s, g, GuitarEngineKind::Modeled, { 50, 75, 28 }, PerformerProfile::Auto);
        }
        {
            FxSettings s = base(); // Clean Electric: limpio con chorus leve y 1x12
            on (s, { FxStage::Amp, FxStage::Cabinet, FxStage::Eq, FxStage::Chorus, FxStage::Reverb });
            s.ampSatType = 2; s.ampTrebleDb = 2; s.ampBassDb = 1; s.cabType = 0; s.eqHighDb = 1;
            s.chorusMix = 0.18f; s.chorusDepth = 0.3f; s.chorusRateHz = 0.5f; s.ampOutputLpHz = 9500; s.outputDb = -6.0f;
            GuitarParams g; g.pickup = 0; g.pickupPosition = 0.3f; g.brightness = 0.7f; g.sustain = 0.5f; g.stiffness = 0.2f; g.bodyResonance = 0.45f;
            add ("Clean Electric", "Limpio brillante, pastilla simple, 1x12 abierta, chorus leve", s, g, GuitarEngineKind::Modeled, { 22, 55, 30 }, PerformerProfile::Auto);
        }
        {
            FxSettings s = base(); // Emotional Lead: crunch cantarín, delay con puntillo y reverb larga
            on (s, { FxStage::Overdrive, FxStage::Amp, FxStage::Cabinet, FxStage::Eq, FxStage::Delay, FxStage::Reverb });
            s.ampSatType = 0; s.odTone = 0.4f; s.ampMidDb = 2.5f; s.ampTrebleDb = 0; s.cabType = 1; s.eqLowDb = -2;
            s.delayBeats = 0.75f; s.delayToneHz = 3200; s.reverbHighCutHz = 6000; s.outputDb = -14.0f;
            GuitarParams g; g.pickup = 1; g.pickupPosition = 0.2f; g.brightness = 0.5f; g.sustain = 0.75f; g.stiffness = 0.2f; g.bodyResonance = 0.3f;
            add ("Emotional Lead", "Crunch cantarín con mucho sustain, delay con puntillo y reverb larga", s, g, GuitarEngineKind::Modeled, { 58, 85, 55 }, PerformerProfile::Auto);
        }
        {
            FxSettings s = base(); // High Gain Solo: alta ganancia, 4x12 cerrada, graves recortados
            on (s, { FxStage::Overdrive, FxStage::Distortion, FxStage::Amp, FxStage::Cabinet, FxStage::Eq, FxStage::Delay, FxStage::Reverb });
            s.ampSatType = 1; s.odTone = 0.6f; s.distTone = 0.5f; s.ampBassDb = 1; s.ampMidDb = -1.5f; s.ampTrebleDb = 2; s.cabType = 2;
            s.eqLowDb = -3; s.eqMidDb = 1.5f; s.eqMidHz = 1400; s.delayBeats = 0.75f; s.ampOversample = 4; s.outputDb = -10.0f;
            GuitarParams g; g.pickup = 1; g.pickupPosition = 0.14f; g.brightness = 0.65f; g.sustain = 0.8f; g.stiffness = 0.3f; g.bodyResonance = 0.15f;
            add ("High Gain Solo", "Alta ganancia con compresión, 4x12 cerrada, graves recortados, oversampling 4x", s, g, GuitarEngineKind::Modeled, { 90, 60, 32 }, PerformerProfile::Auto);
        }
        {
            FxSettings s = base(); // Ambient Guitar: limpio, chorus, delay largo y reverb enorme
            on (s, { FxStage::Amp, FxStage::Cabinet, FxStage::Eq, FxStage::Filter, FxStage::Chorus, FxStage::Delay, FxStage::Reverb });
            s.ampSatType = 2; s.cabType = 0; s.eqHighDb = -2; s.filterCutoffHz = 5500; s.chorusMix = 0.3f; s.chorusDepth = 0.5f; s.chorusRateHz = 0.3f;
            s.delayBeats = 1.5f; s.delayToneHz = 2600; s.reverbDamping = 0.3f; s.reverbHighCutHz = 7000; s.reverbLowCutHz = 120; s.outputDb = -4.5f;
            GuitarParams g; g.pickup = 0; g.pickupPosition = 0.32f; g.brightness = 0.55f; g.sustain = 0.85f; g.stiffness = 0.15f; g.bodyResonance = 0.5f;
            add ("Ambient Guitar", "Limpio etéreo: chorus, delay largo y reverb enorme con predelay", s, g, GuitarEngineKind::Modeled, { 30, 55, 85 }, PerformerProfile::Auto);
        }
        return v;
    }();
    return presets;
}

int tonePresetIndex (const std::string& name)
{
    const auto& p = tonePresets();
    for (size_t i = 0; i < p.size(); ++i)
        if (name == p[i].name) return (int) i;
    return -1;
}

} // namespace solo
