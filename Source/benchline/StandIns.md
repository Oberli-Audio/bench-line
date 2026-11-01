# Source/benchline: stand-ins for the shared Oberli code

The family's shared code (template `Source/oberli/`: LookAndFeel, Strip frame, meters, Loudness
BS.1770, Analysis/FFT bands, References, RenderSafety) is being built in Oberli-Audio/bob-skills.
Until it lands, Bench Line keeps the few pieces it needs behind small interfaces here; nothing
else in Bench Line reaches past them. When `Source/oberli/` arrives (refreshed from
plugin-template, never edited here), each stand-in is replaced by wiring the shared module in
behind the same interface, and the G5 null test (`tools/measure/null_vs_model.py`) decides
whether the result still matches the model.

| Stand-in (this folder) | Interface | Replaced by (Source/oberli/) | What must stay true |
|---|---|---|---|
| `Loudness.h` | `KWeighting` (prepare, reset, process), `lufsOf()` | Loudness BS.1770 | K-weighting redesigned per rate; EBU 3341 short-term within 0.1 LU at six rates |
| `BandBank.h/.cpp` | `BandBank` (prepare, reset, process M/S per sample, idle, sub-block powers) | Analysis bands | 31 base-10 third-octave bands; band power within 0.3 dB of the full-rate bank at every rate, including 20-63 Hz (G1 decision 1: a 4096 FFT has under one bin there) |
| `ReferencesFolder.h` | `referencesFolder()` | References | the shared folder of A B see and Bench Line; the targets and cache layout below it stay Bench Line's |
| harness malloc trap on Linux (`tools/harness/Main.cpp`) | - | RenderSafety | the trap aborts on allocation inside processBlock |
| editor drawing (`Source/PluginEditor.*`) | - | LookAndFeel, Strip frame, meters | brand sheet tokens |

Bench Line's own parts (never shared): `Engine` (framing, gate, normalisation, crest, the
tracker, targets as percentile tables), `TargetLibrary` (target files, the per-file cache, the
build), `Analyser` (FIFO, worker, snapshot).
