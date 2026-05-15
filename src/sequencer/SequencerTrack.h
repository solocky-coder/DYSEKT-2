#pragma once
#include "MidiClip.h"
#include "../audio/SfzPlayer.h"
#include <juce_core/juce_core.h>

//==============================================================================
//  SequencerTrack
//
//  One track in the multi-track linear sequencer.  Three types:
//
//    MainSlice      — triggers slices by their pad MIDI note (ch 1 by default).
//                     Always present, cannot be deleted.
//
//    ChromaticSlice — one track per slice with chromatic mode + sequencer
//                     toggle enabled. Fires notes on s.chromaticChannel (0-15).
//
//    SfPlayer       — one track per SF2 instrument group. Fires notes on
//                     MIDI channel 16 (index 15), prepends a program change
//                     at playback start.
//==============================================================================
enum class TrackType { MainSlice, ChromaticSlice, SfPlayer };

struct SequencerTrack
{
    //==========================================================================
    TrackType    type        = TrackType::MainSlice;
    bool         enabled     = true;   // mute when false

    // Display
    juce::String name;
    juce::Colour colour      = juce::Colour (0xFF3A6080);

    // ChromaticSlice fields
    int          sliceIdx    = -1;     // which slice this track belongs to
    int          midiChannel = 0;      // 0-15 (s.chromaticChannel - 1)

    // SfPlayer fields
    Sf2PresetInfo preset;              // bank + program + name

    // The clip — owns its own note data
    MidiClip     clip;

    //==========================================================================
    //  Factory helpers
    static SequencerTrack makeMain()
    {
        SequencerTrack t;
        t.type    = TrackType::MainSlice;
        t.name    = "MAIN";
        t.colour  = juce::Colour (0xFF25D9D9);  // accent teal
        return t;
    }

    static SequencerTrack makeChromatic (int sliceIdx, int chromaticChannel,
                                         const juce::String& sliceName,
                                         juce::Colour sliceColour)
    {
        SequencerTrack t;
        t.type        = TrackType::ChromaticSlice;
        t.sliceIdx    = sliceIdx;
        t.midiChannel = chromaticChannel - 1;  // convert 1-16 → 0-15
        t.name        = sliceName.isEmpty()
                            ? ("CHROM " + juce::String (sliceIdx + 1))
                            : sliceName;
        t.colour      = sliceColour;
        return t;
    }

    static SequencerTrack makeSfPlayer (const Sf2PresetInfo& p,
                                        juce::Colour colour)
    {
        SequencerTrack t;
        t.type    = TrackType::SfPlayer;
        t.preset  = p;
        t.midiChannel = 15;   // channel 16 (0-indexed)
        t.name    = p.name;
        t.colour  = colour;
        return t;
    }

    //==========================================================================
    //  Serialisation
    void writeToStream (juce::MemoryOutputStream& s) const
    {
        s.writeInt  ((int) type);
        s.writeBool (enabled);
        s.writeString (name);
        s.writeInt  (colour.getARGB());
        s.writeInt  (sliceIdx);
        s.writeInt  (midiChannel);
        s.writeInt  (preset.bank);
        s.writeInt  (preset.preset);
        s.writeString (preset.name);
        clip.writeToStream (s);
    }

    bool readFromStream (juce::MemoryInputStream& s)
    {
        type        = (TrackType) s.readInt();
        enabled     = s.readBool();
        name        = s.readString();
        colour      = juce::Colour ((juce::uint32) s.readInt());
        sliceIdx    = s.readInt();
        midiChannel = s.readInt();
        preset.bank   = s.readInt();
        preset.preset = s.readInt();
        preset.name   = s.readString();
        return clip.readFromStream (s);
    }
};
