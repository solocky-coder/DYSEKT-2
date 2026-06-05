// =============================================================================
//  SoundFontLoader.cpp
// =============================================================================
#include "SoundFontLoader.h"
#include "../PluginProcessor.h"

#if DYSEKT_HAS_SFIZZ
  // Include sfizz C API via path relative to the project root.
  // This avoids relying on target_include_directories propagation.
  #include "../../sfizz/src/sfizz.h"
#endif
#include <cmath>
#include <algorithm>

#if DYSEKT_HAS_SFIZZ

// =============================================================================
//  Constants
// =============================================================================
namespace SfzConst
{
    constexpr int   kBlockSize        = 256;    // sfizz render block size
    constexpr int   kProbeSize        = 512;    // samples for note-discovery probe
    constexpr int   kVelocity         = 100;    // MIDI velocity used for all renders
    constexpr float kNoteDurationSec  = 2.0f;   // sustain phase length per note
    constexpr float kReleaseSec       = 0.8f;   // release tail length per note
    constexpr float kGapSec           = 0.005f; // silence gap between concatenated notes
    constexpr float kSilenceThreshold = 1e-5f;  // below this = silent
}

// =============================================================================
//  LoadJob  (ThreadPoolJob)
// =============================================================================
class SoundFontLoader::LoadJob final : public juce::ThreadPoolJob
{
public:
    LoadJob (juce::File f, double sr, int tok, DysektProcessor& proc)
        : juce::ThreadPoolJob ("SfzLoadJob"),
          file (std::move (f)),
          sampleRate (sr),
          token (tok),
          processor (proc),
          guard (proc.loadCallbacksValid)   // shared_ptr copy — safe after processor dtor
    {}

    // ── Main entry point ──────────────────────────────────────────────────────
    JobStatus runJob() override
    {
        using namespace SfzConst;

        sfizz_synth_t* sfz = sfizz_create_synth();
        sfizz_set_sample_rate  (sfz, (float) sampleRate);
        sfizz_set_samples_per_block (sfz, kBlockSize);

        const bool ok = sfizz_load_file (sfz, file.getFullPathName().toRawUTF8());
        if (! ok || shouldExit())
        {
            sfizz_free (sfz);
            postFailure();
            return jobHasFinished;
        }

        // ── Step 1: discover active notes ─────────────────────────────────────
        std::vector<int> activeNotes = discoverActiveNotes (sfz, *this);
        if (shouldExit()) { sfizz_free (sfz); return jobHasFinished; }

        if (activeNotes.empty())
        {
            // Fallback: assume standard piano range
            for (int n = 21; n <= 108; ++n)
                activeNotes.push_back (n);
        }

        // ── Step 2: render each active note ───────────────────────────────────
        struct NoteRender
        {
            int   midiNote;
            std::vector<float> L, R;  // time-domain samples
        };

        const int sustainSamples = (int) (sampleRate * kNoteDurationSec);
        const int releaseSamples = (int) (sampleRate * kReleaseSec);
        const int totalPerNote   = sustainSamples + releaseSamples;

        std::vector<NoteRender> renders;
        renders.reserve (activeNotes.size());

        std::vector<float> blockL (kBlockSize), blockR (kBlockSize);

        for (int note : activeNotes)
        {
            if (shouldExit()) break;

            sfizz_send_note_on (sfz, 0, note, kVelocity);

            NoteRender nr;
            nr.midiNote = note;
            nr.L.reserve ((size_t) totalPerNote);
            nr.R.reserve ((size_t) totalPerNote);

            // Sustain phase
            renderPhase (sfz, sustainSamples, blockL, blockR, nr.L, nr.R);

            // Note-off, then release tail
            sfizz_send_note_off (sfz, 0, note, kVelocity);
            renderPhase (sfz, releaseSamples, blockL, blockR, nr.L, nr.R);

            // Kill remaining audio before next note
            sfizz_all_sound_off (sfz);

            // Silence-trim and check peak
            silenceTrim (nr.L, nr.R);

            float peak = 0.f;
            for (size_t i = 0; i < nr.L.size(); ++i)
                peak = std::max (peak, std::max (std::abs (nr.L[i]),
                                                 std::abs (nr.R[i])));
            if (peak < kSilenceThreshold)
                continue;  // note produced no audio — skip

            renders.push_back (std::move (nr));
        }

        sfizz_free (sfz);
        sfz = nullptr;

        if (renders.empty() || shouldExit())
        {
            postFailure();
            return jobHasFinished;
        }

        // ── Step 3: concatenate into one stereo AudioBuffer ───────────────────
        const int gapSamples = std::max (1, (int) (sampleRate * SfzConst::kGapSec));
        int totalFrames = gapSamples;
        for (auto& r : renders) totalFrames += (int) r.L.size() + gapSamples;

        auto decoded = std::make_unique<SampleData::DecodedSample>();
        decoded->buffer.setSize (2, totalFrames, false, true, false);

        {
            auto nameNoExt = file.getFileNameWithoutExtension();
            decoded->fileName = nameNoExt;
        }

        float* dstL = decoded->buffer.getWritePointer (0);
        float* dstR = decoded->buffer.getWritePointer (1);

        // Build slice payload
        auto* payload = new SfzSlicePayload();
        payload->slices.reserve (renders.size());

        int writePos = gapSamples;
        for (auto& r : renders)
        {
            const int len   = (int) r.L.size();
            const int start = writePos;
            const int end   = writePos + len;

            std::copy (r.L.begin(), r.L.end(), dstL + start);
            std::copy (r.R.begin(), r.R.end(), dstR + start);

            SfzSliceDescriptor desc;
            desc.startSample = start;
            desc.endSample   = end;
            desc.midiNote    = r.midiNote;
            payload->slices.push_back (desc);

            writePos = end + gapSamples;
        }

        // Build waveform peak mipmaps so SliceWaveformLcd can display the
        // rendered preset audio.  Must happen before posting to completedLoadData.
        SampleData::buildPeakMipmaps (*decoded);

        // ── Step 4: post results ──────────────────────────────────────────────
        // Guard against the processor having been destroyed while we were rendering.
        if (! guard->load (std::memory_order_acquire))
            return jobHasFinished;

        // Post slice layout (processBlock picks this up right after applyDecodedSample)
        auto* oldPayload = processor.pendingSfzSlices.exchange (payload,
                                                                 std::memory_order_acq_rel);
        delete oldPayload;

        // Post decoded audio (same path as WAV loader — processBlock polls this)
        auto* old = processor.completedLoadData.exchange (decoded.release(),
                                                          std::memory_order_acq_rel);
        delete old;

        processor.latestLoadKind.store ((int) DysektProcessor::LoadKindReplace,
                                        std::memory_order_release);
        return jobHasFinished;
    }

private:
    // ── Helpers ───────────────────────────────────────────────────────────────

    void renderPhase (sfizz_synth_t* sfz, int numSamples,
                      std::vector<float>& blockL, std::vector<float>& blockR,
                      std::vector<float>& outL,   std::vector<float>& outR) const
    {
        int remaining = numSamples;
        while (remaining > 0)
        {
            int block = std::min (remaining, SfzConst::kBlockSize);
            std::fill (blockL.begin(), blockL.end(), 0.f);
            std::fill (blockR.begin(), blockR.end(), 0.f);
            float* outs[2] = { blockL.data(), blockR.data() };
            sfizz_render_block (sfz, outs, 2, block);
            outL.insert (outL.end(), blockL.begin(), blockL.begin() + block);
            outR.insert (outR.end(), blockR.begin(), blockR.begin() + block);
            remaining -= block;
        }
    }

    static void silenceTrim (std::vector<float>& L, std::vector<float>& R)
    {
        // Trim leading silence
        int start = 0;
        for (int i = 0; i < (int) L.size() - 1; ++i)
        {
            if (std::abs (L[i]) > SfzConst::kSilenceThreshold ||
                std::abs (R[i]) > SfzConst::kSilenceThreshold)
                break;
            ++start;
        }
        if (start > 0)
        {
            L.erase (L.begin(), L.begin() + start);
            R.erase (R.begin(), R.begin() + start);
        }

        // Trim trailing silence (keep minimum 64 samples)
        int end = (int) L.size();
        while (end > 64)
        {
            if (std::abs (L[(size_t)(end-1)]) > SfzConst::kSilenceThreshold ||
                std::abs (R[(size_t)(end-1)]) > SfzConst::kSilenceThreshold)
                break;
            --end;
        }
        L.resize ((size_t) end);
        R.resize ((size_t) end);
    }

    // Fast pass to find which notes produce audio
    static std::vector<int> discoverActiveNotes (sfizz_synth_t* sfz,
                                                 const juce::ThreadPoolJob& job)
    {
        std::vector<int> found;
        std::vector<float> probeL (SfzConst::kProbeSize, 0.f);
        std::vector<float> probeR (SfzConst::kProbeSize, 0.f);
        float* outs[2] = { probeL.data(), probeR.data() };

        for (int n = 0; n <= 127; ++n)
        {
            if (job.shouldExit()) return {};   // bail early on shutdown

            std::fill (probeL.begin(), probeL.end(), 0.f);
            std::fill (probeR.begin(), probeR.end(), 0.f);

            sfizz_send_note_on  (sfz, 0, n, SfzConst::kVelocity);
            sfizz_render_block  (sfz, outs, 2, SfzConst::kProbeSize);
            sfizz_all_sound_off (sfz);

            float peak = 0.f;
            for (int i = 0; i < SfzConst::kProbeSize; ++i)
                peak = std::max (peak, std::max (std::abs (probeL[i]),
                                                 std::abs (probeR[i])));
            if (peak > SfzConst::kSilenceThreshold)
                found.push_back (n);
        }
        return found;
    }

    void postFailure()
    {
        if (! guard->load (std::memory_order_acquire))
            return;

        auto* payload = new DysektProcessor::FailedLoadResult();
        payload->token = token;
        payload->kind  = DysektProcessor::LoadKindReplace;
        payload->file  = file;
        auto* old = processor.completedLoadFailure.exchange (payload,
                                                             std::memory_order_acq_rel);
        delete old;
    }

    juce::File       file;
    double           sampleRate;
    int              token;
    DysektProcessor& processor;
    std::shared_ptr<std::atomic<bool>> guard;  // copy of processor.loadCallbacksValid
};

// =============================================================================
//  SoundFontLoader::load  (public entry point — UI thread)
// =============================================================================
void SoundFontLoader::load (const juce::File& file)
{
    const double sr = processor.currentSampleRate > 0.0
                      ? processor.currentSampleRate : 44100.0;

    const int token = processor.nextLoadToken.fetch_add (1, std::memory_order_relaxed) + 1;
    processor.latestLoadToken.store (token, std::memory_order_release);
    processor.latestLoadKind.store  ((int) DysektProcessor::LoadKindReplace,
                                     std::memory_order_release);

    // Discard any pending payload from a previous load
    delete processor.completedLoadData.exchange  (nullptr, std::memory_order_acq_rel);
    delete processor.completedLoadFailure.exchange(nullptr, std::memory_order_acq_rel);
    delete processor.pendingSfzSlices.exchange   (nullptr, std::memory_order_acq_rel);

    processor.fileLoadPool.addJob (new LoadJob (file, sr, token, processor), true);
}

#else  // DYSEKT_HAS_SFIZZ not defined

void SoundFontLoader::load (const juce::File& file)
{
    // sfizz not linked — hand off to the regular audio file loader.
    // It will likely fail and show the normal "failed to load" UI.
    processor.requestSampleLoad (file, DysektProcessor::LoadKindReplace);
}

#endif // DYSEKT_HAS_SFIZZ