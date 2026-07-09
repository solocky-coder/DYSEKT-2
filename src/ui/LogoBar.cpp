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

    // Icon: symmetric 11-element waveform — 5 bars ramping up, a tapered
    // centre spike, then 5 bars ramping back down. Proportions measured
    // directly from the DYSEKT mark artwork (bar heights ~0.17/0.32/0.55/
    // 0.75/1.0 of the tallest bar; spike ~1.24x the tallest bar).
    const int barW    = juce::roundToInt (2.6f * sf);
    const int gap     = juce::roundToInt (1.4f * sf);
    constexpr int kNumBarsSide = 5;
    constexpr int kNumElems = kNumBarsSide * 2 + 1;
    constexpr int kSpikeIdx = kNumBarsSide;
    const int spikeW  = juce::jmax (2, juce::roundToInt (1.8f * sf));

    const int iconW = kNumBarsSide * 2 * barW + spikeW + (kNumElems - 1) * gap;
    const int iconGap = juce::roundToInt (7 * sf);

    // Total block width → centre it
    const int blockW  = iconW + iconGap + wordW;
    const int startX  = (w - blockW) / 2;

    // ── Waveform-slice icon ───────────────────────────────────────────────
    const float barH = (float)(h - 8);
    const float sideHeights[kNumBarsSide] = { 0.174f, 0.317f, 0.548f, 0.747f, 1.0f };
    const float spikeHeightRatio = 1.237f;

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

    // The centre spike is the dark tapered divider from the artwork —
    // themed off the panel's own dark colour so it reads correctly on
    // light themes too.
    const auto spikeColour = getTheme().background.getPerceivedBrightness() < 0.5f
                                ? getTheme().background.brighter (0.05f)
                                : getTheme().darkBar;

    int bx = startX;
    for (int i = 0; i < kNumElems; ++i)
    {
        const bool isSpike = (i == kSpikeIdx);

        if (isSpike)
        {
            const int sh = juce::roundToInt (spikeHeightRatio * barH);
            const int sy = cy - sh / 2;
            const float midX = bx + spikeW * 0.5f;

            // Tapered lens/spike shape — pointed top & bottom, widest at mid-height.
            juce::Path spike;
            spike.startNewSubPath (midX, (float) sy);
            spike.quadraticTo ((float)(bx + spikeW), (float)(sy + sh * 0.30f),
                                (float)(bx + spikeW), (float) cy);
            spike.quadraticTo ((float)(bx + spikeW), (float)(sy + sh * 0.70f),
                                midX, (float)(sy + sh));
            spike.quadraticTo ((float) bx, (float)(sy + sh * 0.70f),
                                (float) bx, (float) cy);
            spike.quadraticTo ((float) bx, (float)(sy + sh * 0.30f),
                                midX, (float) sy);
            spike.closeSubPath();

            g.setColour (spikeColour);
            g.fillPath (spike);

            bx += spikeW + gap;
        }
        else
        {
            const int sideIdx = i < kSpikeIdx ? i : (kNumElems - 1 - i);
            const int bh = juce::roundToInt (sideHeights[sideIdx] * barH);
            const int by = cy - bh / 2;

            g.setGradientFill (iconGrad);
            g.setOpacity (1.0f);
            g.fillRoundedRectangle ((float) bx, (float) by, (float) barW, (float) bh, barW * 0.4f);

            bx += barW + gap;
        }
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
