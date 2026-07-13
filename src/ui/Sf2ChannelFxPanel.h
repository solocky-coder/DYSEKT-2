#pragma once
// =============================================================================
//  Sf2ChannelFxPanel.h  —  Per-channel (per-preset) SF2 reverb + gain strip
// =============================================================================
//  Displays one column per active FluidSynth channel.  Each column shows:
//    • Preset name (from the sequencer track list or sfzPlayer preset list)
//    • Reverb-mix knob  (0–100 %)
//    • Reverb-size knob (0–100 %)
//    • Reverb-damp knob (0–100 %)
//    • Gain knob        (0–200 %, 100 = unity)
//
//  Thread safety:
//    All knob mutations call sfzPlayer.setChannel*() directly (atomic stores,
//    UI-thread safe) and also push a CmdSetSliceParam so MIDI-learn can track
//    the field.  The panel polls on its Timer (30 Hz) to refresh display.
//
//  Integration:
//    1. Instantiate in PluginEditor, add as child, lay out below SfzModulePanel.
//    2. Call setActiveChannelMask() whenever the set of active SF2 tracks changes.
//    3. The panel auto-hides when no SF2 file is loaded (isVisible() = false).
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "ThemeData.h"
#include "DysektLookAndFeel.h"
#include "UIHelpers.h"

class Sf2ChannelFxPanel  : public juce::Component,
                            public juce::Timer
{
public:
    explicit Sf2ChannelFxPanel (DysektProcessor& p)
        : processor (p)
    {
        startTimerHz (30);
    }

    ~Sf2ChannelFxPanel() override { stopTimer(); }

    // ── Called from editor / sequencer callbacks ──────────────────────────────

    /** Set which FluidSynth channels (0-15) to display.  Pass the bitmask
     *  that matches the active SF2 tracks (same mask as liveInputChannelMask). */
    void setActiveChannelMask (uint16_t mask)
    {
        if (mask == activeMask) return;
        activeMask = mask;
        repaint();
    }

    /** Update the label shown above channel `ch` (0-15).  Typically the SF2
     *  preset name taken from the sequencer track.  Call from the message thread
     *  whenever a new preset is assigned to a channel. */
    void setChannelLabel (int ch, const juce::String& label)
    {
        if (ch < 0 || ch >= 16) return;
        channelLabels[ch] = label;
        repaint();
    }

    // ── Component ─────────────────────────────────────────────────────────────

    void paint (juce::Graphics& g) override
    {
        const auto theme = ThemeData::darkTheme();
        UIHelpers::drawTexturedPanel (g, getLocalBounds().toFloat(), theme.darkBar,
                                       UIHelpers::PanelZone::Chassis);

        const int numActive = countActiveBits();
        if (numActive == 0)
        {
            g.setColour (theme.foreground.withAlpha (0.4f));
            g.setFont (DysektLookAndFeel::makeFont(13.f));
            g.drawText ("No SF2 channels active", getLocalBounds(), juce::Justification::centred);
            return;
        }

        const float colW = (float) getWidth() / (float) numActive;
        int col = 0;

        for (int ch = 0; ch < 16; ++ch)
        {
            if (! (activeMask & (1u << ch))) continue;

            const float x = (float) col * colW;
            const juce::Rectangle<float> colRect (x, 0.f, colW, (float) getHeight());
            paintChannel (g, ch, colRect, theme);
            ++col;
        }
    }

    void resized() override {}

    void timerCallback() override
    {
        // Show/hide based on whether SF2 is loaded
        const bool shouldShow = processor.sfzPlayer.isLoaded();
        if (shouldShow != isVisible()) setVisible (shouldShow);
        if (! shouldShow) return;
        repaint();
    }

    // ── Mouse interaction ─────────────────────────────────────────────────────

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragState = findKnobAt (e.getPosition());
        if (dragState.ch < 0) return;
        dragStartY   = e.getScreenY();
        dragStartVal = getCurrentVal (dragState.ch, dragState.knob);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragState.ch < 0) return;
        const float delta = (float)(dragStartY - e.getScreenY()) / 120.f;  // px → 0-1
        const float newNorm = juce::jlimit (0.0f, 1.0f, dragStartVal + delta);
        applyNorm (dragState.ch, dragState.knob, newNorm);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragState = {};
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto hit = findKnobAt (e.getPosition());
        if (hit.ch < 0) return;
        resetToDefault (hit.ch, hit.knob);
        repaint();
    }

private:
    // ── Knob IDs ──────────────────────────────────────────────────────────────
    enum class Knob { None, ReverbMix, ReverbSize, ReverbDamp, Gain };

    static constexpr float kKnobH    = 44.f;   // px tall per knob row
    static constexpr float kLabelH   = 18.f;   // preset name label
    static constexpr float kPadding  =  4.f;

    struct DragState { int ch { -1 }; Knob knob { Knob::None }; };

    // ── Helpers ───────────────────────────────────────────────────────────────

    int countActiveBits() const noexcept
    {
        int n = 0;
        for (int i = 0; i < 16; ++i) if (activeMask & (1u << i)) ++n;
        return n;
    }

    float getCurrentVal (int ch, Knob k) const noexcept
    {
        switch (k)
        {
            case Knob::ReverbMix:  return processor.sfzPlayer.getChannelReverbMix  (ch) / 100.f;
            case Knob::ReverbSize: return processor.sfzPlayer.getChannelReverbSize (ch) / 100.f;
            case Knob::ReverbDamp: return processor.sfzPlayer.getChannelReverbDamp (ch) / 100.f;
            case Knob::Gain:       return processor.sfzPlayer.getChannelGain       (ch) / 200.f;
            default:               return 0.f;
        }
    }

    void applyNorm (int ch, Knob k, float norm)
    {
        // Apply directly via atomic (UI-thread safe)
        switch (k)
        {
            case Knob::ReverbMix:  processor.sfzPlayer.setChannelReverbMix  (ch, norm * 100.f); break;
            case Knob::ReverbSize: processor.sfzPlayer.setChannelReverbSize (ch, norm * 100.f); break;
            case Knob::ReverbDamp: processor.sfzPlayer.setChannelReverbDamp (ch, norm * 100.f); break;
            case Knob::Gain:       processor.sfzPlayer.setChannelGain       (ch, norm * 200.f); break;
            default: break;
        }

        // Also push a CmdSetSliceParam so MIDI-learn and the undo system see it.
        // intParam1 = field, intParam2 = channel index, floatParam1 = raw value.
        DysektProcessor::Command cmd;
        cmd.type       = DysektProcessor::CmdSetSliceParam;
        cmd.intParam2  = ch;
        cmd.floatParam1 = norm;

        switch (k)
        {
            case Knob::ReverbMix:  cmd.intParam1 = DysektProcessor::FieldSfzChReverbMix;  cmd.floatParam1 = norm * 100.f; break;
            case Knob::ReverbSize: cmd.intParam1 = DysektProcessor::FieldSfzChReverbSize; cmd.floatParam1 = norm * 100.f; break;
            case Knob::ReverbDamp: cmd.intParam1 = DysektProcessor::FieldSfzChReverbDamp; cmd.floatParam1 = norm * 100.f; break;
            case Knob::Gain:       cmd.intParam1 = DysektProcessor::FieldSfzChGain;       cmd.floatParam1 = norm * 200.f; break;
            default: return;
        }
        processor.pushCommand (cmd);
    }

    void resetToDefault (int ch, Knob k)
    {
        switch (k)
        {
            case Knob::ReverbMix:  applyNorm (ch, k, 0.f);   break;   // default: dry
            case Knob::ReverbSize: applyNorm (ch, k, 0.5f);  break;   // 50 %
            case Knob::ReverbDamp: applyNorm (ch, k, 0.5f);  break;   // 50 %
            case Knob::Gain:       applyNorm (ch, k, 0.5f);  break;   // 100 % (unity)
            default: break;
        }
    }

    // ── Layout & hit testing ─────────────────────────────────────────────────

    /** Returns the column rect for channel `ch` (0-15) given activeMask. */
    juce::Rectangle<float> colRectFor (int ch) const
    {
        const int numActive = countActiveBits();
        if (numActive == 0) return {};
        const float colW = (float) getWidth() / (float) numActive;
        int col = 0;
        for (int i = 0; i < 16; ++i)
        {
            if (! (activeMask & (1u << i))) continue;
            if (i == ch) return { (float) col * colW, 0.f, colW, (float) getHeight() };
            ++col;
        }
        return {};
    }

    juce::Rectangle<float> knobRect (const juce::Rectangle<float>& col, Knob k) const
    {
        const float y0 = kLabelH + kPadding;
        const float kH = kKnobH;
        const float x  = col.getX() + kPadding;
        const float w  = col.getWidth() - kPadding * 2.f;

        switch (k)
        {
            case Knob::ReverbMix:  return { x, y0,              w, kH };
            case Knob::ReverbSize: return { x, y0 + kH,         w, kH };
            case Knob::ReverbDamp: return { x, y0 + kH * 2.f,   w, kH };
            case Knob::Gain:       return { x, y0 + kH * 3.f,   w, kH };
            default:               return {};
        }
    }

    DragState findKnobAt (juce::Point<int> pt) const
    {
        const juce::Point<float> fpt = pt.toFloat();
        for (int ch = 0; ch < 16; ++ch)
        {
            if (! (activeMask & (1u << ch))) continue;
            const auto col = colRectFor (ch);
            if (! col.contains (fpt)) continue;
            for (auto k : { Knob::ReverbMix, Knob::ReverbSize, Knob::ReverbDamp, Knob::Gain })
                if (knobRect (col, k).contains (fpt))
                    return { ch, k };
        }
        return {};
    }

    // ── Painting ─────────────────────────────────────────────────────────────

    void paintChannel (juce::Graphics& g,
                       int ch,
                       const juce::Rectangle<float>& col,
                       const ThemeData& theme)
    {
        // Separator
        g.setColour (theme.separator);
        g.drawLine (col.getX(), col.getY(), col.getX(), col.getBottom(), 1.f);

        // Preset label
        g.setColour (theme.accent);
        g.setFont (DysektLookAndFeel::makeFont(12.f, true));
        const auto labelRect = col.withHeight (kLabelH).reduced (kPadding, 1.f);
        g.drawText (channelLabels[ch].isEmpty() ? ("CH " + juce::String (ch + 1))
                                                 : channelLabels[ch],
                    labelRect.toNearestInt(), juce::Justification::centredLeft, true);

        // Draw each knob
        paintKnob (g, ch, Knob::ReverbMix,  col, "MIX",  theme);
        paintKnob (g, ch, Knob::ReverbSize, col, "SIZE", theme);
        paintKnob (g, ch, Knob::ReverbDamp, col, "DAMP", theme);
        paintKnob (g, ch, Knob::Gain,       col, "GAIN", theme);
    }

    void paintKnob (juce::Graphics& g, int ch, Knob k,
                    const juce::Rectangle<float>& col,
                    const char* label,
                    const ThemeData& theme)
    {
        const float norm   = getCurrentVal (ch, k);
        const auto  kr     = knobRect (col, k);
        const float cx     = kr.getCentreX();
        const float cy     = kr.getCentreY();
        const float radius = juce::jmin (kr.getWidth(), kr.getHeight()) * 0.38f;

        // Track arc
        constexpr float startAngle = juce::MathConstants<float>::pi * 1.2f;
        constexpr float endAngle   = juce::MathConstants<float>::pi * 2.8f;
        const float fillAngle = startAngle + norm * (endAngle - startAngle);

        juce::Path trackArc;
        trackArc.addCentredArc (cx, cy, radius, radius, 0.f, startAngle, endAngle, true);
        g.setColour (theme.button);
        g.strokePath (trackArc, juce::PathStrokeType (3.f));

        juce::Path fillArc;
        fillArc.addCentredArc (cx, cy, radius, radius, 0.f, startAngle, fillAngle, true);
        g.setColour (theme.accent);
        g.strokePath (fillArc, juce::PathStrokeType (3.f));

        // Pointer
        const float px = cx + radius * 0.6f * std::sin (fillAngle);
        const float py = cy - radius * 0.6f * std::cos (fillAngle);
        g.setColour (theme.accent);
        g.drawLine (cx, cy, px, py, 1.5f);

        // Label and value
        g.setFont (DysektLookAndFeel::makeFont(10.f));
        g.setColour (theme.foreground.withAlpha (0.6f));

        const auto topLabel = kr.withHeight (12.f);
        g.drawText (label, topLabel.toNearestInt(), juce::Justification::centred);

        juce::String valStr;
        switch (k)
        {
            case Knob::Gain:  valStr = juce::String (juce::roundToInt (norm * 200.f)) + "%"; break;
            default:          valStr = juce::String (juce::roundToInt (norm * 100.f)) + "%"; break;
        }

        g.setColour (theme.foreground);
        g.setFont (DysektLookAndFeel::makeFont(11.f, true));
        const auto botLabel = kr.withY (kr.getBottom() - 13.f).withHeight (13.f);
        g.drawText (valStr, botLabel.toNearestInt(), juce::Justification::centred);
    }

    // ── State ─────────────────────────────────────────────────────────────────

    DysektProcessor& processor;
    uint16_t         activeMask  { 0xFFFF };   // show all 16 until told otherwise
    juce::String     channelLabels[16];

    DragState dragState;
    int       dragStartY   { 0 };
    float     dragStartVal { 0.f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sf2ChannelFxPanel)
};
