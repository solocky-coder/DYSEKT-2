#include "LazyChopEngine.h"
#include "AudioAnalysis.h"
#include <cmath>

void LazyChopEngine::start (int sampleLen, SliceManager& sliceMgr,
                            const PreviewStretchParams& params,
                            bool snap, const juce::AudioBuffer<float>* buf)
{
    active = true;
    playing = false;
    chopPos = 0;
    sampleLength = sampleLen;
    lastNote = -1;
    cachedParams = params;
    snapEnabled = snap;
    sampleBuffer = buf;

    nextMidiNote = sliceMgr.rootNote.load();
    int num = sliceMgr.getNumSlices();
    for (int i = 0; i < num; ++i)
    {
        const auto& s = sliceMgr.getSlice (i);
        if (s.active && s.midiNote >= nextMidiNote)
            nextMidiNote = s.midiNote + 1;
    }
    nextMidiNote = std::min (nextMidiNote, 127);
}

void LazyChopEngine::startPreview (VoicePool& voicePool, int fromPos)
{
    auto& v = voicePool.getVoice (getPreviewVoiceIndex());
    v.active        = true;
    v.sliceIdx      = -1;
    v.position      = (double) fromPos;
    v.direction     = 1;
    v.velocity      = 1.0f;
    v.midiNote      = -1;
    v.startSample   = 0;
    v.endSample     = sampleLength;
    v.bufferEnd     = sampleLength;
    v.pingPong      = false;
    v.muteGroup     = 0;
    v.stretchActive = false;
    v.looping       = true;
    v.releaseTail   = false;
    v.oneShot       = false;
    v.volume        = 1.0f;
    v.speed         = 1.0;

    // Apply stretch from cached sample-level params
    const auto& p = cachedParams;
    int algo = p.algorithm;

    if (p.stretchEnabled && p.dawBpm > 0.0f && p.bpm > 0.0f)
    {
        float speedRatio = p.dawBpm / p.bpm;

        if (algo == 0)
        {
            // Repitch: speed change (pitch is consequence)
            v.speed = speedRatio;
        }
        else if (p.sample != nullptr)
        {
            // Signalsmith Stretch
            v.stretchActive = true;
            v.speed = 1.0;
            v.stretchTimeRatio = speedRatio;
            v.stretchPitchSemis = p.pitch;
            v.stretchSrcPos = fromPos;
            VoicePool::initStretcher (v, p.pitch, p.sampleRate,
                                      p.tonality, p.formant, *p.sample);
        }
    }
    else if (algo == 1 && p.sample != nullptr)
    {
        // Stretch algo, no stretch enabled — pitch only via Signalsmith
        v.stretchActive = true;
        v.speed = 1.0;
        v.stretchTimeRatio = 1.0f;
        v.stretchPitchSemis = p.pitch;
        v.stretchSrcPos = fromPos;
        VoicePool::initStretcher (v, p.pitch, p.sampleRate,
                                  p.tonality, p.formant, *p.sample);
    }
    else
    {
        // Repitch: apply pitch ratio to speed
        v.speed = std::pow (2.0f, p.pitch / 12.0f);
    }

    // Sustain at half volume
    v.envelope.noteOn (0.0f, 0.0f, 0.5f, 0.02f, cachedParams.sampleRate);
}

void LazyChopEngine::stop (VoicePool& voicePool, SliceManager& /*sliceMgr*/)
{
    // Stop preview voice
    auto& v = voicePool.getVoice (getPreviewVoiceIndex());
    v.active = false;
    v.stretchActive = false;

    active = false;
    playing = false;
}

int LazyChopEngine::onNote (int note, VoicePool& voicePool, SliceManager& sliceMgr)
{
    // First note — start playback from the beginning.
    // No slice is created yet; the root slice at 0 will be seeded the moment
    // the first real cut is placed so the UI never shows a full-span placeholder
    // from the first key press alone.
    if (! playing)
    {
        startPreview (voicePool, 0);
        playing  = true;
        lastNote = note;
        chopPos  = 0;
        return -1;
    }

    // Re-press the same note: re-audition from the current chop start point.
    if (note == lastNote && chopPos >= 0)
    {
        startPreview (voicePool, chopPos);
        return -1;
    }

    // Every other key press places a cut at the current playhead.
    // Intentionally NOT checking midiNoteToSlice() here — during an active
    // Lazy Chop session every new key press must produce a chop, not an
    // audition of a previously-assigned slice.  The old audition check was
    // silently consuming key presses and setting chopPos = -1, which caused
    // the following note to burn its press repositioning the start instead
    // of cutting, giving the appearance of every other marker being skipped.

    auto& v = voicePool.getVoice (getPreviewVoiceIndex());
    double rawPos = v.stretchActive ? v.stretchSrcPos
                  :                   v.position;
    int playhead = (int) std::floor (rawPos);

    if (snapEnabled && sampleBuffer != nullptr)
        playhead = AudioAnalysis::findNearestZeroCrossing (*sampleBuffer, playhead);

    // Clamp any stale negative chopPos (e.g. carried over from before LazyChop
    // was restarted on a session that already had slices).
    if (chopPos < 0)
        chopPos = 0;

    // Handle wrap-around: playhead looped back past chopPos — reset chop start
    // to 0 so the next cut begins a fresh pass through the sample.
    if (playhead < chopPos)
        chopPos = 0;

    int resultIdx = -1;

    // Seed a root slice at position 0 the first time a real cut is placed so
    // that insertMarker has a slice to split.  Both this createSlice call and
    // the insertMarker below run within the same audio-thread callback; the UI
    // snapshot is only refreshed after onNote returns, so the transient
    // full-span root slice is never visible to the user.
    if (sliceMgr.getNumSlices() == 0 && playhead > 0)
        sliceMgr.createSlice (0, sampleLength);   // rebuildMidiMap called inside

    // Place the chop marker at the playhead.
    if (playhead - chopPos >= 64)
    {
        int newIdx = sliceMgr.insertMarker (playhead, sampleLength);
        if (newIdx >= 0)
        {
            auto& s = sliceMgr.getSlice (newIdx);
            s.midiNote = nextMidiNote;
            nextMidiNote = std::min (nextMidiNote + 1, 127);
            sliceMgr.rebuildMidiMap();
            resultIdx = newIdx;
        }
    }

    chopPos = playhead;
    lastNote = note;
    return resultIdx;
}
