#include "IconManager.h"
#include "BinaryData.h"

namespace IconManager
{
    std::unique_ptr<juce::Drawable> loadDrawableFromBinary(
        const void* data, int size, const juce::String& name)
    {
        if (data == nullptr || size <= 0)
        {
            juce::Logger::writeToLog ("IconManager: empty data for " + name);
            return nullptr;
        }

        auto drawable = juce::Drawable::createFromImageData (data, (size_t) size);
        if (!drawable)
        {
            juce::Logger::writeToLog ("IconManager: failed to parse " + name);
            return nullptr;
        }
        return drawable;
    }

    // ── Icon SVG getters ────────────────────────────────────────────────────────
    std::unique_ptr<juce::Drawable> getIconMute()
    {
        return loadDrawableFromBinary (
            BinaryData::icon_mute_svg, BinaryData::icon_mute_svgSize, "icon_mute");
    }

    std::unique_ptr<juce::Drawable> getIconSolo()
    {
        return loadDrawableFromBinary (
            BinaryData::icon_solo_svg, BinaryData::icon_solo_svgSize, "icon_solo");
    }

    std::unique_ptr<juce::Drawable> getIconLock()
    {
        return loadDrawableFromBinary (
            BinaryData::icon_lock_svg, BinaryData::icon_lock_svgSize, "icon_lock");
    }

    std::unique_ptr<juce::Drawable> getIconPower()
    {
        return loadDrawableFromBinary (
            BinaryData::icon_power_svg, BinaryData::icon_power_svgSize, "icon_power");
    }

    std::unique_ptr<juce::Drawable> getIconPan()
    {
        return loadDrawableFromBinary (
            BinaryData::icon_pan_svg, BinaryData::icon_pan_svgSize, "icon_pan");
    }

    std::unique_ptr<juce::Drawable> getIconVolume()
    {
        return loadDrawableFromBinary (
            BinaryData::icon_volume_svg, BinaryData::icon_volume_svgSize, "icon_volume");
    }

    // ── Button state getters ────────────────────────────────────────────────────
    std::unique_ptr<juce::Drawable> getButtonIdle()
    {
        return loadDrawableFromBinary (
            BinaryData::button_idle_svg, BinaryData::button_idle_svgSize, "button_idle");
    }

    std::unique_ptr<juce::Drawable> getButtonHover()
    {
        return loadDrawableFromBinary (
            BinaryData::button_hover_svg, BinaryData::button_hover_svgSize, "button_hover");
    }

    std::unique_ptr<juce::Drawable> getButtonActive()
    {
        return loadDrawableFromBinary (
            BinaryData::button_active_svg, BinaryData::button_active_svgSize, "button_active");
    }

    // ── Knob getters ────────────────────────────────────────────────────────────
    std::unique_ptr<juce::Drawable> getKnobFace()
    {
        return loadDrawableFromBinary (
            BinaryData::knob_face_svg, BinaryData::knob_face_svgSize, "knob_face");
    }

    std::unique_ptr<juce::Drawable> getKnobPointer()
    {
        return loadDrawableFromBinary (
            BinaryData::knob_pointer_svg, BinaryData::knob_pointer_svgSize, "knob_pointer");
    }

    // ── Sprite sheet frame getter ───────────────────────────────────────────────
    juce::Image getKnobSpriteFrame(int frameIndex, int frameCount)
    {
        // Load knob sprite 64x64 @1x (default fallback size)
        auto sheet = juce::ImageCache::getFromMemory (
            BinaryData::knob_sprite_64x64_64frames_png,
            BinaryData::knob_sprite_64x64_64frames_pngSize);

        if (sheet.isNull())
        {
            juce::Logger::writeToLog ("IconManager: knob sprite sheet not loaded");
            return juce::Image();
        }

        frameIndex = juce::jlimit (0, frameCount - 1, frameIndex);
        int frameHeight = sheet.getHeight() / frameCount;
        int frameWidth  = sheet.getWidth();

        return sheet.getClippedImage (
            juce::Rectangle<int> (0, frameIndex * frameHeight, frameWidth, frameHeight));
    }
}
