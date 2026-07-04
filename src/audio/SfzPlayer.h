#pragma once
// =============================================================================
//  SfzPlayer.h  —  Real-time SF2/SFZ playback engine
//                  SF2 → FluidSynth backend
//                  SFZ → sfizz backend
// =============================================================================
//  Owned by DysektProcessor.  prepareToPlay / processBlock / loadFile are
//  called from the audio thread (processBlock) or UI thread (load/param set).
//
//  Thread safety:
//    loadFile()           — UI thread; PendingLoad posted via atomic
//    setVolume/Trans()    — UI thread; stored as std::atomic<float>
//    setPresetByIndex()   — UI thread; sets atomics + programChangePending flag
//    prepare()            — audio thread (prepareToPlay)
//    process()            — audio thread (processBlock); applies pending loads
//                           and program changes at the top of each block
//
//  Preset list handoff (audio → UI):
//    After a successful sfont load the audio thread allocates a new
//    std::vector<Sf2PresetInfo>* and stores it in freshPresets.
//    getPresetList() (UI thread) swaps it out and caches it.
// =============================================================================

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>

#if DYSEKT_HAS_FLUIDSYNTH
  #include <fluidsynth.h>
#endif

#if DYSEKT_HAS_SFIZZ
  #include "../../sfizz/src/sfizz.h"
#endif

// -----------------------------------------------------------------------------
//  Preset descriptor — used by the UI to populate the preset picker
// -----------------------------------------------------------------------------
struct Sf2PresetInfo
{
    int          bank   { 0 };
    int          preset { 0 };
    juce::String name;
};

// =============================================================================
class SfzPlayer
{
public:
    SfzPlayer();
    ~SfzPlayer();

    // ── Called from UI thread ─────────────────────────────────────────────────

    /** Queue a new SF2 file for loading. Returns immediately. */
    void loadFile (const juce::File& f, juce::ThreadPool& pool);

    /** Unload current instrument (silent output). */
    void unload();

    void setVolume      (float gainLinear);   ///< 0..2
    void setTranspose   (int semitones);      ///< SF2 only — MIDI note shift; SFZ uses setPitchShift
    void setPitchShift  (float semitones);    ///< SFZ audio-rate pitch shift, -24..+24 semitones
    void setMidiChannel (int ch);             ///< 0 = omni, 1-16 = specific
    void setPan         (float centred);      ///< -1.0 (L) .. 0.0 (C) .. +1.0 (R)
    void setFineTune    (float cents);        ///< -100 .. +100 cents
    void setReverb      (float level);        ///< 0..1 wet level
    void setChorus      (float level);        ///< 0..1 wet level

    /** Select a preset by its index in the list returned by getPresetList().
     *  Single-preset mode: assigns the chosen preset to FluidSynth channel 0.
     *  For multi-timbral use, prefer setPresetOnChannel() instead. */
    void setPresetByIndex (int idx);

    /** Multi-timbral SF2: assign a specific bank/preset to a FluidSynth channel (0-15).
     *  Each sequencer track calls this with its own channel so FluidSynth plays
     *  multiple programs simultaneously.  No-op for SFZ files. */
    void setPresetOnChannel (int channel, int bank, int preset);

    /** Clear all pending channel-preset assignments (e.g. on SF2 unload). */
    void clearChannelPresets();

    /** Preview mode: load bank/preset onto the dedicated preview channel (15)
     *  and route live controller input to it.  Call from the UI thread.
     *  A second call with the same bank/preset clears the preview (toggle). */
    void previewPreset (int bank, int preset);

    /** Stop any active preview: silence channel 15 and clear the live mask bit. */
    void clearPreview();

    /** Set which FluidSynth channels (bitmask, bit 0 = ch 0 … bit 15 = ch 15)
     *  should receive live controller input that arrives on MIDI channel 1.
     *  Call from the UI/message thread whenever the user selects or deselects
     *  SF2 tracks.  0 = no fan-out (controller input is silenced for SF2). */
    void setLiveInputChannelMask (uint16_t mask) noexcept
    {
        liveInputChannelMask.store (mask, std::memory_order_relaxed);
    }

    uint16_t getLiveInputChannelMask() const noexcept
    {
        return liveInputChannelMask.load (std::memory_order_relaxed);
    }

    float      getVolume()      const noexcept { return volume.load(); }
    int        getTranspose()   const noexcept { return transpose.load(); }
    float      getPitchShift()  const noexcept { return pitchShift.load (std::memory_order_relaxed); }
    int        getMidiChannel() const noexcept { return midiChannel.load(); }
    float      getPan()         const noexcept { return pan.load(); }
    float      getFineTune()    const noexcept { return fineTune.load(); }
    float      getReverb()      const noexcept { return reverb.load(); }
    float      getChorus()      const noexcept { return chorus.load(); }
    int        getCurrentPresetIndex() const noexcept { return presetIndex.load(); }

    /** UI-display-only "currently selected" preset in the multitimbral program
     *  grid — the last preset the user previewed or selected for per-channel FX
     *  editing. Purely cosmetic: unlike setPresetByIndex()/presetIndex, setting
     *  this never sends a program change on any MIDI channel. */
    void       setDisplayPresetIndex (int idx) noexcept { displayPresetIndex.store (idx); }
    int        getDisplayPresetIndex() const noexcept { return displayPresetIndex.load(); }

    juce::File getLoadedFile()  const;

    /** Returns the file most recently passed to loadFile(), even if the async
     *  load hasn't finished yet.  Safe to call on the UI thread.
     *  Returns an empty File if nothing has ever been queued. */
    juce::File getPendingFilePath() const { return juce::File (pendingFilePath); }
    bool       isLoaded()       const noexcept { return loaded.load(); }

    // ── SFZ ADSR (applied via sfizz OSC messages per region) ──────────────────
    //  Values are stored as atomics and flushed to sfizz at the start of each
    //  processBlock() call when dirty.  Call from any thread; sfizz update is RT.
    void  setSfzAttack  (float sec)  noexcept;   ///< 0-30 s
    void  setSfzDecay   (float sec)  noexcept;   ///< 0-30 s
    void  setSfzSustain (float pct)  noexcept;   ///< 0-100 %
    void  setSfzRelease (float sec)  noexcept;   ///< 0-60 s

    /** Set per-region volume and pan for SFZ files (sfizz OSC, real-time safe).
     *  regionIndex is the 0-based zone/region index from the parsed Keyzone list.
     *  No-op for SF2 files. */
    void setZoneVolume (int regionIndex, float volDb)    noexcept;
    void setZonePan    (int regionIndex, float pan)      noexcept;  ///< pan: -1..+1
    void setZoneTune   (int regionIndex, float cents)    noexcept;  ///< cents: -100..+100

    float getSfzAttack()  const noexcept { return sfzAttackSec .load (std::memory_order_relaxed); }
    float getSfzDecay()   const noexcept { return sfzDecaySec  .load (std::memory_order_relaxed); }
    float getSfzSustain() const noexcept { return sfzSustainPct.load (std::memory_order_relaxed); }
    float getSfzRelease() const noexcept { return sfzReleaseSec.load (std::memory_order_relaxed); }

    /** Loop point in samples within the sfizz-rendered preview buffer.
     *  Returns -1 if no loop is defined. */
    int  getLoopStartSample() const noexcept { return sfzLoopStartSample.load (std::memory_order_relaxed); }
    int  getLoopEndSample()   const noexcept { return sfzLoopEndSample  .load (std::memory_order_relaxed); }

    /** Called by SoundFontLoader after rendering to store loop metadata. */
    void setLoopPoints (int loopStart, int loopEnd) noexcept
    {
        sfzLoopStartSample.store (loopStart, std::memory_order_relaxed);
        sfzLoopEndSample  .store (loopEnd,   std::memory_order_relaxed);
    }

    // ── Post-processing Reverb EFX (JUCE DSP — works for both SF2 & SFZ) ──
    void setReverbSize   (float pct) noexcept;   ///< 0–100 %
    void setReverbDamp   (float pct) noexcept;   ///< 0–100 %
    void setReverbWidth  (float pct) noexcept;   ///< 0–100 %
    void setReverbMix    (float pct) noexcept;   ///< 0–100 %
    void setReverbFreeze (bool  on)  noexcept;   ///< infinite sustain

    float getReverbSize()   const noexcept { return reverbSize  .load (std::memory_order_relaxed); }
    float getReverbDamp()   const noexcept { return reverbDamp  .load (std::memory_order_relaxed); }
    float getReverbWidth()  const noexcept { return reverbWidth .load (std::memory_order_relaxed); }
    float getReverbMix()    const noexcept { return reverbMix   .load (std::memory_order_relaxed); }
    bool  getReverbFreeze() const noexcept { return reverbFreeze.load (std::memory_order_relaxed); }

    /**
     * Returns the cached preset list for the currently loaded SF2.
     * If the audio thread has posted new data since the last call,
     * the cache is updated first (wait-free on both sides).
     * Safe to call from any thread except the audio thread.
     */
    std::vector<Sf2PresetInfo> getPresetList() const;

    // ── Per-channel mixer strip (SF2 multi-timbral) ───────────────────────────

    /** Snapshot of one channel's mixer state — safe to copy on the UI thread. */
    struct ChannelStrip
    {
        float volume     { 1.0f };   ///< normalised 0..1  (maps to CC7 0..127)
        float pan        { 0.0f };   ///< -1..+1           (maps to CC10 0..127)
        float reverbSend { 0.0f };   ///< normalised 0..1  (maps to CC91 0..127)
        float preMuteVol { 1.0f };   ///< volume saved before mute
        bool  muted      { false };
    };

    /** Read a channel's current strip state.  Safe to call on any thread. */
    ChannelStrip getChannelStrip (int channel) const noexcept;

    void setChannelVolume     (int channel, float normVol)   noexcept; ///< 0..1
    void setChannelPan        (int channel, float pan)       noexcept; ///< -1..+1
    void setChannelReverbSend (int channel, float normSend)  noexcept; ///< 0..1
    void setChannelMuted      (int channel, bool muted)      noexcept;
    void soloChannel          (int channel)                  noexcept;
    void clearSolo            ()                             noexcept;

    // ── Called from audio thread ──────────────────────────────────────────────

    void prepare (double sampleRate, int maxBlockSize);

    /**
     * Process one block. MIDI events from @p midiIn whose channel matches
     * midiChannel (0 = all) are forwarded to FluidSynth.  Rendered stereo
     * audio is mixed additively into @p outL / @p outR.
     */
    void process (const juce::MidiBuffer& midiIn,
                  float* outL, float* outR, int numSamples);

    // ── JUCE ADSR (Option B — applied post-FluidSynth/sfizz in processBlock) ──
    //  The envelope is owned here so it lives on the audio thread.
    //  UI thread sets parameters via setJuceAdsr(); noteOn/Off are signalled via
    //  atomics so the audio thread fires the envelope at the right moment.
    //  On SF2 load, suppressFluidAdsr() is called once to zero out FluidSynth's
    //  internal envelope generators so JUCE ADSR has exclusive control.

    /** Update ADSR parameters.  Safe to call from any thread. */
    void setJuceAdsr (float attackSec, float decaySec,
                      float sustainLvl, float releaseSec) noexcept;

    /** Signal a note-on to the JUCE envelope (called from UI when key is pressed). */
    void juceAdsrNoteOn (int noteNumber = -1) noexcept
    {
        juceAdsrNoteOnPending.store (true, std::memory_order_relaxed);
        if (noteNumber >= 0)
            pendingTriggeredNote.store (noteNumber, std::memory_order_relaxed);
    }

    /** Signal a note-off to the JUCE envelope. */
    void juceAdsrNoteOff() noexcept { juceAdsrNoteOffPending.store (true, std::memory_order_relaxed); }

    /** True when the JUCE ADSR is active (envelope not idle). */
    bool juceAdsrIsActive() const noexcept { return juceAdsrActive.load (std::memory_order_relaxed); }

    /** Sample position within the rendered preview buffer (processor.sampleData2)
     *  of the most recently triggered note, advancing each block while a note
     *  is active.  Mirrors VoicePool::voicePositions semantics: 0 = not
     *  playing/idle.  Resets to 0 on note-on, freezes when the envelope goes
     *  idle (after release tail completes) so the UI playhead disappears the
     *  same way the Slicer's does.  Safe to read from the UI/message thread. */
    int getPreviewPositionSample() const noexcept
    {
        return previewPositionSample.load (std::memory_order_relaxed);
    }

    /** MIDI note number of the most recently triggered note-on (sfizz or
     *  FluidSynth branch, MIDI or UI-keyboard injection — all sites that
     *  reset previewPositionSample also record this). -1 if no note has
     *  fired yet. Combined with previewPositionSample by the UI layer
     *  (which knows the note->region mapping via previewZones3) to find
     *  the absolute buffer position of the playhead, since SfzPlayer
     *  itself has no knowledge of region/zone boundaries. */
    int getLastTriggeredNote() const noexcept
    {
        return lastTriggeredNote.load (std::memory_order_relaxed);
    }

    // ── Per-channel peak meters (public — read by MixerPanel timer) ──────────
    // Written on audio thread after each process() block; read on UI thread.
    std::atomic<float> channelPeakL[16] {};
    std::atomic<float> channelPeakR[16] {};

private:
    // ── Pending load (UI → audio thread handoff) ──────────────────────────────
    struct PendingLoad
    {
        juce::File file;
        bool       shouldUnload { false };
    };
    std::atomic<PendingLoad*> pendingLoad { nullptr };

    // Stores the path of the most recently queued file (set by loadFile() on
    // the UI thread; safe to read via getPendingFilePath() at any time).
    juce::String pendingFilePath;

    // ── Pending preset list (audio → UI handoff) ──────────────────────────────
    mutable std::atomic<std::vector<Sf2PresetInfo>*> freshPresets { nullptr };
    mutable std::vector<Sf2PresetInfo>               cachedPresets;

    // ── Audio-thread FluidSynth state (SF2) ───────────────────────────────────
#if DYSEKT_HAS_FLUIDSYNTH
    fluid_settings_t* settings { nullptr };
    fluid_synth_t*    synth    { nullptr };
    int               sfontId  { -1 };
#endif

    // ── Audio-thread sfizz state (SFZ) ────────────────────────────────────────
#if DYSEKT_HAS_SFIZZ
    sfizz_synth_t*    sfizzSynth { nullptr };
#endif

    bool isSfzFile { false };   ///< true when the loaded file is .sfz
    double   currentSR    { 44100.0 };
    int      currentBlock { 256 };
    juce::File activeFile;

    // ── Shared params (atomic, UI-writable) ───────────────────────────────────
    std::atomic<float> volume      { 1.0f };
    std::atomic<int>   transpose   { 0 };
    std::atomic<float> pitchShift  { 0.0f };  ///< SFZ audio-rate pitch, -24..+24 semitones
    std::atomic<int>   midiChannel { 16 };   // 0 = omni, default 16 = DY-SFP dedicated channel
    std::atomic<float> pan         { 0.0f }; // -1..+1
    std::atomic<float> fineTune    { 0.0f }; // cents -100..+100
    std::atomic<float> reverb      { 0.4f }; // 0..1
    std::atomic<float> chorus      { 0.2f }; // 0..1
    std::atomic<int>   presetIndex   { 0 };  // index into cachedPresets (UI display)
    std::atomic<int>   displayPresetIndex { -1 }; // grid's "last selected" preset — UI-only, no engine effect
    std::atomic<int>   pendingBank   { 0 };  // bank number for applyProgramChange
    std::atomic<int>   pendingProgram{ 0 };  // program number for applyProgramChange
    std::atomic<bool>  loaded      { false };

    // ── SFZ ADSR atomics (written from any thread, read on audio thread) ──────
    std::atomic<float> sfzAttackSec   { 0.005f };  ///< seconds (SFZ default ~0)
    std::atomic<float> sfzDecaySec    { 0.1f   };  ///< seconds
    std::atomic<float> sfzSustainPct  { 100.0f };  ///< percent 0-100
    std::atomic<float> sfzReleaseSec  { 0.05f  };  ///< seconds (SFZ default ~0)
    std::atomic<bool>  sfzAdsrDirty   { false  };

    // ── SFZ loop points (written by SoundFontLoader, read by Sf2WaveformLcd) ───
    std::atomic<int>   sfzLoopStartSample { -1 };   ///< -1 = no loop
    std::atomic<int>   sfzLoopEndSample   { -1 };  ///< set by setters, cleared in processBlock

    /** Set when presetIndex changes; audio thread picks it up in process(). */
    std::atomic<bool>  programChangePending { false };

    // ── Multi-timbral channel assignments (SF2 only) ──────────────────────────
    // pendingChannelAssignment[ch] holds a packed (bank << 16) | preset value,
    // or -1 if no change is pending on that channel.
    // Written from the UI thread via setPresetOnChannel(); read+cleared on audio thread.
    std::atomic<int>  pendingChannelAssignment[16];  // initialised to -1 in ctor
    std::atomic<bool> anyChannelDirty { false };

    // ── Live controller fan-out (SF2 multi-timbral) ───────────────────────────
    // Bitmask of FluidSynth channels (bit 0 = ch 0 … bit 15 = ch 15) that should
    // receive fan-out of incoming MIDI ch-1 controller input.
    // Written from UI thread via setLiveInputChannelMask(); read on audio thread.
    std::atomic<uint16_t> liveInputChannelMask { 0 };

    // ── Scratch buffer for FluidSynth interleaved → planar conversion ─────────
    std::vector<float> scratchL, scratchR;

    // ── Pitch shift render buffer (SFZ only) ──────────────────────────────────
    // sfizz renders into pitchL/R at an oversampled or undersampled block size,
    // then a linear interpolating resampler writes the pitch-shifted result into
    // scratchL/R at the true block size.
    std::vector<float> pitchBufL, pitchBufR;

    /** Apply a semitone pitch shift to src (srcLen samples) into dst (dstLen
     *  samples) using linear interpolation.  srcLen/dstLen == ratio == 2^(semi/12). */
    static void pitchShiftBlock (const float* src, float* dst,
                                  int srcLen, int dstLen) noexcept;

    // ── Post-processing Reverb EFX (juce::dsp::Reverb) ───────────────────────
    juce::dsp::Reverb dspReverb;

    std::atomic<float> reverbSize   { 50.0f };   // 0–100
    std::atomic<float> reverbDamp   { 50.0f };   // 0–100
    std::atomic<float> reverbWidth  { 50.0f };   // 0–100
    std::atomic<float> reverbMix    {  0.0f };   // 0–100 (default dry)
    std::atomic<bool>  reverbFreeze { false };

    void updateReverbParams();  ///< maps atomics → juce::dsp::Reverb::Parameters

    // ── Per-channel mixer strip state ─────────────────────────────────────────
    // Written on UI thread via setChannel*(); read on audio thread in applyDirtyStrips().
    struct ChannelStripAtomics
    {
        std::atomic<float> volume     { 1.0f };
        std::atomic<float> pan        { 0.0f };
        std::atomic<float> reverbSend { 0.0f };
        std::atomic<float> preMuteVol { 1.0f };
        std::atomic<bool>  muted      { false };
        std::atomic<bool>  dirty      { false };
    };
    ChannelStripAtomics channelStrips[16];
    std::atomic<bool>   anyStripDirty { false };

    void applyDirtyStrips();   ///< called at top of FluidSynth process block

    void measureChannelPeaks (int numSamples);   ///< called at end of SF2 render block

    // ── JUCE ADSR private state (audio-thread owned) ─────────────────────────
    juce::ADSR                 juceAdsr;
    juce::ADSR::Parameters     juceAdsrParams { 0.005f, 0.1f, 1.0f, 0.05f };
    std::atomic<bool>          juceAdsrParamsDirty   { false };
    std::atomic<float>         juceAdsrAttack        { 0.005f };
    std::atomic<float>         juceAdsrDecay         { 0.1f   };
    std::atomic<float>         juceAdsrSustain        { 1.0f   };
    std::atomic<float>         juceAdsrRelease        { 0.05f  };
    std::atomic<bool>          juceAdsrNoteOnPending  { false  };
    std::atomic<bool>          juceAdsrNoteOffPending { false  };
    std::atomic<bool>          juceAdsrActive         { false  };

    // ── Preview playhead position (UI display only) ──────────────────────────
    // Tracks elapsed samples since the most recent note-on for the waveform
    // LCD's playhead cursor. 0 = idle/not playing. Reset to 0 on note-on,
    // advanced each block in process() while the envelope is active, frozen
    // once the envelope goes idle (matches VoicePool::voicePositions semantics
    // — see WaveformView::drawPlaybackCursors's `pos <= 0.0f` idle check).
    std::atomic<int>           previewPositionSample  { 0 };

    // Note number behind previewPositionSample's region lookup (see
    // getLastTriggeredNote doc comment in the header). -1 = none yet.
    std::atomic<int>           lastTriggeredNote      { -1 };

    // Staging slot for juceAdsrNoteOn(int): set by the caller (UI-injection
    // path), consumed into lastTriggeredNote when juceAdsrNoteOnPending is
    // processed in process(). -1 = no note number supplied for this trigger.
    std::atomic<int>           pendingTriggeredNote   { -1 };

    /** Called once after a successful SF2/SFZ load to zero FluidSynth's internal
     *  envelope generators on all channels so JUCE ADSR has exclusive control.
     *  No-op for SFZ (sfizz envelope is bypassed differently). */
    void suppressFluidAdsr();

    // ── Private helpers ───────────────────────────────────────────────────────
    void applyPendingLoad();             ///< called at top of process()
    void applyProgramChange();           ///< single-preset legacy (channel 0); called when programChangePending
    void applyPendingChannelChanges();   ///< multi-timbral; called when anyChannelDirty

    /** Send current ADSR atomics to sfizz via OSC messages (audio thread only). */
    void sendAdsrToSfizz();

    /** Build and post a fresh preset list after a successful sfont load.
     *  Called from the audio thread — no locks needed on write side. */
    void postPresetList();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SfzPlayer)
};
