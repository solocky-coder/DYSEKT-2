#pragma once
// =============================================================================
//  Sf2MixerPanel.h  —  Per-channel mixer strip for active SF2 presets (header-only)
// =============================================================================
#include <juce_gui_basics/juce_gui_basics.h>
#include <unordered_map>
#include "DysektLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../audio/SfzPlayer.h"

struct Sf2PresetInfo;

// =============================================================================
class Sf2MixerPanel : public juce::Component
{
public:
    explicit Sf2MixerPanel (DysektProcessor& p) : processor (p) { setOpaque (false); }
    ~Sf2MixerPanel() override = default;

    void setActiveChannels (const std::vector<Sf2PresetInfo>& presets,
                            const std::unordered_map<int, int>& presetChannels)
    {
        strips.clear();
        for (auto& [presetIdx, midiCh1] : presetChannels)
        {
            if (midiCh1 < 1 || midiCh1 > 16) continue;
            ActiveStrip s;
            s.channel = midiCh1 - 1;
            s.midiCh  = midiCh1;
            if (presetIdx >= 0 && presetIdx < (int) presets.size())
                s.name = presets[(size_t) presetIdx].name;
            else
                s.name = "Ch " + juce::String (midiCh1);
            strips.push_back (s);
        }
        std::sort (strips.begin(), strips.end(),
                   [] (const ActiveStrip& a, const ActiveStrip& b) { return a.midiCh < b.midiCh; });
        layoutStrips();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& theme = getTheme();
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
            drawStrip (g, s, processor.sfzPlayer.getChannelStrip (s.channel));
    }

    void resized() override { layoutStrips(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto pos = e.getPosition();
        dragTarget = DragTarget::None; dragChannel = -1;
        for (const auto& s : strips)
        {
            if (s.muteBtn.contains (pos))
            {
                const auto st = processor.sfzPlayer.getChannelStrip (s.channel);
                processor.sfzPlayer.setChannelMuted (s.channel, ! st.muted);
                repaint(); return;
            }
            if (s.soloBtn.contains (pos))
            {
                if (soloedChannel == s.channel) { processor.sfzPlayer.clearSolo(); soloedChannel = -1; }
                else                            { processor.sfzPlayer.soloChannel (s.channel); soloedChannel = s.channel; }
                repaint(); return;
            }
            if (s.panKnob.contains (pos))
            {
                dragTarget = DragTarget::Pan; dragChannel = s.channel; dragStartY = pos.y;
                dragStartVal = (processor.sfzPlayer.getChannelStrip (s.channel).pan + 1.0f) * 0.5f;
                return;
            }
            if (s.revKnob.contains (pos))
            {
                dragTarget = DragTarget::Reverb; dragChannel = s.channel; dragStartY = pos.y;
                dragStartVal = processor.sfzPlayer.getChannelStrip (s.channel).reverbSend;
                return;
            }
            if (s.volFaderTrack.contains (pos))
            {
                dragTarget = DragTarget::Volume; dragChannel = s.channel; dragStartY = pos.y;
                const float n = 1.0f - (float)(pos.y - s.volFaderTrack.getY()) / (float) s.volFaderTrack.getHeight();
                dragStartVal = juce::jlimit (0.f, 1.f, n);
                processor.sfzPlayer.setChannelVolume (s.channel, dragStartVal);
                repaint(); return;
            }
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragTarget == DragTarget::None || dragChannel < 0) return;
        const float newNorm = juce::jlimit (0.f, 1.f, dragStartVal + (float)(dragStartY - e.getPosition().y) / 150.0f);
        switch (dragTarget)
        {
            case DragTarget::Volume:  processor.sfzPlayer.setChannelVolume     (dragChannel, newNorm); break;
            case DragTarget::Pan:     processor.sfzPlayer.setChannelPan        (dragChannel, newNorm * 2.0f - 1.0f); break;
            case DragTarget::Reverb:  processor.sfzPlayer.setChannelReverbSend (dragChannel, newNorm); break;
            default: break;
        }
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override { dragTarget = DragTarget::None; dragChannel = -1; }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto pos = e.getPosition();
        for (const auto& s : strips)
        {
            if (s.panKnob.contains (pos))      { processor.sfzPlayer.setChannelPan        (s.channel, 0.0f); repaint(); return; }
            if (s.revKnob.contains (pos))      { processor.sfzPlayer.setChannelReverbSend (s.channel, 0.0f); repaint(); return; }
            if (s.volFaderTrack.contains (pos)) { processor.sfzPlayer.setChannelVolume     (s.channel, 1.0f); repaint(); return; }
        }
    }

private:
    DysektProcessor& processor;

    struct ActiveStrip
    {
        int channel{-1}; juce::String name; int midiCh{0};
        juce::Rectangle<int> bounds, volFaderTrack, volFaderThumb,
                              panKnob, revKnob, muteBtn, soloBtn, nameLbl, chBadge;
    };
    std::vector<ActiveStrip> strips;

    enum class DragTarget { None, Volume, Pan, Reverb };
    DragTarget dragTarget { DragTarget::None };
    int   dragChannel { -1 }, dragStartY { 0 };
    float dragStartVal{ 0.f };
    int   soloedChannel { -1 };

    static constexpr int kStripMinW = 64, kStripMaxW = 96;

    void layoutStrips()
    {
        if (strips.empty()) return;
        const int w = getWidth(), h = getHeight();
        if (w <= 0 || h <= 0) return;
        const int n = (int) strips.size();
        const int stripW = juce::jlimit (kStripMinW, kStripMaxW, w / n);
        int startX = (w - stripW * n) / 2;
        for (auto& s : strips)
        {
            s.bounds = juce::Rectangle<int> (startX, 0, stripW, h);
            startX += stripW;
            auto r = s.bounds.reduced (4, 4);
            s.nameLbl = r.removeFromTop (16);
            s.chBadge = s.nameLbl.removeFromRight (22);
            r.removeFromTop (2);
            auto btnRow = r.removeFromTop (18);
            s.muteBtn = btnRow.removeFromLeft (btnRow.getWidth() / 2 - 1);
            s.soloBtn = btnRow.removeFromRight (btnRow.getWidth());
            r.removeFromTop (4);
            s.panKnob = r.removeFromTop (34).withSizeKeepingCentre (34, 34);
            r.removeFromTop (2);
            s.revKnob = r.removeFromTop (34).withSizeKeepingCentre (34, 34);
            r.removeFromTop (4);
            s.volFaderTrack = r;
        }
    }

    void drawStrip (juce::Graphics& g, const ActiveStrip& s, const SfzPlayer::ChannelStrip& state) const
    {
        const auto& theme = getTheme();
        const bool soloed = (soloedChannel == s.channel);
        g.setColour (soloed ? theme.accent.withAlpha (0.08f) : theme.darkBar.darker (0.15f));
        g.fillRoundedRectangle (s.bounds.reduced (2).toFloat(), 3.0f);
        g.setColour (theme.accent.withAlpha (0.12f));
        g.fillRect (s.bounds.getRight() - 1, s.bounds.getY() + 4, 1, s.bounds.getHeight() - 8);

        g.setFont (DysektLookAndFeel::makeFont (10.5f));
        g.setColour (theme.foreground.withAlpha (0.70f));
        g.drawText (s.name, s.nameLbl, juce::Justification::centredLeft, true);

        g.setColour (theme.accent.withAlpha (0.22f));
        g.fillRoundedRectangle (s.chBadge.toFloat(), 2.0f);
        g.setFont (DysektLookAndFeel::makeFont (9.5f, true));
        g.setColour (theme.accent.withAlpha (0.90f));
        g.drawText (juce::String (s.midiCh), s.chBadge, juce::Justification::centred, false);

        const bool muted = state.muted;
        g.setColour (muted ? juce::Colour (0xFFFF6B6B).withAlpha (0.85f) : theme.darkBar.brighter (0.20f));
        g.fillRoundedRectangle (s.muteBtn.toFloat(), 2.0f);
        g.setFont (DysektLookAndFeel::makeFont (9.5f, true));
        g.setColour (muted ? juce::Colours::white : theme.foreground.withAlpha (0.65f));
        g.drawText ("M", s.muteBtn, juce::Justification::centred, false);

        g.setColour (soloed ? juce::Colour (0xFFFFD93D).withAlpha (0.85f) : theme.darkBar.brighter (0.20f));
        g.fillRoundedRectangle (s.soloBtn.toFloat(), 2.0f);
        g.setFont (DysektLookAndFeel::makeFont (9.5f, true));
        g.setColour (soloed ? juce::Colours::black : theme.foreground.withAlpha (0.65f));
        g.drawText ("S", s.soloBtn, juce::Justification::centred, false);

        drawKnob (g, s.panKnob,  (state.pan + 1.0f) * 0.5f, "PAN");
        drawKnob (g, s.revKnob,  state.reverbSend,           "REV");
        drawFader (g, s, state);
    }

    void drawKnob (juce::Graphics& g, juce::Rectangle<int> bounds, float normalised, const juce::String& label) const
    {
        const auto& theme = getTheme();
        const float r  = (float) juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.45f;
        const float cx = (float) bounds.getCentreX(), cy = (float) bounds.getCentreY();
        const float startA = juce::MathConstants<float>::pi * 1.25f;
        const float endA   = juce::MathConstants<float>::pi * 2.75f;
        const float angle  = startA + normalised * (endA - startA);

        juce::Path track; track.addCentredArc (cx, cy, r, r, 0.f, startA, endA, true);
        g.setColour (theme.darkBar.brighter (0.15f));
        g.strokePath (track, juce::PathStrokeType (2.0f));

        juce::Path fill; fill.addCentredArc (cx, cy, r, r, 0.f, startA, angle, true);
        g.setColour (theme.accent);
        g.strokePath (fill, juce::PathStrokeType (2.0f));

        const float tx = cx + (r - 4.f) * std::cos (angle - juce::MathConstants<float>::halfPi);
        const float ty = cy + (r - 4.f) * std::sin (angle - juce::MathConstants<float>::halfPi);
        g.setColour (theme.accent.brighter (0.3f));
        g.fillEllipse (tx - 2.f, ty - 2.f, 4.f, 4.f);

        g.setFont (DysektLookAndFeel::makeFont (9.0f));
        g.setColour (theme.foreground.withAlpha (0.40f));
        g.drawText (label, bounds.getX(), bounds.getBottom() - 12, bounds.getWidth(), 12,
                    juce::Justification::centred, false);
    }

    void drawFader (juce::Graphics& g, const ActiveStrip& s, const SfzPlayer::ChannelStrip& state) const
    {
        const auto& theme = getTheme();
        const auto& track = s.volFaderTrack;
        if (track.getHeight() < 10) return;
        const int trackX = track.getCentreX() - 3, trackW = 6;
        g.setColour (theme.darkBar.brighter (0.10f));
        g.fillRoundedRectangle ((float) trackX, (float) track.getY(), (float) trackW, (float) track.getHeight(), 3.0f);

        const float vol  = state.muted ? state.preMuteVol : state.volume;
        const int fillH  = juce::roundToInt ((float) track.getHeight() * vol);
        const int fillY  = track.getBottom() - fillH;
        g.setColour (state.muted ? theme.accent.withAlpha (0.30f) : theme.accent.withAlpha (0.55f));
        if (fillH > 0)
            g.fillRoundedRectangle ((float) trackX, (float) fillY, (float) trackW, (float) fillH, 3.0f);

        g.setColour (state.muted ? theme.foreground.withAlpha (0.30f) : theme.foreground.withAlpha (0.85f));
        g.fillRoundedRectangle ((float)(trackX - 5), (float)(fillY - 4), (float)(trackW + 10), 8.0f, 2.0f);

        const float db = juce::Decibels::gainToDecibels (vol);
        g.setFont (DysektLookAndFeel::makeFont (9.0f));
        g.setColour (theme.foreground.withAlpha (0.45f));
        g.drawText (db <= -95.f ? "-inf" : juce::String (db, 1),
                    track.getX(), track.getY() - 11, track.getWidth(), 10,
                    juce::Justification::centred, false);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sf2MixerPanel)
};
