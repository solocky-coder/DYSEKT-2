#include "PluginEditor.h"
#include "ui/DysektLookAndFeel.h"
#include "ui/PluginEditorConstants.h"
#include "ui/LogoIcon.h"
#include "ui/UIHelpers.h"

#if JUCE_WINDOWS && ! DYSEKT_STANDALONE
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

// ========================== FILEPATH HELPERS ==========================
static juce::File getSettingsDir()
{
 return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
 .getChildFile ("DYSEKT-SF");
}
static juce::File getUserSettingsFile() { return getSettingsDir().getChildFile ("settings.yaml"); }
static juce::File getThemesDir() { return getSettingsDir().getChildFile ("themes"); }

// ========================= CLASS CONSTRUCTOR ==========================
DysektEditor::DysektEditor (DysektProcessor& p)
 : AudioProcessorEditor (p),
 processor (p),
 logoBar (p),
 headerBar (p),
 sliceLcd (p),
 sliceWaveformLcd (p),
 sf2Lcd (p),
 sf2WaveformLcd (p),
 sliceLane (p),
 waveformView (p),
 waveformOverview (p),
 sliceControlBar (p),
 browserPanel (p),
 mixerPanel (p),
      eqPanel (p),
 sfzDropdown (p),
 sfzPlayerDropdown (p),
 padGridView (p),
 shortcutsPanel (p)
{
 juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
 setLookAndFeel (&lnf);

 addAndMakeVisible (logoBar);
 addAndMakeVisible (headerBar);

 addAndMakeVisible (sliceLcd);
 addAndMakeVisible (sliceWaveformLcd);
 addAndMakeVisible (sf2Lcd);
 addAndMakeVisible (sf2WaveformLcd);
 sf2Lcd.setVisible (false);
 sf2WaveformLcd.setVisible (false);
 if (auto* cf = headerBar.getControlFrame())
 addAndMakeVisible (*cf);

 addAndMakeVisible (sliceLane);
 addAndMakeVisible (waveformView);
 addAndMakeVisible (waveformOverview);
 addAndMakeVisible (sliceControlBar);
  browserPanel.setVisible (false);
 addChildComponent (browserPanel);
 mixerPanel.setVisible (false);
 addChildComponent (mixerPanel);
 eqPanel.setVisible (false);
 addChildComponent (eqPanel);

 sfzDropdown.setVisible (false);
 addChildComponent (sfzDropdown);

 sfzPlayerDropdown.setVisible (false);
 addChildComponent (sfzPlayerDropdown);
 sfzPlayerDropdown.onFileLoaded = [this] (const juce::File&)
 {
     sfzPlayer2PanelRestored = false;
 };

 addChildComponent (padGridView);
 padGridView.onRenameRequest = [this] (int sliceIdx, const juce::String& currentName)
 {
     renameOverlay = std::make_unique<RenameOverlay> (sliceIdx + 1, currentName);
     addAndMakeVisible (*renameOverlay);
     renameOverlay->setBounds (getLocalBounds());
     renameOverlay->toFront (true);
     renameOverlay->onResult = [this, sliceIdx] (const juce::String& newName, bool cancelled)
     {
         renameOverlay.reset();
         if (! cancelled)
         {
             DysektProcessor::Command cmd;
             cmd.type = DysektProcessor::CmdSetSliceName;
             cmd.intParam1 = sliceIdx;
             cmd.stringParam = newName;
             processor.pushCommand (cmd);
         }
     };
 };
 sliceControlBar.onPadViewToggle = [this] (bool on)
 {
     showPadGrid = on;
     resized();
     repaint(); // clear waveform/overview areas vacated by the old view
 };
 sliceControlBar.onZoneViewToggle = [this] (bool on)
 {
     if (! on && zoneBuilderDirty)
     {
         // SliceControlBar already flipped its own zoneViewActive to false
         // before calling us (see its mouseDown) — put it back to true until
         // the user actually resolves the prompt below, so the toggle keeps
         // reading as "on" while Save/Discard is pending.
         sliceControlBar.setZoneViewActive (true);

         confirmOverlay = std::make_unique<ConfirmOverlay> (
             "Unsaved Zones",
             "You have zones staged that haven't been saved yet. Save them before leaving?",
             "Save",
             "Discard");
         addAndMakeVisible (*confirmOverlay);
         confirmOverlay->setBounds (getLocalBounds());
         confirmOverlay->toFront (true);
         confirmOverlay->onResult = [this] (bool save)
         {
             confirmOverlay.reset();
             if (save)
                 commitZoneBuilderPendingZones();
             else
                 discardZoneBuilderPendingZones();

             showZoneBuilder = false;
             sliceControlBar.setZoneViewActive (false);
             resized();
             repaint();
         };
         return;
     }

     showZoneBuilder = on;
     if (on)
     {
         // zoneBuilderTargetSfz is set synchronously in onLoadRequest at
         // load time (see comment there) — just reflect it here, no engine
         // query needed. Staged pending zones (if any survived a tab switch
         // back in) still need the scratch preview, not the raw on-disk file.
         if (zoneBuilderDirty)
             refreshZoneBuilderScratch();
         else
             refreshZoneBuilderMatrix (zoneBuilderTargetSfz);
     }
     resized();
     repaint(); // clear waveform/overview areas vacated by the old view
 };
 sliceControlBar.onZoneSaveRequested = [this] { commitZoneBuilderPendingZones(); };
 // SFZ-PLAYER Zones view previews/highlights sfzPlayer2's notes, not the
 // legacy SF-Player's — must be set before any note requests are made.
 zoneBuilderKeysPanel.setEngineSource (KeysPanel::EngineSource::SfzPlayer2);
 zoneBuilderKeysPanel.setSfzEditable (true);
 zoneBuilderKeysPanel.setAddZoneButtonVisible (true);
 zoneBuilderKeysPanel.onAddZoneRequested = [this] { openZoneBuilderAddZone(); };
 addChildComponent (zoneBuilderKeysPanel); // hidden until showZoneBuilder is true
 // When a new SF2/SFZ is loaded from the dropdown, reset the restore flag
 // so the timer re-populates the zone matrix on the next completed load.
 sfzDropdown.onFileLoaded = [this] (const juce::File&)
 {
     sfzPanelRestored = false;
 };

#if DYSEKT_STANDALONE
 // SFZ loaded → add one sequencer track automatically (channel 15, 0-based).
 // SF2 loaded → fires once preset list is ready; no tracks yet — user assigns
 // per-preset channels by right-clicking preset rows in the program grid.
#endif

 // SF2 preset right-clicked → user assigned a MIDI channel → create track.
 // NOTE: must run in BOTH standalone and plugin builds — mixerPanel is an
 // unconditional member (see PluginEditor.h), not standalone-only.
 // Only the sequencer piano-roll track is standalone-specific.
 sfzDropdown.onPresetChannelAssigned = [this] (const Sf2PresetInfo& preset, int midiChannel1Based)
 {
     // Keep the inline channel-FX panel in sync
     sfzDropdown.notifyPresetChannelChanged (preset.name, midiChannel1Based);

     // Update the SF2 mixer panel — rebuild strips from the current
     // preset→channel map so the new assignment appears immediately.
     mixerPanel.setActiveChannels (sfzDropdown.getProgramGrid().getPresets(),
                                   sfzDropdown.getProgramGrid().getPresetChannels());

#if DYSEKT_STANDALONE
     // Pick a colour based on the preset number (bank*128 + program).
     static const juce::Colour kPalette[] = {
         juce::Colour (0xFF4060A0), juce::Colour (0xFF60A040),
         juce::Colour (0xFFA04060), juce::Colour (0xFF40A0A0),
         juce::Colour (0xFFA0A040), juce::Colour (0xFF8060C0),
     };
     const int colIdx = (preset.bank * 128 + preset.preset) % 6;
     pianoRollPanel.addOrUpdateSfPresetTrack (preset, midiChannel1Based, kPalette[colIdx]);
#endif
 };

#if DYSEKT_STANDALONE
 // Track-header right-click on an SF track → change MIDI channel.
 pianoRollPanel.onSfTrackChannelChanged = [this] (int trackIndex, int midiChannel1Based)
 {
     const auto info = pianoRollPanel.getTrackInfo (trackIndex);
     if (info.type == TrackType::SfPlayer)
         pianoRollPanel.addOrUpdateSfPresetTrack (info.preset, midiChannel1Based, info.colour);
 };
#endif
 shortcutsPanel.setVisible (false);
 addChildComponent (shortcutsPanel);
#if DYSEKT_STANDALONE
    addChildComponent (pianoRollPanel);
    addChildComponent (arrangeView);

    // Wire ArrangeView double-click → show PianoRollPanel as overlay
    arrangeView.onClipDoubleClicked = [this] (int trackIndex, int clipIndex)
    {
        pianoRollPanel.openFor (trackIndex, clipIndex);
        resized();
    };

    // Route live MIDI to the right engine based on which track type is selected.
    // SF-player track → Sequencer mode (channel mask already set by ArrangeView).
    // Slicer track (MainSlice / ChromaticSlice) → Slicer mode.
    // Nothing selected → Sequencer mode with mask=0 (no live input).
    arrangeView.onTrackTypeSelected = [this] (TrackType type, bool hasSelection)
    {
        if (activeSlot != SlotContent::Seq) return;
        using Mode = DysektProcessor::MidiRouteMode;
        processor.setMidiRouteMode (
            (hasSelection && type != TrackType::SfPlayer)
                ? Mode::Slicer
                : Mode::Sequencer);
    };
#endif
 shortcutsPanel.onDismiss = [this] { toggleShortcutsPanel(); };
 shortcutsPanel.onThemeRequest = [this]
 {
 shortcutsPanel.setVisible (false);
 toggleThemeEditor();
 };
 // Interface mode toggle — switching routes through setUiMode() so the
 // original waveform UI is never destroyed, just hidden.
 headerBar.dualFrame().onUiModeChanged = [this] (int mode) { setUiMode (mode); };

 sliceLane.setWaveformView (&waveformView);

 browserPanel.onFileLoaded = [this]
 {
 // Close whichever browser mode is active once a file has been chosen
 if (initBrowserOpen)
 {
 initBrowserOpen = false;
 browserPanel.setVisible (false);
 headerBar.setBrowserActive (false);
 resized(); repaint();
 }
 else if (activeSlot == SlotContent::Browser)
 {
 toggleBrowserPanel();
 }
 };
 browserPanel.onLoadRequest = [this] (const juce::File& f)
 {
     const auto ext = f.getFileExtension().toLowerCase();
     if (uiMode == 0)
     {
         // SLICER: audio files only — SFZ/SF2 are silently ignored
         if (ext != ".sfz" && ext != ".sf2")
             showTrimDialog (f);
         return;
     }
     if (uiMode == 1)
     {
         // SFZ-PLAYER: .sfz only, routed to sfzPlayer2 (ch2)
         if (ext == ".sfz")
         {
             processor.sfzPlayer2.loadFile (f, processor.fileLoadPool);
             processor.loadSoundFontAsync (f, SoundFontLoadTarget::SfzPlayer2);   // waveform preview -> sampleData2

             // Neither sfzPlayer2 (never .process()'d) nor sampleData2's
             // DecodedSample (SoundFontLoader only sets ->fileName, never
             // ->filePath) reliably tracks which .sfz is loaded. The file is
             // already known right here though, so just remember it directly
             // rather than querying engine state that doesn't carry it.
             zoneBuilderTargetSfz = f;

             // A different .sfz just became the target — any zones staged
             // against the previous target no longer apply. Drop them rather
             // than silently carrying them (and their scratch file, which was
             // built against the old file's contents) across the switch.
             zoneBuilderPendingZones.clear();
             zoneBuilderDirty = false;
             sliceControlBar.setZoneDirty (false);
             if (zoneBuilderScratchFile.existsAsFile())
                 zoneBuilderScratchFile.deleteFile();
             zoneBuilderScratchFile = juce::File();

             if (showZoneBuilder)
                 refreshZoneBuilderMatrix (zoneBuilderTargetSfz);
         }
         return;
     }
     if (uiMode == 2)
     {
        // SF2-PLAYER: SF2 files only. Routing through sfzDropdown.onFileChosen()
        // (rather than duplicating the load logic here) keeps that as the single
        // source of truth -- it also stores sfPlayerChannelMask, opens the SF2
        // program grid, and fires onFileLoaded, matching the drag-and-drop path.
         if (ext == ".sf2")
             sfzDropdown.onFileChosen (f);
     }
 };
 waveformView.onLoadRequest = [this] (const juce::File& f)
 {
     const auto ext = f.getFileExtension().toLowerCase();
     if (ext != ".sfz" && ext != ".sf2")
         showTrimDialog (f);
 };
 waveformView.onShortcutsToggle = [this] { toggleShortcutsPanel(); };
 waveformView.onRenameRequest = [this] (int sliceIdx, const juce::String& currentName)
 {
 renameOverlay = std::make_unique<RenameOverlay> (sliceIdx + 1, currentName);
 addAndMakeVisible (*renameOverlay);
 renameOverlay->setBounds (getLocalBounds());
 renameOverlay->toFront (true);
 renameOverlay->onResult = [this, sliceIdx] (const juce::String& newName, bool cancelled)
 {
 renameOverlay.reset();
 if (! cancelled)
 {
 DysektProcessor::Command cmd;
 cmd.type = DysektProcessor::CmdSetSliceName;
 cmd.intParam1 = sliceIdx;
 cmd.stringParam = newName;
 processor.pushCommand (cmd);
 }
 };
 };
 waveformView.onTrimApplied = [this] (int s, int e)
 {
 processor.applyTrimToCurrentSample (s, e);
 processor.trimModeActive.store (false, std::memory_order_relaxed);
 waveformView.setTrimMode (false);
 trimSession.reset();

 // Destruction is deferred (callAsync) because this callback fires from
 // inside TrimDialog's button onClick — deleting it synchronously would
 // cause a use-after-free on the button.  We remove it as a child component
 // immediately though, so resized() no longer sees it and the trim bar
 // cannot flash behind the waveform view that opens right after.
 if (trimDialog != nullptr)
 {
     trimDialog->setVisible (false);
     trimDialog->setBounds ({});
     removeChildComponent (trimDialog.get());
 }
 juce::MessageManager::callAsync ([dlg = std::shared_ptr<TrimDialog> (std::move (trimDialog))] {});
 resized();
 repaint();
 };
 waveformView.onTrimCancelled = [this]
 {
 processor.trimModeActive.store (false, std::memory_order_relaxed);
 waveformView.setTrimMode (false);
 trimSession.reset();

 if (trimDialog != nullptr)
 {
     trimDialog->setVisible (false);
     trimDialog->setBounds ({});
     removeChildComponent (trimDialog.get());
 }
 juce::MessageManager::callAsync ([dlg = std::shared_ptr<TrimDialog> (std::move (trimDialog))] {});
 resized();
 repaint();
 };

 headerBar.onBodeToggle  = [this] { toggleMixerPanel(); };
 headerBar.onEqToggle    = [this] { toggleEqPanel(); };
 headerBar.onBrowserToggle = [this] { toggleBrowserPanel(); };
 headerBar.onWaveToggle = [this] { toggleSoftWave(); };
 headerBar.onMidiFollowToggle = [this] { toggleMidiFollow(); };
 headerBar.onShortcutsToggle = [this] { toggleShortcutsPanel(); };
#if DYSEKT_STANDALONE
    headerBar.onSeqToggle   = [this] { toggleSeqPanel(); };
#else
    headerBar.onSeqToggle   = nullptr;   // sequencer not present in VST3
#endif

 ensureDefaultThemes();
 loadUserSettings();

 // If SF-Player mode was restored from settings, set up the panel.
 // loadUserSettings() sets uiMode directly (bypassing setUiMode), so
 // the timer-driven sfzPanelRestored path will call panelDidShow() once
 // sfzPlayer.isLoaded() becomes true (async after setStateInformation).
 if (uiMode == 1)
 {
     // SFZ-PLAYER: waveform view — no dropdown panel to restore
 }
 else if (uiMode == 2)
 {
     sfzDropdown.setVisible (true);
     // sfzPlayer2PanelRestored not needed for SF2 (preset grid populates via panelDidShow)
 }

 // Restore the correct MIDI route mode that matches the saved uiMode.
 // setUiMode() wasn't called by loadUserSettings(), so we must do this here.
 syncMidiRouteMode();

 // Keep the tab strip in sync with the restored uiMode — without this the
 // tab highlight defaults to SLICER regardless of which mode was actually
 // restored, so it visually disagrees with the panels resized() shows.
 headerBar.dualFrame().setUiTab (uiMode);

 // Match the browser's file-type filter to the restored uiMode too — without
 // this it stays in kAddZone (Slicer: any audio file) even when uiMode is 1
 // or 2, so a non-.sf2/.sfz file picked from the browser silently fails to
 // load once onLoadRequest routes it by the real (restored) uiMode.
 syncBrowserMode();

 if (processor.sampleData.getSnapshot() == nullptr)
 processor.loadDefaultSampleIfNeeded();

 // Open the browser immediately so the user picks a sample on first launch.
 // initBrowserOpen is cleared automatically once a real sample is loaded.
 {
 auto snap = processor.sampleData.getSnapshot();
 const bool hasReal = snap != nullptr
 && snap->buffer.getNumSamples() > 0
 && ! snap->filePath.containsIgnoreCase ("DYSEKT_default.wav");
 if (! hasReal)
 {
 initBrowserOpen = true;
 browserPanel.setVisible (true);
 headerBar.setBrowserActive (true);
 }
 }

 // Initialise hasSampleLoaded from the real processor state NOW, before
 // setSize() triggers the first resized(). Without this, resized() runs
 // with hasSampleLoaded=false even when a sample is already present
 // (restored via setStateInformation), so the SCB and overview bounds are
 // wrong for the very first paint. The timerCallback would correct them
 // ~33ms later, but hosts often request a paint synchronously during
 // construction — producing the flash of broken layout that disappears on
 // the next open once async state has settled.
 {
     auto initSnap = processor.sampleData.getSnapshot();
     hasSampleLoaded = (initSnap != nullptr && initSnap->buffer.getNumSamples() > 0);
 }
 {
     // Same rationale, for the SFZ-PLAYER tab (sliceManager2/sampleData2).
     auto initSnap2 = processor.sampleData2.getSnapshot();
     hasSampleLoaded2 = (initSnap2 != nullptr && initSnap2->buffer.getNumSamples() > 0);
 }

 setWantsKeyboardFocus (true);
 setResizable (true, true);
 setResizeLimits (kBaseW / 2, kTotalH / 2, 3840, 2160);
 // No setFixedAspectRatio() here — see getDesignArea()/resized() for why:
 // we accept whatever size/aspect the host gives us. Vertical layout (sf)
 // tracks height; extra/less width reflows into the side LCD columns and
 // panel widths instead of forcing a uniform zoom or being letterboxed.
    //
    // Default to 90% of the primary display's usable area on BOTH axes,
    // independently — not capped at kInitW/kInitH ("1.5x design units").
    // That flat 1695x1317 cap used to be the actual ceiling on any screen
    // roomy enough to fit it uncapped (basically anything 1920x1200 or
    // bigger), so the window opened at the same modest fixed size
    // regardless of how much larger the display actually was — looking
    // like a small window sitting in the middle of the screen rather than
    // a window that fills it. There's no aspect lock and no reason to
    // prefer a fixed design size over the screen's own dimensions; just
    // fill 90% of whatever display we're on, leaving a small margin so
    // the title bar/edges stay visibly distinct from the screen edge.
    {
        const auto& displays = juce::Desktop::getInstance().getDisplays();
        const auto* primary  = displays.getPrimaryDisplay();
        const auto  userArea = (primary != nullptr)
                                   ? primary->userArea
                                   : juce::Rectangle<int> (0, 0, 1920, 1080);

        const int w = juce::roundToInt (userArea.getWidth()  * 0.90);
        const int h = juce::roundToInt (userArea.getHeight() * 0.90);
        setSize (w, h);
    }
 lastUiSnapshotVersion = processor.getUiSliceSnapshotVersion();
 startTimerHz (30);
}

DysektEditor::~DysektEditor()
{
 juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
 setLookAndFeel (nullptr);
}

void DysektEditor::visibilityChanged()
{
 // applyWindowIcon() requires a peer (see LogoIcon.h) — the editor doesn't
 // have one yet during construction, so set the icon here once it's
 // actually shown rather than relying solely on the applyTheme() call sites.
 if (isVisible())
     applyWindowIcon (this);
}

// ── MIDI route mode helper ────────────────────────────────────────────────────
void DysektEditor::syncMidiRouteMode()
{
    using Mode = DysektProcessor::MidiRouteMode;
    const Mode mode = (activeSlot == SlotContent::Seq) ? Mode::Sequencer
                    : (uiMode == 1)                    ? Mode::SfzPlayer2
                    : (uiMode == 2)                    ? Mode::SfPlayer
                                                       : Mode::Slicer;
    processor.setMidiRouteMode (mode);
}

void DysektEditor::syncBrowserMode()
{
    // Filter browser files to match the active tab:
    // Slicer → audio files only, SFZ-PLAYER → .sfz only, SF2-PLAYER → .sf2 only
    browserPanel.setBrowserMode (uiMode == 0 ? SfzFileBrowser::Mode::kAddZone
                                : uiMode == 1 ? SfzFileBrowser::Mode::kSfz
                                             : SfzFileBrowser::Mode::kSf2);
}

// ── Interface mode switch ─────────────────────────────────────────────────────
void DysektEditor::setUiMode (int mode)
{
 if (uiMode == mode) return;
 uiMode = mode;
 // Leaving slicer mode — reset pad view to waveform
 if (uiMode != 0) { showPadGrid = false; sliceControlBar.setPadViewActive (false); }
 // Leaving SFZ-PLAYER — reset zone-builder view so it can't leak into other
 // tabs (e.g. keep the SCB spuriously visible in Slicer; see the SCB
 // visibility gate in resized() for why this previously mattered).
 if (uiMode != 1)
 {
     showZoneBuilder = false;
     sliceControlBar.setZoneViewActive (false);

     // Known limitation: unlike the ZONES toggle-off gate (onZoneViewToggle),
     // switching tabs away from SFZ-PLAYER drops any staged-but-unsaved zones
     // with no Save/Discard prompt. Tab switches aren't gated the same way
     // the ZONES button is, so silently discarding here (rather than leaving
     // stale pending zones pointed at a scratch file the user can no longer
     // reach) is the safer of the two bad options.
     zoneBuilderPendingZones.clear();
     zoneBuilderDirty = false;
     sliceControlBar.setZoneDirty (false);
     if (zoneBuilderScratchFile.existsAsFile())
         zoneBuilderScratchFile.deleteFile();
     zoneBuilderScratchFile = juce::File();
 }

 syncBrowserMode();

 // Keep the tab strip in sync (0=SLICER, 1=SFZ-PLAYER, 2=SF2-PLAYER)
 headerBar.dualFrame().setUiTab (uiMode);

 // Slicer note highlights must not appear on SF-player keyboards.
 sfzPlayerDropdown.keysPanel.setSlicerHighlightEnabled (uiMode == 0);

 // Route live MIDI to the active front-end.
 syncMidiRouteMode();

 // SFZ-player mode has no slice cap

 // Show waveform overview for slicer and sfz-player mode
 waveformOverview.setVisible (uiMode == 0 && !showPadGrid);

 // Show/hide sfzDropdown based on mode. sfzPlayerDropdown is no longer
 // shown for any mode: SFZ-PLAYER is now a full second Slicer instance
 // (sliceManager2/voicePool2 — see WaveformView::activeSliceManager and
 // SliceLcdDisplay/SliceWaveformLcd's mode-aware paths), which fully
 // supersedes this panel's knobs/ADSR/file-loading UI built around the
 // now-disconnected sfzPlayer2 live engine.
 // Real tab order (see DualLcdControlFrame::drawTab): 0=SLICER, 1=SFZ-PLAYER, 2=SF2-PLAYER.
 // sfzDropdown drives sfPlayerChannelMask (FluidSynth / SF2-PLAYER), despite its name.
 sfzPlayerDropdown.setVisible (false);
 if (uiMode == 2)
 {
     // SF2-PLAYER: show sfzDropdown (SF2 program grid)
     sfzDropdown.setVisible (true);
     // Re-sync presetList from the processor and restore/reopen the program
     // grid. Without this, switching away to Slicer/SFZ-PLAYER and back left
     // the grid's own cached state stale — it never got a chance to refresh
     // between setVisible(false) and setVisible(true), so it came back empty
     // even though the file was still loaded the whole time.
     sfzDropdown.panelDidShow();
 }
 else
 {
     sfzDropdown.setVisible (false);
 }

 // Persist the new mode
 saveUserSettings (getTheme().name);

 resized();
 repaint();
}

// ─────────────────────────────────────────────────────────────────────────────
void DysektEditor::toggleBrowserPanel()
{
    // If the init browser is open, the first browser-button press simply
    // closes it — exactly like a normal close — so the view behind
    // (waveform or SFZ player) becomes immediately visible.
    if (initBrowserOpen)
    {
        initBrowserOpen = false;
        browserPanel.setVisible (false);
        headerBar.setBrowserActive (false);
        resized();
        repaint();
        return;
    }

    if (activeSlot == SlotContent::Browser)
    {
        activeSlot = SlotContent::None;
        browserPanel.setVisible (false);
        headerBar.setBrowserActive (false);
    }
    else
    {
        if (activeSlot == SlotContent::Mixer)
        {
            activeSlot = SlotContent::None;
            mixerPanel.setVisible (false);
            headerBar.setBodeActive (false);
        }
        else if (activeSlot == SlotContent::Eq)
        {
            eqPanel.setVisible (false);
            headerBar.setEqActive (false);
        }
        else if (activeSlot == SlotContent::Seq)
        {
#if DYSEKT_STANDALONE
            pianoRollPanel.setVisible (false);
            arrangeView.setVisible (false);
#endif
            headerBar.setSeqActive (false);
        }
        activeSlot = SlotContent::Browser;
        syncBrowserMode();   // browserPanel's mode is only otherwise set on an actual
                              // uiMode change (see setUiMode) — without this, opening
                              // the browser while staying on the same tab could leave
                              // it filtering for the wrong file type / previous tab's
                              // mode, so file selection silently failed to load.
        browserPanel.setVisible (true);
        headerBar.setBrowserActive (true);
    }
    resized();
    repaint();
}

void DysektEditor::showTrimDialog (const juce::File& file, bool isRelink)
{
 if (isRelink) {
 processor.loadFileAsync (file);
 return;
 }
 auto ext = file.getFileExtension().toLowerCase();
 // SF2/SFZ files are never routed here — handled exclusively by their own tabs.
 // Guard defensively so a future code path can't accidentally cross-load.
 if (ext == ".sf2" || ext == ".sfz")
     return;
 const int pref = processor.trimPreference.load (std::memory_order_relaxed);
 if (pref == DysektProcessor::TrimPrefNever) {
 processor.loadFileAsync (file);
 processor.zoom.store (1.0f);
 processor.scroll.store (0.0f);
 return;
 }
 if (pref == DysektProcessor::TrimPrefAsk) {
 juce::AudioFormatManager fm;
 fm.registerBasicFormats();
 std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
 double duration = 0.0;
 if (reader != nullptr && reader->sampleRate > 0.0)
 duration = (double) reader->lengthInSamples / reader->sampleRate;

 if (duration < 5.0)
 {
 processor.loadFileAsync (file);
 processor.zoom.store (1.0f);
 processor.scroll.store (0.0f);
 return;
 }

 confirmOverlay = std::make_unique<ConfirmOverlay> (
 "Trim Sample?",
 "This sample is long. Would you like to trim it before slicing?",
 "Trim",
 "No Thanks");
 addAndMakeVisible (*confirmOverlay);
 confirmOverlay->setBounds (getLocalBounds());
 confirmOverlay->toFront (true);
 confirmOverlay->onResult = [this, file] (bool trim)
 {
 confirmOverlay.reset();
 if (trim)
 showTrimMode (file);
 else
 {
 processor.loadFileAsync (file);
 processor.zoom.store (1.0f);
 processor.scroll.store (0.0f);
 }
 };
 return;
 }
 showTrimMode (file);
}

void DysektEditor::showTrimMode (const juce::File& file)
{
 trimSession = std::make_unique<TrimSession>();
 trimSession->file = file;
 trimSession->active = false;

 processor.loadFileAsync (file);
 processor.zoom.store (1.0f);
 processor.scroll.store (0.0f);
}

void DysektEditor::toggleSoftWave()
{
 waveformMode = (waveformMode + 1) % 8;
 waveformView.setWaveformMode (waveformMode);
 waveformOverview.setWaveformMode (waveformMode);
 sf2WaveformLcd.setWaveformMode (waveformMode);
 sliceWaveformLcd.setWaveformMode (waveformMode);
 headerBar.setBrowserActive (activeSlot == SlotContent::Browser);
 headerBar.setWaveMode (waveformMode);
 saveUserSettings (getTheme().name);
}

void DysektEditor::toggleMidiFollow()
{
 const bool newVal = ! processor.midiSelectsSlice.load();
 processor.midiSelectsSlice.store (newVal);
 headerBar.setMidiFollowActive (newVal);
}

void DysektEditor::toggleShortcutsPanel()
{
 const bool show = ! shortcutsPanel.isVisible();
 // Sync the current mode into the panel before showing it
 shortcutsPanel.setVisible (show);
 if (show)
 {
 shortcutsPanel.setBounds (getLocalBounds());
 shortcutsPanel.toFront (true);
 shortcutsPanel.grabKeyboardFocus();
 }
}

void DysektEditor::toggleSeqPanel()
{
    if (activeSlot == SlotContent::Seq) {
        activeSlot = SlotContent::None;
#if DYSEKT_STANDALONE
        arrangeView.setVisible (false);
        pianoRollPanel.setVisible (false);
#endif
        headerBar.setSeqActive (false);
        // Sequencer closed — return live MIDI to whichever front-end is showing.
        syncMidiRouteMode();
#if DYSEKT_STANDALONE
        // Clear sequencer live-input and recording state so a closed sequencer
        // doesn't keep recording into a track or routing notes to a stale channel.
        processor.sequencer.setSelectedLiveChannel (0);
        processor.sequencer.setSelectedSfLiveChannels (0);
        processor.sequencer.setRecordingTrack (-1);
#endif
    } else {
        // Close any currently open slot
        if (activeSlot == SlotContent::Mixer) {
            mixerPanel.setVisible (false);
            headerBar.setBodeActive (false);
        } else if (activeSlot == SlotContent::Browser) {
            browserPanel.setVisible (false);
            headerBar.setBrowserActive (false);
        } else if (activeSlot == SlotContent::Eq) {
            eqPanel.setVisible (false);
            headerBar.setEqActive (false);
        }
        activeSlot = SlotContent::Seq;
#if DYSEKT_STANDALONE
        arrangeView.setVisible (true);
        pianoRollPanel.setVisible (false);  // opens only on clip double-click
#endif
        headerBar.setSeqActive (true);
        // Sequencer opened — apply route mode for whichever track is currently selected.
        syncMidiRouteMode();            // sets Sequencer as default
#if DYSEKT_STANDALONE
        arrangeView.notifyCurrentTrack(); // overrides to Slicer if a slicer track is active
#endif
    }
    resized(); repaint(); resized(); repaint();
}

void DysektEditor::toggleThemeEditor()
{
 if (themeEditorPanel != nullptr)
 {
 themeEditorPanel.reset();
 repaint();
 return;
 }

 themeEditorPanel = std::make_unique<ThemeEditorPanel> (getThemesDir());

 themeEditorPanel->onThemeChanged = [this] (const ThemeData& t)
 {
 setTheme (t);
 processor.sliceManager.setSlicePalette (t.slicePalette);
 repaint();
 };

 themeEditorPanel->onThemeSaved = [this] (const juce::String& name)
 {
 processor.sliceManager.setSlicePalette (getTheme().slicePalette);
 saveUserSettings (name);
 repaint();
 };

 themeEditorPanel->onDismiss = [this] { toggleThemeEditor(); };

 addAndMakeVisible (*themeEditorPanel);
 themeEditorPanel->setBounds (getLocalBounds());
 themeEditorPanel->toFront (true);
 themeEditorPanel->grabKeyboardFocus();
 repaint();
}

// ── Waveform frame rect helper ────────────────────────────────────────────────
static juce::Rectangle<float> waveformFrameRect (const DysektEditor& ed,
                                                  const juce::Rectangle<int>& wvBounds,
                                                  bool hasTrimDialog)
{
 const auto& da = ed.getDesignArea();
 const float sf = (float) da.getHeight() / (float) kTotalH;
 const int kFrameInset = juce::roundToInt (4.0f * sf);
 const int kFX = da.getX() + juce::roundToInt (kMargin * sf);
 const int kFW = da.getWidth() - juce::roundToInt (kMargin * sf) * 2;
 const int trimExtra = hasTrimDialog ? juce::roundToInt (kTrimBarH * sf) : 0;
 return { (float) kFX,
 (float) wvBounds.getY() - kFrameInset,
 (float) kFW,
 (float) (wvBounds.getHeight() + trimExtra + kFrameInset * 2) };
}

void DysektEditor::paint (juce::Graphics& g)
{
 g.fillAll (getTheme().background);

 // Draw the CRT frame in waveform mode, or whenever trim mode forces the waveform visible
 const bool wvVisible  = waveformView.isVisible() && waveformView.getHeight() > 0;
 const bool padVisible = padGridView.isVisible()  && padGridView.getHeight()  > 0;

 if (wvVisible || padVisible)
 {
 const auto& frameSrc = wvVisible ? waveformView.getBounds() : padGridView.getBounds();
 const auto outerF = waveformFrameRect (*this, frameSrc, trimDialog != nullptr);

 juce::ColourGradient outerGrad (juce::Colour (0xFF131313), 0.f, outerF.getY(),
 juce::Colour (0xFF0E0E0E), 0.f, outerF.getBottom(), false);
 g.setGradientFill (outerGrad);
 g.fillRoundedRectangle (outerF, 4.0f);

 const float sf = (float) designArea.getHeight() / (float) kTotalH;
 const auto screenF = outerF.reduced (4.0f * sf);
 UIHelpers::drawTexturedPanel (g, screenF, getTheme().darkBar.darker (0.55f),
                                UIHelpers::PanelZone::Instrument, 2.0f);

 juce::ColourGradient glow (getTheme().accent.withAlpha (0.06f), 0.f, screenF.getY(),
 juce::Colours::transparentBlack, 0.f, screenF.getY() + 20.f, false);
 g.setGradientFill (glow);
 g.fillRoundedRectangle (screenF, 2.0f);
 }
}

void DysektEditor::paintOverChildren (juce::Graphics& g)
{
 const bool modalActive = (midiLearnBackdrop != nullptr)
 || shortcutsPanel.isVisible()
 || (confirmOverlay != nullptr)
 || (renameOverlay != nullptr)
 || (messageOverlay != nullptr)
 || (themeEditorPanel != nullptr);
 if (modalActive) return;

 const bool wvVisible  = waveformView.isVisible() && waveformView.getHeight() > 0;
 const bool padVisible = padGridView.isVisible()  && padGridView.getHeight()  > 0;

 // Scale all border pixel amounts proportionally to avoid sub-pixel overlap
 // at non-integer UI scales (1.5×, 1.75× etc.)
 const float sf = (float) designArea.getHeight() / (float) kTotalH;

 if (wvVisible || padVisible)
 {
 const auto& frameSrc = wvVisible ? waveformView.getBounds() : padGridView.getBounds();
 const auto outerF = waveformFrameRect (*this, frameSrc, trimDialog != nullptr);
 const auto ac = getTheme().accent;

 // Clip to outerF so the expanded outer-glow border cannot bleed into the
 // margin columns or below the SCB boundary, which would produce thin
 // accent-coloured hairlines at the pad-grid edges in pad view.
 {
     juce::Graphics::ScopedSaveState clip (g);
     g.reduceClipRegion (outerF.expanded (1.0f * sf).toNearestInt());
     g.setColour (ac.withAlpha (0.18f));
     g.drawRoundedRectangle (outerF.expanded (1.0f * sf), 5.0f * sf, 1.0f * sf);
 }

 g.setColour (ac.withAlpha (0.60f));
 g.drawRoundedRectangle (outerF.reduced (0.5f * sf), 4.0f * sf, 1.5f * sf);

 const auto screenF = outerF.reduced (4.0f * sf);
 g.setColour (ac.withAlpha (0.30f));
 g.drawRoundedRectangle (screenF.expanded (0.5f * sf), 2.0f * sf, 1.0f * sf);
 }

 // SFZ player frame border — identical recipe and width as the waveform frame
 const bool sfzVisible = (sfzDropdown.isVisible() && sfzDropdown.getHeight() > 0)
                        || (sfzPlayerDropdown.isVisible() && sfzPlayerDropdown.getHeight() > 0);
 if (sfzVisible)
 {
 const juce::Rectangle<int> sfzActiveBounds =
     sfzDropdown.isVisible()     ? sfzDropdown.getBounds()
     : sfzPlayerDropdown.isVisible() ? sfzPlayerDropdown.getBounds()
                                     : juce::Rectangle<int>();
 const auto outerF = waveformFrameRect (*this, sfzActiveBounds, false);
 const auto ac = getTheme().accent;

 {
     juce::Graphics::ScopedSaveState clip (g);
     g.reduceClipRegion (outerF.expanded (1.0f * sf).toNearestInt());
     g.setColour (ac.withAlpha (0.18f));
     g.drawRoundedRectangle (outerF.expanded (1.0f * sf), 5.0f * sf, 1.0f * sf);
 }

 g.setColour (ac.withAlpha (0.60f));
 g.drawRoundedRectangle (outerF.reduced (0.5f * sf), 4.0f * sf, 1.5f * sf);

 g.setColour (ac.withAlpha (0.30f));
 g.drawRoundedRectangle (outerF.reduced (4.0f * sf), 2.0f * sf, 1.0f * sf);
 }

 // Slot-panel frame border (Browser / Mixer / EQ / Sequencer) — same recipe
 // as the waveform and SFZ frames above. resized() hides waveformView/
 // padGridView/sfzDropdown/sfzPlayerDropdown whenever one of these slot
 // panels takes over the workspace, so without this block none of them
 // ever got a closing border — the window just stopped abruptly after
 // their content.
 {
 juce::Rectangle<int> slotBounds;
 bool slotPanelVisible = false;

 if (browserPanel.isVisible() && browserPanel.getHeight() > 0)
 { slotBounds = browserPanel.getBounds(); slotPanelVisible = true; }
 else if (mixerPanel.isVisible() && mixerPanel.getHeight() > 0)
 { slotBounds = mixerPanel.getBounds(); slotPanelVisible = true; }
 else if (eqPanel.isVisible() && eqPanel.getHeight() > 0)
 { slotBounds = eqPanel.getBounds(); slotPanelVisible = true; }
#if DYSEKT_STANDALONE
 else if (arrangeView.isVisible() && arrangeView.getHeight() > 0)
 { slotBounds = arrangeView.getBounds(); slotPanelVisible = true; }
#endif

 if (slotPanelVisible)
 {
 const auto outerF = waveformFrameRect (*this, slotBounds, false);
 const auto ac = getTheme().accent;

 {
     juce::Graphics::ScopedSaveState clip (g);
     g.reduceClipRegion (outerF.expanded (1.0f * sf).toNearestInt());
     g.setColour (ac.withAlpha (0.18f));
     g.drawRoundedRectangle (outerF.expanded (1.0f * sf), 5.0f * sf, 1.0f * sf);
 }

 g.setColour (ac.withAlpha (0.60f));
 g.drawRoundedRectangle (outerF.reduced (0.5f * sf), 4.0f * sf, 1.5f * sf);

 g.setColour (ac.withAlpha (0.30f));
 g.drawRoundedRectangle (outerF.reduced (4.0f * sf), 2.0f * sf, 1.0f * sf);
 }
 }

 // Logo frame border
 if (logoBar.isVisible() && logoBar.getHeight() > 0)
 {
 const auto ac = getTheme().accent;
 const juce::Rectangle<float> logoF (logoBar.getBounds().toFloat()
 .withTrimmedTop (4.0f * sf));
 g.setColour (ac.withAlpha (0.18f));
 g.drawRoundedRectangle (logoF.expanded (1.0f * sf), 5.0f * sf, 1.0f * sf);
 g.setColour (ac.withAlpha (0.72f));
 g.drawRoundedRectangle (logoF.reduced (0.5f * sf), 4.0f * sf, 1.5f * sf);
 g.setColour (ac.withAlpha (0.18f));
 g.drawRoundedRectangle (logoF.reduced (2.0f * sf), 3.5f * sf, 1.0f * sf);
 }

 // Full-window accent frame
 {
 const auto ac = getTheme().accent;
 const juce::Rectangle<float> win (getLocalBounds().toFloat());
 g.setColour (ac.withAlpha (0.60f));
 g.drawRoundedRectangle (win.reduced (2.0f * sf), 2.5f * sf, 1.5f * sf);
 g.setColour (ac.withAlpha (0.14f));
 g.drawRoundedRectangle (win.reduced (4.0f * sf), 2.0f * sf, 1.0f * sf);
 }
}

void DysektEditor::resized()
{
 // Re-clamp the live resize ceiling to whichever monitor we're currently
 // on. setResizeLimits() in the constructor set a flat, screen-agnostic
 // max of 3840x2160 — fine for the corner-resizer's min/max plumbing in
 // general, but on any monitor smaller than that, nothing stops the user
 // from dragging the handle taller/wider than the actual screen. Hosts
 // generally trust whatever max size a plugin reports rather than
 // re-clamping it themselves, so the window just runs off the bottom/
 // right edge of the display with no scrolling — clipping whatever
 // painted content (e.g. a panel's closing border) happens to land past
 // the visible edge. setResizeLimits() only updates stored numbers on
 // the constrainer, not the component's own bounds, so calling it here
 // on every resized() (including mid-drag) is safe and won't recurse.
 {
     const auto& displays = juce::Desktop::getInstance().getDisplays();
     const auto* display  = displays.getDisplayForRect (getScreenBounds());
     const auto  userArea = (display != nullptr) ? display->userArea
                                                   : juce::Rectangle<int> (0, 0, 3840, 2160);

     // No safety-margin shrink here (e.g. ×0.95) — that was leaving a strip
     // of dead host background on one edge whenever the host maximized its
     // own floating window to exactly userArea: the host's frame filled the
     // full screen, but our component refused to grow that last few percent
     // to match. userArea is already the screen's usable area (monitor
     // minus OS taskbar etc.), so clamping our own ceiling to exactly that
     // is the correct "don't run off-screen" limit with no extra shrink.
     const int maxW = juce::jmax (kBaseW  / 2, userArea.getWidth());
     const int maxH = juce::jmax (kTotalH / 2, userArea.getHeight());

     setResizeLimits (kBaseW / 2, kTotalH / 2, maxW, maxH);
 }

 // ── Layout area: no aspect lock ─────────────────────────────────────────────
 // The editor accepts whatever size the host gives it (see the constructor —
 // no setFixedAspectRatio()). designArea is just the full local bounds now;
 // kept as a member/getter so the helper functions below don't need a
 // separate code path. The scale factor `sf` is derived from HEIGHT only —
 // every vertical region in this layout (logo, LCD rows, button bar, slice
 // control bar, etc.) is a fixed proportion of kTotalH stacked top-to-bottom
 // with no slack to absorb extra/less height, so it has to track height
 // directly. Width has no such constraint: the side LCD columns and the
 // waveform/browser/panel widths below are already computed as "whatever's
 // left after the fixed-width centre column," so extra width from a wider
 // host window flows straight into those instead of needing a letterbox —
 // a wider window just reveals more side-panel space, it doesn't zoom the
 // whole UI up.
 designArea = getLocalBounds();
 const float sf = (float) getHeight() / (float) kTotalH;
 auto si = [sf](int v) -> int { return juce::roundToInt ((float) v * sf); };

 // Keep popup menu item heights in sync with the window scale.
 DysektLookAndFeel::setMenuScale (sf);

 auto area = designArea;

 // ── Top strip ─────────────────────────────────────────────────────────────
 // In PAD mode shrink LCD rows to 65% — frees ~116px for the pad grid
 const int lcdRowH = si (kLcdRowH);
 const int ctrlFrmH = si (kCtrlFrameH);
 const int kTopStripH = si (kLogoH) + lcdRowH;
 auto topArea = area.removeFromTop (kTopStripH);
 auto topRow = topArea.reduced (si (kMargin), si (4));

 // Leftover horizontal space after the fixed-width centre column and its
 // margins, split evenly between the two side LCD columns. This is what
 // actually reflows on a wider/narrower host window now that width isn't
 // aspect-locked to height — clamped at 0 so an extremely narrow window
 // (near the resize-limit floor) can't drive this negative.
 const int sideW = juce::jmax (0, (topRow.getWidth() - si (kCtrlFrameW) - si (kMargin) * 2) / 2);
 // Show/hide LCD panels per mode.
 // Real tab order: 0=SLICER, 1=SFZ-PLAYER, 2=SF2-PLAYER.
 // sliceLcd/sliceWaveformLcd are mode-aware (see WaveformView's
 // activeSliceManager/activeVoicePool pattern) and cover BOTH the Slicer
 // and SFZ-PLAYER tabs. SF2-PLAYER still uses its own dedicated panels.
 const bool sf2Mode = (uiMode == 2);
 sliceLcd.setVisible (! sf2Mode);
 sliceWaveformLcd.setVisible (! sf2Mode);
 sf2Lcd.setVisible (sf2Mode);
 sf2WaveformLcd.setVisible (sf2Mode);

 sliceLcd.setBounds (topRow.removeFromLeft (sideW));
 sf2Lcd.setBounds (sliceLcd.getBounds());
 topRow.removeFromLeft (si (kMargin));

 auto centreCol = topRow.removeFromLeft (si (kCtrlFrameW));
 auto logoRow = centreCol.removeFromTop (si (kLogoH));
 // logoBar placed after cfY is known — centred vertically between plugin top and CF top
 {
 const int btnBarY = centreCol.getBottom() - si (kBtnBarH) - si (4);
 headerBar.setBounds (centreCol.getX(), btnBarY, centreCol.getWidth(), si (kBtnBarH));
 if (auto* cf = headerBar.getControlFrame())
 {
 const int cfY = centreCol.getY() + (btnBarY - centreCol.getY() - ctrlFrmH) / 2;
 cf->setBounds (centreCol.getX(), cfY, centreCol.getWidth(), ctrlFrmH);
 // Centre logo: equal margin above and below between plugin top (y=0) and CF top
 const int logoY = (cfY - si (kLogoH)) / 2;
 logoBar.setBounds (logoRow.getX(), logoY, logoRow.getWidth(), si (kLogoH));
 }
 else
 {
 logoBar.setBounds (logoRow); // fallback: top-aligned
 }
 }

 topRow.removeFromLeft (si (kMargin));
 sliceWaveformLcd.setBounds (topRow);
 sf2WaveformLcd.setBounds (topRow);

 auto actionArea = area.removeFromTop (si (kActionH));
 const int kFX = area.getX() + si (kMargin);
 const int kFW = area.getWidth() - si (kMargin) * 2;

 area.removeFromBottom (si (kMargin));

 // Panel slot: open for mixer, or for the normal (non-init) browser.
 // initBrowserOpen uses the waveform frame area instead — no slot needed.
 // At scale > 1.0 (host inflates bounds) we clamp to avoid overlap: the
 // scaled slot must never exceed what the remaining height can accommodate.
 const bool hasActiveSlot = (activeSlot != SlotContent::None && ! initBrowserOpen);
 const int wantedSlotH = hasActiveSlot ? si (kPanelSlotH) : 0;
 const int slotH = juce::jmin (wantedSlotH, juce::jmax (0, area.getHeight() - si (80)));
 auto slot = area.removeFromBottom (slotH);
 if (hasActiveSlot) area.removeFromBottom (si (kMargin));

 if (activeSlot == SlotContent::Mixer) {
 // Expand mixer to fill ALL available area (waveformView space + slot).
 const int mixTop = actionArea.getY();
 const int mixBot = slot.getBottom();
 mixerPanel.setBounds (kFX, mixTop, kFW, mixBot - mixTop);
 mixerPanel.setVisible (true);
 browserPanel.setBounds ({});
 eqPanel.setBounds ({});
#if DYSEKT_STANDALONE
 pianoRollPanel.setBounds ({});
 arrangeView.setBounds ({});
#endif
 }
 else if (activeSlot == SlotContent::Browser && ! initBrowserOpen) {
 // Expand browser to fill ALL available area (waveformView space + slot)
 const int browserTop = actionArea.getY();
 const int browserBot = slot.getBottom();
 browserPanel.setBounds (kFX, browserTop, kFW, browserBot - browserTop);
 mixerPanel.setBounds ({});
 eqPanel.setBounds ({});
#if DYSEKT_STANDALONE
 pianoRollPanel.setBounds ({});
 arrangeView.setBounds ({});
#endif
 }
 else if (activeSlot == SlotContent::Eq) {
     const int eqTop = actionArea.getY();
     const int eqBot = slot.getBottom();
     eqPanel.setBounds (kFX, eqTop, kFW, eqBot - eqTop);
     mixerPanel.setBounds ({});
     browserPanel.setBounds ({});
#if DYSEKT_STANDALONE
     pianoRollPanel.setBounds ({});
     arrangeView.setBounds ({});
#endif
 }
 else if (activeSlot == SlotContent::Seq) {
     const int seqTop = actionArea.getY();
     const int seqBot = slot.getBottom();
     const int seqH   = seqBot - seqTop;

#if DYSEKT_STANDALONE
     arrangeView.setBounds (kFX, seqTop, kFW, seqH);
#endif

     // PianoRollPanel floats as overlay on top of ArrangeView when visible
#if DYSEKT_STANDALONE
     if (pianoRollPanel.isVisible())
     {
         const int overlayH = juce::jmax (250, seqH * 3 / 4);
         pianoRollPanel.setBounds (kFX, seqTop, kFW, juce::jmin (seqH, overlayH));
         pianoRollPanel.toFront (false);
     }
     else
     {
         pianoRollPanel.setBounds ({});
     }
#endif

     mixerPanel.setBounds ({});
     browserPanel.setBounds ({});
     eqPanel.setBounds ({});
 } else {
 mixerPanel.setBounds ({});
 eqPanel.setBounds ({});
#if DYSEKT_STANDALONE
 pianoRollPanel.setBounds ({});
 arrangeView.setBounds ({});
#endif
 if (! initBrowserOpen)
 browserPanel.setBounds ({});
 // initBrowserOpen browser is sized below, in the waveform frame area
 }

 const int kFrameInset = si (4);
 const int kOverviewH = si (28);
 const int kInterGap = si (kMargin) + kFrameInset;
 const int kOverviewRowH = kInterGap + kOverviewH + si (kMargin);

 int overviewTopGuard = area.getBottom();

 // SCB and zoom bar (overview) are only shown when a real user sample is loaded —
 // the default Empty.wav placeholder does not count.
 auto sampleSnap = processor.sampleData.getSnapshot();
 // SFZ-PLAYER is a full second Slicer instance (sliceManager2/sampleData2) —
 // NOT the disconnected legacy sfzPlayer2 live engine. Read sampleData2's own
 // snapshot directly here, exactly like the Slicer branch below does for
 // sampleData — not via getUiSliceSnapshot2(), which only refreshes once
 // processBlock() next consumes uiSnapshotDirty and is an extra, avoidable
 // hop for what should be an immediate "is anything actually loaded?" check.
 auto sampleSnap2 = processor.sampleData2.getSnapshot();
 const bool sfz2HasSample = (sampleSnap2 != nullptr && sampleSnap2->buffer.getNumSamples() > 0);
 const bool hasRealSample = (uiMode == 1)
    ? sfz2HasSample
    : (hasSampleLoaded
       && sampleSnap != nullptr
       && ! sampleSnap->filePath.containsIgnoreCase ("DYSEKT_default.wav"));

 const bool normalBrowserOpen = (activeSlot == SlotContent::Browser && ! initBrowserOpen);

 if (trimDialog != nullptr) {
 sliceControlBar.setBounds ({});
 waveformOverview.setVisible (false);
 waveformOverview.setBounds ({});
 } else {
 // SCB first (bottommost), then overview row sits immediately above it.
 //
 // uiMode == 1 makes SFZ-PLAYER's SCB always reachable, regardless of
 // hasRealSample: for SFZ-PLAYER, hasRealSample requires sampleData2 to
 // already contain decoded sample data, and with nothing loaded yet that's
 // false — so without this, the SCB (the only way to reach the ZONES toggle
 // and enter add-zone mode) was hidden entirely and there was no way in.
 // Slicer (uiMode == 0) keeps the original hasRealSample-only gate — that
 // tab's SCB genuinely has nothing useful to do until a sample is loaded.
 // NOTE: showZoneBuilder is deliberately NOT included here — uiMode == 1
 // already covers reachability while zone-builder view is active, and
 // showZoneBuilder is never reset on tab switch (see setUiMode), so
 // including it here let a stale showZoneBuilder=true from a prior
 // SFZ-PLAYER session keep the SCB visible after switching to Slicer.
 if ((hasRealSample || uiMode == 1) && (uiMode == 0 || uiMode == 1) && activeSlot != SlotContent::Mixer && !normalBrowserOpen)
 {
     {
         const int scbH = si (kSliceCtrlH);
         auto scbStrip = area.removeFromBottom (scbH);
         sliceControlBar.setBounds (kFX, scbStrip.getY(), kFW, scbH);
     }
 }
 else
 {
     sliceControlBar.setBounds ({});
 }

 // Overview row: allocate space and show only when waveform view is active.
 if ((uiMode == 0 || uiMode == 1) && activeSlot != SlotContent::Mixer && !normalBrowserOpen && hasRealSample && !showPadGrid && !showZoneBuilder)
 {
     auto overviewRow = area.removeFromBottom (kOverviewRowH);
     const int overviewY = overviewRow.getY() + kInterGap;
     waveformOverview.setVisible (true);
     waveformOverview.setBounds (kFX, overviewY, kFW, kOverviewH);
     overviewTopGuard = overviewRow.getY();
 }
 else
 {
     waveformOverview.setVisible (false);
     waveformOverview.setBounds ({});
 }

 if (showPadGrid)
     overviewTopGuard = area.getBottom();
 }

 const int kFrameX = kFX;
 const int kFrameW = kFW;
 const int frameTop = actionArea.getY();
 const int frameBot = juce::jmin (area.getBottom(), overviewTopGuard);
 const int screenX = kFrameX + kFrameInset;
 const int screenW = kFrameW - kFrameInset * 2;
 const int screenTop = frameTop + kFrameInset;
 const int screenBot = frameBot - kFrameInset;

  int y = screenTop;
 sliceLane.setBounds ({});

 int trimH = (trimDialog != nullptr) ? si (kTrimBarH) : 0;
 int h = juce::jmax (si (80), screenBot - trimH - y);

 const bool slotCoveringFrame = (activeSlot != SlotContent::None && ! initBrowserOpen);
 const int  waveH      = juce::jmax (si (80), h);

 // ── Route the main content area to the active view ────────────────────────
 // Trim mode always requires the waveform view, regardless of uiMode.
 const bool trimActive = (trimDialog != nullptr || (trimSession != nullptr && trimSession->active));

 if (slotCoveringFrame)
 {
     // Mixer or normal browser is open — hide all main views
     waveformView.setVisible (false);       waveformView.setBounds ({});
     sfzDropdown.setVisible  (false);       sfzDropdown.setBounds  ({});
     sfzPlayerDropdown.setVisible (false);  sfzPlayerDropdown.setBounds ({});
     padGridView.setVisible  (false);       padGridView.setBounds  ({});
     zoneBuilderKeysPanel.setVisible (false); zoneBuilderKeysPanel.setBounds ({});
 }
 else if (initBrowserOpen)
 {
     // No real sample yet — browser occupies the full waveform frame area
     browserPanel.setBounds (screenX, y, screenW, h);
     waveformView.setVisible (false);       waveformView.setBounds ({});
     sfzDropdown.setVisible  (false);       sfzDropdown.setBounds  ({});
     sfzPlayerDropdown.setVisible (false);  sfzPlayerDropdown.setBounds ({});
     padGridView.setVisible  (false);       padGridView.setBounds  ({});
     zoneBuilderKeysPanel.setVisible (false); zoneBuilderKeysPanel.setBounds ({});
 }
 else if (uiMode == 0 || trimActive)
 {
     // Slicer mode — WaveformView or PadGridView depending on toggle
     const bool showPads = showPadGrid && ! trimActive;

     waveformView.setVisible (! showPads);
     waveformView.setBounds (showPads ? juce::Rectangle<int>()
                                      : juce::Rectangle<int> (screenX, y, screenW, waveH));

     padGridView.setVisible (showPads);
     padGridView.setBounds (showPads ? juce::Rectangle<int> (screenX, y, screenW, waveH)
                                     : juce::Rectangle<int>());

     sfzDropdown.setVisible (false);
     sfzDropdown.setBounds ({});
     sfzPlayerDropdown.setVisible (false);
     sfzPlayerDropdown.setBounds ({});
     zoneBuilderKeysPanel.setVisible (false);
     zoneBuilderKeysPanel.setBounds ({});
 }
 else if (uiMode == 1)
 {
    // SFZ-PLAYER: WaveformView / PadGridView / zone-builder KeysPanel,
    // depending on the SCB toggles. ZONES (showZoneBuilder) takes priority
    // over PADS since the SCB never shows both toggles at once (see
    // SliceControlBar::isSfzPlayer2Mode gating), but guard anyway.
    const bool showZones1 = showZoneBuilder && ! trimActive;
    const bool showPads1  = showPadGrid && ! showZones1;

    waveformView.setVisible (! showPads1 && ! showZones1);
    waveformView.setBounds ((showPads1 || showZones1) ? juce::Rectangle<int>()
                                      : juce::Rectangle<int> (screenX, y, screenW, waveH));
    padGridView.setVisible (showPads1);
    padGridView.setBounds (showPads1 ? juce::Rectangle<int> (screenX, y, screenW, waveH)
                                     : juce::Rectangle<int>());
    zoneBuilderKeysPanel.setVisible (showZones1);
    zoneBuilderKeysPanel.setBounds (showZones1 ? juce::Rectangle<int> (screenX, y, screenW, waveH)
                                               : juce::Rectangle<int>());
    sfzDropdown.setVisible (false);
    sfzDropdown.setBounds ({});
    sfzPlayerDropdown.setVisible (false);
    sfzPlayerDropdown.setBounds ({});
}
 else
 {
     // SF2-PLAYER layout (uiMode == 2)
     sfzDropdown.setVisible (true);
     sfzDropdown.setBounds (juce::Rectangle<int> (screenX, y, screenW, waveH));
     sfzPlayerDropdown.setVisible (false);
     sfzPlayerDropdown.setBounds ({});
     waveformView.setVisible (false);
     waveformView.setBounds ({});
     padGridView.setVisible (false);
     padGridView.setBounds ({});
     zoneBuilderKeysPanel.setVisible (false);
     zoneBuilderKeysPanel.setBounds ({});
 }

  // ── Trim bar: hide behind browser or mixer, restore when they close ───────
 if (trimDialog != nullptr)
 {
 if (normalBrowserOpen || activeSlot == SlotContent::Mixer)
 trimDialog->setBounds ({}); // hide trim bar — browser/mixer is covering the workspace
 else
 trimDialog->setBounds (screenX, y + h, screenW, si (kTrimBarH));
 }

 if (shortcutsPanel.isVisible())
 shortcutsPanel.setBounds (getLocalBounds());

 if (midiLearnBackdrop != nullptr)
 midiLearnBackdrop->setBounds (getLocalBounds());
 if (midiLearnDialog != nullptr)
 midiLearnDialog->setBounds (getLocalBounds().reduced (40));
 if (confirmOverlay != nullptr)
 confirmOverlay->setBounds (getLocalBounds());
 if (renameOverlay != nullptr)
 renameOverlay->setBounds (getLocalBounds());
 if (messageOverlay != nullptr)
 messageOverlay->setBounds (getLocalBounds());
 if (themeEditorPanel != nullptr)
 themeEditorPanel->setBounds (getLocalBounds());
}

void DysektEditor::toggleMixerPanel()
{
 if (activeSlot == SlotContent::Mixer) {
 activeSlot = SlotContent::None;
 mixerPanel.setVisible (false);
 headerBar.setBodeActive (false);
 } else {
 if (activeSlot == SlotContent::Browser) {
 activeSlot = SlotContent::None;
 browserPanel.setVisible (false);
 headerBar.setBrowserActive (false);
 } else if (activeSlot == SlotContent::Eq) {
 eqPanel.setVisible (false);
 headerBar.setEqActive (false);
 } else if (activeSlot == SlotContent::Seq) {
#if DYSEKT_STANDALONE
 pianoRollPanel.setVisible (false);
 arrangeView.setVisible (false);
#endif
 headerBar.setSeqActive (false);
 }
 activeSlot = SlotContent::Mixer;
 mixerPanel.setVisible (true);
 headerBar.setBodeActive (true);
 }
 resized(); repaint(); resized(); repaint();
}

void DysektEditor::toggleEqPanel()
{
    if (activeSlot == SlotContent::Eq) {
        activeSlot = SlotContent::None;
        eqPanel.setVisible (false);
        headerBar.setEqActive (false);
    } else {
        // Close any currently open slot
        if (activeSlot == SlotContent::Mixer) {
            mixerPanel.setVisible (false);
            headerBar.setBodeActive (false);
        } else if (activeSlot == SlotContent::Browser) {
            browserPanel.setVisible (false);
            headerBar.setBrowserActive (false);
        } else if (activeSlot == SlotContent::Seq) {
#if DYSEKT_STANDALONE
            pianoRollPanel.setVisible (false);
            arrangeView.setVisible (false);
#endif
            headerBar.setSeqActive (false);
        }
        activeSlot = SlotContent::Eq;
        eqPanel.setVisible (true);
        headerBar.setEqActive (true);
    }
    resized(); repaint(); resized(); repaint();
}

// ── Keyboard shortcuts ────────────────────────────────────────────────────────
bool DysektEditor::keyPressed (const juce::KeyPress& key)
{
 auto mods = key.getModifiers();
 int code = key.getKeyCode();

 if (code == 'Z' && mods.isCommandDown() && mods.isShiftDown())
 { DysektProcessor::Command c; c.type = DysektProcessor::CmdRedo; processor.pushCommand (c); return true; }
 if (code == 'Z' && mods.isCommandDown())
 { DysektProcessor::Command c; c.type = DysektProcessor::CmdUndo; processor.pushCommand (c); return true; }

 if (mods.isCommandDown()) return false;

 if (code == juce::KeyPress::escapeKey && shortcutsPanel.isVisible())
 { toggleShortcutsPanel(); return true; }

 // Esc dismisses the PianoRoll overlay, returning to ArrangeView-only
#if DYSEKT_STANDALONE
 if (code == juce::KeyPress::escapeKey &&
     activeSlot == SlotContent::Seq &&
     pianoRollPanel.isVisible())
 {
     pianoRollPanel.setVisible (false);
     resized(); repaint();
     return true;
 }
#endif

 if (code == '?') { toggleShortcutsPanel(); return true; }

 if (code == 'M')
 {
 if (midiLearnDialog != nullptr)
 {
 midiLearnDialog.reset();
 midiLearnBackdrop.reset();
 resized();
 }
 else
 {
 struct Backdrop : public juce::Component {
 void paint (juce::Graphics& g) override {
 g.fillAll (juce::Colours::black.withAlpha (0.55f));
 }
 };
 midiLearnBackdrop = std::make_unique<Backdrop>();
 addAndMakeVisible (*midiLearnBackdrop);
 midiLearnBackdrop->toFront (false);

 midiLearnDialog = std::make_unique<MidiLearnDialog> (
 processor.midiLearn,
 processor,
 [this] { midiLearnDialog.reset(); midiLearnBackdrop.reset(); resized(); }
 );
 addAndMakeVisible (*midiLearnDialog);
 midiLearnDialog->toFront (true);
 resized();
 }
 return true;
 }

 if (code == 'L' && uiMode == 0)
 {
 DysektProcessor::Command c;
 c.type = processor.lazyChop.isActive() ? DysektProcessor::CmdLazyChopStop
 : DysektProcessor::CmdLazyChopStart;
 processor.pushCommand (c); repaint(); return true;
 }
 if (code == juce::KeyPress::deleteKey)
 {
 const auto& ui = processor.getUiSliceSnapshot();
 if (ui.selectedSlice >= 0)
 { DysektProcessor::Command c; c.type = DysektProcessor::CmdDeleteSlice; c.intParam1 = ui.selectedSlice; processor.pushCommand (c); }
 return true;
 }
 if (code == 'F') { toggleMidiFollow(); return true; }

 if (code == juce::KeyPress::rightKey)
 {
 const auto& ui = processor.getUiSliceSnapshot();
 if (ui.numSlices > 0)
 { DysektProcessor::Command c; c.type = DysektProcessor::CmdSelectSlice; c.intParam1 = juce::jlimit (0, ui.numSlices-1, ui.selectedSlice+1); processor.pushCommand (c); repaint(); }
 return true;
 }
 if (code == juce::KeyPress::leftKey)
 {
 const auto& ui = processor.getUiSliceSnapshot();
 if (ui.numSlices > 0)
 { DysektProcessor::Command c; c.type = DysektProcessor::CmdSelectSlice; c.intParam1 = juce::jlimit (0, ui.numSlices-1, ui.selectedSlice-1); processor.pushCommand (c); repaint(); }
 return true;
 }

 return false;
}

void DysektEditor::timerCallback()
{
 bool uiChanged = false, viewportChanged = false;
 const bool previewActive = waveformView.hasActiveSlicePreview();
 const bool waveformInteracting = waveformView.isInteracting();

 const auto snapshotVersion = (uint32_t) processor.getUiSliceSnapshotVersion();
 if (snapshotVersion != lastUiSnapshotVersion) { lastUiSnapshotVersion = snapshotVersion; uiChanged = true; }

 {
 const bool procState = processor.midiSelectsSlice.load (std::memory_order_relaxed);
 headerBar.setMidiFollowActive (procState);
 }

 {
 const int curSlices = processor.sliceManager.getNumSlices();
 if (lastNumSlices == 0 && curSlices > 0)
 {
 processor.midiSelectsSlice.store (true, std::memory_order_relaxed);
 headerBar.setMidiFollowActive (true);
 }
 lastNumSlices = curSlices;
 }

 const float zoom = processor.zoom.load(), scroll = processor.scroll.load();
 if (zoom != lastZoom || scroll != lastScroll) { lastZoom = zoom; lastScroll = scroll; viewportChanged = true; }

 // MIDI follow: scroll waveform viewport
 if (processor.midiSelectsSlice.load (std::memory_order_relaxed))
 {
 const int followSlice = processor.midiFollowTriggeredSlice.load (std::memory_order_relaxed);
 if (followSlice >= 0 && followSlice != lastMidiFollowSlice)
 {
 lastMidiFollowSlice = followSlice;
 const float z = processor.zoom.load();
 if (z > 1.0f)
 {
 auto snap = processor.sampleData.getSnapshot();
 if (snap != nullptr && snap->buffer.getNumSamples() > 0)
 {
 const int numFrames = snap->buffer.getNumSamples();
 const int visibleLen = (int) ((float) numFrames / z);
 const int maxStart = numFrames - visibleLen;
 const auto& uiSnap = processor.getUiSliceSnapshot();
 if (maxStart > 0 && followSlice < uiSnap.numSlices)
 {
 const int sliceStart = uiSnap.slices[(size_t) followSlice].startSample;
 const int sliceEnd = (followSlice + 1 < uiSnap.numSlices)
 ? uiSnap.slices[(size_t)(followSlice + 1)].startSample
 : numFrames;
 const int sliceCenter = (sliceStart + sliceEnd) / 2;
 const int newStart = juce::jlimit (0, maxStart, sliceCenter - visibleLen / 2);
 processor.scroll.store ((float) newStart / (float) maxStart,
 std::memory_order_relaxed);
 viewportChanged = true;
 }
 }
 }
 }
 }

 {
 const bool trimNow = processor.trimModeActive.load (std::memory_order_relaxed);
 if (trimNow != lastTrimActive)
 {
 lastTrimActive = trimNow;
 }
 }

 const bool playbackActive = std::any_of (processor.voicePool.voicePositions.begin(),
 processor.voicePool.voicePositions.end(),
 [] (const std::atomic<float>& pos) { return pos.load (std::memory_order_relaxed) > 0.0f; })
 || std::any_of (processor.voicePool2.voicePositions.begin(),
 processor.voicePool2.voicePositions.end(),
 [] (const std::atomic<float>& pos) { return pos.load (std::memory_order_relaxed) > 0.0f; });

 const bool slicingActive    = (uiMode == 0);
 const bool waveformAnimating = waveformInteracting || previewActive
 || playbackActive || (slicingActive && processor.lazyChop.isActive())
 || (slicingActive && processor.liveDragSliceIdx.load (std::memory_order_relaxed) >= 0);
 const bool waveformShowing = ((uiMode == 0 || uiMode == 1) && ! showPadGrid) || processor.trimModeActive.load (std::memory_order_relaxed);
 const bool waveformNeedsRepaint = waveformShowing && (uiChanged || viewportChanged || waveformAnimating || lastWaveformAnimating);
 const bool laneNeedsRepaint = slicingActive && waveformShowing && (uiChanged || viewportChanged || previewActive || lastPreviewActive);

 lastWaveformAnimating = waveformAnimating;
 lastPreviewActive = previewActive;

 if (trimSession != nullptr && ! trimSession->active)
 {
 auto snap = processor.sampleData.getSnapshot();
 if (snap != nullptr && snap->filePath == trimSession->file.getFullPathName())
 {
 // Trim mode requires the waveform view — auto-switch if in Pad Grid mode.
 if (uiMode != 0 && uiMode != 1)
 setUiMode (0);

 trimSession->active = true;
 const int totalFrames = snap->buffer.getNumSamples();
 waveformView.enterTrimMode (0, totalFrames);

 processor.trimModeActive.store (true, std::memory_order_relaxed);
 processor.trimRegionStart.store (0, std::memory_order_relaxed);
 processor.trimRegionEnd .store (totalFrames, std::memory_order_relaxed);

 if (trimDialog == nullptr)
 {
 trimDialog = std::make_unique<TrimDialog> (processor, waveformView);
 addAndMakeVisible (*trimDialog);
 trimDialog->toFront (false);
 resized();
 }
 }
 }

 if (processor.trimModeActive.load (std::memory_order_relaxed)
 && ! waveformView.isTrimDragging())
 {
 const int procStart = processor.trimRegionStart.load (std::memory_order_relaxed);
 const int procEnd = processor.trimRegionEnd .load (std::memory_order_relaxed);
 if (procStart != waveformView.getTrimIn() || procEnd != waveformView.getTrimOut())
 waveformView.setTrimPoints (procStart, procEnd);
 }

 const int targetHz = waveformAnimating ? 60 : 30;
 if (targetHz != timerHz) { startTimerHz (targetHz); timerHz = targetHz; }

 if (waveformNeedsRepaint) waveformView.repaint();
 if (laneNeedsRepaint) sliceLane.repaint();

 // SFZ player refresh
    if (showPadGrid) padGridView.repaintGrid();

 // uiMode==1 (SFZ-PLAYER) uses waveformView — repainted above with uiMode==0 path.
 // uiMode==2 (SF2-PLAYER): repaint sfzDropdown (SF2 program grid)
 if (uiMode == 2 && (uiChanged || playbackActive))
     sfzDropdown.repaint();

 sliceLcd.repaintLcd();
 sliceWaveformLcd.repaintLcd();
 sf2Lcd.repaintLcd();
 sf2WaveformLcd.repaintLcd();
 {
 auto timerSnap = processor.sampleData.getSnapshot();
 const bool hasSample = (timerSnap != nullptr
 && timerSnap->buffer.getNumSamples() > 0);
 if (hasSample != hasSampleLoaded)
 {
 hasSampleLoaded = hasSample;
 resized(); // expand/collapse SCB + zoom bar
 }

 // SFZ-PLAYER (sliceManager2/sampleData2): same expand/collapse-on-load
 // detection as above, reading sampleData2's own snapshot directly —
 // not the UI snapshot — for the same reason resized()'s hasRealSample
 // does: it's one hop closer to the actual load completion than waiting
 // on uiSnapshotDirty to be consumed inside processBlock().
 {
     auto timerSnap2 = processor.sampleData2.getSnapshot();
     const bool hasSample2 = (timerSnap2 != nullptr && timerSnap2->buffer.getNumSamples() > 0);
     if (hasSample2 != hasSampleLoaded2)
     {
         hasSampleLoaded2 = hasSample2;
         resized(); // expand/collapse SCB for the SFZ-PLAYER tab
     }
 }
 // Only show the overview / zoom bar for a real user sample, not the default placeholder.
 const bool hasRealSampleNow = hasSample && timerSnap != nullptr
 && ! timerSnap->filePath.containsIgnoreCase ("DYSEKT_default.wav");

 // Auto-close the init browser once the user has loaded a real sample.
 if (initBrowserOpen && hasRealSampleNow)
 {
 initBrowserOpen = false;
 browserPanel.setVisible (false);
 headerBar.setBrowserActive (false);
 resized(); repaint();
 }

 const bool overviewShouldShow = hasRealSampleNow && (uiMode == 0) && !showPadGrid;
 if (overviewShouldShow != waveformOverview.isVisible())
 {
 waveformOverview.setVisible (overviewShouldShow);
 resized();
 }
 }
 if (waveformOverview.isVisible())
 waveformOverview.repaintOverview();
 if (activeSlot == SlotContent::Mixer) mixerPanel.repaint();
 if (activeSlot == SlotContent::Eq)    eqPanel.repaint();

 headerBar.repaint();
 if (uiMode == 0 || uiMode == 1) { sliceControlBar.updateMidiLearnPulse(); sliceControlBar.repaint(); }
if (activeSlot == SlotContent::Mixer)
{
    // Refresh strips in case a preset was just assigned or un-assigned.
    mixerPanel.setActiveChannels (sfzDropdown.getProgramGrid().getPresets(),
                                  sfzDropdown.getProgramGrid().getPresetChannels());
    mixerPanel.updateFromSnapshot();
}
#if DYSEKT_STANDALONE
if (activeSlot == SlotContent::Seq)   pianoRollPanel.syncSnap();
#endif

    // ── Chromatic track sync ────────────────────────────────────────────────
    // Whenever the UI snapshot changes, walk slices and keep the sequencer
    // engine's ChromaticSlice tracks in sync with chromaticChannel settings.
    // addChromaticTrack / removeChromaticTrack are both idempotent.
    if (uiChanged)
    {
        const auto& snap = processor.getUiSliceSnapshot();
        for (int i = 0; i < snap.numSlices; ++i)
        {
            const auto& sl = snap.slices[(size_t) i];
            if (sl.chromaticChannel > 0)
            {
                const juce::String sliceName = "Slice " + juce::String (i + 1);
#if DYSEKT_STANDALONE
                pianoRollPanel.onSliceChromaticToggled (
                    i, true, sl.chromaticChannel, sliceName, sl.colour);
#endif
            }
            else
            {
#if DYSEKT_STANDALONE
                pianoRollPanel.onSliceChromaticToggled (
                    i, false, 0, {}, juce::Colours::transparentBlack);
#endif
            }
        }
    }

#if JUCE_WINDOWS && ! DYSEKT_STANDALONE
    // ── Host editor-window resize desync watchdog ───────────────────────────
    // Reproduces the bug where reopening the plugin (or restoring a session)
    // after the floating editor window was previously maximised/fullscreen
    // leaves our UI laid out at our own small requested size (90% of the
    // primary display, set in the constructor) while the HOST's outer
    // window is left at its remembered larger size — some VST3 hosts
    // restore the floating window's OS-level size without ever routing that
    // through IPlugView::onSize(), so our resized() never sees it and the
    // extra space around our UI is just blank host background.
    //
    // We have no way to intercept a resize the host never tells us about,
    // but we can detect it: our own native window's PARENT (the host's
    // outer view) reports its true client size independently of what JUCE
    // thinks our size is. Require the mismatch to persist for a couple of
    // ticks before acting, so a live corner-drag (which legitimately
    // changes size through the normal path tick-to-tick) is never fought.
    if (auto* hwnd = (HWND) getWindowHandle())
    {
        if (auto* parentHwnd = ::GetParent (hwnd))
        {
            RECT r;
            if (::GetClientRect (parentHwnd, &r))
            {
                const int parentW = (int) (r.right  - r.left);
                const int parentH = (int) (r.bottom - r.top);
                const bool mismatched = parentW > 0 && parentH > 0
                                      && (parentW != getWidth() || parentH != getHeight());

                if (mismatched && parentW == lastPeerMismatchW && parentH == lastPeerMismatchH)
                {
                    if (++peerMismatchTicks >= 2)
                    {
                        setSize (parentW, parentH);
                        peerMismatchTicks = 0;
                    }
                }
                else
                {
                    peerMismatchTicks = 0;
                    lastPeerMismatchW = parentW;
                    lastPeerMismatchH = parentH;
                }
            }
        }
    }
#endif
}

void DysektEditor::ensureDefaultThemes()
{
 auto dir = getThemesDir(); dir.createDirectory();
 auto write = [&] (const juce::String& name, const ThemeData& t)
 {
 auto f = dir.getChildFile (name + ".dsk");
 if (! f.existsAsFile()) f.replaceWithText (t.toThemeFile());
 };
 write ("dark", ThemeData::darkTheme());
 write ("shell", ThemeData::shellTheme());
 write ("lazy", ThemeData::lazyTheme());
 write ("snow", ThemeData::snowTheme());
 write ("ghost", ThemeData::ghostTheme());
 write ("hack", ThemeData::hackTheme());
 write ("midnight", ThemeData::midnightTheme());
 write ("pigments", ThemeData::pigmentsTheme());
 write ("cr8",     ThemeData::cr8Theme());
 write ("dysekt",  ThemeData::dysektTheme());
 write ("serum",   ThemeData::serumTheme());
}

juce::StringArray DysektEditor::getAvailableThemes()
{
 juce::StringArray names;
 for (auto& f : getThemesDir().findChildFiles (juce::File::findFiles, false, "*.dsk"))
 {
 auto t = ThemeData::fromThemeFile (f.loadFileAsString());
 if (t.name.isNotEmpty()) names.add (t.name);
 }
 if (names.isEmpty()) { names.add ("dark"); names.add ("shell"); }
 return names;
}

void DysektEditor::applyTheme (const juce::String& themeName)
{
 for (auto& f : getThemesDir().findChildFiles (juce::File::findFiles, false, "*.dsk"))
 {
 auto t = ThemeData::fromThemeFile (f.loadFileAsString());
 if (t.name == themeName)
 {
 setTheme (t);
 processor.sliceManager.setSlicePalette (getTheme().slicePalette);
 saveUserSettings (themeName);
 applyWindowIcon (this);
 repaint(); return;
 }
 }
 if (themeName == "shell") setTheme (ThemeData::shellTheme());
 else if (themeName == "lazy") setTheme (ThemeData::lazyTheme());
 else if (themeName == "snow") setTheme (ThemeData::snowTheme());
 else if (themeName == "ghost") setTheme (ThemeData::ghostTheme());
 else if (themeName == "hack") setTheme (ThemeData::hackTheme());
 else if (themeName == "midnight") setTheme (ThemeData::midnightTheme());
 else if (themeName == "pigments") setTheme (ThemeData::pigmentsTheme());
 else if (themeName == "cr8")      setTheme (ThemeData::cr8Theme());
 else if (themeName == "dysekt")   setTheme (ThemeData::dysektTheme());
 else if (themeName == "serum")    setTheme (ThemeData::serumTheme());
 else setTheme (ThemeData::darkTheme());
 processor.sliceManager.setSlicePalette (getTheme().slicePalette);
 saveUserSettings (themeName);
 applyWindowIcon (this);
 repaint();
}


void DysektEditor::saveUserSettings (const juce::String& themeName)
{
 auto file = getUserSettingsFile();
 file.getParentDirectory().createDirectory();

 file.replaceWithText ("theme: " + themeName
 + "\nwaveStyle: " + juce::String (waveformMode)
 + "\nuiMode: " + juce::String (uiMode) + "\n");
}

void DysektEditor::loadUserSettings()
{
 juce::String themeName = "dark";
 auto file = getUserSettingsFile();
 if (file.existsAsFile())
 {
 for (auto line : juce::StringArray::fromLines (file.loadFileAsString()))
 {
 line = line.trim();
 if (line.startsWith ("theme:"))
 {
 themeName = line.fromFirstOccurrenceOf (":", false, false).trim();
 }
 else if (line.startsWith ("waveStyle:"))
 {
 auto val = line.fromFirstOccurrenceOf (":", false, false).trim();
 if (val == "soft") waveformMode = 1;
 else if (val == "hard") waveformMode = 0;
 else waveformMode = juce::jlimit (0, 7, val.getIntValue());
 }
 else if (line.startsWith ("uiMode:"))
 {
 auto val = line.fromFirstOccurrenceOf (":", false, false).trim().getIntValue();
 uiMode = juce::jlimit (0, 2, val);
 }
 }
 }
 applyTheme (themeName);

 waveformView.setWaveformMode (waveformMode);
 waveformOverview.setWaveformMode (waveformMode);
 sf2WaveformLcd.setWaveformMode (waveformMode);
 sliceWaveformLcd.setWaveformMode (waveformMode);
 headerBar.dualFrame().setPadGridActive (false);
 headerBar.setWaveMode (waveformMode);
 headerBar.setMidiFollowActive (processor.midiSelectsSlice.load());
}


bool DysektEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
 for (auto& f : files)
 {
 auto ext = juce::File (f).getFileExtension().toLowerCase();
 if (ext == ".wav" || ext == ".aif" || ext == ".aiff" ||
 ext == ".ogg" || ext == ".flac" || ext == ".mp3" ||
 ext == ".sf2" || ext == ".sfz")
 return true;
 }
 return false;
}

void DysektEditor::filesDropped (const juce::StringArray& files, int, int)
{
 if (files.isEmpty()) return;
 juce::File f (files[0]);
 processor.zoom.store (1.0f);
 processor.scroll.store (0.0f);
 showTrimDialog (f);
}

// =============================================================================
// SFZ-PLAYER zone builder — Add Zone / Save SFZ
// =============================================================================
// Ported from SfzPlayerDropdownPanel's existing implementation (that panel is
// never shown live — see PluginEditor investigation history). Logic is
// unchanged: same processor.sfzPlayer2 calls, same <region> block written to
// disk, same overlay classes. Only the sample-picking step differs — the
// dropdown panel used its own private SfzFileBrowser instance; here we use a
// plain native juce::FileChooser instead, since the live browserPanel's
// kAddZone mode is wired specifically for the Slicer's showTrimDialog flow
// (see browserPanel.onLoadRequest, uiMode == 0 branch) and isn't a fit for
// writing SFZ <region> blocks.

void DysektEditor::refreshZoneBuilderMatrix (const juce::File& sfzFile)
{
    if (sfzFile.existsAsFile())
        zoneBuilderKeysPanel.setKeyzones (SfzPlayerDropdownPanel::parseSfzZones (sfzFile));
    else
        zoneBuilderKeysPanel.clearKeyzones();
}

void DysektEditor::openZoneBuilderAddZone()
{
    // Resolve the target SFZ (may be empty if nothing is loaded yet).
    // See onZoneViewToggle's comment: sfzPlayer2 is never actually processed,
    // so sampleData2's tracked path is the real source of truth here too.
    // zoneBuilderTargetSfz is already kept current by onLoadRequest/
    // onZoneViewToggle — reuse it rather than re-querying engine state.
    const juce::File targetSfz = zoneBuilderTargetSfz;

    int prevHiKey = -1;
    if (targetSfz.existsAsFile())
    {
        const auto existing = SfzPlayerDropdownPanel::parseSfzZones (targetSfz);
        for (const auto& z : existing)
            prevHiKey = juce::jmax (prevHiKey, z.hiKey);
    }
    // Zones staged-but-not-yet-saved won't appear in targetSfz's on-disk
    // content yet, but the new zone's default range still needs to start
    // above them, or a second staged Add Zone would silently overlap the
    // first.
    for (const auto& pz : zoneBuilderPendingZones)
        prevHiKey = juce::jmax (prevHiKey, pz.hiKey);

    zoneBuilderTargetSfz  = targetSfz;
    zoneBuilderPrevHiKey  = prevHiKey;

    const auto startDir = targetSfz.existsAsFile()
                         ? targetSfz.getParentDirectory()
                         : juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    zoneBuilderSampleChooser = std::make_unique<juce::FileChooser> (
        "Choose a sample for the new zone", startDir,
        "*.wav;*.aif;*.aiff;*.flac;*.ogg");

    zoneBuilderSampleChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto chosen = fc.getResult();
            if (! chosen.existsAsFile())
                return; // cancelled

            if (! zoneBuilderTargetSfz.existsAsFile())
            {
                // No SFZ loaded yet: name one first, then continue below.
                openZoneBuilderSaveAsNew (chosen);
                return;
            }

            showZoneBuilderAddZoneOverlay (zoneBuilderTargetSfz, chosen, zoneBuilderPrevHiKey);
        });
}

void DysektEditor::showZoneBuilderAddZoneOverlay (const juce::File& sfzFile,
                                                   const juce::File& sampleFile,
                                                   int prevHiKey)
{
    const int defaultLo = (prevHiKey < 0) ? 0 : juce::jmin (prevHiKey + 1, 127);

    zoneAddOverlay = std::make_unique<AddZoneOverlay> (
        sampleFile.getFileNameWithoutExtension(), defaultLo);

    zoneAddOverlay->onResult = [this, sfzFile, sampleFile] (int lo, int hi, int root, bool confirmed)
    {
        // Defer the reset so it runs after onResult has returned and
        // AddZoneOverlay is no longer on the call stack (use-after-free fix,
        // matching the pattern in SfzPlayerDropdownPanel).
        juce::MessageManager::callAsync ([this] { hideZoneBuilderOverlays(); });

        if (! confirmed)
            return;

        // Stage rather than write straight to sfzFile — appendZoneToSfz is
        // now only called from commitZoneBuilderPendingZones (SAVE), so
        // several zones can be added/auditioned before deciding to keep them.
        zoneBuilderPendingZones.push_back ({ sampleFile, lo, hi, root });
        zoneBuilderDirty = true;
        sliceControlBar.setZoneDirty (true);

        // A bad/unresolvable sample= path parses fine in sfizz (regions=1,
        // load "OK") but renders total silence, which looks identical to a
        // genuine pipeline failure from the outside — matrix populated, LCD/
        // waveform/slice-count all stay empty, no error anywhere. Log the
        // exact path that's about to be written and flag it up front if the
        // source file doesn't even exist, so that silent case is diagnosable
        // from the log alone instead of requiring a manual file dump.
        processor.crashLogger.log ("Zone-builder stage: sampleFile=\"" + sampleFile.getFullPathName()
            + "\" exists=" + (sampleFile.existsAsFile() ? "YES" : "NO")
            + "  will write sample=\"" + buildZoneRegionText (zoneBuilderTargetSfz, sampleFile, lo, hi, root)
                  .fromFirstOccurrenceOf ("sample=", false, false)
                  .upToFirstOccurrenceOf ("\n", false, false) + "\"");

        // refreshZoneBuilderScratch() drives both the matrix and the
        // sliceManager2/sampleData2 preview from the rebuilt scratch file —
        // see its doc comment for why sfzPlayer2.loadFile() alone isn't
        // enough to reach the slice view.
        refreshZoneBuilderScratch();
        zoneBuilderKeysPanel.autoScrollToZones();
        repaint();
    };

    addAndMakeVisible (*zoneAddOverlay);
    zoneAddOverlay->setBounds (getLocalBounds());
    zoneAddOverlay->toFront (true);
}

// Builds the raw "<region>...</region>"-style text block for one zone.
// Shared by appendZoneToSfz (the real on-disk write, at SAVE time) and
// refreshZoneBuilderScratch (the in-memory preview) so the two paths can
// never drift out of sync with each other.
juce::String DysektEditor::buildZoneRegionText (const juce::File& sfzFile, const juce::File& sampleFile,
                                                 int loKey, int hiKey, int rootKey)
{
    juce::String samplePath;
    const auto sfzDir = sfzFile.getParentDirectory();
    if (sampleFile.isAChildOf (sfzDir))
        samplePath = sampleFile.getRelativePathFrom (sfzDir).replaceCharacter ('\\', '/');
    else
        samplePath = sampleFile.getFullPathName().replaceCharacter ('\\', '/');

    return "\n<region>\n"
           "sample="          + samplePath              + "\n"
           "lokey="           + juce::String (loKey)    + "\n"
           "hikey="           + juce::String (hiKey)    + "\n"
           "pitch_keycenter=" + juce::String (rootKey)  + "\n"
           "volume=-7\n"
           "pan=0\n"
           "tune=0\n"
           "ampeg_release=0.664\n";
}

bool DysektEditor::appendZoneToSfz (const juce::File& sfzFile, const juce::File& sampleFile,
                                     int loKey, int hiKey, int rootKey)
{
    const juce::String region = buildZoneRegionText (sfzFile, sampleFile, loKey, hiKey, rootKey);

    juce::FileOutputStream stream (sfzFile);
    if (stream.failedToOpen())
        return false;

    stream.setPosition (sfzFile.getSize());
    stream.writeText (region, false, false, nullptr);
    stream.flush();
    return ! stream.getStatus().failed();
}

// Rebuilds the scratch preview file — zoneBuilderTargetSfz's real on-disk
// content plus one <region> block per staged-but-unsaved pending zone — and
// points sfzPlayer2/the zone matrix at it. Called every time a zone is
// staged so the KeysPanel matrix and the slice/waveform preview both reflect
// all pending zones without ever touching zoneBuilderTargetSfz itself.
void DysektEditor::refreshZoneBuilderScratch()
{
    if (! zoneBuilderTargetSfz.existsAsFile())
        return;

    // Scratch file lives alongside the real target (hidden, dot-prefixed) so
    // relative sample= paths — computed relative to whichever file is being
    // loaded — resolve identically whether sfizz opens this file or the real
    // target.
    zoneBuilderScratchFile = zoneBuilderTargetSfz.getSiblingFile (
        "." + zoneBuilderTargetSfz.getFileNameWithoutExtension() + ".zonebuilder_scratch.sfz");

    // Start from a byte-for-byte copy of the real on-disk target — NOT a
    // loadFileAsString() + juce::String concatenation + replaceWithText
    // rebuild — then append each staged zone through the exact same
    // FileOutputStream-based appendZoneToSfz() that SAVE itself uses. That
    // keeps the preview's bytes and the eventual real commit's bytes
    // mechanically identical, so a sfizz-strict-parser-vs-app-lenient-parser
    // mismatch (matrix shows zones sfizz refuses to load) can't be introduced
    // by the scratch-rebuild step itself.
    zoneBuilderTargetSfz.copyFileTo (zoneBuilderScratchFile);

    for (const auto& pz : zoneBuilderPendingZones)
        appendZoneToSfz (zoneBuilderScratchFile, pz.sampleFile, pz.loKey, pz.hiKey, pz.rootKey);

    processor.sfzPlayer2.loadFile (zoneBuilderScratchFile, processor.fileLoadPool);
    processor.sfzPlayer2ChannelMask.store (1u << 2, std::memory_order_relaxed); // ch2 default
    // See showZoneBuilderAddZoneOverlay's onResult (and the older comment
    // that used to live there) for why this call is required: sliceManager2/
    // sampleData2 are only populated by the async soundfont decode, not by
    // sfzPlayer2.loadFile() alone.
    processor.loadSoundFontAsync (zoneBuilderScratchFile, SoundFontLoadTarget::SfzPlayer2);
    refreshZoneBuilderMatrix (zoneBuilderScratchFile);
}

// SAVE — commit every staged pending zone to the real zoneBuilderTargetSfz
// on disk (via the same appendZoneToSfz used before staging existed), then
// clear staging state and point the live preview back at the real file.
void DysektEditor::commitZoneBuilderPendingZones()
{
    if (zoneBuilderPendingZones.empty())
        return;

    if (! zoneBuilderTargetSfz.existsAsFile())
        return;

    for (const auto& pz : zoneBuilderPendingZones)
    {
        if (! appendZoneToSfz (zoneBuilderTargetSfz, pz.sampleFile, pz.loKey, pz.hiKey, pz.rootKey))
        {
            messageOverlay = std::make_unique<MessageOverlay> (
                "Save Failed",
                "Could not write to:\n" + zoneBuilderTargetSfz.getFullPathName(),
                MessageOverlay::Kind::Warning);
            addAndMakeVisible (*messageOverlay);
            messageOverlay->setBounds (getLocalBounds());
            messageOverlay->toFront (true);
            messageOverlay->onDismiss = [this] { messageOverlay.reset(); };
            return; // leave everything staged so the user can retry SAVE
        }
    }

    zoneBuilderPendingZones.clear();
    zoneBuilderDirty = false;
    sliceControlBar.setZoneDirty (false);

    // The scratch file's job is done now that its contents are for real.
    if (zoneBuilderScratchFile.existsAsFile())
        zoneBuilderScratchFile.deleteFile();
    zoneBuilderScratchFile = juce::File();

    processor.sfzPlayer2.loadFile (zoneBuilderTargetSfz, processor.fileLoadPool);
    processor.sfzPlayer2ChannelMask.store (1u << 2, std::memory_order_relaxed);
    processor.loadSoundFontAsync (zoneBuilderTargetSfz, SoundFontLoadTarget::SfzPlayer2);
    refreshZoneBuilderMatrix (zoneBuilderTargetSfz);
    repaint();
}

// DISCARD — drop every staged pending zone without writing anything, and
// restore the live preview back to zoneBuilderTargetSfz's actual on-disk
// state.
void DysektEditor::discardZoneBuilderPendingZones()
{
    zoneBuilderPendingZones.clear();
    zoneBuilderDirty = false;
    sliceControlBar.setZoneDirty (false);

    if (zoneBuilderScratchFile.existsAsFile())
        zoneBuilderScratchFile.deleteFile();
    zoneBuilderScratchFile = juce::File();

    if (zoneBuilderTargetSfz.existsAsFile())
    {
        processor.sfzPlayer2.loadFile (zoneBuilderTargetSfz, processor.fileLoadPool);
        processor.sfzPlayer2ChannelMask.store (1u << 2, std::memory_order_relaxed);
        processor.loadSoundFontAsync (zoneBuilderTargetSfz, SoundFontLoadTarget::SfzPlayer2);
    }
    refreshZoneBuilderMatrix (zoneBuilderTargetSfz);
    repaint();
}

// Called after the user has already picked a sample but no SFZ is loaded yet.
// Shows "Name your SFZ file", creates a blank file, then proceeds to AddZoneOverlay.
void DysektEditor::openZoneBuilderSaveAsNew (const juce::File& sampleFile)
{
    const auto defaultPath = sampleFile.getParentDirectory().getChildFile ("Custom.sfz");
    zoneSaveOverlay = std::make_unique<SaveSfzOverlay> (defaultPath);

    zoneSaveOverlay->onResult = [this, sampleFile] (const juce::File& dest, bool confirmed)
    {
        juce::MessageManager::callAsync ([this] { hideZoneBuilderOverlays(); });

        if (! confirmed || dest == juce::File{})
            return;

        // Always create a fresh blank SFZ. Plain ASCII only in this comment —
        // see the file-level note on why non-ASCII source literals are worth
        // avoiding here specifically.
        dest.replaceWithText ("// Custom SFZ - built with SFZ-PLAYER zone builder\n\n");

        zoneBuilderTargetSfz = dest;

        // Brand-new target — nothing can possibly be staged against it yet.
        zoneBuilderPendingZones.clear();
        zoneBuilderDirty = false;
        sliceControlBar.setZoneDirty (false);
        if (zoneBuilderScratchFile.existsAsFile())
            zoneBuilderScratchFile.deleteFile();
        zoneBuilderScratchFile = juce::File();

        processor.sfzPlayer2.loadFile (dest, processor.fileLoadPool);
        processor.sfzPlayer2ChannelMask.store (1u << 2, std::memory_order_relaxed); // ch2 default
        // See showZoneBuilderAddZoneOverlay's onResult for why this call is
        // required: sliceManager2/sampleData2 are only populated by the async
        // soundfont decode, not by sfzPlayer2.loadFile() alone.
        processor.loadSoundFontAsync (dest, SoundFontLoadTarget::SfzPlayer2);
        refreshZoneBuilderMatrix (dest);
        repaint();

        // Now show the key-range dialog with the already-chosen sample.
        juce::MessageManager::callAsync ([this, sampleFile]
        {
            showZoneBuilderAddZoneOverlay (zoneBuilderTargetSfz, sampleFile, zoneBuilderPrevHiKey);
        });
    };

    addAndMakeVisible (*zoneSaveOverlay);
    zoneSaveOverlay->setBounds (getLocalBounds());
    zoneSaveOverlay->toFront (true);
}

void DysektEditor::hideZoneBuilderOverlays()
{
    if (zoneAddOverlay)
    {
        removeChildComponent (zoneAddOverlay.get());
        zoneAddOverlay.reset();
    }
    if (zoneSaveOverlay)
    {
        removeChildComponent (zoneSaveOverlay.get());
        zoneSaveOverlay.reset();
    }
}
