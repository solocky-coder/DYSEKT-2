#pragma once
// =============================================================================
//  SfzModulePanel.h  —  SFZ / SF2 instrument strip
// =============================================================================
//  Stacks below the waveform / pad-grid frame when sfzModuleOpen == true.
//  Shows: instrument name | file load button | volume knob | transpose knob
//         | MIDI channel selector | VU meters | status pill
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include "KeysPanel.h"
#include "AddZoneOverlay.h"
#include "SaveSfzOverlay.h"
#include "MessageOverlay.h"
#include "SfzFileBrowser.h"

class DysektProcessor;

class SfzModulePanel : public juce::Component,
                       public juce::Timer,
                       public juce::FileDragAndDropTarget
{
public:
    explicit SfzModulePanel (DysektProcessor& p);
    ~SfzModulePanel() override;

    void paint   (juce::Graphics&) override;
    void resized () override;
    void timerCallback() override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // Called by editor when the panel becomes visible
    void panelDidShow();

    // Callback wired by editor — triggers file chooser
    std::function<void()> onLoadRequest;

    // Keyboard sub-component
    KeysPanel keysPanel;

private:
    // ── Layout zones (computed in resized) ────────────────────────────────────
    juce::Rectangle<int> nameZone, loadBtnZone, saveAsBtnZone, volZone, transZone,
                          chZone, meterZone, midiLedZone, statusZone,
                          atkZone, decZone, susZone, relZone,  ///< ADSR knobs
                          rvSizeZone, rvDampZone, rvWidthZone, rvMixZone, rvFreezeZone; ///< Reverb knobs

    // ── Drag state for knobs ──────────────────────────────────────────────────
    enum class ActiveKnob { None, Volume, Transpose, Attack, Decay, Sustain, Release,
                              ReverbSize, ReverbDamp, ReverbWidth, ReverbMix, ReverbFreeze };
    ActiveKnob activeKnob { ActiveKnob::None };
    int        dragStartY  { 0 };
    float      dragStartVal { 0.f };

    // ── VU meter decay ────────────────────────────────────────────────────────
    float meterL { 0.f }, meterR { 0.f };
    float holdL  { 0.f }, holdR  { 0.f };
    static constexpr float kHoldDecay = 0.93f;

    // ── MIDI activity LED ─────────────────────────────────────────────────────
    bool  midiLedOn    { false };
    int   midiLedHold  { 0 };   // timer ticks remaining to keep LED lit after last NoteOff
    static constexpr int kMidiLedHoldTicks = 4; // ~133 ms at 30 Hz

    // ── Helpers ───────────────────────────────────────────────────────────────
    void openFileChooser();

    /** Bootstrap an empty Custom.sfz on first open so [+ ZONE] is always available. */
    void initEmptySfz();

    // ── Custom SFZ builder ────────────────────────────────────────────────────
    /** Step 1: pick an audio sample file. */
    void openAddZoneChooser();

    /** Step 2: show AddZoneOverlay to let user set lo/hi/root before writing. */
    void showAddZoneOverlay (const juce::File& sfzFile,
                              const juce::File& sampleFile,
                              int               prevHiKey);

    /** Write a single <region> to sfzFile with the user-confirmed key range. */
    static bool appendZoneToSfz (const juce::File& sfzFile,
                                  const juce::File& sampleFile,
                                  int loKey, int hiKey, int rootKey);

    /** Show the Save SFZ As overlay, then copy the file on confirm. */
    void openSaveAsOverlay (bool thenOpenAddZone = false);

    /** Show an overlay component full-screen over the top-level component. */
    template <typename OverlayType>
    void showOverlay (std::unique_ptr<OverlayType>& overlayPtr,
                      std::unique_ptr<OverlayType>  newOverlay)
    {
        hideOverlays();
        overlayPtr = std::move (newOverlay);
        if (auto* top = getTopLevelComponent())
        {
            top->addAndMakeVisible (*overlayPtr);
            overlayPtr->setBounds (top->getLocalBounds());
            overlayPtr->toFront (true);
        }
    }

    /** Remove any active overlay. */
    void hideOverlays();

    std::unique_ptr<AddZoneOverlay> addZoneOverlay;
    std::unique_ptr<SaveSfzOverlay> saveSfzOverlay;
    std::unique_ptr<MessageOverlay> messageOverlay;
    void drawKnob (juce::Graphics& g, juce::Rectangle<int> bounds,
                   float normalised, const juce::String& label,
                   const juce::String& valueStr) const;
    void drawMeter (juce::Graphics& g) const;

    float volToNorm    (float linear) const;  ///< linear 0..2 → 0..1
    float normToVol    (float n)      const;  ///< 0..1 → linear 0..2
    float transToNorm  (int semi)     const;  ///< -24..24 → 0..1
    int   normToTrans  (float n)      const;  ///< 0..1 → -24..24

    juce::Rectangle<int> loadBtnHitbox() const;

    void showMidiLearnMenu (int fieldId, juce::Point<int> screenPos);

    void mouseDown        (const juce::MouseEvent&) override;
    void mouseDrag        (const juce::MouseEvent&) override;
    void mouseUp          (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove   (const juce::MouseEvent&,
                           const juce::MouseWheelDetails&) override;

    std::unique_ptr<juce::FileChooser> chooser;  // kept for openFileChooser() (main load)

    SfzFileBrowser fileBrowser;
    bool           browserOpen      { false };

    // State held between openAddZoneChooser() and sample selection
    juce::File     addZoneTargetSfz;
    int            addZonePrevHiKey { -1 };

    void openBrowser  (const juce::File& startDir);
    void closeBrowser ();
    void onSampleChosen (const juce::File& f);

    // Zone parsers
    static std::vector<KeysPanel::Keyzone> parseSfzZones  (const juce::File& f);
    static std::vector<KeysPanel::Keyzone> parseSf2Zones  (const juce::File& f,
                                                              int targetBank   = 0,
                                                              int targetPreset = 0);
    void reloadZones (const juce::File& f);

    DysektProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SfzModulePanel)
};
