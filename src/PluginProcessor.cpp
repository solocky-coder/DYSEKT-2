#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "audio/GrainEngine.h"
#include "audio/AudioAnalysis.h"
#include "audio/SoundFontLoader.h"
#include <BinaryData.h>
#include <functional>
#include <memory>

namespace
{
class SampleDecodeJob final : public juce::ThreadPoolJob
{
public:
    using SuccessFn = std::function<void (int, DysektProcessor::LoadKind,
                                          std::unique_ptr<SampleData::DecodedSample>)>;
    using FailureFn = std::function<void (int, DysektProcessor::LoadKind, const juce::File&)>;

    SampleDecodeJob (juce::File sourceFile, double targetRate, int loadToken,
                     DysektProcessor::LoadKind kind,
                     SuccessFn onSuccessIn, FailureFn onFailureIn)
        : juce::ThreadPoolJob ("SampleDecodeJob"),
          file (std::move (sourceFile)),
          sampleRate (targetRate),
          token (loadToken),
          loadKind (kind),
          onSuccess (std::move (onSuccessIn)),
          onFailure (std::move (onFailureIn))
    {
    }

    JobStatus runJob() override
    {
        auto decoded = SampleData::decodeFromFile (file, sampleRate);
        if (shouldExit())
            return jobHasFinished;

        if (decoded != nullptr)
            onSuccess (token, loadKind, std::move (decoded));
        else
            onFailure (token, loadKind, file);
        return jobHasFinished;
    }

private:
    juce::File file;
    double sampleRate = 44100.0;
    int token = 0;
    DysektProcessor::LoadKind loadKind = DysektProcessor::LoadKindReplace;
    SuccessFn onSuccess;
    FailureFn onFailure;
};

static constexpr uint32_t kValidLockMask =
    kLockBpm | kLockPitch | kLockAlgorithm | kLockAttack | kLockDecay | kLockSustain
    | kLockRelease | kLockMuteGroup | kLockStretch | kLockTonality | kLockFormant
    | kLockFormantComp | kLockGrainMode | kLockVolume | kLockReleaseTail | kLockReverse
    | kLockOutputBus | kLockLoop | kLockOneShot | kLockCentsDetune
    | kLockPan | kLockFilter;
static Slice sanitiseRestoredSlice (Slice s)
{
    s.startSample = juce::jmax (0, s.startSample);
    // Marker model: endSample derived from next marker — no field to sanitise.

    s.midiNote = juce::jlimit (0, 127, s.midiNote);
    s.bpm = juce::jlimit (20.0f, 999.0f, s.bpm);
    s.pitchSemitones = juce::jlimit (-48.0f, 48.0f, s.pitchSemitones);
    s.algorithm = juce::jlimit (0, 1, s.algorithm == 2 ? 1 : s.algorithm);
    s.attackSec = juce::jlimit (0.0f, 120.0f, s.attackSec);
    s.holdSec   = juce::jlimit (0.0f, 120.0f, s.holdSec);
    s.decaySec = juce::jlimit (0.0f, 120.0f, s.decaySec);
    s.sustainLevel = juce::jlimit (0.0f, 1.0f, s.sustainLevel);
    s.releaseSec = juce::jlimit (0.0f, 120.0f, s.releaseSec);
    s.muteGroup = juce::jlimit (0, 32, s.muteGroup);
    s.loopMode = juce::jlimit (0, 2, s.loopMode);
    s.tonalityHz = juce::jlimit (0.0f, 8000.0f, s.tonalityHz);
    s.formantSemitones = juce::jlimit (-24.0f, 24.0f, s.formantSemitones);
    s.grainMode = juce::jlimit (0, 2, s.grainMode);
    s.volume = juce::jlimit (-100.0f, 24.0f, s.volume);
    s.outputBus = juce::jlimit (0, 15, s.outputBus);
    s.centsDetune = juce::jlimit (-100.0f, 100.0f, s.centsDetune);
    s.pan         = juce::jlimit (-1.0f, 1.0f, s.pan);
    s.filterCutoff = juce::jlimit (20.0f, 20000.0f, s.filterCutoff);
    s.filterRes    = juce::jlimit (0.0f, 1.0f, s.filterRes);
    s.lockMask &= kValidLockMask;
    s.name = s.name.toUpperCase();
    return s;
}

static bool isCoalescableCommand (DysektProcessor::CommandType type)
{
    return type == DysektProcessor::CmdSetSliceParam
        || type == DysektProcessor::CmdSetSliceBounds;
}

static bool isCriticalCommand (DysektProcessor::CommandType type)
{
    switch (type)
    {
        case DysektProcessor::CmdLoadFile:
        case DysektProcessor::CmdCreateSlice:
        case DysektProcessor::CmdDeleteSlice:
        case DysektProcessor::CmdSplitSlice:
        case DysektProcessor::CmdTransientChop:
        case DysektProcessor::CmdRelinkFile:
        case DysektProcessor::CmdUndo:
        case DysektProcessor::CmdRedo:
        case DysektProcessor::CmdPanic:
        case DysektProcessor::CmdSelectSlice:
        case DysektProcessor::CmdSetRootNote:
        case DysektProcessor::CmdToggleLock:      // BUG FIX: Ensure lock toggle is never dropped
        case DysektProcessor::CmdSetSliceLockAll: // Also add SetSliceLockAll for consistency
            return true;
        default:
            return false;
    }
}
} // namespace

DysektProcessor::DysektProcessor()
    : AudioProcessor (BusesProperties()
                          // ── MIDI input bus ────────────────────────────────────────────────────
                          // NOTE: JUCE does not expose a getNumMidiInputs() API.
                          // A single MIDI input port is created by NEEDS_MIDI_INPUT=TRUE
                          // in CMakeLists.txt. Channel-based routing (sfPlayerChannelMask /
                          // processMidi) is the split between the slicer and SFZ player
                          // at runtime, on this one merged port.
                          // Do NOT add withInput(disabled) buses — they are audio buses
                          // and DAWs correctly ignore them as MIDI ports.
                          // ── Audio output buses ────────────────────────────────────────────────
                          .withOutput ("Main", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Out 2", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 3", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 4", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 5", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 6", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 7", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 8", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 9", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 10", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 11", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 12", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 13", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 14", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 15", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Out 16", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("SF2 Player", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "PARAMETERS", ParamLayout::createLayout())
{
    masterVolParam = apvts.getRawParameterValue (ParamIds::masterVolume);
    bpmParam       = apvts.getRawParameterValue (ParamIds::defaultBpm);
    pitchParam     = apvts.getRawParameterValue (ParamIds::defaultPitch);
    algoParam      = apvts.getRawParameterValue (ParamIds::defaultAlgorithm);
    attackParam    = apvts.getRawParameterValue (ParamIds::defaultAttack);
    decayParam     = apvts.getRawParameterValue (ParamIds::defaultDecay);
    sustainParam   = apvts.getRawParameterValue (ParamIds::defaultSustain);
    releaseParam   = apvts.getRawParameterValue (ParamIds::defaultRelease);
    holdParam      = apvts.getRawParameterValue (ParamIds::defaultHold);
    muteGroupParam = apvts.getRawParameterValue (ParamIds::defaultMuteGroup);
    monoParam      = apvts.getRawParameterValue (ParamIds::globalMono);
    stretchParam   = apvts.getRawParameterValue (ParamIds::defaultStretchEnabled);
    tonalityParam  = apvts.getRawParameterValue (ParamIds::defaultTonality);
    formantParam   = apvts.getRawParameterValue (ParamIds::defaultFormant);
    formantCompParam = apvts.getRawParameterValue (ParamIds::defaultFormantComp);
    grainModeParam   = apvts.getRawParameterValue (ParamIds::defaultGrainMode);
    releaseTailParam = apvts.getRawParameterValue (ParamIds::defaultReleaseTail);
    reverseParam     = apvts.getRawParameterValue (ParamIds::defaultReverse);
    loopParam        = apvts.getRawParameterValue (ParamIds::defaultLoop);
    oneShotParam     = apvts.getRawParameterValue (ParamIds::defaultOneShot);
    maxVoicesParam   = apvts.getRawParameterValue (ParamIds::maxVoices);
    centsDetuneParam  = apvts.getRawParameterValue (ParamIds::defaultCentsDetune);
    panParam          = apvts.getRawParameterValue (ParamIds::defaultPan);
    filterCutoffParam = apvts.getRawParameterValue (ParamIds::defaultFilterCutoff);
    filterResParam    = apvts.getRawParameterValue (ParamIds::defaultFilterRes);
    sliceStartParam  = apvts.getRawParameterValue (ParamIds::sliceStart);
    sliceEndParam    = apvts.getRawParameterValue (ParamIds::sliceEnd);
    publishUiSliceSnapshot();

    // SF2-PLAYER (sfzPlayer) is multi-timbral: per-preset channel routing is
    // handled entirely via setPresetOnChannel()/sfPlayerChannelMask, and its
    // FluidSynth MIDI branch never consults SfzPlayer::midiChannel — so no
    // setMidiChannel() call belongs here. (A previous single-channel-filter
    // call, setMidiChannel(3), was dead code: it only affects the sfizz/.sfz
    // branch of SfzPlayer::process(), which sfzPlayer never takes.)
    sfzPlayer2.setMidiChannel (2);   // SFZ-PLAYER  → ch 2 (default; user-adjustable, see sfzPlayer2ChannelMask)
}

DysektProcessor::~DysektProcessor()
{
    fileLoadPool.removeAllJobs (true, 5000);
    auto* pending = completedLoadData.exchange (nullptr, std::memory_order_acq_rel);
    delete pending;
    auto* failed = completedLoadFailure.exchange (nullptr, std::memory_order_acq_rel);
    delete failed;
    auto* pending2 = completedLoadData2.exchange (nullptr, std::memory_order_acq_rel);
    delete pending2;
    auto* pendingZones2 = pendingPreviewZones2.exchange (nullptr, std::memory_order_acq_rel);
    delete pendingZones2;
    auto* pending3 = completedLoadData3.exchange (nullptr, std::memory_order_acq_rel);
    delete pending3;
    auto* pendingZones3 = pendingPreviewZones3.exchange (nullptr, std::memory_order_acq_rel);
    delete pendingZones3;
}

bool DysektProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Main output must be stereo
    if (layouts.outputBuses.isEmpty())
        return false;
    if (layouts.outputBuses[0] != juce::AudioChannelSet::stereo())
        return false;

    // Additional outputs: stereo or disabled
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        if (! layouts.outputBuses[i].isDisabled()
            && layouts.outputBuses[i] != juce::AudioChannelSet::stereo())
            return false;
    }

    // No audio input buses declared (MIDI ports use getNumMidiInputs(), not
    // BusesProperties). Reject any layout that somehow presents audio inputs.
    for (int i = 0; i < layouts.inputBuses.size(); ++i)
    {
        if (! layouts.inputBuses[i].isDisabled())
            return false;
    }

    return true;
}

void DysektProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
#if DYSEKT_STANDALONE
    sequencer.setAbletonLink (&abletonLink);
    sequencer.setSfzPlayer   (&sfzPlayer);
    sequencer.addMainTrack();
#endif
    const bool rateChanged = (std::abs (sampleRate - currentSampleRate) > 0.01);

    currentSampleRate = sampleRate;
    voicePool.setSampleRate (sampleRate);
    voicePool2.setSampleRate (sampleRate);
    sfzPlayer.prepare (sampleRate, samplesPerBlock > 0 ? samplesPerBlock : 512);
    sfzPlayer2.prepare (sampleRate, samplesPerBlock > 0 ? samplesPerBlock : 512);
    std::fill (std::begin (heldNotes), std::end (heldNotes), false);
    std::fill (std::begin (heldNotes2), std::end (heldNotes2), false);

    // Initialise CC smoothers — 20 ms ramp gives silky response on absolute knobs
    for (auto& sliceRow : ccSmoothers)
        for (auto& s : sliceRow)
            s.reset (sampleRate, 0.020);

    // ── Re-decode sample if rate changed ─────────────────────────────────────
    // In some DAWs (Nuendo, Studio One) setStateInformation fires before
    // prepareToPlay, so the sample is decoded at the default 44100 Hz fallback
    // rather than the true project rate. When prepareToPlay later arrives with
    // the real rate we re-request the load so the buffer is decoded correctly.
    // clearVoicesBeforeSampleSwap() is called first to prevent any active voice
    // from reading a buffer that is about to be replaced on the background thread.
    if (rateChanged && sampleData.getFilePath().isNotEmpty())
    {
        clearVoicesBeforeSampleSwap();
        requestSampleLoad (juce::File (sampleData.getFilePath()), LoadKindRelink);
    }

    // ── Global post-mix EQ ────────────────────────────────────────────────────
    {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate       = sampleRate;
        spec.maximumBlockSize = (juce::uint32) (samplesPerBlock > 0 ? samplesPerBlock : 512);
        spec.numChannels      = 2;
        globalEq.prepare (spec);
        globalEqNeedsUpdate = true;
    }
}

void DysektProcessor::releaseResources() {}

void DysektProcessor::requestSampleLoad (const juce::File& file, LoadKind kind)
{
    const int token = nextLoadToken.fetch_add (1, std::memory_order_relaxed) + 1;
    latestLoadToken.store (token, std::memory_order_release);
    latestLoadKind.store ((int) kind, std::memory_order_release);

    // Keep only the latest completed decode payload.
    auto* oldDecoded = completedLoadData.exchange (nullptr, std::memory_order_acq_rel);
    delete oldDecoded;
    auto* oldFailure = completedLoadFailure.exchange (nullptr, std::memory_order_acq_rel);
    delete oldFailure;

    if (! file.existsAsFile())
    {
        if (kind == LoadKindRelink)
        {
            auto* payload = new FailedLoadResult();
            payload->token = token;
            payload->kind = kind;
            payload->file = file;
            auto* old = completedLoadFailure.exchange (payload, std::memory_order_acq_rel);
            delete old;
        }
        return;
    }

    const double sr = currentSampleRate > 0.0 ? currentSampleRate : 44100.0;

    auto onSuccess = [this] (int finishedToken, LoadKind finishedKind,
                             std::unique_ptr<SampleData::DecodedSample> decoded)
    {
        if (finishedToken != latestLoadToken.load (std::memory_order_acquire))
            return;

        auto* old = completedLoadData.exchange (decoded.release(), std::memory_order_acq_rel);
        delete old;
        latestLoadKind.store ((int) finishedKind, std::memory_order_release);
    };

    auto onFailure = [this] (int finishedToken, LoadKind finishedKind, const juce::File& failedFile)
    {
        if (finishedToken != latestLoadToken.load (std::memory_order_acquire))
            return;

        auto* payload = new FailedLoadResult();
        payload->token = finishedToken;
        payload->kind = finishedKind;
        payload->file = failedFile;
        auto* old = completedLoadFailure.exchange (payload, std::memory_order_acq_rel);
        delete old;
    };

    fileLoadPool.addJob (new SampleDecodeJob (file, sr, token, kind, onSuccess, onFailure), true);
}

void DysektProcessor::loadFileAsync (const juce::File& file)
{
    requestSampleLoad (file, LoadKindReplace);
}

// ─────────────────────────────────────────────────────────────────────────────
//  applyTrimToCurrentSample
//  Stores the trim region and dispatches CmdApplyTrim so the audio thread
//  can react (e.g. clamp slice boundaries, adjust waveform display start).
//  intParam1 = trimStart (samples), intParam2 = trimEnd (samples).
// ─────────────────────────────────────────────────────────────────────────────
void DysektProcessor::applyTrimToCurrentSample (int trimStart, int trimEnd)
{
    const int total = sampleData.getNumFrames();
    trimStart = juce::jlimit (0, juce::jmax (0, total - 1), trimStart);
    trimEnd   = juce::jlimit (trimStart + 1, total, trimEnd);

    trimRegionStart.store (trimStart, std::memory_order_relaxed);
    trimRegionEnd  .store (trimEnd,   std::memory_order_relaxed);

    Command c;
    c.type      = CmdApplyTrim;
    c.intParam1 = trimStart;
    c.intParam2 = trimEnd;
    pushCommand (c);
}

void DysektProcessor::loadSoundFontAsync (const juce::File& file, SoundFontLoadTarget target,
                                           int presetBank, int presetProgram)
{
#if DYSEKT_HAS_SFIZZ
    // Delegate to SoundFontLoader which uses sfizz to render all active notes
    // into a single stereo buffer and posts the result back via
    // completedLoadData (Slicer target) or completedLoadData2 (SFZ-PLAYER
    // preview target) depending on `target`. presetBank/presetProgram are
    // only meaningful for SfPlayer — see SoundFontLoader::load.
    SoundFontLoader loader (*this);
    loader.load (file, target, presetBank, presetProgram);
#else
    // sfizz is not linked — SF2/SFZ files cannot be decoded.
    if (target == SoundFontLoadTarget::Slicer)
    {
        // Post a failure result so the UI shows the normal "failed to load" state
        // rather than silently doing nothing.
        const int token = nextLoadToken.fetch_add (1, std::memory_order_relaxed) + 1;
        latestLoadToken.store (token, std::memory_order_release);
        latestLoadKind.store  ((int) LoadKindReplace, std::memory_order_release);

        delete completedLoadData.exchange    (nullptr, std::memory_order_acq_rel);
        delete completedLoadFailure.exchange (nullptr, std::memory_order_acq_rel);

        auto* payload  = new FailedLoadResult();
        payload->token = token;
        payload->kind  = LoadKindReplace;
        payload->file  = file;
        delete completedLoadFailure.exchange (payload, std::memory_order_acq_rel);
    }
    else
    {
        // SFZ-PLAYER / SF2-PLAYER previews are visual-only and have no
        // failure UI of their own — just discard any stale preview payload
        // and silently no-op.
        delete completedLoadData2.exchange (nullptr, std::memory_order_acq_rel);
        delete completedLoadData3.exchange (nullptr, std::memory_order_acq_rel);
    }
#endif
}

void DysektProcessor::relinkFileAsync (const juce::File& file)
{
    requestSampleLoad (file, LoadKindRelink);
}

void DysektProcessor::clearVoicesBeforeSampleSwap()
{
    // Stop lazy chop before killing voices; its preview voice and buffer pointer
    // must be torn down before the sample data is replaced.
    lazyChop.stop (voicePool, sliceManager);

    // Kill all active voices before replacing the sample buffer
    // to prevent dangling reads from stretcher pipelines.
    for (int vi = 0; vi < VoicePool::kMaxVoices; ++vi)
    {
        auto& v = voicePool.getVoice (vi);
        v.active = false;
        voicePool.voicePositions[vi].store (0.0f,
            vi == VoicePool::kPreviewVoiceIndex
                ? std::memory_order_release
                : std::memory_order_relaxed);
    }
}

void DysektProcessor::clearVoicesBeforeSampleSwap2()
{
    // SFZ-PLAYER equivalent of clearVoicesBeforeSampleSwap() — no Lazy Chop
    // teardown needed since the SFZ-PLAYER engine never supports manual
    // slicing/lazy-chop in the first place.
    for (int vi = 0; vi < VoicePool::kMaxVoices; ++vi)
    {
        auto& v = voicePool2.getVoice (vi);
        v.active = false;
        voicePool2.voicePositions[vi].store (0.0f,
            vi == VoicePool::kPreviewVoiceIndex
                ? std::memory_order_release
                : std::memory_order_relaxed);
    }
}

void DysektProcessor::clampSlicesToSampleBounds()
{
    const int maxLen = sampleData.getNumFrames();
    if (maxLen <= 1)
        return;

    const int numSlices = sliceManager.getNumSlices();
    for (int i = 0; i < numSlices; ++i)
    {
        auto& s = sliceManager.getSlice (i);
        s.startSample = juce::jlimit (0, maxLen - 1, s.startSample);
        // Marker model: endSample derived from next marker — no field to clamp.
    }
}

void DysektProcessor::publishUiSliceSnapshot()
{
    const int writeIndex = 1 - uiSliceSnapshotIndex.load (std::memory_order_relaxed);
    auto& snap = uiSliceSnapshots[(size_t) writeIndex];
    auto sampleSnap = sampleData.getSnapshot();
    snap.numSlices = sliceManager.getNumSlices();
    snap.selectedSlice = sliceManager.selectedSlice.load (std::memory_order_relaxed);
    snap.rootNote = sliceManager.rootNote.load (std::memory_order_relaxed);
    snap.sampleLoaded = (sampleSnap != nullptr);
    snap.sampleMissing = sampleMissing.load (std::memory_order_relaxed);
    snap.sampleNumFrames = sampleSnap ? sampleSnap->buffer.getNumSamples() : 0;
    if (sampleSnap != nullptr)
    {
        // Identify the bundled placeholder by its actual on-disk path — the
        // same check already used elsewhere (see PluginEditor's "hasReal"
        // sample checks: snap->filePath.containsIgnoreCase ("DYSEKT_default.wav")) —
        // rather than by matching the display filename. Filename-equality
        // against a generic name like "Empty.wav" (which is only the
        // BinaryData resource name and never the actual on-disk filename —
        // loadDefaultSampleIfNeeded() always writes "DYSEKT_default.wav")
        // is unreliable: any real user sample that happens to be named
        // "Empty.wav", or whose resolved display name comes back empty for
        // any other reason, would be misidentified as the placeholder and
        // hidden by SliceWaveformLcd even though a real sample with real
        // slices is loaded.
        const bool isPlaceholder = sampleSnap->filePath.containsIgnoreCase ("DYSEKT_default.wav");
        const juce::String fn = isPlaceholder ? juce::String() : sampleSnap->fileName;
        fn.copyToUTF8 (snap.sampleFileName, sizeof (snap.sampleFileName));
        snap.isDefaultSample = isPlaceholder;
    }
    else if (snap.sampleMissing && missingFilePath.isNotEmpty())
    {
        juce::String fn = juce::File (missingFilePath).getFileName();
        fn.copyToUTF8 (snap.sampleFileName, sizeof (snap.sampleFileName));
        snap.isDefaultSample = false;
    }
    else if (sampleData.getFileName().isNotEmpty())
    {
        juce::String fn = sampleData.getFileName();
        fn.copyToUTF8 (snap.sampleFileName, sizeof (snap.sampleFileName));
        snap.isDefaultSample = sampleData.getFilePath().containsIgnoreCase ("DYSEKT_default.wav");
    }
    else
    {
        snap.sampleFileName[0] = '\0';
        snap.isDefaultSample = true;
    }

    for (int i = 0; i < SliceManager::kMaxSlices; ++i)
    {
        if (i < snap.numSlices)
        {
            snap.slices[(size_t) i] = sliceManager.getSlice (i);
            snap.sliceEndSamples[i] = sliceManager.getEndForSlice (i, snap.sampleNumFrames);
        }
        else
        {
            snap.slices[(size_t) i].active = false;
            snap.sliceEndSamples[i] = 0;
        }
    }

    uiSliceSnapshotIndex.store (writeIndex, std::memory_order_release);
    uiSnapshotVersion.fetch_add (1, std::memory_order_release);
    uiSnapshotDirty.store (false, std::memory_order_release);

    // Keep sliceStart / sliceEnd APVTS params in sync with the selected slice
    // so hosts can map them to Quick Controls and MIDI CC.
    if (sliceStartParam != nullptr && sliceEndParam != nullptr)
    {
        const int sel     = snap.selectedSlice;
        const int total   = snap.sampleNumFrames;
        if (sel >= 0 && sel < snap.numSlices && total > 0)
        {
            const auto& sl = snap.slices[(size_t) sel];
            const float pubStart = (float) sl.startSample / (float) total;
            const float pubEnd   = (float) sliceManager.getEndForSlice (sel, total) / (float) total;
            sliceStartParam->store (pubStart, std::memory_order_relaxed);
            sliceEndParam->store   (pubEnd,   std::memory_order_relaxed);
            sliceStartPublished.store (pubStart, std::memory_order_relaxed);
            sliceEndPublished.store   (pubEnd,   std::memory_order_relaxed);
            paramsSyncedForSlice.store (sel, std::memory_order_relaxed);
        }
    }
}

void DysektProcessor::publishUiSliceSnapshot2()
{
    const int writeIndex = 1 - uiSliceSnapshotIndex2.load (std::memory_order_relaxed);
    auto& snap = uiSliceSnapshots2[(size_t) writeIndex];
    auto sampleSnap = sampleData2.getSnapshot();
    snap.numSlices = sliceManager2.getNumSlices();
    snap.selectedSlice = sliceManager2.selectedSlice.load (std::memory_order_relaxed);
    snap.rootNote = sliceManager2.rootNote.load (std::memory_order_relaxed);
    snap.sampleLoaded = (sampleSnap != nullptr);
    snap.sampleMissing = false;   // SFZ-PLAYER has no separate missing-file state
    snap.sampleNumFrames = sampleSnap ? sampleSnap->buffer.getNumSamples() : 0;
    if (sampleSnap != nullptr)
    {
        // Engine 2 (SFZ-PLAYER) never loads a bundled placeholder sample —
        // there is no "DYSEKT_default.wav" equivalent for sampleData2 — so
        // a successfully loaded sample is never the "default" here. Unlike
        // the Slicer's publishUiSliceSnapshot(), this must NOT fall back to
        // "isEmpty() on the resolved name", which would wrongly blank
        // SliceWaveformLcd for any real render/load whose display name
        // happens to come back empty.
        const juce::String fn = sampleData2.getFileName();
        fn.copyToUTF8 (snap.sampleFileName, sizeof (snap.sampleFileName));
        snap.isDefaultSample = false;
    }
    else
    {
        snap.sampleFileName[0] = '\0';
        snap.isDefaultSample = true;
    }

    for (int i = 0; i < SliceManager::kMaxSlices; ++i)
    {
        if (i < snap.numSlices)
        {
            snap.slices[(size_t) i] = sliceManager2.getSlice (i);
            snap.sliceEndSamples[i] = sliceManager2.getEndForSlice (i, snap.sampleNumFrames);
        }
        else
        {
            snap.slices[(size_t) i].active = false;
            snap.sliceEndSamples[i] = 0;
        }
    }

    uiSliceSnapshotIndex2.store (writeIndex, std::memory_order_release);
    uiSnapshotVersion2.fetch_add (1, std::memory_order_release);

    // No APVTS quick-control sync here — sliceStartParam/sliceEndParam map
    // to the Slicer's selected slice only; SFZ-PLAYER's slices are never
    // user-editable, so there's nothing for a host's Quick Controls to
    // bind to on this engine.
}

void DysektProcessor::rebuildChromaticChannelMask()
{
    // Build a bitmask of all channels currently assigned to chromatic slices.
    // Bit N (1-based, N in 1..16) is set when at least one slice uses channel N.
    uint32_t mask = 0u;
    const int n = sliceManager.getNumSlices();
    for (int i = 0; i < n; ++i)
    {
        const int ch = sliceManager.getSlice (i).chromaticChannel;
        if (ch >= 1 && ch <= 16)
            mask |= (1u << ch);
    }
    chromaticSliceChannelMask.store (mask, std::memory_order_relaxed);

    // Evict any chromatic-owned channels from the SF player masks so the two
    // channel pools remain mutually exclusive.  Both the live routing mask and
    // the saved mask (which survives Slicer-mode zeroing) are scrubbed.
    if (mask != 0u)
    {
        const uint32_t evict = ~mask;
        sfPlayerChannelMask.store (
            sfPlayerChannelMask.load (std::memory_order_relaxed) & evict,
            std::memory_order_relaxed);
        savedSfPlayerChannelMask.store (
            savedSfPlayerChannelMask.load (std::memory_order_relaxed) & evict,
            std::memory_order_relaxed);
        sfzPlayer2ChannelMask.store (
            sfzPlayer2ChannelMask.load (std::memory_order_relaxed) & evict,
            std::memory_order_relaxed);
        savedSfzPlayer2ChannelMask.store (
            savedSfzPlayer2ChannelMask.load (std::memory_order_relaxed) & evict,
            std::memory_order_relaxed);
    }
}

void DysektProcessor::setMidiRouteMode (MidiRouteMode mode)
{
    // Called on the message thread whenever the UI tab changes focus.
    //
    // midiRouteMode is a UI-display hint ONLY (SliceWaveformLcd::isSfPlayerMode,
    // SliceControlBar::isSfzPlayer2Mode, etc.) — it does NOT gate live MIDI
    // routing. Slicer (ch 1), SF-Player, and SFZ-Player2 each listen
    // concurrently on whatever channels they've been assigned, regardless of
    // which tab currently has UI focus — see processMidi()/processMidi2(),
    // which already route purely by channel ownership (sfPlayerChannelMask /
    // sfzPlayer2ChannelMask), independent of mode.
    //
    // sfPlayerChannelMask / sfzPlayer2ChannelMask are owned and toggled by
    // the player dropdown panels (enabled when a file is loaded / a channel
    // range is configured — see SfzDropdownPanel, SfzModulePanel,
    // SfzPlayerDropdownPanel). A tab switch must never zero them out from
    // under the user, or the player silently stops responding to live MIDI
    // the moment focus leaves its tab.
    midiRouteMode.store (static_cast<int> (mode), std::memory_order_relaxed);

#if DYSEKT_STANDALONE
    // Standalone sequencer only: which track's live MIDI preview is
    // "selected" is a distinct, standalone-only concept from plugin channel
    // routing above — keep that tied to tab focus as before.
    switch (mode)
    {
        case MidiRouteMode::Slicer:
        case MidiRouteMode::SfzPlayer2:
            sequencer.setSelectedSfLiveChannels (0);
            break;
        case MidiRouteMode::SfPlayer:
            sequencer.setSelectedSfLiveChannels (sequencer.getAllSfPlayerChannelMask());
            break;
        case MidiRouteMode::Sequencer:
            // Live-input mask is managed by ArrangeView / setSelectedSfLiveChannels();
            // leave it unchanged here so the currently-selected track keeps focus.
            break;
    }
#else
    (void) mode;
#endif
}

void DysektProcessor::pushCommand (Command cmd)
{
    const bool critical = isCriticalCommand (cmd.type);
    const auto scope = commandFifo.write (1);
    if (scope.blockSize1 > 0)
    {
        commandBuffer[(size_t) scope.startIndex1] = std::move (cmd);
        uiSnapshotDirty.store (true, std::memory_order_release);
        return;
    }
    if (scope.blockSize2 > 0)
    {
        commandBuffer[(size_t) scope.startIndex2] = std::move (cmd);
        uiSnapshotDirty.store (true, std::memory_order_release);
        return;
    }

    if (enqueueCoalescedCommand (cmd))
    {
        uiSnapshotDirty.store (true, std::memory_order_release);
        return;
    }

    if (critical && enqueueOverflowCommand (std::move (cmd)))
    {
        uiSnapshotDirty.store (true, std::memory_order_release);
        return;
    }

    droppedCommandCount.fetch_add (1, std::memory_order_relaxed);
    droppedCommandTotal.fetch_add (1, std::memory_order_relaxed);
    if (critical)
        droppedCriticalCommandTotal.fetch_add (1, std::memory_order_relaxed);
}

bool DysektProcessor::enqueueOverflowCommand (Command cmd)
{
    const int write = overflowWriteIndex.load (std::memory_order_relaxed);
    const int read = overflowReadIndex.load (std::memory_order_acquire);
    const int next = (write + 1) % kOverflowFifoSize;
    if (next == read)
        return false;

    overflowCommandBuffer[(size_t) write] = std::move (cmd);
    overflowWriteIndex.store (next, std::memory_order_release);
    return true;
}

void DysektProcessor::drainOverflowCommands (bool& handledAny)
{
    for (;;)
    {
        const int read = overflowReadIndex.load (std::memory_order_relaxed);
        const int write = overflowWriteIndex.load (std::memory_order_acquire);
        if (read == write)
            break;

        handleCommand (overflowCommandBuffer[(size_t) read]);
        overflowReadIndex.store ((read + 1) % kOverflowFifoSize, std::memory_order_release);
        handledAny = true;
    }
}

bool DysektProcessor::enqueueCoalescedCommand (const Command& cmd)
{
    if (! isCoalescableCommand (cmd.type))
        return false;

    if (cmd.type == CmdSetSliceParam)
    {
        pendingSetSliceParamField.store (cmd.intParam1, std::memory_order_relaxed);
        pendingSetSliceParamValue.store (cmd.floatParam1, std::memory_order_relaxed);
        pendingSetSliceParamSkipLock.store (cmd.intParam2, std::memory_order_relaxed);
        pendingSetSliceParam.store (true, std::memory_order_release);
        return true;
    }

    if (cmd.type == CmdSetSliceBounds)
    {
        const int end = cmd.numPositions > 0 ? cmd.positions[0] : (int) cmd.floatParam1;
        pendingSetSliceBoundsIdx.store (cmd.intParam1, std::memory_order_relaxed);
        pendingSetSliceBoundsStart.store (cmd.intParam2, std::memory_order_relaxed);
        pendingSetSliceBoundsEnd.store (end, std::memory_order_relaxed);
        pendingSetSliceBounds.store (true, std::memory_order_release);
        return true;
    }

    return false;
}

void DysektProcessor::drainCoalescedCommands (bool& handledAny)
{
    if (pendingSetSliceBounds.exchange (false, std::memory_order_acq_rel))
    {
        Command cmd;
        cmd.type = CmdSetSliceBounds;
        cmd.intParam1 = pendingSetSliceBoundsIdx.load (std::memory_order_relaxed);
        cmd.intParam2 = pendingSetSliceBoundsStart.load (std::memory_order_relaxed);
        cmd.positions[0] = pendingSetSliceBoundsEnd.load (std::memory_order_relaxed);
        cmd.numPositions = 1;
        handleCommand (cmd);
        handledAny = true;
    }

    if (pendingSetSliceParam.exchange (false, std::memory_order_acq_rel))
    {
        Command cmd;
        cmd.type = CmdSetSliceParam;
        cmd.intParam1 = pendingSetSliceParamField.load (std::memory_order_relaxed);
        cmd.floatParam1 = pendingSetSliceParamValue.load (std::memory_order_relaxed);
        cmd.intParam2 = pendingSetSliceParamSkipLock.load (std::memory_order_relaxed);
        handleCommand (cmd);
        handledAny = true;
    }
}

void DysektProcessor::drainCommands()
{
    bool handledAny = false;

    drainOverflowCommands (handledAny);

    const auto scope = commandFifo.read (commandFifo.getNumReady());

    for (int i = 0; i < scope.blockSize1; ++i)
        handleCommand (commandBuffer[(size_t) (scope.startIndex1 + i)]);
    for (int i = 0; i < scope.blockSize2; ++i)
        handleCommand (commandBuffer[(size_t) (scope.startIndex2 + i)]);

    if (scope.blockSize1 + scope.blockSize2 > 0)
        handledAny = true;

    drainCoalescedCommands (handledAny);

    if (handledAny)
        uiSnapshotDirty.store (true, std::memory_order_release);

    const auto dropped = droppedCommandCount.exchange (0, std::memory_order_relaxed);
    if (handledAny || dropped > 0)
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));

    // Apply live drag bounds every block so note-ons during edge/move drag use
    // the current preview position. No snapshot — undo is handled by the
    // CmdBeginGesture + CmdSetSliceBounds pair sent on mouseDown/mouseUp.
    const int liveIdx = liveDragSliceIdx.load (std::memory_order_acquire);
    if (liveIdx >= 0 && liveIdx < sliceManager.getNumSlices())
    {
        const int maxLen = sampleData.getNumFrames();
        if (maxLen > 1)
        {
            auto& s = sliceManager.getSlice (liveIdx);
            int start = liveDragBoundsStart.load (std::memory_order_relaxed);
            int end   = liveDragBoundsEnd.load   (std::memory_order_relaxed);
            start = juce::jlimit (0, juce::jmax (0, maxLen - 1), start);
            end   = juce::jlimit (start + 1, juce::jmax (start + 1, maxLen), end);
            if (end - start < 64)
                end = juce::jmin (maxLen, start + 64);
            s.startSample = start;
            // Marker model: move next slice's start to represent this slice's new end.
            if (liveIdx + 1 < sliceManager.getNumSlices())
                sliceManager.getSlice (liveIdx + 1).startSample = end;
        }
    }
}

UndoManager::Snapshot DysektProcessor::makeSnapshot()
{
    UndoManager::Snapshot snap;
    for (int i = 0; i < SliceManager::kMaxSlices; ++i)
        snap.slices[(size_t) i] = sliceManager.getSlice (i);
    snap.numSlices = sliceManager.getNumSlices();
    snap.selectedSlice = sliceManager.selectedSlice;
    snap.rootNote = sliceManager.rootNote.load();
    snap.apvtsState = apvts.copyState();
    snap.midiSelectsSlice = midiSelectsSlice.load();
    return snap;
}

void DysektProcessor::captureSnapshot()
{
    undoMgr.push (makeSnapshot());
}

void DysektProcessor::restoreSnapshot (const UndoManager::Snapshot& snap)
{
    for (int i = 0; i < SliceManager::kMaxSlices; ++i)
        sliceManager.getSlice (i) = snap.slices[(size_t) i];
    sliceManager.setNumSlices (snap.numSlices);
    sliceManager.selectedSlice = snap.selectedSlice;
    sliceManager.rootNote.store (snap.rootNote);
    apvts.replaceState (snap.apvtsState);
    midiSelectsSlice.store (snap.midiSelectsSlice);
    sliceManager.rebuildMidiMap();
    uiSnapshotDirty.store (true, std::memory_order_release);
}

void DysektProcessor::handleCommand (const Command& cmd)
{
    switch (cmd.type)
    {
        case CmdBeginGesture:
            // SFZ-PLAYER (targetEngine2): no undo/redo support yet for this
            // engine — see CmdSetSliceParam's identical guard below for the
            // rationale. Without this, every SFZ-PLAYER knob-drag would push
            // a useless Slicer-state snapshot that Ctrl+Z would "restore"
            // without actually undoing the SFZ-PLAYER edit that triggered it.
            if (! cmd.targetEngine2)
            {
                if (! gestureSnapshotCaptured)
                    captureSnapshot();
                gestureSnapshotCaptured = true;
                blocksSinceGestureActivity = 0;
            }
            break;

        case CmdSetSliceParam:
            // SFZ-PLAYER (targetEngine2): no undo/redo support yet for this
            // engine — skip the capture entirely rather than push a useless
            // Slicer-state snapshot that Ctrl+Z would "restore" without
            // actually undoing the SFZ-PLAYER edit that triggered it.
            if (! cmd.targetEngine2)
            {
                if (! gestureSnapshotCaptured)
                    captureSnapshot();
                gestureSnapshotCaptured = true;
                blocksSinceGestureActivity = 0;
            }
            break;

        // Drag-style commands: keep gesture lock open while the drag continues.
        // The 2-block idle timeout in processBlock() will release it automatically
        // once the user stops, collapsing the whole drag into one undo step.
        case CmdSetSliceBounds:
            if (! gestureSnapshotCaptured)
                captureSnapshot();
            gestureSnapshotCaptured = true;   // ← stay locked during drag
            blocksSinceGestureActivity = 0;
            break;

        // Discrete, atomic operations: capture once then immediately unlock so
        // each operation gets its own undo step.
        case CmdCreateSlice:
        case CmdDeleteSlice:
        case CmdStretch:
        case CmdSplitSlice:
        case CmdTransientChop:
        case CmdEqualChop:
            if (! gestureSnapshotCaptured)
                captureSnapshot();
            gestureSnapshotCaptured = false;
            blocksSinceGestureActivity = 0;
            break;

        case CmdToggleLock:
            // SFZ-PLAYER (targetEngine2): no undo/redo support yet — see
            // CmdSetSliceParam's identical guard above for the rationale.
            if (! cmd.targetEngine2)
            {
                if (! gestureSnapshotCaptured)
                    captureSnapshot();
                gestureSnapshotCaptured = false;
                blocksSinceGestureActivity = 0;
            }
            break;

        // State-mutating commands that previously fell through to default without
        // capturing a snapshot — each of these must be undoable.
        case CmdApplyTrim:
        case CmdSetRootNote:
        case CmdSetSliceColour:
        case CmdSetSliceName:
        case CmdSetSliceLockAll:
            if (! gestureSnapshotCaptured)
                captureSnapshot();
            gestureSnapshotCaptured = false;
            blocksSinceGestureActivity = 0;
            break;

        default:
            // Non-mutating commands (select, load callbacks, panic, etc.).
            // Just release any open gesture window.
            gestureSnapshotCaptured = false;
            break;
    }

    switch (cmd.type)
    {
        case CmdLoadFile:
            loadFileAsync (cmd.fileParam);
            break;

        case CmdCreateSlice:
            sliceManager.createSlice (cmd.intParam1, cmd.intParam2);
            break;

        case CmdDeleteSlice:
            sliceManager.deleteSlice (cmd.intParam1);
            break;

        case CmdLazyChopStart:
            if (sampleData.isLoaded())
            {
                PreviewStretchParams psp;
                psp.stretchEnabled = stretchParam->load() > 0.5f;
                psp.algorithm      = (int) algoParam->load();
                psp.bpm            = bpmParam->load();
                psp.pitch          = pitchParam->load();
                psp.dawBpm         = dawBpm.load();
                psp.tonality       = tonalityParam->load();
                psp.formant        = formantParam->load();
                psp.formantComp    = formantCompParam->load() > 0.5f;
                psp.grainMode      = (int) grainModeParam->load();
                psp.sampleRate     = currentSampleRate;
                psp.sample         = &sampleData;
                lazyChop.start (sampleData.getNumFrames(), sliceManager, psp,
                                true /*snap always on*/, &sampleData.getBuffer());
            }
            break;

        case CmdLazyChopStop:
            lazyChop.stop (voicePool, sliceManager);
            break;

        case CmdStretch:
        {
            int sel = sliceManager.selectedSlice;
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                auto& s = sliceManager.getSlice (sel);
                const int stretchSliceEnd = sliceManager.getEndForSlice (sel, sampleData.getNumFrames());
                float newBpm = GrainEngine::calcStretchBpm (
                    s.startSample, stretchSliceEnd, cmd.floatParam1, currentSampleRate);
                s.bpm = newBpm;
                s.lockMask |= kLockBpm;
                s.algorithm = 1;
                s.lockMask |= kLockAlgorithm;
            }
            break;
        }

        case CmdToggleLock:
        {
            // intParam1 = explicit slice index (>= 0), or -1 to use selectedSlice.
            // intParam2 = lock bit to toggle.
            // targetEngine2: SFZ-PLAYER only ever sends ADSR lock bits (kLockAttack/
            // Decay/Sustain/Release), all of which are no-ops in the global-default
            // fallback below ("skipLock=1 drags keep it current") — so the elaborate
            // per-bit APVTS-fallback logic further down never actually executes
            // differently for engine 2; only the SliceManager& resolution matters here.
            SliceManager& sm = cmd.targetEngine2 ? sliceManager2 : sliceManager;
            int sel = (cmd.intParam1 >= 0) ? cmd.intParam1 : (int) sm.selectedSlice;
            if (sel >= 0 && sel < sm.getNumSlices())
            {
                auto& s = sm.getSlice (sel);
                uint32_t bit = (uint32_t) cmd.intParam2;
                bool turningOn = !(s.lockMask & bit);

                if (!turningOn)
                {
                    // UNLOCK PATH: discard any in-flight coalesced CmdSetSliceParam.
                    // Without this, a stale drag value sitting in the coalescer fires
                    // *after* the lock bit clears and overwrites s.attackSec etc. with
                    // whatever the last drag position happened to be — causing the node
                    // to jump to an init/arbitrary value on unlock.
                    pendingSetSliceParam.store (false, std::memory_order_release);
                }

                if (turningOn)
                {
                    // For each field: use the slice's own value if it was already locked
                    // (meaning it was explicitly set), otherwise fall back to the current
                    // global APVTS default.  This is the same logic the SCB dragStartValue
                    // switch uses so all three surfaces (WaveformView, SCB, SliceWaveformLcd)
                    // lock identically.
                    if (bit == kLockBpm)
                        s.bpm = (s.lockMask & kLockBpm) ? s.bpm : bpmParam->load();
                    else if (bit == kLockPitch)
                        s.pitchSemitones = (s.lockMask & kLockPitch) ? s.pitchSemitones : pitchParam->load();
                    else if (bit == kLockAlgorithm)
                        s.algorithm = (s.lockMask & kLockAlgorithm) ? s.algorithm : (int) algoParam->load();
                    else if (bit == kLockAttack)  { /* s.attackSec unchanged — skipLock=1 drags keep it current */ }
                    else if (bit == kLockHold)    { /* s.holdSec unchanged */ }
                    else if (bit == kLockDecay)   { /* s.decaySec unchanged */ }
                    else if (bit == kLockSustain) { /* s.sustainLevel unchanged */ }
                    else if (bit == kLockRelease) { /* s.releaseSec unchanged */ }
                    else if (bit == kLockMuteGroup)
                        s.muteGroup = (s.lockMask & kLockMuteGroup) ? s.muteGroup : (int) muteGroupParam->load();
                    else if (bit == kLockLoop)
                        s.loopMode = (s.lockMask & kLockLoop) ? s.loopMode : (int) loopParam->load();
                    else if (bit == kLockStretch)
                        s.stretchEnabled = (s.lockMask & kLockStretch) ? s.stretchEnabled : stretchParam->load() > 0.5f;
                    else if (bit == kLockReleaseTail)
                        s.releaseTail = (s.lockMask & kLockReleaseTail) ? s.releaseTail : releaseTailParam->load() > 0.5f;
                    else if (bit == kLockReverse)
                        s.reverse = (s.lockMask & kLockReverse) ? s.reverse : reverseParam->load() > 0.5f;
                    else if (bit == kLockOneShot)
                        s.oneShot = (s.lockMask & kLockOneShot) ? s.oneShot : false;
                    else if (bit == kLockCentsDetune)
                        s.centsDetune = (s.lockMask & kLockCentsDetune) ? s.centsDetune : centsDetuneParam->load();
                    else if (bit == kLockTonality)
                        s.tonalityHz = (s.lockMask & kLockTonality) ? s.tonalityHz : tonalityParam->load();
                    else if (bit == kLockFormant)
                        s.formantSemitones = (s.lockMask & kLockFormant) ? s.formantSemitones : formantParam->load();
                    else if (bit == kLockFormantComp)
                        s.formantComp = (s.lockMask & kLockFormantComp) ? s.formantComp : formantCompParam->load() > 0.5f;
                    else if (bit == kLockGrainMode)
                        s.grainMode = (s.lockMask & kLockGrainMode) ? s.grainMode : (int) grainModeParam->load();
                    else if (bit == kLockVolume)
                        s.volume = (s.lockMask & kLockVolume) ? s.volume : masterVolParam->load();
                    else if (bit == kLockPan)
                        s.pan = (s.lockMask & kLockPan) ? s.pan : panParam->load();
                    else if (bit == kLockFilter)
                    {
                        s.filterCutoff = (s.lockMask & kLockFilter) ? s.filterCutoff : filterCutoffParam->load();
                        s.filterRes    = (s.lockMask & kLockFilter) ? s.filterRes    : filterResParam->load();
                    }
                    else if (bit == kLockChromaticChannel)
                        s.chromaticChannel = (s.lockMask & kLockChromaticChannel) ? s.chromaticChannel : 0;
                    else if (bit == kLockChromaticLegato)
                        s.chromaticLegato = (s.lockMask & kLockChromaticLegato) ? s.chromaticLegato : false;
                    // kLockOutputBus: no global default — preserve slice value or 0
                }

                s.lockMask ^= bit;
            }
            uiSnapshotDirty.store (true, std::memory_order_release);
            break;
        }

        case CmdSetSliceLockAll:
        {
            // intParam1 = slice index, floatParam1 = 1.0 lock all / 0.0 unlock all
            int idx = cmd.intParam1;
            if (idx >= 0 && idx < sliceManager.getNumSlices())
            {
                auto& s = sliceManager.getSlice (idx);
                if (cmd.floatParam1 > 0.5f)
                {
                    // Lock all — snapshot current effective values first
                    if (!(s.lockMask & kLockBpm))          s.bpm              = bpmParam->load();
                    if (!(s.lockMask & kLockPitch))         s.pitchSemitones   = pitchParam->load();
                    if (!(s.lockMask & kLockAlgorithm))     s.algorithm        = (int) algoParam->load();
                    // ADSR not synced from APVTS — always per-slice
                    if (!(s.lockMask & kLockHold))          s.holdSec          = holdParam->load()         / 1000.0f;
                    if (!(s.lockMask & kLockMuteGroup))     s.muteGroup        = (int) muteGroupParam->load();
                    if (!(s.lockMask & kLockLoop))          s.loopMode         = (int) loopParam->load();
                    if (!(s.lockMask & kLockStretch))       s.stretchEnabled   = stretchParam->load()      > 0.5f;
                    if (!(s.lockMask & kLockReleaseTail))   s.releaseTail      = releaseTailParam->load()  > 0.5f;
                    if (!(s.lockMask & kLockReverse))       s.reverse          = reverseParam->load()      > 0.5f;
                    if (!(s.lockMask & kLockOneShot))       s.oneShot          = false;
                    if (!(s.lockMask & kLockCentsDetune))   s.centsDetune      = centsDetuneParam->load();
                    if (!(s.lockMask & kLockTonality))      s.tonalityHz       = tonalityParam->load();
                    if (!(s.lockMask & kLockFormant))       s.formantSemitones = formantParam->load();
                    if (!(s.lockMask & kLockFormantComp))   s.formantComp      = formantCompParam->load()  > 0.5f;
                    if (!(s.lockMask & kLockGrainMode))     s.grainMode        = (int) grainModeParam->load();
                    if (!(s.lockMask & kLockVolume))        s.volume           = masterVolParam->load();
                    if (!(s.lockMask & kLockPan))           s.pan              = panParam->load();
                    if (!(s.lockMask & kLockFilter))        { s.filterCutoff   = filterCutoffParam->load();
                                                              s.filterRes      = filterResParam->load(); }
                    // kLockChromaticChannel: keep current slice value when locking
                    s.lockMask = 0xFFFFFFFFu;
                }
                else
                {
                    s.lockMask = 0u;  // unlock all
                }
            }
            break;
        }

        case CmdSetSliceParam:
        {
            // SFZ-PLAYER (targetEngine2): only ADSR fields are meaningfully
            // supported, matching the per-zone ADSR editing already in use
            // for SFZ-loaded files in the Slicer. Anything beyond ADSR sent
            // with this flag set is a caller bug, but handled the same way
            // (harmlessly mutates sliceManager2's slice) rather than crashing.
            SliceManager& sm  = cmd.targetEngine2 ? sliceManager2 : sliceManager;
            int sel = sm.selectedSlice;
            if (sel >= 0 && sel < sm.getNumSlices())
            {
                auto& s = sm.getSlice (sel);
                int field = cmd.intParam1;
                float val = cmd.floatParam1;
                // intParam2 == 1: update value only, do NOT set lock bit.
                // Used by SliceWaveformLcd drag (commitNodes) so node dragging
                // updates the parameter live without locking the field.
                // All other callers leave intParam2 == 0 for normal lock behaviour.
                const bool skipLock = (cmd.intParam2 == 1);

                switch (field)
                {
                    case FieldBpm:       s.bpm = val;             if (!skipLock) s.lockMask |= kLockBpm;       break;
                    case FieldPitch:     s.pitchSemitones = val;  if (!skipLock) s.lockMask |= kLockPitch;     break;
                    case FieldAlgorithm: s.algorithm = (int) val; if (!skipLock) s.lockMask |= kLockAlgorithm; break;
                    case FieldAttack:    s.attackSec = val;       if (!skipLock) s.lockMask |= kLockAttack;    break;
                    case FieldHold:      s.holdSec = val;         if (!skipLock) s.lockMask |= kLockHold;      break;
                    case FieldDecay:     s.decaySec = val;        if (!skipLock) s.lockMask |= kLockDecay;     break;
                    case FieldSustain:   s.sustainLevel = val;    if (!skipLock) s.lockMask |= kLockSustain;   break;
                    case FieldRelease:   s.releaseSec = val;      if (!skipLock) s.lockMask |= kLockRelease;   break;
                    case FieldMuteGroup: s.muteGroup = (int) val; if (!skipLock) s.lockMask |= kLockMuteGroup; break;
                    case FieldGlobalMono:
                        // Global param — write directly to APVTS, not per-slice
                        if (auto* p2 = apvts.getParameter (ParamIds::globalMono))
                            p2->setValueNotifyingHost (val > 0.5f ? 1.0f : 0.0f);
                        break;
                    case FieldStretchEnabled: s.stretchEnabled = val > 0.5f; if (!skipLock) s.lockMask |= kLockStretch; break;
                    case FieldTonality:  s.tonalityHz = val;        if (!skipLock) s.lockMask |= kLockTonality;    break;
                    case FieldFormant:   s.formantSemitones = val;   if (!skipLock) s.lockMask |= kLockFormant;     break;
                    case FieldFormantComp: s.formantComp = val > 0.5f; if (!skipLock) s.lockMask |= kLockFormantComp; break;
                    case FieldGrainMode:  s.grainMode = (int) val;   if (!skipLock) s.lockMask |= kLockGrainMode;  break;
                    case FieldVolume:     s.volume = val;            if (!skipLock) s.lockMask |= kLockVolume;    break;
                    case FieldReleaseTail: s.releaseTail = val > 0.5f; if (!skipLock) s.lockMask |= kLockReleaseTail; break;
                    case FieldReverse:    s.reverse = val > 0.5f;    if (!skipLock) s.lockMask |= kLockReverse;    break;
                    case FieldOutputBus:  s.outputBus = juce::jlimit (0, 15, (int) val); if (!skipLock) s.lockMask |= kLockOutputBus; break;
                    case FieldLoop:       s.loopMode = (int) val;    if (!skipLock) s.lockMask |= kLockLoop;      break;
                    case FieldOneShot:    s.oneShot = val > 0.5f;    if (!skipLock) s.lockMask |= kLockOneShot;   break;
                    case FieldCentsDetune:   s.centsDetune    = val;       if (!skipLock) s.lockMask |= kLockCentsDetune; break;
                    case FieldPan:           s.pan            = val;       if (!skipLock) s.lockMask |= kLockPan;         break;
                    case FieldFilterCutoff:  s.filterCutoff   = val;       if (!skipLock) s.lockMask |= kLockFilter;      break;
                    case FieldFilterRes:       s.filterRes       = val;       if (!skipLock) s.lockMask |= kLockFilter;      break;
                    case FieldEqLowGain:       s.eqLowGain       = val;       if (!skipLock) s.lockMask |= kLockEqLow;       break;
                    case FieldEqMidGain:       s.eqMidGain       = val;       if (!skipLock) s.lockMask |= kLockEqMid;       break;
                    case FieldEqMidFreq:       s.eqMidFreq       = val;       if (!skipLock) s.lockMask |= kLockEqMid;       break;
                    case FieldEqMidQ:          s.eqMidQ          = val;       if (!skipLock) s.lockMask |= kLockEqMid;       break;
                    case FieldEqHighGain:      s.eqHighGain      = val;       if (!skipLock) s.lockMask |= kLockEqHigh;      break;
                    case FieldChromaticChannel: s.chromaticChannel = juce::jlimit (0, 16, (int) val); rebuildChromaticChannelMask(); break;
                    case FieldChromaticLegato:  s.chromaticLegato  = (val > 0.5f); break;
                    case FieldMidiNote:
                        // SFZ-PLAYER (targetEngine2): rebuildMidiMap() would
                        // destructively reassign EVERY slice's midiNote
                        // sequentially from rootNote, destroying the real
                        // SFZ key-zone mapping (see pinSliceMidiNote's doc
                        // comment) — no UI currently sends this field for
                        // engine 2, but guard against it regardless.
                        if (! cmd.targetEngine2)
                        {
                            s.midiNote = juce::jlimit (0, 127, (int) val);
                            sm.rebuildMidiMap();
                        }
                        break;

                }
            }
            uiSnapshotDirty.store (true, std::memory_order_release);
            break;
        }

        case CmdSetSliceBounds:
        {
            int idx = cmd.intParam1;
            if (idx >= 0 && idx < sliceManager.getNumSlices())
            {
                const int maxLen = sampleData.getNumFrames();
                if (maxLen <= 1)
                    break;

                auto& s = sliceManager.getSlice (idx);
                int requestedEnd = (cmd.numPositions > 0) ? cmd.positions[0] : (int) cmd.floatParam1;
                int start = juce::jmin (cmd.intParam2, requestedEnd);
                int end = juce::jmax (cmd.intParam2, requestedEnd);
                start = juce::jlimit (0, juce::jmax (0, maxLen - 1), start);
                end = juce::jlimit (start + 1, juce::jmax (start + 1, maxLen), end);
                // Enforce 64-sample minimum by clamping start BACKWARD rather than
                // pushing end forward.  Pushing end writes a new value to slice[idx+1]
                // which corrupts adjacent markers — this was the root cause of the
                // "next slice marker jumps to previous marker's position" bug when
                // switching CC control between adjacent slices.
                if (end - start < 64)
                    start = juce::jmax (0, end - 64);
                const int totalF = sampleData.getNumFrames();
                int oldEnd = sliceManager.getEndForSlice (idx, totalF);
                // Clamp start against the PREVIOUS slice to prevent overlap.
                // Slices are sorted by startSample, so slices[idx-1].startSample
                // is the hard floor for this slice's start.
                end = juce::jmax (end, start + 1);
                end = juce::jmin (end, maxLen);

                // Drag-delete gesture: dragging marker N back until it touches or
                // passes marker N-1 fires an implicit "delete slice N" — identical to
                // right-click → Delete Slice on that marker.  We guard idx > 0 so the
                // sample-anchor (slice 0) is never removed.
                if (cmd.isCommit && idx > 0
                    && start <= sliceManager.getSlice (idx - 1).startSample)
                {
                    sliceManager.deleteSlice (idx);
                    break;
                }

                // Cull any preceding slices that the drag has crushed to zero width.
                // Slice 0 is the sample anchor and is never deleted.
                // Work backwards so each deleteSlice(j) only shifts indices above j.
                //
                // Inherit identity (name + MIDI note) from the immediately adjacent
                // slice (idx-1) — the first slice the marker physically crosses.
                // Only do this on the final mouseUp commit, not during live drag ticks,
                // so the result is deterministic regardless of how many coalesced
                // CmdSetSliceBounds fired during the drag.
                juce::String inheritedName;
                int          inheritedMidiNote = -1;  // -1 = nothing to inherit

                if (cmd.isCommit
                    && idx - 1 > 0
                    && sliceManager.getSlice (idx - 1).startSample >= start)
                {
                    const auto& cand  = sliceManager.getSlice (idx - 1);
                    inheritedName     = cand.name.toUpperCase();
                    inheritedMidiNote = cand.midiNote;
                }

                // Delete all crushed slices (backwards so indices stay valid).
                int cullCount = 0;
                for (int j = idx - 1; j > 0; --j)
                {
                    if (sliceManager.getSlice (j).startSample >= start)
                    {
                        sliceManager.deleteSlice (j);
                        ++cullCount;
                    }
                    else
                        break; // slices are sorted — safe to stop here
                }
                // After culls, target slice has shifted left by cullCount.
                idx -= cullCount;
                if (idx < 0 || idx >= sliceManager.getNumSlices())
                    break;

                auto& sNew = sliceManager.getSlice (idx);
                sNew.startSample = start;

                // Apply inherited name before rebuild (rebuild doesn't touch name).
                if (inheritedMidiNote >= 0)
                    sNew.name = inheritedName;

                // Marker model: end boundary = next slice's start.
                if (idx + 1 < sliceManager.getNumSlices())
                    sliceManager.getSlice (idx + 1).startSample = end;

                sliceManager.rebuildMidiMap();

                // Re-apply inherited MIDI note AFTER rebuildMidiMap() — rebuild
                // unconditionally reassigns notes sequentially and would overwrite it.
                // pinSliceMidiNote patches both the slice field and the lookup map.
                if (inheritedMidiNote >= 0)
                    sliceManager.pinSliceMidiNote (idx, inheritedMidiNote);
            }
            break;
        }

        case CmdSplitSlice:
        {
            int sel = sliceManager.selectedSlice;
            if (sel >= 0 && sel < sliceManager.getNumSlices())
            {
                Slice srcCopy = sliceManager.getSlice (sel);
                int startS = srcCopy.startSample;
                int endS   = sliceManager.getEndForSlice (sel, sampleData.getNumFrames());
                int count = juce::jlimit (2, 128, cmd.intParam1);
                int len = endS - startS;

                sliceManager.deleteSlice (sel);

                bool doSnap = sampleData.isLoaded(); // snap always on
                int firstNew = -1;
                for (int i = 0; i < count; ++i)
                {
                    int s = startS + i * len / count;
                    int e = startS + (i + 1) * len / count;
                    if (doSnap)
                    {
                        if (i > 0)
                            s = AudioAnalysis::findNearestZeroCrossing (sampleData.getBuffer(), s);
                        if (i < count - 1)
                            e = AudioAnalysis::findNearestZeroCrossing (sampleData.getBuffer(), e);
                    }
                    if (e - s < 64) e = s + 64;
                    int idx = sliceManager.createSlice (s, e);
                    if (idx >= 0)
                    {
                        auto& dst = sliceManager.getSlice (idx);
                        int savedNote      = dst.midiNote;  // assigned by createSlice
                        juce::Colour savedColour = dst.colour;  // assigned from palette
                        dst = srcCopy;         // copy all params + lockMask
                        dst.startSample = s;
                        // Marker model: end derived from next marker — no write needed.
                        dst.midiNote    = savedNote;
                        dst.colour      = savedColour;
                        dst.active      = true;
                    }
                    if (i == 0) firstNew = idx;
                }

                sliceManager.rebuildMidiMap();
                if (firstNew >= 0)
                    sliceManager.selectedSlice = firstNew;
            }
            break;
        }

        case CmdTransientChop:
        {
            int sel = sliceManager.selectedSlice;
            if (sel >= 0 && sel < sliceManager.getNumSlices() && cmd.numPositions > 0)
            {
                Slice srcCopy = sliceManager.getSlice (sel);
                int startS = srcCopy.startSample;
                int endS   = sliceManager.getEndForSlice (sel, sampleData.getNumFrames());

                sliceManager.deleteSlice (sel);

                // Build fixed-size boundary list: [startS, ...positions..., endS]
                int bounds[SliceManager::kMaxSlices + 2];
                int numBounds = 0;
                bounds[numBounds++] = startS;
                for (int bi = 0; bi < cmd.numPositions; ++bi)
                    bounds[numBounds++] = cmd.positions[(size_t) bi];
                bounds[numBounds++] = endS;

                int firstNew = -1;
                for (int i = 0; i + 1 < numBounds; ++i)
                {
                    int s = bounds[i];
                    int e = bounds[i + 1];
                    if (e - s < 64) continue;
                    int idx = sliceManager.createSlice (s, e);
                    if (idx >= 0)
                    {
                        auto& dst = sliceManager.getSlice (idx);
                        int savedNote        = dst.midiNote;
                        juce::Colour savedColour = dst.colour;
                        dst = srcCopy;
                        dst.startSample = s;
                        // Marker model: end derived from next marker — no write needed.
                        dst.midiNote    = savedNote;
                        dst.colour      = savedColour;
                        dst.active      = true;
                    }
                    if (firstNew < 0) firstNew = idx;
                }

                sliceManager.rebuildMidiMap();
                if (firstNew >= 0)
                    sliceManager.selectedSlice = firstNew;
            }
            break;
        }

        case CmdEqualChop:
        {
            const int n     = juce::jlimit (2, 32, cmd.intParam1);
            const int total = sampleData.getNumFrames();
            if (total < n * 64) break;   // sample too short for requested count

            // Clear all existing slices
            while (sliceManager.getNumSlices() > 0)
                sliceManager.deleteSlice (0);

            // Create N equal slices across the full sample
            for (int i = 0; i < n; ++i)
            {
                const int s = (int) (((double) i       / n) * total);
                const int e = (int) (((double) (i + 1) / n) * total);
                if (e - s >= 64)
                    sliceManager.createSlice (s, e);
            }

            sliceManager.rebuildMidiMap();
            sliceManager.selectedSlice = 0;
            break;
        }

        case CmdRelinkFile:
            relinkFileAsync (cmd.fileParam);
            break;

        case CmdFileLoadFailed:
            if (cmd.intParam1 == latestLoadToken.load (std::memory_order_acquire)
                && cmd.intParam2 == (int) LoadKindRelink)
            {
                sampleMissing.store (true);
                missingFilePath = cmd.fileParam.getFullPathName();
                sampleData.setFileName (cmd.fileParam.getFileName());
                sampleData.setFilePath (cmd.fileParam.getFullPathName());
                sampleAvailability.store ((int) SampleStateMissingAwaitingRelink,
                                         std::memory_order_relaxed);
            }
            break;

        case CmdUndo:
            if (undoMgr.canUndo())
                restoreSnapshot (undoMgr.undo (makeSnapshot()));
            break;

        case CmdRedo:
            if (undoMgr.canRedo())
                restoreSnapshot (undoMgr.redo());
            break;

        case CmdBeginGesture:
            break;

        case CmdPanic:
            voicePool.killAll();
            lazyChop.stop (voicePool, sliceManager);
            std::fill (std::begin (heldNotes), std::end (heldNotes), false);
            break;

        case CmdSelectSlice:
        {
            const int newSel = juce::jlimit (-1, juce::jmax (-1, sliceManager.getNumSlices() - 1), cmd.intParam1);
            sliceManager.selectedSlice.store (newSel, std::memory_order_relaxed);
            // New slice selected — do NOT reset per-slice CC state.
            // Each slice independently maintains its own pickup and smoother
            // state, so switching slices never triggers pickup re-acquisition.
            markerPending        = false;
            markerPendingSlice   = -1;
            markerIdleCounter    = 0;
            markerSmootherSlice  = -1;
            liveDragSliceIdx.store (-1, std::memory_order_release);

            // Proactive re-seed: park the smoother at the new slice's current
            // start position NOW, before any CC arrives.  When the first CC
            // passes the pickup gate the glide will start from the correct
            // position rather than from wherever the smoother was left.
            if (newSel >= 0 && newSel < sliceManager.getNumSlices())
            {
                ccSmoothers[(size_t) newSel][(size_t) FieldSliceStart].setCurrentAndTargetValue (
                    (float) sliceManager.getSlice (newSel).startSample);
                markerSmootherSlice = newSel;
                // ccSmootherActive stays false — smoother is seeded but not running.
                // It activates only once pickup mode is satisfied.
            }
            break;
        }

        case CmdSetSliceColour:
        {
            int idx = cmd.intParam1;
            if (idx >= 0 && idx < sliceManager.getNumSlices())
                sliceManager.getSlice (idx).colour = juce::Colour ((juce::uint32) (unsigned) cmd.intParam2);
            break;
        }

        case CmdSetSliceName:
        {
            int idx = cmd.intParam1;
            SliceManager& sm = cmd.targetEngine2 ? sliceManager2 : sliceManager;
            if (idx >= 0 && idx < sm.getNumSlices())
                sm.getSlice (idx).name = cmd.stringParam.toUpperCase();
            break;
        }

        case CmdSetRootNote:
            sliceManager.rootNote.store (juce::jlimit (0, 127, cmd.intParam1),
                                         std::memory_order_relaxed);
            break;

        case CmdApplyTrim:
            // 1. Physically crop the audio buffer to [trimStart, trimEnd)
            // 2. Clear all slices — trimmed sample enters slice window clean,
            //    playing chromatically until user adds first slice (same as fresh load).
            {
                const int tStart = cmd.intParam1;
                const int tEnd   = cmd.intParam2;

                auto snap = sampleData.getSnapshot();
                if (snap != nullptr)
                {
                    auto trimmed = SampleData::createTrimmed (*snap, tStart, tEnd);
                    if (trimmed != nullptr)
                        sampleData.applyDecodedSample (std::move (trimmed));
                }

                const int totalFrames = sampleData.getNumFrames();
                sliceManager.clearAll();
                sliceManager.selectedSlice.store (-1, std::memory_order_relaxed);
                (void) totalFrames;
            }
            publishUiSliceSnapshot();
            break;

        case CmdNone:
            break;
    }
}

void DysektProcessor::processMidi (const juce::MidiBuffer& midi)
{
    // ── MIDI channel routing ──────────────────────────────────────────────────
    // Messages on channels owned by sfPlayerChannelMask are routed exclusively
    // to sfzPlayer (SF2-PLAYER), and messages on channels owned by
    // sfzPlayer2ChannelMask are routed exclusively to sfzPlayer2 (SFZ-PLAYER) —
    // neither should trigger slices, lazy-chop, or MIDI learn here. Both masks
    // are zero only when their respective engine has never been enabled (no
    // file loaded / no channel range configured) — NOT based on which tab has
    // UI focus, so all engines keep listening concurrently regardless of tab.
    const uint32_t sfMask  = sfPlayerChannelMask.load  (std::memory_order_relaxed);
    const uint32_t sfz2Mask = sfzPlayer2ChannelMask.load (std::memory_order_relaxed);

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();

        // Skip messages on SF-player- or SFZ-player-owned channels — they
        // belong to one of the DY-SFP engines, not the slicer.
        if (sfMask != 0 || sfz2Mask != 0)
        {
            const int ch = msg.getChannel();   // 1-based
            if (ch >= 1 && ch <= 16 && (((sfMask & (1u << ch)) != 0) || ((sfz2Mask & (1u << ch)) != 0)))
                continue;
        }

        // ── MIDI Learn CC dispatch ────────────────────────────────────
        if (msg.isController())
        {
            int   outFieldId   = -1;
            float outNorm      = 0.0f;
            bool  outIsRelative = false;
            const int prevArmed = midiLearn.getArmedSlot();
            if (midiLearn.processCc (msg.getControllerNumber(),
                                     msg.getControllerValue(),
                                     msg.getChannel(),
                                     outFieldId, outNorm, outIsRelative))
            {
                const int sel = sliceManager.selectedSlice.load (std::memory_order_relaxed);

                // ── Trim region CC: runs regardless of slice count ──────────
                // FieldSliceStart / FieldTrimOut are hardwired to trim in/out.
                // This block executes BEFORE the slice guard so it works in trim
                // mode even when there are zero slices.
                if (outFieldId == FieldSliceStart || outFieldId == FieldTrimOut)
                {
                    const int total = sampleData.getNumFrames();
                    if (total > 1)
                    {
                        static constexpr float kEndlessSamplesPerStep = 1.0f / 512.0f;
                        const int stepSamples = juce::jmax (1, (int) (total * kEndlessSamplesPerStep));

                        const int curStart = trimRegionStart.load (std::memory_order_relaxed);
                        const int curEnd   = trimRegionEnd  .load (std::memory_order_relaxed);

                        if (outIsRelative)
                        {
                            // Relative: immediate delta — inherently smooth (small steps)
                            if (outFieldId == FieldSliceStart)
                            {
                                const int next = juce::jlimit (0, curEnd - 64,
                                    curStart + (int)(outNorm * stepSamples));
                                trimRegionStart.store (next, std::memory_order_relaxed);
                            }
                            else
                            {
                                const int next = juce::jlimit (curStart + 64, total,
                                    curEnd + (int)(outNorm * stepSamples));
                                trimRegionEnd.store (next, std::memory_order_relaxed);
                            }
                            uiSnapshotDirty.store (true, std::memory_order_release);
                        }
                        else if (trimModeActive.load (std::memory_order_relaxed))
                        {
                            // Absolute in trim mode only: seed from trim atomics and arm
                            // the smoother. processBlock() steps it and writes to the trim
                            // atomics each buffer.
                            // In non-trim mode we deliberately fall through without arming
                            // the smoother — the slice path below seeds from sl.startSample,
                            // preventing a jump to the last trim-in/out position.
                            const int cur    = (outFieldId == FieldSliceStart) ? curStart : curEnd;
                            const int target = (outFieldId == FieldSliceStart)
                                ? juce::jlimit (0, curEnd - 64,       (int)(outNorm * (float)total))
                                : juce::jlimit (curStart + 64, total, (int)(outNorm * (float)total));

                            if (sel >= 0 && sel < kMaxCCSlices)
                            {
                                if (! ccSmootherActive[(size_t) sel][(size_t) outFieldId])
                                    ccSmoothers[(size_t) sel][(size_t) outFieldId].setCurrentAndTargetValue ((float) cur);
                                ccSmoothers[(size_t) sel][(size_t) outFieldId].setTargetValue ((float) target);
                                ccSmootherActive[(size_t) sel][(size_t) outFieldId] = true;
                            }
                        }
                    }

                    // If in trim mode the CC is fully consumed here — skip slice path
                    if (trimModeActive.load (std::memory_order_relaxed))
                        continue;
                }

                // ── Zoom / Scroll CC — runs regardless of slice count ────────
                if (outFieldId == FieldZoom || outFieldId == FieldScroll)
                {
                    if (outIsRelative)
                    {
                        if (outFieldId == FieldZoom)
                        {
                            // Each relative step zooms by a fixed factor
                            const float cur = juce::jmax (1.0f, zoom.load());
                            const float factor = std::pow (1.06f, outNorm);
                            const float newZ = juce::jlimit (1.0f, 16384.0f, cur * factor);
                            // Keep view centre stable
                            const float curFrac  = 1.0f / cur;
                            const float curSc    = scroll.load();
                            const float curStart = curSc * (1.0f - curFrac);
                            const float newFrac  = 1.0f / newZ;
                            const float maxSc    = 1.0f - newFrac;
                            const float newStart = curStart + (curFrac - newFrac) * 0.5f;
                            zoom.store (newZ);
                            scroll.store (maxSc > 0.0f ? juce::jlimit (0.0f, 1.0f, newStart / maxSc) : 0.0f);
                        }
                        else // FieldScroll
                        {
                            const float cur = scroll.load();
                            scroll.store (juce::jlimit (0.0f, 1.0f, cur + outNorm * 0.01f));
                        }
                    }
                    else // absolute
                    {
                        if (outFieldId == FieldZoom)
                            zoom.store (juce::jlimit (1.0f, 16384.0f, 1.0f + outNorm * 16383.0f));
                        else
                            scroll.store (juce::jlimit (0.0f, 1.0f, outNorm));
                    }
                    uiSnapshotDirty.store (true, std::memory_order_release);
                    continue;
                }

                // ── SFZ ADSR CC — global to SfzPlayer, no slice context needed ──
                if (outFieldId == FieldSfzAttack  || outFieldId == FieldSfzDecay  ||
                    outFieldId == FieldSfzSustain || outFieldId == FieldSfzRelease)
                {
                    auto getCurSfz = [&] (int fid) -> float
                    {
                        switch (fid)
                        {
                            case FieldSfzAttack:  return sfzPlayer.getSfzAttack();
                            case FieldSfzDecay:   return sfzPlayer.getSfzDecay();
                            case FieldSfzSustain: return sfzPlayer.getSfzSustain();
                            case FieldSfzRelease: return sfzPlayer.getSfzRelease();
                            default:              return 0.0f;
                        }
                    };

                    float val;
                    if (outIsRelative)
                    {
                        const float cur = getCurSfz (outFieldId);
                        float sens;
                        switch (outFieldId)
                        {
                            case FieldSfzAttack:  sens = 0.02f; break;  // 20 ms/click
                            case FieldSfzDecay:   sens = 0.02f; break;  // 20 ms/click
                            case FieldSfzSustain: sens = 1.0f;  break;  //  1 %/click
                            case FieldSfzRelease: sens = 0.02f; break;  // 20 ms/click
                            default:              sens = 0.01f; break;
                        }
                        const float raw = cur + outNorm * sens;
                        switch (outFieldId)
                        {
                            case FieldSfzAttack:  val = juce::jlimit (0.0f,  30.0f, raw); break;
                            case FieldSfzDecay:   val = juce::jlimit (0.0f,  30.0f, raw); break;
                            case FieldSfzSustain: val = juce::jlimit (0.0f, 100.0f, raw); break;
                            case FieldSfzRelease: val = juce::jlimit (0.0f,  60.0f, raw); break;
                            default:              val = raw;                               break;
                        }
                    }
                    else
                    {
                        switch (outFieldId)
                        {
                            case FieldSfzAttack:  val = outNorm * 30.0f;  break;
                            case FieldSfzDecay:   val = outNorm * 30.0f;  break;
                            case FieldSfzSustain: val = outNorm * 100.0f; break;
                            case FieldSfzRelease: val = outNorm * 60.0f;  break;
                            default:              val = outNorm;           break;
                        }
                    }

                    switch (outFieldId)
                    {
                        case FieldSfzAttack:  sfzPlayer.setSfzAttack  (val); break;
                        case FieldSfzDecay:   sfzPlayer.setSfzDecay   (val); break;
                        case FieldSfzSustain: sfzPlayer.setSfzSustain (val); break;
                        case FieldSfzRelease: sfzPlayer.setSfzRelease (val); break;
                        default: break;
                    }

                    // Keep JUCE ADSR (Option B — post-render envelope) in sync
                    // with the sfizz ADSR atomics whenever any of the four params change.
                    sfzPlayer.setJuceAdsr (
                        sfzPlayer.getSfzAttack(),
                        sfzPlayer.getSfzDecay(),
                        sfzPlayer.getSfzSustain() * 0.01f,   // sfizz stores 0-100%; JUCE wants 0-1
                        sfzPlayer.getSfzRelease());
                    uiSnapshotDirty.store (true, std::memory_order_release);
                    continue;
                }

                // ── SFZ Reverb EFX CC — global to SfzPlayer ──────────────────
                if (outFieldId >= FieldSfzReverbSize && outFieldId <= FieldSfzReverbFreeze)
                {
                    float val;
                    if (outIsRelative)
                    {
                        float cur;
                        switch (outFieldId)
                        {
                            case FieldSfzReverbSize:   cur = sfzPlayer.getReverbSize();   break;
                            case FieldSfzReverbDamp:   cur = sfzPlayer.getReverbDamp();   break;
                            case FieldSfzReverbWidth:  cur = sfzPlayer.getReverbWidth();  break;
                            case FieldSfzReverbMix:    cur = sfzPlayer.getReverbMix();    break;
                            case FieldSfzReverbFreeze: cur = sfzPlayer.getReverbFreeze() ? 100.0f : 0.0f; break;
                            default: cur = 0.0f; break;
                        }
                        val = juce::jlimit (0.0f, 100.0f, cur + outNorm * 2.0f);  // 2 %/click
                    }
                    else
                    {
                        val = outNorm * 100.0f;
                    }

                    switch (outFieldId)
                    {
                        case FieldSfzReverbSize:   sfzPlayer.setReverbSize  (val);         break;
                        case FieldSfzReverbDamp:   sfzPlayer.setReverbDamp  (val);         break;
                        case FieldSfzReverbWidth:  sfzPlayer.setReverbWidth (val);         break;
                        case FieldSfzReverbMix:    sfzPlayer.setReverbMix   (val);         break;
                        case FieldSfzReverbFreeze: sfzPlayer.setReverbFreeze (val > 50.0f); break;
                        default: break;
                    }
                    uiSnapshotDirty.store (true, std::memory_order_release);
                    continue;
                }

                // ── SFZ master knobs CC — Vol / Transpose / Pan / FineTune ───
                if (outFieldId == FieldSfzVol      || outFieldId == FieldSfzTranspose ||
                    outFieldId == FieldSfzPan       || outFieldId == FieldSfzFineTune)
                {
                    auto getCurMaster = [&] (int fid) -> float
                    {
                        switch (fid)
                        {
                            case FieldSfzVol:       return sfzPlayer.getVolume() / 2.0f;              // 0-1
                            case FieldSfzTranspose: return (sfzPlayer.getTranspose() + 24) / 48.0f;  // 0-1
                            case FieldSfzPan:       return (sfzPlayer.getPan() + 1.0f) / 2.0f;       // 0-1
                            case FieldSfzFineTune:  return (sfzPlayer.getFineTune() + 100.0f) / 200.0f; // 0-1
                            default:                return 0.0f;
                        }
                    };

                    float normVal;
                    if (outIsRelative)
                    {
                        const float cur = getCurMaster (outFieldId);
                        float sens;
                        switch (outFieldId)
                        {
                            case FieldSfzVol:       sens = 0.01f;  break;   // 1 %/click of full range
                            case FieldSfzTranspose: sens = 1.0f / 48.0f; break; // 1 semitone/click
                            case FieldSfzPan:       sens = 0.01f;  break;
                            case FieldSfzFineTune:  sens = 0.01f;  break;   // 2 cents/click
                            default:                sens = 0.01f;  break;
                        }
                        normVal = juce::jlimit (0.0f, 1.0f, cur + outNorm * sens);
                    }
                    else
                    {
                        normVal = outNorm;
                    }

                    switch (outFieldId)
                    {
                        case FieldSfzVol:       sfzPlayer.setVolume   (normVal * 2.0f);                   break;
                        case FieldSfzTranspose: sfzPlayer.setTranspose (juce::roundToInt (normVal * 48.0f) - 24); break;
                        case FieldSfzPan:       sfzPlayer.setPan      (normVal * 2.0f - 1.0f);            break;
                        case FieldSfzFineTune:  sfzPlayer.setFineTune (normVal * 200.0f - 100.0f);        break;
                        default: break;
                    }
                    uiSnapshotDirty.store (true, std::memory_order_release);
                    continue;
                }

                // A note-on and a CC can land in the same MidiBuffer.  If the
                // selected slice changed since the last CC in this buffer, the
                // ccPickedUp[] flags from the previous slice are stale for every
                // field — not just FieldSliceStart (which is handled separately).
                // Reset all pickup + smoother state now so absolute knobs cannot
                // jump to their old mapped position on the new slice.
                if (sel != ccLastDispatchedSel && ccLastDispatchedSel >= 0)
                {
                    markerSmootherSlice = -1;

                    // Reset pickup for the new slice (same reason as inter-buffer
                    // detection above: stale ccPickedUp bypasses the gate and causes
                    // the knob to jump the new slice's marker to the old position).
                    if (sel >= 0 && sel < kMaxCCSlices)
                    {
                        for (int j = 0; j < kMidiLearnNumSlots; ++j)
                            ccPickedUp[(size_t) sel][j] = false;
                        if (sel < 128)
                        {
                            markerFinePickupCcNorm    [(size_t) sel] = -1.0f;
                            markerFinePickupMarkerNorm[(size_t) sel] = -1.0f;
                        }
                        markerFineWindowLo.store (-1.0f, std::memory_order_relaxed);
                        markerFineWindowHi.store (-1.0f, std::memory_order_relaxed);
                    }

                    if (sel >= 0 && sel < sliceManager.getNumSlices())
                    {
                        ccSmoothers[(size_t) sel][(size_t) FieldSliceStart].setCurrentAndTargetValue (
                            (float) sliceManager.getSlice (sel).startSample);
                        markerSmootherSlice = sel;
                        // ccSmootherActive stays false — activates only after pickup.
                    }
                }
                ccLastDispatchedSel = sel;

                if (sel >= 0 && sel < sliceManager.getNumSlices())
                {
                    const auto& sl = sliceManager.getSlice (sel);

                    // ── Sensitivity table for endless encoders ────────────────
                    // outNorm is a signed step count (+1 = one click CW).
                    // Each entry is native-units-per-click (fine; hold Shift
                    // for coarse is a future improvement).
                    auto getRelSensitivity = [](int fid) -> float
                    {
                        switch (fid)
                        {
                            case FieldBpm:          return 0.5f;    // 0.5 BPM/click
                            case FieldPitch:        return 0.5f;    // 0.5 semitone/click
                            case FieldCentsDetune:  return 1.0f;    // 1 cent/click
                            case FieldPan:          return 0.02f;   // 2%/click
                            case FieldFilterCutoff: return 100.0f;  // 100 Hz/click
                            case FieldFilterRes:    return 0.01f;   // 1%/click
                            case FieldTonality:     return 100.0f;  // 100 Hz/click
                            case FieldFormant:      return 0.5f;    // 0.5 semitone/click
                            case FieldAttack:       return 0.002f;  // 2 ms/click
                            case FieldHold:         return 0.010f;  // 10 ms/click
                            case FieldDecay:        return 0.010f;  // 10 ms/click
                            case FieldSustain:      return 0.01f;   // 1%/click
                            case FieldRelease:      return 0.010f;  // 10 ms/click
                            case FieldVolume:       return 0.5f;    // 0.5 dB/click
                            case FieldMuteGroup:    return 1.0f;
                            case FieldMidiNote:     return 1.0f;
                            case FieldOutputBus:    return 1.0f;
                            default:                return 1.0f;
                        }
                    };

                    // ── Read current native value for relative delta ──────────
                    auto getCurrentNative = [&](int fid) -> float
                    {
                        switch (fid)
                        {
                            case FieldBpm:          return (sl.lockMask & kLockBpm)         ? sl.bpm              : apvts.getRawParameterValue (ParamIds::defaultBpm)->load();
                            case FieldPitch:        return sl.pitchSemitones;
                            case FieldCentsDetune:  return sl.centsDetune;
                            case FieldPan:          return (sl.lockMask & kLockPan)          ? sl.pan              : apvts.getRawParameterValue (ParamIds::defaultPan)->load();
                            case FieldFilterCutoff: return (sl.lockMask & kLockFilter)       ? sl.filterCutoff     : apvts.getRawParameterValue (ParamIds::defaultFilterCutoff)->load();
                            case FieldFilterRes:    return (sl.lockMask & kLockFilter)       ? sl.filterRes        : apvts.getRawParameterValue (ParamIds::defaultFilterRes)->load();
                            case FieldTonality:     return (sl.lockMask & kLockTonality)     ? sl.tonalityHz       : apvts.getRawParameterValue (ParamIds::defaultTonality)->load();
                            case FieldFormant:      return (sl.lockMask & kLockFormant)      ? sl.formantSemitones : apvts.getRawParameterValue (ParamIds::defaultFormant)->load();
                            case FieldAttack:       return (sl.lockMask & kLockAttack)       ? sl.attackSec        : apvts.getRawParameterValue (ParamIds::defaultAttack)->load() / 1000.0f;
                            case FieldHold:         return (sl.lockMask & kLockHold)         ? sl.holdSec          : apvts.getRawParameterValue (ParamIds::defaultHold)->load()   / 1000.0f;
                            case FieldDecay:        return (sl.lockMask & kLockDecay)        ? sl.decaySec         : apvts.getRawParameterValue (ParamIds::defaultDecay)->load()  / 1000.0f;
                            case FieldSustain:      return (sl.lockMask & kLockSustain)      ? sl.sustainLevel     : apvts.getRawParameterValue (ParamIds::defaultSustain)->load() / 100.0f;
                            case FieldRelease:      return (sl.lockMask & kLockRelease)      ? sl.releaseSec       : apvts.getRawParameterValue (ParamIds::defaultRelease)->load() / 1000.0f;
                            case FieldVolume:       return (sl.lockMask & kLockVolume)       ? sl.volume           : apvts.getRawParameterValue (ParamIds::masterVolume)->load();
                            case FieldMuteGroup:    return (float)((sl.lockMask & kLockMuteGroup)   ? sl.muteGroup  : (int) apvts.getRawParameterValue (ParamIds::defaultMuteGroup)->load());
                            case FieldMidiNote:     return (float) sl.midiNote;
                            case FieldOutputBus:    return (float)((sl.lockMask & kLockOutputBus)   ? sl.outputBus  : 0);
                            default:                return 0.0f;
                        }
                    };

                    // ── Convert CC to native value ────────────────────────────
                    // Relative: current + (step * sensitivity) — no jump, ever.
                    // Absolute: map 0-1 to full range, with pickup gating.
                    float nativeVal = outNorm;

                    if (outIsRelative)
                    {
                        // For relative encoders: use the smoother's current TARGET
                        // as the base, not the committed slice value.  This way rapid
                        // turns accumulate correctly into the pending target instead of
                        // stacking deltas on top of a stale (not-yet-smoothed) value.
                        const float cur = (outFieldId >= 0 && outFieldId < kMidiLearnNumSlots
                                           && sel >= 0 && sel < kMaxCCSlices
                                           && ccSmootherActive[(size_t) sel][(size_t) outFieldId])
                                          ? ccSmoothers[(size_t) sel][(size_t) outFieldId].getTargetValue()
                                          : getCurrentNative (outFieldId);
                        const float sens = getRelSensitivity (outFieldId);
                        const float raw  = cur + outNorm * sens;

                        // Clamp to parameter range
                        switch (outFieldId)
                        {
                            case FieldBpm:          nativeVal = juce::jlimit (20.0f,   999.0f, raw); break;
                            case FieldPitch:        nativeVal = juce::jlimit (-48.0f,   48.0f, raw); break;
                            case FieldCentsDetune:  nativeVal = juce::jlimit (-100.0f, 100.0f, raw); break;
                            case FieldPan:          nativeVal = juce::jlimit (-1.0f,     1.0f, raw); break;
                            case FieldFilterCutoff: nativeVal = juce::jlimit (20.0f, 20000.0f, raw); break;
                            case FieldFilterRes:    nativeVal = juce::jlimit (0.0f,     1.0f,  raw); break;
                            case FieldTonality:     nativeVal = juce::jlimit (0.0f,  8000.0f,  raw); break;
                            case FieldFormant:      nativeVal = juce::jlimit (-24.0f,   24.0f, raw); break;
                            case FieldAttack:       nativeVal = juce::jlimit (0.0f,   120.0f,  raw); break;
                            case FieldHold:         nativeVal = juce::jlimit (0.0f,   120.0f,  raw); break;
                            case FieldDecay:        nativeVal = juce::jlimit (0.0f,   120.0f,  raw); break;
                            case FieldSustain:      nativeVal = juce::jlimit (0.0f,     1.0f,  raw); break;
                            case FieldRelease:      nativeVal = juce::jlimit (0.0f,   120.0f,  raw); break;
                            case FieldVolume:       nativeVal = juce::jlimit (-100.0f,  24.0f, raw); break;
                            case FieldMuteGroup:    nativeVal = std::round (juce::jlimit (0.0f, 32.0f, raw)); break;
                            case FieldMidiNote:     nativeVal = std::round (juce::jlimit (0.0f, 127.0f, raw)); break;
                            case FieldOutputBus:    nativeVal = std::round (juce::jlimit (0.0f,  15.0f, raw)); break;
                            default: nativeVal = raw; break;
                        }
                    }
                    else
                    {
                        // Absolute knob: map 0-1 to full native range.
                        // Pickup gate: ignore until the knob reaches the current value.
                        const float curNative = getCurrentNative (outFieldId);
                        if (outFieldId >= 0 && outFieldId < kMidiLearnNumSlots
                            && outFieldId != FieldSliceStart  // FieldSliceStart has its own pickup + ghost logic below
                            && sel >= 0 && sel < kMaxCCSlices
                            && ! ccPickedUp[(size_t) sel][(size_t) outFieldId])
                        {
                            // Compute what native value outNorm would map to
                            float mappedNative = outNorm;
                            switch (outFieldId)
                            {
                                case FieldBpm:          mappedNative = 20.0f + outNorm * (999.0f - 20.0f); break;
                                case FieldPitch:        mappedNative = -24.0f + outNorm * 48.0f;           break;
                                case FieldCentsDetune:  mappedNative = -100.0f + outNorm * 200.0f;         break;
                                case FieldPan:          mappedNative = -1.0f + outNorm * 2.0f;             break;
                                case FieldFilterCutoff: mappedNative = 20.0f + outNorm * (20000.0f-20.0f); break;
                                case FieldVolume:       mappedNative = -100.0f + outNorm * 124.0f;         break;
                                default:                mappedNative = outNorm; break;
                            }
                            // Allow 4% of range as pickup dead-zone
                            const float rangeSpan = [&] {
                                switch (outFieldId) {
                                    case FieldBpm:          return 979.0f;
                                    case FieldPitch:        return 96.0f;
                                    case FieldCentsDetune:  return 200.0f;
                                    case FieldPan:          return 2.0f;
                                    case FieldFilterCutoff: return 19980.0f;
                                    case FieldVolume:       return 124.0f;
                                    default:                return 1.0f;
                                }
                            }();
                            if (std::abs (mappedNative - curNative) <= rangeSpan * 0.04f)
                                ccPickedUp[(size_t) sel][(size_t) outFieldId] = true;
                            else
                                goto skipCcParam;  // not picked up yet — suppress
                        }

                        switch (outFieldId)
                        {
                            case FieldBpm:          nativeVal = 20.0f + outNorm * (999.0f - 20.0f); break;
                            case FieldPitch:        nativeVal = -24.0f + outNorm * 48.0f;           break;
                            case FieldCentsDetune:  nativeVal = -100.0f + outNorm * 200.0f;         break;
                            case FieldPan:          nativeVal = -1.0f + outNorm * 2.0f;             break;
                            case FieldFilterCutoff: nativeVal = 20.0f + outNorm * (20000.0f-20.0f); break;
                            case FieldFilterRes:    nativeVal = outNorm;                            break;
                            case FieldTonality:     nativeVal = outNorm * 8000.0f;                 break;
                            case FieldFormant:      nativeVal = -24.0f + outNorm * 48.0f;          break;
                            case FieldAttack:       nativeVal = outNorm * 1.0f;                    break;
                            case FieldHold:         nativeVal = outNorm * 5.0f;                    break;
                            case FieldDecay:        nativeVal = outNorm * 5.0f;                    break;
                            case FieldSustain:      nativeVal = outNorm;                           break;
                            case FieldRelease:      nativeVal = outNorm * 5.0f;                    break;
                            case FieldMuteGroup:    nativeVal = std::round (outNorm * 32.0f);      break;
                            case FieldMidiNote:     nativeVal = std::round (outNorm * 127.0f);     break;
                            case FieldVolume:       nativeVal = -100.0f + outNorm * 124.0f;        break;
                            case FieldOutputBus:    nativeVal = std::round (outNorm * 15.0f);      break;
                            case FieldAlgorithm:    nativeVal = std::round (outNorm * 2.0f);       break;
                            case FieldLoop:         nativeVal = std::round (outNorm * 2.0f);       break;
                            case FieldGrainMode:    nativeVal = std::round (outNorm * 2.0f);       break;
                            default: break;
                        }
                    }

                    // ── Slice start/end boundary (non-trim mode only) ────────
                    // Trim CC already handled above the slice guard; this path
                    // only runs when trim mode is inactive.
                    if (outFieldId == FieldSliceStart)
                    {
                        const int total = sampleData.getNumFrames();
                        if (total > 1)
                        {
                            auto& sl     = sliceManager.getSlice (sel);
                            const int slEnd = sliceManager.getEndForSlice (sel, total);

                            if (outIsRelative)
                            {
                                // Kill any absolute smoother armed during the detection phase.
                                // The relative path never sets ccSmootherActive[FieldSliceStart];
                                // any 'true' value here is a stale detection/absolute artefact.
                                // If left alive the smoother loop in processBlock would override
                                // every relative handleCommand below and jump the marker to the
                                // absolute position the smoother was targeting.
                                if (sel >= 0 && sel < kMaxCCSlices)
                                    ccSmootherActive[(size_t) sel][(size_t) FieldSliceStart] = false;
                                markerSmootherSlice = -1;
                                markerPending     = false;
                                markerIdleCounter = 0;

                                // Relative: commit immediately via handleCommand each buffer.
                                // This eliminates the idle-commit jump: sliceManager is updated
                                // atomically each block (including culling), so there is no
                                // discontinuity when the encoder stops turning.
                                // markerPending / idle-commit path is NOT used for CC.
                                const float sensitivity = (float) total / 300.0f;
                                const int newStart = juce::jlimit (0, slEnd - 64,
                                    sl.startSample + (int)(outNorm * sensitivity));
                                // Write to liveDragBoundsStart so SliceControlBar's timer
                                // detects the change and schedules a repaint each CC arrival.
                                liveDragBoundsStart.store (newStart, std::memory_order_relaxed);
                                Command ccCmd;
                                ccCmd.type         = CmdSetSliceBounds;
                                ccCmd.intParam1    = sel;
                                ccCmd.intParam2    = newStart;
                                ccCmd.positions[0] = slEnd;
                                ccCmd.numPositions = 1;
                                handleCommand (ccCmd);
                            }
                            else
                            {
                                // Absolute: Pickup mode — CC must first reach the marker's
                                // current position before tracking begins.  This prevents
                                // the marker jumping to wherever the CC happens to be when
                                // you switch to a new slice.  ccPickedUp[field] is already
                                // cleared by CmdSelectSlice whenever the slice changes, and
                                // by the inter-buffer detection block for other slice-change
                                // paths.  The check below catches the intra-buffer race:
                                // a note-on and a CC can arrive in the same MidiBuffer, so
                                // selectedSlice may have changed since the detection block
                                // ran at the top of processBlock.  If the smoother is
                                // tracking a different slice, the pickup gate is stale —
                                // clear it locally before applying the gate check.
                                // No cross-slice stale reset needed — each slice has
                                // its own ccPickedUp state.

                                const float markerNorm = (float) sl.startSample
                                                       / (float) juce::jmax (1, total);

                                if (outFieldId >= 0 && outFieldId < kMidiLearnNumSlots
                                    && sel >= 0 && sel < kMaxCCSlices
                                    && ! ccPickedUp[(size_t) sel][(size_t) outFieldId])
                                {
                                    // Tolerance: ~1 CC step (1/127 ≈ 0.008).
                                    if (std::abs (outNorm - markerNorm) <= 0.008f)
                                    {
                                        ccPickedUp[(size_t) sel][(size_t) outFieldId] = true;
                                        // Record pickup reference point for fine window.
                                        if (sel < 128)
                                        {
                                            markerFinePickupCcNorm    [(size_t) sel] = outNorm;
                                            markerFinePickupMarkerNorm[(size_t) sel] = markerNorm;
                                        }
                                    }
                                    else
                                    {
                                        markerCcGhostNorm.store (outNorm, std::memory_order_relaxed);
                                        goto skipCcParam;   // not picked up yet — suppress
                                    }
                                }

                                // ── Post-pickup: normal or fine mode ─────────────────────
                                const bool fineEnabled = markerFineMode.load (std::memory_order_relaxed);

                                // Clear the ghost bar — replaced by window edges in fine mode.
                                markerCcGhostNorm.store (-1.0f, std::memory_order_relaxed);

                                float fineTarget;
                                if (fineEnabled)
                                {
                                    // CC physical extremes (bottom 2% or top 2%) re-arm pickup
                                    // so the user can jump to a completely different region.
                                    const bool atExtreme = (outNorm < 0.02f || outNorm > 0.98f);
                                    if (atExtreme && sel >= 0 && sel < kMaxCCSlices && sel < 128)
                                    {
                                        ccPickedUp[(size_t) sel][(size_t) outFieldId] = false;
                                        markerFinePickupCcNorm[(size_t) sel]    = -1.0f;
                                        markerFineWindowLo.store (-1.0f, std::memory_order_relaxed);
                                        markerFineWindowHi.store (-1.0f, std::memory_order_relaxed);
                                        markerCcGhostNorm.store (outNorm, std::memory_order_relaxed);
                                        goto skipCcParam;
                                    }

                                    // Fine window: full knob travel = kMarkerFineWindowNorm of sample.
                                    if (sel >= 0 && sel < 128
                                        && markerFinePickupCcNorm[(size_t) sel] >= 0.0f)
                                    {
                                        const float pickupCc  = markerFinePickupCcNorm    [(size_t) sel];
                                        const float pickupMkr = markerFinePickupMarkerNorm[(size_t) sel];
                                        const float delta     = outNorm - pickupCc;
                                        fineTarget = juce::jlimit (0.0f, 1.0f,
                                            pickupMkr + delta * kMarkerFineWindowNorm);

                                        // Publish window edges for the UI
                                        const float halfW = kMarkerFineWindowNorm * 0.5f;
                                        markerFineWindowLo.store (juce::jlimit (0.0f, 1.0f, pickupMkr - halfW),
                                                                  std::memory_order_relaxed);
                                        markerFineWindowHi.store (juce::jlimit (0.0f, 1.0f, pickupMkr + halfW),
                                                                  std::memory_order_relaxed);
                                    }
                                    else
                                    {
                                        fineTarget = markerNorm; // fallback
                                    }
                                }
                                else
                                {
                                    // Normal mode: CC maps full sample range directly.
                                    fineTarget = outNorm;
                                    markerFineWindowLo.store (-1.0f, std::memory_order_relaxed);
                                    markerFineWindowHi.store (-1.0f, std::memory_order_relaxed);
                                }

                                // Route target through smoother.
                                const int newStart = juce::jlimit (0, slEnd - 64,
                                    (int) (fineTarget * (float) total));
                                if (sel >= 0 && sel < kMaxCCSlices)
                                {
                                    if (! ccSmootherActive[(size_t) sel][(size_t) outFieldId] || sel != markerSmootherSlice)
                                        ccSmoothers[(size_t) sel][(size_t) outFieldId].setCurrentAndTargetValue (
                                            (float) sl.startSample);
                                    ccSmoothers[(size_t) sel][(size_t) outFieldId].setTargetValue ((float) newStart);
                                    ccSmootherActive[(size_t) sel][(size_t) outFieldId] = true;
                                    markerSmootherSlice = sel;
                                }
                            }

                            uiSnapshotDirty.store (true, std::memory_order_release);
                        }
                    }
                    else
                    {
                        // Feed the target into the per-slot smoother.
                        // processBlock() will step it and fire CmdSetSliceParam
                        // each buffer, giving a smooth ~20 ms glide to the target.
                        if (outFieldId >= 0 && outFieldId < kMidiLearnNumSlots)
                        {
                            if (sel >= 0 && sel < kMaxCCSlices)
                            {
                                ccSmoothers[(size_t) sel][(size_t) outFieldId].setTargetValue (nativeVal);
                                ccSmootherActive[(size_t) sel][(size_t) outFieldId] = true;
                            }
                        }
                    }
                    uiSnapshotDirty.store (true, std::memory_order_release);
                    skipCcParam:;
                }
            }
            else if (prevArmed >= 0 && midiLearn.getArmedSlot() < 0)
            {
                if (prevArmed < kMidiLearnNumSlots)
                {
                    // Clear pickup/smoother for the re-armed slot across all slices
                    for (int si = 0; si < kMaxCCSlices; ++si)
                    {
                        ccPickedUp      [(size_t) si][(size_t) prevArmed] = false;
                        ccSmootherActive[(size_t) si][(size_t) prevArmed] = false;
                    }
                    if (prevArmed == FieldSliceStart)
                    {
                        markerSmootherSlice = -1;
                        markerPending       = false;
                        markerPendingSlice  = -1;
                        markerIdleCounter   = 0;
                    }
                }
            }
        }

        if (msg.isNoteOn())
        {
            int note = msg.getNoteNumber();
            float velocity = (float) msg.getVelocity();

            if (lazyChop.isActive())
            {
                // Any MIDI note places a chop boundary at the playhead
                int newSliceIdx = lazyChop.onNote (note, voicePool, sliceManager);
                if (newSliceIdx >= 0)
                {
                    uiSnapshotDirty.store (true, std::memory_order_release);
                    if (midiSelectsSlice.load (std::memory_order_relaxed))
                    {
                        sliceManager.selectedSlice.store (newSliceIdx, std::memory_order_relaxed);
                        midiFollowTriggeredSlice.store (newSliceIdx, std::memory_order_relaxed);
                    }
                }
            }
            else
            {
                heldNotes[note] = true;

                // Build params once; all param loads happen here, not inside the slice loop.
                VoiceStartParams p;
                p.note             = note;
                p.velocity         = velocity;
                p.globalBpm        = bpmParam->load();
                p.globalPitch      = pitchParam->load();
                // globalAlgorithm removed — algo derived from stretchOn flag
                p.globalAttackSec  = attackParam->load()  / 1000.0f;
                p.globalHoldSec    = holdParam->load()    / 1000.0f;
                p.globalDecaySec   = decayParam->load()   / 1000.0f;
                p.globalSustain    = sustainParam->load() / 100.0f;
                p.globalReleaseSec = releaseParam->load() / 1000.0f;
                p.globalMuteGroup  = (int) muteGroupParam->load();
                p.globalStretch    = stretchParam->load()      > 0.5f;
                p.dawBpm           = dawBpm.load();
                p.globalTonality   = tonalityParam->load();
                p.globalFormant    = formantParam->load();
                p.globalFormantComp = formantCompParam->load() > 0.5f;
                // globalGrainMode removed — Grain was a duplicate of Tonal
                p.globalVolume     = masterVolParam->load();
                p.globalReleaseTail = releaseTailParam->load() > 0.5f;
                p.globalReverse    = reverseParam->load()      > 0.5f;
                p.globalLoopMode   = (int) loopParam->load();
                p.globalOneShot    = false;  // One Shot is per-slice only; Hold is always the global default
                p.globalCentsDetune  = centsDetuneParam->load();
                p.globalPan          = panParam->load();
                p.globalFilterCutoff = filterCutoffParam->load();
                p.globalFilterRes    = filterResParam->load();

                // ── v24: per-slice EQ defaults ─────────────────────────────────
                if (auto* pEqLow  = apvts.getRawParameterValue (ParamIds::defaultEqLowGain))
                    p.globalEqLowGain  = pEqLow->load();
                if (auto* pEqMidG = apvts.getRawParameterValue (ParamIds::defaultEqMidGain))
                    p.globalEqMidGain  = pEqMidG->load();
                if (auto* pEqMidF = apvts.getRawParameterValue (ParamIds::defaultEqMidFreq))
                    p.globalEqMidFreq  = pEqMidF->load();
                if (auto* pEqMidQ = apvts.getRawParameterValue (ParamIds::defaultEqMidQ))
                    p.globalEqMidQ     = pEqMidQ->load();
                if (auto* pEqHigh = apvts.getRawParameterValue (ParamIds::defaultEqHighGain))
                    p.globalEqHighGain = pEqHigh->load();

                // ── Trim mode or unsliced: play whole sample / trim region chromatically ──
                // Root = C3 (MIDI 60). Active whenever trim dialog is open, or when
                // a sample is loaded but has no slices yet.
                static constexpr int kChromaticDefaultRoot = 60; // C3
                const int totalFrames = sampleData.getNumFrames();
                const bool inTrim     = trimModeActive.load (std::memory_order_relaxed);
                const bool unsliced   = (sliceManager.getNumSlices() == 0);

                if (totalFrames > 0 && (inTrim || unsliced))
                {
                    const int sStart = inTrim ? trimRegionStart.load (std::memory_order_relaxed) : 0;
                    const int sEnd   = inTrim ? trimRegionEnd  .load (std::memory_order_relaxed) : totalFrames;
                    if (sEnd > sStart)
                    {
                        const float semitoneOffset = (float)(note - kChromaticDefaultRoot);
                        const float savedPitch = p.globalPitch;
                        p.globalPitch = savedPitch + semitoneOffset;

                        int voiceIdx = voicePool.allocate();
                        voicePool.startVoiceUnsliced (voiceIdx, p, sStart, sEnd, sampleData);
                        p.globalPitch = savedPitch;
                    }
                }
                else

                // ── Per-slice chromatic channel routing ────────────────────────
                // If any slice has chromaticChannel == incoming MIDI channel,
                // play that slice pitched relative to root note.
                // Multiple slices can be chromatic on different channels simultaneously.
                {
                    const int inChannel = msg.getChannel(); // 1-16
                    const int root      = sliceManager.rootNote.load (std::memory_order_relaxed);
                    const int numSl     = sliceManager.getNumSlices();
                    bool      handled   = false;

                    for (int ci = 0; ci < numSl; ++ci)
                    {
                        const auto& cs = sliceManager.getSlice (ci);
                        if (! cs.active) continue;
                        if (cs.chromaticChannel != inChannel) continue;

                        const float semitoneOffset = (float) (note - root);
                        // True sample-through chromatic legato:
                        // On a new legato note, only pitch changes — playback position is NOT reset.
                        // Try to retune an already-playing voice first; only start fresh if nothing is playing.
                        const bool legato = cs.chromaticLegato;
                        p.sliceIdx    = ci;
                        const float savedGlobalPitch = p.globalPitch;
                        p.globalPitch = savedGlobalPitch + semitoneOffset;
                        p.chromaticLegatoTrigger = legato;

                        bool retuned = false;
                        if (legato)
                            retuned = voicePool.retuneChromaticLegatoVoice (ci, p.globalPitch,
                                                                             0.0f, note);
                        if (! retuned)
                        {
                            int voiceIdx = voicePool.allocate();
                            const bool globalMono = monoParam->load() > 0.5f;
                            int mg = (globalMono && cs.chromaticChannel == 0)
                                       ? (ci + 1)
                                       : (int) sliceManager.resolveParam (ci, kLockMuteGroup,
                                                                           (float) cs.muteGroup,
                                                                           (float) p.globalMuteGroup);
                            voicePool.muteGroup (mg, voiceIdx);
                            if (legato)
                                voicePool.killVoicesForChromaticLegato (ci);
                            p.globalMuteGroup = mg; // ensure voice stamps the same group used for killing
                            voicePool.startVoice (voiceIdx, p, sliceManager, sampleData);
                        }

                        p.chromaticLegatoTrigger = false;
                        p.globalPitch = savedGlobalPitch;

                        if (midiSelectsSlice.load (std::memory_order_relaxed))
                        {
                            sliceManager.selectedSlice.store (ci, std::memory_order_relaxed);
                            midiFollowTriggeredSlice.store (ci, std::memory_order_relaxed);
                            uiSnapshotDirty.store (true, std::memory_order_release);
                        }
                        handled = true;
                        break; // first matching slice wins
                    }

                    if (! handled && inChannel == 1)
                    {
                    // ── Standard: one slice per note ──────────────────────────
                    // Non-chromatic slices only ever trigger on MIDI channel 1.
                    // Chromatic slices are handled above and can listen on any
                    // channel via cs.chromaticChannel — this gate only applies
                    // to the plain "one slice per note" fallback path.
                    const int sliceIdx = sliceManager.midiNoteToSlice (note);
                    if (sliceIdx >= 0)
                    {
                        if (midiSelectsSlice.load (std::memory_order_relaxed))
                        {
                            sliceManager.selectedSlice.store (sliceIdx, std::memory_order_relaxed);
                            midiFollowTriggeredSlice.store (sliceIdx, std::memory_order_relaxed);
                            uiSnapshotDirty.store (true, std::memory_order_release);
                        }

                        int voiceIdx = voicePool.allocate();
                        const auto& s = sliceManager.getSlice (sliceIdx);
                        const bool globalMono = monoParam->load() > 0.5f;
                        int mg = (globalMono && s.chromaticChannel == 0)
                                   ? (sliceIdx + 1)
                                   : (int) sliceManager.resolveParam (sliceIdx, kLockMuteGroup,
                                                                       (float) s.muteGroup, (float) p.globalMuteGroup);
                        voicePool.muteGroup (mg, voiceIdx);
                        p.sliceIdx = sliceIdx;
                        p.globalMuteGroup = mg; // ensure voice stamps the same group used for killing
                        voicePool.startVoice (voiceIdx, p, sliceManager, sampleData);
                    }
                    } // end if (!handled)
                }   // end per-slice chromatic routing
            }
        }
        else if (msg.isNoteOff())
        {
            int note = msg.getNoteNumber();
            if (heldNotes[note])
            {
                heldNotes[note] = false;
                voicePool.releaseNote (note);           // normal: respects oneShot
            }
            else
            {
                voicePool.releaseNoteForced (note);     // host sweep: kills even oneShot voices
            }
        }
        else if (msg.isAllNotesOff())
        {
            voicePool.releaseAll();  // 50ms fade on all active voices
            lazyChop.stop (voicePool, sliceManager);
            std::fill (std::begin (heldNotes), std::end (heldNotes), false);
        }
        else if (msg.isAllSoundOff())
        {
            voicePool.killAll();     // 5ms hard kill on all active voices
            lazyChop.stop (voicePool, sliceManager);
            std::fill (std::begin (heldNotes), std::end (heldNotes), false);
        }
    }
}

void DysektProcessor::processMidi2 (const juce::MidiBuffer& midi)
{
    const uint32_t sfz2Mask = sfzPlayer2ChannelMask.load (std::memory_order_relaxed);
    if (sfz2Mask == 0) return;   // SFZ-PLAYER not enabled — no channels owned

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        const int  ch  = msg.getChannel();   // 1-based
        if (ch < 1 || ch > 16 || ((sfz2Mask & (1u << ch)) == 0))
            continue;   // not one of SFZ-PLAYER's owned channels

        if (msg.isNoteOn())
        {
            const int   note     = msg.getNoteNumber();
            const float velocity = (float) msg.getVelocity();

            heldNotes2[note] = true;
            sfz2MidiActivity.fetch_add (1, std::memory_order_relaxed);

            // Update the active-note bitmask the SFZ-Player keyboard(s) poll
            // for highlighting — populated here so it only ever reflects
            // notes actually on sfzPlayer2's own channel(s), never bleeding
            // in activity from other channels/engines.
            {
                const int w = note < 64 ? 0 : 1;
                const int b = note < 64 ? note : note - 64;
                sfz2ActiveNotes[w].fetch_or ((uint64_t) 1 << b, std::memory_order_relaxed);
            }

            const int sliceIdx = sliceManager2.midiNoteToSlice (note);
            if (sliceIdx >= 0)
            {
                // Unconditional — matches WaveformView's mouse-click selection for
                // sliceManager2, which isn't gated by midiSelectsSlice either. That
                // flag is an engine-1-only preference (auto-enabled by slicing
                // actions); SFZ-PLAYER has no such workflow to protect, so a
                // played note should always be reflected in the UI here.
                sliceManager2.selectedSlice.store (sliceIdx, std::memory_order_relaxed);
                uiSnapshotDirty.store (true, std::memory_order_release);

                VoiceStartParams p;   // defaults: no global knobs for SFZ-PLAYER —
                                      // every parameter is resolved per-slice from
                                      // the loaded SFZ file's region data.
                p.note     = note;
                p.velocity = velocity;
                p.sliceIdx = sliceIdx;

                int voiceIdx = voicePool2.allocate();
                const auto& s = sliceManager2.getSlice (sliceIdx);
                const int mg = (int) sliceManager2.resolveParam (sliceIdx, kLockMuteGroup,
                                                                  (float) s.muteGroup,
                                                                  (float) p.globalMuteGroup);
                voicePool2.muteGroup (mg, voiceIdx);
                p.globalMuteGroup = mg;
                voicePool2.startVoice (voiceIdx, p, sliceManager2, sampleData2);
            }
        }
        else if (msg.isNoteOff())
        {
            const int note = msg.getNoteNumber();
            if (heldNotes2[note])
            {
                heldNotes2[note] = false;
                voicePool2.releaseNote (note);
            }
            else
            {
                voicePool2.releaseNoteForced (note);
            }
            {
                const int w = note < 64 ? 0 : 1;
                const int b = note < 64 ? note : note - 64;
                sfz2ActiveNotes[w].fetch_and (~((uint64_t) 1 << b), std::memory_order_relaxed);
            }
            int prev = sfz2MidiActivity.load (std::memory_order_relaxed);
            while (prev > 0 &&
                   !sfz2MidiActivity.compare_exchange_weak (prev, prev - 1,
                       std::memory_order_relaxed, std::memory_order_relaxed))
            {}
        }
        else if (msg.isAllNotesOff())
        {
            voicePool2.releaseAll();
            std::fill (std::begin (heldNotes2), std::end (heldNotes2), false);
            sfz2ActiveNotes[0].store (0, std::memory_order_relaxed);
            sfz2ActiveNotes[1].store (0, std::memory_order_relaxed);
        }
        else if (msg.isAllSoundOff())
        {
            voicePool2.killAll();
            std::fill (std::begin (heldNotes2), std::end (heldNotes2), false);
            sfz2ActiveNotes[0].store (0, std::memory_order_relaxed);
            sfz2ActiveNotes[1].store (0, std::memory_order_relaxed);
        }
    }
}

static inline float sanitiseSample (float x)
{
    if (! std::isfinite (x)) return 0.0f;
    return juce::jlimit (-1.0f, 1.0f, x);
}

void DysektProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                            juce::MidiBuffer& midi)
{
    // TEMP diagnostic — fires exactly once, completely unconditionally, the
    // very first time processBlock() is called at all. If this line never
    // appears in dysekt_crash.log, processBlock() itself is not being
    // invoked for this plugin instance (bypass/mute at the host level,
    // wrong instance, or the host never started the audio engine) — nothing
    // inside processBlock, including the SF2/SFZ player, can matter until
    // that's resolved.
    {
        static bool loggedProcessBlockEntryOnce = false;
        if (! loggedProcessBlockEntryOnce)
        {
            loggedProcessBlockEntryOnce = true;
            crashLogger.log ("processBlock() ENTRY — first call reached. numSamples="
                + juce::String (buffer.getNumSamples())
                + " midi.getNumEvents()=" + juce::String (midi.getNumEvents()));
        }
    }

    // NOTE: a prior "TEMP diagnostic" here built a juce::String per MIDI
    // event and wrote it to disk via crashLogger.log() on every block that
    // had any MIDI at all. Both the String concatenation (heap allocation)
    // and the log write (synchronous, locked file I/O) violate this file's
    // own audio-thread rules — under any sustained burst of MIDI (e.g. fast
    // mod-wheel movement queuing 60+ CC messages into one block) it stalled
    // processBlock() long enough that the host force-killed the process,
    // which is exactly what left session.lock behind. Removed; use a
    // debugger or a lock-free ring buffer drained on the message thread if
    // this needs to be re-instrumented.

    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // ── Poll global EQ param changes ──────────────────────────────────────────
    {
        static float cachedEqLow = -999.f, cachedEqLowF = -999.f,
                     cachedEqMidG = -999.f, cachedEqMidF = -999.f,
                     cachedEqMidQ = -999.f,
                     cachedEqHigh = -999.f, cachedEqHighF = -999.f;
        auto* pLow  = apvts.getRawParameterValue (ParamIds::globalEqLowGain);
        auto* pLowF = apvts.getRawParameterValue (ParamIds::globalEqLowFreq);
        auto* pMidG = apvts.getRawParameterValue (ParamIds::globalEqMidGain);
        auto* pMidF = apvts.getRawParameterValue (ParamIds::globalEqMidFreq);
        auto* pMidQ = apvts.getRawParameterValue (ParamIds::globalEqMidQ);
        auto* pHigh = apvts.getRawParameterValue (ParamIds::globalEqHighGain);
        auto* pHighF = apvts.getRawParameterValue (ParamIds::globalEqHighFreq);
        if (pLow && pLowF && pMidG && pMidF && pMidQ && pHigh && pHighF)
        {
            float l = pLow->load(), lf = pLowF->load(),
                  mg = pMidG->load(), mf = pMidF->load(),
                  mq = pMidQ->load(),
                  h = pHigh->load(), hf = pHighF->load();
            if (l != cachedEqLow || lf != cachedEqLowF ||
                mg != cachedEqMidG || mf != cachedEqMidF ||
                mq != cachedEqMidQ ||
                h != cachedEqHigh || hf != cachedEqHighF)
            {
                cachedEqLow = l; cachedEqLowF = lf;
                cachedEqMidG = mg; cachedEqMidF = mf;
                cachedEqMidQ = mq;
                cachedEqHigh = h; cachedEqHighF = hf;
                globalEqNeedsUpdate = true;
            }
        }
    }

    // Read DAW BPM from playhead
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto bpmOpt = pos->getBpm())
            {
                dawBpm.store ((float) *bpmOpt, std::memory_order_relaxed);
#if DYSEKT_STANDALONE
                sequencer.setHostBpm ((float) *bpmOpt);
                if (abletonLink.isEnabled())
                    abletonLink.setBpm (*bpmOpt);
#endif
            }
        }
    }

    // Poll shift preview request (atomic, avoids FIFO latency)
    {
        int req = shiftPreviewRequest.exchange (-2, std::memory_order_relaxed);
        if (req == -1)
            voicePool.stopShiftPreview();
        else if (req >= 0 && ! lazyChop.isActive() && sampleData.isLoaded())
            voicePool.startShiftPreview (req, sampleData.getNumFrames(),
                                         currentSampleRate, sampleData);
    }

    bool loadStateChanged = false;
    {
        auto* rawDecoded = completedLoadData.exchange (nullptr, std::memory_order_acq_rel);
        if (rawDecoded != nullptr)
        {
            std::unique_ptr<SampleData::DecodedSample> decoded (rawDecoded);
            clearVoicesBeforeSampleSwap();
            sampleData.applyDecodedSample (std::move (decoded));
            sampleMissing.store (false);
            missingFilePath.clear();
            sampleAvailability.store ((int) SampleStateLoaded, std::memory_order_relaxed);

            if (latestLoadKind.load (std::memory_order_acquire) == (int) LoadKindReplace)
            {
                sliceManager.clearAll();
                const juce::String fname = sampleData.getFileName();
                const bool isDefault = fname.equalsIgnoreCase ("Empty.wav")
                                    || fname.equalsIgnoreCase ("DYSEKT_default.wav")
                                    || fname.isEmpty();
                // No auto-slice: sample is immediately playable chromatically
                // via the unsliced path. User adds slices explicitly via ADD SLICE.
                sliceManager.selectedSlice.store (isDefault ? -1 : -1, std::memory_order_relaxed);
            }
            else
            {
                clampSlicesToSampleBounds();
                sliceManager.rebuildMidiMap();
            }

            // ── SF2/SFZ: auto-create slices (one per rendered note) ──────────
            // In SFZ-PLAYER mode (uiMode 1) the sfzPayload is consumed but not
            // applied to sliceManager — sfzPlayer2 handles MIDI internally and
            // there is no 32-slice cap to worry about.
            auto* sfzPayload = pendingSfzSlices.exchange (nullptr, std::memory_order_acq_rel);
            if (sfzPayload != nullptr)
            {
                std::unique_ptr<SfzSlicePayload> sfzOwner (sfzPayload);
                const bool isSfzPlayerMode = (midiRouteMode.load (std::memory_order_relaxed)
                                              == static_cast<int> (MidiRouteMode::SfzPlayer2));
                if (! isSfzPlayerMode)  // SFZ-PLAYER: sfzPlayer2 handles MIDI; skip sliceManager
                {
                    // sliceManager was just cleared above — safe to create fresh slices
                    for (auto& desc : sfzOwner->slices)
                    {
                        int idx = sliceManager.createSlice (desc.startSample, desc.endSample);
                        if (idx >= 0)
                        {
                            auto& s = sliceManager.getSlice (idx);
                            s.midiNote = juce::jlimit (0, 127, desc.midiNote);
                        }
                    }
                    sliceManager.rebuildMidiMap();
                }
            }
            // ────────────────────────────────────────────────────────────────

            loadStateChanged = true;
            uiSnapshotDirty.store (true, std::memory_order_release);
        }
    }

    {
        auto* rawFailure = completedLoadFailure.exchange (nullptr, std::memory_order_acq_rel);
        if (rawFailure != nullptr)
        {
            std::unique_ptr<FailedLoadResult> failed (rawFailure);
            if (failed->token == latestLoadToken.load (std::memory_order_acquire)
                && failed->kind == LoadKindRelink)
            {
                sampleMissing.store (true);
                missingFilePath = failed->file.getFullPathName();
                sampleData.setFileName (failed->file.getFileName());
                sampleData.setFilePath (failed->file.getFullPathName());
                sampleAvailability.store ((int) SampleStateMissingAwaitingRelink,
                                         std::memory_order_relaxed);
                loadStateChanged = true;
                uiSnapshotDirty.store (true, std::memory_order_release);
            }
        }
    }

    // ── SFZ-PLAYER engine (sliceManager2/voicePool2/sampleData2) ─────────────
    // A second, independent Slicer instance. Each SFZ key zone/region becomes
    // a real slice — same recipe as the Slicer's own SFZ/SF2 auto-slice path
    // above (sliceManager.createSlice + midiNote + rebuildMidiMap), just
    // applied to sliceManager2 unconditionally (no chromatic/uiMode branching
    // needed: this engine has no other use for its slices).
    {
        auto* rawDecoded2 = completedLoadData2.exchange (nullptr, std::memory_order_acq_rel);
        if (rawDecoded2 != nullptr)
        {
            std::unique_ptr<SampleData::DecodedSample> decoded2 (rawDecoded2);
            clearVoicesBeforeSampleSwap2();
            sampleData2.applyDecodedSample (std::move (decoded2));
            sliceManager2.clearAll();
            uiSnapshotDirty.store (true, std::memory_order_release);
        }

        // Same load event also carries the per-note descriptors that become
        // this engine's real slices (one per SFZ key zone/region).
        auto* rawZones2 = pendingPreviewZones2.exchange (nullptr, std::memory_order_acq_rel);
        if (rawZones2 != nullptr)
        {
            std::unique_ptr<SfzPreviewZonePayload> zonesOwner2 (rawZones2);

            // sliceManager2 was just cleared above (or this load arrived after
            // a previous one — clearAll() again is harmless/idempotent here).
            sliceManager2.clearAll();
            for (auto& desc : zonesOwner2->slices)
            {
                const bool hasLoop = (desc.loopStart >= 0 && desc.loopEnd > desc.loopStart
                                      && desc.loopStart > desc.startSample
                                      && desc.loopEnd <= desc.endSample);

                if (! hasLoop)
                {
                    // No (valid) loop region for this note — plain one-shot slice.
                    int idx = sliceManager2.createSlice (desc.startSample, desc.endSample);
                    if (idx >= 0)
                        sliceManager2.pinSliceMidiNote (idx, desc.midiNote);
                    continue;
                }

                // Two-slice split: one-shot attack head [startSample, loopStart),
                // chained on natural end into a looping sustain tail
                // [loopStart, endSample). Only the head is pinned to a MIDI
                // note (pinSliceMidiNote, NOT rebuildMidiMap — that call
                // reassigns every slice's midiNote sequentially from
                // rootNote and would destroy this exact-key-zone mapping).
                // The tail is reached only via the chain, never directly
                // MIDI-triggerable: see Slice::nextSliceIdx.
                const int headIdx = sliceManager2.createSlice (desc.startSample, desc.loopStart);
                const int tailIdx = sliceManager2.createSlice (desc.loopStart,   desc.endSample);

                if (headIdx >= 0 && tailIdx >= 0)
                {
                    sliceManager2.pinSliceMidiNote (headIdx, desc.midiNote);
                    sliceManager2.getSlice (headIdx).nextSliceIdx = tailIdx;

                    // Tail is intentionally left unpinned (midiMap never
                    // points to it) and marked as the actual loop region.
                    sliceManager2.getSlice (tailIdx).loopMode = 1;   // forward loop, whole-slice
                }
                else if (headIdx >= 0)
                {
                    // Tail creation failed (cap reached) — fall back to a
                    // plain one-shot head so the note still plays something.
                    sliceManager2.pinSliceMidiNote (headIdx, desc.midiNote);
                }
            }

            uiSnapshotDirty.store (true, std::memory_order_release);
        }
    }

    // ── SF2-PLAYER preview pipeline (sampleData3) ────────────────────────────
    // Mirrors the SFZ-PLAYER preview pipeline above exactly, but for the
    // SF2-PLAYER tab's own independent buffer/zone overlay.
    {
        auto* rawDecoded3 = completedLoadData3.exchange (nullptr, std::memory_order_acq_rel);
        if (rawDecoded3 != nullptr)
        {
            std::unique_ptr<SampleData::DecodedSample> decoded3 (rawDecoded3);
            sampleData3.applyDecodedSample (std::move (decoded3));
            uiSnapshotDirty.store (true, std::memory_order_release);
        }

        auto* rawZones3 = pendingPreviewZones3.exchange (nullptr, std::memory_order_acq_rel);
        if (rawZones3 != nullptr)
        {
            std::unique_ptr<SfzPreviewZonePayload> zonesOwner3 (rawZones3);
            sf2PreviewRenderedBank.store    (zonesOwner3->presetBank,    std::memory_order_relaxed);
            sf2PreviewRenderedProgram.store (zonesOwner3->presetProgram, std::memory_order_relaxed);
            sf2PreviewRenderInFlight.store  (false, std::memory_order_release);
            auto zoneList = std::make_unique<SfzPreviewZoneStore::ZoneList> (
                std::move (zonesOwner3->slices));
            previewZones3.set (std::move (zoneList));
            selectedPreviewZone3.store (-1, std::memory_order_relaxed);
            uiSnapshotDirty.store (true, std::memory_order_release);
        }
    }

    if (loadStateChanged)
        updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));

    drainCommands();

    if (gestureSnapshotCaptured)
    {
        ++blocksSinceGestureActivity;
        if (blocksSinceGestureActivity > 2)
            gestureSnapshotCaptured = false;
    }

    // Update max active voices from param
    voicePool.setMaxActiveVoices ((int) maxVoicesParam->load());

    // Apply DAW automation of sliceStart / sliceEnd to the selected slice.
    // Guard: only act when the params have already been published for the
    // currently-selected slice.  If the selection just changed, sliceStartParam
    // and sliceEndParam still hold the previous slice's values; acting on them
    // would corrupt the newly-selected slice's boundaries and cause the rapid
    // knob-jumping seen when clicking a slice name.
    if (sliceStartParam != nullptr && sliceEndParam != nullptr)
    {
        const int sel        = sliceManager.selectedSlice.load (std::memory_order_relaxed);
        const int syncedFor  = paramsSyncedForSlice.load (std::memory_order_relaxed);
        const int total      = sampleData.getNumFrames();

        if (sel >= 0 && sel < sliceManager.getNumSlices() && total > 0
            && syncedFor == sel)   // params are up-to-date for this slice
        {
            const auto sl = sliceManager.getSlice (sel);

            const float rawStart = sliceStartParam->load (std::memory_order_relaxed);
            const float rawEnd   = sliceEndParam->load   (std::memory_order_relaxed);

            // Dead-zone: ignore changes smaller than 2 MIDI CC steps (2/128).
            // This prevents a physical knob from re-applying its last position
            // after slice selection changes (the knob-jump bug).
            const float kDeadZone = 2.0f / 128.0f;
            const float pubStart  = sliceStartPublished.load (std::memory_order_relaxed);
            const float pubEnd    = sliceEndPublished.load   (std::memory_order_relaxed);
            if (std::abs (rawStart - pubStart) < kDeadZone &&
                std::abs (rawEnd   - pubEnd)   < kDeadZone)
                goto skipSliceBoundsUpdate;

            const int newStart = juce::roundToInt (rawStart * (float) total);
            const int newEnd   = juce::roundToInt (rawEnd   * (float) total);

            const int  slCurEnd     = sliceManager.getEndForSlice (sel, total);
            const bool startChanged = (newStart != sl.startSample);
            const bool endChanged   = (newEnd   != slCurEnd);

            if ((startChanged || endChanged) && newStart < newEnd)
            {
                Command cmd;
                cmd.type         = CmdSetSliceBounds;
                cmd.intParam1    = sel;
                cmd.intParam2    = newStart;
                cmd.positions[0] = newEnd;
                cmd.numPositions = 1;
                pushCommand (cmd);
            }
        }
        skipSliceBoundsUpdate:;
    }

    // ── Detect direct slice changes (not via CmdSelectSlice) ─────────────────
    // selectedSlice can be changed by direct .store() calls (pad triggers, UI
    // clicks, note-on routing) that bypass CmdSelectSlice entirely.
    // With per-slice CC state, switching slices no longer requires any reset —
    // each slice already has its own independent pickup and smoother state.
    // We only need to re-seed the FieldSliceStart smoother for the new slice
    // so pickup detection starts from the correct reference position.
    {
        const int curSel = sliceManager.selectedSlice.load (std::memory_order_relaxed);
        if (curSel != lastProcessedSlice)
        {
            markerSmootherSlice = -1;
            lastProcessedSlice  = curSel;

            // Reset pickup for the newly-selected slice so the knob must
            // re-acquire position before controlling it.  Without this,
            // ccPickedUp[curSel] stays true from prior use on that slice,
            // and the knob (physically at the previous slice's position)
            // bypasses the pickup gate and jumps the new slice's marker.
            if (curSel >= 0 && curSel < kMaxCCSlices)
            {
                for (int j = 0; j < kMidiLearnNumSlots; ++j)
                    ccPickedUp[(size_t) curSel][j] = false;
                if (curSel < 128)
                {
                    markerFinePickupCcNorm    [(size_t) curSel] = -1.0f;
                    markerFinePickupMarkerNorm[(size_t) curSel] = -1.0f;
                }
                markerFineWindowLo.store (-1.0f, std::memory_order_relaxed);
                markerFineWindowHi.store (-1.0f, std::memory_order_relaxed);
            }

            // Proactive re-seed: park the slice-start smoother at the new
            // slice's marker position before any CC arrives this buffer.
            if (curSel >= 0 && curSel < sliceManager.getNumSlices()
                && curSel < kMaxCCSlices)
            {
                ccSmoothers[(size_t) curSel][(size_t) FieldSliceStart].setCurrentAndTargetValue (
                    (float) sliceManager.getSlice (curSel).startSample);
                markerSmootherSlice = curSel;
                // ccSmootherActive stays false — smoother is seeded but not
                // running until pickup mode is satisfied.
            }
        }
    }

    // ── UI pad-click MIDI injection ───────────────────────────────────────────
    {
        const int noteOn  = uiNoteOnRequest .exchange (-1, std::memory_order_relaxed);
        const int noteOff = uiNoteOffRequest.exchange (-1, std::memory_order_relaxed);
        if (noteOn  >= 0 && noteOn  <= 127)
            midi.addEvent (juce::MidiMessage::noteOn  (1, noteOn,  (juce::uint8) 100), 0);
        if (noteOff >= 0 && noteOff <= 127)
            midi.addEvent (juce::MidiMessage::noteOff (1, noteOff, (juce::uint8) 0),   0);
    }

    // ── SF2/SFZ keyboard UI note injection ───────────────────────────────────
    // Inject on the lowest set channel of the SF player's assigned bitmask so
    // the note reaches sfzPlayer and is excluded from the slicer by the bitmask
    // check above.  If sfPlayerChannelMask == 0 the SF player is disabled; skip.
    {
        const uint32_t sfMaskInject = sfPlayerChannelMask.load (std::memory_order_relaxed);
        // Find lowest set channel (1-based bits 1..16)
        int injectCh = 1;
        for (int c = 1; c <= 16; ++c)
            if (sfMaskInject & (1u << c)) { injectCh = c; break; }
        const bool sfEnabled = (sfMaskInject != 0);

        const int noteOn  = sfzUiNoteOnRequest .exchange (-1, std::memory_order_relaxed);
        const int noteOff = sfzUiNoteOffRequest.exchange (-1, std::memory_order_relaxed);

        if (sfEnabled)
        {
            if (noteOn  >= 0 && noteOn  <= 127)
            {
                midi.addEvent (juce::MidiMessage::noteOn  (injectCh, noteOn,  (juce::uint8) 100), 0);
                const int w = noteOn < 64 ? 0 : 1;
                const int b = noteOn < 64 ? noteOn : noteOn - 64;
                sfzActiveNotes[w].fetch_or ((uint64_t)1 << b, std::memory_order_relaxed);
                sfzPlayer.juceAdsrNoteOn (noteOn);   // trigger JUCE ADSR envelope
            }
            if (noteOff >= 0 && noteOff <= 127)
            {
                const int offSample = (noteOn == noteOff)
                                    ? juce::jmax (0, buffer.getNumSamples() - 1)
                                    : 0;
                midi.addEvent (juce::MidiMessage::noteOff (injectCh, noteOff, (juce::uint8) 0), offSample);
                if (noteOff != noteOn)
                {
                    const int w = noteOff < 64 ? 0 : 1;
                    const int b = noteOff < 64 ? noteOff : noteOff - 64;
                    sfzActiveNotes[w].fetch_and (~((uint64_t)1 << b), std::memory_order_relaxed);
                }
                sfzPlayer.juceAdsrNoteOff();   // release JUCE ADSR envelope
            }
        }
    }

    // ── SFZ-Player (sfzPlayer2) keyboard UI note injection ───────────────────
    // Inject on the lowest set channel of sfzPlayer2's own channel mask
    // (sfzPlayer2ChannelMask, ch 2 by default) so the note is picked up and
    // dispatched by processMidi2() below — which also updates sfz2ActiveNotes
    // for the SFZ-Player keyboard's highlighting. Kept entirely separate from
    // the sfPlayerChannelMask-driven block above so this engine's preview
    // notes and note display never get mixed up with the legacy SF-Player's.
    {
        const uint32_t sfz2MaskInject = sfzPlayer2ChannelMask.load (std::memory_order_relaxed);
        int injectCh2 = 2;
        for (int c = 1; c <= 16; ++c)
            if (sfz2MaskInject & (1u << c)) { injectCh2 = c; break; }
        const bool sfz2Enabled = (sfz2MaskInject != 0);

        const int noteOn  = sfz2UiNoteOnRequest .exchange (-1, std::memory_order_relaxed);
        const int noteOff = sfz2UiNoteOffRequest.exchange (-1, std::memory_order_relaxed);

        if (sfz2Enabled)
        {
            if (noteOn >= 0 && noteOn <= 127)
                midi.addEvent (juce::MidiMessage::noteOn (injectCh2, noteOn, (juce::uint8) 100), 0);
            if (noteOff >= 0 && noteOff <= 127)
            {
                const int offSample = (noteOn == noteOff)
                                    ? juce::jmax (0, buffer.getNumSamples() - 1)
                                    : 0;
                midi.addEvent (juce::MidiMessage::noteOff (injectCh2, noteOff, (juce::uint8) 0), offSample);
            }
        }
    }

    // ── Snoop note messages to update active-note bitmask for keyboard display ──
    // Snoop messages on the SF player's assigned channels only.
    {
        const uint32_t sfMaskSnoop = sfPlayerChannelMask.load (std::memory_order_relaxed);

        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();
            if (sfMaskSnoop != 0)
            {
                const int ch = msg.getChannel();   // 1-based
                if (ch < 1 || ch > 16 || ! (sfMaskSnoop & (1u << ch))) continue;
            }
            const int n = msg.getNoteNumber();
            if (n < 0 || n > 127) continue;
            const int w = n < 64 ? 0 : 1;
            const int b = n < 64 ? n : n - 64;
            if (msg.isNoteOn())
                sfzActiveNotes[w].fetch_or  ((uint64_t)1 << b, std::memory_order_relaxed);
            else if (msg.isNoteOff())
                sfzActiveNotes[w].fetch_and (~((uint64_t)1 << b), std::memory_order_relaxed);
        }
    }


#if DYSEKT_STANDALONE
    // ── Sequencer MIDI injection ──────────────────────────────────────────────
    {
        juce::MidiBuffer seqEvents;
        sequencer.processBlock (seqEvents, midi, buffer.getNumSamples(), currentSampleRate);

        // Split seqEvents into slicer-bound and SF-player-bound buffers.
        // SF-player tracks emit on their FluidSynth MIDI channel (1-based = ch+1).
        // Those events must NOT reach processMidi/VoicePool — they go to sfzPlayer
        // via the normal FluidSynth channel routing.  Only slicer-track events
        // (MainSlice on ch 1, ChromaticSlice on their chromatic channel) should
        // reach processMidi.
        const uint16_t sfMask = sequencer.getAllSfPlayerChannelMask(); // bit N = ch N (0-based)
        juce::MidiBuffer slicerSeqEvents;
        if (sequencer.isPlaying())
        {
            for (const auto meta : seqEvents)
            {
                const auto msg = meta.getMessage();
                const int ch0  = msg.getChannel() - 1;  // convert to 0-based
                // Keep only events whose channel is NOT owned by an SF-player track.
                if (ch0 < 0 || ! (sfMask & (1u << ch0)))
                    slicerSeqEvents.addEvent (msg, meta.samplePosition);
                // SF-player events stay in seqEvents — sfzPlayer already receives
                // them via the full midi buffer passed to processBlock above.
            }
        }

        // Re-stamp live input to the selected track's channel so the VoicePool
        // routes correctly (chromatic slices key off channel number).
        // liveCh == 0 means an SF-player track is selected: sfzPlayer owns live
        // input via liveInputChannelMask; clear the slicer buffer entirely.
        const int liveCh = sequencer.getSelectedLiveChannel();
        if (liveCh > 0)
        {
            juce::MidiBuffer restamped;
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if (msg.getChannel() == 16) { restamped.addEvent (msg, meta.samplePosition); continue; }
                auto m = msg; m.setChannel (liveCh); restamped.addEvent (m, meta.samplePosition);
            }
            midi = restamped;
            midi.addEvents (slicerSeqEvents, 0, buffer.getNumSamples(), 0);
        }
        else
        {
            // SF-player track selected (or nothing selected): VoicePool only sees
            // slicer sequencer events — live notes go to sfzPlayer only.
            midi.clear();
            midi.addEvents (slicerSeqEvents, 0, buffer.getNumSamples(), 0);
        }
    }
#endif

    processMidi (midi);
    processMidi2 (midi);

    // ── Step CC smoothers ─────────────────────────────────────────────────────
    // Each active smoother advances toward its target over ~20 ms.
    // FieldSliceStart/End write to trim atomics or CmdSetSliceBounds depending
    // on trim mode. All other slots fire CmdSetSliceParam.
    {
        const int numSamples = buffer.getNumSamples();
        const int total      = sampleData.getNumFrames();
        const int sel        = sliceManager.selectedSlice.load (std::memory_order_relaxed);
        const bool inTrim    = trimModeActive.load (std::memory_order_relaxed);

        const int smoothSel = sel >= 0 && sel < kMaxCCSlices ? sel : -1;
        for (int i = 0; i < kMidiLearnNumSlots; ++i)
        {
            if (smoothSel < 0 || ! ccSmootherActive[(size_t) smoothSel][i]) continue;

            ccSmoothers[(size_t) smoothSel][(size_t) i].skip (numSamples);
            const float smoothed = ccSmoothers[(size_t) smoothSel][(size_t) i].getCurrentValue();

            if (i == FieldSliceStart || i == FieldTrimOut)
            {
                // Boundary smoother — target depends on mode
                if (inTrim)
                {
                    // Write smoothed position to trim atomics
                    const int curStart = trimRegionStart.load (std::memory_order_relaxed);
                    const int curEnd   = trimRegionEnd  .load (std::memory_order_relaxed);
                    if (i == FieldSliceStart)
                        trimRegionStart.store (
                            juce::jlimit (0, curEnd - 64, (int) smoothed),
                            std::memory_order_relaxed);
                    else
                        trimRegionEnd.store (
                            juce::jlimit (curStart + 64, total, (int) smoothed),
                            std::memory_order_relaxed);
                    uiSnapshotDirty.store (true, std::memory_order_release);
                }
                else if (sel >= 0 && sel < sliceManager.getNumSlices() && total > 1)
                {
                    // Use the slice index that was active when the smoother was
                    // seeded. If the user selects a different slice mid-glide,
                    // 'sel' would be wrong and would corrupt the new selection.
                    const int smoothSel = (i == FieldSliceStart && markerSmootherSlice >= 0)
                                            ? markerSmootherSlice
                                            : sel;
                    if (smoothSel < 0 || smoothSel >= sliceManager.getNumSlices())
                        break;

                    // Fire CmdSetSliceBounds with smoothed position
                    auto& sl = sliceManager.getSlice (smoothSel);
                    Command smoothCmd;
                    smoothCmd.type      = CmdSetSliceBounds;
                    smoothCmd.intParam1 = smoothSel;
                    smoothCmd.numPositions = 1;
                    if (i == FieldSliceStart)
                    {
                        const int slEnd = sliceManager.getEndForSlice (smoothSel, total);
                        smoothCmd.intParam2    = juce::jlimit (0, slEnd - 64, (int) smoothed);
                        smoothCmd.positions[0] = slEnd;
                    }
                    else
                    {
                        smoothCmd.intParam2    = sl.startSample;
                        smoothCmd.positions[0] = juce::jlimit (sl.startSample + 64, total, (int) smoothed);
                    }
                    handleCommand (smoothCmd);
                    uiSnapshotDirty.store (true, std::memory_order_release);
                }
            }
            else
            {
                // All other params — CmdSetSliceParam
                Command smoothCmd;
                smoothCmd.type        = CmdSetSliceParam;
                smoothCmd.intParam1   = i;
                smoothCmd.floatParam1 = smoothed;
                handleCommand (smoothCmd);
                uiSnapshotDirty.store (true, std::memory_order_release);
            }

            if (smoothSel >= 0 && ! ccSmoothers[(size_t) smoothSel][(size_t) i].isSmoothing())
                ccSmootherActive[(size_t) smoothSel][i] = false;
        }

        // ── Commit-on-idle for marker CC ──────────────────────────────────────
        // Once kMarkerIdleBlocks have passed with no new CC for FieldSliceStart,
        // commit the live drag position to the slice manager and clear the live drag.
       if (markerPending)
{
    ++markerIdleCounter;
    if (markerIdleCounter >= kMarkerIdleBlocks)
    {
        const int pendSel = markerPendingSlice;
        if (pendSel >= 0 && pendSel < sliceManager.getNumSlices() && total > 1)
        {
            const int newStart = liveDragBoundsStart.load (std::memory_order_relaxed);
            const int slEnd    = sliceManager.getEndForSlice (pendSel, total);
            Command cmd;
            cmd.type         = CmdSetSliceBounds;
            cmd.intParam1    = pendSel;
            cmd.intParam2    = juce::jlimit (0, slEnd - 64, newStart);
            cmd.positions[0] = slEnd;
            cmd.numPositions = 1;
            handleCommand (cmd);
            uiSnapshotDirty.store (true, std::memory_order_release);

            // --- Notify UI to optimistically update after CC/knob marker commit ---
            pendingUiOptimisticIdx.store(pendSel, std::memory_order_release);
            pendingUiOptimisticSample.store(newStart, std::memory_order_release);
        }
        // Clear liveDragSliceIdx so the live-preview block in drainCommands
        // stops overwriting startSample every block after the commit lands.
        liveDragSliceIdx.store (-1, std::memory_order_release);
        markerPending      = false;
        markerPendingSlice = -1;
        markerIdleCounter  = 0;
    }
}
    }  // end smoother block

    if (uiSnapshotDirty.exchange (false, std::memory_order_acq_rel))
    {
        publishUiSliceSnapshot();
        publishUiSliceSnapshot2();
    }

    // NOTE: previously this returned out of the ENTIRE processBlock() when
    // the Slicer had no sample loaded — which silently skipped EVERYTHING
    // downstream, including the SF2/SFZ live player's MIDI handling, audio
    // rendering, and metering, even though that engine is fully independent
    // of the Slicer and may have its own file loaded and ready to play.
    // This was the root cause of SF2-PLAYER/SFZ-PLAYER being completely
    // silent (no MIDI activity, no meters, no audio) whenever the Slicer tab
    // simply had nothing loaded in it. The Slicer-only fast/multi-out
    // rendering below is now gated on this flag directly, instead of
    // bailing out of the whole function.
    const bool slicerSampleLoaded = sampleData.isLoaded();
    if (! slicerSampleLoaded)
    {
        if (! sampleMissing.load (std::memory_order_relaxed))
            sampleAvailability.store ((int) SampleStateEmpty, std::memory_order_relaxed);
    }

    // Collect write pointers for all enabled output buses
    static constexpr int kMaxBuses = 17;
    float* busL[kMaxBuses] = {};
    float* busR[kMaxBuses] = {};
    int numActiveBuses = 0;

    for (int b = 0; b < std::min (getBusCount (false), kMaxBuses); ++b)
    {
        auto* bus = getBus (false, b);
        if (bus != nullptr && bus->isEnabled())
        {
            int chOff = getChannelIndexInProcessBlockBuffer (false, b, 0);
            if (chOff < buffer.getNumChannels())
            {
                busL[b] = buffer.getWritePointer (chOff);
                busR[b] = (chOff + 1 < buffer.getNumChannels())
                              ? buffer.getWritePointer (chOff + 1) : nullptr;
                if (b + 1 > numActiveBuses) numActiveBuses = b + 1;
            }
        }
    }

    buffer.clear();

    if (slicerSampleLoaded)
    {
    if (numActiveBuses <= 1)
    {
        // Fast path: single stereo output. Loop per-voice (instead of the
        // batched processSample()) so each voice's real rendered sample is
        // available for per-slice metering — mirrors the multi-out path
        // below, which already does this. Replaces the previous approach of
        // approximating peaks from volume × pan after the fact.
        constexpr int previewIdx = VoicePool::kPreviewVoiceIndex;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            float sL = 0.0f, sR = 0.0f;

            for (int vi = 0; vi < voicePool.getMaxActiveVoices(); ++vi)
            {
                float vL = 0.0f, vR = 0.0f;
                voicePool.processVoiceSample (vi, sampleData, currentSampleRate, vL, vR);
                sL += vL;
                sR += vR;

                // Real per-slice peak (max abs sample over the block).
                const int si = voicePool.getVoice (vi).sliceIdx;
                if (si >= 0 && si < kMaxMeterSlices)
                {
                    const float pkL = std::abs (vL);
                    const float pkR = std::abs (vR);
                    float curL = slicePeakL[si].load (std::memory_order_relaxed);
                    float curR = slicePeakR[si].load (std::memory_order_relaxed);
                    if (pkL > curL) slicePeakL[si].store (pkL, std::memory_order_relaxed);
                    if (pkR > curR) slicePeakR[si].store (pkR, std::memory_order_relaxed);
                }
            }

            // Always process the preview voice (used by LazyChopEngine), same
            // as processSample() did internally — not included in metering.
            if (previewIdx >= voicePool.getMaxActiveVoices() && voicePool.getVoice (previewIdx).active)
            {
                float vL = 0.0f, vR = 0.0f;
                voicePool.processVoiceSample (previewIdx, sampleData, currentSampleRate, vL, vR);
                sL += vL;
                sR += vR;
            }

            if (busL[0]) busL[0][i] = sanitiseSample (sL);
            if (busR[0]) busR[0][i] = sanitiseSample (sR);
        }
    }
    else
    {
        // Multi-out: route each voice to its assigned bus
        constexpr int previewIdx = VoicePool::kPreviewVoiceIndex;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            for (int vi = 0; vi < voicePool.getMaxActiveVoices(); ++vi)
            {
                float vL = 0.0f, vR = 0.0f;
                voicePool.processVoiceSample (vi, sampleData, currentSampleRate, vL, vR);

                int bus = voicePool.getVoice (vi).outputBus;
                if (bus < 0 || bus >= numActiveBuses || busL[bus] == nullptr) bus = 0;
                if (busL[bus]) busL[bus][i] += vL;
                if (busR[bus]) busR[bus][i] += vR;

                // Accumulate per-slice peak (max over block)
                const int si = voicePool.getVoice (vi).sliceIdx;
                if (si >= 0 && si < kMaxMeterSlices)
                {
                    const float pkL = std::abs (vL);
                    const float pkR = std::abs (vR);
                    float curL = slicePeakL[si].load (std::memory_order_relaxed);
                    float curR = slicePeakR[si].load (std::memory_order_relaxed);
                    if (pkL > curL) slicePeakL[si].store (pkL, std::memory_order_relaxed);
                    if (pkR > curR) slicePeakR[si].store (pkR, std::memory_order_relaxed);
                }
            }

            // Always process preview voice (LazyChopEngine) on main bus
            if (previewIdx >= voicePool.getMaxActiveVoices()
                && voicePool.getVoice (previewIdx).active)
            {
                float vL = 0.0f, vR = 0.0f;
                voicePool.processVoiceSample (previewIdx, sampleData, currentSampleRate, vL, vR);
                if (busL[0]) busL[0][i] += vL;
                if (busR[0]) busR[0][i] += vR;
            }
        }
        // Clamp / NaN-guard every active bus after accumulation
        for (int b = 0; b < numActiveBuses; ++b)
        {
            if (busL[b])
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    busL[b][i] = sanitiseSample (busL[b][i]);
            if (busR[b])
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    busR[b][i] = sanitiseSample (busR[b][i]);
        }
    }
    }  // end if (slicerSampleLoaded)


    // ── SF2/SFZ live player — dedicated audio bus ("SF2 Player"), summed to main ──
    //
    // MIDI routing — single MIDI input port (NEEDS_MIDI_INPUT=TRUE in
    // CMakeLists.txt). The slicer and the SF2/SFZ player both read from the
    // same merged `midi` buffer; channel-based routing (sfPlayerChannelMask /
    // processMidi) is the runtime split between them.
    if (buffer.getNumSamples() > 0)
    {
        const int numSamples = buffer.getNumSamples();

        // ── Build sfzMidiBuf — channel-mask split from the merged `midi` buffer ──
        juce::MidiBuffer sfzMidiBuf;
        {
            const uint32_t sfMaskBuild = sfPlayerChannelMask.load (std::memory_order_relaxed);

            if (sfMaskBuild != 0)
            {
                const bool allChannels = ((sfMaskBuild & 0x1FFFEu) == 0x1FFFEu); // bits 1-16 all set
                if (allChannels)
                {
                    sfzMidiBuf = midi;
                }
                else
                {
                    for (const auto meta : midi)
                    {
                        const auto& msg = meta.getMessage();
                        const int ch = msg.getChannel();   // 1-based
                        if (ch >= 1 && ch <= 16 && (sfMaskBuild & (1u << ch)))
                            sfzMidiBuf.addEvent (msg, meta.samplePosition);
                    }
                }
            }
        }

        // (Second "TEMP diagnostic" — same audio-thread String-build +
        // synchronous file-write pattern as above — removed for the same
        // reason; see the note near the top of processBlock().)

        // Render into a clean temp buffer — never overwrite main directly
        juce::AudioBuffer<float> sfzBuf (2, numSamples);
        sfzBuf.clear();
        float* sfzL = sfzBuf.getWritePointer (0);
        float* sfzR = sfzBuf.getWritePointer (1);

        // Track MIDI activity for the SF-player LED in SfzModulePanel.
        // Increment on NoteOn, decrement (clamped) on NoteOff.
        for (const auto meta : sfzMidiBuf)
        {
            const auto& msg = meta.getMessage();
            if (msg.isNoteOn (true))
                sfzMidiActivity.fetch_add (1, std::memory_order_relaxed);
            else if (msg.isNoteOff (true))
            {
                int prev = sfzMidiActivity.load (std::memory_order_relaxed);
                while (prev > 0 &&
                       !sfzMidiActivity.compare_exchange_weak (prev, prev - 1,
                           std::memory_order_relaxed, std::memory_order_relaxed))
                {}
            }
        }

        // Pass the clean, pre-filtered buffer; sfzPlayer's internal channel filter
        // now acts as a redundant safety check rather than the primary split point.
        sfzPlayer.process (sfzMidiBuf, sfzL, sfzR, numSamples);

        // Always sum into main bus (bus 0) — same as slice behaviour
        if (busL[0])
            for (int i = 0; i < numSamples; ++i)
                busL[0][i] += sfzL[i];
        if (busR[0])
            for (int i = 0; i < numSamples; ++i)
                busR[0][i] += sfzR[i];

        // -- Zone-preview click-to-audition (SF2-PLAYER waveform clicks) --
        // Plays back the clicked previewZones3 region directly out of
        // sampleData3's rendered buffer, independent of sfzPlayer/FluidSynth.
        {
            const int newStart = zonePreview3.triggerStart.exchange (-1, std::memory_order_acq_rel);
            if (newStart >= 0)
            {
                const int newEnd = zonePreview3.triggerEnd.load (std::memory_order_relaxed);
                zonePreview3.playPosition.store (newStart, std::memory_order_relaxed);
                zonePreview3.playEnd.store (newEnd, std::memory_order_relaxed);
            }

            int pos = zonePreview3.playPosition.load (std::memory_order_relaxed);
            if (pos >= 0)
            {
                const int end = zonePreview3.playEnd.load (std::memory_order_relaxed);
                auto snap = sampleData3.getSnapshot();
                if (snap != nullptr && end > pos)
                {
                    const auto& src      = snap->buffer;
                    const int   srcChans = src.getNumChannels();
                    const int   srcLen   = src.getNumSamples();
                    const int   playable = juce::jmin (end, srcLen) - pos;
                    const int   n        = juce::jlimit (0, numSamples, playable);

                    if (n > 0)
                    {
                        const float* srcL = src.getReadPointer (0, pos);
                        const float* srcR = (srcChans > 1) ? src.getReadPointer (1, pos) : srcL;
                        if (busL[0])
                            for (int i = 0; i < n; ++i) busL[0][i] += srcL[i];
                        if (busR[0])
                            for (int i = 0; i < n; ++i) busR[0][i] += srcR[i];
                    }

                    pos += numSamples;
                    if (pos >= juce::jmin (end, srcLen))
                        zonePreview3.playPosition.store (-1, std::memory_order_relaxed);
                    else
                        zonePreview3.playPosition.store (pos, std::memory_order_relaxed);
                }
                else
                {
                    zonePreview3.playPosition.store (-1, std::memory_order_relaxed);
                }
            }
        }

        // Also write to dedicated SF2 bus 16 if active in DAW
        constexpr int kSf2Bus = 16;
        if (kSf2Bus < numActiveBuses && busL[kSf2Bus] != nullptr)
        {
            for (int i = 0; i < numSamples; ++i)
                busL[kSf2Bus][i] += sfzL[i];
            for (int i = 0; i < numSamples; ++i)
                busR[kSf2Bus][i] += sfzR[i];
        }

        // Update SF2 peak meters for UI
        float pkL = 0.f, pkR = 0.f;
        for (int i = 0; i < numSamples; ++i)
        {
            pkL = std::max (pkL, std::abs (sfzL[i]));
            pkR = std::max (pkR, std::abs (sfzR[i]));
        }
        const float decaySFZ = 0.85f;
        sfzPeakL.store (std::max (sfzPeakL.load (std::memory_order_relaxed) * decaySFZ, pkL),
                        std::memory_order_relaxed);
        sfzPeakR.store (std::max (sfzPeakR.load (std::memory_order_relaxed) * decaySFZ, pkR),
                        std::memory_order_relaxed);
    }

    // -- SFZ-PLAYER (sliceManager2/voicePool2, ch2 default) -------------
    // A second, independent Slicer instance. Each SFZ key zone becomes a
    // real slice (see SoundFontLoader's SfzPlayer2 render target), played
    // back through voicePool2/sampleData2 exactly like the main Slicer --
    // no live synthesis engine, no chromatic mode, no manual slicing.
    // Note dispatch happens in processMidi2(); this block only renders
    // audio and updates peak meters.
    {
        const int numSamples = buffer.getNumSamples();
        constexpr int previewIdx2 = VoicePool::kPreviewVoiceIndex;
        float pk2L = 0.0f, pk2R = 0.0f;   // block-max, for the overall SFZ-Player meter

        // Loop per-voice (instead of the batched processSample()) so each
        // voice's real rendered sample is available for metering — mirrors
        // the Slicer's mono-bus fast path above. Feeds both the overall
        // sfz2PeakL/R meter and the per-slice slicePeak2L/R (SCB) meters from
        // real audio instead of the volume × pan approximation.
        for (int i = 0; i < numSamples; ++i)
        {
            float sL = 0.0f, sR = 0.0f;

            for (int vi = 0; vi < voicePool2.getMaxActiveVoices(); ++vi)
            {
                float vL = 0.0f, vR = 0.0f;
                voicePool2.processVoiceSample (vi, sampleData2, currentSampleRate, vL, vR, &sliceManager2);
                sL += vL;
                sR += vR;

                const float aL = std::abs (vL);
                const float aR = std::abs (vR);
                pk2L = std::max (pk2L, aL);
                pk2R = std::max (pk2R, aR);

                // Real per-slice peak (max abs sample over the block),
                // written straight to slicePeak2L/R so main-slicer meters
                // stay unaffected.
                const int si = voicePool2.getVoice (vi).sliceIdx;
                if (si >= 0 && si < kMaxMeterSlices)
                {
                    float curL = slicePeak2L[si].load (std::memory_order_relaxed);
                    float curR = slicePeak2R[si].load (std::memory_order_relaxed);
                    if (aL > curL) slicePeak2L[si].store (aL, std::memory_order_relaxed);
                    if (aR > curR) slicePeak2R[si].store (aR, std::memory_order_relaxed);
                }
            }

            // Always process the preview voice, same as processSample() did
            // internally — not included in metering.
            if (previewIdx2 >= voicePool2.getMaxActiveVoices() && voicePool2.getVoice (previewIdx2).active)
            {
                float vL = 0.0f, vR = 0.0f;
                voicePool2.processVoiceSample (previewIdx2, sampleData2, currentSampleRate, vL, vR, &sliceManager2);
                sL += vL;
                sR += vR;
            }

            if (busL[0]) busL[0][i] += sanitiseSample (sL);
            if (busR[0]) busR[0][i] += sanitiseSample (sR);
        }

        // Update SFZ-PLAYER overall meter for UI, with decay against the
        // previous value (same ballistics as before, now fed real peaks).
        const float decaySFZ2 = 0.85f;
        sfz2PeakL.store (std::max (sfz2PeakL.load (std::memory_order_relaxed) * decaySFZ2, pk2L),
                         std::memory_order_relaxed);
        sfz2PeakR.store (std::max (sfz2PeakR.load (std::memory_order_relaxed) * decaySFZ2, pk2R),
                         std::memory_order_relaxed);
    }   // end SFZ-PLAYER block

    // ---- Compute master output peak (sum across all output buses) ----
    // Must be after all engines (voicePool, sfzPlayer, voicePool2) have
    // written to the bus, otherwise SFZ-PLAYER audio is missed.
    {
        float masterL = 0.0f, masterR = 0.0f;
        for (int b = 0; b < numActiveBuses; ++b)
        {
            if (busL[b])
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    masterL = juce::jmax(masterL, std::abs(busL[b][i]));
            if (busR[b])
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    masterR = juce::jmax(masterR, std::abs(busR[b][i]));
        }
        masterPeakL.store(masterL, std::memory_order_relaxed);
        masterPeakR.store(masterR, std::memory_order_relaxed);
    }

    // ── Global post-mix EQ (applied to main bus only) ─────────────────────────
    if (busL[0] != nullptr && buffer.getNumSamples() > 0)
    {
        // Rebuild coefficients if any global EQ param changed
        if (globalEqNeedsUpdate)
        {
            double sr = getSampleRate();
            auto lowG  = apvts.getRawParameterValue (ParamIds::globalEqLowGain)->load();
            auto lowF  = apvts.getRawParameterValue (ParamIds::globalEqLowFreq)->load();
            auto midG  = apvts.getRawParameterValue (ParamIds::globalEqMidGain)->load();
            auto midF  = apvts.getRawParameterValue (ParamIds::globalEqMidFreq)->load();
            auto midQ  = apvts.getRawParameterValue (ParamIds::globalEqMidQ)->load();
            auto hiG   = apvts.getRawParameterValue (ParamIds::globalEqHighGain)->load();
            auto hiF   = apvts.getRawParameterValue (ParamIds::globalEqHighFreq)->load();

            *globalEq.get<0>().coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowShelf  (sr, lowF, 1.f, std::pow (10.f, lowG / 20.f));
            *globalEq.get<1>().coefficients = *juce::dsp::IIR::Coefficients<float>::makePeakFilter (sr, midF, midQ, std::pow (10.f, midG / 20.f));
            *globalEq.get<2>().coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighShelf  (sr, hiF,  1.f, std::pow (10.f, hiG  / 20.f));
            globalEqNeedsUpdate = false;
        }

        // Build a 2-channel AudioBlock over busL[0] / busR[0] and process in-place
        const int ns = buffer.getNumSamples();
        float* chans[2] = { busL[0], busR[0] ? busR[0] : busL[0] };
        juce::dsp::AudioBlock<float> eqBlock (chans, 2, (size_t) ns);
        juce::dsp::ProcessContextReplacing<float> eqCtx (eqBlock);
        globalEq.process (eqCtx);
    }

    // ── Post-EQ spectrum analyser tap ─────────────────────────────────────────
    spectrumAnalyser.pushSamples (buffer);

    // Decay all slice peak meters toward zero (60 dB/s at typical block sizes)
    static const float kDecayPerBlock = 0.60f;  // approx 60 dB/s at 512 @ 44100
    for (int si = 0; si < kMaxMeterSlices; ++si)
    {
        float v = slicePeakL[si].load (std::memory_order_relaxed) * kDecayPerBlock;
        slicePeakL[si].store (v, std::memory_order_relaxed);
        v = slicePeakR[si].load (std::memory_order_relaxed) * kDecayPerBlock;
        slicePeakR[si].store (v, std::memory_order_relaxed);
        v = slicePeak2L[si].load (std::memory_order_relaxed) * kDecayPerBlock;
        slicePeak2L[si].store (v, std::memory_order_relaxed);
        v = slicePeak2R[si].load (std::memory_order_relaxed) * kDecayPerBlock;
        slicePeak2R[si].store (v, std::memory_order_relaxed);
    }
}
juce::AudioProcessorEditor* DysektProcessor::createEditor()
{
    return new DysektEditor (*this);
}

void DysektProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream stream (destData, false);

    // Version
    stream.writeInt (25);

    // APVTS state
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    auto xmlString = xml->toString();
    stream.writeString (xmlString);

    // UI state
    stream.writeFloat (zoom.load());
    stream.writeFloat (scroll.load());
    stream.writeInt (sliceManager.selectedSlice);
    stream.writeBool (midiSelectsSlice.load());
    stream.writeInt (sliceManager.rootNote.load());

    // Slice data
    int numSlices = sliceManager.getNumSlices();
    stream.writeInt (numSlices);
    for (int i = 0; i < numSlices; ++i)
    {
        const auto& s = sliceManager.getSlice (i);
        stream.writeBool (s.active);
        stream.writeInt (s.startSample);
        stream.writeInt (sliceManager.getEndForSlice (i, sampleData.getNumFrames()));
        stream.writeInt (s.midiNote);
        stream.writeFloat (s.bpm);
        stream.writeFloat (s.pitchSemitones);
        stream.writeInt (s.algorithm);
        stream.writeFloat (s.attackSec);
        stream.writeFloat (s.decaySec);
        stream.writeFloat (s.sustainLevel);
        stream.writeFloat (s.releaseSec);
        stream.writeInt (s.muteGroup);
        stream.writeInt (s.loopMode);
        stream.writeBool (s.stretchEnabled);
        stream.writeInt ((int) s.lockMask);
        stream.writeInt ((int) s.colour.getARGB());
        // v5 fields
        stream.writeFloat (s.tonalityHz);
        stream.writeFloat (s.formantSemitones);
        stream.writeBool (s.formantComp);
        // v6 fields
        stream.writeInt (s.grainMode);
        // v7 fields
        stream.writeFloat (s.volume);
        // v10 fields
        stream.writeBool (s.releaseTail);
        // v11 fields
        stream.writeBool (s.reverse);
        stream.writeInt (s.outputBus);
        // v15 fields
        stream.writeBool (s.oneShot);
        // v16 fields
        stream.writeFloat (s.centsDetune);
        // v17 fields
        stream.writeFloat (s.pan);
        stream.writeFloat (s.filterCutoff);
        stream.writeFloat (s.filterRes);
        // v18 fields
        stream.writeInt (s.chromaticChannel);
        // v19 fields
        stream.writeBool (s.chromaticLegato);
        // v20 fields
        stream.writeString (s.name);
        // v24 fields
        stream.writeFloat (s.eqLowGain);
        stream.writeFloat (s.eqMidGain);
        stream.writeFloat (s.eqMidFreq);
        stream.writeFloat (s.eqMidQ);
        stream.writeFloat (s.eqHighGain);
    }

    // v9: store file path only (no PCM)
    stream.writeString (sampleData.getFilePath());
    stream.writeString (sampleData.getFileName());

    // v17: MIDI Learn CC mappings
    midiLearn.writeState (stream);

    // v22: SF-player state
    const juce::File sfzFile = sfzPlayer.getLoadedFile();
    stream.writeString (sfzPlayer.isLoaded() ? sfzFile.getFullPathName() : juce::String());
    stream.writeInt   (sfzPlayer.getCurrentPresetIndex());
    stream.writeFloat (sfzPlayer.getVolume());
    stream.writeInt   (sfzPlayer.getTranspose());
    stream.writeFloat (sfzPlayer.getSfzAttack());
    stream.writeFloat (sfzPlayer.getSfzDecay());
    stream.writeFloat (sfzPlayer.getSfzSustain());
    stream.writeFloat (sfzPlayer.getSfzRelease());
    stream.writeFloat (sfzPlayer.getReverbSize());
    stream.writeFloat (sfzPlayer.getReverbDamp());
    stream.writeFloat (sfzPlayer.getReverbWidth());
    stream.writeFloat (sfzPlayer.getReverbMix());
    stream.writeBool  (sfzPlayer.getReverbFreeze());
    // v23: additional sfzPlayer parameters
    stream.writeFloat (sfzPlayer.getPan());
    stream.writeFloat (sfzPlayer.getFineTune());
    // v23 originally stored a single midiChannel int here.
    // v25 replaces it with a channel range (low, high).  Both are written as
    // separate ints so old presets (v23/v24) can still be read with a fallback.
    // The channel range is derived from sfPlayerChannelMask for serialisation.
    // Channel 1 is hardwired to the slicer and never in the mask.
    {
        const uint32_t mask = sfPlayerChannelMask.load (std::memory_order_relaxed);
        int lo = 0, hi = 0;
        if (mask != 0)
        {
            for (int c = 2; c <= 16; ++c)  if (mask & (1u << c)) { lo = c; break; }
            for (int c = 16; c >= 2; --c)  if (mask & (1u << c)) { hi = c; break; }
        }
        stream.writeInt (lo);
        stream.writeInt (hi);
    }

    // v26: SFZ-Player (sfzPlayer2) channel range
    {
        const uint32_t mask2 = sfzPlayer2ChannelMask.load (std::memory_order_relaxed);
        int lo2 = 0, hi2 = 0;
        if (mask2 != 0)
        {
            for (int c = 2; c <= 16; ++c)  if (mask2 & (1u << c)) { lo2 = c; break; }
            for (int c = 16; c >= 2; --c)  if (mask2 & (1u << c)) { hi2 = c; break; }
        }
        if (lo2 == 0) { lo2 = 2; hi2 = 2; }   // default ch2
        stream.writeInt (lo2);
        stream.writeInt (hi2);
    }
#if DYSEKT_STANDALONE
    sequencer.writeToStream (stream);
#endif
}

void DysektProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream (data, (size_t) sizeInBytes, false);

    int version = stream.readInt();
    if (version < 16 || version > 25)
        return;

    // APVTS state
    auto xmlString = stream.readString();
    if (auto xml = juce::parseXML (xmlString))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));

    // UI state
    zoom.store (juce::jlimit (1.0f, 16384.0f, stream.readFloat()));
    scroll.store (juce::jlimit (0.0f, 1.0f, stream.readFloat()));
    int savedSelectedSlice = stream.readInt();

    midiSelectsSlice.store (stream.readBool());
    if (version <= 17) stream.readBool();  // v17 had chromaticMode global bool here — discard
    sliceManager.rootNote.store (juce::jlimit (0, 127, stream.readInt()));

    // Slice data
    const int storedNumSlices = stream.readInt();
    if (storedNumSlices < 0 || storedNumSlices > 4096)
        return;

    const int validatedNumSlices = juce::jlimit (0, SliceManager::kMaxSlices, storedNumSlices);
    sliceManager.setNumSlices (validatedNumSlices);
    sliceManager.selectedSlice = juce::jlimit (-1, validatedNumSlices - 1, savedSelectedSlice);

    for (int i = 0; i < storedNumSlices; ++i)
    {
        Slice parsed;
        parsed.active         = stream.readBool();
        parsed.startSample    = stream.readInt();
        stream.readInt();  // legacy endSample — marker model derives this at runtime
        parsed.midiNote       = stream.readInt();
        parsed.bpm            = stream.readFloat();
        parsed.pitchSemitones = stream.readFloat();
        parsed.algorithm      = stream.readInt();
        parsed.attackSec      = stream.readFloat();
        parsed.decaySec       = stream.readFloat();
        parsed.sustainLevel   = stream.readFloat();
        parsed.releaseSec     = stream.readFloat();
        parsed.muteGroup      = stream.readInt();
        parsed.loopMode       = stream.readInt();
        parsed.stretchEnabled = stream.readBool();
        parsed.lockMask       = (uint32_t) stream.readInt();
        parsed.colour         = juce::Colour ((juce::uint32) stream.readInt());
        parsed.tonalityHz     = stream.readFloat();
        parsed.formantSemitones = stream.readFloat();
        parsed.formantComp    = stream.readBool();
        parsed.grainMode      = stream.readInt();
        parsed.volume         = stream.readFloat();
        parsed.releaseTail    = stream.readBool();
        parsed.reverse        = stream.readBool();
        parsed.outputBus      = stream.readInt();
        parsed.oneShot        = stream.readBool();
        parsed.centsDetune    = stream.readFloat();
        // v17 fields (with defaults for v16 presets)
        parsed.pan          = (version >= 17) ? stream.readFloat() : 0.0f;
        parsed.filterCutoff      = (version >= 17) ? stream.readFloat() : 20000.0f;
        parsed.filterRes         = (version >= 17) ? stream.readFloat() : 0.0f;
        // v18 fields
        parsed.chromaticChannel  = (version >= 18) ? stream.readInt() : 0;
        // v19 fields
        parsed.chromaticLegato   = (version >= 19) ? stream.readBool() : false;
        // v20 fields
        parsed.name              = (version >= 20) ? stream.readString() : juce::String();
        // v24 fields
        parsed.eqLowGain         = (version >= 24) ? stream.readFloat() : 0.0f;
        parsed.eqMidGain         = (version >= 24) ? stream.readFloat() : 0.0f;
        parsed.eqMidFreq         = (version >= 24) ? stream.readFloat() : 1000.0f;
        parsed.eqMidQ            = (version >= 24) ? stream.readFloat() : 1.0f;
        parsed.eqHighGain        = (version >= 24) ? stream.readFloat() : 0.0f;

        if (i < validatedNumSlices)
            sliceManager.getSlice (i) = sanitiseRestoredSlice (parsed);
    }

    for (int i = validatedNumSlices; i < SliceManager::kMaxSlices; ++i)
        sliceManager.getSlice (i).active = false;

    // Path-based sample restore
    auto filePath = stream.readString();
    auto fileName = stream.readString();

    clearVoicesBeforeSampleSwap();
    sampleData.clear();

    if (filePath.isNotEmpty())
    {
        const juce::File restoredFile (filePath);
        sampleMissing.store (false);
        missingFilePath.clear();
        sampleData.setFileName (fileName.isNotEmpty() ? fileName : restoredFile.getFileName());
        sampleData.setFilePath (filePath);
        sampleAvailability.store ((int) SampleStateEmpty, std::memory_order_relaxed);
        // Preserve restored slices while loading, and report missing path via relink state.
        requestSampleLoad (restoredFile, LoadKindRelink);
    }
    else
    {
        sampleMissing.store (false);
        missingFilePath.clear();
        sampleData.setFileName ({});
        sampleData.setFilePath ({});
        sampleAvailability.store ((int) SampleStateEmpty, std::memory_order_relaxed);
    }

    sliceManager.rebuildMidiMap();
    publishUiSliceSnapshot();

    if (version < 21) stream.readBool();  // v20 had snapToZeroCrossing toggle — removed, always on

    // v17: MIDI Learn CC mappings (optional — older presets simply leave all unassigned)
    if (! stream.isExhausted())
        midiLearn.readState (stream);

    // v22: SF-player state — restore file, preset, and all knob values
    if (version >= 22 && ! stream.isExhausted())
    {
        const auto sfzPath = stream.readString();
        const int  sfzPresetIdx = stream.readInt();
        const float sfzVol    = stream.readFloat();
        const int   sfzTrans  = stream.readInt();
        const float sfzAtk    = stream.readFloat();
        const float sfzDec    = stream.readFloat();
        const float sfzSus    = stream.readFloat();
        const float sfzRel    = stream.readFloat();
        const float sfzRvSz   = stream.readFloat();
        const float sfzRvDp   = stream.readFloat();
        const float sfzRvWd   = stream.readFloat();
        const float sfzRvMx   = stream.readFloat();
        const bool  sfzRvFrz  = stream.readBool();

        // Restore knob values unconditionally (they apply whenever a file loads)
        sfzPlayer.setVolume      (sfzVol);
        sfzPlayer.setTranspose   (sfzTrans);
        sfzPlayer.setSfzAttack   (sfzAtk);
        sfzPlayer.setSfzDecay    (sfzDec);
        sfzPlayer.setSfzSustain  (sfzSus);
        sfzPlayer.setSfzRelease  (sfzRel);
        sfzPlayer.setReverbSize  (sfzRvSz);
        sfzPlayer.setReverbDamp  (sfzRvDp);
        sfzPlayer.setReverbWidth (sfzRvWd);
        sfzPlayer.setReverbMix   (sfzRvMx);
        sfzPlayer.setReverbFreeze(sfzRvFrz);

        // v23/v24: Pan, FineTune, MidiChannel (single int)
        // v25:     Pan, FineTune, sfPlayerChLow, sfPlayerChHigh (two ints)
        if (version >= 23 && ! stream.isExhausted())
        {
            sfzPlayer.setPan      (stream.readFloat());
            sfzPlayer.setFineTune (stream.readFloat());

            if (version >= 25)
            {
                // New range-based routing — convert lo/hi range back to bitmask.
                // Channel 1 is hardwired to the slicer; clamp lo to 2 minimum.
                const int lo = stream.readInt();
                const int hi = stream.readInt();
                uint32_t mask = 0u;
                if (lo >= 1 && hi >= lo)
                    for (int c = juce::jmax (lo, 2); c <= juce::jmin (hi, 16); ++c)
                        mask |= (1u << c);
                sfPlayerChannelMask.store (mask, std::memory_order_relaxed);
            }
            else
            {
                // v23/v24: single-channel value — map to one-channel mask,
                // or the hardcoded ch3 default if it was 0 (old omni mode).
                // Channel 1 is hardwired to the slicer; never set bit 1.
                const int oldCh = stream.readInt();
                uint32_t mask = 0u;
                if (oldCh >= 2 && oldCh <= 16)
                    mask = (1u << oldCh);
                else if (oldCh == 0 || oldCh == 1)
                    mask = (1u << 3);   // legacy omni → hardcoded ch3 default
                sfPlayerChannelMask.store (mask, std::memory_order_relaxed);
            }
        }

        // v26: SFZ-Player channel range
        if (! stream.isExhausted())
        {
            const int lo2 = stream.readInt();
            const int hi2 = stream.readInt();
            uint32_t mask2 = 0u;
            if (lo2 >= 2 && hi2 >= lo2)
                for (int c = juce::jmax (lo2, 2); c <= juce::jmin (hi2, 16); ++c)
                    mask2 |= (1u << c);
            if (mask2 == 0u) mask2 = (1u << 2);   // default ch2
            sfzPlayer2ChannelMask.store      (mask2, std::memory_order_relaxed);
            savedSfzPlayer2ChannelMask.store (mask2, std::memory_order_relaxed);
        }
        if (sfzPath.isNotEmpty())
        {
            const juce::File sfzFile (sfzPath);
            if (sfzFile.existsAsFile())
            {
                sfzPlayer.loadFile (sfzFile, fileLoadPool);
                loadSoundFontAsync (sfzFile, SoundFontLoadTarget::SfPlayer);   // waveform preview -> sampleData3
                // Store the preset index so the audio thread can select it
                // once the soundfont finishes loading and posts its preset list.
                sfzPlayer.setPresetByIndex (sfzPresetIdx);
                pendingSfzPresetIndex.store (sfzPresetIdx, std::memory_order_relaxed);
            }
        }
    }

    // Sequencer state (graceful — older saves won't have this block)
#if DYSEKT_STANDALONE
    if (! stream.isExhausted())
        sequencer.readFromStream (stream);
#endif

    // Mirror the restored sfPlayerChannelMask into savedSfPlayerChannelMask so
    // that switching to Slicer mode and back preserves the loaded channel range.
    savedSfPlayerChannelMask.store (
        sfPlayerChannelMask.load (std::memory_order_relaxed),
        std::memory_order_relaxed);

    // Rebuild chromaticSliceChannelMask now that all slices are restored.
    // This also evicts any chromatic-owned channels from both SF masks.
    rebuildChromaticChannelMask();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return static_cast<juce::AudioProcessor*> (new DysektProcessor());
}

void DysektProcessor::loadDefaultSampleIfNeeded()
{
    if (defaultSampleScheduled)
        return;

    defaultSampleScheduled = true;

    // Write BinaryData::Empty_wav to a temp file and load it.
    // This ensures the plugin opens with a sample already loaded.
    auto tempFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
                        .getChildFile ("DYSEKT_default.wav");

    if (! tempFile.existsAsFile())
    {
        tempFile.replaceWithData (BinaryData::Empty_wav,
                                  (size_t) BinaryData::Empty_wavSize);
    }

    if (tempFile.existsAsFile())
        loadFileAsync (tempFile);
}
