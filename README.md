# WaveLens — Six-Layer Transient Sampler

WaveLens is a JUCE 7.0.12 VST3 audio effect that combines transient-triggered sample layering with a multi-layer waveform and spectrum display. The design direction is inspired by Ableton's compact dark device workflow, with amber PRE layers and cyan MAIN layers.

## Six sample layers

| Slots | Role | Playback start |
|---|---|---|
| PRE 1–3 | Three independent pre-sample layers | Scheduled before the aligned transient by the selected PRE lead |
| MAIN 1–3 | Three independent main-sample layers | At the delayed transient position |

Load a separate audio file into each slot. All six layers can play together. Every layer has independent gain, pan, pitch, high-pass, low-pass, attack, decay, sustain, release, curve selection and mute controls. Pan, pitch, filters and mute are available on the layer panel; the envelope is editable directly in its graph. All values are also exposed for DAW automation.

### Triggering and timing

- **Audio**: detects an input level crossing the adjustable threshold.
- **MIDI**: any MIDI note-on triggers all loaded layers.
- **Both**: accepts audio and MIDI triggers.
- A 25 ms retrigger guard reduces repeated triggering from one transient.
- PRE lead is adjustable from 0 to 100 ms.
- The plug-in reports a fixed 100 ms lookahead latency to the host. The dry input and MAIN sample layers are delayed by 100 ms; PRE layers are placed earlier by the selected PRE lead. This keeps host delay compensation stable when the PRE lead knob changes.
- At a PRE lead of 0 ms, PRE and MAIN layers start together. At 50 ms, PRE samples start 50 ms before the aligned main transient. At 100 ms, PRE samples start at the transient-detection point.

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
- Every layer has an editable A/D/S/R envelope graph. Drag A and D horizontally to change their time values, S vertically to set sustain, and R horizontally to change the tail release duration.
- Each layer also has its own Pan, Pitch, HPF, LPF and Mute controls. The 4th-order Linkwitz-Riley high-pass and low-pass filters are applied before the envelope and output routing.
- Attack, decay and release each have five curve choices: Linear, Exponential, Logarithmic, S-Curve and Bezier. Curve selections and time/level values are exposed as DAW automation parameters.
- The envelope is applied to the rendered sample audio before the layer stem and combined master are written. The waveform/spectrum capture therefore shows the shaped output, not an unprocessed preview.
- For one-shot samples, release is fitted into the final part of the file. On very short samples it is limited to at most half the sample duration so the release does not suppress the attack.
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
  LayeredSampleEngine.h/.cpp     six sample voices, curved envelopes, trigger and stem rendering
  LayerEnvelopeComponent.h/.cpp  draggable A/D/S/R graphs and curve selectors for each layer
  PluginProcessor.h/.cpp         JUCE buses, parameters, host latency, sample state and spectrum capture
  PluginEditor.h/.cpp            Ableton-inspired interface, six layer slots and waveform/spectrum UI
```

JUCE is dual-licensed (AGPLv3 / commercial). Check the applicable JUCE licence before distributing a compiled plug-in.
