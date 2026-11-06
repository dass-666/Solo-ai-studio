#pragma once
// Guitar Tone Studio v3: amplificador y pantalla.
//   filtro previo -> compresor -> ganancia previa -> [overdrive] -> etapa 1 (asimétrica) -> interetapa ->
//   [distorsión] -> etapa de potencia con "sag" -> tonestack + presencia -> pantalla (modelo o IR) -> filtro de
//   salida -> compensación de nivel.
// Las etapas no lineales se procesan con sobremuestreo 1x/2x/4x (FIR de media banda, fase lineal).
// Sin dependencias: el núcleo no usa JUCE, así que el CLI, las pruebas y la exportación usan exactamente este código.
// NO es la emulación de ningún equipo concreto.
#include "solo/Dsp.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace solo
{
// Respuesta al impulso de pantalla cargada por el usuario (hilo de mensajes). Se remuestrea a la frecuencia del motor
// al prepararla y se recorta a kMaxTaps con un fundido.
struct CabImpulse
{
    static constexpr int kMaxTaps = 2048;
    std::vector<float> taps; // ya a la frecuencia del motor, normalizada en energía
    double sampleRate = 48000.0;
    std::string name;
};
// Prepara una IR a partir de audio mono (cualquier frecuencia). Devuelve false si está vacía o no es finita.
bool prepareCabImpulse (const std::vector<float>& mono, double sourceRate, double engineRate, CabImpulse& out, std::string& error);

struct AmpParams
{
    bool overdrive = false, distortion = false, amp = true, cabinet = true;
    float odDrive = 0.4f, odTone = 0.5f, distGain = 0.5f, distTone = 0.5f;
    float preGainDb = 14.4f;
    int satType = 0;      // 0 válvula, 1 alta ganancia, 2 limpio
    float master = 0.5f;  // etapa de potencia (0..1)
    float sag = 0.3f, compression = 0.3f;
    float bassDb = 0.0f, midDb = 0.0f, trebleDb = 0.0f, presenceDb = 0.0f;
    int cabType = 1;      // 0 1x12 abierta, 1 2x12, 2 4x12 cerrada
    float outputLpHz = 8000.0f, trimDb = 0.0f;
    int oversample = 2;   // 1, 2, 4
    bool operator== (const AmpParams& o) const;
    bool operator!= (const AmpParams& o) const { return ! (*this == o); }
};

// FIR de media banda polifásico (2x). Fase lineal: latencia (taps-1)/2 muestras a la frecuencia alta por sentido.
class Halfband2x
{
public:
    static constexpr int kTaps = 63; // M = 31 (impar): coeficientes pares nulos salvo el central
    void prepare();
    void reset();
    void up (float x, float& even, float& odd);
    float down (float even, float odd);
    static int latencyAtLowRate() { return (kTaps - 1) / 2; } // subida + bajada = (kTaps-1) muestras a 2x = (kTaps-1)/2 a 1x

private:
    std::array<float, (kTaps + 1) / 4 + 1> hOdd {}; // coeficientes impares (simétricos)
    std::array<float, 64> histUp {}, histDnOdd {}, histDnEven {};
    int posUp = 0, posDn = 0;
};

class GuitarAmp
{
public:
    void prepare (double sampleRate);
    void reset();
    // Hilo de audio. 'p' se lee una vez por bloque; los filtros se recalculan solo si cambia.
    void setParams (const AmpParams& p);
    void setImpulse (const CabImpulse* ir) { impulse = ir; }
    float process (float x);
    int latencySamples() const;

    // Ganancia de nivel (lineal) que deja la sonoridad aproximadamente constante al variar la ganancia previa:
    // mide la salida de las etapas no lineales con una señal de referencia. Determinista.
    static float compensationGain (const AmpParams& p);
    // Curva de la macro Fuerza (docs/GUITAR-TONE.md): preGainDb = 2 + 22·(F/100)^1.6.
    static float preGainDbForForce (double forcePercent);

private:
    struct Stages
    {
        dsp::Biquad odPre, odTone, inter1Hp, inter1Lp, distTone, inter2Hp;
        float dcX = 0, dcY = 0;
        float envFast = 0, envSlow = 0;
        void reset();
        void design (double rate, const AmpParams& p);
        float run (float x, const AmpParams& p, float preGain);
    };
    static float saturate (float v, int type);

    double sr = 48000.0;
    AmpParams params;
    bool haveParams = false;
    Stages stages;
    double stageRate = 96000.0;
    std::array<Halfband2x, 2> os;
    dsp::Biquad inHp, compHp;
    dsp::Biquad tsLow, tsMid, tsHigh, presence;
    std::array<dsp::Biquad, 5> cab;
    dsp::Biquad outLp;
    float compEnv = 0.0f;
    dsp::Smoothed sPreGain, sComp, sTrim;
    const CabImpulse* impulse = nullptr;
    std::vector<float> irHist;
    int irPos = 0;
};

} // namespace solo
