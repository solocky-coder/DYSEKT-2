#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../sequencer/SequencerEngine.h"

//==============================================================================
//  TrackHeaderStrip
//
//  Vertical list of track headers, one row per track, same height as the
//  piano roll grid rows but grouped rather than per-note.
//  Shows: colour swatch | track name | mute button
//  Clicking a row selects that track for editing in the piano roll.
//==============================================================================
class TrackHeaderStrip : public juce::Component,
                         private juce::Timer
{
public:
    static constexpr int kTrackH = 36;

    explicit TrackHeaderStrip (SequencerEngine& seq)
        : engine (seq)
    {
        startTimerHz (10);
    }

    ~TrackHeaderStrip() override { stopTimer(); }

    int  getSelectedTrack() const noexcept { return selectedTrack; }
    void setSelectedTrack (int i) { selectedTrack = i; repaint(); }

    std::function<void(int trackIndex)> onTrackSelected;
    std::function<void(int trackIndex, bool enabled)> onTrackMuted;

    //==========================================================================
    int getRequiredHeight() const
    {
        return engine.getNumTracks() * kTrackH;
    }

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

            // Background
            g.setColour (sel ? juce::Colour (0xFF182030)
                             : juce::Colour (0xFF0D0D16));
            g.fillRect (rowR);

            // Colour swatch
            g.setColour (info.colour);
            g.fillRect (rowR.removeFromLeft (4).toFloat());

            // Mute indicator
            const auto muteR = rowR.withTrimmedLeft (rowR.getWidth() - 28)
                                   .reduced (4, 8);
            g.setColour (info.enabled
                ? juce::Colour (0xFF2A8060)
                : juce::Colour (0xFF602020));
            g.fillRoundedRectangle (muteR.toFloat(), 3.f);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.setFont (juce::Font (8.f, juce::Font::bold));
            g.drawText (info.enabled ? "M" : "m", muteR,
                        juce::Justification::centred, false);

            // Track name
            g.setFont (juce::Font (10.f, juce::Font::bold));
            g.setColour (sel ? info.colour : juce::Colour (0xFFCCD0D8));
            g.drawText (info.name,
                        rowR.getX() + 6, rowR.getY(),
                        rowR.getWidth() - 34, kTrackH,
                        juce::Justification::centredLeft, true);

            // Track type badge
            juce::String badge;
            switch (info.type)
            {
                case TrackType::MainSlice:      badge = "SL";  break;
                case TrackType::ChromaticSlice: badge = "CH";  break;
                case TrackType::SfPlayer:       badge = "SF";  break;
            }
            g.setFont (juce::Font (8.f));
            g.setColour (info.colour.withAlpha (0.7f));
            g.drawText (badge,
                        rowR.getX() + 6, rowR.getY() + kTrackH / 2,
                        20, kTrackH / 2,
                        juce::Justification::centredLeft, false);

            // Separator
            g.setColour (juce::Colour (0xFF1C2028));
            g.fillRect (0, rowR.getBottom() - 1, getWidth(), 1);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = e.y / kTrackH;
        if (! juce::isPositiveAndBelow (i, engine.getNumTracks())) return;

        // Check mute button hit
        const auto rowR  = getRowBounds (i);
        const auto muteR = rowR.withTrimmedLeft (rowR.getWidth() - 28).reduced (4, 8);

        if (muteR.contains (e.getPosition()))
        {
            const auto info = engine.getTrackInfo (i);
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
    SequencerEngine& engine;
    int selectedTrack = 0;

    juce::Rectangle<int> getRowBounds (int i) const
    {
        return { 0, i * kTrackH, getWidth(), kTrackH };
    }

    void timerCallback() override { repaint(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeaderStrip)
};
