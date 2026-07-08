#pragma once
#include "UIHelpers.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "DysektLookAndFeel.h"

// =============================================================================
//  ArchiveMessageOverlay — themed single-button info/error dialog
// =============================================================================
class ArchiveMessageOverlay : public juce::Component
{
public:
    std::function<void()> onDismiss;

    ArchiveMessageOverlay (const juce::String& title, const juce::String& body)
        : titleText (title), bodyText (body)
    {
        const auto& T = getTheme();

        okBtn.setButtonText ("OK");
        UIHelpers::stylePrimaryPopupButton (okBtn, T);
        okBtn.setMouseCursor (juce::MouseCursor::NormalCursor);
        okBtn.onClick = [this] { dismiss(); };
        addAndMakeVisible (okBtn);

        setInterceptsMouseClicks (true, true);
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& T = getTheme();
        UIHelpers::drawPopupBackdrop (g, getLocalBounds());

        const auto box = dialogBox();
        UIHelpers::drawPopupBox (g, box, T);

        const int padX = 18;
        g.setFont (DysektLookAndFeel::makeFont (14.0f, true));
        g.setColour (T.accent);
        g.drawText (titleText, box.getX() + padX, box.getY() + 14,
                    box.getWidth() - padX * 2, 20,
                    juce::Justification::centredLeft, false);

        g.setFont (DysektLookAndFeel::makeFont (11.5f));
        g.setColour (T.foreground.withAlpha (0.85f));
        g.drawFittedText (bodyText,
                          box.getX() + padX, box.getY() + 46,
                          box.getWidth() - padX * 2, box.getHeight() - 90,
                          juce::Justification::topLeft, 6);
    }

    void resized() override
    {
        const auto box = dialogBox();
        const int padX = 18;
        const int btnH = 26;
        okBtn.setBounds (box.getX() + padX,
                         box.getBottom() - btnH - 14,
                         box.getWidth() - padX * 2,
                         btnH);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! dialogBox().contains (e.getPosition()))
            dismiss();
    }

private:
    juce::String     titleText, bodyText;
    juce::TextButton okBtn;

    void dismiss() { if (onDismiss) onDismiss(); }

    juce::Rectangle<int> dialogBox() const
    {
        const int w = juce::jmin (400, getWidth() - 40);
        const int h = 160;
        return { (getWidth() - w) / 2, (getHeight() - h) / 2, w, h };
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArchiveMessageOverlay)
};

// =============================================================================
//  ArchiveUrlOverlay — themed URL entry dialog
// =============================================================================
class ArchiveUrlOverlay : public juce::Component
{
public:
    /** Called with the trimmed URL string on confirm, or empty + cancelled=true on dismiss. */
    std::function<void (const juce::String& url, bool cancelled)> onResult;

    ArchiveUrlOverlay()
    {
        const auto& T = getTheme();

        editor.setTextToShowWhenEmpty ("https://archive.org/details/IDENTIFIER  or  IDENTIFIER",
                                       T.foreground.withAlpha (0.35f));
        editor.setColour (juce::TextEditor::backgroundColourId,     T.darkBar);
        editor.setColour (juce::TextEditor::textColourId,           T.foreground);
        editor.setColour (juce::TextEditor::outlineColourId,        T.accent.withAlpha (0.5f));
        editor.setColour (juce::TextEditor::focusedOutlineColourId, T.accent);
        editor.setFont (DysektLookAndFeel::makeFont (12.5f));
        editor.onReturnKey = [this] { commit(); };
        editor.onEscapeKey = [this] { cancel(); };
        addAndMakeVisible (editor);

        addBtn.setButtonText ("ADD");
        UIHelpers::stylePrimaryPopupButton (addBtn, T);
        addBtn.setMouseCursor (juce::MouseCursor::NormalCursor);
        addBtn.onClick = [this] { commit(); };
        addAndMakeVisible (addBtn);

        cancelBtn.setButtonText ("CANCEL");
        UIHelpers::styleSecondaryPopupButton (cancelBtn, T);
        cancelBtn.setMouseCursor (juce::MouseCursor::NormalCursor);
        cancelBtn.onClick = [this] { cancel(); };
        addAndMakeVisible (cancelBtn);

        setInterceptsMouseClicks (true, true);
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void visibilityChanged() override
    {
        if (isVisible())
            editor.grabKeyboardFocus();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& T = getTheme();

        UIHelpers::drawPopupBackdrop (g, getLocalBounds());

        const auto box = dialogBox();

        UIHelpers::drawPopupBox (g, box, T);

        const int padX = 18;

        g.setFont (DysektLookAndFeel::makeFont (14.0f, true));
        g.setColour (T.accent);
        g.drawText ("ADD INTERNET ARCHIVE URL",
                    box.getX() + padX, box.getY() + 14,
                    box.getWidth() - padX * 2, 20,
                    juce::Justification::centredLeft, false);

        g.setFont (DysektLookAndFeel::makeFont (11.0f));
        g.setColour (T.foreground.withAlpha (0.65f));
        g.drawText ("Paste an archive.org URL or bare identifier:",
                    box.getX() + padX, box.getY() + 46,
                    box.getWidth() - padX * 2, 16,
                    juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        const auto box   = dialogBox();
        const int padX   = 18;
        const int btnH   = 26;
        const int btnY   = box.getBottom() - btnH - 14;
        const int totalW = box.getWidth() - padX * 2;
        const int gap    = 8;
        const int btnW   = (totalW - gap) / 2;
        const int startX = box.getX() + padX;

        editor.setBounds (box.getX() + padX,
                          box.getY() + 66,
                          box.getWidth() - padX * 2,
                          28);

        addBtn   .setBounds (startX,             btnY, btnW, btnH);
        cancelBtn.setBounds (startX + btnW + gap, btnY, btnW, btnH);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! dialogBox().contains (e.getPosition()))
            cancel();
    }

private:
    juce::TextEditor editor;
    juce::TextButton addBtn, cancelBtn;

    void commit()
    {
        if (onResult) onResult (editor.getText().trim(), false);
    }

    void cancel()
    {
        if (onResult) onResult ({}, true);
    }

    juce::Rectangle<int> dialogBox() const
    {
        const int w = juce::jmin (460, getWidth() - 40);
        const int h = 158;
        return { (getWidth() - w) / 2, (getHeight() - h) / 2, w, h };
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArchiveUrlOverlay)
};
