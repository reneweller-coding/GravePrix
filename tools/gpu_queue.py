"""Runs one GPU job when the GPU is free (the user's rule: one GPU user at a time across all sessions): waits until no
game instance (KipKepler.exe, SpaceOpera.exe, DarkConcordance2 builds) has run for 60 s and no
G:/Tools/Games/GPU_PAUSE.txt exists, then writes G:/Tools/Games/GPU_BELEGT.txt, runs the command below normal
priority and deletes the flag afterwards.

    python tools/gpu_queue.py <command> [args...]
"""
import os
import subprocess
import sys
import time

FLAG = r"G:\Tools\Games\GPU_BELEGT.txt"
PAUSE = r"G:\Tools\Games\GPU_PAUSE.txt"
GAMES = ("kipkepler.exe", "spaceopera.exe", "darkconcordance.exe", "darkconcordance2.exe", "graveprix.exe")


def games_running():
    out = subprocess.run(["tasklist"], capture_output=True, text=True, errors="replace",
                         creationflags=0x08000000 if os.name == "nt" else 0).stdout.lower()   # CREATE_NO_WINDOW
    return any(g in out for g in GAMES)


def main():
    cmd = sys.argv[1:]
    quiet_since = None
    while True:
        busy = games_running() or os.path.exists(PAUSE) or os.path.exists(FLAG)
        if busy:
            quiet_since = None
        elif quiet_since is None:
            quiet_since = time.time()
        elif time.time() - quiet_since >= 60:
            # The flag made atomically: of two queues that saw the GPU free at once, only one goes.
            try:
                fd = os.open(FLAG, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            except FileExistsError:
                quiet_since = None
                continue
            break
        time.sleep(5)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write("Grave Prix: %s, started %s\n" % (" ".join(os.path.basename(c) for c in cmd[:3]), time.strftime("%H:%M")))
    print("GPU free, starting", flush=True)
    try:
        flags = 0x08004000 if os.name == "nt" else 0   # BELOW_NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW
        r = subprocess.run(cmd, creationflags=flags)
        sys.exit(r.returncode)
    finally:
        os.remove(FLAG)


if __name__ == "__main__":
    main()
