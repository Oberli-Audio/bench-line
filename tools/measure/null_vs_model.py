#!/usr/bin/env python3
"""G5: the C++ engine (through the harness) against the G2 model, frame by frame.

Same input file to both; compares every frame's gate decision and its tone, side and punch
values, the final medians and region verdicts against a target built by both, and the
target build itself. Prints one JSON object; exits 1 if a threshold is exceeded.

    HARNESS=build/BenchLineHarness_artefacts/Release/BenchLineHarness python3 tools/measure/null_vs_model.py [--rates 48000,96000]
"""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "sound design" / "model"))
sys.path.insert(0, str(ROOT / "scripts" / "dsp-kit"))
import benchline_model as B  # noqa: E402
import dspkit as K  # noqa: E402

HARNESS = os.environ.get("HARNESS", str(ROOT / "build/BenchLineHarness_artefacts/Release/BenchLineHarness"))
TOL_FRAME_DB = 0.05     # per-frame band level, port vs model (F.8: characterised, not bit-exact: different summation order)
TOL_TARGET_DB = 0.02    # percentiles (frames are quantised to 0.01 dB in the port's build)


def harness(args, cwd):
    out = subprocess.run([HARNESS] + args, capture_output=True, text=True, cwd=cwd, env=dict(os.environ, BENCHLINE_REFERENCES=str(cwd)))
    if out.returncode != 0:
        raise RuntimeError(out.stderr)
    return json.loads(out.stdout)


def main():
    rates = [48000, 96000]
    for a in sys.argv[1:]:
        if a.startswith("--rates"):
            rates = [int(x) for x in a.split("=", 1)[1].split(",")]
    res = {}
    worst = 0.0
    with tempfile.TemporaryDirectory() as d:
        d = Path(d)
        for fs in rates:
            # a mix with a gap of digital silence (idle path) and a quiet passage (gate)
            mix = B.set_lufs(B.peaking(B.stereo_pink(14.0, fs, seed=7), fs, 250.0, 3.0, 1.0), fs, -14.0)
            sig = np.concatenate([mix[: 6 * fs], np.zeros((4 * fs, 2)), mix[6 * fs:], 0.003 * mix[: 3 * fs]]).astype(np.float32)
            wav = d / ("mix%d.wav" % fs)
            K.write_wav(str(wav), sig, fs)
            refs = []
            for i in range(3):
                r = B.set_lufs(B.stereo_pink(12.0, fs, seed=100 + i), fs, -14.0).astype(np.float32)
                K.write_wav(str(d / ("ref%d_%d.wav" % (i, fs))), r, fs)
                refs.append((r.astype(np.float64), fs))
            seconds = len(sig) / fs
            out = harness(["--sample-rate=%d" % fs, "--block=512", "--seconds=%.6f" % seconds, "--in=%s" % wav.name,
                           "--build-target=" + ",".join("ref%d_%d.wav" % (i, fs) for i in range(3)), "--target-name=T%d" % fs,
                           "--target-out=T%d.json" % fs, "--no-cache", "--dump-frames=frames%d.json" % fs], d)
            cpp = json.load(open(d / ("frames%d.json" % fs)))
            mod = B.frames(B.analyse(sig.astype(np.float64), fs), final=False)
            n = min(len(cpp), len(mod))
            gate_mismatch = sum(1 for a, b in zip(cpp[:n], mod[:n]) if a["gated"] != b["gated"])
            dmax = {"tone": 0.0, "side": 0.0, "punch": 0.0}
            for a, b in zip(cpp[:n], mod[:n]):
                if a["gated"] and b["gated"]:
                    for k in dmax:
                        dmax[k] = max(dmax[k], float(np.max(np.abs(np.array(a[k]) - b[k]))))
            # targets
            tc = json.load(open(d / ("T%d.json" % fs)))
            tm = B.build_target(refs)
            tdiff = {k: float(np.max(np.abs(np.array(tc[k]) - np.array(tm[k])))) for k in ("tone", "side", "punch")}
            # final medians and verdicts with the model's own target
            tr = B.Tracker(tm)
            for fr in mod:
                tr.push(fr, fr["ready_s"])
            mv = tr.history[-1]["views"]["tone"]
            cv = out["product"]["views"]["tone"]
            med = float(np.nanmax(np.abs(np.array(cv["median"], dtype=float) - mv["median"])))
            verd = [r["verdict"] for r in cv["regions"]] == [r["verdict"] if r else "none" for r in mv["regions"]]
            res[str(fs)] = dict(frames_cpp=len(cpp), frames_model=len(mod), gate_mismatch=gate_mismatch,
                                frame_max_diff_db=dmax, target_max_diff_db=tdiff, median_max_diff_db=med, verdicts_equal=verd,
                                suggestion_cpp=(cv["suggestions"][0]["text"] if cv["suggestions"] else None),
                                suggestion_model=(mv["suggestions"][0]["text"] if mv["suggestions"] else None))
            worst = max(worst, *dmax.values())
            ok = (len(cpp) == len(mod) and gate_mismatch == 0 and max(dmax.values()) <= TOL_FRAME_DB
                  and max(tdiff.values()) <= TOL_TARGET_DB and verd)
            res[str(fs)]["pass"] = bool(ok)
    print(json.dumps(res, indent=1))
    sys.exit(0 if all(v["pass"] for v in res.values()) else 1)


if __name__ == "__main__":
    main()
