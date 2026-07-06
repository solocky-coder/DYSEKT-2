#include "DysektLookAndFeel.h"
#include "BinaryData.h"

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

    // ── NO glow halo — crisp borders only ────────────────────────────────────

    // ── Flat fill — minimal gradient, OpenDAW-style ───────────────────────────
    auto fillCol = isDown        ? baseBg.brighter (0.20f)
                 : isHighlighted ? baseBg.brighter (0.10f)
                 : toggled       ? baseBg.interpolatedWith (getTheme().accent, 0.18f)
                                 : baseBg;

    if (getTheme().name == "serum")
    {
        // Metallic steel gradient — lighter top edge, darker mid, slight lift at bottom
        auto top    = fillCol.brighter (0.18f);
        auto mid    = fillCol.darker   (0.08f);
        auto bot    = fillCol.brighter (0.06f);
        juce::ColourGradient grad (top, bounds.getX(), bounds.getY(),
                                   mid, bounds.getX(), bounds.getBottom(), false);
        grad.addColour (0.60, bot);
        g.setGradientFill (grad);
    }
    else
    {
        g.setColour (fillCol);
    }
    g.fillRoundedRectangle (bounds, r);

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
                                      bool /*isButtonDown*/, int buttonX, int /*buttonY*/,
                                      int /*buttonW*/, int /*buttonH*/, juce::ComboBox& box)
{
    const auto& t = getTheme();
    const float cbR = 2.0f;   // Midnight — tight radius
    auto cbBounds = juce::Rectangle<float> (0, 0, (float)width, (float)height).reduced (0.5f);

    // Flat fill
    g.setColour (t.button);
    g.fillRoundedRectangle (cbBounds, cbR);

    // Border — accent when focused, separator otherwise
    g.setColour (box.hasKeyboardFocus (false) ? t.accent : t.separator);
    g.drawRoundedRectangle (cbBounds, cbR, 1.0f);

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
                                       bool /*isMouseOver*/,
                                       bool /*isMouseDown*/)
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

        // Pill thumb — accent color, no glow
        auto thumbF = thumb.toFloat().reduced (1.0f);
        const float thumbR = isScrollbarVertical ? (float)(thumb.getWidth() - 2) * 0.5f
                                                 : (float)(thumb.getHeight() - 2) * 0.5f;
        g.setColour (t.accent.withAlpha (0.55f));
        g.fillRoundedRectangle (thumbF, thumbR);
        g.setColour (t.accent.withAlpha (0.35f));
        g.drawRoundedRectangle (thumbF, thumbR, 1.0f);
    }
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
//  DocumentWindow title bar — themed to match DYSEKT's dark palette
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
    // A simple flat button that draws an X, –, or □ in DYSEKT's colours.
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
