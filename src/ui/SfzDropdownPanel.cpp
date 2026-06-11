// =============================================================================
//  SfzDropdownPanel.cpp  —  SF2 / SFZ instrument strip with inline file browser
// =============================================================================
#include "SfzDropdownPanel.h"
#include "DysektLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../PluginEditor.h"
#include <set>
#include <algorithm>

// ── Layout constants (header strip) ──────────────────────────────────────────
static constexpr int kPickerW      = 160;   // narrowed to fit ADSR knobs in strip
static constexpr int kKnobW        = 52;
static constexpr int kMeterW       = 60;
static constexpr int kPresetArrowW = 18;
static constexpr int kFolderIconW  = 20;
static constexpr int kPad          = 6;
static constexpr int kKnobGap      = 4;

// =============================================================================
//  SfzFileBrowser
// =============================================================================

SfzFileBrowser::SfzFileBrowser()
{
    list.setModel (this);
    list.setRowHeight (kRowH);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId,    juce::Colours::transparentBlack);
    addAndMakeVisible (list);

    // Don't call navigateTo() in the constructor — layout isn't ready yet.
    // But DO initialise currentDir to a valid root so that navigateUp() never
    // sees a default-constructed File() and accidentally fires navigateToRoots().
    {
        juce::Array<juce::File> roots;
        juce::File::findFileSystemRoots (roots);
        if (roots.size() > 0)
            currentDir = roots[0];
    }
}

SfzFileBrowser::~SfzFileBrowser()
{
    stopTimer();
    list.setModel (nullptr);
}

// ── paint ────────────────────────────────────────────────────────────────────

void SfzFileBrowser::paint (juce::Graphics& g)
{
    const auto& theme = getTheme();

    // Background
    g.setColour (theme.darkBar.darker (0.45f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    // Breadcrumb bar background
    g.setColour (theme.darkBar.darker (0.20f));
    g.fillRect (breadcrumbZone);

    // Back/up button (← — navigate to parent directory)
    {
        const bool canGoUp  = !atVirtualRoot;
        const bool upHover  = upBtnZone.contains (getMouseXYRelative()) && canGoUp;
        if (upHover)
        {
            g.setColour (theme.accent.withAlpha (0.18f));
            g.fillRoundedRectangle (upBtnZone.toFloat(), 2.0f);
        }
        g.setColour (canGoUp ? theme.accent.withAlpha (0.90f)
                             : theme.accent.withAlpha (0.30f));
        g.drawText (u8"\u2190", upBtnZone, juce::Justification::centred, false);
    }

    // Current path text — shows "Drives" label when in the virtual root view
    {
        const auto pathArea = breadcrumbZone.withTrimmedLeft (upBtnZone.getWidth() + 4)
                                            .withTrimmedRight (4);
        g.setFont (DysektLookAndFeel::makeFont (12.0f));
        g.setColour (theme.foreground.withAlpha (0.55f));

        // Show last 2 path segments so it fits; show "Drives" in virtual-root mode
        juce::String display;
        if (atVirtualRoot)
        {
            display = "Drives";
        }
        else
        {
            const auto parts = juce::StringArray::fromTokens (
                currentDir.getFullPathName(), juce::File::getSeparatorString(), "");
            const int n = parts.size();
            if      (n == 0) display = "/";
            else if (n <= 2) display = currentDir.getFullPathName();
            else             display = u8"\u2026" + juce::File::getSeparatorString()
                                     + parts[n - 2] + juce::File::getSeparatorString()
                                     + parts[n - 1];
        }

        g.drawText (display, pathArea, juce::Justification::centredLeft, true);
    }

    // Separator between breadcrumb and list
    g.setColour (theme.accent.withAlpha (0.12f));
    g.fillRect (0, breadcrumbZone.getBottom(), getWidth(), 1);
}

// ── resized ───────────────────────────────────────────────────────────────────

void SfzFileBrowser::resized()
{
    constexpr int upW = 24;
    breadcrumbZone = { 0, 0, getWidth(), kBreadcrumbH };
    upBtnZone      = { 0, 1, upW, kBreadcrumbH - 2 };

    list.setBounds (0, kBreadcrumbH + 1, getWidth(), getHeight() - kBreadcrumbH - 1);
}

// ── mouseDown ────────────────────────────────────────────────────────────────

void SfzFileBrowser::mouseMove (const juce::MouseEvent&)
{
    repaint (breadcrumbZone);  // refresh hover highlight on up/drive buttons
}

void SfzFileBrowser::mouseDown (const juce::MouseEvent& e)
{
    if (upBtnZone.contains (e.getPosition()))
    {
        navigateUp();
        return;
    }
    // Clicks below the breadcrumb are handled by the ListBox itself
}

// ── navigation ───────────────────────────────────────────────────────────────

void SfzFileBrowser::navigateTo (const juce::File& dir)
{
    if (! dir.isDirectory()) return;
    atVirtualRoot = false;
    navigated     = true;
    currentDir = dir;
    rebuildList();
    repaint();
}

void SfzFileBrowser::navigateUp()
{
    if (! isVisible()) return;   // ignore if browser is closed
    if (atVirtualRoot) return;
    const auto parent = currentDir.getParentDirectory();
    if (parent == currentDir)
        navigateToRoots();   // already at a filesystem root — go to drive picker
    else
        navigateTo (parent);
}

void SfzFileBrowser::navigateToRoots()
{
    if (! isVisible()) return;   // ignore if browser is closed

    // Use JUCE's cross-platform API to enumerate all filesystem roots.
    // On Windows this yields every present drive letter (C:\, D:\, etc.).
    // On macOS/Linux it yields /.
    juce::Array<juce::File> roots;
    juce::File::findFileSystemRoots (roots);

#if JUCE_MAC
    // findFileSystemRoots returns only / on macOS; /Volumes/* are the actual
    // named mounts (external drives, network shares, other partitions).
    auto volumes = juce::File ("/Volumes");
    if (volumes.isDirectory())
    {
        auto vols = volumes.findChildFiles (juce::File::findDirectories, false);
        vols.sort();
        for (auto& v : vols)
        {
            bool dupe = false;
            for (auto& r : roots)
                if (r == v) { dupe = true; break; }
            if (! dupe)
                roots.add (v);
        }
    }
#elif !JUCE_WINDOWS
    // Linux: /media and /mnt are conventional mountpoints for removable drives.
    for (const char* mp : { "/media", "/mnt", "/run/media" })
    {
        juce::File m (mp);
        if (m.isDirectory())
        {
            bool dupe = false;
            for (auto& r : roots)
                if (r == m) { dupe = true; break; }
            if (! dupe)
                roots.add (m);
        }
    }
#endif

    rows.clear();
    for (auto& r : roots)
        rows.add (r);

    atVirtualRoot = true;   // breadcrumb shows "Drives" label instead of a path
    navigated     = true;
    list.updateContent();
    list.repaint();
    repaint();
}

void SfzFileBrowser::setMode (Mode m)
{
    mode = m;
    rebuildList();
}

void SfzFileBrowser::rebuildList()
{
    rows.clear();

    // Directories first (hidden files excluded)
    auto dirs = currentDir.findChildFiles (
        juce::File::findDirectories, false, "*");
    dirs.removeIf ([] (const juce::File& f) { return f.isHidden(); });
    dirs.sort();

    // Matching files — pattern depends on current mode
    const auto* pattern = (mode == Mode::kAddZone)
                            ? "*.wav;*.aif;*.aiff;*.flac;*.ogg"
                            : "*.sf2;*.sfz";

    auto files = currentDir.findChildFiles (
        juce::File::findFiles, false, pattern);
    files.removeIf ([] (const juce::File& f) { return f.isHidden(); });
    files.sort();

    rows.addArray (dirs);
    rows.addArray (files);

    list.updateContent();
    list.repaint();
    repaint();   // breadcrumb path has changed
}

void SfzFileBrowser::setRootDirectory (const juce::File& dir)
{
    navigateTo (dir);
}

void SfzFileBrowser::showDrives()
{
    navigateToRoots();
}

// ── ListBoxModel ──────────────────────────────────────────────────────────────

int SfzFileBrowser::getNumRows() { return rows.size(); }

bool SfzFileBrowser::isDirectory (int row) const
{
    if (row < 0 || row >= rows.size()) return false;
    return rows[row].isDirectory();
}

juce::File SfzFileBrowser::fileForRow (int row) const
{
    if (row < 0 || row >= rows.size()) return {};
    return rows[row];
}

void SfzFileBrowser::paintListBoxItem (int row, juce::Graphics& g,
                                        int w, int h, bool selected)
{
    if (row < 0 || row >= rows.size()) return;

    const auto& theme = getTheme();
    const auto& f     = rows[row];
    const bool  isDir = f.isDirectory();

    if (selected)
    {
        g.setColour (theme.accent.withAlpha (0.14f));
        g.fillAll();
    }

    g.setFont (DysektLookAndFeel::makeFont (16.0f));  // icon size — matches FileBrowserPanel::kIconSize

    if (isDir)
    {
        g.setColour (theme.accent.withAlpha (0.55f));
        g.drawText (u8"\U0001F4C1", 3, 0, 22, h, juce::Justification::centredLeft, false);
        g.setFont (DysektLookAndFeel::makeFont (13.0f));
        g.setColour (selected ? theme.accent : theme.foreground.withAlpha (0.80f));
        g.drawText (f.getFileName(), 28, 0, w - 32, h,
                    juce::Justification::centredLeft, true);
    }
    else
    {
        // Extension badge (only for files with a known extension)
        const auto ext = f.getFileExtension().toUpperCase().trimCharactersAtStart (".");
        if (ext.isEmpty())
        {
            g.setFont (DysektLookAndFeel::makeFont (13.0f));
            g.setColour (selected ? theme.accent : theme.foreground.withAlpha (0.80f));
            g.drawText (f.getFileName(), 6, 0, w - 10, h,
                        juce::Justification::centredLeft, true);
        }
        else
        {
            const int  badgeW = 36;
            const auto badgeRect = juce::Rectangle<int> (w - badgeW - 4, (h - 14) / 2, badgeW, 14);
            g.setColour (theme.accent.withAlpha (0.18f));
            g.fillRoundedRectangle (badgeRect.toFloat(), 2.0f);
            g.setFont (DysektLookAndFeel::makeFont (10.0f));
            g.setColour (theme.accent.withAlpha (0.80f));
            g.drawText (ext, badgeRect, juce::Justification::centred, false);

            // Filename
            g.setFont (DysektLookAndFeel::makeFont (13.0f));
            g.setColour (selected ? theme.accent : theme.foreground.withAlpha (0.80f));
            g.drawText (f.getFileNameWithoutExtension(), 6, 0, w - badgeW - 14, h,
                        juce::Justification::centredLeft, true);
        }
    }
}

void SfzFileBrowser::listBoxItemClicked (int /*row*/, const juce::MouseEvent&)
{
    repaint();
}

void SfzFileBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    loadRow (row);
}

juce::String SfzFileBrowser::getTooltipForRow (int /*row*/)
{
    return {};
}

void SfzFileBrowser::loadRow (int row)
{
    if (row < 0 || row >= rows.size()) return;
    const auto& f = rows[row];

    if (f.isDirectory())
    {
        navigateTo (f);
    }
    else if (onFileChosen)
    {
        onFileChosen (f);
    }
}

void SfzFileBrowser::timerCallback() {}

// =============================================================================
//  SfzDropdownPanel — constructor / destructor
// =============================================================================

SfzDropdownPanel::SfzDropdownPanel (DysektProcessor& p)
    : processor (p),
      keysPanel (p)
{
    addChildComponent (keysPanel);

    // ── Inline file browser ───────────────────────────────────────────────────
    fileBrowser.onFileChosen = [this] (const juce::File& f) { onFileChosen (f); };
    fileBrowser.onDismiss = [this]
    {
        fileBrowser.setMode (SfzFileBrowser::Mode::kSfz);
        closeBrowser();
    };
    addChildComponent (fileBrowser);

    // ── SF2 program grid ──────────────────────────────────────────────────────
    // Left-click in the grid auditions the preset (handled by onPreviewToggled).
    // It must NOT close the grid — the grid stays open until the user explicitly
    // navigates away. Nulling onPresetSelected prevents the fallback close path
    // in Sf2ProgramGrid::mouseDown from firing.
    programGrid.onPresetSelected = nullptr;
    // ── Multitimbral channel assignment ──────────────────────────────────────
    // Right-click on a cell calls this with the preset index and the chosen
    // MIDI channel (1-16), or ch==0 to deactivate.
    //
    // Routing is strictly 1:1: incoming MIDI ch N plays FluidSynth channel N-1.
    // setPresetOnChannel() loads the correct preset onto each FluidSynth channel.
    // No fan-out mask is needed — each MIDI channel has exactly one destination.
    programGrid.onChannelChanged = [this] (int presetIdx, int ch)
    {
        if (presetIdx < 0 || presetIdx >= (int) presetList.size()) return;
        const auto& info = presetList[(size_t) presetIdx];

        // Collision check: refuse to assign a channel already owned by a chromatic slice.
        if (ch >= 1 && ch <= 16)
        {
            const uint32_t chromaMask = processor.chromaticSliceChannelMask.load (std::memory_order_relaxed);
            if (chromaMask & (1u << ch))
                return;   // channel is slicer-owned — silently reject
        }

        if (ch == 0)
        {
            // Deactivate — silence all FluidSynth channels, then reload only the
            // still-assigned presets.  This is the safest way to remove one
            // entry without needing to track which channel it was on here.
            for (int c = 0; c < 16; ++c)
                processor.sfzPlayer.setPresetOnChannel (c, 0, 0);

            const auto& chMap = programGrid.getPresetChannels();
            for (auto& kv : chMap)
            {
                if (kv.first == presetIdx) continue;
                if (kv.second >= 1 && kv.second <= 16 && kv.first < (int)presetList.size())
                    processor.sfzPlayer.setPresetOnChannel (kv.second - 1,
                                                            presetList[(size_t)kv.first].bank,
                                                            presetList[(size_t)kv.first].preset);
            }

            if (onPresetChannelAssigned)
                onPresetChannelAssigned (info, 0);

            // Rebuild sfPlayerChannelMask from the presets that are still assigned.
            {
                uint32_t mask = 0u;
                const auto& chMap2 = programGrid.getPresetChannels();
                for (const auto& kv : chMap2)
                {
                    if (kv.first == presetIdx) continue; // this one is being removed
                    if (kv.second >= 1 && kv.second <= 16)
                        mask |= (1u << kv.second);
                }
                processor.sfPlayerChannelMask.store      (mask, std::memory_order_relaxed);
                processor.savedSfPlayerChannelMask.store (mask, std::memory_order_relaxed);
            }
        }
        else
        {
            // Assign: load preset onto FluidSynth channel (ch-1, 0-based).
            // A controller transmitting on MIDI ch N will route 1:1 to FluidSynth
            // channel N-1, so this preset will be played when the controller
            // sends on the assigned MIDI channel.
            processor.sfzPlayer.setPresetOnChannel (ch - 1, info.bank, info.preset);

            // Add this channel to sfPlayerChannelMask so MIDI on ch N reaches the SF player.
            {
                uint32_t mask = processor.sfPlayerChannelMask.load (std::memory_order_relaxed);
                mask |= (1u << ch);
                processor.sfPlayerChannelMask.store      (mask, std::memory_order_relaxed);
                processor.savedSfPlayerChannelMask.store (mask, std::memory_order_relaxed);
            }

            if (onPresetChannelAssigned)
                onPresetChannelAssigned (info, ch);
        }

        // sfPlayerChannelMask is now updated above on both assign and deactivate,
        // so MIDI routing stays in sync with the grid's channel assignments.
    };

    // ── Preview toggle: left-click radio ─────────────────────────────────────
    // index == -1 → deactivate preview; index >= 0 → audition on ch15.
    // If the preset already has a real channel assigned, left-click just
    // highlights it visually — no need to re-route anything.
    programGrid.onPreviewToggled = [this] (int idx)
    {
        if (idx < 0)
        {
            processor.sfzPlayer.clearPreview();
            return;
        }

        if (idx >= (int) presetList.size()) return;
        const auto& info = presetList[(size_t) idx];

        // If this preset already has a real MIDI channel assigned, the audio
        // routing is already correct — just highlight it visually.
        const auto& chMap = programGrid.getPresetChannels();
        if (chMap.count (idx) && chMap.at (idx) >= 1)
            return;  // already routed on a real channel

        // Pure audition: load onto ch15 and route live input there.
        processor.sfzPlayer.previewPreset (info.bank, info.preset);
    };

    programGrid.onAssignedPresetClicked = [this] (int idx)
    {
        // Sync combo selection to the channel of the clicked preset
        if (idx < 0 || idx >= (int) presetList.size()) return;
        const auto& chMap = programGrid.getPresetChannels();
        if (! chMap.count (idx)) return;
        const int ch = chMap.at (idx);
        // channel-range spinners don't need per-preset sync
        juce::ignoreUnused (ch);
    };

    addChildComponent (programGrid);

    // [+ ZONE] always visible — openAddZoneChooser() creates a Custom.sfz if nothing is loaded
    keysPanel.setAddZoneButtonVisible (true);
    keysPanel.onAddZoneRequested = [this] { openAddZoneChooser(); };

    startTimerHz (30);
}

SfzDropdownPanel::~SfzDropdownPanel() = default;

// =============================================================================
//  Layout
// =============================================================================

void SfzDropdownPanel::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    // Strip order (left → right):
    // [picker 310] [gap] [TRN] [FINE] [REV] [CHO] [PAN] [VOL] [METER]
    auto strip = juce::Rectangle<int> (0, 0, w, kStripH).reduced (kPad, 0);
    strip.removeFromLeft (4);   // left margin

    // Preset picker (wider, no LOAD button)
    auto pickerSlot = strip.removeFromLeft (kPickerW);
    nameZone = pickerSlot.withSizeKeepingCentre (kPickerW, kStripH - 6);
    strip.removeFromLeft (kPad * 2);

    // Right-side knobs
    meterZone   = strip.removeFromRight (kMeterW);
    strip.removeFromRight (kPad);

    // MIDI activity LED — immediately left of VU meter
    {
        const int ledSize = juce::jmin (14, strip.getHeight() - 4);
        midiLedZone = strip.removeFromRight (ledSize + 4)
                          .withSizeKeepingCentre (ledSize, ledSize);
    }
    strip.removeFromRight (kPad);

    volZone    = strip.removeFromRight (kKnobW);
    strip.removeFromRight (kKnobGap);
    panZone    = strip.removeFromRight (kKnobW);
    strip.removeFromRight (kPad);

    rvSizeZone = strip.removeFromRight (kKnobW);
    strip.removeFromRight (kKnobGap);
    rvMixZone  = strip.removeFromRight (kKnobW);
    strip.removeFromRight (kPad);

    fineZone   = strip.removeFromRight (kKnobW);
    strip.removeFromRight (kKnobGap);
    transZone  = strip.removeFromRight (kKnobW);
    strip.removeFromRight (kPad);

    // ADSR knobs — now in the top strip, after picker
    adsrRelZone = strip.removeFromRight (kKnobW);
    strip.removeFromRight (kKnobGap);
    adsrSusZone = strip.removeFromRight (kKnobW);
    strip.removeFromRight (kKnobGap);
    adsrDecZone = strip.removeFromRight (kKnobW);
    strip.removeFromRight (kKnobGap);
    adsrAtkZone = strip.removeFromRight (kKnobW);

    // Ch-FX knobs reuse the same pixel zones as ADSR (shown only when SF2 loaded)
    chMixZone  = adsrAtkZone;
    chSizeZone = adsrDecZone;
    chDampZone = adsrSusZone;
    chGainZone = adsrRelZone;

    // SF2 channel-range spinner zone: all the knob slots that are hidden when
    // SF2 is loaded (ADSR + TRN + FINE) become available real-estate.  Use the
    // combined bounding box of those six zones for a wide, readable spinner.
    // When SFZ is loaded these knobs show normally; the spinner is invisible.
    chComboZone = adsrAtkZone
                      .getUnion (adsrDecZone)
                      .getUnion (adsrSusZone)
                      .getUnion (adsrRelZone)
                      .getUnion (transZone)
                      .getUnion (fineZone)
                      .expanded (kKnobGap / 2, 0);

    // Sub-divide nameZone:
    //   [< arrow] [folder icon] [label] [> arrow]
    {
        auto z = nameZone;
        presetDecBtn  = z.removeFromLeft  (kPresetArrowW);
        presetIncBtn  = z.removeFromRight (kPresetArrowW);
        folderIconZone = z.removeFromRight (kFolderIconW);
        presetLabel   = z;
    }

    // ── Keyboard panel ────────────────────────────────────────────────────────
    const int kbY = kStripH;  // ADSR is now in the top strip, no extra row
    const int kbH = juce::jmax (60, h - kbY);
    // isSf2: true when an SF2 is loaded OR being loaded asynchronously.
    // Checking getPendingFilePath() avoids the keysPanel flash during the
    // window between loadFile() and isLoaded() becoming true (e.g. in VST3
    // after setStateInformation fires before FluidSynth finishes).
    const bool isSf2Loaded = [&]() -> bool {
        if (processor.sfzPlayer.isLoaded())
            return processor.sfzPlayer.getLoadedFile()
                       .getFileExtension().toLowerCase() == ".sf2";
        return processor.sfzPlayer.getPendingFilePath()
                   .getFileExtension().toLowerCase() == ".sf2";
    }();
    keysPanel.setVisible (kbH > 0 && ! browserOpen && ! programPickerOpen && ! isSf2Loaded);
    if (kbH > 0)
        keysPanel.setBounds (kPad, kbY, w - kPad * 2, kbH);
    else
        keysPanel.setBounds ({});

    // ── Inline browser overlay ────────────────────────────────────────────────
    if (browserOpen)
    {
        fileBrowser.setBounds (kPad, kStripH + 1, w - kPad * 2, h - kStripH - 1);
        fileBrowser.setVisible (true);
    }
    else
    {
        fileBrowser.setVisible (false);
        fileBrowser.setBounds ({});
    }

    // ── SF2 program grid overlay ──────────────────────────────────────────────
    if (programPickerOpen)
    {
        programGrid.setBounds (kPad, kStripH + 1, w - kPad * 2, h - kStripH - 1);
        programGrid.setVisible (true);
    }
    else
    {
        programGrid.setVisible (false);
        programGrid.setBounds ({});
    }

    // ── Channel-range spinner hit-zones ──────────────────────────────────
    // Laid out inside chComboZone (the TRN+FINE area when SF2 is loaded).
    // "CH [◂ 1 ▸] – [◂ 16 ▸]"
    {
        // Spinner centred inside the full chComboZone width (~336 px).
        // Large hit targets for easy clicking.
        const int btnW   = 28;   // ◂ / ▸ arrow
        const int numW   = 38;   // two-digit channel number + padding
        const int gap    = 10;
        const int sepW   = 28;   // " – " separator
        const int labelW = 30;   // "CH" prefix
        const int widgetW = labelW + gap + btnW + numW + btnW + gap + sepW + gap + btnW + numW + btnW;
        auto z = chComboZone.withSizeKeepingCentre (widgetW, chComboZone.getHeight());

        chRangeLabelZone = z.removeFromLeft (labelW);
        z.removeFromLeft (gap);

        chLowDec   = z.removeFromLeft (btnW);
        chLowLabel = z.removeFromLeft (numW);
        chLowInc   = z.removeFromLeft (btnW);
        z.removeFromLeft (gap);

        z.removeFromLeft (sepW);
        z.removeFromLeft (gap);

        chHighDec   = z.removeFromLeft (btnW);
        chHighLabel = z.removeFromLeft (numW);
        chHighInc   = z.removeFromLeft (btnW);
    }
}

// =============================================================================
//  Browser open / close
// =============================================================================

void SfzDropdownPanel::openBrowser()
{
    if (browserOpen) return;
    browserOpen = true;

    if (processor.sfzPlayer.isLoaded())
    {
        // Navigate to the directory of the currently loaded file
        fileBrowser.setRootDirectory (
            processor.sfzPlayer.getLoadedFile().getParentDirectory());
    }
    else if (! fileBrowser.hasNavigated())
    {
        // First-ever open with nothing loaded — pick the best default directory
        const juce::File::SpecialLocationType candidates[] = {
            juce::File::userMusicDirectory,
            juce::File::userDocumentsDirectory,
            juce::File::userDesktopDirectory,
            juce::File::userHomeDirectory,
        };
        juce::File startDir;
        for (auto loc : candidates)
        {
            auto d = juce::File::getSpecialLocation (loc);
            if (d.isDirectory()) { startDir = d; break; }
        }
        if (startDir.isDirectory())
            fileBrowser.setRootDirectory (startDir);
        else
            fileBrowser.showDrives();
    }
    // else: browser has been used before — leave it where the user left it

    resized();
    repaint();
}

void SfzDropdownPanel::closeBrowser()
{
    if (! browserOpen) return;
    browserOpen = false;
    resized();
    repaint();
}

// =============================================================================
//  Program grid open / close
// =============================================================================

void SfzDropdownPanel::openProgramGrid()
{
    if (programPickerOpen) return;
    programPickerOpen = true;

    programGrid.setPresets (presetList,
                            processor.sfzPlayer.getCurrentPresetIndex(),
                            processor.sfzPlayer.getMidiChannel());
    {
        const uint32_t mask = processor.sfPlayerChannelMask.load (std::memory_order_relaxed);
        int lo = 0, hi = 0;
        if (mask != 0)
        {
            for (int c = 2; c <= 16; ++c)  if (mask & (1u << c)) { lo = c; break; }
            for (int c = 16; c >= 2; --c)  if (mask & (1u << c)) { hi = c; break; }
        }
        programGrid.setChannelRange (lo, hi);
    }
    restoreGridChannelAssignments();
    resized();
    repaint();
}

void SfzDropdownPanel::closeProgramGrid()
{
    if (! programPickerOpen) return;
    programPickerOpen = false;

    // Stop any active preview: clear ch15 routing and reset the grid visual.
    processor.sfzPlayer.clearPreview();
    programGrid.clearPreviewState();

    resized();
    repaint();
}

void SfzDropdownPanel::restoreGridChannelAssignments()
{
    // Rebuild programGrid.presetChannels from sf2Presets (which survives reloads).
    // Without this, presets that were assigned in a previous session have current==0
    // in the grid, causing "Deactivate" to be permanently greyed out.
    std::unordered_map<int, int> chMap;
    for (const auto& ap : sf2Presets)
    {
        for (int i = 0; i < (int) presetList.size(); ++i)
        {
            if (presetList[(size_t) i].name == ap.name)
            {
                chMap[i] = ap.ch;
                break;
            }
        }
    }
    programGrid.setPresetChannels (chMap);
}

// =============================================================================
//  SF2 channel combo helpers
// =============================================================================
void SfzDropdownPanel::buildSf2Combo()
{
    // sf2ChCombo removed — channel routing is now via sfPlayerChannelMask (bitmask).
    // This method is retained as a no-op so callers (notifyPresetChannelChanged)
    // don't need changes.
}

void SfzDropdownPanel::notifyPresetChannelChanged (const juce::String& presetName,
                                                    int midiCh1Based)
{
    // midiCh1Based == 0 means the mapping was removed
    if (midiCh1Based == 0)
    {
        sf2Presets.erase (std::remove_if (sf2Presets.begin(), sf2Presets.end(),
            [&] (const AssignedPreset& ap) { return ap.name == presetName; }),
            sf2Presets.end());

        // If the removed channel was selected, clear selection
        const bool stillValid = std::any_of (sf2Presets.begin(), sf2Presets.end(),
            [&] (const AssignedPreset& ap) { return ap.ch == selectedSf2Ch + 1; });
        if (! stillValid) selectedSf2Ch = -1;
    }
    else
    {
        // Add or update
        bool found = false;
        for (auto& ap : sf2Presets)
        {
            if (ap.name == presetName) { ap.ch = midiCh1Based; found = true; break; }
        }
        if (! found)
            sf2Presets.push_back ({ presetName, midiCh1Based });
        if (selectedSf2Ch < 0)
            selectedSf2Ch = midiCh1Based - 1;
    }
    buildSf2Combo();
    resized();
    repaint();
}

void SfzDropdownPanel::onFileChosen (const juce::File& f)
{
    if (fileBrowser.getMode() == SfzFileBrowser::Mode::kAddZone)
    {
        // Reset browser back to SFZ mode before showing the overlay
        fileBrowser.setMode (SfzFileBrowser::Mode::kSfz);
        closeBrowser();

        if (! addZoneTargetSfz.existsAsFile())
        {
            // No SFZ loaded yet: ask the user to name a new file first,
            // then continue to the key-range overlay with the chosen sample.
            const juce::File chosenSample = f;  // capture before lambda
            openSaveAsNewForZone (chosenSample);
            return;
        }

        showAddZoneOverlay (addZoneTargetSfz, f, addZonePrevHiKey);
        return;
    }

    processor.sfzPlayer.loadFile (f, processor.fileLoadPool);
    processor.sfPlayerChannelMask.store (0x1FFFEu, std::memory_order_relaxed); // omni
    reloadZones (f);
    closeBrowser();
    if (f.getFileExtension().toLowerCase() == ".sf2")
        openProgramGrid();
    else
        closeProgramGrid();
    repaint();

    if (onFileLoaded)
        onFileLoaded (f);

    {
        const bool isSfz = f.getFileExtension().toLowerCase() == ".sfz";
        if (onSfzFileLoaded)
            onSfzFileLoaded (f, isSfz);
    }
}

// =============================================================================
//  Paint
// =============================================================================

void SfzDropdownPanel::paint (juce::Graphics& g)
{
    const auto& theme = getTheme();
    const int   w     = getWidth();
    const int   h     = getHeight();

    // Full background
    {
        const auto bounds = getLocalBounds().toFloat();
        juce::ColourGradient bg (theme.darkBar.darker (0.35f), 0.f, 0.f,
                                  theme.darkBar.darker (0.10f), 0.f, (float) h, false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (bounds, 4.0f);

        const int sepY = kStripH;
        g.setColour (theme.accent.withAlpha (0.18f));
        g.fillRect (kPad, sepY, w - kPad * 2, 1);
    }

    drawHeaderStrip (g);

    // When SF2 is loaded, repurpose the ADSR strip area for per-channel FX
    {
        const auto& f = processor.sfzPlayer.getLoadedFile();
        const bool isSf2 = f.existsAsFile() && f.hasFileExtension (".sf2");
        if (isSf2)
            drawSf2ChStrip (g);
        else
            drawAdsrStrip (g);
    }

    g.setColour (theme.accent.withAlpha (0.45f));
    g.fillRect (0, 0, w, 1);
}

// =============================================================================
//  drawAdsrStrip
// =============================================================================

void SfzDropdownPanel::drawAdsrStrip (juce::Graphics& g) const
{
    // Attack: 0-30 s, normalised
    drawKnob (g, adsrAtkZone,
              juce::jlimit (0.f, 1.f, processor.sfzPlayer.getSfzAttack()  / 30.0f),
              "ATK",
              juce::String (processor.sfzPlayer.getSfzAttack(), 2) + "s");

    // Decay: 0-30 s
    drawKnob (g, adsrDecZone,
              juce::jlimit (0.f, 1.f, processor.sfzPlayer.getSfzDecay()   / 30.0f),
              "DEC",
              juce::String (processor.sfzPlayer.getSfzDecay(), 2) + "s");

    // Sustain: 0-100 %
    drawKnob (g, adsrSusZone,
              juce::jlimit (0.f, 1.f, processor.sfzPlayer.getSfzSustain() / 100.0f),
              "SUS",
              juce::String (juce::roundToInt (processor.sfzPlayer.getSfzSustain())) + "%");

    // Release: 0-60 s
    drawKnob (g, adsrRelZone,
              juce::jlimit (0.f, 1.f, processor.sfzPlayer.getSfzRelease() / 60.0f),
              "REL",
              juce::String (processor.sfzPlayer.getSfzRelease(), 2) + "s");
}

// =============================================================================
//  drawSf2ChStrip  —  per-channel FX knobs shown instead of ADSR when SF2 loaded
// =============================================================================

void SfzDropdownPanel::drawSf2ChStrip (juce::Graphics& g) const
{
    const auto& theme = getTheme();
    const auto accent  = theme.accent;
    const auto dim     = theme.foreground.withAlpha (0.45f);
    const auto bright  = theme.foreground;

    // ── Per-channel FX knobs (mix/size/damp/gain) ─────────────────────────
    const float chMix  = processor.sfzPlayer.getReverbMix() / 100.0f;
    const float chSize = processor.sfzPlayer.getReverbSize() / 100.0f;
    const float chDamp = processor.sfzPlayer.getReverbDamp() / 100.0f;
    const float chGain = processor.sfzPlayer.getVolume();

    // MIX/SIZE/PAN/VOL already drawn by drawHeaderStrip() — do not duplicate here.

    // ── Channel-range spinner: CH [◂ lo ▸] – [◂ hi ▸] ───────────────────
    // Drawn inside chComboZone (the TRN+FINE area).  Hit-zones are set in resized().
    const int lo = cachedChLow;
    const int hi = cachedChHigh;
    const bool disabled = (lo == 0);

    // "CH" prefix
    g.setFont (DysektLookAndFeel::makeFont (11.f, true));
    g.setColour (dim);
    g.drawText ("CH", chRangeLabelZone, juce::Justification::centred, false);

    // Helper lambda: draw one spinner (dec button, number label, inc button)
    auto drawSpinner = [&](juce::Rectangle<int> decR,
                            juce::Rectangle<int> numR,
                            juce::Rectangle<int> incR,
                            int value)
    {
        // Subtle button backgrounds
        g.setColour ((disabled ? dim : accent).withAlpha (0.18f));
        g.fillRoundedRectangle (decR.toFloat(), 3.f);
        g.fillRoundedRectangle (incR.toFloat(), 3.f);

        // Arrows (plain ASCII so MSVC code page 1252 never chokes)
        g.setColour (disabled ? dim : accent);
        g.setFont (DysektLookAndFeel::makeFont (13.f));
        g.drawText ("<", decR, juce::Justification::centred, false);
        g.drawText (">", incR, juce::Justification::centred, false);

        // Value
        g.setColour (disabled ? dim : bright);
        g.setFont (DysektLookAndFeel::makeFont (14.f, true));
        const auto valStr = disabled ? juce::String ("--") : juce::String (value);
        g.drawText (valStr, numR, juce::Justification::centred, false);
    };

    drawSpinner (chLowDec,  chLowLabel,  chLowInc,  lo);

    // " - " separator
    const auto sepR = juce::Rectangle<int> (chLowInc.getRight(), chLowInc.getY(),
                                            chHighDec.getX() - chLowInc.getRight(),
                                            chLowInc.getHeight());
    g.setColour (dim);
    g.setFont (DysektLookAndFeel::makeFont (13.f));
    g.drawText ("-", sepR, juce::Justification::centred, false);

    drawSpinner (chHighDec, chHighLabel, chHighInc, hi);
}



void SfzDropdownPanel::drawHeaderStrip (juce::Graphics& g) const
{
    const auto& theme = getTheme();
    drawPresetPicker (g);

    const auto& f = processor.sfzPlayer.getLoadedFile();
    const bool isSf2 = f.existsAsFile() && f.hasFileExtension (".sf2");

    // TRN + FINE are replaced by the sf2ChCombo widget when SF2 is loaded
    if (! isSf2)
    {
        drawKnob (g, transZone, transToNorm (processor.sfzPlayer.getTranspose()),
                  "TRN",
                  [&]() -> juce::String {
                      const int s = processor.sfzPlayer.getTranspose();
                      return s == 0 ? "0st" : (s > 0 ? "+" : "") + juce::String (s) + "st";
                  }());

        drawKnob (g, fineZone, fineToNorm (processor.sfzPlayer.getFineTune()),
                  "FINE",
                  [&]() -> juce::String {
                      const float c = processor.sfzPlayer.getFineTune();
                      return (c >= 0 ? "+" : "") + juce::String (c, 0) + "c";
                  }());
    }

    drawKnob (g, rvMixZone, processor.sfzPlayer.getReverbMix() / 100.0f,
              "MIX",
              juce::String (juce::roundToInt (processor.sfzPlayer.getReverbMix())) + "%");

    drawKnob (g, rvSizeZone, processor.sfzPlayer.getReverbSize() / 100.0f,
              "SIZE",
              juce::String (juce::roundToInt (processor.sfzPlayer.getReverbSize())) + "%");

    drawKnob (g, panZone, panToNorm (processor.sfzPlayer.getPan()),
              "PAN",
              [&]() -> juce::String {
                  const float p = processor.sfzPlayer.getPan();
                  if (std::abs (p) < 0.01f) return "C";
                  const int pct = juce::roundToInt (std::abs (p) * 100);
                  return (p < 0 ? "L" : "R") + juce::String (pct);
              }());

    drawKnob (g, volZone, volToNorm (processor.sfzPlayer.getVolume()),
              "VOL",
              [&]() -> juce::String {
                  const float db = juce::Decibels::gainToDecibels (processor.sfzPlayer.getVolume());
                  return db <= -95.f ? "-inf" : juce::String (db, 1) + "dB";
              }());

    drawMeter (g);

    // ── MIDI activity LED ─────────────────────────────────────────────────────
    if (! midiLedZone.isEmpty())
    {
        const juce::Colour ledColour = midiLedOn
            ? theme.accent.brighter (0.3f)
            : theme.darkBar.darker (0.2f);
        const juce::Colour borderColour = midiLedOn
            ? theme.accent
            : theme.foreground.withAlpha (0.15f);

        g.setColour (ledColour);
        g.fillEllipse (midiLedZone.toFloat());
        g.setColour (borderColour);
        g.drawEllipse (midiLedZone.toFloat().reduced (0.5f), 1.0f);

        g.setColour (theme.foreground.withAlpha (0.55f));
        g.setFont (juce::Font (7.0f));
        g.drawText ("M", midiLedZone.translated (0, midiLedZone.getHeight() + 1),
                    juce::Justification::centredTop, false);
    }
}

// =============================================================================
//  drawPresetPicker
// =============================================================================

void SfzDropdownPanel::drawPresetPicker (juce::Graphics& g) const
{
    const auto& theme    = getTheme();
    const bool  isLoaded = processor.sfzPlayer.isLoaded();

    // Background
    {
        auto bg = nameZone.toFloat();
        const bool anyOpen = browserOpen || programPickerOpen;
        g.setColour (anyOpen ? theme.accent.withAlpha (0.10f)
                             : theme.darkBar.darker (0.12f));
        g.fillRoundedRectangle (bg, 3.0f);
        g.setColour (anyOpen ? theme.accent.withAlpha (0.55f)
                             : theme.accent.withAlpha (0.20f));
        g.drawRoundedRectangle (bg.reduced (0.5f), 3.0f, 1.0f);
    }

    // Folder icon (always visible — this is the open/close toggle)
    {
        const bool hover = folderIconZone.contains (getMouseXYRelative());
        g.setFont (DysektLookAndFeel::makeFont (13.0f));
        g.setColour (browserOpen
                     ? theme.accent.withAlpha (0.90f)
                     : hover ? theme.accent.withAlpha (0.70f)
                             : theme.foreground.withAlpha (0.35f));
        g.drawText (u8"\U0001F4C1", folderIconZone, juce::Justification::centred, false);
    }

    // Arrow buttons (only useful when loaded + presets exist)
    auto drawArrow = [&] (juce::Rectangle<int> zone, const juce::String& sym)
    {
        const bool active = isLoaded && ! presetList.empty() && ! browserOpen;
        const bool hover  = zone.contains (getMouseXYRelative()) && active;
        g.setColour (hover ? theme.accent.withAlpha (0.30f) : juce::Colours::transparentBlack);
        g.fillRoundedRectangle (zone.toFloat(), 2.0f);
        g.setFont (DysektLookAndFeel::makeFont (13.0f));
        g.setColour (active ? theme.accent.withAlpha (0.75f)
                            : theme.foreground.withAlpha (0.20f));
        g.drawText (sym, zone, juce::Justification::centred, false);
    };
    drawArrow (presetDecBtn, "<");
    drawArrow (presetIncBtn, ">");

    // Label area
    {
        auto lbl = presetLabel;

        if (browserOpen)
        {
            // Browser is open — show a hint
            g.setFont (DysektLookAndFeel::makeFont (12.0f));
            g.setColour (theme.accent.withAlpha (0.70f));
            g.drawText ("browsing files \u2014 double-click to load", lbl,
                        juce::Justification::centred, true);
        }
        else if (programPickerOpen)
        {
            // Program grid is open
            g.setFont (DysektLookAndFeel::makeFont (12.0f));
            g.setColour (theme.accent.withAlpha (0.70f));
            g.drawText ("select a preset \u2014 right-click for MIDI ch", lbl,
                        juce::Justification::centred, true);
        }
        else if (! isLoaded)
        {
            g.setFont (DysektLookAndFeel::makeFont (12.0f));
            g.setColour (theme.foreground.withAlpha (0.38f));
            g.drawText ("click \U0001F4C1 or drop a file", lbl,
                        juce::Justification::centred, false);
        }
        else if (presetList.empty())
        {
            g.setFont (DysektLookAndFeel::makeFont (12.0f));
            g.setColour (theme.foreground.withAlpha (0.75f));
            g.drawText (processor.sfzPlayer.getLoadedFile().getFileNameWithoutExtension(),
                        lbl, juce::Justification::centred, true);
        }
        else
        {
            const int idx = juce::jlimit (0, (int) presetList.size() - 1,
                                          processor.sfzPlayer.getCurrentPresetIndex());
            const auto& info = presetList[(size_t) idx];

            // Top mini-label
            {
                auto topLine = lbl.removeFromTop (lbl.getHeight() / 2);
                g.setFont (DysektLookAndFeel::makeFont (10.5f));
                g.setColour (theme.foreground.withAlpha (0.38f));
                const auto caption =
                    processor.sfzPlayer.getLoadedFile().getFileNameWithoutExtension()
                    + "  B:" + juce::String (info.bank)
                    + " P:" + juce::String (info.preset);
                g.drawText (caption, topLine, juce::Justification::centred, true);
            }

            // Preset name
            g.setFont (DysektLookAndFeel::makeFont (13.0f));
            g.setColour (theme.foreground);
            g.drawText (info.name, lbl, juce::Justification::centred, true);
        }
    }
}

// =============================================================================
//  drawKnob
// =============================================================================

void SfzDropdownPanel::drawKnob (juce::Graphics& g, juce::Rectangle<int> bounds,
                                   float normalised, const juce::String& label,
                                   const juce::String& valueStr) const
{
    const auto& theme = getTheme();

    const int dia  = juce::jmin (bounds.getHeight() - 6, 26);
    const int cy   = bounds.getCentreY();
    const int cx   = bounds.getX() + 3 + dia / 2;
    const float r  = (float) dia * 0.5f;

    const float startA = juce::MathConstants<float>::pi * 1.25f;
    const float endA   = juce::MathConstants<float>::pi * 2.75f;
    const float angle  = startA + normalised * (endA - startA);

    juce::Path track;
    track.addCentredArc ((float) cx, (float) cy, r - 1.f, r - 1.f, 0.f, startA, endA, true);
    g.setColour (theme.darkBar.brighter (0.15f));
    g.strokePath (track, juce::PathStrokeType (2.0f));

    juce::Path fill;
    fill.addCentredArc ((float) cx, (float) cy, r - 1.f, r - 1.f, 0.f, startA, angle, true);
    g.setColour (theme.accent);
    g.strokePath (fill, juce::PathStrokeType (2.0f));

    const float tx = (float) cx + (r - 4.f) * std::cos (angle - juce::MathConstants<float>::halfPi);
    const float ty = (float) cy + (r - 4.f) * std::sin (angle - juce::MathConstants<float>::halfPi);
    g.setColour (theme.accent.brighter (0.3f));
    g.fillEllipse (tx - 2.f, ty - 2.f, 4.f, 4.f);

    const int textX = cx + (int) r + 5;
    const int textW = bounds.getRight() - textX;

    g.setFont (DysektLookAndFeel::makeFont (10.5f, true));
    g.setColour (theme.foreground.withAlpha (0.38f));
    g.drawText (label,    textX, cy - 10, textW, 10, juce::Justification::centredLeft, false);

    g.setFont (DysektLookAndFeel::makeFont (11.5f));
    g.setColour (theme.foreground.withAlpha (0.82f));
    g.drawText (valueStr, textX, cy,      textW, 10, juce::Justification::centredLeft, false);
}

// =============================================================================
//  drawMeter
// =============================================================================

void SfzDropdownPanel::drawMeter (juce::Graphics& g) const
{
    const auto& theme = getTheme();
    auto area = meterZone.reduced (2, 6);

    const int barW = area.getWidth() / 2 - 2;
    const int barH = area.getHeight();

    auto leftBar  = juce::Rectangle<int> (area.getX(),              area.getY(), barW, barH);
    auto rightBar = juce::Rectangle<int> (area.getX() + barW + 4,  area.getY(), barW, barH);

    g.setColour (theme.darkBar.darker (0.2f));
    g.fillRoundedRectangle (leftBar.toFloat(),  2.0f);
    g.fillRoundedRectangle (rightBar.toFloat(), 2.0f);

    auto drawBar = [&] (juce::Rectangle<int> bar, float peak, float hold)
    {
        const int fillH = juce::roundToInt ((float) bar.getHeight() * juce::jlimit (0.f, 1.f, peak));
        if (fillH > 0)
        {
            juce::ColourGradient grad (theme.accent.withAlpha (0.85f), 0.f, (float) bar.getBottom(),
                                        theme.accent.brighter (0.5f),  0.f, (float) bar.getY(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bar.withTrimmedTop (bar.getHeight() - fillH).toFloat(), 2.0f);
        }
        const int holdY = bar.getBottom() - juce::roundToInt ((float) bar.getHeight()
                           * juce::jlimit (0.f, 1.f, hold));
        g.setColour (theme.accent.brighter (0.6f).withAlpha (0.7f));
        g.fillRect (bar.getX(), holdY - 1, bar.getWidth(), 2);
    };

    drawBar (leftBar,  meterL, holdL);
    drawBar (rightBar, meterR, holdR);
}

// =============================================================================
//  Timer
// =============================================================================

void SfzDropdownPanel::timerCallback()
{
    const float newL = processor.sfzPeakL.load (std::memory_order_relaxed);
    const float newR = processor.sfzPeakR.load (std::memory_order_relaxed);
    if (newL > holdL) holdL = newL;
    if (newR > holdR) holdR = newR;
    holdL *= kHoldDecay;
    holdR *= kHoldDecay;
    meterL = newL;
    meterR = newR;

    // ── MIDI activity LED ─────────────────────────────────────────────────────
    const int activity = processor.sfzMidiActivity.load (std::memory_order_relaxed);
    const bool newLedOn = (activity > 0) || (midiLedHold > 0);
    if (activity > 0)
        midiLedHold = kMidiLedHoldTicks;
    else if (midiLedHold > 0)
        --midiLedHold;

    if (newLedOn != midiLedOn)
    {
        midiLedOn = newLedOn;
        repaint (midiLedZone);
    }

    presetList = processor.sfzPlayer.getPresetList();

    // Poll sfPlayerChannelMask from processor for paint (avoids atomic reads in paint).
    // Derive lo/hi as the lowest and highest set channel bits for spinner display.
    // Channel 1 is hardwired to the slicer and never appears in sfPlayerChannelMask.
    {
        const uint32_t mask = processor.sfPlayerChannelMask.load (std::memory_order_relaxed);
        cachedChLow  = 0;
        cachedChHigh = 0;
        if (mask != 0)
        {
            for (int c = 2; c <= 16; ++c)  if (mask & (1u << c)) { cachedChLow  = c; break; }
            for (int c = 16; c >= 2; --c)  if (mask & (1u << c)) { cachedChHigh = c; break; }
        }
    }

    // Keep the program grid's range and blocked-channel set in sync so the
    // channel picker menu shows correct availability.
    programGrid.setChannelRange    (cachedChLow, cachedChHigh);
    programGrid.setBlockedChannels (
        processor.chromaticSliceChannelMask.load (std::memory_order_relaxed));

    repaint();
}

// =============================================================================
//  Preset navigation
// =============================================================================

void SfzDropdownPanel::selectPreset (int delta)
{
    if (presetList.empty()) return;

    const int cur  = processor.sfzPlayer.getCurrentPresetIndex();
    const int next = juce::jlimit (0, (int) presetList.size() - 1, cur + delta);

    if (next != cur)
    {
        processor.sfzPlayer.setPresetByIndex (next);

        if (processor.sfzPlayer.isLoaded())
            reloadZones (processor.sfzPlayer.getLoadedFile());

        repaint();
    }
}

// =============================================================================
//  MIDI Learn menu
// =============================================================================

void SfzDropdownPanel::showMidiLearnMenu (int fieldId, juce::Point<int> screenPos)
{
    const bool mapped = processor.midiLearn.isMapped (fieldId);
    juce::PopupMenu menu;
    menu.addItem (1, "Learn MIDI CC");
    if (mapped)
        menu.addItem (2, "Clear (" + processor.midiLearn.getLabelText (fieldId) + ")");
    menu.addSeparator();
    menu.addItem (1000, "Open MIDI Learn Dialog...");

    auto* topLvl = getTopLevelComponent();
    float ms = DysektLookAndFeel::getMenuScale();
    menu.showMenuAsync (
        juce::PopupMenu::Options()
            .withTargetScreenArea (juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1))
            .withParentComponent (topLvl)
            .withStandardItemHeight ((int)(24 * ms)),
        [this, fieldId] (int result)
        {
            if (result == 1)      { processor.midiLearn.armLearn (fieldId);     repaint(); }
            else if (result == 2) { processor.midiLearn.clearMapping (fieldId); repaint(); }
            else if (result == 1000)
            {
                if (auto* editor = findParentComponentOfClass<DysektEditor>())
                    editor->keyPressed (juce::KeyPress ('M', juce::ModifierKeys(), 0));
            }
        });
}

// =============================================================================
//  Mouse events
// =============================================================================

void SfzDropdownPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();

    // ── Channel-range spinners (visible in SF2 strip) ─────────────────────
    auto adjustChannel = [&](bool isLow, int delta)
    {
        // Derive current lo/hi from sfPlayerChannelMask for spinner display.
        const uint32_t curMask = processor.sfPlayerChannelMask.load (std::memory_order_relaxed);
        int lo = 0, hi = 0;
        if (curMask != 0)
        {
            for (int c = 2; c <= 16; ++c)  if (curMask & (1u << c)) { lo = c; break; }
            for (int c = 16; c >= 2; --c)  if (curMask & (1u << c)) { hi = c; break; }
        }
        if (lo == 0) lo = 2;   // channel 1 is hardwired to the slicer; SF player starts at 2
        if (hi == 0) hi = lo;

        // Channels owned by chromatic slices are not available to the SF player.
        const uint32_t chromaMask = processor.chromaticSliceChannelMask.load (std::memory_order_relaxed);
        // Channel 1 is also never available to the SF player.
        auto isFree = [&](int ch) -> bool
        {
            if (ch < 2 || ch > 16) return false;
            return ! (chromaMask & (1u << ch));
        };

        if (isLow)
        {
            int newLo = juce::jlimit (2, hi, lo + delta);
            while (newLo >= 2 && newLo <= hi && ! isFree (newLo))
                newLo += delta > 0 ? 1 : -1;
            newLo = juce::jlimit (2, hi, newLo);
            if (isFree (newLo))
            {
                // Build mask for [newLo, hi] but skip chromatic-owned holes so they
                // are never included even when they fall inside the lo–hi range.
                uint32_t mask = 0u;
                for (int c = newLo; c <= hi; ++c)
                    if (isFree (c)) mask |= (1u << c);
                processor.sfPlayerChannelMask.store      (mask, std::memory_order_relaxed);
                processor.savedSfPlayerChannelMask.store (mask, std::memory_order_relaxed);
            }
        }
        else
        {
            int newHi = juce::jlimit (lo, 16, hi + delta);
            while (newHi >= lo && newHi <= 16 && ! isFree (newHi))
                newHi += delta > 0 ? 1 : -1;
            newHi = juce::jlimit (lo, 16, newHi);
            if (isFree (newHi))
            {
                // Build mask for [lo, newHi] but skip chromatic-owned holes.
                uint32_t mask = 0u;
                for (int c = lo; c <= newHi; ++c)
                    if (isFree (c)) mask |= (1u << c);
                processor.sfPlayerChannelMask.store      (mask, std::memory_order_relaxed);
                processor.savedSfPlayerChannelMask.store (mask, std::memory_order_relaxed);
            }
        }
        repaint();
    };

    if (chLowDec .contains (pos)) { adjustChannel (true,  -1); return; }
    if (chLowInc .contains (pos)) { adjustChannel (true,  +1); return; }
    if (chHighDec.contains (pos)) { adjustChannel (false, -1); return; }
    if (chHighInc.contains (pos)) { adjustChannel (false, +1); return; }

    // ── Folder icon — toggle browser ─────────────────────────────────────────
    if (folderIconZone.contains (pos))
    {
        // Close grid if open before opening browser
        if (programPickerOpen) closeProgramGrid();
        if (browserOpen) closeBrowser();
        else             openBrowser();
        return;
    }

    // ── Clicking the label area when browser is closed and no file loaded ─────
    if (presetLabel.contains (pos) && ! browserOpen && ! programPickerOpen
        && ! processor.sfzPlayer.isLoaded())
    {
        openBrowser();
        return;
    }

    // ── Clicking label when the program grid is open — close it ──────────────
    if (nameZone.contains (pos) && programPickerOpen)
    {
        closeProgramGrid();
        return;
    }

    // ── Clicking label when browser is open — close it ────────────────────────
    if (nameZone.contains (pos) && browserOpen)
    {
        closeBrowser();
        return;
    }

    // ── Left-click preset label when SF2 is loaded — open program grid ────────
    if (presetLabel.contains (pos) && ! browserOpen && ! programPickerOpen
        && processor.sfzPlayer.isLoaded()
        && ! presetList.empty()
        && ! e.mods.isRightButtonDown())
    {
        openProgramGrid();
        return;
    }

    // ── Right-click — MIDI Learn menu, Save SFZ As, or SFZ MIDI channel ──────
    if (e.mods.isRightButtonDown())
    {
        if (nameZone.contains (pos) || folderIconZone.contains (pos))
        {
            if (processor.sfzPlayer.isLoaded())
            {
                const auto ext = processor.sfzPlayer.getLoadedFile()
                                     .getFileExtension().toLowerCase();

                if (ext == ".sfz")
                {
                    // SFZ: channel picker + Save As in the same menu
                    const int curCh = processor.sfzPlayer.getMidiChannel();
                    juce::PopupMenu menu;
                    menu.addSectionHeader ("MIDI Input Channel");
                    menu.addItem (200, "Omni (all channels)", true, curCh == 0);
                    menu.addSeparator();
                    for (int ch = 1; ch <= 16; ++ch)
                        menu.addItem (200 + ch, "Channel " + juce::String (ch), true, curCh == ch);
                    menu.addSeparator();
                    menu.addItem (300, "Save SFZ As\u2026");

                    auto* topLvl = getTopLevelComponent();
                    float ms = DysektLookAndFeel::getMenuScale();
                    menu.showMenuAsync (
                        juce::PopupMenu::Options()
                            .withTargetScreenArea (juce::Rectangle<int> (
                                e.getScreenPosition().x, e.getScreenPosition().y, 1, 1))
                            .withParentComponent (topLvl)
                            .withStandardItemHeight ((int)(22 * ms)),
                        [this] (int result)
                        {
                            if (result == 200)
                            {
                                processor.sfzPlayer.setMidiChannel (0);
                            }
                            else if (result > 200 && result <= 216)
                            {
                                const int ch = result - 200;
                                processor.sfzPlayer.setMidiChannel (ch);
                                if (onPresetChannelAssigned && ! presetList.empty())
                                {
                                    const int idx = juce::jlimit (0, (int) presetList.size() - 1,
                                                                  processor.sfzPlayer.getCurrentPresetIndex());
                                    onPresetChannelAssigned (presetList[(size_t) idx], ch);
                                }
                            }
                            else if (result == 300)
                            {
                                openSaveAsOverlay();
                            }
                        });
                    return;
                }
                // SF2: no Save As, fall through to nothing (grid handles its own right-click)
            }
            return;
        }

        // Right-click on any knob → MIDI Learn menu
        using F = DysektProcessor::SliceParamField;
        struct { juce::Rectangle<int>& zone; int fieldId; } knobFields[] =
        {
            { volZone,     F::FieldSfzVol        },
            { transZone,   F::FieldSfzTranspose   },
            { panZone,     F::FieldSfzPan          },
            { fineZone,    F::FieldSfzFineTune     },
            { rvMixZone,   F::FieldSfzReverbMix    },
            { rvSizeZone,  F::FieldSfzReverbSize   },
            { adsrAtkZone, F::FieldSfzAttack        },
            { adsrDecZone, F::FieldSfzDecay         },
            { adsrSusZone, F::FieldSfzSustain       },
            { adsrRelZone, F::FieldSfzRelease       },
        };
        for (auto& kf : knobFields)
        {
            if (kf.zone.contains (pos))
            {
                showMidiLearnMenu (kf.fieldId, e.getScreenPosition());
                return;
            }
        }
        return;
    }

    // ── Preset arrows ─────────────────────────────────────────────────────────
    if (! browserOpen)
    {
        if (presetDecBtn.contains (pos)) { selectPreset (-1); return; }
        if (presetIncBtn.contains (pos)) { selectPreset (+1); return; }
    }

    // ── Knob drag start ───────────────────────────────────────────────────────
    {
        const int selCh = selectedSf2Ch >= 0 ? selectedSf2Ch + 1 : 0;
        struct { juce::Rectangle<int>& zone; ActiveKnob id; float val; } knobs[] =
        {
            { volZone,     ActiveKnob::Volume,      volToNorm   (processor.sfzPlayer.getVolume()) },
            { transZone,   ActiveKnob::Transpose,   transToNorm (processor.sfzPlayer.getTranspose()) },
            { panZone,     ActiveKnob::Pan,         panToNorm   (processor.sfzPlayer.getPan()) },
            { fineZone,    ActiveKnob::FineTune,    fineToNorm  (processor.sfzPlayer.getFineTune()) },
            { rvMixZone,   ActiveKnob::ReverbMix,   processor.sfzPlayer.getReverbMix()  / 100.0f },
            { rvSizeZone,  ActiveKnob::ReverbSize,  processor.sfzPlayer.getReverbSize() / 100.0f },
            { adsrAtkZone, ActiveKnob::AdsrAttack,  juce::jlimit (0.f, 1.f, processor.sfzPlayer.getSfzAttack()  / 30.0f) },
            { adsrDecZone, ActiveKnob::AdsrDecay,   juce::jlimit (0.f, 1.f, processor.sfzPlayer.getSfzDecay()   / 30.0f) },
            { adsrSusZone, ActiveKnob::AdsrSustain, juce::jlimit (0.f, 1.f, processor.sfzPlayer.getSfzSustain() / 100.0f) },
            { adsrRelZone, ActiveKnob::AdsrRelease, juce::jlimit (0.f, 1.f, processor.sfzPlayer.getSfzRelease() / 60.0f) },
            // Per-channel SF2 FX knobs (only active when a ch is selected)
            { chMixZone,  ActiveKnob::ChReverbMix,  selCh > 0 ? processor.sfzPlayer.getReverbMix() / 100.0f : 0.f },
            { chSizeZone, ActiveKnob::ChReverbSize, selCh > 0 ? processor.sfzPlayer.getReverbSize() / 100.0f : 0.f },
            { chDampZone, ActiveKnob::ChReverbDamp, selCh > 0 ? processor.sfzPlayer.getReverbDamp() / 100.0f : 0.f },
            { chGainZone, ActiveKnob::ChGain,       selCh > 0 ? juce::jlimit (0.f, 1.f, processor.sfzPlayer.getVolume() / 2.0f) : 0.5f },
        };

        for (auto& k : knobs)
        {
            if (k.zone.contains (pos))
            {
                activeKnob   = k.id;
                dragStartY   = pos.y;
                dragStartVal = k.val;
                return;
            }
        }
    }
}

void SfzDropdownPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (activeKnob == ActiveKnob::None) return;
    const float delta   = (float)(dragStartY - e.getPosition().y) / 120.0f;
    const float newNorm = juce::jlimit (0.f, 1.f, dragStartVal + delta);

    switch (activeKnob)
    {
        case ActiveKnob::Volume:      processor.sfzPlayer.setVolume    (normToVol   (newNorm)); break;
        case ActiveKnob::Transpose:   processor.sfzPlayer.setTranspose (normToTrans (newNorm)); break;
        case ActiveKnob::Pan:         processor.sfzPlayer.setPan       (normToPan   (newNorm)); break;
        case ActiveKnob::FineTune:    processor.sfzPlayer.setFineTune  (normToFine  (newNorm)); break;
        case ActiveKnob::ReverbMix:   processor.sfzPlayer.setReverbMix  (newNorm * 100.0f);     break;
        case ActiveKnob::ReverbSize:  processor.sfzPlayer.setReverbSize (newNorm * 100.0f);     break;
        case ActiveKnob::AdsrAttack:  processor.sfzPlayer.setSfzAttack  (newNorm * 30.0f);      break;
        case ActiveKnob::AdsrDecay:   processor.sfzPlayer.setSfzDecay   (newNorm * 30.0f);      break;
        case ActiveKnob::AdsrSustain: processor.sfzPlayer.setSfzSustain (newNorm * 100.0f);     break;
        case ActiveKnob::AdsrRelease: processor.sfzPlayer.setSfzRelease (newNorm * 60.0f);      break;
        case ActiveKnob::ChReverbMix:
        case ActiveKnob::ChReverbSize:
        case ActiveKnob::ChReverbDamp:
        case ActiveKnob::ChGain:
        {
            const int selCh = selectedSf2Ch >= 0 ? selectedSf2Ch + 1 : 0;
            if (selCh > 0)
            {
                if      (activeKnob == ActiveKnob::ChReverbMix)  processor.sfzPlayer.setReverbMix  (newNorm * 100.0f);
                else if (activeKnob == ActiveKnob::ChReverbSize) processor.sfzPlayer.setReverbSize (newNorm * 100.0f);
                else if (activeKnob == ActiveKnob::ChReverbDamp) processor.sfzPlayer.setReverbDamp (newNorm * 100.0f);
                else if (activeKnob == ActiveKnob::ChGain)       processor.sfzPlayer.setVolume     (newNorm * 2.0f);
            }
            break;
        }
        default: break;
    }
    repaint();
}

void SfzDropdownPanel::mouseUp (const juce::MouseEvent&)
{
    activeKnob = ActiveKnob::None;
}

void SfzDropdownPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (volZone.contains    (pos)) { processor.sfzPlayer.setVolume    (1.0f);  repaint(); }
    if (transZone.contains  (pos)) { processor.sfzPlayer.setTranspose (0);     repaint(); }
    if (panZone.contains    (pos)) { processor.sfzPlayer.setPan       (0.0f);  repaint(); }
    if (fineZone.contains   (pos)) { processor.sfzPlayer.setFineTune  (0.0f);  repaint(); }
    if (rvMixZone.contains  (pos)) { processor.sfzPlayer.setReverbMix  (0.0f);  repaint(); }
    if (rvSizeZone.contains (pos)) { processor.sfzPlayer.setReverbSize (50.0f);  repaint(); }
    // ADSR defaults
    if (adsrAtkZone.contains (pos)) { processor.sfzPlayer.setSfzAttack  (0.005f);  repaint(); }
    if (adsrDecZone.contains (pos)) { processor.sfzPlayer.setSfzDecay   (0.1f);    repaint(); }
    if (adsrSusZone.contains (pos)) { processor.sfzPlayer.setSfzSustain (100.0f);  repaint(); }
    if (adsrRelZone.contains (pos)) { processor.sfzPlayer.setSfzRelease (0.05f);   repaint(); }
    // Ch-FX defaults
    {
        const int selCh = selectedSf2Ch >= 0 ? selectedSf2Ch + 1 : 0;
        if (selCh > 0)
        {
            if (chMixZone.contains  (pos)) { processor.sfzPlayer.setReverbMix  (0.0f);   repaint(); }
            if (chSizeZone.contains (pos)) { processor.sfzPlayer.setReverbSize (50.0f);  repaint(); }
            if (chDampZone.contains (pos)) { processor.sfzPlayer.setReverbDamp (50.0f);  repaint(); }
            if (chGainZone.contains (pos)) { processor.sfzPlayer.setVolume     (1.0f);   repaint(); }
        }
    }
}

void SfzDropdownPanel::mouseWheelMove (const juce::MouseEvent& e,
                                        const juce::MouseWheelDetails& w)
{
    if (browserOpen || programPickerOpen) return;

    const auto  pos  = e.getPosition();
    const float step = w.deltaY * (e.mods.isShiftDown() ? 0.01f : 0.05f);

    if (nameZone.contains (pos))
    {
        if (w.deltaY > 0.05f)       selectPreset (+1);
        else if (w.deltaY < -0.05f) selectPreset (-1);
        return;
    }

    auto adjustNorm = [&] (float current, float s) {
        return juce::jlimit (0.f, 1.f, current + s);
    };

    if (volZone.contains (pos))
        processor.sfzPlayer.setVolume (normToVol (adjustNorm (volToNorm (processor.sfzPlayer.getVolume()), step)));
    else if (transZone.contains (pos))
        processor.sfzPlayer.setTranspose (normToTrans (adjustNorm (transToNorm (processor.sfzPlayer.getTranspose()), step)));
    else if (panZone.contains (pos))
        processor.sfzPlayer.setPan (normToPan (adjustNorm (panToNorm (processor.sfzPlayer.getPan()), step)));
    else if (fineZone.contains (pos))
        processor.sfzPlayer.setFineTune (normToFine (adjustNorm (fineToNorm (processor.sfzPlayer.getFineTune()), step)));
    else if (rvMixZone.contains (pos))
        processor.sfzPlayer.setReverbMix  (juce::jlimit (0.0f, 100.0f, processor.sfzPlayer.getReverbMix()  + step * 100.0f));
    else if (rvSizeZone.contains (pos))
        processor.sfzPlayer.setReverbSize (juce::jlimit (0.0f, 100.0f, processor.sfzPlayer.getReverbSize() + step * 100.0f));
    else if (adsrAtkZone.contains (pos))
        processor.sfzPlayer.setSfzAttack  (juce::jlimit (0.f, 30.f,  adjustNorm (processor.sfzPlayer.getSfzAttack()  / 30.0f,  step) * 30.0f));
    else if (adsrDecZone.contains (pos))
        processor.sfzPlayer.setSfzDecay   (juce::jlimit (0.f, 30.f,  adjustNorm (processor.sfzPlayer.getSfzDecay()   / 30.0f,  step) * 30.0f));
    else if (adsrSusZone.contains (pos))
        processor.sfzPlayer.setSfzSustain (juce::jlimit (0.f, 100.f, adjustNorm (processor.sfzPlayer.getSfzSustain() / 100.0f, step) * 100.0f));
    else if (adsrRelZone.contains (pos))
        processor.sfzPlayer.setSfzRelease (juce::jlimit (0.f, 60.f,  adjustNorm (processor.sfzPlayer.getSfzRelease() / 60.0f,  step) * 60.0f));

    repaint();
}

// =============================================================================
//  File drag-and-drop
// =============================================================================

bool SfzDropdownPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".sf2" || ext == ".sfz")
            return true;
    }
    return false;
}

void SfzDropdownPanel::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".sf2" || ext == ".sfz")
        {
            juce::File file (f);
            processor.sfzPlayer.loadFile (file, processor.fileLoadPool);
            processor.sfPlayerChannelMask.store (0x1FFFEu, std::memory_order_relaxed); // omni
            reloadZones (file);
            closeBrowser();
            if (ext == ".sf2")
                openProgramGrid();
            else
                closeProgramGrid();
            repaint();
            return;
        }
    }
}

// =============================================================================
//  panelDidShow
// =============================================================================

void SfzDropdownPanel::panelDidShow()
{
    presetList = processor.sfzPlayer.getPresetList();

    // If program grid was left open, refresh it with latest data
    if (programPickerOpen)
    {
        programGrid.setPresets (presetList,
                                processor.sfzPlayer.getCurrentPresetIndex(),
                                processor.sfzPlayer.getMidiChannel());
        restoreGridChannelAssignments();
    }

    if (processor.sfzPlayer.isLoaded())
    {
        const auto f   = processor.sfzPlayer.getLoadedFile();
        const bool sf2 = f.getFileExtension().toLowerCase() == ".sf2";
        reloadZones (f);

        // For SF2: only open the program grid once the preset list is populated.
        // If presets are still empty (FluidSynth hasn't finished async loading),
        // skip openProgramGrid() here — the PluginEditor timer will call
        // panelDidShow() again once getPresetList() returns a non-empty list.
        if (sf2 && ! programPickerOpen)
        {
            if (! presetList.empty())
                openProgramGrid();
            // else: timer retry will open the grid when presets arrive
        }
    }
    else
        initEmptySfz();   // bootstrap so [+ ZONE] is available and zones show immediately
    resized();
    repaint();
}

// =============================================================================
//  initEmptySfz
// =============================================================================

void SfzDropdownPanel::initEmptySfz()
{
    // Create Custom.sfz in the user's Music folder if it doesn't exist yet.
    // (Never overwrites — if the file is already there from a previous session,
    //  we just reload it so the existing zones are restored.)
    auto sfz = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                   .getChildFile ("Custom.sfz");

    if (! sfz.existsAsFile())
        sfz.replaceWithText ("// Custom SFZ — built with SF-Player\n\n");

    processor.sfzPlayer.loadFile (sfz, processor.fileLoadPool);   // sfizz handles empty file gracefully (silence)
    processor.sfPlayerChannelMask.store (0x1FFFEu, std::memory_order_relaxed); // omni
    reloadZones (sfz);                    // sets [+ ZONE] button visible + wires callback
}

// =============================================================================
//  Value mapping
// =============================================================================

float SfzDropdownPanel::volToNorm   (float linear) const { return juce::jlimit (0.f, 1.f, linear * 0.5f); }
float SfzDropdownPanel::normToVol   (float n)       const { return n * 2.0f; }
float SfzDropdownPanel::transToNorm (int semi)       const { return ((float) semi + 24.0f) / 48.0f; }
int   SfzDropdownPanel::normToTrans (float n)        const { return juce::roundToInt (n * 48.0f - 24.0f); }
float SfzDropdownPanel::panToNorm   (float p)        const { return (p + 1.0f) * 0.5f; }
float SfzDropdownPanel::normToPan   (float n)        const { return n * 2.0f - 1.0f; }
float SfzDropdownPanel::fineToNorm  (float cents)    const { return (cents + 100.0f) / 200.0f; }
float SfzDropdownPanel::normToFine  (float n)        const { return n * 200.0f - 100.0f; }

// =============================================================================
//  Zone parsers
// =============================================================================

static juce::Colour zoneColourDP (int index)
{
    static const juce::Colour palette[] = {
        juce::Colour (0xFF4FC3F7), juce::Colour (0xFF81C784), juce::Colour (0xFFFFB74D),
        juce::Colour (0xFFE57373), juce::Colour (0xFFBA68C8), juce::Colour (0xFF4DD0E1),
        juce::Colour (0xFFF06292), juce::Colour (0xFFA1887F),
    };
    return palette[index % 8];
}

std::vector<KeysPanel::Keyzone> SfzDropdownPanel::parseSfzZones (const juce::File& f)
{
    std::vector<KeysPanel::Keyzone> zones;
    const auto lines = juce::StringArray::fromLines (f.loadFileAsString());

    int          loKey    = 0, hiKey = 127;
    bool         inRegion = false;
    int          colIdx   = 0;
    juce::String sampleName;

    auto flush = [&]
    {
        if (inRegion && hiKey >= loKey)
        {
            KeysPanel::Keyzone z;
            z.loKey    = loKey;
            z.hiKey    = hiKey;
            z.loVel    = 0;
            z.hiVel    = 127;
            z.rootPitch= -1;
            z.isLooped = false;
            z.isSfz    = true;   // SFZ zones are editable — must set explicitly (default is false)
            z.colour   = zoneColourDP (colIdx++);
            // Use the sample filename (without extension) as the zone name,
            // falling back to a generic "Zone N" label if none was found.
            z.name     = sampleName.isNotEmpty()
                       ? sampleName
                       : "Zone " + juce::String (colIdx);
            zones.push_back (z);
            loKey = 0; hiKey = 127; sampleName = {};
        }
        inRegion = false;
    };

    for (auto line : lines)
    {
        // Use the original (case-preserved) line for sample= value extraction,
        // since file paths may be case-sensitive.
        const auto lineLower = line.trim().toLowerCase();
        const auto lineOrig  = line.trim();

        if (lineLower.startsWith ("<region>")) { flush(); inRegion = true; loKey = 0; hiKey = 127; sampleName = {}; }
        else if (lineLower.startsWith ("<group>") || lineLower.startsWith ("<global>")) flush();

        if (inRegion)
        {
            auto loRaw = lineLower.indexOf ("lokey=");
            if (loRaw >= 0)
                loKey = juce::jlimit (0, 127,
                    lineLower.substring (loRaw + 6).upToFirstOccurrenceOf (" ", false, false).trim().getIntValue());
            auto hiRaw = lineLower.indexOf ("hikey=");
            if (hiRaw >= 0)
                hiKey = juce::jlimit (0, 127,
                    lineLower.substring (hiRaw + 6).upToFirstOccurrenceOf (" ", false, false).trim().getIntValue());
            auto kRaw = lineLower.indexOf ("key=");
            if (kRaw >= 0 && lineLower.indexOf ("lokey=") < 0)
            {
                const int k = juce::jlimit (0, 127,
                    lineLower.substring (kRaw + 4).upToFirstOccurrenceOf (" ", false, false).trim().getIntValue());
                loKey = hiKey = k;
            }
            // Extract sample= value — strip directory and extension to get bare name.
            auto sRaw = lineLower.indexOf ("sample=");
            if (sRaw >= 0 && sampleName.isEmpty())
            {
                auto rawPath = [&]() -> juce::String {
                    juce::String p = lineOrig.substring (sRaw + 7).trim()
                                             .upToFirstOccurrenceOf ("\t", false, false).trim();
                    // Strip trailing opcodes (word= tokens) to preserve paths with spaces.
                    for (;;) {
                        auto si = p.lastIndexOf (" ");
                        if (si < 0) break;
                        if (p.substring (si + 1).containsChar ('=')) p = p.substring (0, si).trim();
                        else break;
                    }
                    return p;
                }();
                // Handle both / and \ path separators.
                auto bare = rawPath.fromLastOccurrenceOf ("/",  false, false)
                                   .fromLastOccurrenceOf ("\\", false, false);
                // Strip file extension.
                if (bare.contains ("."))
                    bare = bare.upToLastOccurrenceOf (".", false, false);
                sampleName = bare.isNotEmpty() ? bare : rawPath;
            }
        }
    }
    flush();
    return zones;
}

std::vector<KeysPanel::Keyzone> SfzDropdownPanel::parseSf2Zones (const juce::File& f,
                                                                    int targetBank,
                                                                    int targetPreset)
{
    // ── Full SF2 RIFF parser ─────────────────────────────────────────────────
    // Reads phdr → pbag → pgen to resolve the selected preset's instrument,
    // then reads ibag → igen for only that instrument's zones.
    // The previous implementation read ALL igen records (every sample in the
    // file), which caused the zone matrix to show every preset's samples.
    std::vector<KeysPanel::Keyzone> zones;

    juce::FileInputStream stream (f);
    if (stream.failedToOpen()) return zones;

    char riff[4]; stream.read (riff, 4);
    if (juce::String::fromUTF8 (riff, 4) != "RIFF") return zones;
    stream.readInt();
    char sfbk[4]; stream.read (sfbk, 4);
    if (juce::String::fromUTF8 (sfbk, 4) != "sfbk") return zones;

    juce::MemoryBlock phdrData, pbagData, pgenData, instData, ibagData, igenData, shdrData;

    while (! stream.isExhausted())
    {
        char id[4]; if (stream.read (id, 4) < 4) break;
        const auto chunkId = juce::String::fromUTF8 (id, 4);
        const int  sz      = stream.readInt();
        if (chunkId == "LIST")
        {
            char listId[4]; stream.read (listId, 4);
            if (juce::String::fromUTF8 (listId, 4) == "pdta")
            {
                const int pdtaEnd = (int) stream.getPosition() + sz - 4;
                while (stream.getPosition() < pdtaEnd && ! stream.isExhausted())
                {
                    char sub[4]; if (stream.read (sub, 4) < 4) break;
                    const auto subId = juce::String::fromUTF8 (sub, 4);
                    const int  subSz = stream.readInt();
                    auto readChunk = [&] (juce::MemoryBlock& mb)
                    {
                        mb.setSize ((size_t) subSz);
                        stream.read (mb.getData(), subSz);
                    };
                    if      (subId == "phdr") readChunk (phdrData);
                    else if (subId == "pbag") readChunk (pbagData);
                    else if (subId == "pgen") readChunk (pgenData);
                    else if (subId == "inst") readChunk (instData);
                    else if (subId == "ibag") readChunk (ibagData);
                    else if (subId == "igen") readChunk (igenData);
                    else if (subId == "shdr") readChunk (shdrData);
                    else stream.skipNextBytes (subSz);
                }
                break;
            }
            else stream.skipNextBytes (sz - 4);
        }
        else stream.skipNextBytes (sz);
    }

    if (igenData.isEmpty() || phdrData.isEmpty() || pbagData.isEmpty()
        || pgenData.isEmpty() || instData.isEmpty() || ibagData.isEmpty())
        return zones;

    auto readU16 = [] (const juce::MemoryBlock& mb, size_t off) -> uint16_t
    {
        if (off + 1 >= mb.getSize()) return 0;
        const auto* d = static_cast<const uint8_t*> (mb.getData());
        return (uint16_t)(d[off] | (d[off + 1] << 8));
    };

    // ── Step 1: locate preset in phdr (38 bytes/record) ──────────────────────
    constexpr size_t kPhdrSz = 38;
    const size_t numPresets  = phdrData.getSize() / kPhdrSz;

    int presetBagStart = -1, presetBagEnd = -1;
    for (size_t pi = 0; pi + 1 < numPresets; ++pi)
    {
        const uint16_t pNum  = readU16 (phdrData, pi * kPhdrSz + 20);
        const uint16_t pBank = readU16 (phdrData, pi * kPhdrSz + 22);
        const uint16_t bagNdx= readU16 (phdrData, pi * kPhdrSz + 24);
        if ((int) pNum == targetPreset && (int) pBank == targetBank)
        {
            presetBagStart = (int) bagNdx;
            presetBagEnd   = (int) readU16 (phdrData, (pi + 1) * kPhdrSz + 24);
            break;
        }
    }
    // Fallback to first preset if not found
    if (presetBagStart < 0 && numPresets > 1)
    {
        presetBagStart = (int) readU16 (phdrData, 24);
        presetBagEnd   = (int) readU16 (phdrData, kPhdrSz + 24);
    }
    if (presetBagStart < 0) return zones;

    // ── Step 2: pbag → pgen to find instrument index (oper=41) ──────────────
    constexpr size_t kPbagSz = 4, kPgenSz = 4;
    int instrumentIndex = -1;

    for (int bi = presetBagStart; bi < presetBagEnd && instrumentIndex < 0; ++bi)
    {
        const size_t bagOff = (size_t) bi * kPbagSz;
        if (bagOff + 2 > pbagData.getSize()) break;
        const int genStart = (int) readU16 (pbagData, bagOff);
        const int genEnd   = (bi + 1 < (int)(pbagData.getSize() / kPbagSz))
                             ? (int) readU16 (pbagData, (size_t)(bi + 1) * kPbagSz)
                             : (int)(pgenData.getSize() / kPgenSz);
        for (int gi = genStart; gi < genEnd; ++gi)
        {
            const size_t gOff = (size_t) gi * kPgenSz;
            if (gOff + 4 > pgenData.getSize()) break;
            if (readU16 (pgenData, gOff) == 41)  // instrument generator
            {
                instrumentIndex = (int) readU16 (pgenData, gOff + 2);
                break;
            }
        }
    }
    if (instrumentIndex < 0) return zones;

    // ── Step 3: find igen range via inst → ibag ───────────────────────────────
    // inst record: 20-char name + uint16 wInstBagNdx = 22 bytes
    // ibag record: uint16 wInstGenNdx, uint16 wInstModNdx = 4 bytes
    constexpr size_t kInstSz = 22;
    constexpr size_t kIbagSz = 4;

    const size_t numInsts = instData.getSize() / kInstSz;
    if ((size_t) instrumentIndex + 1 >= numInsts) return zones;

    const int ibagStart = (int) readU16 (instData, (size_t) instrumentIndex * kInstSz + 20);
    const int ibagEnd   = (int) readU16 (instData, (size_t)(instrumentIndex + 1) * kInstSz + 20);

    const size_t numIbags = ibagData.getSize() / kIbagSz;
    if ((size_t) ibagStart >= numIbags || ibagEnd < ibagStart) return zones;

    const int igenStart = (int) readU16 (ibagData, (size_t) ibagStart * kIbagSz);
    const int igenEnd   = ((size_t) ibagEnd < numIbags)
                          ? (int) readU16 (ibagData, (size_t) ibagEnd * kIbagSz)
                          : (int)(igenData.getSize() / 4);

    // ── Step 4: sample name lookup from shdr (46 bytes/record) ───────────────
    std::vector<juce::String> sampleNames;
    if (! shdrData.isEmpty())
    {
        constexpr size_t kShdrSz = 46;
        const size_t numSamples  = shdrData.getSize() / kShdrSz;
        const auto*  shdrRaw     = static_cast<const char*> (shdrData.getData());
        sampleNames.reserve (numSamples);
        for (size_t s = 0; s < numSamples; ++s)
            sampleNames.push_back (juce::String::fromUTF8 (shdrRaw + s * kShdrSz, 20).trimEnd());
    }

    // ── Step 5: parse only this instrument's igen records ────────────────────
    const auto* igenRaw = static_cast<const uint8_t*> (igenData.getData());

    struct ZoneCandidate { int lo{0}, hi{127}, sampleId{-1}, root{-1}; bool hasRange{false}; };
    std::vector<ZoneCandidate> candidates;
    ZoneCandidate cur;

    auto flushCandidate = [&]
    {
        if (cur.hasRange && cur.hi >= cur.lo)
            candidates.push_back (cur);
        cur = {};
    };

    for (int i = igenStart; i < igenEnd; ++i)
    {
        const size_t   off  = (size_t) i * 4;
        if (off + 4 > igenData.getSize()) break;
        const uint16_t oper   = (uint16_t)(igenRaw[off] | (igenRaw[off+1] << 8));
        const uint8_t  lo     = igenRaw[off+2];
        const uint8_t  hi     = igenRaw[off+3];
        const uint16_t amount = (uint16_t)(igenRaw[off+2] | (igenRaw[off+3] << 8));

        if (oper == 43)                    { flushCandidate(); cur.lo = lo; cur.hi = hi; cur.hasRange = true; }
        else if (oper == 58)               { cur.root = juce::jlimit (0, 127, (int) lo); }
        else if (oper == 53)               { cur.sampleId = (int) amount; flushCandidate(); }
        else if (oper == 0 && cur.hasRange){ flushCandidate(); }  // zone boundary
    }
    flushCandidate();

    // Build final zone list, de-duplicating by (lo,hi)
    int colIdx = 0;
    std::set<std::pair<int,int>> seen;

    for (auto& c : candidates)
    {
        auto key = std::make_pair (c.lo, c.hi);
        if (seen.find (key) != seen.end()) continue;
        seen.insert (key);

        KeysPanel::Keyzone z;
        z.loKey     = c.lo;
        z.hiKey     = c.hi;
        z.rootPitch = c.root;
        z.loVel     = 0;
        z.hiVel     = 127;
        z.isLooped  = false;
        z.colour    = zoneColourDP (colIdx);

        if (c.sampleId >= 0 && c.sampleId < (int) sampleNames.size()
            && sampleNames[(size_t) c.sampleId] != "EOS"
            && sampleNames[(size_t) c.sampleId].isNotEmpty())
            z.name = sampleNames[(size_t) c.sampleId];
        else
            z.name = "Zone " + juce::String (colIdx + 1);

        zones.push_back (z);
        ++colIdx;
    }

    std::sort (zones.begin(), zones.end(), [] (auto& a, auto& b) { return a.loKey < b.loKey; });
    for (size_t i = 0; i < zones.size(); ++i)
        zones[i].colour = zoneColourDP ((int) i);

    return zones;
}

void SfzDropdownPanel::reloadZones (const juce::File& f)
{
    const auto ext   = f.getFileExtension().toLowerCase();
    const bool isSfz = (ext == ".sfz");
    const bool isSf2 = (ext == ".sf2");

    if (isSf2)
    {
        // SF2: keysPanel is hidden for SF2 files — Sf2ProgramGrid handles everything.
        // Nothing to do here.
        return;
    }
    else
    {
        // ── SFZ (or nothing): editable zone matrix ───────────────────────────
        std::vector<KeysPanel::Keyzone> zones;
        if (isSfz)
            zones = parseSfzZones (f);

        keysPanel.setSf2PresetListMode (false);
        keysPanel.setSfzEditable (isSfz);

        // [+ ZONE] button visibility must be set BEFORE setKeyzones() so that
        // rebuild() sizes the component correctly (it reads addZoneBtnVisible to
        // decide whether to add an extra row).  Setting it after setKeyzones()
        // means rebuild() runs with the wrong value and the component is too short
        // to display the button even though repaint() draws it.
        keysPanel.setAddZoneButtonVisible (isSfz);
        if (isSfz)
            keysPanel.onAddZoneRequested = [this] { openAddZoneChooser(); };
        else
            keysPanel.onAddZoneRequested = nullptr;

        keysPanel.onRowClicked      = nullptr;
        keysPanel.onRowRightClicked = nullptr;

        keysPanel.setKeyzones (zones);

        if (! zones.empty())
            keysPanel.autoScrollToZones();

        // Wire the edit callback — only fires for SFZ (sfzEditable == true)
        keysPanel.onZoneEdited = [this, f] (int rowIndex, const KeysPanel::Keyzone& updated)
        {
            writeSfzZoneChange (f, rowIndex, updated);
        };
    }
}

// =============================================================================
//  writeSfzZoneChange  —  patch one <region> block in the SFZ text file
// =============================================================================

// Helper: set or replace an opcode value within a region line-block.
// 'lines' is the full file split by line. 'regionStart' is the line index of
// the <region> header. We search forward (until the next <region>/<group> or
// EOF) for the opcode and replace it, or append it to the <region> line.
static void setOpcode (juce::StringArray& lines, int regionStart,
                       const juce::String& opcode, const juce::String& value)
{
    const juce::String target = opcode + "=";

    // Search within this region's block
    for (int i = regionStart; i < lines.size(); ++i)
    {
        const auto lower = lines[i].toLowerCase().trim();
        if (i > regionStart && (lower.startsWith ("<region>") ||
                                lower.startsWith ("<group>") ||
                                lower.startsWith ("<global>")))
            break;  // reached next block — opcode not found, append

        const int pos = lines[i].toLowerCase().indexOf (target);
        if (pos >= 0)
        {
            // Replace the value in-place, preserving surrounding tokens
            // Find end of the value token (next space or end of string)
            const int valStart = pos + target.length();
            const auto rest = lines[i].substring (valStart);
            const int valEnd = rest.indexOfChar (' ');
            const juce::String newLine = lines[i].substring (0, valStart)
                                        + value
                                        + (valEnd >= 0 ? rest.substring (valEnd) : "");
            lines.set (i, newLine);
            return;
        }
    }

    // Opcode not present — append it to the <region> header line
    lines.set (regionStart, lines[regionStart].trimEnd() + " " + target + value);
}

void SfzDropdownPanel::writeSfzZoneChange (const juce::File& f,
                                            int rowIndex,
                                            const KeysPanel::Keyzone& z)
{
    if (! f.existsAsFile()) return;

    auto lines = juce::StringArray::fromLines (f.loadFileAsString());

    // Find the Nth <region> block (rowIndex is 0-based count of parsed regions)
    int regionCount = -1;
    int regionLine  = -1;

    for (int i = 0; i < lines.size(); ++i)
    {
        if (lines[i].trim().toLowerCase().startsWith ("<region>"))
        {
            ++regionCount;
            if (regionCount == rowIndex)
            {
                regionLine = i;
                break;
            }
        }
    }

    if (regionLine < 0) return;  // region not found — bail

    // Patch each editable opcode
    auto noteStr = [] (int note) -> juce::String
    {
        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        return juce::String (names[note % 12]) + juce::String (note / 12 - 1);
    };

    setOpcode (lines, regionLine, "lokey",  noteStr (z.loKey));
    setOpcode (lines, regionLine, "hikey",  noteStr (z.hiKey));
    setOpcode (lines, regionLine, "lovel",  juce::String (z.loVel));
    setOpcode (lines, regionLine, "hivel",  juce::String (z.hiVel));

    if (z.rootPitch >= 0)
        setOpcode (lines, regionLine, "pitch_keycenter", noteStr (z.rootPitch));

    // Write extended fields (only for SFZ zones)
    setOpcode (lines, regionLine, "tune",         juce::String ((int) z.tuneCents));
    setOpcode (lines, regionLine, "pan",          juce::String (juce::roundToInt (z.pan * 100.f)));
    setOpcode (lines, regionLine, "volume",       juce::String (z.volDb, 2));
    setOpcode (lines, regionLine, "ampeg_release",juce::String (z.releaseSec, 3));

    if (z.isLooped)
        setOpcode (lines, regionLine, "loop_mode", "loop_continuous");
    else
        setOpcode (lines, regionLine, "loop_mode", "no_loop");

    // Write back — join with \n (preserve original line endings best-effort)
    const bool crlf = f.loadFileAsString().contains ("\r\n");
    const auto newContent = lines.joinIntoString (crlf ? "\r\n" : "\n");
    f.replaceWithText (newContent);

    // Hot-reload the SFZ player so changes take effect immediately
    processor.sfzPlayer.loadFile (f, processor.fileLoadPool);
    processor.sfPlayerChannelMask.store (0x1FFFEu, std::memory_order_relaxed); // omni
}

// =============================================================================
//  openAddZoneChooser  —  Issue 2: Add Zone support
// =============================================================================

void SfzDropdownPanel::openAddZoneChooser()
{
    // Resolve the target SFZ (may be empty if nothing is loaded yet).
    juce::File targetSfz;
    if (processor.sfzPlayer.isLoaded())
    {
        const auto loaded = processor.sfzPlayer.getLoadedFile();
        if (loaded.getFileExtension().toLowerCase() == ".sfz")
            targetSfz = loaded;
    }

    int prevHiKey = -1;
    if (targetSfz.existsAsFile())
    {
        const auto existing = parseSfzZones (targetSfz);
        for (const auto& z : existing)
            prevHiKey = juce::jmax (prevHiKey, z.hiKey);
    }

    // Store for use in onFileChosen. targetSfz may be empty here; if so,
    // onFileChosen will trigger "Save As" after the sample is picked.
    addZoneTargetSfz = targetSfz;
    addZonePrevHiKey = prevHiKey;

    // Open the sample browser first — pick the sample, then name the SFZ.
    fileBrowser.setMode (SfzFileBrowser::Mode::kAddZone);
    const auto browserRoot = targetSfz.existsAsFile()
                           ? targetSfz.getParentDirectory()
                           : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    fileBrowser.setRootDirectory (browserRoot);
    openBrowser();
}

void SfzDropdownPanel::showAddZoneOverlay (const juce::File& sfzFile,
                                            const juce::File& sampleFile,
                                            int               prevHiKey)
{
    const int defaultLo = (prevHiKey < 0) ? 0 : juce::jmin (prevHiKey + 1, 127);

    auto overlay = std::make_unique<AddZoneOverlay> (
        sampleFile.getFileNameWithoutExtension(), defaultLo);

    overlay->onResult = [this, sfzFile, sampleFile] (int lo, int hi, int root, bool confirmed)
    {
        // Defer hideOverlays() so it runs after fire() has returned and
        // AddZoneOverlay is no longer on the call stack (use-after-free fix).
        juce::MessageManager::callAsync ([this] { hideOverlays(); });

        if (! confirmed)
            return;

        if (! appendZoneToSfz (sfzFile, sampleFile, lo, hi, root))
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Add Zone Failed",
                "Could not write to:\n" + sfzFile.getFullPathName());
            return;
        }

        processor.sfzPlayer.loadFile (sfzFile, processor.fileLoadPool);
        processor.sfPlayerChannelMask.store (0x1FFFEu, std::memory_order_relaxed); // omni
        reloadZones (sfzFile);
        keysPanel.autoScrollToZones();
        repaint();
    };

    showOverlay (addZoneOverlay, std::move (overlay));
}

bool SfzDropdownPanel::appendZoneToSfz (const juce::File& sfzFile,
                                          const juce::File& sampleFile,
                                          int loKey, int hiKey, int rootKey)
{
    juce::String samplePath;
    const auto sfzDir = sfzFile.getParentDirectory();
    if (sampleFile.isAChildOf (sfzDir))
        samplePath = sampleFile.getRelativePathFrom (sfzDir).replaceCharacter ('\\', '/');
    else
        samplePath = sampleFile.getFullPathName().replaceCharacter ('\\', '/');

    const juce::String region =
        "\n<region>\n"
        "sample="          + samplePath              + "\n"
        "lokey="           + juce::String (loKey)    + "\n"
        "hikey="           + juce::String (hiKey)    + "\n"
        "pitch_keycenter=" + juce::String (rootKey)  + "\n"
        "volume=-7\n"
        "pan=0\n"
        "tune=0\n"
        "ampeg_release=0.664\n";

    juce::FileOutputStream stream (sfzFile);
    if (stream.failedToOpen())
        return false;

    stream.setPosition (sfzFile.getSize());
    stream.writeText (region, false, false, nullptr);
    stream.flush();
    return ! stream.getStatus().failed();
}

// Called after the user has already picked a sample but no SFZ is loaded yet.
// Shows "Name your SFZ file", creates a blank file, then proceeds to AddZoneOverlay.
void SfzDropdownPanel::openSaveAsNewForZone (const juce::File& sampleFile)
{
    const auto defaultPath = sampleFile.getParentDirectory()
                                 .getChildFile ("Custom.sfz");
    auto overlay = std::make_unique<SaveSfzOverlay> (defaultPath);

    overlay->onResult = [this, sampleFile] (const juce::File& dest, bool confirmed)
    {
        juce::MessageManager::callAsync ([this] { hideOverlays(); });

        if (! confirmed || dest == juce::File{})
            return;

        // Always create a fresh blank SFZ.
        dest.replaceWithText ("// Custom SFZ — built with SF-Player\n\n");

        addZoneTargetSfz = dest;

        processor.sfzPlayer.loadFile (dest, processor.fileLoadPool);
        processor.sfPlayerChannelMask.store (0x1FFFEu, std::memory_order_relaxed); // omni
        reloadZones (dest);
        repaint();

        // Now show the key-range dialog with the already-chosen sample.
        juce::MessageManager::callAsync ([this, sampleFile]
        {
            showAddZoneOverlay (addZoneTargetSfz, sampleFile, addZonePrevHiKey);
        });
    };

    showOverlay (saveSfzOverlay, std::move (overlay));
}

void SfzDropdownPanel::openSaveAsOverlay()
{
    const auto currentFile = processor.sfzPlayer.isLoaded()
                           ? processor.sfzPlayer.getLoadedFile()
                           : juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                 .getChildFile ("Custom.sfz");

    auto overlay = std::make_unique<SaveSfzOverlay> (currentFile);

    overlay->onResult = [this, currentFile] (const juce::File& dest, bool confirmed)
    {
        // Defer hideOverlays() so it runs after fire() has returned and
        // SaveSfzOverlay is no longer on the call stack (use-after-free fix).
        juce::MessageManager::callAsync ([this] { hideOverlays(); });

        if (! confirmed || dest == juce::File{})
            return;

        if (currentFile.existsAsFile())
        {
            // Copy existing SFZ content to the new location.
            const bool ok = currentFile.copyFileTo (dest);
            if (! ok)
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    "Save Failed",
                    "Could not write:\n" + dest.getFullPathName());
                return;
            }
        }
        else
        {
            dest.replaceWithText ("// Custom SFZ — built with SF-Player\n\n");
        }

        processor.sfzPlayer.loadFile (dest, processor.fileLoadPool);
        reloadZones (dest);
        repaint();
    };

    showOverlay (saveSfzOverlay, std::move (overlay));
}

void SfzDropdownPanel::hideOverlays()
{
    if (addZoneOverlay)
    {
        if (auto* p = addZoneOverlay->getParentComponent())
            p->removeChildComponent (addZoneOverlay.get());
        addZoneOverlay.reset();
    }
    if (saveSfzOverlay)
    {
        if (auto* p = saveSfzOverlay->getParentComponent())
            p->removeChildComponent (saveSfzOverlay.get());
        saveSfzOverlay.reset();
    }
}
