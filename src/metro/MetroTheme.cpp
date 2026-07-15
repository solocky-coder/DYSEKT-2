#include "MetroTheme.h"
namespace dysekt::metro {
const juce::Colour MetroTheme::Colours::windowBackground { 0xff17191c };
const juce::Colour MetroTheme::Colours::panelBackground  { 0xff202328 };
const juce::Colour MetroTheme::Colours::raisedPanel      { 0xff292d33 };
const juce::Colour MetroTheme::Colours::separator        { 0xff383d45 };
const juce::Colour MetroTheme::Colours::accent           { 0xff36c4e8 };
const juce::Colour MetroTheme::Colours::accentMuted      { 0xff1d6e82 };
const juce::Colour MetroTheme::Colours::textPrimary      { 0xfff1f4f7 };
const juce::Colour MetroTheme::Colours::textSecondary    { 0xffaeb7c2 };
const juce::Colour MetroTheme::Colours::textDisabled     { 0xff6b737c };
namespace { juce::Font makeFont (float height) { return juce::Font (juce::FontOptions ("Segoe UI", height, juce::Font::plain)); } }
juce::Font MetroTheme::titleFont() { return makeFont (18.0f).boldened(); }
juce::Font MetroTheme::bodyFont() { return makeFont (14.0f); }
juce::Font MetroTheme::smallFont() { return makeFont (11.0f); }
}
