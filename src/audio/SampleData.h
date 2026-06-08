#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <array>
#include <memory>
#include <vector>

#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
#define INTERSECT_HAS_STD_ATOMIC_SHARED_PTR 1
#else
#define INTERSECT_HAS_STD_ATOMIC_SHARED_PTR 0
#endif

class SampleData
{
public:
    struct PeakMipmap
    {
        int samplesPerPeak = 0;
        std::vector<float> maxPeaks;
        std::vector<float> minPeaks;
    };

    static constexpr int kNumMipmapLevels = 3;

    struct DecodedSample
    {
        juce::AudioBuffer<float> buffer;  // always stereo
        std::array<PeakMipmap, kNumMipmapLevels> peakMipmaps;
        juce::String fileName;
        juce::String filePath;
    };

    using SnapshotPtr = std::shared_ptr<const DecodedSample>;

    SampleData();

    static std::unique_ptr<DecodedSample> decodeFromFile (const juce::File& file,
                                                           double projectSampleRate);
    void applyDecodedSample (std::unique_ptr<DecodedSample> decoded);

    /** Build peak mipmaps for a DecodedSample whose buffer has already been
     *  filled.  Call this before posting a DecodedSample to completedLoadData
     *  whenever the audio was assembled outside of decodeFromFile() (e.g. the
     *  SF2/SFZ render path in SoundFontLoader). */
    static void buildPeakMipmaps (DecodedSample& ds);
    bool loadFromFile (const juce::File& file, double projectSampleRate);
    void clear();

    /** Create a new DecodedSample containing only the audio from [trimIn, trimOut).
        Returns nullptr if the range is invalid or the source has no audio. */
    static std::unique_ptr<DecodedSample> createTrimmed (const DecodedSample& src,
                                                          int trimIn, int trimOut);
    SnapshotPtr getSnapshot() const;

    float getInterpolatedSample (double pos, int channel) const;

    // FIX #2: thread-safe reads via atomics (UI thread and audio thread safe)
    int  getNumFrames() const { return numFramesAtomic.load (std::memory_order_acquire); }
    bool isLoaded()     const { return loadedAtomic.load  (std::memory_order_acquire); }

    // NOTE: audio-thread only — backed by audioDecoded; UI callers must use getSnapshot()
    const juce::AudioBuffer<float>& getBuffer() const;

    const juce::String& getFileName() const { return loadedFileName; }
    void setFileName (const juce::String& name) { loadedFileName = name; }

    const juce::String& getFilePath() const { return loadedFilePath; }
    void setFilePath (const juce::String& path) { loadedFilePath = path; }

private:
    /** Audio-thread-exclusive snapshot.  Written only inside applyDecodedSample /
     *  clear(), which are always called from processBlock (the audio thread).
     *  Never accessed from the message thread — UI code must use getSnapshot(). */
    std::shared_ptr<const DecodedSample> audioDecoded;

#if INTERSECT_HAS_STD_ATOMIC_SHARED_PTR
    std::atomic<std::shared_ptr<const DecodedSample>> snapshot;
#else
    std::shared_ptr<const DecodedSample> snapshot;
#endif

    // FIX #2: atomic flags — safe to load from any thread with acquire semantics
    std::atomic<bool> loadedAtomic   { false };
    std::atomic<int>  numFramesAtomic { 0 };

    juce::String loadedFileName;
    juce::String loadedFilePath;
};
