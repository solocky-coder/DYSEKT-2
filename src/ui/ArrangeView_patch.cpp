// ============================================================================
// PATCH: ArrangeView.h — Option C MIDI routing callback plumbing
//
// Apply these three changes to src/ui/ArrangeView.h
// ============================================================================

// ── CHANGE 1 ─────────────────────────────────────────────────────────────────
// In the ArrangeView includes, add MidiRouter (standalone guard optional):
// Place after the existing #includes at the top of the file.
// ─────────────────────────────────────────────────────────────────────────────

#if DYSEKT_STANDALONE
  #include "../standalone/MidiRouter.h"
#endif


// ── CHANGE 2 ─────────────────────────────────────────────────────────────────
// In the ArrangeView public section, add three callback members.
// Place them after the existing onTrackMuted / onSfTrackChannelChanged
// callbacks (search for "trackStrip.onTrackMuted" to find the right spot).
// ─────────────────────────────────────────────────────────────────────────────

    // Wire these from MainWindow (standalone only) to enable Option C routing UI.
    // Left nullptr in the plugin build — no routing controls shown.
#if DYSEKT_STANDALONE
    std::function<juce::StringArray()>             onGetMidiOutputDeviceNames;
    std::function<MidiTrackRoute(int)>             onGetTrackRoute;
    std::function<void(int, MidiTrackRoute)>       onSetTrackRoute;
#endif


// ── CHANGE 3 ─────────────────────────────────────────────────────────────────
// In the ArrangeView constructor, after the existing trackStrip callback setup,
// forward the three callbacks into TrackHeaderStrip.
//
// FIND this block (around line 106):
//
//   trackStrip.onTrackSelected = [this] (int idx)
//   {
//       selectTrack (idx);
//       repaint();
//   };
//   trackStrip.onTrackMuted = [this] (int, bool) { repaint(); };
//
// ADD after the closing semicolon of onTrackMuted:
// ─────────────────────────────────────────────────────────────────────────────

#if DYSEKT_STANDALONE
        // Forward Option C routing callbacks from whoever owns us (MainWindow)
        // into TrackHeaderStrip.  The lambdas capture `this` so they work even
        // if the parent sets the functions after construction.
        trackStrip.onGetOutputDeviceNames = [this] () -> juce::StringArray
        {
            return onGetMidiOutputDeviceNames ? onGetMidiOutputDeviceNames()
                                              : juce::StringArray{};
        };
        trackStrip.onGetTrackRoute = [this] (int ti) -> MidiTrackRoute
        {
            return onGetTrackRoute ? onGetTrackRoute (ti) : MidiTrackRoute{};
        };
        trackStrip.onSetTrackRoute = [this] (int ti, MidiTrackRoute r)
        {
            if (onSetTrackRoute) onSetTrackRoute (ti, r);
        };
#endif


// ── CHANGE 4 (MainWindow.h) ───────────────────────────────────────────────────
// In MainWindow.h constructor, after `editor` is constructed, wire the
// Option C callbacks into ArrangeView.
// ADD after:
//   editor = std::make_unique<DysektEditor> (*processor);
// ─────────────────────────────────────────────────────────────────────────────

        // Wire Option C MIDI routing callbacks into ArrangeView
        editor->arrangeView.onGetMidiOutputDeviceNames = [this] ()
        {
            return midiRouter->getOutputDeviceNames();
        };
        editor->arrangeView.onGetTrackRoute = [this] (int ti)
        {
            return midiRouter->getTrackRoute (ti);
        };
        editor->arrangeView.onSetTrackRoute = [this] (int ti, MidiTrackRoute r)
        {
            midiRouter->setTrackRoute (ti, r);
        };
