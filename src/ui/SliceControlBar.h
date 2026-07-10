#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../MidiLearnManager.h"

class DysektProcessor;

class SliceControlBar : public juce::Component,
                        private juce::Timer
{
public:
    explicit SliceControlBar (DysektProcessor& p);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

    // Called by the parent editor's timer — starts/stops the pulse as needed
    void updateMidiLearnPulse();

    // PAD/WAVE view toggle — set externally by the editor and reflected in the button.
    void setPadViewActive (bool on) { padViewActive = on; repaint(); }
    bool getPadViewActive() const noexcept { return padViewActive; }

    // SFZ-PLAYER zone-matrix toggle. Kept separate from PAD/WAVE because the
    // SFZ player uses the waveform and zone builder as its two primary views.
    void setZoneBuilderActive (bool on) { zoneBuilderActive = on; repaint(); }
    bool getZoneBuilderActive() const noexcept { return zoneBuilderActive; }

    /// Fired when the user clicks the PAD/WAVE toggle button.
    std::function<void (bool padActive)> onPadViewToggle;
    std::function<void (bool zonesActive)> onZonesToggle;

private:
    void timerCallback() override;
    float pulsePhase    = 0.0f;   // 0..1, advances each timer tick
    bool  wasArmed      = false;  // tracks arm state across updateMidiLearnPulse calls
    int   lastLiveDrag  = -1;      // last liveDragBoundsStart value seen, for repaint gating
    bool  padViewActive = false;   // mirrors editor showPadGrid
    bool  zoneBuilderActive = false; // mirrors editor showZoneBuilder
    juce::Rectangle<int> padToggleBtnArea;  // hit-tested in mouseDown — PADS button
    juce::Rectangle<int> waveToggleBtnArea; // hit-tested in mouseDown — WAVE button
    juce::Rectangle<int> zonesToggleBtnArea; // SFZ-PLAYER ZONES button

    // True when the SFZ-PLAYER tab (sliceManager2/voicePool2 — a full second
    // Slicer instance) is the active engine. Mirrors SliceLcdDisplay's
    // identically-named helper. When true, every snapshot/slice/sample-data
    // read below must source from the "2" (engine 2) side, and every pushed
    // Command must set targetEngine2 = true so the audio thread mutates
    // sliceManager2/voicePool2 instead of sliceManager/voicePool.
    bool isSfzPlayer2Mode() const noexcept;

private:
    struct ParamCell
    {
        int x, y, w, h;
        uint32_t lockBit;
        int fieldId; // SliceParamField enum value
        float minVal, maxVal, step;
        bool isBoolean; // for ping-pong toggle
        bool isChoice; // for algorithm popup
        bool isReadOnly = false;
        bool isSetBpm = false;
        bool isMidiLearnBtn = false; // START / END boundary buttons
        bool isKnob = false; // numeric rotary
        bool isMidiLearnable = false; // right-click → Learn menu
        bool isLockIcon = false; // clicking this cell toggles the lock
        float knobNorm = 0.0f; // 0-1 position for knob arc
    };

    std::vector<ParamCell> cells;

    void drawParamCell (juce::Graphics& g, int x, int y, const juce::String& label,
                        const juce::String& value, bool locked, uint32_t lockBit,
                        int fieldId, float minVal, float maxVal, float step,
                        bool isBoolean, bool isChoice, int& outWidth);

    // Rotary knob cell — used for all numeric parameters
    void drawKnobCell (juce::Graphics& g, int x, int y,
                       const juce::String& label, const juce::String& valueText,
                       float normVal, bool locked, uint32_t lockBit,
                       int fieldId, float minVal, float maxVal, float step,
                       int& outWidth);

    // Flat LCD-style slider — used for MARKER (matches TrimDialog IN/OUT style)
    void drawMarkerSliderCell (juce::Graphics& g, int x, int y,
                               int sampleVal, int totalFrames, int& outWidth);

    // Chromatic channel badge — cycles 0 (off) through 1-16 on click
    void drawChroBadgeCell (juce::Graphics& g, int x, int y,
                            int channel, bool locked, int& outWidth);

    // Chromatic legato toggle
    void drawLegatoToggleCell (juce::Graphics& g, int x, int y,
                               bool on, bool locked, int& outWidth);

    // Horizontal bipolar slider — used for PAN
    void drawPanSliderCell (juce::Graphics& g, int x, int y,
                            float panValue, bool locked, int& outWidth);

    // START / END slice boundary MIDI Learn buttons
    void drawMidiLearnCell (juce::Graphics& g, int x, int y,
                            const juce::String& label, int fieldId, int& outWidth);

    void drawKnob (juce::Graphics& g, int cx, int cy, int r,
                   float normVal, bool locked, bool armed, bool mapped,
                   juce::Colour tintOverride = {}, bool hovered = false);

    void drawLockIcon (juce::Graphics& g, int x, int y, bool locked);
    void showTextEditor (const ParamCell& cell, float currentValue);
    void showMidiLearnMenu (int fieldId, juce::Point<int> screenPos);

    // Per-field helpers
    float getCurrentValue (int fieldId) const;
    float toNorm (int fieldId, float nativeVal) const;
    float fromNorm (int fieldId, float norm) const;

    static constexpr int kKnobR = 9; // knob radius (px)
    float paintSf = 1.0f;
    int psCellW  { 74 };   // kParamCellWidth * paintSf
    int psCellH  { 32 };   // 32 * paintSf
    int psKnobR  { kKnobR };// kKnobR * paintSf       // set at start of paint() — scales to component height

    DysektProcessor& processor;

    // Drag state
    int hoveredCellIdx = -1;   // index into cells[] under cursor, -1 = none
    int activeDragCell = -1;
    float dragStartValue = 0.0f;
    int dragStartY = 0;

    // Snapshot of the cell matched in mouseDown — copied out of cells[] so that
    // paint()'s cells.clear() cannot invalidate the active drag mid-gesture.
    ParamCell activeCellSnapshot {};

    // Fine-mode toggle badge — hit area updated each paint, checked in mouseDown.
    juce::Rectangle<int> markerFineModeToggleArea;

    // Text editor overlay
    std::unique_ptr<juce::TextEditor> textEditor;
};
