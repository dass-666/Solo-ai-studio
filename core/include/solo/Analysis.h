#pragma once
// Métricas objetivas de un solo (para pruebas, comparación A/B entre motores e informes).
// Son indicadores estructurales, NO una medida de calidad musical percibida.
#include "solo/SoloComposer.h"
#include <string>

namespace solo
{
struct SoloMetrics
{
    int notes = 0;
    double notesPerSecond = 0;
    double restRatio = 0;          // fracción del tiempo sin sonar
    double strongChordTone = 0;    // fracción de notas en tiempo fuerte que son del acorde (altura final tras bend)
    double stepwiseRatio = 0;      // intervalos de 1-2 semitonos
    double leapRatio = 0;          // intervalos > 5 semitonos
    double repeatedPitchRatio = 0;
    int pitchMin = 0, pitchMax = 0;
    double meanPitch = 0;
    int phrases = 0;               // grupos separados por silencios >= media parte
    double motifRecurrence = 0;    // fracción de 4-gramas de intervalos que reaparecen (exactos o transportados)
    double rhythmRecurrence = 0;   // fracción de patrones rítmicos de compás que se repiten
    bool resolvesToStable = false; // última nota en nota estable del último acorde
    double velocityStdDev = 0;
    int bends = 0, vibratos = 0, slides = 0, legatos = 0, palmMutes = 0;
    double offGridRatio = 0;       // notas fuera de la rejilla de semicorchea (micro-tiempo)

    std::string toString() const;
};

SoloMetrics analyseSolo (const Sequence& s, const ComposerParams& p);

// Sonoridad integrada aproximada según ITU-R BS.1770 (ponderación K, puertas -70 LUFS y -10 LU).
double integratedLoudness (const std::vector<float>& left, const std::vector<float>& right, double sampleRate);
// Ajusta la ganancia para alcanzar 'targetLufs'; limita el pico a 'peakCeiling'. Devuelve la ganancia aplicada.
double normaliseLoudness (std::vector<float>& left, std::vector<float>& right, double sampleRate, double targetLufs,
                          float peakCeiling = 0.97f);

} // namespace solo
