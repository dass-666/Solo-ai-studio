#pragma once
// V4: interfaz del beat y del Melody Explorer.
//   BeatStrip   — franja del modo Fácil (y Pro): importar beat, vista del beat (acordes y energía por compás),
//                 resultado del análisis, tipo de melodía, carácter, libertad creativa, GENERAR (4 propuestas),
//                 variar esta idea; secundarias: proponer ritmo / proponer melodía (WAV).
//   BeatProPanel — pestaña Pro: BPM, tonalidad y acordes confirmables, confianzas, sección, patrón rítmico editable,
//                 corregir compases tarareando, familia/identidad.
// Todo pasa por operaciones de solo::ProjectController (deshacibles y cubiertas por las pruebas del núcleo).
#include "ProPanels.h"

class BeatView : public juce::Component
{
public:
    void setView (const SoloAIStudioProcessor::BeatView& v) { view = v; repaint(); }
    void paint (juce::Graphics&) override;
    std::function<void (int bar)> onBarClick; // elegir sección (clic = inicio, shift+clic = fin)
    void mouseDown (const juce::MouseEvent&) override;

private:
    SoloAIStudioProcessor::BeatView view;
};

class BeatStrip : public ProPanel
{
public:
    explicit BeatStrip (PanelContext&);
    void resized() override;
    void refresh() override;
    void importBeat();
    void generateProposals();
    void varyIdea();
    solo::MelodyBrief briefFromControls() const;

private:
    juce::TextButton importButton { "IMPORTAR BEAT (WAV)..." }, clearButton { "Quitar" };
    juce::Label analysisLabel, typeLabel, characterLabel, freedomLabel, familyLabel;
    juce::ComboBox typeBox, characterBox;
    juce::Slider freedomSlider;
    juce::TextButton generateButton { "GENERAR (4 PROPUESTAS)" }, varyButton { "VARIAR ESTA IDEA" };
    juce::TextButton rhythmButton { "Proponer ritmo (WAV)..." }, melodyButton { juce::String::fromUTF8 ("Proponer melodía (WAV)...") };
    BeatView beatView;
    std::unique_ptr<juce::FileChooser> chooser;
    bool updating = false;
};

class BeatProPanel : public ProPanel
{
public:
    explicit BeatProPanel (PanelContext&);
    void resized() override;
    void refresh() override;

private:
    juce::Label bpmL, keyL, progL, sectionL, infoL, rhythmL, familyL;
    juce::TextEditor bpmEditor, keyEditor, progEditor, rhythmEditor;
    juce::TextButton bpmButton { "Confirmar BPM" }, keyButton { "Confirmar tonalidad" }, progButton { "Acordes a mano" };
    juce::TextButton useButton { "Usar en el compositor" };
    juce::Slider sectionFrom, sectionTo;
    juce::TextButton sectionButton { juce::String::fromUTF8 ("Fijar sección") };
    juce::TextButton rhythmFile { "Proponer ritmo (WAV)..." }, rhythmApply { juce::String::fromUTF8 ("Aplicar patrón") };
    juce::TextButton correctButton { "Corregir compases con voz (WAV)..." }, regionButton { juce::String::fromUTF8 ("Regenerar compases (familia)") };
    std::unique_ptr<juce::FileChooser> chooser;
    bool updating = false;
};
