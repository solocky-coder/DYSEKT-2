#include "WaveformView.h"
#include "UIHelpers.h"
#include "DysektLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../audio/AudioAnalysis.h"

// ── Playback cursors (one per active voice) ───────────────────────────────────
void WaveformView::drawPlaybackCursors (juce::Graphics& g)
{
 const int h = getHeight();
 const int w = getWidth();

 // The MIDI slice preview voice is drawn exclusively by paintLazyChopOverlay
 // (Slicer mode only — SFZ-PLAYER has no Lazy Chop/preview-voice concept).
 // Skipping it here prevents a ghost white line from persisting after MIDI slice stops.
 const int midiPreviewVoice = LazyChopEngine::getPreviewVoiceIndex();
 const bool sfzMode = isSfzPlayer2Mode();

 auto& pool = activeVoicePool();
 for (int i = 0; i < VoicePool::kMaxVoices; ++i)
 {
 if (! sfzMode && i == midiPreviewVoice)
 continue;

 float pos = pool.voicePositions[i].load (std::memory_order_relaxed);
 if (pos <= 0.0f)
 continue;

 int px = sampleToPixel ((int) pos);
 if (px < 0 || px >= w)
 continue;

 // White vertical line
 g.setColour (juce::Colours::white.withAlpha (0.85f));
 g.drawVerticalLine (px, 0.0f, (float) h);

 // Downward triangle at top
 juce::Path tri;
 tri.addTriangle ((float) px - 5.0f, 0.0f,
 (float) px + 5.0f, 0.0f,
 (float) px, 8.0f);
 g.fillPath (tri);
 }
}

void WaveformView::getTrimBounds(int& outStart, int& outEnd) const
{
 outStart = trimInPoint;
 outEnd = trimOutPoint;
}

WaveformView::WaveformView (DysektProcessor& p) : processor (p)
{
 // ── MIDI Slice button ─────────────────────────────────────────────────────
 midiSliceBtn.setButtonText (juce::CharPointer_UTF8 ("\xe2\x97\x8f MIDI SLICE \xe2\x80\x94 Click to finish"));
 midiSliceBtn.setToggleable (true);
 midiSliceBtn.setClickingTogglesState (false);
 midiSliceBtn.setToggleState (true, juce::dontSendNotification);
 midiSliceBtn.setVisible (false);
 addChildComponent (midiSliceBtn);

 midiSliceBtn.onClick = [this]
 {
 DysektProcessor::Command cmd;
 cmd.type = DysektProcessor::CmdLazyChopStop;
 processor.pushCommand (cmd);
 setMidiSliceActive (false);
 };
}

bool WaveformView::hasActiveSlicePreview() const noexcept
{
 if (dragSliceIdx < 0)
 return false;
 return dragMode == DragEdgeLeft || dragMode == MoveSlice;
}

bool WaveformView::getActiveSlicePreview (int& sliceIdx, int& startSample, int& endSample) const
{
 if (! hasActiveSlicePreview())
 return false;
 sliceIdx = dragSliceIdx;
 startSample = dragPreviewStart;
 endSample = dragPreviewEnd;
 return true;
}

bool WaveformView::getLinkedSlicePreview (int& sliceIdx, int& startSample, int& endSample) const
{
 if (linkedSliceIdx < 0 || dragMode == None)
 return false;
 sliceIdx = linkedSliceIdx;
 startSample = linkedPreviewStart;
 endSample = linkedPreviewEnd;
 return true;
}

bool WaveformView::isInteracting() const noexcept
{
 return dragMode != None || midDragging || shiftPreviewActive;
}

bool WaveformView::isSfzPlayer2Mode() const noexcept
{
    return processor.midiRouteMode.load (std::memory_order_relaxed)
           == static_cast<int> (DysektProcessor::MidiRouteMode::SfzPlayer2);
}

SampleData& WaveformView::activeSampleData() const noexcept
{
    return isSfzPlayer2Mode() ? processor.sampleData2 : processor.sampleData;
}

SliceManager& WaveformView::activeSliceManager() const noexcept
{
    return isSfzPlayer2Mode() ? processor.sliceManager2 : processor.sliceManager;
}

VoicePool& WaveformView::activeVoicePool() const noexcept
{
    return isSfzPlayer2Mode() ? processor.voicePool2 : processor.voicePool;
}

WaveformView::ViewState WaveformView::buildViewState (const SampleData::SnapshotPtr& sampleSnap) const
{
 ViewState state;
 if (sampleSnap == nullptr)
 return state;
 const int numFrames = sampleSnap->buffer.getNumSamples();
 const int width = getWidth();
 if (numFrames <= 0 || width <= 0)
 return state;
 const float z = std::max (1.0f, processor.zoom.load());
 const float sc = processor.scroll.load();
 const int visibleLen = juce::jlimit (1, numFrames, (int) (numFrames / z));
 const int maxStart = juce::jmax (0, numFrames - visibleLen);
 const int visibleStart = juce::jlimit (0, maxStart, (int) (sc * (float) maxStart));
 state.numFrames = numFrames;
 state.visibleStart = visibleStart;
 state.visibleLen = visibleLen;
 state.width = width;
 state.samplesPerPixel = (float) visibleLen / (float) width;
 state.valid = true;
 return state;
}

int WaveformView::pixelToSample (int px) const
{
 if (paintViewStateActive && cachedPaintViewState.valid)
 return cachedPaintViewState.visibleStart
 + (int) ((float) px / (float) cachedPaintViewState.width * cachedPaintViewState.visibleLen);
 const auto state = buildViewState (activeSampleData().getSnapshot());
 if (! state.valid) return 0;
 return state.visibleStart + (int) ((float) px / (float) state.width * state.visibleLen);
}

int WaveformView::sampleToPixel (int sample) const
{
 if (paintViewStateActive && cachedPaintViewState.valid)
 return (int) ((float) (sample - cachedPaintViewState.visibleStart)
 / (float) cachedPaintViewState.visibleLen
 * (float) cachedPaintViewState.width);
 const auto state = buildViewState (activeSampleData().getSnapshot());
 if (! state.valid) return 0;
 return (int) ((float) (sample - state.visibleStart) / (float) state.visibleLen * (float) state.width);
}

void WaveformView::rebuildCacheIfNeeded()
{
 auto sampleSnap = activeSampleData().getSnapshot();
 const auto view = buildViewState (sampleSnap);
 if (! view.valid) return;
 const CacheKey key { view.visibleStart, view.visibleLen, view.width, view.numFrames, sampleSnap.get() };
 if (key == prevCacheKey) return;
 cache.rebuild (sampleSnap->buffer, sampleSnap->peakMipmaps,
 view.numFrames, processor.zoom.load(), processor.scroll.load(), view.width);
 prevCacheKey = key;
}

void WaveformView::paint (juce::Graphics& g)
{
 if (! isVisible() || getWidth() <= 0 || getHeight() <= 0)
     return;

 // Explicitly clip to component bounds — prevents antialiased path edges
 // from bleeding into the surrounding frame border, especially on top/bottom.
 g.reduceClipRegion (getLocalBounds());

 auto sampleSnap = activeSampleData().getSnapshot();
 g.fillAll (getTheme().waveformBg);
 int cy = getHeight() / 2;
 g.setColour (getTheme().gridLine.withAlpha (0.5f));
 g.drawHorizontalLine (cy, 0.0f, (float) getWidth());
 g.setColour (getTheme().gridLine.withAlpha (0.2f));
 g.drawHorizontalLine (getHeight() / 4, 0.0f, (float) getWidth());
 g.drawHorizontalLine (getHeight() * 3 / 4, 0.0f, (float) getWidth());
 if (sampleSnap != nullptr) {
 cachedPaintViewState = buildViewState (sampleSnap);
 paintViewStateActive = cachedPaintViewState.valid;
 rebuildCacheIfNeeded();
 drawWaveform (g);
 drawSlices (g);
 paintLazyChopOverlay (g);
 paintTransientMarkers (g);
 paintTrimOverlay (g);
 drawPlaybackCursors (g);
 paintMidiSliceOverlay (g);
 paintViewStateActive = false;
 } else {
 paintViewStateActive = false;
 g.setColour (getTheme().foreground.withAlpha (0.25f));
 g.setFont (DysektLookAndFeel::makeFont (22.0f));
 g.drawText ("DROP AUDIO FILE", getLocalBounds(), juce::Justification::centred);
 paintMidiSliceOverlay (g);
 }
}

void WaveformView::paintMidiSliceOverlay (juce::Graphics& g)
{
 // midiSliceBtn child component covers the full strip when active -- nothing to paint here.
 (void) g;
}

void WaveformView::paintLazyChopOverlay (juce::Graphics& g)
{
 if (! (processor.lazyChop.isActive() && processor.lazyChop.isPlaying()
 && processor.lazyChop.getChopPos() >= 0))
 return;
 int previewIdx = LazyChopEngine::getPreviewVoiceIndex();
 float playhead = processor.voicePool.voicePositions[previewIdx].load (std::memory_order_relaxed);
 if (playhead <= 0.0f)
 return;
 int chopSample = processor.lazyChop.getChopPos();
 int headSample = (int) playhead;
 int x1 = sampleToPixel (std::min (chopSample, headSample));
 int x2 = sampleToPixel (std::max (chopSample, headSample));
 if (x2 > x1) {
 g.setColour (juce::Colour (0xFFCC4444).withAlpha (0.15f));
 g.fillRect (x1, 0, x2 - x1, getHeight());
 g.setColour (juce::Colour (0xFFCC4444).withAlpha (0.5f));
 g.drawVerticalLine (sampleToPixel (chopSample), 0.0f, (float) getHeight());
 }
}

void WaveformView::paintTrimOverlay (juce::Graphics& g)
{
 if (! trimMode) return;
 const int w = getWidth();
 const int h = getHeight();

 auto sampleSnap = activeSampleData().getSnapshot();
 int totalFrames = sampleSnap ? sampleSnap->buffer.getNumSamples() : 1;

 int clampedTrimIn = juce::jlimit(0, totalFrames - 1, trimInPoint);
 int clampedTrimOut = juce::jlimit(clampedTrimIn + 1, totalFrames, trimOutPoint);

 const int x1 = juce::jlimit (0, w - 1, sampleToPixel (clampedTrimIn));
 const int x2 = juce::jlimit (x1 + 1, w - 1, sampleToPixel (clampedTrimOut));
 const auto ac = getTheme().accent;

 g.setColour (juce::Colours::black.withAlpha (0.55f));
 if (x1 > 0) g.fillRect (0, 0, x1, h);
 if (x2 < w - 1) g.fillRect (x2 + 1, 0, w - x2 - 1, h);

 g.setColour (ac.withAlpha (0.90f));
 g.drawVerticalLine (x1, 0.0f, (float) h);
 {
 juce::Path tri;
 tri.addTriangle ((float) x1, 0.0f, (float) x1 + 10.0f, 0.0f, (float) x1, 10.0f);
 g.fillPath (tri);
 }
 g.drawVerticalLine (x2, 0.0f, (float) h);
 {
 juce::Path tri;
 tri.addTriangle ((float) x2, 0.0f, (float) x2 - 10.0f, 0.0f, (float) x2, 10.0f);
 g.fillPath (tri);
 }
 g.setColour (ac.withAlpha (0.04f));
 if (x2 > x1) g.fillRect (x1, 0, x2 - x1, h);
}

void WaveformView::paintTransientMarkers (juce::Graphics& g)
{
 if (transientPreviewPositions.empty()) return;
 g.setColour (getTheme().accent.withAlpha (0.6f));
 float dashLengths[] = { 4.0f, 3.0f };
 for (int pos : transientPreviewPositions)
 {
 int px = sampleToPixel (pos);
 if (px >= 0 && px < getWidth())
 {
 juce::Path dashPath;
 dashPath.startNewSubPath ((float) px, 0.0f);
 dashPath.lineTo ((float) px, (float) getHeight());
 juce::PathStrokeType stroke (1.0f);
 juce::Path dashedPath;
 stroke.createDashedStroke (dashedPath, dashPath, dashLengths, 2);
 g.fillPath (dashedPath);
 }
 }
}

void WaveformView::drawWaveform (juce::Graphics& g)
{
 const int cy = getHeight() / 2;
 const float scale = (float) getHeight() * UILayout::waveformVerticalScale;

 auto& peaks = cache.getPeaks();
 const int numPeaks = std::min (cache.getNumPeaks(), getWidth());
 if (numPeaks <= 0)
 return;

 float samplesPerPixel = 1.0f;
 if (paintViewStateActive && cachedPaintViewState.valid)
 samplesPerPixel = cachedPaintViewState.samplesPerPixel;
 else
 {
 const auto view = buildViewState (activeSampleData().getSnapshot());
 if (view.valid)
 samplesPerPixel = view.samplesPerPixel;
 }

 juce::Path fillPath;
 if (samplesPerPixel >= 1.0f)
 {
 fillPath.startNewSubPath (0.0f, (float) cy - peaks[0].maxVal * scale);
 for (int px = 1; px < numPeaks; ++px)
 fillPath.lineTo ((float) px, (float) cy - peaks[(size_t) px].maxVal * scale);
 for (int px = numPeaks - 1; px >= 0; --px)
 fillPath.lineTo ((float) px, (float) cy - peaks[(size_t) px].minVal * scale);
 fillPath.closeSubPath();
 }

 const juce::Colour waveCol = getTheme().waveform;
 const int h = getHeight();

 switch (waveformMode)
 {
 // ── Mode 0 : Hard ────────────────────────────────────────────────────
 default:
 case 0:
 {
 if (samplesPerPixel < 1.0f)
 {
 g.setColour (waveCol.withAlpha (0.9f));
 juce::Path path;
 bool started = false;
 for (int px = 0; px < numPeaks; ++px)
 {
 float y = (float) cy - peaks[(size_t) px].maxVal * scale;
 if (! started) { path.startNewSubPath ((float) px, y); started = true; }
 else path.lineTo ((float) px, y);
 }
 g.strokePath (path, juce::PathStrokeType (1.5f));

 if (samplesPerPixel < 0.125f)
 {
 const float dotR = 2.5f;
 for (int px = 0; px < numPeaks; ++px)
 {
 float exactPos = (float) pixelToSample (0) + (float) px * samplesPerPixel;
 float frac = exactPos - std::floor (exactPos);
 if (frac < samplesPerPixel)
 {
 float y = (float) cy - peaks[(size_t) px].maxVal * scale;
 g.fillEllipse ((float) px - dotR, y - dotR, dotR * 2.0f, dotR * 2.0f);
 }
 }
 }
 }
 else
 {
 g.setColour (waveCol);
 g.fillPath (fillPath);

 if (samplesPerPixel < 8.0f)
 {
 juce::Path midPath;
 float midY0 = (float) cy - (peaks[0].maxVal + peaks[0].minVal) * 0.5f * scale;
 midPath.startNewSubPath (0.0f, midY0);
 for (int px = 1; px < numPeaks; ++px)
 {
 float mid = (peaks[(size_t) px].maxVal + peaks[(size_t) px].minVal) * 0.5f;
 midPath.lineTo ((float) px, (float) cy - mid * scale);
 }
 g.strokePath (midPath, juce::PathStrokeType (1.5f));
 }
 }
 break;
 }

 // ── Mode 1 : Soft ────────────────────────────────────────────────────
 case 1:
 {
 if (samplesPerPixel < 1.0f)
 {
 g.setColour (waveCol.withAlpha (0.95f));
 juce::Path path;
 bool started = false;
 for (int px = 0; px < numPeaks; ++px)
 {
 float y = (float) cy - peaks[(size_t) px].maxVal * scale;
 if (! started) { path.startNewSubPath ((float) px, y); started = true; }
 else path.lineTo ((float) px, y);
 }
 g.strokePath (path, juce::PathStrokeType (1.8f,
 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

 if (samplesPerPixel < 0.125f)
 {
 g.setColour (waveCol);
 const float dotR = 3.0f;
 for (int px = 0; px < numPeaks; ++px)
 {
 float exactPos = (float) pixelToSample (0) + (float) px * samplesPerPixel;
 float frac = exactPos - std::floor (exactPos);
 if (frac < samplesPerPixel)
 {
 float y = (float) cy - peaks[(size_t) px].maxVal * scale;
 g.fillEllipse ((float) px - dotR, y - dotR, dotR * 2.0f, dotR * 2.0f);
 }
 }
 }
 }
 else
 {
 juce::ColourGradient grad (
 waveCol.withAlpha (0.0f), 0.0f, 0.0f,
 waveCol.withAlpha (0.0f), 0.0f, (float) h,
 false);
 grad.addColour (0.35, waveCol.withAlpha (0.18f));
 grad.addColour (0.5, waveCol.withAlpha (0.28f));
 grad.addColour (0.65, waveCol.withAlpha (0.18f));
 g.setGradientFill (grad);
 g.fillPath (fillPath);

 juce::Path topPath;
 topPath.startNewSubPath (0.0f, (float) cy - peaks[0].maxVal * scale);
 for (int px = 1; px < numPeaks; ++px)
 topPath.lineTo ((float) px, (float) cy - peaks[(size_t) px].maxVal * scale);

 g.setColour (waveCol.withAlpha (0.25f));
 g.strokePath (topPath, juce::PathStrokeType (3.5f,
 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
 g.setColour (waveCol.withAlpha (0.90f));
 g.strokePath (topPath, juce::PathStrokeType (1.3f,
 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

 juce::Path botPath;
 botPath.startNewSubPath (0.0f, (float) cy - peaks[0].minVal * scale);
 for (int px = 1; px < numPeaks; ++px)
 botPath.lineTo ((float) px, (float) cy - peaks[(size_t) px].minVal * scale);

 g.setColour (waveCol.withAlpha (0.25f));
 g.strokePath (botPath, juce::PathStrokeType (3.5f,
 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
 g.setColour (waveCol.withAlpha (0.90f));
 g.strokePath (botPath, juce::PathStrokeType (1.3f,
 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

 if (samplesPerPixel < 8.0f)
 {
 juce::Path midPath;
 float midY0 = (float) cy - (peaks[0].maxVal + peaks[0].minVal) * 0.5f * scale;
 midPath.startNewSubPath (0.0f, midY0);
 for (int px = 1; px < numPeaks; ++px)
 {
 float mid = (peaks[(size_t) px].maxVal + peaks[(size_t) px].minVal) * 0.5f;
 midPath.lineTo ((float) px, (float) cy - mid * scale);
 }
 g.setColour (waveCol.withAlpha (0.85f));
 g.strokePath (midPath, juce::PathStrokeType (1.5f,
 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
 }
 }
 break;
 }

 // ── Mode 2 : Outline ─────────────────────────────────────────────────
 // Top and bottom edge strokes only — no fill
 case 2:
 {
 juce::Path topPath, botPath;
 topPath.startNewSubPath (0.0f, (float) cy - peaks[0].maxVal * scale);
 botPath.startNewSubPath (0.0f, (float) cy - peaks[0].minVal * scale);
 for (int px = 1; px < numPeaks; ++px)
 {
 topPath.lineTo ((float) px, (float) cy - peaks[(size_t) px].maxVal * scale);
 botPath.lineTo ((float) px, (float) cy - peaks[(size_t) px].minVal * scale);
 }
 g.setColour (waveCol.withAlpha (0.30f));
 g.strokePath (topPath, juce::PathStrokeType (3.0f,
 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
 g.strokePath (botPath, juce::PathStrokeType (3.0f,
 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
 g.setColour (waveCol.withAlpha (0.95f));
 g.strokePath (topPath, juce::PathStrokeType (1.2f,
 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
 g.strokePath (botPath, juce::PathStrokeType (1.2f,
 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
 break;
 }

 // ── Mode 3 : Rectified ───────────────────────────────────────────────
 // Fold negative values up — centred on baseline at bottom quarter
 case 3:
 {
 const float baseline = (float) h * 0.85f;
 const float rectScale = scale * 1.6f;
 juce::Path rectPath;
 rectPath.startNewSubPath (0.0f, baseline - std::abs (peaks[0].maxVal) * rectScale);
 for (int px = 1; px < numPeaks; ++px)
 {
 float amp = std::max (std::abs (peaks[(size_t) px].maxVal),
 std::abs (peaks[(size_t) px].minVal));
 rectPath.lineTo ((float) px, baseline - amp * rectScale);
 }
 rectPath.lineTo ((float) (numPeaks - 1), baseline);
 rectPath.lineTo (0.0f, baseline);
 rectPath.closeSubPath();

 juce::ColourGradient grad (waveCol.withAlpha (0.65f), 0.0f, 0.0f,
 waveCol.withAlpha (0.05f), 0.0f, (float) h, false);
 g.setGradientFill (grad);
 g.fillPath (rectPath);

 juce::Path topLine;
 topLine.startNewSubPath (0.0f, baseline - std::abs (peaks[0].maxVal) * rectScale);
 for (int px = 1; px < numPeaks; ++px)
 {
 float amp = std::max (std::abs (peaks[(size_t) px].maxVal),
 std::abs (peaks[(size_t) px].minVal));
 topLine.lineTo ((float) px, baseline - amp * rectScale);
 }
 g.setColour (waveCol.withAlpha (0.95f));
 g.strokePath (topLine, juce::PathStrokeType (1.3f,
 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
 break;
 }

 // ── Mode 4 : Mirrored ────────────────────────────────────────────────
 // Solid fill in upper half + mirror image in lower half
 case 4:
 {
 // Upper half (normal)
 juce::Path upperPath;
 upperPath.startNewSubPath (0.0f, (float) cy);
 for (int px = 0; px < numPeaks; ++px)
 upperPath.lineTo ((float) px, (float) cy - peaks[(size_t) px].maxVal * scale);
 upperPath.lineTo ((float) (numPeaks - 1), (float) cy);
 upperPath.closeSubPath();

 // Lower half (mirrored — positive values go downward)
 juce::Path lowerPath;
 lowerPath.startNewSubPath (0.0f, (float) cy);
 for (int px = 0; px < numPeaks; ++px)
 lowerPath.lineTo ((float) px, (float) cy + peaks[(size_t) px].maxVal * scale);
 lowerPath.lineTo ((float) (numPeaks - 1), (float) cy);
 lowerPath.closeSubPath();

 g.setColour (waveCol.withAlpha (0.75f));
 g.fillPath (upperPath);
 g.setColour (waveCol.withAlpha (0.35f));
 g.fillPath (lowerPath);

 // Outline both edges
 juce::Path edgePath;
 edgePath.startNewSubPath (0.0f, (float) cy - peaks[0].maxVal * scale);
 for (int px = 1; px < numPeaks; ++px)
 edgePath.lineTo ((float) px, (float) cy - peaks[(size_t) px].maxVal * scale);
 g.setColour (waveCol.withAlpha (0.90f));
 g.strokePath (edgePath, juce::PathStrokeType (1.2f,
 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
 break;
 }

 // ── Mode 5 : Bars ────────────────────────────────────────────────────
 // Vertical bars from centre for each pixel — like a spectrum display
 case 5:
 {
 const float barW = 1.0f;
 for (int px = 0; px < numPeaks; ++px)
 {
 float topY = (float) cy - peaks[(size_t) px].maxVal * scale;
 float botY = (float) cy - peaks[(size_t) px].minVal * scale;
 float barH = botY - topY;
 if (barH < 1.0f) barH = 1.0f;

 // Gradient per bar — bright top, fades to centre
 float normAmp = peaks[(size_t) px].maxVal;
 float alpha = 0.4f + normAmp * 0.55f;
 g.setColour (waveCol.withAlpha (alpha));
 g.fillRect ((float) px, topY, barW, barH);
 }
 break;
 }

 // ── Mode 6 : RMS ─────────────────────────────────────────────────────
 // RMS energy envelope — smoother than peak, shows perceived loudness
 case 6:
 {
 // Build RMS path — use average of |max| and |min| as proxy for RMS
 juce::Path rmsPath;
 auto rms0 = (std::abs (peaks[0].maxVal) + std::abs (peaks[0].minVal)) * 0.5f;
 rmsPath.startNewSubPath (0.0f, (float) cy - rms0 * scale);
 for (int px = 1; px < numPeaks; ++px)
 {
 float r = (std::abs (peaks[(size_t) px].maxVal) +
 std::abs (peaks[(size_t) px].minVal)) * 0.5f;
 rmsPath.lineTo ((float) px, (float) cy - r * scale);
 }
 // Mirror bottom
 for (int px = numPeaks - 1; px >= 0; --px)
 {
 float r = (std::abs (peaks[(size_t) px].maxVal) +
 std::abs (peaks[(size_t) px].minVal)) * 0.5f;
 rmsPath.lineTo ((float) px, (float) cy + r * scale);
 }
 rmsPath.closeSubPath();

 juce::ColourGradient grad (
 waveCol.withAlpha (0.0f), 0.0f, 0.0f,
 waveCol.withAlpha (0.0f), 0.0f, (float) h, false);
 grad.addColour (0.35, waveCol.withAlpha (0.22f));
 grad.addColour (0.5, waveCol.withAlpha (0.38f));
 grad.addColour (0.65, waveCol.withAlpha (0.22f));
 g.setGradientFill (grad);
 g.fillPath (rmsPath);

 // Bright outline on top edge
 juce::Path rmsLine;
 rmsLine.startNewSubPath (0.0f, (float) cy - rms0 * scale);
 for (int px = 1; px < numPeaks; ++px)
 {
 float r = (std::abs (peaks[(size_t) px].maxVal) +
 std::abs (peaks[(size_t) px].minVal)) * 0.5f;
 rmsLine.lineTo ((float) px, (float) cy - r * scale);
 }
 g.setColour (waveCol.withAlpha (0.30f));
 g.strokePath (rmsLine, juce::PathStrokeType (3.5f,
 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
 g.setColour (waveCol.withAlpha (0.95f));
 g.strokePath (rmsLine, juce::PathStrokeType (1.3f,
 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
 break;
 }

 // ── Mode 7 : Stepped ─────────────────────────────────────────────────
 // Blocky quantised look — horizontal run then vertical step
 case 7:
 {
 const int stepW = juce::jmax (2, getWidth() / juce::jmax (1, numPeaks / 4));
 juce::Path stepPath;
 bool started7 = false;
 for (int px = 0; px < numPeaks; px += stepW)
 {
 float y = (float) cy - peaks[(size_t) px].maxVal * scale;
 int x = px;
 if (! started7)
 {
 stepPath.startNewSubPath ((float) x, y);
 started7 = true;
 }
 else
 {
 stepPath.lineTo ((float) x, stepPath.getCurrentPosition().y);
 stepPath.lineTo ((float) x, y);
 }
 stepPath.lineTo ((float) juce::jmin (x + stepW, numPeaks - 1), y);
 }

 // Build closed fill path
 juce::Path stepFill = stepPath;
 stepFill.lineTo ((float) (numPeaks - 1), (float) cy);
 stepFill.lineTo (0.0f, (float) cy);
 stepFill.closeSubPath();

 // Mirror bottom
 juce::Path stepBot;
 bool startedBot = false;
 for (int px = 0; px < numPeaks; px += stepW)
 {
 float y = (float) cy - peaks[(size_t) px].minVal * scale;
 int x = px;
 if (! startedBot)
 {
 stepBot.startNewSubPath ((float) x, y);
 startedBot = true;
 }
 else
 {
 stepBot.lineTo ((float) x, stepBot.getCurrentPosition().y);
 stepBot.lineTo ((float) x, y);
 }
 stepBot.lineTo ((float) juce::jmin (x + stepW, numPeaks - 1), y);
 }
 juce::Path botFill = stepBot;
 botFill.lineTo ((float) (numPeaks - 1), (float) cy);
 botFill.lineTo (0.0f, (float) cy);
 botFill.closeSubPath();

 g.setColour (waveCol.withAlpha (0.70f));
 g.fillPath (stepFill);
 g.setColour (waveCol.withAlpha (0.35f));
 g.fillPath (botFill);

 g.setColour (waveCol.withAlpha (0.95f));
 g.strokePath (stepPath, juce::PathStrokeType (1.3f));
 g.strokePath (stepBot, juce::PathStrokeType (1.3f));
 break;
 }
 }
}

void WaveformView::drawSlices (juce::Graphics& g)
{
 const bool sfzMode = isSfzPlayer2Mode();

 // --- Consume pending optimistic marker commit from processor (for MIDI/knob moves) ---
 // SFZ-PLAYER has no live-drag/optimistic-commit machinery at all (its
 // slices come only from the loaded SFZ file, never user-edited), so this
 // entire block is skipped in that mode — optimisticSliceIdx simply never
 // gets set, and the dependent code below naturally never triggers.
 if (! sfzMode)
 {
 int optIdx = processor.pendingUiOptimisticIdx.exchange(-1, std::memory_order_acq_rel);
 int optSample = -1;
 if (optIdx >= 0) {
 optSample = processor.pendingUiOptimisticSample.exchange(-1, std::memory_order_acq_rel);

 // Only set optimistic if different from snapshot
 const auto& uiSnap = processor.getUiSliceSnapshot();
 if (optIdx < (int)uiSnap.slices.size() && uiSnap.slices[(size_t)optIdx].startSample != optSample) {
 optimisticSliceIdx = optIdx;
 optimisticStartSample = optSample;
 } else {
 // It's already committed in the snapshot, so don't set
 optimisticSliceIdx = -1;
 optimisticStartSample = -1;
 }
 }

 // --- Clear optimistic state once snapshot reflects the real model ---
 if (optimisticSliceIdx >= 0) {
 const auto& snap = processor.getUiSliceSnapshot();
 if (optimisticSliceIdx < (int)snap.slices.size()) {
 const auto& optSlice = snap.slices[(size_t)optimisticSliceIdx];
 if (optSlice.startSample == optimisticStartSample || !optSlice.active) {
 optimisticSliceIdx = -1;
 optimisticStartSample = -1;
 }
 } else {
 optimisticSliceIdx = -1;
 optimisticStartSample = -1;
 }
 }
 }

 const auto& ui = sfzMode ? processor.getUiSliceSnapshot2() : processor.getUiSliceSnapshot();
 auto& activeMgr = activeSliceManager();
 int sel = ui.selectedSlice;
 int num = ui.numSlices;

 for (int i = 0; i < num; ++i)
 {
 const auto& s = ui.slices[(size_t) i];
 if (! s.active) continue;

 int drawStartSample = s.startSample;
 if (! sfzMode && i == optimisticSliceIdx && optimisticStartSample >= 0)
 drawStartSample = optimisticStartSample;

 int drawEndSample = activeMgr.getEndForSlice(i, ui.sampleNumFrames);

 // Live preview during drag: fill tracks dragPreviewStart/End for any dragged slice
 // (SFZ-PLAYER mode: dragSliceIdx/linkedSliceIdx/liveDragSliceIdx are never
 // set against this engine — see the mouse handlers' sfzMode guards — so
 // these branches naturally fall through to the plain committed bounds.)
 if (dragSliceIdx == i &&
 (dragMode == DragEdgeLeft || dragMode == MoveSlice))
 {
 drawStartSample = dragPreviewStart;
 drawEndSample = dragPreviewEnd;
 }
 else if (i == linkedSliceIdx && linkedSliceIdx >= 0 && dragMode != None)
 {
 drawStartSample = linkedPreviewStart;
 drawEndSample = linkedPreviewEnd;
 }
 else if (dragMode == None)
 {
 // No local waveform drag — check if SliceControlBar is driving the marker
 // via the live atomics (same path the marker bar already uses for mx)
 const int liveIdx2 = processor.liveDragSliceIdx.load (std::memory_order_acquire);
 if (liveIdx2 == i)
 {
 const int liveStart2 = processor.liveDragBoundsStart.load (std::memory_order_relaxed);
 if (liveStart2 >= 0)
 drawStartSample = liveStart2;
 }
 }

 // Marker position for live drag: only the thin bar moves, fill stays at committed bounds
 int liveMarkerSample = -1;
 {
 const int liveIdx = processor.liveDragSliceIdx.load (std::memory_order_acquire);
 if (liveIdx == i)
 {
 const int liveStart = processor.liveDragBoundsStart.load (std::memory_order_relaxed);
 if (liveStart >= 0) liveMarkerSample = liveStart;
 }
 }

 int x1 = std::max (0, sampleToPixel (drawStartSample));
 int x2 = std::min (getWidth(), sampleToPixel (drawEndSample));
 int sw = x2 - x1;
 if (sw <= 0) continue;
 // Marker bar tracks live drag independently of the fill
 const int mx = (liveMarkerSample >= 0)
 ? juce::jlimit (0, getWidth(), sampleToPixel (liveMarkerSample))
 : x1;

 // 3px breathing room top and bottom so markers stay inside the frame border
 const int kTopPad = 3;
 const int kBotPad = 3;
 const int markerH = getHeight() - kTopPad - kBotPad;

 // --- CUBASE-STYLE SLICE OVERLAY (fill stays at committed bounds during live drag) ---
 // Flash red when dragging this marker into the delete zone
 const bool inDeleteZone = (dragSliceIdx == i && dragInDeleteZone);
 g.setColour (inDeleteZone ? juce::Colour (0xFFFF2D55).withAlpha (0.35f)
 : s.colour.withAlpha (0.18f));
 g.fillRect(x1, kTopPad, sw, markerH);

 // Strong colored borders: top & bottom
 g.setColour(s.colour.withAlpha(0.75f));
 g.drawHorizontalLine(kTopPad, (float)x1, (float)x2);
 g.drawHorizontalLine(getHeight() - kBotPad, (float)x1, (float)x2);

 // --- MARKER BAR: always follows live drag position (mx), not fill left edge ---
 g.setColour(s.colour.withAlpha(0.92f));
 g.drawVerticalLine(mx, (float)kTopPad, (float)(kTopPad + markerH));
 {
     juce::Path tri;
     tri.addTriangle((float)mx,         (float)kTopPad,
                     (float)mx + 10.0f, (float)kTopPad,
                     (float)mx,         (float)(kTopPad + 10));
     g.fillPath(tri);
 }

 // Selection highlight overlay
 if (i == sel) {
 g.setColour(s.colour.withAlpha(0.22f));
 g.fillRect(x1, kTopPad, sw, markerH);
 }

 // Slice label: name (if set) or number — always white, always readable
 g.setColour (juce::Colours::white);
 if (s.name.isNotEmpty() && sw > 20)
 {
 // Name: centred in slice, font size scales with available width
 const float nameFontH = juce::jlimit (9.0f, 13.0f, (float)sw * 0.18f);
 g.setFont (DysektLookAndFeel::makeFont (nameFontH, true));
 g.drawText (s.name.toUpperCase(), x1 + 2, kTopPad + 1, sw - 4, 14,
 juce::Justification::centred, true);
 }
 else
 {
 // Number: just to the right of the marker arrow (10px triangle),
 // so it does not overlap the marker head.
 g.setFont (DysektLookAndFeel::makeFont (10.0f, true));
 g.drawText (juce::String (i + 1), mx + 13, 3, 20, 12, juce::Justification::left);
 }
 }
}

void WaveformView::resized()
{
 prevCacheKey = {};
 auto sampleSnap = activeSampleData().getSnapshot();
 if (sampleSnap) {
 int totalFrames = sampleSnap->buffer.getNumSamples();
 trimInPoint = juce::jlimit(0, totalFrames - 1, trimInPoint);
 trimOutPoint = juce::jlimit(trimInPoint + 1, totalFrames, trimOutPoint);
 }
 midiSliceBtn.setBounds (0, 0, getWidth(), kMidiOverlayH);
}

void WaveformView::setMidiSliceActive (bool active)
{
 midiSliceOverlayActive = active;
 midiSliceBtn.setVisible (active);
 repaint();
}

void WaveformView::mouseMove (const juce::MouseEvent& e)
{
 if (trimMode)
 {
 const int x1 = sampleToPixel (trimInPoint);
 const int x2 = sampleToPixel (trimOutPoint);
 if (std::abs (e.x - x1) < 8 || std::abs (e.x - x2) < 8)
 setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
 else
 setMouseCursor (juce::MouseCursor::NormalCursor);
 return;
 }

 if (isSfzPlayer2Mode())
 {
     // SFZ-PLAYER's slices come from the loaded SFZ file and are never
     // user-edited — no draggable edges, always the plain cursor.
     setMouseCursor (juce::MouseCursor::NormalCursor);
     if (hoveredEdge != HoveredEdge::None) { hoveredEdge = HoveredEdge::None; repaint(); }
     return;
 }

 auto sampleSnap = activeSampleData().getSnapshot();
 if (sampleSnap == nullptr) return;
 const auto& ui = processor.getUiSliceSnapshot();
 int sel = ui.selectedSlice;
 int num = ui.numSlices;
 HoveredEdge newEdge = HoveredEdge::None;
 for (int i = 0; i < num; ++i)
 {
 const auto& s = ui.slices[(size_t) i];
 if (! s.active) continue;
 int x1 = sampleToPixel (s.startSample);
 if (std::abs (e.x - x1) < 6) { newEdge = HoveredEdge::Left; break; }
 }
 setMouseCursor (newEdge != HoveredEdge::None
 ? juce::MouseCursor::LeftRightResizeCursor
 : juce::MouseCursor::NormalCursor);

 if (newEdge != hoveredEdge) { hoveredEdge = newEdge; repaint(); }
}

void WaveformView::mouseEnter (const juce::MouseEvent& e) { mouseMove (e); }
void WaveformView::mouseExit (const juce::MouseEvent&) { if (hoveredEdge != HoveredEdge::None) { hoveredEdge = HoveredEdge::None; repaint(); } }

void WaveformView::mouseDown (const juce::MouseEvent& e)
{
 auto sampleSnap = activeSampleData().getSnapshot();
 if (sampleSnap == nullptr) return;
 int samplePos = std::max (0, std::min (pixelToSample (e.x), sampleSnap->buffer.getNumSamples()));

 // ── Trim mode: handle drag start only ────────────────────────────────────
 if (trimMode)
 {
 int w = getWidth();
 auto totalFrames = sampleSnap->buffer.getNumSamples();
 int x1 = juce::jlimit(0, w, sampleToPixel(juce::jlimit(0, totalFrames - 1, trimInPoint)));
 int x2 = juce::jlimit(0, w, sampleToPixel(juce::jlimit(trimInPoint + 1, totalFrames, trimOutPoint)));
 if (std::abs (e.x - x1) < 8)
 {
 dragMode = DragTrimIn;
 trimDragging = true;
 return;
 }
 else if (std::abs (e.x - x2) < 8)
 {
 dragMode = DragTrimOut;
 trimDragging = true;
 return;
 }
 return;
 }

 // ── SFZ-PLAYER mode: real slices (sliceManager2), but display-only — no
 // context menu, edge-dragging, move, or lock (the slice layout is fixed
 // by the loaded SFZ file, never user-edited). Clicking a slice here only
 // selects it for highlight and triggers a one-shot audition by starting
 // a real voice on voicePool2, exactly as if that slice's pinned MIDI
 // note had just been played — so ADSR/looping/everything sounds exactly
 // like it would during normal MIDI playback, not a raw buffer dub.
 if (isSfzPlayer2Mode())
 {
     const auto& ui = processor.getUiSliceSnapshot2();
     const int num = ui.numSlices;
     int hitIdx = -1;
     for (int i = 0; i < num; ++i)
     {
         const auto& s = ui.slices[(size_t) i];
         if (! s.active) continue;
         const int sEnd = processor.sliceManager2.getEndForSlice (i, ui.sampleNumFrames);
         if (samplePos >= s.startSample && samplePos < sEnd) { hitIdx = i; break; }
     }

     processor.sliceManager2.selectedSlice.store (hitIdx, std::memory_order_relaxed);
     processor.markUiSnapshotDirty();

     if (hitIdx >= 0 && ! e.mods.isRightButtonDown())
     {
         const auto& s = ui.slices[(size_t) hitIdx];
         // Tail slices (loop targets, midiNote left unpinned by design —
         // see Slice::nextSliceIdx) have no sensible note to audition
         // directly; only audition slices that are actually MIDI-reachable.
         if (s.midiNote >= 0 && s.midiNote <= 127)
         {
             VoiceStartParams p;
             p.sliceIdx = hitIdx;
             p.note     = s.midiNote;
             p.velocity = 100.0f;

             int voiceIdx = processor.voicePool2.allocate();
             const int mg = (int) processor.sliceManager2.resolveParam (
                 hitIdx, kLockMuteGroup, (float) s.muteGroup, (float) p.globalMuteGroup);
             processor.voicePool2.muteGroup (mg, voiceIdx);
             p.globalMuteGroup = mg;
             processor.voicePool2.startVoice (voiceIdx, p, processor.sliceManager2, processor.sampleData2);
         }
     }

     repaint();
     return;
 }

 // ── MIDI Slice overlay: handled by midiSliceBtn child component ──────────
 if (midiSliceOverlayActive && ! e.mods.isRightButtonDown() && e.y < kMidiOverlayH)
 return; // button child handles clicks in this strip

 // ── Right-click: show slice context menu anywhere on the waveform ─────────
 if (e.mods.isRightButtonDown())
 {
 const auto& ui = processor.getUiSliceSnapshot();
 const int num = ui.numSlices;
 int targetSlice = -1;

 // Find the slice under cursor; prefer the currently selected slice
 for (int i = 0; i < num; ++i)
 {
 const auto& s = ui.slices[(size_t) i];
 if (! s.active) continue;
 const int slEnd = processor.sliceManager.getEndForSlice (i, ui.sampleNumFrames);
 if (samplePos >= s.startSample && samplePos < slEnd)
 {
 if (i == ui.selectedSlice) { targetSlice = i; break; }
 if (targetSlice < 0) targetSlice = i;
 }
 }

 // ── Build menu ────────────────────────────────────────────────────────
 juce::PopupMenu menu;

 // MIDI Slice — always visible
 const juce::String midiLabel = midiSliceOverlayActive ? "Stop MIDI Slice" : "MIDI Slice";
 menu.addItem (50, midiLabel, true, midiSliceOverlayActive);

 // Auto Slice sub-menu — always visible; divides whole sample into N equal slices
 {
 juce::PopupMenu autoSub;
 autoSub.addItem (60, "4 Slices");
 autoSub.addItem (61, "8 Slices");
 autoSub.addItem (62, "16 Slices");
 autoSub.addItem (63, "32 Slices");
 const bool hasSample = (ui.sampleNumFrames > 0);
 menu.addSubMenu ("Auto Slice", autoSub, hasSample);
 }

 menu.addItem (51, "Shortcuts");

 // Slice-specific items appear below when cursor is over a slice
 bool hasSliceItems = (targetSlice >= 0);
 if (hasSliceItems)
 {
 const auto& s = ui.slices[(size_t) targetSlice];
 const bool allLocked = (s.lockMask == 0xFFFFFFFFu);
 const juce::String lockLabel = allLocked ? "Unlock Slice" : "Lock Slice";

 // ═══════════════════════════════════════════════════════════════════
 // BUG FIX: Added Hold (kLockHold) to the lock status checks
 // ═══════════════════════════════════════════════════════════════════
 const bool lockA = (s.lockMask & kLockAttack) != 0;
 const bool lockH = (s.lockMask & kLockHold) != 0; // ← NEW
 const bool lockD = (s.lockMask & kLockDecay) != 0;
 const bool lockS = (s.lockMask & kLockSustain) != 0;
 const bool lockR = (s.lockMask & kLockRelease) != 0;

 static const struct { const char* name; juce::uint32 argb; } kPal[] = {
 { "Cyan", 0xFF00C8FF }, { "Green", 0xFF00FF87 },
 { "Yellow", 0xFFFFE800 }, { "Orange", 0xFFFF6B00 },
 { "Red", 0xFFFF2D55 }, { "Pink", 0xFFFF2D9A },
 { "Violet", 0xFFB44FFF }, { "Blue", 0xFF4A80FF },
 { "Sky", 0xFF00BFFF }, { "Mint", 0xFF00FFD0 },
 { "Lime", 0xFFA8FF3E }, { "Gold", 0xFFFFD700 },
 { "Coral", 0xFFFF7F50 }, { "Magenta", 0xFFFF00FF },
 { "White", 0xFFE8E8E8 }, { "Silver", 0xFF888888 },
 };

 juce::PopupMenu colourSub;
 juce::Colour curCol = s.colour;
 for (int ci = 0; ci < 16; ++ci)
 {
 juce::Colour c ((juce::uint32) kPal[ci].argb);
 colourSub.addColouredItem (20 + ci, kPal[ci].name, c,
 true, c.toDisplayString (false) == curCol.toDisplayString (false));
 }

 // ═══════════════════════════════════════════════════════════════════
 // BUG FIX: Added Hold lock option to the AHDSR submenu
 // ═══════════════════════════════════════════════════════════════════
 juce::PopupMenu adsrSub;
 adsrSub.addItem (10, "Lock Attack", true, lockA);
 adsrSub.addItem (14, "Lock Hold", true, lockH); // ← NEW (using ID 14)
 adsrSub.addItem (11, "Lock Decay", true, lockD);
 adsrSub.addItem (12, "Lock Sustain", true, lockS);
 adsrSub.addItem (13, "Lock Release", true, lockR);

 menu.addSeparator();
 menu.addItem (1, "Delete Slice");
 menu.addSeparator();
 menu.addSubMenu ("Slice Color", colourSub);
 menu.addSeparator();
 menu.addItem (2, lockLabel, true, allLocked);
 menu.addSubMenu ("AHDSR Lock", adsrSub); // ← Changed label from "ADSR Lock" to "AHDSR Lock"
 menu.addSeparator();
 menu.addItem (3, "Rename Slice...");
 }

 auto* topLvl = getTopLevelComponent();
 float ms = DysektLookAndFeel::getMenuScale();
 const auto screenPt = e.getScreenPosition();
 menu.showMenuAsync (
 juce::PopupMenu::Options()
 .withTargetScreenArea ({ screenPt, screenPt })
 .withParentComponent (topLvl)
 .withStandardItemHeight ((int) (24 * ms)),
 [this, targetSlice, hasSliceItems] (int result)
 {
 // ── MIDI Slice toggle ──────────────────────────────────────────
 if (result == 50)
 {
 DysektProcessor::Command cmd;
 if (midiSliceOverlayActive)
 {
 cmd.type = DysektProcessor::CmdLazyChopStop;
 processor.pushCommand (cmd);
 setMidiSliceActive (false);
 }
 else
 {
 cmd.type = DysektProcessor::CmdLazyChopStart;
 processor.pushCommand (cmd);
 processor.midiSelectsSlice.store (true);
 setMidiSliceActive (true);
 }
 return;
 }
 // ── Shortcuts ─────────────────────────────────────────────────
 // ── Auto Slice (equal division of whole sample) ───────────────────
 if (result >= 60 && result <= 63)
 {
 const int counts[] = { 4, 8, 16, 32 };
 const int n = counts[result - 60];
 DysektProcessor::Command cmd;
 cmd.type = DysektProcessor::CmdEqualChop;
 cmd.intParam1 = n;
 processor.pushCommand (cmd);
 return;
 }

 if (result == 51)
 {
 if (onShortcutsToggle) onShortcutsToggle();
 return;
 }

 if (! hasSliceItems) { repaint(); return; }

 auto allLocked = false;
 {
 const auto& uiSnap = processor.getUiSliceSnapshot();
 if (targetSlice >= 0 && targetSlice < uiSnap.numSlices)
 allLocked = (uiSnap.slices[(size_t) targetSlice].lockMask == 0xFFFFFFFFu);
 }

 auto toggleLock = [&] (uint32_t bit)
 {
 DysektProcessor::Command cmd;
 cmd.type = DysektProcessor::CmdToggleLock;
 cmd.intParam1 = targetSlice; // slice to lock — explicit, not racy selectedSlice
 cmd.intParam2 = (int) bit;
 processor.pushCommand (cmd);
 };

 if (result == 1)
 {
 DysektProcessor::Command cmd;
 cmd.type = DysektProcessor::CmdDeleteSlice;
 cmd.intParam1 = targetSlice;
 processor.pushCommand (cmd);
 }
 else if (result == 2)
 {
 DysektProcessor::Command cmd;
 cmd.type = DysektProcessor::CmdSetSliceLockAll;
 cmd.intParam1 = targetSlice;
 cmd.floatParam1 = allLocked ? 0.f : 1.f;
 processor.pushCommand (cmd);
 }
 // ═══════════════════════════════════════════════════════════════
 // BUG FIX: Added handler for Hold lock toggle (result == 14)
 // ═══════════════════════════════════════════════════════════════
 else if (result == 10) toggleLock (kLockAttack);
 else if (result == 14) toggleLock (kLockHold); // ← NEW
 else if (result == 11) toggleLock (kLockDecay);
 else if (result == 12) toggleLock (kLockSustain);
 else if (result == 13) toggleLock (kLockRelease);
 else if (result >= 20 && result < 36)
 {
 static const juce::uint32 kPalARGB[] = {
 0xFF00C8FF, 0xFF00FF87, 0xFFFFE800, 0xFFFF6B00,
 0xFFFF2D55, 0xFFFF2D9A, 0xFFB44FFF, 0xFF4A80FF,
 0xFF00BFFF, 0xFF00FFD0, 0xFFA8FF3E, 0xFFFFD700,
 0xFFFF7F50, 0xFFFF00FF, 0xFFE8E8E8, 0xFF888888,
 };
 DysektProcessor::Command cmd;
 cmd.type = DysektProcessor::CmdSetSliceColour;
 cmd.intParam1 = targetSlice;
 cmd.intParam2 = (int) kPalARGB[result - 20];
 processor.pushCommand (cmd);
 }
 else if (result == 3)
 {
 // ── Rename Slice ──────────────────────────────────────────
 // Delegate to the editor's RenameOverlay so the dialog
 // renders inside the plugin window at any UI scale.
 juce::String currentName;
 {
 const auto& snap = processor.getUiSliceSnapshot();
 if (targetSlice >= 0 && targetSlice < snap.numSlices)
 currentName = snap.slices[(size_t) targetSlice].name;
 }
 if (onRenameRequest)
 onRenameRequest (targetSlice, currentName);
 return; // async — skip repaint() below
 }
 repaint();
 });
 return;
 }

 // ── Left-click: slice edge drag — works on ANY marker, not just selected ───
 const auto& ui = processor.getUiSliceSnapshot();
 int sel = ui.selectedSlice;
 int num = ui.numSlices;
 {
 // Find the nearest marker within the 6px hit zone across ALL slices
 int nearestSlice = -1;
 int nearestDist = 7; // just outside the 6px threshold
 for (int i = 0; i < num; ++i)
 {
 const auto& s = ui.slices[(size_t) i];
 if (! s.active) continue;
 int dist = std::abs (e.x - sampleToPixel (s.startSample));
 if (dist < nearestDist) { nearestDist = dist; nearestSlice = i; }
 }

 if (nearestSlice >= 0)
 {
 const auto& s = ui.slices[(size_t) nearestSlice];
 const int selEnd = processor.sliceManager.getEndForSlice (nearestSlice, ui.sampleNumFrames);

 DysektProcessor::Command gestureCmd;
 gestureCmd.type = DysektProcessor::CmdBeginGesture;
 processor.pushCommand (gestureCmd);

 // Auto-select the dragged slice so the LCD and marker slider track it in real time
 if (nearestSlice != sel)
 {
 DysektProcessor::Command selCmd;
 selCmd.type = DysektProcessor::CmdSelectSlice;
 selCmd.intParam1 = nearestSlice;
 processor.pushCommand (selCmd);
 }

 dragMode = DragEdgeLeft;
 dragSliceIdx = nearestSlice;
 dragPreviewStart = s.startSample;
 dragPreviewEnd = selEnd;
 dragOrigStart = s.startSample;
 dragOrigEnd = selEnd;
 linkedSliceIdx = -1;

 for (int i = 0; i < num; ++i)
 {
 if (i == nearestSlice || ! ui.slices[(size_t) i].active) continue;
 if (processor.sliceManager.getEndForSlice (i, ui.sampleNumFrames) == s.startSample)
 {
 linkedSliceIdx = i;
 linkedPreviewStart = ui.slices[(size_t) i].startSample;
 linkedPreviewEnd = s.startSample;
 break;
 }
 }
 return;
 }
 }

 // ── Left-click: select slice ───────────────────────────────────────────────
 for (int i = 0; i < num; ++i)
 {
 const auto& sl = ui.slices[(size_t) i];
 if (sl.active && samplePos >= sl.startSample
 && samplePos < processor.sliceManager.getEndForSlice (i, ui.sampleNumFrames))
 {
 DysektProcessor::Command cmd;
 cmd.type = DysektProcessor::CmdSelectSlice;
 cmd.intParam1 = i;
 processor.pushCommand (cmd);
 break;
 }
 }
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
 if (trimMode) return;
 if (isSfzPlayer2Mode()) return;   // no manual slice creation — layout comes from the SFZ file
 auto sampleSnap = activeSampleData().getSnapshot();
 if (sampleSnap == nullptr) return;
 int rawPos = juce::jlimit (0, sampleSnap->buffer.getNumSamples(), pixelToSample (e.x));
 int samplePos = AudioAnalysis::findNearestZeroCrossing (sampleSnap->buffer, rawPos);
 DysektProcessor::Command cmd;
 cmd.type = DysektProcessor::CmdCreateSlice;
 cmd.intParam1 = samplePos;
 cmd.intParam2 = sampleSnap->buffer.getNumSamples();
 processor.pushCommand (cmd);
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
 auto sampleSnap = activeSampleData().getSnapshot();
 if (sampleSnap == nullptr) return;

 if (trimMode && trimDragging && (dragMode == DragTrimIn || dragMode == DragTrimOut))
 {
 int totalFrames = sampleSnap->buffer.getNumSamples();
 int minLen = 1;
 int newSample = juce::jlimit(0, totalFrames, pixelToSample(e.x));
 if (dragMode == DragTrimIn)
 trimInPoint = juce::jlimit(0, trimOutPoint - minLen, newSample);
 else if (dragMode == DragTrimOut)
 trimOutPoint = juce::jlimit(trimInPoint + minLen, totalFrames, newSample);
 repaint();
 return;
 }

 // Slice edge dragging (visual preview)
 if (dragMode == DragEdgeLeft && dragSliceIdx >= 0)
 {
 dragPreviewStart = AudioAnalysis::findNearestZeroCrossing (sampleSnap->buffer, pixelToSample(e.x));
 if (linkedSliceIdx >= 0)
 linkedPreviewEnd = dragPreviewStart;

 // Publish live position so SliceControlBar slider and waveform marker bar
 // both track the drag in real time (same atomics used by the CC/MIDI path)
 processor.liveDragSliceIdx.store (dragSliceIdx, std::memory_order_release);
 processor.liveDragBoundsStart.store(dragPreviewStart, std::memory_order_relaxed);
 processor.liveDragBoundsEnd.store (dragPreviewEnd, std::memory_order_relaxed);

 // Visual cue: turn the slice red when it enters the delete zone
 dragInDeleteZone = (linkedSliceIdx >= 0 && dragPreviewStart <= linkedPreviewStart);

 repaint();
 }
 // TODO: add MoveSlice/other modes if needed
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
 // ---- TRIM MODE: commit new in/out points ----
 if (trimMode)
 {
 trimDragging = false;
 dragMode = None;
 processor.trimRegionStart.store (trimInPoint, std::memory_order_relaxed);
 processor.trimRegionEnd.store (trimOutPoint, std::memory_order_relaxed);
 repaint();
 return;
 }

 // ---- SLICE EDGE DRAG: commit marker move ----
 if ((dragMode == DragEdgeLeft || dragMode == MoveSlice) && dragSliceIdx >= 0)
 {
 // If the dragged marker touched or crossed the previous marker's start,
 // treat it as a Delete Slice (same result as right-click → Delete Slice).
 const bool crossedPrev = (linkedSliceIdx >= 0 && dragPreviewStart <= linkedPreviewStart);

 if (crossedPrev)
 {
 DysektProcessor::Command cmd;
 cmd.type = DysektProcessor::CmdDeleteSlice;
 cmd.intParam1 = dragSliceIdx;
 processor.pushCommand (cmd);
 // No CmdSetSliceBounds needed — the slice is gone.
 }
 else
 {
 DysektProcessor::Command cmd;
 cmd.type = DysektProcessor::CmdSetSliceBounds;
 cmd.intParam1 = dragSliceIdx;
 cmd.intParam2 = dragPreviewStart;
 cmd.positions[0] = dragPreviewEnd;
 cmd.numPositions = 1;
 processor.pushCommand (cmd);

 if (linkedSliceIdx >= 0)
 {
 DysektProcessor::Command lCmd;
 lCmd.type = DysektProcessor::CmdSetSliceBounds;
 lCmd.intParam1 = linkedSliceIdx;
 lCmd.intParam2 = linkedPreviewStart;
 lCmd.positions[0] = linkedPreviewEnd;
 lCmd.numPositions = 1;
 processor.pushCommand (lCmd);
 }

 optimisticSliceIdx = dragSliceIdx;
 optimisticStartSample = dragPreviewStart;
 }
 }
 dragInDeleteZone = false;

 processor.liveDragSliceIdx.store (-1, std::memory_order_release);

 dragMode = None;
 dragSliceIdx = -1;
 linkedSliceIdx = -1;
 trimDragging = false;
 dragPreviewStart = 0;
 dragPreviewEnd = 0;
 repaint();
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
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
 int width = getWidth();
 float oldZoom = processor.zoom.load();
 float oldViewFrac = 1.0f / oldZoom;
 float oldScroll = processor.scroll.load();
 float cursorPixelFrac = (width > 0) ? (float) e.x / (float) width : 0.5f;
 float newZoom = (w.deltaY > 0)
 ? std::min (16384.0f, oldZoom * 1.2f)
 : std::max (1.0f, oldZoom / 1.2f);
 processor.zoom.store (newZoom);
 float newViewFrac = 1.0f / newZoom;
 float maxScroll = 1.0f - newViewFrac;
 if (maxScroll > 0.0f)
 {
 float oldViewStart = oldScroll * (1.0f - oldViewFrac);
 float anchorFrac = oldViewStart + cursorPixelFrac * oldViewFrac;
 float newViewStart = anchorFrac - cursorPixelFrac * newViewFrac;
 processor.scroll.store (juce::jlimit (0.0f, 1.0f, newViewStart / maxScroll));
 }
 else
 processor.scroll.store (0.0f);
 }
 prevCacheKey = {};
}

bool WaveformView::isInterestedInFileDrag (const juce::StringArray& files)
{
 for (auto& f : files)
 {
 auto ext = juce::File (f).getFileExtension().toLowerCase();
        // In SFZ-PLAYER or SF2-player mode: only accept .sfz files
        const int routeMode = processor.midiRouteMode.load (std::memory_order_relaxed);
        const bool sfzMode = (routeMode == static_cast<int> (DysektProcessor::MidiRouteMode::SfPlayer))
                          || (routeMode == static_cast<int> (DysektProcessor::MidiRouteMode::SfzPlayer2));
 if (sfzMode)
 {
     if (ext == ".sfz") return true;
 }
 else
 {
     if (ext == ".wav" || ext == ".ogg" || ext == ".aif" ||
         ext == ".aiff" || ext == ".flac" || ext == ".mp3" ||
         ext == ".sf2" || ext == ".sfz")
         return true;
 }
 }
 return false;
}

void WaveformView::filesDropped (const juce::StringArray& files, int, int)
{
 if (files.isEmpty()) return;
 juce::File f (files[0]);
 auto ext = f.getFileExtension().toLowerCase();
 processor.zoom.store (1.0f);
 processor.scroll.store (0.0f);
 prevCacheKey = {};
    const int routeMode2 = processor.midiRouteMode.load (std::memory_order_relaxed);
    const bool isSfPlayerTab    = (routeMode2 == static_cast<int> (DysektProcessor::MidiRouteMode::SfPlayer));
    const bool isSfzPlayer2Tab  = (routeMode2 == static_cast<int> (DysektProcessor::MidiRouteMode::SfzPlayer2));
 if (isSfPlayerTab || isSfzPlayer2Tab)
 {
        // SFZ-PLAYER tab: load via SoundFontLoader, which renders every SFZ
        // key zone as a real slice into sliceManager2 (see PluginProcessor's
        // pendingPreviewZones2 consumption). No separate live-engine load —
        // sfzPlayer2 no longer processes audio; voicePool2/sliceManager2 do.
        // SF2-PLAYER tab: only .sf2 accepted; routes to its own sampleData3/
        // previewZones3 pipeline via SoundFontLoadTarget::SfPlayer.
        if (isSfzPlayer2Tab && ext == ".sfz")
            processor.loadSoundFontAsync (f, SoundFontLoadTarget::SfzPlayer2);
        else if (isSfPlayerTab && ext == ".sf2")
            processor.loadSoundFontAsync (f, SoundFontLoadTarget::SfPlayer);
 }
 else if (ext == ".sf2" || ext == ".sfz")
     processor.loadSoundFontAsync (f);
 else
     processor.loadFileAsync (f);
}

void WaveformView::enterTrimMode (int start, int end)
{
 trimMode = true;
 trimStart = start;
 trimEnd = end;
 trimInPoint = start;
 trimOutPoint = end;
 trimDragging = false;
 dragMode = None;
 repaint();
 auto sampleSnap = activeSampleData().getSnapshot();
 if (sampleSnap) {
 int totalFrames = sampleSnap->buffer.getNumSamples();
 trimInPoint = juce::jlimit(0, totalFrames - 1, trimInPoint);
 trimOutPoint = juce::jlimit(trimInPoint + 1, totalFrames, trimOutPoint);
 }
}

void WaveformView::setTrimPoints (int inPt, int outPt)
{
 trimInPoint = inPt;
 trimOutPoint = outPt;
 repaint();
}

void WaveformView::setTrimMode (bool active)
{
 trimMode = active;
 if (! active)
 dragMode = None;
 repaint();
}