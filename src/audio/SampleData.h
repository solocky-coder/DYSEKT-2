#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <array>
#include <functional>
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
                                                           double projectSampleRate,
                                                           std::function<bool()> shouldExit = nullptr);
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

    // Audio-thread accessors — read from activeDecoded; no redundant member copies.
    int  getNumFrames() const
    {
        return activeDecoded ? activeDecoded->buffer.getNumSamples() : 0;
    }

    bool isLoaded() const { return loaded; }

    const juce::AudioBuffer<float>& getBuffer() const
    {
        static const juce::AudioBuffer<float> kEmpty;
        return activeDecoded ? activeDecoded->buffer : kEmpty;
    }

    const juce::String& getFileName() const { return loadedFileName; }
    void setFileName (const juce::String& name) { loadedFileName = name; }

    const juce::String& getFilePath() const { return loadedFilePath; }
    void setFilePath (const juce::String& path) { loadedFilePath = path; }

private:
    // Strong reference held on the audio thread.
    // Written only in applyDecodedSample() and clear(), both called from
    // processBlock() — single writer, no lock needed.
    std::shared_ptr<DecodedSample> activeDecoded;

    // Atomic snapshot for UI / message-thread read access.
#if INTERSECT_HAS_STD_ATOMIC_SHARED_PTR
    std::atomic<std::shared_ptr<const DecodedSample>> snapshot;
#else
    std::shared_ptr<const DecodedSample> snapshot;
#endif

    juce::String loadedFileName;
    juce::String loadedFilePath;
    bool loaded = false;
};
