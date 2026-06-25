#pragma once
#include <cstdint>
#include <juce_graphics/juce_graphics.h>

enum LockBit : uint32_t
{
    kLockBpm         = 1,
    kLockPitch       = 2,
    kLockAlgorithm   = 4,
    kLockAttack      = 8,
    kLockDecay       = 16,
    kLockSustain     = 32,
    kLockRelease     = 64,
    kLockMuteGroup   = 128,
    // 256 was kLockPingPong (removed — merged into kLockLoop)
    kLockStretch       = 512,
    kLockTonality      = 1024,
    kLockFormant       = 2048,
    kLockFormantComp   = 4096,
    kLockGrainMode     = 8192,
    kLockVolume        = 16384,
    kLockReleaseTail   = 32768,
    kLockReverse       = 65536,
    kLockOutputBus     = 131072,
    kLockLoop          = 262144,
    kLockOneShot       = 524288,    // bit 19
    kLockCentsDetune   = 1048576,   // bit 20
    kLockPan           = 2097152,   // bit 21
    kLockFilter        = 4194304,   // bit 22
    kLockChromaticChannel = 8388608, // bit 23
    kLockChromaticLegato  = 16777216, // bit 24
    kLockHold              = 33554432, // bit 25
    kLockEqLow             = 67108864,   // bit 26
    kLockEqMid             = 134217728,  // bit 27
    kLockEqHigh            = 268435456,  // bit 28
};

struct Slice
{
    bool     active         = false;
    int      startSample    = 0;
    // endSample REMOVED — in the marker model, end is always derived:
    //   sliceManager.getEndForSlice(idx, totalFrames)
    //   = slices[idx+1].startSample  (or totalFrames for the last slice)
    int      midiNote       = 36;
    float    bpm            = 120.0f;
    float    pitchSemitones = 0.0f;
    int      algorithm      = 0;        // 0=Repitch, 1=Stretch
    float    attackSec      = 0.0f;   // default at slice start
    float    holdSec        = 0.0f;   // AHDSR hold time in seconds
    float    decaySec       = 0.0f;
    float    sustainLevel   = 1.0f;
    float    releaseSec     = 0.0f;   // default at slice end
    int      muteGroup      = 1;
    int      loopMode       = 0;        // 0=Off, 1=Loop, 2=Ping-Pong
    bool     stretchEnabled = false;
    float    tonalityHz     = 0.0f;
    float    formantSemitones = 0.0f;
    bool     formantComp    = false;
    int      grainMode      = 0;        // reserved (was Bungee grain mode — kept for preset compat)
    float    volume         = 0.0f;     // dB, -100..+24
    bool     releaseTail    = false;
    bool     reverse        = false;
    int      outputBus      = 0;
    bool     oneShot        = false;
    float    centsDetune    = 0.0f;     // fine pitch: -100..+100 cents
    float    pan            = 0.0f;     // stereo pan: -1 (L) .. 0 (C) .. +1 (R)
    float    filterCutoff   = 20000.0f; // low-pass cutoff Hz: 20..20000
    float    filterRes      = 0.0f;     // resonance: 0..1

    // ── Per-slice 3-band EQ ───────────────────────────────────────────────────
    // Low shelf  : gain ±18 dB, fixed fc = 200 Hz
    // Mid peak   : gain ±18 dB, fc 200-8000 Hz, Q 0.5-4.0
    // High shelf : gain ±18 dB, fixed fc = 8000 Hz
    float    eqLowGain      = 0.0f;    // dB  -18..+18
    float    eqMidGain      = 0.0f;    // dB  -18..+18
    float    eqMidFreq      = 1000.0f; // Hz  200..8000
    float    eqMidQ         = 1.0f;    // Q   0.5..4.0
    float    eqHighGain     = 0.0f;    // dB  -18..+18
    int      chromaticChannel = 0;     // 0=off, 1-16 = receive chromatic play on this MIDI channel
    bool     chromaticLegato  = false; // when true: pitch-only (no speed change), monophonic voice steal
    int      rrCounter      = 0;        // round-robin playback counter (not saved)

    /** -1 = no chain. Otherwise, the index of a slice to retrigger (looping)
     *  the instant this one-shot slice's voice naturally reaches its end.
     *  Used exclusively by the SFZ-PLAYER engine to approximate SFZ
     *  loop_start/loop_end opcodes: an SFZ region with a sustain loop is
     *  split into two slices (one-shot attack head + looped sustain tail),
     *  with the head's nextSliceIdx pointing at the tail. The Slicer never
     *  sets this field. See VoicePool::processVoiceSample's chain-retrigger
     *  path, only taken when a non-null SliceManager pointer is supplied. */
    int      nextSliceIdx   = -1;

    juce::String name;              // user-defined label; empty = show slice number
    uint32_t lockMask       = 0;
    juce::Colour colour     { []() -> juce::Colour {
        static const juce::uint32 kPal[32] = {
            // Reds / Oranges
            0xFFD82626, 0xFFF45F3D, 0xFFAD541E, 0xFFF28D0C,
            // Yellows / Golds
            0xFFE0BC51, 0xFFC1B60A, 0xFFC2D826, 0xFFBBF43D,
            // Greens
            0xFF66AD1E, 0xFF54F20C, 0xFF63E051, 0xFF0AC115, 0xFF26D852,
            // Teals / Cyans
            0xFF3DF48D, 0xFF1EAD77, 0xFF0CF2C7, 0xFF51E0E0, 0xFF0A9FC1,
            // Blues
            0xFF2695D8, 0xFF3D8DF4, 0xFF1E42AD, 0xFF0C1BF2,
            // Purples
            0xFF6351E0, 0xFF430AC1, 0xFF7F26D8, 0xFFBB3DF4, 0xFF9B1EAD,
            // Pinks / Magentas
            0xFFF20CE3, 0xFFE051BC, 0xFFC10A71, 0xFFD82669, 0xFFF43D5F,
        };
        return juce::Colour (kPal[juce::Random::getSystemRandom().nextInt (32)]);
    }() };
};
