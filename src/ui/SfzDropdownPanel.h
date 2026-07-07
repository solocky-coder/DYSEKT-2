#pragma once
// =============================================================================
//  SfzDropdownPanel.h  —  SF2 / SFZ instrument strip
// =============================================================================
//  Header strip layout (left → right):
//    [< Preset Name >] [TRN] [FINE] [REV MIX] [REV SIZE] [PAN] [VOL] [METER]
//
//  File loading is handled by the shared, global FileBrowserPanel (opened via
//  the header's Browse button) rather than an inline browser — this matches
//  the Slicer tab's behavior. PluginEditor wires FileBrowserPanel's
//  onLoadRequest callback to call onFileChosen() on this panel when a .sf2
//  file is picked while uiMode == SF2-PLAYER.
//
//  The preset picker still doubles as navigation once a file IS loaded:
//    • Left/right arrows step through SF2 presets.
//    • Clicking the label opens the SF2 program grid.
//    • Mouse-wheel on the picker scrolls presets.
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "KeysPanel.h"
#include "../audio/SfzPlayer.h"

class DysektProcessor;

// =============================================================================
#include "Sf2ProgramGrid.h"

// =============================================================================
//  SfzDropdownPanel
// =============================================================================
class SfzDropdownPanel : public juce::Component,
                         public juce::Timer,
                         public juce::FileDragAndDropTarget
{
public:
    explicit SfzDropdownPanel (DysektProcessor& p);
    ~SfzDropdownPanel() override;

    // ── Core overrides ────────────────────────────────────────────────────────
    void paint   (juce::Graphics&) override;
    void resized () override;
    void timerCallback() override;

    // ── FileDragAndDropTarget ─────────────────────────────────────────────────
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // ── Public API ────────────────────────────────────────────────────────────
    void panelDidShow();

    /** Returns true if the SF2 program grid is currently shown (programPickerOpen). */
    bool isProgramGridOpen() const noexcept { return programPickerOpen; }

    /** Called after a new SF2 file has been accepted. */
    std::function<void (const juce::File&)> onFileLoaded;

    /**
     * Accepts a newly-picked .sf2 file (e.g. routed here from PluginEditor's
     * shared FileBrowserPanel::onLoadRequest when uiMode == SF2-PLAYER).
     * Loads the file, sets the fan-out channel mask, opens the program grid,
     * and fires onFileLoaded. Silently ignores non-.sf2 files.
     */
    void onFileChosen (const juce::File& f);

    /** Fired when the user right-clicks a preset cell and assigns a MIDI channel. */
    std::function<void (const Sf2PresetInfo&, int midiChannel1Based)> onPresetChannelAssigned;

    // ── SF2 channel-FX public API ─────────────────────────────────────────────
    /** Called by PluginEditor whenever a preset<->channel mapping changes. */
    void notifyPresetChannelChanged (const juce::String& presetName, int midiCh1Based);

    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr int kStripH  = 36;

    /** Direct access to the SF2 program grid (read-only) for PluginEditor. */
    const Sf2ProgramGrid& getProgramGrid() const noexcept { return programGrid; }

private:
    // ── Header-strip drawing ──────────────────────────────────────────────────
    void drawHeaderStrip (juce::Graphics& g) const;
    void drawSf2ChStrip  (juce::Graphics& g) const;
    void drawKnob (juce::Graphics& g, juce::Rectangle<int> bounds,
                   float normalised, const juce::String& label,
                   const juce::String& valueStr) const;
    void drawMeter (juce::Graphics& g) const;

    // ── Layout zones (computed in resized) ────────────────────────────────────
    juce::Rectangle<int> volZone, transZone,
                          panZone, fineZone,
                          rvMixZone, rvSizeZone,
                          meterZone;

    // Per-channel SF2 FX zones
    juce::Rectangle<int> chComboZone;
    juce::Rectangle<int> chMixZone;
    juce::Rectangle<int> chSizeZone;
    juce::Rectangle<int> chDampZone;
    juce::Rectangle<int> chGainZone;

    // ── Drag state for knobs ──────────────────────────────────────────────────
    enum class ActiveKnob { None, Volume, Transpose, Pan, FineTune, ReverbMix, ReverbSize,
                            ChReverbMix, ChReverbSize, ChReverbDamp, ChGain };
    ActiveKnob activeKnob  { ActiveKnob::None };
    int        dragStartY  { 0 };
    float      dragStartVal{ 0.f };

    // ── VU meter ──────────────────────────────────────────────────────────────
    float meterL { 0.f }, meterR { 0.f };
    float holdL  { 0.f }, holdR  { 0.f };
    static constexpr float kHoldDecay = 0.93f;

    // ── MIDI activity LED ─────────────────────────────────────────────────────
    juce::Rectangle<int> midiLedZone;
    bool  midiLedOn   { false };
    int   midiLedHold { 0 };
    static constexpr int kMidiLedHoldTicks = 4;

    // ── Cached preset list ────────────────────────────────────────────────────
    std::vector<Sf2PresetInfo> presetList;

    // ── SF2 program grid ──────────────────────────────────────────────────────
    Sf2ProgramGrid programGrid;
    bool           programPickerOpen { false };

    // ── SF2 per-channel FX state ──────────────────────────────────────────────
    struct AssignedPreset { juce::String name; int ch { 0 }; };
    std::vector<AssignedPreset> sf2Presets;
    int                         selectedSf2Ch { -1 };

    // ── MIDI channel-range spinners ───────────────────────────────────────────
    juce::Rectangle<int> chLowDec,  chLowLabel,  chLowInc;
    juce::Rectangle<int> chHighDec, chHighLabel, chHighInc;
    juce::Rectangle<int> chRangeLabelZone;
    int cachedChLow  { 1 };
    int cachedChHigh { 16 };

    void buildSf2Combo();
    void openProgramGrid();
    void closeProgramGrid();
    void restoreGridChannelAssignments();

    // ── Value mapping helpers ─────────────────────────────────────────────────
    float volToNorm    (float linear) const;
    float normToVol    (float n)      const;
    float transToNorm  (int semi)     const;
    int   normToTrans  (float n)      const;
    float panToNorm    (float p)      const;
    float normToPan    (float n)      const;
    float fineToNorm   (float cents)  const;
    float normToFine   (float n)      const;

    // ── Zone parser (SF2 only — for KeysPanel display) ────────────────────────
    static std::vector<KeysPanel::Keyzone> parseSf2Zones (const juce::File& f,
                                                           int targetBank   = 0,
                                                           int targetPreset = 0);

    void showMidiLearnMenu (int fieldId, juce::Point<int> screenPos);

    // ── Mouse events ──────────────────────────────────────────────────────────
    void mouseDown        (const juce::MouseEvent&) override;
    void mouseDrag        (const juce::MouseEvent&) override;
    void mouseUp          (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove   (const juce::MouseEvent&,
                           const juce::MouseWheelDetails&) override;

    DysektProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SfzDropdownPanel)
};
