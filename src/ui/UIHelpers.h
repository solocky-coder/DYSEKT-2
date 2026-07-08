#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ThemeData.h"
#include <cmath>

namespace UIHelpers
{

// Shared button styling for popup/overlay dialogs (ConfirmOverlay, RenameOverlay,
// AddZoneOverlay, ArchiveUrlOverlay, SaveSfzOverlay, etc.) so every dialog's
// primary and secondary buttons render with identical colours — including
// text colour — instead of each overlay setting its own copy of these values.
//
// Primary  = the affirmative/confirming action (e.g. "OK", "Trim", "Save", "Add Zone")
// Secondary = the neutral/dismissive action (e.g. "Cancel", "No Thanks", "Clear")
inline void stylePrimaryPopupButton (juce::TextButton& b, const ThemeData& T)
{
    b.setColour (juce::TextButton::buttonColourId,  T.accent.withAlpha (0.85f));
    b.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    b.setColour (juce::TextButton::textColourOnId,  juce::Colours::black);
}

inline void styleSecondaryPopupButton (juce::TextButton& b, const ThemeData& T)
{
    b.setColour (juce::TextButton::buttonColourId,  T.button);
    b.setColour (juce::TextButton::textColourOffId, T.foreground);
    b.setColour (juce::TextButton::textColourOnId,  T.foreground);
}

// Shared "modern" popup dialog chrome, used by every overlay (ConfirmOverlay,
// RenameOverlay, AddZoneOverlay, ArchiveUrlOverlay, SaveSfzOverlay). Replaces
// the old flat/sharp-cornered dialog look (5px radius, no elevation, hard
// divider rule under the title) with a softer rounded box, a real drop
// shadow for elevation, and a darker backdrop so the dialog reads as
// floating above the plugin UI rather than pasted flat on top of it.
static constexpr float kPopupCornerRadius = 12.0f;

inline void drawPopupBackdrop (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.fillRect (bounds);
}

inline void drawPopupBox (juce::Graphics& g, juce::Rectangle<int> box, const ThemeData& T)
{
    // Soft elevation shadow so the box reads as floating above the UI.
    juce::DropShadow shadow (juce::Colours::black.withAlpha (0.5f), 22, { 0, 6 });
    juce::Path shadowPath;
    shadowPath.addRoundedRectangle (box.toFloat(), kPopupCornerRadius);
    shadow.drawForPath (g, shadowPath);

    g.setColour (T.header);
    g.fillRoundedRectangle (box.toFloat(), kPopupCornerRadius);
    g.setColour (T.accent.withAlpha (0.7f));
    g.drawRoundedRectangle (box.toFloat().reduced (0.5f), kPopupCornerRadius, 1.5f);
}

// Computes a new parameter value from a vertical drag gesture.
// startVal    : value at drag start
// deltaY      : upward pixels (positive = increase)
// minVal/maxVal: parameter range
// coarse      : true when Shift is held (5-unit snap; default: 1-unit)
// Sensitivity : full parameter range covered in 200 px of drag
inline float computeDragValue (float startVal, float deltaY,
                                float minVal, float maxVal, bool coarse)
{
    float sensitivity = (maxVal - minVal) / 200.0f;
    float newVal = startVal + deltaY * sensitivity;
    float snap = coarse ? 5.0f : 1.0f;
    newVal = std::round (newVal / snap) * snap;
    return juce::jlimit (minVal, maxVal, newVal);
}

// Computes a zoom multiplier from a vertical drag delta.
// Each pixel of downward drag multiplies zoom by 1.01, giving
// approximately 70 px to double or halve the zoom level.
inline float computeZoomFactor (float deltaY)
{
    return std::pow (1.01f, deltaY);
}

} // namespace UIHelpers

namespace UILayout
{

// Waveform vertical scale factor per channel.
// At 0.48, each channel's peak reaches 48% of component height,
// leaving a small visible gap between the two channels at 0 dBFS.
static constexpr float waveformVerticalScale = 0.48f;

} // namespace UILayout
