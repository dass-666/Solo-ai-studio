#pragma once
// Pestañas del modo Pro. Cada una usa SOLO operaciones de solo::ProjectController a través de
// SoloAIStudioProcessor::perform(), de modo que todo lo que se hace aquí es deshacible y queda
// cubierto por las pruebas del núcleo.
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Lo que las pestañas necesitan del editor.
struct PanelContext
{
    SoloAIStudioProcessor& proc;
    std::function<void()> refreshAll;                       // releer proyecto (piano roll, etiquetas…)
    std::function<void (const juce::String&, bool)> status; // mensaje en la barra de estado
    std::function<int()> rangeStart, rangeEnd;              // compases elegidos en la regla (-1 = ninguno)
    std::function<int()> selectedNote;                      // índice de nota seleccionada (-1 = ninguna)
};

class ProPanel : public juce::Component
{
public:
    explicit ProPanel (PanelContext& c) : ctx (c) {}
    virtual void refresh() {}

protected:
    // Ejecuta una operación, muestra el resultado y refresca la interfaz.
    solo::OpResult run (const std::function<solo::OpResult (solo::ProjectController&)>& op);
    bool rangeOrWarn (int& a, int& b);
    PanelContext& ctx;
};

// ---------------------------------------------------------------- Notas: bloqueos, articulaciones, transposición
class NotesPanel : public ProPanel
{
public:
    explicit NotesPanel (PanelContext&);
    void resized() override;

private:
    juce::TextButton lockBars { "Bloquear compases" }, unlockBars { "Desbloquear compases" },
        lockNote { "Bloquear/desbloquear nota" }, regenBars { "Regenerar compases" }, regenNote { "Regenerar nota" };
    juce::TextButton tDown12 { "-12" }, tDown1 { "-1" }, tUp1 { "+1" }, tUp12 { "+12" };
    juce::ComboBox artBox;
    juce::TextButton artAdd { juce::String::fromUTF8 ("Añadir") }, artRemove { "Quitar" };
    juce::Label hint, transLabel, artLabel;
};

// ---------------------------------------------------------------- Efectos
class FxPanel : public ProPanel
{
public:
    explicit FxPanel (PanelContext&);
    void resized() override;
    void refresh() override;

private:
    void commitSliders();
    juce::ComboBox presetBox;
    juce::ToggleButton enableToggle { "Efectos activos" };
    juce::OwnedArray<juce::ToggleButton> stageToggles;
    juce::OwnedArray<juce::Slider> sliders; // uno por destino de automatización (valor normalizado)
    juce::OwnedArray<juce::Label> sliderLabels;
    bool updating = false;
};

// ---------------------------------------------------------------- Ampli / Samples (Guitar Tone Studio v3)
// Muestra y edita los valores DSP reales que las macros escriben (previo, compresión, presencia, predelay…),
// el motor de guitarra, los parámetros de la fuente, el banco SFZ y la IR de pantalla.
class TonePanel : public ProPanel
{
public:
    explicit TonePanel (PanelContext&);
    void resized() override;
    void refresh() override;

private:
    struct Param
    {
        juce::String name;
        double lo, hi, step;
        std::function<double (const solo::FxSettings&, const solo::GuitarParams&)> get;
        std::function<void (solo::FxSettings&, solo::GuitarParams&, double)> set;
        bool guitar; // true: cambia GuitarParams (setGuitarParams); false: FxSettings (setFx)
    };
    void commit (int index);
    void loadSfz();
    void loadIr();
    std::vector<Param> params;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::Label> labels;
    juce::ComboBox engineBox, pickupBox, satBox, oversampleBox;
    juce::Label engineLabel, pickupLabel, satLabel, oversampleLabel, bankLabel, irLabel, latencyLabel;
    juce::TextButton sfzButton { "Cargar SFZ..." }, sfzClear { "Quitar" }, irButton { "Cargar IR (WAV)..." }, irClear { "Quitar" },
        synthButton { juce::String::fromUTF8 ("Banco sintético de prueba") };
    std::unique_ptr<juce::FileChooser> chooser;
    bool updating = false;
};

// ---------------------------------------------------------------- Automatización (curva por compases)
class AutomationCurve : public juce::Component
{
public:
    std::function<void (const solo::AutomationLane&)> onCommit;
    void setLane (const solo::AutomationLane& l, int64_t totalTicks, int64_t barTicks);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    juce::Point<float> toScreen (const solo::AutoPoint& p) const;
    solo::AutoPoint fromScreen (juce::Point<float> p) const;
    int nearest (juce::Point<float> p) const;
    solo::AutomationLane lane;
    int64_t total = 1, bar = 1;
    int dragging = -1;
    bool changed = false;
};

class AutomationPanel : public ProPanel
{
public:
    explicit AutomationPanel (PanelContext&);
    void resized() override;
    void refresh() override;

private:
    solo::AutoTarget target() const;
    juce::ComboBox targetBox;
    juce::ToggleButton enableToggle { "Activa" };
    juce::TextButton clearButton { "Borrar curva" };
    juce::Label hint;
    AutomationCurve curve;
};

// ---------------------------------------------------------------- Voz (Hum-to-Solo) y ADN creativo
class VoicePanel : public ProPanel
{
public:
    explicit VoicePanel (PanelContext&);
    void resized() override;
    void refresh() override;
    solo::HumToSoloOptions humOptions() const;
    void importVoice(); // también lo usa el modo fácil

private:
    juce::ComboBox humMode, dnaMode;
    juce::ToggleButton rhythmOnly { "Solo ritmo" };
    juce::TextButton importButton { "Importar voz (WAV)..." }, correctButton { "Corregir compases con voz..." };
    juce::TextButton extractButton { "Extraer ADN" }, deriveButton { "Generar desde ADN" };
    juce::Slider fidelity, freedom, expression, development;
    juce::Label fidelityL, freedomL, expressionL, developmentL, dnaSummary, voiceNote;
    std::unique_ptr<juce::FileChooser> chooser;
};

// ---------------------------------------------------------------- Historial, favoritas y A/B
class HistoryPanel : public ProPanel, private juce::ListBoxModel
{
public:
    explicit HistoryPanel (PanelContext&);
    void resized() override;
    void refresh() override;

private:
    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int lastRowSelected) override;
    juce::ListBox list;
    juce::TextButton favButton { juce::String::fromUTF8 ("★ Favorita") }, renameButton { "Renombrar" },
        deleteButton { "Eliminar" }, slotA { "Asignar A" }, slotB { "Asignar B" }, switchAB { "Escuchar A/B" };
    juce::TextEditor nameEditor;
    juce::Label abLabel;
    std::vector<juce::String> rows;
    int currentRow = -1;
    bool updating = false;
};

// ---------------------------------------------------------------- Dirección por texto (y voz, cuando haya reconocedor)
class CommandPanel : public ProPanel
{
public:
    explicit CommandPanel (PanelContext&);
    void resized() override;

private:
    void interpret();
    juce::TextEditor input;
    juce::TextButton interpretButton { "Interpretar" }, executeButton { "Ejecutar" }, cancelButton { "Cancelar" };
    juce::Label preview;
    solo::ParsedCommand pending;
    bool hasPending = false;
};
