#pragma once
#include "Slice.h"
#include <array>
#include <atomic>

class SliceManager
{
public:
    static constexpr int kMaxSlices = 32;

    /// Samples within which a placed marker snaps to an existing boundary.
    static constexpr int kSnapTolerance = 10;

    SliceManager();

    // ── Core marker-model API ─────────────────────────────────────────────────

    /** Insert a marker at @p markerPos, splitting whichever slice currently
     *  contains that position into two.  The left slice inherits all parameters
     *  from the original; the right slice gets fresh defaults.
     *
     *  @param markerPos   Sample position for the new boundary.
     *  @param totalFrames Total length of the loaded sample (for clamping).
     *  @returns           Index of the newly-created right-hand slice, or -1 on
     *                     failure (too many slices, position outside range, or
     *                     within 64 samples of an existing boundary). */
    int  insertMarker (int markerPos, int totalFrames);

    /** Remove the marker at the START of slice @p idx, merging slice @p idx
     *  into the preceding slice.  The preceding slice's parameters are kept;
     *  slice @p idx is destroyed.
     *
     *  Deleting slice 0 removes it entirely (its region becomes unassigned).
     *  The caller is responsible for ensuring @p idx > 0 when a merge is
     *  intended; pass 0 to simply remove the first slice. */
    void deleteSlice (int idx);

    /** Return the end sample of slice @p idx.
     *  In the marker model this is always slices[idx+1].startSample, or
     *  @p totalFrames for the last active slice.
     *
     *  Thread-safe: reads only startSample of the next slice, which is an int
     *  aligned field.  Safe to call from the audio thread provided totalFrames
     *  matches the currently-loaded sample length. */
    int  getEndForSlice (int idx, int totalFrames) const noexcept;

    // ── Legacy compatibility helper used by LazyChopEngine and batch operations ──

    /** Create a slice spanning [start, end).  In the marker model this inserts
     *  a marker at @p start (if one does not already exist) and one at @p end
     *  (if one does not already exist and @p end < totalFrames).
     *  Returns the index of the slice starting at @p start, or -1 on failure.
     *
     *  This keeps LazyChopEngine, CmdSplitSlice, CmdTransientChop and the
     *  SoundFontLoader working without modification. */
    int  createSlice (int start, int end);

    void clearAll();
    void rebuildMidiMap();
    int  midiNoteToSlice (int note) const;

    /** After a rebuildMidiMap() call, pin one slice to a specific MIDI note,
     *  updating both the slice's midiNote field and the internal midiMap lookup.
     *  Call this after rebuildMidiMap() — not before. */
    void pinSliceMidiNote (int sliceIdx, int note);

    float resolveParam (int sliceIdx, LockBit lockBit, float sliceValue, float globalDefault) const;

    Slice&       getSlice (int idx)       { return slices[(size_t) idx]; }
    const Slice& getSlice (int idx) const { return slices[(size_t) idx]; }
    int  getNumSlices()  const            { return numSlices; }
    void setNumSlices (int n)             { numSlices = juce::jlimit (0, kMaxSlices, n); }

    std::atomic<int> selectedSlice { -1 };
    std::atomic<int> rootNote      { 36 };

    void setSlicePalette (const juce::Colour* p) { palette.store (p, std::memory_order_relaxed); }

    // ── Internal sort (called after bulk inserts to restore order) ────────────
    /** Sort slices by startSample ascending so getEndForSlice() is correct.
     *  Must be called on the audio thread (or before the audio thread starts). */
    void sortByStart();

private:
    void assignDefaults (Slice& s, int idx);
    void assignColor   (Slice& s, int idx);

    std::atomic<const juce::Colour*> palette { nullptr };

    std::array<Slice, kMaxSlices> slices;
    int numSlices = 0;

    std::array<int, 128>              midiMap;       // note → slice index (-1 if unassigned)
};
