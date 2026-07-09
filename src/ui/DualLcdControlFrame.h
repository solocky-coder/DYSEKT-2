#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class DysektProcessor;

/**  Centre control frame between the two LCD panels.
 *
 *   Top row:    [FIL icon] [WA icon] [CH icon]  [sliceCount chip]
 *   Bottom row: [ROOT knob]  [PITCH knob]  [VOL knob]
 *
 *   No child components — everything is drawn in paint() and hit-tested
 *   in mouseDown() so child buttons can't paint over the custom icons.
 */
class DualLcdControlFrame : public juce::Component
{
public:
    explicit DualLcdControlFrame (DysektProcessor& p);

    void paint        (juce::Graphics& g) override;
    void mouseDown    (const juce::MouseEvent& e) override;
    void mouseDrag    (const juce::MouseEvent& e) override;
    void mouseUp      (const juce::MouseEvent& e) override;
    void mouseMove    (const juce::MouseEvent& e) override;
    void mouseExit    (const juce::MouseEvent& e) override;

    std::function<void()>    onBrowserToggle;
    std::function<void()>    onWaveToggle;
    std::function<void()>    onMidiFollowToggle;
    std::function<void()>    onBodeToggle;
    std::function<void()>    onEqToggle;
    std::function<void()>    onSeqToggle;
    std::function<void(int)> onUiModeChanged;   // 0 = Edit, 1 = SFZ Player

    void setBrowserActive    (bool v) { browserActive    = v; repaint(); }

    /** Legacy bool helper — kept for backward compat; mode 0 = inactive. */
    void setWaveActive       (bool v) { setWaveMode (v ? 1 : 0); }

    /** Set the current waveform display mode (0-7) and update the icon. */
    void setWaveMode         (int m)  { waveMode = juce::jlimit (0, 7, m); repaint(); }

    void setMidiFollowActive (bool v) { midiFollowActive = v; repaint(); }
    void setBodeActive       (bool v) { bodeActive       = v; repaint(); }
    void setEqActive         (bool v) { eqActive         = v; repaint(); }
    /** Set active tab: 0 = SLICER, 1 = SFZ-PLAYER, 2 = SF2-PLAYER */
    void setUiTab            (int t)  { uiTab = juce::jlimit (0, 2, t); repaint(); }
    /** Legacy helper: maps bool to uiTab 0/1. */
    void setPadGridActive    (bool v) { setUiTab (v ? 1 : 0); }
    void setSeqActive        (bool v) { seqActive        = v; repaint(); }

private:
    void drawIcon (juce::Graphics& g, juce::Rectangle<float> b, int type, bool active, bool hovered = false);

    DysektProcessor& processor;

    // Icon toggle state
    bool browserActive    = false;
    int  waveMode         = 0;   // 0=Hard 1=Soft 2=Outline 3=Rectified 4=Mirrored 5=Bars 6=RMS 7=Stepped
    bool midiFollowActive = false;
    bool bodeActive       = false;
    bool eqActive         = false;
    int  uiTab            = 0;   // 0=SLICER, 1=SFZ-PLAYER, 2=SF2-PLAYER
    bool seqActive        = false;

    // Hit areas (set during paint, used in mouseDown)
    juce::Rectangle<int> filIconArea;
    juce::Rectangle<int> waIconArea;
    juce::Rectangle<int> midiFollowIconArea;
    juce::Rectangle<int> bodeIconArea;
    juce::Rectangle<int> seqIconArea;
    juce::Rectangle<int> eqIconArea;
    juce::Rectangle<int> sfzIconArea;  // kept as unused placeholder for layout math
    juce::Rectangle<int> editTabArea;
    juce::Rectangle<int> padTabArea;
    juce::Rectangle<int> sfzPlayerTabArea;   // third tab: SFZ-PLAYER
    juce::Rectangle<int> pitchKnobArea;
    juce::Rectangle<int> volKnobArea;

    int        hoveredIcon   = -1;   // 0=FIL 1=WA 2=MIDI 3=MIXER 4=SEQ, -1=none
    enum class DragTarget { None, Pitch, Volume };
    DragTarget dragTarget    = DragTarget::None;
    float  dragStartValue    = 0.0f;
    int    dragStartY        = 0;
    int    dragStartX        = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DualLcdControlFrame)
};
