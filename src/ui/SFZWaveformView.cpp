#include "SFZWaveformView.h"
#include "UIHelpers.h"
#include "DysektLookAndFeel.h"
#include "../PluginProcessor.h"

// =============================================================================
//  Constructor
// =============================================================================

SFZWaveformView::SFZWaveformView (DysektProcessor& p)
    : processor (p)
{
    setOpaque (false);
}

// =============================================================================
//  Resize
// =============================================================================

void SFZWaveformView::resized()
{
    prevCacheKey = {};
}

// =============================================================================
//  Timer tick  (call at ~30 Hz from DysektEditor::timerCallback)
// =============================================================================

void SFZWaveformView::timerTick()
{
    repaint();
}

// =============================================================================
//  View helpers
// =============================================================================

int SFZWaveformView::sampleToPixel (int sample) const
{
    if (cachedNumFrames <= 0 || getWidth() <= 0) return 0;
    const int startSample = cachedVisibleStart;
    const int lenSamples  = cachedVisibleLen;
    if (lenSamples <= 0) return 0;
    return (int) ((float) (sample - startSample) / (float) lenSamples * (float) getWidth());
}

int SFZWaveformView::pixelToSample (int px) const
{
    if (getWidth() <= 0) return 0;
    return cachedVisibleStart
         + (int) ((float) px / (float) getWidth() * (float) cachedVisibleLen);
}

void SFZWaveformView::rebuildCacheIfNeeded()
{
    auto sampleSnap = processor.sampleData2.getSnapshot();
    if (sampleSnap == nullptr) return;

    const int numFrames  = sampleSnap->buffer.getNumSamples();
    const int width      = getWidth();
    if (numFrames <= 0 || width <= 0) return;

    const CacheKey key { cachedVisibleStart, cachedVisibleLen, width, numFrames, sampleSnap.get() };
    if (key == prevCacheKey) return;

    cache.rebuild (sampleSnap->buffer, sampleSnap->peakMipmaps,
                   numFrames, processor.zoom.load(), processor.scroll.load(), width);
    prevCacheKey = key;
}

// =============================================================================
//  Paint
// =============================================================================

void SFZWaveformView::paint (juce::Graphics& g)
{
    if (! isVisible() || getWidth() <= 0 || getHeight() <= 0)
        return;

    g.reduceClipRegion (getLocalBounds());
    g.fillAll (getTheme().waveformBg);

    // Grid lines
    const int cy = getHeight() / 2;
    g.setColour (getTheme().gridLine.withAlpha (0.5f));
    g.drawHorizontalLine (cy, 0.0f, (float) getWidth());
    g.setColour (getTheme().gridLine.withAlpha (0.2f));
    g.drawHorizontalLine (getHeight() / 4,     0.0f, (float) getWidth());
    g.drawHorizontalLine (getHeight() * 3 / 4, 0.0f, (float) getWidth());

    auto sampleSnap = processor.sampleData2.getSnapshot();
    if (sampleSnap == nullptr || sampleSnap->buffer.getNumSamples() <= 0)
    {
        drawPlaceholder (g);
        if (dragHighlight)
        {
            g.setColour (getTheme().accent.withAlpha (0.25f));
            g.fillAll();
            g.setColour (getTheme().accent.withAlpha (0.90f));
            g.drawRect (getLocalBounds(), 2);
        }
        return;
    }

    // Build viewport
    const int   numFrames  = sampleSnap->buffer.getNumSamples();
    const float z          = std::max (1.0f, processor.zoom.load());
    const float sc         = processor.scroll.load();
    const int   visibleLen = juce::jlimit (1, numFrames, (int) ((float) numFrames / z));
    const int   maxStart   = juce::jmax (0, numFrames - visibleLen);
    const int   visStart   = juce::jlimit (0, maxStart, (int) (sc * (float) maxStart));

    // Cache for sampleToPixel / pixelToSample during this paint
    cachedVisibleStart = visStart;
    cachedVisibleLen   = visibleLen;
    cachedNumFrames    = numFrames;
    paintStateActive   = true;

    rebuildCacheIfNeeded();

    drawWaveform        (g);
    drawLoopOverlay     (g);
    drawPlaybackCursors (g);

    paintStateActive = false;

    // Drag-over highlight
    if (dragHighlight)
    {
        g.setColour (getTheme().accent.withAlpha (0.18f));
        g.fillAll();
        g.setColour (getTheme().accent.withAlpha (0.90f));
        g.drawRect (getLocalBounds(), 2);
    }
}

// =============================================================================
//  Draw helpers
// =============================================================================

void SFZWaveformView::drawWaveform (juce::Graphics& g)
{
    const int   cy       = getHeight() / 2;
    const float scale    = (float) getHeight() * UILayout::waveformVerticalScale;
    auto&       peaks    = cache.getPeaks();
    const int   numPeaks = std::min (cache.getNumPeaks(), getWidth());
    if (numPeaks <= 0) return;

    const juce::Colour waveCol = getTheme().waveform;

    if (waveformMode == 1)
    {
        // ── Soft mode: gradient fill + glowing edge lines ──────────────────────
        juce::Path fillPath;
        fillPath.startNewSubPath (0.0f, (float) cy - peaks[0].maxVal * scale);
        for (int px = 1; px < numPeaks; ++px)
            fillPath.lineTo ((float) px, (float) cy - peaks[(size_t) px].maxVal * scale);
        for (int px = numPeaks - 1; px >= 0; --px)
            fillPath.lineTo ((float) px, (float) cy - peaks[(size_t) px].minVal * scale);
        fillPath.closeSubPath();

        const int h = getHeight();
        juce::ColourGradient grad (waveCol.withAlpha (0.0f), 0, 0,
                                   waveCol.withAlpha (0.0f), 0, (float) h, false);
        grad.addColour (0.35, waveCol.withAlpha (0.18f));
        grad.addColour (0.5,  waveCol.withAlpha (0.28f));
        grad.addColour (0.65, waveCol.withAlpha (0.18f));
        g.setGradientFill (grad);
        g.fillPath (fillPath);

        // Top and bottom edge lines
        juce::Path topPath, botPath;
        topPath.startNewSubPath (0.0f, (float) cy - peaks[0].maxVal * scale);
        botPath.startNewSubPath (0.0f, (float) cy - peaks[0].minVal * scale);
        for (int px = 1; px < numPeaks; ++px)
        {
            topPath.lineTo ((float) px, (float) cy - peaks[(size_t) px].maxVal * scale);
            botPath.lineTo ((float) px, (float) cy - peaks[(size_t) px].minVal * scale);
        }

        g.setColour (waveCol.withAlpha (0.25f));
        g.strokePath (topPath, juce::PathStrokeType (3.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.strokePath (botPath, juce::PathStrokeType (3.5f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (waveCol.withAlpha (0.90f));
        g.strokePath (topPath, juce::PathStrokeType (1.3f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.strokePath (botPath, juce::PathStrokeType (1.3f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    else
    {
        // ── Hard mode: solid fill ──────────────────────────────────────────────
        juce::Path fillPath;
        fillPath.startNewSubPath (0.0f, (float) cy - peaks[0].maxVal * scale);
        for (int px = 1; px < numPeaks; ++px)
            fillPath.lineTo ((float) px, (float) cy - peaks[(size_t) px].maxVal * scale);
        for (int px = numPeaks - 1; px >= 0; --px)
            fillPath.lineTo ((float) px, (float) cy - peaks[(size_t) px].minVal * scale);
        fillPath.closeSubPath();

        g.setColour (waveCol);
        g.fillPath (fillPath);
    }
}

void SFZWaveformView::drawLoopOverlay (juce::Graphics& g)
{
    if (cachedNumFrames <= 0) return;

    const int loopStart = processor.sfzPlayer2.getLoopStartSample();
    const int loopEnd   = processor.sfzPlayer2.getLoopEndSample();
    if (loopStart < 0 || loopEnd <= loopStart) return;

    const int x0 = juce::jlimit (0, getWidth() - 1, sampleToPixel (loopStart));
    const int x1 = juce::jlimit (0, getWidth(),      sampleToPixel (loopEnd));
    if (x1 <= x0) return;

    const juce::Colour loopCol { 0xFFFFE800 };   // Radioactive Yellow — matches SfzWaveformLcd

    g.setColour (loopCol.withAlpha (0.08f));
    g.fillRect (x0, 0, x1 - x0, getHeight());

    g.setColour (loopCol.withAlpha (0.60f));
    g.drawVerticalLine (x0, 0.0f, (float) getHeight());
    g.drawVerticalLine (x1, 0.0f, (float) getHeight());

    // Loop label centred in the region
    const float labelCx = ((float) x0 + (float) x1) * 0.5f;
    g.setFont (DysektLookAndFeel::makeFont (9.0f, true));
    g.setColour (loopCol.withAlpha (0.65f));
    g.drawText ("LOOP",
                juce::Rectangle<float> (labelCx - 22.0f, 4.0f, 44.0f, 12.0f),
                juce::Justification::centred, false);
}

void SFZWaveformView::drawPlaybackCursors (juce::Graphics& g)
{
    const int h = getHeight();
    const int w = getWidth();

    for (int i = 0; i < VoicePool::kMaxVoices; ++i)
    {
        const float pos = processor.voicePool.voicePositions[i].load (std::memory_order_relaxed);
        if (pos <= 0.0f) continue;

        const int px = sampleToPixel ((int) pos);
        if (px < 0 || px >= w) continue;

        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawVerticalLine (px, 0.0f, (float) h);

        juce::Path tri;
        tri.addTriangle ((float) px - 5.0f, 0.0f,
                         (float) px + 5.0f, 0.0f,
                         (float) px,         8.0f);
        g.fillPath (tri);
    }
}

void SFZWaveformView::drawPlaceholder (juce::Graphics& g)
{
    g.setColour (getTheme().foreground.withAlpha (0.25f));
    g.setFont (DysektLookAndFeel::makeFont (22.0f));
    g.drawText ("DROP SFZ / SF2 FILE", getLocalBounds(), juce::Justification::centred);
}

// =============================================================================
//  Mouse wheel  —  zoom + scroll, identical behaviour to WaveformView
// =============================================================================

void SFZWaveformView::mouseWheelMove (const juce::MouseEvent& e,
                                      const juce::MouseWheelDetails& w)
{
    if (w.deltaX != 0.0f)
    {
        float sc = processor.scroll.load();
        sc -= w.deltaX * 0.05f;
        processor.scroll.store (juce::jlimit (0.0f, 1.0f, sc));
        prevCacheKey = {};
        return;
    }

    if (e.mods.isShiftDown())
    {
        float sc = processor.scroll.load();
        sc -= w.deltaY * 0.05f;
        processor.scroll.store (juce::jlimit (0.0f, 1.0f, sc));
    }
    else
    {
        const int   width       = getWidth();
        const float oldZoom     = processor.zoom.load();
        const float oldViewFrac = 1.0f / oldZoom;
        const float oldScroll   = processor.scroll.load();
        const float cursorFrac  = (width > 0) ? (float) e.x / (float) width : 0.5f;
        const float newZoom     = (w.deltaY > 0)
                                    ? std::min (16384.0f, oldZoom * 1.2f)
                                    : std::max (1.0f,     oldZoom / 1.2f);
        processor.zoom.store (newZoom);

        const float newViewFrac = 1.0f / newZoom;
        const float maxScroll   = 1.0f - newViewFrac;
        if (maxScroll > 0.0f)
        {
            const float oldViewStart = oldScroll * (1.0f - oldViewFrac);
            const float anchorFrac   = oldViewStart + cursorFrac * oldViewFrac;
            const float newViewStart = anchorFrac   - cursorFrac * newViewFrac;
            processor.scroll.store (juce::jlimit (0.0f, 1.0f, newViewStart / maxScroll));
        }
        else
        {
            processor.scroll.store (0.0f);
        }
    }
    prevCacheKey = {};
}

// =============================================================================
//  File drag-and-drop
// =============================================================================

bool SFZWaveformView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".sfz")   // SFZ-PLAYER is a .sfz-only engine — .sf2 is not accepted here
            return true;
    }
    return false;
}

void SFZWaveformView::fileDragEnter (const juce::StringArray& files, int, int)
{
    if (isInterestedInFileDrag (files))
    {
        dragHighlight = true;
        repaint();
    }
}

void SFZWaveformView::fileDragExit (const juce::StringArray&)
{
    dragHighlight = false;
    repaint();
}

void SFZWaveformView::filesDropped (const juce::StringArray& files, int, int)
{
    dragHighlight = false;
    repaint();

    if (files.isEmpty()) return;

    const juce::File f (files[0]);
    const auto ext = f.getFileExtension().toLowerCase();
    if (ext == ".sfz")   // SFZ-PLAYER is a .sfz-only engine — .sf2 is ignored
    {
        processor.zoom.store   (1.0f);
        processor.scroll.store (0.0f);
        prevCacheKey = {};
        processor.loadSoundFontAsync (f, SoundFontLoadTarget::SfzPlayer2);  // waveform preview → sampleData2
    }
}
