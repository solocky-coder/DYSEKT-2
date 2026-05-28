#include "ParamLayout.h"
#include "ParamIds.h"

juce::AudioProcessorValueTreeState::ParameterLayout ParamLayout::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // ── Most-reached-for (first 8) ────────────────────────────────────────────

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultBpm, 1 },
        "Sample BPM",
        juce::NormalisableRange<float> (20.0f, 999.0f, 0.01f),
        120.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultPitch, 1 },
        "Sample Pitch",
        juce::NormalisableRange<float> (-48.0f, 48.0f, 0.01f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultCentsDetune, 1 },
        "Sample Cents Detune",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIds::defaultAlgorithm, 1 },
        "Sample Algorithm",
        juce::StringArray { "Repitch", "Stretch" },
        0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultAttack, 1 },
        "Sample Attack",
        juce::NormalisableRange<float> (0.0f, 5000.0f, 0.1f, 0.3f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultHold, 1 },
        "Sample Hold",
        juce::NormalisableRange<float> (0.0f, 5000.0f, 0.1f, 0.3f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultRelease, 1 },
        "Sample Release",
        juce::NormalisableRange<float> (0.0f, 5000.0f, 0.1f, 0.3f),
        10.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::masterVolume, 1 },
        "Master Gain",
        juce::NormalisableRange<float> (-100.0f, 24.0f, 0.1f),
        0.0f));

    // ── Secondary sample params ────────────────────────────────────────────────

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultReverse, 1 },
        "Sample Reverse",
        false));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIds::defaultLoop, 1 },
        "Sample Loop Mode",
        juce::StringArray { "Off", "Loop", "Ping-Pong" },
        0));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultStretchEnabled, 1 },
        "Sample Stretch",
        false));

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParamIds::defaultMuteGroup, 1 },
        "Sample Mute Group",
        0, 32, 0));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultDecay, 1 },
        "Sample Decay",
        juce::NormalisableRange<float> (0.0f, 5000.0f, 0.1f, 0.3f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultSustain, 1 },
        "Sample Sustain",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f),
        100.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultReleaseTail, 1 },
        "Sample Release Tail",
        false));

    // defaultOneShot kept in layout for DAW preset compatibility —
    // the global is intentionally never read; per-slice oneShot is used instead.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultOneShot, 1 },
        "Sample One Shot",
        false));

    // ── Advanced / algorithm-specific ─────────────────────────────────────────

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultTonality, 1 },
        "Sample Tonality",
        juce::NormalisableRange<float> (0.0f, 8000.0f, 1.0f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFormant, 1 },
        "Sample Formant",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::defaultFormantComp, 1 },
        "Sample Formant Comp",
        false));

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIds::defaultGrainMode, 1 },
        "Sample Grain Mode",
        juce::StringArray { "Fast", "Normal", "Smooth" },
        1));

    // ── Global utility ─────────────────────────────────────────────────────────

    params.push_back (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { ParamIds::maxVoices, 1 },
        "Max Voices",
        1, 31, 16));

    // ── v17: Pan, Filter ──────────────────────────────────────────────────────
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultPan, 1 },
        "Pan",
        juce::NormalisableRange<float> (-1.0f, 1.0f, 0.01f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterCutoff, 1 },
        "Filter Cutoff",
        juce::NormalisableRange<float> (20.0f, 20000.0f, 1.0f, 0.25f),  // skewed toward low end
        20000.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultFilterRes, 1 },
        "Filter Resonance",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.0f));

    // v20: global Poly/Mono switch
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { ParamIds::globalMono, 1 },
        "Global Mono",
        false));

    // ── v24: per-slice EQ defaults ─────────────────────────────────────────────
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultEqLowGain, 1 },
        "EQ Low Gain",
        juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultEqMidGain, 1 },
        "EQ Mid Gain",
        juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultEqMidFreq, 1 },
        "EQ Mid Freq",
        juce::NormalisableRange<float> (200.0f, 8000.0f, 1.0f, 0.4f),
        1000.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultEqMidQ, 1 },
        "EQ Mid Q",
        juce::NormalisableRange<float> (0.5f, 4.0f, 0.01f),
        1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::defaultEqHighGain, 1 },
        "EQ High Gain",
        juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f),
        0.0f));

    // ── v24: global post-mix EQ ────────────────────────────────────────────────
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::globalEqLowGain, 1 },
        "Global EQ Low",
        juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::globalEqLowFreq, 1 },
        "Global EQ Low Freq",
        juce::NormalisableRange<float> (20.0f, 500.0f, 1.0f, 0.4f),
        80.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::globalEqLowMidGain, 1 },
        "Global EQ Low-Mid Gain",
        juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::globalEqLowMidFreq, 1 },
        "Global EQ Low-Mid Freq",
        juce::NormalisableRange<float> (100.0f, 1000.0f, 1.0f, 0.4f),
        250.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::globalEqLowMidQ, 1 },
        "Global EQ Low-Mid Q",
        juce::NormalisableRange<float> (0.5f, 4.0f, 0.01f),
        1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::globalEqMidGain, 1 },
        "Global EQ Mid",
        juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::globalEqMidFreq, 1 },
        "Global EQ Mid Freq",
        juce::NormalisableRange<float> (500.0f, 5000.0f, 1.0f, 0.4f),
        1000.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::globalEqMidQ, 1 },
        "Global EQ Mid Q",
        juce::NormalisableRange<float> (0.5f, 4.0f, 0.01f),
        1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::globalEqHighMidGain, 1 },
        "Global EQ High-Mid Gain",
        juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::globalEqHighMidFreq, 1 },
        "Global EQ High-Mid Freq",
        juce::NormalisableRange<float> (1000.0f, 10000.0f, 1.0f, 0.4f),
        4000.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::globalEqHighMidQ, 1 },
        "Global EQ High-Mid Q",
        juce::NormalisableRange<float> (0.5f, 4.0f, 0.01f),
        1.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::globalEqHighGain, 1 },
        "Global EQ High",
        juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f),
        0.0f));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::globalEqHighFreq, 1 },
        "Global EQ High Freq",
        juce::NormalisableRange<float> (2000.0f, 20000.0f, 1.0f, 0.4f),
        12000.0f));

    // v25: master audio-domain pitch shift (semitones) — applied post-EQ on summed output
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIds::masterPitch, 1 },
        "Master Pitch",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f),
        0.0f));

    return { params.begin(), params.end() };
}
