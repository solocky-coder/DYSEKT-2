#pragma once
// =============================================================================
//  SfzDropdownPanel.h  —  SF2 / SFZ instrument strip with inline file browser
// =============================================================================
//  Header strip layout (left → right):
//    [< Preset Name  📁 >] [TRN] [FINE] [REV MIX] [REV SIZE] [PAN] [VOL] [METER]
//
//  The preset picker doubles as the file browser entry-point:
//    • When a file IS loaded   — scrolls through SF2 presets as before.
//                                Small 📁 icon on right edge opens browser.
//    • When NO file is loaded  — clicking anywhere on the picker opens the
//                                inline browser.
//    • Mouse-wheel on the picker scrolls presets (when loaded).
//
//  The inline browser is a full-panel overlay (below the header strip) with:
//    • Breadcrumb path bar + ↑ up-button
//    • Scrollable list: directories first, then .sfz / .sf2 files
//    • Single-click selects; double-click enters directory or loads file
//    • Pressing Escape / clicking the 📁 icon again closes the browser
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "KeysPanel.h"
#include "../audio/SfzPlayer.h"

class DysektProcessor;

// =============================================================================
#include "SfzFileBrowser.h"
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

    /** Returns true if the inline file browser overlay is open. */
    bool isBrowserOpen()     const noexcept { return browserOpen; }

    /** Called after a new SF2 file has been accepted. */
    std::function<void (const juce::File&)> onFileLoaded;

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
    void drawPresetPicker (juce::Graphics& g) const;

    // ── Layout zones (computed in resized) ────────────────────────────────────
    juce::Rectangle<int> nameZone,
                          volZone, transZone,
                          panZone, fineZone,
                          rvMixZone, rvSizeZone,
                          meterZone;

    // Per-channel SF2 FX zones
    juce::Rectangle<int> chComboZone;
    juce::Rectangle<int> chMixZone;
    juce::Rectangle<int> chSizeZone;
    juce::Rectangle<int> chDampZone;
    juce::Rectangle<int> chGainZone;

    // Sub-zones inside nameZone
    juce::Rectangle<int> presetDecBtn, presetLabel, presetIncBtn, folderIconZone;

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

    // ── Preset-preview reload debounce ────────────────────────────────────────
    // Requesting a full waveform re-render (renderWithFluidSynth probes all
    // 128 MIDI notes, then renders sustain+release for every active one) on
    // every single arrow-key/click during preset navigation would queue a
    // heavy job per keystroke on fileLoadPool's single worker thread, so the
    // preview would lag further behind with every rapid press. Instead,
    // requestPreviewReload() just records the latest requested bank/program
    // and arms a short countdown; timerCallback() (running at 30Hz already)
    // only fires the actual reload once the countdown reaches zero, i.e.
    // ~200ms after the user's *last* navigation, coalescing bursts into one
    // job. Nothing is lost — only the final, settled-on preset ever renders.
    static constexpr int kPreviewReloadDelayTicks = 6;   // ~200ms at 30Hz
    int  previewReloadCountdown { -1 };                  // -1 = idle
    int  pendingPreviewBank     { 0 };
    int  pendingPreviewProgram  { 0 };
    void requestPreviewReload (int bank, int program);

    // ── Inline file browser ───────────────────────────────────────────────────
    SfzFileBrowser fileBrowser;
    bool           browserOpen      { false };

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

    void openBrowser();
    void closeBrowser();
    void onFileChosen (const juce::File& f);

    // ── Value mapping helpers ─────────────────────────────────────────────────
    float volToNorm    (float linear) const;
    float normToVol    (float n)      const;
    float transToNorm  (int semi)     const;
    int   normToTrans  (float n)      const;
    float panToNorm    (float p)      const;
    float normToPan    (float n)      const;
    float fineToNorm   (float cents)  const;
    float normToFine   (float n)      const;

    // ── Preset navigation ─────────────────────────────────────────────────────
    void selectPreset (int delta);

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
