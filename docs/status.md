# Status - Bench Line 0.1.0 (2026-11-01)
Tier P, track A (analysis; plugin kind effect, Bone, accent peacock #039BE5), at gate G4 (editor) with G3/G6 evidence in progress. Recipe D.8.1. Work paused by the owner 2026-11-01; continue from "Handoff".

## Waiting for Björn
- Host checks: none yet. Due when CI produces a test bundle (`BenchLine-test-macOS`, `BenchLine-test-Windows` artifacts of the Build workflow on this branch); the list to run is in "Handoff", step 5.
- Listening (reading the display on your own mixes): none yet. Due together with the host checks.
- `Source/oberli/` (bob-skills): not in plugin-template yet (checked 2026-11-01, plugin-template ec5ee50). Rubik arrives with the shared LookAndFeel; until then the editor uses JUCE's sans (reads as cheap, plugin-visual-craft 9).
- paramgen cannot express "60 s reads Song" (parameters.csv note): the editor shows "Song", the host's value string "60 s". Fix in the kit (bob-skills), then regenerate `Source/Params.h`.

## Gates
| Gate | State | Evidence (command -> number, test name, log) |
|---|---|---|
| G0 | closed 2026-11-01 | `docs/brief.md` owner-confirmed; Q1-Q10 resolved; four dated amendments |
| G1 | closed 2026-11-01 | owner: "build"; decisions below |
| G2 | closed 2026-11-01 | `python3 "sound design/model/g2_check.py"` -> `sound design/model/g2_numbers.json`: band vs full-rate bank <= 0.040 dB (6 rates); rate-to-rate <= 0.105 dB (multitone), 20 kHz band <= 0.042 dB; EBU 3341 short-term cases 1, 2, 9 <= 0.020 LU (6 rates); acceptance 2: Low-mid outside at 1.7 s, suggestion "250 Hz, -2.0 dB, Q 0.7" at 9.7 s; acceptance 3: 0.0 dB; gate shift 0.0 dB; histogram median vs weighted median 0.088 dB (criterion: one 0.1 dB bin); slope target reads slope noise inside |
| G3 | open | `ctest --test-dir build -L fast` -> 4/4 pass (suite_fast 13 tests, 39 s; harness_contract; parameter_table; preset_bank at S). `HARNESS=... python3 tools/tests/test_plugin.py --label render` -> 12/13 pass on the last full run, the 13th (Side) rewritten and passing alone; rerun in full. Editor: `BenchLineGuiSnapshot` renders at 1x/1.5x/2x; `snapshot_contrast.py --metrics --theme bone` 0 findings on editor@1x, editor@2x, empty@1x; text 5.6-8.2:1. Missing: CPU row (below), code checklist, renders committed |
| G4 | open | engine, worker, targets, processor (a wire), harness hooks, editor: Linux Release build clean (GCC). macOS/Windows not compiled yet (CI run 41351099528 cancelled) |
| G5 | closed 2026-11-01 | `python3 tools/measure/null_vs_model.py --rates=44100,48000,96000,192000` -> pass at every rate: per-frame max 2.7e-10 dB, gate mismatches 0, targets <= 0.005 dB (0.01 dB frame quantisation), verdicts and suggestion text equal |
| G6 | - | validators via CI (`validate_plugin.py --profile quick` in build.yml); full profile, hosts |
| G7 | - | |

Build speed (`test_build_speed_and_reload`): 6 x 60 s at 44.1 kHz in 0.95-1.20 s = 300-377x realtime on this Linux container (24 min in 20 s needs 72x); fresh and cached builds byte-identical.
Audio thread (`BenchLineHarness --seconds=12 --signal=noise:pink:-14 --target=slope --profile --block=64`, Linux container, not the reference slice): p99.9 2.1 %, max 4.5 % of the deadline at 48 k/64; worker 1.38 % of one core with a snapshot per 64-sample block (the plugin pumps every 15 ms). `perf.py run` timed out at 10 min here: run it on the Mac.
Malloc trap: live on Linux and macOS (`test_malloc_trap_is_live`).

## Listening debt
- The display on real mixes and real references: never seen by Björn. Due with the first host check.

## Open exceptions
- EX-1 2026-11-01: preset bank 4 (Init, Song, Section, Low end) instead of 8-12 at P; `presetlint.py bank presets.csv parameters.csv --tier P --track effect --categories Analyse` -> 2 errors (size, edge presets). Acceptable because the plugin is a wire: presets set the view and the averaging, not a sound (brief Q7). **Unacceptable if** a friend asks for a preset that sets a target (targets are files, not presets). Friends: none.
- EX-2 2026-11-01: no edge presets and no translation battery (rule 18): a wire has no output that could translate differently. Re-checked if Bench Line ever processes (it must not, decision 1). Friends: none.
- EX-3 2026-11-01: the editor uses stand-ins for the shared LookAndFeel, Strip frame, knob, meter and preset drawer (`Source/benchline/StandIns.md`) and JUCE's sans instead of Rubik. **Unacceptable** at release: closed by wiring `Source/oberli/` in. Friends: the type looks generic.

## Handoff
Task: Bench Line Tier P, from G3/G4 to the release gate. Plan:
1. [x] G0-G2, G5 (above)
2. [ ] CI on this branch green on macOS and Windows (compile fixes only; the run on 1e7b2e6 was cancelled)
3. [ ] G3: rerun the full render label; `perf.py run` on the Mac (x86_64 slice for Live 10.1 under Rosetta, and arm64); code checklist (plugin-ui-bob audio-plugins.md 15); commit the snapshot renders under `docs/renders/`
4. [ ] When `Source/oberli/` lands in plugin-template: refresh it, wire it in behind the stand-ins (StandIns.md), rerun G5 and the snapshots
5. [ ] Host checks for Björn (Live 10.1 under Rosetta, Live on Windows): load on the master; pass-through null (insert Utility with phase invert on a duplicate track); build a target from 6 songs and time it; Tone/Punch/Side; Hold, save and reopen the project (target and hold come back); automation of View and Average; editor at 100/150/200 %; CPU meter idle and playing; space bar reaches the transport
6. [ ] G6: `validate_plugin.py --profile full`, pluginval L10, state from this version loads (golden state test), corrupted state; G7: docs/parameters.md regenerated, README and manual (ship-bob templates), notices (JUCE formats: FLAC, Ogg Vorbis, MP3 decoder), release gate, then ship-bob
Working tree: clean after the handoff commit. Build: green on Linux (Release). Tests: fast 4/4; render 12/13 + Side alone.
Next command: `cmake --preset release && cmake --build --preset release && ctest --preset fast`

## Decisions
- 2026-11-01 G0: brief confirmed; Q1 Hold and Tolerance stay host parameters; Q2 400 ms frames gated on centred short-term at -40 LUFS (refined: frame loudness too, amendment); Q3 11 stored percentiles; Q4 axis -4.5 dB/oct density-equivalent; Q5 worker runs while signal is present; Q6 regions Sub 20-50, Low 63-160, Low-mid 200-500, Mid 630-1.6k, Presence 2-5k, Air 6.3-20k; Q7 bank of 4; Q8 slope + Mine at release 1; Q10 Bench Line defines the shared cache format.
- 2026-11-01 G1: band analysis by an IEC-style multirate third-octave bank (order-3 Butterworth band-pass, octave stages at >= 5 x the band centre, 8th-order decimation low-pass at 0.2 x rate), not the sketch's 4096 FFT (under one bin per band below 63 Hz; fixed in samples). Upper band edge clamped to 0.49 x rate with a bandwidth correction (only the 20 kHz band at 44.1 kHz). Mix curve = exponentially weighted median from a 0.1 dB histogram. Tone = (L^2+R^2)/2 per band, Side = ((L-R)/2)^2 normalised to the mid loudness. Display with a target: mix minus target median (flat = on target). Suggestions: 2/3 of the deviation, 0.5 dB steps, after 8 s, 30 s lockout.
- 2026-11-01 owner: shared code (`Source/oberli/`: LookAndFeel, Strip frame, meters, Loudness, Analysis bands, References, RenderSafety) comes from bob-skills via plugin-template; Bench Line keeps stand-ins behind small interfaces in `Source/benchline/` and never edits bob-skills. Gates pass on green evidence; stop only for listening, host checks, a sketch conflict, a release.
- 2026-11-01 owner: accent peacock #039BE5 (category accent, analysis); product text in American English (sketch).
- 2026-11-01 template fixes kept in this repo: the harness's work buffer sized for oversized blocks, the integer sample-rate echo, `JucePlugin_Name`/`VersionString` for the harness, the Linux malloc trap (failures.md). Report them to bob-skills for the template.
