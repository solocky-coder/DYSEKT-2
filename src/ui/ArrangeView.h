#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "TransportBar.h"
#include "TrackHeaderStrip.h"
#include "../sequencer/SequencerEngine.h"
#include "../sequencer/MidiClip.h"

//==============================================================================
//  ArrangeView  —  Cubase-style arrange window
//
//  Ruler:
//   Left-click / drag         → seek + scrub playhead
//   Alt + drag                → set loop in/out (L/R markers)
//   Right-click               → clear loop markers
//
//  Clip body:
//   Left-click                → select track
//   Drag                      → move clip (ghost preview, committed on mouseUp)
//   Drag right edge (8 px)    → resize clip length
//   Double-click              → open piano roll
//   Right-click               → context menu
//
//  Empty space:
//   Left-click                → deselect
//   Right-click               → track context menu
//
//  Keyboard:
//   Space                     → play / stop toggle
//   Home / Numpad 0           → rewind to bar 1
//   +/-                       → increase / decrease track height
//   Delete                    → clear selected clip contents
//
//  Scroll / zoom:
//   Ctrl + scroll             → horizontal zoom (centred on mouse)
//   Shift + scroll            → fast horizontal scroll
//   Scroll                    → horizontal scroll
//   Vertical scrollbar        → track rows scroll (when > screen height)
//
//  Auto-scroll:
//   Playhead auto-follows during playback (Cubase-style page scroll)
//==============================================================================
class ArrangeView : public juce::Component,
                    private juce::Timer,
                    private juce::ScrollBar::Listener
{
public:
    static constexpr int kTransportH   = 34;
    static constexpr int kStripW       = 160;
    static constexpr int kRulerH       = 28;
    static constexpr int kScrollH      = 14;
    static constexpr int kScrollW      = 14;
    static constexpr int kMinClipPx    = 6;
    static constexpr int kResizeZone   = 10;
    static constexpr int kDefaultTrackH = 54;
    static constexpr int kMinTrackH    = 24;
    static constexpr int kMaxTrackH    = 140;

    /** Owner wires this to open the piano roll for the given track + clip. */
    std::function<void(int trackIndex, int clipIndex)> onClipDoubleClicked;

    /** Fired whenever the selected track changes.
     *  @param type        Track type of the newly selected track.
     *  @param hasSelection  false when nothing is selected (deselect / empty view). */
    std::function<void(TrackType type, bool hasSelection)> onTrackTypeSelected;

    /** Re-fires onTrackTypeSelected for the currently selected track.
     *  Call this when opening the sequencer panel so the editor can
     *  apply the correct MIDI route mode for whatever track is already selected. */
    void notifyCurrentTrack()
    {
        if (onTrackTypeSelected)
        {
            if (juce::isPositiveAndBelow (selectedTrack, engine.getNumTracks()))
                onTrackTypeSelected (engine.getTrackInfo (selectedTrack).type, true);
            else
                onTrackTypeSelected (TrackType::MainSlice, false);
        }
    }

    //==========================================================================
    ArrangeView (SequencerEngine& seq, AbletonLink* link = nullptr)
        : engine (seq),
          transport (seq, link),
          trackStrip (seq)
    {
        addAndMakeVisible (transport);
        addAndMakeVisible (trackStrip);

        // ── Horizontal scrollbar ──────────────────────────────────────────────
        hScroll.setRangeLimits (0.0, 1.0);
        hScroll.setCurrentRange (0.0, 0.5);
        hScroll.setAutoHide (false);
        styleScrollBar (hScroll);
        hScroll.addListener (this);
        addAndMakeVisible (hScroll);

        // ── Vertical scrollbar ────────────────────────────────────────────────
        vScroll.setRangeLimits (0.0, 1.0);
        vScroll.setCurrentRange (0.0, 1.0);
        vScroll.setAutoHide (false);
        styleScrollBar (vScroll);
        vScroll.addListener (this);
        addAndMakeVisible (vScroll);

        // ── Track-strip callbacks ─────────────────────────────────────────────
        trackStrip.onTrackSelected = [this] (int idx)
        {
            selectTrack (idx);
            repaint();
        };
        trackStrip.onTrackMuted = [this] (int, bool) { repaint(); };

        setWantsKeyboardFocus (true);
        startTimerHz (30);
    }

    ~ArrangeView() override
    {
        hScroll.removeListener (this);
        vScroll.removeListener (this);
        stopTimer();
    }

    //==========================================================================
    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        transport.setBounds (r.removeFromTop (kTransportH));

        // Corner square between the two scrollbars
        auto cornerR = r;
        cornerR = cornerR.removeFromBottom (kScrollH).removeFromRight (kScrollW);

        auto hScrollR = r.removeFromBottom (kScrollH).withTrimmedRight (kScrollW);
        auto vScrollR = r.removeFromRight  (kScrollW);

        hScroll.setBounds (hScrollR.withTrimmedLeft (kStripW));
        vScroll.setBounds (vScrollR);

        auto leftCol = r.removeFromLeft (kStripW);
        leftCol.removeFromTop (kRulerH);
        trackStrip.setBounds (leftCol);
        trackStrip.setTrackHeight (trackH);

        gridArea      = r;
        rulerBounds   = gridArea.removeFromTop (kRulerH);
        clipGridBounds = gridArea;

        updateScrollRanges();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& theme = getTheme();
        const auto ac = theme.accent;
        auto b = getLocalBounds();

        // ── LCD-style frame — identical recipe to MixerPanel ─────────────────
        {
            juce::ColourGradient outerGrad (juce::Colour (0xFF131313), 0, 0,
                                             juce::Colour (0xFF0E0E0E), 0, (float) b.getHeight(), false);
            g.setGradientFill (outerGrad);
            g.fillRoundedRectangle (b.toFloat(), 4.0f);

            g.setColour (ac.withAlpha (0.65f));
            g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 4.0f, 1.0f);

            auto screen = b.reduced (4);
            g.setColour (theme.darkBar.darker (0.55f));
            g.fillRoundedRectangle (screen.toFloat(), 2.0f);

            g.setColour (juce::Colours::black.withAlpha (0.18f));
            for (int y = screen.getY(); y < screen.getBottom(); y += 2)
                g.drawHorizontalLine (y, (float) screen.getX(), (float) screen.getRight());

            juce::ColourGradient glow (ac.withAlpha (0.06f), 0, (float) screen.getY(),
                                        juce::Colours::transparentBlack, 0, (float) (screen.getY() + 20), false);
            g.setGradientFill (glow);
            g.fillRoundedRectangle (screen.toFloat(), 2.0f);

            g.setColour (ac.withAlpha (0.12f));
            g.drawRoundedRectangle (screen.toFloat().expanded (0.5f), 2.0f, 1.0f);
        }

        // Clip all track content to the inner screen rect
        g.saveState();
        g.reduceClipRegion (b.reduced (4));

        paintRuler (g);
        paintTrackRows (g);
        paintLoopOverlay (g);
        paintPlayhead (g);

        // Corner fill between scrollbars
        if (getWidth() > kStripW + 8 && getHeight() > kTransportH + kScrollH + 8)
        {
            g.setColour (juce::Colour (0xFF0A0A14));
            g.fillRect (getWidth() - kScrollW - 4,
                        getHeight() - kScrollH - 4,
                        kScrollW, kScrollH);
        }

        g.restoreState();
    }

    //==========================================================================
    //  Mouse
    //==========================================================================
    void mouseMove (const juce::MouseEvent& e) override
    {
        updateCursor (e);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();

        // ── Ruler ─────────────────────────────────────────────────────────────
        if (rulerBounds.contains (e.getPosition()))
        {
            if (e.mods.isRightButtonDown())
            {
                loopStart = -1; loopEnd = -1;
                repaint(); return;
            }
            const int64_t tick = xToTick (e.x);
            if (e.mods.isAltDown())
            {
                loopStart = tick; loopEnd = tick;
                rulerDrag = RulerDrag::LoopSet;
                loopDragAnchor = tick;
            }
            else
            {
                engine.seekToTick (juce::jmax ((int64_t)0, tick));
                rulerDrag = RulerDrag::Scrub;
            }
            repaint(); return;
        }

        // ── Clip grid ─────────────────────────────────────────────────────────
        if (! clipGridBounds.contains (e.getPosition())) return;

        const int trackIdx = trackFromY (e.y);
        if (! juce::isPositiveAndBelow (trackIdx, engine.getNumTracks())) return;

        // Hit test all clips on this track
        const int numClips = engine.getNumClips (trackIdx);
        int hitClip = -1;
        juce::Rectangle<int> hitRect;
        for (int ci = 0; ci < numClips; ++ci)
        {
            const auto r = clipRectForClip (trackIdx, ci);
            if (r.contains (e.getPosition())) { hitClip = ci; hitRect = r; break; }
        }
        const bool onClip = (hitClip >= 0);

        if (e.mods.isRightButtonDown())
        {
            showContextMenu (trackIdx, hitClip, e);
            return;
        }

        // Resize handle — right edge of a clip
        if (onClip && e.x >= hitRect.getRight() - kResizeZone)
        {
            dragMode       = DragMode::ResizeRight;
            dragTrack      = trackIdx;
            dragClip       = hitClip;
            dragStartX     = e.x;
            dragStartTicks = engine.getClipInfo (trackIdx, hitClip).lengthTicks;
            dragResizeLen  = dragStartTicks;
            selectTrack (trackIdx);
            selectedClip   = hitClip;
            updateCursor (e);
            repaint(); return;
        }

        // Clip body — move
        if (onClip)
        {
            dragMode       = DragMode::MoveClip;
            dragTrack      = trackIdx;
            dragClip       = hitClip;
            dragStartX     = e.x;
            dragStartTicks = engine.getClipInfo (trackIdx, hitClip).startTick;
            dragLiveOffset = dragStartTicks;
            selectTrack (trackIdx);
            selectedClip   = hitClip;
            updateCursor (e);
            repaint(); return;
        }

        // Empty track space — create new clip at click position
        {
            const int64_t clickTick = snapTick (xToTick (e.x));
            const int64_t defLen    = MidiClip::kPPQ * 4 * 4;  // 4 bars default
            const int newIdx = engine.addClip (trackIdx, clickTick, defLen);
            selectTrack (trackIdx);
            selectedClip  = newIdx;
            repaint(); return;
        }
    }   // end mouseDown

    void mouseDrag (const juce::MouseEvent& e) override
    {
        // ── Ruler scrub / loop drag ────────────────────────────────────────────
        if (rulerDrag == RulerDrag::Scrub)
        {
            engine.seekToTick (juce::jmax ((int64_t)0, xToTick (e.x)));
            repaint(); return;
        }

        if (rulerDrag == RulerDrag::LoopSet)
        {
            const int64_t tick = xToTick (e.x);
            if (tick >= loopDragAnchor)
            {
                loopStart = loopDragAnchor;
                loopEnd   = tick;
            }
            else
            {
                loopStart = tick;
                loopEnd   = loopDragAnchor;
            }
            repaint(); return;
        }

        if (dragMode == DragMode::None) return;

        const int dx = e.x - dragStartX;

        if (dragMode == DragMode::ResizeRight)
        {
            const int64_t newLen = juce::jmax (
                MidiClip::kPPQ,
                dragStartTicks + (int64_t)(dx * ticksPerPixel()));
            dragResizeLen = snapTick (newLen);
            repaint(); return;
        }

        if (dragMode == DragMode::MoveClip)
        {
            const int64_t newOff = juce::jmax ((int64_t)0,
                dragStartTicks + (int64_t)(dx * ticksPerPixel()));
            dragLiveOffset = snapTick (newOff);

            // Auto-scroll: nudge scrollX if near edges
            const int margin = 40;
            if (e.x < clipGridBounds.getX() + margin)
                scrollX = juce::jmax (0.0, scrollX - 8.0);
            else if (e.x > clipGridBounds.getRight() - margin)
                scrollX = scrollX + 8.0;

            updateScrollRanges();
            repaint(); return;
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        // Commit clip resize
        if (dragMode == DragMode::ResizeRight && dragTrack >= 0)
        {
            engine.setClipLengthTicks (dragTrack, dragClip, dragResizeLen);
            dragResizeLen = 0;
        }

        // Commit clip move
        if (dragMode == DragMode::MoveClip && dragTrack >= 0)
        {
            if (dragLiveOffset != engine.getClipInfo (dragTrack, dragClip).startTick)
            {
                // Shift all notes within the clip are clip-relative, so just move the slot
                engine.setClipStartTick (dragTrack, dragClip, dragLiveOffset);
                // Re-find clip index after sort (sortClips may have changed order)
                // Use dragLiveOffset to locate it
                for (int ci = 0; ci < engine.getNumClips (dragTrack); ++ci)
                    if (engine.getClipInfo (dragTrack, ci).startTick == dragLiveOffset)
                        { selectedClip = ci; break; }
            }
            dragLiveOffset = 0;
        }

        rulerDrag  = RulerDrag::None;
        dragMode   = DragMode::None;
        dragTrack  = -1;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! clipGridBounds.contains (e.getPosition())) return;
        const int trackIdx = trackFromY (e.y);
        if (! juce::isPositiveAndBelow (trackIdx, engine.getNumTracks())) return;

        for (int ci = 0; ci < engine.getNumClips (trackIdx); ++ci)
        {
            if (clipRectForClip (trackIdx, ci).contains (e.getPosition()))
            {
                selectTrack (trackIdx);
                selectedClip  = ci;
                trackStrip.setSelectedTrack (trackIdx);
                repaint();
                if (onClipDoubleClicked) onClipDoubleClicked (trackIdx, ci);
                return;
            }
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCtrlDown())
        {
            // Zoom around mouse position
            const double tickAtMouse = xToTick (e.x);
            const double factor      = w.deltaY > 0 ? 1.18 : (1.0 / 1.18);
            pixelsPerTick = juce::jlimit (0.003, 6.0, pixelsPerTick * factor);
            scrollX = juce::jmax (0.0,
                tickAtMouse * pixelsPerTick - (e.x - clipGridBounds.getX()));
        }
        else if (e.mods.isShiftDown())
        {
            scrollX = juce::jmax (0.0, scrollX - w.deltaY * 200.0);
        }
        else if (e.mods.isAltDown())
        {
            // Vertical scroll with Alt
            const int totalH = engine.getNumTracks() * trackH;
            const int viewH  = clipGridBounds.getHeight();
            scrollY = juce::jlimit (0, juce::jmax (0, totalH - viewH),
                                    scrollY - (int)(w.deltaY * 40.0));
        }
        else
        {
            scrollX = juce::jmax (0.0, scrollX - w.deltaY * 80.0);
        }
        updateScrollRanges();
        repaint();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::spaceKey)
        {
            if (engine.isPlaying()) engine.stop();
            else                    engine.play();
            return true;
        }

        if (k == juce::KeyPress::homeKey ||
            k.getKeyCode() == juce::KeyPress::numberPad0)
        {
            engine.rewind();
            scrollX = 0.0;
            updateScrollRanges();
            repaint();
            return true;
        }

        if (k.getKeyCode() == '+' || k.getKeyCode() == '=')
        {
            trackH = juce::jlimit (kMinTrackH, kMaxTrackH, trackH + 6);
            trackStrip.setTrackHeight (trackH);
            updateScrollRanges(); trackStrip.repaint(); repaint();
            return true;
        }
        if (k.getKeyCode() == '-')
        {
            trackH = juce::jlimit (kMinTrackH, kMaxTrackH, trackH - 6);
            trackStrip.setTrackHeight (trackH);
            updateScrollRanges(); trackStrip.repaint(); repaint();
            return true;
        }

        if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
            && selectedTrack >= 0)
        {
            if (MidiClip* c = engine.getClip (selectedTrack, selectedClip))
                c->clear();
            repaint();
            return true;
        }

        return false;
    }

private:
    //==========================================================================
    //  State
    //==========================================================================
    SequencerEngine&      engine;
    TransportBar          transport;
    TrackHeaderStrip      trackStrip;
    juce::ScrollBar       hScroll { false };
    juce::ScrollBar       vScroll { true  };

    juce::Rectangle<int>  gridArea, rulerBounds, clipGridBounds;

    int      selectedTrack  = -1;
    int      trackH         = kDefaultTrackH;

    // Horizontal scroll / zoom
    double   pixelsPerTick  = 0.08;
    double   scrollX        = 0.0;

    // Vertical scroll (pixels from top of track list)
    int      scrollY        = 0;

    // Per-track clip start offsets (arrange-view only, not serialised)
    // clip offsets are now stored per ClipSlot inside the engine

    // Loop markers (-1 = unset)
    int64_t  loopStart      = -1;
    int64_t  loopEnd        = -1;
    int64_t  loopDragAnchor = 0;

    // Playhead auto-scroll
    int64_t  lastAutoScrollTick = -1;

    // Drag state
    enum class DragMode  { None, MoveClip, ResizeRight };
    enum class RulerDrag { None, Scrub, LoopSet };
    DragMode  dragMode       = DragMode::None;
    RulerDrag rulerDrag      = RulerDrag::None;
    int       dragTrack      = -1;
    int       dragClip       = -1;   // which clip slot is being dragged/resized
    int       dragStartX     = 0;
    int64_t   dragStartTicks = 0;
    int64_t   dragLiveOffset = 0;
    int64_t   dragResizeLen  = 0;   // live preview length during ResizeRight

    int       selectedClip   = 0;   // which clip is selected on selectedTrack

    //==========================================================================
    //  Timer — repaints + auto-scroll
    //==========================================================================
    void timerCallback() override
    {
        if (engine.isPlaying())
            autoScrollToPlayhead();
        repaint();
    }

    void autoScrollToPlayhead()
    {
        const int64_t tick = engine.getPlayheadTick();
        if (tick == lastAutoScrollTick) return;
        lastAutoScrollTick = tick;

        const float px = tickToX (tick);
        const int   right = clipGridBounds.getRight();
        const int   left  = clipGridBounds.getX();

        // Page-scroll: if playhead goes past the right edge, jump one page
        if (px > right - 20)
        {
            scrollX += clipGridBounds.getWidth() * 0.85;
            updateScrollRanges();
        }
        // Snap back if rewound past left edge
        else if (px < left && scrollX > 0)
        {
            scrollX = juce::jmax (0.0, tick * pixelsPerTick - 40.0);
            updateScrollRanges();
        }
    }

    //==========================================================================
    //  Coordinate helpers
    //==========================================================================
    double ticksPerPixel() const noexcept
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
        return (y - clipGridBounds.getY() + scrollY) / trackH;
    }

    int trackTopY (int i) const noexcept
    {
        return clipGridBounds.getY() + i * trackH - scrollY;
    }

    juce::Rectangle<int> clipRectForClip (int trackIdx, int clipIdx) const
    {
        if (clipGridBounds.isEmpty()) return {};
        const auto info = engine.getClipInfo (trackIdx, clipIdx);

        int64_t startTick  = info.startTick;
        int64_t lengthTicks = info.lengthTicks;

        // Live overrides during drag
        if (dragMode == DragMode::MoveClip && dragTrack == trackIdx && dragClip == clipIdx)
            startTick = dragLiveOffset;
        if (dragMode == DragMode::ResizeRight && dragTrack == trackIdx && dragClip == clipIdx)
            lengthTicks = dragResizeLen;

        const int w = juce::jmax (kMinClipPx, (int)(lengthTicks * pixelsPerTick));
        const int x = clipGridBounds.getX() + (int)(startTick * pixelsPerTick - scrollX);
        const int y = trackTopY (trackIdx);
        return { x, y, w, trackH - 1 };
    }

    int64_t snapTick (int64_t t) const noexcept
    {
        const int64_t snap = MidiClip::kPPQ;  // quarter-note snap
        return ((t + snap / 2) / snap) * snap;
    }

    int64_t totalVisibleTicks() const noexcept
    {
        int64_t maxEnd = MidiClip::kPPQ * 4 * 4;
        for (int ti = 0; ti < engine.getNumTracks(); ++ti)
            for (int ci = 0; ci < engine.getNumClips (ti); ++ci)
            {
                const auto info = engine.getClipInfo (ti, ci);
                maxEnd = juce::jmax (maxEnd, info.endTick());
            }
        return juce::jmax (maxEnd * 2, MidiClip::kPPQ * 4 * 32);
    }

    //==========================================================================
    //  Scrollbars
    //==========================================================================
    /** Set the selected track index and update the SfzPlayer's live input channel mask.
     *  If the selected track is an SF2/SFZ track, its FluidSynth channel receives
     *  live controller (ch-1) input.  Any other track type clears the mask (silence). */
    void selectTrack (int idx)
    {
        selectedTrack = idx;

        uint16_t mask = 0;
        TrackType type = TrackType::MainSlice;
        bool hasSelection = juce::isPositiveAndBelow (idx, engine.getNumTracks());

        int liveCh = 0;  // 0 = disabled (SfPlayer handles its own mask)

        if (hasSelection)
        {
            const auto info = engine.getTrackInfo (idx);
            type = info.type;
            switch (info.type)
            {
                case TrackType::MainSlice:
                    liveCh = 1;  // slicer always responds on ch 1
                    break;
                case TrackType::ChromaticSlice:
                    liveCh = info.midiChannel + 1;  // stored 0-based
                    break;
                case TrackType::SfPlayer:
                    liveCh = 0;  // SfPlayer uses liveInputChannelMask instead
                    if (info.midiChannel >= 0 && info.midiChannel < 16)
                        mask = (uint16_t)(1u << info.midiChannel);
                    break;
            }
        }

        engine.setSelectedLiveChannel (liveCh);
        engine.setSelectedSfLiveChannels (mask);
        engine.setRecordingTrack (hasSelection ? idx : -1);

        if (onTrackTypeSelected)
            onTrackTypeSelected (type, hasSelection);
    }

    static void styleScrollBar (juce::ScrollBar& sb)
    {
        sb.setColour (juce::ScrollBar::backgroundColourId, juce::Colour (0xFF0A0A14));
        sb.setColour (juce::ScrollBar::thumbColourId,      juce::Colour (0xFF2A3848));
        sb.setColour (juce::ScrollBar::trackColourId,      juce::Colour (0xFF111820));
    }

    void updateScrollRanges()
    {
        // Horizontal
        const double totalW = totalVisibleTicks() * pixelsPerTick;
        hScroll.setRangeLimits (0.0, totalW);
        hScroll.setCurrentRange (scrollX,
                                 scrollX + clipGridBounds.getWidth(),
                                 juce::dontSendNotification);

        // Vertical
        const int totalH = engine.getNumTracks() * trackH;
        const int viewH  = clipGridBounds.getHeight();
        scrollY = juce::jlimit (0, juce::jmax (0, totalH - viewH), scrollY);
        vScroll.setRangeLimits (0.0, (double)juce::jmax (viewH, totalH));
        vScroll.setCurrentRange ((double)scrollY, (double)(scrollY + viewH),
                                 juce::dontSendNotification);
    }

    void scrollBarMoved (juce::ScrollBar* sb, double newRangeStart) override
    {
        if (sb == &hScroll)
            scrollX = newRangeStart;
        else
            scrollY = (int) newRangeStart;
        repaint();
    }

    //==========================================================================
    //  Cursor
    //==========================================================================
    void updateCursor (const juce::MouseEvent& e)
    {
        if (dragMode == DragMode::ResizeRight)
        { setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); return; }
        if (dragMode == DragMode::MoveClip)
        { setMouseCursor (juce::MouseCursor::DraggingHandCursor); return; }

        if (clipGridBounds.contains (e.getPosition()))
        {
            const int trackIdx = trackFromY (e.y);
            if (juce::isPositiveAndBelow (trackIdx, engine.getNumTracks()))
            {
                for (int ci = 0; ci < engine.getNumClips (trackIdx); ++ci)
                {
                    const auto clipR = clipRectForClip (trackIdx, ci);
                    if (clipR.contains (e.getPosition()))
                    {
                        if (e.x >= clipR.getRight() - kResizeZone)
                            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
                        else
                            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
                        return;
                    }
                }
                // Over empty track space — show pencil / crosshair to signal clip creation
                setMouseCursor (juce::MouseCursor::CrosshairCursor);
                return;
            }
        }
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    //==========================================================================
    //  Context menus
    //==========================================================================
    void showContextMenu (int trackIdx, int clipIdx, const juce::MouseEvent& e)
    {
        const auto info  = engine.getTrackInfo (trackIdx);
        const bool onClip = (clipIdx >= 0);
        juce::PopupMenu m;

        if (onClip)
        {
            m.addItem (1, "Open in piano roll");
            m.addSeparator();
            m.addItem (8, "Repeat clip");
            m.addItem (4, "Duplicate to next track");
            m.addSeparator();
            m.addItem (2, info.enabled ? "Mute track" : "Unmute track");
            m.addItem (3, "Clear clip");
            m.addItem (6, "Delete clip");
            m.addSeparator();
            m.addItem (5, "Set loop to clip length");
        }
        else
        {
            m.addItem (2, info.enabled ? "Mute track" : "Unmute track");
        }

        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (juce::Rectangle<int> (e.getScreenX(), e.getScreenY(), 1, 1)),
            [this, trackIdx, clipIdx, info, onClip] (int result)
            {
                switch (result)
                {
                    case 1:
                        selectTrack (trackIdx);
                        selectedClip  = clipIdx;
                        trackStrip.setSelectedTrack (trackIdx);
                        if (onClipDoubleClicked) onClipDoubleClicked (trackIdx, clipIdx);
                        break;
                    case 2:
                        engine.setTrackEnabled (trackIdx, ! info.enabled);
                        break;
                    case 3:
                        if (MidiClip* c = engine.getClip (trackIdx, clipIdx))
                            c->clear();
                        break;
                    case 4:
                        duplicateClipToNextTrack (trackIdx, clipIdx);
                        break;
                    case 5:
                    {
                        const auto ci = engine.getClipInfo (trackIdx, clipIdx);
                        loopStart = ci.startTick;
                        loopEnd   = ci.endTick();
                        break;
                    }
                    case 6:
                        engine.removeClip (trackIdx, clipIdx);
                        if (selectedTrack == trackIdx && selectedClip == clipIdx)
                            selectedClip = 0;
                        break;
                    case 8:  // Repeat clip
                    {
                        MidiClip* src = engine.getClip (trackIdx, clipIdx);
                        if (src)
                        {
                            const auto srcInfo = engine.getClipInfo (trackIdx, clipIdx);
                            juce::Array<MidiNote> notes;
                            { const juce::ScopedReadLock sl (src->getLock()); notes = src->getNotes(); }
                            for (int rep = 1; rep <= 1; ++rep)
                            {
                                const int64_t start = srcInfo.startTick + srcInfo.lengthTicks * rep;
                                const int newIdx = engine.addClip (trackIdx, start, srcInfo.lengthTicks);
                                if (MidiClip* dst = engine.getClip (trackIdx, newIdx))
                                    dst->setNotes (notes);
                            }
                        }
                        break;
                    }
                    default: break;
                }
                repaint(); trackStrip.repaint();
            });
    }

    void duplicateClipToNextTrack (int srcTrack, int srcClipIdx)
    {
        const int dstTrack = srcTrack + 1;
        MidiClip* src = engine.getClip (srcTrack, srcClipIdx);
        if (! src || dstTrack >= engine.getNumTracks()) return;
        const auto srcInfo = engine.getClipInfo (srcTrack, srcClipIdx);
        // Add a new clip on the destination track at the same start position
        const int newIdx = engine.addClip (dstTrack, srcInfo.startTick, srcInfo.lengthTicks);
        MidiClip* dst = engine.getClip (dstTrack, newIdx);
        if (! dst) return;
        const juce::ScopedReadLock sl (src->getLock());
        dst->setNotes (src->getNotes());
        repaint();
    }

    //==========================================================================
    //  Painting
    //==========================================================================
    void paintRuler (juce::Graphics& g) const
    {
        // Background
        g.setColour (juce::Colour (0xFF0C0C1A));
        g.fillRect (rulerBounds);

        // Left-strip region
        g.setColour (juce::Colour (0xFF090912));
        g.fillRect (rulerBounds.withWidth (kStripW));

        // Bottom border
        g.setColour (juce::Colour (0xFF1E2436));
        g.fillRect (rulerBounds.getX(), rulerBounds.getBottom() - 1,
                    rulerBounds.getWidth(), 1);

        // Loop region shading inside ruler
        if (loopStart >= 0 && loopEnd > loopStart)
        {
            const float lx = tickToX (loopStart);
            const float rx = tickToX (loopEnd);
            g.setColour (juce::Colour::fromFloatRGBA (0.25f, 0.85f, 0.85f, 0.22f));
            g.fillRect (lx, (float)rulerBounds.getY(), rx - lx,
                        (float)rulerBounds.getHeight());
        }

        const int64_t ppq    = MidiClip::kPPQ;
        const int64_t barLen = ppq * 4;
        const int     gx     = clipGridBounds.getX();
        const int     gw     = clipGridBounds.getWidth();
        const int64_t total  = totalVisibleTicks();

        // Decide beat/bar visibility based on zoom
        const double pxPerBar  = barLen * pixelsPerTick;
        const double pxPerBeat = ppq  * pixelsPerTick;
        const bool showBeats   = pxPerBeat >= 6.0;

        // Beat ticks
        if (showBeats)
        {
            const int64_t firstBeat = (int64_t)(scrollX / (pixelsPerTick * ppq));
            const int64_t lastBeat  = firstBeat + (int64_t)(gw / (pixelsPerTick * ppq)) + 2;
            for (int64_t b = firstBeat; b <= lastBeat && b * ppq <= total; ++b)
            {
                const int x = gx + (int)((b * ppq) * pixelsPerTick - scrollX);
                if (x < gx || x > gx + gw) continue;
                const bool isBar = (b % 4 == 0);
                g.setColour (isBar ? juce::Colour (0xFF2E3C52) : juce::Colour (0xFF1A2030));
                g.fillRect (x, rulerBounds.getY(),
                            1, isBar ? rulerBounds.getHeight()
                                     : rulerBounds.getHeight() / 2);
            }
        }

        // Bar numbers
        g.setFont (juce::Font (10.f, juce::Font::bold));
        const int64_t firstBar = (int64_t)(scrollX / (pixelsPerTick * barLen));
        const int64_t lastBar  = firstBar + (int64_t)(gw / (pixelsPerTick * barLen)) + 2;
        for (int64_t bar = firstBar; bar <= lastBar && bar * barLen <= total; ++bar)
        {
            const int x = gx + (int)((bar * barLen) * pixelsPerTick - scrollX);
            if (x < gx || x > gx + gw) continue;
            g.setColour (juce::Colour (0xFF7080A0));
            g.drawText (juce::String (bar + 1),
                        x + 3, rulerBounds.getY(),
                        48, rulerBounds.getHeight(),
                        juce::Justification::centredLeft, false);
        }

        // Loop L / R labels
        if (loopStart >= 0 && loopEnd > loopStart)
        {
            const float lx = tickToX (loopStart);
            const float rx = tickToX (loopEnd);
            g.setFont (juce::Font (9.f, juce::Font::bold));
            g.setColour (juce::Colour::fromFloatRGBA (0.25f, 0.95f, 0.95f, 0.95f));
            g.drawText ("L", (int)lx + 2, rulerBounds.getY(),
                        14, rulerBounds.getHeight(), juce::Justification::centredLeft);
            g.drawText ("R", (int)rx - 16, rulerBounds.getY(),
                        14, rulerBounds.getHeight(), juce::Justification::centredRight);
            // Bracket lines
            g.drawVerticalLine ((int)lx, (float)rulerBounds.getY(), (float)rulerBounds.getBottom());
            g.drawVerticalLine ((int)rx, (float)rulerBounds.getY(), (float)rulerBounds.getBottom());
        }
    }

    void paintTrackRows (juce::Graphics& g) const
    {
        const int n = engine.getNumTracks();
        for (int i = 0; i < n; ++i)
        {
            const int rowTop = trackTopY (i);
            if (rowTop + trackH < clipGridBounds.getY()) continue;  // above view
            if (rowTop > clipGridBounds.getBottom()) break;          // below view
            paintOneTrack (g, i);
        }
    }

    void paintLoopOverlay (juce::Graphics& g) const
    {
        if (loopStart < 0 || loopEnd <= loopStart) return;
        const float lx = tickToX (loopStart);
        const float rx = tickToX (loopEnd);
        // Tinted band across all track rows
        g.setColour (juce::Colour::fromFloatRGBA (0.2f, 0.8f, 0.8f, 0.06f));
        g.fillRect (lx, (float)clipGridBounds.getY(),
                    rx - lx, (float)clipGridBounds.getHeight());
        // Side lines
        g.setColour (juce::Colour::fromFloatRGBA (0.25f, 0.85f, 0.85f, 0.45f));
        g.drawVerticalLine ((int)lx,
                            (float)clipGridBounds.getY(),
                            (float)clipGridBounds.getBottom());
        g.drawVerticalLine ((int)rx,
                            (float)clipGridBounds.getY(),
                            (float)clipGridBounds.getBottom());
    }

    void paintOneTrack (juce::Graphics& g, int i) const
    {
        const auto info  = engine.getTrackInfo (i);
        const bool isSel = (i == selectedTrack);
        const bool muted = ! info.enabled;

        const juce::Rectangle<int> rowR (
            clipGridBounds.getX(),
            trackTopY (i),
            clipGridBounds.getWidth(),
            trackH);

        // Clip to grid area
        g.saveState();
        g.reduceClipRegion (clipGridBounds);

        // Row background — alternating shades, selected highlight
        g.setColour (isSel ? juce::Colour (0xFF0F1828)
                           : (i % 2 == 0 ? juce::Colour (0xFF09090F)
                                         : juce::Colour (0xFF0B0B14)));
        g.fillRect (rowR);

        // Separator
        g.setColour (juce::Colour (0xFF16202E));
        g.fillRect (rowR.getX(), rowR.getBottom() - 1, rowR.getWidth(), 1);

        // Vertical grid lines
        paintGridLines (g, rowR);

        // Clips — paint all slots on this track
        const int numClips = engine.getNumClips (i);
        for (int ci = 0; ci < numClips; ++ci)
        {
            const bool isSelClip = isSel && (ci == selectedClip);
            paintClip (g, i, ci, info, isSelClip, muted);
        }

        g.restoreState();
    }

    void paintGridLines (juce::Graphics& g, const juce::Rectangle<int>& rowR) const
    {
        const int64_t ppq    = MidiClip::kPPQ;
        const int64_t barLen = ppq * 4;
        const int64_t total  = totalVisibleTicks();
        const bool showBeats = (ppq * pixelsPerTick) >= 4.0;

        const int64_t firstBeat = (int64_t)(scrollX / (pixelsPerTick * ppq));
        const int64_t lastBeat  = firstBeat + (int64_t)(rowR.getWidth() / (pixelsPerTick * ppq)) + 2;

        for (int64_t b = firstBeat; b <= lastBeat && b * ppq <= total; ++b)
        {
            const int x = clipGridBounds.getX() + (int)((b * ppq) * pixelsPerTick - scrollX);
            if (x < rowR.getX() || x > rowR.getRight()) continue;
            const bool isBar = (b % 4 == 0);
            if (!showBeats && !isBar) continue;
            g.setColour (isBar ? juce::Colour (0xFF141C2A) : juce::Colour (0xFF0E1420));
            g.fillRect (x, rowR.getY(), 1, rowR.getHeight() - 1);
        }
    }

    void paintClip (juce::Graphics& g, int i, int ci,
                    const SequencerTrackInfo& info,
                    bool isSel, bool muted) const
    {
        const auto clipR = clipRectForClip (i, ci);
        if (! clipGridBounds.intersects (clipR)) return;

        const juce::Colour base = muted
            ? info.colour.withSaturation (0.08f).withBrightness (0.22f)
            : info.colour;

        // Fill gradient
        {
            juce::ColourGradient grad (
                base.withAlpha (0.38f), (float)clipR.getX(), (float)clipR.getY(),
                base.withAlpha (0.18f), (float)clipR.getX(), (float)clipR.getBottom(),
                false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (clipR.toFloat().reduced (1.f, 1.f), 4.f);
        }

        // Border — brighter when selected
        if (isSel)
        {
            g.setColour (base.brighter (0.7f).withAlpha (0.95f));
            g.drawRoundedRectangle (clipR.toFloat().reduced (1.f, 1.f), 4.f, 1.5f);
        }
        else
        {
            g.setColour (base.withAlpha (muted ? 0.28f : 0.6f));
            g.drawRoundedRectangle (clipR.toFloat().reduced (1.f, 1.f), 4.f, 1.f);
        }

        // Mute hatch
        if (muted)
        {
            g.setColour (juce::Colours::black.withAlpha (0.38f));
            g.fillRoundedRectangle (clipR.toFloat().reduced (1.f, 1.f), 4.f);
        }

        // Colour header strip along top
        const float headerH = juce::jmin (7.f, (float)trackH * 0.13f);
        g.setColour (base.withAlpha (muted ? 0.28f : 0.82f));
        {
            juce::Path hdr;
            hdr.addRoundedRectangle (clipR.getX() + 1.f, clipR.getY() + 1.f,
                                     clipR.getWidth() - 2.f, headerH,
                                     3.f, 3.f, true, true, false, false);
            g.fillPath (hdr);
        }

        // Track name
        if (trackH >= 20)
        {
            g.setFont (juce::Font (juce::jmin (11.f, (float)trackH * 0.22f), juce::Font::bold));
            g.setColour (muted ? juce::Colour (0xFF3A4A5A)
                               : juce::Colours::white.withAlpha (0.88f));
            g.drawText (info.name,
                        clipR.getX() + 5, clipR.getY() + (int)headerH,
                        juce::jmax (0, clipR.getWidth() - 24),
                        juce::jmax (0, (int)(trackH * 0.38f)),
                        juce::Justification::centredLeft, true);
        }

        // Track type badge
        if (clipR.getWidth() > 32 && trackH >= 20)
        {
            juce::String badge;
            switch (info.type)
            {
                case TrackType::MainSlice:      badge = "SL"; break;
                case TrackType::ChromaticSlice: badge = "CH"; break;
                case TrackType::SfPlayer:       badge = "SF"; break;
            }
            g.setFont (juce::Font (8.f));
            g.setColour (base.withAlpha (0.55f));
            g.drawText (badge,
                        clipR.getRight() - 22, clipR.getY() + 2,
                        20, 12,
                        juce::Justification::centredRight, false);
        }

        // Resize handle
        {
            const juce::Rectangle<float> handleR (
                clipR.toFloat().reduced (1.f, 1.f)
                    .withLeft ((float)(clipR.getRight() - 8)));
            g.setColour (base.withAlpha (0.4f));
            g.fillRoundedRectangle (handleR, 2.f);
            g.setColour (base.brighter (0.5f).withAlpha (0.45f));
            const float cx = handleR.getCentreX();
            for (int dot = 0; dot < 3; ++dot)
            {
                const float dy = handleR.getY() + handleR.getHeight() * (0.25f + dot * 0.25f);
                g.fillEllipse (cx - 1.f, dy - 1.f, 2.f, 2.f);
            }
        }

        // Mini note preview
        paintNotePreview (g, i, ci, clipR, base, muted);
    }

    void paintNotePreview (juce::Graphics& g, int trackIdx, int clipIdx,
                           juce::Rectangle<int> clipR,
                           juce::Colour base, bool muted) const
    {
        const MidiClip* clip = engine.getClip (trackIdx, clipIdx);
        if (! clip) return;
        const int64_t clipLen = clip->getLengthTicks();
        if (clipLen <= 0) return;

        const int headerH  = (int)(trackH * 0.38f);
        const int previewY = clipR.getY() + headerH;
        const int previewH = clipR.getHeight() - headerH - 3;
        if (previewH < 4) return;

        g.saveState();
        g.reduceClipRegion (clipR.withTrimmedTop (headerH).withTrimmedBottom (2));

        {
            const juce::ScopedReadLock sl (clip->getLock());
            const auto& notes = clip->getNotes();
            if (notes.isEmpty()) { g.restoreState(); return; }

            int loNote = 127, hiNote = 0;
            for (const auto& n : notes)
            {
                loNote = juce::jmin (loNote, n.note);
                hiNote = juce::jmax (hiNote, n.note);
            }
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
                const float va = muted ? 0.22f : (0.4f + 0.5f * n.velocity / 127.f);
                g.setColour (base.brighter (0.2f).withAlpha (va));
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

        const int x = (int)tickToX (tick);
        if (x < clipGridBounds.getX() || x > clipGridBounds.getRight()) return;

        // Thin line through tracks
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillRect (x, rulerBounds.getY(),
                    1, rulerBounds.getHeight() + clipGridBounds.getHeight());
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangeView)
};
