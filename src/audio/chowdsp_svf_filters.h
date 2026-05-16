#pragma once
/**
 * chowdsp_svf_filters.h
 *
 * Drop-in replacement for the three chowdsp SVF filter types used in Voice.h:
 *   chowdsp::SVFLowShelf<float>
 *   chowdsp::SVFBell<float>
 *   chowdsp::SVFHighShelf<float>
 *
 * API is intentionally identical to chowdsp_utils v2.3.0 so Voice.h and
 * VoicePool.cpp compile unchanged.  No external dependencies — only <cmath>.
 *
 * Implementation: Zavalishin "The Art of VA Filter Design" (2018) TPT SVF,
 * with gain shelving / peaking extensions from the same source.
 */

#include <cmath>

namespace chowdsp
{

// ─────────────────────────────────────────────────────────────────────────────
//  Minimal ProcessSpec — mirrors juce::dsp::ProcessSpec without pulling in
//  juce_dsp.  VoicePool.cpp constructs one as an aggregate: { sr, blockSize, ch }
// ─────────────────────────────────────────────────────────────────────────────
struct ProcessSpec
{
    double       sampleRate;
    unsigned int maximumBlockSize;
    unsigned int numChannels;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Shared base: 2-channel TPT state variable filter
// ─────────────────────────────────────────────────────────────────────────────
template <typename SampleType>
class SVFBase
{
public:
    SVFBase() = default;

    void prepare (const ProcessSpec& spec)
    {
        sampleRate = static_cast<SampleType> (spec.sampleRate);
        reset();
        updateCoefficients();
    }

    void reset()
    {
        for (auto& s : ic1) s = SampleType (0);
        for (auto& s : ic2) s = SampleType (0);
    }

    void setCutoffFrequency (SampleType hz)
    {
        cutoffHz = hz;
        updateCoefficients();
    }

    void setGainDecibels (SampleType dB)
    {
        gainDB = dB;
        updateCoefficients();
    }

    void setQValue (SampleType q)
    {
        Q = q;
        updateCoefficients();
    }

    /** Process one sample on the given channel (0 or 1). */
    SampleType processSample (int ch, SampleType x)
    {
        return tick (ch, x);
    }

protected:
    SampleType g  = SampleType (1);
    SampleType k  = SampleType (1);
    SampleType m0 = SampleType (1);
    SampleType m1 = SampleType (0);
    SampleType m2 = SampleType (0);

    SampleType sampleRate = SampleType (44100);
    SampleType cutoffHz   = SampleType (1000);
    SampleType gainDB     = SampleType (0);
    SampleType Q          = SampleType (0.707);

    SampleType ic1[2] = {};
    SampleType ic2[2] = {};

    virtual void updateCoefficients() = 0;

    inline SampleType tick (int ch, SampleType x)
    {
        const SampleType v1 = (x - ic1[ch] * (k + g) - ic2[ch]) / (SampleType (1) + g * (k + g));
        const SampleType v2 = ic1[ch] + g * v1;
        const SampleType v3 = ic2[ch] + g * v2;
        ic1[ch] = v1 + v2;
        ic2[ch] = v2 + v3;
        return m0 * x + m1 * v1 + m2 * v2;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Low-shelf filter
// ─────────────────────────────────────────────────────────────────────────────
template <typename SampleType>
class SVFLowShelf : public SVFBase<SampleType>
{
protected:
    void updateCoefficients() override
    {
        const SampleType pi = SampleType (3.14159265358979323846);
        const SampleType A  = std::pow (SampleType (10), this->gainDB / SampleType (40));
        const SampleType w0 = pi * this->cutoffHz / this->sampleRate;
        this->g  = std::tan (w0) / std::sqrt (A);
        this->k  = SampleType (1) / this->Q;
        this->m0 = SampleType (1);
        this->m1 = this->k * (A * A - SampleType (1));
        this->m2 = A * A - SampleType (1);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  Bell (peaking EQ) filter
// ─────────────────────────────────────────────────────────────────────────────
template <typename SampleType>
class SVFBell : public SVFBase<SampleType>
{
protected:
    void updateCoefficients() override
    {
        const SampleType pi = SampleType (3.14159265358979323846);
        const SampleType A  = std::pow (SampleType (10), this->gainDB / SampleType (40));
        const SampleType w0 = pi * this->cutoffHz / this->sampleRate;
        this->g  = std::tan (w0);
        this->k  = SampleType (1) / (this->Q * A);
        this->m0 = SampleType (1);
        this->m1 = this->k * (A * A - SampleType (1));
        this->m2 = SampleType (0);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
//  High-shelf filter
// ─────────────────────────────────────────────────────────────────────────────
template <typename SampleType>
class SVFHighShelf : public SVFBase<SampleType>
{
protected:
    void updateCoefficients() override
    {
        const SampleType pi = SampleType (3.14159265358979323846);
        const SampleType A  = std::pow (SampleType (10), this->gainDB / SampleType (40));
        const SampleType w0 = pi * this->cutoffHz / this->sampleRate;
        this->g  = std::tan (w0) * std::sqrt (A);
        this->k  = SampleType (1) / this->Q;
        this->m0 = A * A;
        this->m1 = this->k * (SampleType (1) - A * A);
        this->m2 = SampleType (1) - A * A;
    }
};

} // namespace chowdsp
