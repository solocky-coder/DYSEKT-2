#pragma once

#include <juce_graphics/juce_graphics.h>

// ── Fixed black STN-LCD palette ──────────────────────────────────────────────
// This panel simulates a physical black-tinted STN LCD readout (classic Roland
// hardware style), so its colours are fixed "hardware" values rather than being
// derived from the app theme — a real LCD doesn't change colour when the host
// app switches between light/dark mode.
//
// Shared by SliceLcdDisplay, Sf2LcdDisplay, and SfzLcdDisplay so all three
// engines' LCDs read identically. Tune values here once — do NOT re-declare
// a local copy of this namespace in any of those .cpp files; that's exactly
// the drift that caused SliceLcdDisplay's palette to diverge from the other
// two in the first place.
namespace LcdColours
{
    // Background is a near-black shade; ink is a bright green "phosphor"
    // colour that reads clearly against it.
    static const juce::Colour kBgMid    { 0xFF0A0F08 };
    static const juce::Colour kInk      { 0xFF6BFF4A };
    static const juce::Colour kOutline  { 0xFF1C3A12 };
    static const juce::Colour kGlow     { 0xFF6BFF4A };   // used with alpha for glow layers

    struct Palette
    {
        juce::Colour background, bezel, phosphor, dim, highlight,
                     labelCol, scanline, cursor, noDataCol, border,
                     flagOn, flagOff, flagBg;
    };

    static inline Palette fromTheme()
    {
        // Kept the name fromTheme() to minimise churn at call sites, but the
        // values below are fixed hardware colours, not theme-derived.
        const auto bg = kBgMid;
        const auto ac = kInk;                                 // ink / "phosphor" colour

        Palette p;
        p.background = bg;
        p.bezel      = kOutline;
        p.phosphor   = ac;
        p.dim        = ac.withAlpha (0.55f).overlaidWith (bg);
        p.highlight  = ac.brighter (0.35f);                   // slightly brighter ink for emphasis rows
        // Labels are drawn at full phosphor brightness, same as values — a
        // real LCD doesn't dim the label glyphs relative to the value glyphs.
        p.labelCol   = ac;
        p.scanline   = juce::Colours::black;
        p.cursor     = ac;
        p.noDataCol  = ac.withAlpha (0.40f).overlaidWith (bg);
        p.border     = kOutline;
        p.flagOn     = ac;
        p.flagOff    = ac.withAlpha (0.55f).overlaidWith (bg);
        p.flagBg     = bg.brighter (0.15f);           // visible pill outline against screen bg
        return p;
    }

    // ── Shared chassis/backlight/screen rendering ───────────────────────────
    // Draws the outer plastic chassis, dark-green bezel, backlight glow, and
    // the near-black inner screen with its scanline texture. Identical across
    // all three LCD panels; each caller still owns its own screen-content
    // rows (labels/values/flags), only the surrounding "hardware" is shared.
    static inline void drawChassisAndScreen (juce::Graphics& g, juce::Rectangle<int> b,
                                              int scanlineAlpha = 28)
    {
        const auto pal = fromTheme();

        // ── Outer chassis frame — dark plastic surround ─────────────────────
        juce::ColourGradient outerGrad (juce::Colour (0xFF131313), 0, 0,
                                         juce::Colour (0xFF0E0E0E), 0, (float) b.getHeight(), false);
        g.setGradientFill (outerGrad);
        g.fillRoundedRectangle (b.toFloat(), 4.0f);
        // Main LCD frame border — dark green bezel
        g.setColour (kOutline);
        g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 4.0f, 1.5f);

        // ── Outer backlight glow — faint ink-coloured spill onto the chassis ──
        g.setColour (juce::Colour (0x306BFF4A));      // tight, low opacity
        g.drawRoundedRectangle (b.toFloat().expanded (1.0f), 5.0f, 3.0f);
        g.setColour (juce::Colour (0x186BFF4A));      // wide, lower opacity
        g.drawRoundedRectangle (b.toFloat().expanded (4.0f), 7.0f, 6.0f);

        // ── Inner screen — solid near-black fill with a subtle top glow ─────
        auto screen = b.reduced (4);
        g.setColour (kBgMid);
        g.fillRoundedRectangle (screen.toFloat(), 2.0f);

        juce::ColourGradient glow (pal.phosphor.withAlpha (0.06f), 0, (float) screen.getY(),
                                    juce::Colours::transparentBlack, 0, (float) (screen.getY() + 20), false);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (screen.toFloat(), 2.0f);

        // ── Scanline texture — subtle physical-screen feel ──────────────────
        g.setColour (pal.scanline.withAlpha ((uint8_t) scanlineAlpha));
        for (int y = screen.getY(); y < screen.getBottom(); y += 2)
            g.drawHorizontalLine (y, (float) screen.getX(), (float) screen.getRight());

        g.setColour (kOutline.withAlpha (0.5f));
        g.drawRoundedRectangle (screen.toFloat().expanded (0.5f), 2.0f, 1.0f);
    }
}
