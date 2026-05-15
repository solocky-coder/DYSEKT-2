#pragma once
#include "SequencerTrack.h"
#include "AbletonLink.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

//==============================================================================
//  SequencerTrackInfo  —  lightweight metadata-only snapshot of a SequencerTrack.
//
//  Returned by SequencerEngine::getTrackInfo() so UI code can read track
//  properties without attempting to copy the non-copyable MidiClip/ReadWriteLock.
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

class SequencerEngine
{
public:
    //==========================================================================
    //  Transport
    //==========================================================================
    bool    isPlaying()    const noexcept { return playing.load   (std::memory_order_relaxed); }
    bool    isLooping()    const noexcept { return looping.load   (std::memory_order_relaxed); }
    bool    isRecording()  const noexcept { return recording.load (std::memory_order_relaxed); }
    int64_t getPlayheadTick()  const noexcept { return playheadTick.load (std::memory_order_relaxed); }
    double  getPlayheadBeats() const noexcept { return (double) getPlayheadTick() / (double) MidiClip::kPPQ; }
    float   getBpm()       const noexcept { return internalBpm.load (std::memory_order_relaxed); }
    bool    getSyncToHost()const noexcept { return syncToHost.load (std::memory_order_relaxed); }
    int64_t getLengthTicks()const noexcept{ return clipLengthTicks.load (std::memory_order_relaxed); }

    void play()
    {
        if (abletonLink != nullptr && abletonLink->isEnabled())
            abletonLink->requestBeatAlignedStart (4.0);
        pendingPlay.store (true, std::memory_order_relaxed);
    }
    void stop()
    {
        if (abletonLink != nullptr && abletonLink->isEnabled())
            abletonLink->notifyStop();
        pendingStop.store (true, std::memory_order_relaxed);
    }
    void rewind() { pendingRewind.store (true, std::memory_order_relaxed); }

    void setLooping   (bool v) { looping  .store (v, std::memory_order_relaxed); }
    void setRecording (bool v) { recording.store (v, std::memory_order_relaxed); }
    void setSyncToHost(bool v) { syncToHost.store(v, std::memory_order_relaxed); }

    void setBpm (float b)
    {
        const float clamped = juce::jlimit (20.f, 999.f, b);
        internalBpm.store (clamped, std::memory_order_relaxed);
        if (abletonLink != nullptr && abletonLink->isEnabled())
            abletonLink->setBpm ((double) clamped);
    }
    void setHostBpm (float b) { hostBpm    .store (juce::jlimit (20.f, 999.f, b), std::memory_order_relaxed); }

    void seekToTick (int64_t tick)
    {
        pendingSeekTick.store (tick, std::memory_order_relaxed);
        pendingSeek    .store (true, std::memory_order_relaxed);
    }

    void setLengthTicks (int64_t ticks)
    {
        clipLengthTicks.store (juce::jmax ((int64_t) MidiClip::kPPQ, ticks),
                               std::memory_order_relaxed);
        const juce::ScopedWriteLock sl (tracksLock);
        for (auto& t : tracks) t.clip.setLengthTicks (ticks);
    }

    //==========================================================================
    //  Track management  (message thread)
    //==========================================================================
    int getNumTracks() const
    {
        const juce::ScopedReadLock sl (tracksLock);
        return tracks.size();
    }

    SequencerTrackInfo getTrackInfo (int i) const
    {
        const juce::ScopedReadLock sl (tracksLock);
        if (! juce::isPositiveAndBelow (i, tracks.size()))
            return {};
        const auto& t = tracks.getReference (i);
        return { t.type, t.enabled, t.name, t.colour, t.sliceIdx, t.midiChannel, t.preset };
    }

    MidiClip* getClip (int trackIndex)
    {
        const juce::ScopedReadLock sl (tracksLock);
        if (juce::isPositiveAndBelow (trackIndex, tracks.size()))
            return &tracks.getReference (trackIndex).clip;
        return nullptr;
    }

    // Legacy single-clip accessor (main track)
    MidiClip& getClip() { return tracks.getReference (0).clip; }

    void setTrackEnabled (int i, bool enabled)
    {
        const juce::ScopedWriteLock sl (tracksLock);
        if (juce::isPositiveAndBelow (i, tracks.size()))
            tracks.getReference (i).enabled = enabled;
    }

    void addMainTrack()
    {
        const juce::ScopedWriteLock sl (tracksLock);
        if (tracks.isEmpty())
        {
            auto t = SequencerTrack::makeMain();
            t.clip.setLengthTicks (clipLengthTicks.load (std::memory_order_relaxed));
            tracks.add (std::move (t));
        }
    }

    void addChromaticTrack (int sliceIdx, int chromaticChannel,
                            const juce::String& name, juce::Colour colour)
    {
        const juce::ScopedWriteLock sl (tracksLock);
        for (auto& t : tracks)
            if (t.type == TrackType::ChromaticSlice && t.sliceIdx == sliceIdx)
                return;
        auto t = SequencerTrack::makeChromatic (sliceIdx, chromaticChannel, name, colour);
        t.clip.setLengthTicks (clipLengthTicks.load (std::memory_order_relaxed));
        tracks.add (std::move (t));
    }

    void removeChromaticTrack (int sliceIdx)
    {
        const juce::ScopedWriteLock sl (tracksLock);
        for (int i = tracks.size() - 1; i >= 0; --i)
            if (tracks[i].type == TrackType::ChromaticSlice && tracks[i].sliceIdx == sliceIdx)
                tracks.remove (i);
    }

    void addSfTrack (const Sf2PresetInfo& preset, juce::Colour colour)
    {
        const juce::ScopedWriteLock sl (tracksLock);
        for (auto& t : tracks)
            if (t.type == TrackType::SfPlayer
                && t.preset.bank == preset.bank
                && t.preset.preset == preset.preset)
                return;
        auto t = SequencerTrack::makeSfPlayer (preset, colour);
        t.clip.setLengthTicks (clipLengthTicks.load (std::memory_order_relaxed));
        tracks.add (std::move (t));
    }

    void removeSfTrack (int trackIndex)
    {
        const juce::ScopedWriteLock sl (tracksLock);
        if (juce::isPositiveAndBelow (trackIndex, tracks.size())
            && tracks[trackIndex].type == TrackType::SfPlayer)
            tracks.remove (trackIndex);
    }

    void rebuildSfTracks (const std::vector<Sf2PresetInfo>& presets,
                          const juce::Colour* palette, int paletteSize)
    {
        const juce::ScopedWriteLock sl (tracksLock);
        for (int i = tracks.size() - 1; i >= 0; --i)
            if (tracks[i].type == TrackType::SfPlayer)
                tracks.remove (i);

        const int64_t len = clipLengthTicks.load (std::memory_order_relaxed);
        for (int i = 0; i < (int) presets.size(); ++i)
        {
            const juce::Colour col = paletteSize > 0
                ? palette[i % paletteSize] : juce::Colour (0xFF406080);
            auto t = SequencerTrack::makeSfPlayer (presets[i], col);
            t.clip.setLengthTicks (len);
            tracks.add (std::move (t));
        }
    }

    //==========================================================================
    //  Audio thread
    //==========================================================================
    void processBlock (juce::MidiBuffer& outMidi, int numSamples, double sampleRate)
    {
        // Transport commands
        if (pendingStop.exchange (false, std::memory_order_relaxed))
        {
            if (playing.load (std::memory_order_relaxed))
            {
                playing.store (false, std::memory_order_relaxed);
                flushAllActiveNotes (outMidi, 0);
            }
        }
        if (pendingRewind.exchange (false, std::memory_order_relaxed))
        {
            flushAllActiveNotes (outMidi, 0);
            currentTick = 0.0;
            playheadTick.store (0, std::memory_order_relaxed);
            justStarted = true;
        }
        if (pendingSeek.exchange (false, std::memory_order_relaxed))
        {
            flushAllActiveNotes (outMidi, 0);
            currentTick = (double) pendingSeekTick.load (std::memory_order_relaxed);
            playheadTick.store ((int64_t) currentTick, std::memory_order_relaxed);
        }
        if (pendingPlay.exchange (false, std::memory_order_relaxed))
        {
            playing.store (true, std::memory_order_relaxed);
            justStarted = true;
        }

        if (! playing.load (std::memory_order_relaxed)) return;

        const float fallbackBpm = syncToHost.load (std::memory_order_relaxed)
                            ? hostBpm.load (std::memory_order_relaxed)
                            : internalBpm.load (std::memory_order_relaxed);
        const float bpm = (abletonLink != nullptr && abletonLink->isEnabled())
                            ? abletonLink->getBpm (fallbackBpm)
                            : fallbackBpm;
        if (bpm < 1.f || sampleRate < 1.0) return;

        const double ticksPerSample = (bpm / 60.0) * (double) MidiClip::kPPQ / sampleRate;
        const int64_t clipLen       = clipLengthTicks.load (std::memory_order_relaxed);
        const bool    doLoop        = looping.load (std::memory_order_relaxed);
        const double  blockEndTick  = currentTick + ticksPerSample * numSamples;

        {
            const juce::ScopedReadLock sl (tracksLock);
            for (int ti = 0; ti < tracks.size(); ++ti)
            {
                auto& track = tracks.getReference (ti);
                if (! track.enabled) continue;

                // SF-player: program change at start of playback
                if (track.type == TrackType::SfPlayer && justStarted)
                    outMidi.addEvent (
                        juce::MidiMessage::programChange (16, track.preset.preset), 0);

                processTrackRange (outMidi, track, ti,
                                   currentTick, blockEndTick,
                                   0, numSamples, ticksPerSample, clipLen, doLoop);
            }
        }

        justStarted = false;

        // Advance playhead — handle loop wrap
        if (doLoop && clipLen > 0 && blockEndTick >= (double) clipLen)
        {
            const int loopSample = juce::jlimit (0, numSamples - 1,
                (int)(((double) clipLen - currentTick) / ticksPerSample));
            flushAllActiveNotes (outMidi, loopSample);
            currentTick = std::fmod (blockEndTick, (double) clipLen);
            justStarted = true;
        }
        else
        {
            currentTick = blockEndTick;
        }

        playheadTick.store ((int64_t) currentTick, std::memory_order_relaxed);
    }

    //==========================================================================
    //  Serialisation
    //==========================================================================
    void writeToStream (juce::MemoryOutputStream& s) const
    {
        s.writeFloat (internalBpm    .load (std::memory_order_relaxed));
        s.writeBool  (looping        .load (std::memory_order_relaxed));
        s.writeBool  (syncToHost     .load (std::memory_order_relaxed));
        s.writeInt64 (clipLengthTicks.load (std::memory_order_relaxed));

        const juce::ScopedReadLock sl (tracksLock);
        s.writeInt (tracks.size());
        for (const auto& t : tracks)
            const_cast<SequencerTrack&>(t).writeToStream (s);
    }

    bool readFromStream (juce::MemoryInputStream& s)
    {
        const float   bpm  = s.readFloat();
        const bool    loop = s.readBool();
        const bool    sync = s.readBool();
        const int64_t len  = s.readInt64();
        const int     n    = s.readInt();
        if (bpm < 20.f || bpm > 999.f || n < 0 || n > 256) return false;

        juce::Array<SequencerTrack> loaded;
        for (int i = 0; i < n; ++i)
        {
            SequencerTrack t;
            if (! t.readFromStream (s)) return false;
            loaded.add (std::move (t));
        }

        internalBpm    .store (bpm,  std::memory_order_relaxed);
        looping        .store (loop, std::memory_order_relaxed);
        syncToHost     .store (sync, std::memory_order_relaxed);
        clipLengthTicks.store (len,  std::memory_order_relaxed);

        const juce::ScopedWriteLock sl (tracksLock);
        tracks = std::move (loaded);
        return true;
    }

private:
    //==========================================================================
    juce::Array<SequencerTrack> tracks;
    mutable juce::ReadWriteLock tracksLock;

    double               currentTick = 0.0;
    bool                 justStarted = false;
    std::atomic<int64_t> playheadTick { 0 };

    struct ActiveNote { int trackIdx; int note; int channel; };
    juce::Array<ActiveNote> activeNotes;

    std::atomic<bool>    playing      { false };
    std::atomic<bool>    recording    { false };
    std::atomic<bool>    looping      { true  };
    std::atomic<bool>    pendingPlay  { false };
    std::atomic<bool>    pendingStop  { false };
    std::atomic<bool>    pendingRewind{ false };
    std::atomic<bool>    pendingSeek  { false };
    std::atomic<int64_t> pendingSeekTick { 0 };
    std::atomic<float>   internalBpm  { 120.f };
    std::atomic<float>   hostBpm      { 120.f };
    std::atomic<bool>    syncToHost   { false  };
    std::atomic<int64_t> clipLengthTicks { MidiClip::kPPQ * 4 * 4 };

    //==========================================================================
    static int midiChannelForTrack (const SequencerTrack& t) noexcept
    {
        switch (t.type)
        {
            case TrackType::MainSlice:      return 1;
            case TrackType::ChromaticSlice: return t.midiChannel + 1;
            case TrackType::SfPlayer:       return 16;
        }
        return 1;
    }

    void processTrackRange (juce::MidiBuffer& outMidi, SequencerTrack& track,
                            int trackIdx, double startTick, double endTick,
                            int sampleOffset, int numSamples,
                            double ticksPerSample, int64_t clipLen, bool doLoop)
    {
        const int ch = midiChannelForTrack (track);

        double localStart = startTick;
        if (doLoop && clipLen > 0 && (int64_t) localStart >= clipLen)
            localStart = std::fmod (localStart, (double) clipLen);

        const juce::ScopedReadLock cl (track.clip.getLock());
        for (const auto& n : track.clip.getNotes())
        {
            const double nStart = (double) n.startTick;
            const double nEnd   = (double) n.endTick();

            if (nStart >= localStart && nStart < endTick)
            {
                const int sp = sampleOffset + juce::jlimit (0, numSamples - 1,
                    (int)((nStart - localStart) / ticksPerSample));
                outMidi.addEvent (
                    juce::MidiMessage::noteOn (ch, n.note, (juce::uint8) n.velocity), sp);
                activeNotes.add ({ trackIdx, n.note, ch });
            }

            if (nEnd > localStart && nEnd <= endTick)
            {
                const int sp = sampleOffset + juce::jlimit (0, numSamples - 1,
                    (int)((nEnd - localStart) / ticksPerSample));
                outMidi.addEvent (juce::MidiMessage::noteOff (ch, n.note), sp);
                for (int i = activeNotes.size() - 1; i >= 0; --i)
                    if (activeNotes[i].trackIdx == trackIdx && activeNotes[i].note == n.note)
                        { activeNotes.remove (i); break; }
            }
        }
    }

    void flushAllActiveNotes (juce::MidiBuffer& outMidi, int samplePos)
    {
        for (const auto& an : activeNotes)
            outMidi.addEvent (juce::MidiMessage::noteOff (an.channel, an.note), samplePos);
        activeNotes.clear();
    }

    AbletonLink* abletonLink = nullptr;

public:
    /** Set the shared AbletonLink instance (owned by PluginProcessor). */
    void setAbletonLink (AbletonLink* l) noexcept { abletonLink = l; }
};
