#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class DysektProcessor;

/**  Right LCD panel shown when uiMode == 1 (SF-Player).
 *
 *   Renders the SF-player ADSR envelope as a draggable node graph.
 *   Nodes map directly to sfzPlayer.setSfzAttack/Decay/Sustain/Release.
 *   No APVTS writes — sfzPlayer ADSR params are atomic, not registered
 *   parameters.
 *
 *   Node colours and layout match SliceWaveformLcd so the two LCDs look
 *   visually consistent when switching modes.
 *
 *   Dimensions match SliceWaveformLcd::kPreferredHeight.
 *   Call repaintLcd() from the editor's timerCallback() at ~30 Hz.
 */
class SfzWaveformLcd : public juce::Component
{
public:
    explicit SfzWaveformLcd (DysektProcessor& p);

    void paint     (juce::Graphics& g) override;
    void resized   () override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp        (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

    void repaintLcd();

    static constexpr int kPreferredHeight = 136;

private:
    // ── Node roles ────────────────────────────────────────────────────────────
    enum class NodeRole { None, Attack, Decay, Sustain, Release };

    struct EnvNode
    {
        float      xn    { 0.0f };
        float      yn    { 0.0f };
        NodeRole   role  { NodeRole::None };
        juce::Colour colour;
        const char* label { nullptr };
    };

    // ── Normalised envelope state ─────────────────────────────────────────────
    struct
    {
        float ax    { 0.07f };   // attack peak X
        float ay    { 0.04f };   // attack peak Y (near 0 = loud)
        float dx    { 0.25f };   // decay end X
        float sy    { 0.30f };   // sustain Y level
        float rx    { 0.99f };   // release start X
        float sxEnd { 0.99f };   // sustain plateau end X (tracks rx)
    } env;

    juce::Array<EnvNode> envNodes;

    void buildEnvelopeNodes();   // read sfzPlayer ADSR → env / envNodes
    void commitNodes();          // env → sfzPlayer setters

    // ── Draw helpers ──────────────────────────────────────────────────────────
    void drawBackground    (juce::Graphics& g);
    void drawEnvelope      (juce::Graphics& g, const juce::Rectangle<float>& area);
    void drawNodes         (juce::Graphics& g, const juce::Rectangle<float>& area);
    void drawHeader        (juce::Graphics& g, const juce::Rectangle<float>& area);
    void drawNoInstrument  (juce::Graphics& g);
    void drawPlayhead      (juce::Graphics& g, const juce::Rectangle<float>& area);
    void drawSegmentLabel  (juce::Graphics& g,
                            float x0, float y0, float x1, float y1,
                            const char* text, juce::Colour col,
                            const juce::Rectangle<float>& area);

    NodeRole hitTest (juce::Point<float> pos) const;

    DysektProcessor& processor;

    // ── Waveform backdrop (zoom/scroll-aware) ────────────────────────────────
    static constexpr int kPeaks = 512;
    juce::Array<float> peaks;
    int   cachedTotalFrames { 0 };
    float cachedZoom   { 1.0f };
    float cachedScroll { 0.0f };
    void buildWaveformPeaks();
    void drawWaveformBackdrop (juce::Graphics& g, const juce::Rectangle<float>& area);
    void drawLoopOverlay      (juce::Graphics& g, const juce::Rectangle<float>& area);

    NodeRole dragRole { NodeRole::None };
    NodeRole hovRole  { NodeRole::None };
    int      postCommitGuard { 0 };

    juce::Rectangle<float> screenArea;

    static constexpr int   kScanlineAlpha = 18;
    static constexpr float kNodeR         = 14.0f;
    static constexpr float kHitR          = 26.0f;

    // Fixed view-window for SF-player (no slice duration)
    static constexpr float kViewMs = 2000.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SfzWaveformLcd)
};
