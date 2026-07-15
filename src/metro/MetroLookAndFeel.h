#pragma once
#include "MetroTheme.h"
namespace dysekt::metro {
class MetroLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    MetroLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
};
}
