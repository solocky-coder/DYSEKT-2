#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

//==============================================================================
//  MidiNote  —  a single note event inside a MidiClip.
//
//  All timing is stored in **ticks** (PPQ-based).  The SequencerEngine owns
//  the PPQ resolution (kPPQ = 960) and converts ticks ↔ samples at runtime.
//==============================================================================
struct MidiNote
{
    int   note      = 60;     // MIDI note number (0-127)
    int   velocity  = 100;    // note-on velocity  (1-127)
    int64_t startTick = 0;    // note-on position  (ticks from clip start)
    int64_t durationTick = 480; // note length in ticks (default = 1 beat @ 960 PPQ)

    int64_t endTick() const noexcept { return startTick + durationTick; }

    bool operator< (const MidiNote& o) const noexcept { return startTick < o.startTick; }
};

//==============================================================================
//  MidiClip  —  an ordered collection of MidiNotes with a fixed length.
//
//  - Notes are kept sorted by startTick at all times.
//  - lengthTicks defines the loop/clip boundary; notes beyond it are ignored
//    during playback but preserved for editing.
//  - Thread safety: all editing happens on the message thread; the audio
//    thread only reads via a lock-free snapshot (see SequencerEngine).
//==============================================================================
class MidiClip
{
public:
    static constexpr int64_t kPPQ = 960;   // ticks per quarter note

    //==========================================================================
    MidiClip() = default;

    /** Move constructor — creates a fresh lock and steals the note data.
        Required because juce::ReadWriteLock is non-copyable and non-movable. */
    MidiClip (MidiClip&& other) noexcept
    {
        const juce::ScopedWriteLock sl (other.lock);
        lengthTicks       = other.lengthTicks;
        notes             = std::move (other.notes);
        other.lengthTicks = kPPQ * 4 * 4;
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
        }
        return *this;
    }

    JUCE_DECLARE_NON_COPYABLE (MidiClip)

    /** Replace all notes atomically (call from message thread). */
    void setNotes (juce::Array<MidiNote> newNotes)
    {
        newNotes.sort();
        const juce::ScopedWriteLock sl (lock);
        notes = std::move (newNotes);
    }

    const juce::Array<MidiNote>& getNotes() const noexcept { return notes; }

    /** Clip length in ticks. Defaults to 4 bars (4 * 4 beats * kPPQ). */
    int64_t getLengthTicks() const noexcept { return lengthTicks; }
    void    setLengthTicks (int64_t t)      { lengthTicks = juce::jmax ((int64_t)kPPQ, t); }

    /** Convenience: length in beats (quarter notes). */
    double getLengthBeats() const noexcept { return (double)lengthTicks / (double)kPPQ; }
    void   setLengthBeats (double beats)   { setLengthTicks ((int64_t)(beats * kPPQ)); }

    //==========================================================================
    //  Editing helpers (message thread only)
    //==========================================================================

    int addNote (MidiNote n)
    {
        n.startTick    = juce::jmax ((int64_t)0, n.startTick);
        n.durationTick = juce::jmax ((int64_t)1, n.durationTick);
        const juce::ScopedWriteLock sl (lock);
        int idx = notes.addSorted (comparator, n);
        return idx;
    }

    void removeNote (int index)
    {
        const juce::ScopedWriteLock sl (lock);
        if (juce::isPositiveAndBelow (index, notes.size()))
            notes.remove (index);
    }

    void moveNote (int index, int64_t newStartTick, int newNote = -1)
    {
        const juce::ScopedWriteLock sl (lock);
        if (! juce::isPositiveAndBelow (index, notes.size())) return;
        notes.getReference (index).startTick = juce::jmax ((int64_t)0, newStartTick);
        if (newNote >= 0 && newNote <= 127)
            notes.getReference (index).note = newNote;
        notes.sort();
    }

    void resizeNote (int index, int64_t newDurationTick)
    {
        const juce::ScopedWriteLock sl (lock);
        if (! juce::isPositiveAndBelow (index, notes.size())) return;
        notes.getReference (index).durationTick = juce::jmax ((int64_t)1, newDurationTick);
    }

    void setNoteVelocity (int index, int velocity)
    {
        const juce::ScopedWriteLock sl (lock);
        if (! juce::isPositiveAndBelow (index, notes.size())) return;
        notes.getReference (index).velocity = juce::jlimit (1, 127, velocity);
    }

    void clear()
    {
        const juce::ScopedWriteLock sl (lock);
        notes.clear();
    }

    /** Returns index of note hit at (tick, noteNum), or -1. */
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
    //  Serialisation
    //==========================================================================

    void writeToStream (juce::MemoryOutputStream& s) const
    {
        const juce::ScopedReadLock sl (lock);
        s.writeInt64 (lengthTicks);
        s.writeInt (notes.size());
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
        const int64_t len = s.readInt64();
        if (len <= 0) return false;
        const int count = s.readInt();
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
        notes = std::move (loaded);
        return true;
    }

    //==========================================================================
    //  Lock (readers: audio thread snapshot; writers: message thread editing)
    //==========================================================================
    const juce::ReadWriteLock& getLock() const noexcept { return lock; }

private:
    int64_t               lengthTicks = kPPQ * 4 * 4;  // 4 bars default
    juce::Array<MidiNote> notes;
    mutable juce::ReadWriteLock lock;

    struct Comparator
    {
        static int compareElements (const MidiNote& a, const MidiNote& b) noexcept
        {
            return (a.startTick < b.startTick) ? -1
                 : (a.startTick > b.startTick) ?  1 : 0;
        }
    } comparator;
};
