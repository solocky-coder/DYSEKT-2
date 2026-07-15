#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "DysektLookAndFeel.h"

class DysektProcessor;
class WaveformView;

/** Inline trim panel rendered at the bottom of the waveform view.
    Contains draggable IN / OUT knob cells plus APPLY TRIM and CANCEL buttons. */
class TrimDialog : public juce::Component,
                   public juce::Timer
{
public:
    struct Result
    {
        bool trim     = false;
        bool remember = false;
    };

    static void show (const juce::String& fileName, double durationSecs,
                      juce::Component* parent,
                      std::function<void (Result)> callback);

    TrimDialog (DysektProcessor& processor, WaveformView& waveformView);
    ~TrimDialog() override;

    void paint        (juce::Graphics& g) override;
    void resized      () override;
    void mouseDown    (const juce::MouseEvent& e) override;
    void mouseDrag    (const juce::MouseEvent& e) override;
    void mouseUp      (const juce::MouseEvent& e) override;
    void timerCallback() override;

private:
    DysektProcessor& processor;
    WaveformView&    waveformView;

    juce::TextButton applyBtn  { "APPLY" };
    juce::TextButton cancelBtn { "CANCEL" };

    juce::Rectangle<int> inCell, outCell, labelArea;
    int  activeDrag   = -1;   // 0=IN, 1=OUT, -1=none
    int  dragStartY   = 0;
    int  dragStartVal = 0;

    void drawTrimKnob (juce::Graphics& g, juce::Rectangle<int> cell,
                       const char* label, int sampleVal, int totalFrames,
                       bool invertFill);
    void showMidiLearnMenu (int fieldId, juce::Point<int> screenPos);
    void onApply();
    void onCancel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrimDialog)
};
