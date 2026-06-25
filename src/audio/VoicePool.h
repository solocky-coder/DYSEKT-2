#pragma once
#include "Voice.h"
#include "SliceManager.h"
#include "SampleData.h"
#include "signalsmith-stretch.h"
#include <array>
#include <atomic>

// All global parameter values needed to start a voice, pre-loaded from APVTS on the UI thread.
// Units match slice storage: seconds for ADSR, 0-1 for sustain, dB for volume.
struct VoiceStartParams
{
    int sliceIdx = 0;
    float velocity = 0.0f; // raw MIDI 0-127
    int note = 0;
    float globalBpm = 120.0f;
    float globalPitch = 0.0f;
    float globalAttackSec = 0.005f;
    float globalHoldSec    = 0.0f;
    float globalDecaySec = 0.1f;
    float globalSustain = 1.0f; // 0-1
    float globalReleaseSec = 0.02f;
    int globalMuteGroup = 1;
    bool globalStretch = false;
    float dawBpm = 120.0f;
    float globalTonality = 0.0f;
    float globalFormant = 0.0f;
    bool globalFormantComp = false;
    float globalVolume = 0.0f; // dB
    bool globalReleaseTail = false;
    bool globalReverse = false;
    int globalLoopMode = 0;
    bool globalOneShot = false;
    float globalCentsDetune = 0.0f;

    // ── v17 additions ─────────────────────────────────────────────────────────
    float globalPan = 0.0f; // -1..+1
    float globalFilterCutoff = 20000.0f; // Hz
    float globalFilterRes = 0.0f; // 0..1

    // ── v24 additions: per-slice EQ ──────────────────────────────────────────
    float globalEqLowGain  = 0.0f;    // dB
    float globalEqMidGain  = 0.0f;    // dB
    float globalEqMidFreq  = 1000.0f; // Hz
    float globalEqMidQ     = 1.0f;    // Q
    float globalEqHighGain = 0.0f;    // dB

    // ── Chromatic legato ──────────────────────────────────────────────────────
    bool chromaticLegatoTrigger = false; // when true: steal prior legato voices, force Stretch pitch-only
};

class VoicePool
{
public:
    static constexpr int kMaxVoices = 256;
    static constexpr int kPreviewVoiceIndex = kMaxVoices - 1;

    VoicePool();

    int allocate();
    void startVoice (int voiceIdx, const VoiceStartParams& params,
                     SliceManager& sliceMgr, const SampleData& sample);

    // Unsliced / trim-mode chromatic playback — no SliceManager needed.
    // Uses global params only; startSample/endSample are explicit bounds.
    void startVoiceUnsliced (int voiceIdx, const VoiceStartParams& params,
                             int startSample, int endSample,
                             const SampleData& sample);

    static constexpr float kShortReleaseSec = 0.05f; // All Notes Off (CC 123): 50ms fade
    static constexpr float kKillReleaseSec  = 0.005f; // All Sound Off (CC 120): 5ms hard kill

    void releaseNote       (int note);
    void releaseNoteForced (int note); // host-sweep note-off: forceRelease even on oneShot voices
    void releaseAll();                 // CC 123 — 50ms fade on all active voices
    void killAll();                    // CC 120 — 5ms hard kill on all active voices
    void muteGroup (int group, int exceptVoice);

    /** @param chainSource  Optional. When non-null, a one-shot voice that
     *  naturally reaches its end and whose slice has a valid nextSliceIdx
     *  will immediately retrigger that slice (same note/velocity) instead
     *  of simply deactivating -- used by the SFZ-PLAYER engine to chain a
     *  one-shot attack-head slice into a looped sustain-tail slice. The
     *  Slicer never passes this (defaults to nullptr; behaviour unchanged). */
    void processSample (const SampleData& sample, double sampleRate,
                        float& outL, float& outR,
                        SliceManager* chainSource = nullptr);

    void setSampleRate (double sr) { sampleRate = sr; }
    double getSampleRate() const   { return sampleRate; }

    void setMaxActiveVoices (int n);
    int  getMaxActiveVoices() const { return maxActive; }

    Voice&       getVoice (int idx)       { return voices[(size_t) idx]; }
    const Voice& getVoice (int idx) const { return voices[(size_t) idx]; }

    void startShiftPreview (int startSample, int bufferSize, double sr, const SampleData& sd);
    void stopShiftPreview();

    // Public helpers so LazyChopEngine can initialise stretch on preview voice
    static void initStretcher (Voice& v, float pitchSemis, double sr,
                                float tonalityHz, float formantSemis,
                                const SampleData& sample);

    void killVoicesForChromaticLegato (int sliceIdx);

    /// True sample-through chromatic legato retune.
    ///
    /// Finds the active chromatic-legato voice playing sliceIdx and updates its
    /// pitch WITHOUT resetting the playback position (stretchSrcPos / position).
    ///   - Stretch / pitch-only mode  → calls stretcher->setTransposeSemitones() live.
    ///   - Repitch mode               → recalculates v.speed from the new ratio.
    ///
    /// Returns true  if a voice was found and retuned.
    ///               Caller must NOT allocate or start a new voice.
    /// Returns false if no active legato voice exists for this slice.
    ///               Caller should fall through to kill + startVoice as normal.
    bool retuneChromaticLegatoVoice (int sliceIdx, float newPitchSemis,
                                     float tonalityHz, int newMidiNote);

    // Atomic voice positions for UI cursor display
    std::array<std::atomic<float>, kMaxVoices> voicePositions;

    // ── Legato pitch glide (thread-safe: written MIDI thread, read audio thread) ──
    // Stores the target and active-flag for smooth pitch transitions.
    // Plain atomics here so Voice struct stays unchanged.
    std::array<std::atomic<float>, kMaxVoices> legatoTargetSpeed;   // repitch mode target ratio
    std::array<std::atomic<float>, kMaxVoices> legatoTargetSemis;   // stretch mode target semitones
    std::array<std::atomic<bool>,  kMaxVoices> legatoPitchGliding;  // true while ramping

    // ── Global legato glide time (ms) — written from UI thread, read from audio thread ──
    std::atomic<float> legatoGlideMs { 15.0f };

    void processVoiceSample (int i, const SampleData& sample, double sampleRate,
                             float& outL, float& outR,
                             SliceManager* chainSource = nullptr);

private:
    void retriggerChainedSlice (int voiceIdx, SliceManager* chainSource,
                                int targetSliceIdx, int note, float velocity,
                                const SampleData& sample);

    std::array<Voice, kMaxVoices> voices;
    int maxActive = 16; // playable voices, excluding preview voice
    double sampleRate = 44100.0;
};
