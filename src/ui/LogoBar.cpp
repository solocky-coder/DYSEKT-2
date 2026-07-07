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

    g.setColour (accent.withAlpha (0.12f));
    g.drawHorizontalLine (cy, (float)startX, (float)(startX + iconW));

    for (int i = 0; i < 5; ++i)
    {
        const int bx = startX + i * barStep;
        const int bh = juce::roundToInt (heights[i] * barH);
        const int by = cy - bh / 2;

        if (i == activeBar)
        {
            g.setColour (accent.withAlpha (0.22f));
            g.fillRect (bx, by, barW, bh);
            g.setColour (accent);
        }
        else
        {
            g.setColour (accent.withAlpha (0.45f));
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
