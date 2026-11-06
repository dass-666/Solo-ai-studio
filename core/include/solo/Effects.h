#pragma once
// Cadena de efectos integrada (sin dependencias, sin samples ni respuestas al impulso):
//   overdrive -> distorsión -> amplificador -> pantalla (cab) -> EQ -> filtro -> chorus -> delay -> reverb -> salida
// Las etapas hasta el filtro son mono (el instrumento es una voz solista mono); desde el chorus, estéreo.
// Los modelos son aproximaciones digitales sencillas (waveshaping + filtros), NO emulaciones de equipos concretos.
#include "solo/Dsp.h"
#include "solo/GuitarAmp.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace solo
{
enum class FxStage { Overdrive = 0, Distortion, Amp, Cabinet, Eq, Filter, Chorus, Delay, Reverb, Output };
constexpr int kNumFxStages = 10;
const char* fxStageName (FxStage s);

struct FxSettings
{
    std::array<bool, kNumFxStages> enabled { { false, false, false, false, false, false, false, false, false, true } };

    // Overdrive (suave, medios marcados)
    float odDrive = 0.4f, odTone = 0.5f, odLevelDb = 0.0f;
    // Distorsión (alta ganancia)
    float distGain = 0.5f, distTone = 0.5f, distLevelDb = -6.0f;
    // Amplificador: ganancia de previo, tonos (dB) y "master" (saturación de etapa de potencia)
    float ampGain = 0.4f, ampBassDb = 0.0f, ampMidDb = 0.0f, ampTrebleDb = 0.0f, ampMaster = 0.5f;
    // Pantalla: 0 = 1x12 abierta (brillante), 1 = 2x12, 2 = 4x12 cerrada (oscura)
    int cabType = 1;
    // EQ de 3 bandas
    float eqLowDb = 0.0f, eqMidDb = 0.0f, eqMidHz = 800.0f, eqHighDb = 0.0f;
    // Filtro (SVF): 0 = paso bajo, 1 = paso alto
    int filterMode = 0;
    float filterCutoffHz = 8000.0f, filterResonance = 0.2f;
    // Chorus
    float chorusRateHz = 0.6f, chorusDepth = 0.4f, chorusMix = 0.35f;
    // Delay sincronizado: tiempo en negras (0.75 = corchea con puntillo)
    float delayBeats = 0.75f, delayFeedback = 0.35f, delayMix = 0.25f, delayToneHz = 4500.0f;
    bool delayPingPong = true;
    // Reverb (algoritmo tipo Freeverb)
    float reverbSize = 0.6f, reverbDamping = 0.4f, reverbMix = 0.2f, reverbWidth = 1.0f;
    // Salida
    float outputDb = 0.0f;
    bool limiter = true;

    // ---- Guitar Tone Studio v3 (ampModel = 1 sustituye overdrive/distorsión/ampli/pantalla por GuitarAmp;
    //      las etapas siguen activándose con 'enabled' y odDrive/odTone/distGain/distTone/ampBass.../cabType se reutilizan)
    int ampModel = 0;             // 0 = cadena v2 (bit a bit igual que antes), 1 = Tone Studio v3
    float ampPreGainDb = 14.4f;   // ganancia previa (la macro Fuerza la fija: 2 + 22·(F/100)^1.6)
    int ampSatType = 0;           // 0 válvula, 1 alta ganancia, 2 limpio
    int ampOversample = 2;        // 1, 2, 4
    float ampPresenceDb = 2.0f, ampCompression = 0.3f, ampSag = 0.3f, ampTrimDb = 0.0f, ampOutputLpHz = 8000.0f;
    std::string cabIrPath;        // IR de pantalla del usuario (vacío = modelo por cabType)
    // Ambiente v3
    // Valores neutros por defecto (los presets v2 suenan bit a bit igual que antes): 20 Hz / 20 kHz = sin filtro.
    float reverbPredelayMs = 0.0f, reverbLowCutHz = 20.0f, reverbHighCutHz = 20000.0f, delayLowCutHz = 20.0f;
    float ambienceWidth = 1.0f;   // anchura estéreo de delay+reverb (0 = mono, 1 = normal)

    bool isOn (FxStage s) const { return enabled[(size_t) s]; }
    void setOn (FxStage s, bool on) { enabled[(size_t) s] = on; }
    // Corrige valores fuera de rango (estados corruptos o editados a mano).
    void sanitise();
    bool operator== (const FxSettings& o) const;
};

// Presets de fábrica.
struct FxPreset { const char* name; FxSettings settings; };
const std::vector<FxPreset>& fxPresets();
int fxPresetIndex (const std::string& name); // -1 si no existe

// Texto clave=valor (persistencia).
std::string serializeFx (const FxSettings& s);
bool applyFxSetting (FxSettings& s, const std::string& key, const std::string& value);

// ---------------------------------------------------------------- automatización
enum class AutoTarget { Drive = 0, DelayMix, ReverbMix, FilterCutoff, Volume, ChorusMix };
constexpr int kNumAutoTargets = 6;
const char* autoTargetName (AutoTarget t);

struct AutoPoint
{
    int64_t tick = 0;
    float value = 0.5f; // normalizado 0..1
    bool operator== (const AutoPoint& o) const { return tick == o.tick && std::abs (value - o.value) < 1e-6f; }
};

// Curva por compases: interpolación lineal entre puntos; antes del primero / después del último
// se mantiene el valor extremo. Si la curva está vacía o desactivada no afecta al parámetro.
struct AutomationLane
{
    AutoTarget target = AutoTarget::Drive;
    bool enabled = false;
    float smoothingMs = 30.0f;
    std::vector<AutoPoint> points; // ordenados por tick

    float valueAt (double tick) const;
    void sortPoints();
    bool operator== (const AutomationLane& o) const
    {
        return target == o.target && enabled == o.enabled && std::abs (smoothingMs - o.smoothingMs) < 1e-6f && points == o.points;
    }
};

// Convierte valor normalizado <-> valor real del parámetro.
float autoToParam (AutoTarget t, float normalised);
float paramToAuto (AutoTarget t, const FxSettings& s);
// Fija el parámetro principal de un destino a partir de un valor normalizado y activa su etapa si estaba apagada.
void setAutoParam (FxSettings& s, AutoTarget t, float normalised);

// ---------------------------------------------------------------- procesador
class EffectsChain
{
public:
    static constexpr int kMaxAutomation = kNumAutoTargets;

    // Hilo de mensajes: reserva toda la memoria (delay de hasta 4 s, reverb, chorus).
    void prepare (double sampleRate, int maxBlockSize);
    void reset(); // vacía colas (sin reservar)

    // Tiempo real. 'settings' se lee una vez por bloque; 'autoValues[t]' >= 0 sustituye al parámetro t
    // (valor normalizado), < 0 = sin automatización. 'bpm' sincroniza el delay.
    // 'soloTimeSec' >= 0: posición dentro del solo (segundos) al inicio del bloque; sincroniza el LFO del
    // chorus con el solo para que la exportación y la reproducción en vivo coincidan. < 0: LFO libre.
    void process (float* left, float* right, int numSamples, const FxSettings& settings, double bpm,
                  const float* autoValues = nullptr, float autoSmoothingMs = 30.0f, double soloTimeSec = -1.0);

    // Segundos aproximados hasta que la cola (delay + reverb) cae por debajo de -60 dB.
    static double tailSeconds (const FxSettings& s, double bpm);
    // v3: IR de pantalla publicada por el hilo de mensajes (puntero vigente mientras no se publique otra).
    void setImpulse (const CabImpulse* ir) { impulse = ir; }
    // Latencia (muestras) que introduce la cadena con los ajustes actuales (sobremuestreo del ampli v3).
    static int latencySamples (const FxSettings& s);
    // Parámetros del ampli v3 derivados de los ajustes.
    static AmpParams ampParamsFor (const FxSettings& s);

private:
    struct Comb { std::vector<float> buf; int pos = 0; float store = 0; };
    struct Allpass { std::vector<float> buf; int pos = 0; };

    float overdrive (float x, float drive);
    float distortion (float x, float gain);
    void updateFilters (const FxSettings& s);

    double sr = 48000.0;
    bool prepared = false;
    FxSettings last;
    bool haveLast = false;

    // Sobremuestreo x2 para las etapas no lineales.
    dsp::Biquad upLp1, upLp2, downLp1, downLp2;
    dsp::Biquad odPre, odTone, distPre, distTone;
    dsp::Biquad ampBass, ampMid, ampTreble, ampPostLp;
    std::array<dsp::Biquad, 4> cab;
    dsp::Biquad eqLow, eqMid, eqHigh;
    float svfIc1 = 0, svfIc2 = 0;
    float dcX = 0, dcY = 0;

    // Chorus
    std::vector<float> chorusBuf;
    int chorusPos = 0;
    double chorusPhase = 0;

    // Delay estéreo
    std::vector<float> delayL, delayR;
    int delayPos = 0;
    dsp::Biquad delayToneL, delayToneR;
    float delaySmoothed = -1.0f;

    // Reverb
    std::array<Comb, 8> combL, combR;
    std::array<Allpass, 4> apL, apR;
    std::vector<float> predelayBuf;
    int predelayPos = 0;
    dsp::Biquad revLowCut, revHighCutL, revHighCutR, delayLowCutL, delayLowCutR;

    // v3
    GuitarAmp amp;
    const CabImpulse* impulse = nullptr;

    // Parámetros suavizados
    dsp::Smoothed sDrive, sDelayMix, sReverbMix, sCutoff, sVolume, sChorusMix, sDistGain;
};

} // namespace solo
