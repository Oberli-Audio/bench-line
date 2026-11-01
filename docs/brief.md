# Sonic brief (owner-confirmed 2026-11-01; amend below, never edit the YAML)

Source: `docs/sketch.md` (decided Tier S sketch), promoted to Tier P. The three structural
decisions are copied unchanged. Conflicts and gaps in the sketch were raised as Q1-Q10 and
resolved by the owner on 2026-11-01 (all recommendations accepted); `resolved` records them.

```yaml
name: Bench Line
track: analysis          # A leads (dsp-analysis.md); the plugin kind is "effect" (Bone theme, VST3 effect)
stages: [analysis]       # the audio path is a wire: no processing stage, no mix stage
type: meter              # tonal-balance analyser against a statistical target
musical_role: effect     # sits last on the master or a bus; never changes the sound
tier: P
plugin_code: Bnln
bundle_id: com.oberliaudio.benchline
formats: [VST3]          # ship.toml
theme: bone
accent: "lime #A8D45A"

character_line: >
  A bench line for the mix: shows in ten seconds where the tonal balance sits against the
  statistics of your own references, and says it in words. Never an EQ, never a "match"
  button, never touches the audio.
one_sentence: >
  Compares your mix's tonal balance, low-end punch and side-channel spectrum to a target
  built from your own reference songs, and suggests one EQ move per problem.
init_patch: >
  View Tone, Average 10 s, Hold off, Tolerance 60 % (20th-80th percentile), target "Mine"
  if the library has one, else none ("Choose a target or build one from your references").
  Ten-second test: on the reference stem the curve is drawn and sits mostly inside the band.
  The audio is a bit-exact wire, so the G0 artefact is the harness null on the reference stem
  plus the editor snapshot at G3, not an audio render.

users: >
  Björn mastering the worship EP and the live album and checking mixes on the master bus
  while mixing; friends who get the build.
core_jobs:
  - Know in ten seconds whether the mix is too heavy, thin, dull or harsh against a target that fits the genre.
  - Build that target from my own references, not someone else's genre curve.
  - Judge low-end punch and side-channel balance separately, because a spectrum hides both.
  - Get one plain suggestion per problem, worded as an EQ Eight move, without the plugin touching the audio.
context: >
  Last device on the master, or on a bus. Window opened every few minutes, glanced at,
  closed. Often on a laptop beside the DAW.
success:
  - A +3 dB bump at 250 Hz shows Low-mid outside the band within 10 s; the suggestion names 250 Hz and about -2 dB.
  - Six reference files build a target in under 20 s.
  - The plugin is a bit-exact wire.
  - Silence or a stopped transport costs nothing beyond the copy.

references:
  - "iZotope Tonal Balance Control 2: target bands and Crest view; paid, targets from iZotope's genre curves. Bench Line is free and builds targets from the user's own references."
  - "Metric AB / ADPTR, REFERENCE (Mastering The Mix): match views against one reference; paid. Bench Line compares against the statistics of many references."
  - "SPAN, FreqAnalyst, Ableton Spectrum: spectrum only, no target, no tolerance, no recall."
  - "A B see (Oberli Audio): A/B against one reference section; shares the reference folder and the per-file analysis cache."

structural_decisions:     # fixed (sketch, section 1); a conflict is raised, never resolved here
  1_wire: >
    The audio path is a wire, always. Bench Line never processes; suggestions are words,
    not an EQ.
  2_targets_are_statistics: >
    A target is a median curve plus a tolerance band (20th to 80th percentile at the default)
    across many short-term frames of many songs, stored as 31 third-octave values per
    percentile. Built-in targets ship as numbers only (no audio); user targets are built the
    same way from dropped files.
  3_like_with_like: >
    Mix and target are measured after K-weighted loudness matching and on a -4.5 dB/oct
    compensated axis; frames below -40 LUFS are ignored so silence and fades do not pull
    the curve.

must_do:
  - Bit-exact pass-through in every state, rate and block size.
  - 31 third-octave bands, 20 Hz-20 kHz, ISO centres; mix curve as an exponential average over gated frames.
  - Targets from files on a background thread; percentile tables saved as small JSON in Oberli Audio/References/, shareable.
  - Tolerance re-derives the band from stored percentiles instantly, no re-analysis.
  - Views Tone (spectrum), Punch (crest per region), Side (L-R spectrum normalised to mid loudness).
  - Six region marks (inside / outside the band) and at most two suggestion lines.
  - LUFS short-term meter in the Output section, EBU Tech 3341 compliant at every rate (rule 17).
  - Hold freezes the mix curve; the readout states since when.

defining_features:        # each gets a presence test (rule 13)
  - "Wire: output bit-identical to input in every View, Hold, Tolerance, bypass and editor state, at all six rates and the test block sizes."
  - "Detection: pink noise shaped to a target's median reads inside the band in all six regions; with +3 dB at 250 Hz (Q 1) Low-mid reads outside within 10 s at Average 10 s."
  - "Suggestion: for that case names 250 Hz +/- 1/3 octave and -1.5 to -2.5 dB (undershoot on purpose: half to two-thirds of the deviation)."
  - "Loudness independence: the same mix at -8 and -16 LUFS gives the same curve within 0.2 dB per band."
  - "Punch: a signal with more compression than the target's reads 'squashed', a less compressed one 'spiky', in the region where the difference is."
  - "Side: bass added to the side channel below 120 Hz reads outside the band in Sub/Low in Side view only."
  - "Tolerance: changing it moves the band without re-analysis (no worker job started)."

regions:                  # six regions on third-octave band edges, fixed at P
  Sub: "20-50 Hz (5 bands)"
  Low: "63-160 Hz (5)"
  Low-mid: "200-500 Hz (5)"
  Mid: "630 Hz-1.6 kHz (5)"
  Presence: "2-5 kHz (5)"
  Air: "6.3-20 kHz (6)"

out_of_scope:
  - Any audio processing: no EQ, no match, no auto-gain.
  - Per-instrument targets (vocal, kick).
  - A cloud genre library; targets are files the user owns.
  - Editable region edges at P.

core_controls:            # front panel; host parameters per parameters.csv (Q1)
  - "Target: list of built-in and own targets (state, not a host parameter)."
  - "View: Tone | Punch | Side (host parameter)."
  - "Average: 1-60 s, centre 10 s, 60 s reads 'Song' (host parameter)."
  - "Hold: freezes the mix curve (host parameter, Q1)."
  - "Build from files: drop or choose references; progress 'Analysing 3 of 6' (state)."
  # plus Tolerance (50-90 %, non-core, host parameter) and Bypass (host bypass)

signal_flow: |
  in L/R ──────────────────────────────────────────────────────────► out L/R   (wire, bit-exact)
     │
     └─► SPSC FIFO (lock-free copy, audio thread) ─► worker thread:
            K-weighting per rate ─► 100 ms sub-block energies ─► S-LUFS meter, -40 LUFS gate
            mid / side ─► third-octave analysis (31 bands) ─► per-frame loudness normalisation
            ─► mix curve (exp. average, Average s; Hold freezes)  ─┐
            region crest factors (50 ms windows) ─► Punch          ├─► compare with target
     target JSON (31 bands x percentiles x Tone/Side, 6 regions   ─┘     percentiles ─► display,
       x percentiles for Punch) from Oberli Audio/References/            region marks, suggestions
     Build from files: decode ─► 400 ms frames ─► gate ─► normalise ─► per-band percentiles ─► JSON

cpu_budget:               # rule 20, per instance, audio thread
  p999_pct: 25
  max_pct: 50
  at: 48k/64, slice x86_64 (Live 10.1 under Rosetta), Init
  audio_thread_expected: "copy only, well under 0.1 % of an M1 core"
  editor_closed_total: "0.1 % on an M1 at 48 kHz / 128, copy plus worker while signal is above the gate (Q5)"
  editor_open_total: "under 1 % (sketch target 0.8 %) on an M1 at 48 kHz / 128, worker included"
  idle: "silence below the gate or transport stopped: worker sleeps"
cpu_reference_machine: Björn's M1 MacBook (Live 10.1 under Rosetta), and the Windows machine for Live on Windows
instances_expected: "1 on the master, up to 4 on buses"
latency_budget: 0 samples, reported 0 and verified by impulse
sample_rates: [44100, 48000, 88200, 96000, 176400, 192000]
channels: "stereo; on mono input the Side view says: Mono signal, no side to compare"
register: n/a (analysis)
input_material: >
  Full mixes and masters of worship pop / rock, live recordings, buses; references are the
  songs Björn wants to sound like (WAV, AIFF, FLAC, MP3 via JUCE's formats).

presets:                  #
  bank_size: "4: Init, Song (Average 60 s), Section (Average 3 s), Low end (Punch)"
  categories: [Analyse]
  loudness_window: "n/a; a wire, every preset nulls against the input; the presetlint loudness check is a logged exception"
  edge_presets: n/a
  naming: English, plain

mix_context: n/a (analysis; never changes the audio)

quality_targets:          # every target with the method that works here
  wire_null:          { target: "bit-identical (max |out-in| = 0) in every state, rate and block size", method: "harness render vs input, test_plugin.py fast" }
  band_accuracy_db:   { target: "+/-0.3 dB per band vs the numpy reference on pink noise and sines at band centres, all six rates", method: "harness band dump vs G2 numpy model (G5)" }
  detection:          { target: "acceptance test 2 (Low-mid outside within 10 s; suggestion 250 Hz +/- 1/3 oct, -1.5 to -2.5 dB)", method: "shaped pink noise through the harness, simulated time" }
  loudness_independence_db: { target: "<= 0.2 dB per band between -8 and -16 LUFS", method: "same file at two gains through the harness" }
  short_term_lufs:    { target: "EBU Tech 3341 short-term cases within +/-0.1 LU at all six rates", method: "3341 test signals (synthesised with dspkit) through the harness" }
  gate:               { target: "silence and fades below -40 LUFS do not move the curve (< 0.1 dB after 10 s of -60 LUFS noise)", method: "harness" }
  build_speed_s:      { target: "six 4-minute songs in < 20 s on an M1; file reloads bit-identical", method: "harness --build timing here (Linux, stated as such) and Björn's M1" }
  tolerance_instant:  { target: "Tolerance change redraws without re-analysis", method: "harness echo: no worker job counted" }
  cpu:                { target: "rule 20 on the audio thread; worker < 1 % of one core editor open", method: "perf.py per-block distribution; worker time per second of audio from the harness" }
  editor:             { target: "plugin-ui-bob P evidence: snapshots 100/150/200 %, snapshot_contrast --theme bone, code checklist", method: "snapshot tool" }

resolved:                 # owner answers 2026-11-01: all recommendations accepted
  - "Q1 (conflict, sketch vs parameters.csv): the brief says 'only View and Average are host parameters', but the table and parameters.csv make Hold and Tolerance host parameters too (Bypass is the host's bypass either way). Resolved: keep parameters.csv (Hold automatable is useful: freeze at the chorus), and read the brief sentence as 'only View and Average are core host parameters'."
  - "Q2 (conflict inside decision 3): the mix gate is '-40 LUFS short-term' (3 s), the target build gates 400 ms frames at -40 LUFS without saying which loudness. Like with like needs one rule. Resolved: both sides use 400 ms analysis frames, each frame gated on the short-term (3 s) loudness centred on it, at -40 LUFS, and normalised by its own momentary loudness."
  - "Q3 (gap in decision 2 / Tolerance range): stored percentiles 10/20/30/50/70/80/90 give band widths 40, 60 and 80 %, but Tolerance runs 50-90 %; 50 % needs the 25th/75th and 90 % the 5th/95th. Resolved: store 5/10/15/20/25/50/75/80/85/90/95 (11 values) and interpolate between them; the file stays small (31 x 11 x 2 views + 6 x 11 Punch)."
  - "Q4 (definition in decision 3): -4.5 dB/oct is the usual slope on per-Hz power density; summed into third-octave bands that is -1.5 dB/oct. Resolved: define the axis on band power as density-equivalent -4.5 dB/oct, so it reads like SPAN's 4.5 dB tilt. It only changes the drawn axis, never the in/out decision (that compares mix and target directly)."
  - "Q5 (conflict, sketch 4.6 vs success): 'FFT only while the editor is open' and 'the mix curve is current when the window opens' cannot both hold; and success says idle 0 % while the budget says 0.1 % closed. Resolved: the worker runs whenever the signal is above the gate, editor open or closed (that is what makes Hold, suggestions' 'since 0:42' and the curve current), sleeps in silence or with the transport stopped; the 0.1 % closed budget then covers copy plus worker, and is measured at G3."
  - "Q6 (gap): the six regions have no edges in the sketch. Resolved: the table above, fixed at P (edges as state can come later behind a state version)."
  - "Q7 (P bank size): the P effect bank is 8-12 loudness-matched presets; for a wire analyser that means nothing. Resolved: 4 presets as above and a dated exception for bank size and loudness matching."
  - "Q8 (open in the sketch): built-in targets. Five genre targets need your reference material and their provenance (songs, years, licence). I cannot produce them here. Resolved: release 1 ships the plain slope plus 'Mine'; genre targets are added when you hand me the reference sets (or the files' analysis JSON computed on your machine with the harness)."
  - "Q9 (G1 note, not structural): a 4096-point FFT at 48 kHz has 11.7 Hz bins, wider than the 20 Hz third-octave band (4.6 Hz) and a fixed size in samples breaks rule 12 at 96 kHz. I will choose the band analysis at G1 against that (window in seconds, a longer window or a third-octave filter bank for the low bands); the sketch's 4096 FFT is approach, not decision, so this is flagged, not changed."
  - "Q10 (A B see): Bench Line is built first, so it defines the shared References folder layout and the per-file analysis cache format that A B see will read. Resolved: yes, versioned JSON, documented in docs/."
open_questions: []
```

## Amendments
- (none yet)
