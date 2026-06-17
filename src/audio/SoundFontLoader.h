#pragma once
// =============================================================================
//  SoundFontLoader.h  —  SF2 / SFZ → DYSEKT sample engine  (sfizz backend)
//  ─────────────────────────────────────────────────────────────────────────
//  Requires: sfizz linked in CMakeLists.txt and DYSEKT_HAS_SFIZZ=1 defined.
//
//  What it does
//  ─────────────
//  1. Opens the SF2/SFZ with sfizz on a background thread.
//  2. Discovers which MIDI notes have audio (fast probe pass).
//  3. Renders each active note (sustain + release tail) into its own buffer.
//  4. Silence-trims both ends of every note render.
//  5. Concatenates all note renders into one stereo AudioBuffer with small
//     silence gaps between notes.
//  6. Posts the buffer via the processor's completedLoadData atomic for
//     SoundFontLoadTarget::Slicer (same path as a normal WAV load), or via
//     completedLoadData2 for SoundFontLoadTarget::SfzPlayer2 (a separate,
//     visual-only preview buffer decoupled from the Slicer engine).
//  7. For SoundFontLoadTarget::Slicer only, also posts matching slice
//     positions + MIDI notes via the pendingSfzSlices atomic so processBlock
//     can create them after apply.
//
//  Thread safety
//  ─────────────
//  Everything is posted through the same atomics the WAV loader uses, so no
//  extra synchronisation is needed.  The processor's processBlock already
//  polls completedLoadData every callback.
// =============================================================================

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#if DYSEKT_HAS_SFIZZ
  #include "../../sfizz/src/sfizz.h"
#endif

// Forward declaration — full definition is in PluginProcessor.h
class DysektProcessor;

// =============================================================================
//  SoundFontLoadTarget — which preview buffer a load() call should populate.
//  ─────────────────────────────────────────────────────────────────────────
//  Slicer     — posts to the processor's completedLoadData / pendingSfzSlices
//                atomics, same as a normal WAV load. This is the Slicer
//                engine's actual sample buffer (sampleData), used for
//                real-time playback/slicing.
//  SfzPlayer2 — posts to the processor's completedLoadData2 atomic only.
//                This is a visual-only preview buffer (sampleData2) for the
//                SFZ-PLAYER tab; it is never touched by any audio engine
//                (sfzPlayer2 has its own internal sfizz state for actual
//                playback), so no slice payload is computed/posted for it.
// =============================================================================
enum class SoundFontLoadTarget { Slicer = 0, SfzPlayer2 = 1 };

// =============================================================================
class SoundFontLoader
{
public:
    explicit SoundFontLoader (DysektProcessor& p) : processor (p) {}

    // ── Public API (call from UI thread) ─────────────────────────────────────
    // Queues a background job; returns immediately.
    void load (const juce::File& file, SoundFontLoadTarget target = SoundFontLoadTarget::Slicer);

private:
    DysektProcessor& processor;

#if DYSEKT_HAS_SFIZZ
    // ── Background job ────────────────────────────────────────────────────────
    class LoadJob;
#endif
};

// =============================================================================
//  Per-note slice descriptor — carried through to processBlock
// =============================================================================
struct SfzSliceDescriptor
{
    int startSample = 0;
    int endSample   = 0;
    int midiNote    = 36;
    int loopStart   = -1;   // -1 = no loop; sample offset within the concatenated buffer
    int loopEnd     = -1;
};

// Heap-allocated payload posted via pendingSfzSlices atomic.
// processBlock takes ownership, creates slices, then deletes it.
struct SfzSlicePayload
{
    std::vector<SfzSliceDescriptor> slices;
};
