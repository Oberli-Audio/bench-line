#!/usr/bin/env python3
"""G2 numbers for Bench Line's reference model. Prints one JSON object; docs/status.md quotes it.

    python3 "sound design/model/g2_check.py" [--quick]
"""
import json
import sys
import time
from pathlib import Path

import numpy as np
from scipy import signal

sys.path.insert(0, str(Path(__file__).resolve().parent))
import benchline_model as B  # noqa: E402

QUICK = "--quick" in sys.argv
RATES = (44100, 48000, 96000) if QUICK else (44100, 48000, 88200, 96000, 176400, 192000)
out = {}


def band_levels(x, fs, multirate=True):
    """Mean band power over the whole signal (dB), multirate engine or the full-rate truth."""
    if multirate:
        f = B.analyse(x, fs)
        return 10 * np.log10(np.mean(f["PM"][5:] + f["PS"][5:], axis=0))
    M = 0.5 * (x[:, 0] + x[:, 1]); S = 0.5 * (x[:, 0] - x[:, 1])
    res = []
    for b in range(B.NB):
        f2 = min(B.BAND_HI[b], B.TOP_EDGE * fs)
        sos = signal.butter(3, [B.BAND_LO[b], f2], "bandpass", fs=fs, output="sos")
        ym, ys = signal.sosfilt(sos, M), signal.sosfilt(sos, S)
        n0 = int(0.5 * fs)
        res.append(10 * np.log10(B.band_correction(b, fs) * (np.mean(ym[n0:] ** 2) + np.mean(ys[n0:] ** 2))))
    return np.array(res)


# 1. band accuracy: multirate vs the full-rate direct bank, pink noise, every rate
acc = {}
pinkdb = {}
for fs in RATES:
    x = B.stereo_pink(8.0, fs, seed=3)
    a, t = band_levels(x, fs), band_levels(x, fs, multirate=False)
    acc[fs] = float(np.max(np.abs(a - t)))
    pinkdb[fs] = a
out["band_vs_truth_max_db"] = acc
# 2. one signal at every rate: a deterministic multitone (bands 20 Hz-16 kHz, tones below
#    16 kHz's upper edge) and, for the 20 kHz band (G1 rejection criterion), 30 s of pink noise
ratecmp = {}
ref = None
for fs in RATES:
    lv = band_levels(B.multitone(4.0, fs, seed=5, fmax=B.BAND_HI[29]), fs)[:30]
    p20 = band_levels(B.stereo_pink(30.0, fs, seed=11), fs)
    p20 = p20[30] - p20[29]                     # 20 kHz band relative to 16 kHz on pink noise
    if ref is None:
        ref = (lv, p20)
    ratecmp[fs] = {"max_diff_20Hz_16kHz_db": round(float(np.max(np.abs(lv - ref[0]))), 4),
                   "band20k_vs_first_rate_db": round(float(p20 - ref[1]), 3)}
out["rate_vs_%d" % RATES[0]] = ratecmp

# 3. sines at band centres read in their own band, neighbours down (selectivity)
fs = 48000
sel = []
for b in (0, 5, 11, 17, 24, 29):
    n = int(4 * fs); t = np.arange(n) / fs
    s = np.sin(2 * np.pi * B.BAND_FC[b] * t)
    lv = band_levels(np.stack([s, s], 1), fs)
    own = lv[b]
    sel.append(dict(band=B.BAND_NOMINAL[b], own_db=round(float(own - 10 * np.log10(0.5)), 3),
                    next_band_down_db=round(float(own - max(lv[b - 1] if b > 0 else -300, lv[b + 1] if b < 30 else -300)), 1)))
out["sine_at_centre"] = sel

# 4. EBU Tech 3341 short-term: cases 1, 2 (1 kHz at -23 / -33 dBFS, stereo) and 9 (bursts, S = -23)
ebu = {}
for fs in RATES:
    n = int(20 * fs); t = np.arange(n) / fs
    errs = []
    for amp_db, want in ((-23.0, -23.0), (-33.0, -33.0)):
        s = 10 ** (amp_db / 20) * np.sin(2 * np.pi * 1000 * t)
        st = B.short_term_series(B.analyse(np.stack([s, s], 1), fs))[30:]
        errs.append(float(np.max(np.abs(st - want))))
    # case 9: 1.34 s at -20 dBFS, 1.66 s at -30 dBFS, repeated
    ph = np.mod(t, 3.0)
    amp = np.where(ph < 1.34, 10 ** (-20 / 20), 10 ** (-30 / 20))
    s = amp * np.sin(2 * np.pi * 1000 * t)
    st = B.short_term_series(B.analyse(np.stack([s, s], 1), fs))[30:]
    errs.append(float(np.max(np.abs(st + 23.0))))
    ebu[fs] = [round(e, 4) for e in errs]
out["ebu3341_short_term_max_err_lu"] = ebu

# 5. acceptance 2 (detection) and 3 (loudness independence), 48 kHz
fs = 48000
t0 = time.time()
refs = [(B.set_lufs(B.stereo_pink(30.0, fs, seed=100 + i), fs, -14.0), fs) for i in range(6)]
target = B.build_target(refs, "pink")
out["build_seconds_model_6x30s"] = round(time.time() - t0, 2)
mix = B.set_lufs(B.stereo_pink(20.0, fs, seed=7), fs, -14.0)
tr = B.run_mix(mix, fs, target)
last = tr.history[-1]["views"]["tone"]
out["acc2_flat_regions"] = [r["verdict"] for r in last["regions"]]
bump = B.set_lufs(B.peaking(mix, fs, 250.0, 3.0, 1.0), fs, -14.0)
tr = B.run_mix(bump, fs, target)
first_out = next((h["t"] for h in tr.history if h["views"]["tone"]["regions"][2] and h["views"]["tone"]["regions"][2]["verdict"] == "above"), None)
first_sugg = next((h for h in tr.history if h["views"]["tone"]["suggestions"]), None)
last = tr.history[-1]["views"]["tone"]
out["acc2_bump_regions"] = [r["verdict"] for r in last["regions"]]
out["acc2_lowmid_outside_at_s"] = first_out
out["acc2_suggestion"] = None if first_sugg is None else dict(t=first_sugg["t"], **{k: v for k, v in first_sugg["views"]["tone"]["suggestions"][0].items() if k != "since_s"})
# 3: -8 vs -16 LUFS
a = B.run_mix(B.set_lufs(mix, fs, -8.0), fs, target).history[-1]["views"]["tone"]["median"]
b = B.run_mix(B.set_lufs(mix, fs, -16.0), fs, target).history[-1]["views"]["tone"]["median"]
out["acc3_loudness_independence_max_db"] = float(np.max(np.abs(a - b)))
# 6. gate: 10 s of -60 LUFS noise of a different spectrum (low-passed at 500 Hz) after the mix
#    does not move the medians; baseline: the mix followed by 10 s of digital silence
q = signal.sosfilt(signal.butter(4, 500, fs=fs, output="sos"), B.stereo_pink(10.0, fs, seed=9), axis=0)
quiet = np.concatenate([mix, B.set_lufs(q, fs, -60.0)])
m1 = B.run_mix(np.concatenate([mix, np.zeros((int(10 * fs), 2))]), fs, target).history[-1]["views"]["tone"]["median"]
m2 = B.run_mix(quiet, fs, target).history[-1]["views"]["tone"]["median"]
out["gate_shift_max_db"] = float(np.max(np.abs(m1 - m2)))
# 7. histogram median vs numpy's weighted median of the same frames
fr = [f for f in B.frames(B.analyse(mix, fs), final=False) if f["gated"]]
vals = np.array([f["tone"] for f in fr])
w = np.exp(-B.SUB_S / 10.0) ** np.arange(len(vals))[::-1]
def wmed(v):
    o = np.argsort(v); c = np.cumsum(w[o]); return v[o][np.searchsorted(c, c[-1] / 2)]
ref = np.array([wmed(vals[:, k]) for k in range(B.NB)])
out["hist_median_vs_weighted_median_max_db"] = float(np.max(np.abs(B.run_mix(mix, fs, target).history[-1]["views"]["tone"]["median"] - ref)))
# 8. plain slope target: noise on the slope (-4.5 dB/oct density) reads inside in every region
rng = np.random.default_rng(21); n = 30 * fs
X = np.fft.rfft(rng.standard_normal(n)); fr_ = np.fft.rfftfreq(n, 1 / fs); fr_[0] = 1; X /= fr_ ** 0.75; X[0] = 0
y = np.fft.irfft(X, n); y /= np.std(y)
out["slope_target_on_slope_noise"] = [r["verdict"] for r in B.run_mix(np.stack([y, y], 1), fs, B.slope_target()).history[-1]["views"]["tone"]["regions"]]
print(json.dumps(out, indent=1, default=float))
