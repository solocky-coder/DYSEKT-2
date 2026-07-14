#include "SliceLcdDisplay.h"
#include "DysektLookAndFeel.h"
#include "LcdColours.h"
#include "../PluginProcessor.h"

// Palette and chassis rendering now live in LcdColours.h, shared with
// Sf2LcdDisplay.cpp and SfzLcdDisplay.cpp — see that header for details.
// Do NOT re-declare a local copy of the LcdColours namespace here.

// ── Phosphor-glow flag fill ─────────────────────────────────────────────────────
// Mirrors the "glass gradient + glow" treatment used on the FINE/CHRO/LGTO
// badges (SliceControlBar), but phosphor-tinted so the flags row reads as a
// lit segment of the CRT screen rather than a plastic button glued onto it.
// A subtle vertical gradient gives every pill some depth; active ("on")
// flags additionally get a soft radial glow blooming up from the bottom
// centre, like a genuinely lit phosphor segment. Border and text drawing
// are untouched — only the fill changes.
static void fillPhosphorFlag (juce::Graphics& g, juce::Rectangle<float> bounds,
                               juce::Colour phosphor, juce::Colour flagBg,
                               bool on, float radius)
{
    // flagBg was defined in the palette ("visible pill outline against
    // screen bg") but never actually applied anywhere — pills were only
    // ever getting a low-alpha phosphor tint directly over the near-black
    // screen background, which is why they read as flat/invisible regardless
    // of alpha tuning. Painting the solid flagBg backdrop first guarantees
    // contrast against the screen no matter what the theme's accent colour
    // or alpha values are.
    g.setColour (flagBg);
    g.fillRoundedRectangle (bounds, radius);

    const juce::Colour top    = on ? phosphor.withAlpha (0.32f) : phosphor.withAlpha (0.14f);
    const juce::Colour bottom = on ? phosphor.withAlpha (0.16f) : phosphor.withAlpha (0.06f);

    juce::ColourGradient grad (top, bounds.getX(), bounds.getY(),
                                bottom, bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (bounds, radius);

    if (on)
    {
        juce::ColourGradient glow (phosphor.withAlpha (0.30f),
                                    bounds.getCentreX(), bounds.getBottom(),
                                    juce::Colours::transparentBlack,
                                    bounds.getCentreX(), bounds.getY(), true);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (bounds, radius);
    }
}

// ── Helpers ────────────────────────────────────────────────────────────────────

juce::String SliceLcdDisplay::midiNoteName (int note)
{
    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    note = juce::jlimit (0, 127, note);
    juce::String s (names[note % 12]);
    s += juce::String (note / 12 - 2);   // C3 = 60 → octave = 60/12-2 = 3
    // Pad to 3 chars
    while (s.length() < 3) s += " ";
    return s;
}

juce::String SliceLcdDisplay::formatMs (float secs)
{
    float ms = secs * 1000.0f;
    if (ms < 10.0f)   return juce::String (ms, 1) + "ms";
    if (ms < 1000.0f) return juce::String (juce::roundToInt (ms)) + "ms";
    return juce::String (secs, 2) + "s ";
}


juce::String SliceLcdDisplay::formatPan (float pan)
{
    if (std::abs (pan) < 0.01f) return "C  ";
    if (pan < 0.0f)
    {
        int v = juce::roundToInt (-pan * 100.0f);
        return "L" + juce::String (v).paddedLeft ('0', 2);
    }
    int v = juce::roundToInt (pan * 100.0f);
    return "R" + juce::String (v).paddedLeft ('0', 2);
}

// ── Constructor ────────────────────────────────────────────────────────────────

SliceLcdDisplay::SliceLcdDisplay (DysektProcessor& p)
    : processor (p)
{
    setOpaque (true);
}

bool SliceLcdDisplay::isSfzPlayer2Mode() const noexcept
{
    // midiRouteMode: 0=Slicer, 1=SfPlayer, 2=SfzPlayer2, 3=Sequencer
    return processor.midiRouteMode.load (std::memory_order_relaxed) == 2;
}

// ── Data building ──────────────────────────────────────────────────────────────

void SliceLcdDisplay::buildDisplayData()
{
    data = {};

    const bool sfzMode = isSfzPlayer2Mode();
    const auto& snap = sfzMode ? processor.getUiSliceSnapshot2() : processor.getUiSliceSnapshot();
    data.hasSample    = snap.sampleLoaded && ! snap.sampleMissing;
    data.numSlices    = snap.numSlices;
    data.rootNote     = snap.rootNote;
    data.sampleName   = juce::String (snap.sampleFileName);
    data.sampleNumFrames = snap.sampleNumFrames;
    data.sampleRate   = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 44100.0;

    if (! data.hasSample || snap.selectedSlice < 0 || snap.selectedSlice >= snap.numSlices)
    {
        data.hasSlice = false;
        if (sfzMode) processor.releaseUiSliceSnapshot2(); else processor.releaseUiSliceSnapshot();
        return;
    }

    data.hasSlice    = true;
    data.sliceIndex  = snap.selectedSlice;

    const auto& sl   = snap.slices[(size_t) snap.selectedSlice];
    data.midiNote    = sl.midiNote;
    data.sliceName   = sl.name;

    // During a live drag (waveform or CC), override startSample with the real-time
    // position so the ST: row updates without waiting for the audio-thread snapshot.
    // SFZ-PLAYER never sets liveDragSliceIdx (no manual slicing/bounds-dragging for
    // that engine), so this naturally always falls through to sl.startSample there.
    {
        const int liveIdx = processor.liveDragSliceIdx.load (std::memory_order_acquire);
        const int liveStart = processor.liveDragBoundsStart.load (std::memory_order_relaxed);
        data.startSample = (! sfzMode && liveIdx == snap.selectedSlice && liveStart >= 0)
                               ? liveStart
                               : sl.startSample;
    }

    // Marker model: end derived from next slice's start (or sampleNumFrames).
    data.endSample   = (snap.selectedSlice >= 0 && snap.selectedSlice < snap.numSlices)
                     ? snap.sliceEndSamples[snap.selectedSlice] : snap.sampleNumFrames;
    data.volume      = sl.volume;
    data.pan         = sl.pan;
    data.pitchSemitones = sl.pitchSemitones;
    data.centsDetune = sl.centsDetune;
    // Apply the same lock-resolve logic as SliceControlBar: when a parameter
    // is NOT locked to this slice, show the global APVTS default so the LCD
    // and the SCB knobs always agree.
    auto apvtsMs = [&] (const juce::String& id) -> float {
        auto* p = processor.apvts.getRawParameterValue (id);
        return p ? p->load() / 1000.0f : 0.0f;
    };
    auto apvtsPct = [&] (const juce::String& id) -> float {
        auto* p = processor.apvts.getRawParameterValue (id);
        return p ? p->load() / 100.0f : 1.0f;
    };
    // Always read per-slice. Lock = protection only.
    data.attackSec    = sl.attackSec;
    data.holdSec      = sl.holdSec;
    data.decaySec     = sl.decaySec;
    data.sustainLevel = sl.sustainLevel;
    data.releaseSec   = sl.releaseSec;
    data.reverse     = sl.reverse;
    data.loopMode    = sl.loopMode;
    data.oneShot     = sl.oneShot;
    data.muteGroup   = sl.muteGroup;
    data.globalMono  = processor.apvts.getRawParameterValue (ParamIds::globalMono)->load() > 0.5f;
    data.filterCutoff    = sl.filterCutoff;
    data.filterRes       = sl.filterRes;
    data.sliceLocked     = (sl.lockMask == 0xFFFFFFFFu);
    data.sliceColour     = sl.colour;
    // Extended fields for scroll rows 7-9
    data.stretchEnabled  = sl.stretchEnabled;
    data.tonalityHz      = sl.tonalityHz;
    data.formantSemitones = sl.formantSemitones;

    data.releaseTail     = sl.releaseTail;
    data.outputBus       = sl.outputBus;
    data.bpm             = sl.bpm;

    if (sfzMode) processor.releaseUiSliceSnapshot2(); else processor.releaseUiSliceSnapshot();
}

// ── Repaint trigger ────────────────────────────────────────────────────────────

void SliceLcdDisplay::repaintLcd()
{
    repaint();
}


void SliceLcdDisplay::resized() {}

// ── Paint ──────────────────────────────────────────────────────────────────────

void SliceLcdDisplay::drawLcdBackground (juce::Graphics& g)
{
    LcdColours::drawChassisAndScreen (g, getLocalBounds(), kScanlineAlpha);
}

void SliceLcdDisplay::drawRow (juce::Graphics& g, int row, const juce::String& label,
                                const juce::String& value, bool highlight)
{
    const auto pal = LcdColours::fromTheme();
    auto b = getLocalBounds().reduced (4);
    const float sf    = (float) getHeight() / (float) kPreferredHeight;
    const int rowH  = effectiveRowH();
    const int lPad  = juce::roundToInt (kLeftPad * sf);
    int y = b.getY() + 4 + row * rowH;

    // Skip rows fully outside the visible screen area
    if (y + rowH <= b.getY() + 4 || y >= b.getBottom() - 4) return;

    const juce::Font labelFont = DysektLookAndFeel::makeFont (24.0f * sf, true);
    const juce::Font valueFont = DysektLookAndFeel::makeFont (26.0f * sf);

    if (highlight)
    {
        g.setColour (pal.phosphor.withAlpha (0.10f));
        g.fillRect (b.getX(), y, b.getWidth(), rowH - 1);
    }

    // Header row: label left-aligned, value follows directly after a gap
    const int lx = b.getX() + lPad;
    g.setFont (labelFont);
    g.setColour (highlight ? pal.highlight : pal.labelCol);
    g.drawText (label, lx, y, b.getWidth() / 3, rowH,
                juce::Justification::centredLeft, false);

    const int vx = lx + juce::GlyphArrangement::getStringWidthInt(labelFont, label) + 6;
    g.setFont (valueFont);
    g.setColour (highlight ? pal.highlight : pal.phosphor);
    g.drawText (value, vx, y, b.getRight() - vx - lPad, rowH,
                juce::Justification::centredLeft, false);
}

// ── Two-item row ──────────────────────────────────────────────────────────────
// Left item  : label+value as one string, left-aligned in the left half
// Right item : label+value as one string, centred in the right half
void SliceLcdDisplay::drawRowPair (juce::Graphics& g, int row,
                                    const juce::String& leftStr,
                                    const juce::String& rightStr,
                                    bool highlight)
{
    const auto pal = LcdColours::fromTheme();
    auto b = getLocalBounds().reduced (4);
    const float sf    = (float) getHeight() / (float) kPreferredHeight;
    const int rowH  = effectiveRowH();
    const int lPad  = juce::roundToInt (kLeftPad * sf);
    const int y       = b.getY() + 4 + row * rowH;
    const juce::Font  f = DysektLookAndFeel::makeFont (23.0f * sf);

    // Skip rows fully outside the visible screen area
    if (y + rowH <= b.getY() + 4 || y >= b.getBottom() - 4) return;

    if (highlight)
    {
        g.setColour (pal.phosphor.withAlpha (0.10f));
        g.fillRect (b.getX(), y, b.getWidth(), rowH - 1);
    }

    g.setFont (f);

    // ── Left item — left-aligned from the left edge ───────────────────────────
    const int halfW   = b.getWidth() / 2;
    const int leftX   = b.getX() + lPad;
    const int leftW   = halfW - lPad - 4;

    // Split leftStr into label (up to first space or ':') and rest for colouring
    const int colonPos = leftStr.indexOfChar (':');
    if (colonPos > 0)
    {
        juce::String lbl = leftStr.substring (0, colonPos + 1);
        juce::String val = leftStr.substring (colonPos + 1);
        g.setColour (highlight ? pal.highlight : pal.labelCol);
        g.drawText (lbl, leftX, y, juce::GlyphArrangement::getStringWidthInt(f, lbl) + 2, rowH,
                    juce::Justification::centredLeft, false);
        g.setColour (highlight ? pal.highlight : pal.phosphor);
        g.drawText (val, leftX + juce::GlyphArrangement::getStringWidthInt(f, lbl) + 2, y,
                    leftW - juce::GlyphArrangement::getStringWidthInt(f, lbl) - 2, rowH,
                    juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (highlight ? pal.highlight : pal.phosphor);
        g.drawText (leftStr, leftX, y, leftW, rowH,
                    juce::Justification::centredLeft, false);
    }

    // ── Right item — left-aligned from the right-column start ────────────────
    // Use 52% split so right column has enough room and never overflows.
    const int rightX  = b.getX() + (b.getWidth() * 52 / 100);
    const int rightW  = b.getRight() - rightX - lPad;

    const int rColonPos = rightStr.indexOfChar (':');
    if (rColonPos > 0)
    {
        juce::String rlbl = rightStr.substring (0, rColonPos + 1);
        juce::String rval = rightStr.substring (rColonPos + 1);
        const int rlblW = juce::GlyphArrangement::getStringWidthInt(f, rlbl);

        g.setColour (highlight ? pal.highlight : pal.labelCol);
        g.drawText (rlbl, rightX, y, rlblW + 2, rowH,
                    juce::Justification::centredLeft, false);
        g.setColour (highlight ? pal.highlight : pal.phosphor);
        g.drawText (rval, rightX + rlblW + 2, y,
                    rightW - rlblW - 2, rowH,
                    juce::Justification::centredLeft, false);
    }
    else
    {
        g.setColour (highlight ? pal.highlight : pal.phosphor);
        g.drawText (rightStr, rightX, y, rightW, rowH,
                    juce::Justification::centredLeft, false);
    }
}

void SliceLcdDisplay::drawFlagsRow (juce::Graphics& g, int /*row*/)
{
    // ── Flags drawn as a HORIZONTAL strip along the BOTTOM of the screen ──────
    const auto pal = LcdColours::fromTheme();
    auto screen = getLocalBounds().reduced (4);

    const float sf      = (float) getHeight() / (float) kPreferredHeight;
    const juce::Font flagFont = DysektLookAndFeel::makeFont (16.0f * sf, true);
    const int flagH   = juce::roundToInt (22 * sf);
    const int flagGap = juce::roundToInt (4  * sf);
    const int pad     = juce::roundToInt (3  * sf);
    const int stripY  = screen.getBottom() - flagH - juce::roundToInt (3 * sf);

    struct Flag { juce::String text; bool on; int fieldId; bool isCycle; };
    juce::String loopStr = data.loopMode == 1 ? "LOOP" : (data.loopMode == 2 ? "PING" : "LOOP");
    const bool sfzModeFlags = isSfzPlayer2Mode();

    std::vector<Flag> flags = {
        { "REV",  data.reverse,        DysektProcessor::FieldReverse,        false },
        { loopStr, data.loopMode > 0,  DysektProcessor::FieldLoop,           true  },
        { "1SH",  data.oneShot,        DysektProcessor::FieldOneShot,        false },
    };
    // FieldGlobalMono is a true global APVTS param for the whole Slicer
    // engine with no SFZ-PLAYER equivalent (processMidi2 never reads it) —
    // see the guard in mouseDown(). Rather than show a dead button, omit it
    // entirely here so the remaining flags redistribute across the strip.
    if (! sfzModeFlags)
        flags.push_back ({ data.globalMono ? "MONO" : "POLY",
                            data.globalMono, DysektProcessor::FieldGlobalMono, false });
    flags.push_back ({ "STR",  data.stretchEnabled, DysektProcessor::FieldStretchEnabled, false });
    flags.push_back ({ "TAIL", data.releaseTail,    DysektProcessor::FieldReleaseTail,    false });

    const int numFlags  = (int) flags.size();
    const int lPad      = juce::roundToInt (kLeftPad * sf);
    const int availW    = screen.getWidth() - 2 * lPad - flagGap * (numFlags - 1);
    const int flagW     = availW / numFlags;

    flagHitRects.clear();

    g.setFont (flagFont);
    int fx = screen.getX() + lPad;
    for (auto& f : flags)
    {
        juce::Rectangle<int> box (fx, stripY, flagW, flagH);
        flagHitRects.push_back ({ box, f.fieldId, f.isCycle });

        fillPhosphorFlag (g, box.toFloat(), pal.phosphor, pal.flagBg, f.on, 2.0f);
        g.setColour (f.on ? pal.flagOn : pal.flagOff);
        g.drawRoundedRectangle (box.toFloat(), 2.0f, 1.0f);
        g.drawText (f.text, box.getX() + pad, box.getY(),
                    box.getWidth() - pad * 2, box.getHeight(),
                    juce::Justification::centred, false);

        fx += flagW + flagGap;
    }
}

void SliceLcdDisplay::drawNoSliceScreen (juce::Graphics& g)
{
    const auto pal = LcdColours::fromTheme();
    auto b = getLocalBounds().reduced (4);
    const float sf = (float) getHeight() / (float) kPreferredHeight;
    g.setFont (DysektLookAndFeel::makeFont (20.0f * sf));
    g.setColour (pal.noDataCol);

    if (data.hasSample && data.sampleName.isNotEmpty())
    {
        g.setColour (pal.phosphor.withAlpha (0.6f));
        g.drawText (data.sampleName.toUpperCase(),
                    b.reduced (juce::roundToInt (kLeftPad * sf), 0), juce::Justification::centredTop);
        g.setColour (pal.noDataCol);
    }

    g.setFont (DysektLookAndFeel::makeFont (22.0f * sf));
    g.drawText ("-- NO SLICE SELECTED --", b, juce::Justification::centred);

    if (data.numSlices > 0)
    {
        g.setFont (DysektLookAndFeel::makeFont (18.0f * sf));
        g.setColour (pal.dim);
        g.drawText (juce::String (data.numSlices) + " SLICES  |  SELECT A PAD",
                    b, juce::Justification::centredBottom);
    }
}

void SliceLcdDisplay::drawNoSampleScreen (juce::Graphics& g)
{
    const auto pal = LcdColours::fromTheme();
    auto b = getLocalBounds().reduced (4);
    const float sf = (float) getHeight() / (float) kPreferredHeight;
    g.setFont (DysektLookAndFeel::makeFont (22.0f * sf));
    g.setColour (pal.noDataCol);
    g.drawText ("-- NO SAMPLE LOADED --", b, juce::Justification::centred);

    g.setFont (DysektLookAndFeel::makeFont (18.0f * sf));
    g.setColour (pal.dim);
    g.drawText ("DROP A FILE OR USE THE BROWSER",
                b, juce::Justification::centredBottom);
}



// -- effectiveRowH -------------------------------------------------------
int SliceLcdDisplay::effectiveRowH() const noexcept
{
    const float sf      = (float) getHeight() / (float) kPreferredHeight;
    const int   flagH   = juce::roundToInt (22 * sf);
    const int   flagGap = juce::roundToInt (3  * sf);
    auto        b       = getLocalBounds().reduced (4);
    const int   availH  = b.getHeight() - 4 - flagH - flagGap - 2;
    return std::max (8, availH / kTotalRows);
}

void SliceLcdDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (! data.hasSlice) return;

    const auto pos = e.getPosition();

    // ── NAME row: click opens inline text editor ──────────────────────────────
    if (nameRowHitRect.contains (pos))
    {
        if (nameTextEditor) { nameTextEditor.reset(); repaint(); }

        const auto pal = LcdColours::fromTheme();
        nameTextEditor = std::make_unique<juce::TextEditor>();
        addAndMakeVisible (*nameTextEditor);

        // Position editor over the right half of row 1
        auto edBounds = nameRowHitRect.reduced (2, 4);
        nameTextEditor->setBounds (edBounds);
        nameTextEditor->setFont (DysektLookAndFeel::makeFont (14.0f));
        nameTextEditor->setColour (juce::TextEditor::backgroundColourId,
                                   getTheme().darkBar.brighter (0.2f));
        nameTextEditor->setColour (juce::TextEditor::textColourId,    pal.phosphor);
        nameTextEditor->setColour (juce::TextEditor::outlineColourId, pal.phosphor.withAlpha (0.7f));
        nameTextEditor->setInputRestrictions (14);

        // Seed with current custom name (not the slice number fallback)
        nameTextEditor->setText (data.sliceName, false);
        nameTextEditor->selectAll();
        nameTextEditor->grabKeyboardFocus();

        const int sliceIdx = data.sliceIndex;

        auto commit = [this, sliceIdx]
        {
            if (! nameTextEditor) return;
            juce::String newName = nameTextEditor->getText().trim();
            nameTextEditor.reset();
            DysektProcessor::Command cmd;
            cmd.type          = DysektProcessor::CmdSetSliceName;
            cmd.intParam1     = sliceIdx;
            cmd.stringParam   = newName;
            cmd.targetEngine2 = isSfzPlayer2Mode();
            processor.pushCommand (cmd);
            repaint();
        };

        nameTextEditor->onReturnKey  = commit;
        nameTextEditor->onEscapeKey  = [this] { nameTextEditor.reset(); repaint(); };
        nameTextEditor->onFocusLost  = commit;
        return;
    }

    // Dismiss any open editor on a click elsewhere
    if (nameTextEditor) { nameTextEditor.reset(); repaint(); }

    for (const auto& hit : flagHitRects)
    {
        if (! hit.bounds.contains (pos)) continue;

        using F = DysektProcessor;
        const bool sfzMode = isSfzPlayer2Mode();

        // FieldGlobalMono is a true global APVTS param (shared polyphony mode
        // for the WHOLE Slicer engine) with no SFZ-PLAYER equivalent —
        // processMidi2 never reads it, so toggling it from this tab would
        // silently change the Slicer's behaviour instead. Hide/ignore it here.
        if (sfzMode && hit.fieldId == F::FieldGlobalMono)
            return;

        DysektProcessor::Command cmd;
        cmd.type          = F::CmdSetSliceParam;
        cmd.intParam1     = hit.fieldId;
        cmd.targetEngine2 = sfzMode;

        switch (hit.fieldId)
        {
            case F::FieldReverse:
                cmd.floatParam1 = data.reverse ? 0.0f : 1.0f;
                break;
            case F::FieldLoop:
            {
                // Cycle: Off → Loop → Ping → Off
                int newMode = (data.loopMode + 1) % 3;
                cmd.floatParam1 = (float)newMode;
                processor.pushCommand (cmd);
                // LOO and 1SH are mutually exclusive — enabling loop clears one-shot
                if (newMode > 0 && data.oneShot)
                {
                    DysektProcessor::Command clr;
                    clr.type       = F::CmdSetSliceParam;
                    clr.intParam1  = F::FieldOneShot;
                    clr.floatParam1 = 0.0f;
                    clr.targetEngine2 = sfzMode;
                    processor.pushCommand (clr);
                }
                repaint();
                return;
            }
            case F::FieldOneShot:
            {
                float newVal = data.oneShot ? 0.0f : 1.0f;
                cmd.floatParam1 = newVal;
                processor.pushCommand (cmd);
                // 1SH and LOO are mutually exclusive — enabling one-shot clears loop
                if (newVal > 0.0f && data.loopMode > 0)
                {
                    DysektProcessor::Command clr;
                    clr.type       = F::CmdSetSliceParam;
                    clr.intParam1  = F::FieldLoop;
                    clr.floatParam1 = 0.0f;
                    clr.targetEngine2 = sfzMode;
                    processor.pushCommand (clr);
                }
                repaint();
                return;
            }
            case F::FieldGlobalMono:
                // Toggle global Poly/Mono
                cmd.floatParam1 = data.globalMono ? 0.0f : 1.0f;
                break;
            case F::FieldStretchEnabled:
                cmd.floatParam1 = data.stretchEnabled ? 0.0f : 1.0f;
                break;
            case F::FieldReleaseTail:
                cmd.floatParam1 = data.releaseTail ? 0.0f : 1.0f;
                break;
                break;
            default:
                return;
        }

        processor.pushCommand (cmd);
        repaint();
        return;
    }
}

void SliceLcdDisplay::paint (juce::Graphics& g)
{
    // Fill corners with plugin background before clipping so no white dots appear.
    g.fillAll (getTheme().background);
    // Clip to rounded LCD boundary.
    {
        juce::Path clipPath;
        clipPath.addRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        g.reduceClipRegion (clipPath);
    }
    buildDisplayData();
    drawLcdBackground (g);

    if (! data.hasSample)   { drawNoSampleScreen (g); return; }
    if (! data.hasSlice)    { drawNoSliceScreen  (g); return; }

    // ── Clip all row drawing to the inner screen ─────────────────────────────
    auto screen = getLocalBounds().reduced (4);
    g.saveState();
    g.reduceClipRegion (screen);

    // ── Row 0:  Header — centred: "SL xx / xx  SAMPLENAME" ──────────────────
    {
        juce::String sliceStr = "SL "
            + juce::String (data.sliceIndex + 1).paddedLeft ('0', 2)
            + " / "
            + juce::String (data.numSlices).paddedLeft ('0', 2);

        juce::String nameStr = data.sampleName.toUpperCase().substring (0, 18);

        // Draw centred header: measure label+value as one unit, centre in screen
        auto   screen    = getLocalBounds().reduced (4);
        auto   pal       = LcdColours::fromTheme();
        const float sf   = (float) getHeight() / (float) kPreferredHeight;
        const int rowH  = effectiveRowH();
        const int y      = screen.getY() + 4;

        // Highlight background — tinted with the slice's vibrant colour
        g.setColour (data.sliceColour.isTransparent()
                         ? pal.phosphor.withAlpha (0.10f)
                         : data.sliceColour.withAlpha (0.28f));
        g.fillRect (screen.getX(), y, screen.getWidth(), rowH - 1);

        const juce::Font lblF = DysektLookAndFeel::makeFont (24.0f * sf, true);
        const juce::Font valF = DysektLookAndFeel::makeFont (26.0f * sf);
        const int lblW = juce::GlyphArrangement::getStringWidthInt(lblF, sliceStr);
        const int gap  = 8;
        const int valW = juce::GlyphArrangement::getStringWidthInt(valF, nameStr);
        const int totalW = lblW + gap + valW;
        const int startX = screen.getX() + (screen.getWidth() - totalW) / 2;

        g.setFont (lblF);
        g.setColour (pal.highlight);
        g.drawText (sliceStr, startX, y, lblW + 2, rowH,
                    juce::Justification::centredLeft, false);

        g.setFont (valF);
        g.setColour (pal.highlight);
        g.drawText (nameStr, startX + lblW + gap, y, valW + 2, rowH,
                    juce::Justification::centredLeft, false);

        // Lock badge: small pill on the far right when slice is locked
        if (data.sliceLocked)
        {
            const juce::Font lkF = DysektLookAndFeel::makeFont (16.0f * sf, true);
            const juce::String lkStr = "LOCK";
            const int lkW = juce::GlyphArrangement::getStringWidthInt(lkF, lkStr) + 6;
            const int lkX = screen.getRight() - lkW - 6;
            const int lkY = y + 1;
            const int lkH = rowH - 3;
            g.setColour (pal.phosphor.withAlpha (0.25f));
            g.fillRoundedRectangle ((float) lkX, (float) lkY, (float) lkW, (float) lkH, 2.0f);
            g.setColour (pal.highlight);
            g.drawRoundedRectangle ((float) lkX, (float) lkY, (float) lkW, (float) lkH, 2.0f, 1.0f);
            g.setFont (lkF);
            g.setColour (pal.highlight);
            g.drawText (lkStr, lkX + 3, lkY, lkW - 6, lkH, juce::Justification::centred, false);
        }
    }

    // ── Row 1:  NOTE:Cx(nnn)  |  NAME:xx (slice number or custom name) ───────
    {
        juce::String noteStr = "NOTE:" + midiNoteName (data.midiNote).trimEnd()
            + "(" + juce::String (data.midiNote).paddedLeft ('0', 3) + ")";

        // NAME always shows: custom name if set, otherwise the slice number
        juce::String nameVal = data.sliceName.isNotEmpty()
            ? data.sliceName.toUpperCase().substring (0, 10)
            : juce::String (data.sliceIndex + 1);
        juce::String nameStr = "NAME:" + nameVal;

        // Record the right-side hit area for this row (right half of screen)
        {
            auto scr = getLocalBounds().reduced (4);
            const int rightX = scr.getX() + (scr.getWidth() * 52 / 100);
            const float sfHit = (float) getHeight() / (float) kPreferredHeight;
            const int sRowH = effectiveRowH();
            nameRowHitRect = { rightX, scr.getY() + 4 + sRowH,
                               scr.getRight() - rightX, sRowH };
        }

        drawRowPair (g, 1, noteStr, nameStr);
    }

    // ── Row 2:  ST:nnnnn  |  END:nnnnn ───────────────────────────────────────
    {
        juce::String stStr  = "ST:"  + juce::String (data.startSample);
        juce::String endStr = "END:" + juce::String (data.endSample);
        drawRowPair (g, 2, stStr, endStr);
    }

    // ── Row 3:  LEN:xxxms  |  VOL:+x.xdB ────────────────────────────────────
    {
        const int   lenSamples = data.endSample - data.startSample;
        const float lenMs      = (float) lenSamples / (float) data.sampleRate * 1000.0f;
        juce::String lenStr = "LEN:" + (lenMs < 1000.0f
            ? juce::String (juce::roundToInt (lenMs)) + "ms"
            : juce::String (lenMs / 1000.0f, 2) + "s");

        const float vol = data.volume;
        juce::String volStr = juce::String ("VOL:") + (vol >= 0.0f ? "+" : "") + juce::String (vol, 1) + "dB";
        drawRowPair (g, 3, lenStr, volStr);
    }

    // ── Row 4:  PAN:x  |  PIT:+x.xst ────────────────────────────────────────
    {
        juce::String panStr = "PAN:" + formatPan (data.pan).trimEnd();
        const float  pit    = data.pitchSemitones;
        juce::String pitStr = juce::String ("PIT:") + (pit >= 0.0f ? "+" : "") + juce::String (pit, 1) + "st";
        drawRowPair (g, 4, panStr, pitStr);
    }

    // ── Row 5:  TUNE:+xct  |  FLTR:xxxxxHz ─────────────────────────────────
    {
        const float det = data.centsDetune;
        juce::String detStr = juce::String ("TUNE:") + (det >= 0.0f ? "+" : "") + juce::String (juce::roundToInt (det)) + "ct";
        juce::String fltrStr = "FLTR:" + (data.filterCutoff >= 9999.0f
            ? juce::String ("OFF")
            : (data.filterCutoff >= 1000.0f
                ? juce::String (data.filterCutoff / 1000.0f, 1) + "k"
                : juce::String (juce::roundToInt (data.filterCutoff)) + "Hz"));
        drawRowPair (g, 5, detStr, fltrStr);
    }

    // ── Row 6:  A:xxxms  |  D:xxxms ──────────────────────────────────────────
    {
        juce::String atkStr = "A:" + formatMs (data.attackSec).trimEnd();
        juce::String decStr = "D:" + formatMs (data.decaySec).trimEnd();
        drawRowPair (g, 6, atkStr, decStr);
    }

    // ── Row 7:  S:xx%  |  R:xxxms ────────────────────────────────────────────
    {
        juce::String susStr = "S:" + juce::String (juce::roundToInt (data.sustainLevel * 100.0f)) + "%";
        juce::String relStr = "R:" + formatMs (data.releaseSec).trimEnd();
        drawRowPair (g, 7, susStr, relStr);
    }

    // ── Row 8:  BODY:+x.xst  |  ROOT:xxxHz ────────────────────────────────────
    {
        const float fmnt = data.formantSemitones;
        juce::String fmntStr = juce::String ("BODY:") + (fmnt >= 0.0f ? "+" : "")
                             + juce::String (fmnt, 1) + "st";
        juce::String tonalStr = "ROOT:" + (data.tonalityHz < 1.0f
                              ? juce::String ("OFF")
                              : juce::String (juce::roundToInt (data.tonalityHz)) + "Hz");
        drawRowPair (g, 8, fmntStr, tonalStr);
    }

    // ── Row 9:  RESO:x.xx  |  OUT:xx ────────────────────────────────────────
    {
        juce::String fresStr = "RESO:" + juce::String (data.filterRes, 2);
        juce::String outStr  = "OUT:"  + juce::String (data.outputBus + 1);
        drawRowPair (g, 9, fresStr, outStr);
    }

    // ── Floating flags — right-edge vertical column (always visible) ──────────
    drawFlagsRow (g, 6);

    g.restoreState();  // end clip region

}
