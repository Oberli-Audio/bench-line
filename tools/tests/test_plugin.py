#!/usr/bin/env python3
"""Project test scaffold - copy to <project>/tools/tests/ and fill in the TODOs.

Drives the product's offline renderer (harness-contract.md v3) and asserts on its JSON and
WAVs with dspkit. Run directly, or register with CTest (snippet at the bottom):

    HARNESS=build/MyPluginHarness python3 -m unittest test_template -v
    HARNESS=... python3 test_template.py --label fast     # the inner loop (D.3.6: under 60 s)

Labels (D.3.1): fast = contract, rate/block matrix, health, properties (every build, < 60 s);
render = goldens and presence tests (every commit); the host validators are not here
(validate_plugin.py). Every test is a rule or a DoD line from the core card; the name says
which. Per-test seconds are printed; a `fast` test over FAST_BUDGET_S fails.
"""
import json
import os
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, os.environ.get("DSPKIT", str(Path(__file__).resolve().parent)))
import dspkit as K  # noqa: E402

HARNESS = os.environ.get("HARNESS", "build/BenchLineHarness_artefacts/Release/BenchLineHarness")
GOLDEN_DIR = Path(os.environ.get("GOLDEN_DIR", "tools/tests/golden"))   # committed goldens + sidecars
GOLDEN_ARCH = os.environ.get("GOLDEN_ARCH", "x86_64")                # D.2.1: the slice the host runs
DEFAULT = dict(seed=1, sample_rate=48000, block=512, seconds=2.0)
SOURCE = dict(signal="impulse")
PARAM = ("average", 0.25)            # one real parameter ID and a non-default value
RATES = (44100, 48000, 88200, 96000, 176400, 192000)
BLOCKS = (0, 1, 13, 32, 64, 512, 4096)
INSTRUMENT = "notes" in SOURCE
FAST_BUDGET_S = float(os.environ.get("FAST_BUDGET_S", "20"))   # one test; the whole `fast` label stays under 60 s
STATES = Path(__file__).resolve().parent / "states"   # one frozen session state per released version (rule 19)
# F.8 tolerances (peak of the residual, absolute dBFS, no alignment): same machine and
# compiler bit-exact; across slices feed-forward -100, feedback tails -80.
GOLDEN_PEAK_DB = {"same": -140.0, "cross-feedforward": -100.0, "cross-feedback": -80.0}


def run(params=None, **flags):
    f = dict(DEFAULT, **SOURCE)
    f.update(flags)
    res = K.render(HARNESS, params, **f)
    echo = {k: v for k, v in f.items() if k in ("seed", "sample_rate", "block", "seconds", "preset")
            and not (k == "block" and str(v).startswith("random:"))}
    K.assert_echo(res, params, **echo)                                     # rule 13
    return res


class Timed(unittest.TestCase):
    label = "fast"

    def setUp(self):
        self._t0 = time.time()

    def tearDown(self):
        dt = time.time() - self._t0
        print("  [%s %.1f s]" % (self.label, dt), end="", flush=True)
        if self.label == "fast" and dt > FAST_BUDGET_S:
            self.fail("fast test took %.0f s; budget %.0f s (D.3.6: move it to a slower tier, do not delete it)" % (dt, FAST_BUDGET_S))


class Contract(Timed):
    def test_harness_echoes_params(self):                                  # rule 13
        pid, val = PARAM
        res = run({pid: val})
        self.assertAlmostEqual(res["applied"]["params"][pid], val, places=6)

    def test_seeded_runs_are_bit_identical(self):                          # DoD: reset + seed
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d) / "a.wav", Path(d) / "b.wav"
            run(out=a)
            run(out=b)
            xa, _ = K.read_wav(a)
            xb, _ = K.read_wav(b)
            self.assertTrue((xa == xb).all(), "two seeded renders differ")


class RateAndBlock(Timed):
    def test_rate_block_matrix_is_clean(self):                             # DoD: 42 combinations
        for fs in RATES:
            for blk in [b for b in BLOCKS if b > 0]:                       # 0-length blocks: random:<seed> (the contract)
                with self.subTest(fs=fs, block=blk):
                    res = run(sample_rate=fs, block=blk, seconds=0.25)
                    self.assertEqual(res["health"]["nan"] + res["health"]["inf"], 0)
                    self.assertLessEqual(res["levels"]["peak_dbfs"], 0.0)   # a wire: the 0 dBFS impulse comes out as it went in

    def test_random_block_sizes_from_zero(self):                           # D.5.1, fuzz
        res = run(block="random:7", max_block=1024, seconds=0.5)
        self.assertEqual(res["health"]["nan"], 0)

    def test_block_larger_than_announced(self):                            # D.5.1
        # NaN-free is not enough: a plugin that renders only the announced part of an oversized
        # block leaves a silent gap. Compare, window by window, against the same render in
        # announced-size blocks (seen once: a 6 dB drop in one window, whole-file
        # RMS within 1 dB).
        with tempfile.TemporaryDirectory() as d:
            a, b = Path(d) / "ref.wav", Path(d) / "big.wav"
            run(block=256, max_block=256, seconds=0.5, out=a)
            res = run(block=1024, max_block=256, seconds=0.5, out=b)
            self.assertEqual(res["health"]["nan"], 0)
            xa, fs = K.read_wav(a)
            xb, _ = K.read_wav(b)
            self.assertLess(K.dropout_db(xa, xb, fs), 3.0, "an oversized block rendered quieter than its announced-size twin")


class Health(Timed):
    def test_silence_after_notes_has_no_denormal_storm_or_dc(self):        # DoD: silence test, 48 k/64
        flags = dict(seconds=10.0, profile=1, sample_rate=48000, block=64, warmup=200)
        if INSTRUMENT:
            flags["gate"] = 0.5                                             # notes off early, the rest silent
        res = run(**flags)
        self.assertLess(res["levels"]["dc_dbfs"], -80.0)
        self.assertLess(res["profile"]["p999_pct"], 25.0)                  # rule 20: p99.9 judged
        print("over_deadline (reported, not judged, rule 20):", res["profile"]["over_deadline"])

    def test_latency_matches_impulse(self):                                # rule 7
        if INSTRUMENT:
            self.skipTest("effects only")
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "imp.wav"
            res = run(signal="impulse", out=out)
            x, fs = K.read_wav(out)
            ref = x[:, :1] * 0.0
            ref[0, 0] = 0.5
            measured = K.latency_samples(ref, x[:, :1])
            self.assertEqual(measured, res["applied"]["latency_samples"])

    def test_tail_is_silent_after_reported_tail(self):                     # D.2.5, D.5.1
        if not INSTRUMENT:
            self.skipTest("instruments; effects feed an impulse instead")
        with tempfile.TemporaryDirectory() as d:
            out = Path(d) / "tail.wav"
            res = run(seconds=6.0, gate=1.0, out=out)
            x, fs = K.read_wav(out)
            after = x[int((1.0 + res["applied"]["tail_seconds"]) * fs):]
            self.assertLess(K.peak_dbfs(after) if after.size else -400.0, -90.0)


class Properties(Timed):
    def test_state_round_trip_is_bytewise(self):                           # D.5.3
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            pid, val = PARAM
            run({pid: val}, state_out=d / "a.state", out=d / "a.wav", seconds=0.5)
            rb = run(state_in=d / "a.state", state_out=d / "b.state", out=d / "b.wav", seconds=0.5)
            self.assertEqual((d / "a.state").read_bytes(), (d / "b.state").read_bytes())
            self.assertAlmostEqual(rb["applied"]["params"][pid], val, places=6)
            xa, _ = K.read_wav(d / "a.wav")
            xb, _ = K.read_wav(d / "b.wav")
            self.assertTrue((xa == xb).all())

    def test_corrupted_state_does_not_crash(self):                         # D.5.3, fuzz
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            run(state_out=d / "good.state", seconds=0.25)
            good = (d / "good.state").read_bytes()
            cases = {"empty": b"", "truncated": good[: len(good) // 2], "garbage": bytes(range(256)) * 4,
                     "flipped": bytes(b ^ 0x55 for b in good)}
            for name, blob in cases.items():
                with self.subTest(case=name):
                    (d / "bad.state").write_bytes(blob)
                    try:
                        res = K.render(HARNESS, state_in=d / "bad.state", seconds=0.25, **{k: v for k, v in DEFAULT.items() if k != "seconds"}, **SOURCE)
                    except RuntimeError as e:
                        # a clean refusal (non-zero exit with a message) is acceptable; a crash is not
                        self.assertNotIn("Segmentation", str(e))
                        self.assertNotIn("-11", str(e).split(")")[0])
                        continue
                    self.assertEqual(res["health"]["nan"], 0)


    def test_state_from_every_release_loads(self):                         # rule 19, D.5: tools/tests/states/<version>.state
        # one session per released version, frozen when it shipped: View Punch, Average 36 s,
        # Tolerance 75 %, Hold on (31 frozen medians) and the built-in slope target
        expected = {"0.1.0": {"view": 0.5, "average": 0.6, "tolerance": 0.625, "hold": 1.0}}
        held_tone = {"0.1.0": {0: -19.6300, 15: -19.0059, 30: -18.6754}}   # dB, bands 20 Hz, 630 Hz, 20 kHz as frozen
        for version, params in expected.items():
            with self.subTest(version=version):
                res = run(state_in=STATES / ("%s.state" % version), seconds=0.25)
                for pid, val in params.items():
                    self.assertAlmostEqual(res["applied"]["params"][pid], val, places=5, msg="%s: %s" % (version, pid))
                hold = res["product"]["hold"]
                self.assertTrue(hold["active"], "%s: Hold did not come back" % version)
                self.assertEqual(len(hold["tone"]), 31)
                self.assertTrue(all(v is not None and np.isfinite(v) for v in hold["tone"]), "%s: the frozen medians did not come back" % version)
                for band, db in held_tone[version].items():
                    self.assertAlmostEqual(hold["tone"][band], db, delta=0.01, msg="%s: frozen median, band %d" % (version, band))
                self.assertEqual(res["product"]["target"], "Plain slope")


class Goldens(Timed):
    """F.13.6: a golden per defining feature, rendered on GOLDEN_ARCH, with a sidecar."""
    label = "render"

    def test_goldens_match(self):
        sidecars = sorted(GOLDEN_DIR.glob("*.json")) if GOLDEN_DIR.exists() else []
        if not sidecars:
            self.skipTest("no goldens in %s yet (render them with the harness, write <name>.json beside each)" % GOLDEN_DIR)
        for sc in sidecars:
            meta = json.loads(sc.read_text())
            with self.subTest(golden=sc.stem), tempfile.TemporaryDirectory() as d:
                out = Path(d) / "now.wav"
                flags = dict(meta["flags"])
                flags["out"] = out
                res = K.render(HARNESS, meta.get("params"), **flags)
                arch = res["applied"].get("build", {}).get("arch")
                kind = "same" if arch == meta.get("arch") else ("cross-feedback" if meta.get("feedback") else "cross-feedforward")
                now, fs = K.read_wav(out)
                gold, _ = K.read_wav(sc.with_suffix(".wav"))
                r = K.null_residual(gold, now, align=False)                   # a latency change must fail
                self.assertLess(r["peak_dbfs"], GOLDEN_PEAK_DB[kind],
                                "%s: residual against the golden (%s) - regenerate only with a changelog line (D.3.5)" % (sc.stem, kind))


# ---------------------------------------------------------------------------------------------
# Bench Line: the wire and the defining features (brief.md, rule 13). Test signals come from the
# G2 model ("sound design/model/benchline_model.py"), so the port is tested on the model's inputs.
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sound design" / "model"))
import numpy as np  # noqa: E402
import benchline_model as BM  # noqa: E402

_REFS = tempfile.mkdtemp(prefix="benchline-refs-")
os.environ["BENCHLINE_REFERENCES"] = _REFS        # never the user's folder


def wav(path, x, fs):
    K.write_wav(str(path), np.asarray(x, dtype=np.float32), fs)
    return str(path)


def bench(params=None, **flags):
    """A render on an input file with the product flags; no contract echo of the file flags."""
    f = dict(seed=1, block=512)
    f.update(flags)
    return K.render(HARNESS, params, **f)


_PINK_TARGET = {}


def pink_target(fs=48000):
    """Six pink-noise 'reference songs' (30 s each) built into a target by the plugin."""
    if fs not in _PINK_TARGET:
        d = Path(tempfile.mkdtemp(prefix="benchline-t-"))
        files = [wav(d / ("ref%d.wav" % i), BM.set_lufs(BM.stereo_pink(30.0, fs, seed=100 + i), fs, -14.0), fs) for i in range(6)]
        out = d / "pink.json"
        res = bench(sample_rate=fs, seconds=0.2, signal="silence", build_target=",".join(files), target_name="Pink", target_out=out)
        assert res["product"]["build"]["ok"], res["product"]["build"]
        _PINK_TARGET[fs] = (str(out), res["product"]["build"])
    return _PINK_TARGET[fs][0]


class Wire(Timed):
    """Structural decision 1: the output is the input, bit for bit, in every state."""

    def test_output_is_bit_identical_in_every_state(self):
        states = [{}, {"view": 0.5}, {"view": 1.0}, {"hold": 1.0}, {"tolerance": 1.0}, {"average": 1.0}, {"bypass": 1.0}]
        with tempfile.TemporaryDirectory() as d:
            for fs in RATES:
                x = BM.stereo_pink(0.6, fs, seed=fs) * 0.3
                x[: fs // 10] = 0.0
                src = wav(Path(d) / ("in%d.wav" % fs), x, fs)
                ref, _ = K.read_wav(src)
                for i, st in enumerate(states):
                    blk = ("random:%d" % (fs + i)) if i % 2 else [b for b in BLOCKS if b > 1][(i + fs) % (len(BLOCKS) - 2)]
                    with self.subTest(fs=fs, state=st, block=blk):
                        out = Path(d) / "out.wav"
                        bench(st, sample_rate=fs, block=blk, max_block=4096, seconds=0.6, target="slope", out=out, **{"in": src})
                        y, _ = K.read_wav(out)
                        self.assertTrue(np.array_equal(y[: len(ref)], ref), "output differs from the input")

    def test_latency_is_zero(self):                                        # rule 7
        res = bench(signal="impulse", seconds=0.1)
        self.assertEqual(res["applied"]["latency_samples"], 0)

    def test_malloc_trap_is_live(self):                                    # rule 1: the real-time check itself
        # the RTSan build (preset rtsan) carries RealtimeSanitizer instead of the trap: test the one it has
        import subprocess
        check = bench(seconds=0.01)["applied"]["build"].get("rt_check", "malloc-trap")
        selftest, marker = ("PRODUCT_RTSAN_SELFTEST", "RealtimeSanitizer") if check == "rtsan" else ("PRODUCT_MALLOC_TRAP_SELFTEST", "malloc trap")
        r = subprocess.run([HARNESS, "--seconds=0.05"], env=dict(os.environ, **{selftest: "1"}), capture_output=True, text=True)
        if sys.platform.startswith("linux") or sys.platform == "darwin":
            self.assertNotEqual(r.returncode, 0, "the self-test allocation did not abort: the %s is not armed" % check)
            self.assertIn(marker, r.stderr)

    def test_editor_reads_do_not_wait_for_a_draining_worker(self):         # D.5, pluginval "Open editor whilst processing"
        # Audio pushed faster than real time kept the worker's drain loop (and its lock) busy, so
        # the editor's paint waited on it until pluginval's 1 min timeout (docs/failures.md).
        import subprocess, json
        r = subprocess.run([HARNESS, "--lock-wait=2"], capture_output=True, text=True, timeout=60)
        self.assertEqual(r.returncode, 0, r.stderr[-400:])
        res = json.loads(r.stdout)["lock_wait"]
        self.assertGreater(res["reads"], 10)
        self.assertLess(res["max_wait_ms"], 100.0, "an editor read waited %.0f ms for the worker" % res["max_wait_ms"])


class DefiningFeatures(Timed):
    """Rule 13: one presence test per defining feature in brief.md. Each was shown failing on an
    input without the feature (docs/status.md, G3 lines)."""
    label = "render"

    def _mix(self, d, fs=48000, bump=3.0, lufs=-14.0, seconds=20.0, name="mix.wav"):
        x = BM.stereo_pink(seconds, fs, seed=7)
        if bump:
            x = BM.peaking(x, fs, 250.0, bump, 1.0)
        return wav(Path(d) / name, BM.set_lufs(x, fs, lufs), fs)

    def test_detection_flat_mix_reads_inside(self):
        with tempfile.TemporaryDirectory() as d:
            res = bench(seconds=20.0, target=pink_target(), **{"in": self._mix(d, bump=0.0)})
            self.assertEqual([r["verdict"] for r in res["product"]["views"]["tone"]["regions"]], ["inside"] * 6)

    def test_detection_250hz_bump_reads_lowmid_outside_within_10s(self):
        with tempfile.TemporaryDirectory() as d:
            res = bench(seconds=20.0, target=pink_target(), **{"in": self._mix(d)})
            first = res["product"]["first_outside_s"].get("tone/Low-mid/above")
            self.assertIsNotNone(first, "Low-mid never read above the band")
            self.assertLessEqual(first, 10.0)
            self.assertEqual(res["product"]["views"]["tone"]["regions"][2]["verdict"], "above")

    def test_suggestion_names_250hz_and_about_minus_2db(self):
        with tempfile.TemporaryDirectory() as d:
            res = bench(seconds=20.0, target=pink_target(), **{"in": self._mix(d)})
            s = res["product"]["views"]["tone"].get("first_suggestion")
            self.assertIsNotNone(s, "no suggestion")
            self.assertEqual(s["region"], "Low-mid")
            self.assertTrue(250 / 2 ** (1 / 3) - 1 <= s["hz"] <= 250 * 2 ** (1 / 3) + 1, s)
            self.assertTrue(-2.5 <= s["gain_db"] <= -1.5, s)

    def test_loudness_independence(self):
        with tempfile.TemporaryDirectory() as d:
            a = bench(seconds=15.0, target=pink_target(), **{"in": self._mix(d, lufs=-8.0, seconds=15.0, name="a.wav")})
            b = bench(seconds=15.0, target=pink_target(), **{"in": self._mix(d, lufs=-16.0, seconds=15.0, name="b.wav")})
            ma = np.array(a["product"]["views"]["tone"]["median"], dtype=float)
            mb = np.array(b["product"]["views"]["tone"]["median"], dtype=float)
            self.assertLessEqual(float(np.max(np.abs(ma - mb))), 0.2)

    def test_gate_quiet_tail_does_not_move_the_curve(self):
        from scipy import signal as sg
        fs = 48000
        with tempfile.TemporaryDirectory() as d:
            mix = BM.set_lufs(BM.stereo_pink(15.0, fs, seed=7), fs, -14.0)
            q = sg.sosfilt(sg.butter(4, 500, fs=fs, output="sos"), BM.stereo_pink(10.0, fs, seed=9), axis=0)
            a = bench(seconds=25.0, target=pink_target(), **{"in": wav(Path(d) / "a.wav", np.concatenate([mix, np.zeros((10 * fs, 2))]), fs)})
            b = bench(seconds=25.0, target=pink_target(), **{"in": wav(Path(d) / "b.wav", np.concatenate([mix, BM.set_lufs(q, fs, -60.0)]), fs)})
            ma = np.array(a["product"]["views"]["tone"]["median"], dtype=float)
            mb = np.array(b["product"]["views"]["tone"]["median"], dtype=float)
            self.assertLess(float(np.max(np.abs(ma - mb))), 0.1)

    def test_punch_compressed_mix_reads_squashed(self):
        fs = 48000
        with tempfile.TemporaryDirectory() as d:
            # the references: pink noise with a 4 Hz amplitude pulse (punchy); the mix: plain pink (flat envelope)
            t = np.arange(20 * fs) / fs
            env = (0.25 + np.maximum(0, np.sin(2 * np.pi * 2 * t)) ** 8)[:, None]
            refs = [wav(Path(d) / ("p%d.wav" % i), BM.set_lufs(BM.stereo_pink(20.0, fs, seed=200 + i) * env, fs, -14.0), fs) for i in range(3)]
            mix = self._mix(d, bump=0.0, seconds=15.0)
            res = bench(seconds=15.0, build_target=",".join(refs), target_name="Punchy", target_out=Path(d) / "p.json", **{"in": mix})
            verdicts = [r["verdict"] for r in res["product"]["views"]["punch"]["regions"]]
            self.assertIn("below", verdicts, verdicts)
            self.assertNotIn("above", verdicts, verdicts)

    def test_side_bass_moved_to_the_side_reads_outside_in_side_view_only(self):
        """The references carry their bass in the mid; the mix has the same bass in the side.
        Per-channel power is unchanged (L^2 + R^2 = 2 (M^2 + S^2)), so Tone stays inside and only
        Side reads Sub/Low above (brief amendment 2026-11-01)."""
        from scipy import signal as sg
        fs = 48000
        lp = sg.butter(4, 120, fs=fs, output="sos")
        def song(seed, side_bass, seconds=15.0):
            x = BM.stereo_pink(seconds, fs, seed=seed)
            b = 2.0 * sg.sosfilt(lp, BM.pink(int(seconds * fs), np.random.default_rng(seed + 1)))
            y = x.copy()
            y[:, 0] += b
            y[:, 1] += -b if side_bass else b
            return BM.set_lufs(y, fs, -14.0)
        with tempfile.TemporaryDirectory() as d:
            refs = [wav(Path(d) / ("r%d.wav" % i), song(400 + i, False), fs) for i in range(3)]
            res = bench(seconds=15.0, build_target=",".join(refs), target_name="MidBass", target_out=Path(d) / "m.json",
                        **{"in": wav(Path(d) / "s.wav", song(7, True), fs)})
            side = [r["verdict"] for r in res["product"]["views"]["side"]["regions"]]
            tone = [r["verdict"] for r in res["product"]["views"]["tone"]["regions"]]
            self.assertTrue(side[0] == "above" or side[1] == "above", side)
            self.assertEqual(tone[:2], ["inside", "inside"], tone)

    def test_tolerance_moves_the_band_not_the_curve(self):
        with tempfile.TemporaryDirectory() as d:
            mix = self._mix(d, bump=1.5, seconds=12.0)
            narrow = bench({"tolerance": 0.0}, seconds=12.0, target=pink_target(), **{"in": mix})
            wide = bench({"tolerance": 1.0}, seconds=12.0, target=pink_target(), **{"in": mix})
            pn, pw = narrow["product"], wide["product"]
            self.assertEqual(pn["views"]["tone"]["median"], pw["views"]["tone"]["median"])
            self.assertEqual(pn["frames_analysed"], pw["frames_analysed"])
            wn = np.array(pn["views"]["tone"]["hi"]) - np.array(pn["views"]["tone"]["lo"])
            ww = np.array(pw["views"]["tone"]["hi"]) - np.array(pw["views"]["tone"]["lo"])
            self.assertTrue(np.all(ww > wn))

    def test_plain_slope_reads_slope_noise_inside(self):
        fs = 48000
        rng = np.random.default_rng(21)
        n = 15 * fs
        X = np.fft.rfft(rng.standard_normal(n)); f = np.fft.rfftfreq(n, 1 / fs); f[0] = 1; X /= f ** 0.75; X[0] = 0
        y = np.fft.irfft(X, n); y /= np.std(y) * 8
        with tempfile.TemporaryDirectory() as d:
            res = bench(seconds=15.0, target="slope", **{"in": wav(Path(d) / "s.wav", np.stack([y, y], 1), fs)})
            self.assertEqual([r["verdict"] for r in res["product"]["views"]["tone"]["regions"]], ["inside"] * 6)

    def test_hold_survives_the_session(self):
        with tempfile.TemporaryDirectory() as d:
            mix = self._mix(d, bump=0.0, seconds=6.0)
            a = bench({"hold": 1.0}, seconds=6.0, target=pink_target(), state_out=Path(d) / "s.state", **{"in": mix})
            b = bench(seconds=0.5, state_in=Path(d) / "s.state", signal="silence")
            self.assertTrue(b["product"]["hold"]["active"])
            self.assertEqual(b["product"]["target"], "Pink")


class Standards(Timed):
    label = "render"

    def test_ebu3341_short_term_at_every_rate(self):                       # rule 17
        with tempfile.TemporaryDirectory() as d:
            for fs in RATES:
                t = np.arange(int(9 * fs)) / fs
                for name, amp, want in (("c1", np.full_like(t, 10 ** (-23 / 20)), -23.0), ("c2", np.full_like(t, 10 ** (-33 / 20)), -33.0),
                                        ("c9", np.where(np.mod(t, 3.0) < 1.34, 10 ** (-20 / 20), 10 ** (-30 / 20)), -23.0)):
                    s = amp * np.sin(2 * np.pi * 1000 * t)
                    res = bench(sample_rate=fs, seconds=9.0, **{"in": wav(Path(d) / "e.wav", np.stack([s, s], 1), fs)})
                    with self.subTest(fs=fs, case=name):
                        self.assertLess(abs(res["product"]["short_term_lufs"] - want), 0.1)

    def test_build_speed_and_reload(self):
        fs = 44100
        with tempfile.TemporaryDirectory() as d:
            files = [wav(Path(d) / ("song%d.wav" % i), BM.set_lufs(BM.stereo_pink(60.0, fs, seed=300 + i), fs, -12.0), fs) for i in range(6)]
            res = bench(sample_rate=fs, seconds=0.2, signal="silence", build_target=",".join(files), target_out=Path(d) / "t.json", no_cache=1)
            b = res["product"]["build"]
            speed = 360.0 / b["seconds"]                                        # seconds of audio per second
            print(" build: 6 x 60 s in %.2f s = %.0fx realtime (24 min in 20 s needs 72x)" % (b["seconds"], speed), end="")
            self.assertGreater(speed, 72.0)
            # the written file reloads identically: building again from the cache gives the same bytes
            first = (Path(d) / "t.json").read_bytes()
            bench(sample_rate=fs, seconds=0.2, signal="silence", build_target=",".join(files), target_out=Path(d) / "t2.json")
            bench(sample_rate=fs, seconds=0.2, signal="silence", build_target=",".join(files), target_out=Path(d) / "t3.json")
            self.assertEqual((Path(d) / "t2.json").read_bytes(), (Path(d) / "t3.json").read_bytes(), "cached build differs")
            self.assertEqual(first, (Path(d) / "t2.json").read_bytes(), "fresh and cached builds differ")


# CTest registration (CMakeLists.txt of the project):
#   add_test(NAME py-fast   COMMAND ${Python3_EXECUTABLE} tools/tests/test_template.py --label fast)
#   add_test(NAME py-render COMMAND ${Python3_EXECUTABLE} tools/tests/test_template.py --label render)
#   set_tests_properties(py-fast PROPERTIES LABELS "fast" TIMEOUT 60 ENVIRONMENT "HARNESS=$<TARGET_FILE:MyPluginHarness>")
#   set_tests_properties(py-render PROPERTIES LABELS "render" TIMEOUT 600 ENVIRONMENT "HARNESS=$<TARGET_FILE:MyPluginHarness>")
# Then: ctest -L fast (inner loop), ctest -L render, ctest -LE validation.

if __name__ == "__main__":
    label = None
    if "--label" in sys.argv:
        i = sys.argv.index("--label")
        label = sys.argv[i + 1]
        del sys.argv[i:i + 2]
    if label:
        loader = unittest.TestLoader()
        suite = unittest.TestSuite()
        for cls in (Contract, RateAndBlock, Health, Properties, Wire, Goldens, DefiningFeatures, Standards):
            if cls.label == label:
                suite.addTests(loader.loadTestsFromTestCase(cls))
        t0 = time.time()
        result = unittest.TextTestRunner(verbosity=2).run(suite)
        dt = time.time() - t0
        print("label %s: %.0f s" % (label, dt))
        sys.exit(0 if result.wasSuccessful() and (label != "fast" or dt < 3.0 * FAST_BUDGET_S) else 1)
    unittest.main(verbosity=2)
