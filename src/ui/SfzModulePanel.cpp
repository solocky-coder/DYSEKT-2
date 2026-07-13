// =============================================================================
//  SfzModulePanel.cpp
// =============================================================================
#include "SfzModulePanel.h"
#include "DysektLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../PluginEditor.h"
#include <set>
#include <cmath>

// ── Constants ──────────────────────────────────────────────────────────────────
static constexpr int kKnobW    = 60;
static constexpr int kKnobH    = 70;
static constexpr int kChBtnW   = 40;
static constexpr int kMeterW   = 80;
static constexpr int kPad      = 8;
static constexpr int kLoadBtnW = 72;
static constexpr int kLoadBtnH = 26;

// ── Constructor / destructor ──────────────────────────────────────────────────

SfzModulePanel::SfzModulePanel (DysektProcessor& p)
    : processor (p),
      keysPanel (p)
{
    addAndMakeVisible (keysPanel);

    fileBrowser.onFileChosen = [this] (const juce::File& f) { onSampleChosen (f); };
    fileBrowser.onDismiss    = [this]
    {
        fileBrowser.setMode (SfzFileBrowser::Mode::kSfz);
        closeBrowser();
    };
    addChildComponent (fileBrowser);

    // [+ ZONE] always visible — openAddZoneChooser() creates a Custom.sfz if nothing is loaded
    keysPanel.setAddZoneButtonVisible (true);
    keysPanel.onAddZoneRequested = [this] { openAddZoneChooser(); };

    startTimerHz (30);
}

SfzModulePanel::~SfzModulePanel() = default;

// ── Layout ────────────────────────────────────────────────────────────────────

void SfzModulePanel::resized()
{
    auto area = getLocalBounds().reduced (kPad);

    // Bottom portion — keyboard (zone table + piano)
    const int kbH = juce::jmax (90, (area.getHeight() * 3) / 5);
    auto kbArea = area.removeFromBottom (kbH);
    keysPanel.setBounds (kbArea);
    area.removeFromBottom (4); // gap

    // ADSR row — sits between top strip and keyboard
    constexpr int kAdsrRowH  = 68;
    constexpr int kAdsrKnobW = 52;
    auto adsrArea = area.removeFromBottom (kAdsrRowH);
    area.removeFromBottom (4); // gap above ADSR row

    // Centre the four knobs in the ADSR row
    const int totalKnobW = kAdsrKnobW * 4 + kPad * 3;
    const int adsrX = adsrArea.getX() + (adsrArea.getWidth() - totalKnobW) / 2;
    atkZone = juce::Rectangle<int> (adsrX,                               adsrArea.getY(), kAdsrKnobW, kAdsrRowH);
    decZone = juce::Rectangle<int> (adsrX + (kAdsrKnobW + kPad),        adsrArea.getY(), kAdsrKnobW, kAdsrRowH);
    susZone = juce::Rectangle<int> (adsrX + (kAdsrKnobW + kPad) * 2,   adsrArea.getY(), kAdsrKnobW, kAdsrRowH);
    relZone = juce::Rectangle<int> (adsrX + (kAdsrKnobW + kPad) * 3,   adsrArea.getY(), kAdsrKnobW, kAdsrRowH);

    // Reverb row — sits between ADSR row and keyboard
    constexpr int kRevRowH  = 68;
    constexpr int kRevKnobW = 52;
    auto revArea = area.removeFromBottom (kRevRowH);
    area.removeFromBottom (4); // gap above reverb row

    const int totalRevW = kRevKnobW * 5 + kPad * 4;
    const int revX = revArea.getX() + (revArea.getWidth() - totalRevW) / 2;
    rvSizeZone   = juce::Rectangle<int> (revX,                               revArea.getY(), kRevKnobW, kRevRowH);
    rvDampZone   = juce::Rectangle<int> (revX + (kRevKnobW + kPad),         revArea.getY(), kRevKnobW, kRevRowH);
    rvWidthZone  = juce::Rectangle<int> (revX + (kRevKnobW + kPad) * 2,     revArea.getY(), kRevKnobW, kRevRowH);
    rvMixZone    = juce::Rectangle<int> (revX + (kRevKnobW + kPad) * 3,     revArea.getY(), kRevKnobW, kRevRowH);
    rvFreezeZone = juce::Rectangle<int> (revX + (kRevKnobW + kPad) * 4,     revArea.getY(), kRevKnobW, kRevRowH);

    // Top control strip — left to right:
    //   [LOAD] [VOL knob] [TRANS knob] [name label ... ] [meter] [status]

    // LOAD button — leftmost
    loadBtnZone = area.removeFromLeft (kLoadBtnW).withSizeKeepingCentre (kLoadBtnW, kLoadBtnH);
    area.removeFromLeft (4);

    // SAVE AS button — visible only when a custom SFZ is active
    constexpr int kSaveBtnW = 56;
    const bool showSaveAs = processor.sfzPlayer.isLoaded() &&
                            processor.sfzPlayer.getLoadedFile()
                                .getFileExtension().toLowerCase() == ".sfz";
    if (showSaveAs)
    {
        saveAsBtnZone = area.removeFromLeft (kSaveBtnW).withSizeKeepingCentre (kSaveBtnW, kLoadBtnH);
        area.removeFromLeft (kPad);
    }
    else
    {
        saveAsBtnZone = {};
        area.removeFromLeft (kPad);
    }

    // VOL knob
    constexpr int kKnobWNarrow = 48;
    volZone   = area.removeFromLeft (kKnobWNarrow);
    area.removeFromLeft (4);
    transZone = area.removeFromLeft (kKnobWNarrow);
    area.removeFromLeft (kPad);

    // Status pill — rightmost
    statusZone = area.removeFromRight (60);
    area.removeFromRight (4);

    // VU meter — second from right
    meterZone = area.removeFromRight (kMeterW);
    area.removeFromRight (kPad);

    // MIDI activity LED — immediately left of VU meter (square, centred vertically)
    {
        const int ledSize = juce::jmin (14, area.getHeight() - 4);
        midiLedZone = area.removeFromRight (ledSize + 4)
                          .withSizeKeepingCentre (ledSize, ledSize);
    }
    area.removeFromRight (kPad);

    chZone   = {};
    nameZone = area;

    // File browser overlays the entire panel when visible
    if (browserOpen)
        fileBrowser.setBounds (getLocalBounds());
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void SfzModulePanel::paint (juce::Graphics& g)
{
    const auto& theme = getTheme();
    const float sf = (float) getWidth() / 1114.0f;  // approx kBaseW minus margins

    // ── Background ────────────────────────────────────────────────────────────
    // Border is drawn by the editor's paintOverChildren (same CRT-frame recipe
    // as the waveform / pad-grid panels) so we only fill the background here.
    auto bounds = getLocalBounds().toFloat();
    g.setColour (theme.darkBar.darker (0.25f));
    g.fillRoundedRectangle (bounds, 4.0f);

    // ── Header label ─────────────────────────────────────────────────────────
    {
        auto lbl = nameZone.reduced (4, 0);
        g.setFont (DysektLookAndFeel::makeFont (10.0f));
        g.setColour (theme.foreground.withAlpha (0.40f));
        g.drawText ("SFZ / SF2", lbl, juce::Justification::topLeft, false);

        g.setFont (DysektLookAndFeel::makeFont (12.0f));
        const bool isLoaded = processor.sfzPlayer.isLoaded();
        if (isLoaded)
        {
            g.setColour (theme.foreground);
            const auto name = processor.sfzPlayer.getLoadedFile()
                                .getFileNameWithoutExtension();
            g.drawText (name, lbl.withTrimmedTop (14), juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (theme.foreground.withAlpha (0.30f));
            g.drawText ("No instrument loaded", lbl.withTrimmedTop (14),
                        juce::Justification::centredLeft, false);
        }
    }

    // ── Load button ───────────────────────────────────────────────────────────
    {
        const auto btn = loadBtnZone.toFloat();
        const bool hover = loadBtnZone.contains (getMouseXYRelative());
        g.setColour (hover ? theme.accent.withAlpha (0.25f)
                           : theme.darkBar.brighter (0.05f));
        g.fillRoundedRectangle (btn, 3.0f);
        g.setColour (theme.accent.withAlpha (hover ? 0.85f : 0.55f));
        g.drawRoundedRectangle (btn.reduced (0.5f), 3.0f, 1.0f);

        g.setFont (DysektLookAndFeel::makeFont (11.0f));
        g.setColour (theme.accent);
        g.drawText ("LOAD", loadBtnZone, juce::Justification::centred, false);
    }

    // ── Save As button (SFZ mode only) ────────────────────────────────────────
    if (! saveAsBtnZone.isEmpty())
    {
        const auto btn   = saveAsBtnZone.toFloat();
        const bool hover = saveAsBtnZone.contains (getMouseXYRelative());
        g.setColour (hover ? theme.accent.withAlpha (0.18f)
                           : theme.darkBar.brighter (0.03f));
        g.fillRoundedRectangle (btn, 3.0f);
        g.setColour (theme.accent.withAlpha (hover ? 0.70f : 0.38f));
        g.drawRoundedRectangle (btn.reduced (0.5f), 3.0f, 1.0f);

        g.setFont (DysektLookAndFeel::makeFont (9.5f));
        g.setColour (theme.accent.withAlpha (hover ? 0.95f : 0.65f));
        g.drawText ("SAVE AS", saveAsBtnZone, juce::Justification::centred, false);
    }

    {
        const float vol  = processor.sfzPlayer.getVolume();
        const float norm = volToNorm (vol);
        const float db   = (vol > 0.001f) ? juce::Decibels::gainToDecibels (vol) : -96.0f;
        const auto  str  = (db <= -95.f) ? juce::String ("-inf")
                                          : juce::String (db, 1) + " dB";
        drawKnob (g, volZone, norm, "VOL", str);
    }

    // ── Transpose knob ────────────────────────────────────────────────────────
    {
        const int  semi = processor.sfzPlayer.getTranspose();
        const float norm = transToNorm (semi);
        const auto  str  = (semi == 0) ? juce::String ("0 st")
                                       : (semi > 0 ? "+" : "") + juce::String (semi) + " st";
        drawKnob (g, transZone, norm, "TRANS", str);
    }

    // ── Status pill ───────────────────────────────────────────────────────────
    {
        const bool loaded = processor.sfzPlayer.isLoaded();
        auto pill = statusZone.withSizeKeepingCentre (52, 18).toFloat();
        g.setColour (loaded ? theme.accent.withAlpha (0.20f)
                            : theme.foreground.withAlpha (0.08f));
        g.fillRoundedRectangle (pill, 9.0f);
        g.setColour (loaded ? theme.accent : theme.foreground.withAlpha (0.30f));
        g.drawRoundedRectangle (pill.reduced (0.5f), 9.0f, 1.0f);
        g.setFont (DysektLookAndFeel::makeFont (10.0f));
        g.drawText (loaded ? "READY" : "EMPTY", pill.toNearestInt(), juce::Justification::centred, false);
    }

    // ── VU meter ─────────────────────────────────────────────────────────────
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

        // Tiny "M" label below LED
        g.setColour (theme.foreground.withAlpha (0.55f));
        g.setFont (juce::Font (7.0f));
        g.drawText ("M", midiLedZone.translated (0, midiLedZone.getHeight() + 1),
                    juce::Justification::centredTop, false);
    }

    // ── ADSR row background ───────────────────────────────────────────────────
    {
        // Span all four knob zones
        auto rowBounds = atkZone.getUnion (relZone).expanded (kPad / 2, 2).toFloat();
        g.setColour (theme.darkBar.brighter (0.04f));
        g.fillRoundedRectangle (rowBounds, 3.0f);
        g.setColour (theme.foreground.withAlpha (0.07f));
        g.drawRoundedRectangle (rowBounds.reduced (0.5f), 3.0f, 1.0f);
    }

    // ── ADSR knobs ───────────────────────────────────────────────────────────
    {
        const bool sfzLoaded = processor.sfzPlayer.isLoaded();
        const float alpha    = sfzLoaded ? 1.0f : 0.35f;

        auto fmtTime = [] (float sec) -> juce::String
        {
            if (sec < 1.0f) return juce::String (juce::roundToInt (sec * 1000.0f)) + " ms";
            return juce::String (sec, 2) + " s";
        };

        // Normalise to 0-1 for drawKnob arc
        const float atkNorm = processor.sfzPlayer.getSfzAttack()  / 30.0f;
        const float decNorm = processor.sfzPlayer.getSfzDecay()   / 30.0f;
        const float susNorm = processor.sfzPlayer.getSfzSustain() / 100.0f;
        const float relNorm = processor.sfzPlayer.getSfzRelease() / 60.0f;

        // Draw each knob — highlight if MIDI-learned
        auto drawAdsrKnob = [&] (juce::Rectangle<int> zone, float norm,
                                  const juce::String& label, const juce::String& valStr,
                                  int fieldId)
        {
            const bool learned = processor.midiLearn.isMapped (fieldId);
            const bool armed   = processor.midiLearn.getArmedSlot() == fieldId;
            g.saveState();
            g.setOpacity (alpha);
            drawKnob (g, zone, norm, label, valStr);
            g.restoreState();
            // Accent ring if learned or armed
            if (learned || armed)
            {
                const auto& th = getTheme();
                const auto  rc = zone.toFloat().reduced (2.0f);
                g.setColour ((armed ? th.accent.brighter (0.4f) : th.accent).withAlpha (0.55f));
                g.drawRoundedRectangle (rc, 3.0f, 1.5f);
            }
        };

        drawAdsrKnob (atkZone, atkNorm, "ATK", fmtTime (processor.sfzPlayer.getSfzAttack()),  DysektProcessor::FieldSfzAttack);
        drawAdsrKnob (decZone, decNorm, "DEC", fmtTime (processor.sfzPlayer.getSfzDecay()),   DysektProcessor::FieldSfzDecay);
        drawAdsrKnob (susZone, susNorm, "SUS", juce::String (juce::roundToInt (processor.sfzPlayer.getSfzSustain())) + "%", DysektProcessor::FieldSfzSustain);
        drawAdsrKnob (relZone, relNorm, "REL", fmtTime (processor.sfzPlayer.getSfzRelease()), DysektProcessor::FieldSfzRelease);
    }

    // ── Reverb row ────────────────────────────────────────────────────────────
    {
        auto rowBounds = rvSizeZone.getUnion (rvFreezeZone).expanded (kPad / 2, 2).toFloat();
        g.setColour (theme.darkBar.brighter (0.04f));
        g.fillRoundedRectangle (rowBounds, 3.0f);
        g.setColour (theme.foreground.withAlpha (0.07f));
        g.drawRoundedRectangle (rowBounds.reduced (0.5f), 3.0f, 1.0f);

        const float alpha = processor.sfzPlayer.isLoaded() ? 1.0f : 0.35f;

        auto drawRevKnob = [&] (juce::Rectangle<int> zone, float norm,
                                 const juce::String& label, const juce::String& valStr,
                                 int fieldId)
        {
            const bool learned = processor.midiLearn.isMapped (fieldId);
            const bool armed   = processor.midiLearn.getArmedSlot() == fieldId;
            g.saveState();
            g.setOpacity (alpha);
            drawKnob (g, zone, norm, label, valStr);
            g.restoreState();
            if (learned || armed)
            {
                const auto& th = getTheme();
                const auto  rc = zone.toFloat().reduced (2.0f);
                g.setColour ((armed ? th.accent.brighter (0.4f) : th.accent).withAlpha (0.55f));
                g.drawRoundedRectangle (rc, 3.0f, 1.5f);
            }
        };

        const float rvSize  = processor.sfzPlayer.getReverbSize();
        const float rvDamp  = processor.sfzPlayer.getReverbDamp();
        const float rvWidth = processor.sfzPlayer.getReverbWidth();
        const float rvMix   = processor.sfzPlayer.getReverbMix();
        const bool  rvFrz   = processor.sfzPlayer.getReverbFreeze();

        drawRevKnob (rvSizeZone,   rvSize  / 100.0f, "SIZE",  juce::String (juce::roundToInt (rvSize))  + "%", DysektProcessor::FieldSfzReverbSize);
        drawRevKnob (rvDampZone,   rvDamp  / 100.0f, "DAMP",  juce::String (juce::roundToInt (rvDamp))  + "%", DysektProcessor::FieldSfzReverbDamp);
        drawRevKnob (rvWidthZone,  rvWidth / 100.0f, "WIDTH", juce::String (juce::roundToInt (rvWidth)) + "%", DysektProcessor::FieldSfzReverbWidth);
        drawRevKnob (rvMixZone,    rvMix   / 100.0f, "MIX",   juce::String (juce::roundToInt (rvMix))   + "%", DysektProcessor::FieldSfzReverbMix);
        // Freeze — draws as a binary toggle (0 or 1 normalised)
        drawRevKnob (rvFreezeZone, rvFrz ? 1.0f : 0.0f, "FRZ", rvFrz ? "ON" : "OFF", DysektProcessor::FieldSfzReverbFreeze);
    }

    // ── Drop-target hint when dragging over ───────────────────────────────────
    if (isCurrentlyBlockedByAnotherModalComponent())
    {
        g.setColour (theme.accent.withAlpha (0.12f));
        g.fillRoundedRectangle (bounds, 4.0f);
    }
}

// ── Knob drawing helper ───────────────────────────────────────────────────────

void SfzModulePanel::drawKnob (juce::Graphics& g, juce::Rectangle<int> bounds,
                                 float normalised, const juce::String& label,
                                 const juce::String& valueStr) const
{
    const auto& theme = getTheme();
    const int   cx    = bounds.getCentreX();
    const int   dia   = juce::jmin (bounds.getWidth(), 40);
    const int   cy    = bounds.getY() + dia / 2 + 4;
    const float r     = (float) dia * 0.5f;

    const float startAngle = juce::MathConstants<float>::pi * 1.25f;
    const float endAngle   = juce::MathConstants<float>::pi * 2.75f;
    const float angle      = startAngle + normalised * (endAngle - startAngle);

    // Track arc
    juce::Path track;
    track.addCentredArc ((float) cx, (float) cy, r - 2, r - 2,
                          0.0f, startAngle, endAngle, true);
    g.setColour (theme.darkBar.brighter (0.10f));
    g.strokePath (track, juce::PathStrokeType (3.0f));

    // Fill arc
    juce::Path fill;
    fill.addCentredArc ((float) cx, (float) cy, r - 2, r - 2,
                         0.0f, startAngle, angle, true);
    g.setColour (theme.accent);
    g.strokePath (fill, juce::PathStrokeType (3.0f));

    // Thumb dot
    const float tx = (float) cx + (r - 6) * std::cos (angle - juce::MathConstants<float>::halfPi);
    const float ty = (float) cy + (r - 6) * std::sin (angle - juce::MathConstants<float>::halfPi);
    g.setColour (theme.accent.brighter (0.2f));
    g.fillEllipse (tx - 3, ty - 3, 6, 6);

    // Labels
    g.setFont (DysektLookAndFeel::makeFont (10.0f));
    g.setColour (theme.foreground.withAlpha (0.45f));
    g.drawText (label, bounds.withTrimmedTop (cy - bounds.getY() + (int) r + 2),
                juce::Justification::centredTop, false);

    g.setFont (DysektLookAndFeel::makeFont (10.0f));
    g.setColour (theme.foreground.withAlpha (0.80f));
    g.drawText (valueStr,
                bounds.withTrimmedTop (cy - bounds.getY() + (int) r + 14),
                juce::Justification::centredTop, false);
}

// ── VU meter drawing ──────────────────────────────────────────────────────────

void SfzModulePanel::drawMeter (juce::Graphics& g) const
{
    const auto& theme = getTheme();
    auto area = meterZone.reduced (2, 4);

    const int barW  = area.getWidth() / 2 - 2;
    const int barH  = area.getHeight();

    auto leftBar  = juce::Rectangle<int> (area.getX(), area.getY(), barW, barH);
    auto rightBar = juce::Rectangle<int> (area.getX() + barW + 4, area.getY(), barW, barH);

    // Background tracks
    g.setColour (theme.darkBar.darker (0.2f));
    g.fillRoundedRectangle (leftBar.toFloat(),  2.0f);
    g.fillRoundedRectangle (rightBar.toFloat(), 2.0f);

    auto drawBar = [&] (juce::Rectangle<int> bar, float peak, float hold)
    {
        const int fillH = juce::roundToInt ((float) bar.getHeight() * juce::jlimit (0.0f, 1.0f, peak));
        if (fillH > 0)
        {
            juce::ColourGradient grad (theme.accent.withAlpha (0.85f),
                                       0.f, (float) bar.getBottom(),
                                       theme.accent.brighter (0.5f),
                                       0.f, (float) bar.getY(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bar.withTrimmedTop (bar.getHeight() - fillH).toFloat(), 2.0f);
        }
        // Hold line
        const int holdY = bar.getBottom() - juce::roundToInt ((float) bar.getHeight() * juce::jlimit (0.0f, 1.0f, hold));
        g.setColour (theme.accent.brighter (0.6f).withAlpha (0.7f));
        g.fillRect (bar.getX(), holdY - 1, bar.getWidth(), 2);

        // Label
        g.setFont (DysektLookAndFeel::makeFont (9.0f));
        g.setColour (theme.foreground.withAlpha (0.35f));
    };

    drawBar (leftBar,  meterL, holdL);
    drawBar (rightBar, meterR, holdR);
}

// ── Timer: poll processor SFZ peak meters ────────────────────────────────────

void SfzModulePanel::timerCallback()
{
    // SfzPlayer exposes peak via sfzPeakL/R atomics (added to processor below)
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

    repaint();
}

// ── MIDI Learn context menu ───────────────────────────────────────────────────

void SfzModulePanel::showMidiLearnMenu (int fieldId, juce::Point<int> screenPos)
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
            if (result == 1)       { processor.midiLearn.armLearn (fieldId);     repaint(); }
            else if (result == 2)  { processor.midiLearn.clearMapping (fieldId); repaint(); }
            else if (result == 1000)
            {
                if (auto* editor = findParentComponentOfClass<DysektEditor>())
                    editor->keyPressed (juce::KeyPress ('M', juce::ModifierKeys(), 0));
            }
        });
}

// ── Mouse events ─────────────────────────────────────────────────────────────

void SfzModulePanel::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();

    // Load button
    if (loadBtnZone.contains (pos))
    {
        openFileChooser();
        return;
    }

    // Save As button (SFZ mode only)
    if (! saveAsBtnZone.isEmpty() && saveAsBtnZone.contains (pos))
    {
        openSaveAsOverlay();
        return;
    }

    // Right-click on ADSR knobs → MIDI Learn menu
    if (e.mods.isRightButtonDown())
    {
        if (atkZone.contains (pos)) { showMidiLearnMenu (DysektProcessor::FieldSfzAttack,  e.getScreenPosition()); return; }
        if (decZone.contains (pos)) { showMidiLearnMenu (DysektProcessor::FieldSfzDecay,   e.getScreenPosition()); return; }
        if (susZone.contains (pos)) { showMidiLearnMenu (DysektProcessor::FieldSfzSustain, e.getScreenPosition()); return; }
        if (relZone.contains (pos)) { showMidiLearnMenu (DysektProcessor::FieldSfzRelease, e.getScreenPosition()); return; }
        if (rvSizeZone.contains   (pos)) { showMidiLearnMenu (DysektProcessor::FieldSfzReverbSize,   e.getScreenPosition()); return; }
        if (rvDampZone.contains   (pos)) { showMidiLearnMenu (DysektProcessor::FieldSfzReverbDamp,   e.getScreenPosition()); return; }
        if (rvWidthZone.contains  (pos)) { showMidiLearnMenu (DysektProcessor::FieldSfzReverbWidth,  e.getScreenPosition()); return; }
        if (rvMixZone.contains    (pos)) { showMidiLearnMenu (DysektProcessor::FieldSfzReverbMix,    e.getScreenPosition()); return; }
        if (rvFreezeZone.contains (pos)) { showMidiLearnMenu (DysektProcessor::FieldSfzReverbFreeze, e.getScreenPosition()); return; }
    }

    // Knob drag start
    if (volZone.contains (pos))
    {
        activeKnob  = ActiveKnob::Volume;
        dragStartY  = pos.y;
        dragStartVal = volToNorm (processor.sfzPlayer.getVolume());
        return;
    }
    if (transZone.contains (pos))
    {
        activeKnob  = ActiveKnob::Transpose;
        dragStartY  = pos.y;
        dragStartVal = transToNorm (processor.sfzPlayer.getTranspose());
        return;
    }
    if (atkZone.contains (pos))
    {
        activeKnob   = ActiveKnob::Attack;
        dragStartY   = pos.y;
        dragStartVal = processor.sfzPlayer.getSfzAttack() / 30.0f;
        return;
    }
    if (decZone.contains (pos))
    {
        activeKnob   = ActiveKnob::Decay;
        dragStartY   = pos.y;
        dragStartVal = processor.sfzPlayer.getSfzDecay() / 30.0f;
        return;
    }
    if (susZone.contains (pos))
    {
        activeKnob   = ActiveKnob::Sustain;
        dragStartY   = pos.y;
        dragStartVal = processor.sfzPlayer.getSfzSustain() / 100.0f;
        return;
    }
    if (relZone.contains (pos))
    {
        activeKnob   = ActiveKnob::Release;
        dragStartY   = pos.y;
        dragStartVal = processor.sfzPlayer.getSfzRelease() / 60.0f;
        return;
    }
    if (rvSizeZone.contains (pos))
    {
        activeKnob   = ActiveKnob::ReverbSize;
        dragStartY   = pos.y;
        dragStartVal = processor.sfzPlayer.getReverbSize() / 100.0f;
        return;
    }
    if (rvDampZone.contains (pos))
    {
        activeKnob   = ActiveKnob::ReverbDamp;
        dragStartY   = pos.y;
        dragStartVal = processor.sfzPlayer.getReverbDamp() / 100.0f;
        return;
    }
    if (rvWidthZone.contains (pos))
    {
        activeKnob   = ActiveKnob::ReverbWidth;
        dragStartY   = pos.y;
        dragStartVal = processor.sfzPlayer.getReverbWidth() / 100.0f;
        return;
    }
    if (rvMixZone.contains (pos))
    {
        activeKnob   = ActiveKnob::ReverbMix;
        dragStartY   = pos.y;
        dragStartVal = processor.sfzPlayer.getReverbMix() / 100.0f;
        return;
    }
    if (rvFreezeZone.contains (pos))
    {
        // Freeze is a toggle on click, not a drag
        processor.sfzPlayer.setReverbFreeze (!processor.sfzPlayer.getReverbFreeze());
        repaint();
        return;
    }
}

void SfzModulePanel::mouseDrag (const juce::MouseEvent& e)
{
    if (activeKnob == ActiveKnob::None) return;

    const float delta = (float)(dragStartY - e.getPosition().y) / 150.0f;
    const float newNorm = juce::jlimit (0.0f, 1.0f, dragStartVal + delta);

    switch (activeKnob)
    {
        case ActiveKnob::Volume:
            processor.sfzPlayer.setVolume (normToVol (newNorm));    break;
        case ActiveKnob::Transpose:
            processor.sfzPlayer.setTranspose (normToTrans (newNorm)); break;
        case ActiveKnob::Attack:
            processor.sfzPlayer.setSfzAttack  (newNorm * 30.0f);   break;
        case ActiveKnob::Decay:
            processor.sfzPlayer.setSfzDecay   (newNorm * 30.0f);   break;
        case ActiveKnob::Sustain:
            processor.sfzPlayer.setSfzSustain (newNorm * 100.0f);  break;
        case ActiveKnob::Release:
            processor.sfzPlayer.setSfzRelease (newNorm * 60.0f);   break;
        case ActiveKnob::ReverbSize:
            processor.sfzPlayer.setReverbSize  (newNorm * 100.0f); break;
        case ActiveKnob::ReverbDamp:
            processor.sfzPlayer.setReverbDamp  (newNorm * 100.0f); break;
        case ActiveKnob::ReverbWidth:
            processor.sfzPlayer.setReverbWidth (newNorm * 100.0f); break;
        case ActiveKnob::ReverbMix:
            processor.sfzPlayer.setReverbMix   (newNorm * 100.0f); break;
        default: break;
    }
    repaint();
}

void SfzModulePanel::mouseUp (const juce::MouseEvent&)
{
    activeKnob = ActiveKnob::None;
}

void SfzModulePanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (volZone.contains (pos))   { processor.sfzPlayer.setVolume (1.0f);         repaint(); }
    if (transZone.contains (pos)) { processor.sfzPlayer.setTranspose (0);          repaint(); }
    if (atkZone.contains (pos))   { processor.sfzPlayer.setSfzAttack  (0.005f);   repaint(); }
    if (decZone.contains (pos))   { processor.sfzPlayer.setSfzDecay   (0.1f);     repaint(); }
    if (susZone.contains (pos))   { processor.sfzPlayer.setSfzSustain (100.0f);   repaint(); }
    if (relZone.contains (pos))   { processor.sfzPlayer.setSfzRelease (0.05f);    repaint(); }
    if (rvSizeZone.contains (pos))   { processor.sfzPlayer.setReverbSize  (50.0f);  repaint(); }
    if (rvDampZone.contains (pos))   { processor.sfzPlayer.setReverbDamp  (50.0f);  repaint(); }
    if (rvWidthZone.contains (pos))  { processor.sfzPlayer.setReverbWidth (50.0f);  repaint(); }
    if (rvMixZone.contains (pos))    { processor.sfzPlayer.setReverbMix   ( 0.0f);  repaint(); }
    if (rvFreezeZone.contains (pos)) { processor.sfzPlayer.setReverbFreeze (false); repaint(); }
}

void SfzModulePanel::mouseWheelMove (const juce::MouseEvent& e,
                                      const juce::MouseWheelDetails& w)
{
    const auto pos = e.getPosition();
    const float step = w.deltaY * (e.mods.isShiftDown() ? 0.01f : 0.05f);

    if (volZone.contains (pos))
    {
        const float n = juce::jlimit (0.0f, 1.0f, volToNorm (processor.sfzPlayer.getVolume()) + step);
        processor.sfzPlayer.setVolume (normToVol (n));
        repaint();
    }
    else if (transZone.contains (pos))
    {
        const float n = juce::jlimit (0.0f, 1.0f, transToNorm (processor.sfzPlayer.getTranspose()) + step);
        processor.sfzPlayer.setTranspose (normToTrans (n));
        repaint();
    }
    else if (atkZone.contains (pos))
    {
        const float n = juce::jlimit (0.0f, 1.0f, processor.sfzPlayer.getSfzAttack() / 30.0f + step);
        processor.sfzPlayer.setSfzAttack (n * 30.0f);
        repaint();
    }
    else if (decZone.contains (pos))
    {
        const float n = juce::jlimit (0.0f, 1.0f, processor.sfzPlayer.getSfzDecay() / 30.0f + step);
        processor.sfzPlayer.setSfzDecay (n * 30.0f);
        repaint();
    }
    else if (susZone.contains (pos))
    {
        const float n = juce::jlimit (0.0f, 1.0f, processor.sfzPlayer.getSfzSustain() / 100.0f + step);
        processor.sfzPlayer.setSfzSustain (n * 100.0f);
        repaint();
    }
    else if (relZone.contains (pos))
    {
        const float n = juce::jlimit (0.0f, 1.0f, processor.sfzPlayer.getSfzRelease() / 60.0f + step);
        processor.sfzPlayer.setSfzRelease (n * 60.0f);
        repaint();
    }
    else if (rvSizeZone.contains (pos))
    {
        const float n = juce::jlimit (0.0f, 1.0f, processor.sfzPlayer.getReverbSize() / 100.0f + step);
        processor.sfzPlayer.setReverbSize (n * 100.0f);
        repaint();
    }
    else if (rvDampZone.contains (pos))
    {
        const float n = juce::jlimit (0.0f, 1.0f, processor.sfzPlayer.getReverbDamp() / 100.0f + step);
        processor.sfzPlayer.setReverbDamp (n * 100.0f);
        repaint();
    }
    else if (rvWidthZone.contains (pos))
    {
        const float n = juce::jlimit (0.0f, 1.0f, processor.sfzPlayer.getReverbWidth() / 100.0f + step);
        processor.sfzPlayer.setReverbWidth (n * 100.0f);
        repaint();
    }
    else if (rvMixZone.contains (pos))
    {
        const float n = juce::jlimit (0.0f, 1.0f, processor.sfzPlayer.getReverbMix() / 100.0f + step);
        processor.sfzPlayer.setReverbMix (n * 100.0f);
        repaint();
    }
}

// ── File drag-and-drop ────────────────────────────────────────────────────────

bool SfzModulePanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".sf2" || ext == ".sfz") return true;
    }
    return false;
}

void SfzModulePanel::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".sf2" || ext == ".sfz")
        {
            juce::File file (f);
            processor.sfzPlayer.loadFile (file, processor.fileLoadPool);
            processor.loadSoundFontAsync (file, SoundFontLoadTarget::SfPlayer);   // waveform preview -> sampleData3
            processor.sfPlayerChannelMask.store (1u << 3, std::memory_order_relaxed); // ch3 default
            reloadZones (file);
            repaint();
            return;
        }
    }
}

// ── File chooser ─────────────────────────────────────────────────────────────

void SfzModulePanel::openFileChooser()
{
    chooser = std::make_unique<juce::FileChooser> (
        "Load SF2 or SFZ Instrument",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.sf2;*.sfz");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                        | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result.existsAsFile())
            {
                processor.sfzPlayer.loadFile (result, processor.fileLoadPool);
                processor.loadSoundFontAsync (result, SoundFontLoadTarget::SfPlayer);   // waveform preview -> sampleData3
                processor.sfPlayerChannelMask.store (1u << 3, std::memory_order_relaxed); // ch3 default
                reloadZones (result);
                repaint();
            }
        });
}

// ── Custom SFZ builder ────────────────────────────────────────────────────────

void SfzModulePanel::openBrowser (const juce::File& startDir)
{
    fileBrowser.setRootDirectory (startDir);
    fileBrowser.setBounds (getLocalBounds());
    fileBrowser.setVisible (true);
    fileBrowser.toFront (false);
    browserOpen = true;
}

void SfzModulePanel::closeBrowser()
{
    fileBrowser.setVisible (false);
    browserOpen = false;
}

void SfzModulePanel::onSampleChosen (const juce::File& f)
{
    fileBrowser.setMode (SfzFileBrowser::Mode::kSfz);
    closeBrowser();
    showAddZoneOverlay (addZoneTargetSfz, f, addZonePrevHiKey);
}

void SfzModulePanel::openAddZoneChooser()
{
    // ── Resolve or create the target .sfz ─────────────────────────────────────
    juce::File targetSfz;
    if (processor.sfzPlayer.isLoaded())
    {
        const auto loaded = processor.sfzPlayer.getLoadedFile();
        if (loaded.getFileExtension().toLowerCase() == ".sfz")
            targetSfz = loaded;
    }

    if (! targetSfz.existsAsFile())
    {
        // Nothing loaded yet: show Save As so the user names the file first,
        // then chain back into the sample browser.
        openSaveAsOverlay (/*thenOpenAddZone=*/true);
        return;
    }

    int prevHiKey = -1;
    if (targetSfz.existsAsFile())
    {
        const auto existing = parseSfzZones (targetSfz);
        for (const auto& z : existing)
            prevHiKey = juce::jmax (prevHiKey, z.hiKey);
    }

    addZoneTargetSfz  = targetSfz;
    addZonePrevHiKey  = prevHiKey;

    fileBrowser.setMode (SfzFileBrowser::Mode::kAddZone);
    openBrowser (targetSfz.getParentDirectory());
}

void SfzModulePanel::showAddZoneOverlay (const juce::File& sfzFile,
                                          const juce::File& sampleFile,
                                          int               prevHiKey)
{
    const int defaultLo = (prevHiKey < 0) ? 0 : juce::jmin (prevHiKey + 1, 127);

    auto overlay = std::make_unique<AddZoneOverlay> (
        sampleFile.getFileNameWithoutExtension(), defaultLo);

    overlay->onResult = [this, sfzFile, sampleFile] (int lo, int hi, int root, bool confirmed)
    {
        // Defer hideOverlays() so it runs after fire() has returned and
        // AddZoneOverlay is no longer on the call stack (use-after-free fix).
        juce::MessageManager::callAsync ([this] { hideOverlays(); });

        if (! confirmed)
            return;

        if (! appendZoneToSfz (sfzFile, sampleFile, lo, hi, root))
        {
            showOverlay (messageOverlay, std::make_unique<MessageOverlay> (
                "Add Zone Failed",
                "Could not write to:\n" + sfzFile.getFullPathName(),
                MessageOverlay::Kind::Warning));
            messageOverlay->onDismiss = [this] { juce::MessageManager::callAsync ([this] { hideOverlays(); }); };
            return;
        }

        processor.sfzPlayer.loadFile (sfzFile, processor.fileLoadPool);
        processor.loadSoundFontAsync (sfzFile, SoundFontLoadTarget::SfPlayer);   // waveform preview -> sampleData3
        processor.sfPlayerChannelMask.store (1u << 3, std::memory_order_relaxed); // ch3 default
        reloadZones (sfzFile);
        keysPanel.autoScrollToZones();
        repaint();
    };

    showOverlay (addZoneOverlay, std::move (overlay));
}

bool SfzModulePanel::appendZoneToSfz (const juce::File& sfzFile,
                                        const juce::File& sampleFile,
                                        int loKey, int hiKey, int rootKey)
{
    // Relative path from .sfz directory → portable file
    juce::String samplePath;
    const auto sfzDir = sfzFile.getParentDirectory();
    if (sampleFile.isAChildOf (sfzDir))
        samplePath = sampleFile.getRelativePathFrom (sfzDir).replaceCharacter ('\\', '/');
    else
        samplePath = sampleFile.getFullPathName().replaceCharacter ('\\', '/');

    const juce::String region =
        "\n<region>\n"
        "sample="          + samplePath              + "\n"
        "lokey="           + juce::String (loKey)    + "\n"
        "hikey="           + juce::String (hiKey)    + "\n"
        "pitch_keycenter=" + juce::String (rootKey)  + "\n"
        "volume=-7\n"
        "pan=0\n"
        "tune=0\n"
        "ampeg_release=0.664\n";

    juce::FileOutputStream stream (sfzFile);
    if (stream.failedToOpen())
        return false;

    stream.setPosition (sfzFile.getSize());
    stream.writeText (region, false, false, nullptr);
    stream.flush();
    return ! stream.getStatus().failed();
}

// ── Save SFZ As ───────────────────────────────────────────────────────────────

void SfzModulePanel::openSaveAsOverlay (bool thenOpenAddZone)
{
    const auto currentFile = processor.sfzPlayer.isLoaded()
                           ? processor.sfzPlayer.getLoadedFile()
                           : juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                 .getChildFile ("Custom.sfz");

    auto overlay = std::make_unique<SaveSfzOverlay> (currentFile);

    overlay->onResult = [this, currentFile, thenOpenAddZone] (const juce::File& dest, bool confirmed)
    {
        // Defer hideOverlays() so it runs after fire() has returned and
        // SaveSfzOverlay is no longer on the call stack (use-after-free fix).
        juce::MessageManager::callAsync ([this] { hideOverlays(); });

        if (! confirmed || dest == juce::File{})
            return;

        // Copy current .sfz to chosen destination
        if (currentFile.existsAsFile())
        {
            const bool ok = currentFile.copyFileTo (dest);
            if (! ok)
            {
                showOverlay (messageOverlay, std::make_unique<MessageOverlay> (
                    "Save Failed",
                    "Could not write:\n" + dest.getFullPathName(),
                    MessageOverlay::Kind::Warning));
                messageOverlay->onDismiss = [this] { juce::MessageManager::callAsync ([this] { hideOverlays(); }); };
                return;
            }
        }
        else
        {
            // No existing file — write a minimal placeholder so sfizz can load it
            dest.replaceWithText ("// Custom SFZ — built with SF-Player\n\n");
        }

        // Switch sfzPlayer and zone matrix to the new file
        processor.sfzPlayer.loadFile (dest, processor.fileLoadPool);
        processor.loadSoundFontAsync (dest, SoundFontLoadTarget::SfPlayer);   // waveform preview -> sampleData3
        processor.sfPlayerChannelMask.store (1u << 3, std::memory_order_relaxed); // ch3 default
        reloadZones (dest);
        repaint();

        // If triggered from [+ ZONE] when nothing was loaded, now open
        // the sample browser to complete the Add Zone flow.
        if (thenOpenAddZone)
            juce::MessageManager::callAsync ([this] { openAddZoneChooser(); });
    };

    showOverlay (saveSfzOverlay, std::move (overlay));
}

// ── Overlay helpers ───────────────────────────────────────────────────────────

void SfzModulePanel::hideOverlays()
{
    if (addZoneOverlay)
    {
        if (auto* p = addZoneOverlay->getParentComponent())
            p->removeChildComponent (addZoneOverlay.get());
        addZoneOverlay.reset();
    }
    if (messageOverlay)
    {
        if (auto* p = messageOverlay->getParentComponent())
            p->removeChildComponent (messageOverlay.get());
        messageOverlay.reset();
    }
    if (saveSfzOverlay)
    {
        if (auto* p = saveSfzOverlay->getParentComponent())
            p->removeChildComponent (saveSfzOverlay.get());
        saveSfzOverlay.reset();
    }
}

// ── panelDidShow ──────────────────────────────────────────────────────────────

void SfzModulePanel::panelDidShow()
{
    if (processor.sfzPlayer.isLoaded())
        reloadZones (processor.sfzPlayer.getLoadedFile());
    else
        initEmptySfz();   // bootstrap so [+ ZONE] is available immediately
    repaint();
}

void SfzModulePanel::initEmptySfz()
{
    // Create Custom.sfz in the user's Music folder if it doesn't exist yet.
    // (Never overwrites — if the file is already there from a previous session,
    //  we just reload it so the existing zones are restored.)
    auto sfz = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                   .getChildFile ("Custom.sfz");

    if (! sfz.existsAsFile())
        sfz.replaceWithText ("// Custom SFZ — built with SF-Player\n\n");

    processor.sfzPlayer.loadFile (sfz, processor.fileLoadPool);   // sfizz handles empty file gracefully (silence)
    processor.loadSoundFontAsync (sfz, SoundFontLoadTarget::SfPlayer);   // waveform preview -> sampleData3
    processor.sfPlayerChannelMask.store (1u << 3, std::memory_order_relaxed); // ch3 default
    reloadZones (sfz);                    // sets [+ ZONE] button visible + wires callback
}

// ── Value mapping helpers ─────────────────────────────────────────────────────

float SfzModulePanel::volToNorm   (float linear) const { return juce::jlimit (0.0f, 1.0f, linear * 0.5f); }
float SfzModulePanel::normToVol   (float n)      const { return n * 2.0f; }
float SfzModulePanel::transToNorm (int semi)      const { return ((float) semi + 24.0f) / 48.0f; }
int   SfzModulePanel::normToTrans (float n)       const { return juce::roundToInt (n * 48.0f - 24.0f); }

// =============================================================================
//  Zone parsers
// =============================================================================

// Palette of distinct colours for successive zones
static juce::Colour zoneColour (int index)
{
    static const juce::Colour palette[] = {
        juce::Colour (0xFF4FC3F7),  // sky blue
        juce::Colour (0xFF81C784),  // sage green
        juce::Colour (0xFFFFB74D),  // warm amber
        juce::Colour (0xFFE57373),  // soft red
        juce::Colour (0xFFBA68C8),  // lavender
        juce::Colour (0xFF4DD0E1),  // teal
        juce::Colour (0xFFF06292),  // rose
        juce::Colour (0xFFA1887F),  // mauve
    };
    return palette[index % 8];
}

std::vector<KeysPanel::Keyzone> SfzModulePanel::parseSfzZones (const juce::File& f)
{
    std::vector<KeysPanel::Keyzone> zones;
    const auto text = f.loadFileAsString();
    const auto lines = juce::StringArray::fromLines (text);

    int   loKey      = 0;
    int   hiKey      = 127;
    int   rootPitch  = -1;
    float volDb      = -7.0f;
    float panVal     = 0.0f;
    float tuneVal    = 0.0f;
    float releaseSec = 0.664f;
    bool  inRegion   = false;
    int   colIdx     = 0;
    juce::String sampleName;
    juce::String groupSampleName;   // inheritable group/global-level sample=

    // ── Extract the bare filename (no directory, no extension) from a raw line ──
    // Uses the original (non-lowercased) line so names preserve their case.
    auto extractSampleName = [] (const juce::String& rawLine) -> juce::String
    {
        const juce::String lc = rawLine.toLowerCase();
        const int smpPos = lc.indexOf ("sample=");
        if (smpPos < 0) return {};

        // Grab everything after sample= then strip trailing opcodes
        // (word= tokens) so paths containing spaces are preserved.
        auto path = rawLine.substring (smpPos + 7).trim()
                           .upToFirstOccurrenceOf ("\t", false, false).trim();
        {
            juce::String tmp = path;
            for (;;)
            {
                auto idx = tmp.lastIndexOf (" ");
                if (idx < 0) break;
                auto tail = tmp.substring (idx + 1);
                if (tail.containsChar ('=')) { tmp = tmp.substring (0, idx).trim(); }
                else break;
            }
            path = tmp;
        }

        // Strip leading directory components (forward- and back-slash).
        juce::String name = path.fromLastOccurrenceOf ("/",  false, false)
                                .fromLastOccurrenceOf ("\\", false, false)
                                .upToLastOccurrenceOf (".",  false, false)
                                .trim();
        return name;
    };

    auto flush = [&]
    {
        if (inRegion && hiKey >= loKey)
        {
            KeysPanel::Keyzone z;
            z.loKey      = loKey;
            z.hiKey      = hiKey;
            z.rootPitch  = rootPitch;
            z.volDb      = volDb;
            z.pan        = panVal;
            z.tuneCents  = tuneVal;
            z.releaseSec = releaseSec;
            z.colour     = zoneColour (colIdx++);
            // Use the per-region name; fall back to group-level name; then "Zone N".
            if (sampleName.isNotEmpty())
                z.name = sampleName;
            else if (groupSampleName.isNotEmpty())
                z.name = groupSampleName;
            else
                z.name = "Zone " + juce::String (colIdx);
            z.isSfz      = true;
            zones.push_back (z);
        }
        loKey = 0; hiKey = 127; rootPitch = -1;
        volDb = -7.0f; panVal = 0.0f; tuneVal = 0.0f; releaseSec = 0.664f;
        sampleName = {};
        inRegion = false;
    };

    for (const auto& rawLine : lines)
    {
        // Use lowercase copy for tag/opcode detection; rawLine for sample name.
        const juce::String line = rawLine.trim().toLowerCase();

        if (line.startsWith ("<region>"))
        {
            flush();
            inRegion = true;
            loKey = 0; hiKey = 127;
        }
        else if (line.startsWith ("<group>"))
        {
            flush();
            groupSampleName = {};   // reset — each group owns its own default
        }
        else if (line.startsWith ("<global>"))
        {
            flush();
            groupSampleName = {};
        }

        // Parse group/global-level sample= so regions can inherit it.
        if (! inRegion)
        {
            juce::String inherited = extractSampleName (rawLine);
            if (inherited.isNotEmpty())
                groupSampleName = inherited;
        }

        if (inRegion)
        {
            // Generic float extractor (operates on lowercased line).
            auto getFloat = [&] (const juce::String& key) -> float
            {
                int pos = line.indexOf (key + "=");
                if (pos < 0) return -999999.f;
                return line.substring (pos + key.length() + 1)
                           .trimStart()
                           .upToFirstOccurrenceOf (" ",  false, false)
                           .upToFirstOccurrenceOf ("\t", false, false)
                           .getFloatValue();
            };

            // lokey / hikey
            auto loRaw = line.indexOf ("lokey=");
            if (loRaw >= 0)
            {
                auto s = line.substring (loRaw + 6)
                             .upToFirstOccurrenceOf (" ", false, false).trim();
                loKey = juce::jlimit (0, 127, s.getIntValue());
            }
            auto hiRaw = line.indexOf ("hikey=");
            if (hiRaw >= 0)
            {
                auto s = line.substring (hiRaw + 6)
                             .upToFirstOccurrenceOf (" ", false, false).trim();
                hiKey = juce::jlimit (0, 127, s.getIntValue());
            }
            // key= shorthand
            auto kRaw = line.indexOf ("key=");
            if (kRaw >= 0 && line.indexOf ("lokey=") < 0)
            {
                auto s = line.substring (kRaw + 4)
                             .upToFirstOccurrenceOf (" ", false, false).trim();
                loKey = hiKey = juce::jlimit (0, 127, s.getIntValue());
            }

            // pitch_keycenter -> rootPitch
            auto pkRaw = line.indexOf ("pitch_keycenter=");
            if (pkRaw >= 0)
            {
                auto s = line.substring (pkRaw + 16)
                             .upToFirstOccurrenceOf (" ", false, false).trim();
                rootPitch = juce::jlimit (0, 127, s.getIntValue());
            }

            // volume (dB in SFZ)
            float v = getFloat ("volume");
            if (v > -999998.f) volDb = v;

            // pan (-100..+100 in SFZ percent, stored as -1..+1)
            float p = getFloat ("pan");
            if (p > -999998.f) panVal = juce::jlimit (-1.0f, 1.0f, p / 100.0f);

            // tune (cents, -100..+100 in SFZ)
            float t = getFloat ("tune");
            if (t > -999998.f) tuneVal = juce::jlimit (-100.0f, 100.0f, t);

            // ampeg_release (seconds)
            float rel = getFloat ("ampeg_release");
            if (rel > -999998.f) releaseSec = juce::jlimit (0.0f, 60.0f, rel);

            // Per-region sample= (takes priority over group-level name).
            juce::String perRegion = extractSampleName (rawLine);
            if (perRegion.isNotEmpty())
                sampleName = perRegion;
        }
    }
    flush();
    return zones;
}

std::vector<KeysPanel::Keyzone> SfzModulePanel::parseSf2Zones (const juce::File& f,
                                                                  int targetBank,
                                                                  int targetPreset)
{
    // ── Full SF2 RIFF parser ─────────────────────────────────────────────────
    // Reads phdr → pbag → pgen to find the instrument index for the selected
    // preset, then reads ibag → igen to extract only THAT instrument's zones.
    // Previously this read ALL igen records regardless of preset, which caused
    // every sample in the file to appear in the zone matrix.
    std::vector<KeysPanel::Keyzone> zones;

    juce::FileInputStream stream (f);
    if (stream.failedToOpen()) return zones;

    // RIFF / sfbk header
    char riff[4]; stream.read (riff, 4);
    if (juce::String::fromUTF8 (riff, 4) != "RIFF") return zones;
    stream.readInt();
    char sfbk[4]; stream.read (sfbk, 4);
    if (juce::String::fromUTF8 (sfbk, 4) != "sfbk") return zones;

    // Collect all pdta sub-chunks we need
    juce::MemoryBlock phdrData, pbagData, pgenData, instData, ibagData, igenData, shdrData;

    while (! stream.isExhausted())
    {
        char id[4]; if (stream.read (id, 4) < 4) break;
        const auto chunkId = juce::String::fromUTF8 (id, 4);
        const int  sz      = stream.readInt();

        if (chunkId == "LIST")
        {
            char listId[4]; stream.read (listId, 4);
            if (juce::String::fromUTF8 (listId, 4) == "pdta")
            {
                const int pdtaEnd = (int) stream.getPosition() + sz - 4;
                while (stream.getPosition() < pdtaEnd && ! stream.isExhausted())
                {
                    char sub[4]; if (stream.read (sub, 4) < 4) break;
                    const auto subId = juce::String::fromUTF8 (sub, 4);
                    const int  subSz = stream.readInt();
                    auto readChunk = [&] (juce::MemoryBlock& mb)
                    {
                        mb.setSize ((size_t) subSz);
                        stream.read (mb.getData(), subSz);
                    };
                    if      (subId == "phdr") readChunk (phdrData);
                    else if (subId == "pbag") readChunk (pbagData);
                    else if (subId == "pgen") readChunk (pgenData);
                    else if (subId == "inst") readChunk (instData);
                    else if (subId == "ibag") readChunk (ibagData);
                    else if (subId == "igen") readChunk (igenData);
                    else if (subId == "shdr") readChunk (shdrData);
                    else stream.skipNextBytes (subSz);
                }
                break;
            }
            else stream.skipNextBytes (sz - 4);
        }
        else stream.skipNextBytes (sz);
    }

    if (igenData.isEmpty() || phdrData.isEmpty() || pbagData.isEmpty()
        || pgenData.isEmpty() || instData.isEmpty() || ibagData.isEmpty())
        return zones;

    // ── Helper lambdas for reading little-endian values ───────────────────────
    auto readU16 = [] (const juce::MemoryBlock& mb, size_t byteOffset) -> uint16_t
    {
        if (byteOffset + 1 >= mb.getSize()) return 0;
        const auto* d = static_cast<const uint8_t*> (mb.getData());
        return (uint16_t)(d[byteOffset] | (d[byteOffset + 1] << 8));
    };
    auto readI16 = [&] (const juce::MemoryBlock& mb, size_t byteOffset) -> int16_t
    {
        return (int16_t) readU16 (mb, byteOffset);
    };

    // ── Step 1: find preset in phdr ───────────────────────────────────────────
    // phdr record: 20-char name, uint16 preset, uint16 bank, uint16 wPresetBagNdx,
    //              uint32 dwLibrary, uint32 dwGenre, uint32 dwMorphology  = 38 bytes
    constexpr size_t kPhdrSz = 38;
    const size_t numPresets  = phdrData.getSize() / kPhdrSz;
    const auto*  phdrRaw     = static_cast<const uint8_t*> (phdrData.getData());

    int presetBagStart = -1;
    int presetBagEnd   = -1;

    for (size_t pi = 0; pi + 1 < numPresets; ++pi)  // last record is EOP sentinel
    {
        const uint16_t pNum  = readU16 (phdrData, pi * kPhdrSz + 20);
        const uint16_t pBank = readU16 (phdrData, pi * kPhdrSz + 22);
        const uint16_t bagNdx= readU16 (phdrData, pi * kPhdrSz + 24);

        if ((int) pNum == targetPreset && (int) pBank == targetBank)
        {
            presetBagStart = (int) bagNdx;
            // Next record's bag index gives us the exclusive end
            presetBagEnd = (int) readU16 (phdrData, (pi + 1) * kPhdrSz + 24);
            break;
        }
    }

    // Fallback: if the requested bank/preset wasn't found, use the first preset
    if (presetBagStart < 0 && numPresets > 1)
    {
        presetBagStart = (int) readU16 (phdrData, 24);
        presetBagEnd   = (int) readU16 (phdrData, kPhdrSz + 24);
    }

    if (presetBagStart < 0) return zones;

    // ── Step 2: walk pbag/pgen to find instrument index (oper=41) ────────────
    // pbag record: uint16 wGenNdx, uint16 wModNdx  = 4 bytes
    constexpr size_t kPbagSz = 4;
    constexpr size_t kPgenSz = 4;

    int instrumentIndex = -1;

    for (int bi = presetBagStart; bi < presetBagEnd; ++bi)
    {
        const size_t bagOff = (size_t) bi * kPbagSz;
        if (bagOff + 2 > pbagData.getSize()) break;
        const int genStart = (int) readU16 (pbagData, bagOff);
        const int genEnd   = (bi + 1 < (int)(pbagData.getSize() / kPbagSz))
                             ? (int) readU16 (pbagData, (size_t)(bi + 1) * kPbagSz)
                             : (int)(pgenData.getSize() / kPgenSz);

        for (int gi = genStart; gi < genEnd; ++gi)
        {
            const size_t gOff = (size_t) gi * kPgenSz;
            if (gOff + 4 > pgenData.getSize()) break;
            const uint16_t oper = readU16 (pgenData, gOff);
            if (oper == 41)  // instrument generator
            {
                instrumentIndex = (int) readU16 (pgenData, gOff + 2);
                break;
            }
        }
        if (instrumentIndex >= 0) break;
    }

    if (instrumentIndex < 0) return zones;

    // ── Step 3: find igen range via inst → ibag ───────────────────────────────
    // inst record: 20-char name + uint16 wInstBagNdx = 22 bytes
    // ibag record: uint16 wInstGenNdx, uint16 wInstModNdx = 4 bytes
    constexpr size_t kInstSz = 22;
    constexpr size_t kIbagSz = 4;

    const size_t numInsts = instData.getSize() / kInstSz;
    if ((size_t) instrumentIndex + 1 >= numInsts) return zones;  // need [i] and [i+1]

    // inst[instrumentIndex].wInstBagNdx is at byte offset 20 within the record
    const int ibagStart = (int) readU16 (instData, (size_t) instrumentIndex * kInstSz + 20);
    const int ibagEnd   = (int) readU16 (instData, (size_t)(instrumentIndex + 1) * kInstSz + 20);

    const size_t numIbags = ibagData.getSize() / kIbagSz;
    if ((size_t) ibagStart >= numIbags || ibagEnd < ibagStart) return zones;

    const int igenStart = (int) readU16 (ibagData, (size_t) ibagStart * kIbagSz);
    const int igenEnd   = ((size_t) ibagEnd < numIbags)
                          ? (int) readU16 (ibagData, (size_t) ibagEnd * kIbagSz)
                          : (int)(igenData.getSize() / 4);

    // ── Step 4: build sample-name lookup from shdr ────────────────────────────
    // shdr record: 20-char name + 26 bytes of other fields = 46 bytes
    std::vector<juce::String> sampleNames;
    if (! shdrData.isEmpty())
    {
        constexpr size_t kShdrSz = 46;
        const size_t numSamples  = shdrData.getSize() / kShdrSz;
        const auto*  shdrRaw     = static_cast<const char*> (shdrData.getData());
        sampleNames.reserve (numSamples);
        for (size_t s = 0; s < numSamples; ++s)
            sampleNames.push_back (juce::String::fromUTF8 (shdrRaw + s * kShdrSz, 20).trimEnd());
    }

    // ── Step 5: parse igen records in range → build zones ────────────────────
    // igen record: uint16 sfGenOper, then 2 bytes genAmount (lo/hi for ranges)
    const auto* igenRaw = static_cast<const uint8_t*> (igenData.getData());

    int   zLoKey     = 0;
    int   zHiKey     = 127;
    int   zRootPitch = -1;
    float zVolDb     = -7.0f;
    float zPan       = 0.0f;
    float zTune      = 0.0f;
    float zRelSec    = 0.664f;
    int   zSampleId  = -1;
    bool  zHasRange  = false;

    std::set<std::pair<int,int>> seen;
    int colIdx = 0;

    auto flushZone = [&]
    {
        if (! zHasRange || zHiKey < zLoKey) return;
        auto key = std::make_pair (zLoKey, zHiKey);
        if (seen.find (key) == seen.end())
        {
            seen.insert (key);
            KeysPanel::Keyzone z;
            z.loKey      = zLoKey;
            z.hiKey      = zHiKey;
            z.rootPitch  = zRootPitch;
            z.volDb      = zVolDb;
            z.pan        = zPan;
            z.tuneCents  = zTune;
            z.releaseSec = zRelSec;
            z.colour     = zoneColour (colIdx);
            z.isSfz      = false;   // SF2 zones are read-only

            if (zSampleId >= 0 && zSampleId < (int) sampleNames.size()
                && sampleNames[(size_t) zSampleId] != "EOS"
                && sampleNames[(size_t) zSampleId].isNotEmpty())
                z.name = sampleNames[(size_t) zSampleId];
            else
                z.name = "Zone " + juce::String (colIdx + 1);

            zones.push_back (z);
            ++colIdx;
        }
        // Reset accumulators for next zone
        zLoKey = 0; zHiKey = 127; zRootPitch = -1;
        zVolDb = -7.0f; zPan = 0.0f; zTune = 0.0f; zRelSec = 0.664f;
        zSampleId = -1; zHasRange = false;
    };

    for (int i = igenStart; i < igenEnd; ++i)
    {
        const size_t   off  = (size_t) i * 4;
        if (off + 4 > igenData.getSize()) break;
        const uint16_t oper = (uint16_t)(igenRaw[off] | (igenRaw[off + 1] << 8));
        const uint8_t  lo   = igenRaw[off + 2];
        const uint8_t  hi   = igenRaw[off + 3];

        if (oper == 43)  // keyRange — marks start of a new zone
        {
            flushZone();
            zLoKey = (int) lo; zHiKey = (int) hi; zHasRange = true;
        }
        else if (oper == 58)  // overridingRootKey
        {
            zRootPitch = juce::jlimit (0, 127, (int) lo);
        }
        else if (oper == 48)  // initialAttenuation (centibels → dB)
        {
            const int16_t cb = (int16_t)((uint16_t)(lo | (hi << 8)));
            zVolDb = -(float) cb / 10.0f;
        }
        else if (oper == 17)  // pan (0.1% units; -500=L, 0=C, +500=R)
        {
            const int16_t raw = (int16_t)((uint16_t)(lo | (hi << 8)));
            zPan = juce::jlimit (-1.0f, 1.0f, (float) raw / 500.0f);
        }
        else if (oper == 52)  // fineTune (cents)
        {
            const int16_t raw = (int16_t)((uint16_t)(lo | (hi << 8)));
            zTune = juce::jlimit (-100.0f, 100.0f, (float) raw);
        }
        else if (oper == 38)  // releaseVolEnv (timecents)
        {
            const int16_t tc = (int16_t)((uint16_t)(lo | (hi << 8)));
            zRelSec = juce::jlimit (0.0f, 60.0f,
                          (float) std::pow (2.0, (double) tc / 1200.0));
        }
        else if (oper == 53)  // sampleID — terminal generator, ends this zone
        {
            zSampleId = (int)(uint16_t)(lo | (hi << 8));
            flushZone();
        }
    }
    flushZone();

    // Sort by loKey and re-assign palette colours
    std::sort (zones.begin(), zones.end(),
               [] (auto& a, auto& b) { return a.loKey < b.loKey; });
    for (size_t i = 0; i < zones.size(); ++i)
        zones[i].colour = zoneColour ((int) i);

    return zones;
}

void SfzModulePanel::reloadZones (const juce::File& f)
{
    // Refresh layout so Save As button visibility reflects the new file type
    resized();

    const auto ext = f.getFileExtension().toLowerCase();
    std::vector<KeysPanel::Keyzone> zones;

    if (ext == ".sfz")
        zones = parseSfzZones (f);
    else if (ext == ".sf2")
    {
        // Look up the bank and program of the currently selected preset so we
        // only show zones that belong to it (not every zone in the entire file).
        int bank = 0, program = 0;
        const auto presets = processor.sfzPlayer.getPresetList();
        const int  idx     = processor.sfzPlayer.getCurrentPresetIndex();
        if (idx >= 0 && idx < (int) presets.size())
        {
            bank    = presets[(size_t) idx].bank;
            program = presets[(size_t) idx].preset;
        }
        zones = parseSf2Zones (f, bank, program);
    }

    keysPanel.setKeyzones (zones);
    if (! zones.empty())
        keysPanel.autoScrollToZones();

    // Wire zone-edit callback → SfzPlayer real-time OSC (SFZ only; SF2 is no-op)
    keysPanel.onZoneChanged = [this] (int zoneIndex, float volDb, float pan, float tuneCents)
    {
        processor.sfzPlayer.setZoneVolume (zoneIndex, volDb);
        processor.sfzPlayer.setZonePan    (zoneIndex, pan);
        processor.sfzPlayer.setZoneTune   (zoneIndex, tuneCents);
    };

    // [+ ZONE] is available whenever nothing is loaded OR an .sfz is loaded.
    // It must stay HIDDEN when an .sf2 is loaded — SF2 zones are read-only
    // (see parseSf2Zones above), so adding a zone on top of a loaded SF2
    // makes no sense. openAddZoneChooser() handles the "nothing loaded yet"
    // case by prompting Save-As after the sample is picked.
    const bool isSf2 = (ext == ".sf2");
    keysPanel.setAddZoneButtonVisible (! isSf2);
    if (isSf2)
        keysPanel.onAddZoneRequested = nullptr;
    else
        keysPanel.onAddZoneRequested = [this] { openAddZoneChooser(); };
}
