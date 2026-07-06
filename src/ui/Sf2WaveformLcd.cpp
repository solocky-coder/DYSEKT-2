#include "Sf2WaveformLcd.h"
#include "UIHelpers.h"
#include "DysektLookAndFeel.h"
#include "../PluginProcessor.h"

// ── Theme helpers ─────────────────────────────────────────────────────────────
static juce::Colour sf2Lcd2Bg()       { return getTheme().darkBar.darker (0.55f); }
static juce::Colour sf2Lcd2Phosphor() { return getTheme().accent; }
static juce::Colour sf2Lcd2Dim()      { return getTheme().accent.withAlpha (0.15f).overlaidWith (sf2Lcd2Bg()); }

// Toxic-Candy node colours — same palette used across all envelope LCD widgets.
static const juce::Colour kSf2ColAttack  { 0xFF00FF87 };
static const juce::Colour kSf2ColDecay   { 0xFFFFE800 };
static const juce::Colour kSf2ColSustain { 0xFF00C8FF };
static const juce::Colour kSf2ColRelease { 0xFFFF6B00 };

// ── Constructor ───────────────────────────────────────────────────────────────

Sf2WaveformLcd::Sf2WaveformLcd (DysektProcessor& p)
    : processor (p)
{
    setOpaque (false);
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void Sf2WaveformLcd::resized()
{
    screenArea = getLocalBounds().reduced (4).toFloat();
}

void Sf2WaveformLcd::repaintLcd()
{
    if (dragRole == NodeRole::None)
    {
        if (postCommitGuard > 0)
            --postCommitGuard;
        else
        {
            buildEnvelopeNodes();
            buildWaveformPeaks();
        }
    }
    repaint();
}

// ── Envelope: read sfzPlayer ADSR → normalised env + envNodes ────────────────

void Sf2WaveformLcd::buildEnvelopeNodes()
{
    const float attackMs  = processor.sfzPlayer.getSfzAttack()  * 1000.0f;
    const float decayMs   = processor.sfzPlayer.getSfzDecay()   * 1000.0f;
    const float sustainPc = processor.sfzPlayer.getSfzSustain();
    const float releaseMs = processor.sfzPlayer.getSfzRelease() * 1000.0f;

    static constexpr float kMin = 0.01f, kMax = 0.99f, kGap = 0.01f;

    const float attackNorm  = std::sqrt (juce::jmin (attackMs  / kViewMs, 1.0f));
    const float decayNorm   = std::sqrt (juce::jmin (decayMs   / kViewMs, 1.0f));
    const float releaseNorm = std::sqrt (juce::jmin (releaseMs / kViewMs, 1.0f));

    const float ax_raw = kMin + attackNorm  * (kMax - kMin);
    const float rx_raw = (releaseMs < 0.5f)
                         ? kMax
                         : juce::jlimit (kMin, kMax, kMax - releaseNorm * (kMax - kMin));

    env.ax = juce::jlimit (kMin, kMax - 2.0f * kGap, ax_raw);
    env.rx = juce::jlimit (env.ax + 2.0f * kGap, kMax, rx_raw);

    const float dSpan = env.rx - env.ax - 2.0f * kGap;
    env.dx = juce::jlimit (env.ax + kGap,
                           env.rx - kGap,
                           env.ax + kGap + decayNorm * dSpan);

    env.sy    = juce::jlimit (0.04f, 0.94f, 1.0f - (sustainPc / 100.0f));
    env.ay    = 0.04f;
    env.sxEnd = env.rx;

    envNodes.clear();

    EnvNode a; a.xn = env.ax; a.yn = env.ay; a.role = NodeRole::Attack;
    a.colour = kSf2ColAttack;  a.label = "A"; envNodes.add (a);

    EnvNode d; d.xn = env.dx; d.yn = env.sy; d.role = NodeRole::Decay;
    d.colour = kSf2ColDecay;   d.label = "D"; envNodes.add (d);

    EnvNode s;
    s.xn = (env.dx + env.sxEnd) * 0.5f; s.yn = env.sy;
    s.role = NodeRole::Sustain; s.colour = kSf2ColSustain; s.label = "S";
    envNodes.add (s);

    EnvNode r; r.xn = env.rx; r.yn = env.sy; r.role = NodeRole::Release;
    r.colour = kSf2ColRelease; r.label = "R"; envNodes.add (r);
}

// ── Commit: inverse-map env → sfzPlayer setters ───────────────────────────────

void Sf2WaveformLcd::commitNodes()
{
    static constexpr float kMin = 0.01f, kMax = 0.99f, kGap = 0.01f;

    const float aRatio = (env.ax - kMin) / juce::jmax (0.001f, kMax - kMin);
    const float rRatio = (kMax - env.rx) / juce::jmax (0.001f, kMax - kMin);
    const float dSpan  = env.rx - env.ax - 2.0f * kGap;
    const float dRatio = (env.dx - (env.ax + kGap)) / juce::jmax (0.001f, dSpan);

    const float attackMs  = juce::jlimit (0.0f, kViewMs, aRatio * aRatio * kViewMs);
    const float decayMs   = juce::jlimit (0.0f, kViewMs, dRatio * dRatio * kViewMs);
    const float sustainPc = juce::jlimit (0.0f, 100.0f, (1.0f - env.sy) * 100.0f);
    const float releaseMs = juce::jlimit (0.0f, kViewMs, rRatio * rRatio * kViewMs);

    if (dragRole == NodeRole::Attack)
        processor.sfzPlayer.setSfzAttack  (attackMs  / 1000.0f);
    else if (dragRole == NodeRole::Decay)
        processor.sfzPlayer.setSfzDecay   (decayMs   / 1000.0f);
    else if (dragRole == NodeRole::Sustain)
        processor.sfzPlayer.setSfzSustain (sustainPc);
    else if (dragRole == NodeRole::Release)
        processor.sfzPlayer.setSfzRelease (releaseMs / 1000.0f);

    postCommitGuard = 4;
}

// ── Hit testing ───────────────────────────────────────────────────────────────

Sf2WaveformLcd::NodeRole Sf2WaveformLcd::hitTest (juce::Point<float> pos) const
{
    if (screenArea.isEmpty()) return NodeRole::None;

    const float W  = screenArea.getWidth();
    const float H  = screenArea.getHeight();
    const float ox = screenArea.getX();
    const float oy = screenArea.getY();

    NodeRole best   = NodeRole::None;
    float    bestD2 = kHitR * kHitR;

    for (const auto& n : envNodes)
    {
        const float nx = ox + n.xn * W;
        const float ny = oy + n.yn * H;
        const float dx = pos.x - nx;
        const float dy = pos.y - ny;
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestD2) { bestD2 = d2; best = n.role; }
    }
    return best;
}

// ── Mouse ─────────────────────────────────────────────────────────────────────

void Sf2WaveformLcd::mouseMove (const juce::MouseEvent& e)
{
    const NodeRole newHov = hitTest (e.position);
    if (newHov != hovRole)
    {
        hovRole = newHov;
        setMouseCursor (hovRole != NodeRole::None
                            ? juce::MouseCursor::PointingHandCursor
                            : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void Sf2WaveformLcd::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown()) return;
    dragRole = hitTest (e.position);
}

void Sf2WaveformLcd::mouseDrag (const juce::MouseEvent& e)
{
    if (dragRole == NodeRole::None || screenArea.isEmpty()) return;

    const float W  = screenArea.getWidth();
    const float H  = screenArea.getHeight();
    const float ox = screenArea.getX();
    const float oy = screenArea.getY();

    const float xn = juce::jlimit (0.01f, 0.99f, (e.position.x - ox) / W);
    const float yn = juce::jlimit (0.02f, 0.98f, (e.position.y - oy) / H);

    static constexpr float kMin = 0.01f, kMax = 0.99f, kGap = 0.01f;

    if      (dragRole == NodeRole::Attack)
        env.ax = juce::jlimit (kMin, env.dx - kGap, xn);
    else if (dragRole == NodeRole::Decay)
        env.dx = juce::jlimit (env.ax + kGap, env.rx - kGap, xn);
    else if (dragRole == NodeRole::Sustain)
        env.sy = juce::jlimit (0.04f, 0.94f, yn);
    else if (dragRole == NodeRole::Release)
        env.rx = juce::jlimit (env.dx + kGap, kMax, xn);

    env.sxEnd = env.rx;

    envNodes.clear();
    EnvNode a; a.xn = env.ax; a.yn = env.ay; a.role = NodeRole::Attack;
    a.colour = kSf2ColAttack;  a.label = "A"; envNodes.add (a);
    EnvNode d; d.xn = env.dx; d.yn = env.sy; d.role = NodeRole::Decay;
    d.colour = kSf2ColDecay;   d.label = "D"; envNodes.add (d);
    EnvNode s; s.xn = (env.dx + env.sxEnd) * 0.5f; s.yn = env.sy;
    s.role = NodeRole::Sustain; s.colour = kSf2ColSustain; s.label = "S"; envNodes.add (s);
    EnvNode r; r.xn = env.rx; r.yn = env.sy; r.role = NodeRole::Release;
    r.colour = kSf2ColRelease; r.label = "R"; envNodes.add (r);

    commitNodes();
    repaint();
}

void Sf2WaveformLcd::mouseUp (const juce::MouseEvent&)
{
    dragRole        = NodeRole::None;
    postCommitGuard = 6;
    repaint();
}

// ── Waveform backdrop ─────────────────────────────────────────────────────────

void Sf2WaveformLcd::buildWaveformPeaks()
{
    peaks.clearQuick();
    peaks.insertMultiple (-1, 0.0f, kPeaks);

    const auto snap = processor.sampleData3.getSnapshot();
    if (snap == nullptr) return;
    const int totalFrames = snap->buffer.getNumSamples();
    if (totalFrames <= 0) return;

    const float zoom   = std::max (1.0f, processor.zoom.load());
    const float scroll = processor.scroll.load();

    const float windowFrac = 1.0f / zoom;
    const float maxScroll  = (float) totalFrames * (1.0f - windowFrac);
    const int   startF     = (int) juce::jlimit (0.0f, (float) (totalFrames - 1),
                                                  scroll * maxScroll);
    const int   endF       = (int) juce::jlimit ((float) startF + 1.0f,
                                                  (float) totalFrames,
                                                  startF + windowFrac * (float) totalFrames);

    cachedTotalFrames = totalFrames;
    cachedZoom   = zoom;
    cachedScroll = scroll;

    for (int i = 0; i < kPeaks; ++i)
    {
        const float t   = (float) i / (float) kPeaks;
        const int   pos = startF + (int) (t * (float) (endF - startF));
        peaks.set (i, DysektProcessor::getWaveformPeakAtIn (processor.sampleData3, pos));
    }
}

void Sf2WaveformLcd::drawWaveformBackdrop (juce::Graphics& g,
                                            const juce::Rectangle<float>& area)
{
    if (peaks.isEmpty()) return;

    const float W  = area.getWidth();
    const float H  = area.getHeight();
    const float cx = area.getX();
    const float cy = area.getY() + H * 0.5f;

    const int n = peaks.size();
    const float scale = H * UILayout::waveformVerticalScale;
    const juce::Colour waveCol = sf2Lcd2Phosphor();

    // ── Helpers — the backdrop only has a single (positive) amplitude per
    //    peak slot, so top/bottom edges are mirrored around the centreline
    //    rather than using independent max/min values like WaveformView.
    auto xAt   = [&] (int i) { return cx + ((float) i / (float) n) * W; };
    auto ampAt = [&] (int i) { return juce::jlimit (0.0f, 1.0f, peaks[i]); };

    switch (waveformMode)
    {
        // ── Mode 0 : Hard ────────────────────────────────────────────────
        default:
        case 0:
        {
            juce::Path top, bot;
            top.startNewSubPath (cx, cy);
            bot.startNewSubPath (cx, cy);
            for (int i = 0; i < n; ++i)
            {
                const float x   = xAt (i);
                const float amp = ampAt (i) * scale;
                top.lineTo (x, cy - amp);
                bot.lineTo (x, cy + amp);
            }
            top.lineTo (cx + W, cy);
            bot.lineTo (cx + W, cy);
            bot.closeSubPath();
            top.addPath (bot);

            g.setColour (waveCol.withAlpha (0.22f));
            g.fillPath (top);

            juce::Path topLine, botLine;
            topLine.startNewSubPath (cx, cy - ampAt (0) * scale);
            botLine.startNewSubPath (cx, cy + ampAt (0) * scale);
            for (int i = 1; i < n; ++i)
            {
                const float x = xAt (i);
                topLine.lineTo (x, cy - ampAt (i) * scale);
                botLine.lineTo (x, cy + ampAt (i) * scale);
            }
            g.setColour (waveCol.withAlpha (0.90f));
            g.strokePath (topLine, juce::PathStrokeType (1.4f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.strokePath (botLine, juce::PathStrokeType (1.4f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }

        // ── Mode 1 : Soft ────────────────────────────────────────────────
        case 1:
        {
            juce::ColourGradient grad (
                waveCol.withAlpha (0.0f), 0.0f, area.getY(),
                waveCol.withAlpha (0.0f), 0.0f, area.getBottom(),
                false);
            grad.addColour (0.35, waveCol.withAlpha (0.12f));
            grad.addColour (0.5,  waveCol.withAlpha (0.20f));
            grad.addColour (0.65, waveCol.withAlpha (0.12f));

            juce::Path top, bot;
            top.startNewSubPath (cx, cy);
            bot.startNewSubPath (cx, cy);
            for (int i = 0; i < n; ++i)
            {
                const float x   = xAt (i);
                const float amp = ampAt (i) * scale;
                top.lineTo (x, cy - amp);
                bot.lineTo (x, cy + amp);
            }
            top.lineTo (cx + W, cy);
            bot.lineTo (cx + W, cy);
            bot.closeSubPath();
            top.addPath (bot);

            g.setGradientFill (grad);
            g.fillPath (top);

            juce::Path topLine, botLine;
            topLine.startNewSubPath (cx, cy - ampAt (0) * scale);
            botLine.startNewSubPath (cx, cy + ampAt (0) * scale);
            for (int i = 1; i < n; ++i)
            {
                const float x = xAt (i);
                topLine.lineTo (x, cy - ampAt (i) * scale);
                botLine.lineTo (x, cy + ampAt (i) * scale);
            }
            g.setColour (waveCol.withAlpha (0.75f));
            g.strokePath (topLine, juce::PathStrokeType (1.2f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.strokePath (botLine, juce::PathStrokeType (1.2f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }

        // ── Mode 2 : Outline ─────────────────────────────────────────────
        case 2:
        {
            juce::Path top, bot;
            top.startNewSubPath (cx, cy - ampAt (0) * scale);
            bot.startNewSubPath (cx, cy + ampAt (0) * scale);
            for (int i = 1; i < n; ++i)
            {
                const float x = xAt (i);
                top.lineTo (x, cy - ampAt (i) * scale);
                bot.lineTo (x, cy + ampAt (i) * scale);
            }
            g.setColour (waveCol.withAlpha (0.35f));
            g.strokePath (top, juce::PathStrokeType (2.2f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.strokePath (bot, juce::PathStrokeType (2.2f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (waveCol.withAlpha (0.90f));
            g.strokePath (top, juce::PathStrokeType (1.0f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.strokePath (bot, juce::PathStrokeType (1.0f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }

        // ── Mode 3 : Rectified ───────────────────────────────────────────
        case 3:
        {
            const float baseline = area.getBottom() - 4.0f;
            const float rectScale = scale * 1.6f;
            juce::Path rectPath;
            rectPath.startNewSubPath (cx, baseline - ampAt (0) * rectScale);
            for (int i = 1; i < n; ++i)
                rectPath.lineTo (xAt (i), baseline - ampAt (i) * rectScale);
            rectPath.lineTo (cx + W, baseline);
            rectPath.lineTo (cx, baseline);
            rectPath.closeSubPath();

            juce::ColourGradient grad (waveCol.withAlpha (0.32f), 0.0f, area.getY(),
                                        waveCol.withAlpha (0.04f), 0.0f, baseline, false);
            g.setGradientFill (grad);
            g.fillPath (rectPath);

            juce::Path topLine;
            topLine.startNewSubPath (cx, baseline - ampAt (0) * rectScale);
            for (int i = 1; i < n; ++i)
                topLine.lineTo (xAt (i), baseline - ampAt (i) * rectScale);
            g.setColour (waveCol.withAlpha (0.85f));
            g.strokePath (topLine, juce::PathStrokeType (1.2f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }

        // ── Mode 4 : Mirrored ────────────────────────────────────────────
        case 4:
        {
            juce::Path upper;
            upper.startNewSubPath (cx, cy);
            for (int i = 0; i < n; ++i)
                upper.lineTo (xAt (i), cy - ampAt (i) * scale);
            upper.lineTo (cx + W, cy);
            upper.closeSubPath();

            juce::Path lower;
            lower.startNewSubPath (cx, cy);
            for (int i = 0; i < n; ++i)
                lower.lineTo (xAt (i), cy + ampAt (i) * scale);
            lower.lineTo (cx + W, cy);
            lower.closeSubPath();

            g.setColour (waveCol.withAlpha (0.28f));
            g.fillPath (upper);
            g.setColour (waveCol.withAlpha (0.15f));
            g.fillPath (lower);

            juce::Path edge;
            edge.startNewSubPath (cx, cy - ampAt (0) * scale);
            for (int i = 1; i < n; ++i)
                edge.lineTo (xAt (i), cy - ampAt (i) * scale);
            g.setColour (waveCol.withAlpha (0.85f));
            g.strokePath (edge, juce::PathStrokeType (1.0f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }

        // ── Mode 5 : Bars ────────────────────────────────────────────────
        case 5:
        {
            const float barW = juce::jmax (1.0f, W / (float) n);
            for (int i = 0; i < n; ++i)
            {
                const float amp  = ampAt (i) * scale;
                const float topY = cy - amp;
                float barH = amp * 2.0f;
                if (barH < 1.0f) barH = 1.0f;
                const float alpha = 0.20f + ampAt (i) * 0.55f;
                g.setColour (waveCol.withAlpha (alpha));
                g.fillRect (xAt (i), topY, barW, barH);
            }
            break;
        }

        // ── Mode 6 : RMS ─────────────────────────────────────────────────
        case 6:
        {
            juce::Path rmsPath;
            rmsPath.startNewSubPath (cx, cy - ampAt (0) * scale);
            for (int i = 1; i < n; ++i)
                rmsPath.lineTo (xAt (i), cy - ampAt (i) * scale);
            for (int i = n - 1; i >= 0; --i)
                rmsPath.lineTo (xAt (i), cy + ampAt (i) * scale);
            rmsPath.closeSubPath();

            juce::ColourGradient grad (
                waveCol.withAlpha (0.0f), 0.0f, area.getY(),
                waveCol.withAlpha (0.0f), 0.0f, area.getBottom(), false);
            grad.addColour (0.35, waveCol.withAlpha (0.18f));
            grad.addColour (0.5,  waveCol.withAlpha (0.28f));
            grad.addColour (0.65, waveCol.withAlpha (0.18f));
            g.setGradientFill (grad);
            g.fillPath (rmsPath);

            juce::Path rmsLine;
            rmsLine.startNewSubPath (cx, cy - ampAt (0) * scale);
            for (int i = 1; i < n; ++i)
                rmsLine.lineTo (xAt (i), cy - ampAt (i) * scale);
            g.setColour (waveCol.withAlpha (0.85f));
            g.strokePath (rmsLine, juce::PathStrokeType (1.2f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }

        // ── Mode 7 : Stepped ─────────────────────────────────────────────
        case 7:
        {
            const int stepEvery = juce::jmax (1, n / juce::jmax (1, (int) (W / 4.0f)));
            juce::Path stepTop, stepBot;
            bool started = false;
            for (int i = 0; i < n; i += stepEvery)
            {
                const float x = xAt (i);
                const float yTop = cy - ampAt (i) * scale;
                const float yBot = cy + ampAt (i) * scale;
                if (! started)
                {
                    stepTop.startNewSubPath (x, yTop);
                    stepBot.startNewSubPath (x, yBot);
                    started = true;
                }
                else
                {
                    stepTop.lineTo (x, stepTop.getCurrentPosition().y);
                    stepTop.lineTo (x, yTop);
                    stepBot.lineTo (x, stepBot.getCurrentPosition().y);
                    stepBot.lineTo (x, yBot);
                }
                const int xEndIdx = juce::jmin (i + stepEvery, n - 1);
                stepTop.lineTo (xAt (xEndIdx), yTop);
                stepBot.lineTo (xAt (xEndIdx), yBot);
            }

            juce::Path fillTop = stepTop;
            fillTop.lineTo (cx + W, cy);
            fillTop.lineTo (cx, cy);
            fillTop.closeSubPath();

            juce::Path fillBot = stepBot;
            fillBot.lineTo (cx + W, cy);
            fillBot.lineTo (cx, cy);
            fillBot.closeSubPath();

            g.setColour (waveCol.withAlpha (0.24f));
            g.fillPath (fillTop);
            g.setColour (waveCol.withAlpha (0.13f));
            g.fillPath (fillBot);

            g.setColour (waveCol.withAlpha (0.80f));
            g.strokePath (stepTop, juce::PathStrokeType (1.0f));
            g.strokePath (stepBot, juce::PathStrokeType (1.0f));
            break;
        }
    }

    if (cachedZoom > 1.01f)
    {
        const float windowFrac = 1.0f / cachedZoom;
        const float barY  = area.getBottom() - 3.0f;
        const float barW  = W * windowFrac;
        const float barX  = cx;
        g.setColour (sf2Lcd2Phosphor().withAlpha (0.25f));
        g.fillRoundedRectangle (barX, barY, barW, 2.0f, 1.0f);
    }
}

void Sf2WaveformLcd::drawLoopOverlay (juce::Graphics& g,
                                       const juce::Rectangle<float>& area)
{
    const int bufLoopStart = processor.sfzPlayer.getLoopStartSample();
    const int bufLoopEnd   = processor.sfzPlayer.getLoopEndSample();
    if (bufLoopStart < 0 || bufLoopEnd <= bufLoopStart || cachedTotalFrames <= 0) return;

    const float windowFrac = 1.0f / cachedZoom;
    const float maxScroll  = (float) cachedTotalFrames * (1.0f - windowFrac);
    const float startF_f   = cachedScroll * maxScroll;
    const float endF_f     = startF_f + windowFrac * (float) cachedTotalFrames;

    auto toX = [&] (int sample) -> float
    {
        const float t = ((float) sample - startF_f) / (endF_f - startF_f);
        return area.getX() + t * area.getWidth();
    };

    const float x0 = juce::jlimit (area.getX(), area.getRight(), toX (bufLoopStart));
    const float x1 = juce::jlimit (area.getX(), area.getRight(), toX (bufLoopEnd));
    if (x1 <= x0 + 1.0f) return;

    const juce::Colour loopColour { 0xFFFFE800 };
    g.setColour (loopColour.withAlpha (0.06f));
    g.fillRect  (x0, area.getY(), x1 - x0, area.getHeight());

    g.setColour (loopColour.withAlpha (0.50f));
    g.drawVerticalLine (juce::roundToInt (x0), area.getY(), area.getBottom());
    g.drawVerticalLine (juce::roundToInt (x1), area.getY(), area.getBottom());

    g.setFont (DysektLookAndFeel::makeFont (7.0f, true));
    g.setColour (loopColour.withAlpha (0.55f));
    const float labelX = (x0 + x1) * 0.5f - 18.0f;
    g.drawText ("LOOP", juce::Rectangle<float> (labelX, area.getY() + 3.0f, 36.0f, 10.0f),
                juce::Justification::centred, false);
}

void Sf2WaveformLcd::mouseWheelMove (const juce::MouseEvent& e,
                                      const juce::MouseWheelDetails& w)
{
    const auto snap = processor.sampleData3.getSnapshot();
    if (snap == nullptr || snap->buffer.getNumSamples() <= 0)
        return;

    const bool isZoom = e.mods.isCtrlDown() || e.mods.isCommandDown();

    if (isZoom)
    {
        const float anchorFrac = juce::jlimit (0.0f, 1.0f,
            (e.position.x - screenArea.getX()) / screenArea.getWidth());

        const float oldZoom   = std::max (1.0f, processor.zoom.load());
        const float newZoom   = juce::jlimit (1.0f, 32.0f,
                                              oldZoom * (w.deltaY > 0 ? 1.25f : 0.8f));
        const float oldScroll = processor.scroll.load();
        const int   total     = snap->buffer.getNumSamples();

        const float oldWindowFrac = 1.0f / oldZoom;
        const float oldStart      = oldScroll * (float) total * (1.0f - oldWindowFrac);
        const float anchorSample  = oldStart + anchorFrac * oldWindowFrac * (float) total;

        const float newWindowFrac = 1.0f / newZoom;
        const float newStart      = anchorSample - anchorFrac * newWindowFrac * (float) total;
        const float newMaxStart   = (float) total * (1.0f - newWindowFrac);
        const float newScroll     = (newMaxStart > 0.0f)
                                    ? juce::jlimit (0.0f, 1.0f, newStart / newMaxStart)
                                    : 0.0f;

        processor.zoom  .store (newZoom,   std::memory_order_relaxed);
        processor.scroll.store (newScroll, std::memory_order_relaxed);
    }
    else
    {
        const float delta = w.deltaX != 0.0f ? -w.deltaX : -w.deltaY;
        processor.scroll.store (
            juce::jlimit (0.0f, 1.0f, processor.scroll.load() + delta * 0.08f),
            std::memory_order_relaxed);
    }

    buildWaveformPeaks();
    repaint();
}

// ── Draw helpers ──────────────────────────────────────────────────────────────

void Sf2WaveformLcd::drawBackground (juce::Graphics& g)
{
    const auto ac = getTheme().accent;
    const auto b  = getLocalBounds();

    const auto bgTop = getTheme().darkBar.darker (0.45f);
    const auto bgBot = getTheme().darkBar.darker (0.65f);
    juce::ColourGradient outerGrad (bgTop, 0, 0, bgBot, 0, (float) b.getHeight(), false);
    g.setGradientFill (outerGrad);
    g.fillRoundedRectangle (b.toFloat(), 4.0f);
    g.setColour (ac.withAlpha (0.18f));
    g.drawRoundedRectangle (b.toFloat().expanded (1.0f), 5.0f, 1.0f);
    g.setColour (ac.withAlpha (0.60f));
    g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 4.0f, 1.5f);

    const auto screen = b.reduced (4);
    g.setColour (sf2Lcd2Bg());
    g.fillRoundedRectangle (screen.toFloat(), 2.0f);

    juce::ColourGradient glow (sf2Lcd2Phosphor().withAlpha (0.07f), 0, (float) screen.getY(),
                                juce::Colours::transparentBlack, 0, (float) (screen.getY() + 18), false);
    g.setGradientFill (glow);
    g.fillRoundedRectangle (screen.toFloat(), 2.0f);

    g.setColour (juce::Colour (0xFF000000).withAlpha ((uint8_t) kScanlineAlpha));
    for (int y = screen.getY(); y < screen.getBottom(); y += 2)
        g.drawHorizontalLine (y, (float) screen.getX(), (float) screen.getRight());

    g.setColour (ac.withAlpha (0.30f));
    g.drawRoundedRectangle (screen.toFloat().expanded (0.5f), 2.0f, 1.0f);
}

void Sf2WaveformLcd::drawSegmentLabel (juce::Graphics& g,
                                        float x0, float y0,
                                        float x1, float y1,
                                        const char* text,
                                        juce::Colour col,
                                        const juce::Rectangle<float>& area)
{
    const float mx = area.getX() + ((x0 + x1) * 0.5f) * area.getWidth();
    const float my = area.getY() + ((y0 + y1) * 0.5f) * area.getHeight() - 9.0f;
    g.setFont (DysektLookAndFeel::makeFont (8.0f));
    g.setColour (col.withAlpha (0.40f));
    g.drawText (juce::String (text),
                juce::Rectangle<float> (mx - 30.0f, my - 6.0f, 60.0f, 12.0f),
                juce::Justification::centred, false);
}

void Sf2WaveformLcd::drawHeader (juce::Graphics& g,
                                  const juce::Rectangle<float>& area)
{
    const float headerH = 18.0f;
    const auto  headerR = area.withHeight (headerH);

    g.setFont (DysektLookAndFeel::makeFont (8.5f, true));
    g.setColour (sf2Lcd2Phosphor().withAlpha (0.55f));
    g.drawText ("SF2 PLAYER", headerR.withRight (headerR.getX() + 72.0f),
                juce::Justification::centredLeft, false);

    // A preset-grid click is re-rendering sampleData3/previewZones3 for that
    // specific preset in the background (see SoundFontLoader::load's
    // presetBank/presetProgram params) — this can take a moment for a
    // 128-note probe+render pass, so show that explicitly instead of leaving
    // the previous preset's stale waveform up with no explanation.
    if (processor.sf2PreviewRenderInFlight.load (std::memory_order_relaxed))
    {
        g.setFont (DysektLookAndFeel::makeFont (8.0f, true));
        g.setColour (kSf2ColDecay.withAlpha (0.75f));
        g.drawText ("RENDERING PREVIEW...", headerR, juce::Justification::centredRight, true);
        return;
    }

    if (processor.sfzPlayer.isLoaded())
    {
        const juce::String name = processor.sfzPlayer.getLoadedFile()
                                      .getFileNameWithoutExtension();
        if (name.isNotEmpty())
        {
            g.setFont (DysektLookAndFeel::makeFont (8.0f));
            g.setColour (sf2Lcd2Phosphor().withAlpha (0.38f));
            g.drawText (name, headerR, juce::Justification::centredRight, true);
        }
    }
}

void Sf2WaveformLcd::drawEnvelope (juce::Graphics& g,
                                    const juce::Rectangle<float>& area)
{
    const float W  = area.getWidth();
    const float H  = area.getHeight();
    const float ox = area.getX();
    const float oy = area.getY();

    auto px = [&] (float xn) { return ox + xn * W; };
    auto py = [&] (float yn) { return oy + yn * H; };

    juce::Path envFill;
    envFill.startNewSubPath (px (0.0f),        py (1.0f));
    envFill.lineTo           (px (env.ax),     py (env.ay));
    envFill.lineTo           (px (env.dx),     py (env.sy));
    envFill.lineTo           (px (env.sxEnd),  py (env.sy));
    envFill.lineTo           (px (env.rx),     py (env.sy));
    envFill.lineTo           (px (1.0f),       py (1.0f));
    envFill.closeSubPath();

    juce::ColourGradient fillGrad (kSf2ColDecay.withAlpha (0.07f), 0, oy,
                                    kSf2ColDecay.withAlpha (0.00f), 0, oy + H, false);
    g.setGradientFill (fillGrad);
    g.fillPath (envFill);

    juce::Path envLine;
    envLine.startNewSubPath (px (0.0f),        py (1.0f));
    envLine.lineTo           (px (env.ax),     py (env.ay));
    envLine.lineTo           (px (env.dx),     py (env.sy));
    envLine.lineTo           (px (env.sxEnd),  py (env.sy));
    envLine.lineTo           (px (env.rx),     py (env.sy));
    envLine.lineTo           (px (1.0f),       py (1.0f));

    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.strokePath (envLine, juce::PathStrokeType (2.5f));

    juce::Path dashedLine;
    {
        juce::PathStrokeType stroke (1.0f);
        float dashes[] = { 3.0f, 5.0f };
        stroke.createDashedStroke (dashedLine, envLine, dashes, 2);
    }
    g.setColour (juce::Colours::white.withAlpha (0.20f));
    g.fillPath (dashedLine);

    juce::Path susLine;
    susLine.startNewSubPath (px (env.dx), py (env.sy));
    susLine.lineTo           (px (env.sxEnd), py (env.sy));
    g.setColour (kSf2ColSustain.withAlpha (0.35f));
    g.strokePath (susLine, juce::PathStrokeType (1.0f));

    drawSegmentLabel (g, 0.0f, 1.0f, env.ax, env.ay, "ATTACK",  kSf2ColAttack,  area);
    drawSegmentLabel (g, env.ax, env.ay, env.dx, env.sy, "DECAY", kSf2ColDecay, area);
    drawSegmentLabel (g, env.rx, env.sy, 1.0f, 1.0f, "RELEASE", kSf2ColRelease, area);
}

void Sf2WaveformLcd::drawNodes (juce::Graphics& g,
                                 const juce::Rectangle<float>& area)
{
    const float W  = area.getWidth();
    const float H  = area.getHeight();
    const float ox = area.getX();
    const float oy = area.getY();

    for (const auto& node : envNodes)
    {
        const float cx  = ox + node.xn * W;
        const bool  hov = (node.role == hovRole || node.role == dragRole);
        const float r   = hov ? kNodeR + 2.5f : kNodeR;

        const float compH = (float) getHeight();
        const float cyRaw = oy + node.yn * H;
        const float cy    = juce::jmax (r + 2.0f, juce::jmin (compH - r - 2.0f, cyRaw));

        if (node.role != NodeRole::Sustain)
        {
            g.setColour (node.colour.withAlpha (0.18f));
            g.drawVerticalLine (juce::roundToInt (cx), cy + r, oy + H);
        }

        g.setColour (node.colour.withAlpha (hov ? 0.55f : 0.25f));
        g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, hov ? 1.5f : 1.0f);

        const float dr = hov ? 3.0f : 2.5f;
        g.setColour (node.colour.withAlpha (hov ? 1.0f : 0.80f));
        g.fillEllipse (cx - dr, cy - dr, dr * 2.0f, dr * 2.0f);

        g.setFont (DysektLookAndFeel::makeFont (hov ? 11.0f : 9.5f, true));
        g.setColour (node.colour.withAlpha (hov ? 1.0f : 0.70f));
        g.drawText (juce::String (node.label),
                    juce::Rectangle<float> (cx - 14.0f, cy + r + 2.0f, 28.0f, 12.0f),
                    juce::Justification::centred, false);
    }
}

void Sf2WaveformLcd::drawNoInstrument (juce::Graphics& g)
{
    const auto b = getLocalBounds().reduced (4);
    g.setFont (DysektLookAndFeel::makeFont (10.0f));
    g.setColour (sf2Lcd2Dim().brighter (0.4f));
    g.drawText ("-- NO INSTRUMENT LOADED --", b, juce::Justification::centred);
}

void Sf2WaveformLcd::drawPlayhead (juce::Graphics& g, const juce::Rectangle<float>& area)
{
    // Mirrors WaveformView::drawPlaybackCursors: previewPositionSample is
    // elapsed samples since the most recent note-on, not an absolute
    // position in sampleData3. Look up the triggered note's region via
    // previewZones3 and add its startSample, or every note appears to
    // play from sample 0 (i.e. always inside the first region).
    const int elapsed = processor.sfzPlayer.getPreviewPositionSample();
    if (elapsed <= 0 || cachedTotalFrames <= 0) return;

    const int note = processor.sfzPlayer.getLastTriggeredNote();
    int regionStart = 0, regionEnd = -1;
    if (note >= 0)
    {
        auto zones = processor.previewZones3.get();
        for (const auto& z : *zones)
        {
            if (z.midiNote == note) { regionStart = z.startSample; regionEnd = z.endSample; break; }
        }
    }

    const int posSample = regionStart + elapsed;
    if (regionEnd > regionStart && posSample >= regionEnd) return;   // past this region's audio

    const float windowFrac = 1.0f / cachedZoom;
    const float maxScroll  = (float) cachedTotalFrames * (1.0f - windowFrac);
    const float startF_f   = cachedScroll * maxScroll;
    const float endF_f     = startF_f + windowFrac * (float) cachedTotalFrames;
    if (endF_f <= startF_f) return;

    const float t = ((float) posSample - startF_f) / (endF_f - startF_f);
    if (t < 0.0f || t > 1.0f) return;

    const float px = area.getX() + t * area.getWidth();

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.drawVerticalLine (juce::roundToInt (px), area.getY(), area.getBottom());

    juce::Path tri;
    tri.addTriangle (px - 5.0f, area.getY(),
                      px + 5.0f, area.getY(),
                      px,        area.getY() + 8.0f);
    g.fillPath (tri);
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void Sf2WaveformLcd::paint (juce::Graphics& g)
{
    {
        juce::Path clipPath;
        clipPath.addRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        g.reduceClipRegion (clipPath);
    }

    drawBackground (g);

    const bool loaded   = processor.sfzPlayer.isLoaded();
    const auto nodeArea = getLocalBounds().reduced (4).toFloat();
    screenArea = nodeArea;
    const auto lcdArea  = nodeArea.reduced (2.0f);

    if (! loaded)
    {
        drawNoInstrument (g);
        return;
    }

    drawHeader           (g, lcdArea);
    drawWaveformBackdrop (g, lcdArea);
    drawLoopOverlay      (g, lcdArea);
    drawEnvelope         (g, lcdArea);
    drawNodes            (g, nodeArea);
    drawPlayhead         (g, lcdArea);
}
