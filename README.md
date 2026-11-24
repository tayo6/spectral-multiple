# WaveLens — Six-Layer Transient Sampler (Web Edition)

WaveLens is a Web Audio API & React application ported from the JUCE six-layer transient sampler plug-in, designed for layering three PRE samples with three MAIN samples in a dark, compact Ableton-inspired rack interface.

## Features

- **Six Independent Sample Layers (`PRE 1–3` & `MAIN 1–3`)**:
  - Load audio files (`WAV`, `AIFF`, `FLAC`, `OGG`, `MP3`) via file picker or drag-and-drop onto any rack slot.
  - Default timing shifts: `-50 ms` for `PRE 1–3` (amber) and `0 ms` for `MAIN 1–3` (cyan), adjustable from `-100 ms` to `+100 ms` around a fixed `100 ms` lookahead.
  - Per-layer Gain (`0%–200%`), Pan, Pitch (`-24` to `+24` semitones), 4th-order Linkwitz-Riley HPF (`20 Hz–1 kHz`) and LPF (`1 kHz–20 kHz`), Mute, and independent Audition (`>`).
  - 4-voice polyphony per layer with round-robin voice stealing in a real-time `AudioWorkletProcessor`.
- **Draggable AHDSR Envelope Over Waveform**:
  - Interactive `A`, `H`, `D`, `S`, and `R` handles drawn directly over each layer's peak waveform overview.
  - Seven selectable curve shapes for Attack, Decay, and Release: `Linear`, `Convex`, `Concave`, `S-Curve`, `Exponential`, `Logarithmic`, and `Bezier`.
- **Master Overlay View (`M`)**:
  - Overlays all six loaded source waveforms in their amber/cyan trace colours.
- **Live Monitor & FFT Spectrum (`MONITOR / FFT`)**:
  - Real-time `Waveform` and `4096`-point Hann-windowed `Spectrum` (`20 Hz–20 kHz`) views with `Overlay` and `Split` layouts.
  - Per-layer trace visibility toggles (`P1–P3`, `M1–M3`), `Freeze`, `Hold`, and `Reset` controls.
  - Interactive `PEAK` / `RMS` / `CREST` meter (click to cycle mode).
- **Triggering**:
  - Manual **Trigger** button / `Spacebar` shortcut, Web MIDI Note-On input, and optional **Audio In** microphone threshold triggering (`3%` threshold, `25 ms` retrigger guard).

## Development

```bash
npm install
npm run dev
```

The development server runs on `http://0.0.0.0:3000`.
