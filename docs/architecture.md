# Architecture notes (M1)

## Shape

```
MIDI in ──┐
          ├─> ScratchpadProcessor::processBlock ──> SynthEngine ──> VoiceAllocator ──> Voice[16]
UI events ┘      (splits the block at MIDI timestamps)                                  oscillator -> gate
```

- `ScratchpadProcessor` owns the `AudioProcessorValueTreeState` and the `UndoManager`.
  Every user-facing control is a parameter. `osc1.enabled` (module presence) and
  `osc1.wave` are not automatable.
- `SynthEngine` is plain C++ over `EngineParams`. It never touches JUCE parameter
  objects, so tests drive it directly.
- `VoiceAllocator` is custom, not `juce::Synthesiser`. Steal order: released voices
  (oldest release first), then held voices (oldest note first). A stolen voice fades
  over 5 ms before the new note starts on it. 8 voices are active in M1; 16 are
  pre-allocated.
- `ScratchpadCore` (a static library) holds everything except `PluginEntry.cpp`, which
  is the only file in the plugin target. Tests link the same library.

## Real-time rules

Nothing on the audio thread allocates, locks, logs or waits.

- The editor never calls into DSP. Keyboard, bend wheel and wave-picker preview notes
  go through `UiEventQueue`, a single-producer single-consumer `AbstractFifo` of fixed
  size. The on-screen keyboard uses its own `MidiKeyboardState` on the message thread
  and forwards to the queue, because `MidiKeyboardState::processNextMidiBuffer` takes
  a lock.
- `BlepTable` is built in `SynthEngine::prepare` and read-only afterwards.
- The output peak for the meter is one atomic updated with a compare-exchange.

## Oscillator

Saw and pulse use a band-limited step (BLEP) added at each jump. Triangle uses the
integral of the same table (BLAMP) at each corner. The table is the integral of a
Kaiser-windowed sinc low-pass (cutoff 0.43 cycles per sample, 32 taps, 256
sub-sample rows, linear interpolation between rows). A symmetric kernel needs
samples from before the jump, so every oscillator runs `Oscillator::latencySamples`
(16) samples late; the processor reports that to the host.

Measured (`tests/OscillatorTests.cpp`): aliasing is at least 60 dB below the
fundamental for notes up to C7 at 48 kHz, and pitch is within 0.1 cent across the
range. The simpler two-sample PolyBLEP the brief names was measured first and reached
only about 30 dB at C7, which is why the table approach is used.

Switching wave while a note is held crossfades the two shapes at the same phase over
10 ms. A shape that was not running is primed from the phase history first, so the
crossfade does not start with a gap.

## Gate

Until an Amp Envelope exists, a fixed linear 2 ms ramp gates each voice. It can be
re-targeted mid-ramp, so a quick note-off or retrigger continues from the current
level rather than jumping. The gate is drawn on screen because it is part of the
audio path.

## State and undo

- Programmatic edits (pick wave, change wave, new patch) first catch the tree up with
  `copyState()`, then open a named undo transaction and write through
  `getParameterAsValue`, which records the change in the transaction immediately.
  Parameter values reach the tree on a 10-50 Hz timer, so skipping the catch-up could
  write into a stale tree and silently do nothing.
- Knob drags use `SliderParameterAttachment` with the undo manager. One drag is one
  step, renamed on release to "Level -6.0 dB → -22.4 dB".
- `ParameterAttachment` callbacks use the value they are handed. JUCE calls listeners
  last-added-first, so the APVTS raw value can still be the old one inside a callback.
- Saved state is the tree as XML with `schemaVersion`. `patch::migrate` upgrades older
  versions in place and refuses newer ones.

## Not in M1

Filter, envelopes, LFOs and modulation, mod wheel and aftertouch destinations (both
are parsed from MIDI but nothing listens yet), effects, the safety limiter, the
Explain panel and scope, patch save and open, resizing, the light theme switch and
Steinberg validator in CI. With no limiter yet, eight voices at full level can exceed
0 dBFS: the oscillator defaults to −6 dB and the output meter shows it.
