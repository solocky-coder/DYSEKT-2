#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class DysektProcessor;

/** Second LCD panel: renders the selected slice waveform with an interactive
 * ADSR envelope overlay. Each envelope node is a draggable handle; moving
 * it updates the corresponding parameter via apvts in real time.
 *
 * Node layout:
 * P0 (fixed) — silence at slice start
 * P1 Attack  — drag X (time) + Y (peak level)    colour: Toxic Lime
 * P2 Decay   — drag X (time) + Y (sustain level) colour: Radioactive Yellow
 * P3 Sustain — mid-plateau Y handle               colour: Ice Blue
 * P4 Release — drag X (release-start time)        colour: Molten Orange
 * P5 (fixed) — silence at slice end
 *
 * Dimensions match SliceLcdDisplay::kPreferredHeight.
 * Call repaintLcd() from the editor's timerCallback() at ~30 Hz.
 */
class SliceWaveformLcd : public juce::Component
{
public:
    explicit SliceWaveformLcd (DysektProcessor& p);

    void paint   (juce::Graphics& g) override;
    void resized () override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;

    /** Call periodically from the UI timer to refresh the display. */
    void repaintLcd();

    /** Suggested height in pixels (un-scaled) — matches SliceLcdDisplay. */
    static constexpr int kPreferredHeight = 136;

    /** Set the current waveform display style (0-7) and repaint.
     *  0=Hard 1=Soft 2=Outline 3=Rectified 4=Mirrored 5=Bars 6=RMS 7=Stepped
     *  Mirrors WaveformView::setWaveformMode so the WAVE icon toggle in
     *  DualLcdControlFrame drives this display too, not just the main
     *  waveform views. */
    void setWaveformMode (int m) { waveformMode = juce::jlimit (0, 7, m); repaint(); }
    int  getWaveformMode() const noexcept { return waveformMode; }

    // ── Fixed blue STN-LCD palette (public: read by free helper functions
    //    in SliceWaveformLcd.cpp, e.g. lcd2Bg()/lcd2Phosphor()/etc.) ──────────
    static const juce::Colour kBg;
    static const juce::Colour kBezel;
    static const juce::Colour kPhosphor;
    static const juce::Colour kDim;
    static const juce::Colour kBright;
    static const juce::Colour kLabel;

private:
    // ── Snapshot used for one paint pass ─────────────────────────────────────
    struct DisplayData
    {
        bool          hasSlice       { false };
        bool          hasSample      { false };
        int           sliceIndex     { -1 };
        int           numSlices      { 0 };
        int           startSample    { 0 };
        int           endSample      { 0 };
        int           totalFrames    { 0 };
        int           midiNote       { 60 };
        float         volume         { 0.0f };
        float         pan            { 0.0f };
        float         pitchSemitones { 0.0f };

        double        sampleRate     { 44100.0 };
        juce::String  sampleName;
        bool          isDefault      { false };
        juce::Array<float> peaks;
    };

    // ── ADSR envelope node ────────────────────────────────────────────────────
    enum class NodeRole { None, Attack, Decay, Sustain, Release };

    struct EnvNode
    {
        float      xn    { 0.0f };   // normalised 0-1 across component width
        float      yn    { 0.0f };   // normalised 0-1 (0=top/loud  1=bottom/silent)
        NodeRole   role  { NodeRole::None };
        juce::Colour colour;
        const char* label { nullptr };
    };

    void  buildDisplayData();
    void  buildEnvelopeNodes();        // read params → nodes[]
    void  commitNodes();               // nodes[]    → write params
    float getSliceDurMs() const;       // actual selected slice duration in ms
    float envAt (float xn)  const;     // interpolated Y at position xn

    // ── SF-PLAYER mode helpers ────────────────────────────────────────────────
    bool  isSfPlayerMode() const;      // true when midiRouteMode == SfPlayer
    bool  isSfzPlayer2Mode() const;    // true when midiRouteMode == SfzPlayer2 (SFZ-PLAYER tab)
    void  buildSfEnvelopeNodes();      // read sfzPlayer ADSR → sfEnv / envNodes
    void  commitSfNodes();             // sfEnv → sfzPlayer setters (no APVTS)
    void  drawSfPlayerPanel (juce::Graphics& g, const juce::Rectangle<float>& area);

    void drawBackground   (juce::Graphics& g);
    void drawWaveform     (juce::Graphics& g, const juce::Rectangle<float>& area);
    void drawEnvelope     (juce::Graphics& g, const juce::Rectangle<float>& area);
    void drawNodes        (juce::Graphics& g, const juce::Rectangle<float>& area);
    void drawNoData       (juce::Graphics& g);
    void drawSegmentLabel (juce::Graphics& g, float x0, float y0,
                           float x1, float y1, const char* text,
                           juce::Colour col, const juce::Rectangle<float>& area);
    void drawSegmentLabel (juce::Graphics& g, const char* text,
                           juce::Colour col, const juce::Rectangle<float>& area);
    void drawPlayhead     (juce::Graphics& g, const juce::Rectangle<float>& area);

    NodeRole hitTest (juce::Point<float> pos) const;

    DysektProcessor& processor;
    DisplayData      data;

    // ── Envelope state ────────────────────────────────────────────────────────
    // Five control points (normalised).
    // P0 = (0,1) fixed   P1=attack   P2=decay   SH=sustain   P3=release   P4=(1,1) fixed
    struct {
        float ax    { 0.07f };   // attack peak X
        float ay    { 0.10f };   // attack peak Y  (near 0 = loud peak)
        float dx    { 0.25f };   // decay end X
        float sy    { 0.30f };   // sustain Y level
        float rx    { 0.99f };   // release start X  ← init at end of slice
        float sxEnd { 0.99f };   // dynamic sustain plateau end X  ← tracks rx
    } env;

    // ── SF-PLAYER envelope state (sfzPlayer ADSR, no slice) ──────────────────
    struct {
        float ax    { 0.07f };   // attack peak X
        float ay    { 0.04f };   // attack peak Y (near 0 = loud)
        float dx    { 0.25f };   // decay end X
        float sy    { 0.30f };   // sustain Y level
        float rx    { 0.99f };   // release start X
        float sxEnd { 0.99f };   // sustain plateau end X (tracks rx)
    } sfEnv;

    // Cache of computed nodes (rebuilt each paint + mouse event)
    juce::Array<EnvNode> envNodes;    // P1..P3 + sustain handle

    NodeRole dragRole        { NodeRole::None };
    NodeRole hovRole         { NodeRole::None };
    float    dragStartX      { 0.0f };
    int      postCommitGuard { 0 };   // frames to skip rebuild after commitNodes()
    int      lastEnvSnapVer      { -1 };  // snapshot version when env was last built
    int      lastBuiltSliceIndex { -1 };  // selected slice when env was last built

    // Last-seen global APVTS ADSR values — used by repaintLcd() to detect knob
    // changes that do not increment the slice snapshot version.
    float lastApvtsAttack  { -1.0f };
    float lastApvtsDecay   { -1.0f };
    float lastApvtsSustain { -1.0f };
    float lastApvtsRelease { -1.0f };

    // Content area cached in resized() / used for hit testing
    juce::Rectangle<float> screenArea;

    // Waveform display style — set via setWaveformMode(), driven by the WAVE
    // icon toggle in DualLcdControlFrame (see DysektEditor::toggleSoftWave()).
    int waveformMode = 0;   // 0=Hard 1=Soft 2=Outline 3=Rectified 4=Mirrored 5=Bars 6=RMS 7=Stepped

    static constexpr int   kScanlineAlpha = 18;
    static constexpr int   kLeftPad       = 8;
    static constexpr float kNodeR         = 14.0f;
    static constexpr float kHitR          = 26.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SliceWaveformLcd)
};
