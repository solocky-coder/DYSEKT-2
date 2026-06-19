#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class DysektProcessor;

/**  Left LCD panel shown when uiMode == 1 (SF-Player).
 *
 *   Rows:
 *     0  Header  —  "SF PLAYER"  |  filename (truncated)
 *     1  Path    —  full directory path (scrolling truncated)
 *     2  pair    —  VOL: +x.xdB       TRNS: +xxst
 *     3  pair    —  A: xxxms           D: xxxms
 *     4  pair    —  S: xx%             R: xxxms
 *     5  pair    —  RV SZ: xx%         RV DMP: xx%
 *     6  pair    —  RV WID: xx%        RV MIX: xx%
 *     7  status  —  "LOADED" / "-- NO INSTRUMENT --"
 *
 *  Dimensions and aesthetic match SliceLcdDisplay exactly.
 */
class SfzLcdDisplay : public juce::Component
{
public:
    explicit SfzLcdDisplay (DysektProcessor& p);

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
        juce::String  fileName;       // leaf name without extension
        juce::String  filePath;       // parent directory
        float         volume        = 0.0f;   // dB
        int           transpose     = 0;      // semitones
        float         attackSec     = 0.0f;
        float         decaySec      = 0.0f;
        float         sustainPct    = 100.0f;
        float         releaseSec    = 0.0f;
        float         reverbSize    = 0.0f;   // 0-100
        float         reverbDamp    = 0.0f;
        float         reverbWidth   = 0.0f;
        float         reverbMix     = 0.0f;

        // Selected previewZones2 zone (-1 = none). Populated from
        // processor.selectedPreviewZone2 / previewZones2 each paint().
        int           selectedZoneIdx   = -1;
        int           selectedZoneNote  = 0;
        int           selectedZoneStart = 0;
        int           selectedZoneEnd   = 0;

        // Pre-computed — populated by buildDisplayData() so paint() never allocates.
        juce::String  fileNameUpperShort; // fileName.toUpperCase().substring(0,18)
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
    static juce::String midiNoteName (int note);

    DysektProcessor& processor;
    DisplayData      data;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SfzLcdDisplay)
};
