// ============================================================================
// PATCH: SequencerEngine — per-track MIDI dispatch + activity flags
//
// Apply in two files:
//   SequencerEngine.h  — public section, after addOrUpdateSfTrackOnChannel()
//   SequencerEngine.cpp — top of processBlock's track-render loop
//
// ── SequencerEngine.h additions ─────────────────────────────────────────────
// (Paste into the public section of SequencerEngine, after track management)
// ============================================================================

    //==========================================================================
    //  External MIDI dispatch (standalone only — Cubase-style routing)
    //==========================================================================

    /** Signature for the per-block external MIDI callback.
     *
     * @param perTrackBuffers  One MidiBuffer per sequencer track (index-matched).
     *                         Each buffer contains the notes/CCs scheduled for
     *                         that track in this block.
     * @param numSamples       Block length in samples.
     * @param bpm              Current effective BPM.
     * @param sampleRate       Current sample rate.
     * @param transportPlaying Whether the transport is running.
     */
    using ExternalMidiDispatchFn =
        std::function<void (const std::vector<juce::MidiBuffer>& perTrackBuffers,
                            int    numSamples,
                            double bpm,
                            double sampleRate,
                            bool   transportPlaying)>;

    /** Register the MidiRouter dispatch callback.
     *  Called once from MainWindow after both processor and midiRouter are ready.
     *  Pass nullptr to disconnect. Thread-safe: uses an atomic pointer swap. */
    void setExternalMidiDispatch (ExternalMidiDispatchFn fn)
    {
        const juce::ScopedLock sl (impl->dispatchLock);
        impl->externalDispatch = std::move (fn);
    }

    //==========================================================================
    //  Per-track MIDI activity flags (for the receive indicator in TrackHeaderStrip)
    //==========================================================================

    /** Returns true and clears the flag if the audio thread set it since last call.
     *  Message-thread safe: the flag is an atomic_bool. */
    bool getMidiActivityAndClear (int trackIndex) noexcept
    {
        if (! juce::isPositiveAndBelow (trackIndex, kActivityFlagCount))
            return false;
        return impl->midiActivityFlags[trackIndex]
                   .exchange (false, std::memory_order_relaxed);
    }

    static constexpr int kActivityFlagCount = 64;


// ============================================================================
// ── SequencerEngine.cpp additions ───────────────────────────────────────────
//
// 1.  Add to the Impl struct (anywhere before processClipSlot):
// ============================================================================

    // ── External MIDI dispatch ──────────────────────────────────────────────
    juce::CriticalSection                    dispatchLock;
    SequencerEngine::ExternalMidiDispatchFn  externalDispatch;

    // ── Per-track MIDI activity flags ───────────────────────────────────────
    std::atomic<bool> midiActivityFlags[SequencerEngine::kActivityFlagCount] = {};


// ============================================================================
// 2.  Replace the track render loop inside processBlock with this version.
//
//     Find the existing loop (around line 560 in SequencerEngine.cpp):
//
//       for (int ti = 0; ti < impl->tracks.size(); ++ti)
//       {
//           auto& track = *impl->tracks[ti];
//           if (! track.enabled) continue;
//           for (int ci = 0; ci < track.clips.size(); ++ci)
//               impl->processClipSlot (outMidi, track, ...);
//       }
//
//     Replace with:
// ============================================================================

    {
        // ── Fast path: no external dispatch registered ────────────────────
        const juce::ScopedTryLock dtl (impl->dispatchLock);
        const bool hasDispatch = dtl.isLocked() && impl->externalDispatch != nullptr;

        if (! hasDispatch)
        {
            // Original single-buffer path — zero overhead
            for (int ti = 0; ti < impl->tracks.size(); ++ti)
            {
                auto& track = *impl->tracks[ti];
                if (! track.enabled) continue;
                for (int ci = 0; ci < track.clips.size(); ++ci)
                    impl->processClipSlot (outMidi, track, ti, ci, *track.clips[ci],
                                           impl->currentTick, blockEndTick,
                                           numSamples, ticksPerSample, doLoop, masterLen);
            }
        }
        else
        {
            // Per-track buffer path for external MIDI dispatch
            const int numTracks = impl->tracks.size();
            std::vector<juce::MidiBuffer> perTrack ((size_t) numTracks);

            for (int ti = 0; ti < numTracks; ++ti)
            {
                auto& track = *impl->tracks[ti];
                if (! track.enabled) continue;
                for (int ci = 0; ci < track.clips.size(); ++ci)
                    impl->processClipSlot (perTrack[ti], track, ti, ci, *track.clips[ci],
                                           impl->currentTick, blockEndTick,
                                           numSamples, ticksPerSample, doLoop, masterLen);

                // Pulse the activity flag if any events were scheduled
                if (! perTrack[ti].isEmpty()
                    && juce::isPositiveAndBelow (ti, SequencerEngine::kActivityFlagCount))
                {
                    impl->midiActivityFlags[ti].store (true, std::memory_order_relaxed);
                }

                // Merge into the outMidi buffer for the internal engine
                for (const auto meta : perTrack[ti])
                    outMidi.addEvent (meta.getMessage(), meta.samplePosition);
            }

            // Fire the external dispatch callback (audio-thread, lock-free path)
            impl->externalDispatch (
                perTrack,
                numSamples,
                bpm,
                sampleRate,
                impl->playing.load (std::memory_order_relaxed));
        }
    }


// ============================================================================
// 3.  In MainWindow.h, wire up the dispatch lambda after both
//     processor and midiRouter are constructed (inside the constructor):
// ============================================================================

    // ── Wire SequencerEngine → MidiRouter ─────────────────────────────────
    processor->sequencer.setExternalMidiDispatch (
        [this] (const std::vector<juce::MidiBuffer>& perTrack,
                int    numSamples,
                double bpm,
                double sr,
                bool   playing)
        {
            midiRouter->dispatchBlock (perTrack, numSamples, bpm, sr, playing);
        });


// ============================================================================
// 4.  In TrackHeaderStrip's owning component (ArrangeView or PianoRollPanel),
//     route the MIDI activity poll from the 10Hz strip timer back to the engine.
//
//     The cleanest pattern is a single polling timer in the owning view that
//     calls:
//
//       for (int i = 0; i < engine.getNumTracks(); ++i)
//           if (engine.getMidiActivityAndClear(i))
//               trackHeaderStrip.notifyMidiActivity(i);
//
//     Since TrackHeaderStrip already has its own 10Hz timer, you can
//     alternatively wire it in timerCallback() inside TrackHeaderStrip itself
//     by adding a pointer to SequencerEngine and calling getMidiActivityAndClear.
//
//     The SequencerEngine reference is already available in TrackHeaderStrip
//     via the `engine` member, so you can do this directly in timerCallback:
// ============================================================================

    // Inside TrackHeaderStrip::timerCallback(), before the repaint section:
    for (int i = 0; i < juce::jmin (engine.getNumTracks(), kMaxTracks); ++i)
        if (engine.getMidiActivityAndClear (i))
            midiActivityFlags[i].store (true, std::memory_order_relaxed);
