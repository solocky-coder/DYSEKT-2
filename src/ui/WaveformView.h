#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "WaveformCache.h"
#include "../audio/SliceManager.h"
#include "../audio/VoicePool.h"

class DysektProcessor;

class WaveformView : public juce::Component,
                     public juce::FileDragAndDropTarget
{
public:
    explicit WaveformView (DysektProcessor& p);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseEnter (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void rebuildCacheIfNeeded();
    bool hasActiveSlicePreview() const noexcept;
    bool getActiveSlicePreview (int& sliceIdx, int& startSample, int& endSample) const;
    bool getLinkedSlicePreview (int& sliceIdx, int& startSample, int& endSample) const;
    bool isInteracting() const noexcept;

    // Trim mode — entered when the user asks to trim before loading
    void enterTrimMode (int start, int end);
    void setTrimPoints (int inPt, int outPt);  // MIDI feedback path
    void getTrimBounds (int& outStart, int& outEnd) const;
    bool isTrimModeActive() const noexcept { return trimMode; }

    // Trim mode API used by TrimDialog and ActionPanel
    void setTrimMode (bool active);
    int  getTrimIn()  const noexcept { return trimInPoint; }
    int  getTrimOut() const noexcept { return trimOutPoint; }
    bool isTrimDragging() const noexcept { return trimDragging; }

    // Callback invoked when user applies trim; parameters are sample-accurate bounds
    std::function<void (int trimStart, int trimEnd)> onTrimApplied;
    // Callback invoked when user cancels trim (CANCEL button)
    std::function<void()> onTrimCancelled;
    // Callback for file load requests (routed through trim dialog if set)
    std::function<void (const juce::File&)> onLoadRequest;
    std::function<void()> onShortcutsToggle;
    // Callback invoked when the user requests a rename action on the waveform
    std::function<void(int sliceIdx, const juce::String& currentName)> onRenameRequest;

    // Legacy 2-state toggle (kept for any callers that still use it)
    void setSoftWaveform (bool soft) { setWaveformMode (soft ? 1 : 0); }
    bool isSoftWaveform() const noexcept { return waveformMode == 1; }

    // 8-mode waveform selector: 0=Hard 1=Soft 2=Outline 3=Rectified
    //                           4=Mirrored 5=Bars 6=RMS 7=Stepped
    void setWaveformMode (int mode) { waveformMode = mode; softWaveform = (mode == 1); repaint(); }
    int  getWaveformMode() const noexcept { return waveformMode; }

    bool shiftPreviewActive = false;
    std::vector<int> transientPreviewPositions;

    juce::TextButton midiSliceBtn;

private:
    struct ViewState
    {
        int numFrames = 0;
        int visibleStart = 0;
        int visibleLen = 0;
        int width = 0;
        float samplesPerPixel = 1.0f;
        bool valid = false;
    };

    enum DragMode { None, DragEdgeLeft, DragEdgeRight, DrawSlice, MoveSlice,
                    TrimMarkerLeft, TrimMarkerRight, DragTrimIn, DragTrimOut };

    enum class HoveredEdge { None, Left, Right };
    HoveredEdge hoveredEdge = HoveredEdge::None;

    int pixelToSample (int px) const;
    int sampleToPixel (int sample) const;
    ViewState buildViewState (const SampleData::SnapshotPtr& sampleSnap) const;

    /** Returns the buffer this view should currently display/interact with:
     *  the Slicer's real engine sample (processor.sampleData) normally, or
     *  the SFZ-PLAYER's own decoupled preview buffer (processor.sampleData2)
     *  when the current MIDI route mode is SfzPlayer2. WaveformView is shared
     *  between the Slicer (uiMode 0) and SFZ-PLAYER (uiMode 1) tabs, so every
     *  read needs to go through this rather than processor.sampleData
     *  directly — otherwise the two modes' loaded files bleed into each
     *  other. */
    SampleData& activeSampleData() const noexcept;
    SliceManager& activeSliceManager() const noexcept;
    VoicePool& activeVoicePool() const noexcept;

    /** True when the current MIDI route mode is SfzPlayer2 — i.e. the
     *  SFZ-PLAYER tab is driving this shared component. SFZ-PLAYER has its
     *  own complete second Slicer instance (sliceManager2/voicePool2/
     *  sampleData2 via activeSliceManager()/activeVoicePool()/
     *  activeSampleData()), so drawSlices()/mouseDown() etc. work normally
     *  against it. The only things disabled in this mode are manual slice
     *  creation, edge-dragging, and the context menu — SFZ-PLAYER's slice
     *  layout always comes from the loaded SFZ file, never user-edited.
     *  Per-slice ADSR editing and the REV/LOOP/1SH/POLY/STR/TAIL flags
     *  (see SliceLcdDisplay/SliceWaveformLcd) are NOT disabled. */
    bool isSfzPlayer2Mode() const noexcept;

    void drawWaveform (juce::Graphics& g);
    void drawSlices (juce::Graphics& g);
    void drawPlaybackCursors (juce::Graphics& g);
    void paintLazyChopOverlay (juce::Graphics& g);
    void paintTransientMarkers (juce::Graphics& g);
    void paintTrimOverlay (juce::Graphics& g);
    void paintMidiSliceOverlay (juce::Graphics& g);

    static constexpr int kMidiOverlayH = 22;

    // Aggregates all cache-invalidation inputs; rebuild is skipped when unchanged.
    struct CacheKey
    {
        int visibleStart = 0, visibleLen = 0, width = 0, numFrames = 0;
        const void* samplePtr = nullptr;
        bool operator== (const CacheKey&) const = default;
    };

    // --- Optimistic state to prevent marker 'jump' after move ---
    int optimisticSliceIdx = -1;
    int optimisticStartSample = -1;


    DysektProcessor& processor;
    WaveformCache cache;
    CacheKey prevCacheKey;
    bool midiSliceOverlayActive = false;
    void setMidiSliceActive (bool active);
    bool softWaveform  = false;   // legacy flag (kept for compat – mirrors waveformMode==1)
    int  waveformMode  = 0;       // 0=Hard 1=Soft 2=Outline 3=Rectified 4=Mirrored 5=Bars 6=RMS 7=Stepped
    bool trimMode      = false;   // trim in/out marker editing mode
    int  trimInPoint   = 0;       // trim-in marker position in samples (DragTrimIn path)
    int  trimOutPoint  = 0;       // trim-out marker position in samples (DragTrimOut path)
    bool trimDragging  = false;   // true while user is actively dragging a trim handle
    int  trimHoverX    = -1;      // last mouse X in trim mode (-1 = not hovering)
    int  trimStart     = 0;       // trim-in marker position in samples (enterTrimMode path)
    int  trimEnd       = 0;       // trim-out marker position in samples (enterTrimMode path)
    mutable ViewState cachedPaintViewState;   // valid only between paint() start and end
    mutable bool paintViewStateActive = false; // true only during paint(); guards cachedPaintViewState

    // Hit areas for trim mode buttons (updated each paint)
    juce::Rectangle<int> trimApplyBtnBounds;
    juce::Rectangle<int> trimResetBtnBounds;
    juce::Rectangle<int> trimCancelBtnBounds;

    static constexpr int kTrimMarkerHitTolerance = 6;   // px within which clicks hit a trim marker
    static constexpr int kMinTrimRegionSamples   = 64;  // minimum trim region in samples

    DragMode dragMode = None;
    int dragSliceIdx = -1;
    int drawStart = 0;
    int drawEnd = 0;
    int addClickStart = -1; // ADD click mode: -1 = waiting for first click, >= 0 = waiting for second click
    int dragOffset = 0;    // for MoveSlice: offset from mouse to slice start
    int dragSliceLen = 0;  // for MoveSlice: original slice length
    int dragPreviewStart = 0; // for edge/move drags: preview start sample
    int dragPreviewEnd = 0;   // for edge/move drags: preview end sample
    int dragOrigStart = 0;    // slice start at the moment drag began (for overlap clamping)
    int dragOrigEnd = 0;      // slice end at the moment drag began (for overlap clamping)
    // Linked (adjacent) slice preview — kept in sync with the dragged edge
    int linkedSliceIdx     = -1;
    int linkedPreviewStart = 0;
    int linkedPreviewEnd   = 0;

    // True while the dragged marker is inside the delete zone (crossed the previous marker)
    bool dragInDeleteZone = false;


    // Middle-mouse drag (scroll+zoom like ScrollZoomBar)
    bool midDragging = false;
    float midDragStartZoom = 1.0f;
    float midDragAnchorFrac = 0.0f;
    float midDragAnchorPixelFrac = 0.0f;
    int   midDragStartX = 0;
    int   midDragStartY = 0;
};
