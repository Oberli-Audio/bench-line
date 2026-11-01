# Status - Bench Line 0.1.0 (2026-11-01)
Tier P, track A (analysis; plugin kind effect), at gate G1 (awaiting owner OK). Recipe D.8.1.

## Gates
| Gate | State | Evidence (command -> number, test name, log) |
|---|---|---|
| G0 | closed 2026-11-01 | `docs/brief.md` owner-confirmed; Q1-Q10 resolved as recommended |
| G1 | proposed 2026-11-01 | decisions below; waits for owner OK |
| G2 | - | numpy reference model in the port's structure (`sound design/`), renders and numbers here |
| G3 | - | |
| G4 | - | |
| G5 | - | |
| G6 | - | |
| G7 | - | |

## Listening debt
- none yet (analysis: "listening" is Björn reading the display on his own mixes, from G4 on)

## Open exceptions
- none yet. Planned at G7 (brief Q7): preset bank 4 instead of 8-12, presetlint loudness check n/a (wire).

## Handoff
Task: Bench Line Tier P. Plan: 1. [x] G0 brief 2. [ ] G1 decisions (owner OK) (next) 3. [ ] G2 numpy model + reference numbers 4. [ ] G3 harness + metric table 5. [ ] G4 port + editor 6. [ ] G5 7. [ ] G6 validate, hosts 8. [ ] G7.
Working tree: clean after commit. Build: not run yet. Tests: not run yet.
Open questions for the owner: G1 OK, and the display reading in G1.8.
Next command: `pip install scipy` then the G2 model under `sound design/model/`.

## Decisions
- 2026-11-01 G0: brief confirmed; Q1 Hold and Tolerance stay host parameters; Q2 400 ms frames gated on centred short-term at -40 LUFS; Q3 11 stored percentiles; Q4 axis -4.5 dB/oct density-equivalent (-1.5 dB/oct band power); Q5 worker runs while signal is present, editor open or closed; Q6 regions fixed; Q7 bank of 4; Q8 slope + Mine at release 1; Q9 band analysis chosen at G1; Q10 Bench Line defines the shared cache format.
- 2026-11-01 G1 (proposed). No selection row for a tonal-balance analyser; nearest rows S.4 loudness meter and S.5 FIFO/atomic swap, both followed.
  1. **Band analysis: IEC 61260-1 class 1 third-octave filter bank, multirate**, not the sketch's 4096 FFT. Each band 6th-order Butterworth band-pass (3 biquads), base-10 ISO centres 20 Hz-20 kHz; the top octave at fs, each lower octave after a x2 decimation (anti-alias IIR low-pass), designed from frequencies at every rate, so nothing is fixed in samples (rule 12). Mean square per 100 ms sub-block per band. Trade-off: costs ~2x the top octave (about 40 biquads per input sample for mid + side) instead of one FFT, but the 20-63 Hz bands get their true IEC shape where a 4096 FFT has fewer than one bin per band (11.7 Hz bins vs 4.6 Hz at 20 Hz), and it reads the same at every rate. Departure measured by: band response against the IEC class 1 mask at all six rates, and pink noise per band within +/-0.3 dB of the full-rate direct filter bank (the G2 truth); **rejected if** worker CPU exceeds 1 % of one core at 48 kHz editor open, or the 20 kHz band at 44.1 kHz (upper edge 22.4 kHz is above Nyquist; top band pre-warped / matched, rule 6) differs from 96 kHz by more than 0.5 dB on pink noise (then the 20 kHz band leaves the Air region).
  2. **Framing and loudness** (Q2, decision 3): everything on the BS.1770 100 ms sub-block grid. K-weighting redesigned per rate (S.4 row, `dspkit.k_weighting_sos`); 400 ms frames at a 100 ms hop; a frame counts if the short-term (3 s) loudness centred on it is >= -40 LUFS (the mix display therefore trails by 1.5 s, presentation only). Tone: per-channel band power (L^2 + R^2)/2, never the mono sum (A.2); Side: band power of (L-R)/2. Each frame's band levels in dB minus the frame's K-weighted loudness: Tone by the stereo BS.1770 loudness, Side by the mid loudness (sketch 4.4). The LUFS S meter is the same sub-block engine (EBU 3341, rule 17).
  3. **Mix curve = exponentially weighted median, not an average of power.** Per band a decaying histogram of the normalised frame levels (0.1 dB bins, -60..+30 dB, decay from Average in seconds); the median is read off the histogram. That is the same statistic as the target's median (like with like); a power average sits several dB above the median on bass-heavy material. Hold freezes the histogram; the held medians go into the plugin state. Rejected if the histogram median differs from numpy's weighted median by more than 0.05 dB (G3).
  4. **Targets**: build = the same engine run offline over decoded files (one job per file, worker pool), per-file cache of the frame features (versioned JSON, Q10) in `Oberli Audio/References/`, percentiles 5/10/15/20/25/50/75/80/85/90/95 per band for Tone and Side and per region for Punch (Q3); Tolerance interpolates linearly between stored percentiles (instant, no worker job). The plain slope target: median = the -1.5 dB/oct band-power axis, spread a Gaussian of sigma 2 dB per band (a documented constant); rejected if Björn's own references give a mean 20-80 width more than 2x different, then their mean replaces it.
  5. **Punch**: six region band-passes (4th-order Butterworth on the region edges of the brief) on the mid signal at full rate; crest = peak over RMS in 50 ms windows, in dB, averaged per 400 ms frame; the same percentiles and median histogram as Tone. Too high reads "spiky", too low "squashed".
  6. **Regions and suggestions**: region deviation = mean over its bands of (mix median - target median); outside when it exceeds the region's mean distance from median to the band edge on that side. Suggestion: the band of the largest deviation in the region names the frequency; the move is 0.6 x the deviation from the median, rounded to 0.5 dB (+3 dB at 250 Hz -> about -2 dB, inside acceptance 1.5-2.5); Q from the width of the run of outside bands; shelf wording for Sub and Air. Shown after 8 s outside, not repeated for a region within 30 s, cleared when back inside.
  7. **Threads and real-time** (rule 1, S.5 rows): `processBlock` is `ScopedNoDenormals`, reads the play-head position into an atomic, and pushes the block into a pre-allocated SPSC FIFO (`juce::AbstractFifo`, 1 s at the prepared rate, overflow counted, never blocks); the buffer itself is untouched, so the wire is bit-exact by construction and latency is 0. One worker thread per instance drains the FIFO; below the -70 LUFS absolute gate it skips and resets the band filters (idle ~ K-weighting only; Q5). The worker publishes a snapshot (bands, regions, suggestions, meter) by triple buffer; the editor reads it at 30 Hz. Bypass: the host bypass stays a wire and pauses the analysis (the display says so).
  8. **Display reading (G1 joint, for the owner's OK)**: with a target, the well draws mix median minus target median, so the band sits around a flat zero line and "on target" reads flat (decision 3's sentence); without a target, the mix is drawn on the -4.5 dB/oct density-equivalent axis (sketch 4.1). The sketch's design plan (section 2) is the interface G1, unchanged.
