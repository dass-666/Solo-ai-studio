#include "PluginEditor.h"

using namespace solo;

namespace
{
const juce::Colour kBg (0xff101216), kPanel (0xff181b21), kText (0xffd7dce5), kDim (0xff8a93a3),
    kAccent (0xff2ec4b6), kError (0xffff6b6b);
constexpr int kHeader = 44, kStatus = 24, kLeft = 296, kPro = 272;

juce::String U (const char* s) { return juce::String::fromUTF8 (s); }

void setupLabel (juce::Label& l, const juce::String& text, juce::Component& parent)
{
    l.setText (text, juce::dontSendNotification);
    l.setColour (juce::Label::textColourId, kDim);
    l.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l.setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l.setFont (juce::FontOptions (13.0f));
    parent.addAndMakeVisible (l);
}

void setupSlider (juce::Slider& s, double lo, double hi, double step, juce::Component& parent)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
    s.setRange (lo, hi, step);
    parent.addAndMakeVisible (s);
}
} // namespace

// ============================================================ LookAndFeel
SoloLookAndFeel::SoloLookAndFeel()
{
    setColourScheme ({ 0xff181b21, 0xff101216, 0xff20242c, 0xff3a404c, 0xffd7dce5, 0xff2ec4b6, 0xff0b0c0f, 0xff2ec4b6, 0xffd7dce5 });
    setColour (juce::TextButton::buttonColourId, juce::Colour (0xff232730));
    setColour (juce::TextButton::buttonOnColourId, kAccent);
    setColour (juce::TextButton::textColourOffId, kText);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff0b0c0f));
    setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff20242c));
    setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff2e333d));
    setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff20242c));
    setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff2e333d));
    setColour (juce::TextEditor::focusedOutlineColourId, kAccent);
    setColour (juce::Slider::trackColourId, kAccent);
    setColour (juce::Slider::thumbColourId, juce::Colour (0xffe8ecf2));
    setColour (juce::Slider::backgroundColourId, juce::Colour (0xff2a2f38));
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ToggleButton::tickColourId, kAccent);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1d2027));
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent.withAlpha (0.35f));
}

void SoloLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    auto c = b.getToggleState() ? findColour (juce::TextButton::buttonOnColourId) : bg;
    if (down) c = c.brighter (0.15f);
    else if (over) c = c.brighter (0.08f);
    if (! b.isEnabled()) c = c.withAlpha (0.4f);
    g.setColour (c);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colour (0xff2e333d));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

// ============================================================ Arrastre
void DragMidiArea::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (0xff1d2027));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (kDim);
    const float dashes[] = { 4.0f, 3.0f };
    juce::Path p, dashed;
    p.addRoundedRectangle (r, 5.0f);
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
    g.fillPath (dashed);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText (U ("Arrastrar MIDI al DAW (experimental)"), getLocalBounds(), juce::Justification::centred);
}

void DragMidiArea::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6 || ! proc.hasSequence()) return;
    dragging = true;
    const auto f = proc.writeTempMidiForDrag();
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this,
                                                                   [this] { dragging = false; });
    else
        dragging = false;
}

// ============================================================ Editor
SoloAIStudioEditor::SoloAIStudioEditor (SoloAIStudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), dragArea (p),
      panelCtx { p,
                 [this] { refreshFromProcessor(); },
                 [this] (const juce::String& m, bool e) { status (m, e); },
                 [this] { return pianoRoll.getRangeStartBar(); },
                 [this] { return pianoRoll.getRangeEndBar(); },
                 [this] { return pianoRoll.getSelectedIndex(); } }
{
    setLookAndFeel (&lnf);

    title.setText ("SOLO AI STUDIO", juce::dontSendNotification);
    title.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    title.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (title);
    hostLabel.setJustificationType (juce::Justification::centredRight);
    hostLabel.setColour (juce::Label::textColourId, kDim);
    addAndMakeVisible (hostLabel);
    statusLabel.setColour (juce::Label::textColourId, kDim);
    statusLabel.setFont (juce::FontOptions (12.5f));
    addAndMakeVisible (statusLabel);

    addAndMakeVisible (easyButton);
    addAndMakeVisible (proButton);
    easyButton.onClick = [this] { setProMode (false); };
    proButton.onClick = [this] { setProMode (true); };
    undoButton.onClick = [this] { proc.perform ([] (ProjectController& pc) { return pc.undoLast(); }); refreshFromProcessor(); };
    redoButton.onClick = [this] { proc.perform ([] (ProjectController& pc) { return pc.redoLast(); }); refreshFromProcessor(); };
    undoButton.setTooltip (U ("Deshacer (Ctrl+Z)"));
    redoButton.setTooltip (U ("Rehacer (Ctrl+Shift+Z)"));
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);

    // ---------------------------------------------------- Modo fácil
    setupLabel (keyLabel, "Tonalidad", *this);
    setupLabel (progLabel, "Acordes", *this);
    setupLabel (instLabel, "Instrumento", *this);
    setupLabel (styleLabel, "Estilo", *this);
    setupLabel (barsLabel, U ("Duración"), *this);
    setupLabel (tsLabel, U ("Compás"), *this);
    setupLabel (energyLabel, "Intensidad", *this);
    setupLabel (densityLabel, "Densidad", *this);
    setupLabel (toneLabel, "Tono", *this);
    setupLabel (guitarLabel, "Guitarra", *this);
    setupLabel (forceLabel, "Fuerza", *this);
    setupLabel (expressionLabel, U ("Expresión"), *this);
    setupLabel (ambienceLabel, "Ambiente", *this);
    for (auto* l : { &forceLabel, &expressionLabel, &ambienceLabel }) l->setJustificationType (juce::Justification::centred);

    keyEditor.setTooltip (U ("Ej.: Am, C, F#m, La menor"));
    progEditor.setTooltip (U ("Un acorde por compás: Am F C G  (también Am | F | C | G, % repite)"));
    for (auto* ed : { &keyEditor, &progEditor })
    {
        ed->setFont (juce::FontOptions (14.0f));
        ed->setIndents (6, 4);
        ed->onTextChange = [this] { if (! updating) { validateTextFields(); pushSettings(); } };
        addAndMakeVisible (ed);
    }

    for (int i = 0; i < kNumInstruments; ++i) instrumentBox.addItem (U (instrumentName ((InstrumentId) i)), i + 1);
    for (int i = 0; i < kNumStyles; ++i) styleBox.addItem (U (styleName ((Style) i)), i + 1);
    for (int b : { 2, 4, 8, 16 }) barsBox.addItem (juce::String (b) + U (" compases"), b);
    tsBox.addItem ("4/4", 4);
    tsBox.addItem ("3/4", 3);
    for (auto* cb : { &instrumentBox, &styleBox, &barsBox, &tsBox })
    {
        cb->onChange = [this] { if (! updating) pushSettings(); };
        addAndMakeVisible (cb);
    }

    setupSlider (energySlider, 0.0, 1.0, 0.01, *this);
    setupSlider (densitySlider, 0.0, 1.0, 0.01, *this);
    energySlider.onValueChange = densitySlider.onValueChange = [this] { if (! updating) pushSettings(); };

    // ---- Guitar Tone Studio v3: tono, guitarra y las tres macroperillas
    for (size_t i = 0; i < tonePresets().size(); ++i) toneBox.addItem (U (tonePresets()[i].name), (int) i + 1);
    toneBox.setTextWhenNothingSelected (U ("Personalizado"));
    toneBox.onChange = [this] {
        if (updating || toneBox.getSelectedId() < 1) return;
        const std::string name = tonePresets()[(size_t) toneBox.getSelectedId() - 1].name;
        const auto r = proc.perform ([name] (ProjectController& pc) { return pc.setTonePreset (name); });
        status (utf8 (r.message), ! r.ok);
        refreshFromProcessor();
    };
    for (int i = 0; i < kNumGuitarEngines; ++i) guitarBox.addItem (U (guitarEngineName ((GuitarEngineKind) i)), i + 1);
    guitarBox.onChange = [this] {
        if (updating || guitarBox.getSelectedId() < 1) return;
        const auto k = (GuitarEngineKind) (guitarBox.getSelectedId() - 1);
        const auto r = proc.perform ([k] (ProjectController& pc) { return pc.setGuitarEngine (k); });
        status (utf8 (r.message), ! r.ok);
        refreshFromProcessor();
    };
    for (auto* cb : { &toneBox, &guitarBox }) addAndMakeVisible (cb);
    for (auto* k : { &forceKnob, &expressionKnob, &ambienceKnob })
    {
        k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 44, 16);
        k->setRange (0.0, 100.0, 1.0);
        k->setTextValueSuffix (" %");
        k->setColour (juce::Slider::rotarySliderFillColourId, kAccent);
        // Arrastrar: previsualización sin pasos de deshacer; soltar (o rueda/teclado): un único paso.
        k->onValueChange = [this, k] { if (! updating) commitMacros (! k->isMouseButtonDown()); };
        k->onDragEnd = [this] { if (! updating) commitMacros (true); };
        addAndMakeVisible (k);
    }
    forceKnob.setTooltip (U ("Fuerza: ganancia del previo (2..24 dB), compresión, ataque y presencia"));
    expressionKnob.setTooltip (U ("Expresión: cuánto vibrato, bends, slides y legato (0 % = ninguno; se aplica al generar y al sonar)"));
    ambienceKnob.setTooltip (U ("Ambiente: reverb (hasta 32 %), delay (hasta 25 %), predelay y cola"));

    generateButton.setColour (juce::TextButton::buttonColourId, kAccent.darker (0.25f));
    generateButton.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    generateButton.onClick = [this] { onGenerate(); };
    playButton.onClick = [this] {
        if (proc.isPreviewing()) proc.stopPreview();
        else if (proc.hasSequence()) proc.startPreview();
        else status (U ("Primero genera un solo"), true);
    };
    prevButton.onClick = [this] { proc.previousVariation(); refreshSequence(); };
    nextButton.onClick = [this] { proc.nextVariation(); refreshSequence(); };
    variationLabel.setJustificationType (juce::Justification::centred);
    variationLabel.setColour (juce::Label::textColourId, kText);
    favButton.setTooltip (U ("Marcar la variación como favorita (las favoritas nunca se descartan del historial)"));
    favButton.onClick = [this] {
        proc.perform ([] (ProjectController& pc) {
            if (! pc.hasSequence()) return OpResult::fail ("No hay variaciones");
            return pc.setFavorite (pc.state.history.currentIndex(), ! pc.state.history.currentItem().favorite);
        });
        refreshFromProcessor();
    };
    voiceButton.setTooltip (U ("Importa una grabación de voz (WAV) tarareando la melodía y la convierte en solo. Modo y opciones en Pro > Voz / ADN."));
    voiceButton.onClick = [this] { pushSettings(); if (voicePanel) voicePanel->importVoice(); };
    exportMidiButton.onClick = [this] { onExportMidi(); };
    exportWavButton.onClick = [this] { onExportWav(); };
    for (auto* c : std::initializer_list<juce::Component*> { &generateButton, &playButton, &prevButton, &nextButton,
                                                             &variationLabel, &favButton, &voiceButton, &exportMidiButton, &exportWavButton, &dragArea })
        addAndMakeVisible (c);

    // ---------------------------------------------------- Modo Pro
    setupLabel (seedLabel, "Semilla", *this);
    setupLabel (complexityLabel, "Complejidad", *this);
    setupLabel (bpmLabel, "BPM manual", *this);
    setupLabel (startBarLabel, U ("Empieza en compás"), *this);
    setupLabel (velocityLabel, "Velocity nota", *this);
    setupLabel (presetLabel, "Preset", *this);

    seedEditor.setInputRestrictions (20, "0123456789");
    seedEditor.onTextChange = [this] { if (! updating) pushSettings(); };
    addAndMakeVisible (seedEditor);
    lockSeedToggle.onClick = [this] { lockSeed = lockSeedToggle.getToggleState(); };
    addAndMakeVisible (lockSeedToggle);

    setupSlider (complexitySlider, 0.0, 1.0, 0.01, *this);
    setupSlider (bpmSlider, 40.0, 240.0, 0.1, *this);
    setupSlider (startBarSlider, 1.0, 256.0, 1.0, *this);
    setupSlider (velocitySlider, 1.0, 127.0, 1.0, *this);
    complexitySlider.onValueChange = bpmSlider.onValueChange = startBarSlider.onValueChange = [this] { if (! updating) pushSettings(); };
    velocitySlider.onDragEnd = [this] { pianoRoll.setSelectedVelocity ((int) velocitySlider.getValue()); };
    velocitySlider.onValueChange = [this] {
        if (! updating && ! velocitySlider.isMouseButtonDown()) pianoRoll.setSelectedVelocity ((int) velocitySlider.getValue());
    };

    for (auto* t : { &hostTempoToggle, &syncToggle, &loopToggle })
    {
        t->onClick = [this] { if (! updating) pushSettings(); };
        addAndMakeVisible (t);
    }
    setupLabel (performerLabel, U ("Intérprete"), *this);
    for (int i = 0; i < kNumPerformers; ++i) performerBox.addItem (U (performerName ((PerformerProfile) i)), i + 1);
    engineBox.addItem (U ("Motor 2 (jerárquico)"), 2);
    engineBox.addItem (U ("Motor 1 (S0–S2)"), 1);
    for (auto* cb : { &performerBox, &engineBox }) { cb->onChange = [this] { if (! updating) pushSettings(); }; addAndMakeVisible (cb); }
    keepMotifsToggle.setTooltip (U ("Al regenerar, conserva el mismo material temático y varía el resto"));
    pitchBendToggle.setTooltip (U ("Exporta bends, vibrato y slides como Pitch Bend (rango ±12 fijado por RPN)"));
    for (auto* t : { &keepMotifsToggle, &pitchBendToggle }) { t->onClick = [this] { if (! updating) pushSettings(); }; addAndMakeVisible (t); }

    const auto factory = factoryPresets();
    for (size_t i = 0; i < factory.size(); ++i) presetBox.addItem (U (factory[i].name), (int) i + 1);
    presetBox.setTextWhenNothingSelected (U ("Presets de fábrica…"));
    presetBox.onChange = [this] {
        const int id = presetBox.getSelectedId();
        const auto f = factoryPresets();
        if (id < 1 || id > (int) f.size()) return;
        auto s = f[(size_t) id - 1].settings;
        const auto cur = proc.getSettings();
        // Un preset cambia el material musical, no la integración con el DAW.
        s.useHostTempo = cur.useHostTempo; s.syncToHost = cur.syncToHost; s.manualBpm = cur.manualBpm;
        s.startBar = cur.startBar; s.loop = cur.loop; s.proMode = cur.proMode; s.seed = cur.seed;
        proc.setSettings (s);
        refreshFromProcessor();
        status (U ("Preset cargado: ") + U (f[(size_t) id - 1].name) + U (". Pulsa GENERAR."));
    };
    rateBox.addItem ("WAV 44.1k", 44100);
    rateBox.addItem ("WAV 48k", 48000);
    fmtBox.addItem ("MIDI 1", 2);
    fmtBox.addItem ("MIDI 0", 1);
    for (auto* cb : { &rateBox, &fmtBox }) cb->onChange = [this] { if (! updating) pushSettings(); };
    savePresetButton.onClick = [this] { onSavePreset(); };
    loadPresetButton.onClick = [this] { onLoadPreset(); };
    transportDiagButton.setTooltip (U ("Guarda en Documentos/Solo AI Studio un registro de los últimos bloques: posición que declara el DAW, "
                                       "eventos y resincronizaciones. Útil para diagnosticar problemas de sincronía."));
    transportDiagButton.onClick = [this] {
        const auto f = defaultExportFolder().getChildFile ("diagnostico-transporte.txt");
        const bool ok = f.replaceWithText (proc.transportDiagnosticText());
        status (ok ? U ("Diagnóstico guardado: ") + f.getFullPathName() : U ("No se pudo guardar el diagnóstico"), ! ok);
    };
    zoomInButton.onClick = [this] { pianoRoll.zoomHorizontal (1.25f); };
    zoomOutButton.onClick = [this] { pianoRoll.zoomHorizontal (0.8f); };
    fitButton.onClick = [this] { pianoRoll.fitToContent(); };
    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &rateBox, &fmtBox, &savePresetButton, &loadPresetButton,
                                                             &zoomInButton, &zoomOutButton, &fitButton })
        addAndMakeVisible (c);

    // ---------------------------------------------------- Piano roll
    pianoRoll.onEdit = [this] (const Sequence& s) {
        proc.replaceCurrentSequence (s);
        pianoRoll.setSequence (proc.getCurrentSequence());
        status (U ("Nota editada"));
    };
    pianoRoll.onSelectionChanged = [this] {
        const int v = pianoRoll.getSelectedVelocity();
        const juce::ScopedValueSetter<bool> svs (updating, true);
        velocitySlider.setEnabled (v > 0);
        if (v > 0) velocitySlider.setValue (v, juce::dontSendNotification);
    };
    addAndMakeVisible (pianoRoll);

    // ---------------------------------------------------- Pestañas Pro
    // Los controles Pro de generación se mueven a su pestaña.
    for (auto* c : std::initializer_list<juce::Component*> {
             &seedLabel, &seedEditor, &lockSeedToggle, &complexityLabel, &complexitySlider, &bpmLabel, &bpmSlider, &hostTempoToggle,
             &syncToggle, &startBarLabel, &startBarSlider, &loopToggle, &velocityLabel, &velocitySlider, &presetLabel, &presetBox,
             &savePresetButton, &loadPresetButton, &rateBox, &fmtBox, &performerLabel, &performerBox, &engineBox, &keepMotifsToggle,
             &pitchBendToggle, &transportDiagButton })
        genTab.addAndMakeVisible (c);
    genTab.layout = [this] { layoutGeneration(); };
    notesPanel = std::make_unique<NotesPanel> (panelCtx);
    fxPanel = std::make_unique<FxPanel> (panelCtx);
    tonePanel = std::make_unique<TonePanel> (panelCtx);
    autoPanel = std::make_unique<AutomationPanel> (panelCtx);
    voicePanel = std::make_unique<VoicePanel> (panelCtx);
    historyPanel = std::make_unique<HistoryPanel> (panelCtx);
    commandPanel = std::make_unique<CommandPanel> (panelCtx);
    const auto tabBg = juce::Colour (0xff181b21);
    tabs.setTabBarDepth (26);
    tabs.setOutline (0);
    tabs.addTab (U ("Generación"), tabBg, &genTab, false);
    tabs.addTab (U ("Notas"), tabBg, notesPanel.get(), false);
    tabs.addTab (U ("Efectos"), tabBg, fxPanel.get(), false);
    tabs.addTab (U ("Ampli / Samples"), tabBg, tonePanel.get(), false);
    tabs.addTab (U ("Automatización"), tabBg, autoPanel.get(), false);
    tabs.addTab (U ("Voz / ADN"), tabBg, voicePanel.get(), false);
    tabs.addTab (U ("Historial A/B"), tabBg, historyPanel.get(), false);
    tabs.addTab (U ("Comandos"), tabBg, commandPanel.get(), false);
    addChildComponent (tabs);
    tabs.getTabbedButtonBar().addChangeListener (this);

    proc.addChangeListener (this);
    constrainer.setMinimumSize (1100, 720);
    constrainer.setMaximumSize (2000, 1400);
    setConstrainer (&constrainer);
    setResizable (true, true);
    setSize (1180, 800);

    refreshFromProcessor();
    pianoRoll.onSelectionChanged();
    startTimerHz (30);
}

SoloAIStudioEditor::~SoloAIStudioEditor()
{
    proc.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

// ------------------------------------------------------------------ sincronización UI <-> procesador
void SoloAIStudioEditor::refreshFromProcessor()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    const auto s = proc.getSettings();
    keyEditor.setText (U (s.keyText.c_str()), false);
    progEditor.setText (U (s.progressionText.c_str()), false);
    instrumentBox.setSelectedId ((int) s.instrument + 1, juce::dontSendNotification);
    styleBox.setSelectedId ((int) s.style + 1, juce::dontSendNotification);
    if (barsBox.indexOfItemId (s.bars) < 0) barsBox.addItem (juce::String (s.bars) + U (" compases"), s.bars);
    barsBox.setSelectedId (s.bars, juce::dontSendNotification);
    tsBox.setSelectedId (s.tsNumerator, juce::dontSendNotification);
    energySlider.setValue (s.energy, juce::dontSendNotification);
    densitySlider.setValue (s.density, juce::dontSendNotification);
    complexitySlider.setValue (s.complexity, juce::dontSendNotification);
    seedEditor.setText (juce::String ((juce::uint64) s.seed), false);
    bpmSlider.setValue (s.manualBpm, juce::dontSendNotification);
    startBarSlider.setValue (s.startBar, juce::dontSendNotification);
    hostTempoToggle.setToggleState (s.useHostTempo, juce::dontSendNotification);
    syncToggle.setToggleState (s.syncToHost, juce::dontSendNotification);
    loopToggle.setToggleState (s.loop, juce::dontSendNotification);
    rateBox.setSelectedId (s.wavSampleRate == 44100 ? 44100 : 48000, juce::dontSendNotification);
    fmtBox.setSelectedId (s.midiFormat == 0 ? 1 : 2, juce::dontSendNotification);
    performerBox.setSelectedId ((int) s.performer + 1, juce::dontSendNotification);
    engineBox.setSelectedId (s.engine == 1 ? 1 : 2, juce::dontSendNotification);
    keepMotifsToggle.setToggleState (s.keepMotifs, juce::dontSendNotification);
    pitchBendToggle.setToggleState (s.midiPitchBend, juce::dontSendNotification);
    {
        ToneMacros m;
        proc.read ([&] (const ProjectController& pc) { m = pc.state.macros; });
        forceKnob.setValue (m.force, juce::dontSendNotification);
        expressionKnob.setValue (m.expression, juce::dontSendNotification);
        ambienceKnob.setValue (m.ambience, juce::dontSendNotification);
        const int t = s.fxEnabled ? tonePresetIndex (s.fxPreset) : -1;
        if (t >= 0) toneBox.setSelectedId (t + 1, juce::dontSendNotification);
        else toneBox.setSelectedId (0, juce::dontSendNotification);
        guitarBox.setSelectedId (juce::jlimit (0, kNumGuitarEngines - 1, s.guitarEngine) + 1, juce::dontSendNotification);
        const bool guitar = s.instrument == InstrumentId::Guitar || s.instrument == InstrumentId::GuitarLite;
        guitarBox.setEnabled (guitar);
    }
    setProMode (s.proMode);
    if (s.proTab >= 0 && s.proTab < tabs.getNumTabs() && tabs.getCurrentTabIndex() != s.proTab) tabs.setCurrentTabIndex (s.proTab, false);
    validateTextFields();
    refreshSequence();
}

void SoloAIStudioEditor::refreshSequence()
{
    pianoRoll.setSequence (proc.getCurrentSequence());
    const int n = proc.variationCount();
    variationLabel.setText (n == 0 ? U ("Sin variaciones")
                                   : U ("Variación ") + juce::String (proc.variationIndex() + 1) + " / " + juce::String (n),
                            juce::dontSendNotification);
    prevButton.setEnabled (proc.variationIndex() > 0);
    nextButton.setEnabled (proc.variationIndex() + 1 < n);
    bool fav = false;
    std::vector<bool> locked;
    juce::String undoTip, redoTip;
    proc.read ([&] (const ProjectController& pc) {
        fav = pc.hasSequence() && pc.state.history.currentItem().favorite;
        locked = pc.lockedBarsFor (pc.current() ? pc.current()->bars : pc.state.settings.bars);
        undoTip = utf8 (pc.undo.undoLabel());
        redoTip = utf8 (pc.undo.redoLabel());
    });
    favButton.setButtonText (fav ? U ("★") : U ("☆"));
    favButton.setToggleState (fav, juce::dontSendNotification);
    favButton.setEnabled (n > 0);
    pianoRoll.setLockedBars (locked);
    undoButton.setEnabled (proc.canUndo());
    redoButton.setEnabled (proc.canRedo());
    undoButton.setTooltip (U ("Deshacer (Ctrl+Z)") + (undoTip.isNotEmpty() ? ": " + undoTip : juce::String()));
    redoButton.setTooltip (U ("Rehacer (Ctrl+Shift+Z)") + (redoTip.isNotEmpty() ? ": " + redoTip : juce::String()));
    refreshPanels();
}

void SoloAIStudioEditor::refreshPanels()
{
    for (ProPanel* p : std::initializer_list<ProPanel*> { notesPanel.get(), fxPanel.get(), tonePanel.get(), autoPanel.get(), voicePanel.get(),
                                                          historyPanel.get(), commandPanel.get() })
        if (p != nullptr) p->refresh();
}

void SoloAIStudioEditor::validateTextFields()
{
    Key k;
    const bool keyOk = Key::parse (keyEditor.getText().toStdString(), k);
    Progression prog;
    std::string err;
    const bool progOk = Progression::parse (progEditor.getText().toStdString(), prog, err);
    keyEditor.setColour (juce::TextEditor::outlineColourId, keyOk ? juce::Colour (0xff2e333d) : kError);
    progEditor.setColour (juce::TextEditor::outlineColourId, progOk ? juce::Colour (0xff2e333d) : kError);
    keyEditor.repaint();
    progEditor.repaint();
    if (progOk) pianoRoll.setProgression (prog, keyOk && k.prefersFlats());
    if (! keyOk) status (U ("Tonalidad no válida (ej.: Am, C, F#m)"), true);
    else if (! progOk) status (U (err.c_str()), true);
    else
    {
        const auto w = prog.validate (k);
        if (! w.empty()) status (U ("Aviso: ") + U (w.front().c_str()));
    }
}

void SoloAIStudioEditor::pushSettings()
{
    auto s = proc.getSettings();
    s.keyText = keyEditor.getText().toStdString();
    s.progressionText = progEditor.getText().toStdString();
    s.instrument = (InstrumentId) juce::jlimit (0, kNumInstruments - 1, instrumentBox.getSelectedId() - 1);
    s.style = (Style) juce::jlimit (0, kNumStyles - 1, styleBox.getSelectedId() - 1);
    if (barsBox.getSelectedId() > 0) s.bars = barsBox.getSelectedId();
    if (tsBox.getSelectedId() > 0) s.tsNumerator = tsBox.getSelectedId();
    s.energy = energySlider.getValue();
    s.density = densitySlider.getValue();
    s.complexity = complexitySlider.getValue();
    s.seed = (uint64_t) seedEditor.getText().getLargeIntValue();
    s.manualBpm = bpmSlider.getValue();
    s.startBar = (int) startBarSlider.getValue();
    s.useHostTempo = hostTempoToggle.getToggleState();
    s.syncToHost = syncToggle.getToggleState();
    s.loop = loopToggle.getToggleState();
    if (rateBox.getSelectedId() > 0) s.wavSampleRate = rateBox.getSelectedId();
    if (fmtBox.getSelectedId() > 0) s.midiFormat = fmtBox.getSelectedId() == 1 ? 0 : 1;
    if (performerBox.getSelectedId() > 0) s.performer = (PerformerProfile) (performerBox.getSelectedId() - 1);
    if (engineBox.getSelectedId() > 0) s.engine = engineBox.getSelectedId();
    if (s.keepMotifs != keepMotifsToggle.getToggleState()) s.motifSeed = 0; // nuevo material temático en el próximo Generar
    s.keepMotifs = keepMotifsToggle.getToggleState();
    s.midiPitchBend = pitchBendToggle.getToggleState();
    proc.setSettings (s);
}

void SoloAIStudioEditor::setProMode (bool pro)
{
    easyButton.setToggleState (! pro, juce::dontSendNotification);
    proButton.setToggleState (pro, juce::dontSendNotification);
    tabs.setVisible (pro);
    if (! updating)
    {
        auto s = proc.getSettings();
        s.proMode = pro;
        proc.setSettings (s);
    }
    resized();
    repaint();
}

void SoloAIStudioEditor::status (const juce::String& s, bool error)
{
    statusLabel.setText (s, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, error ? kError : kDim);
}

// ------------------------------------------------------------------ acciones
void SoloAIStudioEditor::onGenerate()
{
    pushSettings();
    if (! lockSeed)
    {
        auto s = proc.getSettings();
        s.seed = (uint64_t) (juce::Random::getSystemRandom().nextInt64() & 0x7FFFFFFFFFFFll);
        proc.setSettings (s);
        const juce::ScopedValueSetter<bool> svs (updating, true);
        seedEditor.setText (juce::String ((juce::uint64) s.seed), false);
    }
    juce::String msg;
    const bool ok = proc.generateNew (msg);
    status (msg, ! ok);
    if (ok) refreshSequence();
}

juce::File SoloAIStudioEditor::defaultExportFolder() const
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Solo AI Studio");
    d.createDirectory();
    return d;
}

void SoloAIStudioEditor::onExportMidi()
{
    if (! proc.hasSequence()) { status (U ("Primero genera un solo"), true); return; }
    const auto s = proc.getSettings();
    const auto name = juce::String ("Solo ") + juce::String (s.keyText) + " " + juce::String (juce::roundToInt (proc.effectiveBpm()))
                      + "bpm v" + juce::String (proc.variationIndex() + 1) + ".mid";
    chooser = std::make_unique<juce::FileChooser> ("Exportar MIDI", defaultExportFolder().getChildFile (name), "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (! f.hasFileExtension ("mid")) f = f.withFileExtension ("mid");
                              juce::String msg;
                              const bool ok = proc.exportMidi (f, msg);
                              status (msg, ! ok);
                          });
}

void SoloAIStudioEditor::onExportWav()
{
    if (! proc.hasSequence()) { status (U ("Primero genera un solo"), true); return; }
    const auto s = proc.getSettings();
    const auto name = juce::String ("Solo ") + juce::String (s.keyText) + " " + juce::String (juce::roundToInt (proc.effectiveBpm()))
                      + "bpm v" + juce::String (proc.variationIndex() + 1) + ".wav";
    chooser = std::make_unique<juce::FileChooser> ("Exportar WAV", defaultExportFolder().getChildFile (name), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              if (! f.hasFileExtension ("wav")) f = f.withFileExtension ("wav");
                              juce::String msg;
                              const bool ok = proc.exportWav (f, msg);
                              status (msg, ! ok);
                          });
}

void SoloAIStudioEditor::onSavePreset()
{
    pushSettings();
    chooser = std::make_unique<juce::FileChooser> ("Guardar preset", defaultExportFolder().getChildFile ("Mi preset.solopreset"),
                                                   "*.solopreset");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              auto f = fc.getResult();
                              if (f == juce::File()) return;
                              f = f.withFileExtension ("solopreset");
                              const auto txt = serializePreset (proc.getSettings(), f.getFileNameWithoutExtension().toStdString());
                              const bool ok = f.replaceWithText (U (txt.c_str()));
                              status (ok ? U ("Preset guardado: ") + f.getFileName() : U ("No se pudo guardar el preset"), ! ok);
                          });
}

void SoloAIStudioEditor::onLoadPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Cargar preset", defaultExportFolder(), "*.solopreset");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              if (! f.existsAsFile()) return;
                              SoloSettings loaded;
                              std::string name, err;
                              if (! deserializePreset (f.loadFileAsString().toStdString(), loaded, name, err))
                              {
                                  status (U ("Preset no válido: ") + U (err.c_str()), true);
                                  return;
                              }
                              const auto cur = proc.getSettings();
                              loaded.useHostTempo = cur.useHostTempo; loaded.syncToHost = cur.syncToHost;
                              loaded.startBar = cur.startBar; loaded.proMode = cur.proMode;
                              proc.setSettings (loaded);
                              refreshFromProcessor();
                              status (U ("Preset cargado: ") + U (name.c_str()));
                          });
}

// ------------------------------------------------------------------ timer / cambios
void SoloAIStudioEditor::timerCallback()
{
    const auto h = proc.getHostInfo();
    const auto s = proc.getSettings();
    juce::String t;
    if (h.valid)
        t << "DAW: " << juce::String (h.bpm, 1) << " BPM " << h.tsNum << "/" << h.tsDen << (h.playing ? U ("  ▶") : U ("  ■"))
          << (h.hasSampleTime ? "" : U (" (sin pos. en muestras)")) << U ("  resync ") << (int) h.resyncs;
    else
        t << U ("DAW: sin tempo");
    t << U ("   ·   Solo a ") << juce::String (proc.effectiveBpm(), 1) << " BPM";
    if (h.valid && h.tsNum != s.tsNumerator) t << U ("  ⚠ compás distinto");
    hostLabel.setText (t, juce::dontSendNotification);

    playButton.setButtonText (proc.isPreviewing() ? "DETENER" : "ESCUCHAR");
    playButton.setToggleState (proc.isPreviewing(), juce::dontSendNotification);
    bpmSlider.setEnabled (! s.useHostTempo || ! h.valid);
    pianoRoll.setPlayheadTick (proc.playheadTick());

    const auto st = proc.getLastStatus();
    if (st != lastStatusShown) { lastStatusShown = st; status (st); }
}

void SoloAIStudioEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &tabs.getTabbedButtonBar())
    {
        // Recordar la pestaña Pro abierta (se guarda con la sesión).
        if (updating) return;
        auto s = proc.getSettings();
        s.proTab = tabs.getCurrentTabIndex();
        proc.setSettings (s);
        return;
    }
    refreshFromProcessor();
}

// ------------------------------------------------------------------ layout
void SoloAIStudioEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kPanel);
    g.fillRect (0, 0, getWidth(), kHeader);
    g.fillRect (0, kHeader, kLeft, getHeight() - kHeader - kStatus);
    if (proButton.getToggleState()) g.fillRect (kLeft, kHeader, getWidth() - kLeft, kPro);
    g.setColour (juce::Colour (0xff262a33));
    g.drawHorizontalLine (kHeader, 0.0f, (float) getWidth());
    g.drawVerticalLine (kLeft, (float) kHeader, (float) (getHeight() - kStatus));
    g.drawHorizontalLine (getHeight() - kStatus, 0.0f, (float) getWidth());
}

void SoloAIStudioEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (kHeader).reduced (12, 8);
    title.setBounds (header.removeFromLeft (170));
    easyButton.setBounds (header.removeFromLeft (64));
    header.removeFromLeft (4);
    proButton.setBounds (header.removeFromLeft (64));
    header.removeFromLeft (16);
    undoButton.setBounds (header.removeFromLeft (80));
    header.removeFromLeft (4);
    redoButton.setBounds (header.removeFromLeft (76));
    hostLabel.setBounds (header);
    statusLabel.setBounds (r.removeFromBottom (kStatus).reduced (10, 0));

    // ---- panel izquierdo
    auto left = r.removeFromLeft (kLeft).reduced (12, 10);
    auto row = [&] (juce::Label& l, juce::Component& c, int h = 26) {
        auto rr = left.removeFromTop (h);
        l.setBounds (rr.removeFromLeft (84));
        c.setBounds (rr);
        left.removeFromTop (7);
    };
    row (keyLabel, keyEditor);
    row (progLabel, progEditor);
    row (instLabel, instrumentBox);
    row (styleLabel, styleBox);
    row (barsLabel, barsBox);
    row (tsLabel, tsBox);
    row (energyLabel, energySlider);
    row (densityLabel, densitySlider);
    row (toneLabel, toneBox);
    row (guitarLabel, guitarBox);
    {
        auto k = left.removeFromTop (78);
        const int w = k.getWidth() / 3;
        auto one = [&] (juce::Label& l, juce::Slider& s) { auto c = k.removeFromLeft (w); l.setBounds (c.removeFromTop (16)); s.setBounds (c); };
        one (forceLabel, forceKnob); one (expressionLabel, expressionKnob); one (ambienceLabel, ambienceKnob);
        left.removeFromTop (6);
    }
    left.removeFromTop (4);
    generateButton.setBounds (left.removeFromTop (44));
    left.removeFromTop (8);
    playButton.setBounds (left.removeFromTop (32));
    left.removeFromTop (8);
    voiceButton.setBounds (left.removeFromTop (30));
    left.removeFromTop (8);
    {
        auto v = left.removeFromTop (30);
        prevButton.setBounds (v.removeFromLeft (36));
        v.removeFromLeft (4);
        favButton.setBounds (v.removeFromRight (36));
        v.removeFromRight (4);
        nextButton.setBounds (v.removeFromRight (36));
        variationLabel.setBounds (v);
    }
    left.removeFromTop (8);
    {
        auto e = left.removeFromTop (30);
        exportMidiButton.setBounds (e.removeFromLeft (e.getWidth() / 2 - 3));
        e.removeFromLeft (6);
        exportWavButton.setBounds (e);
    }
    left.removeFromTop (8);
    dragArea.setBounds (left.removeFromTop (34));

    // ---- panel Pro (pestañas)
    if (proButton.getToggleState()) tabs.setBounds (r.removeFromTop (kPro).reduced (6, 4));

    // ---- piano roll
    auto roll = r.reduced (8);
    auto tools = roll.removeFromTop (26);
    fitButton.setBounds (tools.removeFromRight (64));
    tools.removeFromRight (4);
    zoomInButton.setBounds (tools.removeFromRight (28));
    tools.removeFromRight (4);
    zoomOutButton.setBounds (tools.removeFromRight (28));
    roll.removeFromTop (4);
    pianoRoll.setBounds (roll);
}

void SoloAIStudioEditor::layoutGeneration()
{
    auto pro = genTab.getLocalBounds().reduced (8, 6);
    auto r1 = pro.removeFromTop (32).reduced (0, 3);
    auto r2 = pro.removeFromTop (32).reduced (0, 3);
    auto r3 = pro.removeFromTop (32).reduced (0, 3);
    auto r4 = pro.removeFromTop (32).reduced (0, 3);
    auto place = [] (juce::Rectangle<int>& rr, juce::Component& c, int w) {
        c.setBounds (rr.removeFromLeft (juce::jmin (w, rr.getWidth())));
        rr.removeFromLeft (6);
    };
    place (r1, seedLabel, 52); place (r1, seedEditor, 130); place (r1, lockSeedToggle, 112);
    place (r1, complexityLabel, 82); place (r1, complexitySlider, 170);
    place (r1, velocityLabel, 86); place (r1, velocitySlider, r1.getWidth());
    place (r2, bpmLabel, 76); place (r2, bpmSlider, 170); place (r2, hostTempoToggle, 112);
    place (r2, syncToggle, 140); place (r2, startBarLabel, 116); place (r2, startBarSlider, 150); place (r2, loopToggle, 62);
    place (r3, performerLabel, 70); place (r3, performerBox, 150); place (r3, engineBox, 170);
    place (r3, keepMotifsToggle, 140); place (r3, pitchBendToggle, 170);
    place (r4, presetLabel, 46);
    place (r4, presetBox, 230);
    place (r4, savePresetButton, 66); place (r4, loadPresetButton, 62);
    place (r4, rateBox, 100); place (r4, fmtBox, 100); place (r4, transportDiagButton, 160);
}

bool SoloAIStudioEditor::keyPressed (const juce::KeyPress& k)
{
    // Muchos DAW se quedan estos atajos; los botones Deshacer/Rehacer funcionan siempre.
    if (k.getModifiers().isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
    {
        const bool redo = k.getModifiers().isShiftDown();
        proc.perform ([redo] (ProjectController& pc) { return redo ? pc.redoLast() : pc.undoLast(); });
        refreshFromProcessor();
        return true;
    }
    return false;
}

// ------------------------------------------------------------------ Guitar Tone Studio v3: macros
void SoloAIStudioEditor::commitMacros (bool remember)
{
    ToneMacros m;
    m.force = forceKnob.getValue();
    m.expression = expressionKnob.getValue();
    m.ambience = ambienceKnob.getValue();
    const auto r = proc.perform ([m, remember] (ProjectController& pc) { return pc.setMacros (m, remember); });
    if (remember)
    {
        status (utf8 (r.message), ! r.ok);
        refreshFromProcessor(); // el preset pasa a "Personalizado" si cambió respecto al de fábrica; Pro se sincroniza
    }
    else refreshPanels();       // mientras se arrastra: solo las pestañas Pro (valores DSP en directo)
}
