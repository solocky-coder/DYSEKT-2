#pragma once
#include "UIHelpers.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "DysektLookAndFeel.h"

/** Themed in-plugin rename dialog.
    Replaces juce::AlertWindow for slice renaming so the prompt
    stays inside the plugin window and respects the current theme. */
class RenameOverlay : public juce::Component
{
public:
    /** Called with the new name (empty = clear), or nothing on cancel. */
    std::function<void (const juce::String& newName, bool cancelled)> onResult;

    RenameOverlay (int sliceNumber, const juce::String& currentName)
    {
        const auto& T = getTheme();

        editor.setText (currentName, false);
        editor.setInputRestrictions (14);
        editor.setColour (juce::TextEditor::backgroundColourId,  T.darkBar);
        editor.setColour (juce::TextEditor::textColourId,        T.foreground);
        editor.setColour (juce::TextEditor::outlineColourId,     T.accent.withAlpha (0.6f));
        editor.setColour (juce::TextEditor::focusedOutlineColourId, T.accent);
        editor.setFont (DysektLookAndFeel::makeFont (13.0f));
        editor.onReturnKey = [this] { commit (true); };
        editor.onEscapeKey = [this] { cancel(); };
        addAndMakeVisible (editor);

        okBtn.setButtonText ("OK");
        UIHelpers::stylePrimaryPopupButton (okBtn, T);
        okBtn.onClick = [this] { commit (true); };
        addAndMakeVisible (okBtn);

        clearBtn.setButtonText ("CLEAR");
        UIHelpers::styleSecondaryPopupButton (clearBtn, T);
        clearBtn.onClick = [this] { commit (false); };
        addAndMakeVisible (clearBtn);

        cancelBtn.setButtonText ("CANCEL");
        UIHelpers::styleSecondaryPopupButton (cancelBtn, T);
        cancelBtn.onClick = [this] { cancel(); };
        addAndMakeVisible (cancelBtn);

        titleText = "RENAME SLICE " + juce::String (sliceNumber);
        setInterceptsMouseClicks (true, true);
    }

    void visibilityChanged() override
    {
        if (isVisible())
            editor.grabKeyboardFocus();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& T = getTheme();

        // Dim background
        UIHelpers::drawPopupBackdrop (g, getLocalBounds());

        const auto box = dialogBox();

        // Card background + border
        UIHelpers::drawPopupBox (g, box, T);

        const int padX = 18;

        // Title
        g.setFont (DysektLookAndFeel::makeFont (15.0f, true));
        g.setColour (T.accent);
        g.drawText (titleText,
                    box.getX() + padX, box.getY() + 14,
                    box.getWidth() - padX * 2, 20,
                    juce::Justification::centredLeft, false);

        // Sub-label above text field
        g.setFont (DysektLookAndFeel::makeFont (11.0f));
        g.setColour (T.foreground.withAlpha (0.65f));
        g.drawText ("Max 14 characters — leave blank to clear",
                    box.getX() + padX, box.getY() + 46,
                    box.getWidth() - padX * 2, 16,
                    juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        const auto box = dialogBox();
        const int padX = 18;

        // Text editor
        editor.setBounds (box.getX() + padX,
                          box.getY() + 66,
                          box.getWidth() - padX * 2,
                          28);

        // Three buttons across the bottom
        const int btnH = 26;
        const int btnY = box.getBottom() - btnH - 14;
        const int totalW = box.getWidth() - padX * 2;
        const int gap = 8;
        const int btnW = (totalW - gap * 2) / 3;
        const int startX = box.getX() + padX;

        okBtn    .setBounds (startX,                btnY, btnW, btnH);
        clearBtn .setBounds (startX + btnW + gap,   btnY, btnW, btnH);
        cancelBtn.setBounds (startX + (btnW + gap) * 2, btnY, btnW, btnH);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! dialogBox().contains (e.getPosition()))
            cancel();
    }

private:
    juce::String       titleText;
    juce::TextEditor   editor;
    juce::TextButton   okBtn, clearBtn, cancelBtn;

    void commit (bool useEditorText)
    {
        if (onResult)
            onResult (useEditorText ? editor.getText().trim() : juce::String(), false);
    }

    void cancel()
    {
        if (onResult) onResult ({}, true);
    }

    juce::Rectangle<int> dialogBox() const
    {
        const int w = juce::jmin (400, getWidth() - 40);
        const int h = 158;
        return juce::Rectangle<int> (
            (getWidth()  - w) / 2,
            (getHeight() - h) / 2,
            w, h);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RenameOverlay)
};
