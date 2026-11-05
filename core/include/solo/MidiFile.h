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
};

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
};
bool readMidiFile (const std::vector<uint8_t>& data, ParsedMidi& out, std::string& error);

} // namespace solo
