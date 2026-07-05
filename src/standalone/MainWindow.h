#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "../PluginProcessor.h"
#include "../PluginEditor.h"
#include "../sequencer/MidiClip.h"
#include "MidiRouter.h"

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
        // Use initialise() (not initialiseWithDefaultDevices) so that ALL
        // registered audio device types — including ASIO on Windows — are
        // available in the AudioDeviceSelectorComponent dropdown.
        deviceManager.initialise (0,       // numInputChannelsNeeded
                                  2,       // numOutputChannelsNeeded
                                  nullptr, // savedState XML (none yet)
                                  true,    // selectDefaultDeviceOnFailure
                                  {},      // preferredDefaultDeviceName
                                  nullptr);// preferredSetupOptions
        deviceManager.addChangeListener (this);

        // ── Plugin processor + editor ─────────────────────────────────────
        processor = std::make_unique<DysektProcessor>();
        processor->prepareToPlay (44100.0, 512);

        editor = std::make_unique<DysektEditor> (*processor);

        // ── Audio callback ────────────────────────────────────────────────
        player.setProcessor (processor.get());
        deviceManager.addAudioCallback (&player);

        // ── MIDI router ───────────────────────────────────────────────────
        midiRouter = std::make_unique<MidiRouter> (deviceManager);

        // ── MIDI input ────────────────────────────────────────────────────
        // Enable every available device and wire it to the player AND the
        // MidiRouter thru handler.  Track IDs for cleanup in destructor.
        for (const auto& input : juce::MidiInput::getAvailableDevices())
        {
            deviceManager.setMidiInputDeviceEnabled (input.identifier, true);
            deviceManager.addMidiInputDeviceCallback (input.identifier, &player);
            deviceManager.addMidiInputDeviceCallback (input.identifier, midiRouter.get());
            registeredMidiInputIds.add (input.identifier);
        }

        // ── Set editor directly as window content ─────────────────────────
        setContentNonOwned (editor.get(), true);

        // ── Menu ──────────────────────────────────────────────────────────
        menuBar = std::make_unique<juce::MenuBarComponent> (this);
        setMenuBar (this, kMenuH);

        // ── Initial size ──────────────────────────────────────────────────
        // Give the window a sane windowed-mode fallback size first (in case
        // setFullScreen() is ever a no-op on some platform/window manager),
        // then maximise. setFullScreen(true) resizes the window to the
        // monitor's usable area and — now that resized() above explicitly
        // fills the content area — the editor reflows to match it on the
        // very first frame, instead of opening at the smaller centred
        // default and needing a manual corner-drag to fill the screen.
        setSize (editor->getWidth(), editor->getHeight() + kMenuH);
        setVisible (true);
        centreWithSize (getWidth(), getHeight());
        setFullScreen (true);
    }

    // NOTE: DysektEditor used to lock itself to a fixed aspect ratio, which
    // meant any time the window grew, the editor would immediately clamp
    // itself back down to the largest size that still fit that ratio while
    // keeping its top-left origin — dumping the leftover space on the
    // right/bottom instead of splitting it evenly. This override used to
    // paper over that by re-centring the (already correctly-sized) editor.
    //
    // That clamp no longer exists — DysektEditor dropped setFixedAspectRatio()
    // and now freely reflows at whatever size it's given (see the comments
    // above resized() in PluginEditor.cpp). Re-centring alone no longer does
    // anything useful: setCentrePosition() only moves the editor, it never
    // resizes it, so on any window resize the editor was left at its old
    // size, just repositioned — which is exactly the "doesn't fill the
    // window until you manually drag the corner" bug. Explicitly fill the
    // content area here so the editor's bounds always match the window.
    void resized() override
    {
        DocumentWindow::resized();

        if (editor != nullptr)
        {
            const auto contentArea = getLocalBounds().withTrimmedTop (
                (menuBar != nullptr) ? kMenuH : 0);
            editor->setBounds (contentArea);
        }
    }

    ~MainWindow() override
    {
        setMenuBar (nullptr);
        deviceManager.removeAudioCallback (&player);

        // Remove every MIDI callback BEFORE player/router are destroyed.
        for (const auto& id : registeredMidiInputIds)
        {
            deviceManager.removeMidiInputDeviceCallback (id, &player);
            if (midiRouter != nullptr)
                deviceManager.removeMidiInputDeviceCallback (id, midiRouter.get());
        }

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
            menu.addSeparator();
            menu.addItem (12, "MIDI Routing...");
        }
        else  // Help
        {
            menu.addItem (20, "About DYSEKT-SF");
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
            case 12: showMidiRouting();      break;
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
                    setName ("DYSEKT-SF");
                }
            }));
    }

    void openProject()
    {
        fileChooser = std::make_unique<juce::FileChooser> (
            "Open DYSEKT-SF Project", juce::File::getSpecialLocation (
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
                setName ("DYSEKT-SF  —  " + result.getFileNameWithoutExtension());
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
            "Save DYSEKT-SF Project", juce::File::getSpecialLocation (
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
                setName ("DYSEKT-SF  —  " + currentProjectFile.getFileNameWithoutExtension());
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
        // minOutputChannels must be >= 1 so JUCE renders the audio-device /
        // driver-type section (including the ASIO dropdown).  With min == 0
        // JUCE considers audio optional and hides the whole device selector.
        auto* audioSettingsComp = new juce::AudioDeviceSelectorComponent (
            deviceManager,
            0, 0,    // min/max input channels  (no recording needed)
            1, 2,    // min/max output channels  — min=1 forces driver selector visible
            false,   // show MIDI input selector  (handled separately in MIDI Settings)
            false,   // show MIDI output selector
            false,   // treat channels as stereo pairs
            false);  // hideAdvancedOptionsWithButton — false = always show ASIO etc.

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

    // ---------------------------------------------------------------------------
    //  Pure-MIDI settings panel
    //
    //  AudioDeviceSelectorComponent always bleeds in audio-device UI when it
    //  shares the main deviceManager, even with 0/0 channel counts, because
    //  JUCE renders the audio section whenever the manager has an active device.
    //
    //  Instead we build a small Component that hosts only the JUCE
    //  MidiInputSelectorComponentListBox directly via its public API:
    //  deviceManager.isMidiInputDeviceEnabled / setMidiInputDeviceEnabled.
    // ---------------------------------------------------------------------------
    class MidiOnlySettingsComponent : public juce::Component
    {
    public:
        explicit MidiOnlySettingsComponent (juce::AudioDeviceManager& dm,
                                             juce::AudioProcessorPlayer& pl,
                                             juce::StringArray& registeredIds)
            : deviceManager (dm), player (pl), registeredMidiInputIds (registeredIds)
        {
            setSize (480, 320);

            // Title label
            titleLabel.setText ("MIDI Input Devices", juce::dontSendNotification);
            titleLabel.setFont (juce::Font (16.0f, juce::Font::bold));
            titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
            addAndMakeVisible (titleLabel);

            // One toggle per available MIDI input
            refreshDeviceList();
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xFF0D0D14));
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (16);
            titleLabel.setBounds (area.removeFromTop (28));
            area.removeFromTop (8);

            for (auto* row : rows)
                row->setBounds (area.removeFromTop (28));
        }

    private:
        void refreshDeviceList()
        {
            rows.clear();
            const auto devices = juce::MidiInput::getAvailableDevices();

            if (devices.isEmpty())
            {
                auto* lbl = new juce::Label();
                lbl->setText ("No MIDI input devices found.", juce::dontSendNotification);
                lbl->setColour (juce::Label::textColourId, juce::Colours::grey);
                addAndMakeVisible (lbl);
                rows.add (lbl);
            }
            else
            {
                for (const auto& dev : devices)
                {
                    auto* btn = new juce::ToggleButton (dev.name);
                    btn->setToggleState (
                        deviceManager.isMidiInputDeviceEnabled (dev.identifier),
                        juce::dontSendNotification);
                    btn->setColour (juce::ToggleButton::textColourId, juce::Colours::white);

                    const juce::String id = dev.identifier;
                    btn->onClick = [this, id, btn]
                    {
                        const bool enable = btn->getToggleState();
                        if (enable)
                        {
                            deviceManager.setMidiInputDeviceEnabled (id, true);
                            // Only add the callback if not already registered.
                            if (! registeredMidiInputIds.contains (id))
                            {
                                deviceManager.addMidiInputDeviceCallback (id, &player);
                                registeredMidiInputIds.add (id);
                            }
                        }
                        else
                        {
                            deviceManager.removeMidiInputDeviceCallback (id, &player);
                            registeredMidiInputIds.removeString (id);
                            deviceManager.setMidiInputDeviceEnabled (id, false);
                        }
                    };

                    addAndMakeVisible (btn);
                    rows.add (btn);
                }
            }

            // Resize to fit content
            const int h = 28 + 8 + (int) rows.size() * 28 + 16;
            setSize (480, juce::jmax (120, h));
        }

        juce::AudioDeviceManager&         deviceManager;
        juce::AudioProcessorPlayer&       player;
        juce::StringArray&                registeredMidiInputIds;
        juce::Label                       titleLabel;
        juce::OwnedArray<juce::Component> rows;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiOnlySettingsComponent)
    };

    void showMidiSettings()
    {
        auto* midiSettingsComp = new MidiOnlySettingsComponent (deviceManager, player, registeredMidiInputIds);

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned (midiSettingsComp);
        opts.dialogTitle             = "MIDI Settings";
        opts.dialogBackgroundColour  = juce::Colour (0xFF0D0D14);
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar       = true;
        opts.resizable               = false;
        opts.launchAsync();
    }

    void showMidiRouting()
    {
        if (midiRouter == nullptr) return;

        // Build track name list from the sequencer
        juce::StringArray trackNames;
        for (int i = 0; i < processor->sequencer.getNumTracks(); ++i)
            trackNames.add (processor->sequencer.getTrackInfo(i).name);

        auto* dlg = new MidiRoutingDialog (*midiRouter, trackNames);

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned (dlg);
        opts.dialogTitle             = "MIDI Routing";
        opts.dialogBackgroundColour  = juce::Colour (0xFF0D0D14);
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar       = true;
        opts.resizable               = true;
        opts.launchAsync();
    }

    void showAbout()
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::AlertWindow::InfoIcon,
            "DYSEKT-SF Standalone",
            "DYSEKT-SF Sampler + Sequencer\nVersion 1.0\n\nPowered by JUCE.");
    }

    //==========================================================================
    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        // Audio device changed — refresh router device list
        if (midiRouter != nullptr)
            midiRouter->refresh();
    }

    //==========================================================================
    juce::AudioDeviceManager            deviceManager;
    juce::AudioProcessorPlayer          player;
    juce::StringArray                   registeredMidiInputIds;  // tracks devices with active callbacks

    std::unique_ptr<MidiRouter>         midiRouter;
    std::unique_ptr<DysektProcessor>    processor;
    std::unique_ptr<DysektEditor>       editor;
    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<juce::FileChooser>  fileChooser;

    juce::File currentProjectFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};
