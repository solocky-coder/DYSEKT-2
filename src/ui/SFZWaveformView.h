#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "WaveformCache.h"

class DysektProcessor;

// =============================================================================
//  SFZWaveformView  —  Main waveform display for SFZ / SF2 Player mode
// =============================================================================
//  Shows the concatenated SFZ/SF2 sample buffer rendered as a waveform with:
//    • Zoom / scroll (shared processor.zoom / processor.scroll atomics)
//    • Playback cursors (one per active voice)
//    • Loop region overlay (from sfzPlayer loop points)
//    • Drag-and-drop target for .sfz / .sf2 files
//    • "DROP SFZ / SF2 FILE" placeholder when no instrument is loaded
//
//  Wire into DysektEditor by including this header, adding a member and
//  calling addAndMakeVisible / setBounds inside resized().
// =============================================================================

class SFZWaveformView : public juce::Component,
                        public juce::FileDragAndDropTarget
{
public:
    explicit SFZWaveformView (DysektProcessor& p);

    // ── juce::Component overrides ─────────────────────────────────────────────
    void paint          (juce::Graphics& g) override;
    void resized        () override;
    void mouseWheelMove (const juce::MouseEvent& e,
                         const juce::MouseWheelDetails& w) override;

    // ── FileDragAndDropTarget ─────────────────────────────────────────────────
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter          (const juce::StringArray& files, int x, int y) override;
    void fileDragExit           (const juce::StringArray& files) override;
    void filesDropped           (const juce::StringArray& files, int x, int y) override;

    // ── Public API ────────────────────────────────────────────────────────────
    /** Call from the editor's timerCallback() at ~30 Hz to keep the view fresh. */
    void timerTick();

    /** Waveform rendering mode: 0 = Hard, 1 = Soft (matches WaveformView modes). */
    void setWaveformMode (int mode) noexcept { waveformMode = mode; repaint(); }
    int  getWaveformMode()          const noexcept { return waveformMode; }

private:
    // ── Cache ─────────────────────────────────────────────────────────────────
    struct CacheKey
    {
        int         visibleStart = 0, visibleLen = 0, width = 0, numFrames = 0;
        const void* samplePtr    = nullptr;
        bool operator== (const CacheKey&) const = default;
    };

    void rebuildCacheIfNeeded();

    WaveformCache cache;
    CacheKey      prevCacheKey;

    // Cached per-frame view values — valid only during paint()
    mutable int  cachedVisibleStart = 0;
    mutable int  cachedVisibleLen   = 0;
    mutable int  cachedNumFrames    = 0;
    mutable bool paintStateActive   = false;

    int  sampleToPixel (int sample) const;
    int  pixelToSample (int px)     const;

    // ── Draw helpers ───────────────────────────────────────────────────────────
    void drawWaveform        (juce::Graphics& g);
    void drawLoopOverlay     (juce::Graphics& g);
    void drawPlaybackCursors (juce::Graphics& g);
    void drawPlaceholder     (juce::Graphics& g);

    // ── State ─────────────────────────────────────────────────────────────────
    int  waveformMode  = 0;      ///< 0 = Hard, 1 = Soft
    bool dragHighlight = false;  ///< true while a valid file is being dragged over

    DysektProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SFZWaveformView)
};
