#pragma once
#include <juce_gui_extra/juce_gui_extra.h>

class DysektProcessor;

//==============================================================================
/// Pad-grid alternate interface view.
///
/// Shows DYSEKT's 32 slices as two banks of 16 pads in a 4×4 grid.
/// Bank A = slices 1–16   Bank B = slices 17–32
/// The user switches banks with the A / B buttons at the top of the component.
/// No scrolling is required — the full bank always fits the visible area.
///
/// Thread safety: all methods must be called from the message (UI) thread.
//==============================================================================
class PadGridView : public juce::Component
{
public:
    explicit PadGridView (DysektProcessor& proc);
    ~PadGridView() override;

    //==========================================================================
    void paint     (juce::Graphics& g) override;
    void resized   () override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    /// Call from the editor's timerCallback to refresh meters / selection.
    void repaintGrid();

    /// Mirror the editor's current waveformMode (0-7).
    void setWaveformMode (int mode) noexcept { waveformMode = mode; repaint(); }

    /// Fired when the user chooses Rename from the right-click menu.
    /// The editor wires this to showRenameOverlay so the dialog stays inside the plugin window.
    std::function<void(int idx, const juce::String& currentName)> onRenameRequest;

    //==========================================================================
    // Layout constants
    static constexpr int kNumCols     = 4;                          ///< Columns per bank.
    static constexpr int kNumRows     = 4;                          ///< Rows per bank.
    static constexpr int kPadsPerBank = kNumCols * kNumRows;        ///< 16
    static constexpr int kNumBanks    = 2;                          ///< Bank A and Bank B.
    static constexpr int kMaxPads     = kPadsPerBank * kNumBanks;   ///< 32

    static constexpr int kBankBarH = 30;   ///< Height of the bank-switcher bar (px).
    static constexpr int kPadGap   =  5;   ///< Gap between cells (px).
    static constexpr int kBarW     =  6;   ///< Width of the color accent bar on pad left.
    static constexpr int kPadPadX  =  8;   ///< Horizontal outer padding.
    static constexpr int kPadPadY  =  6;   ///< Vertical outer padding (below bank bar).

private:
    //==========================================================================
    DysektProcessor& processor;

    int currentBank  = 0;    ///< 0 = Bank A, 1 = Bank B.
    int hoveredPad   = -1;   ///< Absolute pad index under mouse (-1 = none).
    int waveformMode = 0;    ///< Mirrors PluginEditor::waveformMode (0=Hard…7=Stepped).

    /// Last selectedSlice value used for auto-bank-switching in repaintGrid().
    /// Prevents the timer from resetting currentBank after a manual bank button press.
    int lastAutoSwitchSlice = -1;

    //-- bank-switcher ----------------------------------------------------------
    juce::Rectangle<int> bankAButtonBounds;
    juce::Rectangle<int> bankBButtonBounds;

    void layoutBankButtons();
    void drawBankBar (juce::Graphics& g) const;

    //-- grid helpers -----------------------------------------------------------
    /// Pixel bounds of pad @p absIndex within this component.
    /// absIndex is 0-based across both banks (0–31).
    /// Returns empty rect if the pad is not on the current bank page.
    juce::Rectangle<int> cellBounds (int absIndex) const noexcept;

    /// Returns the absolute pad index (0–31) for a given component point,
    /// or -1 if the point is not over a pad on the current bank page.
    int padIndexAt (juce::Point<int> p) const noexcept;

    void drawPad (juce::Graphics& g,
                  juce::Rectangle<int> bounds,
                  int absIndex,
                  bool isEmpty) const;

    static juce::String midiNoteName (int note);

    /// Builds and shows the full right-click context menu for a pad.
    void showPadContextMenu (int idx, juce::Point<int> screenPos);

    /// Opens a color picker (JUCE ColourSelector) in a CallOutBox for the given pad.
    void launchColorPicker (int idx, juce::Rectangle<int> padScreenBounds);

    /// Inline draggable slider row used inside the right-click PopupMenu.
    class MenuSliderItem;

    //==========================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadGridView)
};
