#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

class DysektProcessor;
class WaveformView;

class ActionPanel : public juce::Component
{
public:
    ActionPanel (DysektProcessor& p, WaveformView& wv);
    ~ActionPanel() override;
    void resized() override;
    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;

    /// Invoked when the user clicks the "?" button to open the shortcuts panel.
    std::function<void()> onShortcutsToggle;

    /// Invoked when the user clicks the SEQ button.
    std::function<void()> onSeqToggle;

    // Callbacks wired up by DysektEditor
    void setBrowserActive    (bool v) { browserActive    = v; repaint(); }
    void setWaveActive       (bool v) { waveActive       = v; repaint(); }
    void setChromaticActive  (bool v) { chromaticActive  = v; repaint(); }
    void setTrimActive       (bool v);   // disables/re-enables slice buttons

private:
    DysektProcessor& processor;
    WaveformView&    waveformView;

    bool browserActive    = false;
    bool waveActive       = false;
    bool chromaticActive  = false;

    void updateToggleBtn (juce::TextButton& btn, bool active);
    void updateMidiButtonAppearance (bool active);

    juce::TextButton lazyChopBtn    { "LAZY" };
    juce::TextButton shortcutsBtn   { "HELP" };
    juce::TextButton seqBtn         { "SEQ"  };
};
