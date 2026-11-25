# SNAPBACK flat near-black UI — ported to the modular editor architecture

Branch: `feature/snapback-ui` (single commit `71ce030` on top of `main` @ `e102b5d`)
Base for PR: `main` ← `feature/snapback-ui` (CI runs the Windows VST3 build + both test suites on this branch name)

## Summary

The uploaded snapback-ui design (palette, tiles, trapezoid trigger, blue/violet
banks) ported onto the **current modular component architecture** — the old
monolithic patch was used purely as a visual specification. All components are
kept: `PluginHeaderComponent`, `MasterWaveformComponent`, `WaveformControlBar`,
`SampleGroupComponent`, `SampleSelectorDot`, `SampleBrowserComponent`,
`LayerEnvelopeComponent`, `FilterControlComponent`, `SpectralMultipleLookAndFeel`.
**No parameter IDs, no engine, no state-format, no routing changes**
(`PluginProcessor.cpp`, `LayeredSampleEngine.cpp`, `StateMigration.h` untouched;
+728 / −180 across 17 files).

## How the design was ported

| Area | Change |
| --- | --- |
| Palette | New `SnapbackTheme.h` = single source of truth; `wavelens::ui` now aliases it (no duplicate colours anywhere) |
| Header | White flat `S N A P B A C K` wordmark, new RESET button (view state only), compact BPM −/120/+ cluster; power/menu/preset/favourite retained |
| Scope | Flat near-black bg, subtle blue-grey grid, SNAPBACK-blue trigger line; **Master M starts inactive** (selected pair on open; all-six overlay on demand) |
| Control bar | Blue **trapezoid TRIGGER tab** (`snap::trapezoidPath` helper), split GRID/SECONDS buttons (blue/violet active), zoom −/level/+, bar-span combo moved into the trigger drawer (SPAN) — no lost functionality |
| Groups | Bank-accent headings, 3 blue / 3 violet selector dots, M-styled enable button; selection, mute/solo/audition/load/clear behaviour untouched |
| Layer editor | **11 parameter tiles** in two rows (REV + GAIN, PITCH, POGO, TRIM F, TRIM B, FADE IN, FADE OUT / TIMING, PAN, HP, LP); vertical volume fader → GAIN tile (same `layerGain` param); HP/LP tiles replace the dual-handle filter bar visually, wired to existing `layerHighPass`/`layerLowPass` params; neutral grey-blue waveform under a bright bank-accent FL envelope |
| LookAndFeel | Tile rendering added to `SpectralMultipleLookAndFeel` (label, value, 2 px track, bipolar centre fill, thumb, width-aware font shrink) + trapezoid background + flat buttons/combos/popups |

## Build fixes included (current `main` does not compile with pinned JUCE 7.0.12)

- `SampleSelectorDot` / `FilterControlComponent` now inherit
  `juce::SettableTooltipClient` (plain `Component` has no `setTooltip` in JUCE 7).
- `gainSlider.textFromValueFunction` set **after** the `SliderAttachment` —
  `SliderParameterAttachment` overwrites it with the parameter's `getText`.

## New tool

`Tools/UiSnapshot.cpp` + `BUILD_UI_SNAPSHOT=ON`: headless renderer that loads
synthetic samples through the real processor and saves PNGs of the actual
editor at 1280×760 / 1020×620 / 1920×1200 — used for the verification below.

## Verification

- Linux VST3 build: clean, 0 errors, no new warnings (remaining `-Wswitch-enum`
  warnings pre-exist on `main` in untouched switches).
- CTest: `SpectralMultipleEngineTests` **passed**, `WaveLensProcessorTests` **passed**.
- Screenshot verification: 14/14 objective pixel checks (palette, trapezoid
  geometry, bank colour clusters, neutral waveform, tiles at all three sizes)
  + vision-model inspection of the actual renderings (wordmark, trapezoid,
  all 11 tile labels/values legible, no cut-off text).

## Preserved by design

- All six samples keep independent state: selection only swaps editor
  visibility and rebinds the header toggles — no parameter is ever reset.
- Global Master M (scope overlay) is fully separate from per-sample Mute/Solo.
- APVTS parameter IDs, serialized state format, engine behaviour: unchanged.
