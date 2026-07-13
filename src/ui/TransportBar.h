#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../sequencer/SequencerEngine.h"
#include "../sequencer/AbletonLink.h"
#include "DysektLookAndFeel.h"
#include "UIHelpers.h"

//==============================================================================
//  TransportLAF  —  flat tile buttons with a small coloured indicator dot
//==============================================================================
class TransportLAF : public DysektLookAndFeel
{
public:
    // Register a semantic colour for the indicator dot of each button.
    void registerBtn (juce::Button* b, juce::Colour dotCol)
    {
        colours[b] = dotCol;
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& btn,
                               const juce::Colour& /*bgColour*/,
                               bool isHighlighted, bool isDown) override
    {
        auto bounds = btn.getLocalBounds().toFloat().reduced (1.0f);
        const float r = 4.f;

        // Flat dark tile — slightly lighter on hover/press
        const juce::Colour tile = isDown        ? juce::Colour (0xff2a2a2a)
                                : isHighlighted ? juce::Colour (0xff222222)
                                                : juce::Colour (0xff181818);
        g.setColour (tile);
        g.fillRoundedRectangle (bounds, r);

        // Hairline border — just enough to separate from the background
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawRoundedRectangle (bounds, r, 0.5f);

        // Indicator dot — top-right corner, only when the button is on
        const bool on = btn.getToggleState();
        if (on)
        {
            const juce::Colour dot = colours.count (&btn) ? colours.at (&btn)
                                                          : juce::Colours::white;
            const float dotR = 2.8f;
            const float dotX = bounds.getRight() - dotR * 2.f - 2.f;
            const float dotY = bounds.getY()     + 2.f;
            g.setColour (dot);
            g.fillEllipse (dotX, dotY, dotR * 2.f, dotR * 2.f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& btn,
                         bool /*isHighlighted*/, bool isDown) override
    {
        const bool on = btn.getToggleState();

        // Active: use the dot colour so icon and dot share the same hue
        juce::Colour base = colours.count (&btn) ? colours.at (&btn)
                                                 : juce::Colours::white;
        const juce::Colour textCol = on    ? base.brighter (0.3f).withAlpha (0.95f)
                                   : isDown ? juce::Colours::white.withAlpha (0.90f)
                                           : juce::Colours::white.withAlpha (0.50f);
        g.setColour (textCol);
        g.setFont (juce::Font (15.f));
        g.drawText (btn.getButtonText(), btn.getLocalBounds(),
                    juce::Justification::centred, false);
    }

private:
    std::map<juce::Button*, juce::Colour> colours;
};


//==============================================================================
//  TransportBar
//==============================================================================
class TransportBar : public juce::Component,
                     private juce::Timer
{
public:
    TransportBar (SequencerEngine& seq, AbletonLink* link = nullptr)
        : engine (seq), linkPtr (link)
    {
        // TouchDAW colours: rewind=grey, play=green, stop=amber, rec=red, loop=teal
        const juce::Colour cRewind = juce::Colour (0xff888888);
        const juce::Colour cPlay   = juce::Colour (0xff2ecc40);
        const juce::Colour cStop   = juce::Colour (0xffffb300);
        const juce::Colour cRec    = juce::Colour (0xffdd2222);
        const juce::Colour cLoop   = juce::Colour (0xff00bcd4);
        const juce::Colour cLink   = juce::Colour (0xff7b68ee);

        auto addBtn = [&](juce::TextButton& b, const juce::String& glyph,
                          juce::Colour col, bool isToggle = false)
        {
            b.setButtonText (glyph);
            b.setClickingTogglesState (isToggle);
            b.setLookAndFeel (&laf);
            laf.registerBtn (&b, col);
            addAndMakeVisible (b);
        };

        // Unicode glyphs: rewind ⏮  play ▶  stop ■  rec ⏺  loop ↻
        addBtn (rewindBtn, juce::String::fromUTF8 ("\xe2\x8f\xae"),  cRewind);        // ⏮
        addBtn (playBtn,   juce::String::fromUTF8 ("\xe2\x96\xb6"),  cPlay,  true); // ▶ toggle — dot mirrors engine.isPlaying()
        addBtn (stopBtn,   juce::String::fromUTF8 ("\xe2\x96\xa0"),  cStop);         // ■
        addBtn (recBtn,    juce::String::fromUTF8 ("\xe2\x8f\xba"),  cRec,   true); // ⏺
        addBtn (loopBtn,   juce::String::fromUTF8 ("\xe2\x86\xbb"),  cLoop,  true); // ↻

        loopBtn.setToggleState (true, juce::dontSendNotification);

        rewindBtn.onClick = [this] { engine.rewind(); };
        // playBtn is a visual toggle only — timerCallback() syncs its state to engine.isPlaying()
        playBtn.onClick   = [this] { engine.play(); playBtn.setToggleState (engine.isPlaying(), juce::dontSendNotification); };
        stopBtn.onClick   = [this] { engine.stop(); };
        recBtn.onStateChange  = [this] { engine.setRecording (recBtn.getToggleState()); };
        loopBtn.onStateChange = [this] { engine.setLooping   (loopBtn.getToggleState()); };

        // ── BPM label ────────────────────────────────────────────────────
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
            if (v >= 20.f && v <= 999.f) engine.setBpm (v);
        };
        addAndMakeVisible (bpmLabel);

        // ── Snap combo ───────────────────────────────────────────────────
        snapCombo.addItem ("1/1",  1);
        snapCombo.addItem ("1/2",  2);
        snapCombo.addItem ("1/4",  3);
        snapCombo.addItem ("1/8",  4);
        snapCombo.addItem ("1/16", 5);
        snapCombo.addItem ("1/32", 6);
        snapCombo.addItem ("Free", 7);
        snapCombo.setSelectedId (4, juce::dontSendNotification);
        addAndMakeVisible (snapCombo);

        // ── Position display ─────────────────────────────────────────────
        posLabel.setFont (DysektLookAndFeel::makeMonoFont (12.f));
        posLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (posLabel);

        // ── LINK button ──────────────────────────────────────────────────
        if (linkPtr != nullptr)
        {
            addBtn (linkBtn, "LINK", cLink, true);
            linkBtn.onStateChange = [this]
            {
                if (linkPtr) linkPtr->setEnabled (linkBtn.getToggleState());
            };
        }

        startTimerHz (20);
    }

    ~TransportBar() override
    {
        stopTimer();
        rewindBtn.setLookAndFeel (nullptr);
        playBtn  .setLookAndFeel (nullptr);
        stopBtn  .setLookAndFeel (nullptr);
        recBtn   .setLookAndFeel (nullptr);
        loopBtn  .setLookAndFeel (nullptr);
        linkBtn  .setLookAndFeel (nullptr);
    }

    //==========================================================================
    int64_t getSnapTicks() const
    {
        const int64_t ppq = MidiClip::kPPQ;
        switch (snapCombo.getSelectedId())
        {
            case 1: return ppq * 4;
            case 2: return ppq * 2;
            case 3: return ppq;
            case 4: return ppq / 2;
            case 5: return ppq / 4;
            case 6: return ppq / 8;
            default: return 0;
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
        auto b   = getLocalBounds().reduced (4, 2);
        const int btnH  = b.getHeight();
        const int btnW  = btnH + 4;        // slightly wider than tall — square-ish
        const int bpmW  = 82;
        const int snapW = 58;
        const int posW  = 90;
        const int linkW = 54;
        const int gap   = 5;

        // ── Right side: LINK → pos → snap → BPM ──────────────────────────
        if (linkPtr != nullptr)
        {
            linkBtn  .setBounds (b.removeFromRight (linkW));
            b.removeFromRight (gap);
        }
        posLabel .setBounds (b.removeFromRight (posW));  b.removeFromRight (gap * 2);
        snapCombo.setBounds (b.removeFromRight (snapW)); b.removeFromRight (gap * 2);
        bpmLabel .setBounds (b.removeFromRight (bpmW));  b.removeFromRight (gap * 2);

        // ── Transport: truly centered in full bar ─────────────────────────
        const int nBtns  = 5;
        const int groupW = nBtns * btnW + (nBtns - 1) * gap;
        const int fullW  = getLocalBounds().getWidth();
        const int cx     = (fullW - groupW) / 2;
        const int y      = b.getY();

        rewindBtn.setBounds (cx + 0 * (btnW + gap), y, btnW, btnH);
        playBtn  .setBounds (cx + 1 * (btnW + gap), y, btnW, btnH);
        stopBtn  .setBounds (cx + 2 * (btnW + gap), y, btnW, btnH);
        recBtn   .setBounds (cx + 3 * (btnW + gap), y, btnW, btnH);
        loopBtn  .setBounds (cx + 4 * (btnW + gap), y, btnW, btnH);
    }

    void paint (juce::Graphics& g) override
    {
        UIHelpers::drawTexturedPanel (g, getLocalBounds().toFloat(), getTheme().darkBar,
                                       UIHelpers::PanelZone::Chassis);
        g.setColour (getTheme().separator);
        g.fillRect (getLocalBounds().removeFromBottom (1));
    }

private:
    SequencerEngine&  engine;
    AbletonLink*      linkPtr = nullptr;
    TransportLAF      laf;

    juce::TextButton  rewindBtn, playBtn, stopBtn, recBtn, loopBtn;
    juce::TextButton  linkBtn { "LINK" };
    juce::Label       bpmLabel, posLabel;
    juce::ComboBox    snapCombo;

    void timerCallback() override
    {
        const auto& t = getTheme();

        // Mirror engine play state onto playBtn toggle so the dot shows while playing
        const bool playing = engine.isPlaying();
        if (playBtn.getToggleState() != playing)
            playBtn.setToggleState (playing, juce::dontSendNotification);

        // BPM
        if (! bpmLabel.isBeingEdited())
        {
            juce::String s = juce::String (engine.getBpm(), 1) + " BPM";
            if (bpmLabel.getText() != s) bpmLabel.setText (s, juce::dontSendNotification);
        }
        bpmLabel.setColour (juce::Label::textColourId, t.accent);

        // LINK peer count
        if (linkPtr != nullptr)
        {
            const int peers = linkPtr->getPeerCount();
            juce::String ls = peers > 0 ? ("LINK " + juce::String (peers)) : "LINK";
            if (linkBtn.getButtonText() != ls) linkBtn.setButtonText (ls);
        }

        // Position
        const double beats = engine.getPlayheadBeats();
        const int bar  = (int)(beats / 4) + 1;
        const int beat = (int)(std::fmod (beats, 4.0)) + 1;
        const int tick = (int)(std::fmod (beats, 1.0) * MidiClip::kPPQ);
        posLabel.setText (juce::String::formatted ("%d.%d.%03d", bar, beat, tick),
                          juce::dontSendNotification);
        posLabel.setColour (juce::Label::textColourId, t.foreground.withAlpha (0.65f));

        snapCombo.setColour (juce::ComboBox::backgroundColourId, t.button);
        snapCombo.setColour (juce::ComboBox::textColourId,       t.foreground);
        snapCombo.setColour (juce::ComboBox::outlineColourId,    t.separator);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
