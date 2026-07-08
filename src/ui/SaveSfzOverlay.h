#pragma once
#include "UIHelpers.h"
// =============================================================================
//  SaveSfzOverlay.h  —  In-plugin "Save SFZ As…" dialog
// =============================================================================
//  Lets the user choose a filename (without extension) and optionally a
//  destination directory via a native folder picker.
//
//  onResult (targetFile, confirmed)
//      confirmed == false  →  user cancelled; do not copy/rename anything.
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include "DysektLookAndFeel.h"

class SaveSfzOverlay : public juce::Component
{
public:
    /** targetFile is the resolved destination (.sfz extension added).
     *  confirmed == false → cancelled.  */
    std::function<void (const juce::File& targetFile, bool confirmed)> onResult;

    /** @param currentFile  the .sfz currently loaded (used to seed name + folder) */
    explicit SaveSfzOverlay (const juce::File& currentFile)
        : destDir (currentFile.getParentDirectory()),
          seedName (currentFile.getFileNameWithoutExtension())
    {
        const auto& T = getTheme();

        // ── Name editor ───────────────────────────────────────────────────────
        nameEditor.setText (seedName, false);
        nameEditor.setInputRestrictions (64, "abcdefghijklmnopqrstuvwxyz"
                                              "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                              "0123456789 _-.");
        nameEditor.setColour (juce::TextEditor::backgroundColourId,     T.darkBar);
        nameEditor.setColour (juce::TextEditor::textColourId,           T.foreground);
        nameEditor.setColour (juce::TextEditor::outlineColourId,        T.accent.withAlpha (0.5f));
        nameEditor.setColour (juce::TextEditor::focusedOutlineColourId, T.accent);
        nameEditor.setFont (DysektLookAndFeel::makeFont (13.0f));
        nameEditor.onReturnKey = [this] { commit(); };
        nameEditor.onEscapeKey = [this] { fire (false); };
        addAndMakeVisible (nameEditor);

        // ── Choose folder button ──────────────────────────────────────────────
        folderBtn.setButtonText ("FOLDER: " + destDir.getFileName());
        folderBtn.setColour (juce::TextButton::buttonColourId,  T.darkBar);
        folderBtn.setColour (juce::TextButton::textColourOffId, T.foreground.withAlpha (0.75f));
        folderBtn.onClick = [this] { chooseFolder(); };
        addAndMakeVisible (folderBtn);

        // ── Save / Cancel ─────────────────────────────────────────────────────
        saveBtn.setButtonText ("SAVE");
        UIHelpers::stylePrimaryPopupButton (saveBtn, T);
        saveBtn.onClick = [this] { commit(); };
        addAndMakeVisible (saveBtn);

        cancelBtn.setButtonText ("CANCEL");
        UIHelpers::styleSecondaryPopupButton (cancelBtn, T);
        cancelBtn.onClick = [this] { fire (false); };
        addAndMakeVisible (cancelBtn);

        setInterceptsMouseClicks (true, true);

        // Prevent the PointingHandCursor set on KeysPanel from bleeding through
        // when JUCE walks up the component hierarchy to resolve the cursor.
        setMouseCursor (juce::MouseCursor::NormalCursor);
        for (auto* b : { &folderBtn, &saveBtn, &cancelBtn })
            b->setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void visibilityChanged() override
    {
        if (isVisible()) nameEditor.grabKeyboardFocus();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& T = getTheme();

        UIHelpers::drawPopupBackdrop (g, getLocalBounds());

        const auto box  = dialogBox();
        const int  padX = 18;

        UIHelpers::drawPopupBox (g, box, T);

        g.setFont (DysektLookAndFeel::makeFont (15.0f, true));
        g.setColour (T.accent);
        g.drawText ("SAVE SFZ AS",
                    box.getX() + padX, box.getY() + 14,
                    box.getWidth() - padX * 2, 20,
                    juce::Justification::centredLeft, false);


        // Sub-labels
        g.setFont (DysektLookAndFeel::makeFont (10.0f));
        g.setColour (T.foreground.withAlpha (0.50f));
        g.drawText ("Filename (no extension)",
                    box.getX() + padX, box.getY() + 44,
                    box.getWidth() - padX * 2, 14,
                    juce::Justification::centredLeft, false);

        g.drawText ("Destination",
                    box.getX() + padX, box.getY() + 98,
                    box.getWidth() - padX * 2, 14,
                    juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        const auto box  = dialogBox();
        const int  padX = 18;
        const int  innerW = box.getWidth() - padX * 2;

        nameEditor.setBounds (box.getX() + padX, box.getY() + 60, innerW, 28);
        folderBtn .setBounds (box.getX() + padX, box.getY() + 114, innerW, 26);

        const int btnH = 28, btnW = 100, gap = 10;
        const int btnY = box.getBottom() - btnH - 14;
        const int totalW = btnW * 2 + gap;
        const int btnX = box.getCentreX() - totalW / 2;

        saveBtn  .setBounds (btnX,           btnY, btnW, btnH);
        cancelBtn.setBounds (btnX + btnW + gap, btnY, btnW, btnH);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! dialogBox().contains (e.getPosition()))
            fire (false);
    }

private:
    juce::File        destDir;
    juce::String      seedName;

    juce::TextEditor  nameEditor;
    juce::TextButton  folderBtn;
    juce::TextButton  saveBtn, cancelBtn;

    std::unique_ptr<juce::FileChooser> folderChooser;

    void chooseFolder()
    {
        folderChooser = std::make_unique<juce::FileChooser> (
            "Choose destination folder", destDir, "");

        folderChooser->launchAsync (
            juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& fc)
            {
                auto chosen = fc.getResult();
                if (chosen.isDirectory())
                {
                    destDir = chosen;
                    folderBtn.setButtonText ("FOLDER: " + destDir.getFileName());
                }
            });
    }

    void commit()
    {
        auto name = nameEditor.getText().trim();
        if (name.isEmpty()) name = "Custom";
        if (! name.endsWithIgnoreCase (".sfz"))
            name += ".sfz";
        fire (true, destDir.getChildFile (name));
    }

    void fire (bool confirmed, const juce::File& f = {})
    {
        if (onResult) onResult (f, confirmed);
    }

    juce::Rectangle<int> dialogBox() const
    {
        const int w = juce::jmin (400, getWidth() - 40);
        const int h = 200;
        return juce::Rectangle<int> (
            (getWidth()  - w) / 2,
            (getHeight() - h) / 2,
            w, h);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SaveSfzOverlay)
};
