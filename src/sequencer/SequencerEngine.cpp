// SequencerEngine.cpp
// tracktion Engine/Edit/Transport layer has been removed.
// All note scheduling is done by DYSEKT-SF's own tick scheduler in processBlock().
// MidiClip.cpp retains its tracktion hook (attachMidiList) for future use.

#include "SequencerEngine.h"
#include "../audio/SfzPlayer.h"

#include <atomic>
#include <algorithm>
#include <cmath>

// Stream version tags
static constexpr int kStreamVersion1 = 1;  // legacy single-clip
static constexpr int kStreamVersion2 = 2;  // multi-clip

//==============================================================================
struct SequencerEngine::Impl
{
    //==========================================================================
    juce::OwnedArray<SequencerTrack> tracks;
    mutable juce::ReadWriteLock      tracksLock;

    double               currentTick = 0.0;
    bool                 justStarted = false;
    std::atomic<int64_t> playheadTick { 0 };

    struct ActiveNote { int trackIdx; int clipIdx; int note; int channel; };
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
    std::atomic<bool>    syncToHost   { false };
    float                lastAppliedBpm = 0.f;

    AbletonLink* abletonLink = nullptr;
    SfzPlayer*   sfzPlayer   = nullptr;

    std::atomic<bool> midiActivityFlags[SequencerEngine::kActivityFlagCount] = {};
    std::atomic<int>  selectedLiveChannel  { 0 };  // 1-based; 0 = disabled
    std::atomic<int>  recordingTrackIndex  { -1 }; // which track receives recorded MIDI (-1 = none)

    // Tracks notes currently held during recording so we can set their real
    // duration when the note-off arrives.  Audio-thread only.
    struct OpenRecNote { int note; int64_t startTick; int noteIndexInClip; };
    juce::Array<OpenRecNote> openRecNotes;

    //==========================================================================
    Impl()  = default;
    ~Impl() = default;

    //==========================================================================
    static int midiChannelForTrack (const SequencerTrack& t) noexcept
    {
        switch (t.type)
        {
            case TrackType::MainSlice:      return 1;
            case TrackType::ChromaticSlice: return t.midiChannel + 1;
            case TrackType::SfPlayer:       return t.midiChannel + 1;  // per-track FluidSynth channel (0-based → 1-based MIDI)
        }
        return 1;
    }

    /** Compute the global end tick = rightmost clip end across all tracks. */
    int64_t computeMasterLen() const
    {
        int64_t masterLen = MidiClip::kPPQ * 4 * 4;
        for (auto* track : tracks)
            for (auto* slot : track->clips)
                masterLen = juce::jmax (masterLen, slot->endTick());
        return masterLen;
    }

    //==========================================================================
    //  Audio-thread note rendering — processes one ClipSlot
    //==========================================================================
    void processClipSlot (juce::MidiBuffer& outMidi, SequencerTrack& track,
                          int trackIdx, int clipIdx, const ClipSlot& slot,
                          double playheadStart, double playheadEnd,
                          int numSamples, double ticksPerSample, bool doLoop,
                          int64_t masterLen)
    {
        const int ch = midiChannelForTrack (track);
        const double clipStart = (double) slot.startTick;
        const double clipEnd   = (double) slot.endTick();

        double localStart = playheadStart;
        double localEnd   = playheadEnd;

        if (doLoop && masterLen > 0)
        {
            localStart = std::fmod (localStart, (double) masterLen);
            localEnd   = localStart + (playheadEnd - playheadStart);
        }

        if (localEnd <= clipStart || localStart >= clipEnd)
            return;

        const double winStart = juce::jmax (localStart, clipStart);
        const double winEnd   = juce::jmin (localEnd,   clipEnd);

        const double clipLocalStart = winStart - clipStart;
        const double clipLocalEnd   = winEnd   - clipStart;

        const juce::ScopedReadLock cl (slot.clip.getLock());
        for (const auto& n : slot.clip.getNotes())
        {
            const double nStart = (double) n.startTick;
            const double nEnd   = (double) n.endTick();

            if (nStart >= clipLocalStart && nStart < clipLocalEnd)
            {
                const int sp = juce::jlimit (0, numSamples - 1,
                    (int)((nStart - clipLocalStart + (winStart - localStart)) / ticksPerSample));
                outMidi.addEvent (
                    juce::MidiMessage::noteOn (ch, n.note, (juce::uint8) n.velocity), sp);
                activeNotes.add ({ trackIdx, clipIdx, n.note, ch });
            }

            if (nEnd > clipLocalStart && nEnd <= clipLocalEnd)
            {
                const int sp = juce::jlimit (0, numSamples - 1,
                    (int)((nEnd - clipLocalStart + (winStart - localStart)) / ticksPerSample));
                outMidi.addEvent (juce::MidiMessage::noteOff (ch, n.note), sp);
                for (int i = activeNotes.size() - 1; i >= 0; --i)
                    if (activeNotes[i].trackIdx == trackIdx
                        && activeNotes[i].clipIdx == clipIdx
                        && activeNotes[i].note == n.note)
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
SequencerEngine::SequencerEngine()  : impl (std::make_unique<Impl>()) {}
SequencerEngine::~SequencerEngine() = default;

//==============================================================================
bool    SequencerEngine::isPlaying()        const noexcept { return impl->playing.load   (std::memory_order_relaxed); }
bool    SequencerEngine::isLooping()        const noexcept { return impl->looping.load   (std::memory_order_relaxed); }
bool    SequencerEngine::isRecording()      const noexcept { return impl->recording.load (std::memory_order_relaxed); }
int64_t SequencerEngine::getPlayheadTick()  const noexcept { return impl->playheadTick.load (std::memory_order_relaxed); }
double  SequencerEngine::getPlayheadBeats() const noexcept { return (double) getPlayheadTick() / (double) MidiClip::kPPQ; }
float   SequencerEngine::getBpm()           const noexcept { return impl->internalBpm.load (std::memory_order_relaxed); }
bool    SequencerEngine::getSyncToHost()    const noexcept { return impl->syncToHost.load (std::memory_order_relaxed); }

int64_t SequencerEngine::getLengthTicks() const noexcept
{
    const juce::ScopedReadLock sl (impl->tracksLock);
    return impl->computeMasterLen();
}

//==============================================================================
void SequencerEngine::play()
{
    if (impl->abletonLink != nullptr && impl->abletonLink->isEnabled())
        impl->abletonLink->requestBeatAlignedStart (4.0);
    impl->pendingPlay.store (true, std::memory_order_relaxed);
}

void SequencerEngine::stop()
{
    if (impl->abletonLink != nullptr && impl->abletonLink->isEnabled())
        impl->abletonLink->notifyStop();
    impl->pendingStop.store (true, std::memory_order_relaxed);
}

void SequencerEngine::rewind()
{
    impl->pendingRewind.store (true, std::memory_order_relaxed);
}

void SequencerEngine::setLooping (bool v)
{
    impl->looping.store (v, std::memory_order_relaxed);
}

void SequencerEngine::setRecording  (bool v) { impl->recording.store  (v, std::memory_order_relaxed); }
void SequencerEngine::setSyncToHost (bool v) { impl->syncToHost.store (v, std::memory_order_relaxed); }

void SequencerEngine::setBpm (float b)
{
    const float clamped = juce::jlimit (20.f, 999.f, b);
    impl->internalBpm.store (clamped, std::memory_order_relaxed);
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
}

void SequencerEngine::setLengthTicks (int64_t ticks)
{
    const int64_t clamped = juce::jmax ((int64_t) MidiClip::kPPQ, ticks);
    const juce::ScopedWriteLock sl (impl->tracksLock);
    for (auto* t : impl->tracks)
        if (! t->clips.isEmpty())
            t->clips[0]->clip.setLengthTicks (clamped);
}

//==============================================================================
//  Track management
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
    return { t.type, t.enabled, t.name, t.colour, t.sliceIdx, t.midiChannel, t.preset,
             t.clips.size() };
}

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
        impl->tracks.add (new SequencerTrack (SequencerTrack::makeMain()));
}

void SequencerEngine::addChromaticTrack (int sliceIdx, int chromaticChannel,
                                          const juce::String& name, juce::Colour colour)
{
    const juce::ScopedWriteLock sl (impl->tracksLock);
    for (auto* t : impl->tracks)
        if (t->type == TrackType::ChromaticSlice && t->sliceIdx == sliceIdx) return;
    impl->tracks.add (new SequencerTrack (
        SequencerTrack::makeChromatic (sliceIdx, chromaticChannel, name, colour)));
}

void SequencerEngine::removeChromaticTrack (int sliceIdx)
{
    const juce::ScopedWriteLock sl (impl->tracksLock);
    for (int i = impl->tracks.size() - 1; i >= 0; --i)
        if (impl->tracks[i]->type == TrackType::ChromaticSlice
            && impl->tracks[i]->sliceIdx == sliceIdx)
            impl->tracks.remove (i);
}

void SequencerEngine::addSfTrack (const Sf2PresetInfo& preset, juce::Colour colour)
{
    const juce::ScopedWriteLock sl (impl->tracksLock);
    for (auto* t : impl->tracks)
        if (t->type == TrackType::SfPlayer
            && t->preset.bank   == preset.bank
            && t->preset.preset == preset.preset) return;

    // Assign the next available FluidSynth channel (0-15) to this track.
    // Count existing SfPlayer tracks to determine the channel index.
    int sfCh = 0;
    for (auto* t : impl->tracks)
        if (t->type == TrackType::SfPlayer)
            sfCh = juce::jmax (sfCh, t->midiChannel + 1);
    sfCh = juce::jmin (sfCh, 15);

    auto track = SequencerTrack::makeSfPlayer (preset, colour);
    track.midiChannel = sfCh;
    impl->tracks.add (new SequencerTrack (std::move (track)));

    if (impl->sfzPlayer != nullptr)
        impl->sfzPlayer->setPresetOnChannel (sfCh, preset.bank, preset.preset);
}

void SequencerEngine::removeSfTrack (int trackIndex)
{
    const juce::ScopedWriteLock sl (impl->tracksLock);
    if (juce::isPositiveAndBelow (trackIndex, impl->tracks.size())
        && impl->tracks[trackIndex]->type == TrackType::SfPlayer)
        impl->tracks.remove (trackIndex);
}

void SequencerEngine::rebuildSfTracks (const std::vector<Sf2PresetInfo>& presets,
                                        const juce::Colour* palette, int paletteSize)
{
    const juce::ScopedWriteLock sl (impl->tracksLock);
    for (int i = impl->tracks.size() - 1; i >= 0; --i)
        if (impl->tracks[i]->type == TrackType::SfPlayer)
            impl->tracks.remove (i);

    for (int i = 0; i < (int) presets.size(); ++i)
    {
        const juce::Colour col = paletteSize > 0
            ? palette[i % paletteSize] : juce::Colour (0xFF406080);
        auto track = SequencerTrack::makeSfPlayer (presets[i], col);
        track.midiChannel = juce::jmin (i, 15);   // sequential FluidSynth channels
        impl->tracks.add (new SequencerTrack (std::move (track)));

        if (impl->sfzPlayer != nullptr)
            impl->sfzPlayer->setPresetOnChannel (juce::jmin (i, 15),
                                                  presets[i].bank,
                                                  presets[i].preset);
    }
}

void SequencerEngine::addOrUpdateSfTrackOnChannel (const Sf2PresetInfo& preset,
                                                    int midiChannel0Based,
                                                    juce::Colour colour)
{
    const int ch = juce::jlimit (0, 15, midiChannel0Based);
    bool needsPlayerUpdate = false;

    {
        const juce::ScopedWriteLock sl (impl->tracksLock);

        // If a track already exists for this preset, just update its channel.
        for (auto* t : impl->tracks)
        {
            if (t->type == TrackType::SfPlayer
                && t->preset.bank   == preset.bank
                && t->preset.preset == preset.preset)
            {
                t->midiChannel = ch;
                needsPlayerUpdate = true;
                break;
            }
        }

        if (! needsPlayerUpdate)
        {
            // New track for this preset on the chosen channel.
            auto track = SequencerTrack::makeSfPlayer (preset, colour);
            track.midiChannel = ch;
            impl->tracks.add (new SequencerTrack (std::move (track)));
            needsPlayerUpdate = true;
        }
    }

    // Call sfzPlayer outside the lock to avoid potential deadlock.
    if (needsPlayerUpdate && impl->sfzPlayer != nullptr)
        impl->sfzPlayer->setPresetOnChannel (ch, preset.bank, preset.preset);
}

void SequencerEngine::addSfzTrack (const juce::String& name, int midiChannel0Based,
                                    juce::Colour colour)
{
    const int ch = juce::jlimit (0, 15, midiChannel0Based);
    const juce::ScopedWriteLock sl (impl->tracksLock);

    // Remove any existing SfPlayer track with the same name (previous SFZ load).
    for (int i = impl->tracks.size() - 1; i >= 0; --i)
        if (impl->tracks[i]->type == TrackType::SfPlayer
            && impl->tracks[i]->name == name)
            impl->tracks.remove (i);

    Sf2PresetInfo sfzPreset;
    sfzPreset.name   = name;
    sfzPreset.bank   = 0;
    sfzPreset.preset = 0;

    auto track = SequencerTrack::makeSfPlayer (sfzPreset, colour);
    track.midiChannel = ch;
    impl->tracks.add (new SequencerTrack (std::move (track)));
}

//==============================================================================
//  Clip management
//==============================================================================
int SequencerEngine::getNumClips (int trackIndex) const
{
    const juce::ScopedReadLock sl (impl->tracksLock);
    if (juce::isPositiveAndBelow (trackIndex, impl->tracks.size()))
        return impl->tracks[trackIndex]->clips.size();
    return 0;
}

SequencerClipInfo SequencerEngine::getClipInfo (int trackIndex, int clipIndex) const
{
    const juce::ScopedReadLock sl (impl->tracksLock);
    if (juce::isPositiveAndBelow (trackIndex, impl->tracks.size()))
    {
        const auto& track = *impl->tracks[trackIndex];
        if (juce::isPositiveAndBelow (clipIndex, track.clips.size()))
        {
            const auto& slot = *track.clips[clipIndex];
            return { slot.startTick, slot.clip.getLengthTicks() };
        }
    }
    return {};
}

MidiClip* SequencerEngine::getClip (int trackIndex, int clipIndex)
{
    const juce::ScopedReadLock sl (impl->tracksLock);
    if (juce::isPositiveAndBelow (trackIndex, impl->tracks.size()))
    {
        auto& track = *impl->tracks[trackIndex];
        if (juce::isPositiveAndBelow (clipIndex, track.clips.size()))
            return &track.clips[clipIndex]->clip;
    }
    return nullptr;
}

MidiClip& SequencerEngine::getClip()
{
    return impl->tracks[0]->clips[0]->clip;
}

int SequencerEngine::addClip (int trackIndex, int64_t startTick, int64_t lengthTicks)
{
    const juce::ScopedWriteLock sl (impl->tracksLock);
    if (! juce::isPositiveAndBelow (trackIndex, impl->tracks.size())) return -1;
    return impl->tracks[trackIndex]->addClip (startTick, lengthTicks);
}

void SequencerEngine::removeClip (int trackIndex, int clipIndex)
{
    const juce::ScopedWriteLock sl (impl->tracksLock);
    if (juce::isPositiveAndBelow (trackIndex, impl->tracks.size()))
        impl->tracks[trackIndex]->removeClip (clipIndex);
}

void SequencerEngine::setClipStartTick (int trackIndex, int clipIndex, int64_t newStartTick)
{
    const juce::ScopedWriteLock sl (impl->tracksLock);
    if (juce::isPositiveAndBelow (trackIndex, impl->tracks.size()))
    {
        auto& track = *impl->tracks[trackIndex];
        if (juce::isPositiveAndBelow (clipIndex, track.clips.size()))
        {
            track.clips[clipIndex]->startTick = juce::jmax ((int64_t) 0, newStartTick);
            track.sortClips();
        }
    }
}

void SequencerEngine::setClipLengthTicks (int trackIndex, int clipIndex, int64_t newLength)
{
    const juce::ScopedWriteLock sl (impl->tracksLock);
    if (juce::isPositiveAndBelow (trackIndex, impl->tracks.size()))
    {
        auto& track = *impl->tracks[trackIndex];
        if (juce::isPositiveAndBelow (clipIndex, track.clips.size()))
            track.clips[clipIndex]->clip.setLengthTicks (
                juce::jmax ((int64_t) MidiClip::kPPQ, newLength));
    }
}

int64_t SequencerEngine::getTrackLengthTicks (int trackIndex) const noexcept
{
    const juce::ScopedReadLock sl (impl->tracksLock);
    if (juce::isPositiveAndBelow (trackIndex, impl->tracks.size()))
    {
        const auto& track = *impl->tracks[trackIndex];
        if (! track.clips.isEmpty())
            return track.clips[0]->clip.getLengthTicks();
    }
    return MidiClip::kPPQ * 4 * 4;
}

void SequencerEngine::setTrackLengthTicks (int trackIndex, int64_t ticks)
{
    setClipLengthTicks (trackIndex, 0, ticks);
}

//==============================================================================
void SequencerEngine::setAbletonLink (AbletonLink* l) noexcept { impl->abletonLink = l; }
void SequencerEngine::setSfzPlayer   (SfzPlayer*   p) noexcept { impl->sfzPlayer   = p; }

void SequencerEngine::setSelectedSfLiveChannels (uint16_t channelMask) noexcept
{
    if (impl->sfzPlayer != nullptr)
        impl->sfzPlayer->setLiveInputChannelMask (channelMask);
}

uint16_t SequencerEngine::getAllSfPlayerChannelMask() const noexcept
{
    uint16_t mask = 0;
    const juce::ScopedReadLock sl (impl->tracksLock);
    for (auto* t : impl->tracks)
        if (t->type == TrackType::SfPlayer)
            mask |= static_cast<uint16_t> (1u << (t->midiChannel & 0xF));
    return mask;
}

//==============================================================================
void SequencerEngine::setSelectedLiveChannel (int ch1Based) noexcept
{
    impl->selectedLiveChannel.store (ch1Based, std::memory_order_relaxed);
}

int SequencerEngine::getSelectedLiveChannel() const noexcept
{
    return impl->selectedLiveChannel.load (std::memory_order_relaxed);
}

void SequencerEngine::setRecordingTrack (int trackIndex) noexcept
{
    impl->recordingTrackIndex.store (trackIndex, std::memory_order_relaxed);
    impl->openRecNotes.clearQuick();  // discard any held notes from previous selection
}

//==============================================================================
bool SequencerEngine::getMidiActivityAndClear (int trackIndex) noexcept
{
    if (! juce::isPositiveAndBelow (trackIndex, kActivityFlagCount))
        return false;
    return impl->midiActivityFlags[trackIndex].exchange (false, std::memory_order_relaxed);
}

//==============================================================================
void SequencerEngine::processBlock (juce::MidiBuffer& outMidi, const juce::MidiBuffer& inMidi,
                                    int numSamples, double sampleRate)
{
    const float fallbackBpm = impl->syncToHost.load (std::memory_order_relaxed)
                                ? impl->hostBpm.load     (std::memory_order_relaxed)
                                : impl->internalBpm.load (std::memory_order_relaxed);
    const float bpm = (impl->abletonLink != nullptr && impl->abletonLink->isEnabled())
                        ? impl->abletonLink->getBpm (fallbackBpm)
                        : fallbackBpm;

    // Track effective BPM even though there is no Edit to push it to.
    if (bpm >= 20.f && bpm != impl->lastAppliedBpm)
        impl->lastAppliedBpm = bpm;

    if (impl->pendingStop.exchange (false, std::memory_order_relaxed))
    {
        if (impl->playing.load (std::memory_order_relaxed))
        {
            impl->playing.store (false, std::memory_order_relaxed);
            impl->flushAllActiveNotes (outMidi, 0);
            impl->openRecNotes.clearQuick();
        }
    }
    if (impl->pendingRewind.exchange (false, std::memory_order_relaxed))
    {
        impl->flushAllActiveNotes (outMidi, 0);
        impl->openRecNotes.clearQuick();
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
    const bool   doLoop         = impl->looping.load (std::memory_order_relaxed);
    const double blockEndTick   = impl->currentTick + ticksPerSample * numSamples;

    int64_t masterLen = 0;
    {
        const juce::ScopedReadLock sl (impl->tracksLock);
        masterLen = impl->computeMasterLen();

        for (int ti = 0; ti < impl->tracks.size(); ++ti)
        {
            auto& track = *impl->tracks[ti];
            if (! track.enabled) continue;

            const int priorSize = outMidi.getNumEvents();

            for (int ci = 0; ci < track.clips.size(); ++ci)
            {
                impl->processClipSlot (outMidi, track, ti, ci, *track.clips[ci],
                                       impl->currentTick, blockEndTick,
                                       numSamples, ticksPerSample, doLoop, masterLen);
            }

            if (outMidi.getNumEvents() > priorSize
                && juce::isPositiveAndBelow (ti, kActivityFlagCount))
                impl->midiActivityFlags[ti].store (true, std::memory_order_relaxed);
        }
    }

    impl->justStarted = false;

    // ── MIDI input recording ──────────────────────────────────────────────────
    // Cubase-style: the selected (record-armed) track captures all incoming
    // MIDI regardless of channel.  Note-offs close open notes with their real
    // duration rather than a fixed placeholder.
    const int recTi = impl->recordingTrackIndex.load (std::memory_order_relaxed);
    if (impl->recording.load (std::memory_order_relaxed)
        && impl->playing.load (std::memory_order_relaxed)
        && juce::isPositiveAndBelow (recTi, impl->tracks.size()))
    {
        const juce::ScopedReadLock sl (impl->tracksLock);
        auto& track = *impl->tracks[recTi];
        if (! track.clips.isEmpty())
        {
            auto& clip = track.clips[0]->clip;
            const int64_t mLen        = impl->computeMasterLen();
            const double  ticksPerSmp = (bpm / 60.0) * (double) MidiClip::kPPQ / sampleRate;

            for (const auto meta : inMidi)
            {
                const auto msg = meta.getMessage();
                if (msg.getChannel() == 16) continue;  // skip SFZ-internal channel

                const double  evTick = impl->currentTick + meta.samplePosition * ticksPerSmp;
                const int64_t t      = (int64_t) std::fmod (evTick, (double) mLen);

                if (msg.isNoteOn (true))   // true = treat velocity-0 as note-off
                {
                    MidiNote n;
                    n.note         = msg.getNoteNumber();
                    n.velocity     = msg.getVelocity();
                    n.startTick    = t;
                    n.durationTick = MidiClip::kPPQ / 4;  // filled in on note-off
                    const int idx  = clip.addNote (n);     // addNote returns index
                    impl->openRecNotes.add ({ n.note, t, idx });
                }
                else if (msg.isNoteOff (true))
                {
                    const int noteNum = msg.getNoteNumber();
                    for (int i = impl->openRecNotes.size() - 1; i >= 0; --i)
                    {
                        auto& orn = impl->openRecNotes.getReference (i);
                        if (orn.note == noteNum)
                        {
                            // Compute real duration, clamped to loop length
                            int64_t dur = t - orn.startTick;
                            if (dur <= 0) dur += mLen;   // wrapped loop
                            if (dur <= 0) dur  = MidiClip::kPPQ / 4;
                            clip.setNoteDuration (orn.noteIndexInClip, dur);
                            impl->openRecNotes.remove (i);
                            break;
                        }
                    }
                }
            }
        }
    }

    if (doLoop && masterLen > 0 && blockEndTick >= (double) masterLen)
    {
        const int loopSample = juce::jlimit (0, numSamples - 1,
            (int)(((double) masterLen - impl->currentTick) / ticksPerSample));
        impl->flushAllActiveNotes (outMidi, loopSample);
        impl->currentTick = std::fmod (blockEndTick, (double) masterLen);
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
    s.writeInt   (kStreamVersion2);
    s.writeFloat (impl->internalBpm.load (std::memory_order_relaxed));
    s.writeBool  (impl->looping    .load (std::memory_order_relaxed));
    s.writeBool  (impl->syncToHost .load (std::memory_order_relaxed));

    const juce::ScopedReadLock sl (impl->tracksLock);
    s.writeInt (impl->tracks.size());
    for (const auto* t : impl->tracks)
        const_cast<SequencerTrack*>(t)->writeToStream (s);
}

bool SequencerEngine::readFromStream (juce::MemoryInputStream& s)
{
    // Peek at first int — kStreamVersion2 means multi-clip format,
    // otherwise treat the bytes as a legacy float BPM (version 1).
    const auto startPos = s.getPosition();
    const int firstInt  = s.readInt();

    float bpm;
    bool  loop, sync;
    int   n;
    const bool isV2 = (firstInt == kStreamVersion2);

    if (isV2)
    {
        bpm  = s.readFloat();
        loop = s.readBool();
        sync = s.readBool();
        n    = s.readInt();
    }
    else
    {
        // Legacy: firstInt was the raw bytes of a float BPM.
        s.setPosition (startPos);
        bpm  = s.readFloat();
        loop = s.readBool();
        sync = s.readBool();
        s.readInt64();   // legacy clipLengthTicks — discard
        n    = s.readInt();
    }

    if (bpm < 20.f || bpm > 999.f || n < 0 || n > 256) return false;

    juce::OwnedArray<SequencerTrack> loaded;
    for (int i = 0; i < n; ++i)
    {
        auto t  = std::make_unique<SequencerTrack>();
        bool ok = isV2 ? t->readFromStream (s) : t->readFromStreamV1 (s);
        if (! ok) return false;
        loaded.add (t.release());
    }

    impl->internalBpm.store (bpm,  std::memory_order_relaxed);
    impl->looping    .store (loop, std::memory_order_relaxed);
    impl->syncToHost .store (sync, std::memory_order_relaxed);

    {
        const juce::ScopedWriteLock sl (impl->tracksLock);
        impl->tracks.clear();
        impl->tracks.swapWith (loaded);
    }

    return true;
}
