#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include <deque>
#include <vector>

#include "audio/Slice.h"
#include "audio/SampleData.h"
#include "audio/SliceManager.h"
#include "audio/VoicePool.h"
#if DYSEKT_STANDALONE
#include "sequencer/SequencerEngine.h"
#include "sequencer/AbletonLink.h"
#endif
#include "audio/LazyChopEngine.h"
#include "audio/SoundFontLoader.h"
#include "audio/SfzPlayer.h"
#include "UndoManager.h"
#include "MidiLearnManager.h"
#include "CrashLogger.h"
#include "params/ParamIds.h"
#include "params/ParamLayout.h"

// Forward-declare to avoid pulling the full signalsmith header into every TU
// that includes PluginProcessor.h.  The stretcher lives only in the .cpp.
namespace signalsmith { namespace stretch {
    template<typename Sample, class RandomEngine> struct SignalsmithStretch;
}}

struct SfzSlicePayload;

//==============================================================================
// ── Spectrum analyser — written from audio thread, read by GlobalEqPanel ──────
// Lock-free double buffer.  Audio thread writes into whichever buffer the UI is
// NOT reading, then atomically flips readIndex.  A torn frame is just a slightly
// stale spectrum — no mutex needed.
class SpectrumAnalyser
{
public:
    static constexpr int fftOrder = 11;            // 2048-point FFT
    static constexpr int fftSize  = 1 << fftOrder; // 2048
    static constexpr int numBins  = fftSize / 2;   // 1024

    SpectrumAnalyser()
        : fft    (fftOrder),
          window (fftSize, juce::dsp::WindowingFunction<float>::hann)
    {
        buffers[0].fill (0.f);
        buffers[1].fill (0.f);
    }

    // Call from processBlock() AFTER the global EQ has processed.
    void pushSamples (const juce::AudioBuffer<float>& buf)
    {
        const int numCh      = buf.getNumChannels();
        const int numSamples = buf.getNumSamples();
        if (numCh == 0 || numSamples == 0) return;

        const float invCh = 1.f / (float) numCh;
        for (int s = 0; s < numSamples; ++s)
        {
            float mono = 0.f;
            for (int ch = 0; ch < numCh; ++ch)
                mono += buf.getReadPointer (ch)[s];
            pushSample (mono * invCh);
        }
    }

    // UI thread: returns the latest completed FFT frame.
    // Values are normalised 0..1  (1.0 = 0 dBFS, 0.0 = –80 dBFS).
    const std::array<float, numBins>& getReadBuffer() const noexcept
    {
        return buffers[readIndex.load (std::memory_order_acquire)];
    }

private:
    void pushSample (float sample)
    {
        fifo[fifoIndex++] = sample;
        if (fifoIndex == fftSize)
        {
            fifoIndex = 0;
            const int writeIdx = 1 - readIndex.load (std::memory_order_relaxed);
            std::copy (fifo.begin(), fifo.end(), fftData.begin());
            window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
            fft.performFrequencyOnlyForwardTransform (fftData.data());
            for (int i = 0; i < numBins; ++i)
            {
                const float mag = fftData[i] / (float) fftSize;
                const float dB  = juce::Decibels::gainToDecibels (mag, -80.f);
                buffers[writeIdx][i] = juce::jmap (dB, -80.f, 0.f, 0.f, 1.f);
            }
            readIndex.store (writeIdx, std::memory_order_release);
        }
    }

    juce::dsp::FFT                      fft;
    juce::dsp::WindowingFunction<float> window;
    std::array<float, fftSize>          fifo     {};
    std::array<float, fftSize>          fftData  {};
    int                                 fifoIndex { 0 };
    std::array<float, numBins>          buffers[2] {};
    std::atomic<int>                    readIndex  { 0 };

    JUCE_DECLARE_NON_COPYABLE (SpectrumAnalyser)
};

//==============================================================================
class DysektProcessor : public juce::AudioProcessor
{
public:
    // =========================================================================
    // Inner types
    // =========================================================================

    // Param field identifiers for CmdSetSliceParam.
    // Values are also used as MidiLearnManager slot indices — do not reorder.
    enum SliceParamField
    {
        FieldBpm = 0,
        FieldPitch,
        FieldAlgorithm,
        FieldAttack,
        FieldDecay,
        FieldSustain,
        FieldRelease,
        FieldMuteGroup,
        FieldStretchEnabled,
        FieldTonality,
        FieldFormant,
        FieldFormantComp,
        FieldGrainMode,
        FieldVolume,
        FieldReleaseTail,
        FieldReverse,
        FieldOutputBus,
        FieldLoop,
        FieldOneShot,
        FieldCentsDetune,
        FieldMidiNote,
        FieldSliceStart,   // 21 - normalised 0-1 -> startSample via MIDI CC
        FieldSliceEnd,     // 22 - normalised 0-1 -> endSample via MIDI CC
        FieldPan,          // 23 - per-slice pan -1..+1
        FieldFilterCutoff, // 24 - per-slice LP filter cutoff Hz
        FieldFilterRes,    // 25 - per-slice LP filter resonance 0..1
        FieldChromaticChannel, // 26 - per-slice chromatic MIDI channel (0=off, 1-16)
        FieldChromaticLegato,  // 27 - per-slice chromatic legato (bool)
        FieldTrimOut = 28,     // 28 - trim-out marker via MIDI CC
        FieldHold   = 29,      // 29 - per-slice AHDSR hold time (seconds)
        FieldZoom   = 30,      // 30 - waveform zoom level
        FieldScroll = 31,      // 31 - waveform scroll position
        FieldGlobalMono = 30,  // 30 - global Poly/Mono switch (bool)
        // SfzPlayer ADSR — slots 32-35
        FieldSfzAttack  = 32,  // 32 - sfizz ampeg_attack  (seconds, 0-30)
        FieldSfzDecay   = 33,  // 33 - sfizz ampeg_decay   (seconds, 0-30)
        FieldSfzSustain = 34,  // 34 - sfizz ampeg_sustain (percent, 0-100)
        FieldSfzRelease = 35,  // 35 - sfizz ampeg_release (seconds, 0-60)
        // SfzPlayer Reverb EFX — slots 36-40
        FieldSfzReverbSize   = 36,  // 36 - reverb room size  (0-100 %)
        FieldSfzReverbDamp   = 37,  // 37 - reverb damping    (0-100 %)
        FieldSfzReverbWidth  = 38,  // 38 - reverb width      (0-100 %)
        FieldSfzReverbMix    = 39,  // 39 - reverb wet/dry    (0-100 %)
        FieldSfzReverbFreeze = 40,  // 40 - reverb freeze     (bool)
        // SfzPlayer master knobs — slots 41-44
        FieldSfzVol       = 41,  // 41 - master volume   (0..2 linear, maps 0-100%)
        FieldSfzTranspose = 42,  // 42 - transpose       (-24..+24 semitones)
        FieldSfzPan       = 43,  // 43 - pan             (-1..+1)
        FieldSfzFineTune  = 44,  // 44 - fine tune       (-100..+100 cents)
        // v24: per-slice EQ
        FieldEqLowGain    = 45,  // dB  -18..+18
        FieldEqMidGain    = 46,  // dB  -18..+18
        FieldEqMidFreq    = 47,  // Hz  200..8000
        FieldEqMidQ       = 48,  // Q   0.5..4.0
        FieldEqHighGain   = 49,  // dB  -18..+18
    };

    // ── Command types ─────────────────────────────────────────────────────────
    enum CommandType
    {
        CmdNone = 0,
        CmdLoadFile,
        CmdCreateSlice,
        CmdDeleteSlice,
        CmdLazyChopStart,
        CmdLazyChopStop,
        CmdStretch,
        CmdToggleLock,
        CmdSetSliceParam,
        CmdSetSliceBounds,
        CmdSplitSlice,
        CmdTransientChop,
        CmdEqualChop,
        CmdRelinkFile,
        CmdFileLoadCompleted,
        CmdFileLoadFailed,
        CmdUndo,
        CmdRedo,
        CmdBeginGesture,
        CmdPanic,
        CmdSelectSlice,
        CmdSetRootNote,
        CmdApplyTrim,
        CmdSetSliceLockAll,  // intParam1 = slice index, floatParam1 = 1.0 lock all / 0.0 unlock all
        CmdSetSliceColour,   // intParam1 = slice index, intParam2 = ARGB colour
        CmdSetSliceName,     // intParam1 = slice index, stringParam = new name (empty = clear)
    };

    // ── Load kind ─────────────────────────────────────────────────────────
    enum LoadKind { LoadKindReplace = 0, LoadKindRelink = 1 };

    // ── Trim preference ───────────────────────────────────────────────────────
    enum TrimPreference { TrimPrefAsk = 0, TrimPrefAlways = 1, TrimPrefNever = 2 };

    // ── Sample availability state ────────────────────────────────────────────
    enum SampleAvailabilityState
    {
        SampleStateEmpty                 = 0,
        SampleStateLoaded                = 1,
        SampleStateMissingAwaitingRelink = 2,
    };

    // ── Command ──────────────────────────────────────────────────────────────
    struct Command
    {
        CommandType type        { CmdNone };
        int         intParam1   { 0 };
        int         intParam2   { 0 };
        float       floatParam1 { 0.0f };
        juce::File  fileParam;
        juce::String stringParam;   // used by CmdSetSliceName
        static constexpr int kMaxPositions = SliceManager::kMaxSlices + 2;
        std::array<int, kMaxPositions> positions {};
        int numPositions { 0 };
        bool isCommit { false };    // CmdSetSliceBounds: true = mouseUp final commit, triggers crush inheritance
        /** When true, CmdSetSliceParam/CmdSelectSlice/CmdSetSliceLock target
         *  sliceManager2/voicePool2 (SFZ-PLAYER) instead of sliceManager/
         *  voicePool (the Slicer). Defaults to false so every pre-existing
         *  call site (which never sets this) is completely unaffected.
         *  Only ADSR-related fields are meaningfully supported for engine 2
         *  -- SFZ-PLAYER has no manual slicing, so slice-bounds/creation/
         *  deletion commands are never sent with this flag set. */
        bool targetEngine2 { false };
    };

    // ── UI snapshot (double-buffered, written on audio thread) ───────────────
    struct UiSliceSnapshot
    {
        std::array<Slice, SliceManager::kMaxSlices> slices;
        // Pre-computed end sample for each slice (derived from marker model).
        // sliceEndSamples[i] = slices[i+1].startSample, or sampleNumFrames for last.
        // Populated by publishUiSliceSnapshot() — safe to read on the UI thread.
        std::array<int, SliceManager::kMaxSlices> sliceEndSamples {};
        int          numSlices          { 0 };
        int          selectedSlice      { -1 };
        int          rootNote           { 36 };
        bool         sampleLoaded       { false };
        bool         sampleMissing      { false };
        int          sampleNumFrames    { 0 };
        char         sampleFileName[512] {};   // plain char array — lock-free-safe
        bool         isDefaultSample   { false };
        bool         midiSelectsSlice   { false };
    };

    // ── Oscilloscope ring buffer size ─────────────────────────────────────────
    static constexpr int kOscRingBufferSize = 4096;  // must be power of 2

    // =========================================================================
    // Constructor / Destructor
    // =========================================================================
    DysektProcessor();
    ~DysektProcessor() override;

    // =========================================================================
    // AudioProcessor overrides
    // =========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override              { return true; }

    const juce::String getName() const override  { return "DYSEKT-SF"; }
    bool acceptsMidi()  const override           { return true; }
    bool producesMidi() const override           { return false; }
    bool isMidiEffect() const override           { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms()    override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // =========================================================================
    // Public API
    // =========================================================================
    void pushCommand (Command cmd);

    /** Controls where incoming live MIDI is routed.
     *
     *  Slicer    — all channels (except sf2Ch) go to the slice engine.
     *              sfzPlayer receives mask = 0 (no live input).
     *  SfPlayer  — slicer is bypassed entirely; sfzPlayer receives
     *              mask = all assigned SF-track channels.
     *  Sequencer — sequencer drives SF output; sfzPlayer live mask is
     *              governed by setSelectedSfLiveChannels(); slicer is bypassed.
     */
    enum class MidiRouteMode { Slicer, SfPlayer, SfzPlayer2, Sequencer };
    void setMidiRouteMode (MidiRouteMode mode);

    void loadFileAsync      (const juce::File& file);
    void loadDefaultSampleIfNeeded();   // loads Empty.wav on first launch

    // ADSR param pointers — read by SliceWaveformLcd for envelope display
    std::atomic<float>* attackParam       { nullptr };
    std::atomic<float>* decayParam        { nullptr };
    std::atomic<float>* sustainParam      { nullptr };
    std::atomic<float>* releaseParam      { nullptr };
    std::atomic<float>* holdParam         { nullptr };
    void loadSoundFontAsync (const juce::File& file,
                              SoundFontLoadTarget target = SoundFontLoadTarget::Slicer,
                              int presetBank = -1, int presetProgram = -1);
    void relinkFileAsync    (const juce::File& file);
    void applyTrimToCurrentSample (int trimStart, int trimEnd);

    /** Read-only access to the latest published UI snapshot (UI thread only).
     *  Sets uiReadingSnapshot so publishUiSliceSnapshot() won't stomp the buffer
     *  we are about to read.  Caller must call releaseUiSliceSnapshot() when done. */
    const UiSliceSnapshot& getUiSliceSnapshot() const noexcept
    {
        uiReadingSnapshot.store (true, std::memory_order_seq_cst);
        return uiSliceSnapshots[(size_t) uiSliceSnapshotIndex.load (std::memory_order_acquire)];
    }

    /** Release the read guard acquired by getUiSliceSnapshot(). */
    void releaseUiSliceSnapshot() const noexcept
    {
        uiReadingSnapshot.store (false, std::memory_order_release);
    }

    int getUiSliceSnapshotVersion() const noexcept
    {
        return (int) uiSnapshotVersion.load (std::memory_order_acquire);
    }

    /** SFZ-PLAYER engine equivalents of the three accessors above — read
     *  sliceManager2's published snapshot instead of sliceManager's. */
    const UiSliceSnapshot& getUiSliceSnapshot2() const noexcept
    {
        uiReadingSnapshot2.store (true, std::memory_order_seq_cst);
        return uiSliceSnapshots2[(size_t) uiSliceSnapshotIndex2.load (std::memory_order_acquire)];
    }

    void releaseUiSliceSnapshot2() const noexcept
    {
        uiReadingSnapshot2.store (false, std::memory_order_release);
    }

    int getUiSliceSnapshotVersion2() const noexcept
    {
        return (int) uiSnapshotVersion2.load (std::memory_order_acquire);
    }

    void publishUiSliceSnapshot();
    void publishUiSliceSnapshot2();

    /** Thread-safe: call from the UI thread to request that the next
     *  processBlock() republish both UI snapshots (sliceManager and
     *  sliceManager2) immediately, e.g. after a UI-only selection change
     *  that isn't itself routed through the Command FIFO. */
    void markUiSnapshotDirty() noexcept
    {
        uiSnapshotDirty.store (true, std::memory_order_release);
    }

    /** Returns the peak amplitude (0..1) at a given sample position in the
     *  loaded audio buffer.  Used by SliceWaveformLcd to render the mini waveform.
     *  Safe to call from the UI (message) thread. */
    float getWaveformPeakAt (int samplePosition) const noexcept
    {
        auto snap = sampleData.getSnapshot();
        if (snap == nullptr) return 0.0f;
        const auto& buf = snap->buffer;
        const int n = buf.getNumSamples();
        if (samplePosition < 0 || samplePosition >= n) return 0.0f;
        float peak = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            peak = std::max (peak, std::abs (buf.getSample (ch, samplePosition)));
        return peak;
    }

    /** Same as getWaveformPeakAt, but reads from an arbitrary SampleData
     *  instance instead of always reading the Slicer's sampleData. Used by
     *  SliceWaveformLcd (in SFZ-PLAYER mode, against sampleData2) and
     *  Sf2WaveformLcd (against sampleData3) — neither should read the
     *  Slicer's own buffer. */
    static float getWaveformPeakAtIn (const SampleData& source, int samplePosition) noexcept
    {
        auto snap = source.getSnapshot();
        if (snap == nullptr) return 0.0f;
        const auto& buf = snap->buffer;
        const int n = buf.getNumSamples();
        if (samplePosition < 0 || samplePosition >= n) return 0.0f;
        float peak = 0.0f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            peak = std::max (peak, std::abs (buf.getSample (ch, samplePosition)));
        return peak;
    }

    // =========================================================================
    // Public subsystem members (accessed directly by UI)
    // =========================================================================
    juce::AudioProcessorValueTreeState apvts;
    SliceManager     sliceManager;
    VoicePool        voicePool;
#if DYSEKT_STANDALONE
    SequencerEngine  sequencer;
    AbletonLink      abletonLink;
#endif
    LazyChopEngine   lazyChop;
    SampleData       sampleData;

    /** SFZ-PLAYER's own independent slice engine -- a second, complete
     *  Slicer instance. Loads .sfz files only; each SFZ key zone/region
     *  becomes a real slice pinned to its MIDI note via pinSliceMidiNote,
     *  exactly like the Slicer's chromatic-slice MIDI mapping. No manual
     *  slicing (Lazy Chop, marker editing) and no chromatic mode apply
     *  here -- the slice layout is entirely determined by the loaded SFZ
     *  file, never user-edited. Receives MIDI on channel 2 (see
     *  sfzPlayer2ChannelMask in setMidiRouteMode). Completely independent
     *  of sliceManager/voicePool/sampleData -- loading a file into one
     *  engine never touches the other. */
    SliceManager     sliceManager2;
    VoicePool        voicePool2;

    /** The SFZ-PLAYER tab's own independent preview waveform buffer.
     *  Purely visual — never touched by any audio engine (sfzPlayer2 has its
     *  own internal sfizz state for actual playback) and decoupled from the
     *  Slicer engine's sampleData so that loading a file into one mode can
     *  never bleed into, or corrupt, the other mode's sample. Not persisted
     *  in session state — it's an ephemeral preview, lost on reload, same as
     *  the Slicer's own pre-fix preview behaviour was for this case. */
    SampleData       sampleData2;

    /** The SF2-PLAYER tab's own independent preview waveform buffer.
     *  Mirrors sampleData2 exactly but for the SF2-PLAYER (FluidSynth)
     *  engine -- purely visual, never touched by sfzPlayer's actual
     *  playback. Rendered via SoundFontLoadTarget::SfPlayer, which reuses
     *  the same sfizz-based LoadJob renderer as SfzPlayer2 (sfizz can
     *  load .sf2 files directly), since building a true FluidSynth-based
     *  offline renderer was out of scope -- this is a display-accuracy
     *  tradeoff, not a live-playback one. Not persisted; ephemeral. */
    SampleData       sampleData3;

    /** Read-only "preview zones" overlay for the SF2-PLAYER tab's
     *  waveform -- one colored band per rendered note in sampleData3.
     *  Published by processBlock from pendingPreviewZones3 whenever a
     *  SoundFontLoadTarget::SfPlayer load completes. (The SFZ-PLAYER tab
     *  no longer has an equivalent overlay -- it became a full second
     *  Slicer instance, see sliceManager2/voicePool2, with real slices
     *  instead of a read-only zone display.) */
    SfzPreviewZoneStore previewZones3;

    /** Index into the current previewZones3 snapshot of the zone last
     *  clicked in the SF2-PLAYER waveform view. -1 = no selection. */
    std::atomic<int> selectedPreviewZone3 { -1 };

    /** One-shot click-to-audition voice for SF2-PLAYER preview zones.
     *  Plays back [start, end) directly out of sampleData3's rendered
     *  buffer and mixes in alongside sfzPlayer's output. */
    struct ZonePreviewVoice3
    {
        std::atomic<int>  triggerStart  { -1 };
        std::atomic<int>  triggerEnd    { -1 };
        std::atomic<int>  playPosition  { -1 };
        std::atomic<int>  playEnd       { -1 };
    } zonePreview3;

    MidiLearnManager midiLearn;

    // ── SF2 player (SF-PLAYER, ch3 default) ──────────────────────────────────
    SfzPlayer sfzPlayer;

    // ── SFZ player (SFZ-PLAYER, ch2 default) ─────────────────────────────────
    SfzPlayer sfzPlayer2;

    // ── Spectrum analyser (post-EQ FFT data, read by GlobalEqPanel timer) ────
    SpectrumAnalyser spectrumAnalyser;

    // =========================================================================
    // UI-readable state atomics
    // =========================================================================
    std::atomic<float> zoom   { 1.0f };
    std::atomic<float> scroll { 0.0f };
    std::atomic<float> dawBpm { 120.0f };

    std::atomic<bool> midiSelectsSlice   { false };
    std::atomic<int>  midiFollowTriggeredSlice { -1 }; // last MIDI-triggered slice idx for waveform viewport follow

    // Per-slice peak meters (0..1, decaying, written from audio thread)
    static constexpr int kMaxMeterSlices = 128;
    std::array<std::atomic<float>, kMaxMeterSlices> slicePeakL {};
    std::array<std::atomic<float>, kMaxMeterSlices> slicePeakR {};
    // Per-slice peak meters for SFZ-Player (voicePool2) -- separate so
    // voicePool2 activity never bleeds into the main slicer's SCB meters.
    std::array<std::atomic<float>, kMaxMeterSlices> slicePeak2L {};
    std::array<std::atomic<float>, kMaxMeterSlices> slicePeak2R {};
    // Master output peak meters (0..1, decaying, written in audio thread, read by UI)
    std::atomic<float> masterPeakL {0.0f}, masterPeakR {0.0f};
    // Peak meters written by processBlock, read by SfzModulePanel timer
    std::atomic<float> sfzPeakL { 0.0f };
    std::atomic<float> sfzPeakR { 0.0f };
    // MIDI activity counter for SF player LED: +1 on NoteOn, -1 on NoteOff (clamped ≥0)
    std::atomic<int>   sfzMidiActivity { 0 };

    // SFZ-Player (sfzPlayer2) peak meters and MIDI activity
    std::atomic<float> sfz2PeakL { 0.0f };
    std::atomic<float> sfz2PeakR { 0.0f };
    std::atomic<int>   sfz2MidiActivity { 0 };
    // Live drag bounds (UI -> audio thread, bypasses FIFO for low latency)
    std::atomic<int> liveDragBoundsStart { 0 };
    std::atomic<int> liveDragBoundsEnd   { 0 };
    std::atomic<int> liveDragSliceIdx    { -1 };
    // --- Optimistic marker commit notification for UI (set on knob/CC commit, cleared by UI) ---
    std::atomic<int> pendingUiOptimisticIdx { -1 };
    std::atomic<int> pendingUiOptimisticSample { -1 };
    std::atomic<int>   paramsSyncedForSlice   { -1 };  // slice index that sliceStartParam/sliceEndParam currently describe
    std::atomic<float> sliceStartPublished    { -1.0f }; // value written when syncing, used to detect real CC moves
    // Pre-pickup ghost position for FieldSliceStart absolute CC.
    // Written by the audio thread (processMidi) each time an absolute CC for
    // FieldSliceStart arrives while ccPickedUp is false. Read by the UI thread
    // (drawMarkerSliderCell) to draw a ghost cursor showing where the physical
    // knob/fader is vs. where the actual marker is. -1.0f = not active.
    std::atomic<float> markerCcGhostNorm { -1.0f };

    // Relative encoder directional indicator for Marker slider.
    // markerRelDir: -1 = CCW, 0 = idle, +1 = CW
    // markerRelLastMs: timestamp of last encoder tick for fade-out
    std::atomic<int>  markerRelDir    { 0 };
    std::atomic<int>  markerRelLastMs { 0 };

    // Fine window state for post-pickup absolute CC marker control.
    // After pickup, the CC maps to a narrow window (kMarkerFineWindowNorm * totalSamples)
    // centred on the marker position at the moment of pickup, giving ~7x finer resolution.
    // Indexed by slice. Audio-thread only — no atomics needed.
    static constexpr float kMarkerFineWindowNorm = 0.20f; // full knob sweep = 20% of sample
    float markerFinePickupCcNorm   [128] {};   // CC norm (0-1) at pickup moment, -1 = not set
    float markerFinePickupMarkerNorm[128] {};   // marker norm at pickup moment
    // UI: expose fine window for ghost bar drawing (UI thread reads)
    std::atomic<float> markerFineWindowLo { -1.0f }; // low edge norm, -1 = inactive
    std::atomic<float> markerFineWindowHi { -1.0f }; // high edge norm
    // User toggle: when true the post-pickup fine window is active;
    // when false the CC maps the full sample range directly (normal mode).
    std::atomic<bool>  markerFineMode     { false };

    // ── Per-slice CC state ───────────────────────────────────────────────────
    // Each slice independently tracks pickup and smoother state for every
    // MIDI learn slot.  Switching slices never requires pickup re-acquisition
    // for a CC that has already been used on that slice — eliminating the
    // root cause of the cross-slice jump problem.
    //
    // Memory: 128 slices × 30 slots × (1 bool + 1 bool + ~32B smoother) ≈ 128 KB
    // Audio-thread write/read only.
    static constexpr int kMaxCCSlices = 128; // matches SliceManager::kMaxSlices

    // [sliceIdx][fieldId]
    std::array<std::array<bool, kMidiLearnNumSlots>, kMaxCCSlices> ccPickedUp {};
    std::array<std::array<bool, kMidiLearnNumSlots>, kMaxCCSlices> ccSmootherActive {};
    std::array<std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>,
               kMidiLearnNumSlots>, kMaxCCSlices> ccSmoothers;

    // Commit-on-idle for FieldSliceStart CC — write live drag atomics during
    // movement, commit to SliceManager only after kIdleBlocks of silence.
    static constexpr int kMarkerIdleBlocks = 4;  // ~80ms at 512/44100
    int  markerIdleCounter  = 0;    // counts blocks since last CC message
    bool markerPending       = false; // true while a commit is outstanding
    int  markerPendingSlice  = -1;   // which slice the pending commit is for
    int  markerSmootherSlice  = -1;  // slice active when absolute-CC smoother was seeded
    int  lastProcessedSlice   = -1;  // detects direct selectedSlice.store() changes between blocks
    int  ccLastDispatchedSel  = -1;  // intra-buffer slice-switch guard (see processMidi)
    std::atomic<float> sliceEndPublished      { -1.0f };

    // Shift-preview request (-2 = idle, -1 = stop, >= 0 = start at position)
    std::atomic<int> shiftPreviewRequest { -2 };

    // ── UI pad-click MIDI injection ───────────────────────────────────────────
    // Written by the UI thread (PadGridView::mouseDown/Up), consumed in processBlock.
    // -1 = nothing pending.
    std::atomic<int> uiNoteOnRequest  { -1 };
    std::atomic<int> uiNoteOffRequest { -1 };

    // SF2 / SFZ keyboard UI note injection — routed on the sfz channel (ch16)
    // so processMidi() skips them and only sfzPlayer receives them.
    std::atomic<int> sfzUiNoteOnRequest  { -1 };
    std::atomic<int> sfzUiNoteOffRequest { -1 };

    // SFZ-Player (sfzPlayer2) keyboard UI note injection
    std::atomic<int> sfz2UiNoteOnRequest  { -1 };
    std::atomic<int> sfz2UiNoteOffRequest { -1 };

    // 128-bit active-note bitmask for the SF2/SFZ player (updated on audio thread,
    // read on UI thread for KeysPanel highlighting — display-only, torn reads OK)
    std::atomic<uint64_t> sfzActiveNotes[2] {}; // [0]=notes 0-63, [1]=notes 64-127

    // 128-bit active-note bitmask for SFZ-Player (sfzPlayer2)
    std::atomic<uint64_t> sfz2ActiveNotes[2] {};

    // MIDI channel routing bitmasks (bit N = channel N, 1-based, bits 1–16 used).
    //
    // Channel ownership rules:
    //   channel 1                 → slicer always (hardwired, not stored in any mask)
    //   chromaticSliceChannelMask → channels 2–16 explicitly assigned to chromatic slices
    //   sfPlayerChannelMask       → channels currently active for SF player live MIDI;
    //                               never overlaps channel 1 or chromaticSliceChannelMask
    //   savedSfPlayerChannelMask  → mirrors sfPlayerChannelMask for state-save/restore
    //   0 in sfPlayerChannelMask  = SF player disabled (no file loaded / no range set yet)
    //
    // IMPORTANT: these masks are channel-ownership state, NOT tied to which UI
    // tab currently has focus — setMidiRouteMode() only updates the UI-display
    // hint (midiRouteMode) and must never zero these. All engines (Slicer,
    // SF-Player, SFZ-Player2) listen on their owned channels concurrently,
    // regardless of which tab is in view.
    //
    // All three are derived/cached state — not serialised directly; rebuilt on load
    // (sfPlayerChannelMask and savedSfPlayerChannelMask are restored from the saved
    //  lo/hi range in setStateInformation; chromaticSliceChannelMask from slice data).
    std::atomic<uint32_t> sfPlayerChannelMask      { 0u };       // disabled until user sets a range
    std::atomic<uint32_t> savedSfPlayerChannelMask { 1u << 3 };  // hardcoded ch3 default (unless preset overrides)
    std::atomic<uint32_t> chromaticSliceChannelMask { 0u };

    // SFZ-Player (sfzPlayer2) channel ownership — default ch2 (bit 2)
    std::atomic<uint32_t> sfzPlayer2ChannelMask      { 1u << 2 }; // ch 2 default
    std::atomic<uint32_t> savedSfzPlayer2ChannelMask { 1u << 2 };

    /** Rebuild chromaticSliceChannelMask from current slice data.
     *  Must be called on the audio thread (or before first audio callback). */
    void rebuildChromaticChannelMask();

    // Trim region markers (stored in samples)
    std::atomic<int>  trimRegionStart  { 0 };
    std::atomic<int>  trimRegionEnd    { 0 };
    std::atomic<bool> trimModeActive   { false };  // set by editor; CC routes to trim when true
    std::atomic<int>  midiRouteMode    { 0 };       // 0=Slicer, 1=SfPlayer, 2=Sequencer
    std::atomic<int> trimInSample    { 0 };
    std::atomic<int> trimOutSample   { 0 };

    // Trim dialog preference (persisted)
    std::atomic<int> trimPreference  { (int) TrimPrefAsk };

    // Oscilloscope ring buffer (audio thread writes, UI reads)
    std::array<float, kOscRingBufferSize> oscRingBuffer {};
    std::atomic<int> oscRingWriteHead { 0 };

    // Sample availability (see SampleAvailabilityState enum)
    std::atomic<int>  sampleAvailability { (int) SampleStateEmpty };
    std::atomic<bool> sampleMissing      { false };
    juce::String      missingFilePath;

    /** Set by setStateInformation when restoring an SF2 preset index.
     *  The editor polls this on its timer and applies it once the preset
     *  list becomes available, then resets it to -1. */
    std::atomic<int> pendingSfzPresetIndex { -1 };

    // Peak metering (written in processBlock, read by UI)

private:
    // =========================================================================
    // Private types
    // =========================================================================
    struct FailedLoadResult
    {
        int        token { 0 };
        LoadKind   kind  { LoadKindReplace };
        juce::File file;
    };

    // =========================================================================
    // Private helpers
    // =========================================================================
    void requestSampleLoad (const juce::File& file, LoadKind kind);
    void clearVoicesBeforeSampleSwap();
    void clearVoicesBeforeSampleSwap2();
    void clampSlicesToSampleBounds();
    void handleCommand (const Command& cmd);
    void drainCommands();
    void drainOverflowCommands (bool& handledAny);
    void drainCoalescedCommands (bool& handledAny);
    bool enqueueOverflowCommand (Command cmd);
    bool enqueueCoalescedCommand (const Command& cmd);
    void processMidi (const juce::MidiBuffer& midi);

    /** SFZ-PLAYER's MIDI handler -- a minimal counterpart to processMidi().
     *  Unlike the Slicer, SFZ-PLAYER has no manual slicing (Lazy Chop, marker
     *  editing), no chromatic-channel routing, no MIDI Learn CC dispatch, and
     *  no trim-mode chromatic fallback -- its slice layout comes entirely
     *  from the loaded SFZ file's key zones. This handler only does:
     *  note-on -> sliceManager2.midiNoteToSlice -> voicePool2.startVoice,
     *  note-off, all-notes-off, all-sound-off. */
    void processMidi2 (const juce::MidiBuffer& midi);

    UndoManager::Snapshot makeSnapshot();
    void captureSnapshot();
    void restoreSnapshot (const UndoManager::Snapshot& snap);

    static float sanitiseSample (float s) noexcept
    {
        return (std::isfinite (s) && s >= -8.0f && s <= 8.0f) ? s : 0.0f;
    }

    // =========================================================================
    // Command FIFO
    // =========================================================================
    static constexpr int kCommandFifoSize  = 256;
    static constexpr int kOverflowFifoSize = 32;

    juce::AbstractFifo commandFifo { kCommandFifoSize };
    std::array<Command, kCommandFifoSize> commandBuffer;

    std::array<Command, kOverflowFifoSize> overflowCommandBuffer {};
    std::atomic<int> overflowWriteIndex { 0 };
    std::atomic<int> overflowReadIndex  { 0 };

    // Coalescing slots for high-frequency drag commands
    std::atomic<bool>  pendingSetSliceParam         { false };
    std::atomic<int>   pendingSetSliceParamField    { 0 };
    std::atomic<float> pendingSetSliceParamValue    { 0.0f };
    std::atomic<int>   pendingSetSliceParamSkipLock { 0 };   // preserves intParam2 through coalesce

    std::atomic<bool> pendingSetSliceBounds      { false };
    std::atomic<int>  pendingSetSliceBoundsIdx   { -1 };
    std::atomic<int>  pendingSetSliceBoundsStart { 0 };
    std::atomic<int>  pendingSetSliceBoundsEnd   { 0 };

    // Diagnostics
    std::atomic<int> droppedCommandCount         { 0 };
    std::atomic<int> droppedCommandTotal         { 0 };
    std::atomic<int> droppedCriticalCommandTotal { 0 };

    // =========================================================================
    // UI snapshot double-buffer
    // =========================================================================
    std::array<UiSliceSnapshot, 2> uiSliceSnapshots;
    std::atomic<int>      uiSliceSnapshotIndex { 0 };
    std::atomic<uint32_t> uiSnapshotVersion    { 0 };
    std::atomic<bool>     uiSnapshotDirty      { false };
    // Set to true by the UI thread while it holds a reference to the snapshot.
    // publishUiSliceSnapshot() skips a flip if this is set, preventing a
    // data race on the juce::String (now char[]) fields.
    mutable std::atomic<bool>     uiReadingSnapshot    { false };

    /** Second, independent double-buffer for the SFZ-PLAYER engine
     *  (sliceManager2/sampleData2) -- mirrors the block above exactly.
     *  A SEPARATE uiReadingSnapshot2 guard is required: sharing the
     *  original flag between two unrelated engines' read/publish races
     *  would let one engine's read block the other's publish, or worse,
     *  let a stale guard value incorrectly skip a real publish. */
    std::array<UiSliceSnapshot, 2> uiSliceSnapshots2;
    std::atomic<int>      uiSliceSnapshotIndex2 { 0 };
    std::atomic<uint32_t> uiSnapshotVersion2    { 0 };
    mutable std::atomic<bool>     uiReadingSnapshot2   { false };

    // =========================================================================
    // Undo / redo
    // =========================================================================
    UndoManager undoMgr;
    bool gestureSnapshotCaptured    { false };
    int  blocksSinceGestureActivity { 0 };

public:
    // =========================================================================
    // Sample loading (public so UI thread can dispatch SFZ/SF2 loads)
    // =========================================================================
    juce::ThreadPool fileLoadPool { 1 };
    bool             defaultSampleScheduled { false }; // true once default or saved sample is queued
    std::atomic<int>  nextLoadToken  { 0 };
    std::atomic<int>  latestLoadToken{ 0 };
    std::atomic<int>  latestLoadKind { (int) LoadKindReplace };
    std::atomic<SampleData::DecodedSample*> completedLoadData   { nullptr };
    std::atomic<FailedLoadResult*>          completedLoadFailure { nullptr };
    std::atomic<SfzSlicePayload*>           pendingSfzSlices     { nullptr };

    /** Second, independent load-result pipeline for the SFZ-PLAYER's preview
     *  buffer (sampleData2). Populated by loadSoundFontAsync(file,
     *  SoundFontLoadTarget::SfzPlayer2) via SoundFontLoader; consumed in
     *  processBlock and applied to sampleData2 only — never sampleData. */
    std::atomic<SampleData::DecodedSample*> completedLoadData2  { nullptr };

    /** Heap-allocated zone payload posted by SoundFontLoader for a
     *  SoundFontLoadTarget::SfzPlayer2 load -- the same per-note
     *  descriptors as pendingSfzSlices, but consumed differently:
     *  processBlock turns each descriptor into a REAL slice in
     *  sliceManager2 (see Slice::nextSliceIdx for the loop-region
     *  two-slice split), not a read-only display overlay. */
    std::atomic<SfzPreviewZonePayload*> pendingPreviewZones2 { nullptr };

    /** Third, independent load-result pipeline for the SF2-PLAYER's preview
     *  buffer (sampleData3). Populated by loadSoundFontAsync(file,
     *  SoundFontLoadTarget::SfPlayer) via SoundFontLoader; consumed in
     *  processBlock and applied to sampleData3 only. Mirrors
     *  completedLoadData2/pendingPreviewZones2 exactly. */
    std::atomic<SampleData::DecodedSample*> completedLoadData3  { nullptr };
    std::atomic<SfzPreviewZonePayload*>     pendingPreviewZones3 { nullptr };

    /** SF2-PLAYER per-preset waveform preview state. sampleData3/previewZones3
     *  used to be locked to whichever preset the .sf2 file defaulted to at
     *  load time; these track which preset they now reflect so a preset-grid
     *  click can request a scoped re-render (see SoundFontLoader::load's
     *  presetBank/presetProgram params) and the UI can dedupe/show progress.
     *  -1/-1 means "the file's default preset" (nothing requested yet). */
    std::atomic<int>  sf2PreviewRenderedBank     { -1 };
    std::atomic<int>  sf2PreviewRenderedProgram  { -1 };
    std::atomic<int>  sf2PreviewRequestedBank    { -1 };
    std::atomic<int>  sf2PreviewRequestedProgram { -1 };
    std::atomic<bool> sf2PreviewRenderInFlight   { false };


    // =========================================================================
    // APVTS parameter pointers (assigned in constructor, constant thereafter)
    // =========================================================================
    std::atomic<float>* masterVolParam    { nullptr };
    std::atomic<float>* bpmParam          { nullptr };
    std::atomic<float>* pitchParam        { nullptr };
    std::atomic<float>* algoParam         { nullptr };

    std::atomic<float>* muteGroupParam    { nullptr };
    std::atomic<float>* monoParam         { nullptr };
    std::atomic<float>* stretchParam      { nullptr };
    std::atomic<float>* tonalityParam     { nullptr };
    std::atomic<float>* formantParam      { nullptr };
    std::atomic<float>* formantCompParam  { nullptr };
    std::atomic<float>* grainModeParam    { nullptr };
    std::atomic<float>* releaseTailParam  { nullptr };
    std::atomic<float>* reverseParam      { nullptr };
    std::atomic<float>* loopParam         { nullptr };
    std::atomic<float>* oneShotParam      { nullptr };
    std::atomic<float>* maxVoicesParam    { nullptr };
    std::atomic<float>* centsDetuneParam  { nullptr };
    std::atomic<float>* panParam          { nullptr };
    std::atomic<float>* filterCutoffParam { nullptr };
    std::atomic<float>* filterResParam    { nullptr };
    std::atomic<float>* sliceStartParam   { nullptr };
    std::atomic<float>* sliceEndParam     { nullptr };
    std::atomic<float>* masterPitchParam  { nullptr };  // v25: master audio-domain pitch shift

    // =========================================================================
    // Playback state
    // =========================================================================
    double currentSampleRate { 44100.0 };
    bool   heldNotes[128]    {};
    bool   heldNotes2[128]   {};   // SFZ-PLAYER's own note tracking — independent of the Slicer's

    // ── Global post-mix EQ (juce::dsp, runs after voice mix, before master volume) ──
    juce::dsp::ProcessorChain<
        juce::dsp::IIR::Filter<float>,   // band 0: low shelf  (~80 Hz)
        juce::dsp::IIR::Filter<float>,   // band 1: low-mid peak (draggable 100–1000 Hz)
        juce::dsp::IIR::Filter<float>,   // band 2: mid peak   (draggable 500–5000 Hz)
        juce::dsp::IIR::Filter<float>,   // band 3: high-mid peak (draggable 1–10 kHz)
        juce::dsp::IIR::Filter<float>    // band 4: high shelf (~12 kHz)
    > globalEq;
    bool globalEqNeedsUpdate = true;

    // ── v25: master audio-domain pitch shift (post-EQ, pre-output) ───────────
    // Uses signalsmith-stretch in pitch-only mode (stretch ratio = 1.0).
    // Allocated in prepareToPlay; nullptr until then.
    std::unique_ptr<signalsmith::stretch::SignalsmithStretch<float, void>> masterPitchShifter;
    float  masterPitchSemitones = 0.0f;   // last applied value — avoid reinit on no change
    // Per-channel scratch buffers resized in prepareToPlay
    std::vector<float> masterPitchScratchL;
    std::vector<float> masterPitchScratchR;

    friend class SoundFontLoader;

    // ── Crash logger ────────────────────────────────────────────────────────
    // Declared last so it is constructed first and destroyed last,
    // ensuring the log captures the full object lifetime.
    CrashLogger crashLogger;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DysektProcessor)
};
