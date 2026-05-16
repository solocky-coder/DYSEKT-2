#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../sequencer/SequencerEngine.h"
#include "../audio/SfzPlayer.h"   // for Sf2PresetInfo

//==============================================================================
//  TrackHeaderStrip
//
//  Vertical list of track headers, one row per track.
//  Shows: colour swatch | track name | type badge | mute | (×) remove SF
//
//  Footer:
//    [ + SF TRACK ]  — opens a PopupMenu of available SF2 presets.
//                      Presets already added show as ticked/greyed.
//
//  Owner wires:
//    setAvailablePresets()       — call on every SF2 load
//    onAddSfTrackRequested       — user picked a preset to add
//    onRemoveSfTrack(trackIndex) — user clicked × on an SF track
//
//  Scrolls automatically when track list exceeds visible height.
//==============================================================================
class TrackHeaderStrip : public juce::Component,
                         private juce::Timer,
                         private juce::ScrollBar::Listener
{
public:
    static constexpr int kTrackH  = 36;
    static constexpr int kFooterH = 28;
    static constexpr int kScrollW = 10;

    //==========================================================================
    std::function<void(int)>                   onTrackSelected;
    std::function<void(int, bool)>             onTrackMuted;
    std::function<void(const Sf2PresetInfo&)>  onAddSfTrackRequested;
    std::function<void(int)>                   onRemoveSfTrack;

    //==========================================================================
    explicit TrackHeaderStrip (SequencerEngine& seq) : engine (seq)
    {
        scrollBar.setRangeLimits (0.0, 1.0);
        scrollBar.setCurrentRange (0.0, 1.0);
        scrollBar.setAutoHide (true);
        scrollBar.setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xFF2A3848));
        scrollBar.addListener (this);
        addChildComponent (scrollBar);
        startTimerHz (10);
    }

    ~TrackHeaderStrip() override { scrollBar.removeListener (this); stopTimer(); }

    int  getSelectedTrack() const noexcept { return selectedTrack; }
    void setSelectedTrack (int i)          { selectedTrack = i; repaint(); }

    /** Feed in the full preset list from the loaded SF2/SFZ.
     *  Call whenever a new instrument is loaded. */
    void setAvailablePresets (std::vector<Sf2PresetInfo> p)
    {
        availablePresets = std::move (p);
        repaint();
    }

    int getRequiredHeight() const { return engine.getNumTracks() * kTrackH + kFooterH; }

    //==========================================================================
    void resized() override
    {
        updateScrollRange();
        scrollBar.setBounds (getWidth() - kScrollW, 0, kScrollW, getHeight() - kFooterH);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xFF0A0A10));

        const int listH   = getHeight() - kFooterH;
        const int n       = engine.getNumTracks();
        const int scrollY = scrollOffset;

        // ── Track rows ────────────────────────────────────────────────────────
        g.saveState();
        g.reduceClipRegion (0, 0, getWidth(), listH);

        for (int i = 0; i < n; ++i)
        {
            const auto rowR = rowBounds (i, scrollY);
            if (rowR.getBottom() < 0 || rowR.getY() > listH) continue;
            paintRow (g, i, rowR);
        }

        g.restoreState();

        // ── Footer ────────────────────────────────────────────────────────────
        paintFooter (g, listH);
    }

    //==========================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int listH   = getHeight() - kFooterH;
        const int scrollY = scrollOffset;

        // Footer click → preset picker
        if (e.y >= listH) { showPresetMenu(); return; }

        const int n = engine.getNumTracks();
        for (int i = 0; i < n; ++i)
        {
            const auto rowR = rowBounds (i, scrollY);
            if (! rowR.contains (e.getPosition())) continue;

            const auto info   = engine.getTrackInfo (i);
            const auto muteR  = muteRect (rowR);
            const auto closeR = closeRect (rowR);

            if (info.type == TrackType::SfPlayer && closeR.contains (e.getPosition()))
            {
                if (onRemoveSfTrack) onRemoveSfTrack (i);
                return;
            }
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
            return;
        }
    }

    void mouseWheelMove (const juce::MouseEvent&,
                         const juce::MouseWheelDetails& w) override
    {
        const int totalH   = engine.getNumTracks() * kTrackH;
        const int visibleH = getHeight() - kFooterH;
        if (totalH <= visibleH) return;
        scrollOffset = juce::jlimit (0, totalH - visibleH,
                                     scrollOffset - (int)(w.deltaY * 60.f));
        updateScrollRange();
        repaint();
    }

private:
    SequencerEngine&           engine;
    int                        selectedTrack  = 0;
    int                        scrollOffset   = 0;
    std::vector<Sf2PresetInfo> availablePresets;
    juce::ScrollBar            scrollBar { true };

    //==========================================================================
    juce::Rectangle<int> rowBounds (int i, int scrollY) const
    {
        return { 0, i * kTrackH - scrollY, getWidth() - kScrollW, kTrackH };
    }

    juce::Rectangle<int> muteRect (juce::Rectangle<int> r) const
    {
        return r.withTrimmedLeft (r.getWidth() - 28).reduced (4, 8);
    }

    juce::Rectangle<int> closeRect (juce::Rectangle<int> r) const
    {
        return r.withTrimmedLeft (r.getWidth() - 54)
                .withTrimmedRight (28)
                .reduced (4, 10);
    }

    //==========================================================================
    void paintRow (juce::Graphics& g, int i, juce::Rectangle<int> rowR) const
    {
        const auto info = engine.getTrackInfo (i);
        const bool sel  = (i == selectedTrack);

        // Background
        g.setColour (sel ? juce::Colour (0xFF182030) : juce::Colour (0xFF0D0D16));
        g.fillRect (rowR);

        // Colour swatch
        g.setColour (info.colour);
        g.fillRect (rowR.withTrimmedRight (rowR.getWidth() - 4).toFloat());

        // Mute button
        const auto muteR = muteRect (rowR);
        g.setColour (info.enabled ? juce::Colour (0xFF2A8060) : juce::Colour (0xFF602020));
        g.fillRoundedRectangle (muteR.toFloat(), 3.f);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.setFont (juce::Font (8.f, juce::Font::bold));
        g.drawText (info.enabled ? "M" : "m", muteR, juce::Justification::centred, false);

        // × remove — SF tracks only
        if (info.type == TrackType::SfPlayer)
        {
            const auto cr = closeRect (rowR);
            g.setColour (juce::Colour (0xFF3A1A1A));
            g.fillRoundedRectangle (cr.toFloat(), 3.f);
            g.setColour (juce::Colour (0xFFBB5050));
            g.setFont (juce::Font (9.f, juce::Font::bold));
            g.drawText (juce::CharPointer_UTF8 ("\xc3\x97"), cr, juce::Justification::centred, false);
        }

        // Track name
        const int btnPad = (info.type == TrackType::SfPlayer) ? 58 : 32;
        g.setFont (juce::Font (10.f, juce::Font::bold));
        g.setColour (sel ? info.colour : juce::Colour (0xFFCCD0D8));
        g.drawText (info.name, rowR.getX() + 6, rowR.getY(),
                    rowR.getWidth() - 6 - btnPad, kTrackH,
                    juce::Justification::centredLeft, true);

        // Type badge (bottom-left)
        juce::String badge;
        switch (info.type)
        {
            case TrackType::MainSlice:      badge = "SL"; break;
            case TrackType::ChromaticSlice: badge = "CH"; break;
            case TrackType::SfPlayer:       badge = "SF"; break;
        }
        g.setFont (juce::Font (8.f));
        g.setColour (info.colour.withAlpha (0.7f));
        g.drawText (badge, rowR.getX() + 6, rowR.getY() + kTrackH / 2,
                    20, kTrackH / 2, juce::Justification::centredLeft, false);

        // Separator
        g.setColour (juce::Colour (0xFF1C2028));
        g.fillRect (0, rowR.getBottom() - 1, getWidth(), 1);
    }

    void paintFooter (juce::Graphics& g, int listH) const
    {
        const auto footR = juce::Rectangle<int> (0, listH, getWidth() - kScrollW, kFooterH);
        g.setColour (juce::Colour (0xFF070710));
        g.fillRect (footR);
        g.setColour (juce::Colour (0xFF1C2028));
        g.fillRect (footR.getX(), footR.getY(), footR.getWidth(), 1);

        const bool hasSf = ! availablePresets.empty();
        const auto btnR  = footR.reduced (6, 5);

        g.setColour (hasSf ? juce::Colour (0xFF142030) : juce::Colour (0xFF0E0E18));
        g.fillRoundedRectangle (btnR.toFloat(), 4.f);
        g.setColour (hasSf ? juce::Colour (0xFF3A7868) : juce::Colour (0xFF222830));
        g.drawRoundedRectangle (btnR.toFloat(), 4.f, 1.f);

        g.setFont (juce::Font (9.5f, juce::Font::bold));
        g.setColour (hasSf ? juce::Colour (0xFF60C0A8) : juce::Colour (0xFF303848));
        g.drawText ("+ SF TRACK", btnR, juce::Justification::centred, false);
    }

    //==========================================================================
    void showPresetMenu()
    {
        if (availablePresets.empty()) return;

        juce::PopupMenu menu;
        for (int i = 0; i < (int) availablePresets.size(); ++i)
        {
            const auto& p       = availablePresets[(size_t) i];
            const bool  already = isAlreadyAdded (p);
            juce::String lbl    = p.name.isNotEmpty()
                ? p.name
                : ("Bank " + juce::String (p.bank) + "  Prg " + juce::String (p.preset));
            menu.addItem (i + 1, lbl, /*enabled=*/ ! already, /*ticked=*/ already);
        }

        menu.showMenuAsync (juce::PopupMenu::Options{},
            [this] (int result)
            {
                if (result > 0 && onAddSfTrackRequested)
                    onAddSfTrackRequested (availablePresets[(size_t)(result - 1)]);
            });
    }

    bool isAlreadyAdded (const Sf2PresetInfo& p) const
    {
        for (int i = 0; i < engine.getNumTracks(); ++i)
        {
            const auto info = engine.getTrackInfo (i);
            if (info.type == TrackType::SfPlayer
                && info.preset.bank   == p.bank
                && info.preset.preset == p.preset)
                return true;
        }
        return false;
    }

    //==========================================================================
    void updateScrollRange()
    {
        const int totalH   = engine.getNumTracks() * kTrackH;
        const int visibleH = juce::jmax (1, getHeight() - kFooterH);

        if (totalH <= visibleH)
        {
            scrollOffset = 0;
            scrollBar.setVisible (false);
        }
        else
        {
            scrollBar.setVisible (true);
            scrollBar.setRangeLimits (0.0, (double) totalH);
            scrollBar.setCurrentRange ((double) scrollOffset, (double) visibleH);
        }
    }

    void scrollBarMoved (juce::ScrollBar* bar, double newRange) override
    {
        if (bar == &scrollBar)
        { scrollOffset = (int) newRange; repaint(); }
    }

    void timerCallback() override { updateScrollRange(); repaint(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackHeaderStrip)
};
