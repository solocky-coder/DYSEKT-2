#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class DysektProcessor;

/**  Right LCD panel shown when uiMode == 2 (SF2-Player / FluidSynth).
 *
 *   Renders the SF2-player ADSR envelope as a draggable node graph.
 *   Nodes map directly to sfzPlayer.setSfzAttack/Decay/Sustain/Release.
 *   No APVTS writes — sfzPlayer ADSR params are atomic, not registered
 *   parameters.
 *
 *   Uses processor.sampleData3 for the waveform backdrop — its own
 *   independent render via SoundFontLoadTarget::SfPlayer, decoupled from
 *   the Slicer's sampleData and from the SFZ-PLAYER tab's sampleData2
 *   (the SFZ-PLAYER tab is now a full second Slicer instance — see
 *   sliceManager2/voicePool2 — and uses SliceLcdDisplay/SliceWaveformLcd
 *   in mode-aware fashion instead of a dedicated class like this one).
 *
 *   Call repaintLcd() from the editor's timerCallback() at ~30 Hz.
 */
class Sf2WaveformLcd : public juce::Component
{
public:
    explicit Sf2WaveformLcd (DysektProcessor& p);

    void paint     (juce::Graphics& g) override;
    void resized   () override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp        (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;

    void repaintLcd();

    void setWaveformMode (int mode) { waveformMode = juce::jlimit (0, 7, mode); repaint(); }
    int  getWaveformMode () const   { return waveformMode; }

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
        float ax    { 0.07f };
        float ay    { 0.04f };
        float dx    { 0.25f };
        float sy    { 0.30f };
        float rx    { 0.99f };
        float sxEnd { 0.99f };
    } env;

    juce::Array<EnvNode> envNodes;

    void buildEnvelopeNodes();
    void commitNodes();

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

    // ── Waveform backdrop ────────────────────────────────────────────────────
    static constexpr int kPeaks = 512;
    juce::Array<float> peaks;
    int   cachedTotalFrames { 0 };
    float cachedZoom   { 1.0f };
    float cachedScroll { 0.0f };
    void buildWaveformPeaks();
    void drawWaveformBackdrop (juce::Graphics& g, const juce::Rectangle<float>& area);
    void drawLoopOverlay      (juce::Graphics& g, const juce::Rectangle<float>& area);

    int waveformMode { 0 };

    NodeRole dragRole { NodeRole::None };
    NodeRole hovRole  { NodeRole::None };
    int      postCommitGuard { 0 };

    juce::Rectangle<float> screenArea;

    static constexpr int   kScanlineAlpha = 18;
    static constexpr float kNodeR         = 14.0f;
    static constexpr float kHitR          = 26.0f;
    static constexpr float kViewMs        = 2000.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sf2WaveformLcd)
};
