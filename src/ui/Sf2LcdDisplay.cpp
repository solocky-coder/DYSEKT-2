#include "Sf2LcdDisplay.h"
#include "DysektLookAndFeel.h"
#include "LcdColours.h"
#include "../PluginProcessor.h"

// Palette and chassis rendering now live in LcdColours.h, shared with
// SliceLcdDisplay.cpp and SfzLcdDisplay.cpp — see that header for details.
// Do NOT re-declare a local copy of the LcdColours namespace here.

// ── Static formatters ─────────────────────────────────────────────────────────

juce::String Sf2LcdDisplay::formatMs (float secs)
{
    const float ms = secs * 1000.0f;
    if (ms < 10.0f)   return juce::String (ms, 1) + "ms";
    if (ms < 1000.0f) return juce::String (juce::roundToInt (ms)) + "ms";
    return juce::String (secs, 2) + "s ";
}

juce::String Sf2LcdDisplay::formatPct (float v)
{
    return juce::String (juce::roundToInt (v)) + "%";
}

// ── Constructor ───────────────────────────────────────────────────────────────

Sf2LcdDisplay::Sf2LcdDisplay (DysektProcessor& p)
    : processor (p)
{
    setOpaque (true);
}

void Sf2LcdDisplay::resized() {}

void Sf2LcdDisplay::repaintLcd()
{
    repaint();
}

// ── Layout ────────────────────────────────────────────────────────────────────

int Sf2LcdDisplay::effectiveRowH() const noexcept
{
    auto      b      = getLocalBounds().reduced (4);
    const int availH = b.getHeight() - 4;
    return std::max (8, availH / kTotalRows);
}

// ── Data building ─────────────────────────────────────────────────────────────

void Sf2LcdDisplay::buildDisplayData()
{
    data = {};

    data.loaded = processor.sfzPlayer.isLoaded();
    if (! data.loaded)
        return;

    const juce::File f = processor.sfzPlayer.getLoadedFile();
    data.fileName = f.getFileNameWithoutExtension();
    data.filePath = f.getParentDirectory().getFullPathName();

    // Preset info: prefer whatever the user last selected in the program grid
    // (preview or per-channel-FX-edit click); fall back to the engine's default
    // index (0) before anything's been touched.
    const auto presets  = processor.sfzPlayer.getPresetList();
    const int  gridIdx  = processor.sfzPlayer.getDisplayPresetIndex();
    const int  idx      = gridIdx >= 0 ? gridIdx : processor.sfzPlayer.getCurrentPresetIndex();
    if (idx >= 0 && idx < (int) presets.size())
    {
        data.bankNumber   = presets[(size_t) idx].bank;
        data.presetNumber = presets[(size_t) idx].preset;
        data.presetName   = presets[(size_t) idx].name.toUpperCase().substring (0, 14);
    }

    // Volume: linear gain → dB
    const float gainLinear = processor.sfzPlayer.getVolume();
    data.volume = gainLinear > 0.0f
                      ? 20.0f * std::log10 (gainLinear)
                      : -96.0f;

    data.transpose  = processor.sfzPlayer.getTranspose();

    // ADSR — same atomic API as sfzPlayer2
    data.attackSec  = processor.sfzPlayer.getSfzAttack();
    data.decaySec   = processor.sfzPlayer.getSfzDecay();
    data.sustainPct = processor.sfzPlayer.getSfzSustain();
    data.releaseSec = processor.sfzPlayer.getSfzRelease();

    // Reverb
    data.reverbSize = processor.sfzPlayer.getReverbSize();
    data.reverbDamp = processor.sfzPlayer.getReverbDamp();

    data.fileNameUpperShort = data.fileName.toUpperCase().substring (0, 18);
}

// ── Draw helpers ──────────────────────────────────────────────────────────────

void Sf2LcdDisplay::drawLcdBackground (juce::Graphics& g)
{
    LcdColours::drawChassisAndScreen (g, getLocalBounds(), kScanlineAlpha);
}

void Sf2LcdDisplay::drawRow (juce::Graphics& g, int row,
                              const juce::String& label,
                              const juce::String& value,
                              bool highlight)
{
    const auto  pal  = LcdColours::fromTheme();
    const auto  b    = getLocalBounds().reduced (4);
    const float sf   = (float) getHeight() / (float) kPreferredHeight;
    const int   rowH = effectiveRowH();
    const int   lPad = juce::roundToInt (kLeftPad * sf);
    const int   y    = b.getY() + 4 + row * rowH;

    if (y + rowH <= b.getY() + 4 || y >= b.getBottom() - 4) return;

    const juce::Font labelFont = DysektLookAndFeel::makeFont (24.0f * sf, true);
    const juce::Font valueFont = DysektLookAndFeel::makeFont (26.0f * sf);

    if (highlight)
    {
        g.setColour (pal.phosphor.withAlpha (0.10f));
        g.fillRect (b.getX(), y, b.getWidth(), rowH - 1);
    }

    const int lx = b.getX() + lPad;
    g.setFont (labelFont);
    g.setColour (highlight ? pal.highlight : pal.labelCol);
    g.drawText (label, lx, y, b.getWidth() / 3, rowH, juce::Justification::centredLeft, false);

    const int vx = lx + juce::GlyphArrangement::getStringWidthInt (labelFont, label) + 6;
    g.setFont (valueFont);
    g.setColour (highlight ? pal.highlight : pal.phosphor);
    g.drawText (value, vx, y, b.getRight() - vx - lPad, rowH, juce::Justification::centredLeft, false);
}

void Sf2LcdDisplay::drawRowPair (juce::Graphics& g, int row,
                                  const juce::String& leftStr,
                                  const juce::String& rightStr,
                                  bool highlight)
{
    const auto  pal  = LcdColours::fromTheme();
    const auto  b    = getLocalBounds().reduced (4);
    const float sf   = (float) getHeight() / (float) kPreferredHeight;
    const int   rowH = effectiveRowH();
    const int   lPad = juce::roundToInt (kLeftPad * sf);
    const int   y    = b.getY() + 4 + row * rowH;
    const juce::Font f = DysektLookAndFeel::makeFont (23.0f * sf);

    if (y + rowH <= b.getY() + 4 || y >= b.getBottom() - 4) return;

    if (highlight)
    {
        g.setColour (pal.phosphor.withAlpha (0.10f));
        g.fillRect (b.getX(), y, b.getWidth(), rowH - 1);
    }

    g.setFont (f);

    auto drawItem = [&] (int x, int w, const juce::String& str)
    {
        const int colonPos = str.indexOfChar (':');
        if (colonPos > 0)
        {
            const juce::String lbl  = str.substring (0, colonPos + 1);
            const juce::String val  = str.substring (colonPos + 1);
            const int          lblW = juce::GlyphArrangement::getStringWidthInt (f, lbl);
            g.setColour (highlight ? pal.highlight : pal.labelCol);
            g.drawText (lbl, x, y, lblW + 2, rowH, juce::Justification::centredLeft, false);
            g.setColour (highlight ? pal.highlight : pal.phosphor);
            g.drawText (val, x + lblW + 2, y, w - lblW - 2, rowH, juce::Justification::centredLeft, false);
        }
        else
        {
            g.setColour (highlight ? pal.highlight : pal.phosphor);
            g.drawText (str, x, y, w, rowH, juce::Justification::centredLeft, false);
        }
    };

    const int leftX = b.getX() + lPad;
    const int leftW = b.getWidth() / 2 - lPad - 4;
    drawItem (leftX, leftW, leftStr);

    const int rightX = b.getX() + (b.getWidth() * 52 / 100);
    const int rightW = b.getRight() - rightX - lPad;
    drawItem (rightX, rightW, rightStr);
}

void Sf2LcdDisplay::drawNoInstrument (juce::Graphics& g)
{
    const auto  pal = LcdColours::fromTheme();
    const auto  b   = getLocalBounds().reduced (4);
    const float sf  = (float) getHeight() / (float) kPreferredHeight;

    g.setFont (DysektLookAndFeel::makeFont (22.0f * sf));
    g.setColour (pal.noDataCol);
    g.drawText ("-- NO INSTRUMENT --", b, juce::Justification::centred);

    g.setFont (DysektLookAndFeel::makeFont (18.0f * sf));
    g.setColour (pal.dim);
    g.drawText ("DROP AN SF2 FILE",
                b, juce::Justification::centredBottom);
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void Sf2LcdDisplay::paint (juce::Graphics& g)
{
    g.fillAll (getTheme().background);

    {
        juce::Path clipPath;
        clipPath.addRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
        g.reduceClipRegion (clipPath);
    }

    buildDisplayData();
    drawLcdBackground (g);

    if (! data.loaded)
    {
        drawNoInstrument (g);
        return;
    }

    const auto  pal    = LcdColours::fromTheme();
    const float sf     = (float) getHeight() / (float) kPreferredHeight;
    const int   rowH   = effectiveRowH();
    const auto  screen = getLocalBounds().reduced (4);

    g.saveState();
    g.reduceClipRegion (screen);

    // ── Row 0: header — "SF2 PLAYER" label + instrument filename ─────────────
    {
        const juce::String tagStr  = "SF2 PLAYER";
        const juce::String nameStr = data.fileNameUpperShort;
        const int y = screen.getY() + 4;

        g.setColour (pal.phosphor.withAlpha (0.10f));
        g.fillRect (screen.getX(), y, screen.getWidth(), rowH - 1);

        const juce::Font lblF = DysektLookAndFeel::makeFont (24.0f * sf, true);
        const juce::Font valF = DysektLookAndFeel::makeFont (26.0f * sf);

        const int lblW  = juce::GlyphArrangement::getStringWidthInt (lblF, tagStr);
        const int gap   = 8;
        const int valW  = juce::GlyphArrangement::getStringWidthInt (valF, nameStr);
        const int total = lblW + gap + valW;
        const int startX = screen.getX() + (screen.getWidth() - total) / 2;

        g.setFont (lblF);
        g.setColour (pal.highlight);
        g.drawText (tagStr, startX, y, lblW + 2, rowH, juce::Justification::centredLeft, false);

        g.setFont (valF);
        g.setColour (pal.highlight);
        g.drawText (nameStr, startX + lblW + gap, y, valW + 2, rowH, juce::Justification::centredLeft, false);
    }

    // ── Row 1: file path ──────────────────────────────────────────────────────
    drawRow (g, 1, "PATH:", data.filePath.substring (0, 28));

    // ── Row 2: bank + preset name ─────────────────────────────────────────────
    {
        const juce::String bankStr   = "BNK:" + juce::String (data.bankNumber);
        const juce::String presetStr = "PRE:" + juce::String (data.presetNumber)
                                       + " " + data.presetName;
        drawRowPair (g, 2, bankStr, presetStr);
    }

    // ── Row 3: VOL + TRANSPOSE ────────────────────────────────────────────────
    {
        const float v = data.volume;
        juce::String volStr = "VOL:" + juce::String (v >= 0.0f ? "+" : "")
                              + juce::String (v, 1) + "dB";
        const int    tr     = data.transpose;
        juce::String trStr  = "TRNS:" + juce::String (tr >= 0 ? "+" : "")
                              + juce::String (tr) + "st";
        drawRowPair (g, 3, volStr, trStr);
    }

    // ── Row 4: A + D ──────────────────────────────────────────────────────────
    {
        drawRowPair (g, 4,
                     "A:" + formatMs (data.attackSec).trimEnd(),
                     "D:" + formatMs (data.decaySec).trimEnd());
    }

    // ── Row 5: S + R ──────────────────────────────────────────────────────────
    {
        drawRowPair (g, 5,
                     "S:" + formatPct (data.sustainPct),
                     "R:" + formatMs (data.releaseSec).trimEnd());
    }

    // ── Row 6: RV SIZE + RV DAMP ─────────────────────────────────────────────
    {
        drawRowPair (g, 6,
                     "RV SZ:"  + formatPct (data.reverbSize),
                     "RV DMP:" + formatPct (data.reverbDamp));
    }

    // ── Row 7: status ─────────────────────────────────────────────────────────
    drawRow (g, 7, "STATUS:", "LOADED");

    g.restoreState();
}
