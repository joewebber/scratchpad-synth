# Architecture notes (M2)

## Shape

```
MIDI in ──┐
          ├─> ScratchpadProcessor::processBlock ──> SynthEngine ──> VoiceAllocator ──> Voice[16]
UI events ┘      (splits the block at MIDI timestamps)       osc 1-3 + noise -> filter -> amp (envelope or gate)
```

- `ScratchpadProcessor` owns the `AudioProcessorValueTreeState` and the `UndoManager`.
  Every user-facing control is a parameter. `osc1.enabled` (module presence) and
  `osc1.wave` are not automatable.
- `SynthEngine` is plain C++ over `EngineParams`. It never touches JUCE parameter
  objects, so tests drive it directly.
- `VoiceAllocator` is custom, not `juce::Synthesiser`. Steal order: released voices
  (oldest release first), then held voices (oldest note first). A stolen voice fades
  over 5 ms before the new note starts on it. `voicing.voices` (default 8) are active
  in poly mode; 16 are pre-allocated. Mono and legato use one voice and a
  last-note-priority stack instead.
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

## Voice chain (M2)

Per voice: up to three oscillators and a noise source are summed, then the filter, then
the amp stage. `EngineParams` (plain values in the engine's units) is built once per
block by the processor from cached parameter pointers.

- **Smoothing.** The engine owns one 20 ms smoother per patch-wide value (oscillator
  and noise gains, cutoff in log2, resonance, drive, filter-envelope amount, output
  volume) so every voice follows the same curve. It hands each voice the value at the
  start and end of every 32-sample sub-block (`BlockLevels`), and the voice
  interpolates linearly per sample. Pitch is interpolated the same way. Disabling a
  module ramps its gain to zero; it never steps.
- **Filter.** Cutoff for a voice is `log2(cutoff) + keyTracking * (note - 60) / 12 +
  filterEnvAmount * 6 * envelope`. LP 12, HP 12 and BP 12 are one TPT state-variable
  filter; LP 24 is a four-pole TPT ladder with `tanh` in the feedback. Switching mode
  crossfades the outgoing and incoming filters over 10 ms. Resonance 1.0 rings forever
  by design: the ladder's feedback goes slightly past 4, and the SVF's damping goes
  slightly negative with a term that grows with the band-pass level, so the ringing
  settles at a steady level at every cutoff. Bypassing the filter is a 10 ms wet fade.
- **Envelopes.** Exponential ADSR. Each segment aims a little past its destination
  (`Envelope.h`) so it arrives in finite time: the attack reaches 1.0 after the attack
  time, decay reaches sustain after the decay time, and release falls from full scale to
  silence (below -80 dB) in the release time. Retriggering starts from the current level.
  The amp and filter envelopes always run inside a voice; `enabled` decides whether the
  amp stage uses the envelope (crossfaded with the gate over 10 ms) and whether the
  filter envelope's amount is applied.
- **Voicing.** Glide is a linear ramp in semitones over the glide time. Poly voices
  glide from the last note they played; mono and legato glide from the sounding pitch.
  Mono retriggers on every note and on returning to a held note; legato retriggers only
  when no key was held. Changing voicing mode releases the played notes.
- **Latency.** The oscillator runs 16 samples late, and a fresh voice holds its amp
  stage closed for the same 16 samples so the attack is not eaten.
- **Modules.** `plugin::Module` names the six things a user adds. `addModule` and
  `removeModule` reset the module's parameters and flip its `enabled` flag in one named
  undo step ("Added Filter"). `newPatch` resets every parameter.

Measured: eight voices with every module on cost about 2% of one core at 48 kHz in
256-sample blocks (the brief's target is under 15%).

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
  versions in place and refuses newer ones. Schema 2 (M2) only adds parameters, so a
  schema 1 state migrates by restamping; `setStateInformation` resets every parameter to
  its default first, so parameters an older state does not mention come up at defaults.

## Not in M2

LFOs and modulation, oscillator sync, ring modulation, FM and unison, mod wheel and aftertouch destinations (both
are parsed from MIDI but nothing listens yet), effects, the safety limiter, the
Explain panel and scope, live module visualisations, patch save and open, resizing, the light theme switch and
Steinberg validator in CI. With no limiter yet, eight voices at full level can exceed
0 dBFS: the oscillator defaults to −6 dB and the output meter shows it.
