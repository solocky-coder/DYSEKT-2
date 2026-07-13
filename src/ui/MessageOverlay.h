#pragma once
#include "UIHelpers.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "DysektLookAndFeel.h"

// Themed, single-button replacement for juce::AlertWindow::showMessageBoxAsync /
// juce::AlertWindow::showAsync. The stock AlertWindow only picks up the plugin's
// colours (see DysektLookAndFeel::drawAlertBox); its box shape, title strip, and
// button row are still the generic JUCE dialog layout. MessageOverlay reuses the
// exact chrome ConfirmOverlay/RenameOverlay already use (UIHelpers::drawPopupBackdrop
// / drawPopupBox / stylePrimaryPopupButton) so a plain info/warning popup looks
// like it belongs to the same dialog family as every other overlay in the plugin.
class MessageOverlay : public juce::Component
{
public:
    enum class Kind { Info, Warning };

    std::function<void()> onDismiss;

    MessageOverlay (const juce::String& title,
                    const juce::String& message,
                    Kind kind = Kind::Info,
                    const juce::String& buttonText = "OK")
        : titleText (title), messageText (message), messageKind (kind)
    {
        okBtn.setButtonText (buttonText);
        UIHelpers::stylePrimaryPopupButton (okBtn, getTheme());
        okBtn.onClick = [this] { if (onDismiss) onDismiss(); };
        addAndMakeVisible (okBtn);

        setInterceptsMouseClicks (true, true);
        setWantsKeyboardFocus (true);
    }

    void visibilityChanged() override
    {
        if (isVisible())
            grabKeyboardFocus();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& T = getTheme();

        UIHelpers::drawPopupBackdrop (g, getLocalBounds());

        const auto box = dialogBox();
        UIHelpers::drawPopupBox (g, box, T);

        // Warning titles pick up a warm amber instead of the theme accent so a
        // failed-write message still reads as "something went wrong" even in
        // themes where the accent colour itself isn't red/orange (e.g. Ice Blue).
        const auto titleColour = messageKind == Kind::Warning
                                ? juce::Colour (0xFFE0A030)
                                : T.accent;

        const int padX = 18;
        g.setFont (DysektLookAndFeel::makeFont (15.0f, true));
        g.setColour (titleColour);
        g.drawText (titleText,
                    box.getX() + padX, box.getY() + 14,
                    box.getWidth() - padX * 2, 20,
                    juce::Justification::centredLeft, false);

        g.setFont (DysektLookAndFeel::makeFont (12.5f));
        g.setColour (T.foreground.withAlpha (0.85f));
        g.drawFittedText (messageText,
                    box.getX() + padX, box.getY() + 42,
                    box.getWidth() - padX * 2, box.getHeight() - 42 - 46,
                    juce::Justification::topLeft, 4, 1.0f);
    }

    void resized() override
    {
        const auto box = dialogBox();
        const int btnW = 96;
        const int btnH = 28;
        okBtn.setBounds (box.getCentreX() - btnW / 2, box.getBottom() - btnH - 14, btnW, btnH);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! dialogBox().contains (e.getPosition()))
            if (onDismiss) onDismiss();
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::returnKey || k == juce::KeyPress::escapeKey)
        {
            if (onDismiss) onDismiss();
            return true;
        }
        return false;
    }

private:
    juce::String titleText, messageText;
    Kind messageKind;
    juce::TextButton okBtn;

    juce::Rectangle<int> dialogBox() const
    {
        const int w = juce::jmin (420, getWidth() - 40);
        const int h = 156;
        return juce::Rectangle<int> (
            (getWidth()  - w) / 2,
            (getHeight() - h) / 2,
            w, h);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MessageOverlay)
};
