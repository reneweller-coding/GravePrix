"""Builds GRAVE PRIX's skinned people from Quaternius' CC0 base characters and animation libraries (the same ones
DARK CONCORDANCE uses): the bodies decimated in Blender (tools/people3d_decimate.py) to a light and a fuller version,
the skin weights carried over from the nearest vertex of the full body, written as GLB files with the original rig;
and the two animation libraries cut down to the clips the game plays.

    python tools/make_people3d.py --dc G:/Tools/Games/DarkConcordance --blender "C:/.../blender.exe"

Writes assets/models/people3d/{male,female}_{lo,hi}.glb and anims1.glb, anims2.glb.
"""
import argparse
import json
import os
import struct
import subprocess

import numpy as np

CLIPS1 = ["A_TPose", "Idle_Loop", "Crouch_Idle_Loop", "Dance_Loop", "Roll", "Death01", "Idle_Talking_Loop",
          "Idle_Torch_Loop", "Jump_Loop", "Interact", "Sitting_Idle_Loop"]
CLIPS2 = ["A_TPose", "Idle_Rail_Call", "Idle_TalkingPhone_Loop", "Yes", "Idle_FoldArms_Loop", "Hit_Knockback",
          "LayToIdle", "Idle_No_Loop"]
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


def body(src, npz, out):
    j, bins = load(src)
    # The body: the mesh with the most triangles.
    best = max(range(len(j["meshes"])),
               key=lambda m: j["accessors"][j["meshes"][m]["primitives"][0]["indices"]]["count"])
    prim = j["meshes"][best]["primitives"][0]
    P = read(j, bins, prim["attributes"]["POSITION"]).astype(np.float32)
    J = read(j, bins, prim["attributes"]["JOINTS_0"]).astype(np.uint16)
    W = read(j, bins, prim["attributes"]["WEIGHTS_0"]).astype(np.float32)
    d = np.load(npz)
    V, T = d["verts"].astype(np.float32), d["tris"].astype(np.uint16)
    # Blender's import may move or scale the body: fit the boxes.
    lo, hi, vlo, vhi = P.min(0), P.max(0), V.min(0), V.max(0)
    V = ((V - vlo) / np.maximum(vhi - vlo, 1e-6) * (hi - lo) + lo).astype(np.float32)
    nn = np.empty(len(V), np.int64)
    for s in range(0, len(V), 256):
        dd = ((V[s:s + 256, None, :] - P[None, :, :]) ** 2).sum(-1)
        nn[s:s + 256] = dd.argmin(1)
    Jn, Wn = J[nn], W[nn]
    Wn = Wn / np.maximum(Wn.sum(1, keepdims=True), 1e-6)
    N = np.zeros_like(V)
    f = np.cross(V[T[:, 1]] - V[T[:, 0]], V[T[:, 2]] - V[T[:, 0]])
    for k in range(3):
        np.add.at(N, T[:, k], f)
    N /= np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-9)
    w = Writer()
    attrs = {"POSITION": w.add(V, 5126, "VEC3", 34962, True),
             "NORMAL": w.add(N.astype(np.float32), 5126, "VEC3", 34962),
             "JOINTS_0": w.add(Jn, 5123, "VEC4", 34962), "WEIGHTS_0": w.add(Wn.astype(np.float32), 5126, "VEC4", 34962)}
    idx = w.add(T.reshape(-1, 1), 5123, "SCALAR", 34963)
    skins = []
    for sk in j["skins"]:
        ib = read(j, bins, sk["inverseBindMatrices"]).astype(np.float32)
        s2 = dict(sk)
        s2["inverseBindMatrices"] = w.add(ib, 5126, "MAT4")
        skins.append(s2)
    nodes = []
    for n in j["nodes"]:
        n2 = dict(n)
        if "mesh" in n2:
            if n2["mesh"] == best:
                n2["mesh"] = 0
            else:
                del n2["mesh"]
                n2.pop("skin", None)
        nodes.append(n2)
    out_j = {"asset": {"version": "2.0", "generator": "GRAVE PRIX make_people3d.py"}, "scene": 0,
             "scenes": j["scenes"], "nodes": nodes, "skins": skins,
             "meshes": [{"name": "Body", "primitives": [{"attributes": attrs, "indices": idx}]}],
             "accessors": w.accs, "bufferViews": w.views}
    save(out, out_j, w.blob)
    print(out, len(V), "verts", len(T), "tris", os.path.getsize(out) // 1024, "KB", "box", lo, hi, vlo, vhi)


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
    ap.add_argument("--blender", default="C:/Program Files/Blender Foundation/Blender 5.2/blender.exe")
    ap.add_argument("--work", default="work/people3d")
    a = ap.parse_args()
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    q = os.path.join(a.dc, "assets/models/quaternius")
    out = os.path.join(here, "assets/models/people3d")
    os.makedirs(out, exist_ok=True)
    os.makedirs(a.work, exist_ok=True)
    for sex in ("Male", "Female"):
        src = os.path.join(q, f"Superhero_{sex}_FullBody.gltf")
        pre = os.path.join(a.work, sex)
        if not os.path.exists(pre + "_0.20.npz"):
            subprocess.run([a.blender, "--background", "--factory-startup", "--python",
                            os.path.join(here, "tools/people3d_decimate.py"), "--", src, pre, "0.20,0.62"], check=True)
        body(src, pre + "_0.20.npz", os.path.join(out, f"{sex.lower()}_lo.glb"))
        body(src, pre + "_0.62.npz", os.path.join(out, f"{sex.lower()}_hi.glb"))
    anims(os.path.join(q, "anim_library.glb"), CLIPS1, os.path.join(out, "anims1.glb"))
    anims(os.path.join(q, "anim_library2.glb"), CLIPS2, os.path.join(out, "anims2.glb"))


if __name__ == "__main__":
    main()
