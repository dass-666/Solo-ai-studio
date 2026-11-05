#pragma once
// Dirección por voz — arquitectura en etapas estrictamente separadas:
//   1. Reconocimiento  (voz -> texto)       : interfaz SpeechRecognizer. NO hay implementación incluida
//                                             (no se usan servicios de pago ni modelos grandes); hoy el texto
//                                             se escribe en la interfaz o lo aporta un reconocedor externo.
//   2. Interpretación  (texto -> operaciones): parseCommand(), reglas en español, sin red.
//   3. Operaciones      (conjunto pequeño y estructurado): CommandOp.
//   4. Confirmación     : previewCommand() aplica sobre una COPIA y describe el resultado.
//   5. Ejecución        : executeCommand() aplica sobre el proyecto (cada operación es deshacible).
//   6. Historial        : UndoStack del ProjectController (deshacer / rehacer).
#include "solo/Project.h"
#include <string>
#include <vector>

namespace solo
{
// ---- 1. reconocimiento (extensión futura)
struct RecognizedText
{
    std::string text;
    double confidence = 1.0;      // 1 = escrito a mano
    std::string source = "texto";
};

class SpeechRecognizer
{
public:
    virtual ~SpeechRecognizer() = default;
    // Debe ser local (sin servicios de pago). Devuelve false si no hay reconocedor disponible.
    virtual bool recognise (const std::vector<float>& mono16k, RecognizedText& out, std::string& error) = 0;
};

// ---- 3. operaciones
enum class OpKind
{
    Generate, RegenerateBars, LockBars, UnlockBars, Adjust, SetStyle, SetInstrument,
    FxPreset, FxAdjust, FxOff, Transpose, AddArticulation, RemoveArticulation,
    Undo, Redo, Play, Stop, Favorite, SwitchAB
};

struct CommandOp
{
    OpKind kind = OpKind::Generate;
    int fromBar = -1, toBar = -1;   // base 0; -1 = todo el solo
    double amount = 0;              // ajustes (+/-), semitonos
    std::string what;               // "energy"/"density"/"complexity", nombre de preset...
    Style style = Style::MelodicRock;
    InstrumentId instrument = InstrumentId::Guitar;
    AutoTarget fxTarget = AutoTarget::DelayMix;
    Articulation articulation = Articulation::Vibrato;

    std::string describe() const; // en español, para la confirmación
};

// ---- 2. interpretación
struct ParsedCommand
{
    std::vector<CommandOp> ops;
    std::vector<std::string> unrecognised; // fragmentos que no se entendieron
    double confidence = 0;                 // fracción de fragmentos entendidos x confianza del reconocimiento
    bool needsConfirmation = true;         // las operaciones que cambian notas siempre piden confirmación
    std::string summary() const;
};

ParsedCommand parseCommand (const RecognizedText& input);

// ---- 4. confirmación (vista previa) y 5. ejecución
struct CommandPreview
{
    bool ok = false;
    std::vector<std::string> steps;  // qué se hará / qué pasó en la copia
    int notesBefore = 0, notesAfter = 0;
    int variationsAdded = 0;
};
CommandPreview previewCommand (const ProjectController& project, const ParsedCommand& cmd, uint64_t seed);

struct CommandResult
{
    bool ok = true;
    std::vector<OpResult> results;
    bool sequenceChanged = false, fxChanged = false, settingsChanged = false;
    bool requestPlay = false, requestStop = false;
};
CommandResult executeCommand (ProjectController& project, const ParsedCommand& cmd, uint64_t seed);

// Normaliza texto en español: minúsculas, sin tildes, números escritos en cifras.
std::string normaliseSpanish (const std::string& utf8);

} // namespace solo
