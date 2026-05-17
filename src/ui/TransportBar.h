#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../sequencer/SequencerEngine.h"
#include "../sequencer/AbletonLink.h"
#include "DysektLookAndFeel.h"

//==============================================================================
//  TransportBar  —  play / stop / record / loop / BPM / snap
//==============================================================================
class TransportBar : public juce::Component,
                     private juce::Timer
{
public:
    TransportBar (SequencerEngine& seq, AbletonLink* link = nullptr)
        : engine (seq), linkPtr (link)
    {
        auto makeBtn = [this](juce::TextButton& b, const juce::String& label)
        {
            b.setButtonText (label);
            addAndMakeVisible (b);
        };

        makeBtn (rewindBtn, "|<<");
        makeBtn (playBtn,   "PLAY");
        makeBtn (stopBtn,   "STOP");
        makeBtn (recBtn,    "REC");
        makeBtn (loopBtn,   "LOOP");

        playBtn.setClickingTogglesState (false);
        stopBtn.setClickingTogglesState (false);
        rewindBtn.setClickingTogglesState (false);
        recBtn.setToggleState  (false, juce::dontSendNotification);
        loopBtn.setToggleState (true,  juce::dontSendNotification);
        recBtn.setClickingTogglesState  (true);
        loopBtn.setClickingTogglesState (true);

        rewindBtn.onClick = [this] { engine.rewind(); };
        playBtn.onClick   = [this] { engine.play();   };
        stopBtn.onClick   = [this] { engine.stop();   };
        recBtn.onStateChange  = [this] { engine.setRecording (recBtn.getToggleState()); };
        loopBtn.onStateChange = [this] { engine.setLooping   (loopBtn.getToggleState()); };

        // BPM label + drag
        bpmLabel.setText ("120.0 BPM", juce::dontSendNotification);
        bpmLabel.setFont (DysektLookAndFeel::makeMonoFont (13.f, true));
        bpmLabel.setJustificationType (juce::Justification::centred);
        bpmLabel.setEditable (true, true, false);
        bpmLabel.onEditorShow = [this]
        {
            if (auto* ed = bpmLabel.getCurrentTextEditor())
            {
                ed->setColour (juce::TextEditor::backgroundColourId, getTheme().button);
                ed->setColour (juce::TextEditor::textColourId,       getTheme().accent);
                ed->setInputRestrictions (6, "0123456789.");
            }
        };
        bpmLabel.onTextChange = [this]
        {
            float v = bpmLabel.getText().getFloatValue();
            if (v >= 20.f && v <= 999.f)
                engine.setBpm (v);
        };
        addAndMakeVisible (bpmLabel);

        // Snap combo
        snapCombo.addItem ("1/1",   1);
        snapCombo.addItem ("1/2",   2);
        snapCombo.addItem ("1/4",   3);
        snapCombo.addItem ("1/8",   4);
        snapCombo.addItem ("1/16",  5);
        snapCombo.addItem ("1/32",  6);
        snapCombo.addItem ("Free",  7);
        snapCombo.setSelectedId (4, juce::dontSendNotification); // default 1/8
        addAndMakeVisible (snapCombo);

        // Playhead time display
        posLabel.setFont (DysektLookAndFeel::makeMonoFont (11.f));
        posLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (posLabel);

        // LINK button
        if (linkPtr != nullptr)
        {
            linkBtn.setButtonText ("LINK");
            linkBtn.setClickingTogglesState (true);
            linkBtn.onStateChange = [this]
            {
                if (linkPtr) linkPtr->setEnabled (linkBtn.getToggleState());
            };
            addAndMakeVisible (linkBtn);
        }

        startTimerHz (20);
    }

    ~TransportBar() override { stopTimer(); }

    //==========================================================================
    /** Snap grid in ticks. */
    int64_t getSnapTicks() const
    {
        const int64_t ppq = MidiClip::kPPQ;
        switch (snapCombo.getSelectedId())
        {
            case 1: return ppq * 4;          // 1/1
            case 2: return ppq * 2;          // 1/2
            case 3: return ppq;              // 1/4
            case 4: return ppq / 2;          // 1/8
            case 5: return ppq / 4;          // 1/16
            case 6: return ppq / 8;          // 1/32
            default: return 0;               // Free
        }
    }

    static int64_t snapTick (int64_t tick, int64_t snapTicks) noexcept
    {
        if (snapTicks <= 0) return tick;
        return ((tick + snapTicks / 2) / snapTicks) * snapTicks;
    }

    //==========================================================================
    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 2);
        const int btnW = 44, gap = 4;

        rewindBtn.setBounds (r.removeFromLeft (btnW)); r.removeFromLeft (gap);
        playBtn  .setBounds (r.removeFromLeft (btnW)); r.removeFromLeft (gap);
        stopBtn  .setBounds (r.removeFromLeft (btnW)); r.removeFromLeft (gap);
        recBtn   .setBounds (r.removeFromLeft (btnW)); r.removeFromLeft (gap * 3);
        loopBtn  .setBounds (r.removeFromLeft (btnW)); r.removeFromLeft (gap * 3);

        bpmLabel .setBounds (r.removeFromLeft (90));   r.removeFromLeft (gap * 2);
        snapCombo.setBounds (r.removeFromLeft (62));   r.removeFromLeft (gap * 2);
        if (linkPtr != nullptr)
        {
            posLabel.setBounds (r.removeFromRight (100));
            linkBtn .setBounds (r.removeFromRight (btnW)); r.removeFromRight (gap * 2);
        }
        else
        {
            posLabel.setBounds (r);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (getTheme().darkBar);
        g.setColour (getTheme().separator);
        g.fillRect (getLocalBounds().removeFromBottom (1));
    }

private:
    SequencerEngine& engine;

    AbletonLink*     linkPtr = nullptr;
    juce::TextButton rewindBtn, playBtn, stopBtn, recBtn, loopBtn;
    juce::TextButton linkBtn { "LINK" };
    juce::Label      bpmLabel, posLabel;
    juce::ComboBox   snapCombo;

    void refreshColours()
    {
        const auto& t = getTheme();
        const bool isPlaying = engine.isPlaying();

        auto applyBtn = [&](juce::TextButton& b)
        {
            b.setColour (juce::TextButton::buttonColourId,  t.button);
            b.setColour (juce::TextButton::buttonOnColourId, t.accent);
            b.setColour (juce::TextButton::textColourOffId,  t.foreground);
            b.setColour (juce::TextButton::textColourOnId,   t.background);
        };

        applyBtn (rewindBtn);
        applyBtn (stopBtn);
        applyBtn (recBtn);
        applyBtn (loopBtn);
        applyBtn (linkBtn);

        // Play button gets a teal tint while playing
        playBtn.setColour (juce::TextButton::buttonColourId,
                           isPlaying ? t.accent.withAlpha (0.25f) : t.button);
        playBtn.setColour (juce::TextButton::buttonOnColourId, t.accent);
        playBtn.setColour (juce::TextButton::textColourOffId,  t.foreground);
        playBtn.setColour (juce::TextButton::textColourOnId,   t.background);

        bpmLabel.setColour (juce::Label::textColourId, t.accent);
        posLabel.setColour (juce::Label::textColourId, t.foreground.withAlpha (0.55f));

        snapCombo.setColour (juce::ComboBox::backgroundColourId, t.button);
        snapCombo.setColour (juce::ComboBox::textColourId,       t.foreground);
        snapCombo.setColour (juce::ComboBox::outlineColourId,    t.separator);
    }

    void timerCallback() override
    {
        refreshColours();

        // Update BPM display (only when not editing)
        if (! bpmLabel.isBeingEdited())
        {
            juce::String bpmStr = juce::String (engine.getBpm(), 1) + " BPM";
            if (bpmLabel.getText() != bpmStr)
                bpmLabel.setText (bpmStr, juce::dontSendNotification);
        }

        // Update LINK button label with peer count
        if (linkPtr != nullptr && linkBtn.getToggleState())
        {
            const int peers = linkPtr->getPeerCount();
            linkBtn.setButtonText (peers > 0
                ? ("LINK " + juce::String (peers))
                : "LINK");
        }
        else if (linkPtr != nullptr)
        {
            linkBtn.setButtonText ("LINK");
        }

        // Update position display
        const double beats = engine.getPlayheadBeats();
        const int bar  = (int)(beats / 4) + 1;
        const int beat = (int)(std::fmod (beats, 4.0)) + 1;
        const int tick = (int)(std::fmod (beats, 1.0) * MidiClip::kPPQ);
        posLabel.setText (juce::String::formatted ("%d . %d . %03d", bar, beat, tick),
                          juce::dontSendNotification);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

public:
    TransportBar (SequencerEngine& seq, AbletonLink* link = nullptr)
        : engine (seq), linkPtr (link)
    {
        auto makeBtn = [this](juce::TextButton& b, const juce::String& label)
        {
            b.setButtonText (label);
            b.setColour (juce::TextButton::buttonColourId,  getTheme().button);
            b.setColour (juce::TextButton::buttonOnColourId, getTheme().accent);
            b.setColour (juce::TextButton::textColourOffId,  getTheme().foreground);
            b.setColour (juce::TextButton::textColourOnId,   getTheme().background);
            addAndMakeVisible (b);
        };

        makeBtn (rewindBtn, "|<<");
        makeBtn (playBtn,   "PLAY");
        makeBtn (stopBtn,   "STOP");
        makeBtn (recBtn,    "REC");
        makeBtn (loopBtn,   "LOOP");

        playBtn.setClickingTogglesState (false);
        stopBtn.setClickingTogglesState (false);
        rewindBtn.setClickingTogglesState (false);
        recBtn.setToggleState  (false, juce::dontSendNotification);
        loopBtn.setToggleState (true,  juce::dontSendNotification);
        recBtn.setClickingTogglesState  (true);
        loopBtn.setClickingTogglesState (true);

        rewindBtn.onClick = [this] { engine.rewind(); };
        playBtn.onClick   = [this] { engine.play();   };
        stopBtn.onClick   = [this] { engine.stop();   };
        recBtn.onStateChange  = [this] { engine.setRecording (recBtn.getToggleState()); };
        loopBtn.onStateChange = [this] { engine.setLooping   (loopBtn.getToggleState()); };

        // BPM label + drag
        bpmLabel.setText ("120.0 BPM", juce::dontSendNotification);
        bpmLabel.setFont (DysektLookAndFeel::makeMonoFont (13.f, true));
        bpmLabel.setColour (juce::Label::textColourId, getTheme().accent);
        bpmLabel.setJustificationType (juce::Justification::centred);
        bpmLabel.setEditable (true, true, false);
        bpmLabel.onEditorShow = [this]
        {
            if (auto* ed = bpmLabel.getCurrentTextEditor())
            {
                ed->setColour (juce::TextEditor::backgroundColourId, getTheme().button);
                ed->setColour (juce::TextEditor::textColourId,       getTheme().accent);
                ed->setInputRestrictions (6, "0123456789.");
            }
        };
        bpmLabel.onTextChange = [this]
        {
            float v = bpmLabel.getText().getFloatValue();
            if (v >= 20.f && v <= 999.f)
                engine.setBpm (v);
        };
        addAndMakeVisible (bpmLabel);

        // Snap combo
        snapCombo.addItem ("1/1",   1);
        snapCombo.addItem ("1/2",   2);
        snapCombo.addItem ("1/4",   3);
        snapCombo.addItem ("1/8",   4);
        snapCombo.addItem ("1/16",  5);
        snapCombo.addItem ("1/32",  6);
        snapCombo.addItem ("Free",  7);
        snapCombo.setSelectedId (4, juce::dontSendNotification); // default 1/8
        snapCombo.setColour (juce::ComboBox::backgroundColourId, getTheme().button);
        snapCombo.setColour (juce::ComboBox::textColourId,       getTheme().foreground);
        snapCombo.setColour (juce::ComboBox::outlineColourId,    getTheme().separator);
        addAndMakeVisible (snapCombo);

        // Playhead time display
        posLabel.setFont (DysektLookAndFeel::makeMonoFont (11.f));
        posLabel.setColour (juce::Label::textColourId, getTheme().foreground.withAlpha (0.55f));
        posLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (posLabel);

        // LINK button
        if (linkPtr != nullptr)
        {
            linkBtn.setButtonText ("LINK");
            linkBtn.setColour (juce::TextButton::buttonColourId,   getTheme().button);
            linkBtn.setColour (juce::TextButton::buttonOnColourId, getTheme().accent.withAlpha (0.85f));
            linkBtn.setColour (juce::TextButton::textColourOffId,  getTheme().foreground);
            linkBtn.setColour (juce::TextButton::textColourOnId,   getTheme().background);
            linkBtn.setClickingTogglesState (true);
            linkBtn.onStateChange = [this]
            {
                if (linkPtr) linkPtr->setEnabled (linkBtn.getToggleState());
            };
            addAndMakeVisible (linkBtn);
        }

        startTimerHz (20);
    }

    ~TransportBar() override { stopTimer(); }

    //==========================================================================
    /** Snap grid in ticks. */
    int64_t getSnapTicks() const
    {
        const int64_t ppq = MidiClip::kPPQ;
        switch (snapCombo.getSelectedId())
        {
            case 1: return ppq * 4;          // 1/1
            case 2: return ppq * 2;          // 1/2
            case 3: return ppq;              // 1/4
            case 4: return ppq / 2;          // 1/8
            case 5: return ppq / 4;          // 1/16
            case 6: return ppq / 8;          // 1/32
            default: return 0;               // Free
        }
    }

    static int64_t snapTick (int64_t tick, int64_t snapTicks) noexcept
    {
        if (snapTicks <= 0) return tick;
        return ((tick + snapTicks / 2) / snapTicks) * snapTicks;
    }

    //==========================================================================
    void resized() override
    {
        auto r = getLocalBounds().reduced (4, 2);
        const int btnW = 44, gap = 4;

        rewindBtn.setBounds (r.removeFromLeft (btnW)); r.removeFromLeft (gap);
        playBtn  .setBounds (r.removeFromLeft (btnW)); r.removeFromLeft (gap);
        stopBtn  .setBounds (r.removeFromLeft (btnW)); r.removeFromLeft (gap);
        recBtn   .setBounds (r.removeFromLeft (btnW)); r.removeFromLeft (gap * 3);
        loopBtn  .setBounds (r.removeFromLeft (btnW)); r.removeFromLeft (gap * 3);

        bpmLabel .setBounds (r.removeFromLeft (90));   r.removeFromLeft (gap * 2);
        snapCombo.setBounds (r.removeFromLeft (62));   r.removeFromLeft (gap * 2);
        if (linkPtr != nullptr)
        {
            posLabel.setBounds (r.removeFromRight (100));
            linkBtn .setBounds (r.removeFromRight (btnW)); r.removeFromRight (gap * 2);
        }
        else
        {
            posLabel.setBounds (r);
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (getTheme().darkBar);
        g.setColour (getTheme().separator);
        g.fillRect (getLocalBounds().removeFromBottom (1));
    }

private:
    SequencerEngine& engine;

    AbletonLink*     linkPtr = nullptr;
    juce::TextButton rewindBtn, playBtn, stopBtn, recBtn, loopBtn;
    juce::TextButton linkBtn { "LINK" };
    juce::Label      bpmLabel, posLabel;
    juce::ComboBox   snapCombo;

    void timerCallback() override
    {
        // Update play button colour
        const bool isPlaying = engine.isPlaying();
        playBtn.setColour (juce::TextButton::buttonColourId,
                           isPlaying ? getTheme().accent.withAlpha (0.25f)
                                     : getTheme().button);

        // Update BPM display (only when not editing)
        if (! bpmLabel.isBeingEdited())
        {
            juce::String bpmStr = juce::String (engine.getBpm(), 1) + " BPM";
            if (bpmLabel.getText() != bpmStr)
                bpmLabel.setText (bpmStr, juce::dontSendNotification);
        }

        // Update LINK button label with peer count
        if (linkPtr != nullptr && linkBtn.getToggleState())
        {
            const int peers = linkPtr->getPeerCount();
            linkBtn.setButtonText (peers > 0
                ? ("LINK " + juce::String (peers))
                : "LINK");
        }
        else
        {
            linkBtn.setButtonText ("LINK");
        }

        // Update position display
        const double beats = engine.getPlayheadBeats();
        const int bar  = (int)(beats / 4) + 1;
        const int beat = (int)(std::fmod (beats, 4.0)) + 1;
        const int tick = (int)(std::fmod (beats, 1.0) * MidiClip::kPPQ);
        posLabel.setText (juce::String::formatted ("%d . %d . %03d", bar, beat, tick),
                          juce::dontSendNotification);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
