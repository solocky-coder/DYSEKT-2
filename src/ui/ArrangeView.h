#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "TransportBar.h"
#include "TrackHeaderStrip.h"
#include "../sequencer/SequencerEngine.h"
#include "../sequencer/MidiClip.h"

//==============================================================================
//  ArrangeView  —  Cubase-style arrange window (full revision)
//
//  Interaction:
//   Ruler left-click          → seek playhead
//   Ruler click-drag          → scrub playhead
//   Ruler Alt+drag            → set loop in/out markers
//   Ruler right-click         → clear loop region
//   Clip left-click           → select clip
//   Clip drag body            → move clip (shifts all notes, committed on mouseUp)
//   Clip drag right edge      → resize clip length (8 px zone)
//   Clip double-click         → open piano roll (fires onClipDoubleClicked)
//   Clip right-click          → context menu (Mute, Rename, Duplicate, Clear, Open)
//   Empty row right-click     → context menu (Mute track, Toggle track type badge)
//   Ctrl+scroll               → horizontal zoom (centred on mouse)
//   Scroll                    → horizontal scroll
//   +/-                       → track height
//==============================================================================
class ArrangeView : public juce::Component,
                    private juce::Timer
{
public:
    static constexpr int kTransportH  = 34;
    static constexpr int kStripW      = 160;
    static constexpr int kRulerH      = 24;
    static constexpr int kMinClipPx   = 6;
    static constexpr int kResizeZone  = 8;
    static constexpr int kDefaultTrackH = 52;
    static constexpr int kMinTrackH   = 28;
    static constexpr int kMaxTrackH   = 120;

    /** Fired when user double-clicks a clip — owner should open PianoRollPanel. */
    std::function<void(int trackIndex)> onClipDoubleClicked;

    //==========================================================================
    ArrangeView (SequencerEngine& seq, AbletonLink* link = nullptr)
        : engine (seq),
          transport (seq, link),
          trackStrip (seq)
    {
        addAndMakeVisible (transport);
        addAndMakeVisible (trackStrip);

        trackStrip.onTrackSelected = [this] (int idx)
        {
            selectedTrack = idx;
            repaint();
        };
        trackStrip.onTrackMuted = [this] (int, bool) { repaint(); };

        hScroll.setRangeLimits (0.0, 1.0);
        hScroll.setCurrentRange (0.0, 0.5);
        hScroll.setAutoHide (false);
        hScroll.setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xFF2A3848));
        hScroll.addListener (this);
        addAndMakeVisible (hScroll);

        setWantsKeyboardFocus (true);
        startTimerHz (30);
    }

    ~ArrangeView() override
    {
        hScroll.removeListener (this);
        stopTimer();
    }

    //==========================================================================
    void resized() override
    {
        auto r = getLocalBounds();
        transport.setBounds (r.removeFromTop (kTransportH));

        auto hScrollR = r.removeFromBottom (14);
        hScroll.setBounds (hScrollR.withTrimmedLeft (kStripW));

        // Strip occupies full remaining height (minus ruler)
        auto leftCol = r.removeFromLeft (kStripW);
        leftCol.removeFromTop (kRulerH);    // ruler gap
        trackStrip.setBounds (leftCol);

        gridArea     = r;
        rulerBounds  = gridArea.removeFromTop (kRulerH);
        clipGridBounds = gridArea;

        updateScrollRange();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF080810));
        paintRuler    (g);
        paintTrackRows (g);
        paintPlayhead (g);
    }

    //==========================================================================
    //  Mouse
    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();

        // ── Ruler ────────────────────────────────────────────────────────────
        if (rulerBounds.contains (e.getPosition()))
        {
            if (e.mods.isRightButtonDown())
            {
                loopStart = -1; loopEnd = -1; repaint(); return;
            }
            const int64_t tick = xToTick (e.x);
            if (e.mods.isAltDown())
            {
                loopStart = tick; loopEnd = tick;
                rulerDrag = RulerDrag::LoopEnd;
            }
            else
            {
                engine.seekToTick (tick);
                rulerDrag = RulerDrag::Scrub;
            }
            repaint(); return;
        }

        // ── Clip grid ────────────────────────────────────────────────────────
        if (! clipGridBounds.contains (e.getPosition())) return;

        const int trackIdx = trackFromY (e.y);
        if (! juce::isPositiveAndBelow (trackIdx, engine.getNumTracks())) return;

        auto clipR = clipRectForTrack (trackIdx);

        if (e.mods.isRightButtonDown())
        {
            showContextMenu (trackIdx, clipR.contains (e.getPosition()));
            return;
        }

        // Near right edge → resize
        if (clipR.contains (e.getPosition()) &&
            e.x >= clipR.getRight() - kResizeZone)
        {
            dragMode        = DragMode::ResizeRight;
            dragTrack       = trackIdx;
            dragStartX      = e.x;
            dragStartTicks  = engine.getLengthTicks();
            selectedTrack   = trackIdx;
            repaint(); return;
        }

        // Clip body → move
        if (clipR.contains (e.getPosition()))
        {
            dragMode        = DragMode::MoveClip;
            dragTrack       = trackIdx;
            dragStartX      = e.x;
            dragStartTicks  = clipOffsets[trackIdx];
            selectedTrack   = trackIdx;
            repaint(); return;
        }

        // Empty space → deselect
        selectedTrack = -1;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // Ruler scrub / loop drag
        if (rulerDrag == RulerDrag::Scrub)
        {
            engine.seekToTick (juce::jmax ((int64_t)0, xToTick (e.x)));
            repaint(); return;
        }
        if (rulerDrag == RulerDrag::LoopEnd)
        {
            const int64_t tick = xToTick (e.x);
            loopEnd = juce::jmax (loopStart, tick);
            repaint(); return;
        }

        if (dragMode == DragMode::None) return;

        const double tpp = ticksPerPixel();
        const int    dx  = e.x - dragStartX;

        if (dragMode == DragMode::ResizeRight)
        {
            const int64_t newLen = juce::jmax (
                MidiClip::kPPQ,
                dragStartTicks + (int64_t)(dx * tpp));
            engine.setLengthTicks (snapTick (newLen));
            repaint(); return;
        }

        if (dragMode == DragMode::MoveClip)
        {
            // Show a ghost offset while dragging — commit on mouseUp
            const int64_t newOffset = juce::jmax ((int64_t)0,
                dragStartTicks + (int64_t)(dx * tpp));
            dragLiveOffset = snapTick (newOffset);
            repaint(); return;
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragMode == DragMode::MoveClip && dragTrack >= 0)
        {
            // Commit: shift all notes in the clip by the delta
            const int64_t oldOffset = clipOffsets[dragTrack];
            const int64_t newOffset = dragLiveOffset;
            const int64_t delta     = newOffset - oldOffset;

            if (delta != 0)
            {
                MidiClip* clip = engine.getClip (dragTrack);
                if (clip)
                {
                    const juce::ScopedReadLock sl (clip->getLock());
                    juce::Array<MidiNote> shifted = clip->getNotes();
                    for (auto& n : shifted)
                        n.startTick = juce::jmax ((int64_t)0, n.startTick + delta);
                    clip->setNotes (shifted);
                }
                clipOffsets[dragTrack] = newOffset;
            }
            dragLiveOffset = 0;
        }

        rulerDrag  = RulerDrag::None;
        dragMode   = DragMode::None;
        dragTrack  = -1;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! clipGridBounds.contains (e.getPosition())) return;

        const int trackIdx = trackFromY (e.y);
        if (! juce::isPositiveAndBelow (trackIdx, engine.getNumTracks())) return;

        if (clipRectForTrack (trackIdx).contains (e.getPosition()))
        {
            selectedTrack = trackIdx;
            trackStrip.setSelectedTrack (trackIdx);
            repaint();
            if (onClipDoubleClicked) onClipDoubleClicked (trackIdx);
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCtrlDown())
        {
            // Horizontal zoom around mouse
            const double tickAtMouse = xToTick (e.x);
            const double zf = w.deltaY > 0 ? 1.15 : (1.0 / 1.15);
            pixelsPerTick = juce::jlimit (0.005, 4.0, pixelsPerTick * zf);
            scrollX = juce::jmax (0.0, tickAtMouse * pixelsPerTick
                                       - (e.x - clipGridBounds.getX()));
        }
        else
        {
            scrollX = juce::jmax (0.0, scrollX - w.deltaY * 80.0);
        }
        updateScrollRange(); repaint();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k.getKeyCode() == '+' || k.getKeyCode() == '=')
            { trackH = juce::jlimit (kMinTrackH, kMaxTrackH, trackH + 4); trackStrip.repaint(); repaint(); return true; }
        if (k.getKeyCode() == '-')
            { trackH = juce::jlimit (kMinTrackH, kMaxTrackH, trackH - 4); trackStrip.repaint(); repaint(); return true; }
        if (k.getKeyCode() == juce::KeyPress::deleteKey && selectedTrack >= 0)
        {
            // Clear clip contents
            MidiClip* clip = engine.getClip (selectedTrack);
            if (clip) { clip->clear(); repaint(); }
            return true;
        }
        return false;
    }

private:
    //==========================================================================
    SequencerEngine&      engine;
    TransportBar          transport;
    TrackHeaderStrip      trackStrip;
    juce::ScrollBar       hScroll { false };

    juce::Rectangle<int>  gridArea, rulerBounds, clipGridBounds;

    int     selectedTrack  = -1;
    int     trackH         = kDefaultTrackH;

    // Per-track clip start offset (message thread only — not serialised yet)
    std::array<int64_t, 256> clipOffsets {};  // zero-initialised

    // Horizontal scroll / zoom
    double  pixelsPerTick  = 0.08;
    double  scrollX        = 0.0;
    int64_t dragLiveOffset = 0;

    // Loop markers (-1 = not set)
    int64_t loopStart = -1, loopEnd = -1;

    // Drag state
    enum class DragMode  { None, MoveClip, ResizeRight };
    enum class RulerDrag { None, Scrub, LoopEnd };
    DragMode  dragMode       = DragMode::None;
    RulerDrag rulerDrag      = RulerDrag::None;
    int       dragTrack      = -1;
    int       dragStartX     = 0;
    int64_t   dragStartTicks = 0;

    //==========================================================================
    //  Coordinate helpers
    //==========================================================================
    double  ticksPerPixel() const noexcept
    {
        return pixelsPerTick > 0.0 ? 1.0 / pixelsPerTick : 1.0;
    }

    int64_t xToTick (int x) const noexcept
    {
        return (int64_t) juce::jmax (0.0,
            (x - clipGridBounds.getX() + scrollX) / pixelsPerTick);
    }

    float tickToX (int64_t t) const noexcept
    {
        return (float)(t * pixelsPerTick - scrollX + clipGridBounds.getX());
    }

    int trackFromY (int y) const noexcept
    {
        return (y - clipGridBounds.getY()) / trackH;
    }

    juce::Rectangle<int> clipRectForTrack (int i) const
    {
        if (clipGridBounds.isEmpty()) return {};

        // During a live move drag, show offset preview
        const int64_t offset = (dragMode == DragMode::MoveClip && dragTrack == i)
                                ? dragLiveOffset
                                : clipOffsets[i];

        const int64_t len = engine.getLengthTicks();
        const int w   = juce::jmax (kMinClipPx, (int)(len * pixelsPerTick));
        const int x   = clipGridBounds.getX() + (int)(offset * pixelsPerTick) - (int)scrollX;
        const int y   = clipGridBounds.getY() + i * trackH;
        return { x, y, w, trackH - 1 };
    }

    int64_t snapTick (int64_t t) const noexcept
    {
        const int64_t snap = MidiClip::kPPQ;   // snap to quarter note in arrange view
        return ((t + snap / 2) / snap) * snap;
    }

    int64_t totalVisibleTicks() const noexcept
    {
        return juce::jmax (engine.getLengthTicks() * 2,
                           MidiClip::kPPQ * 4 * 16);
    }

    void updateScrollRange()
    {
        const double totalW = totalVisibleTicks() * pixelsPerTick;
        hScroll.setRangeLimits (0.0, totalW);
        hScroll.setCurrentRange (scrollX, scrollX + clipGridBounds.getWidth(),
                                 juce::dontSendNotification);
    }

    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override
    {
        scrollX = newRangeStart;
        repaint();
    }

    void timerCallback() override { repaint(); }

    //==========================================================================
    //  Context menus
    //==========================================================================
    void showContextMenu (int trackIdx, bool onClip)
    {
        juce::PopupMenu m;
        const auto info = engine.getTrackInfo (trackIdx);

        if (onClip)
        {
            m.addItem (1, "Open in piano roll");
            m.addSeparator();
            m.addItem (2, info.enabled ? "Mute track" : "Unmute track");
            m.addItem (3, "Clear clip contents");
            m.addItem (4, "Duplicate clip to next track");
            m.addSeparator();
            m.addItem (5, "Rename track…");
        }
        else
        {
            m.addItem (2, info.enabled ? "Mute track" : "Unmute track");
            m.addItem (5, "Rename track…");
        }

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
            [this, trackIdx, info](int result)
            {
                switch (result)
                {
                    case 1:
                        selectedTrack = trackIdx;
                        trackStrip.setSelectedTrack (trackIdx);
                        if (onClipDoubleClicked) onClipDoubleClicked (trackIdx);
                        break;
                    case 2:
                        engine.setTrackEnabled (trackIdx, ! info.enabled);
                        break;
                    case 3:
                    {
                        MidiClip* c = engine.getClip (trackIdx);
                        if (c) c->clear();
                        break;
                    }
                    case 4:
                        duplicateClipToNextTrack (trackIdx);
                        break;
                    case 5:
                        promptRenameTrack (trackIdx);
                        break;
                    default: break;
                }
                repaint(); trackStrip.repaint();
            });
    }

    void duplicateClipToNextTrack (int srcIdx)
    {
        // Copy notes from srcIdx clip into the next track's clip
        const int dstIdx = srcIdx + 1;
        MidiClip* src = engine.getClip (srcIdx);
        MidiClip* dst = engine.getClip (dstIdx);
        if (! src || ! dst) return;
        const juce::ScopedReadLock sl (src->getLock());
        dst->setNotes (src->getNotes());
        repaint();
    }

    void promptRenameTrack (int /*trackIdx*/)
    {
        // Rename is driven by TrackHeaderStrip in the full UI;
        // here we just flash the strip so the user knows where to double-click.
        trackStrip.repaint();
    }

    //==========================================================================
    //  Painting
    //==========================================================================
    void paintRuler (juce::Graphics& g) const
    {
        // Background
        g.setColour (juce::Colour (0xFF0C0C18));
        g.fillRect (rulerBounds);

        // Strip gap fill (left of clip area)
        g.setColour (juce::Colour (0xFF0A0A14));
        g.fillRect (rulerBounds.withWidth (kStripW));

        g.setColour (juce::Colour (0xFF1C2030));
        g.fillRect (rulerBounds.getX(), rulerBounds.getBottom() - 1,
                    rulerBounds.getWidth(), 1);

        // Loop region
        if (loopStart >= 0 && loopEnd > loopStart)
        {
            const float lx = tickToX (loopStart);
            const float rx = tickToX (loopEnd);
            g.setColour (juce::Colour::fromFloatRGBA (0.25f, 0.85f, 0.85f, 0.18f));
            g.fillRect (lx, (float)rulerBounds.getY(), rx - lx, (float)rulerBounds.getHeight());
            g.setColour (juce::Colour::fromFloatRGBA (0.25f, 0.85f, 0.85f, 0.7f));
            g.drawVerticalLine ((int)lx, (float)rulerBounds.getY(), (float)rulerBounds.getBottom());
            g.drawVerticalLine ((int)rx, (float)rulerBounds.getY(), (float)rulerBounds.getBottom());

            // "L" / "R" labels
            g.setFont (juce::Font (9.f, juce::Font::bold));
            g.setColour (juce::Colour::fromFloatRGBA (0.25f, 0.85f, 0.85f, 0.9f));
            g.drawText ("L", (int)lx + 2, rulerBounds.getY(), 12, rulerBounds.getHeight(), juce::Justification::centredLeft);
            g.drawText ("R", (int)rx - 14, rulerBounds.getY(), 12, rulerBounds.getHeight(), juce::Justification::centredRight);
        }

        // Bar ticks + labels
        const int64_t ppq    = MidiClip::kPPQ;
        const int64_t barLen = ppq * 4;
        const int64_t total  = totalVisibleTicks();
        const int     gx     = clipGridBounds.getX();
        const int     gw     = clipGridBounds.getWidth();

        // Beat sub-ticks
        const int64_t firstBeat = (int64_t)(scrollX / (pixelsPerTick * ppq));
        const int64_t lastBeat  = firstBeat + (int64_t)(gw / (pixelsPerTick * ppq)) + 2;
        for (int64_t beat = firstBeat; beat <= lastBeat && beat * ppq <= total; ++beat)
        {
            const int x = gx + (int)((beat * ppq) * pixelsPerTick - scrollX);
            if (x < gx || x > gx + gw) continue;
            const bool isBar = (beat % 4 == 0);
            g.setColour (isBar ? juce::Colour (0xFF2A3448) : juce::Colour (0xFF181E2A));
            g.fillRect (x, rulerBounds.getY(),
                        1, isBar ? rulerBounds.getHeight() : rulerBounds.getHeight() / 2);
        }

        // Bar numbers
        g.setFont (juce::Font (10.f, juce::Font::bold));
        const int64_t firstBar = (int64_t)(scrollX / (pixelsPerTick * barLen));
        const int64_t lastBar  = firstBar + (int64_t)(gw / (pixelsPerTick * barLen)) + 2;
        for (int64_t bar = firstBar; bar <= lastBar && bar * barLen <= total; ++bar)
        {
            const int x = gx + (int)((bar * barLen) * pixelsPerTick - scrollX);
            if (x < gx || x > gx + gw) continue;
            g.setColour (juce::Colour (0xFF6A7A90));
            g.drawText (juce::String (bar + 1),
                        x + 3, rulerBounds.getY(),
                        40, rulerBounds.getHeight(),
                        juce::Justification::centredLeft, false);
        }
    }

    void paintTrackRows (juce::Graphics& g) const
    {
        const int n = engine.getNumTracks();
        for (int i = 0; i < n; ++i)
            paintOneTrack (g, i);
    }

    void paintOneTrack (juce::Graphics& g, int i) const
    {
        if (clipGridBounds.isEmpty()) return;

        const auto info  = engine.getTrackInfo (i);
        const bool isSel = (i == selectedTrack);
        const bool muted = ! info.enabled;

        const juce::Rectangle<int> rowR (
            clipGridBounds.getX(),
            clipGridBounds.getY() + i * trackH,
            clipGridBounds.getWidth(),
            trackH);

        // Row background
        g.setColour (isSel ? juce::Colour (0xFF0E1624)
                           : juce::Colour (0xFF090912));
        g.fillRect (rowR);

        // Row separator
        g.setColour (juce::Colour (0xFF181E28));
        g.fillRect (rowR.getX(), rowR.getBottom() - 1, rowR.getWidth(), 1);

        // Vertical grid lines (bars)
        {
            const int64_t ppq    = MidiClip::kPPQ;
            const int64_t barLen = ppq * 4;
            const int64_t total  = totalVisibleTicks();
            const int64_t firstBeat = (int64_t)(scrollX / (pixelsPerTick * ppq));
            const int64_t lastBeat  = firstBeat + (int64_t)(rowR.getWidth() / (pixelsPerTick * ppq)) + 2;

            for (int64_t beat = firstBeat; beat <= lastBeat && beat * ppq <= total; ++beat)
            {
                const int gx = clipGridBounds.getX() + (int)((beat * ppq) * pixelsPerTick - scrollX);
                if (gx < rowR.getX() || gx > rowR.getRight()) continue;
                const bool isBar = (beat % 4 == 0);
                g.setColour (isBar ? juce::Colour (0xFF161E2C) : juce::Colour (0xFF101620));
                g.fillRect (gx, rowR.getY(), 1, rowR.getHeight() - 1);
            }
        }

        // ── Clip rectangle ───────────────────────────────────────────────────
        auto clipR = clipRectForTrack (i);
        if (! rowR.intersects (clipR)) return;

        const juce::Colour clipBase = muted
            ? info.colour.withSaturation (0.1f).withBrightness (0.25f)
            : info.colour;

        // Clip background fill (gradient feel)
        {
            juce::ColourGradient grad (
                clipBase.withAlpha (0.35f), (float)clipR.getX(), (float)clipR.getY(),
                clipBase.withAlpha (0.18f), (float)clipR.getX(), (float)clipR.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (clipR.toFloat().reduced (1.f, 2.f), 4.f);
        }

        // Selection glow border
        if (isSel)
        {
            g.setColour (clipBase.brighter (0.6f).withAlpha (0.9f));
            g.drawRoundedRectangle (clipR.toFloat().reduced (1.f, 2.f), 4.f, 1.5f);
        }
        else
        {
            g.setColour (clipBase.withAlpha (muted ? 0.3f : 0.65f));
            g.drawRoundedRectangle (clipR.toFloat().reduced (1.f, 2.f), 4.f, 1.f);
        }

        // Mute overlay hatching
        if (muted)
        {
            g.setColour (juce::Colour::fromFloatRGBA (0.f, 0.f, 0.f, 0.35f));
            g.fillRoundedRectangle (clipR.toFloat().reduced (1.f, 2.f), 4.f);
        }

        // Clip header bar (coloured strip along top)
        const juce::Rectangle<float> headerBar (
            clipR.toFloat().reduced (1.f, 2.f).withHeight (juce::jmin (6.f, (float)trackH * 0.12f)));
        g.setColour (clipBase.withAlpha (muted ? 0.3f : 0.8f));
        g.fillRoundedRectangle (headerBar, 2.f);

        // Track name inside clip header
        {
            g.setFont (juce::Font (juce::jmin (10.f, (float)trackH * 0.2f), juce::Font::bold));
            g.setColour (muted ? juce::Colour (0xFF445566)
                               : juce::Colours::white.withAlpha (0.85f));
            g.drawText (info.name,
                        clipR.getX() + 6, clipR.getY(),
                        juce::jmax (0, clipR.getWidth() - 12), (int)(trackH * 0.38f),
                        juce::Justification::centredLeft, true);
        }

        // Track type badge (SL / CH / SF)
        {
            juce::String badge;
            switch (info.type)
            {
                case TrackType::MainSlice:      badge = "SL"; break;
                case TrackType::ChromaticSlice: badge = "CH"; break;
                case TrackType::SfPlayer:       badge = "SF"; break;
            }
            g.setFont (juce::Font (8.f));
            g.setColour (clipBase.withAlpha (0.55f));
            g.drawText (badge,
                        clipR.getRight() - 20, clipR.getY() + 2,
                        18, 12,
                        juce::Justification::centredRight, false);
        }

        // Resize handle on right edge
        {
            const juce::Rectangle<float> handleR (
                clipR.toFloat().reduced(1.f, 2.f).withLeft(clipR.getRight() - 7.f));
            g.setColour (clipBase.withAlpha (0.45f));
            g.fillRoundedRectangle (handleR, 2.f);
            // Grip dots
            g.setColour (clipBase.brighter(0.5f).withAlpha(0.5f));
            for (int dot = 0; dot < 3; ++dot)
            {
                const float dy = clipR.getY() + clipR.getHeight() * 0.25f + dot * clipR.getHeight() * 0.25f;
                g.fillEllipse (clipR.getRight() - 5.f, dy - 1.f, 2.f, 2.f);
            }
        }

        // ── Mini piano-roll note preview ─────────────────────────────────────
        paintNotePreview (g, i, clipR, clipBase, muted);
    }

    void paintNotePreview (juce::Graphics& g, int trackIdx,
                           juce::Rectangle<int> clipR,
                           juce::Colour clipBase, bool muted) const
    {
        const MidiClip* clip = engine.getClip (trackIdx);
        if (! clip) return;

        const int64_t clipLen = clip->getLengthTicks();
        if (clipLen <= 0) return;

        // Note preview area (below header strip, above bottom padding)
        const int headerH  = (int)(trackH * 0.38f);
        const int previewY = clipR.getY() + headerH;
        const int previewH = clipR.getHeight() - headerH - 3;
        if (previewH < 4) return;

        // Clip to the clip rectangle
        g.saveState();
        g.reduceClipRegion (clipR.withTrimmedTop(headerH).withTrimmedBottom(2));

        {
            const juce::ScopedReadLock sl (clip->getLock());
            const auto& notes = clip->getNotes();

            // Find pitch range for this clip
            int loNote = 127, hiNote = 0;
            for (const auto& n : notes)
            {
                loNote = juce::jmin (loNote, n.note);
                hiNote = juce::jmax (hiNote, n.note);
            }
            if (notes.isEmpty()) { g.restoreState(); return; }

            const int range = juce::jmax (12, hiNote - loNote + 2);

            for (const auto& n : notes)
            {
                if (n.startTick >= clipLen) continue;

                const float nx = clipR.getX()
                    + (float)(n.startTick) / (float)clipLen * clipR.getWidth();
                const float nw = juce::jmax (1.5f,
                    (float)(n.durationTick) / (float)clipLen * clipR.getWidth() - 0.5f);

                const float pitch = (float)(n.note - loNote) / (float)range;
                const float ny = (float)previewY + (1.f - pitch) * (float)(previewH - 3);
                const float nh = juce::jmax (1.5f, (float)previewH / (float)range);

                const float velAlpha = muted ? 0.25f : (0.4f + 0.5f * n.velocity / 127.f);
                g.setColour (clipBase.brighter (0.2f).withAlpha (velAlpha));
                g.fillRoundedRectangle (nx, ny, nw, nh, 0.5f);
            }
        }

        g.restoreState();
    }

    void paintPlayhead (juce::Graphics& g) const
    {
        if (clipGridBounds.isEmpty()) return;

        const int64_t tick  = engine.getPlayheadTick();
        const int64_t total = totalVisibleTicks();
        if (tick < 0 || tick > total) return;

        const int x = clipGridBounds.getX() + (int)(tick * pixelsPerTick - scrollX);
        if (x < clipGridBounds.getX() || x > clipGridBounds.getRight()) return;

        // Orange line through tracks
        g.setColour (juce::Colour::fromFloatRGBA (0.9f, 0.5f, 0.15f, 0.8f));
        g.fillRect (x, rulerBounds.getY(), 1, rulerBounds.getHeight() + clipGridBounds.getHeight());

        // Triangle in ruler
        juce::Path tri;
        tri.addTriangle ((float)x, (float)rulerBounds.getBottom(),
                         (float)x - 5.f, (float)rulerBounds.getY() + 4.f,
                         (float)x + 5.f, (float)rulerBounds.getY() + 4.f);
        g.setColour (juce::Colour::fromFloatRGBA (0.9f, 0.5f, 0.15f, 0.95f));
        g.fillPath (tri);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangeView)
};
