#!/usr/bin/env python3
"""Bench Line - G2 reference model (numpy), in the structure of the C++ port (F.8).

Every stage here has a one-to-one counterpart in Source/dsp/ (the port), and the constants
are the same numbers, documented once in docs/analysis.md. The model is the truth the port
is null-tested against at G5 (band levels, medians, region verdicts, suggestions).

Structure (G1 decisions, docs/status.md):
  input L/R -> M = (L+R)/2, S = (L-R)/2
  K-weighting (dspkit.k_weighting_sos, redesigned per rate) of M and S
  third-octave filter bank on M and S: 31 bands, order-3 Butterworth band-pass (6th order),
    multirate: each band at the lowest octave stage whose rate is >= 5 x its centre;
    stage s+1 = stage s through an 8th-order Butterworth low-pass at 0.2 x rate, every 2nd sample
  per 100 ms sub-block: band mean squares, K-weighted mean squares, crest windows
  400 ms frames at a 100 ms hop; gate: centred short-term and the frame's own loudness >= -40 LUFS
  Tone band level = 10 log10(P_M + P_S) - stereo loudness of the frame   ((L^2+R^2)/2 = M^2+S^2)
  Side band level = 10 log10(P_S) - mid loudness of the frame
  Punch = crest (peak/RMS, 50 ms windows, mean of dB) per region on M
  mix curve = exponentially weighted median per band (decaying histogram, 0.1 dB bins)
  target = percentiles 5..95 over every gated frame of every file
"""
import math
import sys
from pathlib import Path

import numpy as np
from scipy import signal

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "dsp-kit"))
import dspkit as K  # noqa: E402

# ------------------------------------------------------------------ constants (docs/analysis.md)
BAND_N = np.arange(-17, 14)                                  # 1 kHz * 10^(n/10), n = -17..13
BAND_FC = 1000.0 * 10.0 ** (BAND_N / 10.0)                   # exact base-10 centres
BAND_LO = BAND_FC * 10.0 ** (-1.0 / 20.0)
BAND_HI = BAND_FC * 10.0 ** (1.0 / 20.0)
BAND_NOMINAL = [20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
                1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000]
NB = 31
REGIONS = ["Sub", "Low", "Low-mid", "Mid", "Presence", "Air"]
REGION_BANDS = [range(0, 5), range(5, 10), range(10, 15), range(15, 20), range(20, 25), range(25, 31)]
STAGE_RATIO = 5.0              # a band runs at the lowest stage rate >= 5 x its centre
DECIM_CUTOFF = 0.2             # anti-alias low-pass cutoff, fraction of the stage rate
DECIM_ORDER = 8
BAND_ORDER = 3                 # Butterworth prototype order (band-pass order 6)
REGION_ORDER = 2               # crest region band-pass prototype order (order 4)
TOP_EDGE = 0.49                # band upper edges are clamped to 0.49 x rate (20 kHz band at 44.1 kHz)
SUB_S = 0.1                    # sub-block, seconds
FRAME_SUB = 4                  # frame = 4 sub-blocks (400 ms)
ST_SUB = 30                    # short-term = 30 sub-blocks (3 s)
GATE_LUFS = -40.0
GATE_BACK = 16                 # centred short-term window: sub-blocks j-16 .. j+13 for the frame ending at j
GATE_AHEAD = 13
CREST_S = 0.05                 # crest window, seconds
LEVEL_MIN, LEVEL_MAX = -80.0, 20.0   # normalised band level range (histogram, clamp)
CREST_MIN, CREST_MAX = 0.0, 40.0
BIN_DB = 0.1
PERCENTILES = [5, 10, 15, 20, 25, 50, 75, 80, 85, 90, 95]
SUGGEST_AFTER_S = 8.0
SUGGEST_LOCKOUT_S = 30.0
SUGGEST_FRACTION = 2.0 / 3.0        # the move undershoots on purpose (sketch 4.5: half to two-thirds)
SILENT_AFTER_S = 2.0
SLOPE_DB_PER_OCT = -1.5        # band power; = -4.5 dB/oct on power density (brief Q4)
SLOPE_SIGMA_DB = 2.0           # plain slope target spread (G1 decision 4)
SLOPE_OFFSET_DB = -18.5        # normalised level at 1 kHz of noise on the slope (g2_check, two seeds: -18.52, -18.49)


# ------------------------------------------------------------------ filter design (port-identical)
def _blt(s, r):
    return (2.0 * r + s) / (2.0 * r - s)


def bandpass_sections(f1, f2, r, order=BAND_ORDER):
    """Butterworth band-pass (prototype order N, band-pass order 2N) as N biquads.
    Analog prototype, prewarped edges, bilinear transform; each section has zeros at z = +1
    and -1 and unity gain at the centre. The upper edge is clamped to TOP_EDGE x rate (only the
    20 kHz band at 44.1 kHz); band_correction() restores the power of the missing width.
    Returns an (N, 6) sos array."""
    N = order
    lp = [np.exp(1j * np.pi * (2 * k + N + 1) / (2 * N)) for k in range(N)]
    f2 = min(f2, TOP_EDGE * r)
    W1 = 2.0 * r * math.tan(math.pi * f1 / r)
    W2 = 2.0 * r * math.tan(math.pi * f2 / r)
    w0sq, B = W1 * W2, W2 - W1
    allp = []
    for p in lp:
        disc = np.sqrt(complex(p * p * B * B - 4.0 * w0sq))
        allp += [(p * B + disc) / 2.0, (p * B - disc) / 2.0]
    tol = 1e-9 * math.sqrt(w0sq)
    pairs = [(s, s.conjugate()) for s in allp if s.imag > tol]          # conjugate pairs
    reals = sorted(s.real for s in allp if abs(s.imag) <= tol)          # wide bands: real poles, paired in order
    pairs += [(complex(reals[i]), complex(reals[i + 1])) for i in range(0, len(reals), 2)]
    assert len(pairs) == N
    wc = 2.0 * math.atan(math.sqrt(w0sq) / (2.0 * r))      # digital centre, rad/sample
    ejw = np.exp(1j * wc)
    sos = []
    for s1, s2 in pairs:
        z1, z2 = _blt(s1, r), _blt(s2, r)
        b = np.array([1.0, 0.0, -1.0])
        a = np.array([1.0, -(z1 + z2).real, (z1 * z2).real])
        h = (b[0] + b[1] / ejw + b[2] / ejw ** 2) / (a[0] + a[1] / ejw + a[2] / ejw ** 2)
        g = 1.0 / abs(h)
        sos.append(list(g * b) + list(a))
    return np.array(sos)


def band_correction(b, r):
    """Power factor for a band whose upper edge was clamped: the full third-octave width over
    the clamped width, in octaves (a pink-flat band). 1.0 for every other band."""
    f2 = min(BAND_HI[b], TOP_EDGE * r)
    return math.log(BAND_HI[b] / BAND_LO[b]) / math.log(f2 / BAND_LO[b])


def lowpass_sections(fc, r, order=DECIM_ORDER):
    N = order
    W = 2.0 * r * math.tan(math.pi * fc / r)
    sos = []
    for k in range(N):
        p = W * np.exp(1j * np.pi * (2 * k + N + 1) / (2 * N))
        if p.imag > 1e-12:
            z = _blt(p, r)
            a = [1.0, -2.0 * z.real, abs(z) ** 2]
            g = (a[0] + a[1] + a[2]) / 4.0
            sos.append([g, 2.0 * g, g] + a)
    return np.array(sos)


def stage_of_band(fc, fs):
    s = 0
    while fs / 2 ** (s + 1) >= STAGE_RATIO * fc:
        s += 1
    return s


class Design:
    """Everything prepare() computes for one sample rate."""

    def __init__(self, fs):
        self.fs = fs
        self.sub_len = int(round(SUB_S * fs))
        self.crest_len = int(round(CREST_S * fs))
        assert self.sub_len == 2 * self.crest_len
        self.band_stage = [stage_of_band(fc, fs) for fc in BAND_FC]
        self.n_stages = max(self.band_stage) + 1
        self.band_sos = [bandpass_sections(BAND_LO[b], BAND_HI[b], fs / 2 ** self.band_stage[b]) for b in range(NB)]
        self.band_gain = [band_correction(b, fs / 2 ** self.band_stage[b]) for b in range(NB)]
        self.decim_sos = [lowpass_sections(DECIM_CUTOFF * fs / 2 ** s, fs / 2 ** s) for s in range(self.n_stages - 1)]
        self.k_sos = K.k_weighting_sos(fs)
        self.region_sos = [bandpass_sections(BAND_LO[rb[0]], BAND_HI[rb[-1]], fs, REGION_ORDER) for rb in REGION_BANDS]


# ------------------------------------------------------------------ the engine, whole-signal form
def analyse(x, fs, design=None):
    """Per-sub-block features of a stereo signal (N, 2). Equivalent to streaming the
    samples through the port's engine from reset; only complete sub-blocks are returned."""
    d = design or Design(fs)
    x = np.asarray(x, dtype=np.float64)
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    L, R = x[:, 0], x[:, 1]
    M, S = 0.5 * (L + R), 0.5 * (L - R)
    nsub = len(x) // d.sub_len
    n_used = nsub * d.sub_len

    def per_sub(v, step=1, offset_len=None):
        """mean square per sub-block of a stage signal whose sample k is input sample k*step."""
        idx = (np.arange(len(v)) * step) // d.sub_len
        keep = idx < nsub
        sums = np.bincount(idx[keep], weights=v[keep] ** 2, minlength=nsub)
        cnt = np.bincount(idx[keep], minlength=nsub)
        return sums / np.maximum(cnt, 1)

    kM = signal.sosfilt(d.k_sos, M)
    kS = signal.sosfilt(d.k_sos, S)
    zM, zS = per_sub(kM), per_sub(kS)
    stages_M, stages_S = [M], [S]
    for s in range(d.n_stages - 1):
        stages_M.append(signal.sosfilt(d.decim_sos[s], stages_M[-1])[::2])
        stages_S.append(signal.sosfilt(d.decim_sos[s], stages_S[-1])[::2])
    PM = np.zeros((nsub, NB))
    PS = np.zeros((nsub, NB))
    for b in range(NB):
        s = d.band_stage[b]
        PM[:, b] = d.band_gain[b] * per_sub(signal.sosfilt(d.band_sos[b], stages_M[s]), 2 ** s)
        PS[:, b] = d.band_gain[b] * per_sub(signal.sosfilt(d.band_sos[b], stages_S[s]), 2 ** s)
    # crest: per 50 ms window peak and rms on M per region; per sub-block the sum of window crests (dB) and count
    nwin = n_used // d.crest_len
    crest_sum = np.zeros((nsub, 6))
    crest_cnt = np.zeros((nsub, 6))
    for r in range(6):
        y = signal.sosfilt(d.region_sos[r], M)[: nwin * d.crest_len].reshape(nwin, d.crest_len)
        pk = np.max(np.abs(y), axis=1)
        rms = np.sqrt(np.mean(y * y, axis=1))
        ok = rms > 1e-10
        c = np.where(ok, 20.0 * np.log10(np.maximum(pk, 1e-30) / np.maximum(rms, 1e-30)), 0.0)
        sub = np.arange(nwin) // 2
        crest_sum[:, r] = np.bincount(sub, weights=c * ok, minlength=nsub)[:nsub]
        crest_cnt[:, r] = np.bincount(sub, weights=ok.astype(float), minlength=nsub)[:nsub]
    return dict(fs=fs, nsub=nsub, zM=zM, zS=zS, PM=PM, PS=PS, crest_sum=crest_sum, crest_cnt=crest_cnt)


def lufs(power):
    with np.errstate(divide="ignore"):
        return -0.691 + 10.0 * np.log10(power)


def short_term_series(f):
    """Short-term loudness (last 30 sub-blocks, fewer at the start) after each sub-block, as the
    Output meter shows it. Stereo loudness: z_L + z_R = 2 (z_M + z_S)."""
    z = 2.0 * (f["zM"] + f["zS"])
    c = np.concatenate([[0.0], np.cumsum(z)])
    j = np.arange(f["nsub"])
    lo = np.maximum(0, j - ST_SUB + 1)
    return lufs((c[j + 1] - c[lo]) / (j + 1 - lo))


def frames(f, final=True):
    """400 ms frames at a 100 ms hop. Frame j ends at sub-block j (j >= 3). Returns a list of
    dicts: j, ready_s (when the streaming engine can emit it), gated, tone, side, punch.
    final=True: the input has ended, so the last frames use the sub-blocks that exist (the
    build); final=False: a frame needs its 13 look-ahead sub-blocks (the live mix)."""
    n = f["nsub"]
    z = 2.0 * (f["zM"] + f["zS"])
    c = np.concatenate([[0.0], np.cumsum(z)])
    out = []
    for j in range(FRAME_SUB - 1, n):
        hi = j + GATE_AHEAD
        if hi >= n:
            if not final:
                break
            hi = n - 1
        lo = max(0, j - GATE_BACK)
        st = lufs((c[hi + 1] - c[lo]) / (hi + 1 - lo))
        sl = slice(j - FRAME_SUB + 1, j + 1)
        zf = np.mean(z[sl])
        zmid = np.mean(f["zM"][sl])
        PT = np.mean(f["PM"][sl] + f["PS"][sl], axis=0)
        PSd = np.mean(f["PS"][sl], axis=0)
        cs, cc = f["crest_sum"][sl].sum(axis=0), f["crest_cnt"][sl].sum(axis=0)
        # gate: the short-term loudness centred on the frame AND the frame's own (momentary)
        # loudness >= -40 LUFS (brief amendment 2026-11-01: the centred window alone lets up to
        # 1.5 s of a quiet passage after a loud one through)
        gated = bool(st >= GATE_LUFS and zf > 0.0 and lufs(zf) >= GATE_LUFS)
        tone = np.clip(10.0 * np.log10(PT + 1e-30) - lufs(zf), LEVEL_MIN, LEVEL_MAX) if gated else None
        side = np.clip(10.0 * np.log10(PSd + 1e-30) - lufs(max(zmid, 1e-30)), LEVEL_MIN, LEVEL_MAX) if gated else None
        punch = np.clip(np.where(cc > 0, cs / np.maximum(cc, 1), 0.0), CREST_MIN, CREST_MAX) if gated else None
        out.append(dict(j=j, ready_s=(hi + 1) * SUB_S, gated=gated, tone=tone, side=side, punch=punch, st=st))
    return out


# ------------------------------------------------------------------ targets
def percentile_table(values):
    """values: (n_frames, k) -> (k, len(PERCENTILES)), numpy 'linear' interpolation (the port's)."""
    v = np.asarray(values)
    return np.percentile(v, PERCENTILES, axis=0).T


def build_target(signals, name="Mine"):
    """signals: list of (x, fs). Every gated frame of every file, pooled."""
    tone, side, punch = [], [], []
    for x, fs in signals:
        for fr in frames(analyse(x, fs), final=True):
            if fr["gated"]:
                tone.append(fr["tone"]); side.append(fr["side"]); punch.append(fr["punch"])
    if not tone:
        raise ValueError("no frame above -40 LUFS in the files")
    return dict(format="oberli-target", version=1, name=name, frames=len(tone), percentiles=PERCENTILES,
                bands_hz=BAND_NOMINAL, regions=REGIONS,
                tone=percentile_table(tone).tolist(), side=percentile_table(side).tolist(),
                punch=percentile_table(punch).tolist())


def norm_quantile(p):
    from scipy.stats import norm
    return float(norm.ppf(p / 100.0))


def slope_target(offset_db=SLOPE_OFFSET_DB):
    """The built-in plain slope: median on the -1.5 dB/oct band-power axis through offset_db
    at 1 kHz, spread a Gaussian of sigma 2 dB. Tone only."""
    med = offset_db + SLOPE_DB_PER_OCT * np.log2(BAND_FC / 1000.0)
    tab = [[m + SLOPE_SIGMA_DB * norm_quantile(p) for p in PERCENTILES] for m in med]
    return dict(format="oberli-target", version=1, name="Plain slope", frames=0, percentiles=PERCENTILES,
                bands_hz=BAND_NOMINAL, regions=REGIONS, tone=tab, side=None, punch=None)


def edges(row, tolerance):
    """(lower, median, upper) of one band for Tolerance in % (50..90): percentiles 50 -/+ T/2,
    interpolated linearly between the stored ones."""
    lo_p, hi_p = 50.0 - tolerance / 2.0, 50.0 + tolerance / 2.0
    return float(np.interp(lo_p, PERCENTILES, row)), float(row[PERCENTILES.index(50)]), float(np.interp(hi_p, PERCENTILES, row))


# ------------------------------------------------------------------ the mix curve
class Histogram:
    def __init__(self, k, lo, hi):
        self.lo, self.k = lo, k
        self.nbins = int(round((hi - lo) / BIN_DB))
        self.h = np.zeros((k, self.nbins))

    def add(self, values, alpha):
        self.h *= alpha
        idx = np.clip(np.floor((np.asarray(values) - self.lo) / BIN_DB).astype(int), 0, self.nbins - 1)
        self.h[np.arange(self.k), idx] += 1.0

    def median(self):
        out = np.full(self.k, np.nan)
        for i in range(self.k):
            row = self.h[i]
            W = row.sum()
            if W <= 0:
                continue
            c = np.cumsum(row)
            b = int(np.searchsorted(c, W / 2.0))
            before = c[b - 1] if b > 0 else 0.0
            out[i] = self.lo + (b + (W / 2.0 - before) / row[b]) * BIN_DB
        return out


def hz_label(hz):
    return "%.0f Hz" % hz if hz < 1000 else ("%.1f kHz" % (hz / 1000.0)).replace(".0 kHz", " kHz")


def half_db(v):
    return math.copysign(math.floor(abs(v) * 2.0 + 0.5) / 2.0, v)


class Tracker:
    """Mix medians, region verdicts and suggestions for all three views, frame by frame."""

    def __init__(self, target, average_s=10.0, tolerance=60.0):
        self.target, self.average_s, self.tolerance = target, average_s, tolerance
        self.hist = dict(tone=Histogram(NB, LEVEL_MIN, LEVEL_MAX), side=Histogram(NB, LEVEL_MIN, LEVEL_MAX),
                         punch=Histogram(6, CREST_MIN, CREST_MAX))
        self.outside_since = {v: [None] * 6 for v in ("tone", "side", "punch")}
        self.locked_until = {v: [-1e9] * 6 for v in ("tone", "side", "punch")}
        self.shown = {v: {} for v in ("tone", "side", "punch")}       # region -> suggestion dict
        self.last_gated_s = None
        self.history = []

    def push(self, fr, now_s):
        if fr["gated"]:
            alpha = math.exp(-SUB_S / self.average_s)
            for v in ("tone", "side", "punch"):
                self.hist[v].add(fr[v], alpha)
            self.last_gated_s = now_s
        silent = self.last_gated_s is None or now_s - self.last_gated_s > SILENT_AFTER_S
        state = dict(t=now_s, silent=silent, views={})
        for v in ("tone", "side", "punch"):
            state["views"][v] = self._view(v, now_s, silent)
        self.history.append(state)
        return state

    def _view(self, v, now, silent):
        med = self.hist[v].median()
        tab = self.target.get(v) if self.target else None
        res = dict(median=med, regions=[None] * 6, suggestions=[])
        if tab is None or silent or np.isnan(med).all():
            return res
        e = np.array([edges(row, self.tolerance) for row in tab])            # (k, 3)
        dev = med - e[:, 1]
        excess = [0.0] * 6
        for r in range(6):
            idx = [r] if v == "punch" else list(REGION_BANDS[r])
            dr = float(np.mean(dev[idx]))
            up = float(np.mean(e[idx, 2] - e[idx, 1]))
            dn = float(np.mean(e[idx, 1] - e[idx, 0]))
            verdict = "above" if dr > up else ("below" if -dr > dn else "inside")
            res["regions"][r] = dict(verdict=verdict, dev_db=dr)
            if verdict == "inside":
                self.outside_since[v][r] = None
                if r in self.shown[v]:
                    del self.shown[v][r]
                    self.locked_until[v][r] = now + SUGGEST_LOCKOUT_S
            else:
                excess[r] = dr - up if verdict == "above" else -dr - dn
                if self.outside_since[v][r] is None:
                    self.outside_since[v][r] = now
        cand = [r for r in range(6) if excess[r] > 0 and now - self.outside_since[v][r] >= SUGGEST_AFTER_S
                and (r in self.shown[v] or now >= self.locked_until[v][r])]
        cand.sort(key=lambda r: -excess[r])
        keep = cand[:2]
        for r in list(self.shown[v]):
            if r not in keep:
                del self.shown[v][r]
        for r in keep:
            if r not in self.shown[v]:
                self.shown[v][r] = self._suggest(v, r, med, e, res["regions"][r]["verdict"], now)
            else:
                self.shown[v][r] = dict(self._suggest(v, r, med, e, res["regions"][r]["verdict"], now),
                                        since_s=self.shown[v][r]["since_s"])
        res["suggestions"] = [self.shown[v][r] for r in keep]
        return res

    def _suggest(self, v, r, med, e, verdict, now):
        sign = 1.0 if verdict == "above" else -1.0
        if v == "punch":
            d = float(med[r] - e[r, 1])
            beyond = d - (e[r, 2] - e[r, 1]) if sign > 0 else -d - (e[r, 1] - e[r, 0])
            word = "spiky" if sign > 0 else "squashed"
            move = ("could take more compression or a little saturation" if sign > 0
                    else "try less compression or a slower attack")
            return dict(view=v, region=REGIONS[r], hz=None, gain_db=None, q=None, since_s=now,
                        text="%s: %s, crest %.1f dB %s the band; %s." % (REGIONS[r], word, beyond, "above" if sign > 0 else "below", move))
        bands = list(REGION_BANDS[r])
        dev = med - e[:, 1]
        b = max(bands, key=lambda i: sign * dev[i])
        beyond = sign * dev[b] - ((e[b, 2] - e[b, 1]) if sign > 0 else (e[b, 1] - e[b, 0]))
        # the run of neighbouring bands outside on the same side, for Q
        def out(i):
            return (dev[i] > e[i, 2] - e[i, 1]) if sign > 0 else (-dev[i] > e[i, 1] - e[i, 0])
        k0 = k1 = b
        while k0 - 1 >= 0 and out(k0 - 1):
            k0 -= 1
        while k1 + 1 < NB and out(k1 + 1):
            k1 += 1
        width_oct = (k1 - k0 + 1) / 3.0
        q = math.floor(10.0 * math.sqrt(2.0 ** width_oct) / (2.0 ** width_oct - 1.0) + 0.5) / 10.0
        gain = -sign * half_db(SUGGEST_FRACTION * sign * dev[b])
        if gain == 0.0:
            gain = -sign * 0.5
        hz = BAND_NOMINAL[b]
        side_word = "above" if sign > 0 else "below"
        if v == "side":
            move = "try about %+.1f dB in the side around %s (mid/side EQ), or mono the bass below it" % (gain, hz_label(hz)) \
                if r <= 1 else "try about %+.1f dB in the side around %s (mid/side EQ)" % (gain, hz_label(hz))
            text = "%s, side: %.1f dB %s the band around %s; %s." % (REGIONS[r], beyond, side_word, hz_label(hz), move)
        elif r in (0, 5):
            text = "%s: %.1f dB %s the band around %s; try a %s shelf, %+.1f dB at %s." % (
                REGIONS[r], beyond, side_word, hz_label(hz), "low" if r == 0 else "high", gain, hz_label(hz))
        else:
            text = "%s: %.1f dB %s the band around %s; try about %+.1f dB, Q %.1f." % (
                REGIONS[r], beyond, side_word, hz_label(hz), gain, q)
        return dict(view=v, region=REGIONS[r], hz=hz, gain_db=gain, q=q, since_s=now, text=text)


def run_mix(x, fs, target, average_s=10.0, tolerance=60.0):
    """Stream a mix through the tracker; the state after every emitted frame (live: no
    look-ahead beyond the input)."""
    tr = Tracker(target, average_s, tolerance)
    for fr in frames(analyse(x, fs), final=False):
        tr.push(fr, fr["ready_s"])
    return tr


# ------------------------------------------------------------------ test signals
def pink(n, rng):
    """Pink noise (Voss-free: 1/f shaping of white in the frequency domain), unit RMS."""
    w = rng.standard_normal(n)
    X = np.fft.rfft(w)
    f = np.arange(len(X), dtype=float)
    f[0] = 1.0
    X /= np.sqrt(f)
    X[0] = 0.0
    y = np.fft.irfft(X, n)
    return y / np.sqrt(np.mean(y * y))


def stereo_pink(seconds, fs, seed, width=0.3):
    """Pink noise with a fixed amount of side: L = M + w*S, R = M - w*S."""
    rng = np.random.default_rng(seed)
    n = int(round(seconds * fs))
    m, s = pink(n, rng), pink(n, rng)
    return np.stack([m + width * s, m - width * s], axis=1)


def multitone(seconds, fs, seed, per_band=8, fmax=21000.0, width=0.3):
    """Deterministic pink-like test signal: per_band equal tones log-spaced inside every
    third-octave band (none above fmax), fixed random phases; reads the same at every rate."""
    rng = np.random.default_rng(seed)
    t = np.arange(int(round(seconds * fs))) / fs
    m = np.zeros_like(t); s = np.zeros_like(t)
    for b in range(NB):
        for f in np.geomspace(BAND_LO[b], BAND_HI[b], per_band + 2)[1:-1]:
            if f < fmax:
                m += np.sin(2 * np.pi * f * t + rng.uniform(0, 2 * np.pi))
                s += np.sin(2 * np.pi * f * t + rng.uniform(0, 2 * np.pi))
    m /= np.sqrt(np.mean(m * m)); s /= np.sqrt(np.mean(s * s))
    return np.stack([m + width * s, m - width * s], axis=1)


def peaking(x, fs, f0, gain_db, q):
    """RBJ peaking EQ, for test signals only."""
    A = 10 ** (gain_db / 40.0)
    w = 2 * math.pi * f0 / fs
    al = math.sin(w) / (2 * q)
    b = [1 + al * A, -2 * math.cos(w), 1 - al * A]
    a = [1 + al / A, -2 * math.cos(w), 1 - al / A]
    return signal.lfilter(b, a, x, axis=0)


def set_lufs(x, fs, target):
    return x * 10 ** ((target - K.integrated_lufs(x, fs)) / 20.0)
