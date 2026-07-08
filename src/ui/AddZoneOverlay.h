#pragma once
#include "UIHelpers.h"
// =============================================================================
//  AddZoneOverlay.h  —  In-plugin dialog: configure a new SFZ zone
// =============================================================================
//  Shown after the user picks a sample file.  Lets them set loKey / hiKey /
//  rootKey before the <region> is written, preventing accidental overlaps.
//
//  Default: single key (loKey == hiKey == prevHiKey + 1), root == loKey.
//  The user can expand the range before confirming.
//
//  onResult (loKey, hiKey, rootKey, confirmed)
//      confirmed == false  →  user cancelled; do not write anything.
// =============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include "DysektLookAndFeel.h"

class AddZoneOverlay : public juce::Component
{
public:
    /** lo, hi, root are MIDI note numbers (0-127).  confirmed=false = cancel. */
    std::function<void (int lo, int hi, int root, bool confirmed)> onResult;

    /** @param sampleName  bare filename shown in the title bar
     *  @param defaultLo   suggested loKey (= prevHiKey + 1, or 0 if first zone)
     */
    AddZoneOverlay (const juce::String& sampleName, int defaultLo)
        : loKey  (juce::jlimit (0, 127, defaultLo)),
          hiKey  (juce::jlimit (0, 127, defaultLo)),   // single-key default
          rootKey(juce::jlimit (0, 127, defaultLo)),
          title  ("ADD ZONE  —  " + sampleName.toUpperCase())
    {
        const auto& T = getTheme();

        auto styleBtn = [&] (juce::TextButton& b, bool accent)
        {
            if (accent)
                UIHelpers::stylePrimaryPopupButton (b, T);
            else
                UIHelpers::styleSecondaryPopupButton (b, T);
            addAndMakeVisible (b);
        };

        auto styleSpinner = [&] (juce::TextButton& dn, juce::TextButton& up)
        {
            for (auto* b : { &dn, &up })
            {
                b->setColour (juce::TextButton::buttonColourId,  T.darkBar);
                b->setColour (juce::TextButton::textColourOffId, T.foreground);
                addAndMakeVisible (*b);
            }
        };

        styleSpinner (loDown, loUp);
        styleSpinner (hiDown, hiUp);
        styleSpinner (rtDown, rtUp);

        loDown.setButtonText ("<");  loUp.setButtonText (">");
        hiDown.setButtonText ("<");  hiUp.setButtonText (">");
        rtDown.setButtonText ("<");  rtUp.setButtonText (">");

        loDown.onClick = [this] { adjust (loKey, -1, true);  };
        loUp  .onClick = [this] { adjust (loKey, +1, true);  };
        hiDown.onClick = [this] { adjust (hiKey, -1, false); };
        hiUp  .onClick = [this] { adjust (hiKey, +1, false); };
        rtDown.onClick = [this] { rootKey = juce::jlimit (0, 127, rootKey - 1); repaint(); };
        rtUp  .onClick = [this] { rootKey = juce::jlimit (0, 127, rootKey + 1); repaint(); };

        styleBtn (confirmBtn, true);
        styleBtn (cancelBtn,  false);

        confirmBtn.setButtonText ("ADD ZONE");
        cancelBtn .setButtonText ("CANCEL");

        confirmBtn.onClick = [this] { fire (true);  };
        cancelBtn .onClick = [this] { fire (false); };

        setInterceptsMouseClicks (true, true);

        // Prevent the PointingHandCursor set on KeysPanel from bleeding through
        // when JUCE walks up the component hierarchy to resolve the cursor.
        setMouseCursor (juce::MouseCursor::NormalCursor);
        for (auto* b : { &loDown, &loUp, &hiDown, &hiUp, &rtDown, &rtUp,
                         &confirmBtn, &cancelBtn })
            b->setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& T = getTheme();

        UIHelpers::drawPopupBackdrop (g, getLocalBounds());

        const auto box = dialogBox();

        // Card
        UIHelpers::drawPopupBox (g, box, T);

        const int padX = 18;

        // Title
        g.setFont (DysektLookAndFeel::makeFont (13.0f, true));
        g.setColour (T.accent);
        g.drawText (title,
                    box.getX() + padX, box.getY() + 14,
                    box.getWidth() - padX * 2, 20,
                    juce::Justification::centredLeft, true);


        // Row labels + note readouts
        g.setFont (DysektLookAndFeel::makeFont (10.5f));
        g.setColour (T.foreground.withAlpha (0.60f));

        const auto rows = spinnerRows (box);

        const char* labels[] = { "loKey", "hiKey", "root" };
        const int   vals[]   = { loKey,   hiKey,   rootKey };

        for (int i = 0; i < 3; ++i)
        {
            const auto& r = rows[i];

            // Label
            g.setColour (T.foreground.withAlpha (0.55f));
            g.drawText (labels[i],
                        r.getX(), r.getY(),
                        50, r.getHeight(),
                        juce::Justification::centredLeft, false);

            // Note name readout (centred between the two spinner buttons)
            g.setFont (DysektLookAndFeel::makeFont (13.0f, true));
            g.setColour (T.foreground);
            g.drawText (noteName (vals[i]),
                        r.getX() + 56 + kArrowW + 4,
                        r.getY(),
                        kReadoutW, r.getHeight(),
                        juce::Justification::centred, false);

            // MIDI number subscript
            g.setFont (DysektLookAndFeel::makeFont (8.5f));
            g.setColour (T.foreground.withAlpha (0.38f));
            g.drawText ("(" + juce::String (vals[i]) + ")",
                        r.getX() + 56 + kArrowW + 4 + kReadoutW,
                        r.getY(),
                        30, r.getHeight(),
                        juce::Justification::centredLeft, false);
        }

        // Hint line
        g.setFont (DysektLookAndFeel::makeFont (9.5f));
        g.setColour (T.foreground.withAlpha (0.35f));
        g.drawText ("Default is one key. Expand the range before confirming.",
                    box.getX() + padX,
                    rows[2].getBottom() + 6,
                    box.getWidth() - padX * 2,
                    14,
                    juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        const auto box  = dialogBox();
        const auto rows = spinnerRows (box);

        for (int i = 0; i < 3; ++i)
        {
            const auto& r = rows[i];
            const int spinX = r.getX() + 56;           // after label

            juce::TextButton* dn[] = { &loDown, &hiDown, &rtDown };
            juce::TextButton* up[] = { &loUp,   &hiUp,   &rtUp   };

            dn[i]->setBounds (spinX,                       r.getY() + 3, kArrowW, r.getHeight() - 6);
            up[i]->setBounds (spinX + kArrowW + kReadoutW + 38, r.getY() + 3, kArrowW, r.getHeight() - 6);
        }

        // Confirm / Cancel buttons
        const int btnH  = 28;
        const int btnW  = 110;
        const int gap   = 10;
        const int btnY  = box.getBottom() - btnH - 14;
        const int totalW = btnW * 2 + gap;
        const int btnX  = box.getCentreX() - totalW / 2;

        confirmBtn.setBounds (btnX,           btnY, btnW, btnH);
        cancelBtn .setBounds (btnX + btnW + gap, btnY, btnW, btnH);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! dialogBox().contains (e.getPosition()))
            fire (false);
    }

private:
    static constexpr int kArrowW   = 20;
    static constexpr int kReadoutW = 48;
    static constexpr int kRowH     = 30;

    int loKey, hiKey, rootKey;
    juce::String title;

    juce::TextButton loDown, loUp;
    juce::TextButton hiDown, hiUp;
    juce::TextButton rtDown, rtUp;
    juce::TextButton confirmBtn, cancelBtn;

    // ── Helpers ───────────────────────────────────────────────────────────────

    static juce::String noteName (int n)
    {
        static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        return juce::String (names[n % 12]) + juce::String (n / 12 - 1);
    }

    /** Adjust a key value, clamping and keeping lo <= hi. */
    void adjust (int& val, int delta, bool isLo)
    {
        val = juce::jlimit (0, 127, val + delta);
        if (isLo && loKey > hiKey) hiKey  = loKey;
        if (!isLo && hiKey < loKey) loKey = hiKey;
        // Keep root inside [lo, hi]
        rootKey = juce::jlimit (loKey, hiKey, rootKey);
        repaint();
    }

    void fire (bool confirmed)
    {
        if (onResult)
            onResult (loKey, hiKey, rootKey, confirmed);
    }

    juce::Rectangle<int> dialogBox() const
    {
        const int w = juce::jmin (420, getWidth() - 40);
        const int h = 230;
        return juce::Rectangle<int> (
            (getWidth()  - w) / 2,
            (getHeight() - h) / 2,
            w, h);
    }

    /** Three evenly-spaced rows for lo / hi / root spinners. */
    std::array<juce::Rectangle<int>, 3> spinnerRows (const juce::Rectangle<int>& box) const
    {
        const int padX   = 18;
        const int startY = box.getY() + 46;
        std::array<juce::Rectangle<int>, 3> r;
        for (int i = 0; i < 3; ++i)
            r[i] = juce::Rectangle<int> (box.getX() + padX,
                                          startY + i * (kRowH + 4),
                                          box.getWidth() - padX * 2,
                                          kRowH);
        return r;
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AddZoneOverlay)
};
