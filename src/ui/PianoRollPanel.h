#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "TransportBar.h"
#include "PianoRollComponent.h"
#include "TrackHeaderStrip.h"
#include "../sequencer/SequencerEngine.h"

//==============================================================================
//  PianoRollPanel  –  content component (no window chrome of its own)
//
//  Layout:
//    ┌─────────────────────────────────────────────────────┐
//    │  TransportBar                                        │
//    ├──────────────┬──────────────────────────────────────┤
//    │  Track       │   PianoRollComponent                 │
//    │  Header      │   (edits active track's clip)        │
//    │  Strip       │                                       │
//    │  (160px)     │                                       │
//    └──────────────┴──────────────────────────────────────┘
//
//  Hosted inside PianoRollWindow (a DocumentWindow) so the OS supplies
//  the real title bar with a native X / close button.
//==============================================================================
class PianoRollPanel : public juce::Component
{
public:
    static constexpr int kTransportH  = 34;
    static constexpr int kTrackStripW = 160;

    PianoRollPanel (SequencerEngine& seq, AbletonLink* link = nullptr)
        : engine (seq), transport (seq, link), pianoRoll (seq), trackStrip (seq)
    {
        addAndMakeVisible (transport);
        addAndMakeVisible (trackStrip);
        addAndMakeVisible (pianoRoll);

        engine.addMainTrack();

        trackStrip.onTrackSelected = [this] (int idx)
        {
            pianoRoll.setActiveTrack (idx, 0);
        };

        trackStrip.onSfTrackChannelChanged = [this] (int trackIndex, int ch1Based)
        {
            if (onSfTrackChannelChanged)
                onSfTrackChannelChanged (trackIndex, ch1Based);
        };
    }

    void resized() override
    {
        auto r = getLocalBounds();
        transport.setBounds  (r.removeFromTop  (kTransportH));
        trackStrip.setBounds (r.removeFromLeft (kTrackStripW));
        pianoRoll.setBounds  (r);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF060608));
    }

    void syncSnap()
    {
        pianoRoll.setSnapTicks (transport.getSnapTicks());
    }

    void setActiveTool (PianoRollComponent::Tool t) { pianoRoll.setActiveTool (t); }

    void setActiveTrackPublic (int trackIndex, int clipIndex = 0)
    {
        trackStrip.setSelectedTrack (trackIndex);
        pianoRoll.setActiveTrack (trackIndex, clipIndex);
    }

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

    void addOrUpdateSfPresetTrack (const Sf2PresetInfo& preset, int midiChannel1Based,
                                   juce::Colour colour)
    {
        engine.addOrUpdateSfTrackOnChannel (preset, midiChannel1Based - 1, colour);
        trackStrip.repaint();
    }

    void addSfzInstrumentTrack (const juce::String& name, juce::Colour colour)
    {
        engine.addSfzTrack (name, 15, colour);
        trackStrip.repaint();
    }

    SequencerTrackInfo getTrackInfo (int i) const { return engine.getTrackInfo (i); }

    std::function<void(int trackIndex, int midiChannel1Based)> onSfTrackChannelChanged;

private:
    SequencerEngine&   engine;
    TransportBar       transport;
    PianoRollComponent pianoRoll;
    TrackHeaderStrip   trackStrip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollPanel)
};

//==============================================================================
//  PianoRollWindow  –  native OS floating window that hosts PianoRollPanel
//
//  Usage (from PluginEditor):
//      pianoRollWindow->openFor (trackIndex, clipIndex);   // show / bring to front
//      pianoRollWindow->closeWindow();                     // hide
//==============================================================================
class PianoRollWindow : public juce::DocumentWindow
{
public:
    PianoRollWindow (SequencerEngine& seq, juce::LookAndFeel& lnf, AbletonLink* link = nullptr)
        : juce::DocumentWindow ("Piano Roll",
                                juce::Colour (0xFF0D0D14),
                                juce::DocumentWindow::closeButton |
                                juce::DocumentWindow::minimiseButton |
                                juce::DocumentWindow::maximiseButton),
          panel (seq, link)
    {
        setUsingNativeTitleBar (false);  // use our themed title bar
        setLookAndFeel (&lnf);
        setResizable (true, true);
        setContentNonOwned (&panel, true);
        setSize (1100, 600);
        centreWithSize (getWidth(), getHeight());
        wireCallbacks();
    }

    ~PianoRollWindow() override
    {
        setLookAndFeel (nullptr);
    }

    //==========================================================================
    /** Show the window and focus the given track/clip. */
    void openFor (int trackIndex, int clipIndex = 0)
    {
        panel.setActiveTrackPublic (trackIndex, clipIndex);
        panel.syncSnap();
        setVisible (true);
        toFront (true);
    }

    /** Hide the window without destroying it. */
    void closeWindow()
    {
        setVisible (false);
    }

    //==========================================================================
    //  Forwarded accessors (so PluginEditor can reach panel internals)
    void setActiveTool (PianoRollComponent::Tool t)   { panel.setActiveTool (t); }
    SequencerTrackInfo getTrackInfo (int i) const     { return panel.getTrackInfo (i); }
    void syncSnap()                                    { panel.syncSnap(); }

    void onSliceChromaticToggled (int si, bool en, int ch,
                                  const juce::String& name, juce::Colour col)
    { panel.onSliceChromaticToggled (si, en, ch, name, col); }

    void onSf2Loaded (const std::vector<Sf2PresetInfo>& p,
                      const juce::Colour* pal, int palSz)
    { panel.onSf2Loaded (p, pal, palSz); }

    void addOrUpdateSfPresetTrack (const Sf2PresetInfo& preset,
                                   int midiChannel1Based, juce::Colour colour)
    { panel.addOrUpdateSfPresetTrack (preset, midiChannel1Based, colour); }

    void addSfzInstrumentTrack (const juce::String& name, juce::Colour colour)
    { panel.addSfzInstrumentTrack (name, colour); }

    /** Wire this in PluginEditor after construction to receive SF track channel changes. */
    std::function<void(int trackIndex, int midiChannel1Based)> onSfTrackChannelChanged;

    void wireCallbacks()
    {
        panel.onSfTrackChannelChanged = [this] (int ti, int ch)
        {
            if (onSfTrackChannelChanged) onSfTrackChannelChanged (ti, ch);
        };
    }

    //==========================================================================
    /** Native X button — hide rather than delete. */
    void closeButtonPressed() override
    {
        closeWindow();
    }

private:
    PianoRollPanel panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollWindow)
};
