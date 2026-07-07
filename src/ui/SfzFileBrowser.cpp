// =============================================================================
//  SfzFileBrowser.cpp  —  Implementation of the self-contained directory browser
// =============================================================================
#include "SfzFileBrowser.h"
#include "DysektLookAndFeel.h"

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

// ── mouse ────────────────────────────────────────────────────────────────────

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
                              : "*.sfz";

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

void SfzFileBrowser::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    repaint();
    if (row < 0 || row >= rows.size()) return;
    if (rows[row].isDirectory())
        navigateTo (rows[row]);
    else if (onFileSingleClicked)
        onFileSingleClicked (rows[row]);
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
