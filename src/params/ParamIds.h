#pragma once
#include <juce_core/juce_core.h>

namespace ParamIds
{
    inline const juce::String masterVolume    { "masterVolume" };
    inline const juce::String defaultBpm      { "defaultBpm" };
    inline const juce::String defaultPitch    { "defaultPitch" };
    inline const juce::String defaultAlgorithm { "defaultAlgorithm" };
    inline const juce::String defaultAttack   { "defaultAttack" };
    inline const juce::String defaultHold     { "defaultHold" };
    inline const juce::String defaultDecay    { "defaultDecay" };
    inline const juce::String defaultSustain  { "defaultSustain" };
    inline const juce::String defaultRelease  { "defaultRelease" };
    inline const juce::String defaultMuteGroup { "defaultMuteGroup" };
    inline const juce::String defaultLoop    { "defaultLoop" };
    inline const juce::String defaultStretchEnabled { "defaultStretchEnabled" };
    inline const juce::String defaultTonality     { "defaultTonality" };
    inline const juce::String defaultFormant      { "defaultFormant" };
    inline const juce::String defaultFormantComp  { "defaultFormantComp" };
    inline const juce::String defaultGrainMode     { "defaultGrainMode" };
    inline const juce::String defaultReleaseTail  { "defaultReleaseTail" };
    inline const juce::String defaultReverse      { "defaultReverse" };
    inline const juce::String defaultOneShot      { "defaultOneShot" };
    inline const juce::String defaultCentsDetune  { "defaultCentsDetune" };
    inline const juce::String maxVoices           { "maxVoices" };

    // Selected slice boundary controls — normalised 0..1 position within sample
    inline const juce::String sliceStart          { "sliceStart" };
    inline const juce::String sliceEnd            { "sliceEnd" };

    // v17 additions
    inline const juce::String defaultPan          { "defaultPan" };
    inline const juce::String defaultFilterCutoff { "defaultFilterCutoff" };
    inline const juce::String defaultFilterRes    { "defaultFilterRes" };

    // v20 additions
    inline const juce::String globalMono          { "globalMono" };

    // v24: global EQ (post-mix, applied in PluginProcessor::processBlock)
    inline const juce::String globalEqLowGain     { "globalEqLowGain" };
    inline const juce::String globalEqLowFreq     { "globalEqLowFreq" };
    inline const juce::String globalEqLowMidGain  { "globalEqLowMidGain" };
    inline const juce::String globalEqLowMidFreq  { "globalEqLowMidFreq" };
    inline const juce::String globalEqLowMidQ     { "globalEqLowMidQ" };
    inline const juce::String globalEqMidGain     { "globalEqMidGain" };
    inline const juce::String globalEqMidFreq     { "globalEqMidFreq" };
    inline const juce::String globalEqMidQ        { "globalEqMidQ" };
    inline const juce::String globalEqHighMidGain { "globalEqHighMidGain" };
    inline const juce::String globalEqHighMidFreq { "globalEqHighMidFreq" };
    inline const juce::String globalEqHighMidQ    { "globalEqHighMidQ" };
    inline const juce::String globalEqHighGain    { "globalEqHighGain" };
    inline const juce::String globalEqHighFreq    { "globalEqHighFreq" };
    // Default per-slice EQ globals (used as fallback when slice has no lock)
    inline const juce::String defaultEqLowGain    { "defaultEqLowGain" };
    inline const juce::String defaultEqMidGain    { "defaultEqMidGain" };
    inline const juce::String defaultEqMidFreq    { "defaultEqMidFreq" };
    inline const juce::String defaultEqMidQ       { "defaultEqMidQ" };
    inline const juce::String defaultEqHighGain   { "defaultEqHighGain" };

    // v25: master audio-domain pitch shift (semitones, applied post-EQ on summed output)
    inline const juce::String masterPitch         { "masterPitch" };
}
