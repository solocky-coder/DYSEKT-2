#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../PluginProcessor.h"
#include "MetroLookAndFeel.h"
namespace dysekt::metro {
class MetroStandaloneEditor final : public juce::AudioProcessorEditor {
public:
    explicit MetroStandaloneEditor (DysektProcessor&); ~MetroStandaloneEditor() override;
    void paint (juce::Graphics&) override; void resized() override;
private:
    class TransportBar; class Sidebar; class ArrangementWorkspace; class Inspector;
    DysektProcessor& processor; MetroLookAndFeel lookAndFeel;
    std::unique_ptr<TransportBar> transportBar; std::unique_ptr<Sidebar> sidebar;
    std::unique_ptr<ArrangementWorkspace> workspace; std::unique_ptr<Inspector> inspector;
};
}
