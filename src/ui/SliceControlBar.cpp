#include "SliceControlBar.h"

// Synthetic field ID for the GLIDE cell — not in DysektProcessor::SliceParamField enum
// because it maps directly to VoicePool::legatoGlideMs (global, not per-slice).
static constexpr int kFieldGlide    = 9998;
static constexpr int kFieldRootNote = 9999;
#include <juce_gui_basics/juce_gui_basics.h>
#include "UIHelpers.h"
#include "DysektLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../MidiLearnManager.h"
#include "../audio/GrainEngine.h"
#include "../PluginEditor.h"

// ── Fixed ADSR knob colours — match LCD node colours, theme-independent ───────
static const juce::Colour kAdsrAttack { 0xFF00FF87 }; // Toxic Lime
static const juce::Colour kAdsrDecay { 0xFFFFE800 }; // Radioactive Yellow
static const juce::Colour kAdsrSustain { 0xFF00C8FF }; // Ice Blue
static const juce::Colour kAdsrRelease { 0xFFFF6B00 }; // Molten Orange
static const juce::Colour kAdsrHold    { 0xFFFF00FF }; // Hot Magenta

static juce::Colour adsrTintForField (int fieldId)
{
 using F = DysektProcessor;
 if (fieldId == F::FieldAttack) return kAdsrAttack;
 if (fieldId == F::FieldDecay) return kAdsrDecay;
 if (fieldId == F::FieldSustain) return kAdsrSustain;
 if (fieldId == F::FieldRelease) return kAdsrRelease;
 if (fieldId == F::FieldHold)    return kAdsrHold;
 return {}; // invalid = use theme default
}

namespace
{
constexpr int kParamCellTextX = 14;
constexpr int kParamCellTextWidth = 60;
constexpr int kParamCellWidth = kParamCellTextX + kParamCellTextWidth;

constexpr float kKnobStart = juce::MathConstants<float>::pi * 1.25f;
constexpr float kKnobEnd = juce::MathConstants<float>::pi * 2.75f;
}

SliceControlBar::SliceControlBar (DysektProcessor& p) : processor (p)
{
    startTimerHz (30); // always running so CC-driven marker repaints in real time
}

void SliceControlBar::timerCallback()
{
    // Advance pulse at ~1.2 Hz (full cycle every ~0.85s)
    pulsePhase += 1.0f / (30.0f / 1.2f);
    if (pulsePhase >= 1.0f) pulsePhase -= 1.0f;

    // Real-time repaint for CC-driven marker movement
    const int curLive = processor.liveDragBoundsStart.load (std::memory_order_relaxed);
    const bool liveChanged = (curLive != lastLiveDrag);
    lastLiveDrag = curLive;

    // Ghost bar: repaint whenever the pre-pickup ghost position is active.
    // The audio thread writes markerCcGhostNorm on every absolute CC message
    // before pickup; without this the UI never repaints to show the ghost.
    const float ghostNorm = processor.markerCcGhostNorm.load (std::memory_order_relaxed);
    const bool ghostActive = (ghostNorm >= 0.0f);

    // Repaint if pulse is animating (MIDI learn arm), live drag value changed,
    // or ghost bar is active (pre-pickup absolute CC sweep in progress).
    if (processor.midiLearn.isArmed() || liveChanged || ghostActive)
        repaint();
}

void SliceControlBar::updateMidiLearnPulse()
{
    // Timer is always running (started in constructor for real-time CC repaint).
    // Just reset pulse phase when arming so the blink starts cleanly.
    // Also keep requesting repaints during the 300ms arrow fade-out window.
    if (processor.midiLearn.isArmed())
        pulsePhase = 0.0f;

    const int lastMs  = processor.markerRelLastMs.load (std::memory_order_relaxed);
    const int elapsed = (int) juce::Time::getMillisecondCounter() - lastMs;
    if (processor.markerRelDir.load (std::memory_order_relaxed) != 0 && elapsed < 300)
        repaint();
}
void SliceControlBar::resized() {}

// =============================================================================
// drawLockIcon (unchanged)
// =============================================================================
void SliceControlBar::drawLockIcon (juce::Graphics& g, int x, int y, bool locked)
{
 const int iconSz = juce::roundToInt (10 * paintSf);
 if (locked)
 {
 g.setColour (getTheme().lockActive);
 g.fillRect (x, y, iconSz, iconSz);
 }
 else
 {
 g.setColour (getTheme().lockInactive.withAlpha (0.6f));
 g.drawRect (x, y, iconSz, iconSz, 1);
 }
}

// =============================================================================
// drawParamCell (unchanged — booleans / choices only)
// =============================================================================
void SliceControlBar::drawParamCell (juce::Graphics& g, int x, int y,
 const juce::String& label, const juce::String& value,
 bool locked, uint32_t lockBit,
 int fieldId, float minVal, float maxVal, float step,
 bool isBoolean, bool isChoice, int& outWidth)
{
 const int cellH = psCellH;
 const int cellW = psCellW;
 const int textX = juce::roundToInt (14.0f * paintSf);
 const int textW = juce::roundToInt (60.0f * paintSf);

 g.setFont (DysektLookAndFeel::makeFont (12.0f * paintSf));
 g.setColour (locked ? getTheme().lockActive.withAlpha (0.8f)
 : getTheme().foreground.withAlpha (0.45f));
 g.drawText (label, x + textX, y + juce::roundToInt (2.0f * paintSf), textW, juce::roundToInt (13.0f * paintSf), juce::Justification::centredLeft);

 g.setFont (DysektLookAndFeel::makeMonoFont (13.0f * paintSf));
 g.setColour (locked ? getTheme().foreground
 : getTheme().foreground.withAlpha (0.4f));
 g.drawText (value, x + textX, y + juce::roundToInt (15.0f * paintSf), textW, juce::roundToInt (14.0f * paintSf), juce::Justification::centredLeft);

 outWidth = cellW;
 cells.push_back ({ x, y, outWidth, cellH, lockBit, fieldId,
 minVal, maxVal, step, isBoolean, isChoice, false, false });
}

// =============================================================================
// drawKnob — small rotary arc
// =============================================================================
void SliceControlBar::drawKnob (juce::Graphics& g,
 int cx, int cy, int r,
 float normVal,
 bool locked, bool armed, bool mapped,
 juce::Colour tintOverride,
 bool hovered)
{
 // Hover ring — faint accent circle drawn before everything else so arcs sit on top
 if (hovered && !armed)
 {
     const float fr2 = (float) r + 3.5f;
     const float fcx2 = (float) cx, fcy2 = (float) cy;
     g.setColour (getTheme().accent.withAlpha (0.18f));
     g.drawEllipse (fcx2 - fr2, fcy2 - fr2, fr2 * 2.0f, fr2 * 2.0f, 1.2f);
 }
 const float angle = kKnobStart + normVal * (kKnobEnd - kKnobStart);
 const float fcx = (float) cx, fcy = (float) cy, fr = (float) r;

 juce::Path track;
 track.addCentredArc (fcx, fcy, fr, fr, 0.f, kKnobStart, kKnobEnd, true);
 g.setColour (getTheme().darkBar.brighter (0.22f));
 g.strokePath (track, juce::PathStrokeType (1.5f,
 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

 // Base colour: fixed tint if provided, otherwise theme accent
 const juce::Colour base = tintOverride.isTransparent() ? getTheme().accent
 : tintOverride;

 // For ADSR knobs (tintOverride set), always keep the fixed colour — even when
 // locked. Lock state is shown via the lock icon; the colour identity must not
 // change to a theme colour.
 const bool hasTint = ! tintOverride.isTransparent();

 juce::Colour arcCol = armed ? base
 : mapped ? base.withAlpha (0.7f)
 : locked ? (hasTint ? base.withAlpha (0.75f)
 : getTheme().lockActive.withAlpha (0.85f))
 : base.withAlpha (0.55f);

 juce::Path arc;
 arc.addCentredArc (fcx, fcy, fr, fr, 0.f, kKnobStart, angle, true);
 g.setColour (arcCol);
 g.strokePath (arc, juce::PathStrokeType (2.2f,
 juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

 // The arc uses JUCE's convention (0 = 12 o'clock, clockwise).
 // std::cos/sin uses standard maths (0 = 3 o'clock, counter-clockwise).
 // Offset by -π/2 so the indicator line aligns with the arc end.
 const float lineAngle = angle - juce::MathConstants<float>::halfPi;
 float lineR = fr - 2.5f;
 g.setColour (arcCol.brighter (0.15f));
 g.drawLine (fcx, fcy,
 fcx + lineR * std::cos (lineAngle),
 fcy + lineR * std::sin (lineAngle), 1.5f);

 g.setColour (locked ? getTheme().foreground.withAlpha (0.75f)
 : getTheme().foreground.withAlpha (0.25f));
 g.fillEllipse (fcx - 2.f, fcy - 2.f, 4.f, 4.f);
}

// =============================================================================
// toNorm — native value → 0-1 for knob arc
// =============================================================================
float SliceControlBar::toNorm (int fieldId, float v) const
{
 using F = DysektProcessor;
 switch (fieldId)
 {
 case F::FieldBpm: return juce::jlimit (0.f, 1.f, (v - 20.f) / (999.f - 20.f));
 case F::FieldPitch: return juce::jlimit (0.f, 1.f, (v + 48.f) / 96.f);
 case F::FieldCentsDetune: return juce::jlimit (0.f, 1.f, (v + 100.f) / 200.f);
 case F::FieldTonality: return juce::jlimit (0.f, 1.f, v / 8000.f);
 case F::FieldFormant: return juce::jlimit (0.f, 1.f, (v + 24.f) / 48.f);
 case F::FieldAttack: return juce::jlimit (0.f, 1.f, v / 1.f);
 case F::FieldHold:   return juce::jlimit (0.f, 1.f, v / 5.f);
 case F::FieldDecay: return juce::jlimit (0.f, 1.f, v / 5.f);
 case F::FieldSustain: return juce::jlimit (0.f, 1.f, v);
 case F::FieldRelease: return juce::jlimit (0.f, 1.f, v / 5.f);
 case F::FieldMuteGroup: return juce::jlimit (0.f, 1.f, v / 32.f);
 case F::FieldMidiNote: return juce::jlimit (0.f, 1.f, v / 127.f);
 case F::FieldVolume: return juce::jlimit (0.f, 1.f, (v + 100.f) / 124.f);
 case F::FieldOutputBus: return juce::jlimit (0.f, 1.f, v / 15.f);
 case F::FieldPan: return juce::jlimit (0.f, 1.f, (v + 1.f) * 0.5f);
        case F::FieldEqLowGain:  return juce::jlimit (0.f, 1.f, (v + 18.f) / 36.f);
        case F::FieldEqMidGain:  return juce::jlimit (0.f, 1.f, (v + 18.f) / 36.f);
        case F::FieldEqMidFreq:  return juce::jlimit (0.f, 1.f, (std::log2(v) - std::log2(200.f)) / (std::log2(8000.f) - std::log2(200.f)));
        case F::FieldEqMidQ:     return juce::jlimit (0.f, 1.f, (v - 0.5f) / 3.5f);
        case F::FieldEqHighGain: return juce::jlimit (0.f, 1.f, (v + 18.f) / 36.f);
    case kFieldGlide:       return juce::jlimit (0.f, 1.f, v / 200.f);
    case kFieldRootNote:    return juce::jlimit (0.f, 1.f, v / 127.f);
 default: return 0.5f;
 }
}

// =============================================================================
// drawKnobCell — rotary knob cell for all numeric params
// =============================================================================
void SliceControlBar::drawKnobCell (juce::Graphics& g, int x, int y,
 const juce::String& label,
 const juce::String& valueText,
 float normVal,
 bool locked, uint32_t lockBit,
 int fieldId,
 float minVal, float maxVal, float step,
 int& outWidth)
{
 const int cellW = psCellW;
 const int cellH = psCellH;
 const int knobCX = x + psKnobR + juce::roundToInt (3.0f * paintSf);
 const int knobCY = y + cellH / 2;

 const bool armed = (processor.midiLearn.getArmedSlot() == fieldId);
 const bool mapped = processor.midiLearn.isMapped (fieldId);
 const int sel = processor.getUiSliceSnapshot().selectedSlice;

 if (armed)
 {
     const float pulse = 0.5f + 0.5f * std::sin (pulsePhase * juce::MathConstants<float>::twoPi);
     g.setColour (getTheme().accent.withAlpha (0.08f + 0.10f * pulse));
     g.fillRoundedRectangle ((float) x, (float) y, (float) cellW, (float) cellH, 2.f);
     g.setColour (getTheme().accent.withAlpha (0.55f + 0.45f * pulse));
     g.drawRoundedRectangle ((float) x + 0.5f, (float) y + 0.5f,
                             (float) cellW - 1.f, (float) cellH - 1.f, 2.f,
                             1.0f + 1.0f * pulse);
 }

 const bool hovered = (! armed) && (cells.size() > 0) &&
                       ((int) cells.size() == hoveredCellIdx + 1 ||
                        (hoveredCellIdx >= 0 && (int) cells.size() == hoveredCellIdx));
 // Determine if this specific cell is hovered by checking against hoveredCellIdx
 // (cells is built sequentially so current cell index = cells.size() after push_back)
 // We compare after push_back below, so pass index = current cells.size() pre-push
 const int thisCellIdx = (int) cells.size();
 drawKnob (g, knobCX, knobCY, psKnobR, normVal, locked, armed, mapped,
 adsrTintForField (fieldId), (hoveredCellIdx == thisCellIdx) && !armed);

 // ── Pickup chasing indicator ─────────────────────────────────────────
 // When a knob is mapped to an absolute CC but hasn't been picked up yet,
 // draw a ghost arc showing where the CC currently is vs. the parameter.
 // This makes "silent until catchup" feel intentional rather than broken.
 if (mapped && !armed
     && fieldId >= 0 && fieldId < kMidiLearnNumSlots
     && !processor.midiLearn.isEndless (fieldId)
     && sel >= 0 && sel < DysektProcessor::kMaxCCSlices
     && !processor.ccPickedUp[(size_t) sel][(size_t) fieldId])
 {
     const float ccNorm  = processor.ccSmoothers[(size_t) sel][(size_t) fieldId].getCurrentValue();
     // Map raw smoother value back to 0-1 norm for display
     const float ccDisplayNorm = toNorm (fieldId, ccNorm);
     const float ccAngle = kKnobStart + ccDisplayNorm * (kKnobEnd - kKnobStart);
     const float fcx2 = (float) knobCX, fcy2 = (float) knobCY, fr2 = (float) psKnobR;

     // Ghost arc at CC position — white so it's visible on every theme
     juce::Path chaseArc;
     chaseArc.addCentredArc (fcx2, fcy2, fr2 + 3.5f, fr2 + 3.5f, 0.f,
                              kKnobStart, ccAngle, true);
     g.setColour (juce::Colours::white.withAlpha (0.45f));
     g.strokePath (chaseArc, juce::PathStrokeType (1.5f,
                   juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

     // Bright dot at CC tip — pure white, fully opaque
     const float dotAngle = ccAngle - juce::MathConstants<float>::halfPi;
     const float dotR = fr2 + 3.5f;
     g.setColour (juce::Colours::white.withAlpha (0.90f));
     g.fillEllipse (fcx2 + dotR * std::cos (dotAngle) - 2.0f,
                    fcy2 + dotR * std::sin (dotAngle) - 2.0f, 4.0f, 4.0f);
 }

 if (armed || mapped)
 {
 g.setFont (DysektLookAndFeel::makeFont (8.0f * paintSf));
 g.setColour (getTheme().accent.withAlpha (armed ? 1.0f : 0.65f));
 g.drawText (armed ? "ARM" : processor.midiLearn.getLabelText (fieldId),
 x, y + cellH - juce::roundToInt (10.0f * paintSf), psKnobR * 2 + juce::roundToInt (6.0f * paintSf), juce::roundToInt (10.0f * paintSf),
 juce::Justification::centred);
 }

 const int textX = knobCX + psKnobR + juce::roundToInt (4.0f * paintSf);
 // Reserve scaled lock icon space on the right
 const int lockReserve = (lockBit != 0) ? juce::roundToInt (12.0f * paintSf) : 0;
 const int textW = cellW - (textX - x) - 1 - lockReserve;
 const juce::Colour adsr = adsrTintForField (fieldId);
 const bool hasAdsr = ! adsr.isTransparent();

 g.setFont (DysektLookAndFeel::makeFont (10.0f * paintSf));
 // ADSR label always uses the fixed ADSR colour — even when locked.
 // Non-ADSR locked params use lockActive; everything else uses foreground.
 g.setColour (locked && ! hasAdsr ? getTheme().lockActive.withAlpha (0.8f)
 : hasAdsr ? adsr.withAlpha (0.70f)
 : getTheme().foreground.withAlpha (0.42f));
 g.drawText (label, textX, y + juce::roundToInt (2.0f * paintSf), textW, juce::roundToInt (12.0f * paintSf), juce::Justification::centredLeft);

 g.setFont (DysektLookAndFeel::makeMonoFont (11.0f * paintSf));
 g.setColour (locked ? getTheme().foreground
 : getTheme().foreground.withAlpha (0.38f));
 g.drawText (valueText, textX, y + juce::roundToInt (14.0f * paintSf), textW, juce::roundToInt (14.0f * paintSf), juce::Justification::centredLeft);

 outWidth = cellW;

 ParamCell c{};
 c.x = x; c.y = y; c.w = cellW; c.h = cellH;
 c.lockBit = lockBit; c.fieldId = fieldId;
 c.minVal = minVal; c.maxVal = maxVal; c.step = step;
 c.isKnob = true; c.isMidiLearnable = true;
 c.knobNorm = normVal;
 cells.push_back (c);

 // ── Lock icon — drawn to the right of the label, registers as a separate
 //    hit cell so clicking it toggles the lock without touching MIDI Learn.
 //    Only registered for knobs that have a real lockBit (lockBit == 0 means
 //    the field has no per-slice lock, e.g. FieldSliceStart).
 if (lockBit != 0)
 {
     const int kLockW = juce::roundToInt (10.0f * paintSf);
     const int kLockH = kLockW;
     // Position: right-aligned inside the text area, top of the label row
     const int lx = x + cellW - kLockW - juce::roundToInt (1.0f * paintSf);
     const int ly = y + juce::roundToInt (2.0f * paintSf);

     // Padlock body
     if (locked)
     {
         g.setColour (getTheme().lockActive.withAlpha (0.90f));
         g.fillRoundedRectangle ((float) lx + paintSf, (float) ly + 4.0f * paintSf,
                                 (float) kLockW - 2.0f * paintSf, (float) kLockH - 4.0f * paintSf, 1.0f);
         g.setColour (getTheme().lockActive);
         juce::Path shackle;
         shackle.addCentredArc ((float) lx + kLockW * 0.5f, (float) ly + 4.5f * paintSf,
                                2.5f * paintSf, 2.5f * paintSf, 0.f,
                                juce::MathConstants<float>::pi, 0.f, true);
         g.strokePath (shackle, juce::PathStrokeType (1.3f));
     }
     else
     {
         g.setColour (getTheme().lockInactive.withAlpha (0.45f));
         g.drawRoundedRectangle ((float) lx + paintSf, (float) ly + 4.0f * paintSf,
                                 (float) kLockW - 2.0f * paintSf, (float) kLockH - 4.0f * paintSf, 1.0f, 1.0f);
         juce::Path shackle;
         shackle.addCentredArc ((float) lx + kLockW * 0.5f, (float) ly + 4.0f * paintSf,
                                2.5f * paintSf, 2.5f * paintSf, 0.f,
                                juce::MathConstants<float>::pi, 0.f, true);
         g.strokePath (shackle, juce::PathStrokeType (1.0f));
     }

     // Register as a separate hit cell — isLockIcon = true
     // Use a slightly taller hit area (full label row height) so it's easy to click
     ParamCell lc{};
     lc.x = lx; lc.y = ly; lc.w = kLockW; lc.h = kLockH + juce::roundToInt (2.0f * paintSf);
     lc.lockBit = lockBit; lc.fieldId = fieldId;
     lc.isLockIcon = true;
     cells.push_back (lc);
 }
}

// =============================================================================
// drawPanSliderCell — horizontal bipolar slider for PAN
// =============================================================================
void SliceControlBar::drawPanSliderCell (juce::Graphics& g, int x, int y,
 float panValue, // -1..+1
 bool locked, int& outWidth)
{
 const int cellW = psCellW;
 const int cellH = psCellH;
 const auto& theme = getTheme();
 const auto ac = theme.accent;

 const bool armed = (processor.midiLearn.getArmedSlot() == DysektProcessor::FieldPan);
 const bool mapped = processor.midiLearn.isMapped (DysektProcessor::FieldPan);

 if (armed)
 {
     const float pulse = 0.5f + 0.5f * std::sin (pulsePhase * juce::MathConstants<float>::twoPi);
     g.setColour (ac.withAlpha (0.08f + 0.10f * pulse));
     g.fillRoundedRectangle ((float) x, (float) y, (float) cellW, (float) cellH, 2.f);
     g.setColour (ac.withAlpha (0.55f + 0.45f * pulse));
     g.drawRoundedRectangle ((float) x + 0.5f, (float) y + 0.5f,
                             (float) cellW - 1.f, (float) cellH - 1.f, 2.f,
                             1.0f + 1.0f * pulse);
 }

 // ── Label ──────────────────────────────────────────────────────────────
 g.setFont (DysektLookAndFeel::makeFont (10.0f * paintSf));
 g.setColour (locked ? theme.lockActive.withAlpha (0.8f)
 : theme.foreground.withAlpha (0.42f));
 g.drawText ("PAN", x, y + juce::roundToInt (2.0f * paintSf), cellW, juce::roundToInt (12.0f * paintSf), juce::Justification::centredLeft);

 // MIDI-learn label
 if (armed || mapped)
 {
 g.setFont (DysektLookAndFeel::makeFont (8.0f * paintSf));
 g.setColour (ac.withAlpha (armed ? 1.0f : 0.65f));
 g.drawText (armed ? "ARM" : processor.midiLearn.getLabelText (DysektProcessor::FieldPan),
 x, y + cellH - 10, cellW, 10, juce::Justification::centredLeft);
 }

 // ── Slider track ───────────────────────────────────────────────────────
 const int trackY = y + juce::roundToInt (18.0f * paintSf);
 const int trackH = juce::roundToInt (6.0f * paintSf);
 const int trackX = x + juce::roundToInt (2.0f * paintSf);
 const int trackW = cellW - juce::roundToInt (4.0f * paintSf);

 // ── Value text — centred above the slider track ────────────────────────────
 const int pct = juce::jlimit (-100, 100, (int) std::round (panValue * 100.f));
 juce::String panStr = (pct == 0) ? "C"
 : (pct < 0) ? ("L" + juce::String (-pct))
 : ("R" + juce::String ( pct));
 g.setFont (DysektLookAndFeel::makeMonoFont (10.0f * paintSf));
 g.setColour (locked ? theme.foreground : theme.foreground.withAlpha (0.75f));
 g.drawText (panStr, trackX, trackY - juce::roundToInt (11.0f * paintSf), trackW, juce::roundToInt (10.0f * paintSf), juce::Justification::centred);

 // Track background
 g.setColour (theme.darkBar.darker (0.3f));
 g.fillRoundedRectangle ((float) trackX, (float) trackY,
 (float) trackW, (float) trackH, 2.f);

 // Centre line
 const int centreX = trackX + trackW / 2;
 g.setColour (theme.foreground.withAlpha (0.18f));
 g.drawVerticalLine (centreX, (float) trackY, (float) (trackY + trackH));

 // Fill from centre toward current position
 const float norm = juce::jlimit (0.f, 1.f, (panValue + 1.f) * 0.5f);
 const int thumbX = trackX + (int) (norm * (float) trackW);
 const juce::Colour fillCol = locked ? theme.lockActive : ac;

 if (std::abs (panValue) > 0.005f)
 {
 const int fillX = (panValue < 0.f) ? thumbX : centreX;
 const int fillW = std::abs (thumbX - centreX);
 if (fillW > 0)
 {
 g.setColour (fillCol.withAlpha (locked ? 0.55f : 0.40f));
 g.fillRect (fillX, trackY + 1, fillW, trackH - 2);
 }
 }

 // Thumb
 g.setColour (locked ? theme.lockActive : (armed ? ac.brighter (0.4f) : ac));
 g.fillRoundedRectangle ((float) (thumbX - juce::roundToInt (2.0f * paintSf)), (float) (trackY - juce::roundToInt (1.0f * paintSf)),
 4.0f * paintSf, (float) (trackH + juce::roundToInt (2.0f * paintSf)), 1.5f);

 // ── Register cell ──────────────────────────────────────────────────────
 outWidth = cellW;
 ParamCell c{};
 c.x = x; c.y = y; c.w = cellW; c.h = cellH;
 c.lockBit = 0; c.fieldId = DysektProcessor::FieldPan;
 c.minVal = -1.f; c.maxVal = 1.f; c.step = 0.01f;
 c.isKnob = true; c.isMidiLearnable = true;
 c.knobNorm = norm;
 cells.push_back (c);
}


// =============================================================================
// drawMarkerSliderCell — flat LCD-style slider for MARKER (matches TrimDialog)
// =============================================================================
void SliceControlBar::drawMarkerSliderCell (juce::Graphics& g, int x, int y,
                                            int sampleVal, int totalFrames,
                                            int& outWidth)
{
    const int cellW = psCellW;
    // Stop 2px short of the separator line (which sits at row1y + psCellH)
    // so the frame bottom is visually above the separator, not on it.
    const int cellH = psCellH - juce::roundToInt (2.0f * paintSf);
    const auto& T = getTheme();
    auto cell = juce::Rectangle<int> (x, y, cellW, cellH);
    const bool armed = (processor.midiLearn.getArmedSlot() == DysektProcessor::FieldSliceStart);

    // Background + border — pulses when MIDI learn is armed
    g.setColour (T.darkBar);
    g.fillRoundedRectangle (cell.toFloat(), 3.0f);
    if (armed)
    {
        const float pulse = 0.5f + 0.5f * std::sin (pulsePhase * juce::MathConstants<float>::twoPi);
        g.setColour (T.accent.withAlpha (0.08f + 0.10f * pulse));
        g.fillRoundedRectangle (cell.toFloat(), 3.0f);
        g.setColour (T.accent.withAlpha (0.55f + 0.45f * pulse));
        g.drawRoundedRectangle (cell.toFloat().reduced (0.5f), 3.0f, 1.0f + 1.0f * pulse);
    }
    else
    {
        g.setColour (T.accent.withAlpha (0.55f));
        g.drawRoundedRectangle (cell.toFloat().reduced (0.5f), 3.0f, 1.0f);
    }

    // Progress bar along bottom edge (shows marker position in file)
    const float frac = (totalFrames > 0)
        ? juce::jlimit (0.0f, 1.0f, (float) sampleVal / (float) totalFrames)
        : 0.0f;

    const float ghostNorm   = processor.markerCcGhostNorm.load   (std::memory_order_relaxed);
    const float fineWinLo   = processor.markerFineWindowLo.load  (std::memory_order_relaxed);
    const float fineWinHi   = processor.markerFineWindowHi.load  (std::memory_order_relaxed);
    const bool mapped       = processor.midiLearn.isMapped  (DysektProcessor::FieldSliceStart);
    const bool endless      = processor.midiLearn.isEndless (DysektProcessor::FieldSliceStart);
    const int sel           = processor.getUiSliceSnapshot().selectedSlice;
    const bool prePickup    = mapped && !endless
        && sel >= 0 && sel < DysektProcessor::kMaxCCSlices
        && !processor.ccPickedUp[(size_t) sel][(size_t) DysektProcessor::FieldSliceStart];
    const bool fineActive   = mapped && !endless && !prePickup
        && fineWinLo >= 0.0f && fineWinHi > fineWinLo;

    // ── Pre-pickup ghost overlay (drawn explicitly inside cell interior) ──
    if (prePickup)
    {
        // 4 px horizontal + 2 px vertical inset — keeps ghost content clearly
        // inside the rounded-rect border at all positions including 0 and 1.
        const auto inner = cell.reduced (2).toFloat();

        g.saveState();
        g.reduceClipRegion (cell.reduced (2));

        // Dim fill from 0 to marker position (the target the knob must reach)
        const float markerX = inner.getX() + frac * inner.getWidth();
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float> (inner.getX(), inner.getY(),
                                             markerX - inner.getX(), inner.getHeight()));

        // Bright tick at the marker (pickup target) — 1 px, inside frame
        const float clampedMkr = juce::jlimit (inner.getX() + 0.5f,
                                                inner.getRight() - 0.5f, markerX);
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (clampedMkr - 0.5f, inner.getY(),
                                             1.0f, inner.getHeight()));

        // Physical knob position tick (when known) — 1 px, inside frame
        if (ghostNorm >= 0.0f)
        {
            const float knobX = inner.getX() + ghostNorm * inner.getWidth();
            const float clampedKnob = juce::jlimit (inner.getX() + 0.5f,
                                                    inner.getRight() - 0.5f, knobX);
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.fillRect (juce::Rectangle<float> (clampedKnob - 0.5f, inner.getY(),
                                                1.0f, inner.getHeight()));
        }

        g.restoreState();
    }

    // ── Post-pickup fine window overlay ───────────────────────────────────
    if (fineActive)
    {
        const auto fineInner = cell.reduced (2).toFloat();

        g.saveState();
        g.reduceClipRegion (cell.reduced (2));

        const float loX = fineInner.getX() + fineWinLo * fineInner.getWidth();
        const float hiX = fineInner.getX() + fineWinHi * fineInner.getWidth();

        // Shaded band showing the fine window range
        g.setColour (T.accent.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> (loX, fineInner.getY(),
                                             hiX - loX, fineInner.getHeight()));

        // Edge tick — low boundary
        g.setColour (T.accent.withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (loX, fineInner.getY(), 1.0f, fineInner.getHeight()));
        // Edge tick — high boundary
        g.fillRect (juce::Rectangle<float> (hiX - 1.0f, fineInner.getY(), 1.0f, fineInner.getHeight()));

        g.restoreState();
    }

    {
        // Ghost bar: 3px strip anchored to the inside-bottom of the cell frame.
        // Inset by 1px on left/right/bottom so it never bleeds past the border.
        // Clip to the frame's rounded-rect shape (not a plain rectangle) so the
        // bar is masked by the visible rounded corners at any UI scale.
        const int inset = juce::roundToInt (1.0f * paintSf);
        const int barH  = juce::roundToInt (3.0f * paintSf);
        const auto bar  = juce::Rectangle<float> (
            (float) (cell.getX()     + inset),
            (float) (cell.getBottom() - inset - barH),
            (float) (cell.getWidth() - 2 * inset),
            (float) barH);

        g.saveState();
        // Use a rounded-rect path that exactly matches the visible frame border
        // so content drawn near the corners is clipped to the rounded shape
        // rather than a plain rectangle (which leaks past the corners at higher scales).
        {
            juce::Path roundedClip;
            roundedClip.addRoundedRectangle (cell.toFloat().reduced (0.5f), 3.0f);
            g.reduceClipRegion (roundedClip);
        }

        g.setColour (T.separator);
        g.fillRect (bar);

        // Ghost position on the bottom progress strip (pre-pickup only)
        if (prePickup)
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.fillRect (bar.withWidth (bar.getWidth() * frac));
        }

        // Fine window edges on the bottom strip
        if (fineActive)
        {
            g.setColour (T.accent.withAlpha (0.60f));
            const float loX = bar.getX() + fineWinLo * bar.getWidth();
            const float hiX = bar.getX() + fineWinHi * bar.getWidth();
            g.fillRect (juce::Rectangle<float> (loX, bar.getY(), 1.0f, bar.getHeight()));
            g.fillRect (juce::Rectangle<float> (hiX - 1.0f, bar.getY(), 1.0f, bar.getHeight()));
        }

        // Solid marker bar — theme accent, always on top
        g.setColour (T.accent);
        g.fillRect (bar.withWidth (bar.getWidth() * frac));

        g.restoreState();
    }

    // FINE toggle badge — top-right corner, only when a CC is mapped
    const bool showFineBadge = mapped && !endless;
    if (showFineBadge)
    {
        const bool fineOn = processor.markerFineMode.load (std::memory_order_relaxed);
        const int bw = juce::roundToInt (22.0f * paintSf);
        const int bh = juce::roundToInt (10.0f * paintSf);
        const int bx = cell.getRight() - bw - juce::roundToInt (2.0f * paintSf);
        const int by = cell.getY() + juce::roundToInt (2.0f * paintSf);
        markerFineModeToggleArea = juce::Rectangle<int> (bx, by, bw, bh);

        g.setColour (fineOn ? T.accent.withAlpha (0.85f) : T.foreground.withAlpha (0.18f));
        g.fillRoundedRectangle (markerFineModeToggleArea.toFloat(), 2.0f);
        g.setFont (DysektLookAndFeel::makeFont (7.5f * paintSf));
        g.setColour (fineOn ? T.darkBar : T.foreground.withAlpha (0.45f));
        g.drawText ("FINE", markerFineModeToggleArea, juce::Justification::centred, false);
    }
    else
    {
        markerFineModeToggleArea = {};
    }

    // Label — top half
    const int midY = cell.getY() + cell.getHeight() / 2;
    g.setFont (DysektLookAndFeel::makeFont (8.0f * paintSf));
    g.setColour (T.accent.withAlpha (0.65f));
    // Shrink label area if fine badge is shown
    const int labelW = showFineBadge ? (cell.getWidth() - juce::roundToInt (26.0f * paintSf)) : cell.getWidth();
    g.drawText ("MARKER",
                cell.getX(), cell.getY() + 2,
                labelW, midY - cell.getY() - 2,
                juce::Justification::centred, false);

    // Value — bottom half
    g.setFont (DysektLookAndFeel::makeMonoFont (10.0f * paintSf));
    g.setColour (T.foreground);
    g.drawText (juce::String (sampleVal),
                cell.getX(), midY,
                cell.getWidth(), cell.getBottom() - midY - juce::roundToInt (3.0f * paintSf),
                juce::Justification::centred, false);

    // Directional arrow — relative encoder only, fades 300ms after last tick
    {
        const int dir     = processor.markerRelDir.load    (std::memory_order_relaxed);
        const int lastMs  = processor.markerRelLastMs.load (std::memory_order_relaxed);
        const int elapsed = (int) juce::Time::getMillisecondCounter() - lastMs;
        if (dir != 0 && elapsed < 300)
        {
            const float fade = 1.0f - (float) elapsed / 300.0f;
            g.setFont (DysektLookAndFeel::makeFont (11.0f * paintSf));
            g.setColour (T.accent.withAlpha (fade * 0.9f));
            g.drawText (dir > 0 ? juce::CharPointer_UTF8 ("\xe2\x96\xb6")
                                : juce::CharPointer_UTF8 ("\xe2\x97\x80"),
                        cell.getX(), cell.getY() + 2,
                        cell.getWidth(), cell.getHeight() - 5,
                        juce::Justification::centred, false);
        }
    }

    outWidth = cellW;

    ParamCell c{};
    c.x = x; c.y = y; c.w = cellW; c.h = cellH;
    c.lockBit = 0; c.fieldId = DysektProcessor::FieldSliceStart;
    c.minVal = 0.f; c.maxVal = 1.f; c.step = 0.001f;
    c.isKnob = true; c.isMidiLearnable = true;
    c.knobNorm = frac;
    cells.push_back (c);
}

// =============================================================================
// drawMidiLearnCell — START / END slice boundary buttons
// =============================================================================
void SliceControlBar::drawMidiLearnCell (juce::Graphics& g, int x, int y,
 const juce::String& label,
 int fieldId, int& outWidth)
{
 const int cellW = juce::roundToInt (52.0f * paintSf), cellH = psCellH;
 const bool armed = (processor.midiLearn.getArmedSlot() == fieldId);
 const bool mapped = processor.midiLearn.isMapped (fieldId);

 if (armed)
 {
 g.setColour (getTheme().accent.withAlpha (0.2f));
 g.fillRoundedRectangle ((float) x, (float) y, (float) cellW, (float) cellH, 3.f);
 }

 g.setColour (armed ? getTheme().accent
 : mapped ? getTheme().accent.withAlpha (0.48f)
 : getTheme().foreground.withAlpha (0.18f));
 g.drawRoundedRectangle ((float) x + 0.5f, (float) y + 0.5f,
 (float) cellW - 1.f, (float) cellH - 1.f, 3.f, 1.f);

 g.setFont (DysektLookAndFeel::makeFont (12.0f * paintSf));
 g.setColour (armed ? getTheme().accent
 : mapped ? getTheme().foreground.withAlpha (0.65f)
 : getTheme().foreground.withAlpha (0.38f));
 g.drawText (label, x + juce::roundToInt (5.0f * paintSf), y + juce::roundToInt (2.0f * paintSf), cellW - juce::roundToInt (6.0f * paintSf), juce::roundToInt (13.0f * paintSf), juce::Justification::centredLeft);

 g.setFont (DysektLookAndFeel::makeMonoFont (13.0f * paintSf));
 g.setColour (armed ? getTheme().accent
 : mapped ? getTheme().foreground
 : getTheme().foreground.withAlpha (0.28f));
 g.drawText (processor.midiLearn.getLabelText (fieldId),
 x + juce::roundToInt (5.0f * paintSf), y + juce::roundToInt (15.0f * paintSf), cellW - juce::roundToInt (6.0f * paintSf), juce::roundToInt (14.0f * paintSf), juce::Justification::centredLeft);

 outWidth = cellW;

 ParamCell c{};
 c.x = x; c.y = y; c.w = cellW; c.h = cellH;
 c.fieldId = fieldId;
 c.isMidiLearnBtn = true; c.isMidiLearnable = true;
 cells.push_back (c);
}

// =============================================================================
// showMidiLearnMenu
// =============================================================================
void SliceControlBar::showMidiLearnMenu (int fieldId, juce::Point<int> screenPos)
{
 const bool mapped = processor.midiLearn.isMapped (fieldId);
 juce::PopupMenu menu;
 menu.addItem (1, "Learn MIDI CC");
 if (mapped)
 menu.addItem (2, "Clear (" + processor.midiLearn.getLabelText (fieldId) + ")");

 menu.addSeparator();
 menu.addItem(1000, "Open MIDI Learn Dialog...");

 auto* topLvl = getTopLevelComponent();
 float ms = DysektLookAndFeel::getMenuScale();
 menu.showMenuAsync (
 juce::PopupMenu::Options()
 .withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1))
 .withParentComponent(topLvl)
 .withStandardItemHeight((int)(24 * ms)),
 [this, fieldId] (int result) {
 if (result == 1) { processor.midiLearn.armLearn (fieldId); repaint(); }
 else if (result == 2) { processor.midiLearn.clearMapping (fieldId); repaint(); }
 else if (result == 1000)
 {
 if (auto* editor = findParentComponentOfClass<DysektEditor>())
 editor->keyPressed(juce::KeyPress('M', juce::ModifierKeys(), 0));
 }
 }
 );
}

// =============================================================================
// paint
// =============================================================================
void SliceControlBar::paint (juce::Graphics& g)
{
 // ── LCD-style frame — matches waveform + LCD screen aesthetic ────────────
 {
 const auto ac = getTheme().accent;
 auto b = getLocalBounds();

 juce::ColourGradient outerGrad (juce::Colour (0xFF131313), 0, 0,
 juce::Colour (0xFF0E0E0E), 0, (float) b.getHeight(), false);
 g.setGradientFill (outerGrad);
 g.fillRoundedRectangle (b.toFloat(), 4.0f);

 g.setColour (ac.withAlpha (0.65f));
 g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 4.0f, 1.0f);

 auto screen = b.reduced (4);
 g.setColour (getTheme().darkBar.darker (0.55f));
 g.fillRoundedRectangle (screen.toFloat(), 2.0f);

 g.setColour (juce::Colours::black.withAlpha (0.18f));
 for (int y = screen.getY(); y < screen.getBottom(); y += 2)
 g.drawHorizontalLine (y, (float) screen.getX(), (float) screen.getRight());

 juce::ColourGradient glow (ac.withAlpha (0.06f), 0, (float) screen.getY(),
 juce::Colours::transparentBlack, 0, (float) (screen.getY() + 20), false);
 g.setGradientFill (glow);
 g.fillRoundedRectangle (screen.toFloat(), 2.0f);

 g.setColour (ac.withAlpha (0.12f));
 g.drawRoundedRectangle (screen.toFloat().expanded (0.5f), 2.0f, 1.0f);
 }

 cells.clear();

 // Scale all fixed pixel constants proportionally with component height
 paintSf = (float) getHeight() / 72.0f;
 psCellW = juce::roundToInt ((float) kParamCellWidth * paintSf);
 psCellH = juce::roundToInt (28.0f * paintSf);
 psKnobR = juce::roundToInt ((float) kKnobR * paintSf);
 auto si  = [this](int v) { return juce::roundToInt ((float) v * paintSf); };

 const auto& ui = processor.getUiSliceSnapshot();
 int idx = ui.selectedSlice;
 int numSlices = ui.numSlices;
 const int kToggleBtnW = si (52);
 int rightEdge = getWidth() - si (8) - kToggleBtnW * 2 - si (4) - si (6); // two buttons + gap
 int row1y = si (7), row2y = si (38); // centred: (72-59)/2 = 6.5 → 7px top padding
 if (idx < 0 || idx >= numSlices)
 {
 g.setFont (DysektLookAndFeel::makeFont (15.0f * paintSf));
 g.setColour (getTheme().foreground.withAlpha (0.35f));
 g.drawText ("No slice selected", si (8), si (24), si (220), si (18), juce::Justification::centredLeft);
 return;
 }

 // Read live slice values directly from sliceManager — not the UI snapshot.
 // The snapshot lags by one processBlock cycle; sliceManager has the current
 // committed value which the smoother writes to immediately.
 const auto& s = (processor.sliceManager.getNumSlices() > idx && idx >= 0)
 ? processor.sliceManager.getSlice (idx)
 : ui.slices[(size_t) juce::jmax (0, idx)];

 // ── Resolve all per-slice params against their global APVTS fallbacks ────────
 // This mirrors SliceManager::resolveParam on the UI thread so knob display,
 // drag-start values, and audio all show/play the same effective value.
 // Rule: if lock bit set → use slice value; otherwise → use global APVTS value.
 auto apvtsVal = [&] (const juce::String& id) -> float {
     auto* p = processor.apvts.getRawParameterValue (id);
     return p ? p->load() : 0.0f;
 };
 auto resolveF = [&] (uint32_t bit, float sliceVal, float globalVal) -> float {
     return (s.lockMask & bit) ? sliceVal : globalVal;
 };

 const float effPitch    = s.pitchSemitones;
 const float effCents    = s.centsDetune;
 const float effAttack   = s.attackSec;    // always per-slice
 const float effDecay    = s.decaySec;     // always per-slice
 const float effSustain  = s.sustainLevel; // always per-slice
 const float effRelease  = s.releaseSec;   // always per-slice
 const float effVolume   = resolveF (kLockVolume,  s.volume,          apvtsVal (ParamIds::masterVolume));
 const float effPan      = resolveF (kLockPan,     s.pan,             apvtsVal (ParamIds::defaultPan));
 const float effTonality = resolveF (kLockTonality, s.tonalityHz,     apvtsVal (ParamIds::defaultTonality));
 const float effFormant  = resolveF (kLockFormant,  s.formantSemitones, apvtsVal (ParamIds::defaultFormant));
 const bool  effFComp    = resolveF (kLockFormantComp, s.formantComp ? 1.f : 0.f, apvtsVal (ParamIds::defaultFormantComp)) > 0.5f;

 const int   effOutputBus = (int) resolveF (kLockOutputBus, (float) s.outputBus, 0.0f);
 const bool  effStretch  = resolveF (kLockStretch, s.stretchEnabled ? 1.f : 0.f, apvtsVal (ParamIds::defaultStretchEnabled)) > 0.5f;

 // Aliases used by existing paint code further down
 const bool  stretchVal    = effStretch;

 const float effEqLow  = resolveF (kLockEqLow,  s.eqLowGain,  apvtsVal (ParamIds::defaultEqLowGain));
 const float effEqMidG = resolveF (kLockEqMid,  s.eqMidGain,  apvtsVal (ParamIds::defaultEqMidGain));
 const float effEqMidF = resolveF (kLockEqMid,  s.eqMidFreq,  apvtsVal (ParamIds::defaultEqMidFreq));
 const float effEqMidQ = resolveF (kLockEqMid,  s.eqMidQ,     apvtsVal (ParamIds::defaultEqMidQ));
 const float effEqHigh = resolveF (kLockEqHigh, s.eqHighGain, apvtsVal (ParamIds::defaultEqHighGain));

 int cw;
 using F = DysektProcessor;



 // Slice index label removed — shown in waveform and LCD panels already.

 // ── Row 1 params ──────────────────────────────────────────────────
 int x = si (8);

 // PITCH — knob (no lock — always per-slice)
 {
 float pv = effPitch;
 int pvi = (int) std::round (pv);
 drawKnobCell (g, x, row1y, "PITCH",
 (pvi >= 0 ? "+" : "") + juce::String (pvi) + "st",
 toNorm (F::FieldPitch, pv),
 true, 0, F::FieldPitch, -48.f, 48.f, 0.1f, cw);
 x += cw + si (4);
 }

 // TUNE — knob (no lock — always per-slice)
 {
 float cv = effCents;
 int cvi = juce::jlimit (-100, 100, (int) std::round (cv));
 drawKnobCell (g, x, row1y, "TUNE",
 (cvi >= 0 ? "+" : "") + juce::String (cvi) + "ct",
 toNorm (F::FieldCentsDetune, cv),
 true, 0, F::FieldCentsDetune, -100.f, 100.f, 0.1f, cw);
 x += cw + si (4);
 }

 // ALGO selector removed — Repitch vs Stretch is now derived from the STCH toggle
 if (stretchVal)
 {
 float tv = effTonality;
 drawKnobCell (g, x, row1y, "ROOT",
 juce::String ((int) tv) + "Hz",
 toNorm (F::FieldTonality, tv),
 true, 0, F::FieldTonality, 0.f, 8000.f, 100.f, cw);
 x += cw + si (4);

 float fv = effFormant;
 drawKnobCell (g, x, row1y, "BODY",
 (fv >= 0.f ? "+" : "") + juce::String (fv, 1),
 toNorm (F::FieldFormant, fv),
 true, 0, F::FieldFormant, -24.f, 24.f, 0.1f, cw);
 x += cw + si (4);

 bool fmntCVal = effFComp;
 drawParamCell (g, x, row1y, "FMNT C", fmntCVal ? "ON" : "OFF",
 true, 0, F::FieldFormantComp,
 0.f, 1.f, 1.f, true, false, cw);
 x += cw + si (4);
 }
 // STRETCH — boolean (no lock)
 {
 bool sv = effStretch;
 drawParamCell (g, x, row1y, "STRETCH", sv ? "ON" : "OFF",
 true, 0, F::FieldStretchEnabled,
 0.f, 1.f, 1.f, true, false, cw);
 x += cw + si (4);
 }

 // MARKER — slice boundary knob in row 1
 // During MIDI CC movement, read from live drag atomics so the readout
 // tracks the encoder in real time instead of waiting for idle commit.
 {
 g.setColour (getTheme().separator.withAlpha (0.5f));
 g.drawVerticalLine (x + 2, (float) row1y + 4, (float) row1y + 28);
 x += 8;

 const int liveIdx = processor.liveDragSliceIdx.load (std::memory_order_acquire);
 const int markerSample = (liveIdx == idx)
 ? processor.liveDragBoundsStart.load (std::memory_order_relaxed)
 : s.startSample;
 drawMarkerSliderCell (g, x, row1y, markerSample, ui.sampleNumFrames, cw);
 x += cw + si (4);
 }

 // GAIN, PAN, OUT — mix group in row 1
 {
 g.setColour (getTheme().separator.withAlpha (0.5f));
 g.drawVerticalLine (x + 2, (float) row1y + 4, (float) row1y + 28);
 x += 8;

 // GAIN (no lock — always per-slice)
 {
 float gv = effVolume;
 drawKnobCell (g, x, row1y, "GAIN",
 (gv >= 0.f ? "+" : "") + juce::String (gv, 1) + "dB",
 toNorm (F::FieldVolume, gv),
 true, 0, F::FieldVolume, -100.f, 24.f, 0.1f, cw);
 x += cw + si (4);
 }

 // PAN (no lock — always per-slice)
 {
 float pv = effPan;
 drawPanSliderCell (g, x, row1y, pv, true, cw);
 x += cw + si (4);
 }

 // OUT (no lock — always per-slice)
 {
 int ov = effOutputBus;
 const juce::String outLabel = (ov == 0) ? juce::String ("MAIN") : ("AUX " + juce::String (ov));
 drawParamCell (g, x, row1y, "OUT", outLabel,
 true, 0, F::FieldOutputBus, 0.f, 15.f, 1.f,
 false, true, cw);
 x += cw + si (4);
 }

 // ── Separator before chromatic group ────────────────────────────────
 g.setColour (getTheme().separator.withAlpha (0.5f));
 g.drawVerticalLine (x + 2, (float) row1y + 4.f, (float) row1y + 28.f);
 x += 8;

 // CHRO — chromatic channel badge (per-slice)
 {
     const int  chVal        = s.chromaticChannel;  // always read from slice
     drawChroBadgeCell (g, x, row1y, chVal, true, cw);
     x += cw + si (4);
 }

 // LEGATO — chromatic legato toggle (per-slice, no lock)
 {
     const bool legatoOn     = s.chromaticLegato;  // always read from slice
     drawLegatoToggleCell (g, x, row1y, legatoOn, true, cw);
     x += cw + si (4);
 }

 // ALGO -- Repitch / Stretch toggle (chromatic mode only, no lock)
 if (s.chromaticChannel > 0)
 {
     const int  algoVal    = s.algorithm;
     drawParamCell (g, x, row1y,
                    "MODE",
                    algoVal == 0 ? "RPITCH" : "STRETCH",
                    true, 0, F::FieldAlgorithm,
                    0.f, 1.f, 1.f,
                    false, true, cw);
     x += cw + si (4);

     // ROOT — chromatic reference MIDI note (only meaningful when chromaticChannel > 0)
     {
         const int liveRoot = processor.sliceManager.rootNote.load (std::memory_order_relaxed);
         static const char* kNoteNames[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
         const int rn = juce::jlimit (0, 127, liveRoot);
         juce::String rootStr = juce::String (kNoteNames[rn % 12]) + juce::String (rn / 12 - 2);
         drawKnobCell (g, x, row1y, "ROOT", rootStr,
                       toNorm (kFieldRootNote, (float) liveRoot),
                       false, 0, kFieldRootNote,
                       0.f, 127.f, 1.f, cw);
         x += cw + si (4);
     }
 }
 }
 g.setColour (getTheme().separator);
 g.drawHorizontalLine (si (36), (float) si (8), (float) getWidth() - (float) si (8));

 // ── Row 2 ─────────────────────────────────────────────────────────
 x = si (8);
 int adsrGroupX1 = x, adsrGroupX2 = x;
float relMaxSec = 5.0f;
{
    const int total = processor.sampleData.getNumFrames();
    if (total > 0)
    {
        const int sliceEnd = processor.sliceManager.getEndForSlice (idx, total);
        const int sliceLen = sliceEnd - s.startSample;
        const float sr = (float) processor.voicePool.getSampleRate();
        if (sliceLen > 0 && sr > 0.0f)
            relMaxSec = juce::jmax (0.001f, (float) sliceLen / sr);
    }
}

 // ATK — knob (stored seconds, display ms)
 {
 adsrGroupX1 = x;
 bool locked = (s.lockMask & kLockAttack) != 0;
 float atk = effAttack;
 drawKnobCell (g, x, row2y, "ATK",
 juce::String ((int) (atk * 1000.f)) + "ms",
 toNorm (F::FieldAttack, atk),
 locked, kLockAttack, F::FieldAttack, 0.f, 1.f, 0.001f, cw);
 x += cw + si (4);
 }


 // HLD removed

 // DEC — knob
 {
 bool locked = (s.lockMask & kLockDecay) != 0;
 float dec = effDecay;
 drawKnobCell (g, x, row2y, "DEC",
 juce::String ((int) (dec * 1000.f)) + "ms",
 toNorm (F::FieldDecay, dec),
 locked, kLockDecay, F::FieldDecay, 0.f, 5.f, 0.001f, cw);
 x += cw + si (4);
 }

 // SUS — knob (stored 0-1, display %)
 {
 bool locked = (s.lockMask & kLockSustain) != 0;
 float sus = effSustain;
 drawKnobCell (g, x, row2y, "SUS",
 juce::String ((int) (sus * 100.f)) + "%",
 toNorm (F::FieldSustain, sus),
 locked, kLockSustain, F::FieldSustain, 0.f, 1.f, 0.01f, cw);
 x += cw + si (4);
 }

 // REL — knob
 {
 bool locked = (s.lockMask & kLockRelease) != 0;
 float rel = effRelease;
// REL knob norm mirrors SliceWaveformLcd's inverted sqrt mapping:
//   LCD:  rx = kMax - sqrt(releaseMs/kViewMs) * range   → node RIGHT when rel=0
//   Knob: norm = 1 - sqrt(rel/relMaxSec)                → arc RIGHT when rel=0
// This aligns the knob arc direction with the node position:
//   rel=0ms  → norm=1.0 → knob hard RIGHT  (node at hard right)
//   rel=max  → norm=0.0 → knob hard LEFT   (node at hard left)
const float relNorm = juce::jlimit (0.f, 1.f,
    1.0f - std::sqrt (juce::jmin (rel / relMaxSec, 1.0f)));
 drawKnobCell (g, x, row2y, "REL",
 juce::String ((int) (rel * 1000.f)) + "ms",
 relNorm,
locked, kLockRelease, F::FieldRelease, 0.f, relMaxSec, 0.001f, cw);
 x += cw + si (4);
 adsrGroupX2 = x - 4;
 }

    // GLIDE — shown only when this slice is in chromatic mode
    if (s.chromaticChannel > 0)
    {
        const float glideMs = processor.voicePool.legatoGlideMs.load (std::memory_order_relaxed);
        const juce::String glideStr = (glideMs < 1.0f) ? "0ms"
                                                        : juce::String ((int) glideMs) + "ms";
        drawKnobCell (g, x, row2y, "GLIDE", glideStr,
                      juce::jlimit (0.f, 1.f, glideMs / 200.f),
                      false, 0, kFieldGlide,
                      0.f, 200.f, 1.f, cw);
        x += cw + si (4);
    }

 // ── Separator before EQ group ───────────────────────────────────────────
 g.setColour (getTheme().separator.withAlpha (0.5f));
 g.drawVerticalLine (x + 2, (float) row2y + 4.f, (float) row2y + 28.f);
 x += si (8);

 // ── EQ knobs: LOW | MID | FREQ | Q | HIGH ───────────────────────────────────
 {
     auto gainStr = [](float db) -> juce::String {
         return (db >= 0.f ? "+" : "") + juce::String (db, 1) + "dB";
     };

     int eqCw = 0;
     drawKnobCell (g, x, row2y, "LOW", gainStr (effEqLow),
                   toNorm (F::FieldEqLowGain, effEqLow),
                   false, 0, F::FieldEqLowGain,
                   -18.f, 18.f, 0.1f, eqCw);
     x += eqCw + si (4);

     drawKnobCell (g, x, row2y, "MID", gainStr (effEqMidG),
                   toNorm (F::FieldEqMidGain, effEqMidG),
                   false, 0, F::FieldEqMidGain,
                   -18.f, 18.f, 0.1f, eqCw);
     x += eqCw + si (4);

     drawKnobCell (g, x, row2y, "HIGH", gainStr (effEqHigh),
                   toNorm (F::FieldEqHighGain, effEqHigh),
                   false, 0, F::FieldEqHighGain,
                   -18.f, 18.f, 0.1f, eqCw);
     x += eqCw + si (4);

     drawKnobCell (g, x, row2y, "FREQ",
                   juce::String ((int) effEqMidF) + "Hz",
                   toNorm (F::FieldEqMidFreq, effEqMidF),
                   false, 0, F::FieldEqMidFreq,
                   200.f, 8000.f, 10.f, eqCw);
     x += eqCw + si (4);

     drawKnobCell (g, x, row2y, "Q",
                   juce::String (effEqMidQ, 1),
                   toNorm (F::FieldEqMidQ, effEqMidQ),
                   false, 0, F::FieldEqMidQ,
                   0.5f, 4.f, 0.01f, eqCw);
     x += eqCw + si (4);

 }

 // ── Meter — takes remaining space after EQ ──────────────────────────────────
 {
     const int meterX = x + si (4);
     const int meterW = juce::jmax (si (20), rightEdge - meterX - si (4));
     const int meterY = row2y + si (4);
     const int meterH = si (22);

     // Background + border
     g.setColour (getTheme().separator.withAlpha (0.20f));
     g.fillRect (meterX, meterY, meterW, meterH);
     g.setColour (getTheme().separator.withAlpha (0.50f));
     g.drawRect (meterX, meterY, meterW, meterH);

     if (idx >= 0 && idx < DysektProcessor::kMaxMeterSlices)
     {
         const float pkL = processor.slicePeakL[(size_t) idx].load (std::memory_order_relaxed);
         const float pkR = processor.slicePeakR[(size_t) idx].load (std::memory_order_relaxed);
         const int barH  = (meterH - 2) / 2;
         const int inner = meterW - 4; // pixels available for signal fill

         auto drawBar = [&] (int barY, float peak)
         {
             const int litW = juce::roundToInt (juce::jlimit (0.f, 1.f, peak) * (float) inner);
             // Lit portion
             if (litW > 0)
             {
                 // Green/accent section (0-75%)
                 const int greenW = juce::jmin (litW, juce::roundToInt (0.75f * (float) inner));
                 if (greenW > 0)
                 {
                     g.setColour (getTheme().accent.withAlpha (0.70f));
                     g.fillRect (meterX + 2, barY, greenW, barH);
                 }
                 // Red section (75-100%)
                 const int redStart = juce::roundToInt (0.75f * (float) inner);
                 const int redW = litW - greenW;
                 if (redW > 0)
                 {
                     g.setColour (juce::Colour (0xFFE05050).withAlpha (0.80f));
                     g.fillRect (meterX + 2 + redStart, barY, redW, barH);
                 }
             }
             // Unlit portion (dim)
             const int unlitStart = litW;
             const int unlitW     = inner - unlitStart;
             if (unlitW > 0)
             {
                 g.setColour (getTheme().foreground.withAlpha (0.06f));
                 g.fillRect (meterX + 2 + unlitStart, barY, unlitW, barH);
             }
         };

         drawBar (meterY + 1,             pkL);
         drawBar (meterY + 1 + barH + 1,  pkR);
     }
 }

 {
 g.setFont (DysektLookAndFeel::makeFont (7.5f * paintSf, true));
 g.setColour (getTheme().foreground.withAlpha (0.40f));

 auto drawGroupLabel = [&] (int x1, int x2, const char* label)
 {
 // Draw label centred over the group, sitting on the separator line
 g.drawText (label, x1, 26, x2 - x1, 9,
 juce::Justification::centred);
 };

 if (adsrGroupX2 > adsrGroupX1) drawGroupLabel (adsrGroupX1, adsrGroupX2, "");
 }

 // ── PAD / WAVE — two separate toggle buttons side by side ────────────────────────────────────
 {
     const int btnY   = si (9);  // centred in row1 (y=7..35): (7+35-24)/2 = 9
     const int btnH   = si (24);
     const int gap    = si (4);
     const int rightX = getWidth() - si (8);

     padToggleBtnArea  = juce::Rectangle<int> (rightX - kToggleBtnW * 2 - gap, btnY, kToggleBtnW, btnH);
     waveToggleBtnArea = juce::Rectangle<int> (rightX - kToggleBtnW,            btnY, kToggleBtnW, btnH);

     g.setFont (DysektLookAndFeel::makeFont (9.5f * paintSf, true));

     auto drawBtn = [&] (const juce::Rectangle<int>& area, const juce::String& label, bool active)
     {
         const auto bg = active
             ? getTheme().accent.withAlpha (0.22f)
             : getTheme().separator.withAlpha (0.10f);
         g.setColour (bg);
         g.fillRoundedRectangle (area.toFloat(), 4.0f);

         const auto border = active
             ? getTheme().accent.withAlpha (0.80f)
             : getTheme().separator.withAlpha (0.35f);
         g.setColour (border);
         g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);

         g.setColour (active
             ? getTheme().accent
             : getTheme().foreground.withAlpha (0.50f));
         g.drawText (label, area, juce::Justification::centred);
     };

     drawBtn (padToggleBtnArea,  "PADS",  padViewActive);
     drawBtn (waveToggleBtnArea, "WAVE", !padViewActive);
 }
}

// =============================================================================
// mouseDown
// =============================================================================
void SliceControlBar::mouseDown (const juce::MouseEvent& e)
{
    // ── PAD / WAVE — two separate buttons, each sets its own active state ───
    if (e.mods.isLeftButtonDown())
    {
        if (padToggleBtnArea.contains (e.getPosition()) && !padViewActive)
        {
            padViewActive = true;
            repaint();
            if (onPadViewToggle) onPadViewToggle (true);
            return;
        }
        if (waveToggleBtnArea.contains (e.getPosition()) && padViewActive)
        {
            padViewActive = false;
            repaint();
            if (onPadViewToggle) onPadViewToggle (false);
            return;
        }
        if (padToggleBtnArea.contains (e.getPosition()) || waveToggleBtnArea.contains (e.getPosition()))
            return; // already active — swallow click, no-op
    }

 // ── Lock guard: block all param changes if selected slice is fully locked ─
 {
 const auto& snap = processor.getUiSliceSnapshot();
 if (snap.selectedSlice >= 0 && snap.selectedSlice < snap.numSlices)
 {
 const auto& sl = snap.slices[(size_t) snap.selectedSlice];
 if (sl.lockMask == 0xFFFFFFFFu)
 {
 repaint();
 return;
 }
 }
 }

 if (textEditor != nullptr) textEditor.reset();
 activeDragCell = -1;
 auto pos = e.getPosition();
 const auto& ui = processor.getUiSliceSnapshot();

 // FINE mode toggle badge — check before cell loop so it doesn't start a drag
 if (!markerFineModeToggleArea.isEmpty() && markerFineModeToggleArea.contains (pos))
 {
     const bool cur = processor.markerFineMode.load (std::memory_order_relaxed);
     processor.markerFineMode.store (!cur, std::memory_order_relaxed);
     // Switching to normal mode: clear fine window UI state
     if (cur)
     {
         processor.markerFineWindowLo.store (-1.0f, std::memory_order_relaxed);
         processor.markerFineWindowHi.store (-1.0f, std::memory_order_relaxed);
     }
     repaint();
     return;
 }

 // ── Priority pass: lock icons share bounds with their knob cell.
 //    Check lock-icon cells first so they always win the hit test.
 for (int li = 0; li < (int) cells.size(); ++li)
 {
     const auto& lc = cells[(size_t) li];
     if (! lc.isLockIcon || lc.lockBit == 0) continue;
     if (! juce::Rectangle<int> (lc.x, lc.y, lc.w, lc.h).contains (pos)) continue;
     const auto& snap = processor.getUiSliceSnapshot();
     const int sIdx = snap.selectedSlice;
     if (sIdx >= 0 && sIdx < snap.numSlices)
     {
         DysektProcessor::Command cmd;
         cmd.type      = DysektProcessor::CmdToggleLock;
         cmd.intParam1 = sIdx;
         cmd.intParam2 = (int) lc.lockBit;
         processor.pushCommand (cmd);
         repaint();
     }
     return;
 }

 for (int i = 0; i < (int) cells.size(); ++i)
 {
 const auto& cell = cells[(size_t) i];
 if (cell.isLockIcon) continue; // handled in priority pass above
 if (! juce::Rectangle<int> (cell.x, cell.y, cell.w, cell.h).contains (pos)) continue;

 // MIDI Learn boundary button
 if (cell.isMidiLearnBtn)
 {
 if (e.mods.isRightButtonDown()) showMidiLearnMenu (cell.fieldId, e.getScreenPosition());
 else
 {
 if (processor.midiLearn.getArmedSlot() == cell.fieldId)
 processor.midiLearn.armLearn (-1);
 else
 processor.midiLearn.armLearn (cell.fieldId);
 repaint();
 }
 return;
 }

 // Knob right-click → MIDI Learn menu
 if (cell.isKnob && e.mods.isRightButtonDown())
 {
 showMidiLearnMenu (cell.fieldId, e.getScreenPosition()); return;
 }

 if (cell.isSetBpm) { return; } // SET BPM removed

 if (cell.isReadOnly) return;

 // Knob left-click
 if (cell.isKnob)
 {
 if (processor.midiLearn.getArmedSlot() == cell.fieldId)
 {
 processor.midiLearn.armLearn (-1); repaint(); return;
 }
 DysektProcessor::Command gc; gc.type = DysektProcessor::CmdBeginGesture;
 processor.pushCommand (gc);
 activeDragCell = i;
 activeCellSnapshot = cell; // snapshot before cells[] is rebuilt by next repaint()
 // Pan and Marker sliders are horizontal - store x; all others store y
 dragStartY = (cell.fieldId == DysektProcessor::FieldPan
            || cell.fieldId == DysektProcessor::FieldSliceStart) ? pos.x : pos.y;

 // Activate live drag for slice boundary knobs
 if (cell.fieldId == DysektProcessor::FieldSliceStart)
 {
 int liveSel = ui.selectedSlice;
 if (liveSel >= 0 && liveSel < ui.numSlices)
 {
 processor.liveDragBoundsStart.store (ui.slices[(size_t) liveSel].startSample, std::memory_order_relaxed);
 processor.liveDragBoundsEnd.store (processor.sliceManager.getEndForSlice (liveSel, ui.sampleNumFrames), std::memory_order_relaxed);
 processor.liveDragSliceIdx.store (liveSel, std::memory_order_release);
 }
 }

 int sIdx = ui.selectedSlice;
 if (sIdx >= 0 && sIdx < ui.numSlices)
 {
 const auto& sl = ui.slices[(size_t) sIdx];
 using F = DysektProcessor;
 // Resolve each field against its global APVTS fallback — mirrors
 // SliceManager::resolveParam so the drag starts from what the audio
 // engine actually uses, not the raw (possibly stale) per-slice field.
 auto apvtsRaw = [&] (const juce::String& id) -> float {
     auto* p = processor.apvts.getRawParameterValue (id);
     return p ? p->load() : 0.0f;
 };
 auto res = [&] (uint32_t bit, float sv, float gv) -> float {
     return (sl.lockMask & bit) ? sv : gv;
 };
 switch (cell.fieldId)
 {
 case F::FieldBpm:         dragStartValue = sl.bpm; break;
 case F::FieldPitch:       dragStartValue = sl.pitchSemitones; break;
 case F::FieldCentsDetune: dragStartValue = sl.centsDetune;    break;
 case F::FieldTonality:    dragStartValue = res (kLockTonality,    sl.tonalityHz,       apvtsRaw (ParamIds::defaultTonality)); break;
 case F::FieldFormant:     dragStartValue = res (kLockFormant,     sl.formantSemitones, apvtsRaw (ParamIds::defaultFormant)); break;
 case F::FieldAttack:      dragStartValue = sl.attackSec;    break;
 case F::FieldHold:        dragStartValue = res (kLockHold,        sl.holdSec,          apvtsRaw (ParamIds::defaultHold)    / 1000.f); break;
 case F::FieldDecay:       dragStartValue = sl.decaySec;     break;
 case F::FieldSustain:     dragStartValue = sl.sustainLevel; break;
 case F::FieldRelease:     dragStartValue = sl.releaseSec;   break;
 case F::FieldVolume:      dragStartValue = res (kLockVolume,      sl.volume,           apvtsRaw (ParamIds::masterVolume)); break;
 case F::FieldPan:         dragStartValue = res (kLockPan,         sl.pan,              apvtsRaw (ParamIds::defaultPan)); break;
        case F::FieldEqLowGain:   dragStartValue = res (kLockEqLow,   sl.eqLowGain,    apvtsRaw (ParamIds::defaultEqLowGain)); break;
        case F::FieldEqMidGain:   dragStartValue = res (kLockEqMid,   sl.eqMidGain,    apvtsRaw (ParamIds::defaultEqMidGain)); break;
        case F::FieldEqMidFreq:   dragStartValue = res (kLockEqMid,   sl.eqMidFreq,    apvtsRaw (ParamIds::defaultEqMidFreq)); break;
        case F::FieldEqMidQ:      dragStartValue = res (kLockEqMid,   sl.eqMidQ,       apvtsRaw (ParamIds::defaultEqMidQ));    break;
        case F::FieldEqHighGain:  dragStartValue = res (kLockEqHigh,  sl.eqHighGain,   apvtsRaw (ParamIds::defaultEqHighGain)); break;
 case F::FieldMuteGroup:   dragStartValue = (float)((sl.lockMask & kLockMuteGroup) ? sl.muteGroup : (int) apvtsRaw (ParamIds::defaultMuteGroup)); break;
 case F::FieldOutputBus:   dragStartValue = (float)((sl.lockMask & kLockOutputBus) ? sl.outputBus : 0); break;
 case F::FieldSliceStart:  dragStartValue = (float) sl.startSample; break;
 case F::FieldMidiNote:    dragStartValue = (float) sl.midiNote; break;
 case kFieldGlide:
     dragStartValue = processor.voicePool.legatoGlideMs.load (std::memory_order_relaxed);
     break;
 case kFieldRootNote:
     dragStartValue = (float) processor.sliceManager.rootNote.load (std::memory_order_relaxed);
     break;
 default: dragStartValue = 0.f; break;
 }
 }
 return;
 }

 // Boolean toggle
 if (cell.isBoolean)
 {
 int sIdx = ui.selectedSlice;
 if (sIdx >= 0 && sIdx < ui.numSlices)
 {
 const auto& sl = ui.slices[(size_t) sIdx];
 bool sliceLocked = (sl.lockMask & cell.lockBit) != 0;
 bool currentVal = false;
 using F = DysektProcessor;
 if (cell.fieldId == F::FieldStretchEnabled) currentVal = sliceLocked ? sl.stretchEnabled : (processor.apvts.getRawParameterValue (ParamIds::defaultStretchEnabled)->load() > 0.5f);
 else if (cell.fieldId == F::FieldFormantComp) currentVal = sliceLocked ? sl.formantComp : (processor.apvts.getRawParameterValue (ParamIds::defaultFormantComp)->load() > 0.5f);
 else if (cell.fieldId == F::FieldReleaseTail) currentVal = sliceLocked ? sl.releaseTail : (processor.apvts.getRawParameterValue (ParamIds::defaultReleaseTail)->load() > 0.5f);
 else if (cell.fieldId == F::FieldReverse) currentVal = sliceLocked ? sl.reverse : (processor.apvts.getRawParameterValue (ParamIds::defaultReverse)->load() > 0.5f);
 else if (cell.fieldId == F::FieldChromaticLegato) currentVal = sl.chromaticLegato;
 else if (cell.fieldId == F::FieldOneShot)
 {
 // Two-pill: left=ONE SHOT(1), right=HOLD(0) — pick by click X vs cell centre
 const bool clickOneShot = (e.x < (cell.x + cell.w / 2));
 DysektProcessor::Command pCmd;
 pCmd.type = DysektProcessor::CmdSetSliceParam;
 pCmd.intParam1 = F::FieldOneShot;
 pCmd.floatParam1 = clickOneShot ? 1.f : 0.f;
 processor.pushCommand (pCmd); repaint();
 return;
 }
 DysektProcessor::Command cmd;
 cmd.type = DysektProcessor::CmdSetSliceParam;
 cmd.intParam1 = cell.fieldId; cmd.floatParam1 = currentVal ? 0.f : 1.f;
 processor.pushCommand (cmd); repaint();
 }
 return;
 }

 // Choice — show PopupMenu for discrete params
 if (cell.isChoice)
 {
 int sIdx = ui.selectedSlice;
 if (sIdx < 0 || sIdx >= ui.numSlices) return;

 using F = DysektProcessor;
 const auto& sl = ui.slices[(size_t) sIdx];

 juce::PopupMenu menu;
 const int fieldId = cell.fieldId;

 auto addItems = [&] (const juce::StringArray& names, int currentVal)
 {
 for (int i = 0; i < names.size(); ++i)
 {
 const bool ticked = (i == currentVal);
 menu.addItem (i + 1, names[i], true, ticked);
 }
 };

 if (fieldId == F::FieldAlgorithm)
 {
 int cur = (sl.lockMask & kLockAlgorithm) ? sl.algorithm
 : (int) processor.apvts.getRawParameterValue (ParamIds::defaultAlgorithm)->load();
 addItems ({ "Repitch", "Stretch" }, cur);
 }
 else if (fieldId == F::FieldLoop)
 {
 int cur = (sl.lockMask & kLockLoop) ? sl.loopMode
 : (int) processor.apvts.getRawParameterValue (ParamIds::defaultLoop)->load();
 addItems ({ "Off", "Loop", "Ping-Pong" }, cur);
 }
 else if (fieldId == F::FieldMuteGroup)
 {
 int cur = (sl.lockMask & kLockMuteGroup) ? sl.muteGroup
 : (int) processor.apvts.getRawParameterValue (ParamIds::defaultMuteGroup)->load();
 juce::StringArray names; names.add ("Off");
 for (int i = 1; i <= 32; ++i) names.add ("Group " + juce::String (i));
 addItems (names, cur);
 }
 else if (fieldId == F::FieldOutputBus)
 {
 int cur = (sl.lockMask & kLockOutputBus) ? sl.outputBus : 0;
 juce::StringArray names; names.add ("Main");
 for (int i = 1; i <= 15; ++i) names.add ("Aux " + juce::String (i));
 addItems (names, cur);
 }
 else if (fieldId == F::FieldChromaticChannel)
 {
 int cur = sl.chromaticChannel;  // always read from slice
 juce::StringArray names; names.add ("Off");
 for (int i = 1; i <= 16; ++i) names.add ("Channel " + juce::String (i));
 addItems (names, cur);
 }
 else
 {
 // Fallback: old-style cycle
 int current = 0;
 if (fieldId == F::FieldGrainMode) current = (sl.lockMask & kLockGrainMode) ? sl.grainMode : (int) processor.apvts.getRawParameterValue (ParamIds::defaultGrainMode)->load();
 int next = (current + 1) > (int) cell.maxVal ? 0 : current + 1;
 DysektProcessor::Command cmd;
 cmd.type = DysektProcessor::CmdSetSliceParam;
 cmd.intParam1 = fieldId; cmd.floatParam1 = (float) next;
 processor.pushCommand (cmd); repaint();
 return;
 }

 const auto cellScreenRect = localAreaToGlobal (
 juce::Rectangle (cell.x, cell.y, cell.w, cell.h));
 menu.showMenuAsync (juce::PopupMenu::Options()
 .withTargetScreenArea (cellScreenRect)
 .withParentComponent (getTopLevelComponent()),
 [this, fieldId] (int result)
 {
 if (result <= 0) return;
 DysektProcessor::Command cmd;
 cmd.type = DysektProcessor::CmdSetSliceParam;
 cmd.intParam1 = fieldId;
 cmd.floatParam1 = (float)(result - 1);
 processor.pushCommand (cmd);
 repaint();
 });
 return;
 }
 }
}

// =============================================================================
// mouseDrag
// =============================================================================
void SliceControlBar::mouseDrag (const juce::MouseEvent& e)
{
 // ── Lock guard: block all param changes if selected slice is fully locked ─
 {
 const auto& snap = processor.getUiSliceSnapshot();
 if (snap.selectedSlice >= 0 && snap.selectedSlice < snap.numSlices)
 {
 const auto& sl = snap.slices[(size_t) snap.selectedSlice];
 if (sl.lockMask == 0xFFFFFFFFu)
 {
 repaint();
 return;
 }
 }
 }

 if (activeDragCell < 0) return;
 // Use the snapshot taken at mouseDown — cells[] is rebuilt by every repaint()
 // so indexing by activeDragCell after a repaint reads the wrong cell.
 const auto& cell = activeCellSnapshot;
 if (! cell.isKnob) return;

 float deltaY = (float) (dragStartY - e.y);
 using F = DysektProcessor;

 // ── Slice boundary knobs: live drag in sample space ───────────────────
 if (cell.fieldId == F::FieldSliceStart)
 {
 const auto& ui2 = processor.getUiSliceSnapshot();
 int liveSel = ui2.selectedSlice;
 if (liveSel >= 0 && liveSel < ui2.numSlices && ui2.sampleNumFrames > 1)
 {
 // Scale: drag 300px = full sample length, shift = fine mode
 float sensitivity = (float) ui2.sampleNumFrames / 300.f;
 if (e.mods.isShiftDown()) sensitivity *= 0.05f;

 const auto& sl = ui2.slices[(size_t) liveSel];
  int delta = (int) ((e.x - dragStartY) * sensitivity); // horizontal drag

 if (cell.fieldId == F::FieldSliceStart)
 {
 const int liveSliceEnd = processor.sliceManager.getEndForSlice (liveSel, ui2.sampleNumFrames);
 int newStart = juce::jlimit (0, liveSliceEnd - 64,
 (int) dragStartValue + delta);
 processor.liveDragBoundsStart.store (newStart, std::memory_order_relaxed);
 processor.liveDragBoundsEnd.store (liveSliceEnd, std::memory_order_relaxed);
 }
 else
 {
 int newEnd = juce::jlimit (sl.startSample + 64, ui2.sampleNumFrames,
 (int) dragStartValue + delta);
 processor.liveDragBoundsStart.store (sl.startSample, std::memory_order_relaxed);
 processor.liveDragBoundsEnd.store (newEnd, std::memory_order_relaxed);
 }
 processor.liveDragSliceIdx.store (liveSel, std::memory_order_release);
 }
 repaint(); return;
 }

    // ── GLIDE: write directly to VoicePool’s legatoGlideMs atomic ──────────────
    if (cell.fieldId == kFieldGlide)
    {
        const float sensitivity = e.mods.isShiftDown() ? 0.2f : 1.0f; // ms/pixel
        float newMs = juce::jlimit (0.0f, 200.0f, dragStartValue - deltaY * sensitivity);
        processor.voicePool.legatoGlideMs.store (newMs, std::memory_order_relaxed);
        repaint(); return;
    }

    // ROOT NOTE: send via command FIFO -- chromatic reference MIDI note
    if (cell.fieldId == kFieldRootNote)
    {
        const float sensitivity = e.mods.isShiftDown() ? 0.1f : 0.4f; // notes/pixel
        const int newRoot = juce::jlimit (0, 127,
            (int) std::round (dragStartValue + deltaY * sensitivity));
        DysektProcessor::Command cmd;
        cmd.type      = DysektProcessor::CmdSetRootNote;
        cmd.intParam1 = newRoot;
        processor.pushCommand (cmd);
        repaint(); return;
    }

    // ── Guard: do not modify a field that has its individual lock bit set ──
    {
        const auto& snap = processor.getUiSliceSnapshot();
        const int sIdx = snap.selectedSlice;
        if (sIdx >= 0 && sIdx < snap.numSlices && cell.lockBit != 0)
        {
            if (snap.slices[(size_t) sIdx].lockMask & cell.lockBit)
            {
                repaint();
                return;
            }
        }
    }

 // ── All other knobs: CmdSetSliceParam ─────────────────────────────────
 bool isAdsr = (cell.fieldId == F::FieldAttack
 || cell.fieldId == F::FieldHold
 || cell.fieldId == F::FieldDecay
 || cell.fieldId == F::FieldSustain
 || cell.fieldId == F::FieldRelease);
 bool isBpm = (cell.fieldId == F::FieldBpm);

 float newNative;
 if (isAdsr || isBpm)
 {
 // Sensitivity in display units per pixel:
 // Attack: 2 ms/px (range 0-1000ms → 500px full sweep)
 // Decay: 10 ms/px (range 0-5000ms → 500px full sweep)
 // Release: 10 ms/px
 // Sustain: 0.5 %/px (range 0-100% → 200px full sweep)
 // BPM: 2 bpm/px
 // Shift = fine mode (÷10)
 float sensitivity = 1.0f;
 if (cell.fieldId == F::FieldAttack) sensitivity = 2.0f;
 else if (cell.fieldId == F::FieldHold)  sensitivity = 10.0f;
 else if (cell.fieldId == F::FieldDecay) sensitivity = 10.0f;
 else if (cell.fieldId == F::FieldRelease) sensitivity = 10.0f;
 else if (cell.fieldId == F::FieldSustain) sensitivity = 0.5f;
 else if (cell.fieldId == F::FieldBpm) sensitivity = 2.0f;
 if (e.mods.isShiftDown()) sensitivity *= 0.1f;

 float ds = dragStartValue, dmin = cell.minVal, dmax = cell.maxVal;
 // Convert to display units
 if (cell.fieldId == F::FieldAttack ||
 cell.fieldId == F::FieldHold ||
 cell.fieldId == F::FieldDecay ||
 cell.fieldId == F::FieldRelease)
 { ds *= 1000.f; dmin *= 1000.f; dmax *= 1000.f; }
 else if (cell.fieldId == F::FieldSustain)
 { ds *= 100.f; dmin *= 100.f; dmax *= 100.f; }

 // Release knob is inverted (right=less release): drag-up must decrease value.
 const float relSign = (cell.fieldId == F::FieldRelease) ? -1.0f : 1.0f;
 float dv = juce::jlimit (dmin, dmax, ds + relSign * deltaY * sensitivity);

 if (cell.fieldId == F::FieldAttack ||
 cell.fieldId == F::FieldHold ||
 cell.fieldId == F::FieldDecay ||
 cell.fieldId == F::FieldRelease)
 newNative = dv / 1000.f;
 else if (cell.fieldId == F::FieldSustain)
 newNative = dv / 100.f;
 else
 newNative = dv;
 }
 else
 {
 // Pan slider responds to horizontal drag — all other params use vertical
 const float delta = (cell.fieldId == F::FieldPan)
 ? (float)(e.x - dragStartY) // dragStartY stores startX for pan
 : deltaY;
 const float sensitivity = (cell.fieldId == F::FieldPan)
 ? (e.mods.isShiftDown() ? 0.002f : 0.01f) // 100px = full L→R sweep, shift=fine
 : 1.0f;
 if (cell.fieldId == F::FieldPan)
 newNative = juce::jlimit (-1.f, 1.f, dragStartValue + delta * sensitivity);
 else
 newNative = UIHelpers::computeDragValue (dragStartValue, delta,
 cell.minVal, cell.maxVal,
 e.mods.isShiftDown());
 }

    // ADSR knobs always write per-slice. Lock = protection only (drag is
    // already blocked by the lockBit guard above when the node is locked).
    if (isAdsr && cell.fieldId != F::FieldHold)
    {
        DysektProcessor::Command cmd;
        cmd.type        = F::CmdSetSliceParam;
        cmd.intParam1   = cell.fieldId;
        cmd.floatParam1 = newNative;
        cmd.intParam2   = 1; // skipLock — write value without modifying lockMask
        processor.pushCommand (cmd); repaint(); return;
    }


 DysektProcessor::Command cmd;
 cmd.type = F::CmdSetSliceParam;
 cmd.intParam1 = cell.fieldId; cmd.floatParam1 = newNative;
 cmd.intParam2 = 0;
 processor.pushCommand (cmd); repaint();
}

// =============================================================================
// mouseUp — commit slice bounds and deactivate live drag
// =============================================================================
void SliceControlBar::mouseUp (const juce::MouseEvent& /*e*/)
{
 using F = DysektProcessor;

 if (activeDragCell >= 0)
 {
 // Use the snapshot — cells[] may have been rebuilt since mouseDown.
 const auto& cell = activeCellSnapshot;
 if (cell.fieldId == F::FieldSliceStart)
 {
 const int liveIdx = processor.liveDragSliceIdx.load (std::memory_order_acquire);
 if (liveIdx >= 0)
 {
 const int commitStart = processor.liveDragBoundsStart.load (std::memory_order_relaxed);
 const int commitEnd   = processor.liveDragBoundsEnd.load (std::memory_order_relaxed);
 F::Command cmd;
 cmd.type = F::CmdSetSliceBounds;
 cmd.intParam1 = liveIdx;
 cmd.intParam2 = commitStart;
 cmd.positions[0] = commitEnd;
 cmd.numPositions = 1;
 cmd.isCommit = true;   // final commit — triggers crush name/note inheritance
 processor.pushCommand (cmd);
 // Bridge the gap between command commit and UI snapshot update so
 // WaveformView shows the correct position immediately on mouse up.
 processor.pendingUiOptimisticIdx.store (liveIdx, std::memory_order_relaxed);
 processor.pendingUiOptimisticSample.store (commitStart, std::memory_order_relaxed);
 }
 }
 }

 // Deactivate live drag (mirrors WaveformView::mouseUp)
 processor.liveDragSliceIdx.store (-1, std::memory_order_release);
 activeDragCell = -1;
}

// =============================================================================
// mouseDoubleClick
// =============================================================================
void SliceControlBar::mouseDoubleClick (const juce::MouseEvent& e)
{
 auto pos = e.getPosition();
 const auto& ui = processor.getUiSliceSnapshot();

 for (int i = 0; i < (int) cells.size(); ++i)
 {
 const auto& cell = cells[(size_t) i];
 if (! juce::Rectangle (cell.x, cell.y, cell.w, cell.h).contains (pos)) continue;
 if (cell.isMidiLearnBtn || cell.isBoolean || cell.isChoice || cell.isReadOnly) return;
 if (! cell.isKnob) return;

 float currentVal = 0.f;
 int sIdx = ui.selectedSlice;
 if (sIdx >= 0 && sIdx < ui.numSlices)
 {
 const auto& sl = ui.slices[(size_t) sIdx];
 using F = DysektProcessor;
 // Values in display units (ms, %, raw)
 switch (cell.fieldId)
 {
 case F::FieldBpm: currentVal = sl.bpm; break;
 case F::FieldPitch: currentVal = sl.pitchSemitones; break;
 case F::FieldCentsDetune: currentVal = sl.centsDetune; break;
 case F::FieldTonality: currentVal = sl.tonalityHz; break;
 case F::FieldFormant: currentVal = sl.formantSemitones; break;
 case F::FieldAttack:  currentVal = sl.attackSec   * 1000.f; break;
 case F::FieldHold:    currentVal = sl.holdSec     * 1000.f; break;
 case F::FieldDecay:   currentVal = sl.decaySec    * 1000.f; break;
 case F::FieldSustain: currentVal = sl.sustainLevel * 100.f; break;
 case F::FieldRelease: currentVal = sl.releaseSec  * 1000.f; break;
 case F::FieldMuteGroup: currentVal = (float)((sl.lockMask & kLockMuteGroup) ? sl.muteGroup : (int) processor.apvts.getRawParameterValue (ParamIds::defaultMuteGroup)->load()); break;
 case F::FieldMidiNote: currentVal = (float) sl.midiNote; break;
 case F::FieldVolume: currentVal = sl.volume; break;
 case F::FieldOutputBus: currentVal = (float)((sl.lockMask & kLockOutputBus) ? sl.outputBus : 0); break;
 case F::FieldPan: currentVal = sl.pan; break;
        case F::FieldEqLowGain:  currentVal = sl.eqLowGain;  break;
        case F::FieldEqMidGain:  currentVal = sl.eqMidGain;  break;
        case F::FieldEqMidFreq:  currentVal = sl.eqMidFreq;  break;
        case F::FieldEqMidQ:     currentVal = sl.eqMidQ;     break;
        case F::FieldEqHighGain: currentVal = sl.eqHighGain; break;
 default: break;
 }
 }
 // ROOT NOTE special case -- commits via CmdSetRootNote, not CmdSetSliceParam
 if (cell.fieldId == kFieldRootNote)
 {
     const int liveRoot = processor.sliceManager.rootNote.load (std::memory_order_relaxed);
     textEditor = std::make_unique<juce::TextEditor>();
     addAndMakeVisible (*textEditor);
     textEditor->setBounds (cell.x + kParamCellTextX, cell.y + 14,
                            cell.w - kParamCellTextX - 2, 16);
     textEditor->setFont (DysektLookAndFeel::makeFont (14.0f));
     textEditor->setColour (juce::TextEditor::backgroundColourId, getTheme().darkBar.brighter (0.15f));
     textEditor->setColour (juce::TextEditor::textColourId,    getTheme().foreground);
     textEditor->setColour (juce::TextEditor::outlineColourId, getTheme().accent);
     textEditor->setText (juce::String (liveRoot), false);
     textEditor->selectAll(); textEditor->grabKeyboardFocus();
     textEditor->onReturnKey = [this] {
         if (! textEditor) return;
         const int val = juce::jlimit (0, 127, textEditor->getText().getIntValue());
         DysektProcessor::Command cmd;
         cmd.type      = DysektProcessor::CmdSetRootNote;
         cmd.intParam1 = val;
         processor.pushCommand (cmd);
         textEditor.reset(); repaint();
     };
     textEditor->onEscapeKey = [this] { textEditor.reset(); repaint(); };
     textEditor->onFocusLost = [this] { textEditor.reset(); repaint(); };
     return;
 }

 showTextEditor (cell, currentVal); return;
 }
}

// =============================================================================
// drawChroBadgeCell — chromatic channel badge (0=off, 1-16)
// Clicking cycles 0→1→2→...→16→0. Right-click locks.
// =============================================================================
void SliceControlBar::drawChroBadgeCell (juce::Graphics& g, int x, int y,
                                         int channel, bool locked, int& outWidth)
{
    const int cellW = psCellW;
    const int cellH = psCellH;
    const auto& theme = getTheme();
    const bool active = (channel > 0);

    // Label
    g.setFont (DysektLookAndFeel::makeFont (10.0f * paintSf));
    g.setColour (locked ? theme.lockActive.withAlpha (0.8f)
                        : theme.foreground.withAlpha (0.42f));
    g.drawText ("CHRO", x + juce::roundToInt (4.0f * paintSf), y + juce::roundToInt (2.0f * paintSf), cellW - juce::roundToInt (4.0f * paintSf), juce::roundToInt (12.0f * paintSf), juce::Justification::centredLeft);

    // Badge
    const int bx = x + juce::roundToInt (4.0f * paintSf), by = y + juce::roundToInt (14.0f * paintSf), bw = cellW - juce::roundToInt (8.0f * paintSf), bh = juce::roundToInt (14.0f * paintSf);
    g.setColour (active ? (locked ? theme.lockActive.withAlpha (0.15f)
                                  : theme.accent.withAlpha (0.15f))
                        : theme.separator.withAlpha (0.25f));
    g.fillRoundedRectangle ((float)bx, (float)by, (float)bw, (float)bh, 2.5f);
    g.setColour (active ? (locked ? theme.lockActive : theme.accent)
                        : (locked ? theme.lockActive.withAlpha (0.5f)
                                  : theme.foreground.withAlpha (0.22f)));
    g.drawRoundedRectangle ((float)bx, (float)by, (float)bw, (float)bh, 2.5f, 0.8f);

    g.setFont (DysektLookAndFeel::makeMonoFont (11.0f * paintSf));
    g.setColour (active ? (locked ? theme.lockActive : theme.accent)
                        : (locked ? theme.lockActive.withAlpha (0.5f)
                                  : theme.foreground.withAlpha (0.28f)));
    g.drawText (active ? ("CH" + juce::String (channel)) : "OFF",
                bx, by, bw, bh, juce::Justification::centred);

    outWidth = cellW;
    ParamCell c {};
    c.x = x; c.y = y; c.w = cellW; c.h = cellH;
    c.lockBit = kLockChromaticChannel;
    c.fieldId = DysektProcessor::FieldChromaticChannel;
    c.minVal = 0.f; c.maxVal = 16.f; c.step = 1.f;
    c.isChoice = true;   // click cycles values
    cells.push_back (c);
}

// =============================================================================
// drawLegatoToggleCell — chromatic legato on/off toggle
// =============================================================================
void SliceControlBar::drawLegatoToggleCell (juce::Graphics& g, int x, int y,
                                            bool on, bool locked, int& outWidth)
{
    const int cellW = psCellW;
    const int cellH = psCellH;
    const auto& theme = getTheme();

    // Label
    g.setFont (DysektLookAndFeel::makeFont (10.0f * paintSf));
    g.setColour (locked ? theme.lockActive.withAlpha (0.8f)
                        : theme.foreground.withAlpha (0.42f));
    g.drawText ("LGTO", x + juce::roundToInt (4.0f * paintSf), y + juce::roundToInt (2.0f * paintSf), cellW - juce::roundToInt (4.0f * paintSf), juce::roundToInt (12.0f * paintSf), juce::Justification::centredLeft);

    // Toggle pill
    const int bx = x + juce::roundToInt (4.0f * paintSf), by = y + juce::roundToInt (14.0f * paintSf), bw = cellW - juce::roundToInt (8.0f * paintSf), bh = juce::roundToInt (14.0f * paintSf);
    const juce::Colour col = on ? (locked ? theme.lockActive : theme.accent)
                               : (locked ? theme.lockActive.withAlpha (0.4f)
                                         : theme.foreground.withAlpha (0.18f));
    g.setColour (col.withAlpha (on ? 0.15f : 0.08f));
    g.fillRoundedRectangle ((float)bx, (float)by, (float)bw, (float)bh, 2.5f);
    g.setColour (col);
    g.drawRoundedRectangle ((float)bx, (float)by, (float)bw, (float)bh, 2.5f, 0.8f);

    g.setFont (DysektLookAndFeel::makeMonoFont (11.0f * paintSf));
    g.setColour (col);
    g.drawText (on ? "ON" : "OFF", bx, by, bw, bh, juce::Justification::centred);

    outWidth = cellW;
    ParamCell c {};
    c.x = x; c.y = y; c.w = cellW; c.h = cellH;
    c.lockBit = kLockChromaticLegato;
    c.fieldId = DysektProcessor::FieldChromaticLegato;
    c.minVal = 0.f; c.maxVal = 1.f; c.step = 1.f;
    c.isBoolean = true;
    cells.push_back (c);
}

// =============================================================================
// mouseMove / mouseExit — hover highlight for knob cells
// =============================================================================
void SliceControlBar::mouseMove (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    int found = -1;
    for (int i = 0; i < (int) cells.size(); ++i)
    {
        const auto& c = cells[(size_t) i];
        if (pos.x >= c.x && pos.x < c.x + c.w &&
            pos.y >= c.y && pos.y < c.y + c.h)
        {
            found = i;
            break;
        }
    }
    if (found != hoveredCellIdx)
    {
        hoveredCellIdx = found;
        repaint();
    }
}

void SliceControlBar::mouseExit (const juce::MouseEvent&)
{
    if (hoveredCellIdx != -1)
    {
        hoveredCellIdx = -1;
        repaint();
    }
}

// =============================================================================
// showTextEditor (unchanged logic)
// =============================================================================
void SliceControlBar::showTextEditor (const ParamCell& cell, float currentValue)
{
 textEditor = std::make_unique<juce::TextEditor>();
 addAndMakeVisible (*textEditor);
 textEditor->setBounds (cell.x + kParamCellTextX, cell.y + 14,
 cell.w - kParamCellTextX - 2, 16);
 textEditor->setFont (DysektLookAndFeel::makeFont (14.0f));
 textEditor->setColour (juce::TextEditor::backgroundColourId, getTheme().darkBar.brighter (0.15f));
 textEditor->setColour (juce::TextEditor::textColourId, getTheme().foreground);
 textEditor->setColour (juce::TextEditor::outlineColourId, getTheme().accent);

 using F = DysektProcessor;
 juce::String displayVal;
 if (cell.fieldId == F::FieldAttack || cell.fieldId == F::FieldHold || cell.fieldId == F::FieldDecay || cell.fieldId == F::FieldRelease)
 displayVal = juce::String ((int) currentValue);
 else if (cell.fieldId == F::FieldSustain)
 displayVal = juce::String ((int) currentValue);
 else if (cell.fieldId == F::FieldVolume || cell.fieldId == F::FieldBpm)
 displayVal = juce::String (currentValue, 2);
 else if (cell.step >= 1.f)
 displayVal = juce::String ((int) currentValue);
 else
 displayVal = juce::String (currentValue, 1);

 textEditor->setText (displayVal, false);
 textEditor->selectAll(); textEditor->grabKeyboardFocus();

 int fieldId = cell.fieldId;
 float minV = cell.minVal, maxV = cell.maxVal;

 textEditor->onReturnKey = [this, fieldId, minV, maxV] {
 if (! textEditor) return;
 float val = textEditor->getText().getFloatValue();
 using F2 = DysektProcessor;
 const bool isAdsrField = (fieldId == F2::FieldAttack || fieldId == F2::FieldDecay
                           || fieldId == F2::FieldSustain || fieldId == F2::FieldRelease);
 if (fieldId == F2::FieldAttack || fieldId == F2::FieldDecay || fieldId == F2::FieldRelease)
 val /= 1000.f;
 else if (fieldId == F2::FieldSustain)
 val /= 100.f;
 val = juce::jlimit (minV, maxV, val);
 // Check the current lock state from the snapshot
 const auto& snap2 = processor.getUiSliceSnapshot();
 const int sIdx2 = snap2.selectedSlice;
 const bool fieldLocked = (sIdx2 >= 0 && sIdx2 < snap2.numSlices)
     && (snap2.slices[(size_t) sIdx2].lockMask & activeCellSnapshot.lockBit) != 0;
 if (isAdsrField && ! fieldLocked)
 {
     juce::String paramId;
     float apvtsNative = val;
     if      (fieldId == F2::FieldAttack)  { paramId = ParamIds::defaultAttack;  apvtsNative *= 1000.f; }
     else if (fieldId == F2::FieldDecay)   { paramId = ParamIds::defaultDecay;   apvtsNative *= 1000.f; }
     else if (fieldId == F2::FieldSustain) { paramId = ParamIds::defaultSustain; apvtsNative *= 100.f;  }
     else if (fieldId == F2::FieldRelease) { paramId = ParamIds::defaultRelease; apvtsNative *= 1000.f; }
     if (! paramId.isEmpty())
         if (auto* p = processor.apvts.getParameter (paramId))
             p->setValueNotifyingHost (p->convertTo0to1 (apvtsNative));
     textEditor.reset(); repaint(); return;
 }
 DysektProcessor::Command cmd;
 cmd.type = DysektProcessor::CmdSetSliceParam;
 cmd.intParam1 = fieldId; cmd.floatParam1 = val;
 cmd.intParam2 = 0;
 processor.pushCommand (cmd); textEditor.reset(); repaint();
 };
 textEditor->onEscapeKey = [this] { textEditor.reset(); repaint(); };
 textEditor->onFocusLost = [this] { textEditor.reset(); repaint(); };
}

// =============================================================================
// showSetBpmPopup removed — SET BPM functionality accessible via BPM knob