#pragma once
#include <juce_core/juce_core.h>
#include <atomic>

// ─────────────────────────────────────────────────────────────────────────────
//  AbletonLink
//
//  Wraps the Ableton Link SDK (link/include/ableton/Link.hpp).
//  Conditionally compiled — without DYSEKT_HAS_LINK the class is a no-op
//  stub so the rest of the codebase needs zero #ifdefs.
//
//  Setup (one-time):
//    git submodule add https://github.com/Ableton/link.git link
//    git submodule update --init
//  CMakeLists.txt detects the submodule and sets DYSEKT_HAS_LINK=1.
//
//  Audio-thread use in SequencerEngine::processBlock():
//    float bpm = abletonLink.getBpm (internalBpm);   // Link BPM or fallback
// ─────────────────────────────────────────────────────────────────────────────

#if DYSEKT_HAS_LINK
  #include <ableton/Link.hpp>
#endif

class AbletonLink
{
public:
    AbletonLink()
    {
#if DYSEKT_HAS_LINK
        link = std::make_unique<ableton::Link> (120.0);
        link->enable (false);
        link->setTempoCallback ([this] (double bpm)
        {
            cachedBpm.store ((float) bpm, std::memory_order_relaxed);
        });
        link->setNumPeersCallback ([this] (std::size_t n)
        {
            numPeers.store ((int) n, std::memory_order_relaxed);
        });
#endif
    }

    ~AbletonLink()
    {
#if DYSEKT_HAS_LINK
        if (link) link->enable (false);
#endif
    }

    // ── Enable / disable  (message thread) ───────────────────────────────────
    void setEnabled (bool v)
    {
#if DYSEKT_HAS_LINK
        if (link) link->enable (v);
#endif
        enabled.store (v, std::memory_order_relaxed);
    }

    bool isEnabled()   const noexcept { return enabled .load (std::memory_order_relaxed); }
    int  getPeerCount()const noexcept { return numPeers.load (std::memory_order_relaxed); }

    // ── Propagate local BPM change to Link session ────────────────────────────
    void setBpm (double bpm)
    {
#if DYSEKT_HAS_LINK
        if (link && isEnabled())
        {
            auto s = link->captureAppSessionState();
            s.setTempo (bpm, ableton::Link::Clock::now());
            link->commitAppSessionState (s);
        }
#endif
        cachedBpm.store ((float) bpm, std::memory_order_relaxed);
    }

    // ── Audio-thread accessors (no allocation, no locks) ─────────────────────

    /** Returns Link BPM when enabled, or fallback otherwise. */
    float getBpm (float fallback = 120.f) const noexcept
    {
        return isEnabled() ? cachedBpm.load (std::memory_order_relaxed) : fallback;
    }

    /** Phase in beats within the current quantum (0 .. quantum).
     *  quantum=4 = bar-level alignment. */
    double getPhase (double quantum = 4.0) const noexcept
    {
#if DYSEKT_HAS_LINK
        if (link && isEnabled())
        {
            const auto s = link->captureAudioSessionState();
            return s.phaseAtTime (ableton::Link::Clock::now(), quantum);
        }
#endif
        juce::ignoreUnused (quantum);
        return 0.0;
    }

    /** Request play aligned to the next quantum boundary — call on Play press. */
    void requestBeatAlignedStart (double quantum = 4.0)
    {
#if DYSEKT_HAS_LINK
        if (link && isEnabled())
        {
            auto s = link->captureAppSessionState();
            s.requestBeatAtStartPlayingTime (0.0, quantum);
            s.setIsPlaying (true, ableton::Link::Clock::now());
            link->commitAppSessionState (s);
        }
#endif
        juce::ignoreUnused (quantum);
    }

    void notifyStop()
    {
#if DYSEKT_HAS_LINK
        if (link && isEnabled())
        {
            auto s = link->captureAppSessionState();
            s.setIsPlaying (false, ableton::Link::Clock::now());
            link->commitAppSessionState (s);
        }
#endif
    }

private:
#if DYSEKT_HAS_LINK
    std::unique_ptr<ableton::Link> link;
#endif
    std::atomic<bool>  enabled  { false };
    std::atomic<float> cachedBpm{ 120.f };
    std::atomic<int>   numPeers { 0     };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AbletonLink)
};
