// =============================================================================
//  SfzPlayerDropdownPanel.cpp  —  SF2 / SFZ instrument strip with inline file browser
// =============================================================================
#include "SfzPlayerDropdownPanel.h"
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

// ── Themed folder glyph ───────────────────────────────────────────────────────
// Colour-emoji glyphs (like U+1F4C1) render from their own embedded colour
// palette in most font backends, so Graphics::setColour() has no effect on
// them — that's why the folder icon was stuck yellow regardless of theme.
// This draws a simple vector folder outline that actually respects theme.accent.
static void drawFolderGlyph (juce::Graphics& g, juce::Rectangle<float> box, juce::Colour colour)
{
    const float tabW  = box.getWidth()  * 0.45f;
    const float tabH  = box.getHeight() * 0.30f;
    const float bodyY = box.getY() + tabH;

    juce::Path p;
    p.startNewSubPath (box.getX(),                      box.getBottom());
    p.lineTo           (box.getX(),                      bodyY);
    p.lineTo           (box.getX() + tabW,               bodyY);
    p.lineTo           (box.getX() + tabW + tabH * 0.8f, box.getY());
    p.lineTo           (box.getRight(),                  box.getY());
    p.lineTo           (box.getRight(),                  box.getBottom());
    p.closeSubPath();

    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.3f));
}

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

    // Self-computed scale factor — same pattern as SliceLcdDisplay/DualLcdControlFrame.
    // Derived from the breadcrumb bar's actual height vs. its design height, so it
    // tracks whatever resized() decided (which itself tracks overall UI scale).
    const float sf = breadcrumbZone.getHeight() > 0
                        ? (float) breadcrumbZone.getHeight() / (float) kBreadcrumbH
                        : 1.0f;

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
        g.setFont (DysektLookAndFeel::makeFont (14.0f * sf));
        g.setColour (canGoUp ? theme.accent.withAlpha (0.90f)
                             : theme.accent.withAlpha (0.30f));
        g.drawText (u8"\u2190", upBtnZone, juce::Justification::centred, false);
    }

    // Current path text — shows "Drives" label when in the virtual root view
    {
        const auto pathArea = breadcrumbZone.withTrimmedLeft (upBtnZone.getWidth() + 4)
                                            .withTrimmedRight (4);
        g.setFont (DysektLookAndFeel::makeFont (12.0f * sf));
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
    // Self-computed scale factor — same pattern as SliceLcdDisplay/DualLcdControlFrame.
    // This browser fills the width of its parent module panel, which itself is laid
    // out proportional to overall UI/window scale, so getWidth() tracks that scale.
    const float sf = juce::jlimit (0.75f, 2.5f, (float) getWidth() / 1114.0f);

    const int breadcrumbH = juce::roundToInt (kBreadcrumbH * sf);
    const int rowH        = juce::roundToInt (kRowH        * sf);
    const int upW         = juce::roundToInt (24.0f        * sf);

    breadcrumbZone = { 0, 0, getWidth(), breadcrumbH };
    upBtnZone      = { 0, 1, upW, breadcrumbH - 2 };

    list.setRowHeight (rowH);
    list.setBounds (0, breadcrumbH + 1, getWidth(), getHeight() - breadcrumbH - 1);
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
                            ? "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3"
                            : (mode == Mode::kSf2)
                              ? "*.sf2"
                              : "*.sfz";   // SFZ-Player: SFZ files only

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

    // Self-computed scale factor — same pattern as SliceLcdDisplay/DualLcdControlFrame.
    // Derived straight from the actual row height JUCE gives us, so it always matches
    // whatever resized() set via list.setRowHeight(), with no shared state needed.
    const float sf = (float) h / (float) kRowH;

    if (selected)
    {
        g.setColour (theme.accent.withAlpha (0.14f));
        g.fillAll();
    }

    const int   iconColW = juce::roundToInt (22.0f * sf);
    const float textSize = 13.0f * sf;

    if (isDir)
    {
        // Vector folder glyph — replaces the color-emoji icon, which ignored
        // setColour() and was stuck yellow regardless of theme.
        auto iconBox = juce::Rectangle<float> (3.0f * sf, (float) h * 0.28f,
                                                (float) iconColW - 6.0f * sf, (float) h * 0.46f);
        drawFolderGlyph (g, iconBox, theme.accent.withAlpha (0.75f));

        g.setFont (DysektLookAndFeel::makeFont (textSize));
        g.setColour (selected ? theme.accent : theme.foreground.withAlpha (0.80f));
        g.drawText (f.getFileName(), iconColW + 6, 0, w - iconColW - 10, h,
                    juce::Justification::centredLeft, true);
    }
    else
    {
        // Extension badge (only for files with a known extension)
        const auto ext = f.getFileExtension().toUpperCase().trimCharactersAtStart (".");
        if (ext.isEmpty())
        {
            g.setFont (DysektLookAndFeel::makeFont (textSize));
            g.setColour (selected ? theme.accent : theme.foreground.withAlpha (0.80f));
            g.drawText (f.getFileName(), juce::roundToInt (6 * sf), 0,
                        w - juce::roundToInt (10 * sf), h,
                        juce::Justification::centredLeft, true);
        }
        else
        {
            const int  badgeW    = juce::roundToInt (36 * sf);
            const int  badgeH    = juce::roundToInt (14 * sf);
            const auto badgeRect = juce::Rectangle<int> (w - badgeW - juce::roundToInt (4 * sf),
                                                          (h - badgeH) / 2, badgeW, badgeH);
            g.setColour (theme.accent.withAlpha (0.18f));
            g.fillRoundedRectangle (badgeRect.toFloat(), 2.0f * sf);
            g.setFont (DysektLookAndFeel::makeFont (10.0f * sf));
            g.setColour (theme.accent.withAlpha (0.80f));
            g.drawText (ext, badgeRect, juce::Justification::centred, false);

            // Filename
            g.setFont (DysektLookAndFeel::makeFont (textSize));
            g.setColour (selected ? theme.accent : theme.foreground.withAlpha (0.80f));
            g.drawText (f.getFileNameWithoutExtension(), juce::roundToInt (6 * sf), 0,
                        w - badgeW - juce::roundToInt (14 * sf), h,
                        juce::Justification::centredLeft, true);
        }
    }
}

void SfzFileBrowser::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < rows.size() && ! rows[row].isDirectory() && onFileSingleClicked)
        onFileSingleClicked (rows[row]);

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
//  SfzPlayerDropdownPanel — constructor / destructor
// =============================================================================

SfzPlayerDropdownPanel::SfzPlayerDropdownPanel (DysektProcessor& p)
    : processor (p),
      keysPanel (p)
{
    // This strip's keyboard previews/highlights sfzPlayer2's notes (this panel
    // IS the SFZ-Player, ch2 default), not the legacy SF-Player's — must be
    // set before any note requests are made.
    keysPanel.setEngineSource (KeysPanel::EngineSource::SfzPlayer2);
    addChildComponent (keysPanel);

    // ── Inline file browser ───────────────────────────────────────────────────
    fileBrowser.onFileChosen = [this] (const juce::File& f) { onFileChosen (f); };
    fileBrowser.onDismiss = [this]
    {
        fileBrowser.setMode (SfzFileBrowser::Mode::kSfz);
        closeBrowser();
    };
    addChildComponent (fileBrowser);

    // [+ ZONE] always visible — openAddZoneChooser() creates a Custom.sfz if nothing is loaded
    keysPanel.setAddZoneButtonVisible (true);
    keysPanel.onAddZoneRequested = [this] { openAddZoneChooser(); };

    startTimerHz (30);
}

SfzPlayerDropdownPanel::~SfzPlayerDropdownPanel() = default;

// =============================================================================
//  Layout
// =============================================================================

void SfzPlayerDropdownPanel::resized()
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
    keysPanel.setVisible (kbH > 0 && ! browserOpen);
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

    // ── Channel-range spinner hit-zones (unused, retained for layout calc) ────
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

void SfzPlayerDropdownPanel::openBrowser()
{
    if (browserOpen) return;
    browserOpen = true;

    if (processor.sfzPlayer2.isLoaded())
    {
        // Navigate to the directory of the currently loaded file
        fileBrowser.setRootDirectory (
            processor.sfzPlayer2.getLoadedFile().getParentDirectory());
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

void SfzPlayerDropdownPanel::closeBrowser()
{
    if (! browserOpen) return;
    browserOpen = false;
    resized();
    repaint();
}

void SfzPlayerDropdownPanel::onFileChosen (const juce::File& f)
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

    processor.sfzPlayer2.loadFile (f, processor.fileLoadPool);
    processor.sfzPlayer2ChannelMask.store (1u << 2, std::memory_order_relaxed); // ch2 default
    reloadZones (f);
    closeBrowser();
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

void SfzPlayerDropdownPanel::paint (juce::Graphics& g)
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

    drawAdsrStrip (g);

    g.setColour (theme.accent.withAlpha (0.45f));
    g.fillRect (0, 0, w, 1);
}

// =============================================================================
//  drawAdsrStrip
// =============================================================================

void SfzPlayerDropdownPanel::drawAdsrStrip (juce::Graphics& g) const
{
    // Attack: 0-30 s, normalised
    drawKnob (g, adsrAtkZone,
              juce::jlimit (0.f, 1.f, processor.sfzPlayer2.getSfzAttack()  / 30.0f),
              "ATK",
              juce::String (processor.sfzPlayer2.getSfzAttack(), 2) + "s");

    // Decay: 0-30 s
    drawKnob (g, adsrDecZone,
              juce::jlimit (0.f, 1.f, processor.sfzPlayer2.getSfzDecay()   / 30.0f),
              "DEC",
              juce::String (processor.sfzPlayer2.getSfzDecay(), 2) + "s");

    // Sustain: 0-100 %
    drawKnob (g, adsrSusZone,
              juce::jlimit (0.f, 1.f, processor.sfzPlayer2.getSfzSustain() / 100.0f),
              "SUS",
              juce::String (juce::roundToInt (processor.sfzPlayer2.getSfzSustain())) + "%");

    // Release: 0-60 s
    drawKnob (g, adsrRelZone,
              juce::jlimit (0.f, 1.f, processor.sfzPlayer2.getSfzRelease() / 60.0f),
              "REL",
              juce::String (processor.sfzPlayer2.getSfzRelease(), 2) + "s");
}

void SfzPlayerDropdownPanel::drawHeaderStrip (juce::Graphics& g) const
{
    const auto& theme = getTheme();
    drawPresetPicker (g);

    {
        drawKnob (g, transZone, transToNorm (processor.sfzPlayer2.getTranspose()),
                  "TRN",
                  [&]() -> juce::String {
                      const int s = processor.sfzPlayer2.getTranspose();
                      return s == 0 ? "0st" : (s > 0 ? "+" : "") + juce::String (s) + "st";
                  }());

        drawKnob (g, fineZone, fineToNorm (processor.sfzPlayer2.getFineTune()),
                  "FINE",
                  [&]() -> juce::String {
                      const float c = processor.sfzPlayer2.getFineTune();
                      return (c >= 0 ? "+" : "") + juce::String (c, 0) + "c";
                  }());
    }

    drawKnob (g, rvMixZone, processor.sfzPlayer2.getReverbMix() / 100.0f,
              "MIX",
              juce::String (juce::roundToInt (processor.sfzPlayer2.getReverbMix())) + "%");

    drawKnob (g, rvSizeZone, processor.sfzPlayer2.getReverbSize() / 100.0f,
              "SIZE",
              juce::String (juce::roundToInt (processor.sfzPlayer2.getReverbSize())) + "%");

    drawKnob (g, panZone, panToNorm (processor.sfzPlayer2.getPan()),
              "PAN",
              [&]() -> juce::String {
                  const float p = processor.sfzPlayer2.getPan();
                  if (std::abs (p) < 0.01f) return "C";
                  const int pct = juce::roundToInt (std::abs (p) * 100);
                  return (p < 0 ? "L" : "R") + juce::String (pct);
              }());

    drawKnob (g, volZone, volToNorm (processor.sfzPlayer2.getVolume()),
              "VOL",
              [&]() -> juce::String {
                  const float db = juce::Decibels::gainToDecibels (processor.sfzPlayer2.getVolume());
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

void SfzPlayerDropdownPanel::drawPresetPicker (juce::Graphics& g) const
{
    const auto& theme    = getTheme();
    const bool  isLoaded = processor.sfzPlayer2.isLoaded();

    // Background
    {
        auto bg = nameZone.toFloat();
        const bool anyOpen = browserOpen;
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
            g.drawText (processor.sfzPlayer2.getLoadedFile().getFileNameWithoutExtension(),
                        lbl, juce::Justification::centred, true);
        }
        else
        {
            const int idx = juce::jlimit (0, (int) presetList.size() - 1,
                                          processor.sfzPlayer2.getCurrentPresetIndex());
            const auto& info = presetList[(size_t) idx];

            // Top mini-label
            {
                auto topLine = lbl.removeFromTop (lbl.getHeight() / 2);
                g.setFont (DysektLookAndFeel::makeFont (10.5f));
                g.setColour (theme.foreground.withAlpha (0.38f));
                const auto caption =
                    processor.sfzPlayer2.getLoadedFile().getFileNameWithoutExtension()
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

void SfzPlayerDropdownPanel::drawKnob (juce::Graphics& g, juce::Rectangle<int> bounds,
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

void SfzPlayerDropdownPanel::drawMeter (juce::Graphics& g) const
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

void SfzPlayerDropdownPanel::timerCallback()
{
    const float newL = processor.sfz2PeakL.load (std::memory_order_relaxed);
    const float newR = processor.sfz2PeakR.load (std::memory_order_relaxed);
    if (newL > holdL) holdL = newL;
    if (newR > holdR) holdR = newR;
    holdL *= kHoldDecay;
    holdR *= kHoldDecay;
    meterL = newL;
    meterR = newR;

    // ── MIDI activity LED ─────────────────────────────────────────────────────
    const int activity = processor.sfz2MidiActivity.load (std::memory_order_relaxed);
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

    presetList = processor.sfzPlayer2.getPresetList();

    // Poll sfzPlayer2ChannelMask from processor for paint (avoids atomic reads in paint).
    // Derive lo/hi as the lowest and highest set channel bits for spinner display.
    // Channel 1 is hardwired to the slicer and never appears in sfzPlayer2ChannelMask.
    {
        const uint32_t mask = processor.sfzPlayer2ChannelMask.load (std::memory_order_relaxed);
        cachedChLow  = 0;
        cachedChHigh = 0;
        if (mask != 0)
        {
            for (int c = 2; c <= 16; ++c)  if (mask & (1u << c)) { cachedChLow  = c; break; }
            for (int c = 16; c >= 2; --c)  if (mask & (1u << c)) { cachedChHigh = c; break; }
        }
    }

    repaint();
}

// =============================================================================
//  Preset navigation
// =============================================================================

void SfzPlayerDropdownPanel::selectPreset (int delta)
{
    if (presetList.empty()) return;

    const int cur  = processor.sfzPlayer2.getCurrentPresetIndex();
    const int next = juce::jlimit (0, (int) presetList.size() - 1, cur + delta);

    if (next != cur)
    {
        processor.sfzPlayer2.setPresetByIndex (next);

        if (processor.sfzPlayer2.isLoaded())
            reloadZones (processor.sfzPlayer2.getLoadedFile());

        repaint();
    }
}

// =============================================================================
//  MIDI Learn menu
// =============================================================================

void SfzPlayerDropdownPanel::showMidiLearnMenu (int fieldId, juce::Point<int> screenPos)
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

void SfzPlayerDropdownPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();

    // ── Channel-range spinners (visible in SF2 strip) ─────────────────────
    auto adjustChannel = [&](bool isLow, int delta)
    {
        // Derive current lo/hi from sfzPlayer2ChannelMask for spinner display.
        const uint32_t curMask = processor.sfzPlayer2ChannelMask.load (std::memory_order_relaxed);
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
        const uint32_t sf2Mask = processor.sfzPlayer2ChannelMask.load (std::memory_order_relaxed);
        const uint32_t sfPlayerMask = processor.sfPlayerChannelMask.load (std::memory_order_relaxed);
        auto isFree = [&](int ch) -> bool
        {
            if (ch < 2 || ch > 16) return false;
            return ! ((chromaMask | sfPlayerMask) & (1u << ch));
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
                processor.sfzPlayer2ChannelMask.store      (mask, std::memory_order_relaxed);
                processor.savedSfzPlayer2ChannelMask.store (mask, std::memory_order_relaxed);
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
                processor.sfzPlayer2ChannelMask.store      (mask, std::memory_order_relaxed);
                processor.savedSfzPlayer2ChannelMask.store (mask, std::memory_order_relaxed);
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
        if (browserOpen) closeBrowser();
        else             openBrowser();
        return;
    }

    // ── Clicking the label area when browser is closed and no file loaded ─────
    if (presetLabel.contains (pos) && ! browserOpen
        && ! processor.sfzPlayer2.isLoaded())
    {
        openBrowser();
        return;
    }

    // ── Clicking label when browser is open — close it ────────────────────────
    if (nameZone.contains (pos) && browserOpen)
    {
        closeBrowser();
        return;
    }

    // ── Right-click — MIDI Learn menu, Save SFZ As, or SFZ MIDI channel ──────
    if (e.mods.isRightButtonDown())
    {
        if (nameZone.contains (pos) || folderIconZone.contains (pos))
        {
            if (processor.sfzPlayer2.isLoaded())
            {
                const auto ext = processor.sfzPlayer2.getLoadedFile()
                                     .getFileExtension().toLowerCase();

                if (ext == ".sfz")
                {
                    // SFZ: channel picker + Save As in the same menu
                    const int curCh = processor.sfzPlayer2.getMidiChannel();
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
                                processor.sfzPlayer2.setMidiChannel (0);
                            }
                            else if (result > 200 && result <= 216)
                            {
                                const int ch = result - 200;
                                processor.sfzPlayer2.setMidiChannel (ch);
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
        struct { juce::Rectangle<int>& zone; ActiveKnob id; float val; } knobs[] =
        {
            { volZone,     ActiveKnob::Volume,      volToNorm   (processor.sfzPlayer2.getVolume()) },
            { transZone,   ActiveKnob::Transpose,   transToNorm (processor.sfzPlayer2.getTranspose()) },
            { panZone,     ActiveKnob::Pan,         panToNorm   (processor.sfzPlayer2.getPan()) },
            { fineZone,    ActiveKnob::FineTune,    fineToNorm  (processor.sfzPlayer2.getFineTune()) },
            { rvMixZone,   ActiveKnob::ReverbMix,   processor.sfzPlayer2.getReverbMix()  / 100.0f },
            { rvSizeZone,  ActiveKnob::ReverbSize,  processor.sfzPlayer2.getReverbSize() / 100.0f },
            { adsrAtkZone, ActiveKnob::AdsrAttack,  juce::jlimit (0.f, 1.f, processor.sfzPlayer2.getSfzAttack()  / 30.0f) },
            { adsrDecZone, ActiveKnob::AdsrDecay,   juce::jlimit (0.f, 1.f, processor.sfzPlayer2.getSfzDecay()   / 30.0f) },
            { adsrSusZone, ActiveKnob::AdsrSustain, juce::jlimit (0.f, 1.f, processor.sfzPlayer2.getSfzSustain() / 100.0f) },
            { adsrRelZone, ActiveKnob::AdsrRelease, juce::jlimit (0.f, 1.f, processor.sfzPlayer2.getSfzRelease() / 60.0f) },
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

void SfzPlayerDropdownPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (activeKnob == ActiveKnob::None) return;
    const float delta   = (float)(dragStartY - e.getPosition().y) / 120.0f;
    const float newNorm = juce::jlimit (0.f, 1.f, dragStartVal + delta);

    switch (activeKnob)
    {
        case ActiveKnob::Volume:      processor.sfzPlayer2.setVolume    (normToVol   (newNorm)); break;
        case ActiveKnob::Transpose:   processor.sfzPlayer2.setTranspose (normToTrans (newNorm)); break;
        case ActiveKnob::Pan:         processor.sfzPlayer2.setPan       (normToPan   (newNorm)); break;
        case ActiveKnob::FineTune:    processor.sfzPlayer2.setFineTune  (normToFine  (newNorm)); break;
        case ActiveKnob::ReverbMix:   processor.sfzPlayer2.setReverbMix  (newNorm * 100.0f);     break;
        case ActiveKnob::ReverbSize:  processor.sfzPlayer2.setReverbSize (newNorm * 100.0f);     break;
        case ActiveKnob::AdsrAttack:  processor.sfzPlayer2.setSfzAttack  (newNorm * 30.0f);      break;
        case ActiveKnob::AdsrDecay:   processor.sfzPlayer2.setSfzDecay   (newNorm * 30.0f);      break;
        case ActiveKnob::AdsrSustain: processor.sfzPlayer2.setSfzSustain (newNorm * 100.0f);     break;
        case ActiveKnob::AdsrRelease: processor.sfzPlayer2.setSfzRelease (newNorm * 60.0f);      break;
        default: break;
    }
    repaint();
}

void SfzPlayerDropdownPanel::mouseUp (const juce::MouseEvent&)
{
    activeKnob = ActiveKnob::None;
}

void SfzPlayerDropdownPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (volZone.contains    (pos)) { processor.sfzPlayer2.setVolume    (1.0f);  repaint(); }
    if (transZone.contains  (pos)) { processor.sfzPlayer2.setTranspose (0);     repaint(); }
    if (panZone.contains    (pos)) { processor.sfzPlayer2.setPan       (0.0f);  repaint(); }
    if (fineZone.contains   (pos)) { processor.sfzPlayer2.setFineTune  (0.0f);  repaint(); }
    if (rvMixZone.contains  (pos)) { processor.sfzPlayer2.setReverbMix  (0.0f);  repaint(); }
    if (rvSizeZone.contains (pos)) { processor.sfzPlayer2.setReverbSize (50.0f);  repaint(); }
    // ADSR defaults
    if (adsrAtkZone.contains (pos)) { processor.sfzPlayer2.setSfzAttack  (0.005f);  repaint(); }
    if (adsrDecZone.contains (pos)) { processor.sfzPlayer2.setSfzDecay   (0.1f);    repaint(); }
    if (adsrSusZone.contains (pos)) { processor.sfzPlayer2.setSfzSustain (100.0f);  repaint(); }
    if (adsrRelZone.contains (pos)) { processor.sfzPlayer2.setSfzRelease (0.05f);   repaint(); }
}

void SfzPlayerDropdownPanel::mouseWheelMove (const juce::MouseEvent& e,
                                        const juce::MouseWheelDetails& w)
{
    if (browserOpen) return;

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
        processor.sfzPlayer2.setVolume (normToVol (adjustNorm (volToNorm (processor.sfzPlayer2.getVolume()), step)));
    else if (transZone.contains (pos))
        processor.sfzPlayer2.setTranspose (normToTrans (adjustNorm (transToNorm (processor.sfzPlayer2.getTranspose()), step)));
    else if (panZone.contains (pos))
        processor.sfzPlayer2.setPan (normToPan (adjustNorm (panToNorm (processor.sfzPlayer2.getPan()), step)));
    else if (fineZone.contains (pos))
        processor.sfzPlayer2.setFineTune (normToFine (adjustNorm (fineToNorm (processor.sfzPlayer2.getFineTune()), step)));
    else if (rvMixZone.contains (pos))
        processor.sfzPlayer2.setReverbMix  (juce::jlimit (0.0f, 100.0f, processor.sfzPlayer2.getReverbMix()  + step * 100.0f));
    else if (rvSizeZone.contains (pos))
        processor.sfzPlayer2.setReverbSize (juce::jlimit (0.0f, 100.0f, processor.sfzPlayer2.getReverbSize() + step * 100.0f));
    else if (adsrAtkZone.contains (pos))
        processor.sfzPlayer2.setSfzAttack  (juce::jlimit (0.f, 30.f,  adjustNorm (processor.sfzPlayer2.getSfzAttack()  / 30.0f,  step) * 30.0f));
    else if (adsrDecZone.contains (pos))
        processor.sfzPlayer2.setSfzDecay   (juce::jlimit (0.f, 30.f,  adjustNorm (processor.sfzPlayer2.getSfzDecay()   / 30.0f,  step) * 30.0f));
    else if (adsrSusZone.contains (pos))
        processor.sfzPlayer2.setSfzSustain (juce::jlimit (0.f, 100.f, adjustNorm (processor.sfzPlayer2.getSfzSustain() / 100.0f, step) * 100.0f));
    else if (adsrRelZone.contains (pos))
        processor.sfzPlayer2.setSfzRelease (juce::jlimit (0.f, 60.f,  adjustNorm (processor.sfzPlayer2.getSfzRelease() / 60.0f,  step) * 60.0f));

    repaint();
}

// =============================================================================
//  File drag-and-drop
// =============================================================================

bool SfzPlayerDropdownPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".sfz")   // SFZ-Player accepts SFZ only
            return true;
    }
    return false;
}

void SfzPlayerDropdownPanel::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".sfz")   // SFZ-Player only
        {
            juce::File file (f);
            processor.sfzPlayer2.loadFile (file, processor.fileLoadPool);
            processor.sfzPlayer2ChannelMask.store (1u << 2, std::memory_order_relaxed); // ch2 default
            reloadZones (file);
            closeBrowser();
            repaint();
            return;
        }
    }
}

// =============================================================================
//  panelDidShow
// =============================================================================

void SfzPlayerDropdownPanel::panelDidShow()
{
    presetList = processor.sfzPlayer2.getPresetList();

    if (processor.sfzPlayer2.isLoaded())
    {
        const auto f = processor.sfzPlayer2.getLoadedFile();
        reloadZones (f);
    }
    else
        initEmptySfz();   // bootstrap so [+ ZONE] is available and zones show immediately
    resized();
    repaint();
}

// =============================================================================
//  initEmptySfz
// =============================================================================

void SfzPlayerDropdownPanel::initEmptySfz()
{
    // Create Custom.sfz in the user's Music folder if it doesn't exist yet.
    // (Never overwrites — if the file is already there from a previous session,
    //  we just reload it so the existing zones are restored.)
    auto sfz = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                   .getChildFile ("Custom.sfz");

    if (! sfz.existsAsFile())
        sfz.replaceWithText ("// Custom SFZ — built with SF-Player\n\n");

    processor.sfzPlayer2.loadFile (sfz, processor.fileLoadPool);   // sfizz handles empty file gracefully (silence)
    processor.sfzPlayer2ChannelMask.store (1u << 2, std::memory_order_relaxed); // ch2 default
    reloadZones (sfz);                    // sets [+ ZONE] button visible + wires callback
}

// =============================================================================
//  Value mapping
// =============================================================================

float SfzPlayerDropdownPanel::volToNorm   (float linear) const { return juce::jlimit (0.f, 1.f, linear * 0.5f); }
float SfzPlayerDropdownPanel::normToVol   (float n)       const { return n * 2.0f; }
float SfzPlayerDropdownPanel::transToNorm (int semi)       const { return ((float) semi + 24.0f) / 48.0f; }
int   SfzPlayerDropdownPanel::normToTrans (float n)        const { return juce::roundToInt (n * 48.0f - 24.0f); }
float SfzPlayerDropdownPanel::panToNorm   (float p)        const { return (p + 1.0f) * 0.5f; }
float SfzPlayerDropdownPanel::normToPan   (float n)        const { return n * 2.0f - 1.0f; }
float SfzPlayerDropdownPanel::fineToNorm  (float cents)    const { return (cents + 100.0f) / 200.0f; }
float SfzPlayerDropdownPanel::normToFine  (float n)        const { return n * 200.0f - 100.0f; }

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

std::vector<KeysPanel::Keyzone> SfzPlayerDropdownPanel::parseSfzZones (const juce::File& f)
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
            // Helper: extract opcode value after 'prefix=' handling end-of-line (no trailing space).
            auto getOpcodeValue = [&] (const juce::String& src, const juce::String& prefix) -> juce::String
            {
                const int pos = src.indexOf (prefix);
                if (pos < 0) return {};
                auto rest = src.substring (pos + prefix.length()).trim();
                const int sp = rest.indexOfChar (' ');
                return sp >= 0 ? rest.substring (0, sp).trim() : rest.trim();
            };

            auto loRaw = getOpcodeValue (lineLower, "lokey=");
            if (loRaw.isNotEmpty())
                loKey = juce::jlimit (0, 127, loRaw.getIntValue());

            auto hiRaw = getOpcodeValue (lineLower, "hikey=");
            if (hiRaw.isNotEmpty())
                hiKey = juce::jlimit (0, 127, hiRaw.getIntValue());

            // key= only applies when neither lokey= nor hikey= present on this line
            if (loRaw.isEmpty() && hiRaw.isEmpty())
            {
                auto kRaw = getOpcodeValue (lineLower, "key=");
                if (kRaw.isNotEmpty())
                {
                    const int k = juce::jlimit (0, 127, kRaw.getIntValue());
                    loKey = hiKey = k;
                }
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

std::vector<KeysPanel::Keyzone> SfzPlayerDropdownPanel::parseSf2Zones (const juce::File& f,
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

void SfzPlayerDropdownPanel::reloadZones (const juce::File& f)
{
    const auto ext   = f.getFileExtension().toLowerCase();
    const bool isSfz = (ext == ".sfz");

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
        //
        // [+ ZONE] is available whenever nothing is loaded OR an .sfz is loaded.
        // It must stay HIDDEN when an .sf2 is loaded: this same panel/engine is
        // shared with the SF2-PLAYER tab (see PluginEditor::onLoadRequest, uiMode==2,
        // which routes .sf2 files here via sfzDropdown.onFileChosen()), and SF2 zones
        // are read-only — adding a zone on top of a loaded SF2 makes no sense.
        // openAddZoneChooser() handles the "nothing loaded yet" case by prompting
        // Save-As after the sample is picked, creating a new .sfz target.
        const bool isSf2 = (ext == ".sf2");
        keysPanel.setAddZoneButtonVisible (! isSf2);
        if (isSf2)
            keysPanel.onAddZoneRequested = nullptr;
        else
            keysPanel.onAddZoneRequested = [this] { openAddZoneChooser(); };

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

void SfzPlayerDropdownPanel::writeSfzZoneChange (const juce::File& f,
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
    processor.sfzPlayer2.loadFile (f, processor.fileLoadPool);
    processor.sfzPlayer2ChannelMask.store (1u << 2, std::memory_order_relaxed); // ch2 default
}

// =============================================================================
//  openAddZoneChooser  —  Issue 2: Add Zone support
// =============================================================================

void SfzPlayerDropdownPanel::openAddZoneChooser()
{
    // Resolve the target SFZ (may be empty if nothing is loaded yet).
    juce::File targetSfz;
    if (processor.sfzPlayer2.isLoaded())
    {
        const auto loaded = processor.sfzPlayer2.getLoadedFile();
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

void SfzPlayerDropdownPanel::showAddZoneOverlay (const juce::File& sfzFile,
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
            showOverlay (messageOverlay, std::make_unique<MessageOverlay> (
                "Add Zone Failed",
                "Could not write to:\n" + sfzFile.getFullPathName(),
                MessageOverlay::Kind::Warning));
            messageOverlay->onDismiss = [this] { juce::MessageManager::callAsync ([this] { hideOverlays(); }); };
            return;
        }

        processor.sfzPlayer2.loadFile (sfzFile, processor.fileLoadPool);
        processor.sfzPlayer2ChannelMask.store (1u << 2, std::memory_order_relaxed); // ch2 default
        reloadZones (sfzFile);
        keysPanel.autoScrollToZones();
        repaint();
    };

    showOverlay (addZoneOverlay, std::move (overlay));
}

bool SfzPlayerDropdownPanel::appendZoneToSfz (const juce::File& sfzFile,
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
void SfzPlayerDropdownPanel::openSaveAsNewForZone (const juce::File& sampleFile)
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

        processor.sfzPlayer2.loadFile (dest, processor.fileLoadPool);
        processor.sfzPlayer2ChannelMask.store (1u << 2, std::memory_order_relaxed); // ch2 default
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

void SfzPlayerDropdownPanel::openSaveAsOverlay()
{
    const auto currentFile = processor.sfzPlayer2.isLoaded()
                           ? processor.sfzPlayer2.getLoadedFile()
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
                showOverlay (messageOverlay, std::make_unique<MessageOverlay> (
                    "Save Failed",
                    "Could not write:\n" + dest.getFullPathName(),
                    MessageOverlay::Kind::Warning));
                messageOverlay->onDismiss = [this] { juce::MessageManager::callAsync ([this] { hideOverlays(); }); };
                return;
            }
        }
        else
        {
            dest.replaceWithText ("// Custom SFZ — built with SF-Player\n\n");
        }

        processor.sfzPlayer2.loadFile (dest, processor.fileLoadPool);
        reloadZones (dest);
        repaint();
    };

    showOverlay (saveSfzOverlay, std::move (overlay));
}

void SfzPlayerDropdownPanel::hideOverlays()
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
    if (messageOverlay)
    {
        if (auto* p = messageOverlay->getParentComponent())
            p->removeChildComponent (messageOverlay.get());
        messageOverlay.reset();
    }
}
