#pragma once
// =============================================================================
// DysektMidi2Port.h  —  VST3 two-port MIDI side-channel bridge
// =============================================================================
//
// Problem solved
// ──────────────
// JUCE 8's AudioProcessor::processBlock() receives a single MidiBuffer.  When
// we patch juce_audio_plugin_client_VST3.cpp to advertise two MIDI input buses
// (Port 0 "DYSEKT-SF" → Slicer, Port 1 "DY-SFP" → SF-Player), the DAW sends
// events for each port with a different Steinberg::Vst::Event::busIndex value.
// The wrapper must deliver Port-1 events to processBlock() without contaminating
// the Port-0 MidiBuffer that drives the slicer.
//
// Solution
// ────────
// A thread_local pointer set by the wrapper immediately before each processBlock
// call and cleared immediately after.  DysektProcessor reads it during
// processBlock to obtain the Port-1 (SF-Player) MIDI stream.
//
// Thread safety
// ─────────────
// thread_local gives each audio thread its own instance, so two plugin instances
// running on different audio threads are fully isolated.
//
// Lifetime contract
// ─────────────────
// The pointer is valid ONLY within the duration of processBlock().  Do not
// store it; read-and-use it in place.
//
// Fallback
// ────────
// If the pointer is nullptr (DAW does not support multi-port MIDI, or is calling
// processBlock outside the normal VST3 path), DysektProcessor falls back to the
// legacy channel-based routing (sf2Ch dedicted channel on port 0).
//
// Integration
// ───────────
//  1. juce_audio_plugin_client_VST3.cpp  — includes this header, calls
//       dysektSetSfPlayerMidiPort(&sfPlayerMidiBuffer) / (nullptr)
//  2. src/PluginProcessor.cpp             — defines the thread_local + helpers,
//       reads dysektGetSfPlayerMidiPort() in processBlock()
// =============================================================================

// Forward-declare only — avoids dragging in all of juce_audio_basics from here.
namespace juce { class MidiBuffer; }

// Implemented in PluginProcessor.cpp; called by the patched JUCE VST3 wrapper.
extern "C" void         dysektSetSfPlayerMidiPort (juce::MidiBuffer* buf) noexcept;
extern "C" juce::MidiBuffer* dysektGetSfPlayerMidiPort()                  noexcept;
