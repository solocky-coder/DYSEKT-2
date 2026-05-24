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
#include "AddZoneOverlay.h"
#include "SaveSfzOverlay.h"
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
    void initEmptySfz();

    /** Called after a new SF2/SFZ file has been accepted (any path). */
    std::function<void (const juce::File&)> onFileLoaded;

    /** Fired after a file loads with the file and whether it is SFZ (true) or SF2 (false).
        Only used in standalone builds to auto-create sequencer tracks. */
    std::function<void (const juce::File&, bool isSfz)> onSfzFileLoaded;

    /** Fired when the user right-clicks a preset cell and assigns a MIDI channel.
        Only used in standalone builds to create/update piano-roll tracks. */
    std::function<void (const Sf2PresetInfo&, int midiChannel1Based)> onPresetChannelAssigned;

    /** Reload zone display for the given file — public so PluginEditor can call it directly. */
    void reloadZones (const juce::File& f);

    // ── Layout constants ──────────────────────────────────────────────────────
    static constexpr int kStripH  = 36;
    static constexpr int kAdsrH   = 34;   ///< height of the ADSR knob row

    // ── Keyboard sub-component ────────────────────────────────────────────────
    KeysPanel keysPanel;

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

    // ── Cached preset list ────────────────────────────────────────────────────
    std::vector<Sf2PresetInfo> presetList;

    // ── Inline file browser ───────────────────────────────────────────────────
    SfzFileBrowser fileBrowser;
    bool           browserOpen      { false };

    // ── SF2 program grid ──────────────────────────────────────────────────────
    Sf2ProgramGrid programGrid;
    bool           programPickerOpen { false };


    void openProgramGrid();
    void closeProgramGrid();

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

    // ── Zone parsers ──────────────────────────────────────────────────────────
    static std::vector<KeysPanel::Keyzone> parseSfzZones (const juce::File& f);
    static std::vector<KeysPanel::Keyzone> parseSf2Zones (const juce::File& f,
                                                            int targetBank   = 0,
                                                            int targetPreset = 0);
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
