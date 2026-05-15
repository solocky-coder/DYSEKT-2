#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../sequencer/SequencerEngine.h"
#include "DysektLookAndFeel.h"
#include "VelocityLane.h"

//==============================================================================
//  PianoRollComponent  (multi-track revision)
//
//  Edits one track at a time — the active track is set via setActiveTrack().
//  The playhead is shared across all tracks.
//  Track switching is handled by PianoRollPanel / TrackHeaderStrip.
//==============================================================================
class PianoRollComponent : public juce::Component,
                           private juce::Timer,
                           private juce::ScrollBar::Listener
{
public:
    static constexpr int kKeysW     = 52;
    static constexpr int kRulerH    = 22;
    static constexpr int kVelocityH = 56;
    static constexpr int kScrollH   = 14;
    static constexpr int kNumNotes  = 128;

    explicit PianoRollComponent (SequencerEngine& seq)
        : engine (seq)
    {
        velocityLane.setClip (engine.getClip (0));
        addAndMakeVisible (velocityLane);

        hScroll.setRangeLimits (0.0, 1.0);
        hScroll.setCurrentRange (0.0, 0.3);
        hScroll.setAutoHide (false);
        hScroll.setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xFF2A3848));
        hScroll.addListener (this);
        addAndMakeVisible (hScroll);

        vScroll.setRangeLimits (0.0, 1.0);
        vScroll.setCurrentRange (0.3, 0.7);
        vScroll.setAutoHide (false);
        vScroll.setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xFF2A3848));
        vScroll.addListener (this);
        addAndMakeVisible (vScroll);

        setWantsKeyboardFocus (true);
        startTimerHz (30);
    }

    ~PianoRollComponent() override
    {
        hScroll.removeListener (this);
        vScroll.removeListener (this);
        stopTimer();
    }

    //==========================================================================
    void setActiveTrack (int trackIndex)
    {
        activeTrack = trackIndex;
        velocityLane.setClip (engine.getClip (trackIndex));
        velocityLane.setSelectedNote (-1);
        selectedNotes.clear();
        repaint();
        velocityLane.repaint();
    }

    void setSnapTicks (int64_t ticks) { snapTicks = ticks; }

    //==========================================================================
    void resized() override
    {
        auto r = getLocalBounds();
        auto vScrollR = r.removeFromRight (kScrollH);
        auto hScrollR = r.removeFromBottom (kScrollH);
        auto velR     = r.removeFromBottom (kVelocityH);

        rulerBounds = r.removeFromTop (kRulerH);
        keysBounds  = r.removeFromLeft (kKeysW);
        gridBounds  = r;

        velocityLane.setBounds (velR.withTrimmedLeft (kKeysW));
        hScroll.setBounds      (hScrollR.withTrimmedLeft (kKeysW));
        vScroll.setBounds      (vScrollR.withTrimmedTop (kRulerH));

        updateScrollRanges();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF060608));
        drawRuler    (g);
        drawKeyboard (g);
        drawGrid     (g);
        drawNotes    (g);
        drawPlayhead (g);
        drawRubberBand (g);
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! gridBounds.contains (e.getPosition())) return;
        grabKeyboardFocus();

        MidiClip* clip = engine.getClip (activeTrack);
        if (clip == nullptr) return;

        const int64_t tick    = xToTick (e.x);
        const int     noteNum = yToNote (e.y);

        if (e.mods.isRightButtonDown())
        {
            int idx = hitTestAny (clip, tick, noteNum);
            if (idx >= 0) { clip->removeNote (idx); selectedNotes.clear(); repaint(); }
            return;
        }

        int idx = hitTestAny (clip, tick, noteNum);
        if (idx >= 0)
        {
            if (isNearRightEdge (clip, e.x, idx))
            {
                dragMode = DragMode::Resize;
                dragNoteIdx = idx;
                dragStartTick = tick;
                const juce::ScopedReadLock sl (clip->getLock());
                dragOrigDuration = clip->getNotes()[idx].durationTick;
            }
            else
            {
                dragMode = DragMode::Move;
                dragNoteIdx = idx;
                dragStartTick = tick;
                dragStartNote = noteNum;
                const juce::ScopedReadLock sl (clip->getLock());
                dragOrigStart = clip->getNotes()[idx].startTick;
                dragOrigNote  = clip->getNotes()[idx].note;
                if (! e.mods.isShiftDown()) selectedNotes.clear();
                selectedNotes.addIfNotAlreadyThere (idx);
                velocityLane.setSelectedNote (idx);
            }
        }
        else
        {
            if (! e.mods.isShiftDown()) selectedNotes.clear();
            dragMode = DragMode::Draw;
            dragStartTick = snap (tick);
            dragStartNote = noteNum;

            MidiNote n;
            n.note         = noteNum;
            n.velocity     = 100;
            n.startTick    = dragStartTick;
            n.durationTick = juce::jmax ((int64_t) 1, snapTicks > 0 ? snapTicks : MidiClip::kPPQ / 2);
            dragNoteIdx    = clip->addNote (n);
            selectedNotes.clear();
            selectedNotes.add (dragNoteIdx);
            velocityLane.setSelectedNote (dragNoteIdx);
        }
        repaint(); velocityLane.repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        MidiClip* clip = engine.getClip (activeTrack);
        if (clip == nullptr) return;

        const int64_t tick    = xToTick (e.x);
        const int     noteNum = yToNote (e.y);

        switch (dragMode)
        {
            case DragMode::Draw:
            {
                const int64_t newEnd = snap (juce::jmax (tick, dragStartTick + 1));
                clip->resizeNote (dragNoteIdx, juce::jmax ((int64_t)1, newEnd - dragStartTick));
                break;
            }
            case DragMode::Move:
            {
                const int64_t newStart = snap (juce::jmax ((int64_t)0, dragOrigStart + (tick - dragStartTick)));
                const int     newNote  = juce::jlimit (0, 127, dragOrigNote + (noteNum - dragStartNote));
                clip->moveNote (dragNoteIdx, newStart, newNote);
                break;
            }
            case DragMode::Resize:
            {
                const int64_t newDur = snap (juce::jmax ((int64_t)1, dragOrigDuration + (tick - dragStartTick)));
                clip->resizeNote (dragNoteIdx, newDur);
                break;
            }
            default: break;
        }
        repaint(); velocityLane.repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragMode = DragMode::None; dragNoteIdx = -1; repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCtrlDown())
        {
            const double zf = w.deltaY > 0 ? 1.15 : (1.0 / 1.15);
            const double tickAtMouse = xToTick (e.x);
            pixelsPerTick = juce::jlimit (0.03, 8.0, pixelsPerTick * zf);
            scrollX = juce::jmax (0.0, tickAtMouse * pixelsPerTick - (e.x - kKeysW));
        }
        else if (e.mods.isShiftDown())
            scrollX = juce::jmax (0.0, scrollX - w.deltaY * 80.0);
        else
            noteRowOffset = juce::jlimit (0, kNumNotes - visibleRows(), noteRowOffset + (w.deltaY < 0 ? 3 : -3));

        syncScroll(); updateScrollRanges(); repaint();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == juce::KeyPress::deleteKey || k.getKeyCode() == juce::KeyPress::backspaceKey)
        {
            MidiClip* clip = engine.getClip (activeTrack);
            if (clip)
            {
                juce::Array<int> sorted = selectedNotes;
                sorted.sort();
                for (int i = sorted.size() - 1; i >= 0; --i)
                    clip->removeNote (sorted[i]);
                selectedNotes.clear();
                velocityLane.setSelectedNote (-1);
                repaint(); velocityLane.repaint();
            }
            return true;
        }
        if (k.isKeyCode ('A') && k.getModifiers().isCommandDown())
        {
            MidiClip* clip = engine.getClip (activeTrack);
            if (clip)
            {
                selectedNotes.clear();
                const juce::ScopedReadLock sl (clip->getLock());
                for (int i = 0; i < clip->getNotes().size(); ++i)
                    selectedNotes.add (i);
                repaint();
            }
            return true;
        }
        return false;
    }

private:
    //==========================================================================
    SequencerEngine& engine;
    int activeTrack = 0;

    juce::Rectangle<int> rulerBounds, keysBounds, gridBounds;
    juce::ScrollBar hScroll { false }, vScroll { true };

    double  pixelsPerTick = 0.2;
    double  scrollX       = 0.0;
    int     noteRowOffset = 48;
    int     noteRowH      = 10;
    int64_t snapTicks     = MidiClip::kPPQ / 2;

    enum class DragMode { None, Draw, Move, Resize };
    DragMode dragMode    = DragMode::None;
    int      dragNoteIdx = -1;
    int64_t  dragStartTick = 0, dragOrigStart = 0, dragOrigDuration = 0;
    int      dragStartNote = 0, dragOrigNote = 0;

    juce::Array<int> selectedNotes;
    VelocityLane     velocityLane;

    //==========================================================================
    int  visibleRows() const { return noteRowH > 0 ? gridBounds.getHeight() / noteRowH : 1; }
    int64_t xToTick (int x) const noexcept { return (int64_t)((x - kKeysW + scrollX) / pixelsPerTick); }
    float   tickToX (int64_t t) const noexcept { return (float)(t * pixelsPerTick - scrollX + kKeysW); }
    int     yToNote (int y)  const noexcept { return juce::jlimit (0, 127, (kNumNotes-1) - ((y - kRulerH) / noteRowH + noteRowOffset)); }
    float   noteToY (int n)  const noexcept { return (float)(kRulerH + ((kNumNotes-1-n) - noteRowOffset) * noteRowH); }
    int64_t snap    (int64_t t) const noexcept { return snapTicks > 0 ? ((t + snapTicks/2) / snapTicks) * snapTicks : t; }

    int hitTestAny (MidiClip* clip, int64_t tick, int note) const
    {
        if (! clip) return -1;
        const juce::ScopedReadLock sl (clip->getLock());
        for (int i = 0; i < clip->getNotes().size(); ++i)
        {
            const auto& n = clip->getNotes().getReference (i);
            if (n.note == note && tick >= n.startTick && tick < n.endTick()) return i;
        }
        return -1;
    }

    bool isNearRightEdge (MidiClip* clip, int mouseX, int idx) const
    {
        if (! clip) return false;
        const juce::ScopedReadLock sl (clip->getLock());
        if (! juce::isPositiveAndBelow (idx, clip->getNotes().size())) return false;
        return std::abs (mouseX - (int) tickToX (clip->getNotes()[idx].endTick())) < 6;
    }

    void syncScroll()
    {
        velocityLane.pixelsPerTick = pixelsPerTick;
        velocityLane.scrollOffsetX = scrollX;
        velocityLane.repaint();
    }

    void updateScrollRanges()
    {
        const double totalW = engine.getLengthTicks() * pixelsPerTick + gridBounds.getWidth();
        hScroll.setRangeLimits (0.0, totalW);
        hScroll.setCurrentRange (scrollX, scrollX + gridBounds.getWidth(), juce::dontSendNotification);
        vScroll.setRangeLimits (0.0, (double) kNumNotes);
        vScroll.setCurrentRange ((double) noteRowOffset, (double)(noteRowOffset + visibleRows()), juce::dontSendNotification);
        syncScroll();
    }

    void timerCallback() override { repaint (gridBounds); }

    void scrollBarMoved (juce::ScrollBar* bar, double newRangeStart) override
    {
        if (bar == &hScroll)
        {
            scrollX = newRangeStart;
            syncScroll();
            repaint();
        }
        else if (bar == &vScroll)
        {
            noteRowOffset = juce::jlimit (0, kNumNotes - visibleRows(), (int) newRangeStart);
            repaint();
        }
    }

    //==========================================================================
    void drawRuler (juce::Graphics& g)
    {
        g.setColour (juce::Colour (0xFF0D0D14));
        g.fillRect (rulerBounds);
        g.setColour (juce::Colour (0xFF1C2028));
        g.fillRect (rulerBounds.withTrimmedLeft (kKeysW));

        const double ticksPerBar = MidiClip::kPPQ * 4;
        const int64_t firstBar = (int64_t)(scrollX / (ticksPerBar * pixelsPerTick));
        const int64_t lastBar  = firstBar + (int64_t)(gridBounds.getWidth() / (ticksPerBar * pixelsPerTick)) + 2;

        g.setFont (DysektLookAndFeel::makeMonoFont (10.f));
        for (int64_t bar = firstBar; bar <= lastBar; ++bar)
        {
            const float x = tickToX (bar * (int64_t) ticksPerBar);
            if (x < kKeysW) continue;
            g.setColour (juce::Colour (0xFF2A3040));
            g.drawVerticalLine ((int)x, (float) rulerBounds.getY(), (float) rulerBounds.getBottom());
            g.setColour (juce::Colour (0xFF8090A0));
            g.drawText (juce::String (bar + 1), (int)x + 2, rulerBounds.getY(), 40, kRulerH,
                        juce::Justification::centredLeft, false);
        }
    }

    void drawKeyboard (juce::Graphics& g)
    {
        g.setColour (juce::Colour (0xFF0D0D14));
        g.fillRect (keysBounds);
        const int top = yToNote (kRulerH), bot = yToNote (gridBounds.getBottom());
        for (int note = bot; note <= top; ++note)
        {
            const float y = noteToY (note);
            if (isBlackKey (note))
            {
                g.setColour (juce::Colour (0xFF1A1A22));
                g.fillRect ((float)keysBounds.getX(), y, (float)keysBounds.getWidth() * 0.6f, (float)noteRowH - 0.5f);
            }
            if ((note % 12) == 0)
            {
                g.setColour (juce::Colour (0xFF3A4050));
                g.fillRect ((float)keysBounds.getX(), y, (float)keysBounds.getWidth(), 0.75f);
                g.setFont (DysektLookAndFeel::makeMonoFont (8.f));
                g.setColour (juce::Colour (0xFF6080A0));
                g.drawText ("C" + juce::String (note / 12 - 1),
                            keysBounds.getX(), (int)y, keysBounds.getWidth() - 2, noteRowH,
                            juce::Justification::centredRight, false);
            }
        }
        g.setColour (juce::Colour (0xFF2A3040));
        g.drawVerticalLine (keysBounds.getRight() - 1, (float)keysBounds.getY(), (float)keysBounds.getBottom());
    }

    void drawGrid (juce::Graphics& g)
    {
        g.setColour (juce::Colour (0xFF060608));
        g.fillRect (gridBounds);

        const int top = yToNote (kRulerH), bot = yToNote (gridBounds.getBottom());
        for (int note = bot; note <= top; ++note)
        {
            const float y = noteToY (note);
            if (isBlackKey (note))
            {
                g.setColour (juce::Colour (0xFF090912));
                g.fillRect ((float)gridBounds.getX(), y, (float)gridBounds.getWidth(), (float)noteRowH);
            }
            if ((note % 12) == 0)
            {
                g.setColour (juce::Colour::fromFloatRGBA (0.14f, 0.14f, 0.22f, 0.6f));
                g.fillRect ((float)gridBounds.getX(), y, (float)gridBounds.getWidth(), 0.75f);
            }
        }

        const double tpb = MidiClip::kPPQ;
        const int64_t fb = (int64_t)(scrollX / (tpb * pixelsPerTick));
        const int64_t lb = fb + (int64_t)(gridBounds.getWidth() / (tpb * pixelsPerTick)) + 2;
        for (int64_t beat = fb; beat <= lb; ++beat)
        {
            const float x = tickToX (beat * (int64_t) tpb);
            if (x < gridBounds.getX()) continue;
            g.setColour ((beat % 4) == 0 ? juce::Colour (0xFF1C2030) : juce::Colour (0xFF131320));
            g.drawVerticalLine ((int)x, (float)gridBounds.getY(), (float)gridBounds.getBottom());
        }

        // Active track colour tint on grid header
        const auto info = engine.getTrackInfo (activeTrack);
        g.setColour (info.colour.withAlpha (0.06f));
        g.fillRect (gridBounds);

        // Clip end boundary
        const float clipEndX = tickToX (engine.getLengthTicks());
        if (clipEndX >= gridBounds.getX() && clipEndX <= gridBounds.getRight())
        {
            g.setColour (juce::Colour::fromFloatRGBA (0.8f, 0.3f, 0.1f, 0.6f));
            g.drawVerticalLine ((int)clipEndX, (float)gridBounds.getY(), (float)gridBounds.getBottom());
        }
    }

    void drawNotes (juce::Graphics& g)
    {
        MidiClip* clip = engine.getClip (activeTrack);
        if (! clip) return;

        const auto trackInfo = engine.getTrackInfo (activeTrack);
        const juce::ScopedReadLock sl (clip->getLock());

        for (int i = 0; i < clip->getNotes().size(); ++i)
        {
            const auto& n = clip->getNotes().getReference (i);
            const float x = tickToX (n.startTick);
            const float y = noteToY (n.note);
            const float w = juce::jmax (2.f, (float)(n.durationTick * pixelsPerTick) - 1.f);
            const float h = (float) noteRowH - 1.f;

            if (x + w < kKeysW || x > gridBounds.getRight()) continue;

            const bool sel = selectedNotes.contains (i);
            const auto col = sel
                ? juce::Colour::fromFloatRGBA (0.25f, 0.85f, 0.85f, 1.0f)
                : trackInfo.colour.withAlpha (0.85f);

            g.setColour (col);
            g.fillRect (juce::Rectangle<float> (x, y, w, h).reduced (0.5f));

            // Velocity shading
            g.setColour (juce::Colour::fromFloatRGBA (0.f, 0.f, 0.f, (1.f - n.velocity / 127.f) * 0.4f));
            g.fillRect (juce::Rectangle<float> (x, y, w, h).reduced (0.5f));

            g.setColour (col.brighter (0.3f));
            g.drawRect (juce::Rectangle<float> (x, y, w, h).reduced (0.5f), 0.75f);

            // Resize handle
            g.setColour (col.brighter (0.5f));
            g.fillRect (juce::Rectangle<float> (x + w - 3.f, y + 1.f, 2.f, h - 2.f));
        }
    }

    void drawPlayhead (juce::Graphics& g)
    {
        const float x = tickToX (engine.getPlayheadTick());
        if (x < kKeysW || x > gridBounds.getRight()) return;
        g.setColour (juce::Colour::fromFloatRGBA (0.25f, 0.85f, 0.85f, 0.9f));
        g.drawVerticalLine ((int)x, gridBounds.getY(), gridBounds.getBottom());
        juce::Path tri;
        tri.addTriangle (x - 5.f, (float)rulerBounds.getY(),
                         x + 5.f, (float)rulerBounds.getY(),
                         x,       (float)rulerBounds.getBottom());
        g.fillPath (tri);
    }

    void drawRubberBand (juce::Graphics&) {}  // reserved

    static bool isBlackKey (int note) noexcept
    {
        const int p = note % 12;
        return p==1||p==3||p==6||p==8||p==10;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollComponent)
};
