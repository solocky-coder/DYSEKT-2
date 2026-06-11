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
//  6. Posts the buffer via the processor's existing completedLoadData atomic
//     (same path as a normal WAV load).
//  7. Posts matching slice positions + MIDI notes via the new
//     pendingSfzSlices atomic so processBlock can create them after apply.
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
class SoundFontLoader
{
public:
    explicit SoundFontLoader (DysektProcessor& p) : processor (p) {}

    // ── Public API (call from UI thread) ─────────────────────────────────────
    // Queues a background job; returns immediately.
    void load (const juce::File& file);

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
