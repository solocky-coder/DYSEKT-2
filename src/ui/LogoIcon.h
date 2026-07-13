#pragma once
#include <juce_graphics/juce_graphics.h>
#include "DysektLookAndFeel.h"  // for getTheme()

/**  Renders a square DYSEKT-SF waveform icon from the current theme.
     Mirrors the drawing logic in LogoBar::paint() so the icon always
     matches the live theme — including user-created custom themes.
     @param size  Pixel dimensions of the square image (power of two; 256 recommended). */
inline juce::Image makeLogoImage (int size)
{
    juce::Image img (juce::Image::ARGB, size, size, true);
    juce::Graphics g (img);

    const auto& t  = getTheme();
    const auto accent = t.accent;

    // Scale factor relative to LogoBar's design-time height (52 px)
    const float sf = (float) size / 52.0f;
    const int   cy = size / 2;

    // ── Bar geometry (same constants as LogoBar::paint) ───────────────
    const int barW = juce::roundToInt (2.6f * sf);
    const int gap  = juce::roundToInt (1.4f * sf);
    constexpr int kNumBarsSide  = 5;
    constexpr int kNumElems     = kNumBarsSide * 2 + 1;
    constexpr int kSpikeIdx     = kNumBarsSide;
    const int spikeW = juce::jmax (2, juce::roundToInt (1.8f * sf));

    const int iconW  = kNumBarsSide * 2 * barW + spikeW + (kNumElems - 1) * gap;
    const int startX = (size - iconW) / 2;

    const float framePad         = 6.0f * sf;
    const float spikeHeightRatio = 1.237f;
    const float availableH       = juce::jmax (4.0f, (float) size - 3.0f - 2.0f * framePad);
    const float barH             = availableH / spikeHeightRatio;

    const float sideHeights[kNumBarsSide] = { 0.174f, 0.317f, 0.548f, 0.747f, 1.0f };

    // ── Gradient (same hue-rotation logic as LogoBar) ─────────────────
    juce::Colour accentB;
    if (accent.getSaturation() < 0.15f)
        accentB = accent.getBrightness() > 0.5f ? accent.darker (0.45f) : accent.brighter (0.45f);
    else
        accentB = accent.withRotatedHue (0.16f).withMultipliedSaturation (0.9f);

    const juce::ColourGradient iconGrad (accent,  (float) startX,          (float) cy,
                                          accentB, (float)(startX + iconW), (float) cy,
                                          false);

    // ── Spike colour ──────────────────────────────────────────────────
    const auto spikeColour = t.background.getPerceivedBrightness() < 0.5f
                             ? t.background.brighter (0.05f)
                             : t.darkBar;

    // ── Draw bars + spike ─────────────────────────────────────────────
    int bx = startX;
    for (int i = 0; i < kNumElems; ++i)
    {
        if (i == kSpikeIdx)
        {
            const int sh   = juce::roundToInt (spikeHeightRatio * barH);
            const int sy   = cy - sh / 2;
            const float mx = bx + spikeW * 0.5f;

            juce::Path spike;
            spike.startNewSubPath (mx, (float) sy);
            spike.quadraticTo ((float)(bx + spikeW), (float)(sy + sh * 0.30f),
                               (float)(bx + spikeW), (float) cy);
            spike.quadraticTo ((float)(bx + spikeW), (float)(sy + sh * 0.70f),
                               mx, (float)(sy + sh));
            spike.quadraticTo ((float) bx, (float)(sy + sh * 0.70f),
                               (float) bx, (float) cy);
            spike.quadraticTo ((float) bx, (float)(sy + sh * 0.30f),
                               mx, (float) sy);
            spike.closeSubPath();

            g.setColour (spikeColour);
            g.fillPath (spike);

            bx += spikeW + gap;
        }
        else
        {
            const int side  = (i < kSpikeIdx) ? i : (kNumElems - 1 - i);
            const int bh    = juce::jmax (2, juce::roundToInt (sideHeights[side] * barH));
            const int by    = cy - bh / 2;

            g.setGradientFill (iconGrad);
            g.fillRect (bx, by, barW, bh);

            bx += barW + gap;
        }
    }

    return img;
}

/**  Sets the current-theme waveform logo as the window peer icon
     (Windows / Linux title bar + taskbar) and macOS dock icon.
     Safe to call any time after the component has a peer. */
inline void applyWindowIcon (juce::Component* comp)
{
    const auto img = makeLogoImage (256);

   #if JUCE_MAC
    juce::Process::setDockIconImage (img);
   #endif

    if (comp != nullptr)
        if (auto* peer = comp->getPeer())
            peer->setIcon (img);
}
