#include "MidiLearnDialog.h"
#include "../PluginProcessor.h"
#include "../PluginEditor.h"
#include "DysektLookAndFeel.h"

// ── Parameter name table ──────────────────────────────────────────────────────
static const char* const gSlotParamNames[kMidiLearnNumSlots] = {
    "",                      // 0  BPM         — removed
    "Pitch",                 // 1
    "",                      // 2  Algorithm   — removed
    "Attack",                // 3
    "Decay",                 // 4
    "Sustain",               // 5
    "Release",               // 6
    "Mute Group",            // 7
    "",                      // 8  Stretch Enabled — removed
    "",                      // 9  Tonality    — removed
    "Formant",               // 10
    "",                      // 11 Formant Comp — removed
    "Grain Mode",            // 12
    "Volume",                // 13
    "",                      // 14 Release Tail — removed
    "",                      // 15 Reverse     — removed
    "Output Bus",            // 16
    "Loop",                  // 17
    "",                      // 18 One Shot    — removed
    "Cents Detune",          // 19
    "MIDI Note",             // 20
    "Slice Start",           // 21
    "Slice End",             // 22
    "Pan",                   // 23
    "Filter Cutoff",         // 24
    "Filter Resonance",      // 25
    "",                      // 26 Chromatic Channel — removed
    "",                      // 27 Chromatic Legato  — removed
    "Trim Out",              // 28
    "Hold",                  // 29
    "Zoom",                  // 30
    "Scroll",                // 31
    "SF-Player Attack",      // 32
    "SF-Player Decay",       // 33
    "SF-Player Sustain",     // 34
    "SF-Player Release",     // 35
};

static juce::String getSlotParameterName (int fieldId)
{
    if (fieldId >= 0 && fieldId < kMidiLearnNumSlots
        && juce::String (gSlotParamNames[fieldId]).isNotEmpty())
        return gSlotParamNames[fieldId];
    return juce::String ("Param ") + juce::String (fieldId);
}

static int rowToFieldId (int row)
{
    int count = 0;
    for (int i = 0; i < kMidiLearnNumSlots; ++i)
    {
        if (juce::String (gSlotParamNames[i]).isNotEmpty())
        {
            if (count == row) return i;
            ++count;
        }
    }
    return -1;
}

// ── MappingRowComponent ───────────────────────────────────────────────────────

MidiLearnDialog::MappingRowComponent::MappingRowComponent()
{
    const auto& th = getTheme();

    paramLabel.setFont (DysektLookAndFeel::makeFont (12.0f));
    paramLabel.setColour (juce::Label::textColourId, th.foreground);
    paramLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (paramLabel);

    // Channel combo: Any + 1-16
    channelCombo.addItem ("Any Ch", 1);
    for (int ch = 1; ch <= 16; ++ch)
        channelCombo.addItem ("Ch " + juce::String (ch), ch + 1);
    channelCombo.setJustificationType (juce::Justification::centred);
    channelCombo.onChange = [this]
    {
        if (midiLearn && fieldId >= 0)
            midiLearn->setChannel (fieldId, channelCombo.getSelectedId() - 1);
    };
    addAndMakeVisible (channelCombo);

    // CC combo: None + 0-127
    ccCombo.addItem ("--", 1);
    for (int cc = 0; cc <= 127; ++cc)
        ccCombo.addItem ("CC " + juce::String (cc), cc + 2);
    ccCombo.setJustificationType (juce::Justification::centred);
    ccCombo.onChange = [this]
    {
        if (midiLearn && fieldId >= 0)
        {
            const int sel = ccCombo.getSelectedId();
            if (sel == 1)
                midiLearn->clearMapping (fieldId);
            else
                midiLearn->setManualCC (fieldId, sel - 2);
        }
    };
    addAndMakeVisible (ccCombo);

    // Encoder mode combo
    modeCombo.addItem ("Absolute",            MidiLearnManager::kAbsolute     + 1);
    modeCombo.addItem ("Rel 2's Comp",        MidiLearnManager::kRelTwosComp  + 1);
    modeCombo.addItem ("Rel Sign Bit",        MidiLearnManager::kRelSignBit   + 1);
    modeCombo.addItem ("Rel Bin Offset",      MidiLearnManager::kRelBinOffset + 1);
    modeCombo.setJustificationType (juce::Justification::centred);
    modeCombo.onChange = [this]
    {
        if (midiLearn && fieldId >= 0)
        {
            const auto mode = (MidiLearnManager::EncoderMode)(modeCombo.getSelectedId() - 1);
            midiLearn->setEncoderMode (fieldId, mode);
            const bool isRel = (mode != MidiLearnManager::kAbsolute);
            flipButton.setEnabled (isRel);
            flipButton.setAlpha   (isRel ? 1.0f : 0.3f);
        }
    };
    addAndMakeVisible (modeCombo);

    // ARM button
    armButton.setColour (juce::TextButton::buttonColourId,  th.button);
    armButton.setColour (juce::TextButton::textColourOffId, th.accent);
    armButton.onClick = [this]
    {
        if (! midiLearn || fieldId < 0) return;
        if (midiLearn->getArmedSlot() == fieldId)
        {
            midiLearn->armLearn (-1);
            stopTimer();
            armButton.setButtonText ("LEARN");
            armButton.setColour (juce::TextButton::buttonColourId, getTheme().button);
        }
        else
        {
            midiLearn->armLearn (fieldId);
            startTimerHz (4);
        }
    };
    addAndMakeVisible (armButton);

    // Clear button
    clearButton.setColour (juce::TextButton::buttonColourId,  th.button);
    clearButton.setColour (juce::TextButton::textColourOffId, juce::Colours::tomato.withAlpha (0.85f));
    clearButton.onClick = [this]
    {
        if (midiLearn && fieldId >= 0)
        {
            midiLearn->clearMapping (fieldId);
            ccCombo.setSelectedId (1, juce::dontSendNotification);
            channelCombo.setSelectedId (1, juce::dontSendNotification);
        }
    };
    addAndMakeVisible (clearButton);

    // Flip button
    flipButton.setClickingTogglesState (true);
    flipButton.setColour (juce::TextButton::buttonColourId,   th.button);
    flipButton.setColour (juce::TextButton::buttonOnColourId, th.buttonHover.brighter (0.1f));
    flipButton.setColour (juce::TextButton::textColourOffId,  th.foreground.withAlpha (0.45f));
    flipButton.setColour (juce::TextButton::textColourOnId,   th.accent);
    flipButton.onClick = [this]
    {
        if (midiLearn && fieldId >= 0)
            midiLearn->setDirectionFlip (fieldId, flipButton.getToggleState());
    };
    addAndMakeVisible (flipButton);
}

void MidiLearnDialog::MappingRowComponent::update (int field, MidiLearnManager& ml,
                                                    const juce::String& name)
{
    fieldId   = field;
    midiLearn = &ml;
    paramLabel.setText (name, juce::dontSendNotification);

    // CC combo
    const int mappedCc = ml.getMappedCC (field);
    ccCombo.setSelectedId (mappedCc >= 0 ? mappedCc + 2 : 1, juce::dontSendNotification);

    // Channel combo
    channelCombo.setSelectedId (ml.getChannel (field) + 1, juce::dontSendNotification);

    // Encoder mode
    const auto mode = ml.getEncoderMode (field);
    modeCombo.setSelectedId ((int) mode + 1, juce::dontSendNotification);

    const bool isRel = (mode != MidiLearnManager::kAbsolute);
    flipButton.setToggleState (ml.getDirectionFlip (field), juce::dontSendNotification);
    flipButton.setEnabled (isRel);
    flipButton.setAlpha   (isRel ? 1.0f : 0.3f);

    if (midiLearn->getArmedSlot() != fieldId)
    {
        stopTimer();
        armButton.setButtonText ("LEARN");
        armButton.setColour (juce::TextButton::buttonColourId, getTheme().button);
    }
}

void MidiLearnDialog::MappingRowComponent::timerCallback()
{
    if (! midiLearn) { stopTimer(); return; }

    if (midiLearn->getArmedSlot() != fieldId)
    {
        stopTimer();
        armButton.setButtonText ("LEARN");
        armButton.setColour (juce::TextButton::buttonColourId, getTheme().button);
        // Refresh CC combo after successful learn
        if (fieldId >= 0)
        {
            const int mappedCc = midiLearn->getMappedCC (fieldId);
            ccCombo.setSelectedId (mappedCc >= 0 ? mappedCc + 2 : 1,
                                   juce::dontSendNotification);
        }
        return;
    }

    const bool lit = ((juce::Time::getMillisecondCounter() / 250) % 2 == 0);
    armButton.setButtonText (lit ? "WAITING..." : "");
    armButton.setColour (juce::TextButton::buttonColourId,
                         lit ? getTheme().buttonHover.brighter (0.2f) : getTheme().button);
}

void MidiLearnDialog::MappingRowComponent::resized()
{
    const int w      = getWidth(), h = getHeight();
    const int armW   = 58;
    const int clearW = 18;
    const int flipW  = 34;
    const int chW    = 62;
    const int ccW    = 68;
    const int modeW  = w - 140 - armW - clearW - flipW - chW - ccW - 12;
    int x = 4;
    paramLabel  .setBounds (x, 0, 136, h);       x += 140;
    channelCombo.setBounds (x, 1, chW,  h - 2);  x += chW + 2;
    ccCombo     .setBounds (x, 1, ccW,  h - 2);  x += ccW + 2;
    armButton   .setBounds (x, 2, armW, h - 4);  x += armW + 2;
    clearButton .setBounds (x, 2, clearW, h - 4);x += clearW + 2;
    flipButton  .setBounds (x, 2, flipW, h - 4); x += flipW + 2;
    modeCombo   .setBounds (x, 1, juce::jmax (60, modeW), h - 2);
}

// ── Constructor ───────────────────────────────────────────────────────────────

MidiLearnDialog::MidiLearnDialog (MidiLearnManager& ml,
                                   DysektProcessor&  proc,
                                   std::function<void()> onClose)
    : midiLearn (ml), processor (proc), onCloseCallback (onClose)
{
    setOpaque (true);
    addAndMakeVisible (mappingList);
    addAndMakeVisible (saveButton);
    addAndMakeVisible (loadButton);
    addAndMakeVisible (closeButton);

    mappingList.setRowHeight (26);
    mappingList.setModel (this);
    const auto& th = getTheme();
    mappingList.setColour (juce::ListBox::backgroundColourId, th.background.brighter (0.06f));
    mappingList.setColour (juce::ListBox::outlineColourId,    juce::Colours::transparentBlack);

    saveButton.onClick  = [this] { saveToFile(); };
    loadButton.onClick  = [this] { loadFromFile(); };
    closeButton.onClick = [this] { close(); };

    setSize (680, 460);
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void MidiLearnDialog::paint (juce::Graphics& g)
{
    const auto& th = getTheme();
    const int w = getWidth(), h = getHeight();

    g.fillAll (th.background.brighter (0.04f));
    g.setColour (th.accent.withAlpha (0.35f));
    g.drawRect (0, 0, w, h, 1);

    // Title bar
    g.setColour (th.darkBar.brighter (0.08f));
    g.fillRect (1, 1, w - 2, 36);
    g.setColour (th.accent.withAlpha (0.25f));
    g.drawHorizontalLine (36, 1.f, (float)(w - 1));
    g.setColour (th.foreground);
    g.setFont (DysektLookAndFeel::makeFont (15.0f, true));
    g.drawText ("MIDI Learn Assignments", 0, 0, w, 36, juce::Justification::centred);

    // Column headers
    const int hdrY = 38, hdrH = 20;
    g.setColour (th.darkBar);
    g.fillRect (10, hdrY, w - 20, hdrH);
    g.setColour (th.foreground.withAlpha (0.55f));
    g.setFont (DysektLookAndFeel::makeFont (10.0f, true));
    g.drawText ("PARAMETER",    14, hdrY, 136, hdrH, juce::Justification::centredLeft);
    g.drawText ("CHANNEL",     154, hdrY,  62, hdrH, juce::Justification::centredLeft);
    g.drawText ("CC",          220, hdrY,  68, hdrH, juce::Justification::centredLeft);
    g.drawText ("LEARN",       292, hdrY,  58, hdrH, juce::Justification::centredLeft);
    g.drawText ("MODE",        390, hdrY, 120, hdrH, juce::Justification::centredLeft);
}

void MidiLearnDialog::resized()
{
    const int w = getWidth(), h = getHeight();
    mappingList.setBounds (10, 60, w - 20, h - 100);

    const int btnY = h - 34, btnH = 26, btnW = 90, gap = 8;
    const int totalW = btnW * 3 + gap * 2;
    int bx = (w - totalW) / 2;
    saveButton .setBounds (bx,           btnY, btnW, btnH); bx += btnW + gap;
    loadButton .setBounds (bx,           btnY, btnW, btnH); bx += btnW + gap;
    closeButton.setBounds (bx,           btnY, btnW, btnH);
}

// ── ListBoxModel ──────────────────────────────────────────────────────────────

int MidiLearnDialog::getNumRows()
{
    int count = 0;
    for (int i = 0; i < kMidiLearnNumSlots; ++i)
        if (juce::String (gSlotParamNames[i]).isNotEmpty()) ++count;
    return count;
}

void MidiLearnDialog::paintListBoxItem (int, juce::Graphics& g, int w, int h, bool selected)
{
    if (selected) { g.setColour (getTheme().accent.withAlpha (0.18f)); g.fillAll(); }
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawHorizontalLine (h - 1, 0.f, (float) w);
}

juce::Component* MidiLearnDialog::refreshComponentForRow (int row, bool,
                                                           juce::Component* existing)
{
    auto* comp = dynamic_cast<MappingRowComponent*> (existing);
    if (! comp) comp = new MappingRowComponent();
    const int fieldId = rowToFieldId (row);
    if (fieldId >= 0)
        comp->update (fieldId, midiLearn, getSlotParameterName (fieldId));
    return comp;
}

// ── Save / Load ───────────────────────────────────────────────────────────────

void MidiLearnDialog::saveToFile()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Save MIDI Learn Preset",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.dlm");

    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode
                            | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto result = fc.getResult();
            if (result == juce::File{}) return;

            juce::File file = result.withFileExtension ("dlm");

    juce::MemoryOutputStream stream;
    // Write a simple text format: one line per slot
    // slot|cc|channel|encoderMode|directionFlip
    stream.writeText ("# DYSEKT MIDI Learn Preset v1\n", false, false, nullptr);
    for (int i = 0; i < kMidiLearnNumSlots; ++i)
    {
        const int  cc      = midiLearn.getMappedCC      (i);
        const int  channel = midiLearn.getChannel       (i);
        const int  mode    = (int) midiLearn.getEncoderMode (i);
        const bool flip    = midiLearn.getDirectionFlip (i);
        stream.writeText (juce::String (i)       + "|" +
                          juce::String (cc)      + "|" +
                          juce::String (channel) + "|" +
                          juce::String (mode)    + "|" +
                          juce::String ((int) flip) + "\n",
                          false, false, nullptr);
    }
            file.replaceWithData (stream.getData(), stream.getDataSize());
        });
}

void MidiLearnDialog::loadFromFile()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load MIDI Learn Preset",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
        "*.dlm");

    fileChooser->launchAsync (juce::FileBrowserComponent::openMode
                            | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const auto file = fc.getResult();
            if (file == juce::File{} || ! file.existsAsFile()) return;

    for (auto line : juce::StringArray::fromLines (file.loadFileAsString()))
    {
        line = line.trim();
        if (line.isEmpty() || line.startsWith ("#")) continue;

        auto parts = juce::StringArray::fromTokens (line, "|", "");
        if (parts.size() < 5) continue;

        const int  slot    = parts[0].getIntValue();
        const int  cc      = parts[1].getIntValue();
        const int  channel = parts[2].getIntValue();
        const int  mode    = parts[3].getIntValue();
        const bool flip    = parts[4].getIntValue() != 0;

        if (slot < 0 || slot >= kMidiLearnNumSlots) continue;

        if (cc < 0)
            midiLearn.clearMapping (slot);
        else
            midiLearn.setManualCC (slot, juce::jlimit (0, 127, cc));

        midiLearn.setChannel       (slot, juce::jlimit (0, 16, channel));
        midiLearn.setEncoderMode   (slot, (MidiLearnManager::EncoderMode) juce::jlimit (0, 3, mode));
        midiLearn.setDirectionFlip (slot, flip);
    }

            mappingList.updateContent();
            mappingList.repaint();
        });
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void MidiLearnDialog::encoderModeChanged (int /*fieldId*/, MidiLearnManager::EncoderMode /*mode*/) {}

void MidiLearnDialog::close()
{
    if (onCloseCallback)
        onCloseCallback();
    else if (auto* parent = getParentComponent())
        parent->removeChildComponent (this);
}
