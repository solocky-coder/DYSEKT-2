#include "KeysPanel.h"
#include "DysektLookAndFeel.h"
#include "../PluginProcessor.h"

// =============================================================================
// Full 128-note keyboard helpers
// =============================================================================
// MIDI note 0 = C-1.  For each semitone 0..11 in an octave:
//   white key index within octave (C=0,D=1,E=2,F=3,G=4,A=5,B=6), or -1 if black.
static const int semiToWhite[12] = { 0,-1,1,-1,2, 3,-1,4,-1,5,-1,6 };
// Number of white keys per octave = 7.
// Total white keys for MIDI 0-127:
//   MIDI 0 = C, MIDI 127 = G9.  Octaves 0-9 give 70 white keys (C..B × 10),
//   plus 3 more (G in octave 10 = G9 = MIDI 127 is actually in oct 10 but
//   MIDI 127 = G9, semi=7(G), octave 10 → 10*7=70, +4 = 74+1=75-1=74 whites? 
//   Let's count: 75 white keys total (A0 piano starts at C-1 in MIDI).

// Count total white keys from note 0 to 127
static int totalWhiteKeys()
{
    int count = 0;
    for (int n = 0; n < 128; ++n)
        if (semiToWhite[n % 12] >= 0) ++count;
    return count;   // = 75
}

// Return the white-key index (0-based from note 0) for any MIDI note.
// Returns -1 for black keys.
static int whiteIndexForNote (int note)
{
    if (semiToWhite[note % 12] < 0) return -1;
    int count = 0;
    for (int n = 0; n < note; ++n)
        if (semiToWhite[n % 12] >= 0) ++count;
    return count;
}

// =============================================================================
// ZoneMatrixContent::rebuild
// =============================================================================

void KeysPanel::ZoneMatrixContent::rebuild (const std::vector<Keyzone>& zones,
                                             int kbX, int kbW,
                                             int baseOctave,
                                             int whiteKeyW, int blackKeyW,
                                             int componentWidth)
{
    kbX_        = kbX;
    kbW_        = kbW;
    baseOctave_ = baseOctave;
    contentW_   = componentWidth;

    rows.clear();
    rows.reserve (zones.size());

    for (const auto& z : zones)
    {
        Row r;
        r.zone = z;
        rows.push_back (r);
    }

    // Height = header + one row per zone (minimum 1 row tall for empty state)
    // + one extra row at the bottom for the [+ ZONE] button when visible and
    //   zones already exist (empty state already has 1 spare row).
    const int extraRow = (addZoneBtnVisible && !rows.empty()) ? 1 : 0;
    const int totalH = kHeaderH + (juce::jmax (1, (int) rows.size()) + extraRow) * kRowH;
    setSize (componentWidth, totalH);

    if (selectedRow >= (int) rows.size())
        selectedRow = -1;

    repaint();
}

// =============================================================================
// ZoneMatrixContent::paint   — Mixer-channel row style
//
// Column layout (left → right):
//   [4px colour bar] [Name col, colour-tinted bg] | [Key range] [Root] [Vel badge] [Lp]
// =============================================================================

void KeysPanel::ZoneMatrixContent::paint (juce::Graphics& g)
{
    const auto& theme = getTheme();
    const int   w     = getWidth();
    const int   h     = getHeight();

    // ── Column geometry ───────────────────────────────────────────────────────
    // Right-side fixed columns (anchored from right edge):
    //   LP | REL | VOL | PAN | PITCH | ROOT | loKey | hiKey  — then NAME fills rest
    constexpr int kStripeW  = 4;
    constexpr int kColGap   = 4;
    constexpr int kScrollW  = 10;  // reserve for vertical scrollbar
    constexpr int kLpW      = 30;
    constexpr int kRelW     = 70;
    constexpr int kVolW     = 62;
    constexpr int kPanW     = 52;
    constexpr int kPitchW   = 52;
    constexpr int kRootW    = 52;
    constexpr int kHiKeyW   = 52;
    constexpr int kLoKeyW   = 52;

    const int kLpX    = w - kScrollW - kLpW;
    const int kRelX   = kLpX   - kColGap - kRelW;
    const int kVolX   = kRelX  - kColGap - kVolW;
    const int kPanX   = kVolX  - kColGap - kPanW;
    const int kPitchX = kPanX  - kColGap - kPitchW;
    const int kRootX  = kPitchX- kColGap - kRootW;
    const int kHiKeyX = kRootX - kColGap - kHiKeyW;
    const int kLoKeyX = kHiKeyX- kColGap - kLoKeyW;

    // NAME column fills everything left of loKey
    const int kNameColW = kLoKeyX - kColGap;
    const int kNameX    = kStripeW + 2;
    const int kNameW    = kNameColW - kStripeW - 4;

    // ── Background ────────────────────────────────────────────────────────────
    g.setColour (theme.darkBar.darker (0.55f));
    g.fillRect (0, 0, w, h);

    if (rows.empty())
    {
        if (addZoneBtnVisible)
        {
            // Draw the [+ ZONE] button in the single placeholder row so the
            // user can build a new SFZ from scratch without loading a file first.
            constexpr int kBtnW = 70;
            constexpr int kBtnH = 16;
            const int bx = 6;
            const int by = (h - kBtnH) / 2;
            const juce::Rectangle<int> btnRect (bx, by, kBtnW, kBtnH);

            g.setColour (theme.accent.withAlpha (0.22f));
            g.fillRoundedRectangle (btnRect.toFloat(), 2.5f);
            g.setColour (theme.accent);
            g.drawRoundedRectangle (btnRect.toFloat().reduced (0.5f), 2.5f, 0.8f);
            g.setFont (DysektLookAndFeel::makeFont (9.5f, true));
            g.setColour (theme.accent);
            g.drawText ("+ ZONE", btnRect, juce::Justification::centred, false);
        }
        else
        {
            g.setFont (DysektLookAndFeel::makeFont (11.0f));
            g.setColour (theme.foreground.withAlpha (0.18f));
            g.drawText ("No zones loaded", 0, 0, w, h, juce::Justification::centred, false);
        }
        return;
    }

    // ── SF2 preset-list mode: simplified full-width name list ─────────────────
    if (sf2PresetListMode)
    {
        // Column header
        g.setColour (theme.darkBar.darker (0.25f));
        g.fillRect (0, 0, w, kHeaderH);
        g.setFont (DysektLookAndFeel::makeFont (9.0f, true));
        g.setColour (theme.foreground.withAlpha (0.30f));
        g.drawText ("BANK / PRESET  \xe2\x80\x94  right-click row to assign MIDI channel",
                    10, 0, w - 20, kHeaderH,
                    juce::Justification::centredLeft, false);
        g.setColour (theme.separator.withAlpha (0.45f));
        g.drawHorizontalLine (kHeaderH - 1, 0.f, (float) w);

        constexpr int kStripeW = 4;
        const juce::Font fMain = DysektLookAndFeel::makeFont (13.0f);

        for (int i = 0; i < (int) rows.size(); ++i)
        {
            const auto& r  = rows[(size_t) i];
            const int   ry = kHeaderH + i * kRowH;
            const bool  sel = (i == selectedRow);
            const juce::Colour zc = r.zone.colour;

            // Row stripe
            if (i % 2 == 1)
            {
                g.setColour (juce::Colour (0xFF000000).withAlpha (0.10f));
                g.fillRect (0, ry, w, kRowH);
            }
            if (sel)
            {
                g.setColour (theme.accent.withAlpha (0.12f));
                g.fillRect (kStripeW, ry, w - kStripeW, kRowH);
            }

            // Colour stripe
            g.setColour (zc);
            g.fillRect (0, ry, kStripeW, kRowH);

            // MIDI channel badge (right side) — only when a channel is assigned
            const int assignedCh = r.zone.assignedMidiChannel;
            int nameTrimRight = 16;
            if (assignedCh >= 1 && assignedCh <= 16)
            {
                const juce::String chLabel    = "CH " + juce::String (assignedCh);
                const juce::Font   badgeFont  = DysektLookAndFeel::makeFont (9.0f, true);
                const int          badgeW     = 34;
                const int          badgeH     = 17;
                const int          badgeX     = w - badgeW - 6;
                const int          badgeY     = ry + (kRowH - badgeH) / 2;

                // Badge background
                g.setColour (theme.accent.withAlpha (0.75f));
                g.fillRoundedRectangle ((float) badgeX, (float) badgeY,
                                        (float) badgeW, (float) badgeH, 3.0f);
                // Badge text
                g.setFont (badgeFont);
                g.setColour (juce::Colours::white.withAlpha (0.92f));
                g.drawText (chLabel, badgeX, badgeY, badgeW, badgeH,
                            juce::Justification::centred, false);

                nameTrimRight = badgeW + 10;
            }

            // Name
            g.setFont (fMain);
            g.setColour (sel ? theme.foreground : theme.foreground.withAlpha (0.82f));
            g.drawText (r.zone.name, kStripeW + 6, ry, w - kStripeW - nameTrimRight, kRowH,
                        juce::Justification::centredLeft, true);

            // Row separator
            g.setColour (theme.separator.withAlpha (0.12f));
            g.drawHorizontalLine (ry + kRowH - 1, 0.f, (float) w);
        }

        if (rows.empty())
        {
            g.setFont (DysektLookAndFeel::makeFont (11.0f));
            g.setColour (theme.foreground.withAlpha (0.18f));
            g.drawText ("No presets loaded", 0, kHeaderH, w, h - kHeaderH,
                        juce::Justification::centred, false);
        }
        return;
    }
    // ─────────────────────────────────────────────────────────────────────────
    g.setColour (theme.darkBar.darker (0.25f));
    g.fillRect (0, 0, w, kHeaderH);

    g.setColour (juce::Colours::white.withAlpha (0.02f));
    g.fillRect (0, 0, kNameColW, kHeaderH);

    g.setFont (DysektLookAndFeel::makeFont (9.0f, true));
    g.setColour (theme.foreground.withAlpha (0.30f));

    auto hdr = [&] (const char* txt, int x, int cw,
                    juce::Justification j = juce::Justification::centred)
    {
        g.drawText (txt, x, 0, cw, kHeaderH, j, false);
    };

    hdr ("SAMPLE",      kNameX,   kNameW,   juce::Justification::centredLeft);
    hdr ("loKey",       kLoKeyX,  kLoKeyW);
    hdr ("hiKey",       kHiKeyX,  kHiKeyW);
    hdr ("root",        kRootX,   kRootW);
    hdr ("pitch",       kPitchX,  kPitchW);
    hdr ("pan",         kPanX,    kPanW);
    hdr ("vol (dB)",    kVolX,    kVolW);
    hdr ("release (s)", kRelX,    kRelW);
    hdr ("LP",          kLpX,     kLpW);

    // SFZ: draw up-down drag hint on editable numeric columns
    if (sfzEditable)
    {
        g.setFont (DysektLookAndFeel::makeFont (8.5f));
        g.setColour (theme.accent.withAlpha (0.40f));
        for (int cx : { kLoKeyX + kLoKeyW - 10,
                        kHiKeyX + kHiKeyW - 10,
                        kRootX  + kRootW  - 10,
                        kPitchX + kPitchW - 10,
                        kPanX   + kPanW   - 10,
                        kVolX   + kVolW   - 10,
                        kRelX   + kRelW   - 10 })
            g.drawText (u8"\u2195", cx, 0, 10, kHeaderH, juce::Justification::centred, false);
    }

    g.setColour (theme.separator.withAlpha (0.45f));
    g.drawHorizontalLine (kHeaderH - 1, 0.f, (float) w);

    g.setColour (theme.separator.withAlpha (0.20f));
    g.drawVerticalLine (kNameColW - 1, 0.f, (float) kHeaderH);

    // ── [+ ZONE] add-zone button — drawn as a dedicated strip below the last row ──
    // When no zones exist it occupies the single placeholder row; when zones are
    // present it appears as an extra row pinned below the last one.
    if (addZoneBtnVisible)
    {
        const int btnRowY = kHeaderH + (int) rows.size() * kRowH;

        // Subtle row background so it reads as a distinct "action" slot
        g.setColour (theme.accent.withAlpha (0.06f));
        g.fillRect (0, btnRowY, w, kRowH);

        // Top border to visually separate from data rows
        g.setColour (theme.accent.withAlpha (0.28f));
        g.drawHorizontalLine (btnRowY, 0.f, (float) w);

        // Pill button left-aligned in the row
        constexpr int kBtnW = 70;
        constexpr int kBtnH = 16;
        const int bx = 6;
        const int by = btnRowY + (kRowH - kBtnH) / 2;
        const juce::Rectangle<int> btnRect (bx, by, kBtnW, kBtnH);

        g.setColour (theme.accent.withAlpha (0.20f));
        g.fillRoundedRectangle (btnRect.toFloat(), 3.0f);

        g.setColour (theme.accent.withAlpha (0.65f));
        g.drawRoundedRectangle (btnRect.toFloat().reduced (0.5f), 3.0f, 0.75f);

        g.setFont (DysektLookAndFeel::makeFont (8.5f, true));
        g.setColour (theme.accent.brighter (0.5f));
        g.drawText ("+ ZONE", btnRect, juce::Justification::centred, false);

        // Hint text showing next zone\'s key range
        if (! rows.empty())
        {
            const int lastHi = rows.back().zone.hiKey;
            const int nextLo = juce::jmin (lastHi + 1, 127);
            const int nextHi = juce::jmin (lastHi + 12, 127);
            auto noteName = [] (int n) -> juce::String {
                static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
                return juce::String (names[n % 12]) + juce::String (n / 12 - 2);
            };
            const juce::String hint = noteName (nextLo) + " \u2013 " + noteName (nextHi);
            g.setFont (DysektLookAndFeel::makeFont (9.0f));
            g.setColour (theme.foreground.withAlpha (0.30f));
            g.drawText (hint, bx + kBtnW + 6, btnRowY, 80, kRowH,
                        juce::Justification::centredLeft, false);
        }
    }
    // ── Helper: small padlock for SF2 read-only columns ───────────────────────
    // Renders a 🔒-style padlock via an inline SVG drawable.
    static const char* kLockSvg =
        "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 14 16'>"
        "  <rect x='1' y='7' width='12' height='9' rx='2' ry='2' fill='#ffffff'/>"
        "  <path d='M3.5 7 V4.5 A3.5 3.5 0 0 1 10.5 4.5 V7' "
        "        fill='none' stroke='#ffffff' stroke-width='2'"
        "        stroke-linecap='round'/>"
        "</svg>";

    auto lockDrawable = std::unique_ptr<juce::Drawable> (
        juce::Drawable::createFromSVG (*juce::XmlDocument::parse (kLockSvg)));
    if (lockDrawable)
        lockDrawable->replaceColour (juce::Colours::white,
                                     theme.foreground.withAlpha (0.65f));

    auto drawLock = [&] (int cx, int cy)
    {
        if (! lockDrawable) return;
        const int sz = 10;
        lockDrawable->drawWithin (g,
                                  juce::Rectangle<float> ((float)(cx - sz / 2),
                                                           (float)(cy - sz / 2),
                                                           (float) sz, (float) sz),
                                  juce::RectanglePlacement::centred, 1.0f);
    };

    // ── Note name helper ──────────────────────────────────────────────────────
    auto noteName = [] (int note) -> juce::String
    {
        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        return juce::String (names[note % 12]) + juce::String (note / 12 - 2);
    };

    // ── Rows ──────────────────────────────────────────────────────────────────
    const juce::Font fMain  = DysektLookAndFeel::makeFont (13.0f);
    const juce::Font fSmall = DysektLookAndFeel::makeFont (11.0f);
    const juce::Font fTiny  = DysektLookAndFeel::makeFont (9.5f);

    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const auto& r    = rows[(size_t) i];
        const int   ry   = kHeaderH + i * kRowH;
        const bool  sel  = (i == selectedRow);
        const juce::Colour zc = r.zone.colour;
        // A column is editable only for SFZ zones
        const bool  editable  = sfzEditable && r.zone.isSfz;

        // ── Row base ──────────────────────────────────────────────────────────
        if (i % 2 == 1)
        {
            g.setColour (juce::Colour (0xFF000000).withAlpha (0.10f));
            g.fillRect (0, ry, w, kRowH);
        }
        if (sel)
        {
            g.setColour (theme.accent.withAlpha (0.08f));
            g.fillRect (kStripeW, ry, w - kStripeW, kRowH);
        }

        // ── Colour stripe ─────────────────────────────────────────────────────
        g.setColour (zc);
        g.fillRect (0, ry, kStripeW, kRowH);

        // ── Name column ───────────────────────────────────────────────────────
        g.setColour (zc.withAlpha (0.08f));
        g.fillRect (kStripeW, ry, kNameColW - kStripeW, kRowH);

        g.setFont (fMain);
        g.setColour (sel ? theme.foreground : theme.foreground.withAlpha (0.82f));
        g.drawText (r.zone.name, kNameX, ry, kNameW, kRowH,
                    juce::Justification::centredLeft, true);

        // ── Helper macros for a data cell ─────────────────────────────────────
        // dragHighlight: fill cell if being dragged
        auto cellBg = [&] (EditCol ec, int cx, int cw)
        {
            if (editable && i == dragRow && dragCol == ec)
            {
                g.setColour (theme.accent.withAlpha (0.18f));
                g.fillRect (cx, ry, cw, kRowH);
            }
        };
        // drawCell: text + optional lock icon for read-only
        auto drawCell = [&] (const juce::String& txt, int cx, int cw, bool lock)
        {
            const int textW = lock ? cw - 12 : cw;
            g.drawText (txt, cx, ry, textW, kRowH, juce::Justification::centred, false);
            if (lock) drawLock (cx + cw - 7, ry + kRowH / 2);
        };

        // ── loKey ─────────────────────────────────────────────────────────────
        cellBg (EditCol::LoKey, kLoKeyX, kLoKeyW);
        g.setFont (fSmall);
        g.setColour (editable ? theme.foreground.withAlpha (0.88f) : theme.foreground.withAlpha (0.70f));
        drawCell (noteName (r.zone.loKey), kLoKeyX, kLoKeyW, !editable);

        // ── hiKey ─────────────────────────────────────────────────────────────
        cellBg (EditCol::HiKey, kHiKeyX, kHiKeyW);
        g.setColour (editable ? theme.foreground.withAlpha (0.88f) : theme.foreground.withAlpha (0.70f));
        drawCell (noteName (r.zone.hiKey), kHiKeyX, kHiKeyW, !editable);

        // ── root ──────────────────────────────────────────────────────────────
        cellBg (EditCol::Root, kRootX, kRootW);
        g.setColour (editable ? theme.foreground.withAlpha (0.80f) : theme.foreground.withAlpha (0.60f));
        drawCell ((r.zone.rootPitch >= 0) ? noteName (r.zone.rootPitch) : juce::String ("--"),
                  kRootX, kRootW, !editable);

        // ── pitch (tuneCents) ─────────────────────────────────────────────────
        cellBg (EditCol::Pitch, kPitchX, kPitchW);
        g.setFont (fTiny);
        g.setColour (editable ? theme.foreground.withAlpha (0.80f) : theme.foreground.withAlpha (0.60f));
        {
            const juce::String pitchTxt = (r.zone.tuneCents >= 0 ? "+" : "")
                                          + juce::String ((int) r.zone.tuneCents) + "c";
            drawCell (pitchTxt, kPitchX, kPitchW, !editable);
        }

        // ── pan ───────────────────────────────────────────────────────────────
        cellBg (EditCol::Pan, kPanX, kPanW);
        g.setColour (editable ? theme.foreground.withAlpha (0.80f) : theme.foreground.withAlpha (0.60f));
        {
            const float pPct = r.zone.pan * 100.f;
            juce::String panTxt;
            if (std::abs (pPct) < 1.f) panTxt = "C";
            else if (pPct < 0.f)       panTxt = "L" + juce::String ((int)(-pPct));
            else                        panTxt = "R" + juce::String ((int)(pPct));
            drawCell (panTxt, kPanX, kPanW, !editable);
        }

        // ── vol (dB) ──────────────────────────────────────────────────────────
        cellBg (EditCol::Vol, kVolX, kVolW);
        g.setColour (editable ? theme.foreground.withAlpha (0.80f) : theme.foreground.withAlpha (0.60f));
        drawCell (juce::String (r.zone.volDb, 1) + "dB", kVolX, kVolW, !editable);

        // ── release (s) ───────────────────────────────────────────────────────
        cellBg (EditCol::Release, kRelX, kRelW);
        g.setColour (editable ? theme.foreground.withAlpha (0.80f) : theme.foreground.withAlpha (0.60f));
        drawCell (juce::String (r.zone.releaseSec, 3), kRelX, kRelW, !editable);

        // ── LP (loop toggle) ──────────────────────────────────────────────────
        {
            const int cx = kLpX + kLpW / 2;
            const int cy = ry + kRowH / 2;
            if (r.zone.isLooped)
            {
                g.setColour (theme.accent.withAlpha (0.70f));
                g.fillEllipse ((float) cx - 3.f, (float) cy - 3.f, 6.f, 6.f);
            }
            else
            {
                g.setColour (theme.foreground.withAlpha (editable ? 0.15f : 0.08f));
                g.drawEllipse ((float) cx - 3.f, (float) cy - 3.f, 6.f, 6.f, 1.f);
                if (! editable) drawLock (cx + 5, cy);
            }
        }

        // ── Row separator ─────────────────────────────────────────────────────
        g.setColour (theme.separator.withAlpha (0.12f));
        g.drawHorizontalLine (ry + kRowH - 1, 0.f, (float) w);
    }

    // ── Column separator lines ────────────────────────────────────────────────
    g.setColour (theme.separator.withAlpha (0.18f));
    const int sepLines[] = { kNameColW - 1,
                             kLoKeyX + kLoKeyW, kHiKeyX + kHiKeyW,
                             kRootX + kRootW,   kPitchX + kPitchW,
                             kPanX + kPanW,     kVolX + kVolW,
                             kRelX + kRelW,     kLpX - 1 };
    for (int cx : sepLines)
        g.drawVerticalLine (cx, (float) kHeaderH, (float) h);
}

void KeysPanel::ZoneMatrixContent::highlightNote (int note)
{
    if (note < 0)
        return;   // don't clear selection on note-off — keep last highlighted

    // Find first row whose key range covers this note.
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        const auto& z = rows[(size_t) i].zone;
        if (note >= z.loKey && note <= z.hiKey)
        {
            if (selectedRow == i) return;   // already there — skip scroll + repaint
            selectedRow = i;

            // Ask the parent Viewport to reveal this row.
            // ZoneMatrixContent is the viewedComponent; the viewport is its parent.
            if (auto* vp = findParentComponentOfClass<juce::Viewport>())
            {
                const int rowY = kHeaderH + i * kRowH;
                vp->setViewPosition (0, juce::jmax (0, rowY - vp->getHeight() / 2));
            }
            repaint();
            return;
        }
    }
}

// =============================================================================
// KeysPanel::highlightNoteInMatrix
// =============================================================================

void KeysPanel::highlightNoteInMatrix (int note)
{
    zoneMatrix.highlightNote (note);
}



void KeysPanel::ZoneMatrixContent::mouseDown (const juce::MouseEvent& e)
{
    // ── [+ ZONE] button hit test ──────────────────────────────────────────────
    if (addZoneBtnVisible)
    {
        if (rows.empty())
        {
            // In the empty state the whole component is one clickable zone —
            // there are no data rows or header to avoid.
            if (onAddZoneClicked)
                onAddZoneClicked();
            return;
        }

        const int btnRowY = kHeaderH + (int) rows.size() * kRowH;
        if (e.y >= btnRowY)
        {
            if (onAddZoneClicked)
                onAddZoneClicked();
            return;
        }
    }

    if (e.y < kHeaderH) return;

    const int clickedRow = (e.y - kHeaderH) / kRowH;
    if (clickedRow < 0 || clickedRow >= (int) rows.size()) return;

    // Right-click: fire the right-click callback (e.g. MIDI channel picker) and stop.
    if (e.mods.isRightButtonDown())
    {
        if (onRowRightClicked)
            onRowRightClicked (clickedRow, e.getScreenPosition());
        return;
    }

    selectedRow = clickedRow;
    repaint();

    // Notify the owner that a row was clicked (used e.g. for SF2 preset selection).
    if (onRowClicked)
        onRowClicked (clickedRow);

    // ── SFZ edit mode: check if click lands on an editable column ─────────────
    if (sfzEditable)
    {
        const EditCol col = hitTestCol (e.x, getWidth());

        if (col == EditCol::Loop)
        {
            // Toggle immediately on click — no drag needed
            rows[(size_t) clickedRow].zone.isLooped ^= true;
            repaint();
            if (onZoneEdited)
                onZoneEdited (clickedRow, rows[(size_t) clickedRow].zone);
            return;
        }

        if (col != EditCol::None)
        {
            dragCol = col;
            dragRow = clickedRow;
            dragStartY = e.getPosition().y;
            const auto& z = rows[(size_t) clickedRow].zone;
            switch (col)
            {
                case EditCol::LoKey:   dragStartVal = (float) z.loKey;                    break;
                case EditCol::HiKey:   dragStartVal = (float) z.hiKey;                    break;
                case EditCol::LoVel:   dragStartVal = (float) z.loVel;                    break;
                case EditCol::HiVel:   dragStartVal = (float) z.hiVel;                    break;
                case EditCol::Root:    dragStartVal = (float) juce::jmax (0, z.rootPitch); break;
                case EditCol::Pitch:   dragStartVal = z.tuneCents;                        break;
                case EditCol::Pan:     dragStartVal = z.pan * 100.f;                      break;
                case EditCol::Vol:     dragStartVal = z.volDb;                            break;
                case EditCol::Release: dragStartVal = z.releaseSec;                       break;
                default:               dragStartVal = 0.f;                                break;
            }
            return;  // don't audition when starting a drag-edit
        }
    }

    // ── Audition: send note-on for this zone's loKey ──────────────────────────
    // Suppressed in SF2 preset-list mode — row clicks switch presets, not notes.
    if (sf2PresetListMode) return;

    dragCol = EditCol::None;
    dragRow = -1;

    const int note = rows[(size_t) clickedRow].zone.loKey;
    owner.uiNoteOnAtomic().store (note, std::memory_order_relaxed);
    owner.scheduleNoteOff (note);
}

// =============================================================================
// ZoneMatrixContent — column hit-test helper
// =============================================================================

KeysPanel::ZoneMatrixContent::EditCol
KeysPanel::ZoneMatrixContent::hitTestCol (int x, int w) const
{
    // Must mirror paint() geometry exactly
    constexpr int kColGap   = 4;
    constexpr int kScrollW  = 10;
    constexpr int kLpW      = 30;
    constexpr int kRelW     = 70;
    constexpr int kVolW     = 62;
    constexpr int kPanW     = 52;
    constexpr int kPitchW   = 52;
    constexpr int kRootW    = 52;
    constexpr int kHiKeyW   = 52;
    constexpr int kLoKeyW   = 52;

    const int kLpX    = w - kScrollW - kLpW;
    const int kRelX   = kLpX   - kColGap - kRelW;
    const int kVolX   = kRelX  - kColGap - kVolW;
    const int kPanX   = kVolX  - kColGap - kPanW;
    const int kPitchX = kPanX  - kColGap - kPitchW;
    const int kRootX  = kPitchX- kColGap - kRootW;
    const int kHiKeyX = kRootX - kColGap - kHiKeyW;
    const int kLoKeyX = kHiKeyX- kColGap - kLoKeyW;

    if (x >= kLpX)                              return EditCol::Loop;
    if (x >= kRelX   && x < kRelX  + kRelW)    return EditCol::Release;
    if (x >= kVolX   && x < kVolX  + kVolW)    return EditCol::Vol;
    if (x >= kPanX   && x < kPanX  + kPanW)    return EditCol::Pan;
    if (x >= kPitchX && x < kPitchX+ kPitchW)  return EditCol::Pitch;
    if (x >= kRootX  && x < kRootX + kRootW)   return EditCol::Root;
    if (x >= kHiKeyX && x < kHiKeyX+ kHiKeyW)  return EditCol::HiKey;
    if (x >= kLoKeyX && x < kLoKeyX+ kLoKeyW)  return EditCol::LoKey;

    return EditCol::None;
}

// =============================================================================
// ZoneMatrixContent — mouse events
// =============================================================================

void KeysPanel::ZoneMatrixContent::mouseMove (const juce::MouseEvent& e)
{
    if (! sfzEditable)
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        return;
    }

    // Add Zone button area
    if (addZoneBtnVisible && ! rows.empty())
    {
        const int btnRowY = kHeaderH + (int) rows.size() * kRowH;
        if (e.y >= btnRowY)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            return;
        }
    }

    // Header row
    if (e.y < kHeaderH)
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        return;
    }

    // Data rows — show resize cursor only over editable columns
    const EditCol col = hitTestCol (e.x, getWidth());
    if (col == EditCol::None || col == EditCol::Loop)
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void KeysPanel::ZoneMatrixContent::mouseExit (const juce::MouseEvent&)
{
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void KeysPanel::ZoneMatrixContent::mouseDrag (const juce::MouseEvent& e)
{
    if (! sfzEditable || dragCol == EditCol::None || dragRow < 0
        || dragRow >= (int) rows.size())
        return;

    auto& zone = rows[(size_t) dragRow].zone;

    if (dragCol == EditCol::Loop)
        return;  // loop is toggled on click, not dragged

    // drag up = increase value; 1 px = 1 unit for ints, scaled for floats
    const float delta = (float)(dragStartY - e.getPosition().y);

    switch (dragCol)
    {
        case EditCol::LoKey:
            zone.loKey = juce::jmin (juce::jlimit (0, 127, (int)(dragStartVal + delta)), zone.hiKey);
            break;
        case EditCol::HiKey:
            zone.hiKey = juce::jmax (juce::jlimit (0, 127, (int)(dragStartVal + delta)), zone.loKey);
            break;
        case EditCol::LoVel:
            zone.loVel = juce::jmin (juce::jlimit (0, 127, (int)(dragStartVal + delta)), zone.hiVel);
            break;
        case EditCol::HiVel:
            zone.hiVel = juce::jmax (juce::jlimit (0, 127, (int)(dragStartVal + delta)), zone.loVel);
            break;
        case EditCol::Root:
            zone.rootPitch = juce::jlimit (0, 127, (int)(dragStartVal + delta));
            break;
        case EditCol::Pitch:   // 1 px = 1 cent
            zone.tuneCents = juce::jlimit (-100.f, 100.f, dragStartVal + delta);
            break;
        case EditCol::Pan:     // 1 px = 1% pan; stored as -1..+1
            zone.pan = juce::jlimit (-1.f, 1.f, (dragStartVal + delta) / 100.f);
            break;
        case EditCol::Vol:     // 1 px = 0.5 dB
            zone.volDb = juce::jlimit (-60.f, 12.f, dragStartVal + delta * 0.5f);
            break;
        case EditCol::Release: // 1 px = 0.01 s
            zone.releaseSec = juce::jlimit (0.f, 60.f, dragStartVal + delta * 0.01f);
            break;
        default: break;
    }

    repaint();
}

void KeysPanel::ZoneMatrixContent::mouseUp (const juce::MouseEvent&)
{
    if (sfzEditable && dragCol != EditCol::None && dragRow >= 0
        && dragRow < (int) rows.size())
    {
        // Commit the edit — notify SfzDropdownPanel to write back to the file
        if (onZoneEdited)
            onZoneEdited (dragRow, rows[(size_t) dragRow].zone);
    }

    dragCol = EditCol::None;
    dragRow = -1;
}


// =============================================================================
// KeysPanel::setSfzEditable
// =============================================================================

void KeysPanel::setSfzEditable (bool editable)
{
    zoneMatrix.sfzEditable = editable;
    // Cursor is set dynamically in ZoneMatrixContent::mouseMove — not globally here.
    // Forward zone-edit events from the matrix up to whoever owns KeysPanel
    zoneMatrix.onZoneEdited = [this] (int rowIndex, const Keyzone& z)
    {
        if (onZoneEdited)
            onZoneEdited (rowIndex, z);
        if (onZoneChanged)
            onZoneChanged (rowIndex, z.volDb, z.pan, z.tuneCents);
    };
}

void KeysPanel::setSf2PresetListMode (bool enabled)
{
    zoneMatrix.sf2PresetListMode = enabled;
    // In preset-list mode the matrix height is determined purely by the number
    // of preset rows, so rebuild immediately with the current keyzones.
    rebuildZoneMatrix();
}

void KeysPanel::setSelectedPresetRow (int rowIndex)
{
    zoneMatrix.selectedRow = rowIndex;
    zoneMatrix.repaint();
}

void KeysPanel::setAddZoneButtonVisible (bool visible)
{
    zoneMatrix.addZoneBtnVisible = visible;
    // Wire the inner callback up to our public-facing one
    zoneMatrix.onAddZoneClicked = [this]
    {
        if (onAddZoneRequested)
            onAddZoneRequested();
    };
    // Rebuild so the matrix height recalculates immediately (extraRow logic)
    rebuildZoneMatrix();
}

// =============================================================================
// KeysPanel — engine-source atomic selection
// =============================================================================
// Picks which pair of note-request atomics / active-note bitmask this panel
// reads and writes, based on engineSource (see setEngineSource()). Every
// call site that used to reach straight for processor.sfzUiNoteOnRequest /
// processor.sfzActiveNotes now goes through these so a panel bound to
// SfzPlayer2 previews and displays that engine's notes instead of always
// showing the legacy SF-Player's.

std::atomic<int>& KeysPanel::uiNoteOnAtomic() const
{
    return engineSource == EngineSource::SfzPlayer2
         ? processor.sfz2UiNoteOnRequest
         : processor.sfzUiNoteOnRequest;
}

std::atomic<int>& KeysPanel::uiNoteOffAtomic() const
{
    return engineSource == EngineSource::SfzPlayer2
         ? processor.sfz2UiNoteOffRequest
         : processor.sfzUiNoteOffRequest;
}

std::atomic<uint64_t>* KeysPanel::activeNotesAtomics() const
{
    return engineSource == EngineSource::SfzPlayer2
         ? processor.sfz2ActiveNotes
         : processor.sfzActiveNotes;
}

// =============================================================================
// KeysPanel
// =============================================================================

KeysPanel::KeysPanel (DysektProcessor& p) : processor (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setMouseClickGrabsKeyboardFocus (false);

    // Transpose buttons kept invisible — octave-shift logic intact for future use.
    addAndMakeVisible (transposeDownBtn);
    addAndMakeVisible (transposeUpBtn);
    transposeDownBtn.setMouseCursor (juce::MouseCursor::NormalCursor);
    transposeUpBtn  .setMouseCursor (juce::MouseCursor::NormalCursor);
    transposeDownBtn.setVisible (false);
    transposeUpBtn  .setVisible (false);
    transposeDownBtn.onClick = [this] { if (baseOctave > 0) { --baseOctave; resized(); repaint(); } };
    transposeUpBtn  .onClick = [this] { if (baseOctave < 8) { ++baseOctave; resized(); repaint(); } };

    zoneViewport.setScrollBarsShown (true, false);
    zoneViewport.setViewedComponent (&zoneMatrix, false);
    zoneViewport.setScrollBarThickness (0);
    addAndMakeVisible (zoneViewport);

    startTimerHz (30);
}

KeysPanel::~KeysPanel()
{
    stopTimer();

    // If the panel is destroyed while a note is held or a pending note-off is
    // queued, force the note-off into the processor now.  Without this, the
    // note stays "active" in JUCE's MidiKeyboardState and causes a use-after-
    // free crash inside MidiKeyboardState::removeListener during plugin teardown.
    const int noteToRelease = (lastActiveNote >= 0) ? lastActiveNote
                            : (pendingNoteOff  >= 0) ? pendingNoteOff
                            : -1;
    if (noteToRelease >= 0)
    {
        uiNoteOffAtomic().store (noteToRelease, std::memory_order_relaxed);
        uiNoteOnAtomic() .store (-1,            std::memory_order_relaxed);
    }
}

// =============================================================================

void KeysPanel::scheduleNoteOff (int note)
{
    // Store the pending note-off note; the timer will send it on the next tick.
    pendingNoteOff = note;
}

void KeysPanel::setKeyzones (std::vector<Keyzone> zones)
{
    keyzones = std::move (zones);
    rebuildZoneMatrix();
    repaint();
}

void KeysPanel::clearKeyzones()
{
    keyzones.clear();
    rebuildZoneMatrix();
    repaint();
}

void KeysPanel::autoScrollToZones()
{
    // Scroll so the [+ ZONE] button strip at the bottom of the matrix is visible.
    if (zoneMatrix.addZoneBtnVisible && !keyzones.empty())
    {
        const int btnRowY = ZoneMatrixContent::kHeaderH
                          + (int) keyzones.size() * ZoneMatrixContent::kRowH;
        zoneViewport.setViewPosition (0, juce::jmax (0, btnRowY - zoneViewport.getHeight() + ZoneMatrixContent::kRowH));
    }
    repaint();
}

void KeysPanel::rebuildZoneMatrix()
{
    // Use the viewport's actual width if available, otherwise fall back to the
    // component width (happens when called before the first resized() pass,
    // e.g. after minimize/restore).
    const int vpW = (zoneViewport.isVisible() && zoneViewport.getWidth() > 0)
                  ? zoneViewport.getWidth()
                  : juce::jmax (1, getWidth());
    zoneMatrix.rebuild (keyzones, 0, vpW, 0, kWhiteKeyW, kBlackKeyW, vpW);

    // Forward row-click events from the matrix up to the KeysPanel owner.
    zoneMatrix.onRowClicked = [this] (int rowIndex)
    {
        if (onRowClicked)
            onRowClicked (rowIndex);
    };

    // Forward right-click events from the matrix up to the KeysPanel owner.
    zoneMatrix.onRowRightClicked = [this] (int rowIndex, juce::Point<int> screenPos)
    {
        if (onRowRightClicked)
            onRowRightClicked (rowIndex, screenPos);
    };
}

// =============================================================================
// resized  — full 128-note keyboard
// =============================================================================

void KeysPanel::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    kTransposeRowH = 0;

    // Key height: compact, matching the image reference (thinner keys → shorter).
    // Allow up to 50px tall, minimum 32px.
    kKeyH = juce::jlimit (32, 50, h / 3);

    constexpr int kMatrixKeyGap = 8;   // px gap between zone matrix frame and keyboard
    kZoneViewH = juce::jmax (0, h - kKeyH - kMatrixKeyGap);

    // Full keyboard: 75 white keys spanning the full component width.
    kNumWhite = kTotalWhite;   // 75
    kNumBlack = kTotalBlack;   // 53

    // Key width: fill the full component width with all 75 white keys.
    // Use float division so we can use fractional positions and avoid gaps.
    kWhiteKeyW = juce::jmax (4, w / kNumWhite);     // integer for hit-testing
    kWhiteKeyWf = (float) w / (float) kNumWhite;    // float for drawing
    kBlackKeyW  = juce::roundToInt (kWhiteKeyWf * 0.56f);
    kBlackKeyH  = juce::roundToInt ((float) kKeyH  * 0.60f);

    const int keyboardY = h - kKeyH;

    // ── Build keyRects for all 128 notes ──────────────────────────────────────
    keyRects.clear();
    keyRects.reserve (128);

    // White keys pass
    int wi = 0;   // running white-key index
    for (int note = 0; note < 128; ++note)
    {
        const int semi = note % 12;
        if (semiToWhite[semi] >= 0)
        {
            const int x = juce::roundToInt ((float) wi * kWhiteKeyWf);
            const int x2 = juce::roundToInt ((float)(wi + 1) * kWhiteKeyWf);
            KeyRect kr;
            kr.bounds  = { x, keyboardY, x2 - x - 1, kKeyH };
            kr.note    = note;
            kr.isBlack = false;
            keyRects.push_back (kr);
            ++wi;
        }
    }

    // Black keys pass — positioned at the right edge of the white key before them
    for (int note = 0; note < 128; ++note)
    {
        const int semi = note % 12;
        if (semiToWhite[semi] >= 0) continue;  // skip white keys

        // Find the white-key index of the white key just below this black key.
        // The white key below a black key at semitone s is the white key with
        // the largest note < note that is a white key.
        int prevWhite = -1;
        for (int n = note - 1; n >= 0; --n)
        {
            if (semiToWhite[n % 12] >= 0) { prevWhite = n; break; }
        }
        if (prevWhite < 0) continue;

        // The white-key slot index of prevWhite
        int pwi = whiteIndexForNote (prevWhite);
        if (pwi < 0) continue;

        // Black key sits centred on the right edge of prevWhite's slot
        const float rightEdge = (float)(pwi + 1) * kWhiteKeyWf;
        const int bx = juce::roundToInt (rightEdge - kBlackKeyW * 0.5f);

        KeyRect kr;
        kr.bounds  = { bx, keyboardY, kBlackKeyW, kBlackKeyH };
        kr.note    = note;
        kr.isBlack = true;
        keyRects.push_back (kr);
    }

    // ── Zone viewport ─────────────────────────────────────────────────────────
    if (kZoneViewH > 0)
    {
        // Inset by the LCD frame border (1px outer stroke + 2px inner screen offset = 3px).
        // This ensures rows are clipped inside the visible frame regardless of
        // whether a scrollbar is present — fixing the bleed-outside bug.
        constexpr int kFrameInset = 3;
        zoneViewport.setBounds (kFrameInset, kFrameInset,
                                w - kFrameInset * 2,
                                kZoneViewH - kFrameInset * 2);
        zoneViewport.setVisible (true);
        rebuildZoneMatrix();
    }
    else
    {
        zoneViewport.setVisible (false);
    }

    // If zones are already loaded and the viewport just got a real size,
    // rebuild so the matrix fills correctly after minimize/restore.
    if (! keyzones.empty())
        rebuildZoneMatrix();
}

// =============================================================================
// paint
// =============================================================================

void KeysPanel::paint (juce::Graphics& g)
{
    const auto& theme  = getTheme();
    const auto  accent = theme.accent;

    g.setColour (theme.darkBar.darker (0.35f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);

    // ── LCD-style frame around zone viewport ─────────────────────────────────
    if (kZoneViewH > 0)
    {
        const juce::Rectangle<float> frameRect (0.f, 0.f, (float) getWidth(), (float) kZoneViewH);

        juce::ColourGradient outerGrad (juce::Colour (0xFF131313), 0.f, 0.f,
                                        juce::Colour (0xFF0E0E0E), 0.f,
                                        (float) kZoneViewH, false);
        g.setGradientFill (outerGrad);
        g.fillRoundedRectangle (frameRect, 2.0f);

        g.setColour (accent.withAlpha (0.35f));
        g.drawRoundedRectangle (frameRect.reduced (0.5f), 2.0f, 1.0f);

        const auto screen = frameRect.reduced (2.f);
        g.setColour (theme.darkBar.darker (0.55f));
        g.fillRoundedRectangle (screen, 1.5f);

        g.setColour (juce::Colours::black.withAlpha (0.10f));
        for (int y = (int) screen.getY(); y < (int) screen.getBottom(); y += 2)
            g.drawHorizontalLine (y, screen.getX(), screen.getRight());

        juce::ColourGradient glow (accent.withAlpha (0.05f), 0.f, screen.getY(),
                                   juce::Colours::transparentBlack, 0.f,
                                   screen.getY() + 16.f, false);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (screen, 1.5f);

        g.setColour (accent.withAlpha (0.10f));
        g.drawRoundedRectangle (screen.expanded (0.5f), 1.5f, 1.0f);
    }

    // ── Keyboard ──────────────────────────────────────────────────────────────
    // Populate slicedNotes only when the slicer is the active mode.
    // When the SF-player panel is shown, slicerHighlightEnabled is set to false
    // so slicer MIDI-note assignments never produce coloured outlines on the
    // SF-player keyboard — regardless of whether keyzones have been loaded yet.
    std::set<int> slicedNotes;
    if (slicerHighlightEnabled)
    {
        const auto& ui = processor.getUiSliceSnapshot();
        for (int s = 0; s < ui.numSlices; ++s)
            slicedNotes.insert (ui.slices[(size_t) s].midiNote);
    }

    // White keys first
    for (const auto& kr : keyRects)
    {
        if (kr.isBlack) continue;
        const int n = kr.note;
        const bool midiActive = (n >= 0 && n < 128)
            ? ((n < 64 ? (sfzActiveSnap[0] >> n) : (sfzActiveSnap[1] >> (n - 64))) & 1) != 0
            : false;
        drawKey (g, kr, slicedNotes.count (n) > 0, n == hoveredNote, n == lastActiveNote || midiActive);
    }
    // Black keys on top
    for (const auto& kr : keyRects)
    {
        if (! kr.isBlack) continue;
        const int n = kr.note;
        const bool midiActive = (n >= 0 && n < 128)
            ? ((n < 64 ? (sfzActiveSnap[0] >> n) : (sfzActiveSnap[1] >> (n - 64))) & 1) != 0
            : false;
        drawKey (g, kr, slicedNotes.count (n) > 0, n == hoveredNote, n == lastActiveNote || midiActive);
    }

    // ── Root-pitch dots ───────────────────────────────────────────────────────
    for (const auto& z : keyzones)
    {
        if (z.rootPitch < 0) continue;
        const int n = z.rootPitch;
        if (n < 0 || n >= 128) continue;

        // Find the keyRect for this note
        for (const auto& kr : keyRects)
        {
            if (kr.note != n) continue;
            const auto  b   = kr.bounds.toFloat();
            const float dotX = b.getCentreX() - 2.5f;
            const float dotY = kr.isBlack ? b.getBottom() - 8.f : b.getBottom() - 9.f;
            g.setColour (z.colour.brighter (0.6f).withAlpha (0.90f));
            g.fillEllipse (dotX, dotY, 5.0f, 5.0f);
            g.setColour (juce::Colours::black.withAlpha (0.40f));
            g.drawEllipse (dotX, dotY, 5.0f, 5.0f, 0.8f);
            break;
        }
    }
}

// =============================================================================
// drawKey
// =============================================================================

void KeysPanel::drawKey (juce::Graphics& g, const KeyRect& kr,
                          bool hasSlice, bool hovered, bool active) const
{
    const auto& theme  = getTheme();
    const auto  accent = theme.accent;
    const auto  b      = kr.bounds.toFloat();

    if (! kr.isBlack)
    {
        const juce::Colour topCol = active  ? accent.withBrightness (0.92f).withAlpha (0.9f)
                                   : hovered ? juce::Colour (0xFFF4F4F4)
                                             : juce::Colour (0xFFECECEC);
        const juce::Colour botCol = active  ? accent.withBrightness (0.75f).withAlpha (0.7f)
                                   : hovered ? juce::Colour (0xFFDCDCDC)
                                             : juce::Colour (0xFFD0D0D0);

        juce::ColourGradient grad (topCol, 0.f, b.getY(), botCol, 0.f, b.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (b.reduced (0.5f, 0.f).withTrimmedBottom (-4.f), 2.0f);

        g.setColour (juce::Colour (0xFF707070).withAlpha (0.40f));
        g.fillRect  (b.getRight() - 1.0f, b.getY() + 2.0f, 1.0f, b.getHeight() - 4.0f);

        // Only draw note labels on C notes if key wide enough
        if ((kr.note % 12) == 0 && b.getWidth() >= 6.f)
        {
            g.setFont   (DysektLookAndFeel::makeFont (8.0f));
            g.setColour (hasSlice ? accent.withAlpha (0.9f) : juce::Colour (0xFF505050));
            g.drawText  (juce::MidiMessage::getMidiNoteName (kr.note, true, true, 3),
                         kr.bounds.getX(), kr.bounds.getBottom() - 13,
                         kr.bounds.getWidth(), 12, juce::Justification::centred);
        }

        if (hasSlice)
        {
            g.setColour (accent.withAlpha (0.80f));
            g.drawRoundedRectangle (b.reduced (0.8f, 0.5f), 2.0f, 1.0f);
        }
    }
    else
    {
        const juce::Colour topCol = active  ? accent.darker (0.1f)
                                   : hovered ? juce::Colour (0xFF383838)
                                             : juce::Colour (0xFF222222);
        const juce::Colour botCol = active  ? accent.darker (0.5f)
                                   : hovered ? juce::Colour (0xFF181818)
                                             : juce::Colour (0xFF0A0A0A);

        juce::ColourGradient grad (topCol, 0.f, b.getY(), botCol, 0.f, b.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (b, 1.5f);

        g.setColour (juce::Colours::white.withAlpha (active ? 0.22f : 0.09f));
        g.fillRoundedRectangle (b.getX() + 1.0f, b.getY() + 1.0f,
                                b.getWidth() - 2.0f, 3.5f, 1.0f);

        if (hasSlice)
        {
            g.setColour (accent.withAlpha (0.85f));
            g.drawRoundedRectangle (b.reduced (0.6f), 1.5f, 0.8f);
        }

        g.setColour (juce::Colours::black.withAlpha (0.60f));
        g.drawRoundedRectangle (b.expanded (0.4f), 2.0f, 0.7f);
    }
}

// =============================================================================
// Hit-testing helpers
// =============================================================================

float KeysPanel::noteToX (int note, int /*kbX*/) const
{
    for (const auto& kr : keyRects)
        if (kr.note == note)
            return (float) kr.bounds.getX();
    return 0.f;
}

float KeysPanel::noteKeyWidth (int note) const
{
    for (const auto& kr : keyRects)
        if (kr.note == note)
            return (float) kr.bounds.getWidth();
    return (float) kWhiteKeyW;
}

juce::Colour KeysPanel::zoneColourForNote (int note) const
{
    for (const auto& z : keyzones)
        if (note >= z.loKey && note <= z.hiKey)
            return z.colour;
    return juce::Colour (0);
}

// =============================================================================
// Mouse  — sticky-note fix
//
// Root cause of sticking:
//   1. ZoneMatrixContent::mouseDown set owner.lastActiveNote without an
//      associated mouseUp → note-off was never sent.  Fixed above by using
//      scheduleNoteOff() instead.
//   2. If the mouse is dragged off the component while a key is held, mouseUp
//      is never called on KeysPanel.  Fixed by also releasing in mouseExit.
//   3. mouseDrag was not implemented, so holding and moving the mouse after a
//      click would leave the old note active.
// =============================================================================

void KeysPanel::mouseDown (const juce::MouseEvent& e)
{
    // Zone viewport swallows its own clicks — skip piano hit testing.
    if (zoneViewport.isVisible() &&
        zoneViewport.getBounds().contains (e.getPosition()))
        return;

    // Release any previously held note before pressing a new one.
    releaseLastNote();

    // Black keys first (they sit on top)
    for (auto it = keyRects.rbegin(); it != keyRects.rend(); ++it)
    {
        if (it->isBlack && it->bounds.contains (e.getPosition()))
        {
            lastActiveNote = it->note;
            uiNoteOnAtomic().store (lastActiveNote, std::memory_order_relaxed);
            repaint();
            return;
        }
    }
    for (const auto& kr : keyRects)
    {
        if (! kr.isBlack && kr.bounds.contains (e.getPosition()))
        {
            lastActiveNote = kr.note;
            uiNoteOnAtomic().store (lastActiveNote, std::memory_order_relaxed);
            repaint();
            return;
        }
    }
}

void KeysPanel::mouseDrag (const juce::MouseEvent& e)
{
    // Slide to a new key while holding the mouse button.
    if (zoneViewport.isVisible() &&
        zoneViewport.getBounds().contains (e.getPosition()))
        return;

    int found = -1;
    // Black keys first
    for (auto it = keyRects.rbegin(); it != keyRects.rend(); ++it)
        if (it->isBlack && it->bounds.contains (e.getPosition())) { found = it->note; break; }

    if (found < 0)
        for (const auto& kr : keyRects)
            if (! kr.isBlack && kr.bounds.contains (e.getPosition())) { found = kr.note; break; }

    if (found >= 0 && found != lastActiveNote)
    {
        releaseLastNote();
        lastActiveNote = found;
        uiNoteOnAtomic().store (lastActiveNote, std::memory_order_relaxed);
        repaint();
    }
}

void KeysPanel::mouseUp (const juce::MouseEvent&)
{
    releaseLastNote();
}

void KeysPanel::mouseMove (const juce::MouseEvent& e)
{
    int found = -1;
    for (auto it = keyRects.rbegin(); it != keyRects.rend(); ++it)
        if (it->bounds.contains (e.getPosition())) { found = it->note; break; }
    if (found != hoveredNote) { hoveredNote = found; repaint(); }
}

void KeysPanel::mouseExit (const juce::MouseEvent&)
{
    // Release any held note if the cursor leaves the component.
    releaseLastNote();
    if (hoveredNote != -1) { hoveredNote = -1; repaint(); }
}

void KeysPanel::releaseLastNote()
{
    if (lastActiveNote >= 0)
    {
        // Try to write the note-off immediately.  If the slot already holds an
        // unconsumed value from a prior release, park this one in pendingNoteOff
        // so the timer delivers it on the next tick rather than losing it.
        int expected = -1;
        if (! uiNoteOffAtomic().compare_exchange_strong (
                expected, lastActiveNote, std::memory_order_relaxed))
            pendingNoteOff = lastActiveNote;   // retry via timer

        lastActiveNote = -1;
        repaint();
    }
}

// =============================================================================
// timerCallback
// =============================================================================

void KeysPanel::timerCallback()
{
    // Send any pending scheduled note-off (from zone-table clicks).
    // Use compare_exchange so we never clobber an unconsumed note-off that the
    // audio thread hasn't read yet — if the slot is already occupied, leave
    // pendingNoteOff intact and retry on the next timer tick.
    if (pendingNoteOff >= 0)
    {
        int expected = -1;
        if (uiNoteOffAtomic().compare_exchange_strong (
                expected, pendingNoteOff, std::memory_order_relaxed))
            pendingNoteOff = -1;
        // else: slot busy, will retry next tick
    }

    std::atomic<uint64_t>* const activeNotes = activeNotesAtomics();
    const uint64_t lo = activeNotes[0].load (std::memory_order_relaxed);
    const uint64_t hi = activeNotes[1].load (std::memory_order_relaxed);
    if (lo != sfzActiveSnap[0] || hi != sfzActiveSnap[1])
    {
        sfzActiveSnap[0] = lo;
        sfzActiveSnap[1] = hi;

        // Find the lowest active note and scroll the zone matrix to it.
        for (int n = 0; n < 128; ++n)
        {
            const uint64_t word = (n < 64) ? lo : hi;
            const int      bit  = (n < 64) ? n  : (n - 64);
            if ((word >> bit) & 1)
            {
                highlightNoteInMatrix (n);
                break;
            }
        }
    }

    // Also track UI mouse-pressed key
    if (lastActiveNote >= 0)
        highlightNoteInMatrix (lastActiveNote);

    repaint();
}


