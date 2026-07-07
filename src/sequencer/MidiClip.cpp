// MidiClip.cpp
//
// tracktion_engine header removed. The midiList pointer and attachMidiList()
// are kept as a future hook (see MidiClip.h) but all midiList->... calls are
// now guarded by (midiList != nullptr), which is always false until something
// calls attachMidiList() again.  No functional change to DYSEKT-SF's own sequencer.

#include "MidiClip.h"

//==============================================================================
MidiClip::MidiClip (MidiClip&& other) noexcept
{
    const juce::ScopedWriteLock sl (other.lock);
    lengthTicks       = other.lengthTicks;
    notes             = std::move (other.notes);
    other.lengthTicks = kPPQ * 4 * 4;
    // midiList is not transferred — caller must re-attach if needed.
}

MidiClip& MidiClip::operator= (MidiClip&& other) noexcept
{
    if (this != &other)
    {
        const juce::ScopedWriteLock wl (lock);
        const juce::ScopedWriteLock rl (other.lock);
        lengthTicks       = other.lengthTicks;
        notes             = std::move (other.notes);
        other.lengthTicks = kPPQ * 4 * 4;
        // midiList is not transferred — caller must re-attach if needed.
    }
    return *this;
}

//==============================================================================
void MidiClip::setLengthTicks (int64_t t)
{
    lengthTicks = juce::jmax ((int64_t) kPPQ, t);
    // midiList->setLength() would go here when tracktion is re-enabled.
}

//==============================================================================
void MidiClip::setNotes (juce::Array<MidiNote> newNotes)
{
    newNotes.sort();
    const juce::ScopedWriteLock sl (lock);
    notes = std::move (newNotes);
    rebuildMidiList();
}

//==============================================================================
int MidiClip::addNote (MidiNote n)
{
    n.startTick    = juce::jmax ((int64_t) 0, n.startTick);
    n.durationTick = juce::jmax ((int64_t) 1, n.durationTick);
    const juce::ScopedWriteLock sl (lock);
    const int idx = notes.addSorted (comparator, n);
    addNoteToList (notes.getReference (idx));
    return idx;
}

void MidiClip::removeNote (int index)
{
    const juce::ScopedWriteLock sl (lock);
    if (! juce::isPositiveAndBelow (index, notes.size())) return;
    removeNoteFromList (notes.getReference (index));
    notes.remove (index);
}

void MidiClip::moveNote (int index, int64_t newStartTick, int newNote)
{
    const juce::ScopedWriteLock sl (lock);
    if (! juce::isPositiveAndBelow (index, notes.size())) return;
    removeNoteFromList (notes.getReference (index));
    notes.getReference (index).startTick = juce::jmax ((int64_t) 0, newStartTick);
    if (newNote >= 0 && newNote <= 127)
        notes.getReference (index).note = newNote;
    notes.sort();
    rebuildMidiList();
}

void MidiClip::resizeNote (int index, int64_t newDurationTick)
{
    const juce::ScopedWriteLock sl (lock);
    if (! juce::isPositiveAndBelow (index, notes.size())) return;
    removeNoteFromList (notes.getReference (index));
    notes.getReference (index).durationTick = juce::jmax ((int64_t) 1, newDurationTick);
    addNoteToList (notes.getReference (index));
}

void MidiClip::setNoteVelocity (int index, int velocity)
{
    const juce::ScopedWriteLock sl (lock);
    if (! juce::isPositiveAndBelow (index, notes.size())) return;
    removeNoteFromList (notes.getReference (index));
    notes.getReference (index).velocity = juce::jlimit (1, 127, velocity);
    addNoteToList (notes.getReference (index));
}

void MidiClip::setNoteDuration (int index, int64_t durationTicks)
{
    const juce::ScopedWriteLock sl (lock);
    if (! juce::isPositiveAndBelow (index, notes.size())) return;
    removeNoteFromList (notes.getReference (index));
    notes.getReference (index).durationTick = juce::jmax ((int64_t) 1, durationTicks);
    addNoteToList (notes.getReference (index));
}

void MidiClip::clear()
{
    const juce::ScopedWriteLock sl (lock);
    notes.clear();
    // midiList->clear() would go here when tracktion is re-enabled.
}

//==============================================================================
int MidiClip::hitTest (int64_t tick, int noteNum) const
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

//==============================================================================
void MidiClip::attachMidiList (te::MidiList* list)
{
    // Kept as a future hook. When tracktion is re-enabled, uncomment the body.
    const juce::ScopedWriteLock sl (lock);
    midiList = list;
    rebuildMidiList();   // safe: rebuildMidiList() guards on (midiList != nullptr)
}

//==============================================================================
void MidiClip::writeToStream (juce::MemoryOutputStream& s) const
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

bool MidiClip::readFromStream (juce::MemoryInputStream& s)
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

//==============================================================================
//  Private helpers  —  all guarded on (midiList != nullptr).
//  These are intentional no-ops until tracktion is re-attached.
//==============================================================================
void MidiClip::addNoteToList (const MidiNote& /*n*/)
{
    // When tracktion is re-enabled:
    // midiList->addNote (n.note,
    //                    tracktion::core::BeatPosition::fromBeats (ticksToBeats (n.startTick)),
    //                    tracktion::core::BeatDuration::fromBeats (ticksToBeats (n.durationTick)),
    //                    n.velocity, 0, nullptr);
}

void MidiClip::removeNoteFromList (const MidiNote& /*n*/)
{
    // When tracktion is re-enabled:
    // const double startBeat = ticksToBeats (n.startTick);
    // for (auto* mn : midiList->getNotes())
    //     if (mn->getNoteNumber() == n.note
    //         && juce::approximatelyEqual ((double) mn->getStartBeat(), startBeat))
    //         { midiList->removeNote (*mn, nullptr); return; }
}

void MidiClip::rebuildMidiList()
{
    if (midiList == nullptr) return;
    // When tracktion is re-enabled, restore full rebuild here.
    // For now, midiList is always nullptr so this is unreachable.
}
