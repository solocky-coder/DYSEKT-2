#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class DysektProcessor;

/**  Left LCD panel shown when uiMode == 2 (SF2-Player / FluidSynth).
 *
 *   Rows:
 *     0  Header  —  "SF2 PLAYER"  |  filename (truncated)
 *     1  Path    —  full directory path (scrolling truncated)
 *     2  Preset  —  BANK: xxx  PRESET: xxx NAME: xxxxxxxxxx
 *     3  pair    —  VOL: +x.xdB       TRNS: +xxst
 *     4  pair    —  A: xxxms           D: xxxms
 *     5  pair    —  S: xx%             R: xxxms
 *     6  pair    —  RV SZ: xx%         RV DMP: xx%
 *     7  status  —  "LOADED" / "-- NO INSTRUMENT --"
 *
 *  Dimensions and aesthetic match SliceLcdDisplay's LCD styling.
 *  Call repaintLcd() from the editor's timerCallback() at ~30 Hz.
 */
class Sf2LcdDisplay : public juce::Component
{
public:
    explicit Sf2LcdDisplay (DysektProcessor& p);

    static constexpr int kPreferredHeight = 320;

    void paint      (juce::Graphics& g) override;
    void resized    () override;
    void repaintLcd ();

private:
    // ── Layout ────────────────────────────────────────────────────────────────
    static constexpr int kTotalRows     = 8;
    static constexpr int kLeftPad       = 6;
    static constexpr int kScanlineAlpha = 28;

    int effectiveRowH() const noexcept;

    // ── Data snapshot ─────────────────────────────────────────────────────────
    struct DisplayData
    {
        bool          loaded       = false;
        juce::String  fileName;
        juce::String  filePath;
        int           bankNumber   = 0;
        int           presetNumber = 0;
        juce::String  presetName;
        float         volume       = 0.0f;   // dB
        int           transpose    = 0;      // semitones
        float         attackSec    = 0.0f;
        float         decaySec     = 0.0f;
        float         sustainPct   = 100.0f;
        float         releaseSec   = 0.0f;
        float         reverbSize   = 0.0f;   // 0-100
        float         reverbDamp   = 0.0f;

        // Pre-computed — populated by buildDisplayData() so paint() never allocates.
        juce::String  fileNameUpperShort;
    };

    void buildDisplayData();

    // ── Draw helpers ──────────────────────────────────────────────────────────
    void drawLcdBackground  (juce::Graphics& g);
    void drawRow            (juce::Graphics& g, int row,
                             const juce::String& label,
                             const juce::String& value,
                             bool highlight = false);
    void drawRowPair        (juce::Graphics& g, int row,
                             const juce::String& leftStr,
                             const juce::String& rightStr,
                             bool highlight = false);
    void drawNoInstrument   (juce::Graphics& g);

    // ── Formatters ────────────────────────────────────────────────────────────
    static juce::String formatMs  (float secs);
    static juce::String formatPct (float v);

    DysektProcessor& processor;
    DisplayData      data;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sf2LcdDisplay)
};
