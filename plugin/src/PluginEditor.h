#pragma once
#include "PianoRoll.h"
#include "PluginProcessor.h"
#include "ProPanels.h"
#include <juce_gui_extra/juce_gui_extra.h>

class SoloLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SoloLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
};

// Zona desde la que se arrastra el MIDI al DAW (opcional; depende del host).
class DragMidiArea : public juce::Component
{
public:
    explicit DragMidiArea (SoloAIStudioProcessor& p) : proc (p) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }

private:
    SoloAIStudioProcessor& proc;
    bool dragging = false;
};

class SoloAIStudioEditor : public juce::AudioProcessorEditor,
                           private juce::Timer,
                           private juce::ChangeListener
{
public:
    explicit SoloAIStudioEditor (SoloAIStudioProcessor&);
    ~SoloAIStudioEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct LayoutBox : juce::Component
    {
        std::function<void()> layout;
        void resized() override { if (layout) layout(); }
    };
    void layoutGeneration();
    void refreshPanels();
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void refreshFromProcessor();
    void refreshSequence();
    void pushSettings();
    void setProMode (bool pro);
    void status (const juce::String& s, bool error = false);
    void validateTextFields();

    void onGenerate();
    void commitMacros (bool remember); // perillas Fuerza/Expresión/Ambiente -> valores DSP reales
    void onExportMidi();
    void onExportWav();
    void onSavePreset();
    void onLoadPreset();

    juce::File defaultExportFolder() const;

    SoloAIStudioProcessor& proc;
    SoloLookAndFeel lnf;
    bool updating = false;
    bool lockSeed = false;
    juce::String lastStatusShown;

    // Cabecera
    juce::Label title, hostLabel, statusLabel;
    juce::TextButton easyButton { juce::String::fromUTF8 ("Fácil") }, proButton { "Pro" };
    juce::TextButton undoButton { "Deshacer" }, redoButton { "Rehacer" };

    // Modo fácil
    juce::Label keyLabel, progLabel, instLabel, styleLabel, barsLabel, tsLabel, energyLabel, densityLabel;
    juce::TextEditor keyEditor, progEditor;
    juce::ComboBox instrumentBox, styleBox, barsBox, tsBox;
    juce::Slider energySlider, densitySlider;
    // Guitar Tone Studio v3 (modo fácil)
    juce::Label toneLabel, guitarLabel, forceLabel, expressionLabel, ambienceLabel;
    juce::ComboBox toneBox, guitarBox;
    juce::Slider forceKnob, expressionKnob, ambienceKnob;
    juce::TextButton generateButton { "GENERAR" }, playButton { "ESCUCHAR" };
    juce::TextButton prevButton { "<" }, nextButton { ">" };
    juce::Label variationLabel;
    juce::TextButton favButton { juce::String::fromUTF8 ("☆") };
    juce::TextButton voiceButton { "TARAREAR / IMPORTAR VOZ" };
    juce::TextButton exportMidiButton { "Exportar MIDI" }, exportWavButton { "Exportar WAV" };
    DragMidiArea dragArea;

    // Modo Pro
    juce::Label seedLabel, complexityLabel, bpmLabel, startBarLabel, velocityLabel, presetLabel;
    juce::TextEditor seedEditor;
    juce::ToggleButton lockSeedToggle { "Fijar semilla" };
    juce::Slider complexitySlider, bpmSlider, startBarSlider, velocitySlider;
    juce::ToggleButton hostTempoToggle { "BPM del DAW" }, syncToggle { "Sonar con el DAW" }, loopToggle { "Loop" };
    juce::ComboBox performerBox, engineBox;
    juce::Label performerLabel;
    juce::ToggleButton keepMotifsToggle { "Mantener motivos" }, pitchBendToggle { "MIDI con pitch bend" };
    juce::ComboBox presetBox, rateBox, fmtBox;
    juce::TextButton savePresetButton { "Guardar" }, loadPresetButton { "Cargar" };
    juce::TextButton transportDiagButton { juce::String::fromUTF8 ("Diagnóstico transporte") };
    juce::TextButton zoomInButton { "+" }, zoomOutButton { "-" }, fitButton { "Ajustar" };

    PianoRoll pianoRoll;

    // Pestañas Pro (deben declararse después de pianoRoll: el contexto lo usa)
    PanelContext panelCtx;
    LayoutBox genTab;
    std::unique_ptr<NotesPanel> notesPanel;
    std::unique_ptr<FxPanel> fxPanel;
    std::unique_ptr<TonePanel> tonePanel;
    std::unique_ptr<AutomationPanel> autoPanel;
    std::unique_ptr<VoicePanel> voicePanel;
    std::unique_ptr<HistoryPanel> historyPanel;
    std::unique_ptr<CommandPanel> commandPanel;
    juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoloAIStudioEditor)
};
