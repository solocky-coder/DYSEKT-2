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
 *
 * Usage
 * -----
 *   1. Copy this file into src/audio/ (or anywhere on your include path).
 *   2. In Voice.h, replace:
 *        #include <chowdsp_filters/chowdsp_filters.h>
 *      with:
 *        #include "chowdsp_svf_filters.h"
 *   3. Remove the CHOWDSP_MODULES_DIR include path from CMakeLists.txt
 *      (both Dysekt and DysektStandalone target_include_directories blocks).
 */

#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>   // juce::dsp::ProcessSpec

namespace chowdsp
{

// ─────────────────────────────────────────────────────────────────────────────
//  Shared base: 2-channel TPT state variable filter
// ─────────────────────────────────────────────────────────────────────────────
template <typename SampleType>
class SVFBase
{
public:
    SVFBase() = default;

    void prepare (const juce::dsp::ProcessSpec& spec)
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

protected:
    // Coefficients set by each subclass in updateCoefficients()
    SampleType g  = SampleType (1);   // integrator gain
    SampleType k  = SampleType (1);   // damping
    SampleType m0 = SampleType (1);   // output mix weights
    SampleType m1 = SampleType (0);
    SampleType m2 = SampleType (0);

    SampleType sampleRate = SampleType (44100);
    SampleType cutoffHz   = SampleType (1000);
    SampleType gainDB     = SampleType (0);
    SampleType Q          = SampleType (0.707);

    // Per-channel integrator states (2 channels max)
    SampleType ic1[2] = {};
    SampleType ic2[2] = {};

    virtual void updateCoefficients() = 0;

    // Core TPT SVF tick — returns the weighted output
    inline SampleType tick (int ch, SampleType x)
    {
        const SampleType v1 = (x - ic1[ch] * (k + g) - ic2[ch]) / (SampleType (1) + g * (k + g));
        const SampleType v2 = ic1[ch] + g * v1;
        const SampleType v3 = ic2[ch] + g * v2;   // low-pass output
        ic1[ch] = v1 + v2;  // = 2*v2 - ic1_prev, standard TPT update
        ic2[ch] = v2 + v3;

        // Weighted sum: caller selects LP/BP/HP via m0/m1/m2
        return m0 * x + m1 * v1 + m2 * v2;
    }

public:
    /** Process one sample on the given channel (0 or 1). */
    SampleType processSample (int ch, SampleType x)
    {
        return tick (ch, x);
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
        const SampleType A  = std::pow (SampleType (10), this->gainDB / SampleType (40));
        const SampleType w0 = juce::MathConstants<SampleType>::pi * this->cutoffHz / this->sampleRate;
        this->g  = std::tan (w0) / std::sqrt (A);
        this->k  = SampleType (1) / this->Q;
        // Low-shelf output = A^2 * LP  +  A * BP  +  HP  (then subtract and add)
        // Using the standard Zolzer/Pirkle shelf via SVF:
        // y = HP + A*BP + A^2*LP  where HP = x - k*v1 - v2 (rearranged from SVFBase)
        // Rewrite in terms of m0/m1/m2 applied to (x, v1=BP, v2=LP):
        //   y = x + (A - 1/k)*v1 + (A^2 - 1)*v2   — simplest matching approach
        // Cleaner: use the "direct" shelf gain form:
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
        const SampleType A  = std::pow (SampleType (10), this->gainDB / SampleType (40));
        const SampleType w0 = juce::MathConstants<SampleType>::pi * this->cutoffHz / this->sampleRate;
        this->g  = std::tan (w0);
        this->k  = SampleType (1) / (this->Q * A);
        // Bell: y = x + (A^2 - 1) * k * BP
        //   m0=1, m1=(A^2-1)*k_unscaled, m2=0
        // where BP = v1 in our notation
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
        const SampleType A  = std::pow (SampleType (10), this->gainDB / SampleType (40));
        const SampleType w0 = juce::MathConstants<SampleType>::pi * this->cutoffHz / this->sampleRate;
        this->g  = std::tan (w0) * std::sqrt (A);
        this->k  = SampleType (1) / this->Q;
        // High-shelf: y = A^2*HP + A*BP + LP  (dual of low-shelf)
        // In m0/m1/m2 terms (x, v1=HP_weighted, v2=LP):
        this->m0 = A * A;
        this->m1 = this->k * (SampleType (1) - A * A);
        this->m2 = SampleType (1) - A * A;
    }
};

} // namespace chowdsp
