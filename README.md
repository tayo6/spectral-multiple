# WaveLens

A zero-latency oscilloscope plug-in (VST3) built with **JUCE 7.0.12**. It is an original
implementation of the oscilloscope idea (waveform, multi-signal comparison,
freeze/hold, metering), not a copy of any commercial plug-in.

## Features

| Area | What it does |
|---|---|
| **Capture** | *Tempo*: bar-based window locked to the host clock (1/16 ... 4 bars); each pass restarts on playback or a seek. *Live*: free-running 1 ms - 5 s window, ignores the transport. *Scroll*: history flows across the display. |
| **Input** | Peak (louder channel, polarity kept), L, R, M, S. |
| **Gain** | -24 ... +24 dB display gain. Never changes the output or the meter. |
| **Range / Listen** | Limit the visible frequency range (low cut / high cut). *Listen* auditions only that range through the output. With Listen off the audio is passed through untouched. |
| **Signals (up to 6)** | Signal 1 is the main input. Signals 2-6 are five extra input buses ("Signal 2" ... "Signal 6") that you route to in the DAW. Every routed signal is drawn in its own colour, overlaid on one shared axis (*Overlay*) or in one lane each (*Split*). Six numbered buttons show/hide each signal; a button is only enabled once an input is routed to it. All signals share the same input mode (Peak/L/R/M/S), range and gain. |
| **Freeze / Hold / Reset** | Freeze stops the graph without touching audio. Hold keeps the previous pass as a dim trace. Reset clears the capture, the held pass and the meter holds. |
| **Meter** | Click to cycle Peak / RMS / Crest. A white tick marks the retained maximum. The meter and Listen always follow signal 1. |

No latency is reported or added. The only case where the output changes is *Listen*.

## Routing the extra signals

The extra signals are ordinary sidechain-style input buses, so how you feed them depends on the host. The notes below are general guidance and have **not** been tested in each DAW:

* **Reaper**: widen the track to 4/6/8/10/12 channels and use the plug-in pin connector; pins 3-4 feed Signal 2, 5-6 feed Signal 3, and so on. Use sends with "send to channels 3/4" etc. from the other tracks.
* **Cubase / Nuendo, Studio One, Bitwig**: enable the extra input buses in the plug-in's sidechain/input menu and pick a source for each.
* **Hosts that expose only one sidechain** (for example Ableton Live) can feed Signal 1 and Signal 2 only.

Hosts differ a lot in how many extra plug-in inputs they expose, so check your DAW's documentation. If your DAW offers only a single sidechain, you can still compare two signals (Signal 1 + Signal 2).

## Building (Windows)

Requirements: Visual Studio (C++ desktop workload), CMake 3.22+, Git.

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

(Use `-G "Visual Studio 16 2019"` for VS2019; add `-A Win32` for a 32-bit build.)

CMake downloads JUCE 7.0.12 automatically. To use a local copy instead:
`cmake -B build -DJUCE_PATH=C:/path/to/JUCE ...`

The result is `build/WaveLens_artefacts/Release/VST3/WaveLens.vst3`. Copy that folder to
`C:\Program Files\Common Files\VST3\` and rescan plug-ins in your DAW.

A GitHub Actions workflow (`.github/workflows/build-windows.yml`) builds the same thing on a
Windows runner and uploads the `.vst3` as an artifact, so you don't need Visual Studio locally.

## Windows 7 notes

* JUCE 7.0.12 lists Windows Vista as its deployment target, and the project links the MSVC
  runtime **statically** (`/MT`), so the plug-in does not need the VC++ redistributable or the
  Universal CRT update on the target machine.
* Newer Visual Studio releases are moving their C++ runtime toward newer Windows versions.
  For a machine that is really running Windows 7, build with **Visual Studio 2019** (or older)
  and test on that machine. This project has not been run on Windows 7 by its author.
* Windows 7 itself must be SP1, and the DAW must be able to load 64-bit (or 32-bit, if you built
  with `-A Win32`) VST3 plug-ins.
* Minimum screen size 1280 x 800 is recommended; the window is resizable.

## Licensing

JUCE is dual-licensed (AGPLv3 / commercial). Distributing a binary built with it means either
releasing the plug-in source under AGPLv3 or holding a JUCE commercial licence.

## Layout

```
CMakeLists.txt
Source/PluginProcessor.{h,cpp}   audio engine: capture columns, tempo sync, filters, meter
Source/PluginEditor.{h,cpp}      scope display, meter, controls
```

The audio thread writes min/max pairs into 1024 display columns per signal (full band, low, mid,
high, sidechain); the UI thread only reads them, so there are no locks or allocations in
`processBlock`.
