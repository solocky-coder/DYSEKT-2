#pragma once
// =============================================================================
//  SfzPlayerDropdownPanel.h  —  SFZ-only instrument strip (SFZ-Player, ch3 default)
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
//    • Scrollable list: directories first, then .sfz files only
//    • Single-click selects; double-click enters directory or loads file
//    • Pressing Escape / clicking the 📁 icon again closes the browser
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "KeysPanel.h"
#include "AddZoneOverlay.h"
#include "SaveSfzOverlay.h"
#include "MessageOverlay.h"
#include "../audio/SfzPlayer.h"

class DysektProcessor;

// =============================================================================
#include "SfzFileBrowser.h"

// =============================================================================
//  SfzPlayerDropdownPanel
// =============================================================================
class SfzPlayerDropdownPanel : public juce::Component,
                         public juce::Timer,
                         public juce::FileDragAndDropTarget
{
public:
    explicit SfzPlayerDropdownPanel (DysektProcessor& p);
    ~SfzPlayerDropdownPanel() override;

    // ── Core overrides ────────────────────────────────────────────────────────
    void paint   (juce::Graphics&) override;
    void resized () override;
    void timerCallback() override;

    // ── FileDragAndDropTarget ─────────────────────────────────────────────────
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // ── Public API ────────────────────────────────────────────────────────────
    void panelDidShow();
    void initEmptySfz();

    /** Returns true if the inline file browser overlay is open. */
    bool isBrowserOpen()     const noexcept { return browserOpen; }

    /** Called after a new SF2/SFZ file has been accepted (any path). */
    std::function<void (const juce::File&)> onFileLoaded;

    /** Fired after a file loads with the file and whether it is SFZ (true) or SF2 (false).
        Only used in standalone builds to auto-create sequencer tracks. */
    std::function<void (const juce::File&, bool isSfz)> onSfzFileLoaded;

    /** Reload zone display for the given file — public so PluginEditor can call it directly. */
    void reloadZones (const juce::File& f);

    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr int kStripH  = 36;
    static constexpr int kAdsrH   = 34;   ///< height of the ADSR knob row

    // ── Keyboard sub-component ────────────────────────────────────────────────
    KeysPanel keysPanel;

    // SFZ-Player: no SF2 program grid

    // ── Zone parsers — public so PluginEditor can call them directly ──────────
    static std::vector<KeysPanel::Keyzone> parseSfzZones (const juce::File& f);
    static std::vector<KeysPanel::Keyzone> parseSf2Zones (const juce::File& f,
                                                            int targetBank   = 0,
                                                            int targetPreset = 0);

private:
    // ── Header-strip drawing ──────────────────────────────────────────────────
    void drawHeaderStrip (juce::Graphics& g) const;
    void drawAdsrStrip   (juce::Graphics& g) const;
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

    // ADSR knob zones (second row, below header strip)
    juce::Rectangle<int> adsrAtkZone, adsrDecZone, adsrSusZone, adsrRelZone;

    // Per-channel SF2 FX zones — overlap the ADSR slots when SF2 is loaded
    juce::Rectangle<int> chComboZone;   ///< combo fits where TRN+FINE slots are
    juce::Rectangle<int> chMixZone;     ///< reuse adsrAtkZone slot
    juce::Rectangle<int> chSizeZone;    ///< reuse adsrDecZone slot
    juce::Rectangle<int> chDampZone;    ///< reuse adsrSusZone slot
    juce::Rectangle<int> chGainZone;    ///< reuse adsrRelZone slot

    // Sub-zones inside nameZone
    juce::Rectangle<int> presetDecBtn, presetLabel, presetIncBtn, folderIconZone;

    // ── Drag state for knobs ──────────────────────────────────────────────────
    enum class ActiveKnob { None, Volume, Transpose, Pan, FineTune, ReverbMix, ReverbSize,
                            AdsrAttack, AdsrDecay, AdsrSustain, AdsrRelease };
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
    static constexpr int kMidiLedHoldTicks = 4;  // ~133 ms at 30 Hz

    // ── Cached preset list ────────────────────────────────────────────────────
    std::vector<Sf2PresetInfo> presetList;

    // ── Inline file browser ───────────────────────────────────────────────────
    SfzFileBrowser fileBrowser;
    bool           browserOpen      { false };

    // ── MIDI channel-range spinners (replaces sf2ChCombo) ────────────────
    // Drawn as:  CH [◂ 1 ▸] – [◂ 16 ▸]  inside the SF2 strip.
    // Hit-zones laid out in resized(); clicks handled in mouseDown().
    juce::Rectangle<int> chLowDec,  chLowLabel,  chLowInc;
    juce::Rectangle<int> chHighDec, chHighLabel, chHighInc;
    juce::Rectangle<int> chRangeLabelZone;
    int cachedChLow  { 1 };   ///< polled from processor each timer tick
    int cachedChHigh { 16 };


    // State held between openAddZoneChooser() and onFileChosen() in kAddZone mode
    juce::File     addZoneTargetSfz;
    int            addZonePrevHiKey { -1 };

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

    void writeSfzZoneChange (const juce::File& f, int rowIndex,
                              const KeysPanel::Keyzone& updated);

    // ── Add Zone / Save SFZ As ────────────────────────────────────────────────
    void openAddZoneChooser();
    void showAddZoneOverlay (const juce::File& sfzFile,
                              const juce::File& sampleFile,
                              int               prevHiKey);
    static bool appendZoneToSfz (const juce::File& sfzFile,
                                  const juce::File& sampleFile,
                                  int loKey, int hiKey, int rootKey);
    void openSaveAsOverlay();
    void openSaveAsNewForZone (const juce::File& sampleFile);

    void showMidiLearnMenu (int fieldId, juce::Point<int> screenPos);

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
            // setAlwaysOnTop ensures the overlay receives mouse events above all
            // sibling components in the host's HWND on Windows VST3.
            overlayPtr->setAlwaysOnTop (true);
            overlayPtr->toFront (true);
            overlayPtr->grabKeyboardFocus();
        }
    }

    void hideOverlays();

    std::unique_ptr<AddZoneOverlay>    addZoneOverlay;
    std::unique_ptr<SaveSfzOverlay>    saveSfzOverlay;
    std::unique_ptr<MessageOverlay>    messageOverlay;

    // ── Mouse events ──────────────────────────────────────────────────────────
    void mouseDown        (const juce::MouseEvent&) override;
    void mouseDrag        (const juce::MouseEvent&) override;
    void mouseUp          (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove   (const juce::MouseEvent&,
                           const juce::MouseWheelDetails&) override;

    DysektProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SfzPlayerDropdownPanel)
};
