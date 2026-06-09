#include "PadGridView.h"
#include "DysektLookAndFeel.h"
#include "../PluginProcessor.h"
#include "../params/ParamIds.h"
#include <juce_gui_extra/juce_gui_extra.h>

//==============================================================================
PadGridView::PadGridView (DysektProcessor& proc)
    : processor (proc)
{
    setRepaintsOnMouseActivity (false);
}

PadGridView::~PadGridView() = default;

//==============================================================================
// Bank-switcher
//==============================================================================

void PadGridView::layoutBankButtons()
{
    const int barW = getWidth();
    const int btnW = (barW - kPadPadX * 2 - 6) / 2;
    const int btnY = (kBankBarH - 20) / 2;

    bankAButtonBounds = { kPadPadX, btnY, btnW, 20 };
    bankBButtonBounds = { kPadPadX + btnW + 6, btnY, btnW, 20 };
}

void PadGridView::drawBankBar (juce::Graphics& g) const
{
    const auto& th = getTheme();

    auto barRect = juce::Rectangle<int> (0, 0, getWidth(), kBankBarH).toFloat();
    g.setColour (th.background.darker (0.08f));
    g.fillRect (barRect);

    g.setColour (th.separator.withAlpha (0.40f));
    g.drawHorizontalLine (kBankBarH - 1, 0.0f, (float) getWidth());

    auto drawBtn = [&] (juce::Rectangle<int> r, int bankIndex, const char* label)
    {
        const bool active = (currentBank == bankIndex);

        g.setColour (active ? th.accent : th.button);
        g.fillRoundedRectangle (r.toFloat(), 4.0f);

        g.setColour (active ? th.background : th.separator);
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 4.0f, 1.0f);

        g.setFont (DysektLookAndFeel::makeFont (11.0f, true));
        g.setColour (active ? th.background : th.foreground.withAlpha (0.70f));
        g.drawText (label, r, juce::Justification::centred);
    };

    drawBtn (bankAButtonBounds, 0, "Bank A");
    drawBtn (bankBButtonBounds, 1, "Bank B");
}

//==============================================================================
// Grid geometry
//==============================================================================

juce::Rectangle<int> PadGridView::cellBounds (int absIndex) const noexcept
{
    const int bankStart = currentBank * kPadsPerBank;
    const int bankEnd   = bankStart + kPadsPerBank;
    if (absIndex < bankStart || absIndex >= bankEnd) return {};

    const int localIdx = absIndex - bankStart;
    const int row = localIdx / kNumCols;
    const int col = localIdx % kNumCols;

    const int gridTop = kBankBarH + kPadPadY;
    const int gridH   = getHeight() - gridTop - kPadPadY;
    const int gridW   = getWidth()  - kPadPadX * 2;
    if (gridW <= 0 || gridH <= 0) return {};

    const int cellW = (gridW - (kNumCols - 1) * kPadGap) / kNumCols;
    const int cellH = (gridH - (kNumRows - 1) * kPadGap) / kNumRows;

    const int x = kPadPadX + col * (cellW + kPadGap);
    const int y = gridTop  + row * (cellH + kPadGap);

    return { x, y, cellW, cellH };
}

int PadGridView::padIndexAt (juce::Point<int> p) const noexcept
{
    if (p.y < kBankBarH) return -1;

    const int bankStart = currentBank * kPadsPerBank;
    for (int i = 0; i < kPadsPerBank; ++i)
    {
        const int absIdx = bankStart + i;
        const auto r = cellBounds (absIdx);
        if (! r.isEmpty() && r.contains (p))
            return absIdx;
    }
    return -1;
}

//==============================================================================
// Drawing helpers
//==============================================================================

juce::String PadGridView::midiNoteName (int note)
{
    static const char* kNames[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    const int oct = (note / 12) - 2;
    return juce::String (kNames[note % 12]) + juce::String (oct);
}

void PadGridView::drawPad (juce::Graphics& g,
                            juce::Rectangle<int> bounds,
                            int absIndex,
                            bool isEmpty) const
{
    const auto& th = getTheme();
    const auto& ui = processor.getUiSliceSnapshot();
    const bool sel = (ui.selectedSlice == absIndex);
    const bool hov = (hoveredPad == absIndex);

    // ── Background: full pad filled with the slice color ─────────────────────
    if (isEmpty)
    {
        {
            auto bgTop = th.darkBar.darker (0.45f);
            auto bgBot = th.darkBar.darker (0.65f);
            juce::ColourGradient grad (bgTop, 0, (float) bounds.getY(),
                                       bgBot, 0, (float) bounds.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (bounds.toFloat(), 4.0f);
        }
        g.setColour (th.accent.withAlpha (0.08f));
        g.drawRoundedRectangle (bounds.toFloat().expanded (1.0f), 5.0f, 1.0f);
        g.setColour (th.accent.withAlpha (0.20f));
        g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 4.0f, 1.0f);
        return;
    }

    const auto& slice  = ui.slices[(size_t) absIndex];
    const juce::Colour sliceCol = slice.colour;

    juce::Colour padBg = sliceCol.darker (sel ? 0.38f : 0.58f);
    if (hov) padBg = padBg.brighter (0.12f);

    {
        juce::ColourGradient grad (padBg.brighter (0.08f), 0, (float) bounds.getY(),
                                   padBg.darker  (0.12f), 0, (float) bounds.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bounds.toFloat(), 4.0f);
    }

    g.setColour (th.accent.withAlpha (sel ? 0.28f : 0.14f));
    g.drawRoundedRectangle (bounds.toFloat().expanded (1.0f), 5.0f, 1.0f);

    g.setColour (th.accent.withAlpha (sel ? 0.90f : (hov ? 0.55f : 0.35f)));
    g.drawRoundedRectangle (bounds.toFloat().reduced (0.5f), 4.0f, sel ? 1.5f : 1.0f);

    // ── Inner layout ──────────────────────────────────────────────────────────
    auto inner  = bounds.reduced (3, 3);
    auto topRow = inner.removeFromTop (11);
    auto waveArea = inner;

    // ── MIDI note name ────────────────────────────────────────────────────────
    {
        g.setFont (DysektLookAndFeel::makeMonoFont (7.5f));
        g.setColour (sliceCol.brighter (0.55f).withAlpha (0.90f));
        g.drawText (midiNoteName (slice.midiNote),
                    topRow.withWidth (28),
                    juce::Justification::centredLeft);
    }

    // ── Slice name ────────────────────────────────────────────────────────────
    {
        const juce::String displayName = slice.name.isNotEmpty()
            ? slice.name
            : ("Slice " + juce::String (absIndex + 1));
        g.setFont (DysektLookAndFeel::makeFont (8.5f, true));
        g.setColour (juce::Colours::white.withAlpha (sel ? 1.0f : 0.88f));
        g.drawText (displayName, topRow, juce::Justification::centred, true);
    }

    // ── Waveform ──────────────────────────────────────────────────────────────
    if (waveArea.getWidth() > 4 && waveArea.getHeight() > 4 && ui.sampleLoaded)
    {
        const int startSamp = slice.startSample;
        const int endSamp   = (absIndex >= 0 && absIndex < ui.numSlices) ? ui.sliceEndSamples[absIndex] : ui.sampleNumFrames;
        const int sliceLen  = endSamp - startSamp;

        if (sliceLen > 0)
        {
            const int W = waveArea.getWidth();
            const int H = waveArea.getHeight();
            std::vector<float> peaks ((size_t) W, 0.0f);
            for (int x = 0; x < W; ++x)
            {
                const int samp = startSamp + juce::roundToInt ((float) x / (float) W * (float) sliceLen);
                peaks[(size_t) x] = processor.getWaveformPeakAt (samp);
            }

            g.saveState();
            g.reduceClipRegion (waveArea);

            const float ox    = (float) waveArea.getX();
            const float oy    = (float) waveArea.getY();
            const float cy    = oy + (float) H * 0.5f;
            const float scale = (float) H * 0.44f;

            const juce::Colour waveCol = sliceCol.brighter (0.25f);

            auto px2x = [&] (int px) -> float { return ox + (float) px; };

            switch (waveformMode)
            {
                default:
                case 0:
                {
                    juce::Path fill, lineTop, lineBot;
                    bool first = true;
                    for (int px = 0; px < W; ++px)
                    {
                        const float amp = peaks[(size_t) px] * scale;
                        const float yT  = cy - amp;
                        const float yB  = cy + amp;
                        if (first) { lineTop.startNewSubPath (px2x (px), yT);
                                     lineBot.startNewSubPath (px2x (px), yB); first = false; }
                        else       { lineTop.lineTo (px2x (px), yT);
                                     lineBot.lineTo (px2x (px), yB); }
                    }
                    fill = lineTop;
                    for (int px = W - 1; px >= 0; --px)
                        fill.lineTo (px2x (px), cy + peaks[(size_t) px] * scale);
                    fill.closeSubPath();

                    g.setColour (waveCol.withAlpha (0.12f)); g.fillPath (fill);
                    g.setColour (waveCol.withAlpha (0.22f));
                    g.strokePath (lineTop, juce::PathStrokeType (2.5f));
                    g.strokePath (lineBot, juce::PathStrokeType (2.5f));
                    g.setColour (waveCol.withAlpha (0.85f));
                    g.strokePath (lineTop, juce::PathStrokeType (1.1f));
                    g.strokePath (lineBot, juce::PathStrokeType (1.1f));
                    break;
                }

                case 1:
                {
                    juce::Path fillPath;
                    fillPath.startNewSubPath (px2x (0), cy - peaks[0] * scale);
                    for (int px = 1; px < W; ++px)
                        fillPath.lineTo (px2x (px), cy - peaks[(size_t) px] * scale);
                    for (int px = W - 1; px >= 0; --px)
                        fillPath.lineTo (px2x (px), cy + peaks[(size_t) px] * scale);
                    fillPath.closeSubPath();
                    g.setColour (waveCol.withAlpha (0.60f));
                    g.fillPath (fillPath);
                    juce::Path topLine;
                    topLine.startNewSubPath (px2x (0), cy - peaks[0] * scale);
                    for (int px = 1; px < W; ++px)
                        topLine.lineTo (px2x (px), cy - peaks[(size_t) px] * scale);
                    g.setColour (waveCol.withAlpha (0.95f));
                    g.strokePath (topLine, juce::PathStrokeType (1.2f,
                        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    break;
                }

                case 2:
                {
                    juce::Path topPath, botPath;
                    topPath.startNewSubPath (px2x (0), cy - peaks[0] * scale);
                    botPath.startNewSubPath (px2x (0), cy + peaks[0] * scale);
                    for (int px = 1; px < W; ++px)
                    {
                        topPath.lineTo (px2x (px), cy - peaks[(size_t) px] * scale);
                        botPath.lineTo (px2x (px), cy + peaks[(size_t) px] * scale);
                    }
                    g.setColour (waveCol.withAlpha (0.25f));
                    g.strokePath (topPath, juce::PathStrokeType (2.5f,
                        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    g.strokePath (botPath, juce::PathStrokeType (2.5f,
                        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    g.setColour (waveCol.withAlpha (0.90f));
                    g.strokePath (topPath, juce::PathStrokeType (1.0f,
                        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    g.strokePath (botPath, juce::PathStrokeType (1.0f,
                        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    break;
                }

                case 3:
                {
                    const float baseline  = oy + (float) H * 0.85f;
                    const float rectScale = scale * 1.6f;
                    juce::Path rectPath;
                    rectPath.startNewSubPath (px2x (0), baseline - peaks[0] * rectScale);
                    for (int px = 1; px < W; ++px)
                        rectPath.lineTo (px2x (px), baseline - peaks[(size_t) px] * rectScale);
                    rectPath.lineTo (px2x (W - 1), baseline);
                    rectPath.lineTo (px2x (0),     baseline);
                    rectPath.closeSubPath();
                    juce::ColourGradient grad (waveCol.withAlpha (0.60f), 0.0f, oy,
                                               waveCol.withAlpha (0.05f), 0.0f, oy + (float) H, false);
                    g.setGradientFill (grad);
                    g.fillPath (rectPath);
                    juce::Path topLine;
                    topLine.startNewSubPath (px2x (0), baseline - peaks[0] * rectScale);
                    for (int px = 1; px < W; ++px)
                        topLine.lineTo (px2x (px), baseline - peaks[(size_t) px] * rectScale);
                    g.setColour (waveCol.withAlpha (0.90f));
                    g.strokePath (topLine, juce::PathStrokeType (1.1f,
                        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    break;
                }

                case 4:
                {
                    juce::Path upper, lower;
                    upper.startNewSubPath (px2x (0), cy);
                    lower.startNewSubPath (px2x (0), cy);
                    for (int px = 0; px < W; ++px)
                    {
                        upper.lineTo (px2x (px), cy - peaks[(size_t) px] * scale);
                        lower.lineTo (px2x (px), cy + peaks[(size_t) px] * scale);
                    }
                    upper.lineTo (px2x (W - 1), cy); upper.closeSubPath();
                    lower.lineTo (px2x (W - 1), cy); lower.closeSubPath();
                    g.setColour (waveCol.withAlpha (0.75f)); g.fillPath (upper);
                    g.setColour (waveCol.withAlpha (0.35f)); g.fillPath (lower);
                    juce::Path edge;
                    edge.startNewSubPath (px2x (0), cy - peaks[0] * scale);
                    for (int px = 1; px < W; ++px)
                        edge.lineTo (px2x (px), cy - peaks[(size_t) px] * scale);
                    g.setColour (waveCol.withAlpha (0.90f));
                    g.strokePath (edge, juce::PathStrokeType (1.0f,
                        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    break;
                }

                case 5:
                {
                    for (int px = 0; px < W; ++px)
                    {
                        const float amp   = peaks[(size_t) px] * scale;
                        const float alpha = 0.4f + peaks[(size_t) px] * 0.55f;
                        g.setColour (waveCol.withAlpha (alpha));
                        g.fillRect (px2x (px), cy - amp, 1.0f, juce::jmax (1.0f, amp * 2.0f));
                    }
                    break;
                }

                case 6:
                {
                    juce::Path rmsPath;
                    rmsPath.startNewSubPath (px2x (0), cy - peaks[0] * scale);
                    for (int px = 1; px < W; ++px)
                        rmsPath.lineTo (px2x (px), cy - peaks[(size_t) px] * scale);
                    for (int px = W - 1; px >= 0; --px)
                        rmsPath.lineTo (px2x (px), cy + peaks[(size_t) px] * scale);
                    rmsPath.closeSubPath();
                    juce::ColourGradient grad (waveCol.withAlpha (0.0f), 0.0f, 0.0f,
                                               waveCol.withAlpha (0.0f), 0.0f, oy + (float) H, false);
                    grad.addColour (0.35, waveCol.withAlpha (0.22f));
                    grad.addColour (0.50, waveCol.withAlpha (0.36f));
                    grad.addColour (0.65, waveCol.withAlpha (0.22f));
                    g.setGradientFill (grad);
                    g.fillPath (rmsPath);
                    juce::Path rmsLine;
                    rmsLine.startNewSubPath (px2x (0), cy - peaks[0] * scale);
                    for (int px = 1; px < W; ++px)
                        rmsLine.lineTo (px2x (px), cy - peaks[(size_t) px] * scale);
                    g.setColour (waveCol.withAlpha (0.28f));
                    g.strokePath (rmsLine, juce::PathStrokeType (3.0f,
                        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    g.setColour (waveCol.withAlpha (0.95f));
                    g.strokePath (rmsLine, juce::PathStrokeType (1.1f,
                        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    break;
                }

                case 7:
                {
                    const int stepW = juce::jmax (2, W / juce::jmax (1, W / 4));
                    juce::Path upper;
                    bool started = false;
                    float lastY  = cy;
                    for (int px = 0; px < W; px += stepW)
                    {
                        float y = cy - peaks[(size_t) px] * scale;
                        if (! started) { upper.startNewSubPath (px2x (px), y); started = true; }
                        else           { upper.lineTo (px2x (px), lastY); upper.lineTo (px2x (px), y); }
                        upper.lineTo (px2x (juce::jmin (px + stepW, W - 1)), y);
                        lastY = y;
                    }
                    juce::Path lower;
                    bool startedL = false; float lastYL = cy;
                    for (int px = 0; px < W; px += stepW)
                    {
                        float y = cy + peaks[(size_t) px] * scale;
                        if (! startedL) { lower.startNewSubPath (px2x (px), y); startedL = true; }
                        else            { lower.lineTo (px2x (px), lastYL); lower.lineTo (px2x (px), y); }
                        lower.lineTo (px2x (juce::jmin (px + stepW, W - 1)), y);
                        lastYL = y;
                    }
                    juce::Path uFill = upper;
                    uFill.lineTo (px2x (W - 1), cy); uFill.lineTo (px2x (0), cy); uFill.closeSubPath();
                    juce::Path lFill = lower;
                    lFill.lineTo (px2x (W - 1), cy); lFill.lineTo (px2x (0), cy); lFill.closeSubPath();
                    g.setColour (waveCol.withAlpha (0.70f)); g.fillPath (uFill);
                    g.setColour (waveCol.withAlpha (0.30f)); g.fillPath (lFill);
                    g.setColour (waveCol.withAlpha (0.95f));
                    g.strokePath (upper, juce::PathStrokeType (1.0f));
                    break;
                }
            }

            g.restoreState();
        }
    }
}

//==============================================================================
// Component overrides
//==============================================================================

void PadGridView::paint (juce::Graphics& g)
{
    const auto& th = getTheme();
    const auto& ui = processor.getUiSliceSnapshot();

    g.fillAll (th.background);

    drawBankBar (g);

    const int bankStart = currentBank * kPadsPerBank;
    const int bankEnd   = bankStart + kPadsPerBank;

    for (int absIdx = bankStart; absIdx < bankEnd; ++absIdx)
    {
        const bool isEmpty = (absIdx >= ui.numSlices);
        const auto r = cellBounds (absIdx);
        if (r.isEmpty()) continue;
        drawPad (g, r, absIdx, isEmpty);
    }
}

void PadGridView::resized()
{
    layoutBankButtons();
}

//==============================================================================
// Input
//==============================================================================

void PadGridView::mouseDown (const juce::MouseEvent& e)
{
    const juce::Point<int> pt = e.getPosition();

    if (pt.y < kBankBarH)
    {
        if (bankAButtonBounds.contains (pt) && currentBank != 0)
        {
            currentBank = 0;
            hoveredPad  = -1;
            repaint();
        }
        else if (bankBButtonBounds.contains (pt) && currentBank != 1)
        {
            currentBank = 1;
            hoveredPad  = -1;
            repaint();
        }
        return;
    }

    const int idx = padIndexAt (pt);
    if (idx < 0) return;

    const auto& ui = processor.getUiSliceSnapshot();
    if (idx >= ui.numSlices) return;

    // ── Right-click: context menu ─────────────────────────────────────────────
    if (e.mods.isRightButtonDown())
    {
        DysektProcessor::Command selCmd;
        selCmd.type      = DysektProcessor::CmdSelectSlice;
        selCmd.intParam1 = idx;
        processor.pushCommand (selCmd);
        repaint();

        showPadContextMenu (idx, { e.getScreenX(), e.getScreenY() });
        return;
    }

    // ── Left-click: select + play ─────────────────────────────────────────────
    DysektProcessor::Command cmd;
    cmd.type      = DysektProcessor::CmdSelectSlice;
    cmd.intParam1 = idx;
    processor.pushCommand (cmd);

    const int midiNote = processor.sliceManager.getSlice (idx).midiNote;
    processor.uiNoteOnRequest.store (midiNote, std::memory_order_relaxed);

    repaint();
}

void PadGridView::mouseUp (const juce::MouseEvent& e)
{
    const juce::Point<int> pt = e.getPosition();
    if (pt.y < kBankBarH) return;

    const int idx = padIndexAt (pt);
    if (idx < 0) return;

    const auto& ui = processor.getUiSliceSnapshot();
    if (idx >= ui.numSlices) return;

    const int midiNote = processor.sliceManager.getSlice (idx).midiNote;
    processor.uiNoteOffRequest.store (midiNote, std::memory_order_relaxed);
}

void PadGridView::mouseMove (const juce::MouseEvent& e)
{
    const int idx = (e.getPosition().y < kBankBarH) ? -1
                                                     : padIndexAt (e.getPosition());
    if (idx != hoveredPad)
    {
        hoveredPad = idx;
        repaint();
    }
}

void PadGridView::mouseExit (const juce::MouseEvent&)
{
    if (hoveredPad != -1)
    {
        hoveredPad = -1;
        repaint();
    }
}

//==============================================================================
// Pad right-click context menu
//==============================================================================

class PadGridView::MenuSliderItem : public juce::PopupMenu::CustomComponent
{
public:
    juce::String              label;
    float                     value    { 0.f };
    float                     minVal   { 0.f };
    float                     maxVal   { 1.f };
    float                     resetVal { 0.f };
    std::function<juce::String(float)> formatValue;
    std::function<void(float)>         onChange;

    MenuSliderItem (const juce::String& lbl,
                    float cur, float lo, float hi, float def,
                    std::function<juce::String(float)> fmt,
                    std::function<void(float)> cb)
        : juce::PopupMenu::CustomComponent (false),
          label (lbl), value (cur), minVal (lo), maxVal (hi), resetVal (def),
          formatValue (std::move (fmt)), onChange (std::move (cb))
    {
        setSize (220, 36);
    }

    void getIdealSize (int& w, int& h) override { w = 220; h = 36; }

    void paint (juce::Graphics& g) override
    {
        const auto& th = getTheme();
        const auto b   = getLocalBounds().toFloat().reduced (2.f, 2.f);
        const float norm = (value - minVal) / (maxVal - minVal);

        g.setColour (th.darkBar.darker (0.2f));
        g.fillRoundedRectangle (b, 3.f);

        const bool bipolar = (minVal < 0.f && maxVal > 0.f);
        const float cx = b.getX() + b.getWidth() * (bipolar ? (-minVal / (maxVal - minVal)) : 0.f);
        const float fx = b.getX() + b.getWidth() * norm;

        juce::Rectangle<float> fill;
        if (bipolar)
            fill = { juce::jmin (cx, fx), b.getY(), std::abs (fx - cx), b.getHeight() };
        else
            fill = { b.getX(), b.getY(), fx - b.getX(), b.getHeight() };

        g.setColour (th.accent.withAlpha (isItemHighlighted() ? 0.55f : 0.38f));
        g.fillRoundedRectangle (fill, 3.f);

        if (bipolar)
        {
            g.setColour (th.separator.withAlpha (0.6f));
            g.drawLine (cx, b.getY() + 3.f, cx, b.getBottom() - 3.f, 1.f);
        }

        g.setColour (th.separator.withAlpha (0.5f));
        g.drawRoundedRectangle (b, 3.f, 0.8f);

        g.setFont (DysektLookAndFeel::makeFont (10.5f, true));
        g.setColour (th.foreground.withAlpha (0.80f));
        g.drawText (label, b.reduced (7.f, 0.f).withWidth (60.f),
                    juce::Justification::centredLeft);

        g.setFont (DysektLookAndFeel::makeFont (10.5f, false));
        g.setColour (th.foreground);
        g.drawText (formatValue (value), b.reduced (7.f, 0.f),
                    juce::Justification::centredRight);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.getNumberOfClicks() == 2)
        {
            value = resetVal;
            if (onChange) onChange (value);
            repaint();
            return;
        }
        dragStart    = e.position.x;
        dragStartVal = value;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float range   = maxVal - minVal;
        const float pxRange = (float) getWidth() - 14.f;
        const float delta   = (e.position.x - dragStart) / pxRange * range;
        value = juce::jlimit (minVal, maxVal, dragStartVal + delta);
        if (onChange) onChange (value);
        repaint();
    }

private:
    float dragStart    { 0.f };
    float dragStartVal { 0.f };
};

//==============================================================================

void PadGridView::showPadContextMenu (int idx, juce::Point<int> screenPos)
{
    const auto& snap = processor.getUiSliceSnapshot();
    if (idx < 0 || idx >= snap.numSlices) return;
    const auto& slice = snap.slices[(size_t) idx];

    float ms     = DysektLookAndFeel::getMenuScale();
    int   itemH  = (int) (24 * ms);
    int   sliderH = (int) (36 * ms);
    auto* topLvl = getTopLevelComponent();

    const float effVolume = (slice.lockMask & kLockVolume)
        ? slice.volume
        : processor.apvts.getRawParameterValue (ParamIds::masterVolume)->load();
    const float effPitch  = (slice.lockMask & kLockPitch)
        ? slice.pitchSemitones
        : processor.apvts.getRawParameterValue (ParamIds::defaultPitch)->load();
    const float effPan    = (slice.lockMask & kLockPan)
        ? slice.pan
        : processor.apvts.getRawParameterValue (ParamIds::defaultPan)->load();

    auto makeVolLabel = [] (float v) -> juce::String
    {
        if (v <= -99.f) return "-inf";
        return (v >= 0.f ? "+" : "") + juce::String (v, 1) + " dB";
    };
    auto* volSlider = new MenuSliderItem (
        "VOL", effVolume, -100.f, 24.f, 0.f, makeVolLabel,
        [this] (float v)
        {
            DysektProcessor::Command cmd;
            cmd.type      = DysektProcessor::CmdSetSliceParam;
            cmd.intParam1 = DysektProcessor::FieldVolume;
            cmd.intParam2 = 0;
            cmd.floatParam1 = v;
            processor.pushCommand (cmd);
        });

    auto makePitchLabel = [] (float v) -> juce::String
    {
        return (v >= 0.f ? "+" : "") + juce::String (v, 2) + " st";
    };
    auto* pitchSlider = new MenuSliderItem (
        "PITCH", effPitch, -48.f, 48.f, 0.f, makePitchLabel,
        [this] (float v)
        {
            DysektProcessor::Command cmd;
            cmd.type      = DysektProcessor::CmdSetSliceParam;
            cmd.intParam1 = DysektProcessor::FieldPitch;
            cmd.intParam2 = 0;
            cmd.floatParam1 = v;
            processor.pushCommand (cmd);
        });

    auto makePanLabel = [] (float v) -> juce::String
    {
        if (std::abs (v) < 0.01f) return "C";
        const int pct = (int) std::round (std::abs (v) * 100.f);
        return juce::String (pct) + (v < 0.f ? "L" : "R");
    };
    auto* panSlider = new MenuSliderItem (
        "PAN", effPan, -1.f, 1.f, 0.f, makePanLabel,
        [this] (float v)
        {
            DysektProcessor::Command cmd;
            cmd.type      = DysektProcessor::CmdSetSliceParam;
            cmd.intParam1 = DysektProcessor::FieldPan;
            cmd.intParam2 = 0;
            cmd.floatParam1 = v;
            processor.pushCommand (cmd);
        });

    juce::PopupMenu muteMenu;
    {
        const int curMute = (slice.lockMask & kLockMuteGroup)
            ? slice.muteGroup
            : (int) processor.apvts.getRawParameterValue (ParamIds::defaultMuteGroup)->load();
        muteMenu.addItem (400, "Off (0)", true, curMute == 0);
        for (int g = 1; g <= 16; ++g)
            muteMenu.addItem (400 + g, "Group " + juce::String (g), true, curMute == g);
    }

    juce::PopupMenu outMenu;
    {
        const int curOut = (slice.lockMask & kLockOutputBus) ? slice.outputBus : 0;
        outMenu.addItem (500, "Main (0)", true, curOut == 0);
        for (int o = 1; o <= 31; ++o)
            outMenu.addItem (500 + o, "Output " + juce::String (o + 1), true, curOut == o);
    }

    juce::PopupMenu menu;
    menu.addItem (1, "Rename Slice...");
    menu.addItem (3, "Pad Color...");
    menu.addSeparator();
    menu.addCustomItem (-1, std::unique_ptr<MenuSliderItem> (volSlider),   nullptr, "Volume");
    menu.addCustomItem (-1, std::unique_ptr<MenuSliderItem> (pitchSlider), nullptr, "Pitch");
    menu.addCustomItem (-1, std::unique_ptr<MenuSliderItem> (panSlider),   nullptr, "Pan");
    menu.addSeparator();
    menu.addSubMenu ("Mute Group", muteMenu);
    menu.addSubMenu ("Output",     outMenu);
    menu.addSeparator();
    menu.addItem (2, "Delete Slice");

    const juce::Rectangle<int> padScreenBounds =
        cellBounds (idx) + getScreenPosition() - getPosition();

    juce::ignoreUnused (itemH, sliderH);

    menu.showMenuAsync (
        juce::PopupMenu::Options()
            .withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 })
            .withParentComponent (topLvl)
            .withStandardItemHeight (itemH),
        [this, idx, padScreenBounds] (int result)
        {
            if (result == 0) return;

            if (result == 1)
            {
                const auto& snap2 = processor.getUiSliceSnapshot();
                if (idx < snap2.numSlices && onRenameRequest)
                    onRenameRequest (idx, snap2.slices[(size_t) idx].name);
                return;
            }

            if (result == 2)
            {
                DysektProcessor::Command cmd;
                cmd.type      = DysektProcessor::CmdDeleteSlice;
                cmd.intParam1 = idx;
                processor.pushCommand (cmd);
                repaint();
                return;
            }

            if (result == 3)
            {
                launchColorPicker (idx, padScreenBounds);
                return;
            }

            if (result >= 400 && result < 500)
            {
                DysektProcessor::Command cmd;
                cmd.type        = DysektProcessor::CmdSetSliceParam;
                cmd.intParam1   = DysektProcessor::FieldMuteGroup;
                cmd.intParam2   = 0;
                cmd.floatParam1 = (float) (result - 400);
                processor.pushCommand (cmd);
                return;
            }

            if (result >= 500 && result < 600)
            {
                DysektProcessor::Command cmd;
                cmd.type        = DysektProcessor::CmdSetSliceParam;
                cmd.intParam1   = DysektProcessor::FieldOutputBus;
                cmd.intParam2   = 0;
                cmd.floatParam1 = (float) (result - 500);
                processor.pushCommand (cmd);
                return;
            }
        });
}

//------------------------------------------------------------------------------

void PadGridView::launchColorPicker (int idx, juce::Rectangle<int> padScreenBounds)
{
    const auto& snap = processor.getUiSliceSnapshot();
    if (idx < 0 || idx >= snap.numSlices) return;

    const juce::Colour current = snap.slices[(size_t) idx].colour;

    auto* selector = new juce::ColourSelector (
        juce::ColourSelector::showColourAtTop |
        juce::ColourSelector::showSliders     |
        juce::ColourSelector::showColourspace, 4, 8);

    selector->setName ("padColor");
    selector->setCurrentColour (current);
    selector->setSize (280, 320);

    static const juce::uint32 kPal[32] = {
        0xFFD82626, 0xFFF45F3D, 0xFFAD541E, 0xFFF28D0C,
        0xFFE0BC51, 0xFFC1B60A, 0xFFC2D826, 0xFFBBF43D,
        0xFF66AD1E, 0xFF54F20C, 0xFF63E051, 0xFF0AC115, 0xFF26D852,
        0xFF3DF48D, 0xFF1EAD77, 0xFF0CF2C7, 0xFF51E0E0, 0xFF0A9FC1,
        0xFF2695D8, 0xFF3D8DF4, 0xFF1E42AD, 0xFF0C1BF2,
        0xFF6351E0, 0xFF430AC1, 0xFF7F26D8, 0xFFBB3DF4, 0xFF9B1EAD,
        0xFFF20CE3, 0xFFE051BC, 0xFFC10A71, 0xFFD82669, 0xFFF43D5F,
    };
    const int numSwatches = selector->getNumSwatches();
    for (int i = 0; i < juce::jmin (numSwatches, 32); ++i)
        selector->setSwatchColour (i, juce::Colour (kPal[i]));

    struct ColourListener : public juce::ChangeListener
    {
        DysektProcessor& proc;
        int sliceIdx;
        ColourListener (DysektProcessor& p, int i) : proc (p), sliceIdx (i) {}
        void changeListenerCallback (juce::ChangeBroadcaster* src) override
        {
            if (auto* cs = dynamic_cast<juce::ColourSelector*> (src))
            {
                DysektProcessor::Command cmd;
                cmd.type      = DysektProcessor::CmdSetSliceColour;
                cmd.intParam1 = sliceIdx;
                cmd.intParam2 = (int)(unsigned) cs->getCurrentColour().withAlpha (1.0f).getARGB();
                proc.pushCommand (cmd);
            }
        }
    };

    auto* listener = new ColourListener (processor, idx);
    selector->addChangeListener (listener);

    struct ListenerDeleter : public juce::Component
    {
        juce::ChangeListener*    target;
        juce::ChangeBroadcaster* broadcaster;
        ListenerDeleter (juce::ChangeListener* t, juce::ChangeBroadcaster* b)
            : target (t), broadcaster (b) {}
        ~ListenerDeleter() override
        {
            broadcaster->removeChangeListener (target);
            delete target;
        }
    };
    auto* deleter = new ListenerDeleter (listener, selector);
    deleter->setSize (0, 0);
    selector->addAndMakeVisible (deleter);

    juce::CallOutBox::launchAsynchronously (
        std::unique_ptr<juce::ColourSelector> (selector),
        padScreenBounds, nullptr);
}

//==============================================================================
// FIX: repaintGrid() – only auto-switch banks when selectedSlice actually
//      changes to a different value.  Without this guard the timer was
//      re-evaluating on every tick and resetting currentBank back to 0
//      (Bank A) the moment after the user clicked the Bank B button,
//      because selectedSlice was still pointing at a Bank-A pad.
//==============================================================================
void PadGridView::repaintGrid()
{
    const int activePad = processor.sliceManager.selectedSlice
        .load (std::memory_order_relaxed);

    // Only auto-switch banks when the selected slice actually changes.
    // This prevents the timer from overriding a manual Bank A / Bank B
    // button press on every subsequent tick.
    if (activePad != lastAutoSwitchSlice)
    {
        lastAutoSwitchSlice = activePad;

        if (activePad >= 0 && activePad < kMaxPads)
        {
            const int sliceBank = activePad / kPadsPerBank;
            if (sliceBank != currentBank)
            {
                currentBank = sliceBank;
                hoveredPad  = -1;
            }
        }
    }

    repaint();
}
