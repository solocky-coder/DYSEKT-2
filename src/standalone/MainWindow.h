#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "../PluginProcessor.h"
#include "../PluginEditor.h"
#include "../sequencer/MidiClip.h"

//==============================================================================
//  MainWindow
//
//  The standalone app's main window.  Layout:
//
//    ┌─────────────────────────────────────────────────┐
//    │  MenuBar (File / Audio / Help)                  │
//    ├─────────────────────────────────────────────────┤
//    │                                                 │
//    │   DysektEditor  (full plugin UI)                │
//    │                                                 │
//    └─────────────────────────────────────────────────┘
//
//  Audio I/O is handled by AudioDeviceManager.
//==============================================================================
class MainWindow : public juce::DocumentWindow,
                   public juce::MenuBarModel,
                   private juce::ChangeListener
{
public:
    static constexpr int kMenuH = 24;

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

        // ── Set editor directly as window content ─────────────────────────
        setContentNonOwned (editor.get(), true);

        // ── Menu ──────────────────────────────────────────────────────────
        menuBar = std::make_unique<juce::MenuBarComponent> (this);
        setMenuBar (this, kMenuH);

        // ── Initial size ──────────────────────────────────────────────────
        setSize (editor->getWidth(), editor->getHeight() + kMenuH);
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
            case 11: showMidiSettings();     break;
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
            false,   // show MIDI input selector  (handled in showMidiSettings)
            false,   // show MIDI output selector
            false,   // treat channels as stereo pairs
            false);  // hideAdvancedOptionsWithButton — false = show driver type (ASIO etc.)

        audioSettingsComp->setSize (500, 450);

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned (audioSettingsComp);
        opts.dialogTitle             = "Audio Settings";
        opts.dialogBackgroundColour  = juce::Colour (0xFF0D0D14);
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar       = true;
        opts.resizable               = false;
        opts.launchAsync();
    }

    void showMidiSettings()
    {
        auto* midiSettingsComp = new juce::AudioDeviceSelectorComponent (
            deviceManager,
            0, 0,    // min/max input channels
            0, 0,    // min/max output channels (no audio outputs shown)
            true,    // show MIDI input selector
            false,   // show MIDI output selector
            false,   // treat channels as stereo pairs
            false);  // hide advanced options

        midiSettingsComp->setSize (500, 300);

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned (midiSettingsComp);
        opts.dialogTitle             = "MIDI Settings";
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
    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<juce::FileChooser>  fileChooser;

    juce::File currentProjectFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
