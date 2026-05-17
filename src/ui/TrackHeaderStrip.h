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
    explicit TrackHeaderStrip (SequencerEngine& seq)
        : engine (seq)
    {
        startTimerHz (10);
    }

    ~TrackHeaderStrip() override { stopTimer(); }

    /** Called by ArrangeView whenever its trackH changes so rows stay perfectly in sync. */
    void setTrackHeight (int h)
    {
        trackH = juce::jmax (18, h);
        repaint();
    }

    int getTrackHeight() const noexcept { return trackH; }

    int  getSelectedTrack() const noexcept { return selectedTrack; }
    void setSelectedTrack (int i) { selectedTrack = i; repaint(); }

    std::function<void(int trackIndex)> onTrackSelected;
    std::function<void(int trackIndex, bool enabled)> onTrackMuted;

    //==========================================================================
    int getRequiredHeight() const
    {
        return engine.getNumTracks() * trackH;
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
            g.fillRect (rowR.withTrimmedRight (rowR.getWidth() - 4).toFloat());

            // Mute button — scales with row height
            const int muteW  = juce::jlimit (20, 28, trackH - 8);
            const int muteH  = juce::jlimit (12, 18, trackH - 8);
            const auto muteR = rowR.withTrimmedLeft (rowR.getWidth() - muteW - 4)
                                   .withSizeKeepingCentre (muteW, muteH);
            g.setColour (info.enabled
                ? juce::Colour (0xFF2A8060)
                : juce::Colour (0xFF602020));
            g.fillRoundedRectangle (muteR.toFloat(), 3.f);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            const float muteFontSz = juce::jlimit (10.5f, 16.5f, (float)trackH * 0.22f);
            g.setFont (juce::Font (muteFontSz, juce::Font::bold));
            g.drawText (info.enabled ? "M" : "m", muteR,
                        juce::Justification::centred, false);

            // Track name — centred vertically in full row height
            const float nameFontSz = juce::jlimit (16.5f, 22.5f, (float)trackH * 0.30f);
            g.setFont (juce::Font (nameFontSz, juce::Font::bold));
            g.setColour (sel ? info.colour : juce::Colour (0xFFCCD0D8));
            g.drawText (info.name,
                        rowR.getX() + 6, rowR.getY(),
                        rowR.getWidth() - muteW - 12, trackH,
                        juce::Justification::centredLeft, true);

            // Track type badge — bottom-left quarter of row
            juce::String badge;
            switch (info.type)
            {
                case TrackType::MainSlice:      badge = "SL";  break;
                case TrackType::ChromaticSlice: badge = "CH";  break;
                case TrackType::SfPlayer:       badge = "SF";  break;
            }
            if (trackH >= 32)
            {
                const float badgeFontSz = juce::jlimit (12.0f, 16.5f, (float)trackH * 0.18f);
                g.setFont (juce::Font (badgeFontSz));
                g.setColour (info.colour.withAlpha (0.6f));
                g.drawText (badge,
                            rowR.getX() + 6, rowR.getCentreY(),
                            24, trackH / 2,
                            juce::Justification::centredLeft, false);
            }

            // Separator
            g.setColour (juce::Colour (0xFF1C2028));
            g.fillRect (0, rowR.getBottom() - 1, getWidth(), 1);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = e.y / trackH;
        if (! juce::isPositiveAndBelow (i, engine.getNumTracks())) return;

        // Check mute button hit
        const auto rowR  = getRowBounds (i);
        const int muteW  = juce::jlimit (20, 28, trackH - 8);
        const int muteH  = juce::jlimit (12, 18, trackH - 8);
        const auto muteR = rowR.withTrimmedLeft (rowR.getWidth() - muteW - 4)
                               .withSizeKeepingCentre (muteW, muteH);

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
    int trackH        = 54;   // default matches ArrangeView::kDefaultTrackH; updated via setTrackHeight()

    juce::Rectangle<int> getRowBounds (int i) const
    {
        return { 0, i * trackH, getWidth(), trackH };
    }

    void timerCallback() override { repaint(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeaderStrip)
};
