#pragma once
// MidiEngine (parte 2): exportación/lectura de Standard MIDI File (SMF).
#include "solo/MusicTheory.h"
#include "solo/Sequence.h"
#include <cstdint>
#include <string>
#include <vector>

namespace solo
{
struct MidiExportOptions
{
    int format = 1;            // 0 = una pista, 1 = pista de tempo + pista de notas
    std::string trackName = "Solo AI Studio";
    int gmProgram = -1;        // -1 = no escribir Program Change
    bool writeKeySignature = true;
    Key key;
    // Expresión (bends, vibrato, slides) como Pitch Bend. Rango fijado por RPN 0 al inicio de la pista.
    // Limitación: el pitch bend afecta a todo el canal, así que solo es exacto con una voz a la vez (el solo es monofónico).
    bool pitchBend = true;
    int bendRangeSemitones = 12;
    int bendResolutionTicks = 20;  // cada cuánto se muestrea la curva
    // v3: si >= 0, las curvas se calculan con gesturesFor()/gesturePitchOffset() escaladas por la macro Expresión,
    // de modo que el pitch bend exportado coincide con lo que suena. -1 = curva v2 (NoteExpression sin escalar).
    double expressionPercent = -1.0;
    // Legato: alarga la nota anterior unos ticks para que los sintes en modo mono/portamento liguen.
    bool legatoOverlap = true;
    // Marcas de texto informativas (PM = palm mute, H/P = hammer/pull, BU = bend). Ningún sinte las interpreta.
    bool articulationText = true;
};

// Desplazamiento de altura (semitonos) de una nota con expresión, en el instante tSec desde su inicio.
// Misma forma que el motor de síntesis (bends con curva suave, vibrato con retardo, slide de entrada).
double expressionOffsetSemis (const NoteExpression& e, double tSec, double lengthSec, bool upwardVibrato);

std::vector<uint8_t> writeMidiFile (const Sequence& seq, const MidiExportOptions& opts);
bool saveMidiFile (const std::string& path, const Sequence& seq, const MidiExportOptions& opts, std::string& error);

// Lector mínimo (para pruebas y para importar): notas, tempo y compás.
struct ParsedMidi
{
    int format = -1;
    int numTracks = 0;
    int ppq = 0;
    double bpm = 0.0;
    int tsNumerator = 0, tsDenominator = 0;
    std::vector<Note> notes; // ordenadas por inicio
    int unmatchedNoteOffs = 0;
    int danglingNoteOns = 0;
    std::string trackName;
    int program = -1;
    struct Bend { int64_t tick; int channel; int value; }; // value: -8192..8191
    std::vector<Bend> pitchBends;
    int bendRangeSemitones = 2; // según RPN 0 si aparece (por defecto, 2 del estándar GM)
    std::vector<std::pair<int64_t, std::string>> texts;
};
bool readMidiFile (const std::vector<uint8_t>& data, ParsedMidi& out, std::string& error);

} // namespace solo
