#pragma once
#include "MidiClip.h"
#include "../audio/SfzPlayer.h"
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

//==============================================================================
//  ClipSlot  —  a MidiClip with a start position on the timeline.
//==============================================================================
struct ClipSlot
{
    int64_t  startTick = 0;   // position on the track timeline
    MidiClip clip;            // owns note data + length

    ClipSlot() = default;

    ClipSlot (int64_t start, int64_t lengthTicks)
        : startTick (start)
    {
        clip.setLengthTicks (lengthTicks);
    }

    // Move-only (MidiClip is non-copyable)
    ClipSlot (ClipSlot&&) = default;
    ClipSlot& operator= (ClipSlot&&) = default;
    JUCE_DECLARE_NON_COPYABLE (ClipSlot)

    int64_t endTick() const noexcept { return startTick + clip.getLengthTicks(); }

    void writeToStream (juce::MemoryOutputStream& s) const
    {
        s.writeInt64 (startTick);
        clip.writeToStream (s);
    }

    bool readFromStream (juce::MemoryInputStream& s)
    {
        startTick = s.readInt64();
        return clip.readFromStream (s);
    }
};

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
    SequencerTrack() = default;
    SequencerTrack (SequencerTrack&&) = default;
    SequencerTrack& operator= (SequencerTrack&&) = default;
    JUCE_DECLARE_NON_COPYABLE (SequencerTrack)

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

    // Multi-clip slot list — sorted by startTick
    juce::OwnedArray<ClipSlot> clips;

    //==========================================================================
    //  Clip helpers (message thread)
    //==========================================================================

    /** Add a new empty clip at startTick with given length. Returns the new index. */
    int addClip (int64_t startTick, int64_t lengthTicks = MidiClip::kPPQ * 4 * 4)
    {
        auto* slot = new ClipSlot (startTick, lengthTicks);
        clips.add (slot);
        sortClips();
        // find where it landed
        for (int i = 0; i < clips.size(); ++i)
            if (clips[i] == slot) return i;
        return clips.size() - 1;
    }

    void removeClip (int index)
    {
        if (juce::isPositiveAndBelow (index, clips.size()))
            clips.remove (index);
    }

    int getNumClips() const noexcept { return clips.size(); }

    ClipSlot*       getClipSlot (int i)       { return juce::isPositiveAndBelow(i,clips.size()) ? clips[i] : nullptr; }
    const ClipSlot* getClipSlot (int i) const { return juce::isPositiveAndBelow(i,clips.size()) ? clips[i] : nullptr; }

    /** Ensure clips stay ordered by startTick. */
    void sortClips()
    {
        clips.sort (clipSorter, false);
    }

    //==========================================================================
    //  Factory helpers
    static SequencerTrack makeMain()
    {
        SequencerTrack t;
        t.type    = TrackType::MainSlice;
        t.name    = "MAIN";
        t.colour  = juce::Colour (0xFF25D9D9);
        t.addClip (0);
        return t;
    }

    static SequencerTrack makeChromatic (int sliceIdx, int chromaticChannel,
                                         const juce::String& sliceName,
                                         juce::Colour sliceColour)
    {
        SequencerTrack t;
        t.type        = TrackType::ChromaticSlice;
        t.sliceIdx    = sliceIdx;
        t.midiChannel = chromaticChannel - 1;
        t.name        = sliceName.isEmpty()
                            ? ("CHROM " + juce::String (sliceIdx + 1))
                            : sliceName;
        t.colour      = sliceColour;
        t.addClip (0);
        return t;
    }

    static SequencerTrack makeSfPlayer (const Sf2PresetInfo& p,
                                        juce::Colour colour)
    {
        SequencerTrack t;
        t.type        = TrackType::SfPlayer;
        t.preset      = p;
        t.midiChannel = 15;
        t.name        = p.name;
        t.colour      = colour;
        t.addClip (0);
        return t;
    }

    //==========================================================================
    //  Serialisation  (v2 — multi-clip)
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
        // v2: write clip count then each slot
        s.writeInt (clips.size());
        for (const auto* slot : clips)
            const_cast<ClipSlot*>(slot)->writeToStream (s);
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

        const int n = s.readInt();
        if (n < 0 || n > 1024) return false;
        clips.clear();
        for (int i = 0; i < n; ++i)
        {
            auto* slot = new ClipSlot();
            if (! slot->readFromStream (s)) { delete slot; return false; }
            clips.add (slot);
        }
        sortClips();
        return true;
    }

    /** Legacy v1 read: single MidiClip at startTick=0. */
    bool readFromStreamV1 (juce::MemoryInputStream& s)
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

        clips.clear();
        auto* slot = new ClipSlot();
        slot->startTick = 0;
        if (! slot->clip.readFromStream (s)) { delete slot; return false; }
        clips.add (slot);
        return true;
    }

private:
    struct ClipSorter
    {
        static int compareElements (const ClipSlot* a, const ClipSlot* b) noexcept
        {
            return (a->startTick < b->startTick) ? -1
                 : (a->startTick > b->startTick) ?  1 : 0;
        }
    } clipSorter;
};
