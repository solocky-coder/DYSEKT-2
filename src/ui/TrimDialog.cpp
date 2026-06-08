#include "TrimDialog.h"
#include "DysektLookAndFeel.h"
#include "WaveformView.h"
#include "../PluginProcessor.h"
#include "../PluginEditor.h"

TrimDialog::TrimDialog (DysektProcessor& proc, WaveformView& wv)
    : processor (proc), waveformView (wv)
{
    applyBtn.setColour (juce::TextButton::buttonColourId,  getTheme().accent.withAlpha (0.85f));
    applyBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::black);
    applyBtn.onClick = [this] { onApply(); };
    addAndMakeVisible (applyBtn);

    cancelBtn.setColour (juce::TextButton::buttonColourId,  getTheme().button);
    cancelBtn.setColour (juce::TextButton::textColourOffId, getTheme().foreground);
    cancelBtn.onClick = [this] { onCancel(); };
    addAndMakeVisible (cancelBtn);

    // 30 Hz repaint keeps cells in sync with MIDI CC driving trimRegionStart/End
    startTimerHz (30);
}

TrimDialog::~TrimDialog() { stopTimer(); }

void TrimDialog::timerCallback() { repaint(); }

// ─────────────────────────────────────────────────────────────────────────────
//  Flat LCD-style knob cell — wide enough to read, tall enough for two rows
// ─────────────────────────────────────────────────────────────────────────────
void TrimDialog::drawTrimKnob (juce::Graphics& g,
                                juce::Rectangle<int> cell,
                                const char* label, int sampleVal, int totalFrames)
{
    const auto& T = getTheme();
    const float sf = (float) getHeight() / 34.0f;   // scale relative to nominal 34px bar height

    // Background + border
    g.setColour (T.darkBar);
    g.fillRoundedRectangle (cell.toFloat(), 3.0f);
    g.setColour (T.accent.withAlpha (0.55f));
    g.drawRoundedRectangle (cell.toFloat().reduced (0.5f), 3.0f, 1.0f);

    // Progress bar — inset inside frame, does NOT mutate cell
    if (totalFrames > 0)
    {
        const float frac   = juce::jlimit (0.0f, 1.0f,
                                            (float) sampleVal / (float) totalFrames);
        const int   inset  = juce::roundToInt (1.0f * sf);
        const int   barH   = juce::roundToInt (3.0f * sf);
        const auto  bar    = juce::Rectangle<float> (
            (float) (cell.getX()      + inset),
            (float) (cell.getBottom() - inset - barH),
            (float) (cell.getWidth()  - 2 * inset),
            (float) barH);
        g.setColour (T.separator);
        g.fillRect (bar);
        g.setColour (T.accent);
        g.fillRect (bar.withWidth (bar.getWidth() * frac));
    }

    // Label — top half, small caps style
    const int midY = cell.getY() + cell.getHeight() / 2;
    g.setFont (DysektLookAndFeel::makeFont (8.0f * sf));
    g.setColour (T.accent.withAlpha (0.65f));
    g.drawText (label,
                cell.getX(), cell.getY() + 2,
                cell.getWidth(), midY - cell.getY() - 2,
                juce::Justification::centred, false);

    // Value — bottom half, full brightness
    g.setFont (DysektLookAndFeel::makeFont (10.0f * sf));
    g.setColour (T.foreground);
    const int barH = juce::roundToInt (4.0f * sf);   // reserve space for progress bar
    g.drawText (juce::String (sampleVal),
                cell.getX(), midY,
                cell.getWidth(), cell.getBottom() - midY - barH,
                juce::Justification::centred, false);
}

// ─────────────────────────────────────────────────────────────────────────────
void TrimDialog::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().header);
    g.setColour (getTheme().accent.withAlpha (0.3f));
    g.drawLine (0.0f, 0.0f, (float) getWidth(), 0.0f, 1.0f);

    const int total = processor.sampleData.getNumFrames();
    const int inPt  = processor.trimRegionStart.load (std::memory_order_relaxed);
    const int outPt = processor.trimRegionEnd  .load (std::memory_order_relaxed);

    drawTrimKnob (g, inCell,  "IN",  inPt,  total);
    drawTrimKnob (g, outCell, "OUT", outPt, total);

    // ── "TRIM SAMPLE" title on the left ──────────────────────────────────────
    if (! labelArea.isEmpty())
    {
        const auto& T = getTheme();
        const auto  r = labelArea.toFloat();

        // Background + accent border
        g.setColour (T.darkBar);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (T.accent.withAlpha (0.60f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);

        // Title text — single line: "TRIM SAMPLE" centred
        const float sf = (float) getHeight() / 34.0f;
        g.setFont (DysektLookAndFeel::makeFont (11.0f * sf));
        g.setColour (T.accent);
        g.drawText ("TRIM SAMPLE", r.getX(), (int) r.getY(),
                    (int) r.getWidth(), (int) r.getHeight(),
                    juce::Justification::centred, false);
    }
}

void TrimDialog::resized()
{
    const float sf = (float) getHeight() / 34.0f;   // scale relative to nominal 34px bar height
    auto si = [sf](int v) { return juce::roundToInt ((float) v * sf); };

    auto b = getLocalBounds().reduced (si (6), si (4));

    const int btnW  = si (72);
    const int knobW = si (72);   // wide enough for 6-digit sample number
    const int gap   = si (5);

    cancelBtn.setBounds (b.removeFromRight (btnW));
    b.removeFromRight (gap);
    applyBtn.setBounds (b.removeFromRight (btnW));
    b.removeFromRight (gap * 3);

    outCell = b.removeFromRight (knobW);
    b.removeFromRight (gap);
    inCell  = b.removeFromRight (knobW);
    b.removeFromRight (gap);

    // Left-side title label — whatever space remains
    labelArea = b;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Mouse — vertical drag moves IN / OUT, shift = fine
//  Same CC: FieldSliceStart drives IN, FieldSliceEnd drives OUT (in processor)
// ─────────────────────────────────────────────────────────────────────────────
void TrimDialog::showMidiLearnMenu (int fieldId, juce::Point<int> screenPos)
{
    const bool mapped = processor.midiLearn.isMapped (fieldId);
    juce::PopupMenu menu;
    menu.addItem (1, "Learn MIDI CC");
    if (mapped)
        menu.addItem (2, "Clear (" + processor.midiLearn.getLabelText (fieldId) + ")");
    menu.addSeparator();
    menu.addItem (1000, "Open MIDI Learn Dialog...");
    auto* topLvl = getTopLevelComponent();
    float ms = DysektLookAndFeel::getMenuScale();
    menu.showMenuAsync (
        juce::PopupMenu::Options()
            .withTargetScreenArea (juce::Rectangle<int> (screenPos.x, screenPos.y, 1, 1))
            .withParentComponent (topLvl)
            .withStandardItemHeight ((int)(24 * ms)),
        [this, fieldId] (int result) {
            if (result == 1) { processor.midiLearn.armLearn (fieldId); repaint(); }
            else if (result == 2) { processor.midiLearn.clearMapping (fieldId); repaint(); }
            else if (result == 1000)
            {
                if (auto* editor = findParentComponentOfClass<DysektEditor>())
                    editor->keyPressed (juce::KeyPress ('M', juce::ModifierKeys::commandModifier, 0));
            }
        }
    );
}
void TrimDialog::mouseDown (const juce::MouseEvent& e)
{
    using F = DysektProcessor;
    if (inCell.contains (e.getPosition()))
    {
        if (e.mods.isRightButtonDown()) { showMidiLearnMenu (F::FieldSliceStart, e.getScreenPosition()); return; }
        activeDrag = 0;
    }
    else if (outCell.contains (e.getPosition()))
    {
        if (e.mods.isRightButtonDown()) { showMidiLearnMenu (F::FieldTrimOut, e.getScreenPosition()); return; }
        activeDrag = 1;
    }
    else return;
    dragStartY   = e.getPosition().y;
    dragStartVal = (activeDrag == 0)
        ? processor.trimRegionStart.load (std::memory_order_relaxed)
        : processor.trimRegionEnd  .load (std::memory_order_relaxed);
}

void TrimDialog::mouseDrag (const juce::MouseEvent& e)
{
    if (activeDrag < 0) return;

    const int total = processor.sampleData.getNumFrames();
    if (total <= 0) return;

    float sensitivity = (float) total / 300.f;
    if (e.mods.isShiftDown()) sensitivity *= 0.05f;

    const int delta = (int) ((dragStartY - e.getPosition().y) * sensitivity);

    if (activeDrag == 0)
    {
        const int curEnd = processor.trimRegionEnd.load (std::memory_order_relaxed);
        const int newIn  = juce::jlimit (0, curEnd - 64, dragStartVal + delta);
        processor.trimRegionStart.store (newIn, std::memory_order_relaxed);
        waveformView.setTrimPoints (newIn, curEnd);
    }
    else
    {
        const int curIn  = processor.trimRegionStart.load (std::memory_order_relaxed);
        const int newOut = juce::jlimit (curIn + 64, total, dragStartVal + delta);
        processor.trimRegionEnd.store (newOut, std::memory_order_relaxed);
        waveformView.setTrimPoints (curIn, newOut);
    }

    repaint();
}

void TrimDialog::mouseUp (const juce::MouseEvent&) { activeDrag = -1; }

// ─────────────────────────────────────────────────────────────────────────────
void TrimDialog::onApply()
{
    const int inPt  = processor.trimRegionStart.load (std::memory_order_relaxed);
    const int outPt = processor.trimRegionEnd  .load (std::memory_order_relaxed);
    if (waveformView.onTrimApplied)
        waveformView.onTrimApplied (inPt, outPt);
}

void TrimDialog::onCancel()
{
    if (waveformView.onTrimCancelled)
        waveformView.onTrimCancelled();
}
