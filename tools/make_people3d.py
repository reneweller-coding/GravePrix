"""Cuts Quaternius' CC0 animation libraries (the same ones DARK CONCORDANCE uses) down to the clips GRAVE PRIX plays.
The bodies they move are MakeHuman people in real clothes (tools/make_people_mpfb.py); the Quaternius base bodies
this script once decimated for them are gone.

    python tools/make_people3d.py --dc G:/Tools/Games/DarkConcordance

Writes assets/models/people3d/anims1.glb and anims2.glb.
"""
import argparse
import json
import os
import struct

import numpy as np

# Only the clips src/People.cpp plays (A_TPose is the reference the retargeting measures from).
CLIPS1 = ["A_TPose", "Idle_Loop", "Idle_Talking_Loop", "Death01"]
CLIPS2 = ["A_TPose", "Idle_FoldArms_Loop", "LayToIdle"]
CTYPE = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
NCOMP = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}


def load(path):
    if path.endswith(".glb"):
        b = open(path, "rb").read()
        jl = struct.unpack("<I", b[12:16])[0]
        j = json.loads(b[20:20 + jl])
        off = 20 + jl
        bl = struct.unpack("<I", b[off:off + 4])[0]
        return j, [b[off + 8:off + 8 + bl]]
    j = json.load(open(path))
    base = os.path.dirname(path)
    return j, [open(os.path.join(base, buf["uri"]), "rb").read() for buf in j["buffers"]]


def read(j, bins, ai):
    a = j["accessors"][ai]
    bv = j["bufferViews"][a["bufferView"]]
    dt = CTYPE[a["componentType"]]
    n = NCOMP[a["type"]]
    start = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = bv.get("byteStride", 0)
    item = np.dtype(dt).itemsize * n
    raw = bins[bv.get("buffer", 0)]
    if stride and stride != item:
        out = np.stack([np.frombuffer(raw, dt, n, start + i * stride) for i in range(a["count"])])
    else:
        out = np.frombuffer(raw, dt, a["count"] * n, start).reshape(a["count"], n)
    if a.get("normalized"):
        out = out.astype(np.float32) / np.iinfo(dt).max
    return out


class Writer:
    def __init__(self):
        self.blob = bytearray()
        self.views = []
        self.accs = []

    def add(self, arr, ctype, typ, target=None, minmax=False):
        arr = np.ascontiguousarray(arr)
        while len(self.blob) % 4:
            self.blob += b"\0"
        v = {"buffer": 0, "byteOffset": len(self.blob), "byteLength": arr.nbytes}
        if target:
            v["target"] = target
        self.blob += arr.tobytes()
        self.views.append(v)
        a = {"bufferView": len(self.views) - 1, "componentType": ctype, "count": int(arr.shape[0]), "type": typ}
        if minmax:
            a["min"] = [float(x) for x in arr.min(0)]
            a["max"] = [float(x) for x in arr.max(0)]
        self.accs.append(a)
        return len(self.accs) - 1

    def raw(self, data):
        while len(self.blob) % 4:
            self.blob += b"\0"
        self.views.append({"buffer": 0, "byteOffset": len(self.blob), "byteLength": len(data)})
        self.blob += data
        return len(self.views) - 1


def save(path, j, blob):
    j["buffers"] = [{"byteLength": len(blob)}]
    js = json.dumps(j, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    blob = bytes(blob) + b"\0" * ((4 - len(blob) % 4) % 4)
    out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(blob))
    out += struct.pack("<II", len(js), 0x4E4F534A) + js + struct.pack("<II", len(blob), 0x004E4942) + blob
    open(path, "wb").write(out)


def anims(src, keep, out):
    j, bins = load(src)
    keep_a = [a for a in j["animations"] if a["name"] in keep]
    used = set()
    for a in keep_a:
        for s in a["samplers"]:
            used.add(s["input"])
            used.add(s["output"])
    for m in j.get("meshes", []):
        for p in m["primitives"]:
            used.update(p["attributes"].values())
            if "indices" in p:
                used.add(p["indices"])
    for sk in j.get("skins", []):
        if "inverseBindMatrices" in sk:
            used.add(sk["inverseBindMatrices"])
    amap = {}
    w = Writer()
    for ai in sorted(used):
        a = dict(j["accessors"][ai])
        bv = j["bufferViews"][a["bufferView"]]
        st = bv.get("byteOffset", 0)
        data = bins[bv.get("buffer", 0)][st:st + bv["byteLength"]]
        vi = w.raw(data)
        for k in ("byteStride", "target"):
            if k in bv:
                w.views[vi][k] = bv[k]
        a["bufferView"] = vi
        w.accs.append(a)
        amap[ai] = len(w.accs) - 1
    for a in keep_a:
        for s in a["samplers"]:
            s["input"], s["output"] = amap[s["input"]], amap[s["output"]]
    for m in j.get("meshes", []):
        for p in m["primitives"]:
            p["attributes"] = {k: amap[v] for k, v in p["attributes"].items()}
            if "indices" in p:
                p["indices"] = amap[p["indices"]]
            p.pop("material", None)
    for sk in j.get("skins", []):
        if "inverseBindMatrices" in sk:
            sk["inverseBindMatrices"] = amap[sk["inverseBindMatrices"]]
    j["animations"] = keep_a
    j["accessors"], j["bufferViews"] = w.accs, w.views
    for k in ("materials", "textures", "images", "samplers"):
        j.pop(k, None)
    save(out, j, w.blob)
    print(out, [a["name"] for a in keep_a], os.path.getsize(out) // 1024, "KB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dc", default="G:/Tools/Games/DarkConcordance")
    a = ap.parse_args()
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    q = os.path.join(a.dc, "assets/models/quaternius")
    out = os.path.join(here, "assets/models/people3d")
    os.makedirs(out, exist_ok=True)
    anims(os.path.join(q, "anim_library.glb"), CLIPS1, os.path.join(out, "anims1.glb"))
    anims(os.path.join(q, "anim_library2.glb"), CLIPS2, os.path.join(out, "anims2.glb"))


if __name__ == "__main__":
    main()
