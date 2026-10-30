"""Writes data/models/props_people.txt: the people by the road (spectators, mourners with umbrellas, marshals,
mechanics, workers, undertakers, a tramp at a fire barrel), single and in crowds. Low-poly figures of boxes, a
sphere for the head and the hat or umbrella that tells them apart from above.

    python tools/props_people.py
"""
import math
import os
import random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIN = ["#e0b89a", "#c89070", "#9a6a4a", "#6a4a32", "#f0ccb0"]


def rot(x, z, a):
    c, s = math.cos(a), math.sin(a)
    return x * c - z * s, x * s + z * c


class Model:
    def __init__(self, name, comment):
        self.lines = ["", "# " + comment, "model " + name]

    def part(self, kind, x, y, z, yaw, **kv):
        words = ["  part", kind] + ["%s=%s" % (k, v) for k, v in kv.items()]
        words += ["x=%.2f" % x, "y=%.2f" % y, "z=%.2f" % z]
        if yaw:
            words.append("rot=%.0f" % math.degrees(yaw))
        self.lines.append(" ".join(words))

    def figure(self, x, z, yaw, coat, legs, skin, hat="", arms="down", umbrella="", h=1.0, extra=""):
        """One person at (x, z) facing -z turned by yaw (radians)."""
        def at(dx, dz):
            rx, rz = rot(dx, dz, yaw)
            return x + rx, z + rz
        for side in (-1, 1):
            px, pz = at(0.11 * side, 0)
            self.part("box", px, 0.43 * h, pz, yaw, w="0.15", h="%.2f" % (0.86 * h), d="0.18", color=legs)
        px, pz = at(0, 0)
        self.part("box", px, 1.17 * h, pz, yaw, w="0.48", h="%.2f" % (0.62 * h), d="0.28", taper="0.15", color=coat)
        for side in (-1, 1):
            up = arms == "up" or (arms == "one" and side > 0)
            ax, az = at(0.31 * side, 0)
            if up:
                self.part("box", ax, 1.62 * h, az, yaw, w="0.12", h="0.6", d="0.13", roll="%d" % (-12 * side), color=coat)
                hx, hz = at(0.34 * side, 0)
                self.part("sphere", hx, 1.95 * h, hz, 0, r="0.06", color=skin)
            else:
                self.part("box", ax, 1.15 * h, az, yaw, w="0.12", h="0.58", d="0.13", color=coat)
        self.part("sphere", px, 1.62 * h, pz, 0, r="0.13", color=skin)
        if hat == "top":
            self.part("disc", px, 1.75 * h, pz, 0, r="0.19", h="0.02", seg="10", color="#101012")
            self.part("disc", px, 1.86 * h, pz, 0, r="0.11", h="0.22", seg="10", color="#101012")
        elif hat == "hard":
            self.part("dome", px, 1.71 * h, pz, 0, r="0.16", ry="0.12", seg="8", color="#e8b820")
        elif hat == "cap":
            self.part("dome", px, 1.71 * h, pz, 0, r="0.14", ry="0.08", seg="8", color=extra or "#2a3a6a")
        elif hat == "hood":
            self.part("dome", px, 1.68 * h, pz, 0, r="0.16", ry="0.16", seg="8", color=coat)
        elif hat == "hair":
            self.part("dome", px, 1.68 * h, pz, 0, r="0.14", ry="0.1", seg="8", color=extra or "#3a2a1a")
        if umbrella:
            ux, uz = at(0.18, -0.05)
            self.part("box", ux, 1.75 * h, uz, 0, w="0.03", h="0.75", d="0.03", color="#202020")
            self.part("cone", ux, 2.12 * h, uz, 0, r="0.62", len="0.3", pitch="-90", seg="10", color=umbrella)

    def text(self):
        return "\n".join(self.lines) + "\n"


def main():
    rnd = random.Random(7)
    out = ["# The people by the road (docs/DESIGN.md \"Bosses\" and the scenes). Written by tools/props_people.py.",
           "# Height about 1.8 m; they face -z; from above their hats and umbrellas tell them apart."]
    coats = ["#5a2a2a", "#2a3a5a", "#4a4a3a", "#6a5a3a", "#3a3a3a", "#7a3a1a", "#2a4a3a", "#5a4a6a", "#8a7a5a"]
    legs = ["#222226", "#3a3a40", "#2a2a3a", "#4a3a2a"]

    def single(name, comment, **kw):
        m = Model(name, comment)
        m.figure(0, 0, 0, **kw)
        out.append(m.text())

    single("fig_marshal", "a race marshal in an orange coat, a flag raised", coat="#e06a10", legs="#222226",
           skin=SKIN[0], hat="cap", arms="one", extra="#e06a10")
    single("fig_mechanic", "a mechanic in blue overalls", coat="#2a4a8a", legs="#2a4a8a", skin=SKIN[1], hat="cap",
           extra="#c02020")
    single("fig_worker", "a scrapyard worker in a high-visibility vest and a hard hat", coat="#e0c010", legs="#3a3a40",
           skin=SKIN[2], hat="hard")
    single("fig_undertaker", "an undertaker in black with a top hat", coat="#141416", legs="#141416", skin=SKIN[4],
           hat="top")
    single("fig_tramp", "a tramp in a long brown coat", coat="#5a4a32", legs="#3a3226", skin=SKIN[1], hat="hood")
    single("fig_widow", "a widow in black under a black umbrella", coat="#18161a", legs="#18161a", skin=SKIN[4],
           umbrella="#0e0e10")

    def crowd(name, comment, rows, per, gap, mourners=False, cheer=0.5, seed=1):
        m = Model(name, comment)
        r = random.Random(seed)
        for row in range(rows):
            for i in range(per):
                x = (i - (per - 1) / 2) * gap + r.uniform(-0.15, 0.15) + (0.4 if row % 2 else 0)
                z = row * 0.9 + r.uniform(-0.1, 0.1)
                yaw = math.radians(r.uniform(-25, 25))
                if mourners:
                    m.figure(x, z, yaw, coat=r.choice(["#141416", "#1c1a20", "#222024"]), legs="#141416",
                             skin=r.choice(SKIN), hat=r.choice(["top", "hair", ""]),
                             umbrella=r.choice(["#0e0e10", "#16141a", "#0e0e10", ""]))
                else:
                    m.figure(x, z, yaw, coat=r.choice(coats), legs=r.choice(legs), skin=r.choice(SKIN),
                             hat=r.choice(["cap", "hair", "hood", "", "hair"]), extra=r.choice(["#3a2a1a", "#8a6a3a", "#c02020", "#202020", "#2a3a6a"]),
                             arms=r.choice(["down", "up", "one"]) if r.random() < cheer else "down",
                             umbrella=r.choice(["", "", "", "", "#2a3a6a", "#7a1a1a"]))
        out.append(m.text())

    crowd("crowd_small", "three spectators", 1, 3, 0.8, seed=2)
    crowd("crowd_row", "a row of eight spectators, some cheering", 1, 8, 0.75, seed=3)
    crowd("crowd_block", "a block of spectators, two rows of seven", 2, 7, 0.75, cheer=0.6, seed=4)
    crowd("crowd_mourners", "mourners round a grave, black umbrellas", 2, 4, 0.9, mourners=True, seed=5)

    # A fire barrel to warm hands at: it glows at night.
    m = Model("fire_barrel", "an oil drum with a fire in it")
    m.part("disc", 0, 0.45, 0, 0, r="0.3", h="0.9", seg="10", color="#5a3a26")
    m.part("disc", 0, 0.92, 0, 0, r="0.26", h="0.06", seg="10", color="#ff7020", emit="1")
    m.part("light", 0, 1.2, 0, 0, r="0.1", color="#ff8030")
    out.append(m.text())
    m = Model("tramps_fire", "two tramps at a fire barrel")
    m.part("disc", 0, 0.45, 0, 0, r="0.3", h="0.9", seg="10", color="#5a3a26")
    m.part("disc", 0, 0.92, 0, 0, r="0.26", h="0.06", seg="10", color="#ff7020", emit="1")
    m.part("light", 0, 1.2, 0, 0, r="0.1", color="#ff8030")
    m.figure(0.75, 0.2, math.radians(-110), coat="#5a4a32", legs="#3a3226", skin=SKIN[1], hat="hood")
    m.figure(-0.7, -0.3, math.radians(70), coat="#3a3a2a", legs="#2a2a22", skin=SKIN[3], hat="cap", extra="#4a2a1a")
    out.append(m.text())

    path = os.path.join(ROOT, "data", "models", "props_people.txt")
    open(path, "w", encoding="utf-8", newline="\n").write("\n".join(out))
    print(path)


if __name__ == "__main__":
    main()
