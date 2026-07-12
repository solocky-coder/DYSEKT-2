#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "DysektLookAndFeel.h"
#include "ArchiveIntegration.h"
#include "ArchiveUrlOverlay.h"
#include "SfzFileBrowser.h"

class DysektProcessor;

// ── Themed folder glyph ───────────────────────────────────────────────────────
// Colour-emoji glyphs (like U+1F4C1) render from their own embedded colour
// palette in most font backends, so Graphics::setColour() has no effect on
// them — that's why the folder icon was stuck yellow regardless of theme.
// This draws a simple vector folder outline that actually respects setColour().
static inline void drawFileListFolderGlyph (juce::Graphics& g, juce::Rectangle<float> box, juce::Colour colour)
{
    const float tabW  = box.getWidth()  * 0.45f;
    const float tabH  = box.getHeight() * 0.30f;
    const float bodyY = box.getY() + tabH;

    juce::Path p;
    p.startNewSubPath (box.getX(),                      box.getBottom());
    p.lineTo           (box.getX(),                      bodyY);
    p.lineTo           (box.getX() + tabW,               bodyY);
    p.lineTo           (box.getX() + tabW + tabH * 0.8f, box.getY());
    p.lineTo           (box.getRight(),                  box.getY());
    p.lineTo           (box.getRight(),                  box.getBottom());
    p.closeSubPath();

    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.3f));
}

// ── Tiny LookAndFeel override to shrink the file-list font ───────────────────
class SmallListLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SmallListLookAndFeel() {}

    void refreshTheme()
    {
        const auto& t = getTheme();
        setColour (juce::ListBox::backgroundColourId, t.darkBar.darker (0.3f));
        setColour (juce::DirectoryContentsDisplayComponent::textColourId, t.foreground.withAlpha (0.75f));
        setColour (juce::DirectoryContentsDisplayComponent::highlightColourId, t.accent.withAlpha (0.12f));
        setColour (juce::DirectoryContentsDisplayComponent::highlightedTextColourId, t.accent);
        setColour (juce::FileChooserDialogBox::titleTextColourId, t.foreground.withAlpha (0.75f));
        setColour (juce::TextEditor::backgroundColourId, t.darkBar.darker (0.3f));
        setColour (juce::TextEditor::textColourId, t.accent);
        setColour (juce::TextEditor::outlineColourId, t.separator);
        setColour (juce::Label::backgroundColourId, t.darkBar.darker (0.3f));
        setColour (juce::Label::textColourId, t.foreground.withAlpha (0.75f));
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (getTheme().darkBar);
        g.setColour (getTheme().separator);
        g.drawRect (0, 0, w, h, 1);
    }

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                            bool isSeparator, bool isActive, bool isHighlighted,
                            bool isTicked, bool, const juce::String& text,
                            const juce::String&, const juce::Drawable*,
                            const juce::Colour*) override
    {
        if (isSeparator)
        {
            g.setColour (getTheme().separator);
            g.fillRect (area.reduced (4, 0).withHeight (1).withY (area.getCentreY()));
            return;
        }
        if (isHighlighted && isActive)
        {
            g.setColour (getTheme().accent.withAlpha (0.15f));
            g.fillRect (area);
        }
        const int dotZone = 16;
        if (isTicked)
        {
            g.setColour (getTheme().accent);
            g.fillRect (area.getX() + 6, area.getCentreY() - 2, 4, 4);
        }
        const auto textCol = isTicked ? getTheme().accent
                           : isActive ? getTheme().foreground
                                      : getTheme().foreground.withAlpha (0.4f);
        g.setColour (textCol);
        g.setFont (juce::Font (juce::FontOptions{}.withHeight (19.5f)));
        g.drawText (text,
                    area.withLeft (area.getX() + dotZone)
                        .withRight (area.getRight() - 4),
                    juce::Justification::centredLeft);
    }

    juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions{}.withHeight (19.5f)); }

    void drawComboBox (juce::Graphics& g, int width, int height, bool,
                       int buttonX, int, int, int, juce::ComboBox& box) override
    {
        const auto& t = getTheme();
        g.setColour (t.darkBar.darker (0.3f));
        g.fillRect (0, 0, width, height);
        g.setColour (box.hasKeyboardFocus (false) ? t.accent.withAlpha (0.5f) : t.separator);
        g.drawRect (0, 0, width, height, 1);
        const int cx = buttonX + (width - buttonX) / 2;
        const int cy = height / 2;
        g.setColour (t.foreground.withAlpha (0.85f));
        g.drawLine ((float)(cx - 4), (float)(cy - 2), (float)(cx),     (float)(cy + 2), 1.5f);
        g.drawLine ((float)(cx),     (float)(cy + 2), (float)(cx + 4), (float)(cy - 2), 1.5f);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return juce::Font (juce::FontOptions{}.withHeight (16.5f));
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (4, 1, box.getWidth() - 28, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
        label.setColour (juce::Label::textColourId, getTheme().foreground);
        label.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    }

    void drawFileBrowserRow (juce::Graphics& g, int width, int height,
                             const juce::File& file, const juce::String& filename,
                             juce::Image*, const juce::String& /*fileSizeDescription*/,
                             const juce::String&, bool isDirectory, bool isItemSelected,
                             int, juce::DirectoryContentsDisplayComponent&) override
    {
        const auto& t = getTheme();

        // Selection highlight — same tint as SfzFileBrowser
        if (isItemSelected)
        {
            g.setColour (t.accent.withAlpha (0.14f));
            g.fillAll();
        }

        const auto textCol = isItemSelected ? t.accent : t.foreground.withAlpha (0.80f);

        // Self-computed scale factor — same pattern as SliceLcdDisplay/DualLcdControlFrame.
        // Derived from the actual row height JUCE gives us vs. the design row height.
        const float sf = (float) height / (float) kRowHeight;
        const int   iconColW = juce::roundToInt (kIconWidth * sf);

        // Folder / file icon — same layout as SfzFileBrowser
        if (isDirectory)
        {
            // Vector folder glyph — colour-emoji icons ignore setColour() and were
            // stuck yellow regardless of theme; this respects t.accent properly.
            auto iconBox = juce::Rectangle<float> (3.0f * sf, (float) height * 0.28f,
                                                    (float) iconColW - 6.0f * sf, (float) height * 0.46f);
            drawFileListFolderGlyph (g, iconBox, t.accent.withAlpha (0.55f));
        }
        else
        {
            // Extension badge — same style as SfzFileBrowser
            const auto ext = file.getFileExtension().toUpperCase().trimCharactersAtStart (".");
            if (! ext.isEmpty())
            {
                const int  badgeW    = juce::roundToInt (36 * sf);
                const int  badgeH    = juce::roundToInt (14 * sf);
                const auto badgeRect = juce::Rectangle<int> (width - badgeW - juce::roundToInt (4 * sf),
                                                              (height - badgeH) / 2, badgeW, badgeH);
                g.setColour (t.accent.withAlpha (0.18f));
                g.fillRoundedRectangle (badgeRect.toFloat(), 2.0f * sf);
                g.setFont (juce::Font (juce::FontOptions{}.withHeight (10.0f * sf)));
                g.setColour (t.accent.withAlpha (0.80f));
                g.drawText (ext, badgeRect, juce::Justification::centred, false);
            }
        }

        // Filename — same font size as SfzFileBrowser (13pt design), scaled by sf
        g.setFont (juce::Font (juce::FontOptions{}.withHeight (kTextSize * sf)));
        g.setColour (textCol);
        const int textX = isDirectory ? iconColW + juce::roundToInt (4 * sf) : juce::roundToInt (6 * sf);
        const int textW = isDirectory ? width - textX - juce::roundToInt (4 * sf)
                                      : width - juce::roundToInt (36 * sf) - juce::roundToInt (12 * sf);
        g.drawText (filename, textX, 0, textW, height,
                    juce::Justification::centredLeft, true);
    }

    int smallRowHeight() const { return kRowHeight; }

    // Shared sizing constants — keep in sync with SfzFileBrowser::kRowH
    static constexpr float kIconSize  = 16.0f;   ///< emoji font size
    static constexpr int   kIconWidth = 22;       ///< pixel width reserved for icon column
    static constexpr float kTextSize  = 13.0f;   ///< filename font size
    static constexpr int   kRowHeight = 26;       ///< row pixel height
};

// ── Play/Stop icon button ─────────────────────────────────────
class IconButton : public juce::Button
{
public:
    IconButton() : juce::Button ("IconButton") {}
    void paintButton (juce::Graphics& g, bool isMouseOver, bool isButtonDown) override
    {
        auto area = getLocalBounds().toFloat().reduced (5.0f);

        if (isButtonDown)
            g.setColour (getTheme().accent.withAlpha (0.18f));
        else if (isMouseOver)
            g.setColour (getTheme().accent.withAlpha (0.12f));
        else
            g.setColour (getTheme().accent.withAlpha (0.08f));
        g.fillEllipse (area);

        g.setColour (getTheme().accent.withAlpha (0.95f));

        if (state == Playing)
        {
            float iconSize = std::min (area.getWidth(), area.getHeight()) * 0.50f;
            juce::Rectangle<float> r = area.withSizeKeepingCentre (iconSize, iconSize);
            g.fillRect (r);
        }
        else
        {
            juce::Path triangle;
            auto cx = area.getCentreX();
            auto cy = area.getCentreY();
            float s = std::min (area.getWidth(), area.getHeight()) * 0.58f;
            triangle.addTriangle (cx - s/3.1f, cy - s/2.0f,
                                  cx - s/3.1f, cy + s/2.0f,
                                  cx + s/1.5f, cy);
            g.fillPath (triangle);
        }
    }
    enum IconState { Stopped, Playing };
    void setState (IconState s) { state = s; repaint(); }
    IconState getState() const { return state; }
private:
    IconState state = Stopped;
};

// ── Removable bookmark button (right-click → remove) ────────────────────────
class RemovableButton : public juce::TextButton
{
public:
    std::function<void()> onRightClick;

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown() && onRightClick)
            onRightClick();
        else
            juce::TextButton::mouseDown (e);
    }
};

// ── Archive list row ─────────────────────────────────────────────────────────
struct ArchiveRow
{
    juce::String name;
    juce::String format;       // empty for collection entries
    juce::String downloadUrl;  // empty for collection entries
    juce::int64  sizeBytes = 0;
    bool         isFolder  = false;   // true = collection sub-item
    juce::String folderId;            // identifier for drill-in
};

class FileBrowserPanel : public juce::Component,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    explicit FileBrowserPanel (DysektProcessor& p);
    ~FileBrowserPanel() override;

    void resized() override;
    void paint   (juce::Graphics& g) override;

    void refreshTheme();

    std::function<void()> onFileLoaded;
    std::function<void (const juce::File&)> onLoadRequest;

    void setBrowserMode (SfzFileBrowser::Mode m) { sfzBrowser.setMode (m); }

private:
    // ── ChangeListener ────────────────────────────────────────────────────────
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    // ── Timer (spinner animation) ─────────────────────────────────────────────
    void timerCallback() override;

    // ── File events (wired to sfzBrowser callbacks) ───────────────────────────
    void fileClicked       (const juce::File& f);
    void fileDoubleClicked (const juce::File& f);

    // ── Preview engine ────────────────────────────────────────────────────────
    void startPreview (const juce::File& f);
    void startPreviewFromReader (juce::AudioFormatReader* reader);  // takes ownership
    void stopPreview();
    void updatePlayButton();

    DysektProcessor& processor;

    // ── Local folder bookmarks ────────────────────────────────────────────────
    struct Bookmark
    {
        juce::String name;
        juce::File   path;
        bool         removable = true;
    };

    juce::Array<Bookmark>              bookmarks;
    juce::OwnedArray<RemovableButton>  bmBtns;
    juce::TextButton                   addBmBtn;
    std::unique_ptr<juce::FileChooser> fileChooser;

    void detectCloudFolders();
    void loadCustomBookmarks();
    void saveCustomBookmarks();
    void rebuildBookmarkBar();

    static constexpr int kBmH = 34;

    // ── Internet Archive bookmarks ────────────────────────────────────────────
    struct ArchiveBookmark
    {
        juce::String url;         ///< The original archive.org URL
        juce::String title;       ///< Resolved display name
        bool         isCollection = false;
        bool         pending      = false;  ///< Still resolving
    };

    juce::Array<ArchiveBookmark>       archiveBookmarks;
    juce::OwnedArray<RemovableButton>  archiveBtns;
    juce::TextButton                      addArchiveBtn;
    std::unique_ptr<ArchiveUrlOverlay>    archiveUrlOverlay;
    std::unique_ptr<ArchiveMessageOverlay> archiveMessageOverlay;

    void showArchiveMessage (const juce::String& title, const juce::String& body);

    void loadArchiveBookmarks();
    void saveArchiveBookmarks();
    void rebuildArchiveButtons();
    void showArchiveUrlDialog();
    void resolveAndAddArchiveBookmark (const juce::String& url);

    static juce::File getArchiveBookmarksFile();

    // ── Archive list view (shown instead of local browser) ───────────────────
    bool                         archiveViewActive  = false;
    int                          activeArchiveIndex = -1;   ///< index in archiveBookmarks
    juce::Array<ArchiveRow>      archiveRows;
    juce::ListBox                archiveList;
    juce::String                 archiveListTitle;
    SmallListLookAndFeel         smallLAF;

    // Simple ListBoxModel inline
    struct ArchiveListModel : public juce::ListBoxModel
    {
        FileBrowserPanel* owner = nullptr;
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    } archiveModel;

    void showArchiveItem (int bookmarkIndex);
    void showCollectionItem (const juce::String& collectionId);
    void loadArchiveFile (const ArchiveRow& row);
    void exitArchiveView();

    // ── File browser (SfzFileBrowser — matches SFZ-Player panel style) ───────
    SfzFileBrowser             sfzBrowser;

    juce::AudioDeviceManager       deviceManager;
    juce::AudioFormatManager       formatManager;
    juce::AudioTransportSource     transport;
    juce::AudioSourcePlayer        sourcePlayer;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;

    juce::File                     previewFile;
    juce::String                   streamPreviewUrl;   // non-empty when current preview is a stream
    bool                           previewVisible = false;
    std::atomic<int>               streamGeneration { 0 };  // incremented to cancel stale stream callbacks

    IconButton                     playStopBtn;
    juce::Slider                   volumeSlider;
    juce::Label                    fileNameLabel;

    // ── Spinner state for pending archive bookmarks ───────────────────────────
    int spinnerFrame = 0;

    static constexpr int kBarH = 36;
};
