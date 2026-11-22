# WaveLens — Six-Layer Transient Sampler

WaveLens is a JUCE 7.0.12 VST3 audio effect that combines transient-triggered sample layering with a multi-layer waveform and spectrum display. The design direction is inspired by Ableton's compact dark device workflow, with amber PRE layers and cyan MAIN layers.

## Six sample layers

| Slots | Role | Playback start |
|---|---|---|
| PRE 1–3 | Three independent pre-sample layers | Immediately when a trigger is detected |
| MAIN 1–3 | Three independent main-sample layers | 50 ms after the trigger |

Load a separate audio file into each slot. All six layers can play together. Each slot has its own gain, pan, pitch, attack, decay, sustain, release and mute parameters. Gain is available directly on the device panel; the full parameter set is exposed for DAW automation.

### Triggering and timing

- **Audio**: detects an input level crossing the adjustable threshold.
- **MIDI**: any MIDI note-on triggers all loaded layers.
- **Both**: accepts audio and MIDI triggers.
- A 25 ms retrigger guard reduces repeated triggering from one transient.
- The main input is delayed by 50 ms and that latency is reported to the host. PRE layers start immediately; MAIN layers start at the delayed transient position.

The 50 ms pre-roll is currently fixed in this first engine milestone. This is intentional: the reported plug-in latency, delayed dry path and MAIN-layer trigger stay aligned.

## Individual DAW outputs

The plug-in exposes:

| VST3 output bus | Signal |
|---|---|
| Master | Delayed dry input + all sample layers |
| Pre 1 | PRE sample 1 only |
| Pre 2 | PRE sample 2 only |
| Pre 3 | PRE sample 3 only |
| Main 1 | MAIN sample 1 only |
| Main 2 | MAIN sample 2 only |
| Main 3 | MAIN sample 3 only |

The six layer outputs are optional stereo buses. Enable the buses in the DAW's plug-in routing/pin configuration, then route each one to its own mixer track. The exact bus-activation workflow depends on the host.

## Waveform and spectrum display

The display captures the six rendered sample layers, rather than six external sidechain inputs. It supports:

- Waveform and FFT spectrum views.
- Overlay and split layouts.
- Independent visibility toggles for all six layers.
- Tempo-synchronised, Live and Scroll capture modes.
- Freeze, Hold, Reset, display gain and frequency-range controls.

PRE layers use amber/orange traces. MAIN layers use cyan/blue traces.

## Current implementation notes

This is an active development branch, not a finished commercial plug-in.

- Sample formats are provided by JUCE's registered basic audio formats (WAV, AIFF and supported FLAC/Ogg formats).
- Samples are loaded into memory and sample paths are saved with the plug-in state.
- Sample playback currently uses one voice per layer; a new trigger restarts that layer's current voice.
- The envelope is currently a linear attack/decay/sustain shape with a linear tail fade. Selectable curve shapes and draggable envelope handles are a later GUI/DSP milestone.
- MIDI note-on currently triggers all loaded layers; per-layer MIDI note mapping is not implemented yet.
- Replaced sample buffers are retained until the plug-in instance closes to avoid freeing memory while the audio thread might still be using it. Repeatedly replacing large samples can increase memory use; safe deferred reclamation is a follow-up task.
- Hosts differ in their support for multiple optional VST3 output buses. Test the six separate stem routes in the target DAW.

## Build on Windows

Requirements: CMake 3.22+, Git, Visual Studio C++ desktop tools.

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

For a local Visual Studio 2019 installation, use:

```powershell
cmake -B build -G "Visual Studio 16 2019" -A x64
cmake --build build --config Release --parallel
```

CMake downloads JUCE 7.0.12 automatically. To use a local JUCE checkout, pass `-DJUCE_PATH=C:/path/to/JUCE`.

The VST3 is written to `build/WaveLens_artefacts/Release/VST3/WaveLens.vst3`. The GitHub Actions workflow builds a Windows x64 VST3 and uploads it as an artifact.

## Windows 7

The project statically links the MSVC runtime and targets JUCE 7.0.12. For a Windows 7 target, use a compatible Visual Studio 2019 toolchain and test the resulting VST3 in the actual Windows 7 DAW. A GitHub-hosted build is not itself proof of Windows 7 compatibility.

## Source layout

```
Source/
  LayeredSampleEngine.h/.cpp   six sample voices, audio/MIDI trigger, pre-roll and stem rendering
  PluginProcessor.h/.cpp       JUCE buses, parameters, host latency, sample state and spectrum capture
  PluginEditor.h/.cpp          Ableton-inspired interface, six layer slots and waveform/spectrum UI
```

JUCE is dual-licensed (AGPLv3 / commercial). Check the applicable JUCE licence before distributing a compiled plug-in.
