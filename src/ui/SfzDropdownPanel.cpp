// =============================================================================
//  SfzDropdownPanel.cpp  —  SF2 / SFZ instrument strip with inline file browser
// =============================================================================
#include "SfzDropdownPanel.h"
#include "DysektLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../PluginEditor.h"
#include <set>
#include <algorithm>

// ── Layout constants (header strip) ──────────────────────────────────────────
static constexpr int kKnobW        = 52;
static constexpr int kMeterW       = 60;
static constexpr int kPad          = 6;
static constexpr int kKnobGap      = 4;

// =============================================================================
//  SfzDropdownPanel — constructor / destructor
// =============================================================================

SfzDropdownPanel::SfzDropdownPanel (DysektProcessor& p)
    : processor (p)
{
    // ── SF2 program grid ──────────────────────────────────────────────────────
    // Left-click in the grid auditions the preset (handled by onPreviewToggled).
    // It must NOT close the grid — the grid stays open until the user explicitly
    // navigates away. Nulling onPresetSelected prevents the fallback close path
    // in Sf2ProgramGrid::mouseDown from firing.
    programGrid.onPresetSelected = nullptr;
    // ── Multitimbral channel assignment ──────────────────────────────────────
    // Right-click on a cell calls this with the preset index and the chosen
    // MIDI channel (1-16), or ch==0 to deactivate.
    //
    // Routing is strictly 1:1: incoming MIDI ch N plays FluidSynth channel N-1.
    // setPresetOnChannel() loads the correct preset onto each FluidSynth channel.
    // No fan-out mask is needed — each MIDI channel has exactly one destination.
    programGrid.onChannelChanged = [this] (int presetIdx, int ch)
    {
        if (presetIdx < 0 || presetIdx >= (int) presetList.size()) return;
        const auto& info = presetList[(size_t) presetIdx];

        // Collision check: refuse to assign a channel already owned by a chromatic
        // slice, or currently occupied by the SFZ-Player (sfzPlayer2).
        if (ch >= 1 && ch <= 16)
        {
            const uint32_t chromaMask = processor.chromaticSliceChannelMask.load (std::memory_order_relaxed);
            const uint32_t sfz2Mask   = processor.sfzPlayer2ChannelMask.load (std::memory_order_relaxed);
            if ((chromaMask | sfz2Mask) & (1u << ch))
                return;   // channel is slicer-owned or sfzPlayer2-owned — silently reject
        }

        if (ch == 0)
        {
            // Deactivate — silence all FluidSynth channels, then reload only the
            // still-assigned presets.  This is the safest way to remove one
            // entry without needing to track which channel it was on here.
            for (int c = 0; c < 16; ++c)
                processor.sfzPlayer.setPresetOnChannel (c, 0, 0);

            const auto& chMap = programGrid.getPresetChannels();
            for (auto& kv : chMap)
            {
                if (kv.first == presetIdx) continue;
                if (kv.second >= 1 && kv.second <= 16 && kv.first < (int)presetList.size())
                    processor.sfzPlayer.setPresetOnChannel (kv.second - 1,
                                                            presetList[(size_t)kv.first].bank,
                                                            presetList[(size_t)kv.first].preset);
            }

            if (onPresetChannelAssigned)
                onPresetChannelAssigned (info, 0);

            // Rebuild sfPlayerChannelMask from the presets that are still assigned.
            {
                uint32_t mask = 0u;
                const auto& chMap2 = programGrid.getPresetChannels();
                for (const auto& kv : chMap2)
                {
                    if (kv.first == presetIdx) continue; // this one is being removed
                    if (kv.second >= 1 && kv.second <= 16)
                        mask |= (1u << kv.second);
                }
                processor.sfPlayerChannelMask.store      (mask, std::memory_order_relaxed);
                processor.savedSfPlayerChannelMask.store (mask, std::memory_order_relaxed);
            }
        }
        else
        {
            // Assign: load preset onto FluidSynth channel (ch-1, 0-based).
            // A controller transmitting on MIDI ch N will route 1:1 to FluidSynth
            // channel N-1, so this preset will be played when the controller
            // sends on the assigned MIDI channel.
            processor.sfzPlayer.setPresetOnChannel (ch - 1, info.bank, info.preset);

            // Add this channel to sfPlayerChannelMask so MIDI on ch N reaches the SF player.
            {
                uint32_t mask = processor.sfPlayerChannelMask.load (std::memory_order_relaxed);
                mask |= (1u << ch);
                processor.sfPlayerChannelMask.store      (mask, std::memory_order_relaxed);
                processor.savedSfPlayerChannelMask.store (mask, std::memory_order_relaxed);
            }

            if (onPresetChannelAssigned)
                onPresetChannelAssigned (info, ch);
        }

        // sfPlayerChannelMask is now updated above on both assign and deactivate,
        // so MIDI routing stays in sync with the grid's channel assignments.
    };

    // ── Preview toggle: left-click radio ─────────────────────────────────────
    // index == -1 → deactivate preview; index >= 0 → audition on ch15.
    // If the preset already has a real channel assigned, left-click just
    // highlights it visually — no need to re-route anything.
    programGrid.onPreviewToggled = [this] (int idx)
    {
        if (idx < 0)
        {
            processor.sfzPlayer.clearPreview();
            return;
        }

        if (idx >= (int) presetList.size()) return;
        const auto& info = presetList[(size_t) idx];

        processor.sfzPlayer.setDisplayPresetIndex (idx);

        // Waveform LCD: sampleData3/previewZones3 only ever hold ONE preset's
        // rendered audio, and that used to always be whatever preset sfizz
        // defaulted to at file-load — never the preset actually clicked here.
        // Kick off a scoped re-render for THIS preset so Sf2WaveformLcd shows
        // the right waveform, regardless of whether it also gets a real MIDI
        // channel below. Dedupe against the last-requested preset so rapid
        // re-clicks on the same cell don't spam the background render pool.
        if (processor.sf2PreviewRequestedBank.load (std::memory_order_relaxed)    != info.bank ||
            processor.sf2PreviewRequestedProgram.load (std::memory_order_relaxed) != info.preset)
        {
            processor.sf2PreviewRequestedBank.store    (info.bank,   std::memory_order_relaxed);
            processor.sf2PreviewRequestedProgram.store (info.preset, std::memory_order_relaxed);
            processor.loadSoundFontAsync (processor.sfzPlayer.getLoadedFile(),
                                           SoundFontLoadTarget::SfPlayer,
                                           info.bank, info.preset);
        }

        // If this preset already has a real MIDI channel assigned, the audio
        // routing is already correct — just highlight it visually.
        const auto& chMap = programGrid.getPresetChannels();
        if (chMap.count (idx) && chMap.at (idx) >= 1)
            return;  // already routed on a real channel

        // Pure audition: load onto ch15 and route live input there.
        processor.sfzPlayer.previewPreset (info.bank, info.preset);
    };

    programGrid.onAssignedPresetClicked = [this] (int idx)
    {
        // Sync combo selection to the channel of the clicked preset
        if (idx < 0 || idx >= (int) presetList.size()) return;
        processor.sfzPlayer.setDisplayPresetIndex (idx);
        const auto& chMap = programGrid.getPresetChannels();
        if (! chMap.count (idx)) return;
        const int ch = chMap.at (idx);
        // channel-range spinners don't need per-preset sync
        juce::ignoreUnused (ch);
    };

    addChildComponent (programGrid);

    startTimerHz (30);
}

SfzDropdownPanel::~SfzDropdownPanel() = default;

// =============================================================================
//  Layout
// =============================================================================

void SfzDropdownPanel::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    // Strip order (left → right):
    // [CH range + FX knobs, spans full freed width] [REV] [PAN] [VOL] [METER]
    auto strip = juce::Rectangle<int> (0, 0, w, kStripH).reduced (kPad, 0);
    strip.removeFromLeft (4);   // left margin

    // Right-side knobs
    meterZone   = strip.removeFromRight (kMeterW);
    strip.removeFromRight (kPad);

    // MIDI activity LED — immediately left of VU meter
    {
        const int ledSize = juce::jmin (14, strip.getHeight() - 4);
        midiLedZone = strip.removeFromRight (ledSize + 4)
                          .withSizeKeepingCentre (ledSize, ledSize);
    }
    strip.removeFromRight (kPad);

    volZone    = strip.removeFromRight (kKnobW);
    strip.removeFromRight (kKnobGap);
    panZone    = strip.removeFromRight (kKnobW);
    strip.removeFromRight (kPad);

    rvSizeZone = strip.removeFromRight (kKnobW);
    strip.removeFromRight (kKnobGap);
    rvMixZone  = strip.removeFromRight (kKnobW);
    strip.removeFromRight (kPad);

    fineZone   = strip.removeFromRight (kKnobW);
    strip.removeFromRight (kKnobGap);
    transZone  = strip.removeFromRight (kKnobW);
    strip.removeFromRight (kPad);

    // The preset-picker label was removed (redundant with the LCD readout, and
    // its stepper wrote to a channel reserved for the Slicer). Whatever's left
    // of `strip` now runs all the way to the left margin, so the CH-range
    // spinner gets that whole freed span instead of being squeezed into just
    // the TRN+FINE slot.
    chComboZone = strip.getUnion (transZone)
                        .getUnion (fineZone)
                        .expanded (kKnobGap / 2, 0);

    // ── SF2 program grid overlay ──────────────────────────────────────────────
    if (programPickerOpen)
    {
        programGrid.setBounds (kPad, kStripH + 1, w - kPad * 2, h - kStripH - 1);
        programGrid.setVisible (true);
    }
    else
    {
        programGrid.setVisible (false);
        programGrid.setBounds ({});
    }

    // ── Channel-range spinner hit-zones ──────────────────────────────────
    // Laid out inside chComboZone (the TRN+FINE area when SF2 is loaded).
    // "CH [◂ 1 ▸] – [◂ 16 ▸]"
    {
        // Spinner centred inside the full chComboZone width (~336 px).
        // Large hit targets for easy clicking.
        const int btnW   = 28;   // ◂ / ▸ arrow
        const int numW   = 38;   // two-digit channel number + padding
        const int gap    = 10;
        const int sepW   = 28;   // " – " separator
        const int labelW = 30;   // "CH" prefix
        const int widgetW = labelW + gap + btnW + numW + btnW + gap + sepW + gap + btnW + numW + btnW;
        auto z = chComboZone.withSizeKeepingCentre (widgetW, chComboZone.getHeight());

        chRangeLabelZone = z.removeFromLeft (labelW);
        z.removeFromLeft (gap);

        chLowDec   = z.removeFromLeft (btnW);
        chLowLabel = z.removeFromLeft (numW);
        chLowInc   = z.removeFromLeft (btnW);
        z.removeFromLeft (gap);

        z.removeFromLeft (sepW);
        z.removeFromLeft (gap);

        chHighDec   = z.removeFromLeft (btnW);
        chHighLabel = z.removeFromLeft (numW);
        chHighInc   = z.removeFromLeft (btnW);
    }
}

// =============================================================================
//  Program grid open / close
// =============================================================================

void SfzDropdownPanel::openProgramGrid()
{
    if (programPickerOpen) return;
    programPickerOpen = true;

    programGrid.setPresets (presetList,
                            processor.sfzPlayer.getCurrentPresetIndex(),
                            processor.sfzPlayer.getMidiChannel());
    {
        const uint32_t mask = processor.sfPlayerChannelMask.load (std::memory_order_relaxed);
        int lo = 0, hi = 0;
        if (mask != 0)
        {
            for (int c = 2; c <= 16; ++c)  if (mask & (1u << c)) { lo = c; break; }
            for (int c = 16; c >= 2; --c)  if (mask & (1u << c)) { hi = c; break; }
        }
        programGrid.setChannelRange (lo, hi);
    }
    restoreGridChannelAssignments();
    resized();
    repaint();
}

void SfzDropdownPanel::closeProgramGrid()
{
    if (! programPickerOpen) return;
    programPickerOpen = false;

    // Stop any active preview: clear ch15 routing and reset the grid visual.
    processor.sfzPlayer.clearPreview();
    programGrid.clearPreviewState();

    resized();
    repaint();
}

void SfzDropdownPanel::restoreGridChannelAssignments()
{
    // Rebuild programGrid.presetChannels from sf2Presets (which survives reloads).
    // Without this, presets that were assigned in a previous session have current==0
    // in the grid, causing "Deactivate" to be permanently greyed out.
    std::unordered_map<int, int> chMap;
    for (const auto& ap : sf2Presets)
    {
        for (int i = 0; i < (int) presetList.size(); ++i)
        {
            if (presetList[(size_t) i].name == ap.name)
            {
                chMap[i] = ap.ch;
                break;
            }
        }
    }
    programGrid.setPresetChannels (chMap);
}

// =============================================================================
//  SF2 channel combo helpers
// =============================================================================
void SfzDropdownPanel::buildSf2Combo()
{
    // sf2ChCombo removed — channel routing is now via sfPlayerChannelMask (bitmask).
    // This method is retained as a no-op so callers (notifyPresetChannelChanged)
    // don't need changes.
}

void SfzDropdownPanel::notifyPresetChannelChanged (const juce::String& presetName,
                                                    int midiCh1Based)
{
    // midiCh1Based == 0 means the mapping was removed
    if (midiCh1Based == 0)
    {
        sf2Presets.erase (std::remove_if (sf2Presets.begin(), sf2Presets.end(),
            [&] (const AssignedPreset& ap) { return ap.name == presetName; }),
            sf2Presets.end());

        // If the removed channel was selected, clear selection
        const bool stillValid = std::any_of (sf2Presets.begin(), sf2Presets.end(),
            [&] (const AssignedPreset& ap) { return ap.ch == selectedSf2Ch + 1; });
        if (! stillValid) selectedSf2Ch = -1;
    }
    else
    {
        // Add or update
        bool found = false;
        for (auto& ap : sf2Presets)
        {
            if (ap.name == presetName) { ap.ch = midiCh1Based; found = true; break; }
        }
        if (! found)
            sf2Presets.push_back ({ presetName, midiCh1Based });
        if (selectedSf2Ch < 0)
            selectedSf2Ch = midiCh1Based - 1;
    }
    buildSf2Combo();
    resized();
    repaint();
}

void SfzDropdownPanel::onFileChosen (const juce::File& f)
{
    if (f.getFileExtension().toLowerCase() != ".sf2")
        return;   // SF2-PLAYER only accepts .sf2 — silently ignore anything else

    processor.sfzPlayer.loadFile (f, processor.fileLoadPool);

    // New file — any previously-requested/rendered preset bank/program is
    // now meaningless (could coincidentally match a preset number in THIS
    // file and wrongly skip a needed re-render below). Reset the dedupe
    // state; the load below (no preset override) renders this file's
    // default preset, same as -1/-1 always meant.
    processor.sf2PreviewRequestedBank.store    (-1, std::memory_order_relaxed);
    processor.sf2PreviewRequestedProgram.store (-1, std::memory_order_relaxed);
    processor.loadSoundFontAsync (f, SoundFontLoadTarget::SfPlayer);   // waveform preview -> sampleData3
    processor.sfPlayerChannelMask.store (1u << 3, std::memory_order_relaxed); // ch3 default
    openProgramGrid();
    repaint();

    if (onFileLoaded)
        onFileLoaded (f);
}

// =============================================================================
//  Paint
// =============================================================================

void SfzDropdownPanel::paint (juce::Graphics& g)
{
    const auto& theme = getTheme();
    const int   w     = getWidth();
    const int   h     = getHeight();

    // Full background
    {
        const auto bounds = getLocalBounds().toFloat();
        juce::ColourGradient bg (theme.darkBar.darker (0.35f), 0.f, 0.f,
                                  theme.darkBar.darker (0.10f), 0.f, (float) h, false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (bounds, 4.0f);

        const int sepY = kStripH;
        g.setColour (theme.accent.withAlpha (0.18f));
        g.fillRect (kPad, sepY, w - kPad * 2, 1);
    }

    drawHeaderStrip (g);
    drawSf2ChStrip (g);

    g.setColour (theme.accent.withAlpha (0.45f));
    g.fillRect (0, 0, w, 1);
}

// =============================================================================
// =============================================================================
//  drawSf2ChStrip  —  per-channel FX knobs shown instead of ADSR when SF2 loaded
// =============================================================================

void SfzDropdownPanel::drawSf2ChStrip (juce::Graphics& g) const
{
    const auto& theme = getTheme();
    const auto accent  = theme.accent;
    const auto dim     = theme.foreground.withAlpha (0.45f);
    const auto bright  = theme.foreground;

    // ── Per-channel FX knobs (mix/size/damp/gain) ─────────────────────────
    const float chMix  = processor.sfzPlayer.getReverbMix() / 100.0f;
    const float chSize = processor.sfzPlayer.getReverbSize() / 100.0f;
    const float chDamp = processor.sfzPlayer.getReverbDamp() / 100.0f;
    const float chGain = processor.sfzPlayer.getVolume();

    // MIX/SIZE/PAN/VOL already drawn by drawHeaderStrip() — do not duplicate here.

    // ── Channel-range spinner: CH [◂ lo ▸] – [◂ hi ▸] ───────────────────
    // Drawn inside chComboZone (the TRN+FINE area).  Hit-zones are set in resized().
    const int lo = cachedChLow;
    const int hi = cachedChHigh;
    const bool disabled = (lo == 0);

    // "CH" prefix
    g.setFont (DysektLookAndFeel::makeFont (11.f, true));
    g.setColour (dim);
    g.drawText ("CH", chRangeLabelZone, juce::Justification::centred, false);

    // Helper lambda: draw one spinner (dec button, number label, inc button)
    auto drawSpinner = [&](juce::Rectangle<int> decR,
                            juce::Rectangle<int> numR,
                            juce::Rectangle<int> incR,
                            int value)
    {
        // Subtle button backgrounds
        g.setColour ((disabled ? dim : accent).withAlpha (0.18f));
        g.fillRoundedRectangle (decR.toFloat(), 3.f);
        g.fillRoundedRectangle (incR.toFloat(), 3.f);

        // Arrows (plain ASCII so MSVC code page 1252 never chokes)
        g.setColour (disabled ? dim : accent);
        g.setFont (DysektLookAndFeel::makeFont (13.f));
        g.drawText ("<", decR, juce::Justification::centred, false);
        g.drawText (">", incR, juce::Justification::centred, false);

        // Value
        g.setColour (disabled ? dim : bright);
        g.setFont (DysektLookAndFeel::makeFont (14.f, true));
        const auto valStr = disabled ? juce::String ("--") : juce::String (value);
        g.drawText (valStr, numR, juce::Justification::centred, false);
    };

    drawSpinner (chLowDec,  chLowLabel,  chLowInc,  lo);

    // " - " separator
    const auto sepR = juce::Rectangle<int> (chLowInc.getRight(), chLowInc.getY(),
                                            chHighDec.getX() - chLowInc.getRight(),
                                            chLowInc.getHeight());
    g.setColour (dim);
    g.setFont (DysektLookAndFeel::makeFont (13.f));
    g.drawText ("-", sepR, juce::Justification::centred, false);

    drawSpinner (chHighDec, chHighLabel, chHighInc, hi);
}



void SfzDropdownPanel::drawHeaderStrip (juce::Graphics& g) const
{
    const auto& theme = getTheme();

    const auto& f = processor.sfzPlayer.getLoadedFile();
    const bool isSf2 = f.existsAsFile() && f.hasFileExtension (".sf2");

    // TRN + FINE are replaced by the sf2ChCombo widget when SF2 is loaded
    if (! isSf2)
    {
        drawKnob (g, transZone, transToNorm (processor.sfzPlayer.getTranspose()),
                  "TRN",
                  [&]() -> juce::String {
                      const int s = processor.sfzPlayer.getTranspose();
                      return s == 0 ? "0st" : (s > 0 ? "+" : "") + juce::String (s) + "st";
                  }());

        drawKnob (g, fineZone, fineToNorm (processor.sfzPlayer.getFineTune()),
                  "FINE",
                  [&]() -> juce::String {
                      const float c = processor.sfzPlayer.getFineTune();
                      return (c >= 0 ? "+" : "") + juce::String (c, 0) + "c";
                  }());
    }

    drawKnob (g, rvMixZone, processor.sfzPlayer.getReverbMix() / 100.0f,
              "MIX",
              juce::String (juce::roundToInt (processor.sfzPlayer.getReverbMix())) + "%");

    drawKnob (g, rvSizeZone, processor.sfzPlayer.getReverbSize() / 100.0f,
              "SIZE",
              juce::String (juce::roundToInt (processor.sfzPlayer.getReverbSize())) + "%");

    drawKnob (g, panZone, panToNorm (processor.sfzPlayer.getPan()),
              "PAN",
              [&]() -> juce::String {
                  const float p = processor.sfzPlayer.getPan();
                  if (std::abs (p) < 0.01f) return "C";
                  const int pct = juce::roundToInt (std::abs (p) * 100);
                  return (p < 0 ? "L" : "R") + juce::String (pct);
              }());

    drawKnob (g, volZone, volToNorm (processor.sfzPlayer.getVolume()),
              "VOL",
              [&]() -> juce::String {
                  const float db = juce::Decibels::gainToDecibels (processor.sfzPlayer.getVolume());
                  return db <= -95.f ? "-inf" : juce::String (db, 1) + "dB";
              }());

    drawMeter (g);

    // ── MIDI activity LED ─────────────────────────────────────────────────────
    if (! midiLedZone.isEmpty())
    {
        const juce::Colour ledColour = midiLedOn
            ? theme.accent.brighter (0.3f)
            : theme.darkBar.darker (0.2f);
        const juce::Colour borderColour = midiLedOn
            ? theme.accent
            : theme.foreground.withAlpha (0.15f);

        g.setColour (ledColour);
        g.fillEllipse (midiLedZone.toFloat());
        g.setColour (borderColour);
        g.drawEllipse (midiLedZone.toFloat().reduced (0.5f), 1.0f);

        g.setColour (theme.foreground.withAlpha (0.55f));
        g.setFont (juce::Font (7.0f));
        g.drawText ("M", midiLedZone.translated (0, midiLedZone.getHeight() + 1),
                    juce::Justification::centredTop, false);
    }
}

// =============================================================================
//  drawKnob
// =============================================================================

void SfzDropdownPanel::drawKnob (juce::Graphics& g, juce::Rectangle<int> bounds,
                                   float normalised, const juce::String& label,
                                   const juce::String& valueStr) const
{
    const auto& theme = getTheme();

    const int dia  = juce::jmin (bounds.getHeight() - 6, 26);
    const int cy   = bounds.getCentreY();
    const int cx   = bounds.getX() + 3 + dia / 2;
    const float r  = (float) dia * 0.5f;

    const float startA = juce::MathConstants<float>::pi * 1.25f;
    const float endA   = juce::MathConstants<float>::pi * 2.75f;
    const float angle  = startA + normalised * (endA - startA);

    juce::Path track;
    track.addCentredArc ((float) cx, (float) cy, r - 1.f, r - 1.f, 0.f, startA, endA, true);
    g.setColour (theme.darkBar.brighter (0.15f));
    g.strokePath (track, juce::PathStrokeType (2.0f));

    juce::Path fill;
    fill.addCentredArc ((float) cx, (float) cy, r - 1.f, r - 1.f, 0.f, startA, angle, true);
    g.setColour (theme.accent);
    g.strokePath (fill, juce::PathStrokeType (2.0f));

    const float tx = (float) cx + (r - 4.f) * std::cos (angle - juce::MathConstants<float>::halfPi);
    const float ty = (float) cy + (r - 4.f) * std::sin (angle - juce::MathConstants<float>::halfPi);
    g.setColour (theme.accent.brighter (0.3f));
    g.fillEllipse (tx - 2.f, ty - 2.f, 4.f, 4.f);

    const int textX = cx + (int) r + 5;
    const int textW = bounds.getRight() - textX;

    g.setFont (DysektLookAndFeel::makeFont (10.5f, true));
    g.setColour (theme.foreground.withAlpha (0.38f));
    g.drawText (label,    textX, cy - 10, textW, 10, juce::Justification::centredLeft, false);

    g.setFont (DysektLookAndFeel::makeFont (11.5f));
    g.setColour (theme.foreground.withAlpha (0.82f));
    g.drawText (valueStr, textX, cy,      textW, 10, juce::Justification::centredLeft, false);
}

// =============================================================================
//  drawMeter
// =============================================================================

void SfzDropdownPanel::drawMeter (juce::Graphics& g) const
{
    const auto& theme = getTheme();
    auto area = meterZone.reduced (2, 6);

    const int barW = area.getWidth() / 2 - 2;
    const int barH = area.getHeight();

    auto leftBar  = juce::Rectangle<int> (area.getX(),              area.getY(), barW, barH);
    auto rightBar = juce::Rectangle<int> (area.getX() + barW + 4,  area.getY(), barW, barH);

    g.setColour (theme.darkBar.darker (0.2f));
    g.fillRoundedRectangle (leftBar.toFloat(),  2.0f);
    g.fillRoundedRectangle (rightBar.toFloat(), 2.0f);

    auto drawBar = [&] (juce::Rectangle<int> bar, float peak, float hold)
    {
        const int fillH = juce::roundToInt ((float) bar.getHeight() * juce::jlimit (0.f, 1.f, peak));
        if (fillH > 0)
        {
            juce::ColourGradient grad (theme.accent.withAlpha (0.85f), 0.f, (float) bar.getBottom(),
                                        theme.accent.brighter (0.5f),  0.f, (float) bar.getY(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bar.withTrimmedTop (bar.getHeight() - fillH).toFloat(), 2.0f);
        }
        const int holdY = bar.getBottom() - juce::roundToInt ((float) bar.getHeight()
                           * juce::jlimit (0.f, 1.f, hold));
        g.setColour (theme.accent.brighter (0.6f).withAlpha (0.7f));
        g.fillRect (bar.getX(), holdY - 1, bar.getWidth(), 2);
    };

    drawBar (leftBar,  meterL, holdL);
    drawBar (rightBar, meterR, holdR);
}

// =============================================================================
//  Timer
// =============================================================================

void SfzDropdownPanel::timerCallback()
{
    const float newL = processor.sfzPeakL.load (std::memory_order_relaxed);
    const float newR = processor.sfzPeakR.load (std::memory_order_relaxed);
    if (newL > holdL) holdL = newL;
    if (newR > holdR) holdR = newR;
    holdL *= kHoldDecay;
    holdR *= kHoldDecay;
    meterL = newL;
    meterR = newR;

    // ── MIDI activity LED ─────────────────────────────────────────────────────
    const int activity = processor.sfzMidiActivity.load (std::memory_order_relaxed);
    const bool newLedOn = (activity > 0) || (midiLedHold > 0);
    if (activity > 0)
        midiLedHold = kMidiLedHoldTicks;
    else if (midiLedHold > 0)
        --midiLedHold;

    if (newLedOn != midiLedOn)
    {
        midiLedOn = newLedOn;
        repaint (midiLedZone);
    }

    presetList = processor.sfzPlayer.getPresetList();

    // onFileChosen() opens the program grid immediately after kicking off the
    // async SF2 load, so the grid can end up open with zero presets if
    // FluidSynth hasn't finished parsing the file yet. Catch it up here the
    // moment presets become available -- mirrors the retry logic already used
    // in panelDidShow().
    if (programPickerOpen && programGrid.getPresets().empty() && ! presetList.empty())
    {
        programGrid.setPresets (presetList,
                                processor.sfzPlayer.getCurrentPresetIndex(),
                                processor.sfzPlayer.getMidiChannel());
        restoreGridChannelAssignments();
    }

    // Poll sfPlayerChannelMask from processor for paint (avoids atomic reads in paint).
    // Derive lo/hi as the lowest and highest set channel bits for spinner display.
    // Channel 1 is hardwired to the slicer and never appears in sfPlayerChannelMask.
    {
        const uint32_t mask = processor.sfPlayerChannelMask.load (std::memory_order_relaxed);
        cachedChLow  = 0;
        cachedChHigh = 0;
        if (mask != 0)
        {
            for (int c = 2; c <= 16; ++c)  if (mask & (1u << c)) { cachedChLow  = c; break; }
            for (int c = 16; c >= 2; --c)  if (mask & (1u << c)) { cachedChHigh = c; break; }
        }
    }

    // Keep the program grid's range and blocked-channel set in sync so the
    // channel picker menu shows correct availability. Channels are blocked
    // if owned by a chromatic slice OR currently occupied by sfzPlayer2 —
    // both pools must stay mutually exclusive from SF2 assignment.
    programGrid.setChannelRange    (cachedChLow, cachedChHigh);
    programGrid.setBlockedChannels (
        processor.chromaticSliceChannelMask.load (std::memory_order_relaxed)
        | processor.sfzPlayer2ChannelMask.load (std::memory_order_relaxed));

    repaint();
}

// =============================================================================
//  MIDI Learn menu
// =============================================================================

void SfzDropdownPanel::showMidiLearnMenu (int fieldId, juce::Point<int> screenPos)
{
    const bool mapped = processor.midiLearn.isMapped (fieldId);
    juce::PopupMenu menu;
    menu.addItem (1, "Learn MIDI CC");
    if (mapped)
        menu.addItem (2, "Clear (" + processor.midiLearn.getLabelText (fieldId) + ")");
    menu.addSeparator();
    menu.addItem (1000, "Open MIDI Learn Dialog...");

    auto* topLvl = getTopLevelComponent();
    float ms = DysektLookAndFeel::getMenuScale();
    menu.showMenuAsync (
        juce::PopupMenu::Options()
            .withTargetScreenArea (juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1))
            .withParentComponent (topLvl)
            .withStandardItemHeight ((int)(24 * ms)),
        [this, fieldId] (int result)
        {
            if (result == 1)      { processor.midiLearn.armLearn (fieldId);     repaint(); }
            else if (result == 2) { processor.midiLearn.clearMapping (fieldId); repaint(); }
            else if (result == 1000)
            {
                if (auto* editor = findParentComponentOfClass<DysektEditor>())
                    editor->keyPressed (juce::KeyPress ('M', juce::ModifierKeys(), 0));
            }
        });
}

// =============================================================================
//  Mouse events
// =============================================================================

void SfzDropdownPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();

    // ── Channel-range spinners (visible in SF2 strip) ─────────────────────
    auto adjustChannel = [&](bool isLow, int delta)
    {
        // Derive current lo/hi from sfPlayerChannelMask for spinner display.
        const uint32_t curMask = processor.sfPlayerChannelMask.load (std::memory_order_relaxed);
        int lo = 0, hi = 0;
        if (curMask != 0)
        {
            for (int c = 2; c <= 16; ++c)  if (curMask & (1u << c)) { lo = c; break; }
            for (int c = 16; c >= 2; --c)  if (curMask & (1u << c)) { hi = c; break; }
        }
        if (lo == 0) lo = 2;   // channel 1 is hardwired to the slicer; SF player starts at 2
        if (hi == 0) hi = lo;

        // Channels owned by chromatic slices, or currently occupied by the
        // SFZ-Player (sfzPlayer2), are not available to the SF2 player.
        const uint32_t chromaMask = processor.chromaticSliceChannelMask.load (std::memory_order_relaxed);
        const uint32_t sfz2Mask   = processor.sfzPlayer2ChannelMask.load (std::memory_order_relaxed);
        const uint32_t reservedMask = chromaMask | sfz2Mask;
        // Channel 1 is also never available to the SF player.
        auto isFree = [&](int ch) -> bool
        {
            if (ch < 2 || ch > 16) return false;
            return ! (reservedMask & (1u << ch));
        };

        if (isLow)
        {
            int newLo = juce::jlimit (2, hi, lo + delta);
            while (newLo >= 2 && newLo <= hi && ! isFree (newLo))
                newLo += delta > 0 ? 1 : -1;
            newLo = juce::jlimit (2, hi, newLo);
            if (isFree (newLo))
            {
                // Build mask for [newLo, hi] but skip chromatic-owned holes so they
                // are never included even when they fall inside the lo–hi range.
                uint32_t mask = 0u;
                for (int c = newLo; c <= hi; ++c)
                    if (isFree (c)) mask |= (1u << c);
                processor.sfPlayerChannelMask.store      (mask, std::memory_order_relaxed);
                processor.savedSfPlayerChannelMask.store (mask, std::memory_order_relaxed);
            }
        }
        else
        {
            int newHi = juce::jlimit (lo, 16, hi + delta);
            while (newHi >= lo && newHi <= 16 && ! isFree (newHi))
                newHi += delta > 0 ? 1 : -1;
            newHi = juce::jlimit (lo, 16, newHi);
            if (isFree (newHi))
            {
                // Build mask for [lo, newHi] but skip chromatic-owned holes.
                uint32_t mask = 0u;
                for (int c = lo; c <= newHi; ++c)
                    if (isFree (c)) mask |= (1u << c);
                processor.sfPlayerChannelMask.store      (mask, std::memory_order_relaxed);
                processor.savedSfPlayerChannelMask.store (mask, std::memory_order_relaxed);
            }
        }
        repaint();
    };

    if (chLowDec .contains (pos)) { adjustChannel (true,  -1); return; }
    if (chLowInc .contains (pos)) { adjustChannel (true,  +1); return; }
    if (chHighDec.contains (pos)) { adjustChannel (false, -1); return; }
    if (chHighInc.contains (pos)) { adjustChannel (false, +1); return; }

    // ── Right-click — MIDI Learn menu on knobs ────────────────────────────────
    if (e.mods.isRightButtonDown())
    {
        // Right-click on any knob → MIDI Learn menu
        using F = DysektProcessor::SliceParamField;
        struct { juce::Rectangle<int>& zone; int fieldId; } knobFields[] =
        {
            { volZone,     F::FieldSfzVol        },
            { transZone,   F::FieldSfzTranspose   },
            { panZone,     F::FieldSfzPan          },
            { fineZone,    F::FieldSfzFineTune     },
            { rvMixZone,   F::FieldSfzReverbMix    },
            { rvSizeZone,  F::FieldSfzReverbSize   },
        };
        for (auto& kf : knobFields)
        {
            if (kf.zone.contains (pos))
            {
                showMidiLearnMenu (kf.fieldId, e.getScreenPosition());
                return;
            }
        }
        return;
    }

    // ── Knob drag start ───────────────────────────────────────────────────────
    {
        const int selCh = selectedSf2Ch >= 0 ? selectedSf2Ch + 1 : 0;
        struct { juce::Rectangle<int>& zone; ActiveKnob id; float val; } knobs[] =
        {
            { volZone,     ActiveKnob::Volume,      volToNorm   (processor.sfzPlayer.getVolume()) },
            { transZone,   ActiveKnob::Transpose,   transToNorm (processor.sfzPlayer.getTranspose()) },
            { panZone,     ActiveKnob::Pan,         panToNorm   (processor.sfzPlayer.getPan()) },
            { fineZone,    ActiveKnob::FineTune,    fineToNorm  (processor.sfzPlayer.getFineTune()) },
            { rvMixZone,   ActiveKnob::ReverbMix,   processor.sfzPlayer.getReverbMix()  / 100.0f },
            { rvSizeZone,  ActiveKnob::ReverbSize,  processor.sfzPlayer.getReverbSize() / 100.0f },
            // Per-channel SF2 FX knobs (only active when a ch is selected)
            { chMixZone,  ActiveKnob::ChReverbMix,  selCh > 0 ? processor.sfzPlayer.getReverbMix() / 100.0f : 0.f },
            { chSizeZone, ActiveKnob::ChReverbSize, selCh > 0 ? processor.sfzPlayer.getReverbSize() / 100.0f : 0.f },
            { chDampZone, ActiveKnob::ChReverbDamp, selCh > 0 ? processor.sfzPlayer.getReverbDamp() / 100.0f : 0.f },
            { chGainZone, ActiveKnob::ChGain,       selCh > 0 ? juce::jlimit (0.f, 1.f, processor.sfzPlayer.getVolume() / 2.0f) : 0.5f },
        };

        for (auto& k : knobs)
        {
            if (k.zone.contains (pos))
            {
                activeKnob   = k.id;
                dragStartY   = pos.y;
                dragStartVal = k.val;
                return;
            }
        }
    }
}

void SfzDropdownPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (activeKnob == ActiveKnob::None) return;
    const float delta   = (float)(dragStartY - e.getPosition().y) / 120.0f;
    const float newNorm = juce::jlimit (0.f, 1.f, dragStartVal + delta);

    switch (activeKnob)
    {
        case ActiveKnob::Volume:      processor.sfzPlayer.setVolume    (normToVol   (newNorm)); break;
        case ActiveKnob::Transpose:   processor.sfzPlayer.setTranspose (normToTrans (newNorm)); break;
        case ActiveKnob::Pan:         processor.sfzPlayer.setPan       (normToPan   (newNorm)); break;
        case ActiveKnob::FineTune:    processor.sfzPlayer.setFineTune  (normToFine  (newNorm)); break;
        case ActiveKnob::ReverbMix:   processor.sfzPlayer.setReverbMix  (newNorm * 100.0f);     break;
        case ActiveKnob::ReverbSize:  processor.sfzPlayer.setReverbSize (newNorm * 100.0f);     break;
        case ActiveKnob::ChReverbMix:
        case ActiveKnob::ChReverbSize:
        case ActiveKnob::ChReverbDamp:
        case ActiveKnob::ChGain:
        {
            const int selCh = selectedSf2Ch >= 0 ? selectedSf2Ch + 1 : 0;
            if (selCh > 0)
            {
                if      (activeKnob == ActiveKnob::ChReverbMix)  processor.sfzPlayer.setReverbMix  (newNorm * 100.0f);
                else if (activeKnob == ActiveKnob::ChReverbSize) processor.sfzPlayer.setReverbSize (newNorm * 100.0f);
                else if (activeKnob == ActiveKnob::ChReverbDamp) processor.sfzPlayer.setReverbDamp (newNorm * 100.0f);
                else if (activeKnob == ActiveKnob::ChGain)       processor.sfzPlayer.setVolume     (newNorm * 2.0f);
            }
            break;
        }
        default: break;
    }
    repaint();
}

void SfzDropdownPanel::mouseUp (const juce::MouseEvent&)
{
    activeKnob = ActiveKnob::None;
}

void SfzDropdownPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (volZone.contains    (pos)) { processor.sfzPlayer.setVolume    (1.0f);  repaint(); }
    if (transZone.contains  (pos)) { processor.sfzPlayer.setTranspose (0);     repaint(); }
    if (panZone.contains    (pos)) { processor.sfzPlayer.setPan       (0.0f);  repaint(); }
    if (fineZone.contains   (pos)) { processor.sfzPlayer.setFineTune  (0.0f);  repaint(); }
    if (rvMixZone.contains  (pos)) { processor.sfzPlayer.setReverbMix  (0.0f);   repaint(); }
    if (rvSizeZone.contains (pos)) { processor.sfzPlayer.setReverbSize (50.0f);  repaint(); }
    // Ch-FX defaults
    {
        const int selCh = selectedSf2Ch >= 0 ? selectedSf2Ch + 1 : 0;
        if (selCh > 0)
        {
            if (chMixZone.contains  (pos)) { processor.sfzPlayer.setReverbMix  (0.0f);   repaint(); }
            if (chSizeZone.contains (pos)) { processor.sfzPlayer.setReverbSize (50.0f);  repaint(); }
            if (chDampZone.contains (pos)) { processor.sfzPlayer.setReverbDamp (50.0f);  repaint(); }
            if (chGainZone.contains (pos)) { processor.sfzPlayer.setVolume     (1.0f);   repaint(); }
        }
    }
}

void SfzDropdownPanel::mouseWheelMove (const juce::MouseEvent& e,
                                        const juce::MouseWheelDetails& w)
{
    if (programPickerOpen) return;

    const auto  pos  = e.getPosition();
    const float step = w.deltaY * (e.mods.isShiftDown() ? 0.01f : 0.05f);

    auto adjustNorm = [&] (float current, float s) {
        return juce::jlimit (0.f, 1.f, current + s);
    };

    if (volZone.contains (pos))
        processor.sfzPlayer.setVolume (normToVol (adjustNorm (volToNorm (processor.sfzPlayer.getVolume()), step)));
    else if (transZone.contains (pos))
        processor.sfzPlayer.setTranspose (normToTrans (adjustNorm (transToNorm (processor.sfzPlayer.getTranspose()), step)));
    else if (panZone.contains (pos))
        processor.sfzPlayer.setPan (normToPan (adjustNorm (panToNorm (processor.sfzPlayer.getPan()), step)));
    else if (fineZone.contains (pos))
        processor.sfzPlayer.setFineTune (normToFine (adjustNorm (fineToNorm (processor.sfzPlayer.getFineTune()), step)));
    else if (rvMixZone.contains (pos))
        processor.sfzPlayer.setReverbMix  (juce::jlimit (0.0f, 100.0f, processor.sfzPlayer.getReverbMix()  + step * 100.0f));
    else if (rvSizeZone.contains (pos))
        processor.sfzPlayer.setReverbSize (juce::jlimit (0.0f, 100.0f, processor.sfzPlayer.getReverbSize() + step * 100.0f));

    repaint();
}

// =============================================================================
//  File drag-and-drop
// =============================================================================

bool SfzDropdownPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).getFileExtension().toLowerCase() == ".sf2")
            return true;
    return false;
}

void SfzDropdownPanel::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
    {
        juce::File file (f);
        if (file.getFileExtension().toLowerCase() == ".sf2")
        {
            onFileChosen (file);
            return;
        }
    }
}

// =============================================================================
//  panelDidShow
// =============================================================================

void SfzDropdownPanel::panelDidShow()
{
    presetList = processor.sfzPlayer.getPresetList();

    if (programPickerOpen)
    {
        programGrid.setPresets (presetList,
                                processor.sfzPlayer.getCurrentPresetIndex(),
                                processor.sfzPlayer.getMidiChannel());
        restoreGridChannelAssignments();
    }

    if (processor.sfzPlayer.isLoaded())
    {
        // Only open the program grid once presets have loaded.
        // If presetList is still empty (FluidSynth async not done), the
        // PluginEditor timer will call panelDidShow() again.
        if (! programPickerOpen && ! presetList.empty())
            openProgramGrid();
    }

    resized();
    repaint();
}

// ── Knob normalisation helpers ────────────────────────────────────────────────
float SfzDropdownPanel::volToNorm   (float linear) const { return juce::jlimit (0.f, 1.f, linear * 0.5f); }
float SfzDropdownPanel::normToVol   (float n)      const { return n * 2.0f; }
float SfzDropdownPanel::transToNorm (int semi)     const { return ((float) semi + 24.0f) / 48.0f; }
int   SfzDropdownPanel::normToTrans (float n)      const { return juce::roundToInt (n * 48.0f - 24.0f); }
float SfzDropdownPanel::panToNorm   (float p)      const { return (p + 1.0f) * 0.5f; }
float SfzDropdownPanel::normToPan   (float n)      const { return n * 2.0f - 1.0f; }
float SfzDropdownPanel::fineToNorm  (float cents)  const { return (cents + 100.0f) / 200.0f; }
float SfzDropdownPanel::normToFine  (float n)      const { return n * 200.0f - 100.0f; }
