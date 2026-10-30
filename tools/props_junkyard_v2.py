"""Rewrites the junkyard's scrap pile from things one can recognise from above (rusted car bodies, tyres, doors, a
fridge, a bathtub, drums) and adds the office trailer, the dog's kennel and the chain-link fence the scenes use.

    python tools/props_junkyard_v2.py
"""
import os
import random
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATH = os.path.join(ROOT, "data", "models", "props_junkyard.txt")
RUST = ["#7a4a2a", "#6a5040", "#5a4a40", "#8a5a30", "#5a3a2a", "#4a5050", "#6a3a30", "#7a6a50"]


def scrap_pile():
    r = random.Random(11)
    L = ["", "# a heap of junk ~9 m across: rusted car bodies, tyres, doors, a fridge, a bathtub, drums on a mound of dirt",
         "model scrap_pile",
         "  part dome r=4.6 ry=0.9 color=#3e3226 seg=12",
         "  part dome r=2.6 ry=0.5 x=1.6 z=1.2 color=#463828 seg=10"]
    import math
    hgt = lambda x, z: 0.9 * math.sqrt(max(0.0, 1 - (x * x + z * z) / 21.2))
    cars = [(-1.6, 0, 0.2, 30, 10, 12), (1.9, 0, -1.6, -58, -30, -8), (0.4, 0, 1.9, 100, 6, -12), (-2.6, 0, -2.2, 160, 45, 6)]
    for x, y, z, yaw, roll, pitch in cars:
        y = hgt(x, z) + 0.4
        c = r.choice(RUST)
        L.append("  part body len=4.2 wid=1.75 hgt=0.6 x=%.1f y=%.2f z=%.1f round=4 nose=0.2 tail=0.12 drop=0.3 rot=%d roll=%d pitch=%d color=%s"
                 % (x, y, z, yaw, roll, pitch, c))
        L.append("  part cabin len=2.0 wid=1.55 hgt=0.45 x=%.1f y=%.2f z=%.1f front=0.4 back=0.25 top=0.8 rot=%d roll=%d pitch=%d color=%s glass=#1a1e22"
                 % (x, y + 0.5, z, yaw, roll, pitch, c))
    for _ in range(9):
        a, d = r.uniform(0, 6.28), r.uniform(1.0, 4.2)
        x, z = d * __import__("math").cos(a), d * __import__("math").sin(a)
        L.append("  part ring r=0.32 thick=0.14 x=%.1f y=%.2f z=%.1f color=#1a1a1c roll=%d rot=%d seg=10"
                 % (x, hgt(x, z) + 0.15, z, r.randint(0, 80), r.randint(0, 180)))
    for _ in range(3):   # doors and bonnets
        a, d = r.uniform(0, 6.28), r.uniform(1.0, 3.6)
        x, z = d * __import__("math").cos(a), d * __import__("math").sin(a)
        L.append("  part box w=1.1 h=0.06 d=0.9 x=%.1f y=%.2f z=%.1f rot=%d roll=%d pitch=%d color=%s"
                 % (x, hgt(x, z) + 0.2, z, r.randint(0, 180), r.randint(-40, 40), r.randint(-30, 30), r.choice(RUST)))
    L += ["  part box w=0.75 h=1.5 d=0.7 x=0.9 y=1.5 z=-0.4 roll=18 pitch=-12 color=#d8d4c8   # a fridge",
          "  part box w=0.05 h=1.3 d=0.02 x=1.24 y=1.56 z=-0.76 roll=18 pitch=-12 color=#8a8a8a",
          "  part box w=1.6 h=0.55 d=0.8 x=-0.4 y=1.1 z=-1.2 rot=40 roll=-10 color=#e8e4d8   # a bathtub",
          "  part box w=1.4 h=0.45 d=0.62 x=-0.4 y=1.22 z=-1.2 rot=40 roll=-10 color=#4a4640",
          "  part disc r=0.3 h=0.88 x=3.6 y=0.44 z=-1.2 seg=10 color=#2a4a8a   # drums",
          "  part disc r=0.3 h=0.88 x=3.4 y=0.44 z=-0.5 seg=10 color=#8a2a1a",
          "  part disc r=0.3 h=0.88 x=-3.9 y=0.3 z=1.4 seg=10 roll=80 color=#6a6a3a",
          "  part disc r=0.22 h=0.04 x=-1.0 y=1.0 z=0.6 seg=10 pitch=60 color=#c8ccd4   # a hubcap"]
    return "\n".join(L) + "\n"


EXTRA = """
# the yard's office: a portable trailer on blocks, a door with steps, dark windows, a dish, a skull painted on a board
model junk_office
  part box w=6.4 h=2.5 d=2.6 y=1.55 color=#c8c2b0
  part box w=6.5 h=0.12 d=2.7 y=2.86 color=#8a8478
  part box w=1.4 h=0.7 d=0.06 x=-1.8 y=1.9 z=-1.31 color=#1a2028
  part box w=1.4 h=0.7 d=0.06 x=1.8 y=1.9 z=-1.31 color=#1a2028
  part box w=0.9 h=1.9 d=0.06 y=1.25 z=-1.31 color=#5a4a3a
  part box w=1.2 h=0.3 d=0.8 y=0.15 z=-1.7 color=#6a6a66
  part box w=1.2 h=0.3 d=0.5 y=0.45 z=-1.55 color=#6a6a66
  part box w=0.5 h=0.3 d=0.5 x=-2.8 y=0.15 z=0.9 color=#5a5a56
  part box w=0.5 h=0.3 d=0.5 x=2.8 y=0.15 z=0.9 color=#5a5a56
  part disc r=0.45 h=0.06 x=2.4 y=3.25 z=0.4 pitch=50 seg=10 color=#d8d8d0
  part box w=1.6 h=1.0 d=0.08 x=-1.6 y=3.5 z=-0.9 color=#d8c8a0
  part sphere r=0.24 x=-1.6 y=3.55 z=-0.96 color=#f0f0e8
  part box w=0.3 h=0.06 d=0.04 x=-1.6 y=3.22 z=-0.97 color=#f0f0e8
  part box w=0.08 h=0.06 d=0.03 x=-1.68 y=3.58 z=-1.17 color=#101010
  part box w=0.08 h=0.06 d=0.03 x=-1.52 y=3.58 z=-1.17 color=#101010
  part light r=0.06 x=0 y=2.5 z=-1.5 color=#ffd890

# a dog's kennel with the dog on its chain
model dog_kennel
  part box w=1.2 h=0.8 d=1.0 y=0.4 color=#7a5a3a
  part box w=1.3 h=0.45 d=1.1 y=1.02 taper=0.9 color=#5a3a26
  part box w=0.45 h=0.5 d=0.04 y=0.3 z=-0.51 color=#1a1410
  part box w=0.3 h=0.32 d=0.7 x=0.2 y=0.36 z=-1.2 color=#3a2a1e
  part sphere r=0.18 x=0.2 y=0.6 z=-1.6 color=#3a2a1e
  part box w=0.1 h=0.1 d=0.18 x=0.2 y=0.56 z=-1.78 color=#2a1e16
  part box w=0.06 h=0.3 d=0.06 x=0.08 y=0.15 z=-0.95 color=#3a2a1e
  part box w=0.06 h=0.3 d=0.06 x=0.32 y=0.15 z=-0.95 color=#3a2a1e
  part box w=0.06 h=0.3 d=0.06 x=0.08 y=0.15 z=-1.45 color=#3a2a1e
  part box w=0.06 h=0.3 d=0.06 x=0.32 y=0.15 z=-1.45 color=#3a2a1e
  part box w=0.02 h=0.02 d=0.6 x=0.2 y=0.35 z=-0.65 color=#9a9a9a
  part disc r=0.16 h=0.06 x=-0.5 y=0.03 z=-0.9 seg=8 color=#a02020

# eight metres of chain-link fence with barbed wire on top
model fence_chain
  part box w=0.08 h=2.2 d=0.08 x=-4 y=1.1 color=#8a8a86
  part box w=0.08 h=2.2 d=0.08 y=1.1 color=#8a8a86
  part box w=0.08 h=2.2 d=0.08 x=4 y=1.1 color=#8a8a86
  part box w=8.0 h=0.04 d=0.04 y=2.1 color=#9a9a96
  part box w=8.0 h=0.04 d=0.04 y=1.4 color=#9a9a96
  part box w=8.0 h=0.04 d=0.04 y=0.7 color=#9a9a96
  part box w=8.0 h=0.03 d=0.03 y=0.15 color=#9a9a96
  part box w=8.0 h=1.95 d=0.01 y=1.12 color=#5a5a58
  part box w=8.0 h=0.03 d=0.03 y=2.3 z=0.15 color=#6a6a66
  part box w=8.0 h=0.03 d=0.03 y=2.45 z=0.3 color=#6a6a66
"""


def main():
    text = open(PATH, encoding="utf-8").read()
    # Replace the old scrap pile block.
    text = re.sub(r"\n# a heap of junk[^\n]*\nmodel scrap_pile\n(  part[^\n]*\n)+", "\n", text)
    text = re.sub(r"\nmodel scrap_pile\n(  part[^\n]*\n)+", "\n", text)
    text = text.rstrip("\n") + "\n" + scrap_pile()
    if "model junk_office" not in text:
        text += EXTRA
    open(PATH, "w", encoding="utf-8", newline="\n").write(text)
    print(PATH)


if __name__ == "__main__":
    main()
