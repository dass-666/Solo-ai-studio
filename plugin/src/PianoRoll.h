#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "solo/MusicTheory.h"
#include "solo/Sequence.h"
#include <functional>

// Vista tipo Piano Roll editable.
//  - Clic: seleccionar nota. Arrastrar: mover (rejilla de semicorchea; Shift = rejilla fina).
//  - Arrastrar el borde derecho: cambiar duración. Doble clic en vacío: añadir nota. Doble clic en nota: borrar.
//  - Supr/Retroceso: borrar. Flechas arriba/abajo: transponer (Shift = octava). Flechas izq/der: mover.
//  - Rueda: desplazamiento vertical; Shift+rueda: horizontal; Ctrl+rueda: zoom horizontal; Alt+rueda: zoom vertical.
//  - Arrastrar en la franja superior: seleccionar rango de compases para regenerar.
class PianoRoll : public juce::Component
{
public:
    PianoRoll();

    void setSequence (const solo::Sequence& s);
    const solo::Sequence& getSequence() const { return seq; }
    void setProgression (const solo::Progression& p, bool flats) { progression = p; preferFlats = flats; repaint(); }
    void setPlayheadTick (int64_t tick);
    void setLockedBars (const std::vector<bool>& b) { lockedBars = b; repaint(); }
    void setEditable (bool e) { editable = e; }

    int getSelectedIndex() const { return selected; }
    void setSelectedVelocity (int v);
    int getSelectedVelocity() const;

    // Rango de compases elegido en la franja superior (base 0, inclusivo); -1 si no hay.
    int getRangeStartBar() const { return rangeA; }
    int getRangeEndBar() const { return rangeB; }

    void zoomHorizontal (float factor);
    void zoomVertical (float factor);
    void fitToContent();

    std::function<void (const solo::Sequence&)> onEdit; // al terminar una edición
    std::function<void()> onSelectionChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    static constexpr int kKeyboardWidth = 44;
    static constexpr int kHeaderHeight = 22;

    juce::Rectangle<float> noteBounds (const solo::Note& n) const;
    int noteAt (juce::Point<float> p) const;
    float tickToX (double tick) const;
    double xToTick (float x) const;
    float pitchToY (int pitch) const;
    int yToPitch (float y) const;
    int64_t snap (double tick, bool fine) const;
    void commit();
    void clampScroll();

    solo::Sequence seq;
    solo::Progression progression;
    bool preferFlats = false;
    std::vector<bool> lockedBars;
    bool editable = true;
    int selected = -1;
    int64_t playhead = -1;

    double pixelsPerTick = 0.05;
    float noteHeight = 10.0f;
    double scrollTick = 0.0;
    int topPitch = 84;

    enum class Drag { None, Move, Resize, Range };
    Drag drag = Drag::None;
    solo::Note dragOriginal;
    juce::Point<float> dragStart;
    bool dragChanged = false;
    int rangeA = -1, rangeB = -1, rangeAnchor = -1;
};
