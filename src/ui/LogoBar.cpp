#include "LogoBar.h"
#include "DysektLookAndFeel.h"
#include "../PluginProcessor.h"

LogoBar::LogoBar (DysektProcessor& p) : processor (p) {}

void LogoBar::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().header);

    const auto accent = getTheme().accent;
    const auto fg     = getTheme().foreground;
    const int  w  = getWidth();
    const int  h  = getHeight();
    const int  cy = h / 2;
    // Scale factor relative to the design-time height (kLogoH = 52px).
    static constexpr float kBaseH = 52.0f;
    const float sf = (float) h / kBaseH;

    // ── Measure wordmark so we can centre the whole block ─────────────────
    const float wordmarkSize = 24.0f * sf;
    auto wordmarkFont = DysektLookAndFeel::makeFont (wordmarkSize, true);

    const juce::String dy   = "DY";
    const juce::String sekt = "SEKT-SF";
    const int dyW   = juce::GlyphArrangement::getStringWidthInt(wordmarkFont, dy);
    const int sektW = juce::GlyphArrangement::getStringWidthInt(wordmarkFont, sekt);
    const int wordW = dyW + sektW;

    // Icon: 5 bars
    const int barW    = juce::roundToInt (3 * sf);
    const int gap     = juce::roundToInt (2 * sf);
    const int barStep = barW + gap;
    const int iconW   = 5 * barStep - gap;
    const int iconGap = juce::roundToInt (7 * sf);

    // Total block width → centre it
    const int blockW  = iconW + iconGap + wordW;
    const int startX  = (w - blockW) / 2;

    // ── Waveform-slice icon ───────────────────────────────────────────────
    const float barH = (float)(h - 10);
    const float heights[5] = { 0.55f, 0.90f, 0.48f, 0.80f, 0.52f };
    const int   activeBar  = 1;

    // The icon keeps its gradient look, but the gradient is now derived
    // from the active theme's single accent colour rather than a hardcoded
    // cyan-to-purple pair — so it stays correct for every built-in theme
    // (dysekt's teal, hack's red, snow's orange, ...) and for any
    // user-created theme too, since those only ever define one accent
    // colour (see ThemeData::fromThemeFile). The second stop is normally a
    // hue-rotated variant of the same accent, reproducing the original
    // "cool colour sweeping into a neighbouring hue" effect for any input.
    //
    // Fallback: a custom theme could set a near-grayscale accent (very low
    // saturation), where rotating the hue barely changes anything visible.
    // In that case, shift lightness instead so the gradient still reads as
    // two distinct stops rather than collapsing to a flat colour.
    juce::Colour accentB;
    if (accent.getSaturation() < 0.15f)
        accentB = accent.getBrightness() > 0.5f ? accent.darker (0.45f) : accent.brighter (0.45f);
    else
        accentB = accent.withRotatedHue (0.16f).withMultipliedSaturation (0.9f);

    const juce::ColourGradient iconGrad (accent,  (float) startX,          (float) cy,
                                          accentB, (float)(startX + iconW), (float) cy,
                                          false);

    g.setGradientFill (iconGrad);
    g.setOpacity (0.12f);
    g.drawHorizontalLine (cy, (float)startX, (float)(startX + iconW));

    for (int i = 0; i < 5; ++i)
    {
        const int bx = startX + i * barStep;
        const int bh = juce::roundToInt (heights[i] * barH);
        const int by = cy - bh / 2;

        g.setGradientFill (iconGrad);

        if (i == activeBar)
        {
            g.setOpacity (0.22f);
            g.fillRect (bx, by, barW, bh);
            g.setOpacity (1.0f);
        }
        else
        {
            g.setOpacity (0.45f);
        }
        g.drawRect (bx, by, barW, bh, 1);
    }

    // ── Wordmark ──────────────────────────────────────────────────────────
    const int textX = startX + iconW + iconGap;

    g.setFont (wordmarkFont);

    // "DY" — foreground
    g.setColour (fg.withAlpha (0.90f));
    g.drawText (dy, textX, 0, dyW + 2, h, juce::Justification::centredLeft);

    // "SEKT" — accent
    g.setColour (accent);
    g.drawText (sekt, textX + dyW, 0, sektW + 2, h, juce::Justification::centredLeft);

    // ── Frame border — drawn last so it sits on top of the fill ──────────────
    // The component already has a 4px layout gap from the outer window border;
    // withTrimmedTop(3) adds a further 3px so the frame is clearly separated.
    {
        const juce::Rectangle<float> fr (getLocalBounds().toFloat().withTrimmedTop (3.0f));
        g.setColour (accent.withAlpha (0.18f));
        g.drawRoundedRectangle (fr.expanded (0.5f), 5.0f, 1.0f);
        g.setColour (accent);                       // full opacity — matches other frames
        g.drawRoundedRectangle (fr.reduced (0.5f), 4.0f, 1.5f);
        g.setColour (accent.withAlpha (0.15f));
        g.drawRoundedRectangle (fr.reduced (2.0f), 3.5f, 1.0f);
    }

}
