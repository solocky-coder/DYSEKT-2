#include "SliceWaveformLcd.h"
#include "DysektLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../params/ParamIds.h"
#include "../audio/Slice.h"

// ── Theme-derived colours ─────────────────────────────────────────────────────
static juce::Colour lcd2Bg() { return getTheme().darkBar.darker (0.55f); }
static juce::Colour lcd2Phosphor() { return getTheme().accent; }
static juce::Colour lcd2Dim() { return getTheme().accent.withAlpha (0.15f).overlaidWith (lcd2Bg()); }
static juce::Colour lcd2Bright() { return getTheme().accent.brighter (0.45f); }

const juce::Colour SliceWaveformLcd::kBg { 0xFF050F0E };
const juce::Colour SliceWaveformLcd::kBezel { 0xFF0D1E1C };
const juce::Colour SliceWaveformLcd::kPhosphor { 0xFF2AFFD0 };
const juce::Colour SliceWaveformLcd::kDim { 0xFF0A2A22 };
const juce::Colour SliceWaveformLcd::kBright { 0xFF8AFFF0 };
const juce::Colour SliceWaveformLcd::kLabel { 0xFF1A7060 };

// Toxic Candy node colours (match ThemeData palette)
static const juce::Colour kColAttack { 0xFF00FF87 }; // Toxic Lime
static const juce::Colour kColDecay { 0xFFFFE800 }; // Radioactive Yellow
static const juce::Colour kColSustain { 0xFF00C8FF }; // Ice Blue
static const juce::Colour kColRelease { 0xFFFF6B00 }; // Molten Orange

// ── Constructor ───────────────────────────────────────────────────────────────

SliceWaveformLcd::SliceWaveformLcd (DysektProcessor& p)
 : processor (p)
{
 setOpaque (false); // rounded corners — must not claim full opaque coverage
 setMouseCursor (juce::MouseCursor::NormalCursor);
}

void SliceWaveformLcd::resized()
{
 screenArea = getLocalBounds().reduced (4).toFloat();
}

void SliceWaveformLcd::repaintLcd()
{
 if (dragRole == NodeRole::None)
 {
 if (postCommitGuard > 0)
 {
 --postCommitGuard;
 }
 else
 {
 const int ver = processor.getUiSliceSnapshotVersion();

 // Also detect changes to the global APVTS ADSR knobs — turning a knob
 // does not dirty the slice snapshot (it only updates the APVTS param),
 // so we must check the raw param values directly here.
 if (ver != lastEnvSnapVer)
 {
     buildEnvelopeNodes();
     lastEnvSnapVer = ver;
 }
 }
 }
 repaint();
}

// ── Data building ─────────────────────────────────────────────────────────────

void SliceWaveformLcd::buildDisplayData()
{
 data = {};

 const auto& snap = processor.getUiSliceSnapshot();
 data.hasSample = snap.sampleLoaded && ! snap.sampleMissing;
 data.numSlices = snap.numSlices;
 data.sampleName = snap.isDefaultSample ? juce::String() : snap.sampleFileName;
 data.isDefault = snap.isDefaultSample;
 data.totalFrames = snap.sampleNumFrames;
 data.sampleRate = processor.getSampleRate() > 0.0
 ? processor.getSampleRate() : 44100.0;

 if (! data.hasSample || snap.selectedSlice < 0 || snap.selectedSlice >= snap.numSlices)
 return;

 data.hasSlice = true;
 data.sliceIndex = snap.selectedSlice;

 const auto& sl = snap.slices[(size_t) snap.selectedSlice];
 data.startSample = sl.startSample;
 data.endSample = processor.sliceManager.getEndForSlice (
 snap.selectedSlice, data.totalFrames);
 data.midiNote = sl.midiNote;
 data.volume = sl.volume;
 data.pan = sl.pan;
 data.pitchSemitones = sl.pitchSemitones;
 const int kPeaks = 256;
 data.peaks.clearQuick();
 data.peaks.insertMultiple (-1, 0.0f, kPeaks);

 const int sliceLen = data.endSample - data.startSample;
 if (sliceLen <= 0) return;

 for (int i = 0; i < kPeaks; i++)
 {
 const float t = (float) i / (float) kPeaks;
 const int pos = data.startSample + (int) (t * (float) sliceLen);
 data.peaks.set (i, processor.getWaveformPeakAt (pos));
 }
}

// ── Envelope: read params → normalised nodes ──────────────────────────────────

// ── getSliceDurMs ─────────────────────────────────────────────────────────────
// Returns the duration (in ms) of the currently selected slice.
// Used by buildEnvelopeNodes / commitNodes to scale ADSR positions relative
// to actual slice length so dragging is intuitive regardless of slice size.
float SliceWaveformLcd::getSliceDurMs() const
{
 static constexpr float kDefaultMs = 1000.0f; // fallback if no slice loaded

 const int sel = processor.sliceManager.selectedSlice.load (std::memory_order_relaxed);
 if (sel < 0 || sel >= processor.sliceManager.getNumSlices())
 return kDefaultMs;

 const auto& s = processor.sliceManager.getSlice (sel);
 const int total = processor.sampleData.getNumFrames();
 if (total <= 0)
 return kDefaultMs;

 const int sliceEnd = processor.sliceManager.getEndForSlice (sel, total);
 const int len = sliceEnd - s.startSample;
 if (len <= 0)
 return kDefaultMs;

 const float sr = (float) processor.voicePool.getSampleRate();
 return (float) len / sr * 1000.0f;
}

void SliceWaveformLcd::buildEnvelopeNodes()
{
 // Read effective ADSR values — same resolve logic as SliceControlBar:
 // if the field is locked, use the slice's own stored value;
 // otherwise use the global APVTS knob value.  This ensures the nodes
 // track the ADSR knobs even when the field is not locked.
 float attackMs  = 10.0f;
 float decayMs   = 100.0f;
 float sustainPc = 100.0f;
 float releaseMs = 100.0f;

 auto apvtsMs  = [&] (const juce::String& id) -> float {
     auto* p = processor.apvts.getRawParameterValue (id);
     return p ? p->load() : 0.0f;
 };
 auto apvtsPct = [&] (const juce::String& id) -> float {
     auto* p = processor.apvts.getRawParameterValue (id);
     return p ? p->load() : 100.0f;
 };

 const int sel = processor.sliceManager.selectedSlice.load (std::memory_order_relaxed);
 if (sel >= 0 && sel < processor.sliceManager.getNumSlices())
 {
     const auto& s = processor.sliceManager.getSlice (sel);
     attackMs  = s.attackSec    * 1000.0f;
     decayMs   = s.decaySec     * 1000.0f;
     sustainPc = s.sustainLevel * 100.0f;
     releaseMs = s.releaseSec   * 1000.0f;
 }

    // Free layout — A, D, R travel the full [0..1] width with only a small
    // minimum separation (kGap) enforced between adjacent nodes.
    // A:  0=no attack (node at left edge),  1=full-length attack (node at right).
    // R:  0=no release (node at right edge), 1=full-length release (node at left).
    // D:  mapped proportionally into the span [ax+kGap .. rx-kGap].
    // Sustain plateau = [dx .. rx]; sxEnd == rx (no separate constant needed).
    // Must match commitNodes and mouseDrag exactly.
    static constexpr float kMin = 0.01f, kMax = 0.99f;
    static constexpr float kGap = 0.01f;

    const float sliceDurMs_  = juce::jmax (1.0f, getSliceDurMs());
    const float kViewMs      = sliceDurMs_;

    const float attackNorm  = std::sqrt (juce::jmin (attackMs  / kViewMs, 1.0f));
    const float decayNorm   = std::sqrt (juce::jmin (decayMs   / kViewMs, 1.0f));
    const float releaseNorm = std::sqrt (juce::jmin (releaseMs / kViewMs, 1.0f));

    // Place A and R first, then fit D in the remaining span.
    const float ax_raw = kMin + attackNorm  * (kMax - kMin);
    const float rx_raw = kMin + releaseNorm * (kMax - kMin);

    env.ax = juce::jlimit (kMin, kMax - 2.0f * kGap, ax_raw);
    env.rx = juce::jlimit (env.ax + 2.0f * kGap, kMax, rx_raw);

    const float dSpan = env.rx - env.ax - 2.0f * kGap;
    env.dx = juce::jlimit (env.ax + kGap,
                           env.rx - kGap,
                           env.ax + kGap + decayNorm * dSpan);

    env.sy    = juce::jlimit (0.04f, 0.94f, 1.0f - (sustainPc / 100.0f));
    env.ay    = 0.04f;
    env.sxEnd = env.rx;

 // Rebuild node list
 envNodes.clear();

 EnvNode a; a.xn = env.ax; a.yn = env.ay; a.role = NodeRole::Attack;
 a.colour = kColAttack; a.label = "A"; envNodes.add (a);

 // Decay node sits on the sustain line at decay end (standard ADSR visual).
 EnvNode d; d.xn = env.dx; d.yn = env.sy; d.role = NodeRole::Decay;
 d.colour = kColDecay; d.label = "D"; envNodes.add (d);

 // Sustain handle: mid of plateau [dx .. sxEnd]
 EnvNode s;
 s.xn = (env.dx + env.sxEnd) * 0.5f; s.yn = env.sy; s.role = NodeRole::Sustain;
 s.colour = kColSustain; s.label = "S"; envNodes.add (s);

 // Release node: fade-out start, on sustain level
 EnvNode r; r.xn = env.rx; r.yn = env.sy; r.role = NodeRole::Release;
 r.colour = kColRelease; r.label = "R"; envNodes.add (r);
}

// Write normalised nodes back to ADSR params ──────────────────────────────────

void SliceWaveformLcd::commitNodes()
{
    // Inverse-map — must match buildEnvelopeNodes exactly.
    static constexpr float kMin = 0.01f, kMax = 0.99f;
    static constexpr float kGap = 0.01f;

    const float kViewMs = juce::jmax (1.0f, getSliceDurMs());

    // A: position within full [kMin..kMax] span
    const float aRatio = (env.ax - kMin) / juce::jmax (0.001f, kMax - kMin);
    // R: position within full [kMin..kMax] span (same direction as A — right = longer)
    const float rRatio = (env.rx - kMin) / juce::jmax (0.001f, kMax - kMin);
    // D: position within [ax+kGap .. rx-kGap] span
    const float dSpan  = env.rx - env.ax - 2.0f * kGap;
    const float dRatio = (env.dx - (env.ax + kGap)) / juce::jmax (0.001f, dSpan);

    const float attackMs  = juce::jlimit (0.0f, kViewMs, aRatio * aRatio * kViewMs);
    const float decayMs   = juce::jlimit (0.0f, kViewMs, dRatio * dRatio * kViewMs);
    const float sustainPc = juce::jlimit (0.0f, 100.0f, (1.0f - env.sy) * 100.0f);
    const float releaseMs = juce::jlimit (0.0f, kViewMs, rRatio * rRatio * kViewMs);

    // Read the lock state for the selected slice so we can decide whether to
    // write per-slice or global APVTS — mirroring SliceControlBar::mouseDrag
    // exactly.  Unlocked ADSR fields only update the global APVTS default
    // (affecting every unlocked slice); locked fields write only to the
    // slice's own storage (skipLock = 1, lockMask unchanged).
    uint32_t sliceLockMask = 0;
    {
        const int sel = processor.sliceManager.selectedSlice.load (std::memory_order_relaxed);
        if (sel >= 0 && sel < processor.sliceManager.getNumSlices())
            sliceLockMask = processor.sliceManager.getSlice (sel).lockMask;
    }

    // Write dragged value to per-slice storage without modifying the lock bit.
    // intParam2 = 1 means skipLock — value stored in s.attackSec etc.,
    // lockMask is NOT modified.
    auto writePerSlice = [&] (int fieldId, float nativeVal)
    {
        DysektProcessor::Command cmd;
        cmd.type        = DysektProcessor::CmdSetSliceParam;
        cmd.intParam1   = fieldId;
        cmd.floatParam1 = nativeVal;
        cmd.intParam2   = 1; // skipLock
        processor.pushCommand (cmd);
    };

    // Write to the global APVTS param so the SCB knob reflects the drag.
    auto writeApvts = [&] (const juce::String& paramId, float nativeVal)
    {
        if (auto* p = processor.apvts.getParameter (paramId))
            p->setValueNotifyingHost (p->convertTo0to1 (nativeVal));
    };

    // Always write per-slice. Lock = protection only (drag blocked upstream).
    switch (dragRole)
    {
        case NodeRole::Attack:  writePerSlice (DysektProcessor::FieldAttack,  attackMs  / 1000.f); break;
        case NodeRole::Decay:   writePerSlice (DysektProcessor::FieldDecay,   decayMs   / 1000.f); break;
        case NodeRole::Sustain: writePerSlice (DysektProcessor::FieldSustain, sustainPc / 100.f);  break;
        case NodeRole::Release: writePerSlice (DysektProcessor::FieldRelease, releaseMs / 1000.f); break;
        default: break;
    }

 // Give the processor time to echo the new values before rebuilding
 postCommitGuard = 6;
 lastEnvSnapVer = -1; // force rebuild once guard expires
}

// Envelope Y at normalised X (linear interpolation between nodes) ─────────────

float SliceWaveformLcd::envAt (float xn) const
{
 // Polyline: P0(0,1) → P1(ax,top) → P2(dx,sy) → P3(kSEnd,sy) → P4(rx,sy) → P5(1,1)
 // kSEnd is dynamic — stored in env.sxEnd by buildEnvNodes
 const float kSEnd = env.sxEnd;
 struct Pt { float x, y; };
 const Pt pts[] = {
 { 0.0f,    1.0f    },
 { env.ax,  env.ay  }, // attack peak (env.ay = 0.04, near top)
 { env.dx,  env.sy  }, // end of decay / sustain level
 { kSEnd,   env.sy  }, // dynamic end of sustain plateau
 { env.rx,  env.sy  }, // release start
 { 1.0f,    1.0f    }  // end of release (silence)
 };
 constexpr int N = 6;

 for (int i = 0; i < N - 1; ++i)
 {
 if (xn >= pts[i].x && xn <= pts[i+1].x)
 {
 const float span = pts[i+1].x - pts[i].x;
 const float t = (span > 0.0f) ? (xn - pts[i].x) / span : 0.0f;
 return pts[i].y + t * (pts[i+1].y - pts[i].y);
 }
 }
 return 1.0f;
}

// ── Hit testing ───────────────────────────────────────────────────────────────

SliceWaveformLcd::NodeRole SliceWaveformLcd::hitTest (juce::Point<float> pos) const
{
 if (screenArea.isEmpty()) return NodeRole::None;

 const float W = screenArea.getWidth();
 const float H = screenArea.getHeight();
 const float ox = screenArea.getX();
 const float oy = screenArea.getY();

 NodeRole best = NodeRole::None;
 float bestD2 = kHitR * kHitR;

 for (const auto& n : envNodes)
 {
 const float nx = ox + n.xn * W;
 const float ny = oy + n.yn * H;
 const float dx = pos.x - nx;
 const float dy = pos.y - ny;
 const float d2 = dx*dx + dy*dy;
 if (d2 < bestD2) { bestD2 = d2; best = n.role; }
 }
 return best;
}

// ── Mouse ─────────────────────────────────────────────────────────────────────

void SliceWaveformLcd::mouseMove (const juce::MouseEvent& e)
{
 const auto newHov = hitTest (e.position);
 if (newHov != hovRole)
 {
 hovRole = newHov;
 setMouseCursor (hovRole != NodeRole::None
 ? juce::MouseCursor::PointingHandCursor
 : juce::MouseCursor::NormalCursor);
 repaint();
 }
}

void SliceWaveformLcd::mouseDown (const juce::MouseEvent& e)
{
 const NodeRole hit = hitTest (e.position);

 if (e.mods.isRightButtonDown())
 {
 // Right-click on a node: toggle that ADSR field's lock for the selected slice
 uint32_t bit = 0;
 if      (hit == NodeRole::Attack)  bit = kLockAttack;
 else if (hit == NodeRole::Decay)   bit = kLockDecay;
 else if (hit == NodeRole::Sustain) bit = kLockSustain;
 else if (hit == NodeRole::Release) bit = kLockRelease;

 if (bit != 0)
 {
 const int sel = processor.sliceManager.selectedSlice.load (std::memory_order_relaxed);
 if (sel >= 0 && sel < processor.sliceManager.getNumSlices())
 {
 const auto& s = processor.sliceManager.getSlice (sel);
 const bool currentlyLocked = (s.lockMask & bit) != 0;

 if (currentlyLocked)
 {
     // ── UNLOCK: write slice's locked value back to APVTS first ───────────
     // buildEnvelopeNodes() reads from the slice directly, so the node
     // won't jump — but sync APVTS knob so SCB shows the right value.
     auto writeApvts = [&] (const juce::String& paramId, float nativeVal)
     {
         if (auto* p = processor.apvts.getParameter (paramId))
             p->setValueNotifyingHost (p->convertTo0to1 (nativeVal));
     };
     if      (bit == kLockAttack)  writeApvts (ParamIds::defaultAttack,  s.attackSec    * 1000.0f);
     else if (bit == kLockDecay)   writeApvts (ParamIds::defaultDecay,   s.decaySec     * 1000.0f);
     else if (bit == kLockSustain) writeApvts (ParamIds::defaultSustain, s.sustainLevel * 100.0f);
     else if (bit == kLockRelease) writeApvts (ParamIds::defaultRelease, s.releaseSec   * 1000.0f);

     DysektProcessor::Command cmd;
     cmd.type      = DysektProcessor::CmdToggleLock;
     cmd.intParam1 = sel;
     cmd.intParam2 = (int) bit;
     processor.pushCommand (cmd);
 }
 else
 {
     // ── LOCK: snapshot the slice's CURRENT value into per-slice storage,
     // then set the lock bit.  Read from the slice struct (which holds
     // whatever was last written by drag or the knob) — NOT from APVTS,
     // which could be at a different position and would cause a jump.
     float snapVal = 0.0f;
     DysektProcessor::SliceParamField field = DysektProcessor::FieldAttack;
     if      (bit == kLockAttack)  { snapVal = s.attackSec;    field = DysektProcessor::FieldAttack;  }
     else if (bit == kLockDecay)   { snapVal = s.decaySec;     field = DysektProcessor::FieldDecay;   }
     else if (bit == kLockSustain) { snapVal = s.sustainLevel; field = DysektProcessor::FieldSustain; }
     else if (bit == kLockRelease) { snapVal = s.releaseSec;   field = DysektProcessor::FieldRelease; }

     // Write the value (skipLock=1) so the slice record is current
     {
         DysektProcessor::Command c;
         c.type        = DysektProcessor::CmdSetSliceParam;
         c.intParam1   = (int) field;
         c.floatParam1 = snapVal;
         c.intParam2   = 1; // skipLock
         processor.pushCommand (c);
     }
     // Now toggle the lock bit on
     {
         DysektProcessor::Command cmd;
         cmd.type      = DysektProcessor::CmdToggleLock;
         cmd.intParam1 = sel;
         cmd.intParam2 = (int) bit;
         processor.pushCommand (cmd);
     }
 }
 }
 postCommitGuard = 6;
 lastEnvSnapVer = -1;
 repaint();
 }
 return; // don't start a drag on right-click
 }

 dragRole = hit;
}

void SliceWaveformLcd::mouseDrag (const juce::MouseEvent& e)
{
 if (dragRole == NodeRole::None || screenArea.isEmpty()) return;

 // ═══════════════════════════════════════════════════════════════════════════
 // BUG FIX: Block dragging locked ADSR nodes — check slice's lockMask
 // ═══════════════════════════════════════════════════════════════════════════
 {
     const int sel = processor.sliceManager.selectedSlice.load (std::memory_order_relaxed);
     if (sel >= 0 && sel < processor.sliceManager.getNumSlices())
     {
         const auto& s = processor.sliceManager.getSlice (sel);
         uint32_t bit = 0;
         if      (dragRole == NodeRole::Attack)  bit = kLockAttack;
         else if (dragRole == NodeRole::Decay)   bit = kLockDecay;
         else if (dragRole == NodeRole::Sustain) bit = kLockSustain;
         else if (dragRole == NodeRole::Release) bit = kLockRelease;

         if (bit != 0 && (s.lockMask & bit))
             return;  // node is locked — ignore drag
     }
 }

 const float W = screenArea.getWidth();
 const float H = screenArea.getHeight();
 const float ox = screenArea.getX();
 const float oy = screenArea.getY();

 const float xn  = juce::jlimit (0.01f, 0.99f, (e.position.x - ox) / W);
 const float yn  = juce::jlimit (0.02f, 0.98f, (e.position.y - oy) / H);

 // Free layout — must match buildEnvelopeNodes / commitNodes exactly.
    static constexpr float kMin = 0.01f, kMax = 0.99f;
    static constexpr float kGap = 0.01f;

 if (dragRole == NodeRole::Attack)
 {
     // A: X only — clamp to [kMin, dx-kGap] so it never crosses D
     env.ax    = juce::jlimit (kMin, env.dx - kGap, xn);
 }
 else if (dragRole == NodeRole::Decay)
 {
     // D: X only — stays between A and R
     env.dx    = juce::jlimit (env.ax + kGap, env.rx - kGap, xn);
 }
 else if (dragRole == NodeRole::Sustain)
 {
     // S: Y only — sustain level
     env.sy = juce::jlimit (0.04f, 0.94f, yn);
 }
 else if (dragRole == NodeRole::Release)
 {
     // R: X only — clamp to [dx+kGap, kMax] so it never crosses D
     env.rx    = juce::jlimit (env.dx + kGap, kMax, xn);
 }

 // Plateau end always tracks R
 env.sxEnd = env.rx;

 // Rebuild envNodes[] from updated env.* (no param read during drag)
 envNodes.clear();
 EnvNode a; a.xn = env.ax; a.yn = env.ay; a.role = NodeRole::Attack;
 a.colour = kColAttack; a.label = "A"; envNodes.add (a);
 EnvNode d; d.xn = env.dx; d.yn = env.sy; d.role = NodeRole::Decay;
 d.colour = kColDecay; d.label = "D"; envNodes.add (d);
    EnvNode s; s.xn = (env.dx + env.sxEnd) * 0.5f; s.yn = env.sy; s.role = NodeRole::Sustain;
 s.colour = kColSustain; s.label = "S"; envNodes.add (s);
 EnvNode r; r.xn = env.rx; r.yn = env.sy; r.role = NodeRole::Release;
 r.colour = kColRelease; r.label = "R"; envNodes.add (r);

 // Push to params — also updates APVTS knobs so display stays in sync
 commitNodes();

 repaint();
}

void SliceWaveformLcd::mouseUp (const juce::MouseEvent&)
{
 dragRole = NodeRole::None;
 // Don't rebuild immediately — processor hasn't echoed the new values yet.
 // The guard lets a few paints pass before we re-read from the snapshot.
 postCommitGuard = 6;
 repaint();
}

// ── Draw ──────────────────────────────────────────────────────────────────────

void SliceWaveformLcd::drawBackground (juce::Graphics& g)
{
 const auto ac = getTheme().accent;
 auto b = getLocalBounds();

 auto bgTop = getTheme().darkBar.darker (0.45f);
 auto bgBot = getTheme().darkBar.darker (0.65f);
 juce::ColourGradient outerGrad (bgTop, 0, 0, bgBot, 0, (float) b.getHeight(), false);
 g.setGradientFill (outerGrad);
 g.fillRoundedRectangle (b.toFloat(), 4.0f);
 g.setColour (ac.withAlpha (0.18f));
 g.drawRoundedRectangle (b.toFloat().expanded (1.0f), 5.0f, 1.0f);
 g.setColour (ac.withAlpha (0.60f));
 g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 4.0f, 1.5f);

 auto screen = b.reduced (4);
 g.setColour (lcd2Bg());
 g.fillRoundedRectangle (screen.toFloat(), 2.0f);

 juce::ColourGradient glow (lcd2Phosphor().withAlpha (0.07f), 0, (float) screen.getY(),
 juce::Colours::transparentBlack, 0, (float) (screen.getY() + 18), false);
 g.setGradientFill (glow);
 g.fillRoundedRectangle (screen.toFloat(), 2.0f);

 g.setColour (juce::Colour (0xFF000000).withAlpha ((uint8_t) kScanlineAlpha));
 for (int y = screen.getY(); y < screen.getBottom(); y += 2)
 g.drawHorizontalLine (y, (float) screen.getX(), (float) screen.getRight());

 g.setColour (ac.withAlpha (0.30f));
 g.drawRoundedRectangle (screen.toFloat().expanded (0.5f), 2.0f, 1.0f);
}

void SliceWaveformLcd::drawWaveform (juce::Graphics& g, const juce::Rectangle<float>& area)
{
 if (data.peaks.isEmpty()) return;

 const float cy = area.getCentreY();
 const float W = area.getWidth();
 const float H = area.getHeight();
 const int n = data.peaks.size();

 // Zero line
 g.setColour (lcd2Phosphor().withAlpha (0.20f));
 g.drawHorizontalLine (juce::roundToInt (cy), area.getX(), area.getRight());

 // Build waveform paths — amplitude modulated by envelope shape
 juce::Path fill, lineTop, lineBot;
 bool first = true;

 for (int i = 0; i < n; i++)
 {
 const float xn = (float) i / (float) n;
 const float x = area.getX() + xn * W;
 const float eGain = 1.0f - envAt (xn); // 0=silence 1=full
 const float amp = juce::jlimit (0.0f, 1.0f, data.peaks[i]) * (H * 0.45f) * eGain;

 const float yT = cy - amp;
 const float yB = cy + amp;

 if (first)
 {
 lineTop.startNewSubPath (x, yT);
 lineBot.startNewSubPath (x, yB);
 first = false;
 }
 else
 {
 lineTop.lineTo (x, yT);
 lineBot.lineTo (x, yB);
 }
 }

 fill = lineTop;
 for (int i = n - 1; i >= 0; i--)
 {
 const float xn = (float) i / (float) n;
 const float x = area.getX() + xn * W;
 const float eGain = 1.0f - envAt (xn);
 const float amp = juce::jlimit (0.0f, 1.0f, data.peaks[i]) * (H * 0.45f) * eGain;
 fill.lineTo (x, cy + amp);
 }
 fill.closeSubPath();

 // Use selected slice colour for waveform rendering
 juce::Colour sliceCol = lcd2Phosphor(); // default = theme accent
 {
 const int sel = processor.sliceManager.selectedSlice.load (std::memory_order_relaxed);
 if (sel >= 0 && sel < processor.sliceManager.getNumSlices())
 sliceCol = processor.sliceManager.getSlice (sel).colour;
 }

 g.setColour (sliceCol.withAlpha (0.12f));
 g.fillPath (fill);

 juce::PathStrokeType glow (2.5f);
 g.setColour (sliceCol.withAlpha (0.22f));
 g.strokePath (lineTop, glow);
 g.strokePath (lineBot, glow);

 juce::PathStrokeType sharp (1.1f);
 g.setColour (sliceCol.withAlpha (0.85f));
 g.strokePath (lineTop, sharp);
 g.strokePath (lineBot, sharp);


}

void SliceWaveformLcd::drawSegmentLabel (juce::Graphics& g,
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

void SliceWaveformLcd::drawEnvelope (juce::Graphics& g, const juce::Rectangle<float>& area)
{
 const float W = area.getWidth();
 const float H = area.getHeight();
 const float ox = area.getX();
 const float oy = area.getY();

 auto px = [&] (float xn) { return ox + xn * W; };
 auto py = [&] (float yn) { return oy + yn * H; };

 // ── Filled envelope region ────────────────────────────────────────────────
 juce::Path envFill;
 envFill.startNewSubPath (px (0.0f), py (1.0f));
 envFill.lineTo (px (env.ax),    py (env.ay));
 envFill.lineTo (px (env.dx),    py (env.sy));
 envFill.lineTo (px (env.sxEnd), py (env.sy));
 envFill.lineTo (px (env.rx),    py (env.sy));
 envFill.lineTo (px (1.0f),      py (1.0f));
 envFill.closeSubPath();

 juce::ColourGradient fillGrad (kColDecay.withAlpha (0.08f), 0, oy,
 kColDecay.withAlpha (0.00f), 0, oy + H, false);
 g.setGradientFill (fillGrad);
 g.fillPath (envFill);

 // ── Envelope polyline ─────────────────────────────────────────────────────
 juce::Path envLine;
 envLine.startNewSubPath (px (0.0f), py (1.0f));
 envLine.lineTo (px (env.ax),    py (env.ay));
 envLine.lineTo (px (env.dx),    py (env.sy));
 envLine.lineTo (px (env.sxEnd), py (env.sy));
 envLine.lineTo (px (env.rx),    py (env.sy));
 envLine.lineTo (px (1.0f),      py (1.0f));

 // Glow pass
 juce::PathStrokeType glowStroke (2.5f);
 g.setColour (juce::Colours::white.withAlpha (0.07f));
 g.strokePath (envLine, glowStroke);

 // Main line (dashed via path flattening)
 juce::Path dashedLine;
 {
 juce::PathStrokeType stroke (1.0f);
 float dashes[] = { 3.0f, 5.0f };
 stroke.createDashedStroke (dashedLine, envLine, dashes, 2);
 }
 g.setColour (juce::Colours::white.withAlpha (0.20f));
 g.fillPath (dashedLine);

 // Sustain plateau highlighted
 juce::Path susLine;
 susLine.startNewSubPath (px (env.dx), py (env.sy));
 susLine.lineTo (px (env.sxEnd), py (env.sy));
 g.setColour (kColSustain.withAlpha (0.35f));
 g.strokePath (susLine, juce::PathStrokeType (1.0f));

 // ── Segment labels ────────────────────────────────────────────────────────
 drawSegmentLabel (g, 0.0f, 1.0f, env.ax, env.ay, "FADE IN", kColAttack, area);
 drawSegmentLabel (g, env.ax, env.ay, env.dx, env.sy, "DECAY", kColDecay, area);
 drawSegmentLabel (g, env.rx, env.sy, 1.0f, 1.0f, "FADE OUT", kColRelease, area);
}

void SliceWaveformLcd::drawNodes (juce::Graphics& g, const juce::Rectangle<float>& area)
{
 const float W = area.getWidth();
 const float H = area.getHeight();
 const float ox = area.getX();
 const float oy = area.getY();

 // Read lock state for selected slice
 uint32_t lockMask = 0;
 const int sel = processor.sliceManager.selectedSlice.load (std::memory_order_relaxed);
 if (sel >= 0 && sel < processor.sliceManager.getNumSlices())
 lockMask = processor.sliceManager.getSlice (sel).lockMask;

 for (const auto& node : envNodes)
 {
 const float cx = ox + node.xn * W;
 const bool hov = (node.role == hovRole || node.role == dragRole);
 const float r = hov ? kNodeR + 2.5f : kNodeR;
 // Clamp cy so the circle never escapes the component bounds.
 // Use component-local coordinates (origin = component top-left) so the
 // floor/ceiling hold regardless of the nodeArea inset passed in.
 const float compH = (float) getHeight();
 const float cyRaw = oy + node.yn * H;
 const float cy = juce::jmax (r + 2.0f,
 juce::jmin (compH - r - 2.0f, cyRaw));

 // Determine if this field is locked
 uint32_t fieldBit = 0;
 if      (node.role == NodeRole::Attack)  fieldBit = kLockAttack;
 else if (node.role == NodeRole::Decay)   fieldBit = kLockDecay;
 else if (node.role == NodeRole::Sustain) fieldBit = kLockSustain;
 else if (node.role == NodeRole::Release) fieldBit = kLockRelease;
 const bool locked = (fieldBit != 0) && ((lockMask & fieldBit) != 0);

 // Tick line down to envelope (not for the Sustain mid-handle)
 if (node.role != NodeRole::Sustain)
 {
 g.setColour (node.colour.withAlpha (locked ? 0.30f : 0.18f));
 g.drawVerticalLine (juce::roundToInt (cx), cy + r, oy + H);
 }

 if (locked)
 {
 // ── LOCKED: solid filled ring + glow + padlock pip ────────────────
 // Outer glow
 g.setColour (node.colour.withAlpha (hov ? 0.40f : 0.20f));
 g.drawEllipse (cx - r - 3.0f, cy - r - 3.0f,
 (r + 3.0f) * 2.0f, (r + 3.0f) * 2.0f, 1.0f);

 // Filled ring
 g.setColour (node.colour.withAlpha (hov ? 0.90f : 0.70f));
 g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);

 // White centre dot
 g.setColour (juce::Colours::white.withAlpha (hov ? 0.95f : 0.75f));
 g.fillEllipse (cx - 1.8f, cy - 1.8f, 3.6f, 3.6f);

 // Padlock icon to the RIGHT of the letter label
 {
 const float labelY = cy + r + 2.0f;   // top of the label rect (same as drawText below)
 const float lx     = cx + 6.0f;       // right of the letter (letter right-edge ≈ cx + 4)
 const float ly     = labelY + 2.0f;   // vertically centred near label text
 // shackle arc
 juce::Path shackle;
 shackle.addCentredArc (lx + 3.0f, ly - 2.5f, 2.5f, 2.5f, 0.0f,
                        juce::MathConstants<float>::pi, 0.0f, true);
 g.setColour (node.colour.withAlpha (hov ? 1.0f : 0.85f));
 g.strokePath (shackle, juce::PathStrokeType (1.2f));
 // body rect
 g.setColour (node.colour.withAlpha (hov ? 0.70f : 0.50f));
 g.fillRoundedRectangle (lx, ly, 6.0f, 5.0f, 1.0f);
 g.setColour (node.colour.withAlpha (hov ? 1.0f : 0.80f));
 g.drawRoundedRectangle (lx, ly, 6.0f, 5.0f, 1.0f, 0.8f);
 }

 // Label BELOW node (always inside frame)
 g.setFont (DysektLookAndFeel::makeFont (hov ? 11.0f : 9.5f, true));
 g.setColour (node.colour.withAlpha (hov ? 1.0f : 0.85f));
 g.drawText (juce::String (node.label),
 juce::Rectangle<float> (cx - 14.0f, cy + r + 2.0f, 28.0f, 12.0f),
 juce::Justification::centred, false);

 // Hover tooltip below label
 if (hov)
 {
 g.setFont (DysektLookAndFeel::makeFont (8.5f));
 g.setColour (node.colour.withAlpha (0.75f));
 g.drawText ("right-click to unlock",
 juce::Rectangle<float> (cx - 48.0f, cy + r + 15.0f, 96.0f, 12.0f),
 juce::Justification::centred, false);
 }
 }
 else
 {
 // ── UNLOCKED: hollow ring ─────────────────────────────────────────
 g.setColour (node.colour.withAlpha (hov ? 0.55f : 0.25f));
 g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, hov ? 1.5f : 1.0f);

 // Inner dot
 const float dr = hov ? 3.0f : 2.5f;
 g.setColour (node.colour.withAlpha (hov ? 1.0f : 0.80f));
 g.fillEllipse (cx - dr, cy - dr, dr * 2.0f, dr * 2.0f);

 // Label BELOW node (always inside frame)
 g.setFont (DysektLookAndFeel::makeFont (hov ? 11.0f : 9.5f, true));
 g.setColour (node.colour.withAlpha (hov ? 1.0f : 0.70f));
 g.drawText (juce::String (node.label),
 juce::Rectangle<float> (cx - 14.0f, cy + r + 2.0f, 28.0f, 12.0f),
 juce::Justification::centred, false);

 if (hov)
 {
 g.setFont (DysektLookAndFeel::makeFont (8.5f));
 g.setColour (node.colour.withAlpha (0.60f));
 g.drawText ("right-click to lock",
 juce::Rectangle<float> (cx - 48.0f, cy + r + 15.0f, 96.0f, 12.0f),
 juce::Justification::centred, false);
 }
 }
 }
}

void SliceWaveformLcd::drawNoData (juce::Graphics& g)
{
 auto b = getLocalBounds().reduced (4);

 if (! data.hasSample || data.isDefault)
 {
 // Show "EMPTY" prominently when no real sample is loaded
 g.setFont (DysektLookAndFeel::makeFont (18.0f, true));
 g.setColour (lcd2Phosphor().withAlpha (0.18f));
 g.drawText ("EMPTY", b, juce::Justification::centred);

 g.setFont (DysektLookAndFeel::makeFont (7.5f));
 g.setColour (lcd2Dim().brighter (0.5f));
 g.drawText ("drag a sample here or use the browser",
 b.removeFromBottom (18), juce::Justification::centred);
 }
 else
 {
 g.setFont (DysektLookAndFeel::makeFont (10.0f));
 g.setColour (lcd2Dim().brighter (0.4f));
 g.drawText ("-- SELECT A SLICE --", b, juce::Justification::centred);
 }
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void SliceWaveformLcd::drawPlayhead (juce::Graphics& g, const juce::Rectangle<float>& area)
{
 if (data.sliceIndex < 0) return;

 const int totalRange = data.endSample - data.startSample;
 if (totalRange <= 0) return;

 auto& vp = processor.voicePool;

 for (int i = 0; i < VoicePool::kMaxVoices; ++i)
 {
 const auto& v = vp.getVoice (i);
 if (! v.active || v.sliceIdx != data.sliceIndex) continue;

 const float rawPos = vp.voicePositions[i].load (std::memory_order_relaxed);
 float xn = (rawPos - (float) data.startSample) / (float) totalRange;
 xn = juce::jlimit (0.0f, 1.0f, xn);

 const float x = area.getX() + xn * area.getWidth();

 // Soft glow halo
 g.setColour (lcd2Phosphor().withAlpha (0.12f));
 g.drawLine (x - 1.5f, area.getY(), x - 1.5f, area.getBottom(), 1.0f);
 g.drawLine (x + 1.5f, area.getY(), x + 1.5f, area.getBottom(), 1.0f);

 // Main playhead line
 g.setColour (lcd2Phosphor().withAlpha (0.85f));
 g.drawLine (x, area.getY(), x, area.getBottom(), 1.5f);

 // Small triangle cap at top
 const float capH = 5.0f;
 juce::Path cap;
 cap.addTriangle (x - 3.5f, area.getY(),
 x + 3.5f, area.getY(),
 x, area.getY() + capH);
 g.fillPath (cap);

 // Only draw the most-recently-hit voice
 break;
 }
}

void SliceWaveformLcd::paint (juce::Graphics& g)
{
 // Clip to rounded LCD boundary — stops accent glow artefacts showing as
 // black corner notches against the plugin background.
 {
 juce::Path clipPath;
 clipPath.addRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
 g.reduceClipRegion (clipPath);
 }
 buildDisplayData();
 drawBackground (g);

 // isDefault (Empty.wav) always shows EMPTY — even if an auto-slice exists
 if (! data.hasSample || ! data.hasSlice || data.isDefault)
 {
 drawNoData (g);
 return;
 }

 // Nodes are rebuilt in repaintLcd() (timer-driven), not here.
 // During drag, dragRole != None so mouseDrag maintains envNodes directly.

 // lcdArea: inset used for waveform/envelope drawing (respects border stroke).
 // nodeArea: full usable bounds so node circles sit flush with the frame top
 // instead of overflowing above it. screenArea must match nodeArea
 // so hit-testing and drag coords stay in sync with draw positions.
 const auto lcdArea = getLocalBounds().reduced (4).toFloat().reduced (2.0f);
 const auto nodeArea = getLocalBounds().reduced (4).toFloat();
 screenArea = nodeArea;

 drawWaveform (g, lcdArea);
 drawEnvelope (g, lcdArea);
 drawNodes (g, nodeArea);
 drawPlayhead (g, lcdArea);
}
