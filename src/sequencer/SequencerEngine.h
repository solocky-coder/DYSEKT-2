#pragma once
#include "SequencerTrack.h"
#include "AbletonLink.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <memory>

//==============================================================================
//  SequencerTrackInfo  —  lightweight metadata snapshot of a SequencerTrack.
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
    int           numClips    = 0;
};

//==============================================================================
//  SequencerClipInfo  —  lightweight snapshot of a single ClipSlot.
//==============================================================================
struct SequencerClipInfo
{
    int64_t startTick  = 0;
    int64_t lengthTicks = MidiClip::kPPQ * 4 * 4;
    int64_t endTick() const noexcept { return startTick + lengthTicks; }
};

class SequencerEngine
{
public:
    SequencerEngine();
    ~SequencerEngine();

    //==========================================================================
    //  Transport
    //==========================================================================
    bool    isPlaying()         const noexcept;
    bool    isLooping()         const noexcept;
    bool    isRecording()       const noexcept;
    int64_t getPlayheadTick()   const noexcept;
    double  getPlayheadBeats()  const noexcept;
    float   getBpm()            const noexcept;
    bool    getSyncToHost()     const noexcept;

    /** Global length = end of last clip across all tracks (governs playhead wrap). */
    int64_t getLengthTicks()    const noexcept;

    void play();
    void stop();
    void rewind();

    void setLooping   (bool v);
    void setRecording (bool v);
    void setSyncToHost(bool v);
    void setBpm       (float b);
    void setHostBpm   (float b);
    void seekToTick   (int64_t tick);

    /** Backward-compat: sets clip 0 length on all tracks. */
    void setLengthTicks (int64_t ticks);

    //==========================================================================
    //  Track management  (message thread)
    //==========================================================================
    int                getNumTracks()       const;
    SequencerTrackInfo getTrackInfo (int i) const;

    void setTrackEnabled (int i, bool enabled);

    void addMainTrack();
    void addChromaticTrack (int sliceIdx, int chromaticChannel,
                            const juce::String& name, juce::Colour colour);
    void removeChromaticTrack (int sliceIdx);
    void addSfTrack (const Sf2PresetInfo& preset, juce::Colour colour);
    void removeSfTrack (int trackIndex);
    void rebuildSfTracks (const std::vector<Sf2PresetInfo>& presets,
                          const juce::Colour* palette, int paletteSize);

    /** Add or update an SF2 track for a given preset on a specific MIDI channel (0-based).
     *  If a track already exists for this preset, its channel is updated.
     *  Also calls sfzPlayer->setPresetOnChannel() so audio follows immediately. */
    void addOrUpdateSfTrackOnChannel (const Sf2PresetInfo& preset, int midiChannel0Based,
                                      juce::Colour colour);

    /** Add a single SFZ track (one channel covers the whole instrument).
     *  If an SFZ track already exists (identified by name), it is replaced. */
    void addSfzTrack (const juce::String& name, int midiChannel0Based, juce::Colour colour);

    //==========================================================================
    //  Clip management  (message thread)
    //==========================================================================

    /** Returns the number of clips on a track. */
    int getNumClips (int trackIndex) const;

    /** Snapshot of a clip's position+length. */
    SequencerClipInfo getClipInfo (int trackIndex, int clipIndex) const;

    /** Direct access to a MidiClip for editing (piano roll). Null if out of range. */
    MidiClip* getClip (int trackIndex, int clipIndex = 0);

    /** Legacy single-clip accessor — main track clip 0. */
    MidiClip& getClip();

    /** Create a new empty clip on the track. Returns the new clip index. */
    int addClip (int trackIndex, int64_t startTick,
                 int64_t lengthTicks = MidiClip::kPPQ * 4 * 4);

    /** Delete a clip by index. */
    void removeClip (int trackIndex, int clipIndex);

    /** Move a clip's start position. */
    void setClipStartTick (int trackIndex, int clipIndex, int64_t newStartTick);

    /** Resize a clip. */
    void setClipLengthTicks (int trackIndex, int clipIndex, int64_t newLength);

    /** Backward-compat per-track length helpers (operate on clip 0). */
    int64_t getTrackLengthTicks (int trackIndex) const noexcept;
    void    setTrackLengthTicks (int trackIndex, int64_t ticks);

    //==========================================================================
    //  Audio thread
    //==========================================================================
    void processBlock (juce::MidiBuffer& outMidi, const juce::MidiBuffer& inMidi,
                       int numSamples, double sampleRate);

    //==========================================================================
    //  Serialisation
    //==========================================================================
    void writeToStream (juce::MemoryOutputStream& s) const;
    bool readFromStream (juce::MemoryInputStream& s);

    //==========================================================================
    //  Ableton Link
    //==========================================================================
    void setAbletonLink (AbletonLink* l) noexcept;

    //==========================================================================
    //  SfzPlayer integration  (multi-timbral channel assignment)
    //==========================================================================
    /** Store a pointer to the SfzPlayer so that rebuildSfTracks() and
     *  addSfTrack() can call setPresetOnChannel() automatically.
     *  Call once from PluginProcessor constructor, before any SF2 is loaded. */
    void setSfzPlayer (class SfzPlayer* player) noexcept;

    /** Set which FluidSynth channel should receive live controller (ch-1) input.
     *  Pass the 0-based midiChannel of the selected SF track, or -1 for none.
     *  Called by ArrangeView whenever the selected track changes. */
    void setSelectedSfLiveChannels (uint16_t channelMask) noexcept;

    /** Returns a bitmask (bit N = channel N+1) covering every SfPlayer track's
     *  assigned MIDI channel.  Used by setMidiRouteMode() to direct all live
     *  input to the SF-player when SF-player mode is active. */
    uint16_t getAllSfPlayerChannelMask() const noexcept;

    //==========================================================================
    //  Live MIDI channel for the selected arranger track
    //==========================================================================

    /** Set the MIDI channel (1-based) that live input should be re-stamped to
     *  when the arranger is active.  Pass 0 to disable re-stamping (SfPlayer
     *  tracks handle their own routing via liveInputChannelMask).
     *  Called from ArrangeView::selectTrack on the message thread — atomic. */
    void setSelectedLiveChannel (int ch1Based) noexcept;

    /** Returns the current selected live channel (1-16), or 0 if none. */
    int  getSelectedLiveChannel() const noexcept;

    /** Tell the engine which track index receives recorded MIDI.
     *  Call alongside setSelectedLiveChannel when the user selects a track.
     *  Pass -1 to disable recording. */
    void setRecordingTrack (int trackIndex) noexcept;

    //==========================================================================
    //  Per-track MIDI activity flags (for the receive indicator in TrackHeaderStrip)
    //==========================================================================

    static constexpr int kActivityFlagCount = 64;

    /** Returns true and clears the flag if the audio thread set it since last
     *  call.  Safe to call from the message thread (atomic exchange). */
    bool getMidiActivityAndClear (int trackIndex) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
