#include "ActionPanel.h"
#include "DysektLookAndFeel.h"
#include "WaveformView.h"
#include "../PluginProcessor.h"

ActionPanel::ActionPanel (DysektProcessor& p, WaveformView& wv)
    : processor (p), waveformView (wv)
{
    for (auto* btn : { &lazyChopBtn, &shortcutsBtn })
    {
        addAndMakeVisible (btn);
        btn->setColour (juce::TextButton::buttonColourId,  getTheme().button);
        btn->setColour (juce::TextButton::textColourOnId,  getTheme().foreground);
        btn->setColour (juce::TextButton::textColourOffId, getTheme().foreground);
    }

    lazyChopBtn.setClickingTogglesState(true);

    auto syncButtonColours = [this] {
        updateToggleBtn(lazyChopBtn, lazyChopBtn.getToggleState());
    };

    lazyChopBtn.onClick = [this, syncButtonColours]
    {
        if (processor.trimModeActive.load (std::memory_order_relaxed)) return;
        bool midiActive = lazyChopBtn.getToggleState();

        DysektProcessor::Command cmd;
        cmd.type = midiActive ? DysektProcessor::CmdLazyChopStart : DysektProcessor::CmdLazyChopStop;
        processor.pushCommand(cmd);

        if (midiActive)
            processor.midiSelectsSlice.store(true);

        syncButtonColours();
        repaint();
    };

    shortcutsBtn.onClick = [this] { if (onShortcutsToggle) onShortcutsToggle(); };
    seqBtn.onClick = [this] { if (onSeqToggle) onSeqToggle(); };
    seqBtn.setTooltip ("Piano Roll Sequencer");
    addAndMakeVisible (seqBtn);
    shortcutsBtn.setTooltip ("Keyboard Shortcuts (?)");
    lazyChopBtn.setTooltip ("MIDI Slice");

    lazyChopBtn.setButtonText ("");
    updateMidiButtonAppearance(false);

    syncButtonColours();
}

ActionPanel::~ActionPanel() = default;

void ActionPanel::setTrimActive (bool inTrim)
{
    lazyChopBtn.setEnabled (! inTrim);

    if (inTrim)
    {
        lazyChopBtn.setToggleState (false, juce::dontSendNotification);
        lazyChopBtn.setColour (juce::TextButton::buttonColourId,  getTheme().button.withAlpha (0.4f));
        lazyChopBtn.setColour (juce::TextButton::textColourOffId, getTheme().foreground.withAlpha (0.2f));
    }
    else
    {
        updateToggleBtn (lazyChopBtn, lazyChopBtn.getToggleState());
    }
    repaint();
}

void ActionPanel::updateToggleBtn (juce::TextButton& btn, bool active)
{
    if (active)
    {
        btn.setColour (juce::TextButton::buttonColourId,  getTheme().accent.withAlpha (0.2f));
        btn.setColour (juce::TextButton::textColourOnId,  getTheme().accent);
        btn.setColour (juce::TextButton::textColourOffId, getTheme().accent);
    }
    else
    {
        btn.setColour (juce::TextButton::buttonColourId,  getTheme().button);
        btn.setColour (juce::TextButton::textColourOnId,  getTheme().foreground);
        btn.setColour (juce::TextButton::textColourOffId, getTheme().foreground);
    }
}

void ActionPanel::updateMidiButtonAppearance (bool /*active*/) {}

void ActionPanel::resized()
{
    const int h    = getHeight();
    const int btnW = 34;

    lazyChopBtn.setBounds (0, 0, btnW, h);

    shortcutsBtn.setVisible (false);
    seqBtn.setBounds (getWidth() - btnW, 0, btnW, h);
}

void ActionPanel::paint (juce::Graphics& g)
{
    const bool inTrim = processor.trimModeActive.load (std::memory_order_relaxed);

    if (inTrim)
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (lazyChopBtn.getBounds());
        return;
    }

    if (processor.lazyChop.isActive())
    {
        g.setColour (juce::Colours::red.withAlpha (0.25f));
        g.fillRect (lazyChopBtn.getBounds());
    }
}

static void ap_drawScissors (juce::Graphics& g, float cx, float cy, float sz, juce::Colour col)
{
    const float hw = sz * 0.38f; const float hh = sz * 0.22f;
    g.setColour (col);
    juce::Path sc;
    sc.startNewSubPath (cx - hw, cy - hh * 0.2f); sc.lineTo (cx + hw * 0.55f, cy - hh);
    sc.startNewSubPath (cx - hw, cy + hh * 0.2f); sc.lineTo (cx + hw * 0.55f, cy + hh);
    g.strokePath (sc, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillEllipse (cx - hw * 0.05f, cy - sz * 0.06f, sz * 0.12f, sz * 0.12f);
}
static void ap_drawPiano (juce::Graphics& g, float cx, float cy, float sz, juce::Colour col)
{
    const float kw = sz * 0.11f; const float kh = sz * 0.44f; const float bh = kh * 0.58f;
    const float startX = cx - kw * 2.5f;
    g.setColour (col.withAlpha (0.85f));
    for (int i = 0; i < 5; ++i)
        g.fillRoundedRectangle (startX + i * (kw + 1.f), cy - kh * 0.5f, kw, kh, 0.8f);
    g.setColour (col.withAlpha (0.55f));
    for (int i : { 0, 1, 3 })
        g.fillRect  (startX + i * (kw + 1.f) + kw * 0.65f, cy - kh * 0.5f, kw * 0.68f, bh);
}

static constexpr float iconGap = 9.0f;

void ActionPanel::paintOverChildren (juce::Graphics& g)
{
    const bool inTrim = processor.trimModeActive.load (std::memory_order_relaxed);

    // --- MIDI SLICE icon: Piano + Scissors ---
    {
        auto b  = lazyChopBtn.getBounds().toFloat();
        const float cy    = b.getCentreY();
        const float sz    = b.getHeight() * 0.72f;
        const float alpha = inTrim ? 0.20f : (lazyChopBtn.isEnabled() ? 0.92f : 0.35f);
        const auto  col   = (!inTrim && processor.lazyChop.isActive())
            ? getTheme().accent
            : getTheme().foreground.withAlpha (alpha);

        const float pianoW     = sz * 0.70f;
        const float scissorsW  = sz * 0.70f;
        const float totalW     = pianoW + iconGap + scissorsW;
        const float left       = b.getCentreX() - totalW/2.f;

        ap_drawPiano    (g, left + pianoW/2.f,                       cy, sz, col);
        ap_drawScissors (g, left + pianoW + iconGap + scissorsW/2.f, cy, sz, col);
    }
}
