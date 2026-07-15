#pragma once
#include <atomic>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

class DysektProcessor;

class KeysPanel : public juce::Component,
                  private juce::Timer
{
public:
    explicit KeysPanel (DysektProcessor& p);
    ~KeysPanel() override;

    void paint     (juce::Graphics& g) override;
    void resized   () override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    // ── Keyzone overlay ───────────────────────────────────────────────────────
    struct Keyzone
    {
        int loKey    { 0 };
        int hiKey    { 127 };
        int loVel    { 0 };
        int hiVel    { 127 };
        int rootPitch{ -1 };
        bool isLooped{ false };
        juce::Colour colour;
        juce::String name;

        // Extended fields used by SFZ/SF2 zone display and real-time editing
        float volDb     { -7.0f };   ///< Volume in dB  (SFZ: volume=, SF2: velocity scaling)
        float pan       { 0.0f };    ///< Pan -1..+1    (SFZ: pan=, SF2: pan generator)
        float tuneCents { 0.0f };    ///< Fine tune in cents
        float releaseSec{ 0.664f };  ///< Release time in seconds
        bool  isSfz     { false };   ///< true = SFZ (editable), false = SF2 (read-only)
        int   assignedMidiChannel { 0 };  ///< SF2 only: 1-16 = assigned channel, 0 = none
    };

    void setKeyzones      (std::vector<Keyzone> zones);
    void clearKeyzones    ();
    void autoScrollToZones();

    /** Which processor engine this keyboard previews notes for and displays
     *  live MIDI activity from. Default is SfPlayer (legacy SF2/SFZ engine)
     *  for backward compatibility with existing call sites. */
    enum class EngineSource { SfPlayer, SfzPlayer2 };

    /** Selects the engine this panel is bound to. Call once, right after
     *  construction — SfPlayer routes note preview/highlighting through
     *  processor.sfzUiNoteOn/OffRequest + processor.sfzActiveNotes (the
     *  legacy SF2/SFZ "SF-Player" engine); SfzPlayer2 routes through
     *  processor.sfz2UiNoteOn/OffRequest + processor.sfz2ActiveNotes (the
     *  "SFZ-Player" engine, sfzPlayer2, which is channel-filtered separately
     *  via sfzPlayer2ChannelMask). Using the wrong source here is what causes
     *  a keyboard to show activity from — or be filtered by — the wrong engine. */
    void setEngineSource (EngineSource src) { engineSource = src; }

    /** Call with true when the SF-player panel is active, false when the
     *  slicer is active.  Prevents slicer MIDI-note highlights from bleeding
     *  into the SF-player keyboard even before a soundfont is loaded. */
    void setSlicerHighlightEnabled (bool enabled) { slicerHighlightEnabled = enabled; repaint(); }

    /** Pass true for SFZ files (columns are drag-editable), false for SF2. */
    void setSfzEditable (bool editable);

    /** When true, the zone matrix renders as a bank/preset list (SF2 mode)
        instead of the normal sample-zone grid. */
    void setSf2PresetListMode (bool enabled);

    /** Highlight the given row index in the zone matrix (used in SF2 preset-list
        mode to mark the currently active preset without requiring a click). */
    void setSelectedPresetRow (int rowIndex);

    /** Show or hide the [+ ZONE] button in the matrix header upper-left corner.
        When visible, clicking it fires onAddZoneRequested. */
    void setAddZoneButtonVisible (bool visible);

    /** Fired when the user clicks the [+ ZONE] button in the zone matrix header. */
    std::function<void()> onAddZoneRequested;

    /** Fired when the user drag-edits a zone row (SFZ mode only).
        Connect this in SfzDropdownPanel to write the change back to the file. */
    std::function<void (int rowIndex, const Keyzone&)> onZoneEdited;

    /** Fired when the user clicks a zone row (any mode).
        rowIndex is 0-based into the current zone list.
        Use this in SF2 preset-list mode to switch presets on click. */
    std::function<void (int rowIndex)> onRowClicked;

    /** Fired when the user RIGHT-clicks a zone row (any mode).
        rowIndex is 0-based. Use in SF2 mode to assign a MIDI channel. */
    std::function<void (int rowIndex, juce::Point<int> screenPos)> onRowRightClicked;

    /** Fired when vol/pan/tune change during a drag — lets SfzModulePanel
        forward the values to SfzPlayer for real-time preview. */
    std::function<void (int zoneIndex, float volDb, float pan, float tuneCents)> onZoneChanged;

    /** Scroll the zone matrix to highlight the row covering 'note'. */
    void highlightNoteInMatrix (int note);

    // Called by ZoneMatrixContent to schedule a deferred note-off
    void scheduleNoteOff (int note);

private:
    // =========================================================================
    // ZoneMatrixContent
    // =========================================================================
    class ZoneMatrixContent : public juce::Component
    {
    public:
        static constexpr int kRowH    = 28;
        static constexpr int kHeaderH = 18;

        ZoneMatrixContent (KeysPanel& owner) : owner (owner) {}

        void rebuild (const std::vector<Keyzone>& zones,
                      int kbX, int kbW,
                      int baseOctave,
                      int whiteKeyW, int blackKeyW,
                      int componentWidth);

        /** Select and scroll to the first row that covers 'note'. -1 = clear. */
        void highlightNote (int note);

        void paint     (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp   (const juce::MouseEvent& e) override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent& e) override;

        int  selectedRow = -1;

        /** When true, numeric columns are drag-editable (SFZ only). */
        bool sfzEditable = false;

        /** When true, the matrix is displaying SF2 bank/preset rows (not sample zones).
            This suppresses the numeric columns and shows only the preset name column. */
        bool sf2PresetListMode = false;

        /** Called after a drag-edit commits a zone change.
            Row index and the updated Keyzone are passed. */
        std::function<void (int rowIndex, const Keyzone&)> onZoneEdited;

        /** Called when the user clicks a row (any mode, fires before audition).
            rowIndex is 0-based into rows[]. */
        std::function<void (int rowIndex)> onRowClicked;

        /** Called when the user right-clicks a row (any mode).
            rowIndex is 0-based; screenPos is in screen coordinates. */
        std::function<void (int rowIndex, juce::Point<int> screenPos)> onRowRightClicked;

        /** When true, an [+ ZONE] button is drawn in the upper-left header corner.
            Clicking it fires onAddZoneRequested (forwarded from KeysPanel). */
        bool addZoneBtnVisible = false;

        /** Fired when the user clicks the [+ ZONE] header button. */
        std::function<void()> onAddZoneClicked;

    private:
        enum class EditCol { None, LoKey, HiKey, LoVel, HiVel, Root, Loop,
                             Pitch, Pan, Vol, Release };

        EditCol hitTestCol (int x, int w) const;

        KeysPanel& owner;
        struct Row { Keyzone zone; };
        std::vector<Row> rows;
        int kbX_ = 0, kbW_ = 0, baseOctave_ = 0, contentW_ = 0;

        // Drag-edit state
        EditCol dragCol      = EditCol::None;
        int     dragRow      = -1;
        int     dragStartY   = 0;
        float   dragStartVal = 0.f;   // float to handle dB / cents / pan
    };

    // =========================================================================
    void timerCallback() override;
    void releaseLastNote();

    struct KeyRect { juce::Rectangle<int> bounds; int note; bool isBlack; };
    std::vector<KeyRect>  keyRects;
    std::vector<Keyzone>  keyzones;

    DysektProcessor& processor;

    bool slicerHighlightEnabled = true; ///< false in SF-player mode — suppresses slicer note borders

    int baseOctave     = 3;
    int lastActiveNote = -1;
    int hoveredNote    = -1;
    int pendingNoteOff = -1;

    uint64_t sfzActiveSnap[2] = { 0, 0 };

    EngineSource engineSource = EngineSource::SfPlayer;

    /** Returns processor.sfzUiNoteOnRequest/OffRequest or
     *  processor.sfz2UiNoteOnRequest/OffRequest depending on engineSource. */
    std::atomic<int>& uiNoteOnAtomic()  const;
    std::atomic<int>& uiNoteOffAtomic() const;

    /** Returns processor.sfzActiveNotes or processor.sfz2ActiveNotes
     *  depending on engineSource. */
    std::atomic<uint64_t>* activeNotesAtomics() const;

    juce::TextButton transposeDownBtn { "<" };
    juce::TextButton transposeUpBtn   { ">" };

    juce::Viewport    zoneViewport;
    ZoneMatrixContent zoneMatrix { *this };

    // Full-keyboard: MIDI 0-127 = 75 white + 53 black keys
    static constexpr int kTotalWhite = 75;
    static constexpr int kTotalBlack = 53;

    int   kTransposeRowH = 0;
    int   kZoneViewH     = 0;
    int   kKeyH          = 44;
    int   kWhiteKeyW     = 0;
    float kWhiteKeyWf    = 1.f;
    int   kBlackKeyW     = 0;
    int   kBlackKeyH     = 0;
    int   kNumWhite      = kTotalWhite;
    int   kNumBlack      = kTotalBlack;

    void  drawKey           (juce::Graphics&, const KeyRect&,
                             bool hasSlice, bool hovered, bool active) const;
    float noteToX           (int note, int kbX) const;
    float noteKeyWidth      (int note) const;
    juce::Colour zoneColourForNote (int note) const;
    void  rebuildZoneMatrix ();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeysPanel)
};
