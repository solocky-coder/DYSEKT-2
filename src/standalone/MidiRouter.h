#pragma once
//==============================================================================
//  MidiRouter.h  —  Cubase-style MIDI output routing for the DYSEKT-SF standalone
//
//  Drop this file into  src/standalone/
//
//  Two classes:
//    MidiRouter        — audio-thread-safe routing engine
//    MidiRoutingDialog — UI (launched from MainWindow menu or TrackHeaderStrip)
//
//  Integration points in MainWindow.h (see MainWindow.h patch):
//    1. #include "MidiRouter.h"
//    2. std::unique_ptr<MidiRouter> midiRouter;
//    3. Construct after deviceManager.initialise()
//    4. Register thru callback alongside &player for every MIDI input
//    5. In changeListenerCallback call midiRouter->refresh()
//    6. Register external-dispatch lambda with sequencer (see SequencerEngine patch)
//==============================================================================

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <vector>
#include <atomic>
#include <functional>

//==============================================================================
struct MidiTrackRoute
{
    int  outputDeviceIndex = -1;   // -1 = no external output
    int  channelOverride   =  0;   // 0 = passthrough, 1-16 = force channel
};

//==============================================================================
class MidiRouter : public juce::MidiInputCallback
{
public:
    //==========================================================================
    explicit MidiRouter (juce::AudioDeviceManager& dm)
        : deviceManager (dm)
    {
        refresh();
    }

    ~MidiRouter() override
    {
        closeAllOutputs();
    }

    //==========================================================================
    //  Device management (message thread)
    //==========================================================================

    /** Rescans available MIDI output devices. Call on device change events. */
    void refresh()
    {
        const juce::ScopedLock sl (routeLock);

        // Close devices that are no longer available
        auto available = juce::MidiOutput::getAvailableDevices();

        // Rebuild device name list
        deviceNames.clear();
        deviceIdentifiers.clear();

        for (const auto& d : available)
        {
            deviceNames.add (d.name);
            deviceIdentifiers.add (d.identifier);
        }

        // Remove handles for devices that disappeared
        for (int i = (int) openOutputs.size() - 1; i >= 0; --i)
        {
            if (! deviceIdentifiers.contains (openOutputHandles[i]))
            {
                openOutputs.erase  (openOutputs.begin()  + i);
                openOutputHandles.erase (openOutputHandles.begin() + i);
            }
        }

    }

    juce::StringArray getOutputDeviceNames() const
    {
        const juce::ScopedLock sl (routeLock);
        return deviceNames;
    }

    bool isDeviceOpen (int deviceIndex) const
    {
        const juce::ScopedLock sl (routeLock);
        if (! juce::isPositiveAndBelow (deviceIndex, deviceIdentifiers.size()))
            return false;
        const juce::String& id = deviceIdentifiers[deviceIndex];
        for (const auto& h : openOutputHandles)
            if (h == id) return true;
        return false;
    }

    void setDeviceOpen (int deviceIndex, bool open)
    {
        const juce::ScopedLock sl (routeLock);
        if (! juce::isPositiveAndBelow (deviceIndex, deviceIdentifiers.size())) return;

        const juce::String& id = deviceIdentifiers[deviceIndex];

        if (open)
        {
            for (const auto& h : openOutputHandles)
                if (h == id) return;  // already open

            auto available = juce::MidiOutput::getAvailableDevices();
            for (const auto& dev : available)
            {
                if (dev.identifier == id)
                {
                    auto out = juce::MidiOutput::openDevice (dev.identifier);
                    if (out != nullptr)
                    {
                        openOutputHandles.push_back (id);
                        openOutputs.push_back (std::move (out));
                    }
                    break;
                }
            }
        }
        else
        {
            for (int i = (int) openOutputHandles.size() - 1; i >= 0; --i)
            {
                if (openOutputHandles[i] == id)
                {
                    openOutputs.erase   (openOutputs.begin()   + i);
                    openOutputHandles.erase (openOutputHandles.begin() + i);
                    break;
                }
            }
        }
    }

    //==========================================================================
    //  Per-track routing (message thread)
    //==========================================================================

    void setTrackRoute (int trackIndex, MidiTrackRoute route)
    {
        const juce::ScopedLock sl (routeLock);
        if (trackIndex < 0) return;
        if (trackIndex >= (int) trackRoutes.size())
            trackRoutes.resize ((size_t) trackIndex + 1);
        trackRoutes[(size_t) trackIndex] = route;

        // Auto-open the device if not already open
        if (route.outputDeviceIndex >= 0)
            setDeviceOpen (route.outputDeviceIndex, true);
    }

    MidiTrackRoute getTrackRoute (int trackIndex) const
    {
        const juce::ScopedLock sl (routeLock);
        if (juce::isPositiveAndBelow (trackIndex, (int) trackRoutes.size()))
            return trackRoutes[(size_t) trackIndex];
        return {};
    }

    void removeTrackRoute (int trackIndex)
    {
        const juce::ScopedLock sl (routeLock);
        if (juce::isPositiveAndBelow (trackIndex, (int) trackRoutes.size()))
            trackRoutes[(size_t) trackIndex] = {};
    }

    //==========================================================================
    //  MIDI Clock
    //==========================================================================

    void setSendClock (bool v) noexcept { sendClock.store (v, std::memory_order_relaxed); }
    bool getSendClock()        const noexcept { return sendClock.load (std::memory_order_relaxed); }

    /** Call from the audio thread at transport start. */
    void sendTransportStart()
    {
        if (! sendClock.load (std::memory_order_relaxed)) return;
        const juce::ScopedTryLock stl (routeLock);
        if (! stl.isLocked()) return;
        const auto msg = juce::MidiMessage::midiStart();
        sendToAllOpenOutputs (msg);
    }

    /** Call from the audio thread at transport stop. */
    void sendTransportStop()
    {
        if (! sendClock.load (std::memory_order_relaxed)) return;
        const juce::ScopedTryLock stl (routeLock);
        if (! stl.isLocked()) return;
        sendToAllOpenOutputs (juce::MidiMessage::midiStop());
    }

    //==========================================================================
    //  Audio-thread dispatch
    //
    //  Called from PluginProcessor::processBlock (standalone only) after
    //  SequencerEngine::processBlock fills perTrackBuffers.
    //
    //  perTrackBuffers[i] contains the MIDI events for sequencer track i.
    //  The function routes each buffer to the assigned output device,
    //  re-stamping channels if channelOverride != 0.
    //  It also sends MIDI clock ticks to all open outputs.
    //==========================================================================
    void dispatchBlock (const std::vector<juce::MidiBuffer>& perTrackBuffers,
                        int    numSamples,
                        double bpm,
                        double sampleRate,
                        bool   transportPlaying)
    {
        const juce::ScopedTryLock stl (routeLock);
        if (! stl.isLocked()) return;

        // ── MIDI Clock ────────────────────────────────────────────────────────
        if (sendClock.load (std::memory_order_relaxed) && transportPlaying
            && bpm >= 1.0 && sampleRate >= 1.0)
        {
            const double ticksPerBeat    = 24.0;  // 24 PPQ for MIDI clock
            const double samplesPerTick  = (60.0 / bpm) * sampleRate / ticksPerBeat;
            const auto clockMsg = juce::MidiMessage::midiClock();

            double samplePos = clockPhaseAccumulator;
            while (samplePos < (double) numSamples)
            {
                sendToAllOpenOutputs (clockMsg);
                samplePos += samplesPerTick;
            }
            clockPhaseAccumulator = samplePos - (double) numSamples;
        }

        // ── Per-track routing ─────────────────────────────────────────────────
        for (int ti = 0; ti < (int) perTrackBuffers.size(); ++ti)
        {
            MidiTrackRoute route;
            if (ti < (int) trackRoutes.size())
                route = trackRoutes[(size_t) ti];

            if (route.outputDeviceIndex < 0) continue;  // no external output

            // Find the target output handle
            juce::MidiOutput* target = nullptr;
            {
                const juce::String& targetId = deviceIdentifiers[route.outputDeviceIndex];
                for (size_t k = 0; k < openOutputHandles.size(); ++k)
                    if (openOutputHandles[k] == targetId)
                        { target = openOutputs[k].get(); break; }
            }

            if (target == nullptr) continue;

            // Send events, optionally re-stamping channel
            for (const auto meta : perTrackBuffers[ti])
            {
                auto msg = meta.getMessage();
                if (route.channelOverride >= 1 && route.channelOverride <= 16
                    && msg.getChannel() > 0)
                {
                    msg.setChannel (route.channelOverride);
                }
                target->sendMessageNow (msg);
            }
        }
    }

    //==========================================================================
    //  MIDI Thru (MidiInputCallback)
    //==========================================================================

    void setThruOutputDevice (int deviceIndex)
    {
        const juce::ScopedLock sl (routeLock);
        thruOutputDeviceIndex = deviceIndex;
        if (deviceIndex >= 0)
            setDeviceOpen (deviceIndex, true);
    }

    int getThruOutputDevice() const
    {
        const juce::ScopedLock sl (routeLock);
        return thruOutputDeviceIndex;
    }

    // MidiInputCallback — registered by MainWindow for each active MIDI input.
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& msg) override
    {
        const juce::ScopedTryLock stl (routeLock);
        if (! stl.isLocked()) return;

        const int thruIdx = thruOutputDeviceIndex;
        if (thruIdx < 0) return;

        juce::MidiOutput* target = nullptr;
        {
            if (thruIdx < (int) deviceIdentifiers.size())
            {
                const juce::String& id = deviceIdentifiers[thruIdx];
                for (size_t k = 0; k < openOutputHandles.size(); ++k)
                    if (openOutputHandles[k] == id)
                        { target = openOutputs[k].get(); break; }
            }
        }

        if (target != nullptr)
            target->sendMessageNow (msg);
    }

    //==========================================================================
    //  Serialisation
    //==========================================================================

    std::unique_ptr<juce::XmlElement> writeToXml() const
    {
        const juce::ScopedLock sl (routeLock);
        auto root = std::make_unique<juce::XmlElement> ("MidiRouter");
        root->setAttribute ("sendClock", sendClock.load());
        root->setAttribute ("thruDevice",
            thruOutputDeviceIndex >= 0 && thruOutputDeviceIndex < deviceNames.size()
                ? deviceNames[thruOutputDeviceIndex] : "");

        auto* routes = root->createNewChildElement ("Routes");
        for (int i = 0; i < (int) trackRoutes.size(); ++i)
        {
            const auto& r = trackRoutes[(size_t) i];
            if (r.outputDeviceIndex < 0) continue;
            auto* el = routes->createNewChildElement ("Route");
            el->setAttribute ("track", i);
            el->setAttribute ("device",
                r.outputDeviceIndex < deviceNames.size()
                    ? deviceNames[r.outputDeviceIndex] : "");
            el->setAttribute ("channel", r.channelOverride);
        }
        return root;
    }

    void readFromXml (const juce::XmlElement& xml)
    {
        const juce::ScopedLock sl (routeLock);
        sendClock.store (xml.getBoolAttribute ("sendClock", false));

        const juce::String thruName = xml.getStringAttribute ("thruDevice");
        thruOutputDeviceIndex = deviceNames.indexOf (thruName);

        if (auto* routes = xml.getChildByName ("Routes"))
        {
            for (auto* el : routes->getChildIterator())
            {
                const int ti   = el->getIntAttribute ("track", -1);
                const juce::String devName = el->getStringAttribute ("device");
                const int ch   = el->getIntAttribute ("channel", 0);
                if (ti < 0) continue;

                MidiTrackRoute r;
                r.outputDeviceIndex = deviceNames.indexOf (devName);
                r.channelOverride   = ch;
                if (ti >= (int) trackRoutes.size())
                    trackRoutes.resize ((size_t) ti + 1);
                trackRoutes[(size_t) ti] = r;
            }
        }
    }

private:
    void closeAllOutputs()
    {
        const juce::ScopedLock sl (routeLock);
        openOutputs.clear();
        openOutputHandles.clear();
    }

    void sendToAllOpenOutputs (const juce::MidiMessage& msg)
    {
        for (auto& out : openOutputs)
            if (out) out->sendMessageNow (msg);
    }

    //==========================================================================
    juce::AudioDeviceManager& deviceManager;

    mutable juce::CriticalSection routeLock;

    juce::StringArray deviceNames;        // parallel arrays — index is stable within one session
    juce::StringArray deviceIdentifiers;

    std::vector<std::unique_ptr<juce::MidiOutput>> openOutputs;
    std::vector<juce::String>                       openOutputHandles;

    std::vector<MidiTrackRoute> trackRoutes;   // index == sequencer track index
    int                         thruOutputDeviceIndex = -1;

    std::atomic<bool>   sendClock { false };
    double              clockPhaseAccumulator = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRouter)
};

//==============================================================================
//  MidiRoutingDialog
//
//  Cubase-Inspector-style panel.  Three sections:
//    ① MIDI OUTPUT DEVICES  — toggle open/close each port
//    ② TRACK ROUTING        — per-track Output + Channel Override dropdowns
//    ③ GLOBAL OPTIONS       — MIDI Clock + MIDI Thru destination
//
//  Launched from MainWindow via  showMidiRouting()
//  Also launched from TrackHeaderStrip Option-C flyout when "Open full routing…" is clicked
//==============================================================================
class MidiRoutingDialog : public juce::Component,
                          private juce::Timer
{
public:
    MidiRoutingDialog (MidiRouter& router,
                       const juce::StringArray& trackNames)
        : midiRouter (router),
          names      (trackNames)
    {
        setSize (560, 480);
        startTimerHz (4);  // refresh device open/close state
        rebuild();
    }

    ~MidiRoutingDialog() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF0D0D14));
        g.setColour (juce::Colour (0xFF1A2030));

        // Section headers backgrounds
        g.fillRect (0, 0, getWidth(), 28);
        g.fillRect (0, sectionY[1] - 28, getWidth(), 28);
        g.fillRect (0, sectionY[2] - 28, getWidth(), 28);

        g.setColour (juce::Colour (0xFF00CCAA));
        g.setFont (juce::Font (13.f, juce::Font::bold));
        g.drawText ("MIDI OUTPUT DEVICES",   8, 0,         400, 28, juce::Justification::centredLeft);
        g.drawText ("TRACK MIDI ROUTING",    8, sectionY[1] - 28, 400, 28, juce::Justification::centredLeft);
        g.drawText ("GLOBAL OPTIONS",        8, sectionY[2] - 28, 400, 28, juce::Justification::centredLeft);
    }

    void resized() override
    {
        rebuild();
    }

private:
    //==========================================================================
    void rebuild()
    {
        removeAllChildren();
        rows.clear();

        const juce::StringArray devNames = midiRouter.getOutputDeviceNames();
        int y = 32;

        //── ① Device toggles ─────────────────────────────────────────────────
        for (int di = 0; di < devNames.size(); ++di)
        {
            auto* btn = new juce::ToggleButton (devNames[di]);
            btn->setToggleState (midiRouter.isDeviceOpen (di), juce::dontSendNotification);
            btn->setColour (juce::ToggleButton::textColourId, juce::Colour (0xFFCCD0D8));
            btn->setBounds (12, y, getWidth() - 24, 24);
            const int capturedDi = di;
            btn->onStateChange = [this, capturedDi, btn]
            {
                midiRouter.setDeviceOpen (capturedDi, btn->getToggleState());
                rebuildRouteDropdowns();
            };
            addAndMakeVisible (btn);
            rows.add (btn);
            y += 26;
        }

        y += 8;
        sectionY[1] = y + 28;
        y += 28;  // section header

        //── ② Track routing rows ─────────────────────────────────────────────
        // Column headers
        {
            auto* hTrack = new juce::Label ("", "Track");
            auto* hDev   = new juce::Label ("", "Output Device");
            auto* hCh    = new juce::Label ("", "Ch Override");
            for (auto* lbl : { hTrack, hDev, hCh })
            {
                lbl->setFont (juce::Font (11.f, juce::Font::bold));
                lbl->setColour (juce::Label::textColourId, juce::Colour (0xFF607080));
            }
            hTrack->setBounds (12,  y, 130, 18);
            hDev->setBounds   (148, y, 220, 18);
            hCh->setBounds    (374, y, 120, 18);
            addAndMakeVisible (hTrack);
            addAndMakeVisible (hDev);
            addAndMakeVisible (hCh);
            rows.add (hTrack);
            rows.add (hDev);
            rows.add (hCh);
            y += 20;
        }

        juce::StringArray devChoices;
        devChoices.add ("(None)");
        devChoices.addArray (devNames);

        for (int ti = 0; ti < names.size(); ++ti)
        {
            const MidiTrackRoute route = midiRouter.getTrackRoute (ti);

            auto* nameLbl = new juce::Label ("", names[ti]);
            nameLbl->setFont (juce::Font (12.f));
            nameLbl->setColour (juce::Label::textColourId, juce::Colour (0xFFCCD0D8));
            nameLbl->setBounds (12, y, 130, 24);
            addAndMakeVisible (nameLbl);
            rows.add (nameLbl);

            auto* devBox = new juce::ComboBox();
            devBox->addItemList (devChoices, 1);
            devBox->setSelectedId (route.outputDeviceIndex + 2,
                                   juce::dontSendNotification);  // +2 because item IDs start at 1 and "(None)" = 1
            devBox->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xFF121820));
            devBox->setColour (juce::ComboBox::textColourId,       juce::Colour (0xFFCCD0D8));
            devBox->setBounds (148, y, 220, 24);
            const int capturedTi = ti;
            devBox->onChange = [this, capturedTi, devBox]
            {
                MidiTrackRoute r = midiRouter.getTrackRoute (capturedTi);
                r.outputDeviceIndex = devBox->getSelectedId() - 2;
                midiRouter.setTrackRoute (capturedTi, r);
            };
            addAndMakeVisible (devBox);
            rows.add (devBox);

            auto* chBox = new juce::ComboBox();
            chBox->addItem ("─ pass-thru ─", 1);
            for (int ch = 1; ch <= 16; ++ch)
                chBox->addItem ("Ch " + juce::String (ch), ch + 1);
            chBox->setSelectedId (route.channelOverride == 0 ? 1 : route.channelOverride + 1,
                                  juce::dontSendNotification);
            chBox->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xFF121820));
            chBox->setColour (juce::ComboBox::textColourId,       juce::Colour (0xFFCCD0D8));
            chBox->setBounds (374, y, 120, 24);
            chBox->onChange = [this, capturedTi, chBox]
            {
                MidiTrackRoute r = midiRouter.getTrackRoute (capturedTi);
                r.channelOverride = chBox->getSelectedId() - 1;  // 1 → 0 (passthru), 2 → 1, etc.
                midiRouter.setTrackRoute (capturedTi, r);
            };
            addAndMakeVisible (chBox);
            rows.add (chBox);

            y += 28;
        }

        y += 8;
        sectionY[2] = y + 28;
        y += 28;  // section header

        //── ③ Global options ─────────────────────────────────────────────────
        {
            auto* clockToggle = new juce::ToggleButton ("Send MIDI Clock to all open outputs");
            clockToggle->setToggleState (midiRouter.getSendClock(), juce::dontSendNotification);
            clockToggle->setColour (juce::ToggleButton::textColourId, juce::Colour (0xFFCCD0D8));
            clockToggle->setBounds (12, y, getWidth() - 24, 24);
            clockToggle->onStateChange = [this, clockToggle]
            {
                midiRouter.setSendClock (clockToggle->getToggleState());
            };
            addAndMakeVisible (clockToggle);
            rows.add (clockToggle);
            y += 28;
        }

        {
            auto* thruLabel = new juce::Label ("", "MIDI Thru →");
            thruLabel->setFont (juce::Font (12.f));
            thruLabel->setColour (juce::Label::textColourId, juce::Colour (0xFFCCD0D8));
            thruLabel->setBounds (12, y, 100, 24);
            addAndMakeVisible (thruLabel);
            rows.add (thruLabel);

            juce::StringArray thruChoices;
            thruChoices.add ("(Disabled)");
            thruChoices.addArray (devNames);

            auto* thruBox = new juce::ComboBox();
            thruBox->addItemList (thruChoices, 1);
            const int thruIdx = midiRouter.getThruOutputDevice();
            thruBox->setSelectedId (thruIdx + 2, juce::dontSendNotification);
            thruBox->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xFF121820));
            thruBox->setColour (juce::ComboBox::textColourId,       juce::Colour (0xFFCCD0D8));
            thruBox->setBounds (118, y, 240, 24);
            thruBox->onChange = [this, thruBox]
            {
                midiRouter.setThruOutputDevice (thruBox->getSelectedId() - 2);
            };
            addAndMakeVisible (thruBox);
            rows.add (thruBox);
        }

        const int totalH = y + 40;
        setSize (getWidth(), juce::jmax (300, totalH));
        repaint();
    }

    void rebuildRouteDropdowns()
    {
        // Simple rebuild on toggle change — full refresh
        rebuild();
    }

    void timerCallback() override
    {
        // Periodic refresh in case devices changed externally
        // (lightweight — only triggers full rebuild on name-list change)
        const juce::StringArray current = midiRouter.getOutputDeviceNames();
        if (current != lastDeviceNames)
        {
            lastDeviceNames = current;
            rebuild();
        }
    }

    //==========================================================================
    MidiRouter&           midiRouter;
    juce::StringArray     names;
    juce::StringArray     lastDeviceNames;

    juce::OwnedArray<juce::Component> rows;
    int sectionY[3] = { 0, 0, 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRoutingDialog)
};
