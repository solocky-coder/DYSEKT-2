#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../sequencer/SequencerEngine.h"

//==============================================================================
//  TrackHeaderStrip — vertical list of track headers.
//  Shows: colour swatch | track name | MIDI-RX dot | mute button
//
//  MIDI receive indicator blinks green when the sequencer fires notes on that
//  track.  The 10-Hz timer polls SequencerEngine::getMidiActivityAndClear().
//==============================================================================
class TrackHeaderStrip : public juce::Component,
                         private juce::Timer
{
public:
    explicit TrackHeaderStrip (SequencerEngine& seq) : engine (seq) { startTimerHz (10); }
    ~TrackHeaderStrip() override { stopTimer(); }

    void setTrackHeight (int h) { trackH = juce::jmax (18, h); repaint(); }
    int  getTrackHeight()       const noexcept { return trackH; }
    int  getSelectedTrack()     const noexcept { return selectedTrack; }
    void setSelectedTrack (int i)              { selectedTrack = i; repaint(); }

    std::function<void(int)>            onTrackSelected;
    std::function<void(int, bool)>      onTrackMuted;
    std::function<void(int, int)>       onSfTrackChannelChanged;  // trackIdx, ch 1-based

    int getRequiredHeight() const { return engine.getNumTracks() * trackH; }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF0A0A10));
        const int n = engine.getNumTracks();
        for (int i = 0; i < n; ++i)
        {
            const auto info  = engine.getTrackInfo (i);
            const auto rowR  = getRowBounds (i);
            const bool sel   = (i == selectedTrack);

            g.setColour (sel ? juce::Colour (0xFF182030) : juce::Colour (0xFF0D0D16));
            g.fillRect (rowR);

            g.setColour (info.colour);
            g.fillRect (rowR.withTrimmedRight (rowR.getWidth() - 4).toFloat());

            // Mute button
            const int muteW  = juce::jlimit (20, 28, trackH - 8);
            const int muteH  = juce::jlimit (12, 18, trackH - 8);
            const auto muteR = rowR.withTrimmedLeft (rowR.getWidth() - muteW - 4)
                                   .withSizeKeepingCentre (muteW, muteH);
            g.setColour (info.enabled ? juce::Colour (0xFF2A8060) : juce::Colour (0xFF602020));
            g.fillRoundedRectangle (muteR.toFloat(), 3.f);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.setFont (juce::Font (juce::jlimit (10.5f, 16.5f, (float)trackH * 0.22f), juce::Font::bold));
            g.drawText (info.enabled ? "M" : "m", muteR, juce::Justification::centred, false);

            // MIDI RX dot
            const bool rxActive  = (i < kMaxTracks && midiHoldCounters[i] > 0);
            const int  dotR      = juce::jlimit (4, 7, trackH / 8);
            const auto dotCentre = juce::Point<int> (muteR.getX() - dotR - 4, rowR.getCentreY());
            g.setColour (rxActive ? juce::Colour (0xFF00FF88) : juce::Colour (0xFF223030));
            g.fillEllipse ((float)(dotCentre.x - dotR), (float)(dotCentre.y - dotR),
                           (float)(dotR * 2), (float)(dotR * 2));

            // Track name
            g.setFont (juce::Font (juce::jlimit (16.5f, 22.5f, (float)trackH * 0.30f), juce::Font::bold));
            g.setColour (sel ? info.colour : juce::Colour (0xFFCCD0D8));
            g.drawText (info.name, rowR.getX() + 6, rowR.getY(),
                        rowR.getWidth() - muteW - 12, trackH,
                        juce::Justification::centredLeft, true);

            // Type + channel badge
            if (trackH >= 32)
            {
                juce::String badge;
                switch (info.type)
                {
                    case TrackType::MainSlice:      badge = "SL"; break;
                    case TrackType::ChromaticSlice: badge = "CH"; break;
                    case TrackType::SfPlayer:       badge = "SF"; break;
                }
                g.setFont (juce::Font (juce::jlimit (12.0f, 16.5f, (float)trackH * 0.18f)));
                g.setColour (info.colour.withAlpha (0.6f));
                g.drawText (badge, rowR.getX() + 6, rowR.getCentreY(), 24, trackH / 2,
                            juce::Justification::centredLeft, false);

                if (info.type == TrackType::SfPlayer || info.type == TrackType::ChromaticSlice)
                {
                    g.setColour (info.colour.withAlpha (0.85f));
                    g.drawText ("CH" + juce::String (info.midiChannel + 1),
                                rowR.getX() + 32, rowR.getCentreY(), 44, trackH / 2,
                                juce::Justification::centredLeft, false);
                }
            }

            g.setColour (juce::Colour (0xFF1C2028));
            g.fillRect (0, rowR.getBottom() - 1, getWidth(), 1);
        }
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = e.y / trackH;
        if (! juce::isPositiveAndBelow (i, engine.getNumTracks())) return;
        const auto info = engine.getTrackInfo (i);

        if (e.mods.isRightButtonDown())
        {
            showContextMenu (i, info, e.getScreenPosition());
            return;
        }

        const auto rowR  = getRowBounds (i);
        const int muteW  = juce::jlimit (20, 28, trackH - 8);
        const int muteH  = juce::jlimit (12, 18, trackH - 8);
        const auto muteR = rowR.withTrimmedLeft (rowR.getWidth() - muteW - 4)
                               .withSizeKeepingCentre (muteW, muteH);

        if (muteR.contains (e.getPosition()))
        {
            engine.setTrackEnabled (i, ! info.enabled);
            if (onTrackMuted) onTrackMuted (i, ! info.enabled);
        }
        else
        {
            selectedTrack = i;
            if (onTrackSelected) onTrackSelected (i);
        }
        repaint();
    }

private:
    void showContextMenu (int idx, const SequencerTrackInfo& info, juce::Point<int> pos)
    {
        if (info.type != TrackType::SfPlayer) return;

        juce::PopupMenu menu;
        menu.addSectionHeader ("MIDI Channel – " + info.name);
        for (int ch = 1; ch <= 16; ++ch)
            menu.addItem (ch, "Channel " + juce::String (ch), true, ch == info.midiChannel + 1);

        const int ti = idx;
        menu.showMenuAsync (
            juce::PopupMenu::Options()
                .withTargetComponent (this)
                .withTargetScreenArea ({ pos.x, pos.y, 1, 1 }),
            [this, ti, info] (int result)
            {
                if (result >= 1 && result <= 16 && info.type == TrackType::SfPlayer)
                    if (onSfTrackChannelChanged) onSfTrackChannelChanged (ti, result);
            });
    }

    juce::Rectangle<int> getRowBounds (int i) const
    {
        return { 0, i * trackH, getWidth(), trackH };
    }

    void timerCallback() override
    {
        const int n = juce::jmin (engine.getNumTracks(), kMaxTracks);
        bool needsRepaint = false;
        for (int i = 0; i < n; ++i)
        {
            if (engine.getMidiActivityAndClear (i))
            {
                midiHoldCounters[i] = kHoldTicks;
                needsRepaint = true;
            }
            else if (midiHoldCounters[i] > 0)
            {
                --midiHoldCounters[i];
                needsRepaint = true;
            }
        }
        if (needsRepaint) repaint();
    }

    SequencerEngine& engine;
    int selectedTrack = 0;
    int trackH        = 54;

    static constexpr int kMaxTracks = SequencerEngine::kActivityFlagCount;
    static constexpr int kHoldTicks = 3;
    int midiHoldCounters[kMaxTracks] = {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeaderStrip)
};
