# Spectral Multiple — JUCE Six-Layer Transient Sampler

Spectral Multiple is a Windows 7-targeted JUCE VST3 transient and sample-layering plug-in. It is structured around three **PRE** sample layers and three **MAIN** sample layers, with a separate Master output.

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
- Visual Studio 2022 with the v143 x64 toolset.

The plug-in is configured for the Windows 7 (6.1) API target. The JUCE target defines `WINVER=0x0601`, `_WIN32_WINNT=0x0601`, and `NTDDI_VERSION=0x06010000`, and uses the static MSVC runtime (`/MT`) to avoid a separate Visual C++ runtime installation.

Windows x64 build:

```powershell
cmake -S juce-plugin -B juce-plugin/build -G "Visual Studio 17 2022" -A x64 -T v143
cmake --build juce-plugin/build --config Release --parallel
```

To use a local JUCE checkout:

```powershell
cmake -S juce-plugin -B juce-plugin/build -G "Visual Studio 17 2022" -A x64 -T v143 -DJUCE_PATH="C:/path/to/JUCE"
cmake --build juce-plugin/build --config Release --parallel
```

The VST3 output is created under:

```text
juce-plugin/build/WaveLens_artefacts/Release/VST3/WaveLens.vst3
```

A Windows x64 GitHub Actions build runs for pull requests and pushes to `main`.

## Development status and validation

This repository contains only the native JUCE plug-in project; the former React/Web Audio prototype and its web build tooling are not part of the project.

A successful CI build verifies compilation on the hosted Windows runner only. Actual loading in a Windows 7 system and DAW, confirming each optional output bus and its routing, and exercising sample playback and envelope behavior still require testing on that target system. Do not treat CI success alone as proof of Windows 7 runtime compatibility.
