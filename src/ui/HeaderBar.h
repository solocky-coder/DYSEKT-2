#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "DualLcdControlFrame.h"

class DysektProcessor;

class HeaderBar : public juce::Component
{
public:
    explicit HeaderBar (DysektProcessor& p);
    void paint            (juce::Graphics& g) override;
    void resized          () override;
    void mouseDown        (const juce::MouseEvent& e) override;
    void mouseDrag        (const juce::MouseEvent& e) override;
    void mouseUp          (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

    // Callbacks set by PluginEditor
    std::function<void()> onBrowserToggle;
    std::function<void()> onWaveToggle;
    std::function<void()> onMidiFollowToggle;
    std::function<void()> onBodeToggle;
    std::function<void()> onEqToggle;

    // State sync
    void setBrowserActive    (bool v);
    /** Legacy bool — kept for backward compat (maps to mode 0 / 1). */
    void setWaveActive       (bool v);
    /** Set current waveform mode (0-7) so the icon updates. */
    void setWaveMode         (int m);
    void setMidiFollowActive (bool v);
    void setBodeActive       (bool v);
    void setEqActive         (bool v);

    /** Returns the control frame component (FIL/WA/CH icons + ROOT/PITCH/VOL knobs).
     *  PluginEditor adds this as a visible child and positions it between the LCDs. */
    juce::Component* getControlFrame() { return &controlFrame; }

    /** Typed getter — gives PluginEditor direct access to DualLcdControlFrame API. */
    DualLcdControlFrame& dualFrame() { return controlFrame; }

    // Header buttons
    juce::TextButton undoBtn      { "UNDO"  };
    juce::TextButton redoBtn      { "REDO"  };
    juce::TextButton panicBtn     { "PANIC" };
    juce::TextButton shortcutsBtn;

    std::function<void()> onShortcutsToggle;
    std::function<void()> onSeqToggle;   ///< Forwarded from DualLcdControlFrame::onSeqToggle

    void setSeqActive (bool v);

    void showThemePopup();

private:
    void openRelinkBrowser();

    DysektProcessor& processor;

    DualLcdControlFrame controlFrame;

    std::unique_ptr<juce::FileChooser> fileChooser;
    std::unique_ptr<juce::TextEditor>  textEditor;

    juce::Rectangle<int> sampleInfoBounds;
    juce::Rectangle<int> rootNoteArea;
    juce::Rectangle<int> slicesInfoArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderBar)
};
