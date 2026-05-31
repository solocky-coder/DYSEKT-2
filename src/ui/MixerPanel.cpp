#include "MixerPanel.h"
#include "../PluginProcessor.h"
static constexpr int kMaxMeterSlices = DysektProcessor::kMaxMeterSlices;
#include "DysektLookAndFeel.h"
#include "../audio/Slice.h"
#include "../params/ParamIds.h"
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Ctor / Dtor
// ─────────────────────────────────────────────────────────────────────────────
MixerPanel::MixerPanel (DysektProcessor& p)
    : processor (p)
{
    setWantsKeyboardFocus (false);
    startTimerHz (30);
}

MixerPanel::~MixerPanel() = default;

// ─────────────────────────────────────────────────────────────────────────────
//  updateFromSnapshot
// ─────────────────────────────────────────────────────────────────────────────
void MixerPanel::updateFromSnapshot()
{
    const uint32_t v = (uint32_t) processor.getUiSliceSnapshotVersion();
    if (v != cachedVersion)
    {
        cachedVersion = v;

        // Auto-scroll so the selected row is always visible.
        // Only nudge scroll when the selection actually changes.
        const auto& snap = processor.getUiSliceSnapshot();
        if (snap.selectedSlice >= 0 && snap.selectedSlice != cachedNumSlices)
        {
            const int visTop    = kHeaderH;
            const int visBottom = getHeight() - kSf2RowH - kMasterH;
            const int visH      = visBottom - visTop;

            const int rowTop    = kHeaderH + snap.selectedSlice * kRowH - scrollPixels;
            const int rowBottom = rowTop + kRowH;

            if (rowTop < visTop)
                scrollPixels = kHeaderH + snap.selectedSlice * kRowH - visTop;
            else if (rowBottom > visBottom)
                scrollPixels = kHeaderH + snap.selectedSlice * kRowH - (visH - kRowH);

            // Clamp to valid scroll range
            const int totalH  = snap.numSlices * kRowH + kMasterH + kSf2RowH;
            const int maxScroll = juce::jmax (0, totalH - (getHeight() - kHeaderH));
            scrollPixels = juce::jlimit (0, maxScroll, scrollPixels);
        }
        cachedNumSlices = snap.selectedSlice;

        repaint();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Layout helpers
// ─────────────────────────────────────────────────────────────────────────────
int MixerPanel::colX (Col col) const
{
    return kNameColW + (int)col * kKnobColW;
}

int MixerPanel::rowY (int sliceIdx) const
{
    return kHeaderH + sliceIdx * kRowH - scrollPixels;
}

int MixerPanel::sf2RowY() const
{
    // SF-PLAYER row sits immediately below the slice rows, above Master.
    const auto& snap = processor.getUiSliceSnapshot();
    return kHeaderH + snap.numSlices * kRowH - scrollPixels;
}

int MixerPanel::masterRowY() const
{
    // Master row is at the very bottom, below the SF-PLAYER row.
    const auto& snap = processor.getUiSliceSnapshot();
    return kHeaderH + snap.numSlices * kRowH + kSf2RowH - scrollPixels;
}

MixerPanel::Cell MixerPanel::hitTest (juce::Point<int> pos) const
{
    Cell c;
    if (pos.y < kHeaderH) return c;          // header — no hit

    const auto& snap = processor.getUiSliceSnapshot();
    [[maybe_unused]] const int totalRowsH = snap.numSlices * kRowH + kMasterH;
    const int contentTop = kHeaderH;

    // Which logical row?
    const int relY = pos.y - contentTop + scrollPixels;
    const int logicalRow = relY / kRowH;

    if (logicalRow < snap.numSlices)
    {
        c.row = logicalRow;
        c.isMaster = false;
    }
    else if (relY >= snap.numSlices * kRowH &&
             relY <  snap.numSlices * kRowH + kSf2RowH)
    {
        c.row = -2;
        c.isSf2 = true;
    }
    else if (relY >= snap.numSlices * kRowH + kSf2RowH &&
             relY <  snap.numSlices * kRowH + kSf2RowH + kMasterH)
    {
        c.row = -1;
        c.isMaster = true;
    }
    else return c;  // below content

    // Which column?
    const int cx = pos.x;
    if (cx < kNameColW) { c.row = -2; return c; }  // name column (row select only)
    int colIdx = (cx - kNameColW) / kKnobColW;
    if (colIdx < 0 || colIdx >= kNumCols) return c;
    c.col = (Col) colIdx;

    const int rowTop  = c.isSf2   ? sf2RowY()
                        : c.isMaster ? masterRowY()
                                     : rowY (c.row);
    const int rowHt   = c.isSf2 ? kSf2RowH : c.isMaster ? kMasterH : kRowH;
    c.bounds = { kNameColW + colIdx * kKnobColW, rowTop, kKnobColW, rowHt };
    return c;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Format helpers
// ─────────────────────────────────────────────────────────────────────────────
juce::String MixerPanel::fmtGain (float db) const
{
    return (db >= 0.f ? "+" : "") + juce::String (db, 1) + "dB";
}
juce::String MixerPanel::fmtPan (float pan) const
{
    if (std::abs (pan) < 0.02f) return "C";
    return pan < 0.f ? "L" + juce::String ((int)(-pan * 100.f))
                     : "R" + juce::String ((int)( pan * 100.f));
}
juce::String MixerPanel::fmtFcut (float hz) const
{
    return hz >= 999.f ? juce::String ((int)(hz / 1000.f)) + "k"
                       : juce::String ((int) hz);
}
juce::String MixerPanel::fmtPres (float res) const
{
    return juce::String ((int)(res * 100.f)) + "%";
}
juce::String MixerPanel::fmtOut (int bus) const
{
    return juce::String (bus + 1);
}
juce::String MixerPanel::fmtMute (int mg) const
{
    return juce::String (mg);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Norm helpers
// ─────────────────────────────────────────────────────────────────────────────
// 0 dB maps to 0.5 (12 o'clock).  Range: -24 dB (fully CCW) → +24 dB (fully CW).
// Values below -24 clamp to 0; values above +24 clamp to 1.
float MixerPanel::toNormGain (float db)  const { return juce::jlimit (0.f, 1.f, (db + 24.f) / 48.f); }
float MixerPanel::toNormPan  (float pan) const { return juce::jlimit (0.f, 1.f, (pan + 1.f) * 0.5f); }
float MixerPanel::toNormFcut (float hz)  const
{
    return juce::jlimit (0.f, 1.f,
        std::log2 (juce::jmax (20.f, hz) / 20.f) / std::log2 (20000.f / 20.f));
}
float MixerPanel::toNormPres (float res) const { return juce::jlimit (0.f, 1.f, res); }
float MixerPanel::toNormOut  (int bus)   const { return juce::jlimit (0.f, 1.f, bus / 15.f); }

float MixerPanel::fromNormFcut (float n) const
{
    return 20.f * std::pow (2.f, n * std::log2 (20000.f / 20.f));
}

// ─────────────────────────────────────────────────────────────────────────────
//  Drawing
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int kKnobR = 7;   // knob radius (px) — slightly smaller than SCB

void MixerPanel::drawKnobInRow (juce::Graphics& g, int cx, int cy,
                                 float norm, bool locked, bool isMaster,
                                 bool isGain) const
{
    const auto& theme = getTheme();
    const float r = (float) kKnobR;

    // Track arc (background) — 270° sweep, 7 o'clock → 5 o'clock
    const float startA  = juce::MathConstants<float>::pi * 0.75f;
    const float arcLen  = juce::MathConstants<float>::pi * 1.5f;
    juce::Path track;
    track.addCentredArc ((float)cx, (float)cy, r, r, 0.f, startA, startA + arcLen, true);
    g.setColour (theme.separator.withAlpha (0.6f));
    g.strokePath (track, juce::PathStrokeType (1.5f));

    // Fill arc — for gain knobs: bidirectional from 12 o'clock (norm = 0.5)
    //            for all others: sweep from 7 o'clock (startA) as before
    const auto fillCol = locked ? theme.lockActive
                                : (isMaster ? theme.accent.brighter (0.15f) : theme.accent);
    const float normC = juce::jlimit (0.f, 1.f, norm);

    if (isGain)
    {
        // 0 dB is at norm=0.5 → 12 o'clock (centre of the sweep)
        constexpr float zeroNorm  = 0.5f;
        const float     zeroAngle = startA + arcLen * zeroNorm;   // 12 o'clock

        if (std::abs (normC - zeroNorm) > 0.005f)
        {
            juce::Path fill;
            if (normC > zeroNorm)   // boost → fill rightward from centre
                fill.addCentredArc ((float)cx, (float)cy, r, r, 0.f,
                                    zeroAngle, startA + arcLen * normC, true);
            else                    // cut → fill leftward from centre
                fill.addCentredArc ((float)cx, (float)cy, r, r, 0.f,
                                    startA + arcLen * normC, zeroAngle, true);
            g.setColour (fillCol);
            g.strokePath (fill, juce::PathStrokeType (1.5f));
        }

        // Small centre-tick marker at 12 o'clock so 0 dB is always visible
        const float tx = (float)cx + r * std::cos (zeroAngle);
        const float ty = (float)cy + r * std::sin (zeroAngle);
        g.setColour (theme.foreground.withAlpha (0.30f));
        g.drawLine ((float)cx + (r - 2.f) * std::cos (zeroAngle),
                    (float)cy + (r - 2.f) * std::sin (zeroAngle),
                    tx, ty, 1.0f);
    }
    else
    {
        // Non-gain knob: sweep from hard left stop
        if (normC > 0.001f)
        {
            const float endAngle = startA + arcLen * normC;
            juce::Path fill;
            fill.addCentredArc ((float)cx, (float)cy, r, r, 0.f, startA, endAngle, true);
            g.setColour (fillCol);
            g.strokePath (fill, juce::PathStrokeType (1.5f));
        }
    }

    // Centre dot
    g.setColour (locked ? theme.lockActive.withAlpha (0.7f) : theme.accent.withAlpha (0.55f));
    g.fillEllipse ((float)cx - 2.f, (float)cy - 2.f, 4.f, 4.f);
}

void MixerPanel::drawMuteBadge (juce::Graphics& g, int cx, int cy,
                                  int muteGroup, bool locked, bool dimmed) const
{
    const auto& theme = getTheme();
    const int bw = 18, bh = 16;
    const juce::Rectangle<float> r ((float)(cx - bw/2), (float)(cy - bh/2), (float)bw, (float)bh);

    // When global MONO is on, render the badge fully greyed out
    if (dimmed)
    {
        g.setColour (theme.separator.withAlpha (0.18f));
        g.fillRoundedRectangle (r, 2.5f);
        g.setColour (theme.foreground.withAlpha (0.15f));
        g.drawRoundedRectangle (r, 2.5f, 0.8f);
        g.setFont (DysektLookAndFeel::makeFont (11.0f));
        g.setColour (theme.foreground.withAlpha (0.15f));
        g.drawText (juce::String (muteGroup), r.toNearestInt(), juce::Justification::centred);
        return;
    }

    const bool active = (muteGroup > 0);
    g.setColour (active ? (locked ? theme.lockActive.withAlpha (0.15f)
                                  : theme.accent.withAlpha (0.10f))
                        : theme.separator.withAlpha (0.3f));
    g.fillRoundedRectangle (r, 2.5f);

    g.setColour (active ? (locked ? theme.lockActive : theme.accent)
                        : theme.foreground.withAlpha (0.25f));
    g.drawRoundedRectangle (r, 2.5f, 0.8f);

    g.setFont (DysektLookAndFeel::makeFont (11.0f));
    g.setColour (active ? (locked ? theme.lockActive : theme.accent)
                        : theme.foreground.withAlpha (0.3f));
    g.drawText (juce::String (muteGroup), r.toNearestInt(), juce::Justification::centred);
}

void MixerPanel::drawHeader (juce::Graphics& g) const
{
    const auto& theme = getTheme();

    g.setColour (theme.darkBar.darker (0.3f));
    g.fillRect (0, 0, getWidth(), kHeaderH);

    g.setColour (theme.separator);
    g.drawHorizontalLine (kHeaderH - 1, 0.f, (float) getWidth());

    // Slice column
    g.setFont (DysektLookAndFeel::makeFont (12.0f));
    g.setColour (theme.accent.withAlpha (0.5f));
    g.drawText ("SLICE", 10, 0, kNameColW - 10, kHeaderH, juce::Justification::centredLeft);

    // Knob column headers
    const char* labels[kNumCols] = { "GAIN", "PAN", "FCUT", "PRES", "MUTE GRP", "CHRO", "LEGATO", "OUT" };
    for (int i = 0; i < kNumCols; ++i)
    {
        g.setColour (i < 4 ? theme.accent.withAlpha (0.55f)
                           : theme.foreground.withAlpha (0.28f));
        g.drawText (labels[i],
                    colX ((Col)i), 0, kKnobColW, kHeaderH,
                    juce::Justification::centred);
    }
}

void MixerPanel::drawChroBadge (juce::Graphics& g, int cx, int cy, int channel, bool locked) const
{
    const auto& theme = getTheme();
    const int bw = 18, bh = 16;
    const juce::Rectangle<float> r ((float)(cx - bw/2), (float)(cy - bh/2), (float)bw, (float)bh);

    const bool active = (channel > 0);
    g.setColour (active ? (locked ? theme.lockActive.withAlpha (0.15f) : theme.accent.withAlpha (0.15f))
                        : theme.separator.withAlpha (0.3f));
    g.fillRoundedRectangle (r, 2.5f);
    g.setColour (active ? (locked ? theme.lockActive : theme.accent)
                        : (locked ? theme.lockActive.withAlpha (0.5f) : theme.foreground.withAlpha (0.25f)));
    g.drawRoundedRectangle (r, 2.5f, 0.8f);

    g.setFont (DysektLookAndFeel::makeFont (11.0f));
    g.setColour (active ? (locked ? theme.lockActive : theme.accent)
                        : (locked ? theme.lockActive.withAlpha (0.5f) : theme.foreground.withAlpha (0.3f)));
    g.drawText (active ? juce::String (channel) : "-", r.toNearestInt(), juce::Justification::centred);
}

void MixerPanel::drawMeter (juce::Graphics& g,
                             int x, int y, int w, int h,
                             float peakL, float peakR,
                             juce::Colour tint, int si) const
{
    // ── Phosphor hairline meter ───────────────────────────────────────────
    // Two channels (L top, R bottom), each a single 1px fill bar plus a
    // glowing 1px hold marker.  Colours shift green → yellow → red like
    // a phosphor CRT trace.

    const int gap    = 2;
    const int barH   = (h - gap) / 2;   // height of each channel bar
    const int holdW  = 2;               // hold marker width in px

    // Update hold registers
    const int si2 = juce::jlimit (0, kMaxHoldSlices - 1, si);
    if (peakL > holdL[si2]) holdL[si2] = peakL;
    if (peakR > holdR[si2]) holdR[si2] = peakR;

    // Perceptual mapping: sqrt gives better visual resolution in the low end
    auto toFill = [] (float pk) -> float
    {
        return std::sqrt (juce::jlimit (0.0f, 1.0f, pk));
    };

    // Phosphor colour at normalised position 0-1 along bar
    auto phosphorCol = [&] (float pos, float /*pk*/) -> juce::Colour
    {
        if (pos < 0.70f)
        {
            // Green zone: dim base → bright phosphor green
            const float t = pos / 0.70f;
            return tint.withAlpha (0.25f + t * 0.65f);
        }
        else if (pos < 0.85f)
        {
            // Yellow zone
            const float t = (pos - 0.70f) / 0.15f;
            return tint.interpolatedWith (juce::Colour (0xFFFFE000), t)
                       .withAlpha (0.88f);
        }
        else
        {
            // Red zone
            const float t = (pos - 0.85f) / 0.15f;
            return juce::Colour (0xFFFF2222).withAlpha (0.75f + t * 0.20f);
        }
    };

    auto drawBar = [&] (int barY, float pk, float hold)
    {
        const float fill = toFill (pk);
        const int   litW = juce::roundToInt (fill * (float)(w - holdW - 2));

        // Dark background track
        g.setColour (juce::Colour (0xFF0A0A0A));
        g.fillRect (x, barY, w, barH);

        // Thin border
        g.setColour (juce::Colour (0xFF1E1E1E));
        g.drawRect (x, barY, w, barH);

        // Gradient fill — drawn as a single gradient rect for performance
        if (litW > 0)
        {
            juce::ColourGradient grad (phosphorCol (0.0f,  pk), (float) x,           (float) barY,
                                       phosphorCol (fill,  pk), (float)(x + litW),   (float) barY,
                                       false);
            // add midpoint anchors for accurate colour mapping
            grad.addColour (0.70 / (double) juce::jmax (fill, 0.01f),
                            phosphorCol (0.70f * fill, pk));
            grad.addColour (juce::jlimit (0.0, 1.0, 0.85 / (double) juce::jmax (fill, 0.01f)),
                            phosphorCol (0.85f * fill, pk));
            g.setGradientFill (grad);
            g.fillRect (x + 1, barY + 1, litW, barH - 2);
        }

        // Hold marker — bright hairline with soft glow
        const float hFill = toFill (hold);
        const int   hx    = x + 1 + juce::roundToInt (hFill * (float)(w - holdW - 2));
        if (hFill > 0.01f && hx < x + w - 1)
        {
            // Outer glow
            g.setColour (phosphorCol (hFill, hold).withAlpha (0.25f));
            g.fillRect  (hx - 1, barY + 1, holdW + 2, barH - 2);
            // Core bright line
            g.setColour (phosphorCol (hFill, hold).withAlpha (0.95f));
            g.fillRect  (hx, barY + 1, holdW, barH - 2);
        }
    };

    drawBar (y,            peakL, holdL[si2]);
    drawBar (y + barH + gap, peakR, holdR[si2]);

    // ── dB tick marks ────────────────────────────────────────────────────
    // Draw subtle vertical lines at -6, -12, -18, -24 dB across both bars.
    // toFill uses sqrt mapping so we must invert: fill = sqrt(linear) → tickX.
    struct Tick { float db; const char* label; };
    static constexpr Tick kTicks[] = { {-6,"−6"}, {-12,"−12"}, {-18,"−18"}, {-24,"−24"} };
    g.setFont (DysektLookAndFeel::makeFont (6.5f));
    for (const auto& tick : kTicks)
    {
        const float linear = juce::Decibels::decibelsToGain (tick.db);
        const float fill   = std::sqrt (juce::jlimit (0.0f, 1.0f, linear));
        const int   tx     = x + 1 + juce::roundToInt (fill * (float)(w - 4));
        if (tx <= x || tx >= x + w) continue;

        // Tick line spanning both bars + gap
        g.setColour (juce::Colour (0xFFFFFFFF).withAlpha (0.10f));
        g.drawVerticalLine (tx, (float) y, (float)(y + barH + gap + barH));

        // Label below bottom bar — only if there's enough horizontal space
        if (tx - x > 14)
        {
            g.setColour (juce::Colour (0xFFFFFFFF).withAlpha (0.18f));
            g.drawText (tick.label, tx - 10, y + barH + gap + barH + 1, 20, 6,
                        juce::Justification::centred);
        }
    }
}

void MixerPanel::drawSliceRow (juce::Graphics& g, int ry, int idx, bool selected) const
{
    const auto& theme = getTheme();
    const auto& snap  = processor.getUiSliceSnapshot();
    const auto& sl    = snap.slices[(size_t) idx];

    // Row background
    if (selected)
    {
        g.setColour (theme.accent.withAlpha (0.06f));
        g.fillRect (2, ry, getWidth() - 2, kRowH);
        g.setColour (theme.accent.withAlpha (0.55f));
        g.fillRect (0, ry, 2, kRowH);
    }
    else if (idx % 2 == 1)
    {
        g.setColour (juce::Colour (0xFF000000).withAlpha (0.12f));
        g.fillRect (0, ry, getWidth(), kRowH);
    }

    // ── Full-lane slice colour tint ──────────────────────────────────────
    // Subtle wash across the entire row so each slice is immediately
    // identifiable at a glance, matching the waveform lane colours.
    g.setColour (sl.colour.withAlpha (selected ? 0.16f : 0.10f));
    g.fillRect (kNameColW, ry, getWidth() - kNameColW, kRowH);

    // Row bottom divider
    g.setColour (theme.separator.withAlpha (0.35f));
    g.drawHorizontalLine (ry + kRowH - 1, (float) kNameColW * 0.3f, (float) getWidth());

    // ── Slice name column — tinted with slice colour ─────────────────────
    const juce::Colour dot = sl.colour;

    // Colour bar on left edge (thicker, more visible than old dot)
    g.setColour (dot.withAlpha (0.85f));
    g.fillRect (0, ry, 3, kRowH);

    // Colour tint on name column background
    g.setColour (dot.withAlpha (0.13f));
    g.fillRect (3, ry, kNameColW - 4, kRowH);

    // Slice number / custom name
    g.setFont (DysektLookAndFeel::makeFont (12.0f));
    g.setColour (dot.withAlpha (selected ? 0.95f : 0.65f));
    if (sl.name.isNotEmpty())
    {
        // Custom name set — show it in the name column
        g.drawText (sl.name.toUpperCase().substring (0, 9),
                    5, ry, kNameColW - 8, kRowH, juce::Justification::centredLeft);
    }
    else
    {
        // No custom name — show padded slice number
        g.drawText (juce::String (idx + 1).paddedLeft ('0', 2),
                    8, ry, 30, kRowH, juce::Justification::centredLeft);

        // Duration (only when no custom name, to keep layout clean)
        const double srate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 44100.0;
        const int end = processor.sliceManager.getEndForSlice (idx, snap.sampleNumFrames);
        const double lenSec = (end - sl.startSample) / srate;
        g.setFont (DysektLookAndFeel::makeFont (11.0f));
        g.setColour (theme.foreground.withAlpha (0.30f));
        g.drawText (juce::String (lenSec, 2) + "s",
                    40, ry, kNameColW - 42, kRowH, juce::Justification::centredLeft);
    }

    // ── Knob columns ────────────────────────────────────────────────────
    const int kcy = ry + kRowH / 2;

    static constexpr int kTextOffX = kKnobR + 5;

    auto drawCol = [&] (Col col, float norm, bool locked,
                         const juce::String& valStr)
    {
        const int x = colX (col);
        const int cx = x + kKnobR + 8;
        drawKnobInRow (g, cx, kcy, norm, locked);

        const int tx = cx + kKnobR + 4;
        const int tw = kKnobColW - (tx - x) - 2;
        g.setFont (DysektLookAndFeel::makeFont (12.0f));
        g.setColour (locked ? theme.foreground.withAlpha (0.90f)
                            : theme.foreground.withAlpha (0.40f));
        g.drawText (valStr, tx, ry + 1, tw, kRowH - 2, juce::Justification::centredLeft);
    };

    // GAIN — isGain=true so fill is bidirectional from 12 o'clock
    const bool gainLocked = (sl.lockMask & kLockVolume) != 0;
    {
        const int x  = colX (ColGain);
        const int cx = x + kKnobR + 8;
        drawKnobInRow (g, cx, kcy, toNormGain (sl.volume), gainLocked, false, /*isGain=*/true);
        const int tx = cx + kKnobR + 4;
        const int tw = kKnobColW - (tx - x) - 2;
        g.setFont (DysektLookAndFeel::makeFont (12.0f));
        g.setColour (gainLocked ? theme.foreground.withAlpha (0.90f)
                                : theme.foreground.withAlpha (0.40f));
        g.drawText (fmtGain (sl.volume), tx, ry + 1, tw, kRowH - 2, juce::Justification::centredLeft);
    }

    // PAN — horizontal bipolar slider
    {
        const bool    panLocked = (sl.lockMask & kLockPan) != 0;
        const int     x      = colX (ColPan);
        const int     sliderX = x + 6;
        const int     sliderW = kKnobColW - 12;
        const int     sliderY = kcy - 3;
        const int     sliderH = 6;
        const float   pan     = sl.pan;
        const float   norm    = toNormPan (pan);
        const int     thumbX  = sliderX + (int)(norm * (float)sliderW);
        const int     centreX = sliderX + sliderW / 2;
        const auto    fillCol = panLocked ? theme.lockActive : theme.accent;

        // Track bg
        g.setColour (theme.darkBar.darker (0.3f));
        g.fillRoundedRectangle ((float)sliderX, (float)sliderY,
                                 (float)sliderW, (float)sliderH, 2.f);

        // Centre tick
        g.setColour (theme.foreground.withAlpha (0.18f));
        g.drawVerticalLine (centreX, (float)sliderY, (float)(sliderY + sliderH));

        // Fill from centre
        if (std::abs (pan) > 0.005f)
        {
            const int fillX = (pan < 0.f) ? thumbX : centreX;
            const int fillW = std::abs (thumbX - centreX);
            if (fillW > 0)
            {
                g.setColour (fillCol.withAlpha (panLocked ? 0.55f : 0.35f));
                g.fillRect (fillX, sliderY + 1, fillW, sliderH - 2);
            }
        }

        // Thumb
        g.setColour (fillCol.withAlpha (panLocked ? 1.0f : 0.85f));
        g.fillRoundedRectangle ((float)(thumbX - 2), (float)(sliderY - 1),
                                 4.f, (float)(sliderH + 2), 1.5f);

        // Value label — below the slider track, centred in column
        g.setFont (DysektLookAndFeel::makeFont (12.0f));
        g.setColour (panLocked ? theme.foreground.withAlpha (0.90f)
                               : theme.foreground.withAlpha (0.40f));
        g.drawText (fmtPan (pan), x, sliderY + sliderH + 2, kKnobColW, 10,
                    juce::Justification::centred);
    }

    // FCUT
    const bool filterLocked = (sl.lockMask & kLockFilter) != 0;
    drawCol (ColFcut, toNormFcut (sl.filterCutoff), filterLocked, fmtFcut (sl.filterCutoff));

    // PRES
    drawCol (ColPres, toNormPres (sl.filterRes), filterLocked, fmtPres (sl.filterRes));

    // MUTE GRP
    {
        const bool mg_locked = (sl.lockMask & kLockMuteGroup) != 0;
        const int x = colX (ColMute);
        const int cx = x + kKnobColW / 2;
        const bool monoOn = processor.apvts.getRawParameterValue (ParamIds::globalMono)->load() > 0.5f;
        drawMuteBadge (g, cx, kcy, sl.muteGroup, mg_locked, monoOn);
    }

    // CHRO — chromatic MIDI channel badge
    {
        const bool chromaLocked = (sl.lockMask & kLockChromaticChannel) != 0;
        const int x  = colX (ColChro);
        const int cx = x + kKnobColW / 2;
        drawChroBadge (g, cx, kcy, sl.chromaticChannel, chromaLocked);
    }

    // LEGATO — chromatic legato toggle
    {
        const bool legatoLocked = (sl.lockMask & kLockChromaticLegato) != 0;
        const int x  = colX (ColLegato);
        const bool on = sl.chromaticLegato;
        const auto& T = getTheme();
        juce::Colour col = on ? T.accent : T.foreground.withAlpha (0.28f);
        if (legatoLocked) col = T.lockActive;
        g.setFont (DysektLookAndFeel::makeFont (12.0f));
        g.setColour (col);
        g.drawText (on ? "ON" : "OFF", x, ry, kKnobColW, kRowH, juce::Justification::centred);
    }

    // OUT
    {
        const bool outLocked = (sl.lockMask & kLockOutputBus) != 0;
        const int x = colX (ColOut);
        const int cx = x + kKnobColW / 2 - 6;
        g.setFont (DysektLookAndFeel::makeFont (12.0f));
        g.setColour (outLocked ? theme.foreground.withAlpha (0.85f)
                               : theme.foreground.withAlpha (0.32f));
        g.drawText (fmtOut (sl.outputBus), cx, ry, kKnobColW - 4, kRowH,
                    juce::Justification::centredLeft);
    }

    // METER — horizontal peak bar after OUT, tinted with slice colour
    {
        const int mx = colX (ColOut) + kKnobColW + 4;
        const int mw = getWidth() - mx - 6;
        if (mw > 20)
        {
            const int si = idx < kMaxMeterSlices ? idx : 0;
            const float pkL = processor.slicePeakL[si].load (std::memory_order_relaxed);
            const float pkR = processor.slicePeakR[si].load (std::memory_order_relaxed);
            drawMeter (g, mx, ry + 4, mw, kRowH - 8, pkL, pkR, dot, si);
        }
    }
}

void MixerPanel::drawMasterRow (juce::Graphics& g, int ry) const
{
    const auto& theme = getTheme();

    g.setColour (theme.accent.withAlpha (0.03f));
    g.fillRect (0, ry, getWidth(), kMasterH);
    g.setColour (theme.accent.withAlpha (0.12f));
    g.drawHorizontalLine (ry, 0.f, (float) getWidth());

    // Label
    g.setFont (DysektLookAndFeel::makeFont (11.0f, true));
    g.setColour (theme.accent.withAlpha (0.6f));
    g.drawText ("MASTER", 10, ry, kNameColW - 10, kMasterH, juce::Justification::centredLeft);

    const float masterDb  = processor.apvts.getRawParameterValue (ParamIds::masterVolume)->load();
    const float masterPan = processor.apvts.getRawParameterValue (ParamIds::defaultPan)->load();

    const int kcy = ry + kMasterH / 2;

    auto drawMasterCol = [&] (Col col, float norm, const juce::String& valStr, bool isGain = false)
    {
        const int x  = colX (col);
        const int cx = x + kKnobR + 8;
        drawKnobInRow (g, cx, kcy, norm, false, true, isGain);
        const int tx = cx + kKnobR + 4;
        const int tw = kKnobColW - (tx - x) - 2;
        g.setFont (DysektLookAndFeel::makeFont (11.0f));
        g.setColour (theme.accent.withAlpha (0.55f));
        g.drawText (valStr, tx, ry + 1, tw, kMasterH - 2, juce::Justification::centredLeft);
    };

    drawMasterCol (ColGain, toNormGain (masterDb),  fmtGain (masterDb), /*isGain=*/true);

    // Master PAN — horizontal bipolar slider
    {
        const int   x       = colX (ColPan);
        const int   sliderX = x + 6;
        const int   sliderW = kKnobColW - 12;
        const int   sliderY = kcy - 3;
        const int   sliderH = 6;
        const float norm    = toNormPan (masterPan);
        const int   thumbX  = sliderX + (int)(norm * (float)sliderW);
        const int   centreX = sliderX + sliderW / 2;
        const auto  fillCol = theme.accent;

        g.setColour (theme.darkBar.darker (0.3f));
        g.fillRoundedRectangle ((float)sliderX, (float)sliderY,
                                 (float)sliderW, (float)sliderH, 2.f);
        g.setColour (theme.foreground.withAlpha (0.18f));
        g.drawVerticalLine (centreX, (float)sliderY, (float)(sliderY + sliderH));

        if (std::abs (masterPan) > 0.005f)
        {
            const int fillX = (masterPan < 0.f) ? thumbX : centreX;
            const int fillW = std::abs (thumbX - centreX);
            if (fillW > 0)
            {
                g.setColour (fillCol.withAlpha (0.35f));
                g.fillRect (fillX, sliderY + 1, fillW, sliderH - 2);
            }
        }
        g.setColour (fillCol.withAlpha (0.85f));
        g.fillRoundedRectangle ((float)(thumbX - 2), (float)(sliderY - 1),
                                 4.f, (float)(sliderH + 2), 1.5f);

        const int tx = x + kKnobColW / 2 + 8;
        g.setFont (DysektLookAndFeel::makeFont (11.0f));
        g.setColour (theme.accent.withAlpha (0.55f));
        g.drawText (fmtPan (masterPan), x, sliderY + sliderH + 2, kKnobColW, 10,
                    juce::Justification::centred);
    }

        // Remaining columns — dimmed dashes
    g.setFont (DysektLookAndFeel::makeFont (11.0f));
    g.setColour (theme.foreground.withAlpha (0.15f));
    for (int i = ColFcut; i < kNumCols; ++i)
        g.drawText ("—", colX ((Col)i), ry, kKnobColW, kMasterH, juce::Justification::centred);

    // === ADD THIS BLOCK HERE: ===
    const int mx = colX(ColOut) + kKnobColW + 4;
    const int mw = getWidth() - mx - 6;
    if (mw > 20)
    {
        float pkL = processor.masterPeakL.load(std::memory_order_relaxed);
        float pkR = processor.masterPeakR.load(std::memory_order_relaxed);
        drawMeter(g, mx, ry + 4, mw, kMasterH - 8, pkL, pkR, theme.accent, kMaxMeterSlices - 1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  SF2 player row
// ─────────────────────────────────────────────────────────────────────────────
void MixerPanel::drawSf2Row (juce::Graphics& g, int ry) const
{
    const auto& theme = getTheme();

    // Background — slightly different tint to distinguish from master
    g.setColour (theme.accent.withAlpha (0.06f));
    g.fillRect (0, ry, getWidth(), kSf2RowH);
    g.setColour (theme.accent.withAlpha (0.18f));
    g.drawHorizontalLine (ry, 0.f, (float) getWidth());

    // Label
    g.setFont (DysektLookAndFeel::makeFont (11.0f, true));
    g.setColour (theme.accent.withAlpha (0.75f));
    g.drawText ("SF-PLAYER", 10, ry, kNameColW - 10, kSf2RowH, juce::Justification::centredLeft);

    const int kcy    = ry + kSf2RowH / 2;
    const float volLin = processor.sfzPlayer.getVolume();
    const float volDb  = juce::Decibels::gainToDecibels (volLin, -100.f);
    const float pan    = processor.sfzPlayer.getPan();

    // GAIN knob
    {
        const int x  = colX (ColGain);
        const int cx = x + kKnobR + 8;
        drawKnobInRow (g, cx, kcy, toNormGain (volDb), false, true, /*isGain=*/true);
        const int tx = cx + kKnobR + 4;
        const int tw = kKnobColW - (tx - x);
        g.setFont (DysektLookAndFeel::makeFont (12.0f));
        g.setColour (theme.foreground.withAlpha (0.40f));
        g.drawText (fmtGain (volDb), tx, ry + 1, tw, kSf2RowH - 2, juce::Justification::centredLeft);
    }

    // PAN slider
    {
        const int   x       = colX (ColPan);
        const int   sliderX = x + 6;
        const int   sliderW = kKnobColW - 12;
        const int   sliderY = kcy - 3;
        const int   sliderH = 6;
        const float norm    = toNormPan (pan);
        const int   thumbX  = sliderX + (int)(norm * (float)sliderW);
        const int   centreX = sliderX + sliderW / 2;
        const auto  fillCol = theme.accent;

        g.setColour (theme.darkBar.darker (0.3f));
        g.fillRoundedRectangle ((float)sliderX, (float)sliderY, (float)sliderW, (float)sliderH, 2.f);
        g.setColour (theme.foreground.withAlpha (0.18f));
        g.drawVerticalLine (centreX, (float)sliderY, (float)(sliderY + sliderH));
        if (std::abs (pan) > 0.005f)
        {
            const int fillX = (pan < 0.f) ? thumbX : centreX;
            const int fillW = std::abs (thumbX - centreX);
            if (fillW > 0) { g.setColour (fillCol.withAlpha (0.35f)); g.fillRect (fillX, sliderY + 1, fillW, sliderH - 2); }
        }
        g.setColour (fillCol.withAlpha (0.85f));
        g.fillRoundedRectangle ((float)(thumbX - 2), (float)(sliderY - 1), 4.f, (float)(sliderH + 2), 1.5f);
        g.setFont (DysektLookAndFeel::makeFont (12.0f));
        g.setColour (theme.foreground.withAlpha (0.40f));
        g.drawText (fmtPan (pan), x, sliderY + sliderH + 2, kKnobColW, 10, juce::Justification::centred);
    }

    // Dash-fill columns that don't apply to the SF-Player
    g.setFont (DysektLookAndFeel::makeFont (11.0f));
    g.setColour (theme.foreground.withAlpha (0.15f));
    for (int i = ColFcut; i < kNumCols; ++i)
        g.drawText ("—", colX ((Col)i), ry, kKnobColW, kSf2RowH, juce::Justification::centred);

    // Peak meter using sfzPeakL/R from processor
    const int mx = colX (ColOut) + kKnobColW + 4;
    const int mw = getWidth() - mx - 6;
    if (mw > 20)
    {
        float pkL = processor.sfzPeakL.load (std::memory_order_relaxed);
        float pkR = processor.sfzPeakR.load (std::memory_order_relaxed);
        // Use a dedicated hold slot beyond the slice slots (index kMaxHoldSlices - 2)
        constexpr int kSf2HoldSlot = kMaxHoldSlices - 2;
        holdL[kSf2HoldSlot] = std::max (holdL[kSf2HoldSlot], pkL);
        holdR[kSf2HoldSlot] = std::max (holdR[kSf2HoldSlot], pkR);
        drawMeter (g, mx, ry + 4, mw, kSf2RowH - 8, pkL, pkR, theme.accent, kSf2HoldSlot);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  paint
// ─────────────────────────────────────────────────────────────────────────────
void MixerPanel::paint (juce::Graphics& g)
{
    const auto& theme = getTheme();
    const auto& snap  = processor.getUiSliceSnapshot();

    // ── LCD-style frame — matches waveform + LCD screen aesthetic ────────────
    {
        const auto ac = theme.accent;
        auto b = getLocalBounds();

        juce::ColourGradient outerGrad (juce::Colour (0xFF131313), 0, 0,
                                         juce::Colour (0xFF0E0E0E), 0, (float) b.getHeight(), false);
        g.setGradientFill (outerGrad);
        g.fillRoundedRectangle (b.toFloat(), 4.0f);

        g.setColour (ac.withAlpha (0.65f));
        g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 4.0f, 1.0f);

        auto screen = b.reduced (4);
        g.setColour (theme.darkBar.darker (0.55f));
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

    // Clip ALL content to inner screen rect so the frame border is never overwritten
    g.saveState();
    g.reduceClipRegion (getLocalBounds().reduced (4));

    for (int i = 0; i < snap.numSlices; ++i)
        drawSliceRow (g, rowY (i), i, i == snap.selectedSlice);

    drawSf2Row    (g, sf2RowY());
    drawMasterRow (g, masterRowY());

    // Column dividers
    g.setColour (theme.accent.withAlpha (0.12f));
    g.drawVerticalLine (kNameColW - 1, 0.f, (float) getHeight());
    for (int i = 1; i < kNumCols; ++i)
        g.drawVerticalLine (colX ((Col)i) - 1, (float) kHeaderH, (float) getHeight());

    // Meter column separator
    {
        const int mx = colX (ColOut) + kKnobColW;
        g.setColour (theme.accent.withAlpha (0.12f));
        g.drawVerticalLine (mx + 2, (float) kHeaderH, (float) getHeight());
        g.setFont (DysektLookAndFeel::makeFont (12.0f));
        g.setColour (theme.foreground.withAlpha (0.22f));
        g.drawText ("METER", mx + 6, 0, kMeterColW - 8, kHeaderH, juce::Justification::centredLeft);
    }

    drawHeader (g);

    g.restoreState();
}

void MixerPanel::resized() {}

// ─────────────────────────────────────────────────────────────────────────────
//  Mouse
// ─────────────────────────────────────────────────────────────────────────────
void MixerPanel::mouseDown (const juce::MouseEvent& e)
{
    if (textEditor) { textEditor.reset(); repaint(); }

    const auto& snap = processor.getUiSliceSnapshot();
    const Cell c = hitTest (e.getPosition());

    // Click on name column (c.row == -2) or anywhere in a valid row → select slice
    if (c.row >= 0 && c.row < snap.numSlices)
    {
        // Always select the clicked row
        DysektProcessor::Command sel;
        sel.type = DysektProcessor::CmdSelectSlice;
        sel.intParam1 = c.row;
        processor.pushCommand (sel);

        // If click was in the name column, don't start a drag
        if (c.row == -2) { repaint(); return; }
    }

    if (!c.isMaster && (c.row < 0 || c.row >= snap.numSlices)) return;

    // Mute group — cycle on click (no drag)
    if (c.col == ColMute)
    {
        if (!c.isMaster)
        {
            const auto& sl = snap.slices[(size_t) c.row];
            int next = (sl.muteGroup + 1);
            if (next > 32) next = 0;
            DysektProcessor::Command cmd;
            cmd.type = DysektProcessor::CmdSetSliceParam;
            cmd.intParam1 = DysektProcessor::FieldMuteGroup;
            cmd.floatParam1 = (float) next;
            processor.pushCommand (cmd);
        }
        repaint(); return;
    }

    // CHRO — cycle channel 0→1→2→...→16→0 on click
    if (c.col == ColChro)
    {
        if (!c.isMaster)
        {
            const auto& sl = snap.slices[(size_t) c.row];
            if (e.mods.isRightButtonDown())
            {
                // Right-click: toggle chromatic channel lock
                DysektProcessor::Command cmd;
                cmd.type      = DysektProcessor::CmdToggleLock;
                cmd.intParam1 = (int) kLockChromaticChannel;
                processor.pushCommand (cmd);
            }
            else
            {
                // Left-click: cycle channel 0→1→...→16→0
                int next = (sl.chromaticChannel + 1) % 17;
                DysektProcessor::Command cmd;
                cmd.type = DysektProcessor::CmdSetSliceParam;
                cmd.intParam1 = DysektProcessor::FieldChromaticChannel;
                cmd.floatParam1 = (float) next;
                processor.pushCommand (cmd);
            }
        }
        repaint(); return;
    }

    // OUT — cycle on click
    if (c.col == ColLegato)
    {
        if (!c.isMaster)
        {
            const auto& sl = snap.slices[(size_t) c.row];
            if (e.mods.isRightButtonDown())
            {
                DysektProcessor::Command cmd;
                cmd.type      = DysektProcessor::CmdToggleLock;
                cmd.intParam1 = (int) kLockChromaticLegato;
                processor.pushCommand (cmd);
            }
            else
            {
                DysektProcessor::Command cmd;
                cmd.type        = DysektProcessor::CmdSetSliceParam;
                cmd.intParam1   = DysektProcessor::FieldChromaticLegato;
                cmd.floatParam1 = sl.chromaticLegato ? 0.0f : 1.0f;
                processor.pushCommand (cmd);
            }
        }
        repaint(); return;
    }

    if (c.col == ColOut)
    {
        if (!c.isMaster)
        {
            const auto& sl = snap.slices[(size_t) c.row];
            int next = (sl.outputBus + 1) % 16;
            DysektProcessor::Command cmd;
            cmd.type = DysektProcessor::CmdSetSliceParam;
            cmd.intParam1 = DysektProcessor::FieldOutputBus;
            cmd.floatParam1 = (float) next;
            processor.pushCommand (cmd);
        }
        repaint(); return;
    }

    // Begin knob drag
    drag.active   = true;
    drag.isMaster = c.isMaster;
    drag.isSf2    = c.isSf2;
    drag.sliceIdx = c.row;
    drag.col      = c.col;
    drag.startY   = (c.col == ColPan) ? e.getScreenPosition().x : e.getScreenPosition().y;

    if (c.isSf2)
    {
        if (c.col == ColGain)
            drag.startVal = juce::Decibels::gainToDecibels (processor.sfzPlayer.getVolume(), -100.f);
        else if (c.col == ColPan)
            drag.startVal = processor.sfzPlayer.getPan();
        else
            drag.active = false;  // other columns are not interactive
    }
    else if (c.isMaster)
    {
        drag.startVal = (c.col == ColGain)
            ? processor.apvts.getRawParameterValue (ParamIds::masterVolume)->load()
            : processor.apvts.getRawParameterValue (ParamIds::defaultPan)->load();
    }
    else
    {
        const auto& sl = snap.slices[(size_t) c.row];
        switch (c.col)
        {
            case ColGain: drag.startVal = sl.volume;       break;
            case ColPan:  drag.startVal = sl.pan;          break;
            case ColFcut: drag.startVal = sl.filterCutoff; break;
            case ColPres: drag.startVal = sl.filterRes;    break;
            default:      drag.startVal = 0.f;             break;
        }
    }
}

void MixerPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (!drag.active) return;

    const float dy       = (float)(drag.startY - e.getScreenPosition().y);
    const float dx       = (float)(e.getScreenPosition().x - drag.startY);  // for pan slider
    const bool  fine     = e.mods.isShiftDown();
    const float fineMult = fine ? 0.1f : 1.0f;

    float newVal = drag.startVal;

    if (drag.isSf2)
    {
        if (drag.col == ColGain)
        {
            const float newDb  = juce::jlimit (-100.f, 24.f, drag.startVal + dy * 0.5f * fineMult);
            processor.sfzPlayer.setVolume (juce::Decibels::decibelsToGain (newDb, -100.f));
        }
        else if (drag.col == ColPan)
        {
            processor.sfzPlayer.setPan (juce::jlimit (-1.f, 1.f, drag.startVal + dx * 0.01f * fineMult));
        }
        repaint(); return;
    }

    if (drag.isMaster)
    {
        if (drag.col == ColGain)
        {
            newVal = juce::jlimit (-100.f, 24.f, drag.startVal + dy * 0.5f * fineMult);
            processor.apvts.getRawParameterValue (ParamIds::masterVolume)->store (newVal);
        }
        else if (drag.col == ColPan)
        {
            newVal = juce::jlimit (-1.f, 1.f, drag.startVal + dx * 0.01f * fineMult);
            processor.apvts.getRawParameterValue (ParamIds::defaultPan)->store (newVal);
        }
        repaint(); return;
    }

    DysektProcessor::Command cmd;
    cmd.type = DysektProcessor::CmdSetSliceParam;

    switch (drag.col)
    {
        case ColGain:
            newVal = juce::jlimit (-100.f, 24.f, drag.startVal + dy * 0.5f * fineMult);
            cmd.intParam1 = DysektProcessor::FieldVolume;
            break;
        case ColPan:
            newVal = juce::jlimit (-1.f, 1.f, drag.startVal + dx * 0.01f * fineMult);
            cmd.intParam1 = DysektProcessor::FieldPan;
            break;
        case ColFcut:
        {
            // Log-space drag: convert to norm, adjust, convert back
            float normStart = toNormFcut (drag.startVal);
            float normNew   = juce::jlimit (0.f, 1.f, normStart + dy * 0.005f * fineMult);
            newVal = fromNormFcut (normNew);
            cmd.intParam1 = DysektProcessor::FieldFilterCutoff;
            break;
        }
        case ColPres:
            newVal = juce::jlimit (0.f, 1.f, drag.startVal + dy * 0.005f * fineMult);
            cmd.intParam1 = DysektProcessor::FieldFilterRes;
            break;
        default: return;
    }

    cmd.floatParam1 = newVal;
    processor.pushCommand (cmd);
    repaint();
}

void MixerPanel::mouseUp (const juce::MouseEvent&)
{
    drag.active = false;
}

void MixerPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const Cell c = hitTest (e.getPosition());

    // ── Name column double-click: open inline rename editor ─────────────────
    if (! c.isMaster && c.row >= 0)
    {
        const auto& snap = processor.getUiSliceSnapshot();
        if (c.row < snap.numSlices && e.getPosition().x < kNameColW)
        {
            const auto& sl = snap.slices[(size_t) c.row];
            const auto& theme = getTheme();
            const int ry = kHeaderH + c.row * kRowH;
            const juce::Rectangle<int> nameArea (3, ry + 2, kNameColW - 6, kRowH - 4);

            // Select the row first
            DysektProcessor::Command sel;
            sel.type = DysektProcessor::CmdSelectSlice;
            sel.intParam1 = c.row;
            processor.pushCommand (sel);

            textEditor = std::make_unique<juce::TextEditor>();
            addAndMakeVisible (*textEditor);
            textEditor->setBounds (nameArea);
            textEditor->setFont (DysektLookAndFeel::makeFont (11.0f));
            textEditor->setColour (juce::TextEditor::backgroundColourId, theme.darkBar.brighter (0.15f));
            textEditor->setColour (juce::TextEditor::textColourId,       theme.foreground);
            textEditor->setColour (juce::TextEditor::outlineColourId,    theme.accent);
            textEditor->setInputRestrictions (14);
            textEditor->setText (sl.name, false);
            textEditor->selectAll();
            textEditor->grabKeyboardFocus();

            const int rowIdx = c.row;
            auto commit = [this, rowIdx]
            {
                if (! textEditor) return;
                juce::String newName = textEditor->getText().trim();
                textEditor.reset();
                DysektProcessor::Command cmd;
                cmd.type        = DysektProcessor::CmdSetSliceName;
                cmd.intParam1   = rowIdx;
                cmd.stringParam = newName;
                processor.pushCommand (cmd);
                repaint();
            };
            textEditor->onReturnKey = commit;
            textEditor->onEscapeKey = [this] { textEditor.reset(); repaint(); };
            textEditor->onFocusLost = commit;
            return;
        }
    }

    if (c.col == ColMute || c.col == ColOut || c.col == ColLegato) return;
    if (c.isSf2  && c.col >= ColFcut) return;
    if (!c.isMaster && !c.isSf2 && (c.row < 0)) return;
    if (c.isMaster && c.col >= ColFcut) return;

    // Get current value as display string
    float currentVal = 0.f;
    juce::String suffix;

    if (c.isSf2)
    {
        currentVal = (c.col == ColGain)
            ? juce::Decibels::gainToDecibels (processor.sfzPlayer.getVolume(), -100.f)
            : processor.sfzPlayer.getPan();
    }
    else if (c.isMaster)
    {
        currentVal = (c.col == ColGain)
            ? processor.apvts.getRawParameterValue (ParamIds::masterVolume)->load()
            : processor.apvts.getRawParameterValue (ParamIds::defaultPan)->load();
    }
    else
    {
        const auto& snap = processor.getUiSliceSnapshot();
        if (c.row < 0 || c.row >= snap.numSlices) return;
        const auto& sl = snap.slices[(size_t) c.row];
        switch (c.col)
        {
            case ColGain: currentVal = sl.volume;        break;
            case ColPan:  currentVal = sl.pan;           break;
            case ColFcut: currentVal = sl.filterCutoff;  break;
            case ColPres: currentVal = sl.filterRes * 100.f; break;
            default: return;
        }
    }

    const juce::Rectangle<int> cellBounds = c.bounds;

    textEditor = std::make_unique<juce::TextEditor>();
    addAndMakeVisible (*textEditor);
    textEditor->setBounds (cellBounds.getX() + kKnobR * 2 + 12,
                           cellBounds.getY() + cellBounds.getHeight() / 2 - 8,
                           cellBounds.getWidth() - kKnobR * 2 - 16, 16);
    textEditor->setFont (DysektLookAndFeel::makeFont (11.0f));
    const auto& theme = getTheme();
    textEditor->setColour (juce::TextEditor::backgroundColourId, theme.darkBar.brighter (0.15f));
    textEditor->setColour (juce::TextEditor::textColourId,       theme.foreground);
    textEditor->setColour (juce::TextEditor::outlineColourId,    theme.accent);
    textEditor->setText (juce::String (currentVal, c.col == ColPres ? 0 : 2), false);
    textEditor->selectAll();
    textEditor->grabKeyboardFocus();

    const bool  isMaster = c.isMaster;
    const bool  isSf2    = c.isSf2;
    const Col   col      = c.col;
    const int   rowIdx   = c.row;

    textEditor->onReturnKey = [this, isMaster, isSf2, col, rowIdx]
    {
        if (!textEditor) return;
        float v = textEditor->getText().getFloatValue();
        textEditor.reset();

        if (isSf2)
        {
            if (col == ColGain)
                processor.sfzPlayer.setVolume (juce::Decibels::decibelsToGain (juce::jlimit (-100.f, 24.f, v), -100.f));
            else if (col == ColPan)
                processor.sfzPlayer.setPan (juce::jlimit (-1.f, 1.f, v));
            repaint(); return;
        }

        if (isMaster)
        {
            if (col == ColGain)
            {
                v = juce::jlimit (-100.f, 24.f, v);
                processor.apvts.getRawParameterValue (ParamIds::masterVolume)->store (v);
            }
            else if (col == ColPan)
            {
                v = juce::jlimit (-1.f, 1.f, v);
                processor.apvts.getRawParameterValue (ParamIds::defaultPan)->store (v);
            }
            repaint(); return;
        }

        // Select row first
        DysektProcessor::Command sel;
        sel.type = DysektProcessor::CmdSelectSlice;
        sel.intParam1 = rowIdx;
        processor.pushCommand (sel);

        DysektProcessor::Command cmd;
        cmd.type = DysektProcessor::CmdSetSliceParam;
        switch (col)
        {
            case ColGain: cmd.intParam1 = DysektProcessor::FieldVolume;       cmd.floatParam1 = juce::jlimit (-100.f, 24.f, v);    break;
            case ColPan:  cmd.intParam1 = DysektProcessor::FieldPan;          cmd.floatParam1 = juce::jlimit (-1.f, 1.f, v);       break;
            case ColFcut: cmd.intParam1 = DysektProcessor::FieldFilterCutoff; cmd.floatParam1 = juce::jlimit (20.f, 20000.f, v);   break;
            case ColPres: cmd.intParam1 = DysektProcessor::FieldFilterRes;    cmd.floatParam1 = juce::jlimit (0.f, 1.f, v / 100.f); break;
            default: repaint(); return;
        }
        processor.pushCommand (cmd);
        repaint();
    };
    textEditor->onEscapeKey = [this] { textEditor.reset(); repaint(); };
    textEditor->onFocusLost = [this] { textEditor.reset(); repaint(); };
}

void MixerPanel::mouseWheelMove (const juce::MouseEvent&,
                                   const juce::MouseWheelDetails& wheel)
{
    const auto& snap = processor.getUiSliceSnapshot();
    const int contentH = snap.numSlices * kRowH + kSf2RowH + kMasterH;
    const int visibleH = getHeight() - kHeaderH;
    const int maxScroll = juce::jmax (0, contentH - visibleH);

    scrollPixels = juce::jlimit (0, maxScroll,
        scrollPixels - (int)(wheel.deltaY * 30.f));
    repaint();
}
