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
    void loadFile (const juce::File& f);

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

    float      getVolume()      const noexcept { return volume.load(); }
    int        getTranspose()   const noexcept { return transpose.load(); }
    float      getPitchShift()  const noexcept { return pitchShift.load (std::memory_order_relaxed); }
    int        getMidiChannel() const noexcept { return midiChannel.load(); }
    float      getPan()         const noexcept { return pan.load(); }
    float      getFineTune()    const noexcept { return fineTune.load(); }
    float      getReverb()      const noexcept { return reverb.load(); }
    float      getChorus()      const noexcept { return chorus.load(); }
    int        getCurrentPresetIndex() const noexcept { return presetIndex.load(); }
    juce::File getLoadedFile()  const;
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

    // ── Called from audio thread ──────────────────────────────────────────────

    void prepare (double sampleRate, int maxBlockSize);

    /**
     * Process one block. MIDI events from @p midiIn whose channel matches
     * midiChannel (0 = all) are forwarded to FluidSynth.  Rendered stereo
     * audio is mixed additively into @p outL / @p outR.
     */
    void process (const juce::MidiBuffer& midiIn,
                  float* outL, float* outR, int numSamples);

private:
    // ── Pending load (UI → audio thread handoff) ──────────────────────────────
    struct PendingLoad
    {
        juce::File file;
        bool       shouldUnload { false };
    };
    std::atomic<PendingLoad*> pendingLoad { nullptr };

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
    std::atomic<int>   midiChannel { 16 };   // 0 = omni, default 16 = DYFONT dedicated channel
    std::atomic<float> pan         { 0.0f }; // -1..+1
    std::atomic<float> fineTune    { 0.0f }; // cents -100..+100
    std::atomic<float> reverb      { 0.4f }; // 0..1
    std::atomic<float> chorus      { 0.2f }; // 0..1
    std::atomic<int>   presetIndex { 0 };    // index into cachedPresets
    std::atomic<bool>  loaded      { false };

    // ── SFZ ADSR atomics (written from any thread, read on audio thread) ──────
    std::atomic<float> sfzAttackSec   { 0.005f };  ///< seconds (SFZ default ~0)
    std::atomic<float> sfzDecaySec    { 0.1f   };  ///< seconds
    std::atomic<float> sfzSustainPct  { 100.0f };  ///< percent 0-100
    std::atomic<float> sfzReleaseSec  { 0.05f  };  ///< seconds (SFZ default ~0)
    std::atomic<bool>  sfzAdsrDirty   { false  };  ///< set by setters, cleared in processBlock

    /** Set when presetIndex changes; audio thread picks it up in process(). */
    std::atomic<bool>  programChangePending { false };

    // ── Multi-timbral channel assignments (SF2 only) ──────────────────────────
    // pendingChannelAssignment[ch] holds a packed (bank << 16) | preset value,
    // or -1 if no change is pending on that channel.
    // Written from the UI thread via setPresetOnChannel(); read+cleared on audio thread.
    std::atomic<int>  pendingChannelAssignment[16];  // initialised to -1 in ctor
    std::atomic<bool> anyChannelDirty { false };

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
