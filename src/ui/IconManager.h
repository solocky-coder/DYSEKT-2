#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// ────────────────────────────────────────────────────────────────
//  IconManager — Load SVG drawables and image sprites from embedded BinaryData
// ────────────────────────────────────────────────────────────────
// Usage:
//   auto mute_icon = IconManager::getIconMute();
//   if (mute_icon) g.drawImage(mute_icon->toImage(...), ...);
//
// All getters return unique_ptr<juce::Drawable> (or nullptr on load failure).
// Call std::unique_ptr<juce::Drawable>::createFromImageData() directly for
// raw BinaryData access.

namespace IconManager
{
    // Load an SVG drawable from embedded BinaryData
    // Returns nullptr if data is empty or SVG is malformed.
    std::unique_ptr<juce::Drawable> loadDrawableFromBinary(
        const void* data, int size, const juce::String& name = "");

    // ── Icon getters (returns fresh Drawable instances) ────────────────────────
    std::unique_ptr<juce::Drawable> getIconMute();
    std::unique_ptr<juce::Drawable> getIconSolo();
    std::unique_ptr<juce::Drawable> getIconLock();
    std::unique_ptr<juce::Drawable> getIconPower();
    std::unique_ptr<juce::Drawable> getIconPan();
    std::unique_ptr<juce::Drawable> getIconVolume();

    // ── Button state drawables ──────────────────────────────────────────────────
    std::unique_ptr<juce::Drawable> getButtonIdle();
    std::unique_ptr<juce::Drawable> getButtonHover();
    std::unique_ptr<juce::Drawable> getButtonActive();

    // ── Knob components ──────────────────────────────────────────────────────
    std::unique_ptr<juce::Drawable> getKnobFace();
    std::unique_ptr<juce::Drawable> getKnobPointer();

    // ── Utility: get knob sprite sheet for frame-based rendering ────────────────
    // Returns a clipped subimage of the sprite sheet at the given frame index.
    // frameIndex: 0..frameCount-1; frameCount: typically 64 or 128
    // Returns nullptr if sprite not available.
    juce::Image getKnobSpriteFrame(int frameIndex, int frameCount);
}
