#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <tracktion_engine/tracktion_engine.h>

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

    MidiClip (MidiClip&& other) noexcept
    {
        const juce::ScopedWriteLock sl (other.lock);
        lengthTicks       = other.lengthTicks;
        notes             = std::move (other.notes);
        other.lengthTicks = kPPQ * 4 * 4;
        rebuildMidiList();
    }

    MidiClip& operator= (MidiClip&& other) noexcept
    {
        if (this != &other)
        {
            const juce::ScopedWriteLock wl (lock);
            const juce::ScopedWriteLock rl (other.lock);
            lengthTicks       = other.lengthTicks;
            notes             = std::move (other.notes);
            other.lengthTicks = kPPQ * 4 * 4;
            rebuildMidiList();
        }
        return *this;
    }

    JUCE_DECLARE_NON_COPYABLE (MidiClip)

    //==========================================================================
    //  Clip length
    //==========================================================================
    int64_t getLengthTicks() const noexcept { return lengthTicks; }
    double  getLengthBeats() const noexcept { return (double) lengthTicks / (double) kPPQ; }

    void setLengthTicks (int64_t t)
    {
        lengthTicks = juce::jmax ((int64_t) kPPQ, t);
        if (midiList != nullptr)
            midiList->setLength (ticksToBeats (lengthTicks));
    }

    void setLengthBeats (double beats) { setLengthTicks ((int64_t)(beats * kPPQ)); }

    //==========================================================================
    //  Bulk replace  (message thread)
    //==========================================================================
    void setNotes (juce::Array<MidiNote> newNotes)
    {
        newNotes.sort();
        const juce::ScopedWriteLock sl (lock);
        notes = std::move (newNotes);
        rebuildMidiList();
    }

    /** Read-only access for the audio thread (hold getLock() while iterating). */
    const juce::Array<MidiNote>& getNotes() const noexcept { return notes; }

    //==========================================================================
    //  Editing helpers  (message thread only)
    //==========================================================================
    int addNote (MidiNote n)
    {
        n.startTick    = juce::jmax ((int64_t) 0, n.startTick);
        n.durationTick = juce::jmax ((int64_t) 1, n.durationTick);
        const juce::ScopedWriteLock sl (lock);
        const int idx = notes.addSorted (comparator, n);
        addNoteToList (notes.getReference (idx));
        return idx;
    }

    void removeNote (int index)
    {
        const juce::ScopedWriteLock sl (lock);
        if (! juce::isPositiveAndBelow (index, notes.size())) return;
        removeNoteFromList (notes.getReference (index));
        notes.remove (index);
    }

    void moveNote (int index, int64_t newStartTick, int newNote = -1)
    {
        const juce::ScopedWriteLock sl (lock);
        if (! juce::isPositiveAndBelow (index, notes.size())) return;
        removeNoteFromList (notes.getReference (index));
        notes.getReference (index).startTick = juce::jmax ((int64_t) 0, newStartTick);
        if (newNote >= 0 && newNote <= 127)
            notes.getReference (index).note = newNote;
        notes.sort();
        rebuildMidiList();   // sort can shift indices; safest to rebuild
    }

    void resizeNote (int index, int64_t newDurationTick)
    {
        const juce::ScopedWriteLock sl (lock);
        if (! juce::isPositiveAndBelow (index, notes.size())) return;
        removeNoteFromList (notes.getReference (index));
        notes.getReference (index).durationTick = juce::jmax ((int64_t) 1, newDurationTick);
        addNoteToList (notes.getReference (index));
    }

    void setNoteVelocity (int index, int velocity)
    {
        const juce::ScopedWriteLock sl (lock);
        if (! juce::isPositiveAndBelow (index, notes.size())) return;
        removeNoteFromList (notes.getReference (index));
        notes.getReference (index).velocity = juce::jlimit (1, 127, velocity);
        addNoteToList (notes.getReference (index));
    }

    void clear()
    {
        const juce::ScopedWriteLock sl (lock);
        notes.clear();
        if (midiList != nullptr) midiList->clear();
    }

    /** Returns index of note at (tick, noteNum), or -1. */
    int hitTest (int64_t tick, int noteNum) const
    {
        const juce::ScopedReadLock sl (lock);
        for (int i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes.getReference (i);
            if (n.note == noteNum && tick >= n.startTick && tick < n.endTick())
                return i;
        }
        return -1;
    }

    //==========================================================================
    //  Tracktion integration
    //==========================================================================

    /** Attach a te::MidiList owned by a Tracktion clip.  Must be called once
     *  after the edit + clip are created.  The list is immediately populated
     *  from the current note set and kept in sync on every subsequent edit. */
    void attachMidiList (te::MidiList* list)
    {
        const juce::ScopedWriteLock sl (lock);
        midiList = list;
        rebuildMidiList();
    }

    te::MidiList* getMidiList() const noexcept { return midiList; }

    //==========================================================================
    //  Serialisation  (identical binary format to original)
    //==========================================================================
    void writeToStream (juce::MemoryOutputStream& s) const
    {
        const juce::ScopedReadLock sl (lock);
        s.writeInt64 (lengthTicks);
        s.writeInt   (notes.size());
        for (const auto& n : notes)
        {
            s.writeInt   (n.note);
            s.writeInt   (n.velocity);
            s.writeInt64 (n.startTick);
            s.writeInt64 (n.durationTick);
        }
    }

    bool readFromStream (juce::MemoryInputStream& s)
    {
        const int64_t len   = s.readInt64();
        if (len <= 0) return false;
        const int     count = s.readInt();
        if (count < 0 || count > 100000) return false;

        juce::Array<MidiNote> loaded;
        loaded.ensureStorageAllocated (count);
        for (int i = 0; i < count; ++i)
        {
            MidiNote n;
            n.note         = s.readInt();
            n.velocity     = s.readInt();
            n.startTick    = s.readInt64();
            n.durationTick = s.readInt64();
            if (n.note < 0 || n.note > 127) return false;
            loaded.add (n);
        }
        loaded.sort();

        const juce::ScopedWriteLock sl (lock);
        lengthTicks = len;
        notes       = std::move (loaded);
        rebuildMidiList();
        return true;
    }

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
    //  Tick ↔ beat mapping
    //  te::MidiList stores note positions in beats (quarter notes).
    //  At 960 PPQ, 1 beat = 960 ticks.
    //==========================================================================
    static double ticksToBeats (int64_t ticks) noexcept
    {
        return (double) ticks / (double) kPPQ;
    }

    //==========================================================================
    void addNoteToList (const MidiNote& n)
    {
        if (midiList == nullptr) return;
        midiList->addNote (n.note,
                           ticksToBeats (n.startTick),
                           ticksToBeats (n.durationTick),
                           n.velocity, 0, nullptr);
    }

    void removeNoteFromList (const MidiNote& n)
    {
        if (midiList == nullptr) return;
        const double startBeat = ticksToBeats (n.startTick);
        for (auto* mn : midiList->getNotes())
        {
            if (mn->getNoteNumber() == n.note
                && juce::approximatelyEqual (mn->getStartBeat(), startBeat))
            {
                midiList->removeNote (*mn, nullptr);
                return;
            }
        }
    }

    void rebuildMidiList()
    {
        if (midiList == nullptr) return;
        midiList->clear();
        for (const auto& n : notes)
            addNoteToList (n);
        midiList->setLength (ticksToBeats (lengthTicks));
    }
};
