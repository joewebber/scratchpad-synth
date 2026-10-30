# Architecture notes (M5)

## Shape

```
MIDI in ──┐
          ├─> ScratchpadProcessor::processBlock ──> SynthEngine ──> VoiceAllocator ──> Voice[16]
UI events ┘      (splits the block at MIDI timestamps)       osc 1-3 + noise -> filter -> amp (envelope or gate) -> pan
                                                    shared LFOs, mod wheel, aftertouch ──┘  (modulation sources, see below)
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

- The editor never calls into DSP. Keyboard and bend wheel events
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
- **Modules.** `plugin::Module` names the eight things a user adds: oscillators 2 and 3, noise, the filter, both envelopes and both LFOs. `addModule` and
  `removeModule` reset the module's parameters and flip its `enabled` flag in one named
  undo step ("Added Filter"). `newPatch` resets every parameter.

Measured cost: see "Performance and validation (M6)".

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
  versions in place and refuses newer ones. Schemas 2 (M2), 3 (M3) and 4 (M4) only add parameters, so
  an older state migrates by restamping; `setStateInformation` resets every parameter to
  its default first, so parameters an older state does not mention come up at defaults.

## Modulation (M3)

Eight fixed connection slots, `modN.source`, `modN.dest` (non-automatable choices) and `modN.amount`
(automatable, -100% to +100%). A slot is unused while its source or destination is "None".
`ModSource` and `ModDest` (`engine/Modulation.h`) are saved as choice indices: append, never reorder.

- **Sources.** LFO 1, LFO 2 (bipolar), amp envelope and filter envelope (0..1, read while their module is
  in the patch, else 0), velocity, mod wheel, aftertouch (0..1) and key, `(note - 60) / 60` limited to +-1.
  A source whose module is absent reads 0, so a stale connection does nothing.
- **Evaluation.** Once per 32-sample sub-block per voice: every source is read at the end of the block, the
  connections are summed per destination (`accumulate`), and the voice interpolates linearly from the last
  block's sums to this one's, per sample. Envelopes and the mod wheel are therefore up to one block (0.7 ms)
  behind. The mod wheel and aftertouch are smoothed over 10 ms in the engine.
- **Scales** (a sum of +1.0, i.e. +100% from a source at its peak):

  | Destination | +100% means |
  | --- | --- |
  | Pitch (all, or one oscillator) | 2 semitones |
  | Pulse width | 0.45 of the cycle (the knob's own 5-95% limits still hold) |
  | Osc level, noise level | gain x (1 + sum), limited to 0..2: -100% is silence, +100% is +6 dB |
  | Filter cutoff | 6 octaves, the same scale as the filter envelope's amount |
  | Resonance | the whole 0..1 range |
  | Amp level | gain x (1 + sum), limited to 0..1: modulation can only turn it down |
  | Pan | hard left to hard right; a balance law, so a centred voice is exactly as loud as before |
  | LFO rate | 3 octaves (x or / 8) |

- **LFOs.** `dsp::Lfo` is a phase accumulator read at the end of a block; sample and hold draws a new
  xorshift value at each wrap. An LFO that does not retrigger is one shared phase, advanced by the engine;
  one that does keeps a phase per voice and restarts it (and its fade-in) on every note, including a mono
  retrigger. Fade-in is per voice either way: depth rises from 0 over `lfoN.fade` seconds after the note starts.
  LFO tempo sync is resolved in the processor (see "Effects and output (M5)").
- **LFO rate as a destination.** The shared LFOs can only follow sources every voice agrees on (the LFOs
  themselves, as of the last block, the mod wheel and aftertouch); envelopes, velocity and key do nothing to
  them. A retriggering LFO sees every source. An LFO cannot move its own rate.
- **Pan** needs a stereo voice, so `Voice::render` now adds to a left and a right buffer.
- **Live values.** The engine writes the newest note's source values and the LFO phases into
  `LiveModulation`, relaxed atomics, once per `render`. The editor reads them at 30 Hz for the moving dots.
- **Module removal.** Removing an LFO, an envelope or an oscillator, noise or the filter clears every
  connection that starts or ends there, in the same undo step as the removal.

## Editor structure (M3)

`PatchView` is the chain panel (sources, filter, amp with the voicing under it, output) over the modulation
strip (LFO 1, LFO 2, other sources' handles, the eight slots). `ModulationModel` is its message-thread view of
modulation: the slot snapshot (refreshed by a `ChangeBroadcaster` the processor fires from parameter
attachments, so undo, load and the host reach it), the live values, and the state of a connection being made.
Knobs and dials that modulation can reach register with it as `ModTarget`s and draw an arc per connection
(range the source can move the knob over, plus a dot where it is now). `ModOverlay` sits on top of the
PatchView: it draws the cable and lights valid knobs during a drag, takes the click on a knob while a handle
is armed, and owns the arcs at rest (drag one to change the amount, right-click to remove).

## Learning layer (M4)

- **Copy.** `source/content/explanations.json` (keyed by parameter ID and module key: `what`, `listenFor`,
  `tryThis`, `glossaryTerms`, plus a concept paragraph per module) and `glossary.json` are embedded through
  BinaryData and loaded once by `content::Content`. A test walks every parameter the processor registers and
  fails if one has no copy, so adding a parameter without wording is a test failure; another fails on copy for
  a parameter that does not exist or a glossary link that does not resolve.
- **Explain panel.** Controls name themselves with an `explainId` component property. Hover or keyboard focus
  shows that entry; a card's `?` pins the module's concept explainer until the pointer lands on a control.
  Entries longer than the body scroll with the wheel (a fade marks more below). The glossary lists every term.
- **Controls that are not parameters.** Bypass, solo, remove, Add module, modulation handles, the keyboard,
  octave buttons, wheels, scope switches, meter and the top-bar buttons have copy under `"controls"` in
  `explanations.json` (IDs like `control.bypass`; `Content::control`). A test builds the editor and fails if any
  button, slider or keyboard in it has no `explainId` that resolves to copy, so a new control cannot ship silent.
- **Bypass and solo.** `<module>.bypass` for every module and `<module>.solo` for the four sources are
  non-automatable parameters. The processor applies them when it builds the `EngineParams` for a block, so
  the engine only sees what is audible: a bypassed or muted module is simply absent, and the filter bypass is
  the same 10 ms fade as removing it. Soloing a source un-solos the others in the same undo step.
- **Patch description.** `describe (EngineParams)` is a pure function of the audible patch; reference patches
  are pinned in `PatchDescriptionTests`.
- **Scope.** The engine writes the selected tap (after the oscillators, after the filter, after the amp, after the
  effects and the safety limiter) into an 8,192-sample `AbstractFifo` once per sub-block; the amp tap is the voices'
  mix before any effect, the FX tap is what reaches the output. The editor drains it at 30 Hz and runs a 2,048-point Hann FFT on the message
  thread. The tap is a view, saved nowhere. `ScopeTests` checks that the spectrum at the filter tap loses its
  harmonics as the cutoff closes and that the oscillator tap does not.
- **Visualisations.** The oscillator card draws its wave; `FilterCurve` is computed from the parameter
  values, not measured; `EnvelopeDisplay` draws the ADSR with a dot for the newest note; the LFO card draws
  its shape with a moving dot (all from `LiveModulation`).
- **Suggested next module.** `plugin::suggestNext` walks the path amp envelope, oscillator 2, filter, filter
  envelope, LFO 1, noise, oscillator 3, LFO 2 and returns the first module not yet present, with a sentence
  of reason. The Add menu shows it first, in an accent block; its dashed slot on the canvas gets an accent
  outline and the reason as tooltip. Nothing is locked.

## Editor structure (M5)

The window is now 1500 x 900 (it was 840 high). The effects row cannot sit beside the stages: the chain row is 1,140 px
of the 1,158 the panel offers. Under them there was room for only about 58 px, and the row needs 124 (a 14 px caption,
the 102 px effect cards and their margins), so the chain panel grows from 506 to 566 px (the tallest column, amp, amp
envelope and voicing, is 380 px; the well adds 18 px above it and 6 below). Nothing else moved. The well's metrics are
constants in `PatchView.cpp` (`effectsBlock` is what the row needs), and a `static_assert` there keeps the four effect
places as wide as an effect card. The LFO cards are 288 px wide (were 285) for their third knob; the connections panel
takes the difference.

- **Oscillator extras.** Each oscillator card has a chevron (`MoreButton`, `control.oscMore`) beside Phase reset. It opens
  `OscillatorExtras` (unison copies, detune, spread, anti-aliasing, and on Oscillators 2 and 3 hard sync) as a drawer
  over the cards under it, so the column keeps its height and a full patch still fits. One drawer is open at a time; the
  state lives in `PatchView::openExtras`, not in the patch, and closes with its oscillator. Ring level, FM amount and FM
  target sit in Oscillator 1's drawer because they are Oscillator 1 acting on the others (their copy says "osc1"); they dim,
  and the line at the foot of the drawer says why, while Oscillator 2 (ring) or the FM target is not in the patch.
  The chevron wears a dot when the extras are doing something (unison above 1, sync, ring or FM) and an accent dot when
  anti-aliasing is off; with AA off the wave well also gets an accent outline and an "AA off" tag, which is the calm
  warning the brief asks for. Listeners read the parameter's own value, not the cached atomic (see State and undo).
- **Effect cards.** `EffectCard` (header with grip, name, "?", bypass and remove; a computed picture, `EffectDisplay`;
  the knobs) for distortion, chorus, delay and reverb. The delay shows a note-length knob in place of the time knob
  while Sync is on. The pictures are drawn from the parameters (transfer curve, swaying delay, echo bars with the
  tempo-synced time, decay line), like `FilterCurve`. Effects have no solo and no modulation destinations.
- **Order.** The effects row has four equal places. Present effects fill them from the left in the order of
  `ScratchpadProcessor::getEffectOrder()`; the dashed slots of absent effects follow. The editor never touches the engine:
  dragging a grip (`control.effectGrip`) or pressing Left / Right (Up / Down) while it has focus calls `moveEffect`
  with positions in the full four-entry order (a drop on a present neighbour's place maps to that neighbour's position,
  skipping absent effects). A drop indicator is drawn in the gap while dragging. Cards re-lay out when
  `getEffectsBroadcaster()` fires, so undo, loading and a new patch reorder them too.
- **LFO tempo sync.** Each LFO card has a Sync chip in its header and a note-length knob beside Rate. With Sync on, Rate dims
  and the wave picture shows the rate the tempo gives (`params::lfoSyncRateHz`, following `getBpm()`).
- **Limiter light.** `LimiterLight` (top bar, left of the meter, `control.limiter`) polls
  `getLimiterReductionDb()` on the vertical blank like the meter and lights while it is above 0.1 dB, with a quarter-second
  hold so a brief touch is seen.
- **Learning-layer test.** The test that walks the editor still requires an `explainId` on every button, slider and
  keyboard. New controls get theirs from the parameter ID (knobs, chips, segments), from `control.limiter`,
  `control.effectGrip`, `control.oscMore`, `control.addModule` (the effect slots), or from a module key.

## Oscillator extras (M5)

`OscSection` (`source/engine/OscSection.*`) holds the three oscillators of a voice with everything that links them; the
voice owns two of them on the heap (`Voice` stays small on the stack). It makes stereo audio per oscillator; levels, ring
modulation and the mix stay in the voice.

- **Unison.** 1 to 7 copies per oscillator. Copy `c` of `n` sits at `u = 2c/(n-1) - 1`, is detuned by `u * detuneCents` and
  panned to `u * spread` with an equal-power law; a centred copy is left and right at unity, so unison 1 is exactly the single
  oscillator and the defaults render what they did before. Copies are scaled by `1/sqrt(n)`. Copy 0 starts at phase 0 when
  Phase reset is on, the others at random phases drawn from the voice's seed, so a patch repeats from run to run. Gains
  (3 ms one-pole) and detune (a line across the block) move smoothly: a copy that joins fades in from silence, one that
  leaves fades out first. The filter and envelopes are shared. The signal stays mono until a copy is off centre; the voice
  then runs a second filter state for the right channel (copied from the first when the voice turns stereo).
  Everything is pre-allocated for 16 voices x 3 oscillators x 7 copies.
- **Anti-aliasing off.** The BLEP/BLAMP gain moves to 0 over 10 ms (and back). The delay ring stays, so the naive wave
  arrives with the same latency and the switch costs nothing extra and does not click.
- **Hard sync** (oscillators 2 and 3). A slave copy restarts its cycle when the matching oscillator-1 copy wraps. The restart
  is placed to a fraction of a sample and corrected with a BLEP (and a BLAMP for the slope) at the jump, so the tearing is
  band-limited. Sync together with FM on the same oscillator restarts without the correction and can alias.
- **Ring modulation.** Oscillator 1 x oscillator 2 (stereo, before their levels) is an extra mixer source at `ringLevelDb`.
  Both oscillators run even when their own levels are -inf. The sine now goes through the same 16-sample delay ring as the
  other shapes so a sync restart in a sine slave can be corrected; the ring product uses the delayed outputs.
- **FM.** Oscillator 1 phase-modulates `fmTarget` (oscillator 2, 3 or both), DX style: the carrier's phase is advanced
  by `fmMaxIndex * amount * m / (2 pi)` cycles with `fmMaxIndex = 10` rad, so 100 % is a modulation index of 10 and the
  sidebands are `fc +- k*fm` weighted by Bessel `J_k(index)` (tested for index 3). `m` is oscillator 1's naive wave, not the
  band-limited one: that arrives 16 samples late and the carrier would then be late twice.
- **Oversampling.** While any FM amount is above 0 the second `OscSection` runs at 2x with a 33-tap half-band decimator
  after each oscillator, and the voice crossfades from the 1x section to it over 10 ms (and back). The 2x section starts from
  the 1x section's state and runs unheard for 16 samples to fill the decimator. FM amount 0 is bit-identical to FM off.
- **Latency.** Unchanged at `Oscillator::latencySamples` (16): the 2x oscillators' own delay is 8 base-rate samples and the
  decimator's is 8, so the 1x and 2x paths are in step and oscillators, ring, FM and unison all line up.
- **The safety limiter and the tests.** The limiter acts from -0.9 dBFS, so tests that compare waveforms exactly or expect a
  linear response play at -12 dB or below; a full-scale sine, saw or ring product would legitimately touch it.

## Effects and output (M5)

- **Chain.** `EffectsChain` (`engine/EffectsChain.*`) runs distortion, chorus, delay and reverb on the summed stereo mix, in
  `EngineParams::effectOrder` (default in that order), only the effects that are `enabled`. Each effect lives in `dsp/` and
  smooths its own parameters; nothing steps. The chain works in 32-sample chunks. After it come the output volume, then the
  limiter; both are in `SynthEngine::render`.
- **Switching on and off.** An effect that is switched on or off is crossfaded with the signal arriving at it over 15 ms; it
  keeps running (tail and all) until the fade is finished, then is skipped and costs nothing. Its memory is cleared the next
  time it is switched on, and a patch that arrives with effects on has them fully on from the first sample.
- **Reordering.** Reordering effects that are running (a reorder that leaves the audible order unchanged is applied at once,
  silently) ducks the whole chain's output to silence over 10 ms, swaps the order while it is silent, and fades back in over
  10 ms. Tails are not cleared: at one delay time later the delay line replays what it held, which was made by the old order
  and, at most, is out of step with the new one by the distortion's latency. `EffectsTests` pins that the swap itself does not
  step (it fails if the duck is removed).
- **Limiter.** `dsp::Limiter` is always on, after the output volume: no look-ahead and no latency, one gain for both channels
  from the louder of the pair. Unity up to -0.9 dBFS (0.9), a quadratic soft knee that reaches the ceiling with slope 0, a flat
  ceiling at -0.3 dBFS (0.966); the gain falls at once and recovers with a 100 ms one-pole release, and a final clamp makes the
  ceiling absolute. A signal that never reaches the knee passes bit for bit. Its reduction (deepest of each block, falling
  about 58 dB per second) goes to the top bar's limiter light through `getLimiterReductionDb()`.
- **Latency.** Distortion oversamples 2x, which delays its wet path; inside it the dry path is delayed to match, so a mix is
  aligned. `getEffectsLatencySamples()` is that delay, constant for a sample rate, and is added to the oscillators' 16 samples
  for `setLatencySamples` at `prepareToPlay`. The audio is late only while distortion is on, but the host is always told, so
  the reported latency never changes mid-session.
- **Scope taps.** The scope has four taps: after the oscillators, after the filter, after the amp (the voices' mix times the
  output volume, before any effect) and after the effects (what reaches the host: effects, volume and the limiter, so the
  limited signal is what is drawn). The tap is not part of the patch.
- **What the processor resolves.** The engine only sees what is audible. The processor builds `EngineParams` once per block
  and, for effects, folds in bypass (a bypassed effect has `enabled = false`, so it fades out like one switched off) and tempo
  sync: with sync on, the delay time is `params::delaySyncMs (bpm, division)` in milliseconds, and an LFO's rate is
  `params::lfoSyncRateHz`. The tempo is the host's (`getBpm`) when it reports one, otherwise 120, read before the block's
  parameters so sync uses this block's tempo. The engine has no idea what a beat is.
- **Tests and the limiter.** The limiter touches anything above -0.9 dBFS, so engine tests that measure levels, ratios or
  waveform shape play at -6 dB or below (sums of notes and noise at -12 dB) and scale what they expect.

## Not yet (M6)

Aftertouch has no on-screen control; it arrives from MIDI channel pressure.

## Patch files and state migration (M6)

- **Format.** A patch file is exactly what `getStateInformation` hands the host, as text XML instead of the binary wrapper:
  the `ScratchpadSynth` root, one `PARAM id= value=` child per parameter, the non-parameter `effectOrder` property and the
  non-parameter `patchName` property, stamped `schemaVersion="6"`. Files are named `<name>.scratchpad`
  (`patch::fileExtension`) and live in `<userDocumentsDirectory>/Scratchpad Synth/Patches` (`getPatchesDirectory()`). The
  Save dialog creates that folder the first time; `savePatch` also creates the parent of whatever file it is given.
- **`savePatch (file)`** writes a copy of the state (with `patchName` set to the file's stem) to a temporary file beside the
  target and swaps it in, so an interrupted or failed save never leaves half a patch or damages the old file. Only after
  the write has worked does the live patch take the name. Taking the name is not an undo step: it says where the patch
  lives, not what is in it. Returns false, changing nothing, if the folder or file cannot be written.
- **`loadPatch (file)`** returns false, with the patch, its name and the undo history untouched, for a missing file or a
  folder, empty or malformed XML, another root tag, or a schema newer than ours. Otherwise it migrates the XML, then in
  **one** transaction named "Opened <name>" writes every parameter through the value tree (the file's value, or the
  default for any parameter the file does not mention, plus oscillator 1 forced present), then the effect order (a
  malformed one falls back to the default) and the name. Because every parameter and the two properties are in that
  transaction, bypass, solo, module presence and modulation slots (all parameters) cannot go stale, and one Undo returns the
  previous patch, name included; Redo reapplies the file. It does not go through `replaceState`: that would drop the undo
  history, and loading from the UI should be undoable like New patch. The name is the file's stem, not the one written
  inside, so a renamed file opens under its new name. Values outside a parameter's range are clamped by the parameter; ids
  that are not parameters are ignored.
- **Session recall is unchanged.** `setStateInformation` keeps `replaceState` semantics (no undo step, parameters reset
  first, oscillator 1 forced present). `patchName` rides along, so a recalled session shows its patch name; a session
  without one (schema 5 and older) is "Untitled patch" because `replaceState` removes properties the new state lacks.
- **Patch name.** `getPatchName()` is the `patchName` property or `patch::untitledName`. New patch removes it, as part of its
  undo step. `getPatchNameBroadcaster()` fires for any change (save, open, new patch, undo, recalled session), and the top
  bar's patch pill repaints from it.
- **Schema 6** only adds the optional `patchName` property, so `migrate` has no step for it beyond the restamp (the comment
  there says why); versions 0 to 5 stay loadable and open untitled.
- **UI.** Save and Open sit after New patch in the top bar. Each opens an asynchronous `juce::FileChooser` that starts in the
  patches folder, kept alive in `TopBar` until it finishes. Save appends `.scratchpad` when missing. Ctrl/Cmd+S and Ctrl/Cmd+O
  reach `TopBar::handleKeyPressed` through the editor's `keyPressed`. A failed save or open shows a message box saying what
  failed and that the patch was not saved or is unchanged.
- **Tests.** `tests/PatchFileTests.cpp` covers the rich-patch round trip, the file's XML, undo and redo of an open, every
  refusal leaving the patch untouched, defaults for what a file omits, the name's lifecycle and migration of schemas 0 to 5.
- **Reading the state never costs Redo.** The APVTS has an undo manager, so its flush of parameter values into the tree
  (on a 10-50 Hz timer, and inside `copyState()`, which `getStateInformation` calls) is recorded as an undoable action
  whenever the tree and the parameter disagree. Undo writes an old value back into the tree; the APVTS turns it into a
  normalised parameter value and back, and if that did not return the same float the flush rewrote the tree: a new action,
  which cleared Redo. A host reads the state at any time, so Redo could vanish at random. The cause was `logRange`
  (`Parameters.cpp`): its float `pow`/`log` did not invert each other (about 5e-7 relative off), which hit every
  logarithmic parameter (envelope times, filter cutoff, LFO and chorus rates, delay time, reverb decay). It now computes in
  double, which makes the round trip exact. `tests/UndoFlushTests.cpp` checks every parameter over many values
  (nothing may be rewritten after an undo-style write and a flush), plus undo, state read, redo across skewed, stepped and
  choice parameters. A new logarithmic range must keep that property.

## Window, theme and view settings (M6)

- **View preferences are not the patch.** `ui::ViewSettings` keeps four keys in a properties file (XML) of its own, apart from
  the patch, the host session, the undo history and the standalone app's own `.settings` file: `theme` (`dark` or `light`,
  default dark), `reducedMotion` (`true` or `false`, default false), `explainCollapsed` (`true` or `false`; **absent** means
  "follow the window size") and `editorScale` (0.75 to 2.0, default 1.0). The file is `Scratchpad Synth.view` in the same
  folder JUCE uses for the application's settings: `~/.Scratchpad Synth/` on Linux, `~/Library/Application Support/` on macOS,
  `%APPDATA%\Scratchpad Synth\` on Windows. It is read when an editor opens and written straight away on every change (after
  re-reading the file, so two editors in one process do not lose each other's changes). A missing, unreadable or half-written
  file reads as the defaults and is written over by the next change; a scale that is out of range is pulled into range, and one
  that is not a number reads as 1.0. `ViewSettings (File)` takes an explicit file; `setDefaultFileOverride` points the
  default constructor at one, which `tests/ViewTestSetup.cpp` does for the whole test run so no test reads or writes real
  preferences.
- **A fixed stage under a transform.** `ScratchpadEditor` lays everything out on a `Stage` child that is always 1500 x 900 and
  puts a scale-and-translate transform on it, so no control layout knows the window's size. The transform is on the child, not
  the editor, because `AudioProcessorEditor` keeps the host's own scale in the editor's transform. The window is resizable from
  1125 x 675 (75%) to 3000 x 1800 (200%) at a fixed 5:3 shape by a `ComponentBoundsConstrainer`: the standalone app's frame
  honours it through JUCE's decorator constrainer, a plugin gets a corner grip (`drawCornerResizer`, in palette colours) and
  the host can resize it too. A host that sets some other size gets the stage fitted (`min (w / 1500, h / 900)`) and centred
  with the background round it; nothing is clipped. The scale is written 400 ms after the last resize and again when the
  editor closes, and read back at the next start.
- **Under the transform.** Mouse events arrive in stage units, so drags are 1 px = 1 stage px at any scale. The modulation code
  that goes through screen coordinates (`localPointToGlobal`, `getLocalPoint (nullptr, ...)`, `e.getScreenPosition()`) is
  correct under a transform. Menus take their scale from `PopupMenu::Options::withTargetComponent`, so the two right-click
  menus that had no target (`ModOverlay::showRingMenu`, the connection row's) now name one and are placed at the pointer.
  `findChildWithID` does not look deeper than a window's direct children, so a module's `?` button now finds the panel with
  `ExplainPanel::pinFrom`. A scaled paint is not free: a full repaint of a full patch into an image took about 25 ms at 100%,
  27 ms at 75% and 160 ms at 200% (software renderer, this machine). Live updates repaint small regions (the scope is about
  250 x 262 stage px), so the cost follows the region, not the window.
- **The Explain panel's rail.** `ExplainPanel::setCollapsed` folds the panel to 56 px: the unfold button (`control.explainToggle`)
  and the name "EXPLAIN" running down the rail. Its scope is hidden, so its timer stops. The editor decides: below 99.5% scale
  the panel is folded unless the user has chosen; once the user folds or unfolds it (the fold button, the rail's button, a
  module's `?` which unfolds, or the View menu) that choice is stored and wins at every scale. The 224 px it frees go to the
  signal chain: its panel is that much wider, the row of stages stays centred, and the connections panel's two columns of
  rows now stretch with the panel instead of staying 320 px wide.
- **View menu.** A circle-half-filled button at the right of the top bar (`ViewMenuButton`, "View options",
  `control.viewMenu`) opens `buildViewMenu`: Theme > Dark / Light, Reduced motion, Explain panel, each ticked for what is on. The
  editor applies a choice (`applyViewChoice`) and stores it.
- **Themes.** `setTheme` points the editor's `ScratchpadLookAndFeel` at the other `Palette` and calls `sendLookAndFeelChange`.
  Anything that copies a palette colour into a component (knob arcs, `Scope`'s selected segments, the top bar's no-device
  chip) re-derives it in `lookAndFeelChanged`; everything else reads `paletteOf (*this)` at paint time. A test switches
  theme with every module present and fails if a knob keeps the other theme's colour. Two tokens were added to `Palette`:
  `accentInk` (the accent as a line or text on a pale surface: the suggested slot, AA-off outline, drop ring and markers) and
  `keyBlackInk` (the label on a black key, which was a literal). The light theme's family inks (`srcInk`, `fltInk`, `ampInk`,
  `fxInk`) were darkened a little (about 5 to 14%) to reach 4.5:1; the dark theme needed no change.
  `drawSelectedEdge` outlines a selected segment, "on" chip or solo button only when its fill is closer than 3:1 to what it sits
  on (the pale family fills on the light theme).
- **Contrast.** `ui/Contrast.h` has the WCAG relative-luminance and contrast-ratio functions. `tests/ContrastTests.cpp` checks
  the text pairs the UI draws (4.5:1) and the arcs, rings and markers (3:1) in both palettes. Decorative lines are not in it:
  the empty part of a knob's ring (`track`), the dashed outline of an empty slot (`dash`), shadows, and disabled controls (WCAG
  exempts disabled ones); each has a label or a value beside it, so none carries meaning alone. No text is below 11 px.
- **Reduced motion.** `ui/Motion.h` is the one place the policy lives. `motion::isReduced()` is a process-wide flag the editor
  sets from the preference (every plugin instance shares one preference, so one flag is right). Live pictures do not choose
  their own rate: the scope and `ModulationModel` (which feeds every knob arc, envelope playhead and LFO dot) are
  `motion::LiveTimer`s, which run at their normal rate or at 2 Hz and change rate by themselves when the preference changes;
  the level meter and the limiter's light ride the vertical blank, so they go through `motion::Throttle`. When reduced, a
  value is taken as it is: `motion::follow` and `motion::fall` replace easing and falling decay (the scope's zoom and spectrum
  bars, the meter), and the limiter's quarter-second hold goes (`LimiterHold`), so the light shows the latest reading. The modulation status
  line's six seconds are now wall-clock time, not timer ticks, so they last six seconds at either rate. Pictures that change only
  when a parameter does (filter curve, effect pictures, envelope shape) keep redrawing when it changes. There is nothing
  animated to switch off: panels, drawers and the theme change at once. The operating system's own reduced-motion
  hint is not read.
- **Tests.** `ViewSettingsTests` (defaults, round trip, the keys, corrupt files, silly values, two writers), `MotionTests` (the
  throttle, the meter and limiter steps, timer rate changes, the scope and model following the preference, without waiting on
  a clock), `ContrastTests`, and `EditorViewTests` (limits and shape, panel rules and persistence, layout, hit-testing at 75%,
  100%, 133% and 200%, host sizes, theme reaching every knob, a snapshot's corner pixels in each theme and size, the menu).
- **Limits.** Below the 1125 x 675 minimum a host's window still shows everything, fitted. The scope, envelopes and meters at
  200% are drawn by scaling 1500 x 900 up, so on a high-resolution display the text is rendered at the larger size but images
  (the soft shadows) are scaled bitmaps. The drag sensitivity of knobs is in stage pixels, so a knob needs twice the pointer
  travel at 200%.

## Performance and validation (M6)

**Target (the brief).** Eight voices with every module on and unison at 1 under 15% of one core at 48 kHz with 256-sample blocks;
unison 7 on all three oscillators under 50%.

**Benchmark.** `tools/Benchmark.cpp` (target `ScratchpadBenchmark`) with the scenario in `tools/BenchmarkRig.h`, which the
`[.perf]` tests share. It drives `ScratchpadProcessor::processBlock`, not the engine alone, so reading the parameters each block
is in the cost:

- Poly, 8 voices, eight held notes (G2 to D5) with velocity 0.8, every module added (oscillators 2 and 3, noise, LP 24 filter with
  drive, both envelopes, both LFOs, all four effects), oscillator 1 saw, 2 pulse, 3 triangle, unison detune 25 cents and spread 80%.
- Eight modulation connections, including per-voice destinations (filter envelope to pulse width 2, velocity to level 2, key to
  pan, amp envelope to level 3) and a retriggering LFO 1 (a phase per voice). Distortion at 18 dB, ping-pong delay.
- Rows: unison 1 with FM (35%, both oscillators), hard sync on 2 and 3 and ring modulation (the worst case for "every module on"),
  the same with those three off, unison 7 on all three with them off, and unison 7 with them on.
- 200 warm-up blocks, then N blocks timed one by one (`steady_clock`) and the thread's CPU time over the whole run. "% of a core" is
  processing time over audio time. Each row is repeated and the minimum and median of the per-run means are printed. The
  audio is checked for silence and non-finite samples, and a fingerprint (FNV-1a of the first 200 blocks) is printed per row:
  an optimisation that does not change the sound leaves it unchanged.
- Run it: `cmake --build build-release --target ScratchpadBenchmark && ./build-release/ScratchpadBenchmark [runs=7] [blocks=1500] [row 1-4]`
  (Release; an idle machine). The exit code is 1 if a row's median misses its target. `ScratchpadTests "[perf]"` asserts the two
  targets; it is hidden from the normal run. `ScratchpadTests "[perf-scenario]"` (in the normal run) checks that the scenario
  is audible, repeatable, and that unison, FM, sync and ring each change the sound, so the numbers describe what they say.

**Machine and noise.** Intel Core i7-13620H (laptop, 16 threads), Ubuntu 22.04 under WSL2, GCC 11.4 `-O3`, Release. The box was shared
with other builds and test runs while this was measured, and wall time varies 2x with the load (the same binary gave 10.5% and 20% on
one row), so the figures below are the minimum of many runs, taken when the load average was low; the medians sit within 5%
of them. Load only ever adds time, which is why the minimum is the estimate. CPU time and wall time agree to the second decimal,
because a single thread that is never descheduled spends all its wall time on the CPU; the load shows as slower code (shared cores,
caches), not as waiting.

| Row (8 voices, 256-sample blocks, 48 kHz) | Before | After | Target |
| --- | --- | --- | --- |
| Unison 1, FM + sync + ring on | 10.5% | 8.7% | < 15% |
| Unison 1, FM + sync + ring off | 7.2% | 5.7% | < 15% |
| Unison 7 on all three, FM + sync + ring off | 16.7% | 14.1% | < 50% |
| Unison 7 on all three, FM + sync + ring on | 31.0% | 28.3% | < 50% |

The targets are met with 1.7x headroom on this CPU. A mid-range desktop CPU was not available; a part with 30-40% less single-thread
speed would still be under 15% on the first row (about 12-14%) and under 50% on the last (about 40-45%). That is an extrapolation, not a
measurement. CI runs the benchmark on a GitHub runner for the record (it does not fail the build on it).

**Profile** (`perf record`, a `-g` build, symbols resolved by hand because the WSL `perf` could not read them): before optimising,
about 22% of the first row was `tanh` (`tanhf`, which spends its time in `expm1f`; two per voice per sample in the filter, eight
per sample in the distortion's 4x stage), about 4% was `std::floor` calls from the phase wrap, and the rest was spread evenly across the
per-sample oscillator code (`Oscillator::run` and the BLEP ring, `OscSection::tickBank`, the half-band decimators of the 2x FM
section), the voice's filter and ramps, the reverb and the JUCE oversampler. No single hot spot is left; what remains is the cost of
running 24 band-limited oscillators at 2x, one sample at a time.

**What changed, and why the sound did not.**

- `dsp::fastTanh` replaces `std::tanh` in the ladder's feedback, the filter's drive stage and the distortion. A rational function
  (no library call, no table, no branch), within 4e-7 of the true value (-128 dB at full scale; `FastMathTests` pins 5e-7, relative
  accuracy for small arguments, oddness and the limits). This is the one change that alters the samples, at that level, so the fingerprints
  moved; the other DSP and engine tests, with their original tolerances, pass. It no longer depends on each platform's libm.
  It is most of the saving.
- The distortion computed `exp` and `pow` for its gain on every sample; it now does so only while the drive is moving.
- `dsp::fastFloor` replaces `std::floor` in the per-sample phase wrap (oscillator, chorus, reverb's modulation). Same value
  for any phase (`FastMathTests` checks it against `std::floor` over random and edge values), without a libm call.
- The half-band decimator keeps each sample twice in its history so a read never wraps (no index masks, 32 floats instead of 64
  each); `place` in the BLEP code adds its 32 taps as two straight runs; `readCubic` reads four consecutive slots unless it is
  near the start of the buffer. All three are bit-exact (fingerprints equal before and after, tested in `FastMathTests`).

**Real-time audit.** `tests/RealTimeTests.cpp` counts heap allocations on the audio thread (`malloc`/`calloc`/`realloc` with glibc,
`operator new` elsewhere; a self-test shows the counter sees a SysEx message's storage) while the heaviest patch plays through
notes, controllers, pitch bend, aftertouch, UI events, block sizes from 1 to 2048 samples and SysEx. It found one allocation: a
host's SysEx message was copied into a `MidiMessage` (heap storage above 8 bytes) before being ignored. `processBlock` now
skips anything longer than three bytes. The test is not run under AddressSanitizer (the `malloc` hook is off then).

**Validation.**

- pluginval 1.0.4 at strictness 10, `--validate-in-process --skip-gui-tests`: passes (run twice with different random seeds on the
  Linux build). It also passes with the GUI tests on (`DISPLAY=:0`, the Editor and Editor Automation tests). pluginval's own
  "vst3 validator" step is skipped unless given `--vst3validator`; on Linux it passes the module inside the bundle rather than
  the bundle, which the Steinberg validator rejects, so the validator is run directly instead.
- Steinberg `validator` from the VST3 SDK v3.8.0_build_66 (MIT), built from source (`validator` target only): 47 tests, 0 failed.
  It found one real problem: the single program had an empty name ("Programlist 000->Program 000: has no name"). The
  program is now called "Current patch".
- Run both locally (Linux, from the repository root; Windows is the same with `.exe`):

  ```
  curl -L -o /tmp/pluginval.zip https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Linux.zip
  unzip /tmp/pluginval.zip -d /tmp/pv
  /tmp/pv/pluginval --strictness-level 10 --validate-in-process --skip-gui-tests \
      "build-release/ScratchpadSynth_artefacts/Release/VST3/Scratchpad Synth.vst3"

  git clone --depth 1 --branch v3.8.0_build_66 --recurse-submodules --shallow-submodules https://github.com/steinbergmedia/vst3sdk.git /tmp/vst3sdk
  cmake -S /tmp/vst3sdk -B /tmp/vst3sdk/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF \
      -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF -DSMTG_CREATE_PLUGIN_LINK=OFF
  cmake --build /tmp/vst3sdk/build --target validator
  /tmp/vst3sdk/build/bin/Release/validator "build-release/ScratchpadSynth_artefacts/Release/VST3/Scratchpad Synth.vst3"
  ```

- CI (`windows-latest`) runs pluginval at strictness 10, the Steinberg validator (built once from the pinned SDK tag and cached
  as `vst3validator/validator.exe`) and the benchmark (informational). The Windows job could not be run from here; the same
  commands pass on Linux.
- **Tail length.** `getTailLengthSeconds()` is `tailLengthSeconds (readParams())`: an upper bound, from the cached parameter
  atomics only (the same read the audio thread does, so any host thread may call it), on how long sound continues after the
  last note-off for the current patch. It is the amp envelope's release when the envelope is present and not bypassed (else
  nothing but the gate), plus, for each effect that is present and not bypassed, the reverb's decay time (the RT60) and its
  longest line (0.13 s), the delay's first echo and the repeats until the feedback has fallen 60 dB (`time x (1 + ceil
  (log 0.001 / log feedback))`, so 1 s at 50% is about 11 s), and 30 ms for the chorus; plus 20 ms of slack. Effects run in
  series, so their tails add. No single part (release, delay repeats, reverb decay) is reported above 20 s, so the bound stays
  finite at 95% feedback (it is then a cap, not a bound). `TailTests` pins each of these, and plays a patch to check that the
  reported tail is at least as long as what is heard (above -60 dB).

## Accessibility (M6)

The editor's accessibility tree follows the same signal-chain order as the visual layout. `ScratchpadEditor` keeps
the stage as a focus container, and `PatchView`, module cards, effects, modulation strip and performance bar name their
children explicitly. Decorative drawings use ignored or image regions with current text alternatives; they are never
keyboard stops. Every button, selector, knob, wheel and connection row has a meaningful title, an `explainId`, and a
visible focus ring. The light/dark palettes keep control text and focus rings at WCAG AA contrast, including the
reduced-motion palette path.

`KeyboardSlider` is the common value interface for parameter knobs and performance controls. It exposes the parameter
range and spoken value text, and supports arrows (Shift for fine steps), Page Up/Down, Home/End, Delete/Backspace to
reset and Enter to type a value. `ChoiceSegments` exposes a combo-box value and supports arrows, Home and End.
Toggle chips and header buttons expose their selected state through JUCE's toggle-button role.

Modulation handles are buttons: Enter or Space opens the destination chooser, Escape cancels, and a valid destination
can be activated from the keyboard. Occupied connection rows are amount sliders with a -100%..+100% range; arrows,
Page Up/Down, Home/Enter, Delete/Backspace and Shift+F10 adjust, reset, remove and expose the same actions as the
mouse. Empty rows remain descriptive static text so they do not create dead tab stops. Effect grips use the same
pattern for Left/Right (or Up/Down) reordering.

The computer-key keyboard has a separate interaction path. It only handles mapped keys when the editor or keyboard
has focus, and releases held notes when focus leaves the window, so arrows, typing and Delete remain available to the
focused control. `A11yTests` exercises handlers, ranges, values, selectors, keyboard-slider edits and modulation
connection edits; `ContrastTests`, `MotionTests` and `EditorViewTests` cover contrast, reduced motion and focus/layout
state. JUCE's accessibility tree is tested here; validation with a native screen reader is platform-specific and is
not part of the Linux test environment.
