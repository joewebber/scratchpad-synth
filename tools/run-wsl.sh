#!/usr/bin/env bash
# Runs the Linux standalone under WSLg with sound. JUCE only speaks ALSA, so
# this routes ALSA's default device to WSLg's PulseAudio (see README).
set -e
L="$HOME/.local/alsa-pulse"
export LD_LIBRARY_PATH="$L:$L/pulseaudio${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
root="$(cd "$(dirname "$0")/.." && pwd)"
exec "$root/${BUILD_DIR:-build-release}/ScratchpadSynth_artefacts/Release/Standalone/Scratchpad Synth" "$@"
