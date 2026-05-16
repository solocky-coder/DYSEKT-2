#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "TransportBar.h"
#include "TrackHeaderStrip.h"
#include "../sequencer/SequencerEngine.h"
#include "../sequencer/MidiClip.h"

//==============================================================================
//  ArrangeView  —  Cubase-style arrange window
//
//  Layout:
//    ┌────────────────────────────────────────────────────┐
//    │  TransportBar  (full width, kTransportH px)        │
//    ├──────────────┬─────────────────────────────────────┤
//    │              │  Ruler  (kRulerH px)                │
//    │  Track       ├─────────────────────────────────────┤
//    │  Header      │                                     │
//    │  Strip       │  ClipGrid  (one row per track)      │
//    │  (kStripW)   │                                     │
//    └──────────────┴─────────────────────────────────────┘
//
//  Behaviour:
//   • Single-click a clip  → select it
//   • Double-click a clip  → fires onClipDoubleClicked(trackIndex)
//   • Drag clip body       → move clip start offset
//   • Drag right edge      → resize clip length
//   • Click empty space    → deselect / (optionally) create clip
//   • Playhead redraws every 30 Hz via timer
//
//  The owner (PluginEditor) wires onClipDoubleClicked to show the
//  PianoRollPanel as a floating overlay.
//==============================================================================
class ArrangeView : public juce::Component,
                    private juce::Timer
{
public:
    static constexpr int kTransportH = 34;
    static constexpr int kStripW     = 160;
    static constexpr int kRulerH     = 22;
    static constexpr int kTrackH     = TrackHeaderStrip::kTrackH;   // 36
    static constexpr int kMinClipPx  = 8;   // minimum visible clip width

    // Called when user double-clicks a clip row
    std::function<void(int trackIndex)> onClipDoubleClicked;

    // Wired by PluginEditor — called when user picks a preset from the strip footer
    std::function<void(const Sf2PresetInfo&)> onAddSfTrackRequested;

    // Wired by PluginEditor — called when user clicks × on an SF track row
    std::function<void(int trackIndex)> onRemoveSfTrack;

    /** Feed the full preset list from the loaded SF2 into the strip footer picker. */
    void setStripAvailablePresets (const std::vector<Sf2PresetInfo>& presets)
    {
        trackStrip.setAvailablePresets (presets);
    }

    //==========================================================================
    ArrangeView (SequencerEngine& seq, AbletonLink* link = nullptr)
        : engine (seq),
          transport (seq, link),
          trackStrip (seq)
    {
        addAndMakeVisible (transport);
        addAndMakeVisible (trackStrip);

        // Keep strip selection in sync
        trackStrip.onTrackSelected = [this] (int idx) { selectedTrack = idx; };

        // Forward SF picker callbacks to owner (PluginEditor)
        trackStrip.onAddSfTrackRequested = [this] (const Sf2PresetInfo& p)
        {
            if (onAddSfTrackRequested) onAddSfTrackRequested (p);
        };

        trackStrip.onRemoveSfTrack = [this] (int idx)
        {
            if (onRemoveSfTrack) onRemoveSfTrack (idx);
        };

        startTimerHz (30);
    }

    ~ArrangeView() override { stopTimer(); }

    //==========================================================================
    void resized() override
    {
        auto r = getLocalBounds();
        transport.setBounds (r.removeFromTop (kTransportH));

        // Left strip occupies full remaining height
        trackStrip.setBounds (r.removeFromLeft (kStripW).withTrimmedTop (kRulerH));

        gridArea = r;
        rulerBounds = gridArea.removeFromTop (kRulerH);
        clipGridBounds = gridArea;   // rows drawn dynamically
    }

    void paint (juce::Graphics& g) override
    {
        // Background
        g.fillAll (juce::Colour (0xFF080810));

        // Draw ruler
        paintRuler (g, rulerBounds);

        // Draw clip rows
        const int nTracks = engine.getNumTracks();
        for (int i = 0; i < nTracks; ++i)
            paintTrackRow (g, i);

        // Playhead
        paintPlayhead (g);
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! clipGridBounds.contains (e.getPosition())) return;

        const int trackIdx = trackFromY (e.y);
        if (! juce::isPositiveAndBelow (trackIdx, engine.getNumTracks())) return;

        // Check if we're near the right edge of a clip (resize zone)
        auto clipR = clipRectForTrack (trackIdx);
        const int edgeZone = 8;
        if (clipR.contains (e.getPosition()) &&
            e.x >= clipR.getRight() - edgeZone)
        {
            dragMode      = DragMode::ResizeRight;
            dragTrack     = trackIdx;
            dragStartX    = e.x;
            dragStartTicks = clipLengthForTrack (trackIdx);
            selectedTrack = trackIdx;
            repaint();
            return;
        }

        // Clip body drag (move start offset — currently clips start at 0 so
        // this records drag origin for a future offset field if added)
        if (clipR.contains (e.getPosition()))
        {
            dragMode      = DragMode::MoveClip;
            dragTrack     = trackIdx;
            dragStartX    = e.x;
            dragStartTicks = 0;
            selectedTrack = trackIdx;
            repaint();
            return;
        }

        // Click on empty space — just deselect
        selectedTrack = -1;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragMode == DragMode::None) return;

        const double ticksPerPx = totalTicks() / (double) juce::jmax (1, clipGridBounds.getWidth());
        const int    dx         = e.x - dragStartX;

        if (dragMode == DragMode::ResizeRight)
        {
            const int64_t newLen = juce::jmax (
                MidiClip::kPPQ,   // at least 1 beat
                dragStartTicks + (int64_t) (dx * ticksPerPx));
            engine.setLengthTicks (newLen);
            repaint();
        }
        // MoveClip: no-op for now (clips always start at tick 0 in current design)
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragMode  = DragMode::None;
        dragTrack = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (! clipGridBounds.contains (e.getPosition())) return;

        const int trackIdx = trackFromY (e.y);
        if (! juce::isPositiveAndBelow (trackIdx, engine.getNumTracks())) return;

        // Only fire when double-clicking on an existing clip rect
        auto clipR = clipRectForTrack (trackIdx);
        if (clipR.contains (e.getPosition()))
        {
            selectedTrack = trackIdx;
            trackStrip.setSelectedTrack (trackIdx);
            repaint();

            if (onClipDoubleClicked)
                onClipDoubleClicked (trackIdx);
        }
    }

private:
    //==========================================================================
    SequencerEngine&  engine;
    TransportBar      transport;
    TrackHeaderStrip  trackStrip;

    int selectedTrack = -1;

    juce::Rectangle<int> gridArea;
    juce::Rectangle<int> rulerBounds;
    juce::Rectangle<int> clipGridBounds;

    // Drag state
    enum class DragMode { None, MoveClip, ResizeRight };
    DragMode  dragMode      = DragMode::None;
    int       dragTrack     = -1;
    int       dragStartX    = 0;
    int64_t   dragStartTicks = 0;

    //==========================================================================
    //  Helpers
    //==========================================================================
    int64_t totalTicks() const
    {
        // Show at least 2× the engine clip length, or 8 bars minimum
        const int64_t clipLen = engine.getLengthTicks();
        return juce::jmax (clipLen * 2, MidiClip::kPPQ * 4 * 8);
    }

    double ticksPerPixel() const
    {
        return totalTicks() / (double) juce::jmax (1, clipGridBounds.getWidth());
    }

    int64_t clipLengthForTrack (int /*trackIdx*/) const
    {
        // All tracks share the engine clip length for now
        return engine.getLengthTicks();
    }

    juce::Rectangle<int> clipRectForTrack (int trackIdx) const
    {
        if (clipGridBounds.isEmpty()) return {};
        const int64_t len    = clipLengthForTrack (trackIdx);
        const int     w      = juce::jmax (kMinClipPx,
                                   (int) (len / ticksPerPixel()));
        const int     startX = clipGridBounds.getX();
        const int     y      = clipGridBounds.getY() + trackIdx * kTrackH;
        return { startX, y, w, kTrackH - 1 };
    }

    int trackFromY (int y) const
    {
        return (y - clipGridBounds.getY()) / kTrackH;
    }

    int tickToX (int64_t tick) const
    {
        return clipGridBounds.getX() +
               (int) (tick / ticksPerPixel());
    }

    //==========================================================================
    //  Painting
    //==========================================================================
    void paintRuler (juce::Graphics& g, juce::Rectangle<int> r) const
    {
        // Ruler background
        g.setColour (juce::Colour (0xFF0C0C18));
        g.fillRect (r);

        g.setColour (juce::Colour (0xFF1E2430));
        g.fillRect (r.getX(), r.getBottom() - 1, r.getWidth(), 1);

        // Bar markers
        const int64_t ppq     = MidiClip::kPPQ;
        const int64_t barLen  = ppq * 4;   // 4/4 time
        const int64_t total   = totalTicks();
        const int     gridX   = clipGridBounds.getX();
        const int     gridW   = clipGridBounds.getWidth();

        g.setFont (juce::Font (9.f, juce::Font::bold));

        for (int64_t bar = 0; bar * barLen <= total; ++bar)
        {
            const int x = gridX + (int) (bar * barLen / ticksPerPixel());
            if (x < gridX || x > gridX + gridW) continue;

            // Tall tick on bar, short on beat
            const bool isMajor = (bar % 4 == 0);
            g.setColour (isMajor ? juce::Colour (0xFF3A4455)
                                 : juce::Colour (0xFF222A38));
            g.fillRect (x, r.getY(), 1, r.getHeight());

            if (isMajor)
            {
                g.setColour (juce::Colour (0xFF6A7A90));
                g.drawText (juce::String (bar + 1),
                            x + 3, r.getY(), 30, r.getHeight(),
                            juce::Justification::centredLeft, false);
            }
        }
    }

    void paintTrackRow (juce::Graphics& g, int trackIdx) const
    {
        if (clipGridBounds.isEmpty()) return;

        const auto info   = engine.getTrackInfo (trackIdx);
        const bool isSel  = (trackIdx == selectedTrack);
        const auto rowR   = juce::Rectangle<int> (
            clipGridBounds.getX(), clipGridBounds.getY() + trackIdx * kTrackH,
            clipGridBounds.getWidth(), kTrackH);

        // Row background
        g.setColour (isSel ? juce::Colour (0xFF10182A) : juce::Colour (0xFF090912));
        g.fillRect (rowR);

        // Row separator
        g.setColour (juce::Colour (0xFF1C2028));
        g.fillRect (rowR.getX(), rowR.getBottom() - 1, rowR.getWidth(), 1);

        // Vertical beat grid lines
        const int64_t ppq   = MidiClip::kPPQ;
        const int64_t total = totalTicks();
        for (int64_t beat = 0; beat * ppq <= total; ++beat)
        {
            const int gx = tickToX (beat * ppq);
            if (gx < rowR.getX() || gx > rowR.getRight()) continue;
            const bool isBar = (beat % 4 == 0);
            g.setColour (isBar ? juce::Colour (0xFF1A2234)
                               : juce::Colour (0xFF111820));
            g.fillRect (gx, rowR.getY(), 1, rowR.getHeight() - 1);
        }

        // Clip rectangle
        auto clipR = clipRectForTrack (trackIdx);
        const bool muted = ! info.enabled;

        const juce::Colour clipBase = muted
            ? info.colour.withSaturation (0.15f).withAlpha (0.5f)
            : info.colour;

        // Clip fill
        g.setColour (clipBase.withAlpha (0.28f));
        g.fillRoundedRectangle (clipR.toFloat().reduced (1.f, 2.f), 3.f);

        // Clip border — bright when selected
        g.setColour (isSel ? clipBase.brighter (0.5f)
                           : clipBase.withAlpha (0.7f));
        g.drawRoundedRectangle (clipR.toFloat().reduced (1.f, 2.f), 3.f, 1.f);

        // Track name inside clip
        g.setFont (juce::Font (9.5f, juce::Font::bold));
        g.setColour (muted ? juce::Colour (0xFF445060)
                           : clipBase.brighter (0.3f));
        g.drawText (info.name,
                    clipR.getX() + 5, clipR.getY(),
                    juce::jmax (0, clipR.getWidth() - 10), clipR.getHeight(),
                    juce::Justification::centredLeft, true);

        // Resize handle hint (right edge darker stripe)
        const juce::Rectangle<int> handleR (clipR.getRight() - 6,
                                            clipR.getY() + 2,
                                            5, clipR.getHeight() - 4);
        g.setColour (clipBase.withAlpha (0.4f));
        g.fillRoundedRectangle (handleR.toFloat(), 2.f);

        // Mini note preview — sample the clip's notes and draw tiny bars
        if (const MidiClip* clip = engine.getClip (trackIdx))
        {
            const int64_t clipLen = clip->getLengthTicks();
            if (clipLen > 0 && clipR.getWidth() > 20)
            {
                const juce::ScopedReadLock sl (clip->getLock());
                const auto& notes = clip->getNotes();
                for (const auto& n : notes)
                {
                    if (n.startTick >= clipLen) continue;
                    const float frac  = (float) n.startTick / (float) clipLen;
                    const int   nx    = clipR.getX() + (int) (frac * clipR.getWidth());
                    const float pitch = (n.note - 21) / 107.f;  // piano range
                    const int   ny    = clipR.getBottom() - 4 - (int) (pitch * (clipR.getHeight() - 8));
                    g.setColour (clipBase.withAlpha (0.65f));
                    g.fillRect (nx, ny, 2, 2);
                }
            }
        }
    }

    void paintPlayhead (juce::Graphics& g) const
    {
        if (clipGridBounds.isEmpty()) return;

        const int64_t tick   = engine.getPlayheadTick();
        const int64_t total  = totalTicks();
        if (tick < 0 || tick > total) return;

        const int x = tickToX (tick);
        if (x < clipGridBounds.getX() || x > clipGridBounds.getRight()) return;

        // Line from ruler bottom through all tracks
        g.setColour (juce::Colour (0xFFE06030).withAlpha (0.85f));
        g.fillRect (x, rulerBounds.getY(),
                    1, rulerBounds.getHeight() + clipGridBounds.getHeight());

        // Triangle indicator at ruler top
        juce::Path tri;
        tri.addTriangle ((float) x, (float) rulerBounds.getBottom(),
                         (float) x - 5.f, (float) rulerBounds.getY() + 4.f,
                         (float) x + 5.f, (float) rulerBounds.getY() + 4.f);
        g.fillPath (tri);
    }

    void timerCallback() override { repaint(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArrangeView)
};
