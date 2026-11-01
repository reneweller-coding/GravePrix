"""Adds the estate's garden to data/models/props_estate.txt (between the markers, so it can run again): a hedge maze,
the pool with the sunken hearse, the greenhouse, a gazebo, a weeping angel, topiary skulls and the gnome army the
estate scenes use (tools/make_scenes.py).

    python tools/props_estate_v2.py
"""
import os
import random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(ROOT, "data", "models", "props_estate.txt")
BEGIN = "# --- the garden (tools/props_estate_v2.py) ---"
END = "# --- end of the garden ---"
HEDGE, HEDGE_TOP = "#1e3a1e", "#24442a"


def maze():
    # A 7 x 7 maze of 2 m cells (the recursive backtracker, a fixed seed), hedges 0.7 m thick and 1.8 m high; the way
    # in faces the road (-z), a reaper stands in the middle.
    n, cell = 7, 2.0
    r = random.Random(7)
    seen = [[False] * n for _ in range(n)]
    # walls: h[y][x] the wall south (-z) of cell (x, y) for y in 0..n; v[y][x] the wall west of (x, y) for x in 0..n
    h = [[True] * n for _ in range(n + 1)]
    v = [[True] * (n + 1) for _ in range(n)]
    stack = [(0, 0)]
    seen[0][0] = True
    while stack:
        x, y = stack[-1]
        nb = [(x + dx, y + dy, dx, dy) for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))
              if 0 <= x + dx < n and 0 <= y + dy < n and not seen[y + dy][x + dx]]
        if not nb:
            stack.pop()
            continue
        nx, ny, dx, dy = r.choice(nb)
        if dx == 1: v[y][x + 1] = False
        if dx == -1: v[y][x] = False
        if dy == 1: h[y + 1][x] = False
        if dy == -1: h[y][x] = False
        seen[ny][nx] = True
        stack.append((nx, ny))
    h[0][n // 2] = False       # the way in, from the road
    h[n][n // 2 - 1] = False   # and a way out at the back
    half = n * cell / 2
    L = ["", "# a hedge maze 14 m across, a reaper in the middle; the way in faces the road",
         "model hedge_maze",
         "  part box w=15 h=0.1 d=15 y=0.05 color=#2e2a22"]
    for y in range(n + 1):
        x = 0
        while x < n:
            if not h[y][x]:
                x += 1
                continue
            x0 = x
            while x < n and h[y][x]:
                x += 1
            w = (x - x0) * cell + 0.7
            cx = -half + (x0 + x) / 2 * cell
            cz = -half + y * cell
            L.append("  part box w=%.2f h=1.8 d=0.7 x=%.2f y=0.9 z=%.2f color=%s taper=0.06" % (w, cx, cz, HEDGE))
    for x in range(n + 1):
        y = 0
        while y < n:
            if not v[y][x]:
                y += 1
                continue
            y0 = y
            while y < n and v[y][x]:
                y += 1
            d = (y - y0) * cell + 0.7
            cz = -half + (y0 + y) / 2 * cell
            cx = -half + x * cell
            L.append("  part box w=0.7 h=1.8 d=%.2f x=%.2f y=0.9 z=%.2f color=%s taper=0.06" % (d, cx, cz, HEDGE))
    L += ["  part greeble n=40 w=13 d=13 size=0.35 y=1.8 color=%s seed=9" % HEDGE_TOP,
          "  part box w=1.2 h=0.8 d=1.2 y=0.4 color=#4a4846",
          "  part disc r=0.45 r2=0.25 h=1.6 y=1.6 seg=8 color=#3a3a3e",
          "  part sphere r=0.28 y=2.55 seg=8 color=#2e2e32",
          "  part box w=0.06 h=2.6 d=0.06 x=0.5 y=1.9 color=#5a5650",
          "  part box w=1.1 h=0.08 d=0.05 x=0.1 y=3.1 roll=-15 color=#9a9690"]
    return L


def gnome(x, z, rot, s=1.5):
    k = s
    return ["  part disc r=%.3f r2=%.3f h=%.3f x=%.2f y=%.3f z=%.2f seg=8 color=#4a3a2a" % (0.17 * k, 0.19 * k, 0.08 * k, x, 0.04 * k, z),
            "  part disc r=%.3f r2=%.3f h=%.3f x=%.2f y=%.3f z=%.2f seg=8 color=%s" % (0.16 * k, 0.12 * k, 0.32 * k, x, 0.24 * k, z, rot[1]),
            "  part sphere r=%.3f x=%.2f y=%.3f z=%.2f seg=6 color=#ecc0a0" % (0.12 * k, x, 0.5 * k, z),
            "  part disc r=%.3f r2=%.3f h=%.3f x=%.2f y=%.3f z=%.2f seg=8 color=#f0ece4" % (0.11 * k, 0.06 * k, 0.16 * k, x, 0.42 * k, z - 0.08 * k),
            "  part disc r=%.3f r2=0.005 h=%.3f x=%.2f y=%.3f z=%.2f seg=8 color=%s" % (0.13 * k, 0.34 * k, x, 0.74 * k, z, rot[0])]


def gnome_army():
    r = random.Random(3)
    caps = ["#d01a1a", "#d01a1a", "#c8301a", "#2a6a2a", "#e0b020"]
    coats = ["#2a4a9a", "#3a6a2a", "#6a2a6a", "#2a4a9a", "#8a5a2a"]
    L = ["", "# the gnome army: fifteen garden gnomes drawn up in ranks on a lawn, a general in front",
         "model gnome_army",
         "  part box w=6 h=0.06 d=4 y=0.03 color=#2e4a26"]
    for row in range(3):
        for col in range(5):
            x, z = -2.2 + col * 1.1 + r.uniform(-0.1, 0.1), -0.4 + row * 1.1
            L += gnome(x, z, (r.choice(caps), r.choice(coats)))
    L += gnome(0, -1.6, ("#101010", "#8a1a1a"), 1.9)
    L.append("  part box w=0.04 h=0.9 d=0.04 x=0.3 y=0.75 z=-1.6 color=#5a4a3a   # the general's flag")
    L.append("  part box w=0.45 h=0.3 d=0.03 x=0.53 y=1.05 z=-1.6 color=#1a1a1a")
    return L


EXTRA = """
# the pool, a hearse gone in nose first; lily pads, a wreath afloat, the diving board
model pool_hearse
  part box w=11.4 h=0.3 d=6.4 y=0.15 color=#8a8680
  part box w=10.2 h=0.32 d=5.2 y=0.17 color=#1a4a44 emit=0.12
  part body len=6 wid=2.1 hgt=0.8 x=1.2 y=0.1 z=0.4 round=4 nose=0.2 tail=0.15 drop=0.3 rot=25 roll=-10 pitch=14 color=#121214
  part cabin len=3.2 wid=1.9 hgt=0.7 x=1.8 y=0.75 z=0.6 front=0.4 back=0.05 top=0.9 rot=25 roll=-10 pitch=14 color=#121214 glass=#1a1e22
  part box w=1.4 h=0.05 d=0.5 x=2.6 y=1.25 z=1.0 rot=25 color=#c8b060   # the brass rail
  part disc r=0.35 h=0.02 x=-3.2 y=0.34 z=1.2 seg=8 color=#3a7a2a
  part disc r=0.3 h=0.02 x=-2.4 y=0.34 z=1.8 seg=8 color=#3a7a2a
  part disc r=0.4 h=0.02 x=-3.8 y=0.34 z=-0.6 seg=8 color=#3a7a2a
  part sphere r=0.12 x=-2.4 y=0.4 z=1.8 seg=6 color=#e8d0e0
  part ring r=0.45 thick=0.12 x=-1.0 y=0.36 z=-1.4 seg=10 color=#2e5a26
  part box w=0.6 h=0.08 d=2.6 x=-4.2 y=0.7 z=-2.6 color=#d8d4c8
  part box w=0.5 h=0.6 d=0.4 x=-4.2 y=0.35 z=-3.6 color=#8a8680
  part box w=0.06 h=1.2 d=0.06 x=4.9 y=0.9 z=-2.9 color=#7a7a78   # a sign: no swimming
  part box w=0.8 h=0.5 d=0.04 x=4.9 y=1.5 z=-2.9 color=#e8e4dc

# the greenhouse: brick footing, misted glass, a ribbed roof, a light left on; outside a plant that eats meat
model greenhouse lamp=1
  part box w=8.2 h=0.6 d=4.6 y=0.3 color=#6a4a3a
  part box w=8 h=2.1 d=4.4 y=1.65 color=#4a6a66
  part box w=8.1 h=0.08 d=4.5 y=2.72 color=#d8d4c8
  part box w=8.2 h=0.06 d=2.6 y=3.25 z=-1.08 pitch=-28 color=#5a7a76
  part box w=8.2 h=0.06 d=2.6 y=3.25 z=1.08 pitch=28 color=#5a7a76
  part box w=8.3 h=0.12 d=0.12 y=3.85 color=#d8d4c8
  part box w=0.08 h=0.1 d=4.6 x=-3 y=3.3 color=#d8d4c8
  part box w=0.08 h=0.1 d=4.6 x=-1 y=3.3 color=#d8d4c8
  part box w=0.08 h=0.1 d=4.6 x=1 y=3.3 color=#d8d4c8
  part box w=0.08 h=0.1 d=4.6 x=3 y=3.3 color=#d8d4c8
  part box w=1.1 h=1.9 d=0.06 y=1.55 z=-2.23 color=#2a3a36
  part light x=0 y=2.2 z=-2.3 color=#ffe0a0
  part disc r=0.45 r2=0.35 h=0.5 x=-2.4 y=0.25 z=-3 seg=10 color=#8a5a3a
  part sphere r=0.75 x=-2.4 y=1.0 z=-3 seg=8 color=#3a6a26
  part sphere r=0.35 rx=0.45 x=-2.4 y=1.75 z=-3.25 seg=8 color=#a01828
  part box w=0.6 h=0.05 d=0.12 x=-2.4 y=1.78 z=-3.5 color=#f0f0e0
  part disc r=0.4 r2=0.3 h=0.45 x=2.6 y=0.22 z=-2.9 seg=10 color=#8a5a3a
  part sphere r=0.6 x=2.6 y=0.8 z=-2.9 seg=8 color=#2e5a22

# a gazebo of eight posts under a slate roof, a lantern inside
model gazebo lamp=1
  part disc r=3 h=0.3 y=0.15 seg=8 color=#8a8680
  part disc r=0.12 h=2.6 x=2.6 y=1.6 seg=6 color=#d8d4c8
  part disc r=0.12 h=2.6 x=-2.6 y=1.6 seg=6 color=#d8d4c8
  part disc r=0.12 h=2.6 z=2.6 y=1.6 seg=6 color=#d8d4c8
  part disc r=0.12 h=2.6 z=-2.6 y=1.6 seg=6 color=#d8d4c8
  part disc r=0.12 h=2.6 x=1.84 z=1.84 y=1.6 seg=6 color=#d8d4c8
  part disc r=0.12 h=2.6 x=-1.84 z=1.84 y=1.6 seg=6 color=#d8d4c8
  part disc r=0.12 h=2.6 x=1.84 z=-1.84 y=1.6 seg=6 color=#d8d4c8
  part disc r=0.12 h=2.6 x=-1.84 z=-1.84 y=1.6 seg=6 color=#d8d4c8
  part disc r=3.5 r2=0.25 h=1.5 y=3.65 seg=8 color=#2e3036
  part sphere r=0.3 y=4.45 seg=6 color=#b09040
  part light x=0 y=2.6 z=0 color=#ffd090
  part box w=1.6 h=0.45 d=0.5 x=0 y=0.52 z=1.6 color=#5a4a3a   # a bench
  part disc r=0.5 h=0.75 y=0.67 seg=8 color=#3a2a24              # a little table

# a weeping angel on a pedestal, wings half open, its face in its hands
model statue_angel
  part box w=1.6 h=1.0 d=1.6 y=0.5 color=#5a5854
  part box w=1.8 h=0.18 d=1.8 y=1.09 color=#66645f
  part disc r=0.5 r2=0.3 h=1.9 y=2.13 seg=10 color=#8a8a86
  part sphere r=0.24 y=3.2 z=-0.12 seg=8 color=#8a8a86
  part box w=0.5 h=0.3 d=0.22 y=3.05 z=-0.32 color=#7e7e7a
  part box w=1.3 h=1.6 d=0.12 x=-0.75 y=2.7 z=0.3 rot=-25 roll=-12 color=#7a7a76 taper=0.5
  part box w=1.3 h=1.6 d=0.12 x=0.75 y=2.7 z=0.3 rot=25 roll=12 color=#7a7a76 taper=0.5

# a hedge clipped into a skull on a stone plinth: eyes, a nose, teeth (readable from above)
model topiary_skull
  part box w=1.8 h=0.5 d=1.8 y=0.25 color=#5a5854
  part sphere r=1.05 y=1.65 seg=10 color=#24442a
  part box w=1.1 h=0.6 d=0.9 y=0.85 z=-0.35 color=#24442a
  part sphere r=0.28 x=-0.38 y=2.0 z=-0.78 seg=6 color=#080a08
  part sphere r=0.28 x=0.38 y=2.0 z=-0.78 seg=6 color=#080a08
  part sphere r=0.12 y=1.62 z=-0.98 seg=5 color=#080a08
  part box w=0.8 h=0.08 d=0.1 y=1.02 z=-0.82 color=#080a08
"""


def main():
    text = open(PATH, encoding="utf-8").read()
    if BEGIN in text:
        text = text[:text.index(BEGIN)].rstrip("\n") + "\n" + text[text.index(END) + len(END):].lstrip("\n")
    block = [BEGIN] + maze() + gnome_army() + EXTRA.rstrip("\n").split("\n") + [END]
    text = text.rstrip("\n") + "\n\n" + "\n".join(block) + "\n"
    open(PATH, "w", encoding="utf-8", newline="\n").write(text)
    print("estate garden written")


if __name__ == "__main__":
    main()
