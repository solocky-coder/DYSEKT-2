#include "FileBrowserPanel.h"
#include "DysektLookAndFeel.h"
#include "UIHelpers.h"
#include "../PluginProcessor.h"

#include <windows.h>

// ── ArchiveListModel ─────────────────────────────────────────────────────────

int FileBrowserPanel::ArchiveListModel::getNumRows()
{
    return owner ? owner->archiveRows.size() : 0;
}

void FileBrowserPanel::ArchiveListModel::paintListBoxItem (int row, juce::Graphics& g,
                                                           int w, int h, bool selected)
{
    if (! owner) return;
    if (row < 0 || row >= owner->archiveRows.size()) return;

    const auto& R = owner->archiveRows[row];
    const auto& T = getTheme();

    if (selected)
    {
        g.setColour (T.accent.withAlpha (0.12f));
        g.fillAll();
    }

    g.setFont (juce::Font (juce::FontOptions{}.withHeight (19.5f)));

    // Folder icon placeholder
    if (R.isFolder)
    {
        g.setColour (T.accent.withAlpha (0.6f));
        g.drawText (u8"\U0001F4C1", 4, 0, 18, h, juce::Justification::centredLeft);
        g.setColour (selected ? T.accent : T.foreground.withAlpha (0.8f));
        g.drawText (R.name, 24, 0, w - 28, h, juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour (selected ? T.accent : T.foreground.withAlpha (0.75f));
        g.drawText (R.name, 4, 0, w - 120, h, juce::Justification::centredLeft, true);

        // Format badge
        if (R.format.isNotEmpty())
        {
            auto badgeW = 40;
            auto badgeRect = juce::Rectangle<int> (w - badgeW - 60, (h - 13) / 2, badgeW, 13);
            g.setColour (T.accent.withAlpha (0.18f));
            g.fillRoundedRectangle (badgeRect.toFloat(), 2.0f);
            g.setColour (T.accent.withAlpha (0.85f));
            g.setFont (juce::Font (juce::FontOptions{}.withHeight (16.5f)));
            g.drawText (R.format, badgeRect, juce::Justification::centred);
        }

        // Size
        if (R.sizeBytes > 0)
        {
            g.setFont (juce::Font (juce::FontOptions{}.withHeight (16.5f)));
            g.setColour (T.foreground.withAlpha (0.4f));
            juce::String sizeStr;
            if (R.sizeBytes >= 1024 * 1024)
                sizeStr = juce::String (R.sizeBytes / (1024 * 1024)) + " MB";
            else
                sizeStr = juce::String (R.sizeBytes / 1024) + " KB";
            g.drawText (sizeStr, w - 58, 0, 55, h, juce::Justification::centredRight, true);
        }
    }
}

void FileBrowserPanel::ArchiveListModel::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    if (! owner) return;
    if (row < 0 || row >= owner->archiveRows.size()) return;

    const auto& R = owner->archiveRows[row];

    // Single click on a file row = preview (same behaviour as local fileClicked)
    // Single click on a folder row = do nothing (double-click drills in)
    if (! R.isFolder && R.downloadUrl.isNotEmpty())
        owner->loadArchiveFile (R);
}

void FileBrowserPanel::ArchiveListModel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (! owner) return;
    if (row < 0 || row >= owner->archiveRows.size()) return;

    const auto& R = owner->archiveRows[row];

    if (R.isFolder)
    {
        owner->showCollectionItem (R.folderId);
    }
    else
    {
        // Double-click = download then load into sampler (not just preview)
        ArchiveIntegration::downloadFile (R.downloadUrl,
            [this] (bool ok, juce::File localFile)
            {
                if (! ok) return;

                owner->stopPreview();   // stop streaming preview on load

                auto ext = localFile.getFileExtension().toLowerCase();

                // Prefer the owner-supplied callback so it can route by uiMode; only
                // fall back to the hardcoded sf2/sfz routing when nobody has wired
                // up onLoadRequest.
                if (owner->onLoadRequest)
                {
                    owner->onLoadRequest (localFile);
                }
                else if (ext == ".sf2" || ext == ".sfz")
                {
                    const bool sfzPlayer2Mode = (owner->processor.midiRouteMode.load (std::memory_order_relaxed)
                                                 == static_cast<int> (DysektProcessor::MidiRouteMode::SfzPlayer2));
                    if (sfzPlayer2Mode)
                        owner->processor.sfzPlayer2.loadFile (localFile, owner->processor.fileLoadPool);  // live MIDI engine
                    owner->processor.loadSoundFontAsync (localFile,
                        sfzPlayer2Mode ? SoundFontLoadTarget::SfzPlayer2 : SoundFontLoadTarget::Slicer);
                }
                else
                {
                    owner->processor.loadFileAsync (localFile);
                }

                if (owner->onFileLoaded) owner->onFileLoaded();
            });
    }
}

// ── Constructor / Destructor ─────────────────────────────────────────────────

FileBrowserPanel::FileBrowserPanel (DysektProcessor& p)
    : processor (p)
{
    // ── SfzFileBrowser — same widget used in the SFZ-Player panel ────────────
    sfzBrowser.setMode (SfzFileBrowser::Mode::kAddZone);  // Slicer tab is default
    sfzBrowser.onFileSingleClicked = [this] (const juce::File& f) { fileClicked (f); };
    sfzBrowser.onFileChosen = [this] (const juce::File& f) { fileDoubleClicked (f); };
    addAndMakeVisible (sfzBrowser);

    // ── Archive list view (initially hidden) ─────────────────────────────────
    archiveModel.owner = this;
    archiveList.setModel (&archiveModel);
    archiveList.setLookAndFeel (&smallLAF);
    archiveList.setRowHeight (26);
    archiveList.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    archiveList.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    addChildComponent (archiveList);

    // ── Audio preview setup ───────────────────────────────────────────────────
    formatManager.registerBasicFormats();
    sourcePlayer.setSource (&transport);
    transport.addChangeListener (this);

    // ── Play/Stop icon button ─────────────────────────────────────────────────
    playStopBtn.setTooltip ("Preview");
    playStopBtn.onClick = [this]
    {
        if (transport.isPlaying())
        {
            stopPreview();
        }
        else if (previewFile.existsAsFile())
        {
            startPreview (previewFile);
        }
        else if (streamPreviewUrl.isNotEmpty())
        {
            // Re-stream the archive file
            fileNameLabel.setText ("Buffering\u2026", juce::dontSendNotification);
            playStopBtn.setVisible  (false);
            volumeSlider.setVisible (false);
            repaint();

            auto url = streamPreviewUrl;
            const int replayGen = ++streamGeneration;

            ArchiveIntegration::streamPreview (url, formatManager,
                [this, url, replayGen] (juce::AudioFormatReader* reader)
                {
                    if (streamGeneration.load() != replayGen)
                    {
                        delete reader;
                        return;
                    }

                    if (reader != nullptr)
                    {
                        streamPreviewUrl = url;
                        fileNameLabel.setText (url.fromLastOccurrenceOf ("/", false, false),
                                               juce::dontSendNotification);
                        playStopBtn.setVisible  (true);
                        volumeSlider.setVisible (true);
                        startPreviewFromReader (reader);
                    }
                    else
                    {
                        fileNameLabel.setText (u8"Stream failed \u2014 check connection",
                                               juce::dontSendNotification);
                        streamPreviewUrl = {};
                    }
                    repaint();
                });
        }
    };
    addChildComponent (playStopBtn);

    // ── Volume slider ─────────────────────────────────────────────────────────
    volumeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    volumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volumeSlider.setRange (0.0, 1.0);
    volumeSlider.setValue (0.8);
    volumeSlider.setColour (juce::Slider::thumbColourId, getTheme().accent);
    volumeSlider.setColour (juce::Slider::trackColourId, getTheme().accent.withAlpha (0.25f));
    volumeSlider.onValueChange = [this]
    {
        transport.setGain ((float) volumeSlider.getValue());
    };
    transport.setGain (0.8f);
    addChildComponent (volumeSlider);

    // ── File name label ───────────────────────────────────────────────────────
    fileNameLabel.setFont (juce::Font (juce::FontOptions{}.withHeight (19.5f)));
    fileNameLabel.setColour (juce::Label::textColourId, getTheme().accent);
    fileNameLabel.setColour (juce::Label::backgroundColourId, juce::Colour (0x00000000));
    fileNameLabel.setMinimumHorizontalScale (0.5f);
    fileNameLabel.setEditable (false, false, false);
    addChildComponent (fileNameLabel);

    // Hiding browser filename bar & making editors read-only
    auto enforceReadOnly = [this]
    {
        std::function<void(juce::Component*)> walk = [&](juce::Component* comp)
        {
            if (auto* te = dynamic_cast<juce::TextEditor*> (comp))
            {
                te->setReadOnly (true);
                te->setCaretVisible (false);
                te->setMouseCursor (juce::MouseCursor::NormalCursor);
                te->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xFF000000));
                te->setColour (juce::TextEditor::outlineColourId, getTheme().separator);
                te->setColour (juce::TextEditor::focusedOutlineColourId, getTheme().accent.withAlpha (0.5f));
                te->setColour (juce::TextEditor::textColourId, getTheme().accent);
                te->setVisible (false);
            }
            if (auto* lb = dynamic_cast<juce::Label*> (comp))
            {
                lb->setEditable (false, false, false);
                lb->setColour (juce::Label::backgroundColourId, juce::Colour (0xFF000000));
                lb->setColour (juce::Label::textColourId, getTheme().accent);
            }
            for (int i = 0; i < comp->getNumChildComponents(); ++i)
                walk (comp->getChildComponent (i));
        };
        walk (&sfzBrowser);
    };

    juce::Timer::callAfterDelay (100,  [enforceReadOnly] { enforceReadOnly(); });
    juce::Timer::callAfterDelay (500,  [enforceReadOnly] { enforceReadOnly(); });

    // ── Local folder bookmarks ────────────────────────────────────────────────
    detectCloudFolders();
    loadCustomBookmarks();
    rebuildBookmarkBar();

    addBmBtn.setButtonText ("+");
    addBmBtn.setTooltip    ("Add folder bookmark");
    addBmBtn.setColour (juce::TextButton::buttonColourId,  getTheme().button);
    addBmBtn.setColour (juce::TextButton::textColourOnId,  getTheme().foreground);
    addBmBtn.setColour (juce::TextButton::textColourOffId, getTheme().foreground);
    addBmBtn.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Choose a folder to bookmark",
            juce::File::getSpecialLocation (juce::File::userHomeDirectory));

        fileChooser->launchAsync (
            juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result.isDirectory())
                {
                    bookmarks.add ({ result.getFileName(), result, true });
                    saveCustomBookmarks();
                    rebuildBookmarkBar();
                    resized();
                    repaint();
                }
            });
    };
    addAndMakeVisible (addBmBtn);

    // ── Internet Archive bookmarks ────────────────────────────────────────────
    addArchiveBtn.setButtonText ("IA");
    addArchiveBtn.setTooltip    ("Add Internet Archive bookmark");
    addArchiveBtn.setColour (juce::TextButton::buttonColourId,  getTheme().button);
    addArchiveBtn.setColour (juce::TextButton::textColourOnId,  getTheme().foreground);
    addArchiveBtn.setColour (juce::TextButton::textColourOffId, getTheme().foreground);
    addArchiveBtn.onClick = [this] { showArchiveUrlDialog(); };
    addAndMakeVisible (addArchiveBtn);

    loadArchiveBookmarks();
    rebuildArchiveButtons();
}

FileBrowserPanel::~FileBrowserPanel()
{
    stopTimer();
    archiveList.setLookAndFeel (nullptr);
    transport.stop();
    transport.setSource (nullptr);   // blocks until audio thread releases reader
    readerSource.reset();            // now safe to destroy
    sourcePlayer.setSource (nullptr);
    if (deviceManager.getCurrentAudioDevice() != nullptr)
        deviceManager.removeAudioCallback (&sourcePlayer);

    // ── Clean up temp preview downloads — these are never needed after session ends
    ArchiveIntegration::clearTemp();
}

// ── Timer (spinner) ──────────────────────────────────────────────────────────

void FileBrowserPanel::timerCallback()
{
    ++spinnerFrame;
    rebuildArchiveButtons();
    resized();
}

// ── Layout ───────────────────────────────────────────────────────────────────

void FileBrowserPanel::resized()
{
    // All layout happens inside the inner screen area (4 px inset on all sides).
    auto inner = getLocalBounds().reduced (4);

    // ── Preview bar at the BOTTOM of the inner area ───────────────────────────
    if (previewVisible)
    {
        auto bar = inner.removeFromBottom (kBarH);
        playStopBtn  .setBounds (bar.removeFromLeft (kBarH).reduced (4));
        volumeSlider .setBounds (bar.removeFromRight (90).reduced (4, 8));
        fileNameLabel.setBounds (bar.reduced (6, 4));
    }

    // ── Bookmark bar row 1: local folders ────────────────────────────────────
    {
        auto bmBar = inner.removeFromTop (kBmH);
        const int addW  = 30;
        const int gap   = 3;
        const int n     = bmBtns.size();
        const int avail = bmBar.getWidth() - addW - gap - 4;
        const int btnW  = n > 0 ? juce::jmin (90, juce::jmax (40, (avail - gap * (n - 1)) / n)) : 0;

        int bx = bmBar.getX() + 2;
        for (auto* btn : bmBtns)
        {
            btn->setBounds (bx, bmBar.getY() + 3, btnW, bmBar.getHeight() - 6);
            bx += btnW + gap;
        }
        addBmBtn.setBounds (bmBar.getRight() - addW - 2,
                            bmBar.getY() + 3, addW, bmBar.getHeight() - 6);
    }

    // ── Bookmark bar row 2: archive bookmarks ─────────────────────────────────
    {
        auto archBar = inner.removeFromTop (kBmH);
        const int addW  = 30;
        const int gap   = 3;
        const int n     = archiveBtns.size();
        const int avail = archBar.getWidth() - addW - gap - 4;
        const int btnW  = n > 0 ? juce::jmin (90, juce::jmax (40, (avail - gap * (n - 1)) / n)) : 0;

        int bx = archBar.getX() + 2;
        for (auto* btn : archiveBtns)
        {
            btn->setBounds (bx, archBar.getY() + 3, btnW, archBar.getHeight() - 6);
            bx += btnW + gap;
        }
        addArchiveBtn.setBounds (archBar.getRight() - addW - 2,
                                 archBar.getY() + 3, addW, archBar.getHeight() - 6);
    }

    // ── Main content area: archive list or local browser ──────────────────────
    if (archiveViewActive)
    {
        archiveList.setVisible (true);
        sfzBrowser.setVisible (false);
        archiveList.setBounds (inner);
    }
    else
    {
        archiveList.setVisible (false);
        sfzBrowser.setVisible (true);
        sfzBrowser.setBounds (inner);
    }
}

void FileBrowserPanel::paint (juce::Graphics& g)
{
    // Paint the LCD frame over the FULL component bounds (including the preview
    // bar).  The preview bar is drawn inside the frame, not outside it.
    const auto& T  = getTheme();
    const auto  ac = T.accent;
    const auto  b  = getLocalBounds();

    // ── Outer rounded shell ───────────────────────────────────────────────────
    juce::ColourGradient outerGrad (juce::Colour (0xFF131313), 0, 0,
                                    juce::Colour (0xFF0E0E0E), 0, (float) b.getHeight(), false);
    g.setGradientFill (outerGrad);
    g.fillRoundedRectangle (b.toFloat(), 4.0f);

    // Cheap win: soft glow behind the border, drawn before the hairline
    // stroke, so the border reads as lit from within rather than just
    // outlined.
    UIHelpers::drawPanelGlow (g, b.toFloat(), ac, 4.0f);

    g.setColour (ac.withAlpha (0.65f));
    g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 4.0f, 1.0f);

    // ── Inner screen area (inset 4 px all around) ────────────────────────────
    const auto screen = b.reduced (4);

    // Faint graph-paper grid rather than noise/scanlines, so the texture
    // doesn't fight with row scanning while browsing files.
    UIHelpers::drawTexturedPanel (g, screen.toFloat(), T.darkBar.darker (0.55f),
                                   UIHelpers::PanelZone::FileBrowser, 2.0f);

    // Top glow
    juce::ColourGradient glow (ac.withAlpha (0.06f), 0, (float) screen.getY(),
                                juce::Colours::transparentBlack, 0,
                                (float)(screen.getY() + 20), false);
    g.setGradientFill (glow);
    g.fillRoundedRectangle (screen.toFloat(), 2.0f);

    g.setColour (ac.withAlpha (0.12f));
    g.drawRoundedRectangle (screen.toFloat().expanded (0.5f), 2.0f, 1.0f);

    // ── Preview bar (bottom, inside frame) ────────────────────────────────────
    if (previewVisible)
    {
        const auto barRect = b.reduced (4).withTop (b.getBottom() - 4 - kBarH);

        g.setColour (T.darkBar.darker (0.6f));
        g.fillRect (barRect);
        g.setColour (ac.withAlpha (0.25f));
        g.drawLine ((float) barRect.getX(), (float) barRect.getY(),
                    (float) barRect.getRight(), (float) barRect.getY(), 1.0f);
    }

    // ── Bookmark bar row 1 background ────────────────────────────────────────
    {
        const auto bmRect = b.reduced (4).withHeight (kBmH).toFloat();
        juce::ColourGradient bmGrad (T.darkBar.darker (0.5f), 0, bmRect.getY(),
                                     T.darkBar.darker (0.3f), 0, bmRect.getBottom(), false);
        g.setGradientFill (bmGrad);
        g.fillRect (bmRect);
        g.setColour (T.accent.withAlpha (0.20f));
        g.drawLine (bmRect.getX(), bmRect.getBottom(),
                    bmRect.getRight(), bmRect.getBottom(), 1.0f);
    }

    // ── Bookmark bar row 2 background (archive) ───────────────────────────────
    {
        const auto archRect = b.reduced (4).withTop (b.getY() + 4 + kBmH)
                                           .withHeight (kBmH).toFloat();
        juce::ColourGradient bmGrad (T.darkBar.darker (0.45f), 0, archRect.getY(),
                                     T.darkBar.darker (0.25f), 0, archRect.getBottom(), false);
        g.setGradientFill (bmGrad);
        g.fillRect (archRect);
        g.setColour (T.accent.withAlpha (0.15f));
        g.drawLine (archRect.getX(), archRect.getBottom(),
                    archRect.getRight(), archRect.getBottom(), 1.0f);

        if (archiveViewActive && archiveListTitle.isNotEmpty())
        {
            g.setColour (T.accent.withAlpha (0.6f));
            g.setFont (juce::Font (juce::FontOptions{}.withHeight (18.0f)));
            g.drawText (archiveListTitle, archRect.reduced (4, 0).toNearestInt(),
                        juce::Justification::centredRight);
        }
    }
}

// ── FileBrowserListener ──────────────────────────────────────────────────────

void FileBrowserPanel::fileClicked (const juce::File& f)
{
    if (! f.existsAsFile()) return;

    const bool wasVisible = previewVisible;
    previewFile    = f;
    previewVisible = true;

    fileNameLabel.setText (f.getFileName(), juce::dontSendNotification);
    startPreview (f);

    playStopBtn.setVisible   (true);
    volumeSlider.setVisible  (true);
    fileNameLabel.setVisible (true);

    if (! wasVisible) resized();
    repaint();
}

void FileBrowserPanel::fileDoubleClicked (const juce::File& f)
{
    stopPreview();
    if (! f.existsAsFile()) return;

    auto ext = f.getFileExtension().toLowerCase();

    // Prefer the owner-supplied callback so it can route by uiMode (e.g. SF Player
    // preview vs. Slicer vs. SfzPlayer2 live engine). Only fall back to the
    // hardcoded sf2/sfz routing below when nobody has wired up onLoadRequest.
    if (onLoadRequest)
    {
        onLoadRequest (f);
        if (onFileLoaded) onFileLoaded();
        return;
    }

    if (ext == ".sf2" || ext == ".sfz")
    {
        const bool sfzPlayer2Mode = (processor.midiRouteMode.load (std::memory_order_relaxed)
                                     == static_cast<int> (DysektProcessor::MidiRouteMode::SfzPlayer2));
        if (sfzPlayer2Mode)
            processor.sfzPlayer2.loadFile (f, processor.fileLoadPool);  // live MIDI engine
        processor.loadSoundFontAsync (f,
            sfzPlayer2Mode ? SoundFontLoadTarget::SfzPlayer2 : SoundFontLoadTarget::Slicer);
        if (onFileLoaded) onFileLoaded();
        return;
    }

    processor.loadFileAsync (f);
    if (onFileLoaded) onFileLoaded();
}

// ── Preview engine ───────────────────────────────────────────────────────────

void FileBrowserPanel::startPreview (const juce::File& f)
{
    streamPreviewUrl = {};   // clear any previous stream URL — this is a local file
    if (! f.existsAsFile()) return;

    // ── Safely tear down any current playback before touching readerSource ──
    // transport.stop() is synchronous but the audio callback may still be
    // running — setSource(nullptr) blocks until the audio thread is done.
    transport.stop();
    transport.setSource (nullptr);   // blocks until audio thread releases reader
    readerSource.reset();            // now safe to destroy

    if (deviceManager.getCurrentAudioDevice() == nullptr)
    {
        deviceManager.initialise (0, 2, nullptr, true, {}, nullptr);
        deviceManager.addAudioCallback (&sourcePlayer);
    }

    auto* reader = formatManager.createReaderFor (f);
    if (reader == nullptr) return;   // unsupported format or file not ready

    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    transport.setSource (readerSource.get(), 0, nullptr, reader->sampleRate);
    transport.setGain ((float) volumeSlider.getValue());
    transport.setPosition (0.0);
    transport.start();

    updatePlayButton();
}

void FileBrowserPanel::stopPreview()
{
    ++streamGeneration;              // invalidate any in-flight stream callback
    streamPreviewUrl = {};
    transport.stop();
    transport.setSource (nullptr);   // blocks until audio thread is done
    readerSource.reset();            // now safe
    updatePlayButton();
}

void FileBrowserPanel::startPreviewFromReader (juce::AudioFormatReader* reader)
{
    // Same safe teardown as startPreview(File)
    transport.stop();
    transport.setSource (nullptr);
    readerSource.reset();

    if (reader == nullptr) return;

    if (deviceManager.getCurrentAudioDevice() == nullptr)
    {
        deviceManager.initialise (0, 2, nullptr, true, {}, nullptr);
        deviceManager.addAudioCallback (&sourcePlayer);
    }

    readerSource = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    transport.setSource (readerSource.get(), 0, nullptr, reader->sampleRate);
    transport.setGain ((float) volumeSlider.getValue());
    transport.setPosition (0.0);
    transport.start();

    updatePlayButton();
}

void FileBrowserPanel::updatePlayButton()
{
    if (transport.isPlaying())
        playStopBtn.setState (IconButton::Playing);
    else
        playStopBtn.setState (IconButton::Stopped);
}

void FileBrowserPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    juce::MessageManager::callAsync ([this]
    {
        if (! transport.isPlaying())
            updatePlayButton();
    });
}

void FileBrowserPanel::refreshTheme()
{
    repaint();
    sfzBrowser.repaint();
}

// ── Cloud bookmark detection ──────────────────────────────────────────────────

void FileBrowserPanel::detectCloudFolders()
{
    auto home    = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    auto appData = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);

    // Dedup helper: only add if the directory exists and isn't already listed.
    auto tryAdd = [&] (const juce::String& name, const juce::File& dir)
    {
        if (! dir.isDirectory()) return;
        for (auto& b : bookmarks)
            if (b.path == dir) return;
        bookmarks.add ({ name, dir, false });
    };

    // ── OneDrive ──────────────────────────────────────────────────────────────
    // The user can relocate the OneDrive folder, so we read the authoritative
    // path from the registry instead of guessing.
    //
    // Helper: open a registry key and read a REG_SZ / REG_EXPAND_SZ value.
    auto readRegSz = [] (HKEY hive, const wchar_t* subKey,
                         const wchar_t* valueName) -> juce::String
    {
        HKEY  key     = nullptr;
        DWORD type    = 0;
        DWORD bufSize = 0;

        if (RegOpenKeyExW (hive, subKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
            return {};

        RegQueryValueExW (key, valueName, nullptr, &type, nullptr, &bufSize);

        juce::String result;
        if ((type == REG_SZ || type == REG_EXPAND_SZ) && bufSize > 0)
        {
            juce::HeapBlock<wchar_t> buf (bufSize / sizeof (wchar_t) + 1, true);
            if (RegQueryValueExW (key, valueName, nullptr, &type,
                                  reinterpret_cast<LPBYTE> (buf.getData()),
                                  &bufSize) == ERROR_SUCCESS)
                result = juce::String (buf.getData());
        }
        RegCloseKey (key);
        return result;
    };

    // 1. Personal OneDrive  (HKCU\Software\Microsoft\OneDrive  "UserFolder")
    {
        auto path = readRegSz (HKEY_CURRENT_USER,
                               L"Software\\Microsoft\\OneDrive",
                               L"UserFolder");
        if (path.isNotEmpty())
            tryAdd ("OneDrive", juce::File (path));
    }

    // 2. Work / school OneDrive accounts (one sub-key per account GUID under
    //    HKCU\Software\Microsoft\OneDrive\Accounts\<GUID>)
    {
        HKEY accountsKey = nullptr;
        if (RegOpenKeyExW (HKEY_CURRENT_USER,
                           L"Software\\Microsoft\\OneDrive\\Accounts",
                           0, KEY_READ, &accountsKey) == ERROR_SUCCESS)
        {
            wchar_t subName[256] = {};
            DWORD   subNameLen   = static_cast<DWORD> (std::size (subName));

            for (DWORD i = 0;
                 RegEnumKeyExW (accountsKey, i, subName, &subNameLen,
                                nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
                 ++i, subNameLen = static_cast<DWORD> (std::size (subName)))
            {
                juce::String sub = "Software\\Microsoft\\OneDrive\\Accounts\\"
                                   + juce::String (subName);

                auto path = readRegSz (HKEY_CURRENT_USER, sub.toWideCharPointer(),
                                       L"UserFolder");
                if (path.isEmpty()) continue;

                // "DisplayName" distinguishes "Personal" from a work tenant name.
                auto display = readRegSz (HKEY_CURRENT_USER, sub.toWideCharPointer(),
                                          L"DisplayName");
                juce::String label = display.isNotEmpty()
                                     ? "OneDrive \u2013 " + display
                                     : "OneDrive";
                tryAdd (label, juce::File (path));
            }
            RegCloseKey (accountsKey);
        }
    }

    // 3. Hard-coded fallbacks for very old OneDrive installs that pre-date the
    //    registry key, or installs where the key is missing.
    tryAdd ("OneDrive",            home.getChildFile ("OneDrive"));
    tryAdd ("OneDrive - Personal", home.getChildFile ("OneDrive - Personal"));

    // ── Dropbox ───────────────────────────────────────────────────────────────
    // Dropbox writes the real sync root into %APPDATA%\Dropbox\info.json:
    //   {"personal":{"path":"C:\\Users\\..."},"business":{"path":"..."}}
    // We do a lightweight string scan — no JSON parser needed since the
    // format is stable and documented by Dropbox.
    {
        bool foundViaJson = false;
        auto infoJson = appData.getChildFile ("Dropbox/info.json");

        if (infoJson.existsAsFile())
        {
            auto text = infoJson.loadFileAsString();
            int  pos  = 0;

            while (true)
            {
                int keyPos = text.indexOf (pos, "\"path\"");
                if (keyPos < 0) break;

                int colon  = text.indexOf (keyPos + 6, ":");
                if (colon  < 0) break;

                int q1 = text.indexOf (colon + 1, "\"");
                if (q1 < 0) break;

                // Walk to the closing quote, honouring backslash escapes.
                int q2 = q1 + 1;
                while (q2 < text.length())
                {
                    if (text[q2] == '\\') { q2 += 2; continue; }
                    if (text[q2] == '"')  break;
                    ++q2;
                }

                auto rawPath = text.substring (q1 + 1, q2)
                                   .replace ("\\\\", "\\");
                if (rawPath.isNotEmpty())
                {
                    tryAdd ("Dropbox", juce::File (rawPath));
                    foundViaJson = true;
                }
                pos = q2 + 1;
            }
        }

        // Fallback for installs where info.json is absent.
        if (! foundViaJson)
        {
            tryAdd ("Dropbox",            home.getChildFile ("Dropbox"));
            tryAdd ("Dropbox (Personal)", home.getChildFile ("Dropbox (Personal)"));
        }
    }

    // ── Google Drive ──────────────────────────────────────────────────────────
    // "Google Drive for Desktop" mounts as a virtual lettered drive.
    // The chosen drive letter is stored in the registry.
    {
        auto mountPoint = readRegSz (HKEY_CURRENT_USER,
                                     L"Software\\Google\\DriveFS",
                                     L"MountPoint");
        if (mountPoint.isNotEmpty())
        {
            // Each signed-in Google account gets its own sub-folder named after
            // the account email address under the mount root.
            auto mountRoot = juce::File (mountPoint + "\\");
            auto accounts  = mountRoot.findChildFiles (juce::File::findDirectories, false);

            if (accounts.isEmpty())
            {
                tryAdd ("Google Drive", mountRoot);
            }
            else
            {
                for (auto& acct : accounts)
                {
                    auto label = (accounts.size() == 1)
                                 ? juce::String ("Google Drive")
                                 : "Google Drive (" + acct.getFileName() + ")";
                    tryAdd (label, acct);
                }
            }
        }

        // Fallback: older "Backup and Sync" client syncs into the home directory.
        tryAdd ("Google Drive",          home.getChildFile ("Google Drive"));
        tryAdd ("Google Drive (My Drive)",home.getChildFile ("Google Drive (My Drive)"));
    }

    // ── Box ───────────────────────────────────────────────────────────────────
    // Box stores its sync root in HKCU\Software\Box\Box  "SyncRootFolder".
    {
        auto boxPath = readRegSz (HKEY_CURRENT_USER,
                                  L"Software\\Box\\Box",
                                  L"SyncRootFolder");
        if (boxPath.isNotEmpty())
            tryAdd ("Box", juce::File (boxPath));

        // Fallback for Box Sync (older client) which used a home-relative folder.
        tryAdd ("Box",      home.getChildFile ("Box"));
        tryAdd ("Box Sync", home.getChildFile ("Box Sync"));
    }
}
// ── Local bookmark persistence ────────────────────────────────────────────────

static juce::File getBookmarksFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("DYSEKT-SF/bookmarks.txt");
}

void FileBrowserPanel::loadCustomBookmarks()
{
    auto f = getBookmarksFile();
    if (! f.existsAsFile()) return;

    juce::StringArray lines;
    f.readLines (lines);
    for (auto& line : lines)
    {
        juce::File dir (line.trim());
        if (dir.isDirectory())
        {
            bool dupe = false;
            for (auto& b : bookmarks)
                if (b.path == dir) { dupe = true; break; }
            if (! dupe)
                bookmarks.add ({ dir.getFileName(), dir, true });
        }
    }
}

void FileBrowserPanel::saveCustomBookmarks()
{
    auto f = getBookmarksFile();
    f.getParentDirectory().createDirectory();

    juce::StringArray lines;
    for (auto& b : bookmarks)
        if (b.removable)
            lines.add (b.path.getFullPathName());

    f.replaceWithText (lines.joinIntoString ("\n"));
}

void FileBrowserPanel::rebuildBookmarkBar()
{
    bmBtns.clear();
    const auto& T = getTheme();

    for (int i = 0; i < bookmarks.size(); ++i)
    {
        auto* btn = bmBtns.add (new RemovableButton());
        btn->setButtonText (bookmarks[i].name);
        btn->setTooltip    (bookmarks[i].path.getFullPathName());
        btn->setColour (juce::TextButton::buttonColourId,  T.button);
        btn->setColour (juce::TextButton::buttonOnColourId,T.accent.withAlpha (0.2f));
        btn->setColour (juce::TextButton::textColourOffId, T.foreground);
        btn->setColour (juce::TextButton::textColourOnId,  T.accent);

        btn->onClick = [this, i]
        {
            exitArchiveView();
            sfzBrowser.setRootDirectory (bookmarks[i].path);
        };

        if (bookmarks[i].removable)
        {
            btn->onRightClick = [this, i]
            {
                juce::PopupMenu menu;
                menu.addItem (1, "Remove bookmark");
                menu.showMenuAsync (juce::PopupMenu::Options(),
                    [this, i] (int result)
                    {
                        if (result == 1)
                        {
                            bookmarks.remove (i);
                            saveCustomBookmarks();
                            rebuildBookmarkBar();
                            resized();
                            repaint();
                        }
                    });
            };
        }

        addAndMakeVisible (btn);
    }
}

// ── Internet Archive bookmark persistence ─────────────────────────────────────

juce::File FileBrowserPanel::getArchiveBookmarksFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("DYSEKT-SF/archive_bookmarks.txt");
}

void FileBrowserPanel::loadArchiveBookmarks()
{
    auto f = getArchiveBookmarksFile();
    if (! f.existsAsFile()) return;

    juce::StringArray lines;
    f.readLines (lines);

    for (auto& line : lines)
    {
        auto url = line.trim();
        if (url.isEmpty()) continue;
        if (! ArchiveIntegration::isValidArchiveUrl (url)) continue;

        bool dupe = false;
        for (auto& b : archiveBookmarks)
            if (b.url == url) { dupe = true; break; }
        if (dupe) continue;

        // Add as pending while we re-resolve the title
        ArchiveBookmark bm;
        bm.url     = url;
        bm.title   = ArchiveIntegration::identifierFromUrl (url);
        bm.pending = true;
        archiveBookmarks.add (bm);

        // Fire resolve (captures index)
        int idx = archiveBookmarks.size() - 1;
        ArchiveIntegration::fetchItem (url, [this, idx] (bool ok, ArchiveIntegration::Item item)
        {
            if (idx >= archiveBookmarks.size()) return;
            auto& bm = archiveBookmarks.getReference (idx);
            bm.pending      = false;
            bm.isCollection = item.isCollection;
            if (ok && item.title.isNotEmpty())
                bm.title = item.title;

            // Stop spinner if no more pending
            bool anyPending = false;
            for (auto& b : archiveBookmarks)
                if (b.pending) { anyPending = true; break; }
            if (! anyPending) stopTimer();

            rebuildArchiveButtons();
            resized();
            repaint();
        });
    }

    if (! archiveBookmarks.isEmpty())
    {
        bool anyPending = false;
        for (auto& b : archiveBookmarks)
            if (b.pending) { anyPending = true; break; }
        if (anyPending)
            startTimer (400);
    }
}

void FileBrowserPanel::saveArchiveBookmarks()
{
    auto f = getArchiveBookmarksFile();
    f.getParentDirectory().createDirectory();

    juce::StringArray lines;
    for (auto& b : archiveBookmarks)
        if (! b.pending)
            lines.add (b.url);

    f.replaceWithText (lines.joinIntoString ("\n"));
}

// ── Archive bookmark UI ───────────────────────────────────────────────────────

static const juce::String kSpinnerFrames[] = { "|", "/", "-", "\\" };

void FileBrowserPanel::rebuildArchiveButtons()
{
    archiveBtns.clear();
    const auto& T = getTheme();

    for (int i = 0; i < archiveBookmarks.size(); ++i)
    {
        const auto& bm = archiveBookmarks[i];
        auto* btn = archiveBtns.add (new RemovableButton());

        if (bm.pending)
        {
            // Show spinner
            btn->setButtonText (kSpinnerFrames[spinnerFrame % 4]);
            btn->setTooltip ("Resolving: " + bm.url);
            btn->setEnabled (false);
        }
        else
        {
            btn->setButtonText (bm.title);
            btn->setTooltip    (bm.url);
            btn->setEnabled (true);
        }

        btn->setColour (juce::TextButton::buttonColourId,   T.button);
        btn->setColour (juce::TextButton::buttonOnColourId,  T.accent.withAlpha (0.2f));
        btn->setColour (juce::TextButton::textColourOffId,   T.foreground.withAlpha (bm.pending ? 0.4f : 1.0f));
        btn->setColour (juce::TextButton::textColourOnId,    T.accent);

        if (! bm.pending)
        {
            btn->onClick = [this, i] { showArchiveItem (i); };

            btn->onRightClick = [this, i]
            {
                juce::PopupMenu menu;
                menu.addItem (1, "Remove Archive bookmark");
                menu.addSeparator();

                auto cacheBytes = ArchiveIntegration::getCacheSize();
                juce::String cacheLabel = "Clear download cache";
                if (cacheBytes > 0)
                    cacheLabel += " (" + juce::String (cacheBytes / (1024 * 1024)) + " MB)";
                menu.addItem (2, cacheLabel, cacheBytes > 0);

                menu.showMenuAsync (juce::PopupMenu::Options(),
                    [this, i] (int result)
                    {
                        if (result == 1)
                        {
                            if (activeArchiveIndex == i) exitArchiveView();
                            archiveBookmarks.remove (i);
                            saveArchiveBookmarks();
                            rebuildArchiveButtons();
                            resized();
                            repaint();
                        }
                        else if (result == 2)
                        {
                            ArchiveIntegration::clearCache();
                        }
                    });
            };
        }

        addAndMakeVisible (btn);
    }
}

void FileBrowserPanel::showArchiveMessage (const juce::String& title, const juce::String& body)
{
    if (archiveMessageOverlay)
    {
        if (auto* p = archiveMessageOverlay->getParentComponent())
            p->removeChildComponent (archiveMessageOverlay.get());
        archiveMessageOverlay.reset();
    }

    auto overlay = std::make_unique<ArchiveMessageOverlay> (title, body);

    overlay->onDismiss = [this]
    {
        juce::MessageManager::callAsync ([this]
        {
            if (archiveMessageOverlay)
            {
                if (auto* p = archiveMessageOverlay->getParentComponent())
                    p->removeChildComponent (archiveMessageOverlay.get());
                archiveMessageOverlay.reset();
            }
        });
    };

    archiveMessageOverlay = std::move (overlay);

    if (auto* top = getTopLevelComponent())
    {
        top->addAndMakeVisible (*archiveMessageOverlay);
        archiveMessageOverlay->setBounds (top->getLocalBounds());
        archiveMessageOverlay->toFront (true);
    }
}

void FileBrowserPanel::showArchiveUrlDialog()
{
    // Tear down any existing overlay first
    if (archiveUrlOverlay)
    {
        if (auto* p = archiveUrlOverlay->getParentComponent())
            p->removeChildComponent (archiveUrlOverlay.get());
        archiveUrlOverlay.reset();
    }

    auto overlay = std::make_unique<ArchiveUrlOverlay>();

    overlay->onResult = [this] (const juce::String& url, bool cancelled)
    {
        // Defer teardown off the call stack (same pattern as AddZoneOverlay fix)
        juce::MessageManager::callAsync ([this]
        {
            if (archiveUrlOverlay)
            {
                if (auto* p = archiveUrlOverlay->getParentComponent())
                    p->removeChildComponent (archiveUrlOverlay.get());
                archiveUrlOverlay.reset();
            }
        });

        if (cancelled || url.isEmpty())
            return;

        if (! ArchiveIntegration::isValidArchiveUrl (url))
        {
            // Re-open with an error shown — simplest approach is to just
            // re-invoke so the user can correct the URL
            juce::MessageManager::callAsync ([this] { showArchiveUrlDialog(); });
            return;
        }

        // Check for duplicate
        for (auto& b : archiveBookmarks)
            if (b.url == url) return;

        resolveAndAddArchiveBookmark (url);
    };

    archiveUrlOverlay = std::move (overlay);

    if (auto* top = getTopLevelComponent())
    {
        top->addAndMakeVisible (*archiveUrlOverlay);
        archiveUrlOverlay->setBounds (top->getLocalBounds());
        archiveUrlOverlay->toFront (true);
    }
}

void FileBrowserPanel::resolveAndAddArchiveBookmark (const juce::String& url)
{
    ArchiveBookmark bm;
    bm.url     = url;
    bm.title   = ArchiveIntegration::identifierFromUrl (url);
    bm.pending = true;
    archiveBookmarks.add (bm);

    int idx = archiveBookmarks.size() - 1;

    rebuildArchiveButtons();
    resized();

    if (! isTimerRunning())
        startTimer (400);

    ArchiveIntegration::fetchItem (url, [this, idx, url] (bool ok, ArchiveIntegration::Item item)
    {
        if (idx >= archiveBookmarks.size()) return;
        auto& bm = archiveBookmarks.getReference (idx);
        bm.pending      = false;
        bm.isCollection = item.isCollection;

        if (! ok || (item.audioFiles.isEmpty() && ! item.isCollection))
        {
            // No usable content
            archiveBookmarks.remove (idx);
            rebuildArchiveButtons();
            resized();

            showArchiveMessage ("No Audio Found",
                "No supported audio files were found at that URL.\n\n"
                "Only WAV, FLAC, MP3, OGG, and AIFF files are supported.");
        }
        else
        {
            if (ok && item.title.isNotEmpty())
                bm.title = item.title;

            saveArchiveBookmarks();
            rebuildArchiveButtons();
            resized();
        }

        bool anyPending = false;
        for (auto& b : archiveBookmarks)
            if (b.pending) { anyPending = true; break; }
        if (! anyPending) stopTimer();

        repaint();
    });
}

// ── Archive list view ─────────────────────────────────────────────────────────

void FileBrowserPanel::showArchiveItem (int bookmarkIndex)
{
    if (bookmarkIndex < 0 || bookmarkIndex >= archiveBookmarks.size()) return;

    const auto& bm = archiveBookmarks[bookmarkIndex];
    activeArchiveIndex = bookmarkIndex;

    if (bm.isCollection)
    {
        archiveViewActive  = true;
        archiveRows.clear();
        archiveListTitle   = "Loading collection\u2026";
        archiveList.updateContent();
        resized();
        repaint();

        ArchiveIntegration::fetchCollection (
            ArchiveIntegration::identifierFromUrl (bm.url),
            [this] (bool ok, juce::Array<ArchiveIntegration::CollectionEntry> entries)
            {
                archiveRows.clear();
                if (ok)
                {
                    archiveListTitle = juce::String (entries.size()) + " items";
                    for (auto& e : entries)
                    {
                        ArchiveRow r;
                        r.name     = e.title.isNotEmpty() ? e.title : e.identifier;
                        r.isFolder = true;
                        r.folderId = e.identifier;
                        archiveRows.add (r);
                    }
                }
                else
                {
                    archiveListTitle = "Failed to load collection";
                }
                archiveList.updateContent();
                resized();
                repaint();
            });
    }
    else
    {
        archiveViewActive  = true;
        archiveRows.clear();
        archiveListTitle   = "Loading\u2026";
        archiveList.updateContent();
        resized();
        repaint();

        ArchiveIntegration::fetchItem (bm.url,
            [this] (bool ok, ArchiveIntegration::Item item)
            {
                archiveRows.clear();
                if (ok)
                {
                    archiveListTitle = item.title;
                    for (auto& af : item.audioFiles)
                    {
                        ArchiveRow r;
                        r.name        = af.name;
                        r.format      = af.format;
                        r.downloadUrl = af.downloadUrl;
                        r.sizeBytes   = af.sizeBytes;
                        r.isFolder    = false;
                        archiveRows.add (r);
                    }
                }
                else
                {
                    archiveListTitle = "Failed to load";
                }
                archiveList.updateContent();
                resized();
                repaint();
            });
    }
}

void FileBrowserPanel::showCollectionItem (const juce::String& collectionId)
{
    archiveViewActive  = true;
    archiveRows.clear();
    archiveListTitle   = "Loading " + collectionId + "\u2026";
    archiveList.updateContent();
    resized();
    repaint();

    ArchiveIntegration::fetchItem (collectionId,
        [this] (bool ok, ArchiveIntegration::Item item)
        {
            archiveRows.clear();
            if (ok)
            {
                archiveListTitle = item.title;
                for (auto& af : item.audioFiles)
                {
                    ArchiveRow r;
                    r.name        = af.name;
                    r.format      = af.format;
                    r.downloadUrl = af.downloadUrl;
                    r.sizeBytes   = af.sizeBytes;
                    r.isFolder    = false;
                    archiveRows.add (r);
                }
            }
            else
            {
                archiveListTitle = "Failed to load";
            }
            archiveList.updateContent();
            resized();
            repaint();
        });
}

void FileBrowserPanel::loadArchiveFile (const ArchiveRow& row)
{
    if (row.downloadUrl.isEmpty()) return;

    // Show "Buffering…" immediately — no waiting for a download to finish
    stopPreview();
    streamPreviewUrl = {};
    fileNameLabel.setText ("Buffering: " + row.name, juce::dontSendNotification);
    fileNameLabel.setVisible (true);
    playStopBtn.setVisible  (false);
    volumeSlider.setVisible (false);
    previewVisible = true;
    resized();
    repaint();

    const int myGeneration = ++streamGeneration;

    ArchiveIntegration::streamPreview (row.downloadUrl, formatManager,
        [this, name = row.name, url = row.downloadUrl, myGeneration] (juce::AudioFormatReader* reader)
        {
            // If stopPreview() or a newer stream was started, discard this result
            if (streamGeneration.load() != myGeneration)
            {
                delete reader;
                return;
            }

            if (reader != nullptr)
            {
                previewFile      = {};    // no local file for a streaming preview
                streamPreviewUrl = url;
                previewVisible   = true;

                fileNameLabel.setText    (name, juce::dontSendNotification);
                fileNameLabel.setVisible (true);
                playStopBtn.setVisible   (true);
                volumeSlider.setVisible  (true);

                startPreviewFromReader (reader);   // takes ownership
            }
            else
            {
                streamPreviewUrl = {};
                fileNameLabel.setText (u8"Stream failed \u2014 check connection",
                                       juce::dontSendNotification);
                playStopBtn.setVisible  (false);
                volumeSlider.setVisible (false);
            }
            resized();
            repaint();
        });
}

void FileBrowserPanel::exitArchiveView()
{
    archiveViewActive  = false;
    activeArchiveIndex = -1;
    archiveRows.clear();
    archiveListTitle.clear();
    archiveList.updateContent();
    resized();
    repaint();
}
