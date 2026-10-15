# Scratchpad Synth

A polyphonic subtractive synth whose only job is to teach how synthesis works.
It ships as a standalone app and a VST3 plugin from one codebase, and it has no
presets: every patch starts with the user picking a single waveform and grows
only as they add modules.

This repository is at **M3 (Modulation and real UI)**: the wave picker and one oscillator
from M1, two more oscillators, noise, a multimode filter (LP 24, LP 12, HP 12, BP 12), amp
and filter envelopes and poly, mono and legato voicing with glide from M2, and now two LFOs
and eight modulation connections. Each module is added from the signal-chain view (a dashed
slot, or "+ Add module") and removed again with its x. Later milestones (the learning layer,
oscillator extras, effects) are described in the design and technical brief.

## Build

Requirements: CMake 3.25 or newer, a C++20 compiler and Ninja. On Windows use MSVC 2022
from a "x64 Native Tools" prompt. JUCE 8.0.15 and Catch2 are fetched by CMake.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Outputs, under `build/ScratchpadSynth_artefacts/Release/`:

| Target | Path |
| --- | --- |
| Standalone app | `Standalone/Scratchpad Synth(.exe)` |
| VST3 plugin | `VST3/Scratchpad Synth.vst3` |

Copy the `.vst3` bundle to `C:\Program Files\Common Files\VST3` to use it in a host.

On Linux the code builds and the tests run, but only Windows is a supported target
for v1. JUCE needs `libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev
libxrandr-dev libxinerama-dev libxcursor-dev libgl-dev` there.

### Sound under WSL

JUCE on Linux only speaks ALSA, and WSL has no sound card, so ALSA's default device has to be routed to
WSLg's PulseAudio. Without root: download `libasound2-plugins`, `libpulse0`, `libsndfile1`, `libasyncns0`,
`libflac8`, `libvorbis0a`, `libvorbisenc2`, `libopus0` and `libogg0` with `apt-get download`, `dpkg -x` them,
copy the resulting `usr/lib/x86_64-linux-gnu/*` into `~/.local/alsa-pulse`, and point `~/.asoundrc` at the
two `alsa-lib/libasound_module_{pcm,ctl}_pulse.so` files (`pcm.!default { type pulse }`, same for `ctl`).
Then start the app with `tools/run-wsl.sh`, which sets `LD_LIBRARY_PATH` for those libraries. With root,
`sudo apt install libasound2-plugins` and a two-line `~/.asoundrc` is enough. The "no `/dev/snd/seq`"
message is harmless: WSL has no MIDI.

### Tests

```
ctest --test-dir build --output-on-failure
```

The Catch2 suite covers oscillator pitch and aliasing, the declick gate, noise,
envelope timing, filter response and self-oscillation, voice allocation and stealing
order, voicing modes and glide, the per-voice chain, a click-free bar for note on/off,
stealing, mode and module changes, LFO shapes and rates, every modulation destination
and source, and state round-trip and migration, including the connection slots.

### Plugin validation

```
pluginval --strictness-level 5 --validate-in-process --skip-gui-tests "build/ScratchpadSynth_artefacts/Release/VST3/Scratchpad Synth.vst3"
```

CI runs this on every push.

## Using it

1. Hover or focus a wave in the picker to hear it held, then choose one.
2. Play with the on-screen keyboard, a MIDI keyboard, or the computer keyboard:
   `A` to `K` are the white keys, `W E T Y U` the black keys, `Z` and `X` shift the
   octave, `C` and `V` change velocity.
3. Change the wave or the level, or press **New patch** to go back to the picker.
   Every change is one named undo step.
4. Add an LFO, then drag its round handle (**L1**) onto a knob, or click the handle and then
   a knob. The knob grows an arc: how far the LFO can move it, with a dot where it is now.
   Drag the arc up or down to change the amount, right-click it to remove the connection.
   Dropping on a **Fine** knob asks whether to move that oscillator or all of them (vibrato).
   The strip at the bottom lists all eight slots; drag a row sideways to change its amount.

In the standalone app, **Settings** opens the audio and MIDI device dialog. If no
audio device is open, the top bar says so.

## Layout

```
/CMakeLists.txt
/source/plugin    PluginProcessor, PluginEditor, PluginEntry, parameters, state and migration
/source/engine    SynthEngine, VoiceAllocator, Voice, EngineParams, BlockLevels, Modulation
/source/dsp       Oscillator, BlepTable, Gate, Noise, Envelope, Lfo, SvfFilter, LadderFilter, MultimodeFilter
/source/ui        LookAndFeel, Theme, Typography, Neumorph (shadows), Knob, ModDial, Meter, TopBar, WavePicker, PatchView,
                  ModuleCards, ModulationStrip (LFO cards, handles, slots), ModulationModel, ModArcs, ModOverlay, PerformanceBar
/assets/fonts     Jost (Light, Book, Medium), SIL OFL 1.1, embedded in the binary
/tests            Catch2 tests
/tools            ParameterList: prints the parameter table
/docs             Architecture notes and the generated parameter list
```

See [docs/architecture.md](docs/architecture.md) for how the pieces fit and the
decisions behind them, and [docs/parameters.md](docs/parameters.md) for the
parameter IDs, which are a public contract and must never be renamed.
