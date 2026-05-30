#pragma once
// =============================================================================
//  Sf2MixerPanel.h  —  Per-channel mixer strip for active SF2 presets
// =============================================================================
//  Shows one vertical mixer strip per assigned SF2 preset (channels that have a
//  preset loaded via setPresetOnChannel).  Each strip has:
//
//    • Preset name + MIDI channel badge
//    • Volume fader  (CC7)
//    • Pan knob      (CC10)
//    • Reverb send   (CC91)
//    • Mute button   (CC7 → 0, restores on unmute)
//    • Solo button   (mutes all other channels via CC7)
//
//  State lives in SfzPlayer as ChannelStripAtomics[16], written from the UI
//  thread, applied on the audio thread by applyDirtyStrips() at the top of
//  each process() block.
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include <unordered_map>
#include "../audio/SfzPlayer.h"

class DysektProcessor;
struct Sf2PresetInfo;

// =============================================================================
class Sf2MixerPanel : public juce::Component
{
public:
    explicit Sf2MixerPanel (DysektProcessor& p);
    ~Sf2MixerPanel() override = default;

    // ── Public API ─────────────────────────────────────────────────────────────
    /** Refresh which channels are active.  Pass the current preset list and the
     *  channel-assignment map (presetIndex → 1-based MIDI channel) from Sf2ProgramGrid. */
    void setActiveChannels (const std::vector<Sf2PresetInfo>& presets,
                            const std::unordered_map<int, int>&          presetChannels);

    // ── Overrides ─────────────────────────────────────────────────────────────
    void paint   (juce::Graphics&) override;
    void resized () override;
    void mouseDown  (const juce::MouseEvent&) override;
    void mouseDrag  (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    DysektProcessor& processor;

    // ── Strip descriptor ──────────────────────────────────────────────────────
    struct ActiveStrip
    {
        int          channel   { -1 };   ///< FluidSynth channel index (0-based)
        juce::String name;               ///< preset name
        int          midiCh    { 0 };    ///< 1-based MIDI channel for badge

        // Layout zones (computed in resized)
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> volFaderTrack;
        juce::Rectangle<int> volFaderThumb;
        juce::Rectangle<int> panKnob;
        juce::Rectangle<int> revKnob;
        juce::Rectangle<int> muteBtn;
        juce::Rectangle<int> soloBtn;
        juce::Rectangle<int> nameLbl;
        juce::Rectangle<int> chBadge;
    };

    std::vector<ActiveStrip> strips;

    // ── Drag state ────────────────────────────────────────────────────────────
    enum class DragTarget { None, Volume, Pan, Reverb };
    DragTarget dragTarget { DragTarget::None };
    int        dragChannel { -1 };
    int        dragStartY  { 0 };
    float      dragStartVal{ 0.f };

    // ── Solo tracking ─────────────────────────────────────────────────────────
    int  soloedChannel { -1 };   ///< -1 = no solo active

    // ── Helpers ───────────────────────────────────────────────────────────────
    void layoutStrips();
    void drawStrip (juce::Graphics& g, const ActiveStrip& s,
                    const SfzPlayer::ChannelStrip& state) const;

    void drawKnob   (juce::Graphics& g, juce::Rectangle<int> bounds,
                     float normalised, const juce::String& label) const;
    void drawFader  (juce::Graphics& g, const ActiveStrip& s,
                     const SfzPlayer::ChannelStrip& state) const;

    static constexpr int kStripMinW = 64;
    static constexpr int kStripMaxW = 96;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sf2MixerPanel)
};
