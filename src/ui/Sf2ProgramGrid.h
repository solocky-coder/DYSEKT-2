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
    // Layout
    static constexpr int kCols     = 8;
    static constexpr int kCellH    = 36;
    static constexpr int kHdrH     = 18;   // bank section header
    static constexpr int kScrollW  = 10;
    static constexpr int kPad      = 4;

    std::vector<Sf2PresetInfo> presets;
    int   currentIdx     { -1 };
    int   editingIdx     { -1 };  ///< preset being edited for per-channel FX, or -1
    // Maps preset index → assigned MIDI channel (1-16). 0/absent = not assigned.
    std::unordered_map<int,int> presetChannels;
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
    int  cellIndexAt (juce::Point<int> pt) const;
    juce::Rectangle<int> cellBoundsFor (int presetIdx) const;

    void showChannelMenu (int presetIdx, juce::Point<int> screenPos);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sf2ProgramGrid)
};
