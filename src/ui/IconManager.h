#pragma once

#include <juce_graphics/juce_graphics.h>

namespace IconManager
{
    // Load an SVG drawable directly from embedded BinaryData
    std::unique_ptr<juce::Drawable> loadDrawableFromBinary(const void* data, int size);

    // Per-icon getters (returns a fresh Drawable instance)
    std::unique_ptr<juce::Drawable> getIconMute();
    std::unique_ptr<juce::Drawable> getIconSolo();
    std::unique_ptr<juce::Drawable> getIconLock();
    std::unique_ptr<juce::Drawable> getIconPower();
    std::unique_ptr<juce::Drawable> getIconPan();
    std::unique_ptr<juce::Drawable> getIconVolume();

    // Button / knob drawables
    std::unique_ptr<juce::Drawable> getButtonIdle();
    std::unique_ptr<juce::Drawable> getButtonHover();
    std::unique_ptr<juce::Drawable> getButtonActive();

    std::unique_ptr<juce::Drawable> getKnobFace();
    std::unique_ptr<juce::Drawable> getKnobPointer();
}
