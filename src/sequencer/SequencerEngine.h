#pragma once
#include "SequencerTrack.h"
#include "AbletonLink.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <tracktion_engine/tracktion_engine.h>
#include <atomic>

namespace te = tracktion_engine;

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
//      the Tracktion edit graph runs in parallel for its own rendering path
//      (used by the standalone app's native output and future audio-clip features).
//
//  Ableton Link:
//    Link is still bridged manually via the setAbletonLink() hook.  Tracktion
//    Engine does not have native Link support, so we feed the Link BPM into
//    te::TempoSequence each audio block — same pattern as before.
//
//  Thread safety:
//    processBlock() is called on the audio thread.  All te::Edit / te::Transport
//    state changes (play, stop, setBpm, …) use Tracktion's own thread-safe
//    pending-command queue under the hood, so there are no new lock requirements
//    beyond what the original engine had.
//==============================================================================
class SequencerEngine
{
public:
    //==========================================================================
    //  Constructor / Destructor
    //==========================================================================

    SequencerEngine()
        : teEngine (te::Engine::getInstance())
    {
        // Create a persistent edit owned by this engine instance
        edit = te::Edit::createSingleTrackEdit (teEngine);
        jassert (edit != nullptr);

        // Set default tempo (120 BPM, 4/4)
        auto& ts = edit->tempoSequence;
        ts.getTempo (0)->setBpm (120.0);
    }

    ~SequencerEngine()
    {
        // Stop Tracktion transport before destruction
        if (edit != nullptr)
            edit->getTransport().stop (false, false);
    }

    //==========================================================================
    //  Transport  (public API unchanged)
    //==========================================================================
    bool    isPlaying()       const noexcept { return playing.load   (std::memory_order_relaxed); }
    bool    isLooping()       const noexcept { return looping.load   (std::memory_order_relaxed); }
    bool    isRecording()     const noexcept { return recording.load (std::memory_order_relaxed); }
    int64_t getPlayheadTick() const noexcept { return playheadTick.load (std::memory_order_relaxed); }
    double  getPlayheadBeats() const noexcept { return (double) getPlayheadTick() / (double) MidiClip::kPPQ; }
    float   getBpm()          const noexcept { return internalBpm.load (std::memory_order_relaxed); }
    bool    getSyncToHost()   const noexcept { return syncToHost.load (std::memory_order_relaxed); }
    int64_t getLengthTicks()  const noexcept { return clipLengthTicks.load (std::memory_order_relaxed); }

    void play()
    {
        if (abletonLink != nullptr && abletonLink->isEnabled())
            abletonLink->requestBeatAlignedStart (4.0);
        pendingPlay.store (true, std::memory_order_relaxed);

        // Also start Tracktion transport so its edit graph renders
        if (edit != nullptr)
        {
            auto& t = edit->getTransport();
            t.setLoopRange (te::EditTimeRange (0.0, beatsToSeconds (getLengthBeats())));
            t.looping = looping.load (std::memory_order_relaxed);
            t.play (false);
        }
    }

    void stop()
    {
        if (abletonLink != nullptr && abletonLink->isEnabled())
            abletonLink->notifyStop();
        pendingStop.store (true, std::memory_order_relaxed);

        if (edit != nullptr)
            edit->getTransport().stop (false, false);
    }

    void rewind()
    {
        pendingRewind.store (true, std::memory_order_relaxed);
        if (edit != nullptr)
            edit->getTransport().setCurrentPosition (0.0);
    }

    void setLooping   (bool v)
    {
        looping.store (v, std::memory_order_relaxed);
        if (edit != nullptr)
            edit->getTransport().looping = v;
    }

    void setRecording (bool v) { recording.store (v, std::memory_order_relaxed); }
    void setSyncToHost(bool v) { syncToHost.store(v, std::memory_order_relaxed); }

    void setBpm (float b)
    {
        const float clamped = juce::jlimit (20.f, 999.f, b);
        internalBpm.store (clamped, std::memory_order_relaxed);
        applyBpmToEdit ((double) clamped);
        if (abletonLink != nullptr && abletonLink->isEnabled())
            abletonLink->setBpm ((double) clamped);
    }

    void setHostBpm (float b)
    {
        hostBpm.store (juce::jlimit (20.f, 999.f, b), std::memory_order_relaxed);
    }

    void seekToTick (int64_t tick)
    {
        pendingSeekTick.store (tick, std::memory_order_relaxed);
        pendingSeek    .store (true, std::memory_order_relaxed);
        if (edit != nullptr)
            edit->getTransport().setCurrentPosition (ticksToBeats (tick) * 60.0
                / (double) internalBpm.load (std::memory_order_relaxed));
    }

    void setLengthTicks (int64_t ticks)
    {
        const int64_t clamped = juce::jmax ((int64_t) MidiClip::kPPQ, ticks);
        clipLengthTicks.store (clamped, std::memory_order_relaxed);

        const juce::ScopedWriteLock sl (tracksLock);
        for (auto* t : tracks)
            t->clip.setLengthTicks (clamped);

        // Update Tracktion loop region
        if (edit != nullptr)
        {
            auto& transport = edit->getTransport();
            transport.setLoopRange (te::EditTimeRange (0.0, beatsToSeconds (getLengthBeats())));
        }
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
        if (! juce::isPositiveAndBelow (i, tracks.size())) return {};
        const auto& t = *tracks[i];
        return { t.type, t.enabled, t.name, t.colour, t.sliceIdx, t.midiChannel, t.preset };
    }

    MidiClip* getClip (int trackIndex)
    {
        const juce::ScopedReadLock sl (tracksLock);
        if (juce::isPositiveAndBelow (trackIndex, tracks.size()))
            return &tracks[trackIndex]->clip;
        return nullptr;
    }

    /** Legacy single-clip accessor (main track). */
    MidiClip& getClip() { return tracks[0]->clip; }

    void setTrackEnabled (int i, bool enabled)
    {
        const juce::ScopedWriteLock sl (tracksLock);
        if (juce::isPositiveAndBelow (i, tracks.size()))
            tracks[i]->enabled = enabled;
    }

    void addMainTrack()
    {
        const juce::ScopedWriteLock sl (tracksLock);
        if (tracks.isEmpty())
        {
            auto* t = new SequencerTrack (SequencerTrack::makeMain());
            t->clip.setLengthTicks (clipLengthTicks.load (std::memory_order_relaxed));
            attachClipToEdit (*t, 0);
            tracks.add (t);
        }
    }

    void addChromaticTrack (int sliceIdx, int chromaticChannel,
                            const juce::String& name, juce::Colour colour)
    {
        const juce::ScopedWriteLock sl (tracksLock);
        for (auto* t : tracks)
            if (t->type == TrackType::ChromaticSlice && t->sliceIdx == sliceIdx)
                return;
        auto* t = new SequencerTrack (SequencerTrack::makeChromatic (sliceIdx, chromaticChannel, name, colour));
        t->clip.setLengthTicks (clipLengthTicks.load (std::memory_order_relaxed));
        attachClipToEdit (*t, tracks.size());
        tracks.add (t);
    }

    void removeChromaticTrack (int sliceIdx)
    {
        const juce::ScopedWriteLock sl (tracksLock);
        for (int i = tracks.size() - 1; i >= 0; --i)
            if (tracks[i]->type == TrackType::ChromaticSlice && tracks[i]->sliceIdx == sliceIdx)
                { removeEditTrack (i); tracks.remove (i); }
    }

    void addSfTrack (const Sf2PresetInfo& preset, juce::Colour colour)
    {
        const juce::ScopedWriteLock sl (tracksLock);
        for (auto* t : tracks)
            if (t->type == TrackType::SfPlayer
                && t->preset.bank == preset.bank
                && t->preset.preset == preset.preset)
                return;
        auto* t = new SequencerTrack (SequencerTrack::makeSfPlayer (preset, colour));
        t->clip.setLengthTicks (clipLengthTicks.load (std::memory_order_relaxed));
        attachClipToEdit (*t, tracks.size());
        tracks.add (t);
    }

    void removeSfTrack (int trackIndex)
    {
        const juce::ScopedWriteLock sl (tracksLock);
        if (juce::isPositiveAndBelow (trackIndex, tracks.size())
            && tracks[trackIndex]->type == TrackType::SfPlayer)
        {
            removeEditTrack (trackIndex);
            tracks.remove (trackIndex);
        }
    }

    void rebuildSfTracks (const std::vector<Sf2PresetInfo>& presets,
                          const juce::Colour* palette, int paletteSize)
    {
        const juce::ScopedWriteLock sl (tracksLock);
        for (int i = tracks.size() - 1; i >= 0; --i)
            if (tracks[i]->type == TrackType::SfPlayer)
                { removeEditTrack (i); tracks.remove (i); }

        const int64_t len = clipLengthTicks.load (std::memory_order_relaxed);
        for (int i = 0; i < (int) presets.size(); ++i)
        {
            const juce::Colour col = paletteSize > 0
                ? palette[i % paletteSize] : juce::Colour (0xFF406080);
            auto* t = new SequencerTrack (SequencerTrack::makeSfPlayer (presets[i], col));
            t->clip.setLengthTicks (len);
            attachClipToEdit (*t, tracks.size());
            tracks.add (t);
        }
    }

    //==========================================================================
    //  Audio thread — processBlock()
    //
    //  Identical logic to the original engine.  The Tracktion edit renders in
    //  parallel via the standalone DeviceManager; this path continues to drive
    //  the plugin's MIDI output through VoicePool / sfzPlayer exactly as before.
    //==========================================================================
    void processBlock (juce::MidiBuffer& outMidi, int numSamples, double sampleRate)
    {
        // ── Apply pending BPM from host / Link ────────────────────────────────
        const float fallbackBpm = syncToHost.load (std::memory_order_relaxed)
                                    ? hostBpm.load (std::memory_order_relaxed)
                                    : internalBpm.load (std::memory_order_relaxed);
        const float bpm = (abletonLink != nullptr && abletonLink->isEnabled())
                            ? abletonLink->getBpm (fallbackBpm)
                            : fallbackBpm;

        // Keep Tracktion edit tempo in sync with actual BPM each block
        if (bpm >= 20.f && bpm != lastAppliedBpm)
        {
            applyBpmToEdit ((double) bpm);
            lastAppliedBpm = bpm;
        }

        // ── Transport commands ────────────────────────────────────────────────
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
        if (bpm < 1.f || sampleRate < 1.0) return;

        const double ticksPerSample = (bpm / 60.0) * (double) MidiClip::kPPQ / sampleRate;
        const int64_t clipLen       = clipLengthTicks.load (std::memory_order_relaxed);
        const bool    doLoop        = looping.load (std::memory_order_relaxed);
        const double  blockEndTick  = currentTick + ticksPerSample * numSamples;

        {
            const juce::ScopedReadLock sl (tracksLock);
            for (int ti = 0; ti < tracks.size(); ++ti)
            {
                auto& track = *tracks[ti];
                if (! track.enabled) continue;

                if (track.type == TrackType::SfPlayer && justStarted)
                    outMidi.addEvent (
                        juce::MidiMessage::programChange (16, track.preset.preset), 0);

                processTrackRange (outMidi, track, ti,
                                   currentTick, blockEndTick,
                                   0, numSamples, ticksPerSample, clipLen, doLoop);
            }
        }

        justStarted = false;

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
    //  Serialisation  (identical binary format to original)
    //==========================================================================
    void writeToStream (juce::MemoryOutputStream& s) const
    {
        s.writeFloat (internalBpm    .load (std::memory_order_relaxed));
        s.writeBool  (looping        .load (std::memory_order_relaxed));
        s.writeBool  (syncToHost     .load (std::memory_order_relaxed));
        s.writeInt64 (clipLengthTicks.load (std::memory_order_relaxed));

        const juce::ScopedReadLock sl (tracksLock);
        s.writeInt (tracks.size());
        for (const auto* t : tracks)
            const_cast<SequencerTrack*>(t)->writeToStream (s);
    }

    bool readFromStream (juce::MemoryInputStream& s)
    {
        const float   bpm  = s.readFloat();
        const bool    loop = s.readBool();
        const bool    sync = s.readBool();
        const int64_t len  = s.readInt64();
        const int     n    = s.readInt();
        if (bpm < 20.f || bpm > 999.f || n < 0 || n > 256) return false;

        juce::OwnedArray<SequencerTrack> loaded;
        for (int i = 0; i < n; ++i)
        {
            auto t = std::make_unique<SequencerTrack>();
            if (! t->readFromStream (s)) return false;
            loaded.add (t.release());
        }

        internalBpm    .store (bpm,  std::memory_order_relaxed);
        looping        .store (loop, std::memory_order_relaxed);
        syncToHost     .store (sync, std::memory_order_relaxed);
        clipLengthTicks.store (len,  std::memory_order_relaxed);

        {
            const juce::ScopedWriteLock sl (tracksLock);
            tracks.clear();
            tracks.swapWith (loaded);
            // Re-attach all loaded clips to fresh edit tracks
            rebuildEditTracksLocked();
        }

        // Propagate restored BPM to Tracktion
        applyBpmToEdit ((double) bpm);
        if (edit != nullptr)
        {
            auto& transport = edit->getTransport();
            transport.looping = loop;
            transport.setLoopRange (te::EditTimeRange (0.0, beatsToSeconds (getLengthBeats())));
        }
        return true;
    }

    //==========================================================================
    //  Ableton Link
    //==========================================================================
    void setAbletonLink (AbletonLink* l) noexcept { abletonLink = l; }

    //==========================================================================
    //  Tracktion Engine access  (for standalone app / advanced use)
    //==========================================================================
    te::Edit* getEdit() const noexcept { return edit.get(); }

private:
    //==========================================================================
    //  Tracktion objects
    //==========================================================================
    te::Engine&                teEngine;
    std::unique_ptr<te::Edit>  edit;

    // Edit tracks parallel to our SequencerTrack list
    // Index i in teEditTracks corresponds to tracks[i]
    juce::OwnedArray<te::AudioTrack*> teEditTrackRefs;   // non-owning view

    //==========================================================================
    //  Our track list
    //==========================================================================
    juce::OwnedArray<SequencerTrack> tracks;
    mutable juce::ReadWriteLock      tracksLock;

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
    float                lastAppliedBpm = 0.f;

    AbletonLink* abletonLink = nullptr;

    //==========================================================================
    //  Helpers
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

    double getLengthBeats() const noexcept
    {
        return (double) clipLengthTicks.load (std::memory_order_relaxed) / (double) MidiClip::kPPQ;
    }

    static double ticksToBeats (int64_t ticks) noexcept
    {
        return (double) ticks / (double) MidiClip::kPPQ;
    }

    static double beatsToSeconds (double beats) noexcept
    {
        // Used only for Tracktion loop region — reference at 120 BPM
        return beats * 0.5;
    }

    void applyBpmToEdit (double bpm)
    {
        if (edit == nullptr) return;
        auto& ts = edit->tempoSequence;
        if (ts.getNumTempos() > 0)
            ts.getTempo (0)->setBpm (bpm);
    }

    //==========================================================================
    //  Tracktion edit track management
    //
    //  We maintain one te::AudioTrack (with a te::MidiClip on it) per
    //  SequencerTrack.  The te::MidiClip's MidiList is then attached to
    //  our MidiClip so note edits stay in sync.
    //
    //  IMPORTANT: All *Locked variants must be called while holding tracksLock
    //  for writing.
    //==========================================================================
    void attachClipToEdit (SequencerTrack& seqTrack, int /*trackIndex*/)
    {
        if (edit == nullptr) return;

        // Add an AudioTrack to the edit
        auto* audioTrack = edit->insertNewAudioTrack (
            te::TrackInsertPoint (nullptr, edit->getTrackList().getLastTrack()), nullptr).get();

        if (audioTrack == nullptr) return;

        audioTrack->setName (seqTrack.name);

        // Insert a MidiClip covering the full loop length
        const double clipEndBeats = getLengthBeats();
        te::ClipPosition pos { te::EditTimeRange (0.0, beatsToSeconds (clipEndBeats)), 0.0 };
        auto* midiClip = dynamic_cast<te::MidiClip*> (
            audioTrack->insertClip (te::TrackItem::Type::midi, pos, nullptr));

        if (midiClip != nullptr)
        {
            // Attach the te::MidiList to our MidiClip wrapper
            seqTrack.clip.attachMidiList (&midiClip->getSequence());
        }
    }

    void removeEditTrack (int index)
    {
        if (edit == nullptr || ! juce::isPositiveAndBelow (index, tracks.size()))
            return;

        // Find and delete the matching audio track in the edit
        // We identify it by name match (robust across reloads)
        const juce::String targetName = tracks[index]->name;
        for (auto* track : edit->getTrackList().getTopLevelTracks())
        {
            if (track->getName() == targetName)
            {
                edit->deleteTrack (track);
                break;
            }
        }

        // Detach the MidiList from our wrapper so it doesn't dangle
        tracks[index]->clip.attachMidiList (nullptr);
    }

    /** Re-attach all clips after a readFromStream (tracks already populated). */
    void rebuildEditTracksLocked()
    {
        if (edit == nullptr) return;

        // Clear all existing edit tracks
        for (auto* track : edit->getTrackList().getTopLevelTracks())
            edit->deleteTrack (track);

        for (int i = 0; i < tracks.size(); ++i)
            attachClipToEdit (*tracks[i], i);
    }

    //==========================================================================
    //  Audio-thread note rendering  (identical to original)
    //==========================================================================
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
};
