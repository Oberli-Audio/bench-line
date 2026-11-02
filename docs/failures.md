# Failures - the product's own rows (D.9.3)

| Symptom | Cause | Confirming measurement | Fixed in | Regression test |
|---|---|---|---|---|
| 20 kHz band 1.15 dB off at 44.1 kHz (model) | band-pass design kept only complex poles; after prewarping near Nyquist the band is wide and has real poles | `g2_check.py` band_vs_truth at 44.1 kHz: 1.15 dB -> 0.024 dB | G2 model and port (`designBandpass`) | `g2_check.py` band_vs_truth (all six rates); `tools/measure/null_vs_model.py --rates=44100` |
| 20 kHz band read the 16 kHz band at 44.1/48 kHz (4 dB separation) | the first fix used a high-pass for the top band | `g2_check.py` sine_at_centre 16 kHz: 4.2 dB -> 12.2 dB | G2 | `g2_check.py` sine_at_centre |
| A quiet passage after a loud one moved the curve 0.44 dB | the centred short-term gate lets frames up to 1.5 s after the loud part through | `g2_check.py` gate_shift 0.44 dB -> 0.0 dB | G2 model and port (frame loudness also >= -40 LUFS; brief amendment) | `test_gate_quiet_tail_does_not_move_the_curve` |
| Harness crashed (glibc heap assertion) on a block larger than announced | the harness's own work buffer was sized to `--max-block` (template) | `test_block_larger_than_announced` aborted with exit -6 | harness `Main.cpp` | `test_block_larger_than_announced` |
| Every test's echo check failed | the harness echoed `sample_rate` as 48000.0 (template) | `assert_echo`: "applied 48000.0, asked 48000" | harness `Main.cpp` | `test_harness_echoes_params` |
