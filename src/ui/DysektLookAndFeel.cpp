#include "DysektLookAndFeel.h"
#include "BinaryData.h"
#include "IconManager.h"

static ThemeData globalTheme = ThemeData::opendawTheme();

ThemeData& getTheme() { return globalTheme; }
void setTheme (const ThemeData& t) { globalTheme = t; }

juce::Typeface::Ptr DysektLookAndFeel::sRegularTypeface;
juce::Typeface::Ptr DysektLookAndFeel::sBoldTypeface;
juce::Typeface::Ptr DysektLookAndFeel::sMonoTypeface;
juce::Typeface::Ptr DysektLookAndFeel::sMonoBoldTypeface;
float DysektLookAndFeel::sMenuScale = 1.0f;

DysektLookAndFeel::DysektLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, getTheme().background);

    // Labels / UI text — Barlow Condensed (sharp, narrow, technical)
    regularTypeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::BarlowCondensedRegular_ttf, BinaryData::BarlowCondensedRegular_ttfSize);
    boldTypeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::BarlowCondensedSemiBold_ttf, BinaryData::BarlowCondensedSemiBold_ttfSize);

    // Values / numbers — JetBrains Mono (monospaced, digits never jump width)
    monoTypeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize);
    monoBoldTypeface = juce::Typeface::createSystemTypefaceFor (
        BinaryData::JetBrainsMonoBold_ttf, BinaryData::JetBrainsMonoBold_ttfSize);

    sRegularTypeface  = regularTypeface;
    sBoldTypeface     = boldTypeface;
    sMonoTypeface     = monoTypeface;
    sMonoBoldTypeface = monoBoldTypeface;
}

juce::Font DysektLookAndFeel::makeFont (float pointSize, bool bold)
{
    auto tf = bold ? sBoldTypeface : sRegularTypeface;
    if (tf != nullptr)
        return juce::Font (juce::FontOptions().withTypeface (tf).withPointHeight (pointSize));
    return juce::Font (juce::FontOptions().withHeight (pointSize));
}

juce::Font DysektLookAndFeel::makeMonoFont (float pointSize, bool bold)
{
    auto tf = bold ? sMonoBoldTypeface : sMonoTypeface;
    if (tf != nullptr)
        return juce::Font (juce::FontOptions().withTypeface (tf).withPointHeight (pointSize));
    return makeFont (pointSize, bold);
}

juce::Typeface::Ptr DysektLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    // Check both the bold flag and the typeface style string — macOS can request
    // bold via style name ("Bold", "SemiBold") rather than the isBold() flag,
    // which would otherwise fall through to a system typeface (Helvetica, etc.)
    auto style = f.getTypefaceStyle().toLowerCase();
    if (f.isBold() || style.contains ("bold") || style.contains ("semi"))
        return boldTypeface;
    return regularTypeface;
}

// ── Buttons ───────────────────────────────────────────────────────────────────
// Direction D — Midnight: 2px radius · no glow · flat fill · crisp 1px border
void DysektLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                               const juce::Colour& /*bgColour*/,
                                               bool isHighlighted, bool isDown)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
    const float r = 2.0f;   // sharp — Midnight direction

    auto btnCol = button.findColour (juce::TextButton::buttonColourId);
    auto baseBg = (btnCol != juce::Colour()) ? btnCol : getTheme().button;

    if (baseBg.isTransparent())
        return;

    const bool toggled = button.getToggleState();

    // ── Modern asset base layer ────────────────────────────────────────────
    // Draw the idle/hover/active sprite from IconManager as the button base.
    // NOTE: these ship as flat concept art without per-theme tinting yet —
    // see PRODUCTION_READINESS.md ("Theme tinting strategy") for the follow-up
    // work needed to recolour them per active ThemeData. For now they render
    // as-is and we still draw the theme-coloured border/text on top so
    // toggled/locked state remains legible in every theme.
    auto fillCol = isDown        ? baseBg.brighter (0.20f)
                 : isHighlighted ? baseBg.brighter (0.10f)
                 : toggled       ? baseBg.interpolatedWith (getTheme().accent, 0.18f)
                                 : baseBg;

    auto stateDrawable = isDown        ? IconManager::getButtonActive()
                       : isHighlighted ? IconManager::getButtonHover()
                                       : IconManager::getButtonIdle();

    if (stateDrawable != nullptr)
    {
        stateDrawable->drawWithin (g, bounds, juce::RectanglePlacement::stretchToFit, 1.0f);
    }
    else if (getTheme().name == "serum")
    {
        // Metallic steel gradient — lighter top edge, darker mid, slight lift at bottom
        auto top    = fillCol.brighter (0.18f);
        auto mid    = fillCol.darker   (0.08f);
        auto bot    = fillCol.brighter (0.06f);
        juce::ColourGradient grad (top, bounds.getX(), bounds.getY(),
                                   mid, bounds.getX(), bounds.getBottom(), false);
        grad.addColour (0.60, bot);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (bounds, r);
    }
    else
    {
        g.setColour (fillCol);
        g.fillRoundedRectangle (bounds, r);
    }

    // ── Border — accent on active, subtle otherwise ───────────────────────────
    auto borderCol = toggled      ? getTheme().accent.withAlpha (0.70f)
                   : isHighlighted ? getTheme().separator.brighter (0.30f)
                                   : getTheme().separator.withAlpha (0.60f);
    g.setColour (borderCol);
    g.drawRoundedRectangle (bounds, r, 1.0f);
}

void DysektLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button,
                                         bool /*isHighlighted*/, bool /*isDown*/)
{
    auto textCol = button.findColour (button.getToggleState()
                                       ? juce::TextButton::textColourOnId
                                       : juce::TextButton::textColourOffId);
    if (textCol.isTransparent())
        textCol = button.getToggleState()
                ? getTheme().accent
                : getTheme().foreground.withAlpha (0.85f);

    g.setColour (textCol);

    const int h = button.getHeight();
    // Symbol-only button (loop): single non-ASCII char — render larger with system font
    const juce::String txt = button.getButtonText();
    const bool isSymbol = (txt.length() <= 2 && txt[0] > 127);
    float fontSize = isSymbol ? (h < 22 ? 14.0f : 17.0f)
                   : h < 16 ? 8.0f
                   : h < 22 ? 10.0f
                   : h < 28 ? 11.0f
                   : 13.0f;
    g.setFont (isSymbol ? juce::Font (fontSize) : makeFont (fontSize));
    g.drawText (button.getButtonText(), button.getLocalBounds().reduced (2, 0),
                juce::Justification::centred);
}

// ── Popup menus ───────────────────────────────────────────────────────────────
void DysektLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    const float r = 3.0f;   // tighter radius — Midnight direction
    auto bounds = juce::Rectangle<float> (0, 0, (float)width, (float)height);
    const auto bgColour = getTheme().darkBar.brighter (0.06f);

    // Fill entire rect first — eliminates white OS window corners behind rounded shape
    g.fillAll (bgColour);

    // Flat panel for most themes; metallic gradient for serum
    if (getTheme().name == "serum")
    {
        juce::ColourGradient grad (bgColour.brighter (0.15f), 0, 0,
                                   bgColour.darker   (0.08f), 0, (float) height, false);
        grad.addColour (0.55, bgColour);
        g.setGradientFill (grad);
    }
    else
    {
        g.setColour (bgColour);
    }
    g.fillRoundedRectangle (bounds, r);

    // Outer border — accent tint, full opacity
    g.setColour (getTheme().separator.withAlpha (1.0f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), r, 1.0f);

    // Top accent line — 1px, subtle
    g.setColour (getTheme().accent.withAlpha (0.22f));
    g.fillRect (bounds.reduced (r, 0).removeFromTop (1.0f));
}

void DysektLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                                            bool isSeparator, bool isActive, bool isHighlighted,
                                            bool isTicked, bool /*hasSubMenu*/,
                                            const juce::String& text, const juce::String& /*shortcutText*/,
                                            const juce::Drawable* /*icon*/, const juce::Colour* /*textColour*/)
{
    // Solid background first
    g.setColour (getTheme().darkBar.brighter (0.06f));
    g.fillRect (area);

    if (isSeparator)
    {
        g.setColour (getTheme().separator);
        g.fillRect (area.reduced (4, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    if (isHighlighted && isActive)
    {
        // Flat highlight — no gradient, just a crisp fill
        g.setColour (getTheme().buttonHover);
        g.fillRoundedRectangle (area.reduced (3, 1).toFloat(), 2.0f);

        // Accent left edge — 2px solid bar
        g.setColour (getTheme().accent);
        g.fillRect (juce::Rectangle<float> ((float)area.getX() + 3, (float)area.getY() + 3,
                                             2.0f, (float)area.getHeight() - 6));
    }

    const int tickZoneW = (int) (16 * sMenuScale);
    if (isTicked)
    {
        const int dotSize = (int) (4 * sMenuScale);
        g.setColour (getTheme().accent);
        g.fillRect (area.getX() + (tickZoneW - dotSize) / 2,
                    area.getCentreY() - dotSize / 2,
                    dotSize, dotSize);
    }

    const juce::Colour textCol = isTicked ? getTheme().accent
                               : isActive ? getTheme().foreground
                                          : getTheme().foreground.withAlpha (0.4f);
    g.setColour (textCol);
    g.setFont (getPopupMenuFont());

    auto textArea = area.withLeft (area.getX() + tickZoneW)
                        .withRight (area.getRight() - (int) (4 * sMenuScale));
    g.drawText (text, textArea, juce::Justification::centredLeft);
}

void DysektLookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g,
                                                     const juce::Rectangle<int>& area,
                                                     const juce::String& sectionName)
{
    g.setFont (makeFont (14.0f * sMenuScale, true));
    g.setColour (getTheme().foreground);
    g.drawFittedText (sectionName,
                      area.getX() + (int) (12 * sMenuScale), area.getY(),
                      area.getWidth() - (int) (16 * sMenuScale),
                      (int) ((float) area.getHeight() * 0.8f),
                      juce::Justification::bottomLeft, 1);
}

juce::Font DysektLookAndFeel::getPopupMenuFont()
{
    return makeFont (14.0f * sMenuScale);
}

// ── ComboBox ──────────────────────────────────────────────────────────────────
void DysektLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height,
                                      bool isButtonDown, int buttonX, int /*buttonY*/,
                                      int /*buttonW*/, int /*buttonH*/, juce::ComboBox& box)
{
    const auto& t = getTheme();
    const float cbR = 2.0f;   // Midnight — tight radius
    auto cbBounds = juce::Rectangle<float> (0, 0, (float)width, (float)height).reduced (0.5f);

    // ── Gradient body — subtle vertical bevel instead of a flat fill ──────
    // Lighter top edge fading to the base colour then a faint lift near the
    // bottom, matching the button/knob art's raised-surface language.
    auto baseCol = isButtonDown ? t.button.darker (0.10f) : t.button;
    auto topCol  = baseCol.brighter (0.14f);
    auto midCol  = baseCol.darker   (0.05f);
    auto botCol  = baseCol.brighter (0.04f);

    juce::ColourGradient grad (topCol, cbBounds.getX(), cbBounds.getY(),
                               midCol, cbBounds.getX(), cbBounds.getBottom(), false);
    grad.addColour (0.65, botCol);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (cbBounds, cbR);

    // Inner top highlight — thin 1px sheen just inside the top edge
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawLine (cbBounds.getX() + cbR, cbBounds.getY() + 1.0f,
                cbBounds.getRight() - cbR, cbBounds.getY() + 1.0f, 1.0f);

    // Border — accent when focused, separator otherwise
    const bool focused = box.hasKeyboardFocus (false);
    g.setColour (focused ? t.accent : t.separator);
    g.drawRoundedRectangle (cbBounds, cbR, 1.0f);

    if (focused)
    {
        // Soft accent glow around the focused border
        g.setColour (t.accent.withAlpha (0.18f));
        g.drawRoundedRectangle (cbBounds.expanded (0.75f), cbR + 0.75f, 1.5f);
    }

    // Dropdown arrow — clean chevron
    const int arrowCX = buttonX + (width - buttonX) / 2;
    const int arrowCY = height / 2;
    const int arrowHalf = 4;
    g.setColour (t.foreground.withAlpha (0.85f));
    g.drawLine ((float)(arrowCX - arrowHalf), (float)(arrowCY - 2),
                (float)(arrowCX),              (float)(arrowCY + 2), 1.5f);
    g.drawLine ((float)(arrowCX),              (float)(arrowCY + 2),
                (float)(arrowCX + arrowHalf),  (float)(arrowCY - 2), 1.5f);
}

juce::Font DysektLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return makeFont (14.0f * sMenuScale);
}

void DysektLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (1, 1, box.getWidth() - 28, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setColour (juce::Label::textColourId, getTheme().foreground);
    label.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
}

// Scroll arrows — filled triangles, no unicode
void DysektLookAndFeel::drawPopupMenuUpDownArrow (juce::Graphics& g, int width, int height,
                                                  bool isScrollUpArrow)
{
    g.setColour (getTheme().foreground.withAlpha (0.6f));
    const float cx = (float) width * 0.5f;
    const float cy = (float) height * 0.5f;
    const float sz = 4.0f;
    juce::Path arrow;
    if (isScrollUpArrow)
        arrow.addTriangle (cx - sz, cy + sz * 0.5f, cx + sz, cy + sz * 0.5f, cx, cy - sz * 0.5f);
    else
        arrow.addTriangle (cx - sz, cy - sz * 0.5f, cx + sz, cy - sz * 0.5f, cx, cy + sz * 0.5f);
    g.fillPath (arrow);
}

// ── Tooltip ───────────────────────────────────────────────────────────────────
void DysektLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const float r = 2.0f;   // Midnight — tight
    auto bounds = juce::Rectangle<float> (0, 0, (float)width, (float)height);

    // Solid flat background
    g.setColour (getTheme().darkBar.brighter (0.10f));
    g.fillRoundedRectangle (bounds, r);

    // 1px border — accent tint
    g.setColour (getTheme().accent.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), r, 1.0f);

    g.setColour (getTheme().foreground.withAlpha (0.95f));
    g.setFont (makeFont (13.0f));
    g.drawText (text, 6, 0, width - 12, height, juce::Justification::centredLeft);
}

juce::Rectangle<int> DysektLookAndFeel::getTooltipBounds (const juce::String& text,
                                                            juce::Point<int> screenPos,
                                                            juce::Rectangle<int> parentArea)
{
    int w = (int) juce::GlyphArrangement::getStringWidth(makeFont (14.0f), text) + 14;
    int h = 24;
    int x = screenPos.x;
    int y = screenPos.y + 18;

    if (x + w > parentArea.getRight())
        x = parentArea.getRight() - w;
    if (y + h > parentArea.getBottom())
        y = screenPos.y - h - 4;

    return { x, y, w, h };
}

// ── Scrollbar ─────────────────────────────────────────────────────────────────
void DysektLookAndFeel::drawScrollbar (juce::Graphics& g,
                                       juce::ScrollBar& /*scrollbar*/,
                                       int x, int y, int width, int height,
                                       bool isScrollbarVertical,
                                       int thumbStartPosition,
                                       int thumbSize,
                                       bool isMouseOver,
                                       bool isMouseDown)
{
    const auto& t = getTheme();

    // Track — flat, dark
    g.setColour (t.darkBar.darker (0.30f));
    g.fillRect (x, y, width, height);
    g.setColour (t.separator);
    g.drawRect (x, y, width, height, 1);

    if (thumbSize > 0)
    {
        juce::Rectangle<int> thumb;
        if (isScrollbarVertical)
            thumb = { x + 1, y + thumbStartPosition, width - 2, thumbSize };
        else
            thumb = { x + thumbStartPosition, y + 1, thumbSize, height - 2 };

        // Pill thumb — layered gradient so it reads as a raised control,
        // matching the button/knob/slider bevel language, with a brighter
        // fill + glow when hovered/dragged.
        auto thumbF = thumb.toFloat().reduced (1.0f);
        const float thumbR = isScrollbarVertical ? (float)(thumb.getWidth() - 2) * 0.5f
                                                 : (float)(thumb.getHeight() - 2) * 0.5f;

        const float baseAlpha = isMouseDown ? 0.85f : isMouseOver ? 0.70f : 0.55f;
        auto accentBase = t.accent.withAlpha (baseAlpha);

        if (isMouseOver || isMouseDown)
        {
            // Subtle glow behind the thumb on interaction
            g.setColour (t.accent.withAlpha (isMouseDown ? 0.22f : 0.14f));
            g.fillRoundedRectangle (thumbF.expanded (1.5f), thumbR + 1.5f);
        }

        juce::ColourGradient thumbGrad (accentBase.brighter (0.25f),
                                        thumbF.getX(), thumbF.getY(),
                                        accentBase.darker (0.10f),
                                        isScrollbarVertical ? thumbF.getX() + thumbF.getWidth()
                                                             : thumbF.getX(),
                                        isScrollbarVertical ? thumbF.getY()
                                                             : thumbF.getY() + thumbF.getHeight(),
                                        false);
        g.setGradientFill (thumbGrad);
        g.fillRoundedRectangle (thumbF, thumbR);

        g.setColour (t.accent.withAlpha (0.40f));
        g.drawRoundedRectangle (thumbF, thumbR, 1.0f);
    }
}

// ── Linear slider ─────────────────────────────────────────────────────────────
// Used by the file-browser preview volume slider (LinearHorizontal). Gives it
// the same recessed-track / raised-thumb bevel language as the buttons and
// knob art rather than falling back to stock LookAndFeel_V4 styling.
void DysektLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float minSliderPos, float maxSliderPos,
                                          const juce::Slider::SliderStyle style, juce::Slider& slider)
{
    const auto& t = getTheme();
    auto bounds = juce::Rectangle<float> ((float)x, (float)y, (float)width, (float)height);

    if (style == juce::Slider::LinearVertical)
    {
        // Not currently used anywhere in the plugin, but keep a sane vertical
        // fallback to stock drawing rather than mis-rendering a horizontal bar.
        juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos,
                                                minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const float trackH = juce::jmin (6.0f, bounds.getHeight() * 0.5f);
    auto track = bounds.withSizeKeepingCentre (bounds.getWidth(), trackH);
    const float trackR = trackH * 0.5f;

    // ── Recessed track — inner shadow via a dark-to-slightly-lighter gradient
    auto trackTop = t.darkBar.darker (0.35f);
    auto trackBot = t.darkBar.brighter (0.04f);
    juce::ColourGradient trackGrad (trackTop, track.getX(), track.getY(),
                                    trackBot, track.getX(), track.getBottom(), false);
    g.setGradientFill (trackGrad);
    g.fillRoundedRectangle (track, trackR);
    g.setColour (t.separator.withAlpha (0.7f));
    g.drawRoundedRectangle (track, trackR, 1.0f);

    // ── Filled portion — accent gradient with a soft glow, from the track's
    // left edge up to the thumb position (matches bipolar/unipolar feel used
    // elsewhere in the plugin's LCD-style sliders).
    const float fillLeft  = track.getX() + trackR;
    const float fillRight = juce::jlimit (fillLeft, track.getRight() - trackR, sliderPos);
    if (fillRight > fillLeft)
    {
        auto fillArea = juce::Rectangle<float> (fillLeft, track.getY(), fillRight - fillLeft, track.getHeight());

        g.setColour (t.accent.withAlpha (0.20f));
        g.fillRoundedRectangle (fillArea.expanded (0.0f, 1.5f), trackR);

        juce::ColourGradient fillGrad (t.accent.brighter (0.20f), fillArea.getX(), fillArea.getY(),
                                       t.accent.darker (0.10f), fillArea.getX(), fillArea.getBottom(), false);
        g.setGradientFill (fillGrad);
        g.fillRoundedRectangle (fillArea, trackR);
    }

    // ── Thumb — raised bevel knob, brighter when dragged/hovered ──────────
    const float thumbD = juce::jmin (bounds.getHeight(), 16.0f);
    auto thumb = juce::Rectangle<float> (thumbD, thumbD).withCentre ({ sliderPos, bounds.getCentreY() });

    const bool isDown  = slider.isMouseButtonDown();
    const bool isOver  = slider.isMouseOverOrDragging();
    auto thumbBase = t.button.brighter (isDown ? 0.30f : isOver ? 0.18f : 0.10f);

    if (isOver || isDown)
    {
        g.setColour (t.accent.withAlpha (isDown ? 0.30f : 0.18f));
        g.fillEllipse (thumb.expanded (2.0f));
    }

    juce::ColourGradient thumbGrad (thumbBase.brighter (0.25f), thumb.getX(), thumb.getY(),
                                    thumbBase.darker (0.15f), thumb.getX(), thumb.getBottom(), false);
    g.setGradientFill (thumbGrad);
    g.fillEllipse (thumb);

    g.setColour (t.accent.withAlpha (0.75f));
    g.drawEllipse (thumb, 1.2f);

    // Tiny top-left specular highlight for a glassy, raised feel
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.fillEllipse (thumb.reduced (thumb.getWidth() * 0.32f).translated (-thumb.getWidth() * 0.10f, -thumb.getHeight() * 0.12f));
}

// ── Rotary slider ─────────────────────────────────────────────────────────────
// Only reached if a real rotary juce::Slider is ever instantiated — current
// knob strips in SliceControlBar are hand-painted cells. Mirrors drawLinearSlider:
// recessed track arc, accent fill arc with soft glow, glassy raised thumb dot.
void DysektLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                          float sliderPosProportional, float rotaryStartAngle,
                                          float rotaryEndAngle, juce::Slider& slider)
{
    const auto& t = getTheme();
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (2.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    const float lineW = juce::jmin (4.0f, radius * 0.22f);
    const float arcR  = radius - lineW * 0.5f;

    // ── Recessed track arc — same dark-to-slightly-lighter gradient as the track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (t.darkBar.darker (0.35f));
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (t.separator.withAlpha (0.5f));
    g.strokePath (track, juce::PathStrokeType (1.0f));

    // ── Filled arc — accent, with the same soft glow used by the linear slider's fill
    if (angle > rotaryStartAngle)
    {
        juce::Path fill;
        fill.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, rotaryStartAngle, angle, true);

        g.setColour (t.accent.withAlpha (0.20f));
        g.strokePath (fill, juce::PathStrokeType (lineW + 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setColour (t.accent);
        g.strokePath (fill, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // ── Glassy raised thumb dot at the pointer tip ─────────────────────────
    const bool isDown = slider.isMouseButtonDown();
    const bool isOver = slider.isMouseOverOrDragging();
    auto thumbBase = t.button.brighter (isDown ? 0.30f : isOver ? 0.18f : 0.10f);
    const float thumbD = juce::jmin (14.0f, radius * 0.5f);
    juce::Point<float> tip (centre.x + std::sin (angle) * arcR, centre.y - std::cos (angle) * arcR);
    auto thumb = juce::Rectangle<float> (thumbD, thumbD).withCentre (tip);

    if (isOver || isDown)
    {
        g.setColour (t.accent.withAlpha (isDown ? 0.30f : 0.18f));
        g.fillEllipse (thumb.expanded (2.0f));
    }

    juce::ColourGradient thumbGrad (thumbBase.brighter (0.25f), thumb.getX(), thumb.getY(),
                                    thumbBase.darker (0.15f), thumb.getX(), thumb.getBottom(), false);
    g.setGradientFill (thumbGrad);
    g.fillEllipse (thumb);
    g.setColour (t.accent.withAlpha (0.75f));
    g.drawEllipse (thumb, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.fillEllipse (thumb.reduced (thumb.getWidth() * 0.32f).translated (-thumb.getWidth() * 0.10f, -thumb.getHeight() * 0.12f));
}

// ── TextEditor ────────────────────────────────────────────────────────────────
// Flat fill + 2px-radius outline, matching drawButtonBackground's "Midnight"
// direction, instead of LookAndFeel_V4's default inset chrome.
void DysektLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& te)
{
    auto bg = te.findColour (juce::TextEditor::backgroundColourId);
    if (bg.isTransparent())
        bg = getTheme().darkBar.brighter (0.04f);

    auto bounds = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
    g.setColour (bg);
    g.fillRoundedRectangle (bounds, 2.0f);
}

void DysektLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& te)
{
    if (te.isReadOnly())
        return;

    auto bounds = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
    const bool focused = te.hasKeyboardFocus (true);
    g.setColour (focused ? getTheme().accent.withAlpha (0.70f)
                          : getTheme().separator.withAlpha (0.60f));
    g.drawRoundedRectangle (bounds, 2.0f, focused ? 1.4f : 1.0f);
}

//==============================================================================
// AlertWindow overrides — make every SF-player popup readable
//==============================================================================
juce::Font DysektLookAndFeel::getAlertWindowTitleFont()
{
    return makeFont (16.0f * sMenuScale, true);
}

juce::Font DysektLookAndFeel::getAlertWindowMessageFont()
{
    return makeFont (14.0f * sMenuScale);
}

int DysektLookAndFeel::getAlertWindowButtonHeight()
{
    return juce::roundToInt (28.0f * sMenuScale);
}

void DysektLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert,
                                      const juce::Rectangle<int>& textArea,
                                      juce::TextLayout& textLayout)
{
    const auto& t = getTheme();

    // Background
    g.setColour (t.darkBar);
    g.fillRoundedRectangle (alert.getLocalBounds().toFloat(), 4.0f);

    // Accent border
    g.setColour (t.accent.withAlpha (0.6f));
    g.drawRoundedRectangle (alert.getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);

    // Title strip
    const auto titleArea = juce::Rectangle<int> (0, 0, alert.getWidth(), textArea.getY());
    g.setColour (t.accent.withAlpha (0.15f));
    g.fillRoundedRectangle (titleArea.toFloat(), 4.0f);

    g.setColour (t.foreground);
    g.setFont (getAlertWindowTitleFont());
    g.drawFittedText (alert.getName(),
                      titleArea.reduced (8, 4),
                      juce::Justification::centredLeft, 1);

    // Message body
    textLayout.draw (g, textArea.toFloat());
}

//==============================================================================
//  DocumentWindow title bar — themed to match DYSEKT-SF's dark palette
//==============================================================================

void DysektLookAndFeel::drawDocumentWindowTitleBar (
    juce::DocumentWindow& window, juce::Graphics& g,
    int w, int h, int titleSpaceX, int titleSpaceW,
    const juce::Image* /*icon*/, bool /*drawTitleTextOnLeft*/)
{
    const auto& t = getTheme();

    // Title bar background
    g.setColour (t.header);
    g.fillRect (0, 0, w, h);

    // Thin accent line along the bottom edge
    g.setColour (t.accent.withAlpha (0.35f));
    g.fillRect (0, h - 1, w, 1);

    // Title text
    g.setColour (t.foreground);
    g.setFont (makeFont (13.f, false));
    g.drawFittedText (window.getName(),
                      titleSpaceX + 4, 0, titleSpaceW - 8, h,
                      juce::Justification::centredLeft, 1);
}

//==============================================================================
//  Themed close / minimise / maximise buttons for DocumentWindow
//==============================================================================

namespace
{
    // A simple flat button that draws an X, –, or □ in DYSEKT-SF's colours.
    class DysektTitleBarButton : public juce::Button
    {
    public:
        enum class Kind { Close, Minimise, Maximise };

        DysektTitleBarButton (Kind k) : juce::Button ({}), kind (k) {}

        void paintButton (juce::Graphics& g, bool isHighlighted, bool isDown) override
        {
            const auto& t = getTheme();
            const auto  b = getLocalBounds().toFloat().reduced (2.f);

            // Hover / press fill
            if (isDown)
                g.setColour (kind == Kind::Close ? juce::Colour (0xFFBB3333)
                                                 : t.buttonHover);
            else if (isHighlighted)
                g.setColour (kind == Kind::Close ? juce::Colour (0xFF883333)
                                                 : t.button);
            else
                g.setColour (juce::Colours::transparentBlack);

            g.fillRoundedRectangle (b, 3.f);

            // Icon
            g.setColour (isHighlighted ? t.foreground : t.foreground.withAlpha (0.6f));
            const float cx = b.getCentreX(), cy = b.getCentreY();
            const float r  = b.getHeight() * 0.22f;

            if (kind == Kind::Close)
            {
                g.drawLine (cx - r, cy - r, cx + r, cy + r, 1.5f);
                g.drawLine (cx + r, cy - r, cx - r, cy + r, 1.5f);
            }
            else if (kind == Kind::Minimise)
            {
                g.drawLine (cx - r, cy, cx + r, cy, 1.5f);
            }
            else  // Maximise
            {
                g.drawRect (juce::Rectangle<float> (cx - r, cy - r, r * 2.f, r * 2.f), 1.5f);
            }
        }

    private:
        Kind kind;
    };
}

juce::Button* DysektLookAndFeel::createDocumentWindowButton (int buttonType)
{
    using Kind = DysektTitleBarButton::Kind;
    if (buttonType == juce::DocumentWindow::closeButton)
        return new DysektTitleBarButton (Kind::Close);
    if (buttonType == juce::DocumentWindow::minimiseButton)
        return new DysektTitleBarButton (Kind::Minimise);
    return new DysektTitleBarButton (Kind::Maximise);
}
