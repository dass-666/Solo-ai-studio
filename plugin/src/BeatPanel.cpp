#include "BeatPanel.h"
#include "solo/ChorusComposer.h"

using namespace solo;

namespace
{
juce::String U (const char* s) { return juce::String::fromUTF8 (s); }
juce::String U (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
const juce::Colour kText (0xffd7dce5), kDim (0xff8a93a3), kAccent (0xff2ec4b6), kGold (0xffffc857), kError (0xffff6b6b);

void label (juce::Label& l, const juce::String& t, juce::Component& parent)
{
    l.setText (t, juce::dontSendNotification);
    l.setColour (juce::Label::textColourId, kDim);
    l.setFont (juce::FontOptions (12.5f));
    parent.addAndMakeVisible (l);
}
struct Row
{
    juce::Rectangle<int> r;
    void add (juce::Component& c, int w) { c.setBounds (r.removeFromLeft (juce::jmin (w, r.getWidth()))); r.removeFromLeft (6); }
    void rest (juce::Component& c) { c.setBounds (r); }
};
Row row (juce::Rectangle<int>& area, int h = 26)
{
    auto r = area.removeFromTop (h);
    area.removeFromTop (6);
    return { r };
}
juce::String confText (double c) { return c >= 0.7 ? U ("alta") : c >= 0.4 ? U ("media") : U ("baja"); }
juce::Colour confColour (double c) { return c >= 0.7 ? kAccent : c >= 0.4 ? kGold : kError; }
} // namespace

// ============================================================ vista del beat
void BeatView::paint (juce::Graphics& g)
{
    auto a = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff15171c));
    g.fillRoundedRectangle (a, 4.0f);
    if (! view.loaded || view.bars <= 0)
    {
        g.setColour (kDim);
        g.setFont (12.5f);
        g.drawFittedText (U ("Sin beat. Importa un WAV del beat: se analizan tempo, tonalidad, acordes y energía (estimados, con confianza).\n"
                             "Sin beat, el compositor usa la tonalidad y los acordes escritos a la izquierda."),
                          getLocalBounds().reduced (8), juce::Justification::centredLeft, 3);
        return;
    }
    const int n = view.bars;
    const float w = (a.getWidth() - 8.0f) / (float) n;
    const float x0 = a.getX() + 4.0f;
    const float top = a.getY() + 4.0f, bottom = a.getBottom() - 4.0f;
    const float energyH = juce::jmin (26.0f, (bottom - top) * 0.45f);
    const int secFrom = view.sectionFrom, secTo = view.sectionTo < 0 ? n - 1 : view.sectionTo;
    for (int b = 0; b < n; ++b)
    {
        const float x = x0 + b * w;
        const bool inSection = b >= secFrom && b <= secTo;
        // energía: barra proporcional al nivel (baja/media/alta)
        const int lvl = b < (int) view.energyPerBar.size() ? view.energyPerBar[(size_t) b] : 1;
        const float h = energyH * (0.35f + 0.325f * (float) lvl);
        g.setColour ((inSection ? kAccent : kDim).withAlpha (0.25f + 0.2f * (float) lvl));
        g.fillRect (x + 1.0f, bottom - h, juce::jmax (1.0f, w - 2.0f), h);
        // acorde
        if (b < (int) view.chordPerBar.size())
        {
            const double cc = b < (int) view.chordConfPerBar.size() ? view.chordConfPerBar[(size_t) b] : 0.5;
            g.setColour (inSection ? kText : kDim);
            g.setFont (juce::FontOptions (w > 34 ? 12.5f : 10.0f, juce::Font::bold));
            g.drawFittedText (view.chordPerBar[(size_t) b], juce::Rectangle<float> (x, top, w, 16.0f).toNearestInt(), juce::Justification::centred, 1);
            g.setColour (confColour (cc).withAlpha (0.8f));
            g.fillRect (x + 2.0f, top + 17.0f, (w - 4.0f) * (float) juce::jlimit (0.1, 1.0, cc), 2.0f);
        }
        else
        {
            g.setColour (kDim.withAlpha (0.6f));
            g.setFont (10.0f);
            g.drawFittedText (juce::String (b + 1), juce::Rectangle<float> (x, top, w, 14.0f).toNearestInt(), juce::Justification::centred, 1);
        }
        if (b % 4 == 0) { g.setColour (kDim.withAlpha (0.35f)); g.drawVerticalLine ((int) x, top, bottom); }
    }
    // sección elegida
    g.setColour (kAccent.withAlpha (0.9f));
    g.drawRect (juce::Rectangle<float> (x0 + secFrom * w, top - 2.0f, (secTo - secFrom + 1) * w, bottom - top + 4.0f), 1.0f);
}

void BeatView::mouseDown (const juce::MouseEvent& e)
{
    if (! view.loaded || view.bars <= 0 || ! onBarClick) return;
    const float w = (getWidth() - 8.0f) / (float) view.bars;
    const int bar = juce::jlimit (0, view.bars - 1, (int) ((e.x - 4) / w));
    onBarClick (e.mods.isShiftDown() ? -(bar + 1) : bar); // negativo = fin de sección
}

// ============================================================ franja (modo fácil)
BeatStrip::BeatStrip (PanelContext& c) : ProPanel (c)
{
    addAndMakeVisible (importButton);
    addAndMakeVisible (clearButton);
    importButton.onClick = [this] { importBeat(); };
    clearButton.onClick = [this] { run ([] (ProjectController& p) { return p.clearBeat(); }); };
    label (analysisLabel, "", *this);
    analysisLabel.setColour (juce::Label::textColourId, kText);
    label (typeLabel, U ("Tipo"), *this);
    label (characterLabel, U ("Carácter"), *this);
    label (freedomLabel, U ("Libertad"), *this);
    label (familyLabel, "", *this);
    for (int i = 0; i < kNumMelodyTypes; ++i) typeBox.addItem (U (melodyTypeName ((MelodyType) i)), i + 1);
    for (int i = 0; i < 3; ++i) characterBox.addItem (U (chorusCharacterName ((ChorusCharacter) i)), i + 1);
    typeBox.setSelectedId (1, juce::dontSendNotification);
    characterBox.setSelectedId (1, juce::dontSendNotification);
    typeBox.onChange = [this] { characterBox.setEnabled (typeBox.getSelectedId() == 1); };
    addAndMakeVisible (typeBox);
    addAndMakeVisible (characterBox);
    freedomSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    freedomSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 18);
    freedomSlider.setRange (0.0, 100.0, 1.0);
    freedomSlider.setValue (35.0, juce::dontSendNotification);
    freedomSlider.setTextValueSuffix (" %");
    freedomSlider.setTooltip (U ("0-25 variaciones cercanas · 26-55 conserva la identidad · 56-80 frases nuevas con elementos del motivo · 81-100 experimentación con restricciones"));
    freedomSlider.onValueChange = [this] { if (! updating) ctx.proc.perform ([this] (ProjectController& p) { return p.setMelodyFreedom (freedomSlider.getValue()); }); };
    addAndMakeVisible (freedomSlider);
    generateButton.setColour (juce::TextButton::buttonColourId, kAccent.darker (0.3f));
    generateButton.onClick = [this] { generateProposals(); };
    varyButton.onClick = [this] { varyIdea(); };
    rhythmButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> (U ("Proponer ritmo: tarareo (WAV)"), juce::File(), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            juce::String msg;
            const bool ok = ctx.proc.proposeRhythmFromFile (f, msg);
            if (ok) { juce::String m2; ok ? ctx.proc.perform ([] (ProjectController& p) { return p.applyRhythmPattern (SoloAIStudioProcessor::freshSeed()); }) : OpResult {}; }
            ctx.status (msg + (ok ? U (" · notas puestas por la armonía (patrón editable en Pro > Beat)") : juce::String()), ! ok);
            ctx.refreshAll();
        });
    };
    melodyButton.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> (U ("Proponer melodía: voz o tarareo (WAV)"), juce::File(), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            juce::String msg;
            const bool ok = ctx.proc.proposeMelodyFromFile (f, msg);
            ctx.status (msg, ! ok);
            ctx.refreshAll();
        });
    };
    for (auto* b : { &generateButton, &varyButton, &rhythmButton, &melodyButton }) addAndMakeVisible (b);
    beatView.onBarClick = [this] (int bar) {
        run ([bar] (ProjectController& p) {
            const auto& b = p.state.beat;
            if (bar >= 0) return p.setBeatSection (bar, b.sectionToBar >= 0 && b.sectionToBar < bar ? -1 : b.sectionToBar);
            const int end = -bar - 1;
            return p.setBeatSection (std::min (b.sectionFromBar, end), end);
        });
        ctx.proc.perform ([] (ProjectController& p) { return p.applyBeatToSettings(); });
        ctx.refreshAll();
    };
    addAndMakeVisible (beatView);
}

MelodyBrief BeatStrip::briefFromControls() const
{
    MelodyBrief b;
    ctx.proc.read ([&] (const ProjectController& p) { b = p.defaultBrief(); });
    b.type = (MelodyType) juce::jlimit (0, kNumMelodyTypes - 1, typeBox.getSelectedId() - 1);
    b.character = (ChorusCharacter) juce::jlimit (0, 2, characterBox.getSelectedId() - 1);
    b.freedom = freedomSlider.getValue();
    b.seed = SoloAIStudioProcessor::freshSeed();
    return b;
}

void BeatStrip::importBeat()
{
    chooser = std::make_unique<juce::FileChooser> (U ("Importar beat (WAV)"), juce::File(), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        juce::String msg;
        const bool ok = ctx.proc.importBeat (f, msg);
        ctx.status (msg.upToFirstOccurrenceOf ("\n", false, false), ! ok);
        ctx.refreshAll();
    });
}

void BeatStrip::generateProposals()
{
    const auto b = briefFromControls();
    run ([b] (ProjectController& p) { return p.generateProposals (b); });
}

void BeatStrip::varyIdea()
{
    const double f = freedomSlider.getValue();
    run ([f] (ProjectController& p) {
        p.setMelodyFreedom (f);
        return p.varyCurrent (3, SoloAIStudioProcessor::freshSeed());
    });
}

void BeatStrip::refresh()
{
    const auto v = ctx.proc.getBeatView();
    updating = true;
    beatView.setView (v);
    juce::String t;
    if (v.loaded)
    {
        t = v.summary;
        if (v.bpmConfirmed || v.keyConfirmed) t += U (" · confirmado: ") + (v.bpmConfirmed ? U ("BPM ") : juce::String()) + (v.keyConfirmed ? U ("tonalidad") : juce::String());
        if (! v.warnings.empty()) t += U (" · ") + v.warnings.front();
    }
    else t = U ("Sin beat importado (se usan los acordes de la izquierda)");
    analysisLabel.setText (t, juce::dontSendNotification);
    analysisLabel.setColour (juce::Label::textColourId, v.loaded && ! v.warnings.empty() ? kGold : kText);
    juce::String fam;
    if (v.melodyActive)
    {
        fam = U ("Familia: ") + v.identity + U (" · diversidad ") + juce::String ((int) std::lround (v.diversity * 100)) + U (" %");
        if (v.currentStrategy >= 0) fam += U (" · actual: ") + U (strategyName ((ProposalStrategy) v.currentStrategy));
    }
    else fam = U ("Pulsa GENERAR: cuatro propuestas con estrategias distintas (A sencilla, B emocional, C rítmica, D atrevida) sobre la misma idea.");
    familyLabel.setText (fam, juce::dontSendNotification);
    MelodyBrief b;
    ctx.proc.read ([&] (const ProjectController& p) { b = p.state.melody.brief; });
    if (v.melodyActive)
    {
        typeBox.setSelectedId ((int) b.type + 1, juce::dontSendNotification);
        characterBox.setSelectedId ((int) b.character + 1, juce::dontSendNotification);
        freedomSlider.setValue (b.freedom, juce::dontSendNotification);
    }
    characterBox.setEnabled (typeBox.getSelectedId() == 1);
    varyButton.setEnabled (v.melodyActive && ctx.proc.hasSequence());
    clearButton.setEnabled (v.loaded);
    updating = false;
}

void BeatStrip::resized()
{
    auto a = getLocalBounds().reduced (6, 4);
    auto r1 = row (a, 26);
    r1.add (importButton, 176); r1.add (clearButton, 60); r1.rest (analysisLabel);
    auto mid = a.removeFromTop (juce::jmax (40, a.getHeight() - 70));
    beatView.setBounds (mid);
    a.removeFromTop (6);
    auto r2 = row (a, 26);
    r2.add (typeLabel, 32); r2.add (typeBox, 112); r2.add (characterLabel, 52); r2.add (characterBox, 120);
    r2.add (freedomLabel, 50); r2.add (freedomSlider, 150); r2.add (generateButton, 182); r2.rest (varyButton);
    auto r3 = row (a, 24);
    r3.add (rhythmButton, 152); r3.add (melodyButton, 168); r3.rest (familyLabel);
}

// ============================================================ pestaña Pro
BeatProPanel::BeatProPanel (PanelContext& c) : ProPanel (c)
{
    label (bpmL, "BPM", *this); label (keyL, "Tonalidad", *this); label (progL, "Acordes", *this);
    label (sectionL, U ("Sección (compases)"), *this); label (infoL, "", *this); label (rhythmL, U ("Patrón rítmico (x ataque, - sostiene, . silencio, X acento; una línea por compás)"), *this);
    label (familyL, "", *this);
    for (auto* e : { &bpmEditor, &keyEditor, &progEditor })
    {
        e->setFont (juce::FontOptions (13.0f)); e->setIndents (6, 4);
        addAndMakeVisible (e);
    }
    rhythmEditor.setMultiLine (true, false);
    rhythmEditor.setReturnKeyStartsNewLine (true);
    rhythmEditor.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    addAndMakeVisible (rhythmEditor);
    bpmButton.onClick = [this] { const double v = bpmEditor.getText().getDoubleValue(); run ([v] (ProjectController& p) { auto r = p.confirmBeatBpm (v); if (r.ok) p.applyBeatToSettings(); return r; }); };
    keyButton.onClick = [this] {
        Key k; const bool ok = Key::parse (keyEditor.getText().toStdString(), k);
        if (! ok) { ctx.status (U ("Tonalidad no reconocida (ej.: Am, C, F#m)"), true); return; }
        run ([k] (ProjectController& p) { auto r = p.confirmBeatKey (k); if (r.ok) p.applyBeatToSettings(); return r; });
    };
    progButton.onClick = [this] {
        Progression pr; std::string err;
        if (! Progression::parse (progEditor.getText().toStdString(), pr, err)) { ctx.status (U ("Acordes no válidos: ") + U (err), true); return; }
        run ([pr] (ProjectController& p) { auto r = p.setBeatChords (pr); if (r.ok) p.applyBeatToSettings(); return r; });
    };
    useButton.onClick = [this] { run ([] (ProjectController& p) { return p.applyBeatToSettings(); }); };
    for (auto* s : { &sectionFrom, &sectionTo })
    {
        s->setSliderStyle (juce::Slider::IncDecButtons);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 20);
        s->setRange (1, 64, 1);
        addAndMakeVisible (s);
    }
    sectionButton.onClick = [this] {
        const int a = (int) sectionFrom.getValue() - 1, b = (int) sectionTo.getValue() - 1;
        run ([a, b] (ProjectController& p) { auto r = p.setBeatSection (a, b); if (r.ok) p.applyBeatToSettings(); return r; });
    };
    rhythmFile.onClick = [this] {
        chooser = std::make_unique<juce::FileChooser> (U ("Proponer ritmo: tarareo (WAV)"), juce::File(), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            juce::String msg;
            const bool ok = ctx.proc.proposeRhythmFromFile (f, msg);
            ctx.status (msg.upToFirstOccurrenceOf ("\n", false, false), ! ok);
            ctx.refreshAll();
        });
    };
    rhythmApply.onClick = [this] {
        const auto text = rhythmEditor.getText().toStdString();
        run ([text] (ProjectController& p) { auto r = p.setRhythmPatternText (text); if (! r.ok) return r; return p.applyRhythmPattern (SoloAIStudioProcessor::freshSeed()); });
    };
    correctButton.onClick = [this] {
        int a, b;
        if (! rangeOrWarn (a, b)) return;
        chooser = std::make_unique<juce::FileChooser> (U ("Corregir compases tarareando (WAV)"), juce::File(), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, a, b] (const juce::FileChooser& fc) {
            const auto f = fc.getResult();
            if (! f.existsAsFile()) return;
            juce::String msg;
            const bool ok = ctx.proc.correctRegionFromFile (f, a, b, msg);
            ctx.status (msg, ! ok);
            ctx.refreshAll();
        });
    };
    regionButton.onClick = [this] { int a, b; if (! rangeOrWarn (a, b)) return; run ([a, b] (ProjectController& p) { return p.regenerateMelodyRegion (a, b, SoloAIStudioProcessor::freshSeed()); }); };
    for (auto* b : { &bpmButton, &keyButton, &progButton, &useButton, &sectionButton, &rhythmFile, &rhythmApply, &correctButton, &regionButton }) addAndMakeVisible (b);
}

void BeatProPanel::refresh()
{
    const auto v = ctx.proc.getBeatView();
    updating = true;
    juce::String info, prog, rhythm, fam;
    ctx.proc.read ([&] (const ProjectController& p) {
        const auto& b = p.state.beat;
        if (b.loaded())
        {
            prog = U (b.harmony.segments.empty() ? std::string() : b.harmony.toProgression().toString (b.harmony.key.prefersFlats()));
            info = U ("BPM ") + juce::String (b.harmony.bpm, 1) + U (" (") + (v.bpmConfirmed ? U ("confirmado") : U ("estimado, confianza ") + confText (v.bpmConfidence)) + U (") · ")
                 + U ("tonalidad ") + v.key + U (" (") + (v.keyConfirmed ? U ("confirmada") : (b.analysis.tonal ? U ("estimada, confianza ") + confText (v.keyConfidence) : U ("sin contenido tonal"))) + U (") · ")
                 + U ("acordes ") + (b.harmony.segments.empty() ? U ("sin detectar: escríbelos") : U ("confianza media ") + confText (v.chordConfidence)) + U (" · ")
                 + juce::String (v.bars) + U (" compases");
            if (! b.analysis.bpmCandidates.empty() && b.analysis.bpmCandidates.size() > 1)
                info += U (" · otros BPM: ") + juce::String (b.analysis.bpmCandidates[1].bpm, 1);
            for (const auto& w : b.analysis.warnings) info += U ("\n· ") + U (w);
        }
        else info = U ("Sin beat importado. Importa un WAV en la franja superior; aquí podrás confirmar BPM/tonalidad, escribir los acordes y elegir la sección.");
        if (! p.state.rhythm.empty()) rhythm = U (p.state.rhythm.toText());
        if (p.state.melody.active)
            fam = U ("Familia: ") + U (p.state.melody.identity.description) + U (" · libertad ") + juce::String ((int) std::lround (p.state.melody.brief.freedom)) + U (" % (")
                + U (freedomBandName (MelodyExplorer::freedomBand (p.state.melody.brief.freedom))) + U (")");
        else fam = U ("Sin familia todavía: pulsa GENERAR (4 propuestas) en la franja del beat.");
    });
    if (! bpmEditor.hasKeyboardFocus (true)) bpmEditor.setText (v.loaded ? juce::String (v.bpm, 1) : juce::String(), false);
    if (! keyEditor.hasKeyboardFocus (true)) keyEditor.setText (v.key, false);
    if (! progEditor.hasKeyboardFocus (true)) progEditor.setText (prog, false);
    if (! rhythmEditor.hasKeyboardFocus (true)) rhythmEditor.setText (rhythm, false);
    infoL.setText (info, juce::dontSendNotification);
    familyL.setText (fam, juce::dontSendNotification);
    if (v.loaded)
    {
        sectionFrom.setRange (1, juce::jmax (1, v.bars), 1); sectionTo.setRange (1, juce::jmax (1, v.bars), 1);
        sectionFrom.setValue (v.sectionFrom + 1, juce::dontSendNotification);
        sectionTo.setValue (v.sectionTo < 0 ? v.bars : v.sectionTo + 1, juce::dontSendNotification);
    }
    for (auto* c : std::initializer_list<juce::Component*> { &bpmButton, &keyButton, &progButton, &useButton, &sectionButton, &sectionFrom, &sectionTo }) c->setEnabled (v.loaded);
    rhythmApply.setEnabled (rhythmEditor.getText().isNotEmpty());
    updating = false;
}

void BeatProPanel::resized()
{
    auto a = getLocalBounds().reduced (8, 6);
    auto r1 = row (a);
    r1.add (bpmL, 32); r1.add (bpmEditor, 70); r1.add (bpmButton, 120); r1.add (keyL, 64); r1.add (keyEditor, 70); r1.add (keyButton, 150);
    r1.add (progL, 56); r1.add (progEditor, 260); r1.add (progButton, 130); r1.rest (useButton);
    auto r2 = row (a);
    r2.add (sectionL, 130); r2.add (sectionFrom, 90); r2.add (sectionTo, 90); r2.add (sectionButton, 110); r2.add (regionButton, 200); r2.rest (correctButton);
    auto r3 = row (a, 40);
    r3.rest (infoL);
    auto r4 = row (a, 18);
    r4.add (rhythmL, 560); r4.add (rhythmFile, 170); r4.rest (rhythmApply);
    auto r5 = row (a, juce::jmax (40, a.getHeight() - 24));
    rhythmEditor.setBounds (r5.r.removeFromLeft (juce::jmin (360, r5.r.getWidth())));
    r5.r.removeFromLeft (8);
    familyL.setBounds (r5.r);
}
