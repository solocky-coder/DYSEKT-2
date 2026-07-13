#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ThemeData.h"

ThemeData& getTheme();
void setTheme (const ThemeData& t);

class DysektLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DysektLookAndFeel();

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                               bool isHighlighted, bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool hasSubMenu,
                            const juce::String& text, const juce::String& shortcutText,
                            const juce::Drawable* icon, const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>& area,
                                     const juce::String& sectionName) override;
    juce::Font getPopupMenuFont() override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    void drawPopupMenuUpDownArrow (juce::Graphics&, int width, int height,
                                   bool isScrollUpArrow) override;

    static void setMenuScale (float s) { sMenuScale = s; }
    static float getMenuScale() { return sMenuScale; }

    void drawScrollbar (juce::Graphics&, juce::ScrollBar&,
                        int x, int y, int width, int height,
                        bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize,
                        bool isMouseOver, bool isMouseDown) override;

    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           const juce::Slider::SliderStyle, juce::Slider&) override;

    // Only hit if a real rotary juce::Slider is ever instantiated (current knob
    // strips in SliceControlBar are hand-painted cells, not Slider components) —
    // added so any future rotary Slider still matches the flat "Midnight" look
    // instead of falling back to LookAndFeel_V4's default 3D knob.
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    // Flat fill + 2px-radius outline to match drawButtonBackground, instead of
    // LookAndFeel_V4's default text-editor chrome.
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline    (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;

    // ── DocumentWindow (PianoRollWindow) title bar ────────────────────────
    void drawDocumentWindowTitleBar (juce::DocumentWindow&, juce::Graphics&,
                                     int w, int h, int titleSpaceX, int titleSpaceW,
                                     const juce::Image* icon, bool drawTitleTextOnLeft) override;
    juce::Button* createDocumentWindowButton (int buttonType) override;

    // ── AlertWindow sizing ────────────────────────────────────────────────
    juce::Font getAlertWindowTitleFont()   override;
    juce::Font getAlertWindowMessageFont() override;
    int        getAlertWindowButtonHeight() override;
    void       drawAlertBox (juce::Graphics&, juce::AlertWindow&,
                             const juce::Rectangle<int>& textArea,
                             juce::TextLayout&) override;

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override;

    static juce::Font makeFont     (float pointSize, bool bold = false);  // Barlow Condensed — labels
    static juce::Font makeMonoFont (float pointSize, bool bold = false);  // JetBrains Mono   — values/numbers

private:
    static juce::Typeface::Ptr sRegularTypeface;   // BarlowCondensed-Regular  — labels
    static juce::Typeface::Ptr sBoldTypeface;      // BarlowCondensed-SemiBold — bold labels
    static juce::Typeface::Ptr sMonoTypeface;      // JetBrainsMono-Regular    — values/numbers
    static juce::Typeface::Ptr sMonoBoldTypeface;  // JetBrainsMono-Bold       — bold values
    static float sMenuScale;

    juce::Typeface::Ptr regularTypeface;
    juce::Typeface::Ptr boldTypeface;
    juce::Typeface::Ptr monoTypeface;
    juce::Typeface::Ptr monoBoldTypeface;
};
