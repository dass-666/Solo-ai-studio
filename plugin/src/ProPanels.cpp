#include "ProPanels.h"

using namespace solo;

namespace
{
juce::String U (const char* s) { return juce::String::fromUTF8 (s); }
juce::String U (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }
const juce::Colour kText (0xffd7dce5), kDim (0xff8a93a3), kAccent (0xff2ec4b6), kGold (0xffffc857);

void label (juce::Label& l, const juce::String& t, juce::Component& parent)
{
    l.setText (t, juce::dontSendNotification);
    l.setColour (juce::Label::textColourId, kDim);
    l.setFont (juce::FontOptions (12.5f));
    parent.addAndMakeVisible (l);
}

void slider01 (juce::Slider& s, juce::Component& parent)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 38, 18);
    s.setRange (0.0, 1.0, 0.01);
    parent.addAndMakeVisible (s);
}

// Coloca componentes en fila, de izquierda a derecha.
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
} // namespace

// ============================================================ base
OpResult ProPanel::run (const std::function<OpResult (ProjectController&)>& op)
{
    const auto r = ctx.proc.perform (op);
    ctx.status (U (r.message), ! r.ok);
    ctx.refreshAll();
    return r;
}

bool ProPanel::rangeOrWarn (int& a, int& b)
{
    a = ctx.rangeStart();
    b = ctx.rangeEnd();
    if (a >= 0 && b >= a) return true;
    ctx.status (U ("Elige primero los compases arrastrando sobre la regla del piano roll"), true);
    return false;
}

// ============================================================ Notas
NotesPanel::NotesPanel (PanelContext& c) : ProPanel (c)
{
    label (hint, U ("Compases: arrastra sobre la regla del piano roll. Nota: haz clic en ella."), *this);
    label (transLabel, U ("Transponer"), *this);
    label (artLabel, U ("Articulación"), *this);
    for (int i = 0; i < 5; ++i) artBox.addItem (U (articulationName ((Articulation) i)), i + 1);
    artBox.setSelectedId (1);

    lockBars.onClick = [this] { int a, b; if (rangeOrWarn (a, b)) run ([=] (ProjectController& p) { return p.setBarsLocked (a, b, true); }); };
    unlockBars.onClick = [this] { int a, b; if (rangeOrWarn (a, b)) run ([=] (ProjectController& p) { return p.setBarsLocked (a, b, false); }); };
    regenBars.onClick = [this] {
        int a, b;
        if (! rangeOrWarn (a, b)) return;
        const auto seed = SoloAIStudioProcessor::freshSeed();
        run ([=] (ProjectController& p) { return p.regenerateBars (a, b, seed); });
    };
    lockNote.onClick = [this] {
        const int i = ctx.selectedNote();
        if (i < 0) { ctx.status (U ("Selecciona una nota en el piano roll"), true); return; }
        run ([=] (ProjectController& p) {
            const bool locked = p.current() && i < (int) p.current()->notes.size() && p.current()->notes[(size_t) i].locked;
            return p.setNotesLocked ({ i }, ! locked);
        });
    };
    regenNote.onClick = [this] {
        const int i = ctx.selectedNote();
        if (i < 0) { ctx.status (U ("Selecciona una nota en el piano roll"), true); return; }
        const auto seed = SoloAIStudioProcessor::freshSeed();
        run ([=] (ProjectController& p) { return p.regenerateNotes ({ i }, seed); });
    };
    auto transpose = [this] (int semis) {
        int a = ctx.rangeStart(), b = ctx.rangeEnd();
        if (a < 0) { a = -1; b = -1; } // todo el solo
        run ([=] (ProjectController& p) { return p.transpose (a, b, semis); });
    };
    tDown12.onClick = [=] { transpose (-12); };
    tDown1.onClick = [=] { transpose (-1); };
    tUp1.onClick = [=] { transpose (1); };
    tUp12.onClick = [=] { transpose (12); };
    auto articulation = [this] (bool on) {
        int a = ctx.rangeStart(), b = ctx.rangeEnd();
        if (a < 0) { a = -1; b = -1; }
        const auto art = (Articulation) juce::jlimit (0, 4, artBox.getSelectedId() - 1);
        run ([=] (ProjectController& p) { return p.setArticulation (a, b, art, on); });
    };
    artAdd.onClick = [=] { articulation (true); };
    artRemove.onClick = [=] { articulation (false); };

    for (auto* comp : std::initializer_list<juce::Component*> { &lockBars, &unlockBars, &lockNote, &regenBars, &regenNote, &tDown12,
                                                                &tDown1, &tUp1, &tUp12, &artBox, &artAdd, &artRemove })
        addAndMakeVisible (comp);
    lockBars.setTooltip (U ("Los compases bloqueados no cambian al regenerar"));
    transLabel.setTooltip (U ("Sin compases elegidos se transpone todo el solo"));
}

void NotesPanel::resized()
{
    auto a = getLocalBounds().reduced (8, 6);
    auto r1 = row (a); r1.add (lockBars, 150); r1.add (unlockBars, 160); r1.add (regenBars, 160); r1.add (lockNote, 190); r1.add (regenNote, 130);
    auto r2 = row (a); r2.add (transLabel, 80); r2.add (tDown12, 44); r2.add (tDown1, 40); r2.add (tUp1, 40); r2.add (tUp12, 44);
    r2.r.removeFromLeft (20);
    r2.add (artLabel, 84); r2.add (artBox, 130); r2.add (artAdd, 70); r2.add (artRemove, 70);
    auto r3 = row (a, 20); r3.rest (hint);
}

// ============================================================ Efectos
FxPanel::FxPanel (PanelContext& c) : ProPanel (c)
{
    presetBox.addItem (U ("Sin efectos"), 1);
    const auto& presets = fxPresets();
    for (size_t i = 0; i < presets.size(); ++i) presetBox.addItem (U (presets[i].name), (int) i + 2);
    presetBox.onChange = [this] {
        if (updating) return;
        const int id = presetBox.getSelectedId();
        if (id < 1) return;
        const std::string name = id == 1 ? std::string ("Sin efectos") : std::string (fxPresets()[(size_t) id - 2].name);
        run ([=] (ProjectController& p) { return p.setFxPreset (name); });
    };
    addAndMakeVisible (presetBox);

    enableToggle.onClick = [this] {
        if (updating) return;
        auto s = ctx.proc.getSettings();
        s.fxEnabled = enableToggle.getToggleState();
        ctx.proc.setSettings (s);
        ctx.status (s.fxEnabled ? U ("Efectos activados") : U ("Efectos desactivados (se oye el instrumento seco)"), false);
    };
    addAndMakeVisible (enableToggle);

    for (int i = 0; i < kNumFxStages; ++i)
    {
        auto* t = stageToggles.add (new juce::ToggleButton (U (fxStageName ((FxStage) i))));
        t->onClick = [this, i, t] {
            if (updating) return;
            const bool on = t->getToggleState();
            run ([=] (ProjectController& p) { return p.setFxStage ((FxStage) i, on); });
        };
        addAndMakeVisible (t);
    }
    for (int i = 0; i < kNumAutoTargets; ++i)
    {
        auto* l = sliderLabels.add (new juce::Label());
        label (*l, U (autoTargetName ((AutoTarget) i)), *this);
        auto* s = sliders.add (new juce::Slider());
        slider01 (*s, *this);
        s->onDragEnd = [this] { commitSliders(); };
    }
}

void FxPanel::commitSliders()
{
    if (updating) return;
    FxSettings fx;
    ctx.proc.read ([&] (const ProjectController& p) { fx = p.state.fx; });
    for (int i = 0; i < kNumAutoTargets; ++i)
    {
        const auto t = (AutoTarget) i;
        const float v = (float) sliders[i]->getValue();
        if (std::abs (v - paramToAuto (t, fx)) > 0.004f) setAutoParam (fx, t, v);
    }
    run ([=] (ProjectController& p) { return p.setFx (fx, "Editar efectos"); });
}

void FxPanel::refresh()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    FxSettings fx;
    std::string preset;
    bool enabled = false;
    ctx.proc.read ([&] (const ProjectController& p) { fx = p.state.fx; preset = p.state.settings.fxPreset; enabled = p.state.settings.fxEnabled; });
    enableToggle.setToggleState (enabled, juce::dontSendNotification);
    const int idx = fxPresetIndex (preset);
    if (! enabled) presetBox.setSelectedId (1, juce::dontSendNotification);
    else if (idx >= 0) presetBox.setSelectedId (idx + 2, juce::dontSendNotification);
    else presetBox.setText (U (preset.empty() ? "Personalizado" : preset), juce::dontSendNotification);
    for (int i = 0; i < kNumFxStages; ++i) stageToggles[i]->setToggleState (fx.isOn ((FxStage) i), juce::dontSendNotification);
    for (int i = 0; i < kNumAutoTargets; ++i) sliders[i]->setValue (paramToAuto ((AutoTarget) i, fx), juce::dontSendNotification);
}

void FxPanel::resized()
{
    auto a = getLocalBounds().reduced (8, 6);
    auto r1 = row (a); r1.add (presetBox, 200); r1.add (enableToggle, 130);
    const int w = juce::jmax (80, r1.r.getWidth() / 5);
    for (int i = 0; i < 5; ++i) r1.add (*stageToggles[i], w - 6);
    auto r2 = row (a); r2.r.removeFromLeft (336);
    for (int i = 5; i < kNumFxStages; ++i) r2.add (*stageToggles[i], w - 6);
    auto r3 = row (a);
    const int sw = r3.r.getWidth() / 3;
    for (int i = 0; i < 3; ++i) { r3.add (*sliderLabels[i], 90); r3.add (*sliders[i], sw - 102); }
    auto r4 = row (a);
    for (int i = 3; i < 6; ++i) { r4.add (*sliderLabels[i], 90); r4.add (*sliders[i], sw - 102); }
}

// ============================================================ Automatización
void AutomationCurve::setLane (const AutomationLane& l, int64_t totalTicks, int64_t barTicks)
{
    if (dragging >= 0) return;
    lane = l;
    total = juce::jmax<int64_t> (1, totalTicks);
    bar = juce::jmax<int64_t> (1, barTicks);
    repaint();
}

juce::Point<float> AutomationCurve::toScreen (const AutoPoint& p) const
{
    const auto r = getLocalBounds().toFloat().reduced (4);
    return { r.getX() + r.getWidth() * (float) p.tick / (float) total, r.getBottom() - r.getHeight() * p.value };
}

AutoPoint AutomationCurve::fromScreen (juce::Point<float> s) const
{
    const auto r = getLocalBounds().toFloat().reduced (4);
    AutoPoint p;
    const double t = juce::jlimit (0.0, 1.0, (double) ((s.x - r.getX()) / r.getWidth())) * (double) total;
    const int64_t grid = juce::jmax<int64_t> (1, bar / 4); // a negra (4/4)
    p.tick = juce::jlimit<int64_t> (0, total, (int64_t) std::llround (t / (double) grid) * grid);
    p.value = juce::jlimit (0.0f, 1.0f, (r.getBottom() - s.y) / r.getHeight());
    return p;
}

int AutomationCurve::nearest (juce::Point<float> s) const
{
    int best = -1;
    float bestD = 10.0f;
    for (int i = 0; i < (int) lane.points.size(); ++i)
    {
        const float d = toScreen (lane.points[(size_t) i]).getDistanceFrom (s);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void AutomationCurve::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15171c));
    const auto r = getLocalBounds().toFloat().reduced (4);
    g.setColour (juce::Colour (0xff2c313b));
    for (int64_t t = 0; t <= total; t += bar)
    {
        const float x = r.getX() + r.getWidth() * (float) t / (float) total;
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
    }
    const auto c = lane.enabled ? kAccent : kDim;
    if (lane.points.empty())
    {
        g.setColour (kDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (U ("Haz clic para añadir puntos · arrastra para moverlos · clic derecho para borrar"), getLocalBounds(),
                    juce::Justification::centred);
        return;
    }
    juce::Path path;
    const auto first = toScreen (lane.points.front());
    path.startNewSubPath (r.getX(), first.y);
    for (const auto& p : lane.points) path.lineTo (toScreen (p));
    path.lineTo (r.getRight(), toScreen (lane.points.back()).y);
    g.setColour (c);
    g.strokePath (path, juce::PathStrokeType (2.0f));
    for (const auto& p : lane.points) g.fillEllipse (juce::Rectangle<float> (7, 7).withCentre (toScreen (p)));
}

void AutomationCurve::mouseDown (const juce::MouseEvent& e)
{
    const int i = nearest (e.position);
    if (e.mods.isPopupMenu())
    {
        if (i >= 0) { lane.points.erase (lane.points.begin() + i); changed = true; repaint(); }
        return;
    }
    if (i >= 0) dragging = i;
    else
    {
        lane.points.push_back (fromScreen (e.position));
        lane.sortPoints();
        dragging = nearest (e.position);
        lane.enabled = true;
    }
    changed = true;
    repaint();
}

void AutomationCurve::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0 || dragging >= (int) lane.points.size()) return;
    lane.points[(size_t) dragging] = fromScreen (e.position);
    changed = true;
    repaint();
}

void AutomationCurve::mouseUp (const juce::MouseEvent&)
{
    dragging = -1;
    if (! changed) return;
    changed = false;
    lane.sortPoints();
    // Dos puntos en el mismo tick: se queda el último.
    for (size_t i = 1; i < lane.points.size();)
        if (lane.points[i].tick == lane.points[i - 1].tick) lane.points.erase (lane.points.begin() + (long) i - 1);
        else ++i;
    if (onCommit) onCommit (lane);
}

AutomationPanel::AutomationPanel (PanelContext& c) : ProPanel (c)
{
    for (int i = 0; i < kNumAutoTargets; ++i) targetBox.addItem (U (autoTargetName ((AutoTarget) i)), i + 1);
    targetBox.setSelectedId (2);
    targetBox.onChange = [this] { refresh(); };
    auto commit = [this] (AutomationLane l) {
        const auto t = target();
        l.target = t;
        run ([=] (ProjectController& p) {
            auto set = p.state.automation;
            set.lane (t) = l;
            if (! p.state.settings.fxEnabled) p.state.settings.fxEnabled = true; // la automatización actúa sobre los efectos
            return p.setAutomation (set);
        });
    };
    curve.onCommit = commit;
    enableToggle.onClick = [this, commit] {
        AutomationLane l;
        ctx.proc.read ([&] (const ProjectController& p) { if (auto* f = p.state.automation.find (target())) l = *f; });
        l.enabled = enableToggle.getToggleState();
        commit (l);
    };
    clearButton.onClick = [this, commit] { AutomationLane l; commit (l); };
    label (hint, U ("La curva se aplica por compases con suavizado; se oye en directo y en el WAV exportado."), *this);
    for (auto* comp : std::initializer_list<juce::Component*> { &targetBox, &enableToggle, &clearButton, &curve }) addAndMakeVisible (comp);
}

AutoTarget AutomationPanel::target() const { return (AutoTarget) juce::jlimit (0, kNumAutoTargets - 1, targetBox.getSelectedId() - 1); }

void AutomationPanel::refresh()
{
    AutomationLane l;
    l.target = target();
    int64_t total = 8 * 4 * kPPQ, bar = 4 * kPPQ;
    ctx.proc.read ([&] (const ProjectController& p) {
        if (auto* f = p.state.automation.find (target())) l = *f;
        if (p.current()) { total = p.current()->lengthTicks(); bar = p.current()->timeSignature.ticksPerBar(); }
        else { bar = (int64_t) p.state.settings.tsNumerator * kPPQ; total = bar * p.state.settings.bars; }
    });
    enableToggle.setToggleState (l.enabled, juce::dontSendNotification);
    curve.setLane (l, total, bar);
}

void AutomationPanel::resized()
{
    auto a = getLocalBounds().reduced (8, 6);
    auto left = a.removeFromLeft (190);
    auto r1 = row (left); r1.rest (targetBox);
    auto r2 = row (left); r2.add (enableToggle, 80); r2.rest (clearButton);
    hint.setBounds (left.removeFromTop (48));
    hint.setJustificationType (juce::Justification::topLeft);
    a.removeFromLeft (8);
    curve.setBounds (a);
}

// ============================================================ Voz y ADN
VoicePanel::VoicePanel (PanelContext& c) : ProPanel (c)
{
    for (int i = 0; i < 4; ++i) humMode.addItem (U (humModeName ((HumMode) i)), i + 1);
    humMode.setSelectedId (2);
    humMode.setTooltip (U ("Copiar: alturas y ritmo tal cual · Corregir: ajusta a la armonía · Interpretar: añade expresión · Desarrollar: compone a partir del motivo"));
    for (int i = 0; i < 5; ++i) dnaMode.addItem (U (derivedModeName ((DerivedMode) i)), i + 1);
    dnaMode.setSelectedId (1);
    importButton.onClick = [this] { importVoice(); };
    correctButton.onClick = [this] {
        int a, b;
        if (! rangeOrWarn (a, b)) return;
        chooser = std::make_unique<juce::FileChooser> (U ("Voz para corregir los compases elegidos"), juce::File(), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, a, b] (const juce::FileChooser& fc) {
                                  const auto f = fc.getResult();
                                  if (! f.existsAsFile()) return;
                                  MonoAudio in;
                                  std::string err;
                                  if (! readWavFile (f.getFullPathName().toStdString(), in, err)) { ctx.status (U ("WAV no válido: ") + U (err), true); return; }
                                  const auto va = analyseVoice (in);
                                  const auto o = humOptions();
                                  run ([&] (ProjectController& p) { return p.applyVocalCorrection (va, o, a, b); });
                              });
    };
    extractButton.onClick = [this] { run ([] (ProjectController& p) { return p.extractDNAFromCurrent(); }); };
    deriveButton.onClick = [this] {
        DerivedOptions o;
        o.mode = (DerivedMode) juce::jlimit (0, 4, dnaMode.getSelectedId() - 1);
        o.fidelity = fidelity.getValue();
        o.freedom = freedom.getValue();
        o.expression = expression.getValue();
        o.development = development.getValue();
        const auto seed = SoloAIStudioProcessor::freshSeed();
        run ([=] (ProjectController& p) { return p.generateFromDNA (o, seed); });
    };
    for (auto* s : { &fidelity, &freedom, &expression, &development }) slider01 (*s, *this);
    fidelity.setValue (0.7); freedom.setValue (0.4); expression.setValue (0.6); development.setValue (0.5);
    label (fidelityL, "Fidelidad", *this);
    label (freedomL, "Libertad", *this);
    label (expressionL, U ("Expresión"), *this);
    label (developmentL, "Desarrollo", *this);
    label (dnaSummary, U ("ADN: aún no extraído"), *this);
    label (voiceNote, U ("Graba la voz en el DAW y expórtala a WAV. Validado solo con voces sintéticas."), *this);
    for (auto* comp : std::initializer_list<juce::Component*> { &humMode, &rhythmOnly, &importButton, &correctButton, &extractButton, &dnaMode, &deriveButton })
        addAndMakeVisible (comp);
}

HumToSoloOptions VoicePanel::humOptions() const
{
    HumToSoloOptions o;
    o.mode = (HumMode) juce::jlimit (0, 3, humMode.getSelectedId() - 1);
    o.rhythmOnly = rhythmOnly.getToggleState();
    o.fidelity = fidelity.getValue();
    o.freedom = freedom.getValue();
    return o;
}

void VoicePanel::importVoice()
{
    chooser = std::make_unique<juce::FileChooser> (U ("Importar voz (WAV)"), juce::File(), "*.wav");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              if (! f.existsAsFile()) return;
                              juce::String msg;
                              const bool ok = ctx.proc.importVoice (f, humOptions(), msg);
                              ctx.status (msg, ! ok);
                              ctx.refreshAll();
                          });
}

void VoicePanel::refresh()
{
    std::string s;
    ctx.proc.read ([&] (const ProjectController& p) { s = p.state.dnaSummary; });
    dnaSummary.setText (s.empty() ? U ("ADN: aún no extraído (genera o importa un solo y pulsa «Extraer ADN»)") : U ("ADN: ") + U (s),
                        juce::dontSendNotification);
}

void VoicePanel::resized()
{
    auto a = getLocalBounds().reduced (8, 6);
    auto r1 = row (a); r1.add (humMode, 140); r1.add (rhythmOnly, 100); r1.add (importButton, 170); r1.add (correctButton, 210); r1.rest (voiceNote);
    auto r2 = row (a); r2.add (extractButton, 110); r2.add (dnaMode, 130); r2.add (deriveButton, 150); r2.rest (dnaSummary);
    auto r3 = row (a);
    const int w = r3.r.getWidth() / 4;
    r3.add (fidelityL, 70); r3.add (fidelity, w - 82);
    r3.add (freedomL, 70); r3.add (freedom, w - 82);
    r3.add (expressionL, 70); r3.add (expression, w - 82);
    r3.add (developmentL, 76); r3.rest (development);
}

// ============================================================ Historial y A/B
HistoryPanel::HistoryPanel (PanelContext& c) : ProPanel (c)
{
    list.setModel (this);
    list.setRowHeight (20);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff15171c));
    addAndMakeVisible (list);
    favButton.onClick = [this] {
        run ([] (ProjectController& p) {
            if (! p.hasSequence()) return OpResult::fail ("No hay variaciones");
            const int i = p.state.history.currentIndex();
            return p.setFavorite (i, ! p.state.history.currentItem().favorite);
        });
    };
    renameButton.onClick = [this] {
        const auto name = nameEditor.getText().trim().toStdString();
        run ([=] (ProjectController& p) { return p.renameVariation (p.state.history.currentIndex(), name); });
    };
    deleteButton.onClick = [this] {
        run ([] (ProjectController& p) {
            if (! p.hasSequence()) return OpResult::fail ("No hay variaciones");
            // Protección: una favorita nunca se borra de un clic; primero hay que quitarle la marca.
            if (p.state.history.currentItem().favorite) return OpResult::fail ("Es favorita: quítale la marca antes de eliminarla");
            return p.deleteVariation (p.state.history.currentIndex());
        });
    };
    slotA.onClick = [this] { run ([] (ProjectController& p) { return p.assignSlot ('A', p.state.history.currentIndex()); }); };
    slotB.onClick = [this] { run ([] (ProjectController& p) { return p.assignSlot ('B', p.state.history.currentIndex()); }); };
    switchAB.onClick = [this] { run ([] (ProjectController& p) { return p.switchAB(); }); };
    nameEditor.setTextToShowWhenEmpty (U ("Nombre de la variación"), kDim);
    label (abLabel, "A: -  B: -", *this);
    for (auto* comp : std::initializer_list<juce::Component*> { &favButton, &renameButton, &deleteButton, &slotA, &slotB, &switchAB, &nameEditor })
        addAndMakeVisible (comp);
}

int HistoryPanel::getNumRows() { return (int) rows.size(); }

void HistoryPanel::paintListBoxItem (int r, juce::Graphics& g, int w, int h, bool selected)
{
    if (r < 0 || r >= (int) rows.size()) return;
    if (selected) g.fillAll (kAccent.withAlpha (0.25f));
    g.setColour (rows[(size_t) r].startsWith (U ("★")) ? kGold : kText);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText (rows[(size_t) r], 6, 0, w - 8, h, juce::Justification::centredLeft, true);
}

void HistoryPanel::selectedRowsChanged (int last)
{
    if (updating || last < 0 || last == currentRow) return;
    run ([=] (ProjectController& p) { return p.selectVariation (last); });
}

void HistoryPanel::refresh()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    rows.clear();
    juce::String ab;
    ctx.proc.read ([&] (const ProjectController& p) {
        const auto& items = p.state.history.all();
        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& it = items[i];
            juce::String s;
            s << (it.favorite ? U ("★ ") : "   ") << (int) i + 1 << ". " << (it.name.empty() ? U ("Variación") : U (it.name))
              << U ("  · semilla ") << juce::String ((juce::int64) it.seed);
            if (it.id == p.state.slotA) s << "   [A]";
            if (it.id == p.state.slotB) s << "   [B]";
            rows.push_back (s);
        }
        currentRow = p.state.history.currentIndex();
        auto slotText = [&] (uint32_t id) { const int i = p.state.history.indexOfId (id); return i >= 0 ? juce::String (i + 1) : juce::String ("-"); };
        ab << "A: " << slotText (p.state.slotA) << "   B: " << slotText (p.state.slotB);
    });
    abLabel.setText (ab, juce::dontSendNotification);
    list.updateContent();
    if (currentRow >= 0) list.selectRow (currentRow, false, true);
    list.repaint();
}

void HistoryPanel::resized()
{
    auto a = getLocalBounds().reduced (8, 6);
    auto right = a.removeFromRight (juce::jmin (430, a.getWidth() / 2));
    list.setBounds (a);
    right.removeFromLeft (8);
    auto r1 = row (right); r1.add (favButton, 100); r1.add (deleteButton, 80); r1.rest (abLabel);
    auto r2 = row (right); r2.add (nameEditor, 220); r2.rest (renameButton);
    auto r3 = row (right); r3.add (slotA, 90); r3.add (slotB, 90); r3.rest (switchAB);
}

// ============================================================ Comandos
CommandPanel::CommandPanel (PanelContext& c) : ProPanel (c)
{
    input.setTextToShowWhenEmpty (U ("Ej.: bloquea el compás 1, regenera los compases 2 a 4 y luego más reverb"), kDim);
    input.onReturnKey = [this] { interpret(); };
    interpretButton.onClick = [this] { interpret(); };
    executeButton.setEnabled (false);
    executeButton.onClick = [this] {
        if (! hasPending) return;
        const auto r = ctx.proc.executeParsed (pending);
        hasPending = false;
        executeButton.setEnabled (false);
        juce::String msg;
        for (const auto& x : r.results) msg << (x.ok ? U ("✓ ") : U ("✗ ")) << U (x.message) << "   ";
        preview.setText (msg, juce::dontSendNotification);
        ctx.status (U ("Comando ejecutado (se puede deshacer)"), ! r.ok);
        ctx.refreshAll();
    };
    cancelButton.onClick = [this] { hasPending = false; executeButton.setEnabled (false); preview.setText ({}, juce::dontSendNotification); };
    label (preview, {}, *this);
    preview.setJustificationType (juce::Justification::topLeft);
    preview.setColour (juce::Label::textColourId, kText);
    for (auto* comp : std::initializer_list<juce::Component*> { &input, &interpretButton, &executeButton, &cancelButton }) addAndMakeVisible (comp);
}

void CommandPanel::interpret()
{
    pending = ctx.proc.parseCommandText (input.getText());
    if (pending.ops.empty())
    {
        hasPending = false;
        executeButton.setEnabled (false);
        preview.setText (U ("No se entendió ninguna orden. Prueba: «regenera los compases 3 a 4», «más delay», «sube una octava el compás 2»."),
                         juce::dontSendNotification);
        return;
    }
    const auto pv = ctx.proc.previewParsed (pending);
    juce::String t;
    t << U (pending.summary()) << U ("  (confianza ") << juce::String (pending.confidence, 2) << ")\n";
    for (const auto& s : pv.steps) t << U (s) << "\n";
    if (! pending.unrecognised.empty()) t << U ("No entendido: ") << U (pending.unrecognised.front());
    preview.setText (t, juce::dontSendNotification);
    hasPending = true;
    executeButton.setEnabled (true);
    // Las órdenes de transporte o deshacer no cambian notas: se ejecutan sin confirmar.
    if (! pending.needsConfirmation) executeButton.triggerClick();
}

void CommandPanel::resized()
{
    auto a = getLocalBounds().reduced (8, 6);
    auto r1 = row (a); r1.add (input, juce::jmax (200, r1.r.getWidth() - 3 * 106)); r1.add (interpretButton, 100); r1.add (executeButton, 100); r1.rest (cancelButton);
    preview.setBounds (a);
}
