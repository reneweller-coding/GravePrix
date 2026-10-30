"""Runs --careersim for every difficulty and several seeds at low priority and prints one line per career.

Usage: python tools/careersweep.py [--seeds 5,11,23] [--difficulties 0,1,2,3,4] [--skills 0.85] [--novice 0] [--workers 3]
The logs land in work/careersweep/d<difficulty>_s<seed>_k<skill>.log.
"""
import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXE = ROOT / "bin" / "msvc" / "GravePrix.exe"
OUT = ROOT / "work" / "careersweep"
LOW = getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0)


def run(difficulty, seed, skill, novice):
    log = OUT / f"d{difficulty}_s{seed}_k{skill}_n{novice}.log"
    cmd = [str(EXE), "--careersim", "--difficulty", str(difficulty), "--seed", str(seed), "--skill", str(skill),
           "--novice", str(novice)]
    with open(log, "w", encoding="utf-8") as f:
        subprocess.run(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT, creationflags=LOW, timeout=3600)
    text = log.read_text(encoding="utf-8", errors="replace")
    leagues = re.findall(r"league (\d) done in (\d+) days", text)
    last = [l for l in text.splitlines() if re.match(r"(WON|BANKRUPT|STOPPED) ", l)]
    return difficulty, seed, skill, " ".join(f"L{l}:{d}" for l, d in leagues), last[0] if last else "no result"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", default="5,11,23")
    ap.add_argument("--difficulties", default="0,1,2,3,4")
    ap.add_argument("--skills", default="0.85")
    ap.add_argument("--novice", default="0")
    ap.add_argument("--workers", type=int, default=3)
    a = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    jobs = [(int(d), int(s), k, a.novice) for k in a.skills.split(",") for d in a.difficulties.split(",") for s in a.seeds.split(",")]
    with concurrent.futures.ThreadPoolExecutor(a.workers) as pool:
        results = list(pool.map(lambda j: run(*j), jobs))
    for d, s, k, leagues, last in sorted(results):
        print(f"d{d} s{s:<3} k{k} n{a.novice}  {leagues:<32} {last}")
    sys.stdout.flush()


if __name__ == "__main__":
    main()
