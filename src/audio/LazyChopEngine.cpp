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
    // Unified path: every note press reads the current playhead and places a
    // marker there.  On the very first note the preview voice hasn't started
    // yet, so position is 0 — which is exactly right (marker at sample start).
    // On subsequent notes the voice has been running and position reflects
    // wherever the user chose to chop.
    //
    // N note presses always produce N markers (= N slices), one per press,
    // at the exact sample position where each note landed.

    // Read current playhead from the preview voice.  If not yet playing,
    // the voice doesn't exist yet so we treat position as 0.
    int playhead = 0;
    if (playing)
    {
        auto& v = voicePool.getVoice (getPreviewVoiceIndex());
        double rawPos = v.stretchActive ? v.stretchSrcPos : v.position;
        playhead = (int) std::floor (rawPos);

        if (snapEnabled && sampleBuffer != nullptr)
            playhead = AudioAnalysis::findNearestZeroCrossing (*sampleBuffer, playhead);
    }

    // Place a marker at the playhead.  createSlice is idempotent on exact
    // duplicates and has no minimum-width guard, so rapid or same-buffer
    // presses near position 0 work correctly.
    int idx = sliceMgr.createSlice (playhead, sampleLength);
    if (idx >= 0)
    {
        sliceMgr.getSlice (idx).midiNote = nextMidiNote;
        nextMidiNote = std::min (nextMidiNote + 1, 127);
        sliceMgr.rebuildMidiMap();
    }

    // Start playback on the first note (after placing the marker so the
    // voice starts into a valid slice layout).
    if (! playing)
    {
        startPreview (voicePool, 0);
        playing = true;
    }

    chopPos  = playhead;
    lastNote = note;
    return idx;
}
