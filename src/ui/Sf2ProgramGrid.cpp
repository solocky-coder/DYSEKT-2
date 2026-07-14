// =============================================================================
//  Sf2ProgramGrid.cpp
// =============================================================================
#include "Sf2ProgramGrid.h"
#include "DysektLookAndFeel.h"
#include "IconManager.h"

// ── Helper: theme access (same pattern used throughout the codebase) ──────────
static const ThemeData& gridTheme() { return getTheme(); }

// ── Helper: GM instrument-family colour ────────────────────────────────────
// General MIDI bank 0 groups presets into fixed 8-wide family blocks
// (0-7 Piano, 8-15 Chromatic Perc, 16-23 Organ, 24-31 Guitar, 32-39 Bass,
// 40-47 Strings, 48-55 Ensemble, 56-63 Brass, 64-71 Reed, 72-79 Pipe,
// 80-87 Synth Lead, 88-95 Synth Pad, 96-103 Synth FX, 104-111 Ethnic,
// 112-119 Percussive, 120-127 Sound FX). Same palette-lookup pattern as
// MixerPanel's kChanPalette, applied here so the preset grid reads as
// 16 distinct instrument neighbourhoods instead of one flat block.
static juce::Colour gmFamilyColour (int preset)
{
    static const juce::Colour kFamilyPalette[16] = {
        juce::Colour (0xFF4060A0), // Piano
        juce::Colour (0xFF60A0A0), // Chromatic Perc
        juce::Colour (0xFFA07840), // Organ
        juce::Colour (0xFF60A040), // Guitar
        juce::Colour (0xFFA04060), // Bass
        juce::Colour (0xFF8060C0), // Strings
        juce::Colour (0xFFA0A040), // Ensemble
        juce::Colour (0xFFC08040), // Brass
        juce::Colour (0xFF40A0A0), // Reed
        juce::Colour (0xFF60C080), // Pipe
        juce::Colour (0xFFC04080), // Synth Lead
        juce::Colour (0xFF8080C0), // Synth Pad
        juce::Colour (0xFF40C0C0), // Synth FX
        juce::Colour (0xFFC0A040), // Ethnic
        juce::Colour (0xFF808080), // Percussive
        juce::Colour (0xFFC06060), // Sound FX
    };
    return kFamilyPalette[(juce::jmax (0, preset) / 8) % 16];
}

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
    const bool firstLoad = presets.empty();
    presets = list;
    // midiCh is now per-preset — don't reset presetChannels here.
    // (currentMidiChannel parameter kept for API compatibility but ignored.)

    // On first load only: start with nothing highlighted.
    if (firstLoad)
    {
        currentIdx    = -1;
        previewIdx    = -1;
        hoveredCell   = -1;
        presetChannels.clear();
        scrollY       = 0;
    }

    rebuildLayout();
    updateScrollBar();
    repaint();
}

void Sf2ProgramGrid::setCurrentIndex (int idx)
{
    currentIdx = idx;
    repaint();
}

void Sf2ProgramGrid::setEditingIndex (int idx)
{
    if (editingIdx == idx) return;
    editingIdx = idx;
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

    // Compute totalH using scale-aware row heights
    const int ch = cellH();
    const int hh = hdrH();
    totalH = kPad;
    for (auto& r : rows)
        totalH += r.isHeader ? hh : ch;
    totalH += kPad;
}

// =============================================================================
//  resized
// =============================================================================
void Sf2ProgramGrid::updateScrollBar()
{
    const int h = getHeight();
    if (h <= 0) return;

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

void Sf2ProgramGrid::resized()
{
    rebuildLayout();
    updateScrollBar();
}

// =============================================================================
//  paint
// =============================================================================
void Sf2ProgramGrid::paint (juce::Graphics& g)
{
    const auto& theme = gridTheme();
    const int   availW = getWidth() - (scrollBar.isVisible() ? kScrollW : 0);
    const int   cellW  = availW / kCols;           // fills full width, no kPad gap
    const int   w      = cellW * kCols;            // actual grid width (≤ availW by integer division)
    const int   gridW  = w;

    // Simple background — the CRT frame is drawn by SfzDropdownPanel::paintOverChildren
    // over this component's bounds, so it's not clipped.
    g.setColour (theme.darkBar.darker (0.55f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);

    // Clip to grid area
    g.saveState();
    g.reduceClipRegion (0, 0, getWidth() - (scrollBar.isVisible() ? kScrollW : 0), getHeight());

    // Load once per paint — same sprites HeaderBar buttons use, cached here
    // instead of reloaded per-cell (grid can have far more cells than the
    // 4 header buttons that each call these per paint).
    const auto idleSprite  = IconManager::getButtonIdle();
    const auto hoverSprite = IconManager::getButtonHover();

    int y = kPad - scrollY;

    const int ch = cellH();
    const int hh = hdrH();

    for (auto& row : rows)
    {
        if (row.isHeader)
        {
            // Bank header
            const auto hdrBounds = juce::Rectangle<int> (0, y, gridW, hh);
            g.setColour (theme.accent.withAlpha (0.12f));
            g.fillRect (hdrBounds);
            // Accent left-rule
            g.setColour (theme.accent.withAlpha (0.75f));
            g.fillRect (juce::Rectangle<int> (0, y, 2, hh));
            g.setFont (DysektLookAndFeel::makeFont (11.0f, true));
            g.setColour (theme.accent.withAlpha (0.65f));
            g.drawText ("BANK " + juce::String (row.bank),
                        hdrBounds.reduced (4, 0).withTrimmedLeft (4),
                        juce::Justification::centredLeft, false);
            y += hh;
        }
        else
        {
            // Cell row
            for (int c = 0; c < row.count; ++c)
            {
                const int idx = row.firstIdx + c;
                const auto& info = presets[(size_t) idx];

                const juce::Rectangle<int> cell (c * cellW, y, cellW - 2, ch - 2);

                const bool isSelected   = (idx == currentIdx);
                const bool isPreviewing = (idx == previewIdx);
                const bool isHovered    = (idx == hoveredCell) && ! isSelected && ! isPreviewing;
                const int  assignedCh   = presetChannels.count (idx) ? presetChannels.at (idx) : 0;
                const bool isAssigned   = (assignedCh > 0);
                const bool isEditing    = (idx == editingIdx);

                // Cell background
                if (isPreviewing)
                {
                    // Use the theme accent colour directly — same family as selected,
                    // but brighter fill so it reads as "active/auditing".
                    g.setColour (theme.accent.withAlpha (0.35f));
                    g.fillRoundedRectangle (cell.toFloat(), 3.0f);
                    g.setColour (theme.accent.withAlpha (0.90f));
                    g.drawRoundedRectangle (cell.toFloat().reduced (0.5f), 3.0f, 1.5f);

                    // Small "live" dot in top-right corner
                    const auto dot = juce::Rectangle<float> (
                        (float)(cell.getRight() - 7), (float)(cell.getY() + 3), 4.f, 4.f);
                    g.setColour (theme.accent);
                    g.fillEllipse (dot);
                }
                else if (isEditing)
                {
                    // Assigned + currently selected for per-channel FX editing: bright solid border
                    g.setColour (theme.accent.withAlpha (0.25f));
                    g.fillRoundedRectangle (cell.toFloat(), 3.0f);
                    g.setColour (theme.accent.withAlpha (1.0f));
                    g.drawRoundedRectangle (cell.toFloat().reduced (0.5f), 3.0f, 1.5f);
                }
                else if (isAssigned)
                {
                    // Assigned but not currently editing: subtle tinted fill + accent border
                    g.setColour (theme.accent.withAlpha (0.12f));
                    g.fillRoundedRectangle (cell.toFloat(), 3.0f);
                    g.setColour (theme.accent.withAlpha (0.50f));
                    g.drawRoundedRectangle (cell.toFloat().reduced (0.5f), 3.0f, 1.0f);
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
                    // Same treatment as HeaderBar buttons (see
                    // DysektLookAndFeel::drawButtonBackground): the hover
                    // sprite as base layer, theme-tinted border on top so it
                    // still follows the active theme like every other button.
                    if (hoverSprite != nullptr)
                    {
                        hoverSprite->drawWithin (g, cell.toFloat(),
                                                  juce::RectanglePlacement::stretchToFit, 1.0f);
                    }
                    else
                    {
                        g.setColour (theme.button.brighter (0.10f));
                        g.fillRoundedRectangle (cell.toFloat(), 3.0f);
                    }

                    // Family tint wash + left accent stripe, kept subtle so
                    // hover state still reads as the dominant treatment.
                    const juce::Colour famColHover = gmFamilyColour (info.preset);
                    g.setColour (famColHover.withAlpha (0.08f));
                    g.fillRoundedRectangle (cell.toFloat(), 3.0f);
                    g.setColour (famColHover.withAlpha (0.7f));
                    g.fillRect (cell.getX(), cell.getY(), 3, cell.getHeight());

                    g.setColour (theme.separator.brighter (0.30f));
                    g.drawRoundedRectangle (cell.toFloat().reduced (0.5f), 3.0f, 1.0f);
                }
                else
                {
                    // Idle: button_idle.svg base layer, same as UNDO/REDO/
                    // PANIC/cog, with the theme's separator colour as the
                    // border so it still reads as "this theme" per-preset.
                    if (idleSprite != nullptr)
                    {
                        idleSprite->drawWithin (g, cell.toFloat(),
                                                 juce::RectanglePlacement::stretchToFit, 1.0f);
                    }
                    else
                    {
                        g.setColour (theme.button);
                        g.fillRoundedRectangle (cell.toFloat(), 3.0f);
                    }

                    // Family tint wash + left accent stripe — same visual
                    // language as MixerPanel's slice-colour rows, so each
                    // GM instrument family (piano/organ/guitar/bass/etc.)
                    // reads as its own neighbourhood in the grid instead of
                    // a wall of identical grey tiles.
                    const juce::Colour famCol = gmFamilyColour (info.preset);
                    g.setColour (famCol.withAlpha (0.10f));
                    g.fillRoundedRectangle (cell.toFloat(), 3.0f);
                    g.setColour (famCol.withAlpha (0.7f));
                    g.fillRect (cell.getX(), cell.getY(), 3, cell.getHeight());

                    g.setColour (theme.separator.withAlpha (0.60f));
                    g.drawRoundedRectangle (cell.toFloat().reduced (0.5f), 3.0f, 1.0f);
                }

                // Preset number badge (top-left)
                {
                    const auto badge = cell.withWidth (28).withHeight (14)
                                           .withX (cell.getX() + 2).withY (cell.getY() + 2);
                    g.setFont (DysektLookAndFeel::makeFont (11.5f));
                    g.setColour (isPreviewing ? theme.accent.brighter (0.3f)
                                 : isEditing  ? theme.accent.brighter (0.3f)
                                 : isAssigned ? theme.accent.brighter (0.1f)
                                 : isSelected ? theme.accent.brighter (0.2f)
                                             : theme.foreground.withAlpha (0.30f));
                    g.drawText (juce::String (info.preset), badge,
                                juce::Justification::centredLeft, false);
                }

                // MIDI channel badge (bottom-right corner) — only when assigned
                if (isAssigned)
                {
                    const juce::String chLabel = "ch" + juce::String (assignedCh);
                    const int bw = 26, bh = 14;
                    const auto badgeR = juce::Rectangle<int> (
                        cell.getRight() - bw - 2, cell.getBottom() - bh - 2, bw, bh);
                    g.setColour (isEditing ? theme.accent : theme.accent.withAlpha (0.85f));
                    g.fillRoundedRectangle (badgeR.toFloat(), 2.f);
                    g.setFont (DysektLookAndFeel::makeFont (10.0f, true));
                    g.setColour (theme.darkBar);
                    g.drawText (chLabel, badgeR, juce::Justification::centred, false);
                }

                // Preset name (centred)
                {
                    g.setFont (DysektLookAndFeel::makeFont (15.0f));
                    g.setColour (isPreviewing ? theme.foreground.brighter (0.2f).withAlpha (0.95f)
                                 : isEditing  ? theme.foreground.brighter (0.2f)
                                 : isAssigned ? theme.foreground.brighter (0.05f).withAlpha (0.90f)
                                 : isSelected ? theme.foreground.brighter (0.1f)
                                             : theme.foreground.withAlpha (0.78f));
                    // Shrink name area if channel badge is showing
                    const auto nameRect = isAssigned ? cell.reduced (3, 0).withTrimmedBottom (15)
                                                     : cell.reduced (3, 0);
                    g.drawText (info.name, nameRect, juce::Justification::centred, true);
                }
            }
            y += ch;
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
    const int availW = getWidth() - (scrollBar.isVisible() ? kScrollW : 0);
    const int cellW  = availW / kCols;
    const int gridW  = cellW * kCols;

    int y = kPad - scrollY;

    const int ch = cellH();
    const int hh = hdrH();

    for (const auto& row : rows)
    {
        if (row.isHeader)
        {
            y += hh;
        }
        else
        {
            const juce::Rectangle<int> rowBounds (0, y, gridW, ch);
            if (rowBounds.contains (pt))
            {
                const int col = pt.x / cellW;
                if (col >= 0 && col < row.count)
                    return row.firstIdx + col;
            }
            y += ch;
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
        // Left-click on an already-assigned cell → select it for per-channel FX editing.
        const int assignedCh = presetChannels.count (idx) ? presetChannels.at (idx) : 0;
        if (assignedCh > 0)
        {
            editingIdx = idx;
            if (onAssignedPresetClicked) onAssignedPresetClicked (idx);
            repaint();
            return;
        }

        // Radio-button preview toggle — left-click auditions, click again to deactivate.
        // currentIdx is only set via right-click channel assignment, never on left-click.
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
    menu.addSectionHeader ("Assign MIDI Channel");
    menu.addSeparator();

    const int current = presetChannels.count (presetIdx) ? presetChannels.at (presetIdx) : 0;

    for (int ch = 1; ch <= 16; ++ch)
    {
        // Mark channel as used by another preset so user can see what's taken
        bool usedByOther = false;
        for (auto& kv : presetChannels)
            if (kv.first != presetIdx && kv.second == ch)
                usedByOther = true;

        // Channels owned by chromatic slices, or currently occupied by the
        // SFZ-Player (sfzPlayer2), are never available to the SF2 player.
        const bool reserved = (blockedMask & (1u << ch)) != 0u;
        const bool inRange  = (ch >= rangeLow && ch <= rangeHigh) && ! reserved;

        const juce::String label = "Channel " + juce::String (ch)
                                   + (usedByOther ? "  [in use]"   : "")
                                   + (reserved    ? "  [reserved]" : "")
                                   + (! inRange && ! reserved ? "  [out of range]" : "");
        menu.addItem (100 + ch, label, /*isEnabled=*/ inRange, current == ch);
    }

    menu.addSeparator();
    menu.addItem (1, "Deactivate", current != 0);

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
                // Deactivate — remove channel assignment for this preset
                presetChannels.erase (presetIdx);
                if (onChannelChanged) onChannelChanged (presetIdx, 0);

                // If this was the previewed preset, clear preview too
                if (this->previewIdx == presetIdx)
                {
                    this->previewIdx = -1;
                    if (onPreviewToggled) onPreviewToggled (-1);
                }
            }
            else if (result >= 101 && result <= 116)
            {
                const int ch = result - 100;

                // A MIDI channel can only drive one preset at a time. If another
                // preset already holds this channel, its assignment is now stale
                // (the engine will only honour the newest one) and must be
                // cleared, or that cell would keep showing an "assigned" highlight
                // and channel badge for a channel it no longer actually owns.
                for (auto it = presetChannels.begin(); it != presetChannels.end(); )
                {
                    if (it->first != presetIdx && it->second == ch)
                    {
                        const int staleIdx = it->first;
                        it = presetChannels.erase (it);
                        if (onChannelChanged) onChannelChanged (staleIdx, 0);
                        if (this->previewIdx == staleIdx)
                        {
                            this->previewIdx = -1;
                            if (onPreviewToggled) onPreviewToggled (-1);
                        }
                        if (this->editingIdx == staleIdx)
                            this->editingIdx = -1;
                    }
                    else
                    {
                        ++it;
                    }
                }

                presetChannels[presetIdx] = ch;
                if (onChannelChanged) onChannelChanged (presetIdx, ch);

                // Mark as the active preview so the live dot shows
                this->previewIdx = presetIdx;
                if (onPreviewToggled) onPreviewToggled (presetIdx);
            }
            repaint();
        });
}
