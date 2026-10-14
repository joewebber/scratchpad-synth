# Scratchpad Synth

A polyphonic subtractive synth whose only job is to teach how synthesis works.
It ships as a standalone app and a VST3 plugin from one codebase, and it has no
presets: every patch starts with the user picking a single waveform and grows
only as they add modules.

This repository is at **M1 (Skeleton)**: wave picker, one oscillator through a
click-free gate, MIDI and computer-keyboard input, on-screen keyboard, tests and
CI. Later milestones (filter, envelopes, modulation, the learning layer, effects)
are described in the design and technical brief.

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

The Catch2 suite covers oscillator pitch and aliasing, the declick gate, voice
allocation and stealing order, engine behaviour (MIDI, pitch bend, sustain,
clicks), and state round-trip and migration.

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

In the standalone app, **Settings** opens the audio and MIDI device dialog. If no
audio device is open, the top bar says so.

## Layout

```
/CMakeLists.txt
/source/plugin    PluginProcessor, PluginEditor, PluginEntry, parameters, state and migration
/source/engine    SynthEngine, VoiceAllocator, Voice
/source/dsp       Oscillator, BlepTable, Gate
/source/ui        LookAndFeel, Theme, Typography, Neumorph (shadows), Knob, Meter, TopBar, WavePicker, PatchView, PerformanceBar
/assets/fonts     Jost (Light, Book, Medium), SIL OFL 1.1, embedded in the binary
/tests            Catch2 tests
/tools            ParameterList: prints the parameter table
/docs             Architecture notes and the generated parameter list
```

See [docs/architecture.md](docs/architecture.md) for how the pieces fit and the
decisions behind them, and [docs/parameters.md](docs/parameters.md) for the
parameter IDs, which are a public contract and must never be renamed.
