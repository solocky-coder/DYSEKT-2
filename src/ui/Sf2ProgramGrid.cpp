// =============================================================================
//  Sf2ProgramGrid.cpp
// =============================================================================
#include "Sf2ProgramGrid.h"
#include "DysektLookAndFeel.h"

// ── Helper: theme access (same pattern used throughout the codebase) ──────────
static const ThemeData& gridTheme() { return getTheme(); }

// =============================================================================
Sf2ProgramGrid::Sf2ProgramGrid()
{
    scrollBar.addListener (this);
    scrollBar.setAutoHide (false);
    addChildComponent (scrollBar);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

Sf2ProgramGrid::~Sf2ProgramGrid()
{
    scrollBar.removeListener (this);
}

// =============================================================================
void Sf2ProgramGrid::setPresets (const std::vector<Sf2PresetInfo>& list,
                                  int currentIndex,
                                  int currentMidiChannel)
{
    presets    = list;
    currentIdx = currentIndex;
    midiCh     = currentMidiChannel;
    hoveredCell = -1;
    previewIdx  = -1;
    scrollY    = 0;
    rebuildLayout();
    repaint();
}

void Sf2ProgramGrid::setCurrentIndex (int idx)
{
    currentIdx = idx;
    repaint();
}

void Sf2ProgramGrid::clearPreviewState()
{
    previewIdx = -1;
    repaint();
}

// =============================================================================
//  rebuildLayout
// =============================================================================
void Sf2ProgramGrid::rebuildLayout()
{
    rows.clear();

    if (presets.empty()) { totalH = 0; return; }

    int prevBank = -9999;
    int rowStart = -1;
    int rowCount = 0;

    auto flushRow = [&]
    {
        if (rowCount > 0)
        {
            LayoutRow r;
            r.isHeader = false;
            r.firstIdx = rowStart;
            r.count    = rowCount;
            rows.push_back (r);
            rowCount = 0;
            rowStart = -1;
        }
    };

    const int w        = getWidth() - kScrollW - kPad * 2;
    const int cellW    = (w > 0) ? (w / kCols) : 60;
    (void) cellW;   // used implicitly via kCols grid maths in paint/hit-test

    for (int i = 0; i < (int) presets.size(); ++i)
    {
        const int bank = presets[(size_t) i].bank;

        if (bank != prevBank)
        {
            flushRow();
            LayoutRow hdr;
            hdr.isHeader = true;
            hdr.bank     = bank;
            rows.push_back (hdr);
            prevBank = bank;
        }

        if (rowStart < 0) rowStart = i;
        ++rowCount;

        if (rowCount == kCols)
            flushRow();
    }
    flushRow();

    // Compute totalH
    totalH = kPad;
    for (auto& r : rows)
        totalH += r.isHeader ? kHdrH : kCellH;
    totalH += kPad;
}

// =============================================================================
//  resized
// =============================================================================
void Sf2ProgramGrid::resized()
{
    rebuildLayout();

    const int h = getHeight();
    if (totalH > h)
    {
        scrollBar.setBounds (getWidth() - kScrollW, 0, kScrollW, h);
        scrollBar.setVisible (true);
        scrollBar.setRangeLimits (0.0, (double) totalH);
        scrollBar.setCurrentRange ((double) scrollY, (double) h);
    }
    else
    {
        scrollBar.setVisible (false);
        scrollY = 0;
    }
}

// =============================================================================
//  paint
// =============================================================================
void Sf2ProgramGrid::paint (juce::Graphics& g)
{
    const auto& theme = gridTheme();
    const int   w     = getWidth() - (scrollBar.isVisible() ? kScrollW : 0) - kPad * 2;
    const int   cellW = w / kCols;

    // Background
    g.setColour (theme.darkBar.darker (0.45f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    // Clip to grid area
    g.saveState();
    g.reduceClipRegion (0, 0, getWidth() - (scrollBar.isVisible() ? kScrollW : 0), getHeight());

    int y = kPad - scrollY;

    for (auto& row : rows)
    {
        if (row.isHeader)
        {
            // Bank header
            const auto hdrBounds = juce::Rectangle<int> (kPad, y, w, kHdrH);
            g.setColour (theme.accent.withAlpha (0.12f));
            g.fillRect (hdrBounds);
            g.setFont (DysektLookAndFeel::makeFont (8.5f, true));
            g.setColour (theme.accent.withAlpha (0.65f));
            g.drawText ("BANK " + juce::String (row.bank),
                        hdrBounds.reduced (4, 0),
                        juce::Justification::centredLeft, false);
            y += kHdrH;
        }
        else
        {
            // Cell row
            for (int c = 0; c < row.count; ++c)
            {
                const int idx = row.firstIdx + c;
                const auto& info = presets[(size_t) idx];

                const juce::Rectangle<int> cell (kPad + c * cellW, y, cellW - 2, kCellH - 2);

                const bool isSelected  = (idx == currentIdx);
                const bool isPreviewing = (idx == previewIdx);
                const bool isHovered   = (idx == hoveredCell) && ! isSelected && ! isPreviewing;

                // Cell background
                if (isPreviewing)
                {
                    // Bright teal/green glow — distinct from the accent-coloured "selected" state
                    const auto previewColour = juce::Colour (0xff00c896);
                    g.setColour (previewColour.withAlpha (0.22f));
                    g.fillRoundedRectangle (cell.toFloat(), 3.0f);
                    g.setColour (previewColour.withAlpha (0.80f));
                    g.drawRoundedRectangle (cell.toFloat().reduced (0.5f), 3.0f, 1.5f);

                    // Small "live" dot in top-right corner
                    const auto dot = juce::Rectangle<float> (
                        (float)(cell.getRight() - 7), (float)(cell.getY() + 3), 4.f, 4.f);
                    g.setColour (previewColour);
                    g.fillEllipse (dot);
                }
                else if (isSelected)
                {
                    g.setColour (theme.accent.withAlpha (0.30f));
                    g.fillRoundedRectangle (cell.toFloat(), 3.0f);
                    g.setColour (theme.accent.withAlpha (0.70f));
                    g.drawRoundedRectangle (cell.toFloat().reduced (0.5f), 3.0f, 1.0f);
                }
                else if (isHovered)
                {
                    g.setColour (theme.accent.withAlpha (0.12f));
                    g.fillRoundedRectangle (cell.toFloat(), 3.0f);
                    g.setColour (theme.accent.withAlpha (0.25f));
                    g.drawRoundedRectangle (cell.toFloat().reduced (0.5f), 3.0f, 1.0f);
                }
                else
                {
                    g.setColour (theme.darkBar.brighter (0.06f));
                    g.fillRoundedRectangle (cell.toFloat(), 3.0f);
                }

                // Preset number badge (top-left)
                {
                    const auto badge = cell.withWidth (22).withHeight (10)
                                           .withX (cell.getX() + 2).withY (cell.getY() + 2);
                    g.setFont (DysektLookAndFeel::makeFont (7.0f));
                    g.setColour (isPreviewing ? juce::Colour (0xff00c896).brighter (0.3f)
                                 : isSelected ? theme.accent.brighter (0.2f)
                                             : theme.foreground.withAlpha (0.30f));
                    g.drawText (juce::String (info.preset), badge,
                                juce::Justification::centredLeft, false);
                }

                // Preset name (centred)
                {
                    g.setFont (DysektLookAndFeel::makeFont (9.0f));
                    g.setColour (isPreviewing ? juce::Colour (0xfff0fff8)
                                 : isSelected ? theme.foreground.brighter (0.1f)
                                             : theme.foreground.withAlpha (0.78f));
                    g.drawText (info.name, cell.reduced (3, 0),
                                juce::Justification::centred, true);
                }
            }
            y += kCellH;
        }
    }

    g.restoreState();

    // Top/bottom fade when scrollable
    if (scrollBar.isVisible())
    {
        const int fadeH = 12;
        if (scrollY > 0)
        {
            juce::ColourGradient top (theme.darkBar.darker (0.45f).withAlpha (0.9f), 0, 0,
                                      juce::Colours::transparentBlack, 0, (float) fadeH, false);
            g.setGradientFill (top);
            g.fillRect (0, 0, getWidth() - kScrollW, fadeH);
        }
        if (totalH - scrollY > getHeight())
        {
            juce::ColourGradient bot (juce::Colours::transparentBlack, 0, (float)(getHeight() - fadeH),
                                      theme.darkBar.darker (0.45f).withAlpha (0.9f), 0, (float) getHeight(), false);
            g.setGradientFill (bot);
            g.fillRect (0, getHeight() - fadeH, getWidth() - kScrollW, fadeH);
        }
    }
}

// =============================================================================
//  Hit testing
// =============================================================================
int Sf2ProgramGrid::cellIndexAt (juce::Point<int> pt) const
{
    const int w     = getWidth() - (scrollBar.isVisible() ? kScrollW : 0) - kPad * 2;
    const int cellW = w / kCols;

    int y = kPad - scrollY;

    for (const auto& row : rows)
    {
        if (row.isHeader)
        {
            y += kHdrH;
        }
        else
        {
            const juce::Rectangle<int> rowBounds (kPad, y, w, kCellH);
            if (rowBounds.contains (pt))
            {
                const int col = (pt.x - kPad) / cellW;
                if (col >= 0 && col < row.count)
                    return row.firstIdx + col;
            }
            y += kCellH;
        }
    }
    return -1;
}

// =============================================================================
//  Mouse
// =============================================================================
void Sf2ProgramGrid::mouseMove (const juce::MouseEvent& e)
{
    const int idx = cellIndexAt (e.getPosition());
    if (idx != hoveredCell)
    {
        hoveredCell = idx;
        repaint();
    }
}

void Sf2ProgramGrid::mouseExit (const juce::MouseEvent&)
{
    if (hoveredCell != -1) { hoveredCell = -1; repaint(); }
}

void Sf2ProgramGrid::mouseDown (const juce::MouseEvent& e)
{
    const int idx = cellIndexAt (e.getPosition());
    if (idx < 0) return;

    if (e.mods.isRightButtonDown())
    {
        showChannelMenu (idx, e.getScreenPosition());
    }
    else
    {
        // Radio-button toggle: clicking the active preview preset turns it off;
        // clicking any other preset switches the preview to that one.
        if (idx == previewIdx)
        {
            previewIdx = -1;
            if (onPreviewToggled) onPreviewToggled (-1);
        }
        else
        {
            previewIdx = idx;
            if (onPreviewToggled) onPreviewToggled (idx);
        }
        repaint();
    }
}

void Sf2ProgramGrid::mouseWheelMove (const juce::MouseEvent&,
                                      const juce::MouseWheelDetails& w)
{
    if (! scrollBar.isVisible()) return;

    scrollY = juce::jlimit (0, juce::jmax (0, totalH - getHeight()),
                            scrollY - juce::roundToInt (w.deltaY * 60.0f));
    scrollBar.setCurrentRange ((double) scrollY, (double) getHeight());
    repaint();
}

void Sf2ProgramGrid::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    scrollY = (int) newRangeStart;
    repaint();
}

// =============================================================================
//  Channel picker popup
// =============================================================================
void Sf2ProgramGrid::showChannelMenu (int presetIdx, juce::Point<int> screenPos)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("MIDI Input Channel");
    menu.addSeparator();

    const int current = midiCh;

    menu.addItem (1, "Omni (all channels)", true, current == 0);
    menu.addSeparator();
    for (int ch = 1; ch <= 16; ++ch)
        menu.addItem (100 + ch, "Channel " + juce::String (ch), true, current == ch);

    auto* topLvl = getTopLevelComponent();
    float ms = DysektLookAndFeel::getMenuScale();
    menu.showMenuAsync (
        juce::PopupMenu::Options()
            .withTargetScreenArea (juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1))
            .withParentComponent (topLvl)
            .withStandardItemHeight ((int)(22 * ms)),
        [this, presetIdx] (int result)
        {
            if (result == 1)
            {
                midiCh = 0;
                if (onChannelChanged) onChannelChanged (0);
            }
            else if (result >= 101 && result <= 116)
            {
                midiCh = result - 100;
                if (onChannelChanged) onChannelChanged (midiCh);

                // Keep the preview toggle alive but mark this preset as the
                // one being previewed — the panel will reroute the live mask
                // to the newly assigned real channel.
                this->previewIdx = presetIdx;
                if (onPreviewToggled) onPreviewToggled (presetIdx);
            }
            repaint();
        });
}
