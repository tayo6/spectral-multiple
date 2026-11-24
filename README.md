# Spectral Multiple — JUCE Six-Layer Transient Sampler

Spectral Multiple is a JUCE-based VST3 transient and sample-layering plug-in. It is structured around three **PRE** sample layers and three **MAIN** sample layers, with a separate Master output.

## Current plug-in features

- Six independent sample slots: PRE 1–3 and MAIN 1–3.
- Four overlapping voices per layer, with round-robin voice replacement.
- Audio transient, MIDI Note-On, and manual GUI triggering.
- A global PRE lead control, individual per-layer timing offsets, and a fixed look-ahead delay for dry-signal alignment.
- Optional independent stereo output buses for each layer, plus the Master stereo output.
- Per-layer gain, pan, pitch, Pogo pitch bend, high-pass/low-pass filtering, mute, solo, reverse, trim, fade-in, and fade-out.
- Waveform-backed AHDSR editing with eight curve modes (Hold, Smooth, Single curve, Half sine, Stairs, Smooth stairs, Pulse, and Wave) and per-segment curve tension.
- A six-layer waveform overlap view and live waveform / FFT monitoring.
- Sample-path and parameter state saved with the plug-in.

## Build

Requirements:

- CMake 3.22 or newer.
- A C++17 compiler.
- JUCE 7.0.12 (fetched automatically by CMake, or supplied through `JUCE_PATH`).

Windows x64 with Visual Studio 2022:

```powershell
cmake -S juce-plugin -B juce-plugin/build -G "Visual Studio 17 2022" -A x64
cmake --build juce-plugin/build --config Release --parallel
```

To use a local JUCE checkout:

```powershell
cmake -S juce-plugin -B juce-plugin/build -G "Visual Studio 17 2022" -A x64 -DJUCE_PATH="C:/path/to/JUCE"
cmake --build juce-plugin/build --config Release --parallel
```

The VST3 output is created under:

```text
juce-plugin/build/WaveLens_artefacts/Release/VST3/WaveLens.vst3
```

A Windows x64 GitHub Actions build runs for pull requests and pushes to `main`.

## Development status and validation

This repository is focused on the JUCE plug-in source. The former React/Web Audio prototype has been removed to avoid maintaining two separate implementations.

A successful CI build verifies compilation on the hosted Windows runner only. Loading the VST3 in a DAW, confirming each optional output bus and its routing, exercising sample playback and envelope behavior, and verifying compatibility with Windows 7 are separate validation tasks and must not be assumed from CI success.