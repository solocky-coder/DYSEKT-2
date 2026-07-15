#include "MetroLookAndFeel.h"
namespace dysekt::metro {
MetroLookAndFeel::MetroLookAndFeel() {
    setColour (juce::TextButton::buttonColourId, MetroTheme::Colours::raisedPanel);
    setColour (juce::TextButton::textColourOffId, MetroTheme::Colours::textPrimary);
    setColour (juce::Label::textColourId, MetroTheme::Colours::textPrimary);
    setColour (juce::TextEditor::textColourId, MetroTheme::Colours::textPrimary);
    setColour (juce::TextEditor::backgroundColourId, MetroTheme::Colours::raisedPanel);
}
void MetroLookAndFeel::drawButtonBackground (juce::Graphics& graphics, juce::Button& button, const juce::Colour&, bool hovered, bool down) {
    auto colour = button.getToggleState() ? MetroTheme::Colours::accentMuted : MetroTheme::Colours::raisedPanel;
    if (hovered) colour = colour.brighter (0.12f); if (down) colour = colour.darker (0.12f);
    graphics.setColour (colour); graphics.fillRect (button.getLocalBounds());
}
void MetroLookAndFeel::drawButtonText (juce::Graphics& graphics, juce::TextButton& button, bool, bool) {
    graphics.setColour (button.isEnabled() ? MetroTheme::Colours::textPrimary : MetroTheme::Colours::textDisabled);
    graphics.setFont (MetroTheme::bodyFont());
    graphics.drawFittedText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, 1);
}
}
