#pragma once
// =============================================================================
//  SfzFileBrowser.h  —  Self-contained inline directory browser component
// =============================================================================
//  Shared by SfzDropdownPanel and SfzModulePanel.
//  Mode::kSfz      — lists *.sf2 / *.sfz, onFileChosen fires on selection
//  Mode::kAddZone  — lists audio files (wav/aif/flac/ogg), onFileChosen fires
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>

class SfzFileBrowser : public juce::Component,
                       public juce::ListBoxModel,
                       private juce::Timer
{
public:
    /** Controls which file types are listed and what happens on selection. */
    enum class Mode { kSfz, kSf2, kAddZone };

    /** Called when the user double-clicks a file (type depends on current mode). */
    std::function<void (const juce::File&)> onFileSingleClicked;
    std::function<void (const juce::File&)> onFileChosen;
    /** Called after a new SF2/SFZ file has been accepted (any path). */
    std::function<void (const juce::File&)> onFileLoaded;
    /** Called when the user explicitly closes the browser (Esc / icon click). */
    std::function<void()> onDismiss;

    SfzFileBrowser();
    void setMode (Mode m);               ///< Switch between SFZ-pick and sample-pick modes
    Mode getMode () const { return mode; }
    ~SfzFileBrowser() override;

    // juce::Component overrides
    void paint   (juce::Graphics&) override;
    void resized () override;
    void mouseDown  (const juce::MouseEvent&) override;
    void mouseMove  (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override { return false; }

    // Open to a specific root directory (call before making visible)
    void setRootDirectory (const juce::File& dir);
    void showDrives   ();
    bool hasNavigated () const { return navigated; }
    juce::File getCurrentDirectory() const { return currentDir; }

    // ── ListBoxModel ──────────────────────────────────────────────────────────
    int  getNumRows()                                                  override;
    void paintListBoxItem (int row, juce::Graphics& g,
                           int w, int h, bool selected)               override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&)  override;
    void listBoxItemClicked       (int row, const juce::MouseEvent&)  override;
    juce::String getTooltipForRow (int row)                           override;

private:
    void   navigateTo      (const juce::File& dir);
    void   navigateUp      ();
    void   navigateToRoots ();
    void   loadRow         (int row);
    juce::File fileForRow  (int row) const;
    bool   isDirectory     (int row) const;

    void timerCallback() override;
    void rebuildList();

    Mode       mode       { Mode::kSfz };
    juce::File currentDir;

    juce::Array<juce::File> rows;
    juce::ListBox list;

    juce::Rectangle<int> breadcrumbZone;
    juce::Rectangle<int> upBtnZone;
    bool                 atVirtualRoot { false };
    bool                 navigated     { false };

    static constexpr int kBreadcrumbH = 22;
    static constexpr int kRowH        = 26;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SfzFileBrowser)
};
