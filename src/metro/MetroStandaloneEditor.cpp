#include "MetroStandaloneEditor.h"
#include "../sequencer/SequencerEngine.h"
namespace dysekt::metro {
class MetroStandaloneEditor::TransportBar final : public juce::Component, private juce::Timer {
public:
    explicit TransportBar (SequencerEngine& sequencer) : engine (sequencer) {
        play.setButtonText ("Play"); stop.setButtonText ("Stop"); rewind.setButtonText ("Rewind"); record.setButtonText ("Record"); record.setClickingTogglesState (true);
        for (auto* button : { &rewind, &play, &stop, &record }) addAndMakeVisible (*button);
        rewind.onClick = [this] { engine.rewind(); }; play.onClick = [this] { engine.play(); }; stop.onClick = [this] { engine.stop(); }; record.onClick = [this] { engine.setRecording (record.getToggleState()); };
        tempo.setEditable (true, true, false); tempo.setJustificationType (juce::Justification::centred); tempo.setFont (MetroTheme::bodyFont());
        tempo.onTextChange = [this] { const auto bpm = tempo.getText().upToFirstOccurrenceOf (" ", false, false).getFloatValue(); if (bpm >= 20.0f && bpm <= 999.0f) engine.setBpm (bpm); };
        addAndMakeVisible (tempo); startTimerHz (20);
    }
    void paint (juce::Graphics& graphics) override { graphics.fillAll (MetroTheme::Colours::panelBackground); graphics.setColour (MetroTheme::Colours::separator); graphics.fillRect (getLocalBounds().removeFromBottom (MetroTheme::Metrics::separatorThickness)); }
    void resized() override { auto area = getLocalBounds().reduced (MetroTheme::Metrics::panelPadding, MetroTheme::Metrics::grid); tempo.setBounds (area.removeFromRight (MetroTheme::Metrics::grid * 14)); for (auto* button : { &rewind, &play, &stop, &record }) button->setBounds (area.removeFromLeft (MetroTheme::Metrics::grid * 10).reduced (MetroTheme::Metrics::separatorThickness)); }
private:
    void timerCallback() override { play.setToggleState (engine.isPlaying(), juce::dontSendNotification); record.setToggleState (engine.isRecording(), juce::dontSendNotification); if (! tempo.isBeingEdited()) tempo.setText (juce::String (engine.getBpm(), 1) + " BPM", juce::dontSendNotification); }
    SequencerEngine& engine; juce::TextButton rewind, play, stop, record; juce::Label tempo;
};
class MetroStandaloneEditor::Sidebar final : public juce::Component {
public:
    Sidebar() { for (auto* button : { &arrange, &pads, &browser, &mixer }) { button->setClickingTogglesState (true); addAndMakeVisible (*button); } arrange.setButtonText ("Arrange"); pads.setButtonText ("Pads"); browser.setButtonText ("Browser"); mixer.setButtonText ("Mixer"); arrange.setToggleState (true, juce::dontSendNotification); }
    void paint (juce::Graphics& graphics) override { graphics.fillAll (MetroTheme::Colours::panelBackground); }
    void resized() override { auto area = getLocalBounds().reduced (MetroTheme::Metrics::panelPadding); for (auto* button : { &arrange, &pads, &browser, &mixer }) button->setBounds (area.removeFromTop (MetroTheme::Metrics::controlHeight)); }
private: juce::TextButton arrange, pads, browser, mixer;
};
class MetroStandaloneEditor::ArrangementWorkspace final : public juce::Component, private juce::Timer {
public:
    explicit ArrangementWorkspace (SequencerEngine& sequencer) : engine (sequencer) { startTimerHz (15); }
    void paint (juce::Graphics& graphics) override {
        const auto bounds = getLocalBounds(); graphics.fillAll (MetroTheme::Colours::windowBackground); graphics.setFont (MetroTheme::smallFont()); const int beatWidth = MetroTheme::Metrics::timelineBeatWidth;
        for (int x = 0; x < bounds.getWidth(); x += beatWidth) { graphics.setColour (MetroTheme::Colours::separator.withAlpha (0.55f)); graphics.drawVerticalLine (x, 0.0f, static_cast<float> (bounds.getHeight())); graphics.setColour (MetroTheme::Colours::textDisabled); graphics.drawText (juce::String (x / beatWidth + 1), x + MetroTheme::Metrics::grid, 0, beatWidth, MetroTheme::Metrics::grid * 3, juce::Justification::centredLeft); }
        for (int index = 0; index < engine.getNumTracks(); ++index) {
            const int y = MetroTheme::Metrics::grid * 3 + index * MetroTheme::Metrics::trackHeight; auto row = juce::Rectangle<int> (0, y, bounds.getWidth(), MetroTheme::Metrics::trackHeight); graphics.setColour (MetroTheme::Colours::panelBackground.withAlpha (0.75f)); graphics.fillRect (row); graphics.setColour (MetroTheme::Colours::separator); graphics.drawHorizontalLine (row.getBottom() - 1, 0.0f, static_cast<float> (bounds.getWidth())); const auto track = engine.getTrackInfo (index); graphics.setColour (track.colour.brighter (0.2f)); graphics.fillRect (row.removeFromLeft (MetroTheme::Metrics::grid / 2)); graphics.setColour (MetroTheme::Colours::textPrimary); graphics.drawText (track.name, MetroTheme::Metrics::grid * 2, y, MetroTheme::Metrics::grid * 18, MetroTheme::Metrics::trackHeight, juce::Justification::centredLeft);
            for (int clipIndex = 0; clipIndex < engine.getNumClips (index); ++clipIndex) { const auto clip = engine.getClipInfo (index, clipIndex); const int clipX = static_cast<int> (clip.startTick / 960) * beatWidth; const int clipWidth = juce::jmax (beatWidth, static_cast<int> (clip.lengthTicks / 960) * beatWidth); const auto clipBounds = juce::Rectangle<int> (clipX, y + MetroTheme::Metrics::grid, clipWidth, MetroTheme::Metrics::trackHeight - MetroTheme::Metrics::grid * 2); graphics.setColour (track.colour.withAlpha (0.78f)); graphics.fillRect (clipBounds); graphics.setColour (MetroTheme::Colours::textPrimary); graphics.drawText ("Clip " + juce::String (clipIndex + 1), clipBounds.reduced (MetroTheme::Metrics::grid), juce::Justification::centredLeft, true); }
        }
        graphics.setColour (MetroTheme::Colours::accent); graphics.drawVerticalLine (static_cast<int> (engine.getPlayheadBeats()) * beatWidth, 0.0f, static_cast<float> (bounds.getHeight()));
    }
private: void timerCallback() override { repaint(); } SequencerEngine& engine;
};
class MetroStandaloneEditor::Inspector final : public juce::Component {
public: void paint (juce::Graphics& graphics) override { graphics.fillAll (MetroTheme::Colours::panelBackground); graphics.setColour (MetroTheme::Colours::separator); graphics.fillRect (getLocalBounds().removeFromTop (MetroTheme::Metrics::separatorThickness)); graphics.setColour (MetroTheme::Colours::textSecondary); graphics.setFont (MetroTheme::smallFont()); graphics.drawText ("INSPECTOR  •  Select a clip, pad, or mixer channel", getLocalBounds().reduced (MetroTheme::Metrics::panelPadding), juce::Justification::centredLeft, true); }
};
MetroStandaloneEditor::MetroStandaloneEditor (DysektProcessor& processorToEdit) : AudioProcessorEditor (processorToEdit), processor (processorToEdit) {
    setLookAndFeel (&lookAndFeel); transportBar = std::make_unique<TransportBar> (processor.sequencer); sidebar = std::make_unique<Sidebar>(); workspace = std::make_unique<ArrangementWorkspace> (processor.sequencer); inspector = std::make_unique<Inspector>(); for (auto* component : { static_cast<juce::Component*> (transportBar.get()), sidebar.get(), workspace.get(), inspector.get() }) addAndMakeVisible (*component); setResizable (true, true); setSize (MetroTheme::Metrics::grid * 150, MetroTheme::Metrics::grid * 100);
}
MetroStandaloneEditor::~MetroStandaloneEditor() { setLookAndFeel (nullptr); }
void MetroStandaloneEditor::paint (juce::Graphics& graphics) { graphics.fillAll (MetroTheme::Colours::windowBackground); }
void MetroStandaloneEditor::resized() { auto area = getLocalBounds(); transportBar->setBounds (area.removeFromTop (MetroTheme::Metrics::transportHeight)); inspector->setBounds (area.removeFromBottom (MetroTheme::Metrics::inspectorHeight)); sidebar->setBounds (area.removeFromLeft (MetroTheme::Metrics::sidebarWidth)); workspace->setBounds (area); }
}
