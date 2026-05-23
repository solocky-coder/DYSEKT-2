#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../sequencer/SequencerEngine.h"

//==============================================================================
//  TrackHeaderStrip
//
//  Vertical list of track headers, one row per track, same height as the
//  piano roll grid rows but grouped rather than per-note.
//  Shows: colour swatch | track name | MIDI-RX dot | mute button
//  Clicking a row selects that track for editing in the piano roll.
//
//  Right-click on ANY track → "Set external output…" (Option C, standalone only)
//  Right-click on SfPlayer  → also shows MIDI-channel reassignment items
//
//  MIDI receive indicator: a small dot to the left of the mute button that
//  blinks green when MIDI events fire on that track's channel.
//  The audio thread calls notifyMidiActivity(trackIndex) (lock-free).
//  The 10-Hz timer handles decay and repaint — no allocations, no locks.
//==============================================================================
class TrackHeaderStrip : public juce::Component,
                         private juce::Timer
{
public:
    explicit TrackHeaderStrip (SequencerEngine& seq)
        : engine (seq)
    {
        startTimerHz (10);
    }

    ~TrackHeaderStrip() override { stopTimer(); }

    //==========================================================================
    /** Called by ArrangeView whenever its trackH changes so rows stay perfectly in sync. */
    void setTrackHeight (int h)
    {
        trackH = juce::jmax (18, h);
        repaint();
    }

    int getTrackHeight() const noexcept { return trackH; }

    int  getSelectedTrack() const noexcept { return selectedTrack; }
    void setSelectedTrack (int i) { selectedTrack = i; repaint(); }

    //==========================================================================
    //  Callbacks — wired by the owning component (ArrangeView / PianoRollPanel)
    //==========================================================================

    /** Basic selection / mute */
    std::function<void(int trackIndex)>             onTrackSelected;
    std::function<void(int trackIndex, bool muted)> onTrackMuted;

    /** SfPlayer MIDI channel reassignment (existing). */
    std::function<void(int trackIndex, int midiChannel1Based)> onSfTrackChannelChanged;

    //── Option C: external output routing (standalone only) ──────────────────
    /** Returns the display names of currently open MIDI output devices,
     *  plus "(None)" as element 0.  Provide from MainWindow/MidiRouter. */
    std::function<juce::StringArray()> onGetOutputDeviceNames;

    /** Returns the current route for a sequencer track. */
    std::function<MidiTrackRoute(int trackIndex)> onGetTrackRoute;

    /** Commits a new route for a sequencer track. */
    std::function<void(int trackIndex, MidiTrackRoute)> onSetTrackRoute;

    //── MIDI receive indicator ─────────────────────────────────────────────
    /** Call this from the audio thread when a note fires on sequencer track i.
     *  Uses a lock-free atomic array — safe on the audio thread. */
    void notifyMidiActivity (int trackIndex) noexcept
    {
        if (juce::isPositiveAndBelow (trackIndex, kMaxTracks))
            midiActivityFlags[(size_t) trackIndex].store (true, std::memory_order_relaxed);
    }

    //==========================================================================
    int getRequiredHeight() const
    {
        return engine.getNumTracks() * trackH;
    }

    //==========================================================================
    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF0A0A10));

        const int n = engine.getNumTracks();
        for (int i = 0; i < n; ++i)
        {
            const auto info  = engine.getTrackInfo (i);
            const auto rowR  = getRowBounds (i);
            const bool sel   = (i == selectedTrack);

            // Background
            g.setColour (sel ? juce::Colour (0xFF182030)
                             : juce::Colour (0xFF0D0D16));
            g.fillRect (rowR);

            // Colour swatch
            g.setColour (info.colour);
            g.fillRect (rowR.withTrimmedRight (rowR.getWidth() - 4).toFloat());

            // ── Mute button ───────────────────────────────────────────────
            const int muteW  = juce::jlimit (20, 28, trackH - 8);
            const int muteH  = juce::jlimit (12, 18, trackH - 8);
            const auto muteR = rowR.withTrimmedLeft (rowR.getWidth() - muteW - 4)
                                   .withSizeKeepingCentre (muteW, muteH);
            g.setColour (info.enabled
                ? juce::Colour (0xFF2A8060)
                : juce::Colour (0xFF602020));
            g.fillRoundedRectangle (muteR.toFloat(), 3.f);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            const float muteFontSz = juce::jlimit (10.5f, 16.5f, (float)trackH * 0.22f);
            g.setFont (juce::Font (muteFontSz, juce::Font::bold));
            g.drawText (info.enabled ? "M" : "m", muteR,
                        juce::Justification::centred, false);

            // ── MIDI receive indicator (dot left of mute) ─────────────────
            // midiHoldCounters[i] > 0 → active (decays each timer tick)
            const bool rxActive = (i < kMaxTracks && midiHoldCounters[i] > 0);
            const int  dotR     = juce::jlimit (4, 7, trackH / 8);
            const auto dotCentre = juce::Point<int> (muteR.getX() - dotR - 4,
                                                      rowR.getCentreY());
            g.setColour (rxActive ? juce::Colour (0xFF00FF88)
                                  : juce::Colour (0xFF223030));
            g.fillEllipse ((float)(dotCentre.x - dotR),
                           (float)(dotCentre.y - dotR),
                           (float)(dotR * 2),
                           (float)(dotR * 2));

            // ── External output badge ─────────────────────────────────────
            // If this track has an external output assigned, show a small
            // port indicator between the name and the RX dot.
            if (onGetTrackRoute)
            {
                const MidiTrackRoute route = onGetTrackRoute (i);
                if (route.outputDeviceIndex >= 0 && onGetOutputDeviceNames)
                {
                    const auto devNames = onGetOutputDeviceNames();
                    // devNames[0] is "(None)", actual devices start at 1 —
                    // but onGetOutputDeviceNames() in MidiRouter returns names
                    // without "(None)", so index 0 = first real device.
                    if (route.outputDeviceIndex < devNames.size())
                    {
                        const juce::String portLabel =
                            devNames[route.outputDeviceIndex]
                                .substring (0, 6)
                                .toUpperCase();

                        // Pill background
                        const int pillW = juce::jlimit (34, 58, trackH);
                        const int pillH = juce::jlimit (11, 15, trackH / 3);
                        const auto pillR = juce::Rectangle<int> (
                            dotCentre.x - pillW - dotR * 2 - 6,
                            rowR.getCentreY() - pillH / 2,
                            pillW, pillH);

                        g.setColour (juce::Colour (0xFF003344));
                        g.fillRoundedRectangle (pillR.toFloat(), 3.f);

                        // Lit dot
                        g.setColour (juce::Colour (0xFF00CCAA));
                        g.fillEllipse ((float)(pillR.getX() + 3),
                                       (float)(pillR.getCentreY() - 3),
                                       6.f, 6.f);

                        // Port abbreviation
                        g.setColour (juce::Colour (0xFF00DDBB));
                        g.setFont (juce::Font (juce::jlimit (8.f, 11.f,
                                                (float)pillH * 0.75f)));
                        g.drawText (portLabel,
                                    pillR.getX() + 12, pillR.getY(),
                                    pillR.getWidth() - 14, pillR.getHeight(),
                                    juce::Justification::centredLeft, true);
                    }
                }
            }

            // ── Track name ────────────────────────────────────────────────
            const float nameFontSz = juce::jlimit (16.5f, 22.5f, (float)trackH * 0.30f);
            g.setFont (juce::Font (nameFontSz, juce::Font::bold));
            g.setColour (sel ? info.colour : juce::Colour (0xFFCCD0D8));
            g.drawText (info.name,
                        rowR.getX() + 6, rowR.getY(),
                        rowR.getWidth() - muteW - 12, trackH,
                        juce::Justification::centredLeft, true);

            // ── Type badge ────────────────────────────────────────────────
            juce::String badge;
            switch (info.type)
            {
                case TrackType::MainSlice:      badge = "SL";  break;
                case TrackType::ChromaticSlice: badge = "CH";  break;
                case TrackType::SfPlayer:       badge = "SF";  break;
            }
            if (trackH >= 32)
            {
                const float badgeFontSz = juce::jlimit (12.0f, 16.5f, (float)trackH * 0.18f);
                g.setFont (juce::Font (badgeFontSz));
                g.setColour (info.colour.withAlpha (0.6f));
                g.drawText (badge,
                            rowR.getX() + 6, rowR.getCentreY(),
                            24, trackH / 2,
                            juce::Justification::centredLeft, false);

                if (info.type == TrackType::SfPlayer)
                {
                    const juce::String chBadge = "CH" + juce::String (info.midiChannel + 1);
                    g.setColour (info.colour.withAlpha (0.85f));
                    g.drawText (chBadge,
                                rowR.getX() + 32, rowR.getCentreY(),
                                44, trackH / 2,
                                juce::Justification::centredLeft, false);
                }
            }

            // Separator
            g.setColour (juce::Colour (0xFF1C2028));
            g.fillRect (0, rowR.getBottom() - 1, getWidth(), 1);
        }
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = e.y / trackH;
        if (! juce::isPositiveAndBelow (i, engine.getNumTracks())) return;

        const auto info = engine.getTrackInfo (i);

        if (e.mods.isRightButtonDown())
        {
            showTrackContextMenu (i, info, e.getScreenPosition());
            return;
        }

        // Check mute button hit
        const auto rowR  = getRowBounds (i);
        const int muteW  = juce::jlimit (20, 28, trackH - 8);
        const int muteH  = juce::jlimit (12, 18, trackH - 8);
        const auto muteR = rowR.withTrimmedLeft (rowR.getWidth() - muteW - 4)
                               .withSizeKeepingCentre (muteW, muteH);

        if (muteR.contains (e.getPosition()))
        {
            engine.setTrackEnabled (i, ! info.enabled);
            if (onTrackMuted) onTrackMuted (i, ! info.enabled);
        }
        else
        {
            selectedTrack = i;
            if (onTrackSelected) onTrackSelected (i);
        }

        repaint();
    }

private:
    //==========================================================================
    //  Right-click context menu — Option C
    //==========================================================================
    void showTrackContextMenu (int trackIdx,
                               const SequencerTrackInfo& info,
                               juce::Point<int> screenPos)
    {
        juce::PopupMenu menu;

        //── SfPlayer: existing MIDI channel picker ────────────────────────────
        if (info.type == TrackType::SfPlayer)
        {
            menu.addSectionHeader ("MIDI Channel – " + info.name);
            for (int ch = 1; ch <= 16; ++ch)
                menu.addItem (ch, "Channel " + juce::String (ch), true,
                              ch == info.midiChannel + 1);
            menu.addSeparator();
        }

        //── Option C: external output ─────────────────────────────────────────
        if (onGetOutputDeviceNames && onGetTrackRoute && onSetTrackRoute)
        {
            menu.addItem (kExternalOutputItemId, "Set external output…");
        }

        // Nothing to show?
        if (menu.getNumItems() == 0) return;

        const int capturedTrackIdx = trackIdx;
        const SequencerTrackInfo capturedInfo = info;

        menu.showMenuAsync (
            juce::PopupMenu::Options()
                .withTargetComponent (this)
                .withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
            [this, capturedTrackIdx, capturedInfo] (int result)
            {
                if (result >= 1 && result <= 16
                    && capturedInfo.type == TrackType::SfPlayer)
                {
                    if (onSfTrackChannelChanged)
                        onSfTrackChannelChanged (capturedTrackIdx, result);
                }
                else if (result == kExternalOutputItemId)
                {
                    showExternalOutputDialog (capturedTrackIdx, capturedInfo.name);
                }
            });
    }

    //==========================================================================
    //  Option C flyout — small dialog with device + channel dropdowns
    //==========================================================================
    void showExternalOutputDialog (int trackIdx, const juce::String& trackName)
    {
        // Snapshot the current route and device list
        const juce::StringArray devNames = onGetOutputDeviceNames();
        const MidiTrackRoute    current  = onGetTrackRoute (trackIdx);

        // Build a component inside a DialogWindow
        class ExternalOutputComp : public juce::Component
        {
        public:
            ExternalOutputComp (const juce::StringArray& devs,
                                const MidiTrackRoute&    route,
                                int                      trackIdx,
                                const juce::String&      tName,
                                std::function<void(int, MidiTrackRoute)> apply)
                : deviceNames (devs), applyFn (std::move (apply)), ti (trackIdx)
            {
                setSize (380, 130);

                titleLabel.setText ("External output for:  " + tName,
                                    juce::dontSendNotification);
                titleLabel.setFont (juce::Font (13.f, juce::Font::bold));
                titleLabel.setColour (juce::Label::textColourId, juce::Colour (0xFF00CCAA));
                addAndMakeVisible (titleLabel);

                devLabel.setText ("Output:", juce::dontSendNotification);
                devLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFCCD0D8));
                addAndMakeVisible (devLabel);

                devBox.addItem ("(None)", 1);
                for (int i = 0; i < devs.size(); ++i)
                    devBox.addItem (devs[i], i + 2);
                devBox.setSelectedId (route.outputDeviceIndex + 2,
                                      juce::dontSendNotification);
                devBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xFF121820));
                devBox.setColour (juce::ComboBox::textColourId,       juce::Colour (0xFFCCD0D8));
                addAndMakeVisible (devBox);

                chLabel.setText ("Channel:", juce::dontSendNotification);
                chLabel.setColour (juce::Label::textColourId, juce::Colour (0xFFCCD0D8));
                addAndMakeVisible (chLabel);

                chBox.addItem ("─ pass-thru ─", 1);
                for (int ch = 1; ch <= 16; ++ch)
                    chBox.addItem ("Ch " + juce::String (ch), ch + 1);
                chBox.setSelectedId (route.channelOverride == 0 ? 1 : route.channelOverride + 1,
                                     juce::dontSendNotification);
                chBox.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xFF121820));
                chBox.setColour (juce::ComboBox::textColourId,       juce::Colour (0xFFCCD0D8));
                addAndMakeVisible (chBox);

                applyBtn.setButtonText ("Apply");
                applyBtn.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xFF006644));
                applyBtn.setColour (juce::TextButton::textColourOffId,  juce::Colours::white);
                addAndMakeVisible (applyBtn);

                cancelBtn.setButtonText ("Cancel");
                cancelBtn.setColour (juce::TextButton::buttonColourId,  juce::Colour (0xFF333344));
                cancelBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
                addAndMakeVisible (cancelBtn);

                applyBtn.onClick = [this]
                {
                    MidiTrackRoute r;
                    r.outputDeviceIndex = devBox.getSelectedId() - 2;
                    r.channelOverride   = chBox.getSelectedId() - 1;
                    if (r.channelOverride < 0 || r.channelOverride > 16)
                        r.channelOverride = 0;
                    if (applyFn) applyFn (ti, r);
                    closeParentDialog();
                };
                cancelBtn.onClick = [this] { closeParentDialog(); };
            }

            void paint (juce::Graphics& g) override
            {
                g.fillAll (juce::Colour (0xFF0D0D14));
            }

            void resized() override
            {
                auto a = getLocalBounds().reduced (12);
                titleLabel.setBounds (a.removeFromTop (24));
                a.removeFromTop (8);

                auto devRow = a.removeFromTop (26);
                devLabel.setBounds (devRow.removeFromLeft (70));
                devBox.setBounds   (devRow.reduced (2, 0));

                a.removeFromTop (6);

                auto chRow = a.removeFromTop (26);
                chLabel.setBounds (chRow.removeFromLeft (70));
                chBox.setBounds   (chRow.reduced (2, 0));

                a.removeFromTop (10);
                auto btns = a.removeFromTop (28);
                cancelBtn.setBounds (btns.removeFromLeft (80));
                btns.removeFromLeft (8);
                applyBtn.setBounds  (btns.removeFromLeft (80));
            }

        private:
            void closeParentDialog()
            {
                if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
                    dw->exitModalState (0);
            }

            juce::StringArray                        deviceNames;
            std::function<void(int, MidiTrackRoute)> applyFn;
            int                                      ti;

            juce::Label      titleLabel, devLabel, chLabel;
            juce::ComboBox   devBox, chBox;
            juce::TextButton applyBtn, cancelBtn;
        };

        auto* comp = new ExternalOutputComp (
            devNames, current, trackIdx, trackName,
            [this] (int ti, MidiTrackRoute r)
            {
                if (onSetTrackRoute) onSetTrackRoute (ti, r);
                repaint();
            });

        juce::DialogWindow::LaunchOptions opts;
        opts.content.setOwned (comp);
        opts.dialogTitle             = "External MIDI Output";
        opts.dialogBackgroundColour  = juce::Colour (0xFF0D0D14);
        opts.escapeKeyTriggersCloseButton = true;
        opts.useNativeTitleBar       = false;
        opts.resizable               = false;
        opts.launchAsync();
    }

    //==========================================================================
    juce::Rectangle<int> getRowBounds (int i) const
    {
        return { 0, i * trackH, getWidth(), trackH };
    }

    //==========================================================================
    //  Timer — decays MIDI activity indicators and repaints
    //==========================================================================
    void timerCallback() override
    {
        const int n = juce::jmin (engine.getNumTracks(), kMaxTracks);
        bool needsRepaint = false;

        for (int i = 0; i < n; ++i)
        {
            // Poll the engine's per-track activity flag (lock-free).
            // getMidiActivityAndClear returns true and clears the flag if
            // the audio thread fired a note on this track since last call.
            if (engine.getMidiActivityAndClear (i))
            {
                midiActivityFlags[i].store (true, std::memory_order_relaxed);
            }

            // Consume the local flag and start/continue the hold counter
            if (midiActivityFlags[i].exchange (false, std::memory_order_relaxed))
            {
                midiHoldCounters[i] = kHoldTicks;
                needsRepaint = true;
            }
            else if (midiHoldCounters[i] > 0)
            {
                --midiHoldCounters[i];
                needsRepaint = true;
            }
        }

        if (needsRepaint)
            repaint();
    }

    //==========================================================================
    SequencerEngine& engine;
    int selectedTrack = 0;
    int trackH        = 54;

    // MIDI receive indicator state
    static constexpr int kMaxTracks  = 64;
    static constexpr int kHoldTicks  = 3;   // 3 × 100ms = 300ms hold

    std::atomic<bool> midiActivityFlags[kMaxTracks] = {};
    int               midiHoldCounters  [kMaxTracks] = {};

    // Context menu item IDs
    static constexpr int kExternalOutputItemId = 200;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeaderStrip)
};
