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
    // If this MIDI note is already assigned to an existing slice, audition it
    int existingSlice = sliceMgr.midiNoteToSlice (note);
    if (existingSlice >= 0)
    {
        const auto& s = sliceMgr.getSlice (existingSlice);
        startPreview (voicePool, s.startSample);
        playing = true;
        chopPos = -1;  // reset so next unassigned note only sets a new start
        return -1;
    }

    // First unassigned note — start playback from beginning and immediately
    // commit a slice marker at position 0 so the UI shows slice 1 right away.
    if (! playing)
    {
        startPreview (voicePool, 0);
        playing  = true;
        lastNote = note;
        chopPos  = 0;

        // Insert the first marker so slice 1 is visible immediately.
        // Its end will be determined by the next note (marker model: end = next
        // marker's start, so nothing more needed here).
        int firstIdx = sliceMgr.createSlice (0, sliceMgr.getEndForSlice (0, sampleLength));
        if (firstIdx >= 0)
        {
            auto& s = sliceMgr.getSlice (firstIdx);
            s.midiNote = nextMidiNote;  // always C2 (rootNote) for slice 1
            nextMidiNote = std::min (nextMidiNote + 1, 127);
            sliceMgr.rebuildMidiMap();
        }
        return firstIdx;
    }

    // Re-press same note: re-audition from current start point
    if (note == lastNote && chopPos >= 0)
    {
        startPreview (voicePool, chopPos);
        return -1;
    }

    // Subsequent unassigned note — place slice boundary at playhead
    auto& v = voicePool.getVoice (getPreviewVoiceIndex());
    double rawPos = v.stretchActive ? v.stretchSrcPos
                  :                   v.position;
    int playhead = (int) std::floor (rawPos);

    if (snapEnabled && sampleBuffer != nullptr)
        playhead = AudioAnalysis::findNearestZeroCrossing (*sampleBuffer, playhead);

    // After audition, first unassigned note just sets a new start point
    if (chopPos < 0)
    {
        chopPos = playhead;
        lastNote = note;
        return -1;
    }

    int resultIdx = -1;

    // Handle wrap-around: if playhead wrapped past chopPos, close slice to end of sample
    if (playhead < chopPos)
    {
        if (sampleLength - chopPos >= 64)
        {
            int idx = sliceMgr.createSlice (chopPos, sampleLength);
            if (idx >= 0)
            {
                auto& s = sliceMgr.getSlice (idx);
                s.midiNote = nextMidiNote;
                nextMidiNote = std::min (nextMidiNote + 1, 127);
                sliceMgr.rebuildMidiMap();
                resultIdx = idx;
            }
        }
        chopPos = 0;
    }

    // Insert a marker at the playhead, splitting the current open-ended slice.
    // The left half (chopPos..playhead) already has its note from when it was
    // first created; the right half (playhead onward) gets the new note.
    if (playhead - chopPos >= 64)
    {
        // insertMarker splits the slice that owns `playhead` into two.
        // The right-hand slice is the new one; assign this note to it.
        int newIdx = sliceMgr.insertMarker (playhead, sampleLength);
        if (newIdx >= 0)
        {
            auto& s = sliceMgr.getSlice (newIdx);
            s.midiNote = nextMidiNote;  // sequential from C2 regardless of pressed key
            nextMidiNote = std::min (nextMidiNote + 1, 127);
            sliceMgr.rebuildMidiMap();
            resultIdx = newIdx;
        }
    }

    chopPos = playhead;
    lastNote = note;
    return resultIdx;
}
