#include "VoicePool.h"

// Include Signalsmith Stretch
#include "signalsmith-stretch.h"

#include <algorithm>
#include <cmath>

static constexpr int kStretchBlockSize = 128; // required block size for Signalsmith Stretch processing
static constexpr int kMaxStretchInputSamples = 8192; // max pre-roll/input feed size (empirically tuned)

static inline float dbToLinear (float dB)
{
    if (dB <= -100.0f) return 0.0f;
    return std::pow (10.0f, dB / 20.0f);
}


VoicePool::VoicePool()
{
    for (auto& p : voicePositions)
        p.store (0.0f, std::memory_order_relaxed);

    for (int i = 0; i < kMaxVoices; ++i)
    {
        legatoTargetSpeed [i].store (1.0f,  std::memory_order_relaxed);
        legatoTargetSemis [i].store (0.0f,  std::memory_order_relaxed);
        legatoPitchGliding[i].store (false, std::memory_order_relaxed);
    }

    for (auto& v : voices)
    {
        v.stretchInBufL.resize (kMaxStretchInputSamples);
        v.stretchInBufR.resize (kMaxStretchInputSamples);
        v.stretchOutBufL.resize (kStretchBlockSize);
        v.stretchOutBufR.resize (kStretchBlockSize);
    }
}

int VoicePool::allocate()
{
    // First pass: find inactive voice within maxActive range
    auto it = std::find_if (voices.begin(), voices.begin() + maxActive,
                            [] (const Voice& v) { return ! v.active; });
    if (it != voices.begin() + maxActive)
        return (int) std::distance (voices.begin(), it);

    // Second pass: steal — prefer releasing voices with lowest envelope
    int best = 0;
    float bestScore = 999999.0f;

    for (int i = 0; i < maxActive; ++i)
    {
        float score = voices[i].envelope.getLevel();
        if (voices[i].envelope.getState() == AdsrEnvelope::Release)
            score -= 10.0f;
        if (score < bestScore)
        {
            bestScore = score;
            best = i;
        }
    }

    return best;
}

void VoicePool::setMaxActiveVoices (int n)
{
    n = juce::jlimit (1, kMaxVoices - 1, n);
    if (n < maxActive)
    {
        // Kill voices beyond new limit (preview is permanently reserved).
        constexpr int previewIdx = kPreviewVoiceIndex;
        for (int i = n; i < maxActive; ++i)
        {
            if (i == previewIdx) continue;
            voices[i].active = false;
            voicePositions[i].store (0.0f, std::memory_order_relaxed);
        }
    }
    maxActive = n;
}

void VoicePool::initStretcher (Voice& v, float pitchSemis, double sr,
                                float tonalityHz, float formantSemis,
                                const SampleData& sample)
{
    int blockSize = std::max (256, (int)(sr * 0.023)); // ~1024 @ 44.1k (~23ms)
    int interval  = std::max (64,  (int)(sr * 0.006)); // ~256  @ 44.1k (~6ms)

    if (! v.stretcher)
        v.stretcher = std::make_shared<signalsmith::stretch::SignalsmithStretch<float>>();
    v.stretcher->configure (2, blockSize, interval, false);
    v.stretcher->reset();  // flush stale STFT state from any prior voice that used this slot

    float tonalityLimit = (tonalityHz > 0.0f && sr > 0.0) ? (float)(tonalityHz / sr) : 0.0f;
    v.stretcher->setTransposeSemitones (pitchSemis, tonalityLimit);

    if (formantSemis != 0.0f)
        v.stretcher->setFormantSemitones (formantSemis, false);

    v.stretchOutReadPos = 0;
    v.stretchOutAvail   = 0;

    // Pre-roll: prime the pipeline so first output isn't silence
    float playbackRate = v.stretchTimeRatio;
    int seekLen = v.stretcher->outputSeekLength (playbackRate);
    seekLen = std::min (seekLen, v.endSample - v.startSample);
    seekLen = juce::jlimit (0, (int) v.stretchInBufL.size(), seekLen);

    int maxFrame = sample.getNumFrames() - 1;
    if (seekLen > 0 && sample.isLoaded() && maxFrame >= 0)
    {
        for (int i = 0; i < seekLen; ++i)
        {
            int srcIdx = (v.direction > 0)
                         ? v.startSample + i
                         : v.endSample - 1 - i;
            srcIdx = juce::jlimit (0, maxFrame, srcIdx);
            v.stretchInBufL[(size_t) i] = sample.getInterpolatedSample (srcIdx, 0);
            v.stretchInBufR[(size_t) i] = sample.getInterpolatedSample (srcIdx, 1);
        }
        float* ptrs[2] = { v.stretchInBufL.data(), v.stretchInBufR.data() };
        v.stretcher->outputSeek (ptrs, seekLen);
        if (v.direction > 0)
            v.stretchSrcPos = v.startSample + seekLen;
        else
            v.stretchSrcPos = v.endSample - 1 - seekLen;
    }
}

// ────────────────────────────────────────────────────────────────[...]
// startVoiceUnsliced — chromatic playback with explicit bounds, no slice needed.
// Used for trim-mode and unsliced-sample playback. All params come from globals.
// ────────────────────────────────────────────────────────────────[...]
void VoicePool::startVoiceUnsliced (int voiceIdx, const VoiceStartParams& p,
                                    int startSample, int endSample,
                                    const SampleData& sample)
{
    auto& v = voices[voiceIdx];

    v.active     = true;
    v.sliceIdx   = -1;
    v.midiNote   = p.note;
    v.velocity   = p.velocity / 127.0f;
    v.startSample = startSample;
    v.endSample   = endSample;
    v.bufferEnd   = sample.getNumFrames();
    v.muteGroup   = p.globalMuteGroup;
    v.looping     = false;
    v.pingPong    = false;
    v.outputBus   = 0;
    v.releaseTail = p.globalReleaseTail;
    v.oneShot     = p.globalOneShot;

    v.direction = p.globalReverse ? -1 : 1;
    v.position  = p.globalReverse ? (double)(endSample - 1) : (double)startSample;

    v.envelope.noteOn (p.globalAttackSec, p.globalDecaySec,
                       p.globalSustain, p.globalReleaseSec, sampleRate,
                       p.globalHoldSec, p.globalOneShot > 0.5f);

    // Pitch: already baked in as p.globalPitch = basePitch + semitoneOffset
    const float pitchSt    = p.globalPitch + p.globalCentsDetune / 100.0f;
    const float pitchRatio = std::pow (2.0f, pitchSt / 12.0f);

    v.volume = dbToLinear (p.globalVolume);

    float panAngle = (p.globalPan + 1.0f) * 0.5f * juce::MathConstants<float>::halfPi;
    v.panL = std::cos (panAngle);
    v.panR = std::sin (panAngle);

    float w = 2.0f * juce::MathConstants<float>::pi
              * juce::jlimit (20.0f, 20000.0f, p.globalFilterCutoff) / (float)sampleRate;
    v.filterCoeff  = juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-w));
    v.filterRes    = juce::jlimit (0.0f, 1.0f, p.globalFilterRes);
    v.filterStateL = 0.0f;
    v.filterStateR = 0.0f;

    // Algorithm: derived from stretch flag — no user-facing algo selector needed.
    // Stretch ON  → Signalsmith (pitch-only; BPM time-stretch not available in unsliced mode)
    // Stretch OFF → Repitch (speed = pitchRatio)
    v.stretchActive = false;

    const float tonality = p.globalTonality;
    const float formant  = p.globalFormant;

    if (p.globalStretch)
    {
        initStretcher (v, pitchSt, sampleRate, tonality, formant, sample);
    }
    else
    {
        v.speed = (double) pitchRatio;
    }

    if (! v.stretchActive)
        v.speed = (double) pitchRatio;
}

void VoicePool::startVoice (int voiceIdx, const VoiceStartParams& p,
                             SliceManager& sm, const SampleData& sample)
{
    auto& v = voices[voiceIdx];
    const int sliceIdx = p.sliceIdx;
    const auto& s = sm.getSlice (sliceIdx);

    v.active   = true;
    v.sliceIdx = sliceIdx;
    v.midiNote = p.note;
    v.velocity = p.velocity / 127.0f;

    v.startSample = s.startSample;
    // Marker model: derive end from the next slice's start (or buffer end).
    const int sliceEnd = sm.getEndForSlice (sliceIdx, sample.getNumFrames());
    v.endSample = sliceEnd;

    // Resolve parameters via inheritance
    // ADSR always reads per-slice — lock = protection only, not inheritance
    float attack  = s.attackSec;
    float decay   = s.decaySec;
    float sustain = s.sustainLevel;
    float release = s.releaseSec;

    // Attack and Decay are NOT clamped — both knobs have full range in all modes.
    // Safety net: if the envelope is still active when sample data runs out, the
    // end-of-sample path calls forceRelease(kShortReleaseSec) to terminate cleanly.

    // Resolve one-shot flag: slice value takes priority over global when set,
    // regardless of lock bit — one-shot is a per-slice property, not a global default.
    // (globalOneShot is intentionally always false; see PluginProcessor.)
    const bool isOneShot = s.oneShot || (p.globalOneShot > 0.5f);

    // In one-shot mode, default decay to the natural slice duration so the
    // envelope rides the full sample without the user needing to adjust anything.
    if (isOneShot && decay < 0.0001f)
        decay = (float)(sliceEnd - s.startSample) / (float) sampleRate;

    // Resolve hold time for this slice.
    const float holdSec = sm.resolveParam (sliceIdx, kLockHold,
                                           s.holdSec,
                                           p.globalHoldSec);

    v.envelope.noteOn (attack, decay, sustain, release, sampleRate,
                       holdSec, isOneShot);

    int resolvedLoopMode = (int) sm.resolveParam (sliceIdx, kLockLoop, (float) s.loopMode, (float) p.globalLoopMode);
    v.looping  = (resolvedLoopMode == 1);
    v.pingPong = (resolvedLoopMode == 2);
    v.muteGroup = (int) sm.resolveParam (sliceIdx, kLockMuteGroup, (float) s.muteGroup, (float) p.globalMuteGroup);

    bool rev = sm.resolveParam (sliceIdx, kLockReverse,
                                s.reverse ? 1.0f : 0.0f,
                                p.globalReverse ? 1.0f : 0.0f) > 0.5f;
    v.direction = rev ? -1 : 1;
    v.position  = rev ? (sliceEnd - 1) : s.startSample;

    v.outputBus = (int) sm.resolveParam (sliceIdx, kLockOutputBus, (float) s.outputBus, 0.0f);

    // algo is now derived from stretchOn — removed per-slice algo param

    float sliceBpm = sm.resolveParam (sliceIdx, kLockBpm,        s.bpm,           p.globalBpm);
    float pitchSt  = s.pitchSemitones + p.globalPitch;
    float cents    = s.centsDetune    + p.globalCentsDetune;
    float pitch    = pitchSt + cents / 100.0f;
    float pitchRatio = std::pow (2.0f, pitch / 12.0f);

    bool stretchOn = sm.resolveParam (sliceIdx, kLockStretch,
                                      s.stretchEnabled ? 1.0f : 0.0f,
                                      p.globalStretch ? 1.0f : 0.0f) > 0.5f;

    float tonality = sm.resolveParam (sliceIdx, kLockTonality,    s.tonalityHz,        p.globalTonality);
    float formant  = sm.resolveParam (sliceIdx, kLockFormant,     s.formantSemitones,  p.globalFormant);

    // grainMode / hopAdj removed — Grain algo was a duplicate of Tonal

    v.volume = dbToLinear (sm.resolveParam (sliceIdx, kLockVolume, s.volume, p.globalVolume));

    // ── Pan (constant-power law) ──────────────────────────────────────────────
    float resolvedPan = sm.resolveParam (sliceIdx, kLockPan, s.pan, p.globalPan);
    resolvedPan = juce::jlimit (-1.0f, 1.0f, resolvedPan);
    float panAngle = (resolvedPan + 1.0f) * 0.5f * juce::MathConstants<float>::halfPi;
    v.panL = std::cos (panAngle);
    v.panR = std::sin (panAngle);

    // ── Filter (one-pole IIR low-pass with resonance feedback) ───────────────
    float resolvedCutoff = sm.resolveParam (sliceIdx, kLockFilter, s.filterCutoff, p.globalFilterCutoff);
    float resolvedFRes   = sm.resolveParam (sliceIdx, kLockFilter, s.filterRes,    p.globalFilterRes);
    resolvedCutoff = juce::jlimit (20.0f, 20000.0f, resolvedCutoff);
    float w = 2.0f * juce::MathConstants<float>::pi * resolvedCutoff / (float) sampleRate;
    v.filterCoeff  = juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-w));
    v.filterRes    = juce::jlimit (0.0f, 1.0f, resolvedFRes);
    v.filterStateL = 0.0f;
    v.filterStateR = 0.0f;

    // ── Per-slice 3-band EQ (chowdsp SVF — prepared once at note-on) ──────
    {
        float lowG  = sm.resolveParam (sliceIdx, kLockEqLow,  s.eqLowGain,  p.globalEqLowGain);
        float midG  = sm.resolveParam (sliceIdx, kLockEqMid,  s.eqMidGain,  p.globalEqMidGain);
        float midF  = sm.resolveParam (sliceIdx, kLockEqMid,  s.eqMidFreq,  p.globalEqMidFreq);
        float midQ  = sm.resolveParam (sliceIdx, kLockEqMid,  s.eqMidQ,     p.globalEqMidQ);
        float hiG   = sm.resolveParam (sliceIdx, kLockEqHigh, s.eqHighGain, p.globalEqHighGain);

        constexpr float kBypassThreshold = 0.01f; // dB — treat as bypass below this
        v.eqActive = (std::abs(lowG) > kBypassThreshold ||
                      std::abs(midG) > kBypassThreshold ||
                      std::abs(hiG)  > kBypassThreshold);

        if (v.eqActive)
        {
            // maxBlockSize=1: DYSEKT processes sample-by-sample via processSample()
            const juce::dsp::ProcessSpec spec { sampleRate, 1, 2 };

            v.eqLowShelf.prepare (spec);
            v.eqLowShelf.reset();
            v.eqLowShelf.setCutoffFrequency (200.f);
            v.eqLowShelf.setGainDecibels (lowG);
            v.eqLowShelf.setQValue (0.707f);

            v.eqMid.prepare (spec);
            v.eqMid.reset();
            v.eqMid.setCutoffFrequency (juce::jlimit (200.f, 8000.f, midF));
            v.eqMid.setGainDecibels (midG);
            v.eqMid.setQValue (juce::jlimit (0.5f, 4.f, midQ));

            v.eqHighShelf.prepare (spec);
            v.eqHighShelf.reset();
            v.eqHighShelf.setCutoffFrequency (8000.f);
            v.eqHighShelf.setGainDecibels (hiG);
            v.eqHighShelf.setQValue (0.707f);
        }
    }

    v.releaseTail = sm.resolveParam (sliceIdx, kLockReleaseTail,
                                     s.releaseTail ? 1.0f : 0.0f,
                                     p.globalReleaseTail ? 1.0f : 0.0f) > 0.5f;
    v.oneShot = isOneShot ? 1.0f : 0.0f;  // already resolved before envelope noteOn
    v.bufferEnd = sample.getNumFrames();

    // Reset stretch state (guard against stale data from stolen voices)
    v.stretchActive = false;
    v.startedViaChromaticLegato = p.chromaticLegatoTrigger;

    // Reset any in-flight legato pitch glide from a previous note
    legatoPitchGliding[voiceIdx].store (false, std::memory_order_relaxed);

    // Routing is now derived from stretchOn flag — no separate algo param.
    //   BPM stretch active  → Signalsmith: independent pitch + time
    //   Stretch ON, no BPM  → Signalsmith: pitch-only (time constant — no Mickey Mouse)
    //   Chromatic legato    → Signalsmith: pitch-only regardless of stretch flag
    //   Stretch OFF         → Repitch: direct speed = pitchRatio
    if (stretchOn && p.dawBpm > 0.0f && sliceBpm > 0.0f)
    {
        float speedRatio = p.dawBpm / sliceBpm;

        // Always use Signalsmith for independent pitch + time
        v.stretchActive     = true;
        v.speed             = 1.0;
        v.stretchTimeRatio  = speedRatio;
        v.stretchPitchSemis = pitch;
        v.stretchSrcPos     = rev ? (sliceEnd - 1) : s.startSample;

        initStretcher (v, pitch, sampleRate, tonality, formant, sample);
    }
    else
    {
        if (stretchOn || p.chromaticLegatoTrigger)
        {
            // Stretch ON without BPM, or chromatic legato override:
            // use Signalsmith for pitch-only (time stays constant)
            v.stretchActive     = true;
            v.speed             = 1.0;
            v.stretchTimeRatio  = 1.0f;
            v.stretchPitchSemis = pitch;
            v.stretchSrcPos     = rev ? (sliceEnd - 1) : s.startSample;

            initStretcher (v, pitch, sampleRate, tonality, formant, sample);
        }
        else
        {
            // Repitch: direct speed change
            v.speed = pitchRatio;
        }
    }
}

void VoicePool::releaseNote (int note)
{
    for (int i = 0; i < maxActive; ++i)
    {
        if (voices[i].active && voices[i].midiNote == note)
        {
            if (voices[i].oneShot && !voices[i].looping)
                continue; // ignore note-off; voice plays through to endSample
                          // (but NOT for looping voices — they have no natural end)
            voices[i].envelope.noteOff();
        }
    }
}

void VoicePool::releaseNoteForced (int note)
{
    for (int i = 0; i < maxActive; ++i)
        if (voices[i].active && voices[i].midiNote == note && !voices[i].oneShot)
            voices[i].envelope.forceRelease (kKillReleaseSec, sampleRate);
}

void VoicePool::releaseAll()
{
    for (int i = 0; i < maxActive; ++i)
        if (voices[i].active)
            voices[i].envelope.forceRelease (kShortReleaseSec, sampleRate);
}

void VoicePool::killAll()
{
    for (int i = 0; i < maxActive; ++i)
        if (voices[i].active)
            voices[i].envelope.forceRelease (kKillReleaseSec, sampleRate);
}

void VoicePool::killVoicesForChromaticLegato (int sliceIdx)
{
    // Steal only voices that were themselves started by a chromatic legato
    // trigger on this same slice. Normal (non-chromatic) voices are left alone.
    for (int i = 0; i < maxActive; ++i)
    {
        auto& v = voices[i];
        if (v.active && v.sliceIdx == sliceIdx && v.startedViaChromaticLegato)
            v.envelope.forceRelease (kKillReleaseSec, sampleRate);
    }
}

// ────────────────────────────────────────────────────────────────[...]
// retuneChromaticLegatoVoice — true sample-through legato pitch update.
//
// Instead of killing the old voice and restarting from the slice head, we find
// the active chromatic-legato voice and update only its pitch, leaving the
// playback position completely untouched. This gives smooth, continuous sample
// playback while the pitch follows the incoming MIDI notes.
//
// Stretch mode  : calls stretcher->setTransposeSemitones() live — no re-init.
// Repitch mode  : recalculates v.speed from the new semitone ratio.
//
// Returns true  → caller should NOT start a new voice.
// Returns false → no active legato voice found; caller must start fresh.
// ────────────────────────────────────────────────────────────────[...]
bool VoicePool::retuneChromaticLegatoVoice (int sliceIdx, float newPitchSemis,
                                             float tonalityHz, int newMidiNote)
{
    for (int i = 0; i < maxActive; ++i)
    {
        auto& v = voices[i];
        if (! v.active || v.sliceIdx != sliceIdx || ! v.startedViaChromaticLegato)
            continue;

        // Found an active legato voice on this slice — retune it in place.
        v.midiNote = newMidiNote;

        if (v.stretchActive && v.stretcher)
        {
            // Stretch / pitch-only stretch mode:
            // ARM a glide — processVoiceSample will ramp stretchPitchSemis toward
            // the target one block at a time, eliminating click/smear artifacts.
            legatoTargetSemis [i].store (newPitchSemis, std::memory_order_relaxed);
            legatoPitchGliding[i].store (true,          std::memory_order_relaxed);
        }
        else
        {
            // Repitch mode: ARM a speed glide — processVoiceSample ramps v.speed
            // per sample toward the target, preventing phase-discontinuity clicks.
            const float pitchRatio = std::pow (2.0f, newPitchSemis / 12.0f);
            legatoTargetSpeed [i].store (pitchRatio, std::memory_order_relaxed);
            legatoPitchGliding[i].store (true,       std::memory_order_relaxed);
        }

        return true; // retuned — no new voice needed
    }

    return false; // no active legato voice found — caller should start fresh
}

void VoicePool::muteGroup (int group, int exceptVoice)
{
    if (group <= 0)
        return;

    for (int i = 0; i < maxActive; ++i)
    {
        if (i != exceptVoice && voices[i].active && voices[i].muteGroup == group)
            voices[i].envelope.forceRelease (kKillReleaseSec, sampleRate);
    }
}

static void fillStretchBlock (Voice& v, const SampleData& sample)
{
    int inputSamples = (int) (kStretchBlockSize * v.stretchTimeRatio);
    if (inputSamples < 1) inputSamples = 1;
    const int maxInput = std::min ((int) v.stretchInBufL.size(), (int) v.stretchInBufR.size());
    if (maxInput <= 0)
        return;
    inputSamples = juce::jlimit (1, maxInput, inputSamples);

    for (int i = 0; i < inputSamples; ++i)
    {
        double pos = v.stretchSrcPos;
        // Clamp to sample buffer bounds for safety
        pos = juce::jlimit (0.0, (double) v.bufferEnd - 1, pos);
        v.stretchInBufL[(size_t) i] = sample.getInterpolatedSample (pos, 0);
        v.stretchInBufR[(size_t) i] = sample.getInterpolatedSample (pos, 1);
        v.stretchSrcPos += (double) v.direction;

        // Check bounds
        if (v.direction > 0 && v.stretchSrcPos >= v.endSample)
        {
            if (v.pingPong)
            {
                v.stretchSrcPos = v.endSample - 1;
                v.direction = -1;
            }
            else if (v.looping)
            {
                v.stretchSrcPos = v.startSample;
            }
            else if (v.releaseTail && v.stretchSrcPos < v.bufferEnd)
            {
                v.stretchSrcPos = std::min (v.stretchSrcPos, (double) v.bufferEnd - 1);
            }
            else
            {
                float lastL = v.stretchInBufL[(size_t) i];
                float lastR = v.stretchInBufR[(size_t) i];
                for (int j = i + 1; j < inputSamples; ++j)
                {
                    v.stretchInBufL[(size_t) j] = lastL;
                    v.stretchInBufR[(size_t) j] = lastR;
                }
                v.stretchSrcPos = v.endSample;
                break;
            }
        }
        else if (v.direction < 0 && v.stretchSrcPos <= v.startSample)
        {
            if (v.pingPong)
            {
                v.stretchSrcPos = v.startSample;
                v.direction = 1;
            }
            else if (v.looping)
            {
                v.stretchSrcPos = v.endSample - 1;
            }
            else if (v.releaseTail && v.stretchSrcPos >= 0)
            {
                v.stretchSrcPos = std::max (v.stretchSrcPos, 0.0);
            }
            else
            {
                float lastL = v.stretchInBufL[(size_t) i];
                float lastR = v.stretchInBufR[(size_t) i];
                for (int j = i + 1; j < inputSamples; ++j)
                {
                    v.stretchInBufL[(size_t) j] = lastL;
                    v.stretchInBufR[(size_t) j] = lastR;
                }
                v.stretchSrcPos = v.startSample;
                break;
            }
        }
    }

    const int outCapacity = std::min ((int) v.stretchOutBufL.size(), (int) v.stretchOutBufR.size());
    if (outCapacity <= 0)
    {
        v.stretchOutReadPos = 0;
        v.stretchOutAvail   = 0;
        return;
    }
    const int outputSamples = std::min (kStretchBlockSize, outCapacity);

    // Process through Signalsmith
    float* inPtrs[2]  = { v.stretchInBufL.data(),  v.stretchInBufR.data() };
    float* outPtrs[2] = { v.stretchOutBufL.data(), v.stretchOutBufR.data() };
    v.stretcher->process (inPtrs, inputSamples, outPtrs, outputSamples);

    v.stretchOutReadPos = 0;
    v.stretchOutAvail   = outputSamples;
}

void VoicePool::processVoiceSample (int i, const SampleData& sample, double /*sr*/,
                                     float& outL, float& outR)
{
    auto& v = voices[i];
    outL = 0.0f;
    outR = 0.0f;

    if (! v.active)
        return;

    float voiceL = 0.0f, voiceR = 0.0f;

    if (v.stretchActive)
    {
        // Signalsmith Stretch processing
        float env = v.envelope.processSample();

        if (v.envelope.isDone())
        {
            v.active = false;
            voicePositions[i].store (0.0f, std::memory_order_relaxed);
            return;
        }

        // ── Legato pitch glide (stretch mode) ────────────────────────────────
        // When a legato retune has been armed, ramp stretchPitchSemis toward the
        // target one block at a time. This prevents phase-vocoder smear / metallic
        // artifacts caused by an instant transposition step change.
        if (legatoPitchGliding[i].load (std::memory_order_relaxed)
            && v.stretchOutReadPos >= v.stretchOutAvail
            && v.stretcher)
        {
            const float target = legatoTargetSemis[i].load (std::memory_order_relaxed);
            // ~10ms at 44.1kHz → ≈3.4 blocks of 128 samples; use 4 for safety
            const float blocksFor10ms = std::max (1.0f, legatoGlideMs.load (std::memory_order_relaxed) * 0.001f * (float) sampleRate / kStretchBlockSize);
            v.stretchPitchSemis += (target - v.stretchPitchSemis) / blocksFor10ms;
            if (std::abs (v.stretchPitchSemis - target) < 0.015f)
            {
                v.stretchPitchSemis = target;
                legatoPitchGliding[i].store (false, std::memory_order_relaxed);
            }
            v.stretcher->setTransposeSemitones (v.stretchPitchSemis, 0.0f);
        }

        // Fill output buffer if empty
        if (v.stretchOutReadPos >= v.stretchOutAvail)
        {
            bool pastEnd = (v.direction > 0)
                           ? (v.stretchSrcPos >= v.endSample)
                           : (v.stretchSrcPos <= v.startSample);

            if (pastEnd && !v.pingPong)
            {
                if (v.looping)
                {
                    if (v.direction > 0)
                        v.stretchSrcPos = v.startSample;
                    else
                        v.stretchSrcPos = v.endSample - 1;
                    fillStretchBlock (v, sample);
                }
                else if (v.releaseTail && v.stretchSrcPos < v.bufferEnd && v.stretchSrcPos >= 0)
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                        v.envelope.noteOff();
                    fillStretchBlock (v, sample);
                }
                else
                {
                    if (v.oneShot)
                    {
                        // One-shot: sample played to end — deactivate immediately.
                        v.active = false;
                        voicePositions[i].store (0.0f, std::memory_order_relaxed);
                        return;
                    }
                    // Gate voices: forceRelease so the voice doesn't play silence forever
                    // waiting for a note-off that may never come.
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                        v.envelope.forceRelease (kShortReleaseSec, sampleRate);
                }
            }
            else
            {
                fillStretchBlock (v, sample);
            }
        }

        if (v.stretchOutReadPos < v.stretchOutAvail)
        {
            voiceL = v.stretchOutBufL[(size_t) v.stretchOutReadPos] * env * v.velocity * v.volume;
            voiceR = v.stretchOutBufR[(size_t) v.stretchOutReadPos] * env * v.velocity * v.volume;
            v.stretchOutReadPos++;
        }

        voicePositions[i].store ((float) v.stretchSrcPos, std::memory_order_relaxed);
    }
    else
    {
        // Process envelope
        float env = v.envelope.processSample();

        if (v.envelope.isDone())
        {
            v.active = false;
            voicePositions[i].store (0.0f, std::memory_order_relaxed);
            return;
        }

        // ── Legato pitch glide (repitch mode) ────────────────────────────────
        // Ramp v.speed toward target per sample to avoid phase discontinuity
        // clicks on instant pitch changes. ~10ms exponential smoothing.
        if (legatoPitchGliding[i].load (std::memory_order_relaxed))
        {
            const float target = legatoTargetSpeed[i].load (std::memory_order_relaxed);
            const float alpha  = 1.0f / std::max (1.0f, legatoGlideMs.load (std::memory_order_relaxed) * 0.001f * (float) sampleRate);
            v.speed += ((double) target - v.speed) * alpha;
            if (std::abs ((float) v.speed - target) < 0.000025f)
            {
                v.speed = target;
                legatoPitchGliding[i].store (false, std::memory_order_relaxed);
            }
        }

        // Linear interpolation
        voiceL = sample.getInterpolatedSample (v.position, 0) * env * v.velocity * v.volume;
        voiceR = sample.getInterpolatedSample (v.position, 1) * env * v.velocity * v.volume;

        // Advance position
        double newPos = v.position + v.speed * v.direction;

        if (v.pingPong)
        {
            if (newPos >= v.endSample)
            {
                newPos      = v.endSample - 1;
                v.direction = -1;
            }
            else if (newPos < v.startSample)
            {
                newPos      = v.startSample;
                v.direction = 1;
            }
        }
        else
        {
            if (newPos >= v.endSample || newPos < v.startSample)
            {
                if (v.looping)
                {
                    double len = (double) (v.endSample - v.startSample);
                    if (len > 0)
                        newPos = v.startSample + std::fmod (newPos - v.startSample, len);
                    if (newPos < v.startSample)
                        newPos += len;
                }
                else if (v.releaseTail && newPos >= v.endSample && newPos < v.bufferEnd)
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                        v.envelope.noteOff();
                }
                else if (v.releaseTail && v.direction < 0 && newPos < v.startSample && newPos >= 0)
                {
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                        v.envelope.noteOff();
                }
                else
                {
                    if (v.oneShot)
                    {
                        // One-shot: sample played to end — deactivate immediately.
                        // The ADSR is irrelevant here; position reaching endSample IS the termination.
                        v.active = false;
                        voicePositions[i].store (0.0f, std::memory_order_relaxed);
                        outL = voiceL * v.panL;
                        outR = voiceR * v.panR;
                        return;
                    }
                    // Gate voices: forceRelease so the voice doesn't play silence forever
                    // waiting for a note-off that may never come.
                    if (v.envelope.getState() != AdsrEnvelope::Release)
                        v.envelope.forceRelease (kShortReleaseSec, sampleRate);

                    newPos = juce::jlimit ((double) v.startSample, (double) v.endSample - 1, newPos);
                }
            }
        }

        v.position = newPos;
        voicePositions[i].store ((float) v.position, std::memory_order_relaxed);
    }

    // ── Filter (applied to all rendering paths) ───────────────────────────────
    // One-pole IIR low-pass with resonance feedback.
    // filterCoeff == 1.0 means bypass (cutoff at Nyquist).
    if (v.filterCoeff < 0.9999f)
    {
        float fbL = voiceL - v.filterRes * 3.5f * v.filterStateL;
        v.filterStateL += v.filterCoeff * (fbL - v.filterStateL);
        voiceL = v.filterStateL;

        float fbR = voiceR - v.filterRes * 3.5f * v.filterStateR;
        v.filterStateR += v.filterCoeff * (fbR - v.filterStateR);
        voiceR = v.filterStateR;
    }

    // ── Per-slice 3-band EQ (chowdsp SVF) ────────────────────────────────────
    if (v.eqActive)
    {
        voiceL = v.eqLowShelf.processSample (0, voiceL);
        voiceR = v.eqLowShelf.processSample (1, voiceR);
        voiceL = v.eqMid.processSample (0, voiceL);
        voiceR = v.eqMid.processSample (1, voiceR);
        voiceL = v.eqHighShelf.processSample (0, voiceL);
        voiceR = v.eqHighShelf.processSample (1, voiceR);
    }

    // ── Pan ───────────────────────────────────────────────────────────[...]
    voiceL *= v.panL;
    voiceR *= v.panR;

    outL = voiceL;
    outR = voiceR;
}

void VoicePool::processSample (const SampleData& sample, double sr,
                                float& outL, float& outR)
{
    outL = 0.0f;
    outR = 0.0f;

    for (int i = 0; i < maxActive; ++i)
    {
        float vL = 0.0f, vR = 0.0f;
        processVoiceSample (i, sample, sr, vL, vR);
        outL += vL;
        outR += vR;
    }

    // Always process the preview voice (used by LazyChopEngine)
    // even if it's outside the maxActive range
    constexpr int previewIdx = kPreviewVoiceIndex;
    if (previewIdx >= maxActive && voices[previewIdx].active)
    {
        float vL = 0.0f, vR = 0.0f;
        processVoiceSample (previewIdx, sample, sr, vL, vR);
        outL += vL;
        outR += vR;
    }
}

void VoicePool::startShiftPreview (int startSample, int bufferSize,
                                    double sr, const SampleData& /*sd*/)
{
    // Shares the lazyChop preview slot; only called when lazyChop is inactive
    const int i = kPreviewVoiceIndex;
    Voice& v = voices[i];
    v.active        = true;
    v.sliceIdx      = -1;
    v.position      = (double) startSample;
    v.speed         = 1.0;
    v.direction     = 1;
    v.midiNote      = -1;
    v.velocity      = 0.8f;
    v.startSample   = startSample;
    v.endSample     = bufferSize;
    v.bufferEnd     = bufferSize;
    v.pingPong      = false;
    v.muteGroup     = 0;
    v.looping       = false;
    v.volume        = 1.0f;
    v.releaseTail   = false;
    v.oneShot       = false;
    v.stretchActive = false;
    v.stretchOutReadPos = 0;
    v.stretchOutAvail   = 0;
    v.envelope.noteOn (0.002f, 0.0f, 1.0f, 0.05f, sr);
    voicePositions[i].store ((float) startSample, std::memory_order_relaxed);
}

void VoicePool::stopShiftPreview()
{
    int i = kPreviewVoiceIndex;
    if (voices[i].active)
        voices[i].envelope.forceRelease (kKillReleaseSec, sampleRate);
}
