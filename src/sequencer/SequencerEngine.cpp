#include "SequencerEngine.h"

// Full tracktion include lives here only — not in any header.
#include <tracktion_engine/tracktion_engine.h>

#include <atomic>
#include <algorithm>
#include <cmath>

namespace te = tracktion_engine;

//==============================================================================
//  Impl  — all te:: state and the full original implementation
//==============================================================================
struct SequencerEngine::Impl
{
    //==========================================================================
    //  Tracktion objects
    //==========================================================================
    te::Engine&                teEngine;
    std::unique_ptr<te::Edit>  edit;
    juce::OwnedArray<te::AudioTrack*> teEditTrackRefs;  // non-owning view

    //==========================================================================
    //  Track list
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
    Impl()
        : teEngine (te::Engine::getInstance())
    {
        edit = te::Edit::createSingleTrackEdit (teEngine);
        jassert (edit != nullptr);

        auto& ts = edit->tempoSequence;
        ts.getTempo (0)->setBpm (120.0);
    }

    ~Impl()
    {
        if (edit != nullptr)
            edit->getTransport().stop (false, false);
    }

    //==========================================================================
    //  Helpers
    //==========================================================================
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
        return beats * 0.5;  // reference at 120 BPM
    }

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

    void applyBpmToEdit (double bpm)
    {
        if (edit == nullptr) return;
        auto& ts = edit->tempoSequence;
        if (ts.getNumTempos() > 0)
            ts.getTempo (0)->setBpm (bpm);
    }

    //==========================================================================
    //  Tracktion edit track management
    //==========================================================================
    void attachClipToEdit (SequencerTrack& seqTrack, int /*trackIndex*/)
    {
        if (edit == nullptr) return;

        auto* audioTrack = edit->insertNewAudioTrack (
            te::TrackInsertPoint (nullptr, edit->getTrackList().getLastTrack()), nullptr).get();

        if (audioTrack == nullptr) return;
        audioTrack->setName (seqTrack.name);

        const double clipEndBeats = getLengthBeats();
        te::ClipPosition pos { te::EditTimeRange (0.0, beatsToSeconds (clipEndBeats)), 0.0 };
        auto* midiClip = dynamic_cast<te::MidiClip*> (
            audioTrack->insertClip (te::TrackItem::Type::midi, pos, nullptr));

        if (midiClip != nullptr)
            seqTrack.clip.attachMidiList (&midiClip->getSequence());
    }

    void removeEditTrack (int index)
    {
        if (edit == nullptr || ! juce::isPositiveAndBelow (index, tracks.size()))
            return;

        const juce::String targetName = tracks[index]->name;
        for (auto* track : edit->getTrackList().getTopLevelTracks())
        {
            if (track->getName() == targetName)
            {
                edit->deleteTrack (track);
                break;
            }
        }
        tracks[index]->clip.attachMidiList (nullptr);
    }

    void rebuildEditTracksLocked()
    {
        if (edit == nullptr) return;
        for (auto* track : edit->getTrackList().getTopLevelTracks())
            edit->deleteTrack (track);
        for (int i = 0; i < tracks.size(); ++i)
            attachClipToEdit (*tracks[i], i);
    }

    //==========================================================================
    //  Audio-thread note rendering
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

//==============================================================================
//  SequencerEngine — forwards everything to Impl
//==============================================================================
SequencerEngine::SequencerEngine()  : impl (std::make_unique<Impl>()) {}
SequencerEngine::~SequencerEngine() = default;

//==============================================================================
bool    SequencerEngine::isPlaying()       const noexcept { return impl->playing.load   (std::memory_order_relaxed); }
bool    SequencerEngine::isLooping()       const noexcept { return impl->looping.load   (std::memory_order_relaxed); }
bool    SequencerEngine::isRecording()     const noexcept { return impl->recording.load (std::memory_order_relaxed); }
int64_t SequencerEngine::getPlayheadTick() const noexcept { return impl->playheadTick.load (std::memory_order_relaxed); }
double  SequencerEngine::getPlayheadBeats() const noexcept { return (double) getPlayheadTick() / (double) MidiClip::kPPQ; }
float   SequencerEngine::getBpm()          const noexcept { return impl->internalBpm.load (std::memory_order_relaxed); }
bool    SequencerEngine::getSyncToHost()   const noexcept { return impl->syncToHost.load (std::memory_order_relaxed); }
int64_t SequencerEngine::getLengthTicks()  const noexcept { return impl->clipLengthTicks.load (std::memory_order_relaxed); }

//==============================================================================
void SequencerEngine::play()
{
    if (impl->abletonLink != nullptr && impl->abletonLink->isEnabled())
        impl->abletonLink->requestBeatAlignedStart (4.0);
    impl->pendingPlay.store (true, std::memory_order_relaxed);

    if (impl->edit != nullptr)
    {
        auto& t = impl->edit->getTransport();
        t.setLoopRange (te::EditTimeRange (0.0, impl->beatsToSeconds (impl->getLengthBeats())));
        t.looping = impl->looping.load (std::memory_order_relaxed);
        t.play (false);
    }
}

void SequencerEngine::stop()
{
    if (impl->abletonLink != nullptr && impl->abletonLink->isEnabled())
        impl->abletonLink->notifyStop();
    impl->pendingStop.store (true, std::memory_order_relaxed);

    if (impl->edit != nullptr)
        impl->edit->getTransport().stop (false, false);
}

void SequencerEngine::rewind()
{
    impl->pendingRewind.store (true, std::memory_order_relaxed);
    if (impl->edit != nullptr)
        impl->edit->getTransport().setCurrentPosition (0.0);
}

void SequencerEngine::setLooping (bool v)
{
    impl->looping.store (v, std::memory_order_relaxed);
    if (impl->edit != nullptr)
        impl->edit->getTransport().looping = v;
}

void SequencerEngine::setRecording  (bool v) { impl->recording.store  (v, std::memory_order_relaxed); }
void SequencerEngine::setSyncToHost (bool v) { impl->syncToHost.store (v, std::memory_order_relaxed); }

void SequencerEngine::setBpm (float b)
{
    const float clamped = juce::jlimit (20.f, 999.f, b);
    impl->internalBpm.store (clamped, std::memory_order_relaxed);
    impl->applyBpmToEdit ((double) clamped);
    if (impl->abletonLink != nullptr && impl->abletonLink->isEnabled())
        impl->abletonLink->setBpm ((double) clamped);
}

void SequencerEngine::setHostBpm (float b)
{
    impl->hostBpm.store (juce::jlimit (20.f, 999.f, b), std::memory_order_relaxed);
}

void SequencerEngine::seekToTick (int64_t tick)
{
    impl->pendingSeekTick.store (tick, std::memory_order_relaxed);
    impl->pendingSeek    .store (true, std::memory_order_relaxed);
    if (impl->edit != nullptr)
        impl->edit->getTransport().setCurrentPosition (
            impl->ticksToBeats (tick) * 60.0
            / (double) impl->internalBpm.load (std::memory_order_relaxed));
}

void SequencerEngine::setLengthTicks (int64_t ticks)
{
    const int64_t clamped = juce::jmax ((int64_t) MidiClip::kPPQ, ticks);
    impl->clipLengthTicks.store (clamped, std::memory_order_relaxed);

    const juce::ScopedWriteLock sl (impl->tracksLock);
    for (auto* t : impl->tracks)
        t->clip.setLengthTicks (clamped);

    if (impl->edit != nullptr)
        impl->edit->getTransport().setLoopRange (
            te::EditTimeRange (0.0, impl->beatsToSeconds (impl->getLengthBeats())));
}

//==============================================================================
int SequencerEngine::getNumTracks() const
{
    const juce::ScopedReadLock sl (impl->tracksLock);
    return impl->tracks.size();
}

SequencerTrackInfo SequencerEngine::getTrackInfo (int i) const
{
    const juce::ScopedReadLock sl (impl->tracksLock);
    if (! juce::isPositiveAndBelow (i, impl->tracks.size())) return {};
    const auto& t = *impl->tracks[i];
    return { t.type, t.enabled, t.name, t.colour, t.sliceIdx, t.midiChannel, t.preset };
}

MidiClip* SequencerEngine::getClip (int trackIndex)
{
    const juce::ScopedReadLock sl (impl->tracksLock);
    if (juce::isPositiveAndBelow (trackIndex, impl->tracks.size()))
        return &impl->tracks[trackIndex]->clip;
    return nullptr;
}

MidiClip& SequencerEngine::getClip() { return impl->tracks[0]->clip; }

void SequencerEngine::setTrackEnabled (int i, bool enabled)
{
    const juce::ScopedWriteLock sl (impl->tracksLock);
    if (juce::isPositiveAndBelow (i, impl->tracks.size()))
        impl->tracks[i]->enabled = enabled;
}

void SequencerEngine::addMainTrack()
{
    const juce::ScopedWriteLock sl (impl->tracksLock);
    if (impl->tracks.isEmpty())
    {
        auto* t = new SequencerTrack (SequencerTrack::makeMain());
        t->clip.setLengthTicks (impl->clipLengthTicks.load (std::memory_order_relaxed));
        impl->attachClipToEdit (*t, 0);
        impl->tracks.add (t);
    }
}

void SequencerEngine::addChromaticTrack (int sliceIdx, int chromaticChannel,
                                          const juce::String& name, juce::Colour colour)
{
    const juce::ScopedWriteLock sl (impl->tracksLock);
    for (auto* t : impl->tracks)
        if (t->type == TrackType::ChromaticSlice && t->sliceIdx == sliceIdx)
            return;
    auto* t = new SequencerTrack (SequencerTrack::makeChromatic (sliceIdx, chromaticChannel, name, colour));
    t->clip.setLengthTicks (impl->clipLengthTicks.load (std::memory_order_relaxed));
    impl->attachClipToEdit (*t, impl->tracks.size());
    impl->tracks.add (t);
}

void SequencerEngine::removeChromaticTrack (int sliceIdx)
{
    const juce::ScopedWriteLock sl (impl->tracksLock);
    for (int i = impl->tracks.size() - 1; i >= 0; --i)
        if (impl->tracks[i]->type == TrackType::ChromaticSlice && impl->tracks[i]->sliceIdx == sliceIdx)
            { impl->removeEditTrack (i); impl->tracks.remove (i); }
}

void SequencerEngine::addSfTrack (const Sf2PresetInfo& preset, juce::Colour colour)
{
    const juce::ScopedWriteLock sl (impl->tracksLock);
    for (auto* t : impl->tracks)
        if (t->type == TrackType::SfPlayer
            && t->preset.bank == preset.bank
            && t->preset.preset == preset.preset)
            return;
    auto* t = new SequencerTrack (SequencerTrack::makeSfPlayer (preset, colour));
    t->clip.setLengthTicks (impl->clipLengthTicks.load (std::memory_order_relaxed));
    impl->attachClipToEdit (*t, impl->tracks.size());
    impl->tracks.add (t);
}

void SequencerEngine::removeSfTrack (int trackIndex)
{
    const juce::ScopedWriteLock sl (impl->tracksLock);
    if (juce::isPositiveAndBelow (trackIndex, impl->tracks.size())
        && impl->tracks[trackIndex]->type == TrackType::SfPlayer)
    {
        impl->removeEditTrack (trackIndex);
        impl->tracks.remove (trackIndex);
    }
}

void SequencerEngine::rebuildSfTracks (const std::vector<Sf2PresetInfo>& presets,
                                        const juce::Colour* palette, int paletteSize)
{
    const juce::ScopedWriteLock sl (impl->tracksLock);
    for (int i = impl->tracks.size() - 1; i >= 0; --i)
        if (impl->tracks[i]->type == TrackType::SfPlayer)
            { impl->removeEditTrack (i); impl->tracks.remove (i); }

    const int64_t len = impl->clipLengthTicks.load (std::memory_order_relaxed);
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        const juce::Colour col = paletteSize > 0
            ? palette[i % paletteSize] : juce::Colour (0xFF406080);
        auto* t = new SequencerTrack (SequencerTrack::makeSfPlayer (presets[i], col));
        t->clip.setLengthTicks (len);
        impl->attachClipToEdit (*t, impl->tracks.size());
        impl->tracks.add (t);
    }
}

//==============================================================================
void SequencerEngine::setAbletonLink (AbletonLink* l) noexcept { impl->abletonLink = l; }

//==============================================================================
void SequencerEngine::processBlock (juce::MidiBuffer& outMidi, int numSamples, double sampleRate)
{
    const float fallbackBpm = impl->syncToHost.load (std::memory_order_relaxed)
                                ? impl->hostBpm.load (std::memory_order_relaxed)
                                : impl->internalBpm.load (std::memory_order_relaxed);
    const float bpm = (impl->abletonLink != nullptr && impl->abletonLink->isEnabled())
                        ? impl->abletonLink->getBpm (fallbackBpm)
                        : fallbackBpm;

    if (bpm >= 20.f && bpm != impl->lastAppliedBpm)
    {
        impl->applyBpmToEdit ((double) bpm);
        impl->lastAppliedBpm = bpm;
    }

    if (impl->pendingStop.exchange (false, std::memory_order_relaxed))
    {
        if (impl->playing.load (std::memory_order_relaxed))
        {
            impl->playing.store (false, std::memory_order_relaxed);
            impl->flushAllActiveNotes (outMidi, 0);
        }
    }
    if (impl->pendingRewind.exchange (false, std::memory_order_relaxed))
    {
        impl->flushAllActiveNotes (outMidi, 0);
        impl->currentTick = 0.0;
        impl->playheadTick.store (0, std::memory_order_relaxed);
        impl->justStarted = true;
    }
    if (impl->pendingSeek.exchange (false, std::memory_order_relaxed))
    {
        impl->flushAllActiveNotes (outMidi, 0);
        impl->currentTick = (double) impl->pendingSeekTick.load (std::memory_order_relaxed);
        impl->playheadTick.store ((int64_t) impl->currentTick, std::memory_order_relaxed);
    }
    if (impl->pendingPlay.exchange (false, std::memory_order_relaxed))
    {
        impl->playing.store (true, std::memory_order_relaxed);
        impl->justStarted = true;
    }

    if (! impl->playing.load (std::memory_order_relaxed)) return;
    if (bpm < 1.f || sampleRate < 1.0) return;

    const double ticksPerSample = (bpm / 60.0) * (double) MidiClip::kPPQ / sampleRate;
    const int64_t clipLen       = impl->clipLengthTicks.load (std::memory_order_relaxed);
    const bool    doLoop        = impl->looping.load (std::memory_order_relaxed);
    const double  blockEndTick  = impl->currentTick + ticksPerSample * numSamples;

    {
        const juce::ScopedReadLock sl (impl->tracksLock);
        for (int ti = 0; ti < impl->tracks.size(); ++ti)
        {
            auto& track = *impl->tracks[ti];
            if (! track.enabled) continue;

            if (track.type == TrackType::SfPlayer && impl->justStarted)
                outMidi.addEvent (
                    juce::MidiMessage::programChange (16, track.preset.preset), 0);

            impl->processTrackRange (outMidi, track, ti,
                                     impl->currentTick, blockEndTick,
                                     0, numSamples, ticksPerSample, clipLen, doLoop);
        }
    }

    impl->justStarted = false;

    if (doLoop && clipLen > 0 && blockEndTick >= (double) clipLen)
    {
        const int loopSample = juce::jlimit (0, numSamples - 1,
            (int)(((double) clipLen - impl->currentTick) / ticksPerSample));
        impl->flushAllActiveNotes (outMidi, loopSample);
        impl->currentTick = std::fmod (blockEndTick, (double) clipLen);
        impl->justStarted = true;
    }
    else
    {
        impl->currentTick = blockEndTick;
    }

    impl->playheadTick.store ((int64_t) impl->currentTick, std::memory_order_relaxed);
}

//==============================================================================
void SequencerEngine::writeToStream (juce::MemoryOutputStream& s) const
{
    s.writeFloat (impl->internalBpm    .load (std::memory_order_relaxed));
    s.writeBool  (impl->looping        .load (std::memory_order_relaxed));
    s.writeBool  (impl->syncToHost     .load (std::memory_order_relaxed));
    s.writeInt64 (impl->clipLengthTicks.load (std::memory_order_relaxed));

    const juce::ScopedReadLock sl (impl->tracksLock);
    s.writeInt (impl->tracks.size());
    for (const auto* t : impl->tracks)
        const_cast<SequencerTrack*>(t)->writeToStream (s);
}

bool SequencerEngine::readFromStream (juce::MemoryInputStream& s)
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

    impl->internalBpm    .store (bpm,  std::memory_order_relaxed);
    impl->looping        .store (loop, std::memory_order_relaxed);
    impl->syncToHost     .store (sync, std::memory_order_relaxed);
    impl->clipLengthTicks.store (len,  std::memory_order_relaxed);

    {
        const juce::ScopedWriteLock sl (impl->tracksLock);
        impl->tracks.clear();
        impl->tracks.swapWith (loaded);
        impl->rebuildEditTracksLocked();
    }

    impl->applyBpmToEdit ((double) bpm);
    if (impl->edit != nullptr)
    {
        auto& transport = impl->edit->getTransport();
        transport.looping = loop;
        transport.setLoopRange (te::EditTimeRange (0.0, impl->beatsToSeconds (impl->getLengthBeats())));
    }
    return true;
}
