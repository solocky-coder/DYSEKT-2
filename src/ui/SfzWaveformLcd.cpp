#include "SfzWaveformLcd.h"
#include "DysektLookAndFeel.h"
#include "../PluginProcessor.h"

// ── Theme helpers ─────────────────────────────────────────────────────────────
static juce::Colour sfzLcd2Bg()       { return getTheme().darkBar.darker (0.55f); }
static juce::Colour sfzLcd2Phosphor() { return getTheme().accent; }
static juce::Colour sfzLcd2Dim()      { return getTheme().accent.withAlpha (0.15f).overlaidWith (sfzLcd2Bg()); }

// Toxic-Candy node colours — same as SliceWaveformLcd so the two panels match.
static const juce::Colour kSfzColAttack  { 0xFF00FF87 }; // Toxic Lime
static const juce::Colour kSfzColDecay   { 0xFFFFE800 }; // Radioactive Yellow
static const juce::Colour kSfzColSustain { 0xFF00C8FF }; // Ice Blue
static const juce::Colour kSfzColRelease { 0xFFFF6B00 }; // Molten Orange

// ── Constructor ───────────────────────────────────────────────────────────────

SfzWaveformLcd::SfzWaveformLcd (DysektProcessor& p)
    : processor (p)
{
    setOpaque (false);
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void SfzWaveformLcd::resized()
{
    screenArea = getLocalBounds().reduced (4).toFloat();
}

// ── Timer-driven repaint ──────────────────────────────────────────────────────

void SfzWaveformLcd::repaintLcd()
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

// ── Envelope: read sfzPlayer ADSR → normalised env + envNodes ─────────────────

void SfzWaveformLcd::buildEnvelopeNodes()
{
    const float attackMs  = processor.sfzPlayer2.getSfzAttack()  * 1000.0f;
    const float decayMs   = processor.sfzPlayer2.getSfzDecay()   * 1000.0f;
    const float sustainPc = processor.sfzPlayer2.getSfzSustain();   // already 0-100
    const float releaseMs = processor.sfzPlayer2.getSfzRelease() * 1000.0f;

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
    a.colour = kSfzColAttack; a.label = "A"; envNodes.add (a);

    EnvNode d; d.xn = env.dx; d.yn = env.sy; d.role = NodeRole::Decay;
    d.colour = kSfzColDecay;  d.label = "D"; envNodes.add (d);

    EnvNode s;
    s.xn = (env.dx + env.sxEnd) * 0.5f; s.yn = env.sy;
    s.role = NodeRole::Sustain; s.colour = kSfzColSustain; s.label = "S";
    envNodes.add (s);

    EnvNode r; r.xn = env.rx; r.yn = env.sy; r.role = NodeRole::Release;
    r.colour = kSfzColRelease; r.label = "R"; envNodes.add (r);
}

// ── Commit: inverse-map env → sfzPlayer setters ───────────────────────────────

void SfzWaveformLcd::commitNodes()
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

    // Direct atomic writes — no APVTS
    if (dragRole == NodeRole::Attack)
        processor.sfzPlayer2.setSfzAttack  (attackMs  / 1000.0f);
    else if (dragRole == NodeRole::Decay)
        processor.sfzPlayer2.setSfzDecay   (decayMs   / 1000.0f);
    else if (dragRole == NodeRole::Sustain)
        processor.sfzPlayer2.setSfzSustain (sustainPc);
    else if (dragRole == NodeRole::Release)
        processor.sfzPlayer2.setSfzRelease (releaseMs / 1000.0f);

    postCommitGuard = 4;
}

// ── Hit testing ───────────────────────────────────────────────────────────────

SfzWaveformLcd::NodeRole SfzWaveformLcd::hitTest (juce::Point<float> pos) const
{
    if (screenArea.isEmpty()) return NodeRole::None;

    const float W  = screenArea.getWidth();
    const float H  = screenArea.getHeight();
    const float ox = screenArea.getX();
    const float oy = screenArea.getY();

    NodeRole best  = NodeRole::None;
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

void SfzWaveformLcd::mouseMove (const juce::MouseEvent& e)
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

void SfzWaveformLcd::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown()) return;   // no lock in SF-player mode
    dragRole = hitTest (e.position);
}

void SfzWaveformLcd::mouseDrag (const juce::MouseEvent& e)
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

    // Rebuild envNodes from updated env.*
    envNodes.clear();
    EnvNode a; a.xn = env.ax; a.yn = env.ay; a.role = NodeRole::Attack;
    a.colour = kSfzColAttack; a.label = "A"; envNodes.add (a);
    EnvNode d; d.xn = env.dx; d.yn = env.sy; d.role = NodeRole::Decay;
    d.colour = kSfzColDecay;  d.label = "D"; envNodes.add (d);
    EnvNode s; s.xn = (env.dx + env.sxEnd) * 0.5f; s.yn = env.sy;
    s.role = NodeRole::Sustain; s.colour = kSfzColSustain; s.label = "S"; envNodes.add (s);
    EnvNode r; r.xn = env.rx; r.yn = env.sy; r.role = NodeRole::Release;
    r.colour = kSfzColRelease; r.label = "R"; envNodes.add (r);

    commitNodes();
    repaint();
}

void SfzWaveformLcd::mouseUp (const juce::MouseEvent&)
{
    dragRole        = NodeRole::None;
    postCommitGuard = 6;
    repaint();
}

// ── Waveform backdrop ─────────────────────────────────────────────────────────

void SfzWaveformLcd::buildWaveformPeaks()
{
    peaks.clearQuick();
    peaks.insertMultiple (-1, 0.0f, kPeaks);

    const auto snap = processor.sampleData2.getSnapshot();
    if (snap == nullptr) return;
    const int totalFrames = snap->buffer.getNumSamples();
    if (totalFrames <= 0) return;

    const float zoom   = std::max (1.0f, processor.zoom.load());
    const float scroll = processor.scroll.load();

    // Visible window in [0..totalFrames)
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
        peaks.set (i, DysektProcessor::getWaveformPeakAtIn (processor.sampleData2, pos));
    }
}

void SfzWaveformLcd::drawWaveformBackdrop (juce::Graphics& g,
                                            const juce::Rectangle<float>& area)
{
    if (peaks.isEmpty()) return;

    const float W  = area.getWidth();
    const float H  = area.getHeight();
    const float cx = area.getX();
    const float cy = area.getY() + H * 0.5f;

    const auto col = sfzLcd2Phosphor().withAlpha (0.14f);

    juce::Path top, bot;
    top.startNewSubPath (cx, cy);
    bot.startNewSubPath (cx, cy);

    const int n = peaks.size();
    for (int i = 0; i < n; ++i)
    {
        const float x   = cx + ((float) i / (float) n) * W;
        const float amp = juce::jlimit (0.0f, 1.0f, peaks[i]) * H * 0.45f;
        top.lineTo (x, cy - amp);
        bot.lineTo (x, cy + amp);
    }
    top.lineTo (cx + W, cy);
    bot.lineTo (cx + W, cy);

    bot.closeSubPath();
    top.addPath (bot);

    g.setColour (col);
    g.fillPath (top);

    // Draw zoom/scroll position indicator bar at bottom of backdrop
    if (cachedZoom > 1.01f)
    {
        const float windowFrac = 1.0f / cachedZoom;
        const float maxScroll  = 1.0f - windowFrac;
        const float scrollFrac = (maxScroll > 0.0f)
                                 ? juce::jlimit (0.0f, 1.0f, cachedScroll * maxScroll / maxScroll)
                                 : 0.0f;
        const float barY  = area.getBottom() - 3.0f;
        const float barW  = W * windowFrac;
        const float barX  = cx + scrollFrac * (W - barW);
        g.setColour (sfzLcd2Phosphor().withAlpha (0.25f));
        g.fillRoundedRectangle (barX, barY, barW, 2.0f, 1.0f);
    }
}

void SfzWaveformLcd::drawLoopOverlay (juce::Graphics& g,
                                       const juce::Rectangle<float>& area)
{
    // Loop points are stored as concat-buffer frame offsets by SoundFontLoader
    // for both SFZ (text-opcode parse) and SF2 (SHDR binary parse).
    const int bufLoopStart = processor.sfzPlayer2.getLoopStartSample();
    const int bufLoopEnd   = processor.sfzPlayer2.getLoopEndSample();
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

    const float labelX = (x0 + x1) * 0.5f - 18.0f;
    const float labelY = area.getY() + 3.0f;
    g.setFont (DysektLookAndFeel::makeFont (7.0f, true));
    g.setColour (loopColour.withAlpha (0.55f));
    g.drawText ("LOOP", juce::Rectangle<float> (labelX, labelY, 36.0f, 10.0f),
                juce::Justification::centred, false);
}


void SfzWaveformLcd::mouseWheelMove (const juce::MouseEvent& e,
                                      const juce::MouseWheelDetails& w)
{
    const auto snap = processor.sampleData2.getSnapshot();
    if (snap == nullptr || snap->buffer.getNumSamples() <= 0)
        return;

    const bool isZoom = e.mods.isCtrlDown() || e.mods.isCommandDown();

    if (isZoom)
    {
        // Ctrl+scroll = zoom around cursor position
        const float anchorFrac = juce::jlimit (0.0f, 1.0f,
            (e.position.x - screenArea.getX()) / screenArea.getWidth());

        const float oldZoom   = std::max (1.0f, processor.zoom.load());
        const float newZoom   = juce::jlimit (1.0f, 32.0f,
                                              oldZoom * (w.deltaY > 0 ? 1.25f : 0.8f));
        const float oldScroll = processor.scroll.load();
        const int   total     = snap->buffer.getNumSamples();

        // Keep anchor sample under cursor
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
        // Scroll (pan)
        const float delta    = w.deltaX != 0.0f ? -w.deltaX : -w.deltaY;
        const float sc       = processor.scroll.load();
        const float newSc    = juce::jlimit (0.0f, 1.0f, sc + delta * 0.08f);
        processor.scroll.store (newSc, std::memory_order_relaxed);
    }

    buildWaveformPeaks();
    repaint();
}

// ── Draw helpers ──────────────────────────────────────────────────────────────

void SfzWaveformLcd::drawBackground (juce::Graphics& g)
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
    g.setColour (sfzLcd2Bg());
    g.fillRoundedRectangle (screen.toFloat(), 2.0f);

    juce::ColourGradient glow (sfzLcd2Phosphor().withAlpha (0.07f), 0, (float) screen.getY(),
                                juce::Colours::transparentBlack, 0, (float) (screen.getY() + 18), false);
    g.setGradientFill (glow);
    g.fillRoundedRectangle (screen.toFloat(), 2.0f);

    g.setColour (juce::Colour (0xFF000000).withAlpha ((uint8_t) kScanlineAlpha));
    for (int y = screen.getY(); y < screen.getBottom(); y += 2)
        g.drawHorizontalLine (y, (float) screen.getX(), (float) screen.getRight());

    g.setColour (ac.withAlpha (0.30f));
    g.drawRoundedRectangle (screen.toFloat().expanded (0.5f), 2.0f, 1.0f);
}

void SfzWaveformLcd::drawSegmentLabel (juce::Graphics& g,
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

void SfzWaveformLcd::drawHeader (juce::Graphics& g,
                                   const juce::Rectangle<float>& area)
{
    // ── "SF PLAYER" tag + loaded instrument name ──────────────────────────────
    const float headerH = 18.0f;
    const auto  headerR = area.withHeight (headerH);

    g.setFont (DysektLookAndFeel::makeFont (8.5f, true));
    g.setColour (sfzLcd2Phosphor().withAlpha (0.55f));
    g.drawText ("SF PLAYER", headerR.withRight (headerR.getX() + 64.0f),
                juce::Justification::centredLeft, false);

    if (processor.sfzPlayer2.isLoaded())
    {
        const juce::String name = processor.sfzPlayer2.getLoadedFile()
                                      .getFileNameWithoutExtension();
        if (name.isNotEmpty())
        {
            g.setFont (DysektLookAndFeel::makeFont (8.0f));
            g.setColour (sfzLcd2Phosphor().withAlpha (0.38f));
            g.drawText (name, headerR, juce::Justification::centredRight, true);
        }
    }
}

void SfzWaveformLcd::drawEnvelope (juce::Graphics& g,
                                    const juce::Rectangle<float>& area)
{
    const float W  = area.getWidth();
    const float H  = area.getHeight();
    const float ox = area.getX();
    const float oy = area.getY();

    auto px = [&] (float xn) { return ox + xn * W; };
    auto py = [&] (float yn) { return oy + yn * H; };

    // Filled region
    juce::Path envFill;
    envFill.startNewSubPath (px (0.0f),        py (1.0f));
    envFill.lineTo           (px (env.ax),     py (env.ay));
    envFill.lineTo           (px (env.dx),     py (env.sy));
    envFill.lineTo           (px (env.sxEnd),  py (env.sy));
    envFill.lineTo           (px (env.rx),     py (env.sy));
    envFill.lineTo           (px (1.0f),       py (1.0f));
    envFill.closeSubPath();

    juce::ColourGradient fillGrad (kSfzColDecay.withAlpha (0.07f), 0, oy,
                                    kSfzColDecay.withAlpha (0.00f), 0, oy + H, false);
    g.setGradientFill (fillGrad);
    g.fillPath (envFill);

    // Polyline
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

    // Sustain plateau highlight
    juce::Path susLine;
    susLine.startNewSubPath (px (env.dx), py (env.sy));
    susLine.lineTo           (px (env.sxEnd), py (env.sy));
    g.setColour (kSfzColSustain.withAlpha (0.35f));
    g.strokePath (susLine, juce::PathStrokeType (1.0f));

    // Segment labels
    drawSegmentLabel (g, 0.0f, 1.0f, env.ax, env.ay, "ATTACK",  kSfzColAttack,  area);
    drawSegmentLabel (g, env.ax, env.ay, env.dx, env.sy, "DECAY", kSfzColDecay, area);
    drawSegmentLabel (g, env.rx, env.sy, 1.0f, 1.0f, "RELEASE", kSfzColRelease, area);
}

void SfzWaveformLcd::drawNodes (juce::Graphics& g,
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

        // Tick line down from node to envelope bottom
        if (node.role != NodeRole::Sustain)
        {
            g.setColour (node.colour.withAlpha (0.18f));
            g.drawVerticalLine (juce::roundToInt (cx), cy + r, oy + H);
        }

        // Hollow ring + inner dot (unlocked style — no lock concept in SF mode)
        g.setColour (node.colour.withAlpha (hov ? 0.55f : 0.25f));
        g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, hov ? 1.5f : 1.0f);

        const float dr = hov ? 3.0f : 2.5f;
        g.setColour (node.colour.withAlpha (hov ? 1.0f : 0.80f));
        g.fillEllipse (cx - dr, cy - dr, dr * 2.0f, dr * 2.0f);

        // Letter label below node
        g.setFont (DysektLookAndFeel::makeFont (hov ? 11.0f : 9.5f, true));
        g.setColour (node.colour.withAlpha (hov ? 1.0f : 0.70f));
        g.drawText (juce::String (node.label),
                    juce::Rectangle<float> (cx - 14.0f, cy + r + 2.0f, 28.0f, 12.0f),
                    juce::Justification::centred, false);
    }
}

void SfzWaveformLcd::drawNoInstrument (juce::Graphics& g)
{
    const auto b = getLocalBounds().reduced (4);
    g.setFont (DysektLookAndFeel::makeFont (10.0f));
    g.setColour (sfzLcd2Dim().brighter (0.4f));
    g.drawText ("-- NO INSTRUMENT LOADED --", b, juce::Justification::centred);
}

void SfzWaveformLcd::drawPlayhead (juce::Graphics& g, const juce::Rectangle<float>& area)
{
    // sfizz live engine removed; previewZones2 removed. Playback cursor
    // for SFZ-PLAYER is now driven by voicePool2.voicePositions in the
    // WaveformView layer. Nothing to draw here.
    juce::ignoreUnused (g, area);
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void SfzWaveformLcd::paint (juce::Graphics& g)
{
    {
        juce::Path clipPath;
        clipPath.addRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        g.reduceClipRegion (clipPath);
    }

    drawBackground (g);

    const bool loaded = processor.sfzPlayer2.isLoaded();
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
    drawNodes    (g, nodeArea);
    drawPlayhead (g, lcdArea);
}
