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
#include "ui/KeysPanel.h"
#include "ui/AddZoneOverlay.h"
#include "ui/SaveSfzOverlay.h"
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
    void syncBrowserMode();   // keeps browserPanel's file-type filter matched to uiMode — see toggleBrowserPanel

    /** The editor's full local bounds. No aspect-ratio lock and no letterbox —
     *  the editor accepts whatever size/aspect the host gives it. Kept as a
     *  member + getter (rather than just calling getLocalBounds() inline)
     *  so paint()/paintOverChildren()/the free-function waveformFrameRect()
     *  helper all read the same up-to-date value without needing their own
     *  Component reference plumbing. */
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

#if JUCE_WINDOWS && ! DYSEKT_STANDALONE
    // State for the host editor-window resize desync watchdog — see
    // timerCallback() in PluginEditor.cpp for the full explanation.
    int peerMismatchTicks = 0;
    int lastPeerMismatchW  = 0;
    int lastPeerMismatchH  = 0;
#endif
    int  waveformMode = 0;  // 0=Hard 1=Soft 2=Outline 3=Rectified 4=Mirrored 5=Bars 6=RMS 7=Stepped

    /// Current interface layout mode.
    /// 0 = Waveform View (original UI — never overwritten).
    /// 1 = SFZ Player.
    int  uiMode = 0;
    bool showPadGrid     = false;  ///< true = PadGridView, false = WaveformView (within uiMode 0)
    bool showZoneBuilder = false;  ///< true = zoneBuilderKeysPanel, false = WaveformView (within uiMode 1 / SFZ-PLAYER)
    bool hasSampleLoaded = false;   // true once a sample with audio is loaded
    bool hasSampleLoaded2 = false;  // true once SFZ-PLAYER (sliceManager2/sampleData2) has a real sample loaded

    // ── SFZ-PLAYER zone builder (ZONES toggle in SliceControlBar) ──────────────
    // Ported from SfzPlayerDropdownPanel's existing (but never-shown) Add Zone /
    // Save SFZ flow — same target-file bookkeeping, same processor.sfzPlayer2
    // calls, just driven by the live uiMode==1 layout instead of a hidden panel.
    juce::File zoneBuilderTargetSfz;   // .sfz currently being built/edited, may be empty
    int        zoneBuilderPrevHiKey = -1;
    std::unique_ptr<juce::FileChooser> zoneBuilderSampleChooser;
    std::unique_ptr<AddZoneOverlay>    zoneAddOverlay;
    std::unique_ptr<SaveSfzOverlay>    zoneSaveOverlay;

    void openZoneBuilderAddZone();               // [+ ZONE] click -> pick sample -> AddZoneOverlay
    void showZoneBuilderAddZoneOverlay (const juce::File& sfzFile,
                                         const juce::File& sampleFile,
                                         int prevHiKey);
    void openZoneBuilderSaveAsNew (const juce::File& sampleFile); // no SFZ loaded yet -> name one first
    static bool appendZoneToSfz (const juce::File& sfzFile, const juce::File& sampleFile,
                                  int loKey, int hiKey, int rootKey);
    void hideZoneBuilderOverlays();
    void refreshZoneBuilderMatrix (const juce::File& sfzFile); // re-parse + push into zoneBuilderKeysPanel

    /// The editor's full local bounds, cached each resized() so paint()/
    /// paintOverChildren()/waveformFrameRect() can read it. See
    /// getDesignArea() above.
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
    KeysPanel        zoneBuilderKeysPanel { processor }; // SFZ-PLAYER ZONES view — standalone, NOT sfzPlayerDropdown.keysPanel
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
