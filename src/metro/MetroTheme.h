#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
namespace dysekt::metro {
struct MetroTheme final {
    struct Colours final {
        static const juce::Colour windowBackground, panelBackground, raisedPanel, separator, accent, accentMuted, textPrimary, textSecondary, textDisabled;
    };
    struct Metrics final {
        static constexpr int grid = 8, separatorThickness = 1, transportHeight = grid * 7, sidebarWidth = grid * 28, inspectorHeight = grid * 18, panelPadding = grid * 2, controlHeight = grid * 4, trackHeight = grid * 7, timelineBeatWidth = grid * 8;
    };
    static juce::Font titleFont(); static juce::Font bodyFont(); static juce::Font smallFont();
};
}
