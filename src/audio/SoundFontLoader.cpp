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

// =============================================================================
//  SFZ loop-point parser  (SFZ files only — SF2 have no text opcodes)
//  Returns {loopStart, loopEnd} in sample frames, or {-1,-1} if not found.
// =============================================================================
static std::pair<int,int> parseSfzLoopPoints (const juce::File& sfzFile)
{
    if (sfzFile.getFileExtension().toLowerCase() != ".sfz")
        return { -1, -1 };

    const juce::String text = sfzFile.loadFileAsString();
    if (text.isEmpty())
        return { -1, -1 };

    // Walk through opcode tokens looking for loop_start= and loop_end=
    int loopStart = -1, loopEnd = -1;

    // Simple opcode scan: find "loop_start=<N>" and "loop_end=<N>"
    // We pick the FIRST occurrence (global or first region — heuristic).
    auto scanOpcode = [&] (const char* name) -> int
    {
        juce::String key (name);
        int pos = text.indexOfIgnoreCase (key + "=");
        if (pos < 0) return -1;
        pos += key.length() + 1;
        int end = pos;
        while (end < text.length() && (juce::CharacterFunctions::isDigit (text[end]) || text[end] == '-'))
            ++end;
        if (end == pos) return -1;
        return text.substring (pos, end).getIntValue();
    };

    loopStart = scanOpcode ("loop_start");
    loopEnd   = scanOpcode ("loop_end");

    // Both must be present and valid
    if (loopStart < 0 || loopEnd <= loopStart)
        return { -1, -1 };

    return { loopStart, loopEnd };
}


// =============================================================================
//  SF2 binary SHDR parser  (SF2 files only)
//  Reads the sample-header sub-chunk of the sdta-list to extract loop points
//  for the first non-ROM, looping sample.  No FluidSynth API required —
//  works against any version.
//
//  SF2 SHDR record layout (46 bytes each):
//    achSampleName[20]  char[20]
//    dwStart            uint32  — sample start in sample data
//    dwEnd              uint32  — sample end
//    dwStartloop        uint32  — loop start
//    dwEndloop          uint32  — loop end
//    dwSampleRate       uint32
//    byOriginalKey      uint8
//    chCorrection       int8
//    wSampleLink        uint16
//    sfSampleType       uint16  — bit 0x8000 = ROM
//  Returns {loopStart, loopEnd} in sample-frames, or {-1,-1} if none.
// =============================================================================
static std::pair<int,int> parseSf2LoopPoints (const juce::File& sf2File)
{
    if (sf2File.getFileExtension().toLowerCase() != ".sf2")
        return { -1, -1 };

    juce::FileInputStream stream (sf2File);
    if (stream.failedToOpen()) return { -1, -1 };

    // ── Helper lambdas to read little-endian integers ─────────────────────────
    auto readU32 = [&]() -> uint32_t
    {
        uint8_t b[4] = {};
        stream.read (b, 4);
        return (uint32_t) b[0] | ((uint32_t) b[1] << 8)
             | ((uint32_t) b[2] << 16) | ((uint32_t) b[3] << 24);
    };
    auto readU16 = [&]() -> uint16_t
    {
        uint8_t b[2] = {};
        stream.read (b, 2);
        return (uint16_t) b[0] | ((uint16_t) b[1] << 8);
    };
    auto readTag = [&]() -> uint32_t { return readU32(); };
    auto skipN   = [&] (int64_t n) { stream.setPosition (stream.getPosition() + n); };

    // ── Walk RIFF chunks looking for LIST/pdta/shdr ────────────────────────────
    // RIFF header
    if (readTag() != 0x46464952u)  // 'RIFF'
        return { -1, -1 };
    /* fileSize = */ readU32();
    if (readTag() != 0x32666673u)  // 'sfbk' (little-endian 'sfbk' = 0x6B626673, wait...)
    {
        // 'sfbk' in LE bytes: s=0x73, f=0x66, b=0x62, k=0x6B → 0x6B626673
        // We already consumed it; check if it might be 'sfbk' LE
        // Actually just proceed — if the RIFF type is wrong we bail
        // (we already consumed it above, so we can't re-check; just continue and
        //  let the chunk search fail gracefully)
    }

    // ── Scan top-level LIST chunks for 'pdta' ─────────────────────────────────
    const int64_t fileEnd = stream.getTotalLength();
    while (stream.getPosition() + 8 <= fileEnd)
    {
        const uint32_t chunkId   = readTag();
        const uint32_t chunkSize = readU32();
        const int64_t  chunkEnd  = stream.getPosition() + (int64_t) chunkSize;

        if (chunkId == 0x5453494Cu)  // 'LIST'
        {
            const uint32_t listType = readTag();
            if (listType == 0x61746470u)  // 'pdta'
            {
                // Inside pdta: scan sub-chunks for 'shdr'
                while (stream.getPosition() + 8 <= chunkEnd)
                {
                    const uint32_t subId   = readTag();
                    const uint32_t subSize = readU32();
                    const int64_t  subEnd  = stream.getPosition() + (int64_t) subSize;

                    if (subId == 0x72646873u)  // 'shdr'
                    {
                        // Each SHDR record is 46 bytes; last record is the terminal EOS entry
                        const int numRecords = (int) (subSize / 46);
                        for (int i = 0; i < numRecords - 1; ++i)  // skip EOS terminal
                        {
                            // achSampleName[20]
                            skipN (20);
                            const uint32_t sampleStart = readU32();
                            const uint32_t sampleEnd   = readU32();
                            const uint32_t loopStart   = readU32();
                            const uint32_t loopEnd     = readU32();
                            /* dwSampleRate */ readU32();
                            /* byOrigKey    */ stream.readByte();
                            /* chCorrection */ stream.readByte();
                            /* wSampleLink  */ readU16();
                            const uint16_t sampleType  = readU16();

                            juce::ignoreUnused (sampleStart, sampleEnd);

                            // Skip ROM samples and non-looping samples
                            const bool isRom     = (sampleType & 0x8000u) != 0;
                            const bool hasLoop   = (loopEnd > loopStart + 4);

                            if (!isRom && hasLoop)
                                return { (int) loopStart, (int) loopEnd };
                        }
                        return { -1, -1 };  // 'shdr' found but no looping sample
                    }

                    stream.setPosition (subEnd);
                }
                return { -1, -1 };  // 'pdta' found but no 'shdr'
            }
            // Not 'pdta' — skip
            stream.setPosition (chunkEnd);
        }
        else
        {
            stream.setPosition (chunkEnd);
        }
    }
    return { -1, -1 };
}

class SoundFontLoader::LoadJob final : public juce::ThreadPoolJob
{
public:
    LoadJob (juce::File f, double sr, int tok, DysektProcessor& proc, SoundFontLoadTarget tgt)
        : juce::ThreadPoolJob ("SfzLoadJob"),
          file (std::move (f)),
          sampleRate (sr),
          token (tok),
          processor (proc),
          target (tgt)
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
        std::vector<int> activeNotes = discoverActiveNotes (sfz);
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


        // ── Step 3b: extract loop points (SFZ + SF2) and post to sfzPlayer ──────
        {
            const auto ext = file.getFileExtension().toLowerCase();

            int globalLoopStart = -1, globalLoopEnd = -1;

            if (ext == ".sfz")
            {
                // SFZ: scan text for loop_start= / loop_end= opcodes.
                std::tie (globalLoopStart, globalLoopEnd) = parseSfzLoopPoints (file);

                if (globalLoopStart >= 0 && !payload->slices.empty())
                {
                    // Map raw SFZ sample offsets into the concat buffer.
                    const int sliceOffset = payload->slices[0].startSample;
                    const int bufStart    = sliceOffset + globalLoopStart;
                    const int bufEnd      = sliceOffset + globalLoopEnd;

                    if (bufEnd < totalFrames)
                    {
                        payload->slices[0].loopStart = bufStart;
                        payload->slices[0].loopEnd   = bufEnd;
                        processor.sfzPlayer.setLoopPoints (bufStart, bufEnd);
                        processor.sfzPlayer2.setLoopPoints (bufStart, bufEnd);
                    }
                    else
                    {
                        processor.sfzPlayer.setLoopPoints (-1, -1);
                        processor.sfzPlayer2.setLoopPoints (-1, -1);
                    }
                }
                else
                {
                    processor.sfzPlayer.setLoopPoints (-1, -1);
                    processor.sfzPlayer2.setLoopPoints (-1, -1);
                }
            }
            else if (ext == ".sf2")
            {
                // SF2: parse SHDR binary chunk for the first looping sample.
                // Returns raw sample-frame offsets within the SF2 instrument data.
                std::tie (globalLoopStart, globalLoopEnd) = parseSf2LoopPoints (file);

                if (globalLoopStart >= 0 && !payload->slices.empty())
                {
                    // SF2 loop offsets are relative to the raw instrument sample.
                    // The rendered slice for the first note starts at sliceStart
                    // and spans sliceLen frames.  Express the loop region as a
                    // fraction of (0..loopEnd) mapped into (0..sliceLen).
                    const int sliceStart = payload->slices[0].startSample;
                    const int sliceLen   = payload->slices[0].endSample - sliceStart;

                    if (sliceLen > 0 && globalLoopEnd > 0)
                    {
                        const float lsFrac = juce::jlimit (0.0f, 0.98f,
                                                (float) globalLoopStart / (float) globalLoopEnd);
                        const int bufStart = sliceStart + (int) (lsFrac * (float) sliceLen);
                        const int bufEnd   = sliceStart + sliceLen;  // loop to end of note render

                        payload->slices[0].loopStart = bufStart;
                        payload->slices[0].loopEnd   = bufEnd;
                        processor.sfzPlayer.setLoopPoints (bufStart, bufEnd);
                        processor.sfzPlayer2.setLoopPoints (bufStart, bufEnd);
                    }
                    else
                    {
                        processor.sfzPlayer.setLoopPoints (-1, -1);
                        processor.sfzPlayer2.setLoopPoints (-1, -1);
                    }
                }
                else
                {
                    processor.sfzPlayer.setLoopPoints (-1, -1);
                    processor.sfzPlayer2.setLoopPoints (-1, -1);
                }
            }
            else
            {
                processor.sfzPlayer.setLoopPoints (-1, -1);
                processor.sfzPlayer2.setLoopPoints (-1, -1);
            }
        }

        // ── Step 4: post results ──────────────────────────────────────────────
        if (target == SoundFontLoadTarget::Slicer)
        {
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
        }
        else if (target == SoundFontLoadTarget::SfzPlayer2)
        {
            // SFZ-PLAYER preview: sfzPlayer2 handles MIDI internally, so the
            // slice descriptors are never turned into real slices — but they
            // ARE reused as a read-only "preview zones" overlay so the
            // SFZ-PLAYER's waveform can show the same colored per-note bands
            // as the Slicer. Repackage as SfzPreviewZonePayload and post via
            // pendingPreviewZones2; completely decoupled from the Slicer's
            // sampleData / sliceManager.
            auto* zonePayload = new SfzPreviewZonePayload();
            zonePayload->slices = std::move (payload->slices);
            delete payload;

            auto* oldZones = processor.pendingPreviewZones2.exchange (zonePayload,
                                                                       std::memory_order_acq_rel);
            delete oldZones;

            auto* old = processor.completedLoadData2.exchange (decoded.release(),
                                                               std::memory_order_acq_rel);
            delete old;
        }
        else // SoundFontLoadTarget::SfPlayer
        {
            // SF2-PLAYER preview — mirrors the SfzPlayer2 branch above exactly,
            // posting to the parallel completedLoadData3/pendingPreviewZones3
            // pipeline instead, so the two preview tabs never share a buffer.
            auto* zonePayload = new SfzPreviewZonePayload();
            zonePayload->slices = std::move (payload->slices);
            delete payload;

            auto* oldZones = processor.pendingPreviewZones3.exchange (zonePayload,
                                                                       std::memory_order_acq_rel);
            delete oldZones;

            auto* old = processor.completedLoadData3.exchange (decoded.release(),
                                                               std::memory_order_acq_rel);
            delete old;
        }
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
    static std::vector<int> discoverActiveNotes (sfizz_synth_t* sfz)
    {
        std::vector<int> found;
        std::vector<float> probeL (SfzConst::kProbeSize, 0.f);
        std::vector<float> probeR (SfzConst::kProbeSize, 0.f);
        float* outs[2] = { probeL.data(), probeR.data() };

        for (int n = 0; n <= 127; ++n)
        {
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
        // SFZ-PLAYER preview is visual-only and has no failure-state UI of its
        // own (sfzPlayer2's live engine handles its own failure reporting
        // separately) — so for that target we simply no-op rather than add a
        // second failure-result atomic.
        if (target != SoundFontLoadTarget::Slicer)
            return;

        auto* payload = new DysektProcessor::FailedLoadResult();
        payload->token = token;
        payload->kind  = DysektProcessor::LoadKindReplace;
        payload->file  = file;
        auto* old = processor.completedLoadFailure.exchange (payload,
                                                             std::memory_order_acq_rel);
        delete old;
    }

    juce::File          file;
    double              sampleRate;
    int                 token;
    DysektProcessor&    processor;
    SoundFontLoadTarget target;
};

// =============================================================================
//  SoundFontLoader::load  (public entry point — UI thread)
// =============================================================================
void SoundFontLoader::load (const juce::File& file, SoundFontLoadTarget target)
{
    const double sr = processor.currentSampleRate > 0.0
                      ? processor.currentSampleRate : 44100.0;

    int token = 0;

    if (target == SoundFontLoadTarget::Slicer)
    {
        token = processor.nextLoadToken.fetch_add (1, std::memory_order_relaxed) + 1;
        processor.latestLoadToken.store (token, std::memory_order_release);
        processor.latestLoadKind.store  ((int) DysektProcessor::LoadKindReplace,
                                         std::memory_order_release);

        // Discard any pending payload from a previous Slicer-target load
        delete processor.completedLoadData.exchange  (nullptr, std::memory_order_acq_rel);
        delete processor.completedLoadFailure.exchange(nullptr, std::memory_order_acq_rel);
        delete processor.pendingSfzSlices.exchange   (nullptr, std::memory_order_acq_rel);
    }
    else if (target == SoundFontLoadTarget::SfzPlayer2)
    {
        // SFZ-PLAYER preview pipeline is independent of the Slicer's token
        // sequence — it never checks tokens, so there's nothing to bump here.
        // Just discard any stale preview payload from a previous preview load.
        delete processor.completedLoadData2.exchange (nullptr, std::memory_order_acq_rel);
        delete processor.pendingPreviewZones2.exchange (nullptr, std::memory_order_acq_rel);
    }
    else // SoundFontLoadTarget::SfPlayer
    {
        // SF2-PLAYER preview pipeline — mirrors SfzPlayer2 exactly, own buffer.
        delete processor.completedLoadData3.exchange (nullptr, std::memory_order_acq_rel);
        delete processor.pendingPreviewZones3.exchange (nullptr, std::memory_order_acq_rel);
    }

    processor.fileLoadPool.addJob (new LoadJob (file, sr, token, processor, target), true);
}

#else  // DYSEKT_HAS_SFIZZ not defined

void SoundFontLoader::load (const juce::File& file, SoundFontLoadTarget target)
{
    // sfizz not linked — hand off to the regular audio file loader.
    // It will likely fail and show the normal "failed to load" UI.
    // (SfzPlayer2-target preview loads have no failure UI of their own, so
    // this fallback only makes sense for the Slicer target; for the preview
    // target there is nothing to route the failure to, so just ignore it.)
    if (target == SoundFontLoadTarget::Slicer)
        processor.requestSampleLoad (file, DysektProcessor::LoadKindReplace);
}

#endif // DYSEKT_HAS_SFIZZ