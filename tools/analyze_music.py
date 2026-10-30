"""Lets ACE-Step's planner describe every piece in assets/music (style caption, tempo, key; it "hears" the audio
codes), measures tempo and loudness itself, and writes both beside what tools/music.json asked for:
work/music_analysis.json and a readable work/music_analysis.txt. A machine's ears, not a person's: it tells whether
a piece is roughly what was ordered (genre, tempo, mood, voices where there should be none).

    G:\\Tools\\PythonSkripts\\AudioGenerator\\venv\\Scripts\\python.exe tools\\analyze_music.py

Runs the model once for all files (GPU, ~23 GB): no other GPU job and no game instance beside it.
"""
import glob
import json
import os
import sys
import time

import numpy as np
import soundfile as sf

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AUDIOGEN = r"G:\Tools\PythonSkripts\AudioGenerator"


def tempo(x, fs):
    """The beat from the onset strength's autocorrelation (60-200 BPM)."""
    mono = x.mean(axis=1)
    hop = 512
    frames = len(mono) // hop
    env = np.array([np.sqrt(np.mean(mono[i * hop:(i + 1) * hop] ** 2)) for i in range(frames)])
    onset = np.maximum(0.0, np.diff(env))
    onset -= onset.mean()
    ac = np.correlate(onset, onset, mode="full")[len(onset) - 1:]
    rate = fs / hop
    lo, hi = int(rate * 60 / 200), int(rate * 60 / 60)
    lag = lo + int(np.argmax(ac[lo:hi]))
    return 60.0 * rate / lag


def main():
    sys.path.insert(0, AUDIOGEN)
    os.chdir(AUDIOGEN)
    from core.backends import backend_class
    from core.config import acestep_env, load_config

    spec = json.load(open(os.path.join(ROOT, "tools", "music.json"), encoding="utf-8"))
    asked = {c["id"]: c for c in spec["cues"]}
    cfg = load_config()
    os.environ.update(acestep_env(cfg))
    section = dict(cfg.get("acestep") or {})
    planner = section.get("planner") or "none"
    if planner == "none":
        planner = "acestep-5Hz-lm-4B"
    backend = backend_class("acestep")()
    t = time.time()
    backend.load(section.get("variant"), planner, section, lambda s: None)
    print("model loaded in %.0f s" % (time.time() - t), flush=True)
    results = []
    for path in sorted(glob.glob(os.path.join(ROOT, "assets", "music", "*.ogg"))):
        name = os.path.basename(path)[:-4]
        cue = name[:-2] if name.endswith("_2") else name
        x, fs = sf.read(path, always_2d=True)
        r = {"file": name, "cue": cue, "asked_style": asked.get(cue, {}).get("style", ""),
             "asked_bpm": asked.get(cue, {}).get("bpm", spec.get("bpm")), "asked_key": asked.get(cue, {}).get("key", ""),
             "seconds": round(len(x) / fs, 1), "measured_bpm": round(tempo(x, fs), 1),
             "rms": round(float(np.sqrt((x ** 2).mean())), 3)}
        try:
            a = backend.analyze(path, lambda s: None)
            r.update(heard_style=a.get("style", ""), heard_bpm=a.get("bpm"), heard_key=a.get("key", ""),
                     heard_language=a.get("language", ""), heard_words=(a.get("lyrics_guess") or "")[:200])
        except Exception as e:   # one bad file must not stop the rest
            r["error"] = str(e)
        results.append(r)
        print("%-12s %s" % (name, r.get("heard_style", r.get("error", ""))[:150]), flush=True)
    os.makedirs(os.path.join(ROOT, "work"), exist_ok=True)
    with open(os.path.join(ROOT, "work", "music_analysis.json"), "w", encoding="utf-8") as f:
        json.dump(results, f, indent=1, ensure_ascii=False)
    with open(os.path.join(ROOT, "work", "music_analysis.txt"), "w", encoding="utf-8") as f:
        for r in results:
            f.write("== %s (%.0f s, measured %.0f BPM, asked %s BPM %s)\n" % (r["file"], r["seconds"], r["measured_bpm"],
                                                                            r["asked_bpm"], r["asked_key"]))
            f.write("asked: %s\n" % r["asked_style"])
            f.write("heard: %s\n" % r.get("heard_style", r.get("error", "")))
            f.write("heard bpm %s key %s language %s words %r\n\n" % (r.get("heard_bpm"), r.get("heard_key"),
                                                                       r.get("heard_language"), r.get("heard_words", "")[:80]))
    print("done: work/music_analysis.txt")


if __name__ == "__main__":
    main()
