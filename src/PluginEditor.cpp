#include "PluginEditor.h"
#include "ui/DysektLookAndFeel.h"
#include "ui/PluginEditorConstants.h"

// ========================== FILEPATH HELPERS ==========================
static juce::File getSettingsDir()
{
 return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
 .getChildFile ("DYSEKT");
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
 sliceLane (p),
 waveformView (p),
 waveformOverview (p),
 sliceControlBar (p),
 browserPanel (p),
 mixerPanel (p),
      eqPanel (p),
 sfzDropdown (p),
 padGridView (p),
 shortcutsPanel (p)
{
 juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
 setLookAndFeel (&lnf);

 addAndMakeVisible (logoBar);
 addAndMakeVisible (headerBar);

 addAndMakeVisible (sliceLcd);
 addAndMakeVisible (sliceWaveformLcd);
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
 // When a new SF2/SFZ is loaded from the dropdown, reset the restore flag
 // so the timer re-populates the zone matrix on the next completed load.
 sfzDropdown.onFileLoaded = [this] (const juce::File&)
 {
     sfzPanelRestored = false;
 };

 // SFZ loaded → add one sequencer track automatically (channel 15, 0-based).
 // SF2 loaded → fires once preset list is ready; no tracks yet — user assigns
 // per-preset channels by right-clicking preset rows in the key zone matrix.
 sfzDropdown.onSfzFileLoaded = [this] (const juce::File& f, bool isSfz)
 {
     if (isSfz)
     {
         const juce::String name = f.getFileNameWithoutExtension();
         pianoRollPanel.addSfzInstrumentTrack (name, juce::Colour (0xFF407060));
     }
     // For SF2: do nothing here — tracks appear via onPresetChannelAssigned.
 };

 // SF2 preset right-clicked → user assigned a MIDI channel → create track.
 sfzDropdown.onPresetChannelAssigned = [this] (const Sf2PresetInfo& preset, int midiChannel1Based)
 {
     // Pick a colour based on the preset number (bank*128 + program).
     static const juce::Colour kPalette[] = {
         juce::Colour (0xFF4060A0), juce::Colour (0xFF60A040),
         juce::Colour (0xFFA04060), juce::Colour (0xFF40A0A0),
         juce::Colour (0xFFA0A040), juce::Colour (0xFF8060C0),
     };
     const int colIdx = (preset.bank * 128 + preset.preset) % 6;
     pianoRollPanel.addOrUpdateSfPresetTrack (preset, midiChannel1Based, kPalette[colIdx]);
 };

 // Track-header right-click on an SF track → change MIDI channel.
 pianoRollPanel.onSfTrackChannelChanged = [this] (int trackIndex, int midiChannel1Based)
 {
     const auto info = pianoRollPanel.getTrackInfo (trackIndex);
     if (info.type == TrackType::SfPlayer)
         pianoRollPanel.addOrUpdateSfPresetTrack (info.preset, midiChannel1Based, info.colour);
 };
 shortcutsPanel.setVisible (false);
 addChildComponent (shortcutsPanel);
    addChildComponent (pianoRollPanel);
    addChildComponent (arrangeView);

    // Wire ArrangeView double-click → show PianoRollPanel as overlay
    arrangeView.onClipDoubleClicked = [this] (int trackIndex, int clipIndex)
    {
        pianoRollPanel.setActiveTrackPublic (trackIndex, clipIndex);
        pianoRollPanel.setVisible (true);
        pianoRollPanel.toFront (false);
        resized();
    };
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
 browserPanel.onLoadRequest = [this] (const juce::File& f) { showTrimDialog (f); };
 waveformView.onLoadRequest = [this] (const juce::File& f) { showTrimDialog (f); };
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
    headerBar.onSeqToggle   = [this] { toggleSeqPanel(); };

 ensureDefaultThemes();
 loadUserSettings();

 // If SF-Player mode was restored from settings, set up the panel.
 // loadUserSettings() sets uiMode directly (bypassing setUiMode), so
 // the timer-driven sfzPanelRestored path will call panelDidShow() once
 // sfzPlayer.isLoaded() becomes true (async after setStateInformation).
 if (uiMode == 1)
 {
     sfzDropdown.setVisible (true);
     // sfzPanelRestored starts false; the timerCallback will populate zones.
 }

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

 float apvtsScale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
 if (apvtsScale == 1.0f && savedScale > 0.0f && savedScale != apvtsScale)
 {
 if (auto* param = processor.apvts.getParameter (ParamIds::uiScale))
 param->setValueNotifyingHost (param->convertTo0to1 (savedScale));
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

 setWantsKeyboardFocus (true);
 setResizable (true, false);
 setResizeLimits (kBaseW / 2, kTotalH / 2, 3840, 2160);
 if (auto* c = getConstrainer())
     c->setFixedAspectRatio ((double) kBaseW / (double) kTotalH);
 setSize (kBaseW, kTotalH);
 lastUiSnapshotVersion = processor.getUiSliceSnapshotVersion();
 startTimerHz (30);
}

DysektEditor::~DysektEditor()
{
 juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
 setLookAndFeel (nullptr);
}

// ── Interface mode switch ─────────────────────────────────────────────────────
void DysektEditor::setUiMode (int mode)
{
 if (uiMode == mode) return;
 uiMode = mode;
 // Leaving slicer mode — reset pad view to waveform
 if (uiMode != 0) { showPadGrid = false; sliceControlBar.setPadViewActive (false); }

 // Keep the EDIT|SFZ tab in sync
 headerBar.dualFrame().setPadGridActive (uiMode == 1);

 // Slicer note highlights must not appear on the SF-player keyboard.
 // Disable them as soon as we enter SF-player mode (uiMode == 1), before
 // any soundfont has been loaded, so the KeysPanel guard works from the
 // very first paint in that mode.
 sfzDropdown.keysPanel.setSlicerHighlightEnabled (uiMode == 0);

 // Hide waveform overview immediately when switching to SFZ mode
 waveformOverview.setVisible (uiMode == 0 && !showPadGrid);

 // Show/hide sfzDropdown panel based on mode
 if (uiMode == 1)
 {
     sfzDropdown.setVisible (true);
     // Reset the restore flag so the timer will call panelDidShow once
     // the player is confirmed loaded (handles async load after project open).
     sfzPanelRestored = false;
     if (processor.sfzPlayer.isLoaded())
     {
         sfzDropdown.panelDidShow();
         sfzPanelRestored = true;
     }
 }
 else
 {
     sfzDropdown.setVisible (false);
 }

 // Persist the new mode
 float scale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
 saveUserSettings (scale, getTheme().name);

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
            pianoRollPanel.setVisible (false);
            arrangeView.setVisible (false);
            headerBar.setSeqActive (false);
        }
        activeSlot = SlotContent::Browser;
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
 if (ext == ".sf2" || ext == ".sfz") {
 processor.loadSoundFontAsync (file);
 return;
 }
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
 headerBar.setBrowserActive (activeSlot == SlotContent::Browser);
 headerBar.setWaveMode (waveformMode);
 float scale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
 saveUserSettings (scale, getTheme().name);
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
        arrangeView.setVisible (false);
        pianoRollPanel.setVisible (false);
        headerBar.setSeqActive (false);
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
        arrangeView.setVisible (true);
        pianoRollPanel.setVisible (false);  // opens only on clip double-click
        headerBar.setSeqActive (true);
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
 saveUserSettings (processor.apvts.getRawParameterValue (ParamIds::uiScale)->load(), name);
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
 const float sf = (float) ed.getWidth() / (float) kBaseW;
 const int kFrameInset = juce::roundToInt (4.0f * sf);
 const int kFX = juce::roundToInt (kMargin * sf);
 const int kFW = ed.getWidth() - kFX * 2;
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

 const float sf = (float) getWidth() / (float) kBaseW;
 const auto screenF = outerF.reduced (4.0f * sf);
 g.setColour (getTheme().darkBar.darker (0.55f));
 g.fillRoundedRectangle (screenF, 2.0f);

 g.setColour (juce::Colours::black.withAlpha (0.18f));
 for (int y = juce::roundToInt (screenF.getY()); y < juce::roundToInt (screenF.getBottom()); y += 2)
 g.drawHorizontalLine (y, screenF.getX(), screenF.getRight());

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
 || (themeEditorPanel != nullptr);
 if (modalActive) return;

 const bool wvVisible  = waveformView.isVisible() && waveformView.getHeight() > 0;
 const bool padVisible = padGridView.isVisible()  && padGridView.getHeight()  > 0;

 // Scale all border pixel amounts proportionally to avoid sub-pixel overlap
 // at non-integer UI scales (1.5×, 1.75× etc.)
 const float sf = (float) getWidth() / (float) kBaseW;

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
 const bool sfzVisible = sfzDropdown.isVisible() && sfzDropdown.getHeight() > 0;
 if (sfzVisible)
 {
 const auto outerF = waveformFrameRect (*this, sfzDropdown.getBounds(), false);
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
 // ── Scale factor: all fixed-pixel constants scale with component width ────
 // This prevents layout overlap when the host scales component bounds in
 // response to setTransform (e.g. at 1.5× and above on high-DPI displays).
 const float sf = (float) getWidth() / (float) kBaseW;
 auto si = [sf](int v) -> int { return juce::roundToInt ((float) v * sf); };

 auto area = juce::Rectangle<int> (0, 0, getWidth(), getHeight());

 // ── Top strip ─────────────────────────────────────────────────────────────
 // In PAD mode shrink LCD rows to 65% — frees ~116px for the pad grid
 const int lcdRowH = (uiMode == 1) ? juce::roundToInt (kLcdRowH * sf * 0.65f) : si (kLcdRowH);
 const int ctrlFrmH = (uiMode == 1) ? juce::roundToInt (kCtrlFrameH * sf * 0.65f) : si (kCtrlFrameH);
 const int kTopStripH = si (kLogoH) + lcdRowH;
 auto topArea = area.removeFromTop (kTopStripH);
 auto topRow = topArea.reduced (si (kMargin), si (4));

 const int sideW = (topRow.getWidth() - si (kCtrlFrameW) - si (kMargin) * 2) / 2;
 sliceLcd.setBounds (topRow.removeFromLeft (sideW));
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

 auto actionArea = area.removeFromTop (si (kActionH));
 const int kFX = si (kMargin);
 const int kFW = getWidth() - si (kMargin) * 2;

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
 // Expand mixer to fill ALL available area (waveformView space + slot)
 const int mixTop = actionArea.getY();
 const int mixBot = slot.getBottom();
 mixerPanel.setBounds (kFX, mixTop, kFW, mixBot - mixTop);
 browserPanel.setBounds ({});
 eqPanel.setBounds ({});
 pianoRollPanel.setBounds ({});
 arrangeView.setBounds ({});
 }
 else if (activeSlot == SlotContent::Browser && ! initBrowserOpen) {
 // Expand browser to fill ALL available area (waveformView space + slot)
 const int browserTop = actionArea.getY();
 const int browserBot = slot.getBottom();
 browserPanel.setBounds (kFX, browserTop, kFW, browserBot - browserTop);
 mixerPanel.setBounds ({});
 eqPanel.setBounds ({});
 pianoRollPanel.setBounds ({});
 arrangeView.setBounds ({});
 }
 else if (activeSlot == SlotContent::Eq) {
     const int eqTop = actionArea.getY();
     const int eqBot = slot.getBottom();
     eqPanel.setBounds (kFX, eqTop, kFW, eqBot - eqTop);
     mixerPanel.setBounds ({});
     browserPanel.setBounds ({});
     pianoRollPanel.setBounds ({});
     arrangeView.setBounds ({});
 }
 else if (activeSlot == SlotContent::Seq) {
     const int seqTop = actionArea.getY();
     const int seqBot = slot.getBottom();
     const int seqH   = seqBot - seqTop;

     arrangeView.setBounds (kFX, seqTop, kFW, seqH);

     // PianoRollPanel floats as overlay on top of ArrangeView when visible
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

     mixerPanel.setBounds ({});
     browserPanel.setBounds ({});
     eqPanel.setBounds ({});
 } else {
 mixerPanel.setBounds ({});
 eqPanel.setBounds ({});
 pianoRollPanel.setBounds ({});
 arrangeView.setBounds ({});
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
 const bool hasRealSample = hasSampleLoaded
 && sampleSnap != nullptr
 && ! sampleSnap->filePath.containsIgnoreCase ("DYSEKT_default.wav");

 const bool normalBrowserOpen = (activeSlot == SlotContent::Browser && ! initBrowserOpen);

 if (trimDialog != nullptr) {
 sliceControlBar.setBounds ({});
 waveformOverview.setVisible (false);
 waveformOverview.setBounds ({});
 } else {
 // SCB first (bottommost), then overview row sits immediately above it.
 if (hasRealSample && uiMode == 0 && activeSlot != SlotContent::Mixer && !normalBrowserOpen)
 {
     {
         const int scbH = si (kSliceCtrlH);
         if (showPadGrid)
         {
             // In pad mode reserve the SCB height plus equal padding above and below
             // so the bar sits visually centred between the pad grid and the
             // plugin bottom edge rather than flush against the pad grid.
             const int pad      = si (kMargin);
             auto scbStrip      = area.removeFromBottom (scbH + pad * 2);
             const int scbY     = scbStrip.getY() + (scbStrip.getHeight() - scbH) / 2;
             sliceControlBar.setBounds (kFX, scbY, kFW, scbH);
         }
         else
         {
             auto scbStrip = area.removeFromBottom (scbH);
             sliceControlBar.setBounds (kFX, scbStrip.getY(), kFW, scbH);
         }
     }
 }
 else
 {
     sliceControlBar.setBounds ({});
 }

 // Overview row: allocate space and show only when waveform view is active.
 if (uiMode == 0 && activeSlot != SlotContent::Mixer && !normalBrowserOpen && hasRealSample && !showPadGrid)
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
     waveformView.setVisible (false);   waveformView.setBounds ({});
     sfzDropdown.setVisible  (false);   sfzDropdown.setBounds  ({});
     padGridView.setVisible  (false);   padGridView.setBounds  ({});
 }
 else if (initBrowserOpen)
 {
     // No real sample yet — browser occupies the full waveform frame area
     browserPanel.setBounds (screenX, y, screenW, h);
     waveformView.setVisible (false);   waveformView.setBounds ({});
     sfzDropdown.setVisible  (false);   sfzDropdown.setBounds  ({});
     padGridView.setVisible  (false);   padGridView.setBounds  ({});
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
 }
 else
 {
     // SFZ player layout
     sfzDropdown.setVisible (true);
     sfzDropdown.setBounds (juce::Rectangle<int> (screenX, y, screenW, waveH));

     waveformView.setVisible (false);
     waveformView.setBounds ({});
     padGridView.setVisible (false);
     padGridView.setBounds ({});
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
 pianoRollPanel.setVisible (false);
 arrangeView.setVisible (false);
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
            pianoRollPanel.setVisible (false);
            arrangeView.setVisible (false);
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
 if (code == juce::KeyPress::escapeKey &&
     activeSlot == SlotContent::Seq &&
     pianoRollPanel.isVisible())
 {
     pianoRollPanel.setVisible (false);
     resized(); repaint();
     return true;
 }

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

 if (code == 'L')
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

 float userScale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load();
 float scale = userScale * hostScale;
 if (scaleDirty || scale != lastScale)
 {
 scaleDirty = false; lastScale = scale;
 // Reset to base size first so the host doesn't compound the scale on
 // top of a previously-enlarged component (avoids double-scaling at 1.5×+)
 setTransform (juce::AffineTransform{});
 setSize (kBaseW, kTotalH);
 setTransform (juce::AffineTransform::scale (scale));
 DysektLookAndFeel::setMenuScale (scale);
 saveUserSettings (userScale, getTheme().name); // persist user portion only — hostScale must not be baked in
 uiChanged = true;
 }

 const bool playbackActive = std::any_of (processor.voicePool.voicePositions.begin(),
 processor.voicePool.voicePositions.end(),
 [] (const std::atomic<float>& pos) { return pos.load (std::memory_order_relaxed) > 0.0f; });

 const bool waveformAnimating = waveformInteracting || previewActive
 || playbackActive || processor.lazyChop.isActive()
 || (processor.liveDragSliceIdx.load (std::memory_order_relaxed) >= 0);
 const bool waveformShowing = (uiMode == 0 && ! showPadGrid) || processor.trimModeActive.load (std::memory_order_relaxed);
 const bool waveformNeedsRepaint = waveformShowing && (uiChanged || viewportChanged || waveformAnimating || lastWaveformAnimating);
 const bool laneNeedsRepaint = waveformShowing && (uiChanged || viewportChanged || previewActive || lastPreviewActive);

 lastWaveformAnimating = waveformAnimating;
 lastPreviewActive = previewActive;

 if (trimSession != nullptr && ! trimSession->active)
 {
 auto snap = processor.sampleData.getSnapshot();
 if (snap != nullptr && snap->filePath == trimSession->file.getFullPathName())
 {
 // Trim mode requires the waveform view — auto-switch if in Pad Grid mode.
 if (uiMode != 0)
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

  if (uiMode == 1 && (uiChanged || playbackActive))
     sfzDropdown.repaint();

 // SF-player async restore: once sfzPlayer finishes loading after
 // setStateInformation (or a fresh UI open), repopulate the zone matrix
 // and apply the saved preset index. Runs each timer tick until done.
 if (uiMode == 1 && ! sfzPanelRestored)
 {
     if (processor.sfzPlayer.isLoaded())
     {
         // getPresetList() drains freshPresets; first call after load
         // populates cachedPresets inside sfzPlayer.
         const auto presets = processor.sfzPlayer.getPresetList();
         if (! presets.empty())
         {
             // Apply any preset index that was saved by setStateInformation.
             const int pending = processor.pendingSfzPresetIndex.exchange (
                 -1, std::memory_order_relaxed);
             if (pending >= 0)
                 processor.sfzPlayer.setPresetByIndex (pending);

             sfzDropdown.panelDidShow();
             sfzPanelRestored = true;
         }
     }
 }

 sliceLcd.repaintLcd();
 sliceWaveformLcd.repaintLcd();

 {
 auto timerSnap = processor.sampleData.getSnapshot();
 const bool hasSample = (timerSnap != nullptr
 && timerSnap->buffer.getNumSamples() > 0);
 if (hasSample != hasSampleLoaded)
 {
 hasSampleLoaded = hasSample;
 resized(); // expand/collapse SCB + zoom bar
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
 sliceControlBar.updateMidiLearnPulse();
 sliceControlBar.repaint();
  if (activeSlot == SlotContent::Mixer) mixerPanel.updateFromSnapshot();
if (activeSlot == SlotContent::Seq)   pianoRollPanel.syncSnap();

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
                pianoRollPanel.onSliceChromaticToggled (
                    i, true, sl.chromaticChannel, sliceName, sl.colour);
            }
            else
            {
                pianoRollPanel.onSliceChromaticToggled (
                    i, false, 0, {}, juce::Colours::transparentBlack);
            }
        }
    }
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
 saveUserSettings (processor.apvts.getRawParameterValue (ParamIds::uiScale)->load(), themeName);
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
 saveUserSettings (processor.apvts.getRawParameterValue (ParamIds::uiScale)->load(), themeName);
 repaint();
}

void DysektEditor::setScaleFactor (float newScale)
{
 // On Windows HiDPI, JUCE's component peer already multiplies coordinates by
 // the OS DPI scale factor internally. The VST3 host (Nuendo 12, Studio One 7)
 // calls setScaleFactor() with the *content* scale it expects (e.g. 2.0 on a
 // 200 % display). If we store that raw value and later do
 // setTransform(scale(userScale * newScale))
 // we double-apply the OS scaling → UI renders at 4× / crops badly.
 //
 // Fix: divide newScale by the display's native DPI scale so that the product
 // userScale * hostScale
 // represents only the delta above what JUCE already handles.
 float nativeSF = 1.0f;
 if (auto* disp = juce::Desktop::getInstance().getDisplays()
 .getDisplayForRect (getScreenBounds()))
 nativeSF = juce::jmax (0.01f, (float) disp->scale);

 hostScale = newScale / nativeSF;
 scaleDirty = true;

 // Apply immediately — Nuendo 12 queries the window size synchronously
 // right after this call, so we cannot wait for the next timer tick.
 const float scale = processor.apvts.getRawParameterValue (ParamIds::uiScale)->load()
 * hostScale;
 if (std::abs (scale - lastScale) > 0.001f)
 {
 lastScale = scale;
 scaleDirty = false;
 setTransform (juce::AffineTransform{});
 setSize (kBaseW, kTotalH);
 setTransform (juce::AffineTransform::scale (scale));
 DysektLookAndFeel::setMenuScale (scale);
 }
}

void DysektEditor::saveUserSettings (float scale, const juce::String& themeName)
{
 auto file = getUserSettingsFile();
 file.getParentDirectory().createDirectory();

 file.replaceWithText ("uiScale: " + juce::String (scale, 2)
 + "\ntheme: " + themeName
 + "\nwaveStyle: " + juce::String (waveformMode)
 + "\nuiMode: " + juce::String (uiMode) + "\n");
}

void DysektEditor::loadUserSettings()
{
 savedScale = -1.0f;
 juce::String themeName = "dark";
 auto file = getUserSettingsFile();
 if (file.existsAsFile())
 {
 for (auto line : juce::StringArray::fromLines (file.loadFileAsString()))
 {
 line = line.trim();
 if (line.startsWith ("uiScale:"))
 {
 float v = line.fromFirstOccurrenceOf (":", false, false).trim().getFloatValue();
 if (v >= 0.5f && v <= 2.0f) savedScale = v;
 }
 else if (line.startsWith ("theme:"))
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
 uiMode = juce::jlimit (0, 1, val);
 }
 }
 }
 applyTheme (themeName);

 waveformView.setWaveformMode (waveformMode);
 waveformOverview.setWaveformMode (waveformMode);
 headerBar.dualFrame().setPadGridActive (uiMode == 1);
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
