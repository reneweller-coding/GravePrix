"""Runs --careersim for every difficulty and several seeds at low priority and prints one line per career.

Usage: python tools/careersweep.py [--seeds 5,11,23] [--difficulties 0,1,2,3,4] [--skills 0.85] [--novice 0] [--hands 0]
                                   [--assist 1] [--days 160] [--workers 3] [--exe COPY] [--tag NAME]
The logs land in work/careersweep/d<difficulty>_s<seed>_k<skill>_n<novice>_h<hands>_a<assist>[_<tag>].log (a tag keeps
two sweeps at once apart). Each line ends with the player's average place per league (from the races of that league).
"""
import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXE = ROOT / "bin" / "msvc" / "GravePrix.exe"
EXE_RUN = [EXE]
DAYS = ["160"]
TAG = [""]
OUT = ROOT / "work" / "careersweep"
LOW = getattr(subprocess, "BELOW_NORMAL_PRIORITY_CLASS", 0)


def run(difficulty, seed, skill, novice, assist="1", hands="0"):
    log = OUT / f"d{difficulty}_s{seed}_k{skill}_n{novice}_h{hands}_a{assist}{TAG[0]}.log"
    cmd = [str(EXE_RUN[0]), "--careersim", "--difficulty", str(difficulty), "--seed", str(seed), "--skill", str(skill),
           "--novice", str(novice), "--assist", str(assist), "--hands", str(hands), "--days", DAYS[0]]
    with open(log, "w", encoding="utf-8") as f:
        subprocess.run(cmd, cwd=ROOT, stdout=f, stderr=subprocess.STDOUT, creationflags=LOW, timeout=3600)
    text = log.read_text(encoding="utf-8", errors="replace")
    leagues = re.findall(r"league (\d) done in (\d+) days", text)
    last = [l for l in text.splitlines() if re.match(r"(WON|BANKRUPT|STOPPED) ", l)]
    places = {}
    for m in re.finditer(r"^\s*\d+\s+(\d)\s.*? place (\d+)", text, re.M):
        places.setdefault(m.group(1), []).append(int(m.group(2)))
    avg = " ".join(f"P{l}:{sum(p) / len(p):.1f}" for l, p in sorted(places.items(), reverse=True))
    finals = len(re.findall(r"^\s*\d+\s+0\s.*SHOWDOWN", text, re.M))   # the races against Graves
    avg += f" finals:{finals}"
    return difficulty, seed, skill, " ".join(f"L{l}:{d}" for l, d in leagues), (last[0] if last else "no result") + "  " + avg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", default="5,11,23")
    ap.add_argument("--difficulties", default="0,1,2,3,4")
    ap.add_argument("--skills", default="0.85")
    ap.add_argument("--novice", default="0")
    ap.add_argument("--hands", default="0", help="the test driver's person's hands (0..1, World::hands)")
    ap.add_argument("--assist", default="1", help="the driving assist for the player's car (0 off, 1 on, 2 brake help)")
    ap.add_argument("--workers", type=int, default=3)
    ap.add_argument("--exe", default="", help="a copy of the game to run (so a build can replace bin/msvc meanwhile)")
    ap.add_argument("--days", default="160", help="the longest career")
    ap.add_argument("--tag", default="", help="added to the log names")
    a = ap.parse_args()
    DAYS[0] = a.days
    TAG[0] = "_" + a.tag if a.tag else ""
    if a.exe:
        EXE_RUN[0] = pathlib.Path(a.exe)
    OUT.mkdir(parents=True, exist_ok=True)
    jobs = [(int(d), int(s), k, a.novice, a.assist, a.hands) for k in a.skills.split(",") for d in a.difficulties.split(",") for s in a.seeds.split(",")]
    with concurrent.futures.ThreadPoolExecutor(a.workers) as pool:
        results = list(pool.map(lambda j: run(*j), jobs))
    for d, s, k, leagues, last in sorted(results):
        print(f"d{d} s{s:<3} k{k} n{a.novice} h{a.hands} a{a.assist}  {leagues:<32} {last}")
    sys.stdout.flush()


if __name__ == "__main__":
    main()
