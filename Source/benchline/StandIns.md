# Source/benchline: Bench Line's own code, and what became of the stand-ins

The family's shared code (`Source/oberli/`, refreshed from plugin-template with
`tools/refresh_oberli.py`, never edited here) landed on 2026-11-02 (plugin-template 0e7d4f4).
Each stand-in Bench Line kept until then was either replaced by the shared module behind the
same interface, or stays Bench Line's because the shared module does a different job:

| Was a stand-in | Now | Why |
|---|---|---|
| editor drawing (`Source/PluginEditor.*`): header, preset pill, menu, About, GUI scale, knob | the Strip frame (`oberli::ui::StripEditor`), `Knob`, `SegmentedControl`, `PillToggle`, `ActionButton`, the shared LookAndFeel (Rubik, Nunito) | replaced; the body's parts (`ui/BalanceView.*`) and the LUFS S meter stay Bench Line's (the shared `LevelMeter` is sample peak) |
| `ui/Look.*` (tokens, type, `BenchLineLook`) | Bench Line's names for the shared palette (`oberli::ui::front()`) and type scale | the palette values were already the brand sheet's; the LookAndFeel is the frame's |
| `FactoryPresets.h` (the bank for the pill) | `oberli::PresetLibrary` over `ProductData` (presets.csv and presets/) | replaced; Bench Line's capture leaves out the host's bypass and its recall keeps it (PluginProcessor.cpp) |
| `ReferencesFolder.h` | `oberli::folders::referencesFolder()` (~/Music/Oberli Audio/References), `BENCHLINE_REFERENCES` still overrides it | replaced; the targets and the analysis cache below it stay Bench Line's (brief Q10) |
| harness malloc trap on Linux | kept; `oberli::render::modeFor` in processBlock (offline: the analysis is not fed) and the bounce null test `bounce_null --wire` | RenderSafety adds the bounce rule; the trap stays the harness's |
| `Loudness.h` (`KWeighting`, `lufsOf`) | **stays Bench Line's** | `oberli::Loudness` keeps its K-weighting private and measures windows; the engine needs a per-sample K-weighting for its 400 ms frames and its centred gate (G0 Q2). Same BS.1770 design (both pass EBU 3341 at six rates) |
| `BandBank.h` (multirate third-octave bank) | **stays Bench Line's** | `oberli::Spectrum` is a Hann FFT on base-2 bands: G1 decision 1 rejected an FFT (under one bin per band below 63 Hz) |

Bench Line's own parts (never shared): `Engine` (framing, gate, normalisation, crest, the
tracker, targets as percentile tables), `TargetLibrary` (target files, the per-file cache, the
build), `Analyser` (FIFO, worker, snapshot), `BandBank`, `Loudness.h` (K-weighting), `Filters.h`,
`ui/BalanceView.*` (the display, region marks, suggestions, target list).
