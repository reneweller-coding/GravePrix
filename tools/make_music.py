"""Makes the game's music: tools/music.json (the action pieces) and tools/music_calm.json (story and plan, without
the action's driving style, which every cue of a spec inherits) -> the AudioGenerator's soundtrack mode (ACE-Step 1.5,
local on the GPU) -> work/music, then every take levelled to the same loudness and written as OGG Vorbis (quality 4, the format raylib
streams) to assets/music/<cue>.ogg (take 1) and <cue>_2.ogg (take 2; the game picks one of them at random).

    G:\\Tools\\PythonSkripts\\AudioGenerator\\venv\\Scripts\\python.exe tools\\make_music.py [--spec music_calm.json]
        [--encode-only] [--force]

The generator runs below normal priority. Needs numpy, scipy and soundfile (the AudioGenerator's venv has them) and
ffmpeg with libvorbis on the PATH. Before starting it: no other GPU job and no game instance may run (one GPU user at
a time).
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf
from scipy.signal import lfilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPECS = ("music.json", "music_calm.json")
AUDIOGEN = r"G:\Tools\PythonSkripts\AudioGenerator"

# The music sits under the engines and the guns. The garage and the results a little softer.
LUFS = {"garage": -21.0, "results": -21.0, "title": -19.0, "win": -18.5}
LUFS_BOSS = -17.5
LUFS_DEFAULT = -19.5
PEAK_DB = -1.0
FORCE = False


def kweight(x, fs):
    """BS.1770 K-weighting: a high shelf (+4 dB above ~1.7 kHz) and a high pass at ~38 Hz."""
    g, q, fc = 3.999843853973347, 0.7071752369554196, 1681.974450955533
    a = 10.0 ** (g / 40.0)
    w0 = 2.0 * np.pi * fc / fs
    alpha = np.sin(w0) / (2.0 * q)
    c, s = np.cos(w0), 2.0 * np.sqrt(a) * alpha
    b1 = [a * ((a + 1) + (a - 1) * c + s), -2 * a * ((a - 1) + (a + 1) * c), a * ((a + 1) + (a - 1) * c - s)]
    a1 = [(a + 1) - (a - 1) * c + s, 2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - s]
    q, fc = 0.5003270373238773, 38.13547087602444
    w0 = 2.0 * np.pi * fc / fs
    alpha = np.sin(w0) / (2.0 * q)
    b2 = [1.0, -2.0, 1.0]
    a2 = [1.0 + alpha, -2.0 * np.cos(w0), 1.0 - alpha]
    return lfilter(b2, a2, lfilter(b1, a1, x, axis=0), axis=0)


def lufs(x, fs):
    """Integrated loudness (BS.1770-4: 400 ms blocks, 75 % overlap, absolute and relative gate)."""
    y = kweight(x, fs)
    block, hop = int(0.4 * fs), int(0.1 * fs)
    if len(y) < block:
        block = hop = len(y)
    power = np.array([np.mean(y[i:i + block] ** 2, axis=0).sum() for i in range(0, len(y) - block + 1, hop)])
    loud = -0.691 + 10.0 * np.log10(np.maximum(power, 1e-12))
    power = power[loud > -70.0]
    if not len(power):
        return -70.0
    rel = -0.691 + 10.0 * np.log10(power.mean()) - 10.0
    gated = power[-0.691 + 10.0 * np.log10(power) > rel]
    return -0.691 + 10.0 * np.log10(gated.mean())


def peak_db(x):
    return 20.0 * np.log10(max(1e-9, float(np.max(np.abs(x)))))


def encode(data, fs, path, quality):
    """Writes OGG Vorbis through ffmpeg."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fd, tmp = tempfile.mkstemp(suffix=".wav")
    os.close(fd)
    try:
        sf.write(tmp, data.astype(np.float32), fs, subtype="FLOAT")
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", tmp, "-c:a", "libvorbis", "-q:a", str(quality),
                        path], check=True)
    finally:
        os.remove(tmp)


def nice_run(cmd, cwd, log):
    """Runs the generator below normal priority, its output into a log file."""
    env = dict(os.environ, PYTHONUTF8="1")
    flags = 0x00004000 if os.name == "nt" else 0   # BELOW_NORMAL_PRIORITY_CLASS
    with open(log, "w", encoding="utf-8") as out:
        r = subprocess.run(cmd, cwd=cwd, env=env, stdout=out, stderr=subprocess.STDOUT, creationflags=flags)
    if r.returncode:
        sys.exit("generator failed (%d), see %s" % (r.returncode, log))


def target(cue):
    if cue in LUFS:
        return LUFS[cue]
    return LUFS_BOSS if cue.endswith("boss") or cue in ("boss", "final") else LUFS_DEFAULT


def main():
    global FORCE
    ap = argparse.ArgumentParser()
    ap.add_argument("--encode-only", action="store_true", help="only level and encode what work/music holds")
    ap.add_argument("--force", action="store_true", help="encode again even where the OGG is newer")
    ap.add_argument("--spec", action="append", help="generate only this spec of tools/ (repeatable)")
    args = ap.parse_args()
    FORCE = args.force
    work = os.path.join(ROOT, "work", "music")
    os.makedirs(work, exist_ok=True)
    if not args.encode_only:
        for name in args.spec or SPECS:
            nice_run([os.path.join(AUDIOGEN, "venv", "Scripts", "python.exe"), "main.py", "--soundtrack",
                      os.path.join(ROOT, "tools", name), "--soundtrack-out", work], AUDIOGEN,
                     os.path.join(work, "gen_%s.log" % name[:-5]))
    found = sorted(d for d in os.listdir(work) if os.path.isfile(os.path.join(work, d, "manifest.json")))
    if not found:
        sys.exit("no manifest.json under " + work)
    out = os.path.join(ROOT, "assets", "music")
    total = 0
    for src, cue in [(os.path.join(work, d), c) for d in found
                     for c in json.load(open(os.path.join(work, d, "manifest.json"), encoding="utf-8"))["cues"]]:
        files = cue["levels"][0]["files"]
        shown = []
        for n, f in enumerate(files[:2]):
            path = os.path.join(out, "%s%s.ogg" % (cue["id"], "" if n == 0 else "_%d" % (n + 1)))
            source = os.path.join(src, f)
            if not FORCE and os.path.isfile(path) and os.path.getmtime(path) >= os.path.getmtime(source):
                total += os.path.getsize(path)
                shown.append("t%d =" % (n + 1))
                continue
            x, fs = sf.read(source, dtype="float32", always_2d=True)
            # The loop's seam: where the end does not meet the start, a 3 ms dip on both sides instead of a click.
            if float(np.max(np.abs(x[-1] - x[0]))) > 0.01:
                n = int(0.003 * fs)
                ramp = (0.5 - 0.5 * np.cos(np.linspace(0.0, np.pi, n)))[:, None].astype(np.float32)
                x = x.copy()
                x[:n] *= ramp
                x[-n:] *= ramp[::-1]
            gain = min(target(cue["id"]) - lufs(x, fs), PEAK_DB - peak_db(x))
            encode(x * 10.0 ** (gain / 20.0), fs, path, 4)
            total += os.path.getsize(path)
            shown.append("t%d %+.1f dB" % (n + 1, gain))
        print("%-10s %s" % (cue["id"], ", ".join(shown)))
    print("music: %.1f MB in %s" % (total / 1e6, out))


if __name__ == "__main__":
    main()
