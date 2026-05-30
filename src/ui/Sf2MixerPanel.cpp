// =============================================================================
//  Sf2MixerPanel.cpp  —  Per-channel mixer for active SF2 presets
// =============================================================================
#include "Sf2MixerPanel.h"
#include "DysektLookAndFeel.h"
#include "../PluginProcessor.h"

// =============================================================================
Sf2MixerPanel::Sf2MixerPanel (DysektProcessor& p)
    : processor (p)
{
    setOpaque (false);
}

// =============================================================================
//  Public API
// =============================================================================

void Sf2MixerPanel::setActiveChannels (const std::vector<Sf2PresetInfo>& presets,
                                        const std::unordered_map<int, int>&          presetChannels)
{
    strips.clear();

    for (auto& [presetIdx, midiCh1] : presetChannels)
    {
        if (midiCh1 < 1 || midiCh1 > 16) continue;
        const int fsCh = midiCh1 - 1;   // FluidSynth channel (0-based)

        ActiveStrip s;
        s.channel = fsCh;
        s.midiCh  = midiCh1;

        if (presetIdx >= 0 && presetIdx < (int) presets.size())
            s.name = presets[(size_t) presetIdx].name;
        else
            s.name = "Ch " + juce::String (midiCh1);

        strips.push_back (s);
    }

    // Sort by MIDI channel for a consistent display order
    std::sort (strips.begin(), strips.end(),
               [] (const ActiveStrip& a, const ActiveStrip& b) { return a.midiCh < b.midiCh; });

    layoutStrips();
    repaint();
}

// =============================================================================
//  Layout
// =============================================================================

void Sf2MixerPanel::layoutStrips()
{
    if (strips.empty()) return;

    const int w = getWidth();
    const int h = getHeight();
    if (w <= 0 || h <= 0) return;

    const int n = (int) strips.size();
    const int stripW = juce::jlimit (kStripMinW, kStripMaxW, w / n);
    const int totalW = stripW * n;
    int startX = (w - totalW) / 2;

    for (auto& s : strips)
    {
        s.bounds = juce::Rectangle<int> (startX, 0, stripW, h);
        startX += stripW;

        auto r = s.bounds.reduced (4, 4);

        // Top: name label + channel badge
        s.nameLbl = r.removeFromTop (16);
        s.chBadge = s.nameLbl.removeFromRight (22);

        r.removeFromTop (2);

        // Mute / Solo buttons row
        const int btnH = 18;
        auto btnRow = r.removeFromTop (btnH);
        s.muteBtn = btnRow.removeFromLeft (btnRow.getWidth() / 2 - 1);
        s.soloBtn = btnRow.removeFromRight (btnRow.getWidth());

        r.removeFromTop (4);

        // Pan knob
        const int knobSz = 34;
        s.panKnob = r.removeFromTop (knobSz).withSizeKeepingCentre (knobSz, knobSz);

        r.removeFromTop (2);

        // Reverb knob
        s.revKnob = r.removeFromTop (knobSz).withSizeKeepingCentre (knobSz, knobSz);

        r.removeFromTop (4);

        // Volume fader — rest of space
        s.volFaderTrack = r;
    }
}

// =============================================================================
//  Paint
// =============================================================================

void Sf2MixerPanel::paint (juce::Graphics& g)
{
    const auto& theme = getTheme();

    // Background
    g.setColour (theme.darkBar.darker (0.35f));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    if (strips.empty())
    {
        g.setFont (DysektLookAndFeel::makeFont (12.0f));
        g.setColour (theme.foreground.withAlpha (0.40f));
        g.drawText ("No presets assigned  —  right-click a preset in the grid to assign a MIDI channel",
                    getLocalBounds(), juce::Justification::centred, true);
        return;
    }

    for (const auto& s : strips)
    {
        const auto state = processor.sfzPlayer.getChannelStrip (s.channel);
        drawStrip (g, s, state);
    }
}

void Sf2MixerPanel::drawStrip (juce::Graphics& g, const ActiveStrip& s,
                                const SfzPlayer::ChannelStrip& state) const
{
    const auto& theme = getTheme();

    // Strip background
    const bool soloed = (soloedChannel == s.channel);
    g.setColour (soloed ? theme.accent.withAlpha (0.08f)
                        : theme.darkBar.darker (0.15f));
    g.fillRoundedRectangle (s.bounds.reduced (2).toFloat(), 3.0f);

    // Separator line on right
    g.setColour (theme.accent.withAlpha (0.12f));
    g.fillRect (s.bounds.getRight() - 1, s.bounds.getY() + 4, 1, s.bounds.getHeight() - 8);

    // ── Name label ─────────────────────────────────────────────────────────────
    g.setFont (DysektLookAndFeel::makeFont (10.5f));
    g.setColour (theme.foreground.withAlpha (0.70f));
    g.drawText (s.name, s.nameLbl, juce::Justification::centredLeft, true);

    // ── Channel badge ──────────────────────────────────────────────────────────
    {
        g.setColour (theme.accent.withAlpha (0.22f));
        g.fillRoundedRectangle (s.chBadge.toFloat(), 2.0f);
        g.setFont (DysektLookAndFeel::makeFont (9.5f, true));
        g.setColour (theme.accent.withAlpha (0.90f));
        g.drawText (juce::String (s.midiCh), s.chBadge, juce::Justification::centred, false);
    }

    // ── Mute button ────────────────────────────────────────────────────────────
    {
        const bool muted = state.muted;
        g.setColour (muted ? juce::Colour (0xFFFF6B6B).withAlpha (0.85f)
                           : theme.darkBar.brighter (0.20f));
        g.fillRoundedRectangle (s.muteBtn.toFloat(), 2.0f);
        g.setFont (DysektLookAndFeel::makeFont (9.5f, true));
        g.setColour (muted ? juce::Colours::white : theme.foreground.withAlpha (0.65f));
        g.drawText ("M", s.muteBtn, juce::Justification::centred, false);
    }

    // ── Solo button ────────────────────────────────────────────────────────────
    {
        g.setColour (soloed ? juce::Colour (0xFFFFD93D).withAlpha (0.85f)
                            : theme.darkBar.brighter (0.20f));
        g.fillRoundedRectangle (s.soloBtn.toFloat(), 2.0f);
        g.setFont (DysektLookAndFeel::makeFont (9.5f, true));
        g.setColour (soloed ? juce::Colours::black : theme.foreground.withAlpha (0.65f));
        g.drawText ("S", s.soloBtn, juce::Justification::centred, false);
    }

    // ── Pan knob ───────────────────────────────────────────────────────────────
    {
        const float panNorm = (state.pan + 1.0f) * 0.5f;   // 0..1
        drawKnob (g, s.panKnob, panNorm, "PAN");
    }

    // ── Reverb send knob ───────────────────────────────────────────────────────
    {
        drawKnob (g, s.revKnob, state.reverbSend, "REV");
    }

    // ── Volume fader ───────────────────────────────────────────────────────────
    drawFader (g, s, state);
}

void Sf2MixerPanel::drawKnob (juce::Graphics& g, juce::Rectangle<int> bounds,
                               float normalised, const juce::String& label) const
{
    const auto& theme = getTheme();
    const float r = (float) juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.45f;
    const float cx = (float) bounds.getCentreX();
    const float cy = (float) bounds.getCentreY();

    const float startA = juce::MathConstants<float>::pi * 1.25f;
    const float endA   = juce::MathConstants<float>::pi * 2.75f;
    const float angle  = startA + normalised * (endA - startA);

    // Track
    juce::Path track;
    track.addCentredArc (cx, cy, r, r, 0.f, startA, endA, true);
    g.setColour (theme.darkBar.brighter (0.15f));
    g.strokePath (track, juce::PathStrokeType (2.0f));

    // Fill
    juce::Path fill;
    fill.addCentredArc (cx, cy, r, r, 0.f, startA, angle, true);
    g.setColour (theme.accent);
    g.strokePath (fill, juce::PathStrokeType (2.0f));

    // Dot
    const float tx = cx + (r - 4.f) * std::cos (angle - juce::MathConstants<float>::halfPi);
    const float ty = cy + (r - 4.f) * std::sin (angle - juce::MathConstants<float>::halfPi);
    g.setColour (theme.accent.brighter (0.3f));
    g.fillEllipse (tx - 2.f, ty - 2.f, 4.f, 4.f);

    // Label below
    g.setFont (DysektLookAndFeel::makeFont (9.0f));
    g.setColour (theme.foreground.withAlpha (0.40f));
    g.drawText (label,
                bounds.getX(), bounds.getBottom() - 12,
                bounds.getWidth(), 12,
                juce::Justification::centred, false);
}

void Sf2MixerPanel::drawFader (juce::Graphics& g, const ActiveStrip& s,
                                const SfzPlayer::ChannelStrip& state) const
{
    const auto& theme = getTheme();
    const auto& track = s.volFaderTrack;
    if (track.getHeight() < 10) return;

    const int trackX = track.getCentreX() - 3;
    const int trackW = 6;

    // Track background
    g.setColour (theme.darkBar.brighter (0.10f));
    g.fillRoundedRectangle ((float) trackX, (float) track.getY(),
                             (float) trackW, (float) track.getHeight(), 3.0f);

    // Fill (bottom up to thumb)
    const float vol = state.muted ? state.preMuteVol : state.volume;
    const int   fillH = juce::roundToInt ((float) track.getHeight() * vol);
    const int   fillY = track.getBottom() - fillH;

    const bool muted = state.muted;
    g.setColour (muted ? theme.accent.withAlpha (0.30f) : theme.accent.withAlpha (0.55f));
    if (fillH > 0)
        g.fillRoundedRectangle ((float) trackX, (float) fillY,
                                 (float) trackW, (float) fillH, 3.0f);

    // Thumb
    const int thumbH = 8;
    const int thumbY = fillY - thumbH / 2;
    const int thumbX = trackX - 5;
    const int thumbW = trackW + 10;
    g.setColour (muted ? theme.foreground.withAlpha (0.30f) : theme.foreground.withAlpha (0.85f));
    g.fillRoundedRectangle ((float) thumbX, (float) thumbY, (float) thumbW, (float) thumbH, 2.0f);

    // dB label above fader
    const float db = juce::Decibels::gainToDecibels (vol);
    const juce::String dbStr = (db <= -95.f) ? "-inf" : juce::String (db, 1);
    g.setFont (DysektLookAndFeel::makeFont (9.0f));
    g.setColour (theme.foreground.withAlpha (0.45f));
    g.drawText (dbStr, track.getX(), track.getY() - 11, track.getWidth(), 10,
                juce::Justification::centred, false);
}

// =============================================================================
//  resized
// =============================================================================

void Sf2MixerPanel::resized()
{
    layoutStrips();
}

// =============================================================================
//  Mouse events
// =============================================================================

void Sf2MixerPanel::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    dragTarget  = DragTarget::None;
    dragChannel = -1;

    for (const auto& s : strips)
    {
        // Mute button
        if (s.muteBtn.contains (pos))
        {
            const auto state = processor.sfzPlayer.getChannelStrip (s.channel);
            processor.sfzPlayer.setChannelMuted (s.channel, ! state.muted);
            repaint();
            return;
        }

        // Solo button
        if (s.soloBtn.contains (pos))
        {
            if (soloedChannel == s.channel)
            {
                processor.sfzPlayer.clearSolo();
                soloedChannel = -1;
            }
            else
            {
                processor.sfzPlayer.soloChannel (s.channel);
                soloedChannel = s.channel;
            }
            repaint();
            return;
        }

        // Pan knob
        if (s.panKnob.contains (pos))
        {
            const auto state = processor.sfzPlayer.getChannelStrip (s.channel);
            dragTarget  = DragTarget::Pan;
            dragChannel = s.channel;
            dragStartY  = pos.y;
            dragStartVal = (state.pan + 1.0f) * 0.5f;
            return;
        }

        // Reverb knob
        if (s.revKnob.contains (pos))
        {
            const auto state = processor.sfzPlayer.getChannelStrip (s.channel);
            dragTarget  = DragTarget::Reverb;
            dragChannel = s.channel;
            dragStartY  = pos.y;
            dragStartVal = state.reverbSend;
            return;
        }

        // Volume fader
        if (s.volFaderTrack.contains (pos))
        {
            dragTarget  = DragTarget::Volume;
            dragChannel = s.channel;
            dragStartY  = pos.y;
            // Click anywhere on the fader: jump to click position
            const float clickNorm = 1.0f - (float)(pos.y - s.volFaderTrack.getY())
                                           / (float) s.volFaderTrack.getHeight();
            dragStartVal = juce::jlimit (0.f, 1.f, clickNorm);
            processor.sfzPlayer.setChannelVolume (s.channel, dragStartVal);
            repaint();
            return;
        }
    }
}

void Sf2MixerPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragTarget == DragTarget::None || dragChannel < 0) return;

    const float delta   = (float)(dragStartY - e.getPosition().y) / 150.0f;
    const float newNorm = juce::jlimit (0.f, 1.f, dragStartVal + delta);

    switch (dragTarget)
    {
        case DragTarget::Volume:
            processor.sfzPlayer.setChannelVolume (dragChannel, newNorm);
            break;
        case DragTarget::Pan:
            processor.sfzPlayer.setChannelPan (dragChannel, newNorm * 2.0f - 1.0f);
            break;
        case DragTarget::Reverb:
            processor.sfzPlayer.setChannelReverbSend (dragChannel, newNorm);
            break;
        default:
            break;
    }
    repaint();
}

void Sf2MixerPanel::mouseUp (const juce::MouseEvent&)
{
    dragTarget  = DragTarget::None;
    dragChannel = -1;
}

void Sf2MixerPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();

    for (const auto& s : strips)
    {
        if (s.panKnob.contains (pos))
        {
            processor.sfzPlayer.setChannelPan (s.channel, 0.0f);
            repaint();
            return;
        }
        if (s.revKnob.contains (pos))
        {
            processor.sfzPlayer.setChannelReverbSend (s.channel, 0.0f);
            repaint();
            return;
        }
        if (s.volFaderTrack.contains (pos))
        {
            processor.sfzPlayer.setChannelVolume (s.channel, 1.0f);
            repaint();
            return;
        }
    }
}
