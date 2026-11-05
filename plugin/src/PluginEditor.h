#pragma once
#include "PianoRoll.h"
#include "PluginProcessor.h"
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

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    void refreshFromProcessor();
    void refreshSequence();
    void pushSettings();
    void setProMode (bool pro);
    void status (const juce::String& s, bool error = false);
    void validateTextFields();

    void onGenerate();
    void onExportMidi();
    void onExportWav();
    void onSavePreset();
    void onLoadPreset();
    void onRegenerateRange();

    juce::File defaultExportFolder() const;

    SoloAIStudioProcessor& proc;
    SoloLookAndFeel lnf;
    bool updating = false;
    bool lockSeed = false;
    juce::String lastStatusShown;

    // Cabecera
    juce::Label title, hostLabel, statusLabel;
    juce::TextButton easyButton { juce::String::fromUTF8 ("Fácil") }, proButton { "Pro" };

    // Modo fácil
    juce::Label keyLabel, progLabel, instLabel, styleLabel, barsLabel, tsLabel, energyLabel, densityLabel;
    juce::TextEditor keyEditor, progEditor;
    juce::ComboBox instrumentBox, styleBox, barsBox, tsBox;
    juce::Slider energySlider, densitySlider;
    juce::TextButton generateButton { "GENERAR" }, playButton { "ESCUCHAR" };
    juce::TextButton prevButton { "<" }, nextButton { ">" };
    juce::Label variationLabel;
    juce::TextButton exportMidiButton { "Exportar MIDI" }, exportWavButton { "Exportar WAV" };
    DragMidiArea dragArea;

    // Modo Pro
    juce::Label seedLabel, complexityLabel, bpmLabel, startBarLabel, velocityLabel, presetLabel;
    juce::TextEditor seedEditor;
    juce::ToggleButton lockSeedToggle { "Fijar semilla" };
    juce::Slider complexitySlider, bpmSlider, startBarSlider, velocitySlider;
    juce::ToggleButton hostTempoToggle { "BPM del DAW" }, syncToggle { "Sonar con el DAW" }, loopToggle { "Loop" };
    juce::TextButton regenButton { "Regenerar compases" };
    juce::ComboBox presetBox, rateBox, fmtBox;
    juce::TextButton savePresetButton { "Guardar" }, loadPresetButton { "Cargar" };
    juce::TextButton zoomInButton { "+" }, zoomOutButton { "-" }, fitButton { "Ajustar" };

    PianoRoll pianoRoll;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoloAIStudioEditor)
};
