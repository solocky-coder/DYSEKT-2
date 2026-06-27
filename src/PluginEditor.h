#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "ui/LogoBar.h"
#include "ui/HeaderBar.h"
#include "params/ParamIds.h"
#include "ui/SliceLane.h"
#include "ui/SliceControlBar.h"
#include "ui/WaveformView.h"
#include "ui/ShortcutsPanel.h"
#include "ui/FileBrowserPanel.h"
#include "ui/MixerPanel.h"
#include "ui/TrimDialog.h"
#include "ui/MidiLearnDialog.h"
#include "ui/ConfirmOverlay.h"
#include "ui/RenameOverlay.h"
#include "ui/ThemeEditorPanel.h"
#include "TrimSession.h"
#include "ui/SliceLcdDisplay.h"
#include "ui/SliceWaveformLcd.h"
#include "ui/Sf2LcdDisplay.h"
#include "ui/Sf2WaveformLcd.h"
#include "ui/WaveformOverview.h"
#include "ui/SfzDropdownPanel.h"
#include "ui/SfzPlayerDropdownPanel.h"
#include "ui/GlobalEqPanel.h"
#include "ui/PadGridView.h"
#if DYSEKT_STANDALONE
#include "ui/PianoRollPanel.h"
#include "ui/ArrangeView.h"
#endif

// ── Layout constants ──────────────────────────────────────────────────────────
#include "ui/PluginEditorConstants.h"

class DysektEditor : public juce::AudioProcessorEditor,
                     public juce::FileDragAndDropTarget,
                     private juce::Timer
{
public:
    explicit DysektEditor (DysektProcessor&);
    ~DysektEditor() override;

    void paint              (juce::Graphics&) override;
    void paintOverChildren  (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;


    juce::StringArray getAvailableThemes();
    void applyTheme (const juce::String& themeName);

    void toggleBrowserPanel();
    void toggleSoftWave();
    void toggleMidiFollow();

    void showTrimDialog (const juce::File& file, bool isRelink = false);
    void showTrimMode   (const juce::File& file);

    /// Switch between interface modes.
    /// 0 = Waveform View (original), 1 = SFZ Player.
    void setUiMode (int mode);

    /** Derive and apply the correct MidiRouteMode from the current uiMode and
     *  activeSlot.  Call this whenever either changes instead of repeating the
     *  inline ternary everywhere.
     *
     *  Rules:
     *    activeSlot == Seq           → Sequencer
     *    uiMode == 1 (SFZ panel)     → SfPlayer
     *    otherwise                   → Slicer
     */
    void syncMidiRouteMode();

    /** The centred, aspect-correct sub-rectangle (always kBaseW:kTotalH)
     *  that the actual UI is laid out within. The real component can be
     *  any size/aspect a host gives it — we no longer force a fixed
     *  aspect ratio on the component itself, since a hosted plugin can't
     *  reposition its own floating window to recentre after a hard clamp.
     *  Instead resized() recomputes this each time and everything lays
     *  out relative to it, with paint() filling plain background in the
     *  surrounding letterbox margins. */
    const juce::Rectangle<int>& getDesignArea() const noexcept { return designArea; }

private:
    void timerCallback() override;
    void ensureDefaultThemes();
    void saveUserSettings (const juce::String& themeName);
    void loadUserSettings();

    DysektProcessor& processor;
    float    lastZoom              = -1.0f;
    float    lastScroll            = -1.0f;
    int      lastMidiFollowSlice   = -1;
    int      timerHz               = 30;
    bool     lastWaveformAnimating = false;
    bool     lastPreviewActive     = false;
    uint32_t lastUiSnapshotVersion = 0;
    int      lastNumSlices         = -1;
    bool     lastTrimActive        = false;

    /** True once the SF-player zone matrix has been successfully populated
     *  after the current sfzPlayer load.  Reset to false when a new file
     *  is queued so the timer re-runs panelDidShow on the next load. */
    bool sfzPanelRestored = false;

    /** Same for SFZ-Player (sfzPlayer2). */
    bool sfzPlayer2PanelRestored = false;

    /// Which panel occupies the bottom slot (browser or mixer).
    /// Mutually exclusive.
    enum class SlotContent { None, Browser, Mixer, Eq, Seq };
    SlotContent activeSlot   = SlotContent::None;
    bool initBrowserOpen     = false;  // true until the first real sample is loaded
    int  waveformMode = 0;  // 0=Hard 1=Soft 2=Outline 3=Rectified 4=Mirrored 5=Bars 6=RMS 7=Stepped

    /// Current interface layout mode.
    /// 0 = Waveform View (original UI — never overwritten).
    /// 1 = SFZ Player.
    int  uiMode = 0;
    bool showPadGrid     = false;  ///< true = PadGridView, false = WaveformView (within uiMode 0)
    bool hasSampleLoaded = false;   // true once a sample with audio is loaded
    bool hasSampleLoaded2 = false;  // true once SFZ-PLAYER (sliceManager2/sampleData2) has a real sample loaded

    /// Centred kBaseW:kTotalH sub-rectangle the UI is actually laid out
    /// within; recomputed at the top of every resized() call. See
    /// getDesignArea() above for why this exists instead of a hard
    /// component-level aspect-ratio lock.
    juce::Rectangle<int> designArea;

    std::unique_ptr<TrimSession>       trimSession;
    std::unique_ptr<TrimDialog>        trimDialog;
    std::unique_ptr<juce::Component>   midiLearnBackdrop;
    std::unique_ptr<MidiLearnDialog>   midiLearnDialog;
    std::unique_ptr<ConfirmOverlay>    confirmOverlay;
    std::unique_ptr<RenameOverlay>     renameOverlay;
    std::unique_ptr<ThemeEditorPanel>  themeEditorPanel;

    DysektLookAndFeel lnf;

    LogoBar         logoBar;
    HeaderBar       headerBar;

    SliceLcdDisplay  sliceLcd;
    SliceWaveformLcd sliceWaveformLcd;
    Sf2LcdDisplay    sf2Lcd;
    Sf2WaveformLcd   sf2WaveformLcd;

    SliceLane        sliceLane;
    WaveformView     waveformView;
    WaveformOverview waveformOverview;
    SliceControlBar  sliceControlBar;

    FileBrowserPanel browserPanel;
    MixerPanel       mixerPanel;
    PadGridView      padGridView;
    SfzDropdownPanel       sfzDropdown;
    SfzPlayerDropdownPanel sfzPlayerDropdown;
    ShortcutsPanel   shortcutsPanel { processor };
    GlobalEqPanel    eqPanel;
#if DYSEKT_STANDALONE
    PianoRollWindow  pianoRollPanel { processor.sequencer, lnf, &processor.abletonLink };
    ArrangeView      arrangeView    { processor.sequencer, &processor.abletonLink };
#endif

    juce::TooltipWindow tooltipWindow { this, 500 };

    void toggleMixerPanel();
    void toggleEqPanel();
    void toggleShortcutsPanel();
    void toggleThemeEditor();
    void toggleSeqPanel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DysektEditor)
};
