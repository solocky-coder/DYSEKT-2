#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

// Forward-declare te::MidiList so this header compiles without pulling in
// the full tracktion_engine headers.  The implementation (MidiClip.cpp)
// includes tracktion_engine where the full type is needed.
namespace tracktion_engine { class MidiList; }
namespace te = tracktion_engine;

//==============================================================================
//  MidiNote  —  a single note event.  Unchanged from original.
//==============================================================================
struct MidiNote
{
    int     note         = 60;
    int     velocity     = 100;
    int64_t startTick    = 0;
    int64_t durationTick = 480;

    int64_t endTick() const noexcept { return startTick + durationTick; }
    bool operator< (const MidiNote& o) const noexcept { return startTick < o.startTick; }
};

//==============================================================================
//  MidiClip
//
//  Public API is identical to the original hand-rolled version.  Every note
//  operation is mirrored into an attached te::MidiList so Tracktion Engine
//  can render the sequence natively through its edit graph.
//
//  The juce::Array<MidiNote> shadow copy is kept in sync on every write and
//  is the sole source for the audio-thread read path — getLock() + getNotes()
//  work exactly as before.
//
//  Thread-safety contract (unchanged):
//    • All edits happen on the message thread (addNote, removeNote, …).
//    • Audio thread reads via ScopedReadLock on getLock().
//    • te::MidiList writes are always performed under the same write-lock.
//
//  Tick ↔ seconds mapping:
//    The MidiList stores beat positions in seconds at a reference tempo of
//    120 BPM.  Actual playback tempo is governed by the te::TempoSequence on
//    the edit — the storage positions are just relative timestamps.
//==============================================================================
class MidiClip
{
public:
    static constexpr int64_t kPPQ = 960;   // ticks per quarter note

    //==========================================================================
    MidiClip() = default;

    MidiClip (MidiClip&& other) noexcept;
    MidiClip& operator= (MidiClip&& other) noexcept;

    JUCE_DECLARE_NON_COPYABLE (MidiClip)

    //==========================================================================
    //  Clip length
    //==========================================================================
    int64_t getLengthTicks() const noexcept { return lengthTicks; }
    double  getLengthBeats() const noexcept { return (double) lengthTicks / (double) kPPQ; }

    void setLengthTicks (int64_t t);
    void setLengthBeats (double beats) { setLengthTicks ((int64_t)(beats * kPPQ)); }

    //==========================================================================
    //  Bulk replace  (message thread)
    //==========================================================================
    void setNotes (juce::Array<MidiNote> newNotes);

    /** Read-only access for the audio thread (hold getLock() while iterating). */
    const juce::Array<MidiNote>& getNotes() const noexcept { return notes; }

    //==========================================================================
    //  Editing helpers  (message thread only)
    //==========================================================================
    int  addNote        (MidiNote n);
    void removeNote     (int index);
    void moveNote       (int index, int64_t newStartTick, int newNote = -1);
    void resizeNote     (int index, int64_t newDurationTick);
    void setNoteVelocity(int index, int velocity);
    void clear();

    /** Returns index of note at (tick, noteNum), or -1. */
    int hitTest (int64_t tick, int noteNum) const;

    //==========================================================================
    //  Tracktion integration
    //==========================================================================

    /** Attach a te::MidiList owned by a Tracktion clip.  Must be called once
     *  after the edit + clip are created.  The list is immediately populated
     *  from the current note set and kept in sync on every subsequent edit. */
    void attachMidiList (te::MidiList* list);

    te::MidiList* getMidiList() const noexcept { return midiList; }

    //==========================================================================
    //  Serialisation  (identical binary format to original)
    //==========================================================================
    void writeToStream (juce::MemoryOutputStream& s) const;
    bool readFromStream (juce::MemoryInputStream& s);

    //==========================================================================
    const juce::ReadWriteLock& getLock() const noexcept { return lock; }

private:
    //==========================================================================
    int64_t               lengthTicks = kPPQ * 4 * 4;
    juce::Array<MidiNote> notes;
    mutable juce::ReadWriteLock lock;
    te::MidiList*         midiList = nullptr;   // non-owning; owned by te::MidiClip

    struct Comparator
    {
        static int compareElements (const MidiNote& a, const MidiNote& b) noexcept
        {
            return (a.startTick < b.startTick) ? -1 : (a.startTick > b.startTick) ? 1 : 0;
        }
    } comparator;

    //==========================================================================
    static double ticksToBeats (int64_t ticks) noexcept
    {
        return (double) ticks / (double) kPPQ;
    }

    void addNoteToList      (const MidiNote& n);
    void removeNoteFromList (const MidiNote& n);
    void rebuildMidiList();
};
