#include "PianoRoll.h"
#include <cmath>

using namespace solo;

namespace
{
const juce::Colour kBg (0xff15171c), kRowBlack (0xff111317), kRowWhite (0xff191c22), kGrid (0xff252932),
    kBeat (0xff2c313b), kBar (0xff4a5160), kText (0xffaab2c0), kAccent (0xff2ec4b6), kSel (0xffffc145),
    kPlay (0xffff5c5c), kRange (0x332ec4b6);

bool isBlackKey (int pitch)
{
    const int pc = pitch % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}
} // namespace

PianoRoll::PianoRoll()
{
    setWantsKeyboardFocus (true);
    seq.bars = 8;
}

void PianoRoll::setSequence (const Sequence& s)
{
    Note previous;
    const bool hadSelection = selected >= 0 && selected < (int) seq.notes.size();
    if (hadSelection) previous = seq.notes[(size_t) selected];
    const bool lengthChanged = s.lengthTicks() != seq.lengthTicks();
    seq = s;
    selected = -1;
    if (hadSelection)
        for (size_t i = 0; i < seq.notes.size(); ++i)
            if (seq.notes[i].startTick == previous.startTick && seq.notes[i].pitch == previous.pitch) { selected = (int) i; break; }
    if (lengthChanged) fitToContent();
    if (rangeB >= seq.bars) { rangeA = rangeB = -1; }
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void PianoRoll::setPlayheadTick (int64_t tick)
{
    if (tick == playhead) return;
    const float oldX = tickToX ((double) playhead), newX = tickToX ((double) tick);
    playhead = tick;
    repaint ((int) std::min (oldX, newX) - 2, 0, (int) std::abs (newX - oldX) + 5, getHeight());
}

void PianoRoll::setSelectedVelocity (int v)
{
    if (selected < 0 || selected >= (int) seq.notes.size()) return;
    seq.notes[(size_t) selected].velocity = juce::jlimit (1, 127, v);
    repaint();
    commit();
}

int PianoRoll::getSelectedVelocity() const
{
    return selected >= 0 && selected < (int) seq.notes.size() ? seq.notes[(size_t) selected].velocity : -1;
}

void PianoRoll::fitToContent()
{
    const int w = juce::jmax (100, getWidth() - kKeyboardWidth);
    pixelsPerTick = (double) w / (double) juce::jmax<int64_t> (1, seq.lengthTicks());
    scrollTick = 0.0;
    int lo = 127, hi = 0;
    for (const auto& n : seq.notes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
    if (lo > hi) { lo = 55; hi = 84; }
    // Ajuste vertical: el registro usado (+ margen) ocupa la altura disponible.
    const int span = (hi - lo) + 7;
    noteHeight = juce::jlimit (6.0f, 22.0f, (float) (getHeight() - kHeaderHeight) / (float) span);
    const int visible = (int) ((getHeight() - kHeaderHeight) / noteHeight);
    topPitch = juce::jlimit (visible - 1, 127, (lo + hi) / 2 + visible / 2);
    clampScroll();
    repaint();
}

void PianoRoll::zoomHorizontal (float factor)
{
    pixelsPerTick = juce::jlimit (0.005, 1.0, pixelsPerTick * factor);
    clampScroll();
    repaint();
}

void PianoRoll::zoomVertical (float factor)
{
    noteHeight = juce::jlimit (4.0f, 28.0f, noteHeight * factor);
    clampScroll();
    repaint();
}

void PianoRoll::clampScroll()
{
    const double visibleTicks = (getWidth() - kKeyboardWidth) / pixelsPerTick;
    scrollTick = juce::jlimit (0.0, juce::jmax (0.0, (double) seq.lengthTicks() - visibleTicks), scrollTick);
    const int visible = (int) ((getHeight() - kHeaderHeight) / noteHeight);
    topPitch = juce::jlimit (juce::jmin (127, visible - 1), 127, topPitch);
}

void PianoRoll::resized() { fitToContent(); }

float PianoRoll::tickToX (double tick) const { return (float) (kKeyboardWidth + (tick - scrollTick) * pixelsPerTick); }
double PianoRoll::xToTick (float x) const { return (x - kKeyboardWidth) / pixelsPerTick + scrollTick; }
float PianoRoll::pitchToY (int pitch) const { return kHeaderHeight + (topPitch - pitch) * noteHeight; }
int PianoRoll::yToPitch (float y) const { return topPitch - (int) std::floor ((y - kHeaderHeight) / noteHeight); }

int64_t PianoRoll::snap (double tick, bool fine) const
{
    const double grid = fine ? kPPQ / 12.0 : kPPQ / 4.0; // fina = 80 ticks (admite tresillos)
    return (int64_t) (std::round (tick / grid) * grid);
}

juce::Rectangle<float> PianoRoll::noteBounds (const Note& n) const
{
    const float x = tickToX ((double) n.startTick);
    const float w = juce::jmax (3.0f, (float) (n.lengthTicks * pixelsPerTick));
    return { x, pitchToY (n.pitch) + 1.0f, w, noteHeight - 2.0f };
}

int PianoRoll::noteAt (juce::Point<float> p) const
{
    for (int i = (int) seq.notes.size() - 1; i >= 0; --i)
        if (noteBounds (seq.notes[(size_t) i]).expanded (0, 1).contains (p)) return i;
    return -1;
}

void PianoRoll::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    const int w = getWidth(), h = getHeight();
    const int barT = seq.timeSignature.ticksPerBar(), beatT = seq.timeSignature.ticksPerBeat();

    // Filas de teclas
    for (int p = topPitch; p >= 0; --p)
    {
        const float y = pitchToY (p);
        if (y > h) break;
        g.setColour (isBlackKey (p) ? kRowBlack : kRowWhite);
        g.fillRect ((float) kKeyboardWidth, y, (float) (w - kKeyboardWidth), noteHeight);
        if (p % 12 == 0) { g.setColour (kGrid); g.drawHorizontalLine ((int) (y + noteHeight), (float) kKeyboardWidth, (float) w); }
    }

    // Rango seleccionado para regenerar
    if (rangeA >= 0 && rangeB >= rangeA)
    {
        g.setColour (kRange);
        const float x0 = tickToX ((double) rangeA * barT), x1 = tickToX ((double) (rangeB + 1) * barT);
        g.fillRect (x0, 0.0f, x1 - x0, (float) h);
    }

    // Rejilla de tiempo
    const int64_t total = seq.lengthTicks();
    const int sub = pixelsPerTick * kPPQ / 4 > 6 ? kPPQ / 4 : beatT;
    for (int64_t t = 0; t <= total; t += sub)
    {
        const float x = tickToX ((double) t);
        if (x < kKeyboardWidth || x > w) continue;
        g.setColour (t % barT == 0 ? kBar : (t % beatT == 0 ? kBeat : kGrid));
        g.drawVerticalLine ((int) x, (float) kHeaderHeight, (float) h);
    }

    // Cabecera con compases y acordes
    g.setColour (juce::Colour (0xff1d2027));
    g.fillRect (0, 0, w, kHeaderHeight);
    g.setFont (juce::FontOptions (12.0f));
    for (int b = 0; b < seq.bars; ++b)
    {
        const float x = tickToX ((double) b * barT);
        if (x > w) break;
        const bool inRange = rangeA >= 0 && b >= rangeA && b <= rangeB;
        g.setColour (inRange ? kAccent : kText);
        juce::String label = juce::String (b + 1);
        if (! progression.bars.empty()) label += "  " + juce::String (progression.chordAtBar (b).toString (preferFlats));
        g.drawText (label, (int) x + 4, 0, (int) (barT * pixelsPerTick) - 6, kHeaderHeight, juce::Justification::centredLeft, true);
    }

    // Notas
    for (int i = 0; i < (int) seq.notes.size(); ++i)
    {
        const auto& n = seq.notes[(size_t) i];
        auto r = noteBounds (n);
        if (r.getRight() < kKeyboardWidth || r.getX() > w) continue;
        const float v = n.velocity / 127.0f;
        auto c = (i == selected) ? kSel : kAccent.withMultipliedBrightness (0.55f + 0.45f * v);
        g.setColour (c);
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (c.brighter (0.4f));
        g.drawRoundedRectangle (r, 2.0f, 1.0f);
        // barra de velocity dentro de la nota
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (r.getX() + 1, r.getBottom() - 2, (r.getWidth() - 2) * v, 1.5f);
        if (noteHeight >= 12 && r.getWidth() > 26)
        {
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (juce::String (midiNoteName (n.pitch)), r.reduced (3, 0), juce::Justification::centredLeft, false);
        }
    }

    // Cursor de reproducción
    if (playhead >= 0)
    {
        g.setColour (kPlay);
        g.drawVerticalLine ((int) tickToX ((double) playhead), 0.0f, (float) h);
    }

    // Teclado
    g.setColour (juce::Colour (0xff0e1013));
    g.fillRect (0, kHeaderHeight, kKeyboardWidth, h - kHeaderHeight);
    for (int p = topPitch; p >= 0; --p)
    {
        const float y = pitchToY (p);
        if (y > h) break;
        g.setColour (isBlackKey (p) ? juce::Colour (0xff22252c) : juce::Colour (0xffc9ced8));
        g.fillRect (0.0f, y, (float) kKeyboardWidth - 2, noteHeight - 1);
        if (p % 12 == 0 && noteHeight >= 8)
        {
            g.setColour (juce::Colours::black);
            g.setFont (juce::FontOptions (juce::jmin (11.0f, noteHeight)));
            g.drawText (juce::String (midiNoteName (p)), 2, (int) y, kKeyboardWidth - 6, (int) noteHeight, juce::Justification::centredRight);
        }
    }
    g.setColour (kBar);
    g.drawVerticalLine (kKeyboardWidth - 1, 0.0f, (float) h);

    if (seq.notes.empty())
    {
        g.setColour (kText);
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (juce::String::fromUTF8 ("Pulsa GENERAR para crear un solo"), getLocalBounds().withTrimmedLeft (kKeyboardWidth),
                    juce::Justification::centred);
    }
}

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    const int i = noteAt (e.position);
    if (i >= 0 && editable && e.position.x > noteBounds (seq.notes[(size_t) i]).getRight() - 6)
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (i >= 0 && editable)
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dragChanged = false;
    dragStart = e.position;
    if (e.position.y < kHeaderHeight)
    {
        const int bar = juce::jlimit (0, seq.bars - 1, (int) (xToTick (e.position.x) / seq.timeSignature.ticksPerBar()));
        drag = Drag::Range;
        rangeAnchor = rangeA = rangeB = bar;
        repaint();
        if (onSelectionChanged) onSelectionChanged();
        return;
    }
    const int i = noteAt (e.position);
    selected = i;
    drag = Drag::None;
    if (i >= 0 && editable)
    {
        dragOriginal = seq.notes[(size_t) i];
        drag = e.position.x > noteBounds (dragOriginal).getRight() - 6 ? Drag::Resize : Drag::Move;
    }
    repaint();
    if (onSelectionChanged) onSelectionChanged();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::Range)
    {
        const int bar = juce::jlimit (0, seq.bars - 1, (int) (xToTick (e.position.x) / seq.timeSignature.ticksPerBar()));
        rangeA = juce::jmin (rangeAnchor, bar);
        rangeB = juce::jmax (rangeAnchor, bar);
        repaint();
        if (onSelectionChanged) onSelectionChanged();
        return;
    }
    if (selected < 0 || drag == Drag::None) return;
    auto& n = seq.notes[(size_t) selected];
    const bool fine = e.mods.isShiftDown();
    const double dTicks = (e.position.x - dragStart.x) / pixelsPerTick;
    if (drag == Drag::Move)
    {
        const int dPitch = (int) std::round ((dragStart.y - e.position.y) / noteHeight);
        n.startTick = juce::jlimit<int64_t> (0, seq.lengthTicks() - n.lengthTicks, snap ((double) dragOriginal.startTick + dTicks, fine));
        n.pitch = juce::jlimit (0, 127, dragOriginal.pitch + dPitch);
    }
    else
    {
        const int64_t minLen = fine ? kPPQ / 12 : kPPQ / 4;
        const int64_t end = snap ((double) dragOriginal.endTick() + dTicks, fine);
        n.lengthTicks = juce::jlimit<int64_t> (minLen, seq.lengthTicks() - n.startTick, end - n.startTick);
    }
    dragChanged = ! (n == dragOriginal);
    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    if (drag != Drag::Range && dragChanged) commit();
    drag = Drag::None;
    dragChanged = false;
}

void PianoRoll::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! editable || e.position.y < kHeaderHeight || e.position.x < kKeyboardWidth) return;
    const int i = noteAt (e.position);
    if (i >= 0)
    {
        seq.notes.erase (seq.notes.begin() + i);
        selected = -1;
    }
    else
    {
        Note n;
        n.startTick = juce::jlimit<int64_t> (0, seq.lengthTicks() - kPPQ / 2, snap (xToTick (e.position.x) - kPPQ / 8.0, false));
        n.lengthTicks = kPPQ / 2;
        n.pitch = juce::jlimit (0, 127, yToPitch (e.position.y));
        n.velocity = 90;
        seq.notes.push_back (n);
        selected = (int) seq.notes.size() - 1;
    }
    repaint();
    commit();
    if (onSelectionChanged) onSelectionChanged();
}

void PianoRoll::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float d = ! juce::exactlyEqual (w.deltaY, 0.0f) ? w.deltaY : w.deltaX;
    if (e.mods.isCommandDown() || e.mods.isCtrlDown()) zoomHorizontal (d > 0 ? 1.15f : 1.0f / 1.15f);
    else if (e.mods.isAltDown()) zoomVertical (d > 0 ? 1.1f : 1.0f / 1.1f);
    else if (e.mods.isShiftDown() || ! juce::exactlyEqual (w.deltaX, 0.0f))
    {
        scrollTick -= d * 400.0 / pixelsPerTick * 0.25;
        clampScroll();
        repaint();
    }
    else
    {
        topPitch += d > 0 ? 2 : -2;
        clampScroll();
        repaint();
    }
}

bool PianoRoll::keyPressed (const juce::KeyPress& k)
{
    if (! editable || selected < 0 || selected >= (int) seq.notes.size()) return false;
    auto& n = seq.notes[(size_t) selected];
    const bool shift = k.getModifiers().isShiftDown();
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        seq.notes.erase (seq.notes.begin() + selected);
        selected = -1;
        if (onSelectionChanged) onSelectionChanged();
    }
    else if (k.getKeyCode() == juce::KeyPress::upKey) n.pitch = juce::jmin (127, n.pitch + (shift ? 12 : 1));
    else if (k.getKeyCode() == juce::KeyPress::downKey) n.pitch = juce::jmax (0, n.pitch - (shift ? 12 : 1));
    else if (k.getKeyCode() == juce::KeyPress::leftKey) n.startTick = juce::jmax<int64_t> (0, n.startTick - (shift ? kPPQ / 12 : kPPQ / 4));
    else if (k.getKeyCode() == juce::KeyPress::rightKey)
        n.startTick = juce::jmin<int64_t> (seq.lengthTicks() - n.lengthTicks, n.startTick + (shift ? kPPQ / 12 : kPPQ / 4));
    else return false;
    repaint();
    commit();
    return true;
}

void PianoRoll::commit()
{
    if (onEdit) onEdit (seq);
}
