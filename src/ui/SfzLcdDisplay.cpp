#include "SfzLcdDisplay.h"
#include "DysektLookAndFeel.h"
#include "../PluginProcessor.h"

// ── Theme-derived palette (mirrors SliceLcdDisplay / LcdColours) ──────────────
namespace SfzLcdColours
{
    struct Palette
    {
        juce::Colour background, bezel, phosphor, dim, highlight,
                     labelCol, scanline, noDataCol;
    };

    static Palette fromTheme()
    {
        const auto& t  = getTheme();
        const auto  ac = t.accent;
        const auto  bg = t.darkBar.darker (0.55f);

        Palette p;
        p.background = bg;
        p.bezel      = bg.brighter (0.12f);
        p.phosphor   = ac;
        p.dim        = ac.withAlpha (0.18f).withMultipliedSaturation (0.6f).overlaidWith (bg);
        p.highlight  = ac.brighter (0.5f);
        p.labelCol   = ac.withAlpha (0.70f);
        p.scanline   = juce::Colours::black;
        p.noDataCol  = ac.withAlpha (0.15f).overlaidWith (bg);
        return p;
    }
}

// ── Static formatters ─────────────────────────────────────────────────────────

juce::String SfzLcdDisplay::formatMs (float secs)
{
    const float ms = secs * 1000.0f;
    if (ms < 10.0f)   return juce::String (ms, 1) + "ms";
    if (ms < 1000.0f) return juce::String (juce::roundToInt (ms)) + "ms";
    return juce::String (secs, 2) + "s ";
}

juce::String SfzLcdDisplay::formatPct (float v)
{
    return juce::String (juce::roundToInt (v)) + "%";
}

// ── Constructor ───────────────────────────────────────────────────────────────

SfzLcdDisplay::SfzLcdDisplay (DysektProcessor& p)
    : processor (p)
{
    setOpaque (true);
}

void SfzLcdDisplay::resized() {}

void SfzLcdDisplay::repaintLcd()
{
    repaint();
}

// ── Layout ────────────────────────────────────────────────────────────────────

int SfzLcdDisplay::effectiveRowH() const noexcept
{
    const float sf    = (float) getHeight() / (float) kPreferredHeight;
    auto        b     = getLocalBounds().reduced (4);
    const int   availH = b.getHeight() - 4;
    return std::max (8, availH / kTotalRows);
}

// ── Data building ─────────────────────────────────────────────────────────────

void SfzLcdDisplay::buildDisplayData()
{
    data = {};

    data.loaded = processor.sfzPlayer.isLoaded();
    if (! data.loaded)
        return;

    const juce::File f = processor.sfzPlayer.getLoadedFile();
    data.fileName  = f.getFileNameWithoutExtension();
    data.filePath  = f.getParentDirectory().getFullPathName();

    // Volume: sfzPlayer returns linear gain; convert to dB
    const float gainLinear = processor.sfzPlayer.getVolume();
    data.volume = gainLinear > 0.0f
                      ? 20.0f * std::log10 (gainLinear)
                      : -96.0f;

    data.transpose   = processor.sfzPlayer.getTranspose();

    // ADSR — sfzPlayer stores in seconds (ATK/DEC/REL) and percent (SUS)
    data.attackSec   = processor.sfzPlayer.getSfzAttack();
    data.decaySec    = processor.sfzPlayer.getSfzDecay();
    data.sustainPct  = processor.sfzPlayer.getSfzSustain();   // already 0-100
    data.releaseSec  = processor.sfzPlayer.getSfzRelease();

    // Reverb (0–100 each)
    data.reverbSize  = processor.sfzPlayer.getReverbSize();
    data.reverbDamp  = processor.sfzPlayer.getReverbDamp();
    data.reverbWidth = processor.sfzPlayer.getReverbWidth();
    data.reverbMix   = processor.sfzPlayer.getReverbMix();

    // Pre-compute upper-case string so paint() never allocates heap memory.
    data.fileNameUpperShort = data.fileName.toUpperCase().substring (0, 18);
}

// ── Draw helpers ──────────────────────────────────────────────────────────────

void SfzLcdDisplay::drawLcdBackground (juce::Graphics& g)
{
    const auto pal = SfzLcdColours::fromTheme();
    const auto ac  = getTheme().accent;
    const auto b   = getLocalBounds();

    // Outer frame
    juce::ColourGradient outerGrad (juce::Colour (0xFF131313), 0, 0,
                                    juce::Colour (0xFF0E0E0E), 0, (float) b.getHeight(), false);
    g.setGradientFill (outerGrad);
    g.fillRoundedRectangle (b.toFloat(), 4.0f);
    g.setColour (ac.withAlpha (0.18f));
    g.drawRoundedRectangle (b.toFloat().expanded (1.0f), 5.0f, 1.0f);
    g.setColour (ac.withAlpha (0.60f));
    g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 4.0f, 1.5f);

    // Inner screen
    const auto screen = b.reduced (4);
    g.setColour (pal.background);
    g.fillRoundedRectangle (screen.toFloat(), 2.0f);

    g.setColour (pal.scanline.withAlpha ((uint8_t) kScanlineAlpha));
    for (int y = screen.getY(); y < screen.getBottom(); y += 2)
        g.drawHorizontalLine (y, (float) screen.getX(), (float) screen.getRight());

    juce::ColourGradient glow (pal.phosphor.withAlpha (0.06f), 0, (float) screen.getY(),
                                juce::Colours::transparentBlack, 0, (float) (screen.getY() + 20), false);
    g.setGradientFill (glow);
    g.fillRoundedRectangle (screen.toFloat(), 2.0f);

    g.setColour (ac.withAlpha (0.12f));
    g.drawRoundedRectangle (screen.toFloat().expanded (0.5f), 2.0f, 1.0f);
}

void SfzLcdDisplay::drawRow (juce::Graphics& g, int row,
                              const juce::String& label,
                              const juce::String& value,
                              bool highlight)
{
    const auto  pal    = SfzLcdColours::fromTheme();
    const auto  b      = getLocalBounds().reduced (4);
    const float sf     = (float) getHeight() / (float) kPreferredHeight;
    const int   rowH   = effectiveRowH();
    const int   lPad   = juce::roundToInt (kLeftPad * sf);
    const int   y      = b.getY() + 4 + row * rowH;

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

    const int vx = lx + juce::GlyphArrangement::getStringWidthInt(labelFont, label) + 6;
    g.setFont (valueFont);
    g.setColour (highlight ? pal.highlight : pal.phosphor);
    g.drawText (value, vx, y, b.getRight() - vx - lPad, rowH, juce::Justification::centredLeft, false);
}

void SfzLcdDisplay::drawRowPair (juce::Graphics& g, int row,
                                  const juce::String& leftStr,
                                  const juce::String& rightStr,
                                  bool highlight)
{
    const auto  pal   = SfzLcdColours::fromTheme();
    const auto  b     = getLocalBounds().reduced (4);
    const float sf    = (float) getHeight() / (float) kPreferredHeight;
    const int   rowH  = effectiveRowH();
    const int   lPad  = juce::roundToInt (kLeftPad * sf);
    const int   y     = b.getY() + 4 + row * rowH;
    const juce::Font f = DysektLookAndFeel::makeFont (23.0f * sf);

    if (y + rowH <= b.getY() + 4 || y >= b.getBottom() - 4) return;

    if (highlight)
    {
        g.setColour (pal.phosphor.withAlpha (0.10f));
        g.fillRect (b.getX(), y, b.getWidth(), rowH - 1);
    }

    g.setFont (f);

    // ── Left item ─────────────────────────────────────────────────────────────
    const int leftX = b.getX() + lPad;
    const int leftW = b.getWidth() / 2 - lPad - 4;

    auto drawItem = [&] (int x, int w, const juce::String& str)
    {
        const int colonPos = str.indexOfChar (':');
        if (colonPos > 0)
        {
            const juce::String lbl = str.substring (0, colonPos + 1);
            const juce::String val = str.substring (colonPos + 1);
            const int lblW = juce::GlyphArrangement::getStringWidthInt(f, lbl);
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

    drawItem (leftX, leftW, leftStr);

    // ── Right item ────────────────────────────────────────────────────────────
    const int rightX = b.getX() + (b.getWidth() * 52 / 100);
    const int rightW = b.getRight() - rightX - lPad;
    drawItem (rightX, rightW, rightStr);
}

void SfzLcdDisplay::drawNoInstrument (juce::Graphics& g)
{
    const auto  pal = SfzLcdColours::fromTheme();
    const auto  b   = getLocalBounds().reduced (4);
    const float sf  = (float) getHeight() / (float) kPreferredHeight;

    g.setFont (DysektLookAndFeel::makeFont (22.0f * sf));
    g.setColour (pal.noDataCol);
    g.drawText ("-- NO INSTRUMENT --", b, juce::Justification::centred);

    g.setFont (DysektLookAndFeel::makeFont (18.0f * sf));
    g.setColour (pal.dim);
    g.drawText ("DROP AN SFZ OR SF2 FILE",
                b, juce::Justification::centredBottom);
}

// ── Paint ─────────────────────────────────────────────────────────────────────

void SfzLcdDisplay::paint (juce::Graphics& g)
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

    const auto  pal    = SfzLcdColours::fromTheme();
    const float sf     = (float) getHeight() / (float) kPreferredHeight;
    const int   rowH   = effectiveRowH();
    const auto  screen = getLocalBounds().reduced (4);

    g.saveState();
    g.reduceClipRegion (screen);

    // ── Row 0: header — "SF PLAYER" label + instrument filename ──────────────
    {
        const juce::String tagStr  = "SF PLAYER";
        const juce::String nameStr = data.fileNameUpperShort;
        const int y = screen.getY() + 4;

        g.setColour (pal.phosphor.withAlpha (0.10f));
        g.fillRect (screen.getX(), y, screen.getWidth(), rowH - 1);

        const juce::Font lblF = DysektLookAndFeel::makeFont (24.0f * sf, true);
        const juce::Font valF = DysektLookAndFeel::makeFont (26.0f * sf);

        const int lblW  = juce::GlyphArrangement::getStringWidthInt(lblF, tagStr);
        const int gap   = 8;
        const int valW  = juce::GlyphArrangement::getStringWidthInt(valF, nameStr);
        const int total = lblW + gap + valW;
        const int startX = screen.getX() + (screen.getWidth() - total) / 2;

        g.setFont (lblF);
        g.setColour (pal.highlight);
        g.drawText (tagStr, startX, y, lblW + 2, rowH, juce::Justification::centredLeft, false);

        g.setFont (valF);
        g.setColour (pal.highlight);
        g.drawText (nameStr, startX + lblW + gap, y, valW + 2, rowH, juce::Justification::centredLeft, false);
    }

    // ── Row 1: file path (truncated, right-justified) ─────────────────────────
    {
        const juce::String pathStr = data.filePath;
        drawRow (g, 1, "PATH:", pathStr.substring (0, 28));
    }

    // ── Row 2: VOL + TRANSPOSE ────────────────────────────────────────────────
    {
        const float v = data.volume;
        juce::String volStr  = juce::String ("VOL:") + (v >= 0.0f ? "+" : "") + juce::String (v, 1) + "dB";
        const int    tr = data.transpose;
        juce::String trStr   = juce::String ("TRNS:") + (tr >= 0 ? "+" : "") + juce::String (tr) + "st";
        drawRowPair (g, 2, volStr, trStr);
    }

    // ── Row 3: A + D ──────────────────────────────────────────────────────────
    {
        juce::String atkStr = "A:" + formatMs (data.attackSec).trimEnd();
        juce::String decStr = "D:" + formatMs (data.decaySec).trimEnd();
        drawRowPair (g, 3, atkStr, decStr);
    }

    // ── Row 4: S + R ──────────────────────────────────────────────────────────
    {
        juce::String susStr = "S:" + formatPct (data.sustainPct);
        juce::String relStr = "R:" + formatMs (data.releaseSec).trimEnd();
        drawRowPair (g, 4, susStr, relStr);
    }

    // ── Row 5: RV SIZE + RV DAMP ─────────────────────────────────────────────
    {
        juce::String szStr  = "RV SZ:"  + formatPct (data.reverbSize);
        juce::String dmpStr = "RV DMP:" + formatPct (data.reverbDamp);
        drawRowPair (g, 5, szStr, dmpStr);
    }

    // ── Row 6: RV WIDTH + RV MIX ─────────────────────────────────────────────
    {
        juce::String widStr = "RV WID:" + formatPct (data.reverbWidth);
        juce::String mixStr = "RV MIX:" + formatPct (data.reverbMix);
        drawRowPair (g, 6, widStr, mixStr);
    }

    // ── Row 7: status ─────────────────────────────────────────────────────────
    {
        drawRow (g, 7, "STATUS:", "LOADED", false);
    }

    g.restoreState();
}
