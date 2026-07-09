#include "DualLcdControlFrame.h"
#include "DysektLookAndFeel.h"
#include "IconManager.h"
#include "../PluginProcessor.h"
#include "../params/ParamIds.h"

DualLcdControlFrame::DualLcdControlFrame (DysektProcessor& p)
    : processor (p)
{
    // No child components — all drawing is manual.
}

// ── drawIcon ──────────────────────────────────────────────────────────────────

void DualLcdControlFrame::drawIcon (juce::Graphics& g, juce::Rectangle<float> b,
                                    int type, bool active, bool hovered)
{
    const auto accent = getTheme().accent;
    const auto fg     = getTheme().foreground;

    // ── Chrome — matches DysektLookAndFeel::drawButtonBackground exactly,
    // so these icon toggles read as the same "button" as HeaderBar's real
    // juce::TextButtons (UNDO/REDO/PANIC/⚙): 2px radius, theme.button base
    // fill, accent fill+border when toggled, separator border otherwise.
    {
        auto bounds = b.reduced (0.5f);
        const float r = 2.0f;
        auto baseBg = getTheme().button;
        auto fillCol = hovered ? baseBg.brighter (0.10f)
                     : active  ? baseBg.interpolatedWith (accent, 0.18f)
                               : baseBg;

        // Sprite base layer — must match DysektLookAndFeel::drawButtonBackground's
        // idle/hover/active selection exactly, or these toggles will look like flat
        // vector rectangles next to HeaderBar's real TextButtons, which draw this
        // sprite first and the fillCol/border only as a fallback.
        auto stateDrawable = active  ? IconManager::getButtonActive()
                           : hovered ? IconManager::getButtonHover()
                                     : IconManager::getButtonIdle();

        if (stateDrawable != nullptr)
            stateDrawable->drawWithin (g, bounds, juce::RectanglePlacement::stretchToFit, 1.0f);
        else
        {
            g.setColour (fillCol);
            g.fillRoundedRectangle (bounds, r);
        }

        auto borderCol = active   ? accent.withAlpha (0.70f)
                        : hovered ? getTheme().separator.brighter (0.30f)
                                  : getTheme().separator.withAlpha (0.60f);
        g.setColour (borderCol);
        g.drawRoundedRectangle (bounds, r, 1.0f);
    }

    float cx  = b.getCentreX();
    float cy2 = b.getCentreY();
    auto  col = active ? accent : fg.withAlpha (0.85f);
    g.setColour (col);

    if (type == 0) // Folder / Browser
    {
        g.fillRoundedRectangle (cx - 8, cy2 - 3, 7, 3, 1.0f);
        g.fillRoundedRectangle (cx - 9, cy2 - 2, 18, 11, 1.5f);
    }
    else if (type == 1) // Waveform — distinct shape per waveMode
    {
        // All shapes are drawn within roughly ±9 px horizontally, ±6 px vertically
        // from (cx, cy2).  stroke weight 1.5f for consistency with the other icons.
        juce::Path p;

        switch (waveMode)
        {
            default:
            case 0: // Hard — angular zigzag (sharp peaks, no smoothing)
            {
                float pts[] = { -8,0, -6,-5, -4,5, -2,-5, 0,5, 2,-5, 4,5, 6,-5, 8,0 };
                for (int i = 0; i < 9; ++i)
                {
                    float px = cx + pts[i*2], py = cy2 + pts[i*2+1];
                    i == 0 ? p.startNewSubPath (px, py) : p.lineTo (px, py);
                }
                g.strokePath (p, juce::PathStrokeType (1.5f));
                break;
            }
            case 1: // Soft — smooth sine curve (cubic bezier approximation)
            {
                p.startNewSubPath (cx - 8.0f, cy2);
                p.cubicTo (cx - 4.0f, cy2 - 6.0f,
                           cx - 2.0f, cy2 - 6.0f,
                           cx,        cy2);
                p.cubicTo (cx + 2.0f, cy2 + 6.0f,
                           cx + 4.0f, cy2 + 6.0f,
                           cx + 8.0f, cy2);
                g.strokePath (p, juce::PathStrokeType (1.5f));
                break;
            }
            case 2: // Outline — sine drawn as hollow stroke with fill below
            {
                juce::Path wave;
                wave.startNewSubPath (cx - 8.0f, cy2);
                wave.cubicTo (cx - 4.0f, cy2 - 6.0f,
                              cx - 2.0f, cy2 - 6.0f,
                              cx,        cy2);
                wave.cubicTo (cx + 2.0f, cy2 + 6.0f,
                              cx + 4.0f, cy2 + 6.0f,
                              cx + 8.0f, cy2);
                // Close to baseline to create a fill region
                wave.lineTo (cx + 8.0f, cy2 + 1.0f);
                wave.lineTo (cx - 8.0f, cy2 + 1.0f);
                wave.closeSubPath();
                g.setColour (col.withAlpha (0.18f));
                g.fillPath (wave);
                g.setColour (col);
                // Stroke just the top curve
                juce::Path outline;
                outline.startNewSubPath (cx - 8.0f, cy2);
                outline.cubicTo (cx - 4.0f, cy2 - 6.0f,
                                 cx - 2.0f, cy2 - 6.0f,
                                 cx,        cy2);
                outline.cubicTo (cx + 2.0f, cy2 + 6.0f,
                                 cx + 4.0f, cy2 + 6.0f,
                                 cx + 8.0f, cy2);
                g.strokePath (outline, juce::PathStrokeType (1.5f));
                // Baseline
                g.drawLine (cx - 8.0f, cy2, cx + 8.0f, cy2, 0.8f);
                break;
            }
            case 3: // Rectified — all-positive humps (folded sine, both arcs go up)
            {
                p.startNewSubPath (cx - 8.0f, cy2 + 2.0f);
                p.cubicTo (cx - 6.0f, cy2 - 5.0f,
                           cx - 2.0f, cy2 - 5.0f,
                           cx,        cy2 + 2.0f);
                p.cubicTo (cx + 2.0f, cy2 - 5.0f,
                           cx + 6.0f, cy2 - 5.0f,
                           cx + 8.0f, cy2 + 2.0f);
                g.strokePath (p, juce::PathStrokeType (1.5f));
                // Baseline
                g.drawLine (cx - 8.0f, cy2 + 2.0f, cx + 8.0f, cy2 + 2.0f, 0.8f);
                break;
            }
            case 4: // Mirrored — top & bottom arcs reflected about centre line
            {
                // Top arc
                juce::Path top;
                top.startNewSubPath (cx - 8.0f, cy2);
                top.cubicTo (cx - 4.0f, cy2 - 5.0f,
                             cx + 4.0f, cy2 - 5.0f,
                             cx + 8.0f, cy2);
                g.strokePath (top, juce::PathStrokeType (1.5f));
                // Bottom arc (mirrored)
                juce::Path bot;
                bot.startNewSubPath (cx - 8.0f, cy2);
                bot.cubicTo (cx - 4.0f, cy2 + 5.0f,
                             cx + 4.0f, cy2 + 5.0f,
                             cx + 8.0f, cy2);
                g.strokePath (bot, juce::PathStrokeType (1.5f));
                // Centre line
                g.drawLine (cx - 8.0f, cy2, cx + 8.0f, cy2, 0.8f);
                break;
            }
            case 5: // Bars — vertical bar graph (5 bars, varying heights)
            {
                // Heights representative of a typical amplitude envelope
                const float barH[] = { 3.0f, 6.0f, 5.0f, 4.0f, 2.0f };
                const float barW   = 2.4f;
                const float gap    = 0.8f;
                const float totalW = 5.0f * barW + 4.0f * gap;
                float bx = cx - totalW * 0.5f;
                for (int i = 0; i < 5; ++i)
                {
                    float h = barH[i] * 1.1f;
                    g.fillRoundedRectangle (bx, cy2 - h, barW, h * 2.0f, 0.7f);
                    bx += barW + gap;
                }
                break;
            }
            case 6: // RMS — smooth stepped energy bars (wider, softer look)
            {
                const float rmsH[] = { 2.5f, 5.0f, 5.5f, 4.0f, 1.5f };
                const float barW   = 2.8f;
                const float gap    = 0.7f;
                const float totalW = 5.0f * barW + 4.0f * gap;
                float bx = cx - totalW * 0.5f;
                for (int i = 0; i < 5; ++i)
                {
                    float h = rmsH[i] * 1.1f;
                    // Draw as a wide rounded rectangle (RMS looks "fatter/smoother")
                    g.fillRoundedRectangle (bx, cy2 - h, barW, h * 2.0f, 1.2f);
                    bx += barW + gap;
                }
                // Add a subtle envelope curve on top
                {
                    juce::Path env;
                    float ex = cx - totalW * 0.5f + barW * 0.5f;
                    for (int i = 0; i < 5; ++i)
                    {
                        float ey = cy2 - rmsH[i] * 1.1f;
                        float nx = ex + barW + gap;
                        if (i == 0) env.startNewSubPath (ex, ey);
                        else        env.lineTo (ex, ey);
                        ex = nx;
                    }
                    g.setColour (col.withAlpha (0.50f));
                    g.strokePath (env, juce::PathStrokeType (0.9f));
                    g.setColour (col);
                }
                break;
            }
            case 7: // Stepped — digital staircase waveform
            {
                // One cycle: high half → low half, in 4-px wide steps
                const float stepW = 4.0f;
                const float hi    = cy2 - 4.5f;
                const float lo    = cy2 + 4.5f;
                // Steps: up-up-down-down (two steps each polarity)
                p.startNewSubPath (cx - 8.0f, hi);
                p.lineTo (cx - 4.0f, hi);
                p.lineTo (cx - 4.0f, lo);    // vertical drop at centre
                p.lineTo (cx,        lo);
                p.lineTo (cx,        hi);     // vertical rise back up
                p.lineTo (cx + 4.0f, hi);
                p.lineTo (cx + 4.0f, lo);
                p.lineTo (cx + 8.0f, lo);
                g.strokePath (p, juce::PathStrokeType (1.5f));
                break;
            }
        }
    }
    else if (type == 2) // MIDI Follow — 5-pin DIN connector
    {
        // Outer D-shell body (semicircle top, flat bottom)
        const float bW = 16.0f, bH = 10.0f;
        const float bX = cx - bW * 0.5f, bY = cy2 - 4.0f;

        // Body fill + stroke
        juce::Path body;
        body.addRoundedRectangle (bX, bY, bW, bH, 2.5f);
        g.setColour (col.withAlpha (active ? 0.18f : 0.09f));
        g.fillPath (body);
        g.setColour (col.withAlpha (active ? 0.90f : 0.55f));
        g.strokePath (body, juce::PathStrokeType (1.2f));

        // 5 pins arranged in a D-sub arc inside the body
        // Standard MIDI DIN layout: 3 on top row, 2 on bottom row
        struct Pin { float x, y; };
        const float pr = 1.5f;  // pin radius
        const float pc = active ? 0.95f : 0.60f;

        Pin pins[5] = {
            { cx - 5.0f, bY + 3.0f },   // pin 1 (top-left)
            { cx,        bY + 2.2f },   // pin 2 (top-centre)
            { cx + 5.0f, bY + 3.0f },   // pin 3 (top-right)
            { cx - 2.8f, bY + 6.5f },   // pin 4 (bottom-left)
            { cx + 2.8f, bY + 6.5f },   // pin 5 (bottom-right)
        };

        for (const auto& p : pins)
        {
            // Pin hole outline
            g.setColour (col.withAlpha (pc));
            g.drawEllipse (p.x - pr, p.y - pr, pr * 2.0f, pr * 2.0f, 1.0f);
            // Pin centre dot
            g.setColour (col.withAlpha (active ? 0.70f : 0.30f));
            g.fillEllipse (p.x - 0.7f, p.y - 0.7f, 1.4f, 1.4f);
        }

        // Small cable stub at bottom
        g.setColour (col.withAlpha (active ? 0.75f : 0.40f));
        g.drawLine (cx, bY + bH, cx, bY + bH + 3.5f, 1.5f);
        g.fillEllipse (cx - 2.0f, bY + bH + 3.0f, 4.0f, 3.0f);
    }
    else if (type == 5) // Global EQ — single bell peak with flat baseline
    {
        // Flat baseline
        g.setColour (col.withAlpha (0.45f));
        g.drawHorizontalLine (juce::roundToInt (cy2 + 2.0f), cx - 9.0f, cx + 9.0f);

        // Bell curve: rises symmetrically from baseline, peaks above centre
        juce::Path bell;
        bell.startNewSubPath (cx - 9.0f, cy2 + 2.0f);
        bell.cubicTo (cx - 6.0f, cy2 + 2.0f,
                      cx - 4.5f, cy2 - 7.5f,
                      cx,        cy2 - 7.5f);   // left half rises to peak
        bell.cubicTo (cx + 4.5f, cy2 - 7.5f,
                      cx + 6.0f, cy2 + 2.0f,
                      cx + 9.0f, cy2 + 2.0f);   // right half mirrors down

        // Subtle fill inside the bell
        juce::Path fill = bell;
        fill.lineTo (cx + 9.0f, cy2 + 2.0f);
        fill.closeSubPath();
        g.setColour (col.withAlpha (0.12f));
        g.fillPath (fill);

        // Bell stroke
        g.setColour (col);
        g.strokePath (bell, juce::PathStrokeType (1.5f,
                            juce::PathStrokeType::curved,
                            juce::PathStrokeType::rounded));

        // Small dot at the peak centre
        g.setColour (col);
        g.fillEllipse (cx - 1.8f, cy2 - 7.5f - 1.8f, 3.6f, 3.6f);
    }
    else if (type == 4) // SFZ / SF2 instrument — mini piano keys
    {
        const float keyW  = 5.0f;
        const float keyH  = 11.0f;
        const float startX = cx - keyW * 1.5f - 3.0f;
        const float keyY  = cy2 - keyH * 0.5f;

        // Three white keys
        for (int k = 0; k < 3; ++k)
        {
            float kx = startX + k * (keyW + 1.0f);
            g.setColour (active ? accent.withAlpha (0.20f) : fg.withAlpha (0.08f));
            g.fillRoundedRectangle (kx, keyY, keyW, keyH, 1.0f);
            g.setColour (col);
            g.drawRoundedRectangle (kx, keyY, keyW, keyH, 1.0f, 1.0f);
        }

        // Two black keys between white keys
        const float bkW = 3.5f, bkH = 6.5f;
        g.setColour (col.withAlpha (active ? 0.95f : 0.70f));
        g.fillRoundedRectangle (startX + keyW - bkW * 0.5f,                 keyY, bkW, bkH, 0.8f);
        g.fillRoundedRectangle (startX + keyW * 2.0f + 1.0f - bkW * 0.5f,  keyY, bkW, bkH, 0.8f);
    }
    else if (type == 6) // Sequencer — mini piano-roll grid (note blocks)
    {
        // Draw a small grid of note blocks: 3 rows, staggered lengths
        const float gridL  = cx - 9.0f;
        const float gridR  = cx + 9.0f;
        const float noteH  = 2.8f;
        const float gap2   = 1.4f;
        const float totalH = 3.0f * noteH + 2.0f * gap2;
        float rowY = cy2 - totalH * 0.5f;

        // Row data: [startFrac, endFrac] of grid width
        struct NoteRow { float x0, x1; };
        NoteRow rows[3] = {
            { 0.00f, 0.55f },  // top note: left-biased
            { 0.30f, 1.00f },  // middle note: right-biased
            { 0.10f, 0.70f },  // bottom note: centred
        };

        const float gridW = gridR - gridL;
        for (int r = 0; r < 3; ++r)
        {
            float nx = gridL + rows[r].x0 * gridW;
            float nw = (rows[r].x1 - rows[r].x0) * gridW;
            // Note fill
            g.setColour (col.withAlpha (active ? 0.85f : 0.55f));
            g.fillRoundedRectangle (nx, rowY, nw, noteH, 1.0f);
            // Note outline
            g.setColour (col);
            g.drawRoundedRectangle (nx, rowY, nw, noteH, 1.0f, 0.7f);
            rowY += noteH + gap2;
        }

        // Vertical grid line at beat 1/2 position
        g.setColour (col.withAlpha (0.25f));
        const float midX = gridL + gridW * 0.5f;
        g.drawLine (midX, cy2 - totalH * 0.5f - 1.0f, midX, cy2 + totalH * 0.5f + 1.0f, 0.7f);
    }
    else // type == 3: Mixer — three vertical faders at different positions
    {
        // Three fader grooves
        const float grooveH = 12.0f;
        const float grooveW = 2.0f;
        float gx[] = { cx - 7.0f, cx, cx + 7.0f };

        for (float x : gx)
        {
            g.fillRoundedRectangle (x - grooveW / 2, cy2 - grooveH / 2,
                                    grooveW, grooveH, 1.0f);
        }

        // Three thumbs at different heights (classic mixer look)
        float thumbY[] = { cy2 - 3.0f, cy2 + 1.0f, cy2 - 6.0f };
        const float thumbW = 7.0f, thumbH = 4.0f;

        g.setColour (active ? accent.brighter (0.3f) : fg.withAlpha (0.90f));
        for (int i = 0; i < 3; ++i)
            g.fillRoundedRectangle (gx[i] - thumbW / 2, thumbY[i], thumbW, thumbH, 1.5f);
    }
}

// ── paint ─────────────────────────────────────────────────────────────────────

void DualLcdControlFrame::paint (juce::Graphics& g)
{
    const auto  accent = getTheme().accent;
    const auto  fg     = getTheme().foreground;
    const int   w      = getWidth();
    const int   h      = getHeight();
    const int   half   = h / 2;
    const float sf     = (float) w / 200.0f;   // scale relative to kCtrlFrameW=200
    auto si = [sf](float v) -> int { return juce::roundToInt (v * sf); };
    auto sf1 = [sf](float v) -> float { return v * sf; };

    // ── Background + border ───────────────────────────────────────────────────
    {
        auto bgTop = getTheme().darkBar.darker (0.45f);
        auto bgBot = getTheme().darkBar.darker (0.65f);
        juce::ColourGradient grad (bgTop, 0, 0, bgBot, 0, (float) h, false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        g.setColour (accent.withAlpha (0.18f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().expanded (1.0f), 5.0f, 1.0f);
        g.setColour (accent.withAlpha (0.60f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.5f);
    }

    // ── Divider ───────────────────────────────────────────────────────────────
    g.setColour (accent.withAlpha (0.25f));
    g.drawHorizontalLine (half, 6.0f, (float) w - 6.0f);

    // ── EDIT | PAD mode tab strip (centred on divider) ────────────────────────
    {
        const int tabH    = si (17);
        const int tabW    = si (50);
        const int tabGap  = si (3);
        const int totalTW = tabW * 3 + tabGap * 2;
        const int tabX    = (w - totalTW) / 2;
        const int tabY    = half - tabH / 2;

        editTabArea      = { tabX,                          tabY, tabW, tabH };
        padTabArea       = { tabX + tabW + tabGap,          tabY, tabW, tabH };
        sfzPlayerTabArea = { tabX + (tabW + tabGap) * 2,    tabY, tabW, tabH };

        // Erase the divider line behind the tabs so they float cleanly
        {
            auto bgTop = getTheme().darkBar.darker (0.45f);
            auto bgBot = getTheme().darkBar.darker (0.65f);
            // Sample approx mid-gradient colour at divider
            auto bgMid = bgTop.interpolatedWith (bgBot, 0.5f);
            g.setColour (bgMid);
            g.fillRect (tabX - 2, tabY, totalTW + 4, tabH);
        }

        auto drawTab = [&] (juce::Rectangle<int> r, const char* label, bool active)
        {
            // Same chrome formula as DysektLookAndFeel::drawButtonBackground
            // (HeaderBar's UNDO/REDO/PANIC buttons): 2px radius, theme.button
            // base fill, accent fill+border when active/toggled.
            juce::Rectangle<float> rf = r.toFloat().reduced (0.5f);
            const float r2 = 2.0f;
            auto baseBg  = getTheme().button;
            auto fillCol = active ? baseBg.interpolatedWith (accent, 0.18f) : baseBg;

            // Sprite base layer — see drawIcon() above for why this must mirror
            // DysektLookAndFeel::drawButtonBackground's idle/active sprite choice
            // rather than only the flat-tint fallback.
            auto stateDrawable = active ? IconManager::getButtonActive()
                                         : IconManager::getButtonIdle();

            if (stateDrawable != nullptr)
                stateDrawable->drawWithin (g, rf, juce::RectanglePlacement::stretchToFit, 1.0f);
            else
            {
                g.setColour (fillCol);
                g.fillRoundedRectangle (rf, r2);
            }

            auto borderCol = active ? accent.withAlpha (0.70f)
                                     : getTheme().separator.withAlpha (0.60f);
            g.setColour (borderCol);
            g.drawRoundedRectangle (rf, r2, 1.0f);

            g.setColour (active ? accent : fg.withAlpha (0.85f));
            g.setFont (DysektLookAndFeel::makeFont (sf1 (8.5f), true));
            g.drawText (label, r, juce::Justification::centred);
        };

        drawTab (editTabArea,      "SLICER",      uiTab == 0);
        drawTab (padTabArea,       "SFZ-PLAYER",  uiTab == 1);
        drawTab (sfzPlayerTabArea, "SF2-PLAYER",  uiTab == 2);
    }

    // ── Top row: five icons evenly spread across full width ─────────────────
    {
        const int btnSz  = si (26);
        const int btnY   = (half - btnSz) / 2;
#if DYSEKT_STANDALONE
        const int gap    = (w - 5 * btnSz) / 6;
        filIconArea        = { gap,                       btnY, btnSz, btnSz };
        waIconArea         = { gap * 2 + btnSz,           btnY, btnSz, btnSz };
        midiFollowIconArea = { gap * 3 + btnSz * 2,       btnY, btnSz, btnSz };
        bodeIconArea       = { gap * 4 + btnSz * 3,       btnY, btnSz, btnSz };
        seqIconArea        = { gap * 5 + btnSz * 4,       btnY, btnSz, btnSz };
#else
        const int gap    = (w - 4 * btnSz) / 5;
        filIconArea        = { gap,                       btnY, btnSz, btnSz };
        waIconArea         = { gap * 2 + btnSz,           btnY, btnSz, btnSz };
        midiFollowIconArea = { gap * 3 + btnSz * 2,       btnY, btnSz, btnSz };
        bodeIconArea       = { gap * 4 + btnSz * 3,       btnY, btnSz, btnSz };
        seqIconArea        = {};   // hidden in VST3
#endif
        eqIconArea         = {};
        sfzIconArea        = {};

        drawIcon (g, filIconArea       .toFloat(), 0, browserActive,    hoveredIcon == 0);
        drawIcon (g, waIconArea        .toFloat(), 1, waveMode != 0,    hoveredIcon == 1);
        drawIcon (g, midiFollowIconArea.toFloat(), 2, midiFollowActive, hoveredIcon == 2);
        drawIcon (g, bodeIconArea      .toFloat(), 3, bodeActive,       hoveredIcon == 3);
#if DYSEKT_STANDALONE
        drawIcon (g, seqIconArea       .toFloat(), 6, seqActive,        hoveredIcon == 4);
#endif

        // ── Hover tooltip label ──────────────────────────────────────
        if (hoveredIcon >= 0)
        {
            static const char* kLabels[] = { "FILE BROWSER", "WAVEFORM", "MIDI FOLLOW", "MIXER", "SEQUENCER" };
            const juce::Rectangle<int>* areas[] = { &filIconArea, &waIconArea,
                                                     &midiFollowIconArea, &bodeIconArea, &seqIconArea };
            const auto& area = *areas[hoveredIcon];
            const int labelH  = si (9);
            const int labelY  = area.getY() - labelH - 2;
            g.setFont (DysektLookAndFeel::makeFont (sf1 (7.0f)));
            g.setColour (getTheme().accent.withAlpha (0.80f));
            g.drawText (kLabels[hoveredIcon], area.getX() - si(20), labelY, area.getWidth() + si(40), labelH,
                        juce::Justification::centred);
        }
    }

    // ── Bottom row: GLOBAL PITCH (left) | GLOBAL EQ (centre) | GLOBAL VOL (right) ─────────
    {
        float gPitch = processor.apvts.getRawParameterValue (ParamIds::defaultPitch)->load();
        float gVol   = processor.apvts.getRawParameterValue (ParamIds::masterVolume)->load();

        juce::String pitchStr = (gPitch >= 0.0f ? "+" : "") + juce::String ((int) std::round (gPitch));
        juce::String volStr   = (gVol   >= 0.0f ? "+" : "") + juce::String (gVol, 1);

        // ── Shared vertical geometry ─────────────────────────────────────────
        int rowTop = half + si (4);
        int rowBot = h   - si (20);   // leave room for label + value below

        // Three equally-spaced centres: 1/4, 1/2, 3/4
        const int k1cx = w / 4;
        const int kcx  = w / 2;
        const int k2cx = w - w / 4;
        const int kcy  = rowTop + (rowBot - rowTop) / 2;
        const int kr   = si (12);

        // ── GLOBAL PITCH knob (left quarter) ────────────────────────────────
        {
            float pitchN = 0.5f + gPitch / 96.0f;   // -48..+48 → 0..1

            const float kStart   = juce::MathConstants<float>::pi * 0.75f;
            const float kEnd     = juce::MathConstants<float>::pi * 2.25f;
            const float halfPi   = juce::MathConstants<float>::halfPi;
            const float arcStart = kStart + halfPi;
            const float arcEnd   = kEnd   + halfPi;
            float angle    = kStart + pitchN * (kEnd - kStart);
            float arcAngle = arcStart + pitchN * (arcEnd - arcStart);

            juce::Path track;
            track.addCentredArc ((float)k1cx,(float)kcy,(float)kr,(float)kr,0.f,arcStart,arcEnd,true);
            g.setColour (getTheme().darkBar.brighter (0.3f));
            g.strokePath (track, juce::PathStrokeType (sf1 (1.5f)));

            juce::Path arc;
            arc.addCentredArc ((float)k1cx,(float)kcy,(float)kr,(float)kr,0.f,arcStart,arcAngle,true);
            g.setColour (accent);
            g.strokePath (arc, juce::PathStrokeType (sf1 (2.2f)));

            float lr = (float) kr - 2.0f;
            g.setColour (accent.brighter (0.3f));
            g.drawLine ((float)k1cx, (float)kcy,
                        (float)k1cx + lr * std::cos (angle),
                        (float)kcy  + lr * std::sin (angle), sf1 (1.3f));

            g.setFont (DysektLookAndFeel::makeFont (sf1 (7.5f), true));
            g.setColour (fg.withAlpha (0.45f));
            g.drawText ("GLOBAL PITCH", k1cx - si(28), kcy + kr + si(2), si(56), si(9), juce::Justification::centred);

            g.setFont (DysektLookAndFeel::makeFont (sf1 (8.0f)));
            g.setColour (accent.withAlpha (0.80f));
            g.drawText (pitchStr, k1cx - si(18), kcy + kr + si(11), si(36), si(9), juce::Justification::centred);

            pitchKnobArea = { k1cx - kr - 5, kcy - kr - 3, (kr + 5) * 2, (kr + 5) * 2 };
        }

        // ── GLOBAL EQ button (centre) ────────────────────────────────────────
        {
            // Draw the EQ icon scaled to match knob radius
            const int eqSz = kr * 2;
            eqIconArea = { kcx - kr, kcy - kr, eqSz, eqSz };
            drawIcon (g, eqIconArea.toFloat(), 5, eqActive);

            g.setFont (DysektLookAndFeel::makeFont (sf1 (7.5f), true));
            g.setColour (fg.withAlpha (0.45f));
            g.drawText ("GLOBAL EQ", kcx - si(28), kcy + kr + si(2), si(56), si(9), juce::Justification::centred);
        }

        // ── GLOBAL VOL knob (right quarter) — 0dB-centred logic ─────────────
        {
            // 0 dB → norm=0.5 (12 o'clock).  -24 dB → 0.0 (CCW stop).  +24 dB → 1.0 (CW stop).
            // Values outside -24..+24 are clamped visually (the param itself goes to -100..+24).
            constexpr float kVisMin = -24.0f, kVisMax = 24.0f;
            const float volN = juce::jlimit (0.0f, 1.0f, (gVol - kVisMin) / (kVisMax - kVisMin));

            const float kStart   = juce::MathConstants<float>::pi * 0.75f;  // 7 o'clock
            const float kEnd     = juce::MathConstants<float>::pi * 2.25f;  // 5 o'clock
            const float arcLen   = kEnd - kStart;
            const float halfPi   = juce::MathConstants<float>::halfPi;
            const float arcStart = kStart + halfPi;
            const float arcEnd   = kEnd   + halfPi;

            // Track
            juce::Path track;
            track.addCentredArc ((float)k2cx, (float)kcy, (float)kr, (float)kr,
                                  0.f, arcStart, arcEnd, true);
            g.setColour (getTheme().darkBar.brighter (0.3f));
            g.strokePath (track, juce::PathStrokeType (sf1 (1.5f)));

            // Fill — bidirectional from 12 o'clock (0 dB = norm 0.5)
            constexpr float zeroNorm  = 0.5f;
            const float     zeroAngle = arcStart + (arcEnd - arcStart) * zeroNorm;
            const float     normAngle = arcStart + (arcEnd - arcStart) * volN;

            if (std::abs (volN - zeroNorm) > 0.005f)
            {
                juce::Path fill;
                if (volN > zeroNorm)
                    fill.addCentredArc ((float)k2cx, (float)kcy, (float)kr, (float)kr,
                                         0.f, zeroAngle, normAngle, true);
                else
                    fill.addCentredArc ((float)k2cx, (float)kcy, (float)kr, (float)kr,
                                         0.f, normAngle, zeroAngle, true);
                g.setColour (accent);
                g.strokePath (fill, juce::PathStrokeType (sf1 (2.2f)));
            }

            // 0 dB tick marker
            g.setColour (fg.withAlpha (0.35f));
            g.drawLine ((float)k2cx + (float)(kr - 3) * std::cos (zeroAngle),
                        (float)kcy  + (float)(kr - 3) * std::sin (zeroAngle),
                        (float)k2cx + (float)kr * std::cos (zeroAngle),
                        (float)kcy  + (float)kr * std::sin (zeroAngle), 1.0f);

            // Pointer line
            float angle = kStart + volN * arcLen;
            float lr = (float)kr - 2.0f;
            g.setColour (accent.brighter (0.3f));
            g.drawLine ((float)k2cx, (float)kcy,
                        (float)k2cx + lr * std::cos (angle),
                        (float)kcy  + lr * std::sin (angle), sf1 (1.3f));

            // Labels
            g.setFont (DysektLookAndFeel::makeFont (sf1 (7.5f), true));
            g.setColour (fg.withAlpha (0.45f));
            g.drawText ("GLOBAL VOL", k2cx - si (28), kcy + kr + si (2), si (56), si (9), juce::Justification::centred);

            g.setFont (DysektLookAndFeel::makeFont (sf1 (8.0f)));
            g.setColour (accent.withAlpha (0.80f));
            g.drawText (volStr, k2cx - si (18), kcy + kr + si (11), si (36), si (9), juce::Justification::centred);

            volKnobArea = { k2cx - kr - 5, kcy - kr - 3, (kr + 5) * 2, (kr + 5) * 2 };
        }
    }
}

// ── Mouse ─────────────────────────────────────────────────────────────────────

void DualLcdControlFrame::mouseDown (const juce::MouseEvent& e)
{
    auto pos = e.getPosition();
    dragTarget = DragTarget::None;

    // Icon toggles
    if (filIconArea.contains (pos))
    {
        browserActive = ! browserActive;
        repaint();
        if (onBrowserToggle) onBrowserToggle();
        return;
    }
    if (waIconArea.contains (pos))
    {
        setWaveMode ((waveMode + 1) % 8);
        repaint();
        if (onWaveToggle) onWaveToggle();
        return;
    }
    if (midiFollowIconArea.contains (pos))
    {
        midiFollowActive = ! midiFollowActive;
        repaint();
        if (onMidiFollowToggle) onMidiFollowToggle();
        return;
    }
    if (bodeIconArea.contains (pos))
    {
        bodeActive = ! bodeActive;
        repaint();
        if (onBodeToggle) onBodeToggle();
        return;
    }
    if (seqIconArea.contains (pos))
    {
        seqActive = ! seqActive;
        repaint();
        if (onSeqToggle) onSeqToggle();
        return;
    }
    // ── SLICER | SFZ-PLAYER | SFZ-PLAYER2 tabs ──────────────────────────────
    if (editTabArea.contains (pos))
    {
        if (uiTab != 0)
        {
            uiTab = 0;
            repaint();
            if (onUiModeChanged) onUiModeChanged (0);
        }
        return;
    }
    if (padTabArea.contains (pos))
    {
        if (uiTab != 1)
        {
            uiTab = 1;
            repaint();
            if (onUiModeChanged) onUiModeChanged (1);
        }
        return;
    }
    if (sfzPlayerTabArea.contains (pos))
    {
        if (uiTab != 2)
        {
            uiTab = 2;
            repaint();
            if (onUiModeChanged) onUiModeChanged (2);
        }
        return;
    }

    // Knobs + EQ button (bottom row)
    if (eqIconArea.contains (pos))
    {
        eqActive = ! eqActive;
        repaint();
        if (onEqToggle) onEqToggle();
        return;
    }
    if (pitchKnobArea.contains (pos))
    {
        dragTarget     = DragTarget::Pitch;
        dragStartY     = pos.y;
        dragStartValue = processor.apvts.getRawParameterValue (ParamIds::defaultPitch)->load();
        if (auto* p = processor.apvts.getParameter (ParamIds::defaultPitch))
            p->beginChangeGesture();
        return;
    }
    if (volKnobArea.contains (pos))
    {
        dragTarget     = DragTarget::Volume;
        dragStartY     = pos.y;
        dragStartValue = processor.apvts.getRawParameterValue (ParamIds::masterVolume)->load();
        if (auto* p = processor.apvts.getParameter (ParamIds::masterVolume))
            p->beginChangeGesture();
        return;
    }
}

void DualLcdControlFrame::mouseDrag (const juce::MouseEvent& e)
{
    const float delta = (float) (dragStartY - e.y);

    switch (dragTarget)
    {
        case DragTarget::Pitch:
        {
            float sens = e.mods.isShiftDown() ? 0.05f : 0.5f;
            float newV = juce::jlimit (-48.0f, 48.0f, dragStartValue + delta * sens);
            if (auto* p = processor.apvts.getParameter (ParamIds::defaultPitch))
                p->setValueNotifyingHost (p->convertTo0to1 (newV));
            repaint();
            break;
        }
        case DragTarget::Volume:
        {
            // Vertical drag: up = louder, down = quieter — matches knob visual
            float sens = e.mods.isShiftDown() ? 0.05f : 0.5f;
            float newV = juce::jlimit (-100.0f, 24.0f, dragStartValue + delta * sens);
            if (auto* p = processor.apvts.getParameter (ParamIds::masterVolume))
                p->setValueNotifyingHost (p->convertTo0to1 (newV));
            repaint();
            break;
        }
        default: break;
    }
}

void DualLcdControlFrame::mouseUp (const juce::MouseEvent&)
{
    if (dragTarget == DragTarget::Pitch)
        if (auto* p = processor.apvts.getParameter (ParamIds::defaultPitch))
            p->endChangeGesture();
    if (dragTarget == DragTarget::Volume)
        if (auto* p = processor.apvts.getParameter (ParamIds::masterVolume))
            p->endChangeGesture();
    dragTarget = DragTarget::None;
}

void DualLcdControlFrame::mouseMove (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    int found = -1;
    if (filIconArea.contains (pos))             found = 0;
    else if (waIconArea.contains (pos))         found = 1;
    else if (midiFollowIconArea.contains (pos)) found = 2;
    else if (bodeIconArea.contains (pos))       found = 3;
    else if (seqIconArea.contains (pos))        found = 4;

    if (found != hoveredIcon)
    {
        hoveredIcon = found;
        repaint();
    }
}

void DualLcdControlFrame::mouseExit (const juce::MouseEvent&)
{
    if (hoveredIcon != -1)
    {
        hoveredIcon = -1;
        repaint();
    }
}
