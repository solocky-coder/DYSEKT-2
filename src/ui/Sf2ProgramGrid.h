#include <unordered_map>
#pragma once
// =============================================================================
//  Sf2ProgramGrid.h  —  Korg-M1-style preset grid for SF2 banks
// =============================================================================
//  Shows all presets in a scrollable N-column grid, grouped by bank.
//  Left-click  → radio-toggle preview (audition on channel 15); calls onPreviewToggled
//  Right-click → pops a MIDI channel picker, calls onChannelChanged(ch)
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include "../audio/SfzPlayer.h"
#include "DysektLookAndFeel.h"

class Sf2ProgramGrid : public juce::Component,
                       public juce::ScrollBar::Listener
{
public:
    // ── Callbacks wired by SfzDropdownPanel ──────────────────────────────────
    std::function<void (int index)> onPresetSelected;
    // ch == 0 → deactivate/remove assignment; ch 1-16 → assign that MIDI channel
    std::function<void (int presetIdx, int ch)> onChannelChanged;

    /** Fired when the preview toggle changes.
     *  index == -1  → preview cleared.
     *  index >= 0   → preset at that index is now being previewed. */
    std::function<void (int index)> onPreviewToggled;

    /** Fired when the user clicks an already-assigned preset cell to select it
     *  for per-channel FX editing.  index is the preset index in presets[]. */
    std::function<void (int index)> onAssignedPresetClicked;

    Sf2ProgramGrid();
    ~Sf2ProgramGrid() override;

    void setPresets  (const std::vector<Sf2PresetInfo>& list, int currentIndex,
                      int currentMidiChannel);
    void setCurrentIndex (int idx);

    /** Marks a preset as the one currently being edited for per-channel FX.
     *  Pass -1 to clear.  Triggers a repaint. */
    void setEditingIndex (int idx);

    /** Read-only access to the current per-preset channel assignments. */
    const std::unordered_map<int,int>& getPresetChannels() const noexcept { return presetChannels; }

    /** Read-only access to the full preset list (used by Sf2MixerPanel). */
    const std::vector<Sf2PresetInfo>& getPresets() const noexcept { return presets; }

    /** Restore channel assignments from persisted state (e.g. after plugin reload).
     *  Must be called on the message thread after setPresets().
     *  Key = preset index in the list, value = 1-based MIDI channel. */
    /** Called by SfzDropdownPanel whenever sfPlayerChannelMask changes.
     *  lo/hi are the lowest and highest set channel numbers (1-based) derived
     *  from the mask, used to grey out assigned channels in the grid.
     *  Channels outside [low, high] are greyed out in the right-click menu. */
    void setChannelRange (int low, int high)
    {
        rangeLow  = juce::jlimit (1, 16, low);
        rangeHigh = juce::jlimit (1, 16, high);
    }

    /** Called by SfzDropdownPanel whenever chromaticSliceChannelMask and/or
     *  sfzPlayer2ChannelMask change. mask should be the bitwise-OR of both:
     *  channels reserved by chromatic slices and channels currently occupied
     *  by the SFZ-Player (sfzPlayer2). Channels whose bit is set are disabled
     *  (and labelled [reserved]) in the right-click channel-picker menu. */
    void setBlockedChannels (uint32_t mask) noexcept { blockedMask = mask; }

    void setPresetChannels (const std::unordered_map<int,int>& channels)
    {
        presetChannels = channels;
        repaint();
    }

    /** Clear the preview toggle without firing onPreviewToggled.
     *  Called by SfzDropdownPanel when the grid is closed or a real
     *  channel is assigned so the visual state stays consistent. */
    void clearPreviewState();

    // ── Component overrides ───────────────────────────────────────────────────
    void paint   (juce::Graphics&) override;
    void resized () override;
    void mouseDown        (const juce::MouseEvent&) override;
    void mouseMove        (const juce::MouseEvent&) override;
    void mouseExit        (const juce::MouseEvent&) override;
    void mouseWheelMove   (const juce::MouseEvent&,
                           const juce::MouseWheelDetails&) override;

    // ── ScrollBar::Listener ───────────────────────────────────────────────────
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;

private:
    // Layout — column count and scroll-bar width are fixed; all pixel heights
    // scale with the component so the grid stays proportional at any UI scale.
    static constexpr int kCols      = 8;
    static constexpr int kScrollW   = 10;
    static constexpr int kPad       = 4;
    static constexpr int kMaxCellW  = 220; // cap so cells don't balloon on wide panels

    // Base heights (logical pixels at 1× / default UI scale).
    static constexpr int kBaseCellH = 36;
    static constexpr int kBaseHdrH  = 18;

    // Scale factor: mirrors the live UI scale set on the editor via setTransform().
    // DysektLookAndFeel::getMenuScale() is updated to userScale*hostScale every
    // time the editor calls setTransform(), so it always reflects current scale.
    // Clamped to [0.5, 4] to guard against pathological values.
    float scaleFactor() const noexcept
    {
        return juce::jlimit (0.5f, 4.0f, DysektLookAndFeel::getMenuScale());
    }

    int cellH() const noexcept { return juce::roundToInt ((float) kBaseCellH * scaleFactor()); }
    int hdrH()  const noexcept { return juce::roundToInt ((float) kBaseHdrH  * scaleFactor()); }

    std::vector<Sf2PresetInfo> presets;
    int   currentIdx     { -1 };
    int   editingIdx     { -1 };  ///< preset being edited for per-channel FX, or -1
    // Maps preset index → assigned MIDI channel (1-16). 0/absent = not assigned.
    std::unordered_map<int,int> presetChannels;
    int      rangeLow    { 1 };    ///< lowest channel in sfPlayerChannelMask  — set by SfzDropdownPanel
    int      rangeHigh   { 16 };   ///< highest channel in sfPlayerChannelMask — set by SfzDropdownPanel
    uint32_t blockedMask { 0u };   ///< chromaticSliceChannelMask | sfzPlayer2ChannelMask — channels reserved/occupied and unavailable for SF2 assignment
    int   hoveredCell    { -1 };
    int   previewIdx     { -1 };  ///< index of currently-previewing preset, or -1

    // Each "row" in our layout is either a bank header or a row of up to kCols cells.
    struct LayoutRow
    {
        bool isHeader { false };
        int  bank     { 0 };
        int  firstIdx { 0 };   // index into presets[] for the first cell in this row
        int  count    { 0 };   // how many cells (1..kCols)
    };
    std::vector<LayoutRow> rows;
    int totalH { 0 };

    juce::ScrollBar scrollBar { true };  // vertical
    int scrollY { 0 };

    void rebuildLayout();
    void updateScrollBar();
    int  cellIndexAt (juce::Point<int> pt) const;
    juce::Rectangle<int> cellBoundsFor (int presetIdx) const;

    void showChannelMenu (int presetIdx, juce::Point<int> screenPos);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sf2ProgramGrid)
};
