#pragma once
// =============================================================================
//  SoundFontLoader.h  —  SF2 / SFZ → DYSEKT-SF sample engine  (sfizz backend)
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
//  7. For SoundFontLoadTarget::Slicer, also posts matching slice positions +
//     MIDI notes via the pendingSfzSlices atomic so processBlock can create
//     them after apply. For SoundFontLoadTarget::SfzPlayer2, the same
//     per-note descriptors are instead posted via the pendingPreviewZones2
//     atomic into a SfzPreviewZoneStore — a read-only "preview zones"
//     overlay so the SFZ-PLAYER's waveform can show the same colored
//     per-note zone bands as the Slicer, without ever touching sliceManager.
//
//  Thread safety
//  ─────────────
//  Everything is posted through the same atomics the WAV loader uses, so no
//  extra synchronisation is needed.  The processor's processBlock already
//  polls completedLoadData every callback.
// =============================================================================

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "SampleData.h"   // for INTERSECT_HAS_STD_ATOMIC_SHARED_PTR

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
//  SfzPlayer2 — posts to the processor's completedLoadData2 atomic for the
//                visual-only preview buffer (sampleData2) used by the
//                SFZ-PLAYER tab; it is never touched by any audio engine
//                (sfzPlayer2 has its own internal sfizz state for actual
//                playback). Also posts the same per-note descriptors via
//                pendingPreviewZones2 into a SfzPreviewZoneStore, so the
//                waveform can draw a read-only zone overlay — no slices
//                are ever created in sliceManager for this target.
//  SfPlayer   — posts to the processor's completedLoadData3 atomic for the
//                visual-only preview buffer (sampleData3) used by the
//                SF2-PLAYER tab. Mirrors SfzPlayer2 exactly, including the
//                pendingPreviewZones3 zone overlay. Note this still renders
//                via sfizz (which can load .sf2 files directly) rather than
//                the real FluidSynth engine sfzPlayer uses for live playback
//                — a deliberate display-accuracy tradeoff, not a live-audio
//                one; the waveform shown may not be bit-identical to what
//                FluidSynth actually plays.
// =============================================================================
enum class SoundFontLoadTarget { Slicer = 0, SfzPlayer2 = 1, SfPlayer = 2 };

// =============================================================================
class SoundFontLoader
{
public:
    explicit SoundFontLoader (DysektProcessor& p) : processor (p) {}

    // ── Public API (call from UI thread) ─────────────────────────────────────
    // Queues a background job; returns immediately.
    //
    // presetBank/presetProgram (SfPlayer target only): when presetProgram >= 0,
    // the job sends a bank-select (CC0/CC32) + program-change to the sfizz
    // synth right after sfizz_load_file(), BEFORE probing/rendering — so the
    // resulting sampleData3/previewZones3 reflect that specific preset's
    // regions rather than whatever preset sfizz defaults to on load. Leave
    // both at -1 (the default) to render the file's default preset, as before.
    void load (const juce::File& file, SoundFontLoadTarget target = SoundFontLoadTarget::Slicer,
               int presetBank = -1, int presetProgram = -1);

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

// =============================================================================
//  SfzPreviewZoneStore — read-only "preview zones" for the SFZ-PLAYER tab.
//  ─────────────────────────────────────────────────────────────────────────
//  Holds the same per-note descriptors as SfzSlicePayload, but for display
//  only — there is no sliceManager involvement, no editing, no playback
//  binding. The UI thread reads a snapshot every paint(); processBlock
//  publishes a new one whenever a SfzPlayer2-target load completes.
//
//  Uses the same atomic-shared-ptr snapshot idiom as SampleData (see
//  INTERSECT_HAS_STD_ATOMIC_SHARED_PTR in SampleData.h) so reads from the UI
//  thread never race with a concurrent publish from processBlock.
// =============================================================================
class SfzPreviewZoneStore
{
public:
    using ZoneList    = std::vector<SfzSliceDescriptor>;
    using SnapshotPtr = std::shared_ptr<const ZoneList>;

    /** Publish a new zone list (UI/audio thread — called from processBlock
     *  after consuming pendingPreviewZones2). Takes ownership. */
    void set (std::unique_ptr<ZoneList> zones)
    {
        SnapshotPtr view = std::move (zones);

#if INTERSECT_HAS_STD_ATOMIC_SHARED_PTR
        snapshot.store (view, std::memory_order_release);
#else
        std::atomic_store_explicit (&snapshot, view, std::memory_order_release);
#endif
    }

    /** Read the current zone list (UI thread, called from paint()). May be
     *  empty (but never null) if no SfzPlayer2-target load has completed yet. */
    SnapshotPtr get() const
    {
#if INTERSECT_HAS_STD_ATOMIC_SHARED_PTR
        auto view = snapshot.load (std::memory_order_acquire);
#else
        auto view = std::atomic_load_explicit (&snapshot, std::memory_order_acquire);
#endif
        if (view == nullptr)
            view = std::make_shared<const ZoneList>();
        return view;
    }

private:
#if INTERSECT_HAS_STD_ATOMIC_SHARED_PTR
    std::atomic<SnapshotPtr> snapshot;
#else
    SnapshotPtr snapshot;
#endif
};

// Heap-allocated payload posted via pendingPreviewZones2/pendingPreviewZones3
// atomics. processBlock takes ownership: for pendingPreviewZones2 (SFZ-PLAYER),
// each descriptor becomes a real slice in sliceManager2 (see Slice::nextSliceIdx
// for the loop-region two-slice split); for pendingPreviewZones3 (SF2-PLAYER),
// it's folded into the previewZones3 read-only display overlay. Either way the
// unique_ptr is destroyed once consumed — no manual delete needed, unlike
// SfzSlicePayload (which predates this and still uses raw new/delete).
struct SfzPreviewZonePayload
{
    std::vector<SfzSliceDescriptor> slices;

    // Which preset these zones/sampleData3 belong to (SfPlayer target only).
    // -1/-1 means "the file's default preset" (SfzPlayer2 target, or an
    // SfPlayer load that didn't request a specific preset). Consumed in
    // processBlock to update DysektProcessor::sf2PreviewRenderedBank/Program.
    int presetBank    = -1;
    int presetProgram = -1;
};
