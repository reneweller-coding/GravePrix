"""Writes data/models/props_litter.txt: the small things that lie by the road (tyres, hubcaps, doors, oil stains,
newspapers, rubbish bags, bones, flowers, stones, sandbags, pipes). The venues name them in "litter" lines; the
track lays them in a band a few metres from the road's edge.

    python tools/props_litter.py
"""
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TEXT = """# Litter by the road (written by tools/props_litter.py): small, flat, readable from above.

model tire_single
  part ring r=0.34 thick=0.15 y=0.08 roll=90 color=#1a1a1c seg=12
  part disc r=0.17 h=0.04 y=0.06 color=#5a5a5e seg=8

model hubcap
  part disc r=0.22 h=0.04 y=0.03 color=#c8ccd4 seg=10
  part disc r=0.08 h=0.05 y=0.05 color=#8a8e96 seg=8

model car_door
  part box w=1.1 h=0.06 d=0.9 y=0.04 rot=12 color=#7a4a2a
  part box w=0.7 h=0.07 d=0.4 x=0.1 y=0.05 z=-0.18 rot=12 color=#1a1e22
  part box w=0.12 h=0.08 d=0.04 x=-0.35 y=0.07 z=0.3 rot=12 color=#c8ccd4

model oil_stain
  part disc r=1.1 h=0.01 y=0.05 color=#0e0c0a seg=12
  part disc r=0.6 h=0.012 x=0.6 y=0.052 z=0.3 color=#16120e seg=10

model newspaper
  part box w=0.5 h=0.02 d=0.36 y=0.02 rot=20 color=#d8d2c0
  part box w=0.3 h=0.022 d=0.06 x=-0.05 y=0.025 z=-0.1 rot=20 color=#2a2a2a
  part box w=0.44 h=0.02 d=0.32 x=0.3 y=0.03 z=0.2 rot=-35 color=#c8c2b0

model trash_bag
  part dome r=0.38 ry=0.42 color=#141416 seg=8
  part dome r=0.3 ry=0.34 x=0.5 z=0.2 color=#1a1a1e seg=8
  part box w=0.06 h=0.12 d=0.06 y=0.45 color=#141416

model bones_small
  part box w=0.6 h=0.06 d=0.08 y=0.04 rot=30 color=#e0d8c4
  part box w=0.5 h=0.06 d=0.08 x=0.2 y=0.04 z=0.3 rot=-50 color=#e0d8c4
  part sphere r=0.14 x=-0.3 y=0.12 z=0.2 color=#e8e0cc

model flowers
  part disc r=0.22 h=0.1 y=0.05 color=#3a5a2a seg=8
  part sphere r=0.07 x=0.08 y=0.16 color=#c02040
  part sphere r=0.07 x=-0.08 y=0.15 z=0.06 color=#e0d040
  part sphere r=0.07 y=0.17 z=-0.08 color=#f0f0f0

model rock_small
  part dome r=0.55 ry=0.35 color=#7a7268 seg=7
  part dome r=0.3 ry=0.22 x=0.5 z=0.3 color=#6a645a seg=6

model sandbag
  part box w=0.9 h=0.25 d=0.5 y=0.13 taper=0.2 color=#8a7a5a
  part box w=0.9 h=0.25 d=0.5 x=0.5 y=0.13 z=0.55 rot=20 taper=0.2 color=#7a6a4a

model pipe_short
  part disc r=0.22 h=2.2 y=0.22 pitch=90 rot=30 color=#7a7a80 seg=10
  part disc r=0.17 h=2.22 y=0.22 pitch=90 rot=30 color=#1a1a1a seg=10
"""


def main():
    path = os.path.join(ROOT, "data", "models", "props_litter.txt")
    open(path, "w", encoding="utf-8", newline="\n").write(TEXT)
    print(path)


if __name__ == "__main__":
    main()
