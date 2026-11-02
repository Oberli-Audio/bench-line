# Status - Bench Line 0.1.0 (2026-11-03)
Tier P, track A (analysis; plugin kind effect, Bone, accent peacock #039BE5), at gate G4 (editor) with G3/G6 evidence in progress. Recipe D.8.1. On the Mac since 2026-11-02; continue from "Handoff".

## Waiting for Björn
- Host checks: none yet. Due when CI produces a test bundle (`BenchLine-test-macOS`, `BenchLine-test-Windows` artifacts of the Build workflow on this branch); the list to run is in "Handoff", step 5.
- Listening (reading the display on your own mixes): none yet. Due together with the host checks.
- For bob-skills (template), found while wiring `Source/oberli/` in (2026-11-02): `refresh_oberli.py` overwrites a plugin's own `tools/gui_snapshot/Main.cpp` (Bench Line's has `--target`, `--in`, `--state=hold`) and found no `--non-realtime` anchor in Bench Line's harness; `PresetLibrary` counts the host's bypass in the edited mark and a recall un-bypasses (Bench Line works around it in its capture/apply); the template's `State.h` has the arm64 rounding fix (`savedValue`, `normalisedFor`) that Bench Line's older copy lacks (no failure seen yet: the state round trip passes on arm64).
- paramgen cannot express "60 s reads Song" (parameters.csv note): the editor shows "Song", the host's value string "60 s". Fix in the kit (bob-skills), then regenerate `Source/Params.h`.

## Gates
| Gate | State | Evidence (command -> number, test name, log) |
|---|---|---|
| G0 | closed 2026-11-01 | `docs/brief.md` owner-confirmed; Q1-Q10 resolved; four dated amendments |
| G1 | closed 2026-11-01 | owner: "build"; decisions below |
| G2 | closed 2026-11-01 | `python3 "sound design/model/g2_check.py"` -> `sound design/model/g2_numbers.json`: band vs full-rate bank <= 0.040 dB (6 rates); rate-to-rate <= 0.105 dB (multitone), 20 kHz band <= 0.042 dB; EBU 3341 short-term cases 1, 2, 9 <= 0.020 LU (6 rates); acceptance 2: Low-mid outside at 1.7 s, suggestion "250 Hz, -2.0 dB, Q 0.7" at 9.7 s; acceptance 3: 0.0 dB; gate shift 0.0 dB; histogram median vs weighted median 0.088 dB (criterion: one 0.1 dB bin); slope target reads slope noise inside |
| G3 | open | `ctest --preset fast` (configured with `-DPython3_EXECUTABLE=/usr/bin/python3`) -> 9/9 pass on the Mac arm64, 21 s (suite_fast, harness_contract, parameter_table, bounce_null --wire, preset_bank at S, oberli_build_tests, oberli_loudness: EBU 3341/3342 at 44.1-192 kHz, oberli_shared, oberli_copy). `ctest --test-dir build -L render` -> suite_render pass, 13/13 in 82 s on the Mac (Goldens 1, DefiningFeatures 10 with Side, Standards 2): the full rerun the handoff asked for. Editor on the shared Strip frame: `BenchLineGuiSnapshot` renders in `docs/renders/` (editor at 1x/1.5x/2x, empty, hold, punch, side, drawer, menu, about, drag-average); `snapshot_contrast.py --metrics --theme bone` (About: charcoal) -> 0 metrics outside on all 11; text `--text #5C574F` (ink-subtle: caption, drop hint, "since") 5.87:1, `#45423E` (ink-muted) 7.1-8.2:1. Code checklist below. Missing: CPU row (`perf.py run` on the Mac, both slices) |
| G4 | open | engine, worker, targets, processor (a wire), harness hooks: Linux Release clean (GCC). **macOS 2026-11-02**: `cmake --preset release && cmake --build --preset release` (universal arm64 + x86_64, Apple clang) clean, no warnings in product code (the harness keeps two from the template: -Wswitch-enum, deprecated createWriterFor). Shared code wired in 2026-11-02 (`Source/oberli/` from plugin-template 0e7d4f4; what replaced what: `Source/benchline/StandIns.md`). Windows not compiled yet (CI) |
| G5 | closed 2026-11-01, rerun 2026-11-02 | `HARNESS=$PWD/build/BenchLineHarness_artefacts/Release/BenchLineHarness python3 tools/measure/null_vs_model.py --rates=44100,48000,96000,192000` (absolute path: the script runs the harness from a temp folder) -> pass at every rate, on Linux and, with the shared code wired in, on the Mac arm64: per-frame max 2.7e-10 dB, gate mismatches 0, targets <= 0.005 dB (0.01 dB frame quantisation), verdicts and suggestion text equal |
| G6 | - | validators via CI (`validate_plugin.py --profile quick` in build.yml); full profile, hosts |
| G7 | - | |

Build speed (`test_build_speed_and_reload`): 6 x 60 s at 44.1 kHz in 0.95-1.20 s = 300-377x realtime on this Linux container (24 min in 20 s needs 72x); fresh and cached builds byte-identical.
Audio thread (`BenchLineHarness --seconds=12 --signal=noise:pink:-14 --target=slope --profile --block=64`, Linux container, not the reference slice): p99.9 2.1 %, max 4.5 % of the deadline at 48 k/64; worker 1.38 % of one core with a snapshot per 64-sample block (the plugin pumps every 15 ms). `perf.py run` timed out at 10 min here: run it on the Mac.
Malloc trap: live on Linux and macOS (`test_malloc_trap_is_live`).

Code checklist (plugin-ui-bob audio-plugins.md 15, 2026-11-02, Schwere 1-4):
- keyboard focus: the editor never wants focus; the shared Knob, buttons and segments never take it on a click - ok
- accessible names: knobs (`bindParameter`), View (`SegmentedControl::attach`), Hold (`setTitle`) - ok; the target list's rows are painted, with no accessibility handler - Schwere 2, open
- double-click reset from the parameter default, drag sensitivity, Shift fine mode, host context menu with its null check: the shared `Knob` - ok
- scale choice machine-local (`MachineSettings`), the modified mark by comparison (`PresetLibrary`, bypass left out) - ok
- units in `stringFromValue` and its inverse: generated `Params.h`; "60 s reads Song" only in the editor (Waiting) - Schwere 1
- no `LookAndFeel_V4` remnants in Bench Line's code (`BenchLineLook` removed) - ok
- About's diagnostics: the frame's fourteen fields plus target and references folder - ok
- a window: naming a new target opens a `juce::AlertWindow` (brand: overlays never open a window) - Schwere 2, open; the "target does not load" message now uses the frame's toast
- the target row's "Show in folder" is a plain `juce::PopupMenu`, not the frame's menu - Schwere 1, open
- drag readout of Average covers the OUTPUT title while dragging - Schwere 1, open

## Listening debt
- The display on real mixes and real references: never seen by Björn. Due with the first host check.

## Open exceptions
- EX-1 2026-11-01: preset bank 4 (Init, Song, Section, Low end) instead of 8-12 at P; `presetlint.py bank presets.csv parameters.csv --tier P --track effect --categories Analyse` -> 2 errors (size, edge presets). Acceptable because the plugin is a wire: presets set the view and the averaging, not a sound (brief Q7). **Unacceptable if** a friend asks for a preset that sets a target (targets are files, not presets). Friends: none.
- EX-2 2026-11-01: no edge presets and no translation battery (rule 18): a wire has no output that could translate differently. Re-checked if Bench Line ever processes (it must not, decision 1). Friends: none.

## Handoff
Task: Bench Line Tier P, from G3/G4 to the release gate. Plan:
1. [x] G0-G2, G5 (above)
2. [ ] CI on this branch green on macOS and Windows (compile fixes only; the run on 1e7b2e6 was cancelled)
3. [ ] G3: [x] full render label on the Mac; [ ] `perf.py run` on the Mac (x86_64 slice for Live 10.1 under Rosetta, and arm64); [x] code checklist (above; three Schwere 2 and 1 findings open); [x] renders under `docs/renders/`
4. [x] `Source/oberli/` refreshed (0e7d4f4) and wired in (StandIns.md); G5 and the snapshots rerun on the Mac. EX-3 closed
5. [ ] Host checks for Björn (Live 10.1 under Rosetta, Live on Windows): load on the master; pass-through null (insert Utility with phase invert on a duplicate track); build a target from 6 songs and time it; Tone/Punch/Side; Hold, save and reopen the project (target and hold come back); automation of View and Average; editor at 100/150/200 %; CPU meter idle and playing; space bar reaches the transport
6. [ ] G6: `validate_plugin.py --profile full`, pluginval L10, state from this version loads (golden state test), corrupted state; G7: docs/parameters.md regenerated, README and manual (ship-bob templates), notices (JUCE formats: FLAC, Ogg Vorbis, MP3 decoder), release gate, then ship-bob
Machine note (2026-11-02): CMake finds Homebrew's python3.12 without numpy/scipy and then registers no tests ("No tests were found"); configure with `-DPython3_EXECUTABLE=/usr/bin/python3` (CLT 3.9, numpy 2.0.2, scipy 1.13.1).
Working tree: clean after the checkpoint commit. Build: green on Linux and macOS (Release). Tests: fast 9/9 on the Mac; render 13/13 on the Mac.
Next command: `python3 scripts/dsp-kit/perf.py run --harness $PWD/build/BenchLineHarness_artefacts/Release/BenchLineHarness --arch x86_64` (then without `--arch`), then push and watch CI (step 2)

## Decisions
- 2026-11-01 G0: brief confirmed; Q1 Hold and Tolerance stay host parameters; Q2 400 ms frames gated on centred short-term at -40 LUFS (refined: frame loudness too, amendment); Q3 11 stored percentiles; Q4 axis -4.5 dB/oct density-equivalent; Q5 worker runs while signal is present; Q6 regions Sub 20-50, Low 63-160, Low-mid 200-500, Mid 630-1.6k, Presence 2-5k, Air 6.3-20k; Q7 bank of 4; Q8 slope + Mine at release 1; Q10 Bench Line defines the shared cache format.
- 2026-11-01 G1: band analysis by an IEC-style multirate third-octave bank (order-3 Butterworth band-pass, octave stages at >= 5 x the band centre, 8th-order decimation low-pass at 0.2 x rate), not the sketch's 4096 FFT (under one bin per band below 63 Hz; fixed in samples). Upper band edge clamped to 0.49 x rate with a bandwidth correction (only the 20 kHz band at 44.1 kHz). Mix curve = exponentially weighted median from a 0.1 dB histogram. Tone = (L^2+R^2)/2 per band, Side = ((L-R)/2)^2 normalised to the mid loudness. Display with a target: mix minus target median (flat = on target). Suggestions: 2/3 of the deviation, 0.5 dB steps, after 8 s, 30 s lockout.
- 2026-11-01 owner: shared code (`Source/oberli/`: LookAndFeel, Strip frame, meters, Loudness, Analysis bands, References, RenderSafety) comes from bob-skills via plugin-template; Bench Line keeps stand-ins behind small interfaces in `Source/benchline/` and never edits bob-skills. Gates pass on green evidence; stop only for listening, host checks, a sketch conflict, a release.
- 2026-11-01 owner: accent peacock #039BE5 (category accent, analysis); product text in American English (sketch).
- 2026-11-01 template fixes kept in this repo: the harness's work buffer sized for oversized blocks, the integer sample-rate echo, `JucePlugin_Name`/`VersionString` for the harness, the Linux malloc trap (failures.md). Report them to bob-skills for the template.
- 2026-11-02 shared code wired in: Strip frame, LookAndFeel (Rubik, Nunito), Knob, SegmentedControl, PillToggle, ActionButton, PresetLibrary, the shared references folder (~/Music/Oberli Audio/References; no release yet, so no migration from ~/Documents), RenderSafety (offline: the analysis is not fed; the audio path is the same). Kept Bench Line's own: the K-weighting and the band bank (StandIns.md: the shared Loudness and Spectrum do other jobs), the LUFS S meter (the shared LevelMeter is sample peak).
- 2026-11-02 presets never carry or change the host's bypass: the capture leaves `bypass` out and a recall keeps the current value (PluginProcessor.cpp); the old stand-in skipped it in its edited mark too.
