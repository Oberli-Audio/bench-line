# Bench Line - target-curve tonal balance (Tier S sketch)

Tier S, gate G0-G2 sketch. Oberli Audio effect (analyzer, pass-through), Bone theme, accent Peacock #039BE5 (analysis and monitoring, category scheme confirmed 2026-11-01). Static frame: `bench-line@1x.png` (960 x 520 at 100 %), `bench-line@2x.png` for Retina viewing. Sound side: sound-design-bob is needed for the band analysis, the target statistics and the suggestion rules; the audio path is a wire.

## 1. G0 brief

**Users.** Björn mastering the worship EP and the live album, and checking mixes on the master bus while mixing; friends who get the build.

**Why it is a gap.** iZotope Tonal Balance Control 2 and the "match" views of Metric AB and REFERENCE are paid; free analyzers (SPAN, FreqAnalyst) show a spectrum but not where it should be. No free VST3 compares a mix to a target range, and Ableton's Spectrum has no target, no curve recall and no tolerance. A B see compares against one reference section; Bench Line compares against the statistics of many references, which is the more stable answer.

**Core jobs.**
1. Know in ten seconds whether the mix is too heavy, too thin, too dull or too harsh, against a target that fits the genre.
2. Build that target from my own references (the songs I want to sound like), not someone else's genre curve.
3. Judge low-end punch and side-channel balance separately, because a spectrum alone hides both.
4. Get one plain suggestion per problem, worded as an EQ move I can try in EQ Eight, without the plugin touching the audio.

**Context.** Last device on the master, or on a bus. Window opened every few minutes, glanced at, closed. Often on a laptop beside the DAW.

**Success.** On a mix with a +3 dB bump at 250 Hz against the target, the low-mid region shows outside the band within 10 s of playback and the suggestion names 250 Hz and about -2 dB; building a target from six reference files takes under 20 s; the plugin is a bit-exact wire; idle CPU with the editor closed is 0.

**The five core controls.** Target, View (Tone / Punch / Side), Average, Hold, Build from files. Only View and Average are host parameters; the rest is state.

**CPU and latency budget.** Zero latency; the analysis runs on a background thread from a lock-free copy of the audio. Editor closed: 0.1 % (the copy only). Editor open: 0.8 % on an M1 at 48 kHz, 128 samples.

**Three structural decisions (sound side).**
1. The audio path is a wire, always. Bench Line never processes; suggestions are words, not an EQ. A tonal-balance tool that also equalises invites "make it look right" mixing.
2. Targets are statistics, not one curve. A target is a median curve plus a tolerance band (the 20th to 80th percentile across many short-term frames of many songs), stored as 31 third-octave values each. Built-in targets ship as these numbers only (no audio), computed once from licensed or own material; the user's targets are built the same way from files they drop.
3. Compare like with like. Both the mix and the target are measured after K-weighted loudness matching and on a -4.5 dB/oct compensated axis, so the display is flat when the mix sits on the target and the deviations read in plain dB; frames below -40 LUFS short-term are ignored so silence and fades do not pull the curve.

## 2. G1 design plan (five lines)

1. **Window** 960 x 520 at 100 %, Bone, compact; one display well, the target list on the left like A B see's reference list, so the two plugins feel like one family of tools.
2. **Hero** is what the ear cannot judge: the mix curve inside the target's tolerance band, with the parts outside the band drawn in the accent, the six region bars below (a mark inside or outside a band, Tonal Balance Control's best idea, kept), and the suggestion lines in words.
3. **Accent** Peacock on the set and active only: the tolerance band and target line, the out-of-band curve segments, the outline of out-of-band region marks, the selected View segment and lit LEDs, the knob arc. The mix curve and the meter are ink.
4. **Sections** from the table's groups: Target (the list), Balance (View, display, regions, suggestions), Output (Average 64 px and a LUFS S meter; no Mix, it is a wire, the A B see line for the brand sheet).
5. **States**: modified (pill dims when View or Average differ from the preset), drawer, About (charcoal page), bypass, no target (the well shows only the mix curve and the line "Choose a target or build one from your references"), building (a progress hairline in the list row, "Analyzing 3 of 6"), Hold (the curve freezes, the LED lights, the readout says since when), silence (curve fades to `ink-subtle` after 2 s below -40 LUFS, regions show no mark rather than a wrong one).

## 3. Parameter table (sketch, six columns)

| id | type | name | group | unit / choices | min / max / default |
|---|---|---|---|---|---|
| view | choice | View | Balance | Tone \| Punch \| Side | 0 / 2 / 0 |
| average | float | Average | Output | s, center 10 | 1 / 60 / 10 |
| hold | bool | Hold | Balance | - | 0 / 1 / 0 |
| tolerance | float | Tolerance | Balance | %, percentile width | 50 / 90 / 60 |
| bypass | bool | Bypass | - | - | 0 / 1 / 0 |

Not host parameters (plugin state, saved in the project): the selected target, the user's target library (in the shared `Oberli Audio/References/` folder, read by every Bench Line and A B see instance), the region edges, the suggestion history. Average "60 s" is labeled "Song" in the value string, because that is what it means on a master.

Tooltips:
- View: "Tone compares the spectrum, Punch the low-end dynamics, Side the stereo spectrum."
- Average: "How long the mix curve averages; short to follow a section, long for the whole song."
- Hold: "Freezes the curve so you can change the mix and compare."
- Tolerance: "How wide the band is; wider accepts more of what your references do."

## 4. Approach and best practice

### 4.1 Analysis
- Third-octave bands from 20 Hz to 20 kHz (31 bands), each from a 4096-point FFT with 4x overlap, power-summed per band, after K-weighting loudness normalization of the frame to the target's reference loudness.
- Mix curve: exponential average with the Average time constant over frames above -40 LUFS short-term; Hold freezes it.
- Display: deviation from the -4.5 dB/oct slope, smoothed across one band, ±9 dB visible; out-of-band segments (mix above the 80th or below the 20th percentile) drawn in the accent.

### 4.2 Targets
- Built from files: drop or choose audio files (or pick from A B see's library); each file is decoded on a background thread, split into 400 ms frames, frames below -40 LUFS discarded, each kept frame loudness-normalized; per band, the 20th, 50th and 80th percentile across all frames of all files form the target. Six songs take under 20 s on an M1.
- The Tolerance parameter re-derives the band from stored percentiles (10/20/30/50/70/80/90 are stored), so changing it is instant and needs no re-analysis.
- Built-in targets: five genres and a plain slope, shipped as percentile tables in the plugin binary, no audio. Their provenance is stated in the manual (own and licensed material, how many songs, which years).
- Target files are small JSON (31 bands x 7 percentiles x 3 views) in the user folder; they can be shared with friends.

### 4.3 Punch view
- Per region (the six of the bar row), the crest factor (peak over RMS in 50 ms windows, averaged) of the mix against the same statistic of the target: too high reads "spiky, could take more compression", too low "squashed". This is what Tonal Balance Control calls Crest and what nobody else offers free.

### 4.4 Side view
- The same as Tone, on the side signal (L-R), normalized to the mid loudness; the target stores side percentiles too. Answers "is my top end too wide" or "is there bass in the sides" without a goniometer.

### 4.5 Suggestions
- At most two lines, the regions furthest outside the band for at least 8 s; the line names the region, how far and which side ("2.1 dB above the band around 260 Hz"), and a move in EQ Eight's terms ("try about -2 dB, Q 0.9", "try a shelf, +2 dB at 8 kHz"), rounded to what one can set. The amount is half to two-thirds of the deviation on purpose: a suggestion that lands exactly on the median makes every mix the same.
- Suggestions never repeat for the same region within 30 s, and disappear once the region is back inside the band; "since 0:42" is the song position when it appeared.

### 4.6 Hosts and quirks
- 5 parameters, all visible in Live 10.1's panel.
- The analysis copy is lock-free (single-producer FIFO), the FFT runs on a background thread at 30 Hz only while the editor is open; editor closed, only the copy and the loudness gate run, so the mix curve is current when the window opens.
- Live on Windows auto-scale note (ship-bob).

### 4.7 Deliberately not in it
- No EQ, no "match" button: Bench Line shows and suggests, it never changes the audio.
- No per-instrument targets (vocal, kick): those need source-aware analysis and different statistics.
- No cloud genre library: targets are files the user owns.

## 5. Manual outline
1. One paragraph and the sentence "Bench Line never changes your audio."
2. Choosing a target, and why your own references beat a genre curve.
3. Building a target from files; sharing it.
4. Reading Tone, Punch and Side; the region bars; Hold to compare.
5. Suggestions, and why they deliberately undershoot.
6. Parameters (generated).
7. Troubleshooting: curve jumps (Average too short), everything outside the band (wrong target or a very different genre), regions empty (signal below -40 LUFS).

## 6. Acceptance tests for promotion to P
1. Wire: output is bit-identical to the input in every state.
2. Detection: pink noise shaped to a target's median reads inside the band in all regions; the same with +3 dB at 250 Hz (Q 1) reads Low-mid outside within 10 s at Average 10 s, and the suggestion names 250 Hz ±1/3 octave and -1.5 to -2.5 dB.
3. Loudness independence: the same mix at -8 and -16 LUFS gives the same curve within 0.2 dB.
4. Build: six 4-minute songs build a target in under 20 s; the target file reloads identically.
5. CPU: 0.1 % editor closed, under 1 % editor open.

## 7. Open decisions for Björn
- Name: Bench Line (decided 2026-11-01; benchmark plus baseline, the target line on the display). Slug `bench-line`, plugin ID `BenchLine`. The readme and About page add "compares your mix's tonal balance to your references", since the name does not say "tone".
- Accent: Peacock #039BE5, by the category scheme (decided 2026-11-01): one accent per category, analysis and monitoring.
- Built-in targets at all? They need provenance; the sketch ships five plus the slope, and leads with "Mine".
- Decided 2026-11-01: Bench Line and A B see share one reference folder (`Oberli Audio/References/`), each file's analysis cached once and read by both.

## 8. Report
```
Tier: S, gate: G0-G2 sketch
Hosts: n/a at S
Brand sheet: deviates: output column holds Average and a LUFS meter, no Mix (wire, the A B see line); logo is a CSS stand-in
What works well: slope-compensated axis makes "on target" read as flat; targets as percentile statistics built from the user's own references; suggestions undershoot on purpose; strictly a wire
Findings: none at S
Open / recommended: built-in target provenance (Schwere 2)
Evidence: bench-line@1x.png at 100 %, bench-line@2x.png
```
