#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

class DysektProcessor;

/// Modal overlay panel displaying keyboard shortcuts and global preferences.
/// An "RTFM" button in the settings panel opens the embedded PDF in the
/// system default viewer.
///
/// Shown via ? or the "?" button in ActionPanel.
/// Dismissed by pressing Escape or clicking the close button.
class ShortcutsPanel : public juce::Component
{
public:
    explicit ShortcutsPanel (DysektProcessor& proc);
    ~ShortcutsPanel() override;

    void paint   (juce::Graphics& g) override;
    void resized () override;
    bool keyPressed (const juce::KeyPress& key) override;
    void mouseDown  (const juce::MouseEvent& e) override;

    /// Called when the panel should be dismissed.
    std::function<void()> onDismiss;

    /// Called when the user clicks "Theme Editor" — opens ThemeEditorPanel.
    std::function<void()> onThemeRequest;

    /// Called when the user selects an interface mode.
    /// Argument: 0 = Waveform View, 1 = Pad Grid.
    std::function<void(int)> onUiModeChanged;

    /// Set this to the current UI mode before making the panel visible,
    /// so the correct radio button appears selected.
    int currentUiMode = 0;

private:
    DysektProcessor& processor;

    // ── Manual button ─────────────────────────────────────────────────────────
    juce::TextButton rtfmBtn { "RTFM" };

    /// Extracts the embedded BinaryData PDF to a temp file (once) and opens
    /// it in the system default viewer.
    void openManualPdf();

    // ── Settings widgets ──────────────────────────────────────────────────────
    struct ShortcutEntry    { juce::String keys, description; };
    struct ShortcutCategory { juce::String title; std::vector<ShortcutEntry> entries; };

    juce::TextButton closeBtn     { juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) };
    juce::TextButton themeBtn     { "Theme Editor..." };
    juce::TextEditor searchBox;
    juce::Label      titleLabel;

    std::vector<ShortcutCategory> categories;
    juce::String currentFilter;

    juce::Rectangle<int> trimAlwaysRect, trimNeverRect, trimLongRect;
    juce::Rectangle<int> uiModeWaveRect, uiModeGridRect;

    void buildShortcutData();
    void drawTrimPrefsSection (juce::Graphics& g, juce::Rectangle<int>& area);
    void drawInterfaceSection (juce::Graphics& g, juce::Rectangle<int>& area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShortcutsPanel)
};
