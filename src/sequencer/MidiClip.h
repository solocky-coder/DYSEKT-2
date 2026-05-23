#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

// Forward-declare tracktion type to avoid pulling the full header into every TU.
// tracktion_engine uses:  namespace tracktion { inline namespace engine { ... } }
// so tracktion::MidiList and tracktion::engine::MidiList are the same thing.
namespace tracktion { inline namespace engine { class MidiList; } }
namespace te = tracktion;

//==============================================================================
//  MidiNote  —  a single note event inside a MidiClip.
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
//==============================================================================
class MidiClip
{
public:
    static constexpr int64_t kPPQ = 960;

    MidiClip() = default;
    MidiClip (MidiClip&& other) noexcept;
    MidiClip& operator= (MidiClip&& other) noexcept;
    JUCE_DECLARE_NON_COPYABLE (MidiClip)

    //==========================================================================
    //  Note access
    //==========================================================================
    void setNotes (juce::Array<MidiNote> newNotes);
    const juce::Array<MidiNote>& getNotes() const noexcept { return notes; }

    //==========================================================================
    //  Length
    //==========================================================================
    int64_t getLengthTicks() const noexcept { return lengthTicks; }
    void    setLengthTicks (int64_t t);

    double getLengthBeats() const noexcept { return (double)lengthTicks / (double)kPPQ; }
    void   setLengthBeats (double beats)   { setLengthTicks ((int64_t)(beats * kPPQ)); }

    //==========================================================================
    //  Editing helpers (message thread only)
    //==========================================================================
    int  addNote         (MidiNote n);
    void removeNote      (int index);
    void moveNote        (int index, int64_t newStartTick, int newNote = -1);
    void resizeNote      (int index, int64_t newDurationTick);
    void setNoteVelocity  (int index, int velocity);
    void setNoteDuration  (int index, int64_t durationTicks);
    void clear();

    int hitTest (int64_t tick, int noteNum) const;

    //==========================================================================
    //  tracktion_engine integration
    //==========================================================================
    void attachMidiList (te::MidiList* list);

    //==========================================================================
    //  Serialisation
    //==========================================================================
    void writeToStream (juce::MemoryOutputStream& s) const;
    bool readFromStream (juce::MemoryInputStream& s);

    //==========================================================================
    const juce::ReadWriteLock& getLock() const noexcept { return lock; }

private:
    int64_t               lengthTicks = kPPQ * 4 * 4;
    juce::Array<MidiNote> notes;
    mutable juce::ReadWriteLock lock;
    te::MidiList*         midiList    = nullptr;

    struct Comparator
    {
        static int compareElements (const MidiNote& a, const MidiNote& b) noexcept
        {
            return (a.startTick < b.startTick) ? -1
                 : (a.startTick > b.startTick) ?  1 : 0;
        }
    } comparator;

    static double ticksToBeats (int64_t ticks) noexcept
    {
        return (double)ticks / (double)kPPQ;
    }

    void addNoteToList      (const MidiNote& n);
    void removeNoteFromList (const MidiNote& n);
    void rebuildMidiList();
};
