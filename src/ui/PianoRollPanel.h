#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "TransportBar.h"
#include "PianoRollComponent.h"
#include "TrackHeaderStrip.h"
#include "../sequencer/SequencerEngine.h"

//==============================================================================
//  PianoRollPanel  (multi-track revision)
//
//  Layout:
//    ┌─────────────────────────────────────────────────────┐
//    │  TransportBar                                        │
//    ├──────────────┬──────────────────────────────────────┤
//    │              │                                       │
//    │  Track       │   PianoRollComponent                 │
//    │  Header      │   (edits active track's clip)        │
//    │  Strip       │                                       │
//    │  (160px)     │                                       │
//    └──────────────┴──────────────────────────────────────┘
//
//  Track selection in the strip switches the active clip in the piano roll.
//==============================================================================
class PianoRollPanel : public juce::Component
{
public:
    static constexpr int kTransportH   = 34;
    static constexpr int kTrackStripW  = 160;

    PianoRollPanel (SequencerEngine& seq, AbletonLink* link = nullptr)
        : engine (seq), transport (seq, link), pianoRoll (seq), trackStrip (seq)
    {
        addAndMakeVisible (transport);
        addAndMakeVisible (trackStrip);
        addAndMakeVisible (pianoRoll);

        // Ensure main track exists
        engine.addMainTrack();

        // Wire track selection
        trackStrip.onTrackSelected = [this] (int idx)
        {
            pianoRoll.setActiveTrack (idx, 0);
        };

        // Bubble up SF track channel changes so PluginEditor can update the engine.
        trackStrip.onSfTrackChannelChanged = [this] (int trackIndex, int ch1Based)
        {
            if (onSfTrackChannelChanged)
                onSfTrackChannelChanged (trackIndex, ch1Based);
        };
    }

    void resized() override
    {
        auto r = getLocalBounds();
        transport.setBounds (r.removeFromTop (kTransportH));
        trackStrip.setBounds (r.removeFromLeft (kTrackStripW));
        pianoRoll.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF060608));
    }

    void syncSnap()
    {
        pianoRoll.setSnapTicks (transport.getSnapTicks());
    }

    /** Forward tool selection (e.g. from a global toolbar). */
    void setActiveTool (PianoRollComponent::Tool t)
    {
        pianoRoll.setActiveTool (t);
    }

    /** Set active track+clip from outside (e.g. from ArrangeView double-click). */
    void setActiveTrackPublic (int trackIndex, int clipIndex = 0)
    {
        trackStrip.setSelectedTrack (trackIndex);
        pianoRoll.setActiveTrack (trackIndex, clipIndex);
    }

    void visibilityChanged() override
    {
        if (isVisible()) syncSnap();
    }

    //==========================================================================
    //  Called from PluginProcessor / PluginEditor when slice state changes
    //==========================================================================
    void onSliceChromaticToggled (int sliceIdx, bool enabled,
                                  int chromaticChannel,
                                  const juce::String& name,
                                  juce::Colour colour)
    {
        if (enabled)
            engine.addChromaticTrack (sliceIdx, chromaticChannel, name, colour);
        else
            engine.removeChromaticTrack (sliceIdx);

        trackStrip.repaint();
    }

    void onSf2Loaded (const std::vector<Sf2PresetInfo>& presets,
                      const juce::Colour* palette, int paletteSize)
    {
        engine.rebuildSfTracks (presets, palette, paletteSize);
        trackStrip.repaint();
    }

    /** Add or update an SF2 preset sequencer track on a specific MIDI channel (1-based). */
    void addOrUpdateSfPresetTrack (const Sf2PresetInfo& preset, int midiChannel1Based,
                                   juce::Colour colour)
    {
        engine.addOrUpdateSfTrackOnChannel (preset, midiChannel1Based - 1, colour);
        trackStrip.repaint();
    }

    /** Add a single SFZ instrument track (auto-assigned to channel 16 / 0-based 15). */
    void addSfzInstrumentTrack (const juce::String& name, juce::Colour colour)
    {
        engine.addSfzTrack (name, 15, colour);
        trackStrip.repaint();
    }

    /** Forward track info query for use by PluginEditor. */
    SequencerTrackInfo getTrackInfo (int i) const { return engine.getTrackInfo (i); }

    /** Called when the MIDI channel of an existing SF track is changed from the strip UI. */
    std::function<void(int trackIndex, int midiChannel1Based)> onSfTrackChannelChanged;

private:
    SequencerEngine&   engine;
    TransportBar       transport;
    PianoRollComponent pianoRoll;
    TrackHeaderStrip   trackStrip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollPanel)
};
