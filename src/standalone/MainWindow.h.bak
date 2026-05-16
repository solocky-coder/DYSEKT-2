#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "../PluginProcessor.h"
#include "../PluginEditor.h"
#include "../sequencer/MidiClip.h"

//==============================================================================
//  PatternManagerComponent
//
//  Sidebar panel for the standalone app that lets the user manage multiple
//  MIDI clips (patterns): add, rename, duplicate, reorder, delete.
//  The active pattern is loaded into SequencerEngine.
//==============================================================================
class PatternManagerComponent : public juce::Component,
                                private juce::ListBoxModel
{
public:
    explicit PatternManagerComponent (DysektProcessor& proc)
        : processor (proc)
    {
        // Toolbar buttons
        auto makeBtn = [this] (juce::TextButton& b, const juce::String& t)
        {
            b.setButtonText (t);
            b.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF1C2028));
            b.setColour (juce::TextButton::textColourOffId, juce::Colour (0xFFCCD0D8));
            addAndMakeVisible (b);
        };

        makeBtn (addBtn,   "+");
        makeBtn (dupBtn,   "DUP");
        makeBtn (delBtn,   "DEL");
        makeBtn (upBtn,    "▲");
        makeBtn (downBtn,  "▼");

        addBtn .onClick = [this] { addPattern();       };
        dupBtn .onClick = [this] { duplicatePattern(); };
        delBtn .onClick = [this] { deletePattern();    };
        upBtn  .onClick = [this] { movePattern (-1);   };
        downBtn.onClick = [this] { movePattern (+1);   };

        // Pattern list
        patternList.setModel (this);
        patternList.setColour (juce::ListBox::backgroundColourId,
                               juce::Colour (0xFF090910));
        patternList.setColour (juce::ListBox::outlineColourId,
                               juce::Colour (0xFF1C2028));
        patternList.setRowHeight (26);
        addAndMakeVisible (patternList);

        // Title
        title.setText ("PATTERNS", juce::dontSendNotification);
        title.setFont (DysektLookAndFeel::makeFont (11.f, true));
        title.setColour (juce::Label::textColourId,
                         juce::Colour::fromFloatRGBA (0.25f, 0.85f, 0.85f, 1.0f));
        title.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (title);

        // Seed with one default pattern
        addPattern ("Pattern 1");
    }

    //==========================================================================
    void resized() override
    {
        auto r = getLocalBounds();
        title.setBounds (r.removeFromTop (22));

        auto btnRow = r.removeFromTop (26);
        const int bw = btnRow.getWidth() / 5;
        addBtn .setBounds (btnRow.removeFromLeft (bw));
        dupBtn .setBounds (btnRow.removeFromLeft (bw));
        delBtn .setBounds (btnRow.removeFromLeft (bw));
        upBtn  .setBounds (btnRow.removeFromLeft (bw));
        downBtn.setBounds (btnRow);

        patternList.setBounds (r);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF0D0D14));
        g.setColour (juce::Colour (0xFF1C2028));
        g.drawRect (getLocalBounds(), 1);
    }

    //==========================================================================
    //  Public API
    int getNumPatterns() const   { return patterns.size(); }
    int getActivePattern() const { return activeIdx; }

    MidiClip& getPattern (int i) { return *patterns[i].clip; }

    void setActivePattern (int i)
    {
        if (! juce::isPositiveAndBelow (i, (int)patterns.size())) return;
        activeIdx = i;

        // Swap the active clip into the engine
        processor.sequencer.getClip().setNotes (patterns[i].clip->getNotes());
        processor.sequencer.getClip().setLengthTicks (patterns[i].clip->getLengthTicks());

        patternList.repaint();
    }

private:
    //==========================================================================
    struct PatternEntry
    {
        juce::String             name;
        std::unique_ptr<MidiClip> clip;
    };

    DysektProcessor&            processor;
    std::vector<PatternEntry>   patterns;
    int                         activeIdx = 0;

    juce::ListBox    patternList;
    juce::TextButton addBtn, dupBtn, delBtn, upBtn, downBtn;
    juce::Label      title;

    //==========================================================================
    void addPattern (const juce::String& name = juce::String())
    {
        PatternEntry e;
        e.name = name.isEmpty()
            ? ("Pattern " + juce::String (patterns.size() + 1))
            : name;
        e.clip = std::make_unique<MidiClip>();
        patterns.push_back (std::move (e));
        patternList.updateContent();
        setActivePattern (patterns.size() - 1);
    }

    void duplicatePattern()
    {
        if (! juce::isPositiveAndBelow (activeIdx, (int)patterns.size())) return;
        PatternEntry e;
        e.name = patterns[activeIdx].name + " (copy)";
        e.clip = std::make_unique<MidiClip>();
        e.clip->setNotes (patterns[activeIdx].clip->getNotes());
        e.clip->setLengthTicks (patterns[activeIdx].clip->getLengthTicks());
        patterns.insert (patterns.begin() + activeIdx + 1, std::move (e));
        patternList.updateContent();
        setActivePattern (activeIdx + 1);
    }

    void deletePattern()
    {
        if (patterns.size() <= 1) return;   // always keep at least one
        patterns.erase (patterns.begin() + activeIdx);
        activeIdx = juce::jlimit (0, (int)patterns.size() - 1, activeIdx);
        patternList.updateContent();
        setActivePattern (activeIdx);
    }

    void movePattern (int delta)
    {
        const int newIdx = activeIdx + delta;
        if (! juce::isPositiveAndBelow (newIdx, patterns.size())) return;
        std::swap (patterns[activeIdx], patterns[newIdx]);
        activeIdx = newIdx;
        patternList.updateContent();
        patternList.repaint();
    }

    //==========================================================================
    //  ListBoxModel
    int getNumRows() override { return patterns.size(); }

    void paintListBoxItem (int row, juce::Graphics& g,
                           int w, int h, bool selected) override
    {
        const bool isActive = (row == activeIdx);
        g.fillAll (isActive ? juce::Colour (0xFF1C3040)
                  : selected ? juce::Colour (0xFF181822)
                             : juce::Colour (0xFF090910));

        g.setFont (DysektLookAndFeel::makeMonoFont (11.f));
        g.setColour (isActive
            ? juce::Colour::fromFloatRGBA (0.25f, 0.85f, 0.85f, 1.0f)
            : juce::Colour (0xFFCCD0D8));
        g.drawText (patterns[row].name, 8, 0, w - 8, h,
                    juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked (int row, const juce::MouseEvent&) override
    {
        setActivePattern (row);
    }

    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
    {
        // Inline rename via manually constructed AlertWindow (JUCE 8 compatible)
        auto* aw = new juce::AlertWindow ("Rename Pattern",
                                          "Enter new name:",
                                          juce::MessageBoxIconType::QuestionIcon);
        aw->addTextEditor ("name", patterns[row].name);
        aw->addButton ("OK",     1, juce::KeyPress (juce::KeyPress::returnKey));
        aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

        auto* awPtr = aw;
        aw->enterModalState (true,
            juce::ModalCallbackFunction::create ([this, row, awPtr] (int result)
            {
                if (result == 1)
                {
                    auto name = awPtr->getTextEditorContents ("name");
                    if (name.isNotEmpty())
                    {
                        patterns[row].name = name;
                        patternList.repaint();
                    }
                }
            }),
            true);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternManagerComponent)
};


//==============================================================================
//  MainWindow
//
//  The standalone app's main window.  Layout:
//
//    ┌─────────────────────────────────────────────────┐
//    │  MenuBar (File / Audio / Help)                  │
//    ├──────────────────┬──────────────────────────────┤
//    │                  │                              │
//    │  Pattern Manager │   DysektEditor               │
//    │  (160px wide)    │   (full plugin UI)           │
//    │                  │                              │
//    └──────────────────┴──────────────────────────────┘
//
//  Audio I/O is handled by AudioDeviceManager — same as JUCE's
//  StandalonePluginHolder, but with our own UI for device selection.
//==============================================================================
class MainWindow : public juce::DocumentWindow,
                   public juce::MenuBarModel,
                   private juce::ChangeListener
{
public:
    static constexpr int kPatternPanelW = 160;
    static constexpr int kMenuH         = 24;

    //==========================================================================
    explicit MainWindow (const juce::String& appName)
        : DocumentWindow (appName,
                          juce::Colour (0xFF000000),
                          DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);

        // ── Audio device setup ────────────────────────────────────────────
        deviceManager.initialiseWithDefaultDevices (0, 2);
        deviceManager.addChangeListener (this);

        // ── Plugin processor + editor ─────────────────────────────────────
        processor = std::make_unique<DysektProcessor>();
        processor->prepareToPlay (44100.0, 512);

        editor = std::make_unique<DysektEditor> (*processor);

        // ── Audio callback ────────────────────────────────────────────────
        player.setProcessor (processor.get());
        deviceManager.addAudioCallback (&player);

        // ── MIDI input ────────────────────────────────────────────────────
        const auto midiInputs = juce::MidiInput::getAvailableDevices();
        for (const auto& input : midiInputs)
        {
            deviceManager.setMidiInputDeviceEnabled (input.identifier, true);
            deviceManager.addMidiInputDeviceCallback (input.identifier, &player);
        }

        // ── Main content component ────────────────────────────────────────
        contentHolder = std::make_unique<ContentHolder> (*processor, *editor);
        setContentOwned (contentHolder.release(), true);

        // ── Menu ──────────────────────────────────────────────────────────
        menuBar = std::make_unique<juce::MenuBarComponent> (this);
        setMenuBar (this, kMenuH);

        // ── Initial size ──────────────────────────────────────────────────
        const int plugW = editor->getWidth();
        const int plugH = editor->getHeight();
        setSize (plugW + kPatternPanelW, plugH + kMenuH);
        setVisible (true);
        centreWithSize (getWidth(), getHeight());
    }

    ~MainWindow() override
    {
        setMenuBar (nullptr);
        deviceManager.removeAudioCallback (&player);
        deviceManager.removeChangeListener (this);
        player.setProcessor (nullptr);
    }

    //==========================================================================
    //  MenuBarModel
    juce::StringArray getMenuBarNames() override
    {
        return { "File", "Audio / MIDI", "Help" };
    }

    juce::PopupMenu getMenuForIndex (int menuIndex,
                                     const juce::String& /*name*/) override
    {
        juce::PopupMenu menu;

        if (menuIndex == 0)  // File
        {
            menu.addItem (1, "New Project");
            menu.addItem (2, "Open Project...");
            menu.addItem (3, "Save Project");
            menu.addItem (4, "Save Project As...");
            menu.addSeparator();
            menu.addItem (5, "Export MIDI Clip...");
            menu.addSeparator();
            menu.addItem (6, "Quit");
        }
        else if (menuIndex == 1)  // Audio / MIDI
        {
            menu.addItem (10, "Audio Settings...");
            menu.addItem (11, "MIDI Settings...");
        }
        else  // Help
        {
            menu.addItem (20, "About DYSEKT");
        }

        return menu;
    }

    void menuItemSelected (int itemId, int /*menuIndex*/) override
    {
        switch (itemId)
        {
            case 1:  newProject();           break;
            case 2:  openProject();          break;
            case 3:  saveProject();          break;
            case 4:  saveProjectAs();        break;
            case 5:  exportMidiClip();       break;
            case 6:  juce::JUCEApplication::getInstance()->systemRequestedQuit(); break;
            case 10: showAudioSettings();    break;
            case 20: showAbout();            break;
            default: break;
        }
    }

    //==========================================================================
    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    //==========================================================================
    //  ContentHolder: horizontal split — pattern panel left, plugin editor right
    class ContentHolder : public juce::Component
    {
    public:
        ContentHolder (DysektProcessor& proc, DysektEditor& ed)
            : patternPanel (proc), pluginEditor (ed)
        {
            addAndMakeVisible (patternPanel);
            addAndMakeVisible (pluginEditor);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            patternPanel.setBounds (r.removeFromLeft (kPatternPanelW));
            pluginEditor.setBounds (r);
        }

    private:
        PatternManagerComponent patternPanel;
        DysektEditor&           pluginEditor;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ContentHolder)
    };

    //==========================================================================
    //  Project save / load
    void newProject()
    {
        juce::AlertWindow::showOkCancelBox (
            juce::AlertWindow::QuestionIcon,
            "New Project",
            "Discard current project and start fresh?",
            "New", "Cancel", nullptr,
            juce::ModalCallbackFunction::create ([this] (int result)
            {
                if (result == 1)
                {
                    juce::MemoryBlock blank;
                    processor->setStateInformation (blank.getData(), (int) blank.getSize());
                    currentProjectFile = juce::File();
                    setName ("DYSEKT");
                }
            }));
    }

    void openProject()
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Open DYSEKT Project", juce::File::getSpecialLocation (
                juce::File::userDocumentsDirectory),
            "*.dysekt");

        fileChooser->launchAsync (
            juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result == juce::File()) return;

                juce::FileInputStream fis (result);
                if (! fis.openedOk()) return;

                const int64_t size = fis.getTotalLength();
                juce::MemoryBlock block ((size_t) size);
                fis.read (block.getData(), (int) size);
                processor->setStateInformation (block.getData(), (int) block.getSize());

                currentProjectFile = result;
                setName ("DYSEKT  —  " + result.getFileNameWithoutExtension());
            });
    }

    void saveProject()
    {
        if (currentProjectFile == juce::File())
        { saveProjectAs(); return; }

        juce::MemoryBlock state;
        processor->getStateInformation (state);
        juce::FileOutputStream fos (currentProjectFile);
        if (fos.openedOk())
        {
            fos.setPosition (0);
            fos.truncate();
            fos.write (state.getData(), state.getSize());
        }
    }

    void saveProjectAs()
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Save DYSEKT Project", juce::File::getSpecialLocation (
                juce::File::userDocumentsDirectory).getChildFile ("Untitled.dysekt"),
            "*.dysekt");

        fileChooser->launchAsync (
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result == juce::File()) return;
                currentProjectFile = result.withFileExtension ("dysekt");
                saveProject();
                setName ("DYSEKT  —  " + currentProjectFile.getFileNameWithoutExtension());
            });
    }

    void exportMidiClip()
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Export MIDI Clip",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                        .getChildFile ("DYSEKT_clip.mid"),
            "*.mid");

        fileChooser->launchAsync (
            juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [this] (const juce::FileChooser& fc)
            {
                auto result = fc.getResult();
                if (result == juce::File()) return;

                juce::MidiFile midiFile;
                midiFile.setTicksPerQuarterNote ((int) MidiClip::kPPQ);

                juce::MidiMessageSequence track;
                const MidiClip& clip = processor->sequencer.getClip();
                const juce::ScopedReadLock sl (clip.getLock());

                for (const auto& n : clip.getNotes())
                {
                    track.addEvent (juce::MidiMessage::noteOn  (1, n.note, (juce::uint8) n.velocity),
                                    (double) n.startTick);
                    track.addEvent (juce::MidiMessage::noteOff (1, n.note),
                                    (double) n.endTick());
                }
                track.sort();
                midiFile.addTrack (track);

                auto dest = result.withFileExtension ("mid");
                juce::FileOutputStream fos (dest);
                if (fos.openedOk())
                    midiFile.writeTo (fos);
            });
    }

    void showAudioSettings()
    {
        auto* audioSettingsComp = new juce::AudioDeviceSelectorComponent (
            deviceManager,
            0, 0,    // min/max input channels
            0, 2,    // min/max output channels
            true,    // show MIDI input selector
            false,   // show MIDI output selector
            false,   // treat channels as stereo pairs
            false);  // hide advanced options

        audioSettingsComp->setSize (500, 450);

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned (audioSettingsComp);
        opts.dialogTitle             = "Audio / MIDI Settings";
        opts.dialogBackgroundColour  = juce::Colour (0xFF0D0D14);
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar       = true;
        opts.resizable               = false;
        opts.launchAsync();
    }

    void showAbout()
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::InfoIcon,
            "DYSEKT Standalone",
            "DYSEKT Sampler + Sequencer\nVersion 1.0\n\nPowered by JUCE.");
    }

    //==========================================================================
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        // Audio device changed — nothing specific needed, player auto-reconnects
    }

    //==========================================================================
    juce::AudioDeviceManager            deviceManager;
    juce::AudioProcessorPlayer          player;

    std::unique_ptr<DysektProcessor>    processor;
    std::unique_ptr<DysektEditor>       editor;
    std::unique_ptr<juce::Component>    contentHolder;
    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<juce::FileChooser>  fileChooser;

    juce::File currentProjectFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
