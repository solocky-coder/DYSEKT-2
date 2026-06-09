#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class DysektProcessor;

class SliceLcdDisplay : public juce::Component
{
public:
 explicit SliceLcdDisplay (DysektProcessor& p);

 // Height the component requests — used by PluginEditor for layout
 // 11 rows × 28px + bezel padding
 static constexpr int kPreferredHeight = 320;

 void paint (juce::Graphics& g) override;
 void mouseDown (const juce::MouseEvent& e) override;

 void repaintLcd();
 void resized() override;

private:
 // ── Layout constants ──────────────────────────────────────────────────────
 static constexpr int kTotalRows = 11; // rows 0-9 data + row 10 flags
 static constexpr int kRowH = 28;

 // Returns the row height that exactly fits all kTotalRows data rows above the flags strip.
 int effectiveRowH() const noexcept;
 static constexpr int kLeftPad = 6;
 static constexpr int kLabelW = 46;
 static constexpr int kScanlineAlpha = 28;

 // ── Data snapshot ─────────────────────────────────────────────────────────
 struct DisplayData
 {
 bool hasSample = false;
 bool hasSlice = false;
 int sliceIndex = 0;
 int numSlices = 0;
 int rootNote = 36;
 juce::String sampleName;
 int sampleNumFrames = 0;
 double sampleRate = 44100.0;

 int midiNote = 36;
 int startSample = 0;
 int endSample = 0;
 float volume = 0.0f;
 float pan = 0.0f;
 float pitchSemitones = 0.0f;
 float centsDetune = 0.0f;
 float attackSec = 0.005f;
 float holdSec = 0.0f;
 float decaySec = 0.1f;
 float sustainLevel = 1.0f;
 float releaseSec = 0.02f;
 bool reverse = false;
 int loopMode = 0;
 bool oneShot = false;
 juce::Colour sliceColour;
 int muteGroup = 0;   // kept for serialization compat — not displayed
 bool globalMono = false;
 float filterCutoff = 20000.0f;
 float filterRes = 0.0f;
 juce::String sliceName;
 bool sliceLocked = false;
 // Extended — scroll rows 7-9
 bool stretchEnabled = false;
 float tonalityHz = 0.0f;
 float formantSemitones = 0.0f;
 bool releaseTail = false;
 int outputBus = 0;
 float bpm = 120.0f;
 };

 DisplayData data;

 // ── Draw helpers ──────────────────────────────────────────────────────────
 void buildDisplayData();
 void drawLcdBackground (juce::Graphics& g);
 void drawRow (juce::Graphics& g, int row,
 const juce::String& label,
 const juce::String& value,
 bool highlight = false);
 void drawRowPair (juce::Graphics& g, int row,
 const juce::String& leftStr,
 const juce::String& rightStr,
 bool highlight = false);
 void drawFlagsRow (juce::Graphics& g, int row);
 void drawNoSliceScreen (juce::Graphics& g);
 void drawNoSampleScreen (juce::Graphics& g);

 // ── String formatters ─────────────────────────────────────────────────────
 static juce::String midiNoteName (int note);
 static juce::String formatMs (float secs);
 static juce::String formatPan (float pan);

 DysektProcessor& processor;

 // ── Flag hit rects (updated each paint, used by mouseDown) ───────────────
 struct FlagHitRect
 {
 juce::Rectangle<int> bounds;
 int fieldId;
 bool isCycle;
 };
 std::vector<FlagHitRect> flagHitRects;

 // ── NAME row hit rect (updated each paint, used by mouseDown) ────────────
 juce::Rectangle<int> nameRowHitRect;

 // ── Inline text editor for NAME editing ──────────────────────────────────
 std::unique_ptr<juce::TextEditor> nameTextEditor;

 JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceLcdDisplay)
};
