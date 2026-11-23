# WaveLens — Six-Layer Transient Sampler

WaveLens is a JUCE 7.0.12 VST3 audio effect for layering three PRE samples with three MAIN samples. Its dark, compact interface takes visual inspiration from Ableton-style devices, with amber PRE layers and cyan MAIN layers.

## Six independent sample layers

| Slots | Role | Default timing shift |
|---|---|---:|
| PRE 1–3 | Pre-transient / Snapback layers | -50 ms |
| MAIN 1–3 | Main transient layers | 0 ms |

Each slot has its own sample loader, audition button, waveform, gain, timing shift, envelope, pan, pitch, high-pass, low-pass and mute control. The six layers can be played together and are exposed as individual stereo output buses.

The sample waveform is calculated from the actual loaded audio file and displayed inside the same panel as its editable envelope. Use the per-layer **>** button to audition one sample independently of the transient detector. The host must be processing audio for the preview request to be rendered.

## Per-layer timing

Every layer has an independent timing-shift parameter from **-100 ms to +100 ms**.

- Negative values advance that sample before the delayed dry transient.
- 0 ms aligns the sample with the delayed dry transient.
- Positive values place that sample after the delayed dry transient.

The plug-in reports a fixed 100 ms lookahead latency to the host. This gives the full timing-shift range without changing reported plug-in latency as the controls are adjusted.

## AHDSR envelope over the sample waveform

Each layer has draggable **Attack, Hold, Decay, Sustain and Release** stages, drawn directly over its sample waveform.

Attack, Decay and Release each have seven independent curve choices:

- Linear
- Convex
- Concave
- S-Curve
- Exponential
- Logarithmic
- Bezier

Hold maintains the peak level after Attack. Sustain controls the held level after Decay; it is a level/plateau, rather than a separate curved time segment. Envelope times and curves are available as DAW automation parameters.

For one-shot playback, Release is fitted to the final portion of the sample so the envelope can fade the audio before the file ends. On short samples, the release is limited to at most half the file duration so it does not erase the attack.

## Triggering

The former global Time, Trigger and Range panels have been removed from the interface. Triggering continues internally:

- Audio level threshold crossing.
- MIDI note-on.
- Both sources enabled by default.

The default threshold is 3%, and a 25 ms retrigger guard reduces repeated triggers from a single transient. These trigger settings remain parameters internally but are not shown in the compact interface.

## Individual DAW outputs

| VST3 output bus | Signal |
|---|---|
| Master | Delayed dry input + all sample layers |
| Pre 1 | PRE sample 1 only |
| Pre 2 | PRE sample 2 only |
| Pre 3 | PRE sample 3 only |
| Main 1 | MAIN sample 1 only |
| Main 2 | MAIN sample 2 only |
| Main 3 | MAIN sample 3 only |

The six layer outputs are optional stereo buses. Enable them in the DAW's plug-in routing/pin configuration, then route each one to its own mixer track. Host support and the exact bus-activation workflow vary, so the six stem routes still need testing in the target DAW.

## Master display

The upper scope remains available for the combined layer waveform / spectrum monitoring. It supports:

- Waveform and FFT spectrum views.
- Overlay and split layouts.
- Independent visibility toggles for the six sample layers.
- Freeze, Hold and Reset.

## Current implementation notes

This is an active development branch, not a finished commercial plug-in.

- Sample formats are provided by JUCE's registered basic audio formats (WAV, AIFF and supported FLAC/Ogg formats).
- Samples are loaded into memory and sample paths are saved with plug-in state.
- Each layer supports up to four simultaneous playback voices. New hits overlap existing tails; when all four voices are active, round-robin voice stealing keeps playback allocation-free.
- The per-slot waveform is a precomputed peak overview of the loaded source file. The upper scope monitors rendered layer output.
- Layer controls and envelope parameters are exposed for DAW automation.
- MIDI note-on currently triggers all loaded layers; per-layer MIDI note mapping is not implemented yet.
- Replaced sample buffers are retained until the plug-in instance closes to avoid freeing memory while the audio thread might still be using them. Repeatedly replacing large samples can increase memory use; safe deferred reclamation is a follow-up.
- Sample playback, per-layer preview and the optional output-bus routing require validation inside an actual DAW host.
- The GitHub Windows x64 CI build does not prove Windows 7 compatibility. Windows 7 must be tested with a compatible Visual Studio 2019 toolchain and a DAW on the target system.

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

## Source layout

```
Source/
  LayeredSampleEngine.h/.cpp     six sample voices, timing shifts, AHDSR and waveform previews
  LayerEnvelopeComponent.h/.cpp  waveform display with draggable AHDSR overlay and curve selectors
  PluginProcessor.h/.cpp         JUCE buses, parameters, host latency and sample state
  PluginEditor.h/.cpp            compact six-layer interface and master scope
```

JUCE is dual-licensed (AGPLv3 / commercial). Check the applicable JUCE licence before distributing a compiled plug-in.
