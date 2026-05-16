#pragma once
#include "SequencerTrack.h"
#include "AbletonLink.h"
#include <juce_graphics/juce_graphics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include <vector>

// No tracktion_engine include here — all te:: types are hidden in the Impl
// (see SequencerEngine.cpp).  This keeps the include cost out of every TU
// that pulls in PluginProcessor.h.

//==============================================================================
//  SequencerTrackInfo  —  lightweight metadata snapshot of a SequencerTrack.
//  Returned by SequencerEngine::getTrackInfo() for UI reads.  Unchanged.
//==============================================================================
struct SequencerTrackInfo
{
    TrackType     type        = TrackType::MainSlice;
    bool          enabled     = true;
    juce::String  name;
    juce::Colour  colour      = juce::Colour (0xFF3A6080);
    int           sliceIdx    = -1;
    int           midiChannel = 0;
    Sf2PresetInfo preset;
};

//==============================================================================
//  SequencerEngine
//
//  Public API is identical to the original hand-rolled version.
//
//  Backend changes:
//    • Transport (play/stop/seek/loop/BPM) → te::TransportControl on te::Edit
//    • Tempo / host-BPM sync            → te::TempoSequence on the same edit
//    • Note storage per track            → te::MidiList via MidiClip::attachMidiList()
//    • processBlock() reads the shadow juce::Array<MidiNote> exactly as before;
//      the Tracktion edit graph runs in parallel for its own rendering path.
//
//  Tracktion objects are held in a PIMPL (Impl) defined in SequencerEngine.cpp
//  so this header stays free of the tracktion_engine include.
//==============================================================================
class SequencerEngine
{
public:
    SequencerEngine();
    ~SequencerEngine();

    // Non-copyable, non-movable (owns atomic state + PIMPL)
    SequencerEngine (const SequencerEngine&)            = delete;
    SequencerEngine& operator= (const SequencerEngine&) = delete;

    //==========================================================================
    //  Transport  (public API unchanged)
    //==========================================================================
    bool    isPlaying()       const noexcept;
    bool    isLooping()       const noexcept;
    bool    isRecording()     const noexcept;
    int64_t getPlayheadTick() const noexcept;
    double  getPlayheadBeats() const noexcept;
    float   getBpm()          const noexcept;
    bool    getSyncToHost()   const noexcept;
    int64_t getLengthTicks()  const noexcept;

    void play();
    void stop();
    void rewind();
    void setLooping    (bool v);
    void setRecording  (bool v);
    void setSyncToHost (bool v);
    void setBpm        (float b);
    void setHostBpm    (float b);
    void seekToTick    (int64_t tick);
    void setLengthTicks(int64_t ticks);

    //==========================================================================
    //  Track management  (message thread)
    //==========================================================================
    int  getNumTracks() const;
    SequencerTrackInfo getTrackInfo (int i) const;
    MidiClip* getClip (int trackIndex);
    MidiClip& getClip();   // legacy single-clip accessor

    void setTrackEnabled (int i, bool enabled);
    void addMainTrack();
    void addChromaticTrack (int sliceIdx, int chromaticChannel,
                            const juce::String& name, juce::Colour colour);
    void removeChromaticTrack (int sliceIdx);
    void addSfTrack    (const Sf2PresetInfo& preset, juce::Colour colour);
    void removeSfTrack (int trackIndex);
    void rebuildSfTracks (const std::vector<Sf2PresetInfo>& presets,
                          const juce::Colour* palette, int paletteSize);

    //==========================================================================
    //  Audio thread
    //==========================================================================
    void processBlock (juce::MidiBuffer& outMidi, int numSamples, double sampleRate);

    //==========================================================================
    //  Ableton Link
    //==========================================================================
    void setAbletonLink (AbletonLink* l) noexcept;

    //==========================================================================
    //  Serialisation
    //==========================================================================
    void writeToStream  (juce::MemoryOutputStream& s) const;
    bool readFromStream (juce::MemoryInputStream&  s);

private:
    // PIMPL — hides all te:: types and the full implementation
    struct Impl;
    std::unique_ptr<Impl> impl;
};
