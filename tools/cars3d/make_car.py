"""Models a Grave Prix car in Blender from its description in tools/cars3d/cars.json and exports a GLB the game loads
instead of the Forge's parts (src/Forge.cpp, assets/cars/<model>.glb).

    "C:/Program Files/Blender Foundation/Blender 5.2/blender.exe" -b --python-exit-code 1 -P tools/cars3d/make_car.py -- car_rustbucket
    ... -- car_rustbucket --spec tools/cars3d/cars.json --out assets/cars/car_rustbucket.glb [--blend work/cars3d/rust.blend]

Everything is built in game space (x right, y up, -z the nose, metres, y = 0 the ground) and turned into Blender's
z-up frame at the end, so the glTF exporter's y-up conversion brings it back unchanged. The body is a loft of rounded
sections along the car (its side profile, plan corners, tumblehome and tuck from the description), the wheel arches
cut with a boolean and given a flared lip; the greenhouse a grid over its side profile with a crowned roof, glass
panes inset from the frame; then lamps, bumpers, grille, trim and the car's own character parts. No wheels: the
game draws its own turning wheels at the model's wheel spots, which the description repeats.

Materials the game knows by name: paint0, paint1, paint2 (the driver's colours), glass, chrome*, rubber, light*
(glows; light_dim* less), anything else keeps the colour it is given here.
"""
import json
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

# ----------------------------------------------------------------------------------------------------------- helpers


def srgb_to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def hex_color(h):
    h = h.lstrip("#")
    return tuple(srgb_to_linear(int(h[i:i + 2], 16) / 255.0) for i in (0, 2, 4))


KNOWN = {
    "paint0": "#963028", "paint1": "#1e1e22", "paint2": "#c8c8c4", "glass": "#1a2430", "chrome": "#c8ccd4",
    "rubber": "#18181a", "well": "#101012", "trim": "#18181c",
}


class Materials:
    """The car's materials by name (one Blender material each)."""

    def __init__(self):
        self.mats = {}

    def get(self, name, color=None):
        if name in self.mats:
            return self.mats[name]
        m = bpy.data.materials.new(name)
        col = hex_color(color or KNOWN.get(name, KNOWN.get(name.split("_")[0], "#808080")))
        m.diffuse_color = (*col, 1.0)
        try:
            m.use_nodes = True
        except Exception:
            pass
        bsdf = m.node_tree.nodes.get("Principled BSDF") if m.node_tree else None
        if bsdf:
            bsdf.inputs["Base Color"].default_value = (*col, 1.0)
            rough = 0.15 if name.startswith(("chrome", "glass")) else 0.5
            bsdf.inputs["Roughness"].default_value = rough
            if name.startswith("chrome"):
                bsdf.inputs["Metallic"].default_value = 1.0
            if name.startswith("light"):
                for key in ("Emission Color", "Emission"):
                    if key in bsdf.inputs:
                        bsdf.inputs[key].default_value = (*col, 1.0)
                        break
                if "Emission Strength" in bsdf.inputs:
                    bsdf.inputs["Emission Strength"].default_value = 2.0
        self.mats[name] = m
        return m


class Builder:
    """A bmesh with a material list (one object)."""

    def __init__(self, mats):
        self.bm = bmesh.new()
        self.mats = mats
        self.slots = []

    def slot(self, name, color=None):
        mat = self.mats.get(name, color)
        if mat not in self.slots:
            self.slots.append(mat)
        return self.slots.index(mat)

    def face(self, pts, mi):
        vs = [self.bm.verts.new(p) for p in pts]
        f = self.bm.faces.new(vs)
        f.material_index = mi
        return f

    def merge(self, other_bm, mi, matrix=None):
        """Adds another bmesh's geometry with one material."""
        tmp = bpy.data.meshes.new("tmp")
        other_bm.to_mesh(tmp)
        if matrix is not None:
            tmp.transform(matrix)
        n0 = len(self.bm.faces)
        self.bm.from_mesh(tmp)
        self.bm.faces.ensure_lookup_table()
        for f in self.bm.faces[n0:]:
            f.material_index = mi
        bpy.data.meshes.remove(tmp)

    def object(self, name, smooth_angle=None):
        me = bpy.data.meshes.new(name)
        self.bm.normal_update()
        self.bm.to_mesh(me)
        self.bm.free()
        for m in self.slots:
            me.materials.append(m)
        ob = bpy.data.objects.new(name, me)
        bpy.context.scene.collection.objects.link(ob)
        if smooth_angle is not None:
            smooth(me, smooth_angle)
        return ob


def smooth(me, angle):
    me.shade_smooth()
    try:
        me.set_sharp_from_angle(angle=math.radians(angle))
    except Exception:
        pass


def catmull(points, t):
    """Catmull-Rom through (x, y) points, clamped at the ends."""
    pts = sorted(points)
    if t <= pts[0][0]:
        return pts[0][1]
    if t >= pts[-1][0]:
        return pts[-1][1]
    for i in range(len(pts) - 1):
        if pts[i][0] <= t <= pts[i + 1][0]:
            break
    p0 = pts[max(i - 1, 0)]
    p1, p2 = pts[i], pts[i + 1]
    p3 = pts[min(i + 2, len(pts) - 1)]
    s = (t - p1[0]) / (p2[0] - p1[0])
    # Tangents scaled to the segment (non-uniform knots).
    m1 = (p2[1] - p0[1]) / (p2[0] - p0[0]) * (p2[0] - p1[0]) if p2[0] != p0[0] else 0
    m2 = (p3[1] - p1[1]) / (p3[0] - p1[0]) * (p2[0] - p1[0]) if p3[0] != p1[0] else 0
    s2, s3 = s * s, s * s * s
    return (2 * s3 - 3 * s2 + 1) * p1[1] + (s3 - 2 * s2 + s) * m1 + (-2 * s3 + 3 * s2) * p2[1] + (s3 - s2) * m2


def bevel_all(bm, offset, segments=2, angle=None):
    edges = bm.edges[:]
    if angle is not None:
        edges = [e for e in edges if e.is_manifold and e.calc_face_angle(0) > math.radians(angle)]
    if edges:
        bmesh.ops.bevel(bm, geom=edges, offset=offset, segments=segments, profile=0.5, affect="EDGES",
                        clamp_overlap=True)


def rot_matrix(yaw=0.0, pitch=0.0, roll=0.0):
    # yaw: clockwise from above (the Forge's rot), pitch about x, roll about z; degrees.
    return (Matrix.Rotation(math.radians(-yaw), 4, "Y") @ Matrix.Rotation(math.radians(pitch), 4, "X")
            @ Matrix.Rotation(math.radians(roll), 4, "Z"))


# -------------------------------------------------------------------------------------------------------------- body


class Body:
    def __init__(self, spec):
        self.spec = spec
        b = spec["body"]
        self.L, self.W = spec["len"], spec["wid"]
        self.b = b

    def half_width(self, z):
        b, L, W = self.b, self.L, self.W
        hw = W * 0.5
        front, rear = -L * 0.5 + b["corner_front"], L * 0.5 - b["corner_rear"]
        if z < front:
            t = min(1.0, (front - z) / b["corner_front"])
            return hw - b["inset_front"] * (1 - math.sqrt(max(0.0, 1 - t ** 2.2)))
        if z > rear:
            t = min(1.0, (z - rear) / b["corner_rear"])
            return hw - b["inset_rear"] * (1 - math.sqrt(max(0.0, 1 - t ** 2.2)))
        return hw

    def section(self, z, n=None):
        """The ring at z: (x, y) points round the section, starting at the bottom middle, counter-clockwise seen
        from the nose."""
        b = self.b
        n = n or b["sections"]
        w = self.half_width(z)
        yb, yt = catmull(b["bottom"], z), catmull(b["top"], z)
        pts = []
        e = 2.0 / b["round"]
        for k in range(n):
            a = -math.pi / 2 + 2 * math.pi * k / n
            c, s = math.cos(a), math.sin(a)
            qx = math.copysign(abs(c) ** e, c)
            qy = math.copysign(abs(s) ** e, s)
            hy = (qy + 1) * 0.5
            x = qx * w * (1 - b["tumble"] * hy ** 2) * (1 - b["tuck"] * (1 - hy) ** 3)
            pts.append((x, yb + hy * (yt - yb)))
        return pts

    def side_x(self, y, z):
        best = None
        pts = [p for p in self.section(z, 64) if p[0] > 0]
        pts.sort(key=lambda p: p[1])
        for i in range(len(pts) - 1):
            (x0, y0), (x1, y1) = pts[i], pts[i + 1]
            if y0 <= y <= y1 and y1 > y0:
                best = max(best or 0, x0 + (x1 - x0) * (y - y0) / (y1 - y0))
        return best if best is not None else max(p[0] for p in pts)

    def rings_z(self, extra):
        L = self.L
        zs = [-L / 2 + L * i / self.b["rings"] for i in range(self.b["rings"] + 1)]
        for d in (0.015, 0.06, 0.14):
            zs += [-L / 2 + d, L / 2 - d]
        zs += extra
        zs = sorted(set(round(z, 4) for z in zs if -L / 2 <= z <= L / 2))
        out = []
        for z in zs:
            if out and z - out[-1] < 0.008:
                continue
            out.append(z)
        return out

    def build(self, B, extra_z):
        bm = B.bm
        mi = B.slot(self.spec["paint"]["body"])
        zs = self.rings_z(extra_z)
        rings = []
        for z in zs:
            rings.append([bm.verts.new((x, y, z)) for x, y in self.section(z)])
        n = len(rings[0])
        for i in range(len(rings) - 1):
            for k in range(n):
                k1 = (k + 1) % n
                f = bm.faces.new((rings[i][k], rings[i][k1], rings[i + 1][k1], rings[i + 1][k]))
                f.material_index = mi
        for r, flip in ((rings[0], False), (rings[-1], True)):
            f = bm.faces.new(r if flip else list(reversed(r)))
            f.material_index = mi
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])


class ModernBody(Body):
    """A current car: body and greenhouse one continuous loft (no box on a box). Each ring is a lower half (side,
    tuck, underside) up to the belt line and an upper half from the belt line to the top centre line (bonnet,
    windscreen, roof, rear window, boot), drawn in by the greenhouse's tumblehome where the cabin is."""

    def section(self, z, n=None):
        b = self.b
        n = n or b["sections"]
        w = self.half_width(z)
        yb, belt = catmull(b["bottom"], z), catmull(b["belt"], z)
        top = max(catmull(b["top"], z), belt + 0.02)
        tg = max(0.0, catmull(b["green"], z))
        pts = []
        for k in range(n):
            a = -math.pi / 2 + 2 * math.pi * k / n
            c, s = math.cos(a), math.sin(a)
            nn = b["n_low"] if s < 0 else b["n_up"]
            qx = math.copysign(abs(c) ** (2.0 / nn), c)
            qy = math.copysign(abs(s) ** (2.0 / nn), s)
            if s < 0:
                y = belt + qy * (belt - yb)
                x = qx * w * (1 - b["tuck"] * (-qy) ** 3)
            else:
                y = belt + qy * (top - belt)
                x = qx * w * (1 - tg * qy ** 0.75)
            pts.append((x, y))
        return pts


def subdivide(ob, levels):
    mod = ob.modifiers.new("smooth", "SUBSURF")
    mod.levels = levels
    mod.render_levels = levels
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg), preserve_all_data_layers=True, depsgraph=dg)
    old = ob.data
    ob.modifiers.clear()
    ob.data = me
    bpy.data.meshes.remove(old)


def paint_glass(spec, body, ob, mats):
    """Flush glazing on the one-piece body: faces above the belt line in the cabin are glass, the roof and the
    pillars stay paint, the B pillar is black."""
    g = spec["glass"]
    me = ob.data
    names = [m.name for m in me.materials]

    def slot(name, color=None):
        mat = mats.get(name, color)
        if mat.name not in names:
            me.materials.append(mat)
            names.append(mat.name)
        return names.index(mat.name)

    glass, trim = slot("glass", g.get("color")), slot("trim", g.get("pillar_b", "#141416"))
    roof = slot(spec["paint"]["roof"])
    well = names.index("well") if "well" in names else -1
    zA, zC = g["from"], g["to"]
    b = body.b
    for p in me.polygons:
        if p.material_index == well:
            continue
        c = p.center
        if not (zA < c.z < zC):
            continue
        belt = catmull(b["belt"], c.z)
        top = max(catmull(b["top"], c.z), belt + 0.02)
        qy = (c.y - belt) / (top - belt)
        if qy < g.get("sill", 0.06):
            continue
        # Where round the section the face sits: 0 deg the side, 90 deg the top centre line.
        tg = max(0.0, catmull(b["green"], c.z))
        wq = body.half_width(c.z) * (1 - tg * max(0.0, min(1.0, qy)) ** 0.75)
        phi = math.degrees(math.atan2(max(0.0, min(1.0, qy)), min(1.0, abs(c.x) / max(0.05, wq))))
        r0, r1 = g["roof"]
        if r0 <= c.z <= r1:
            if phi < g.get("side", 52):
                p.material_index = trim if abs(c.z - g["b"]) < g.get("bwidth", 0.05) else glass
            else:
                p.material_index = roof
        else:
            # Windscreen (before the roof) or rear window (after it): the pane in the middle, the pillars beside,
            # the quarter lights below the pillars.
            if phi > g.get("pillar_hi", 50):
                p.material_index = glass
            elif phi < g.get("pillar_lo", 38) and zA + g.get("frame", 0.06) < c.z < zC - g.get("frame", 0.06):
                p.material_index = glass
            else:
                p.material_index = roof


def refine_glass(spec, body, ob, mats, rounds=2):
    """The glass's edges finer: the faces along a border between the glass (or the B pillar) and the rest are cut into
    four and painted again by their middles, twice, so the steps of the per-face glazing are a ninth as big."""
    me = ob.data
    names = [m.name for m in me.materials]
    special = {names.index(n) for n in ("glass", "trim") if n in names}
    if not special:
        return
    for _ in range(rounds):
        bm = bmesh.new()
        bm.from_mesh(me)
        border = [e for e in bm.edges if len(e.link_faces) == 2
                  and e.link_faces[0].material_index != e.link_faces[1].material_index
                  and {e.link_faces[0].material_index, e.link_faces[1].material_index} & special]
        faces = {f for e in border for f in e.link_faces}
        cut = list({e for f in faces for e in f.edges})
        if cut:
            bmesh.ops.subdivide_edges(bm, edges=cut, cuts=1, use_grid_fill=True)
        bm.to_mesh(me)
        bm.free()
        paint_glass(spec, body, ob, mats)


def build_strips(spec, target, mats):
    """Lamps and grilles as strips on the body's skin: two edge polylines resampled, shrink-wrapped a few
    millimetres proud (slim LED lamps that follow the surface)."""
    obs = []
    for i, d in enumerate(spec.get("strips", [])):
        for mirror in ((False, True) if d.get("mirror") else (False,)):
            B = Builder(mats)
            mi = B.slot(d["mat"], d.get("color"))
            sx = -1 if mirror else 1

            def resample(pts, m):
                pts = [Vector((sx * q[0], q[1], q[2])) for q in pts]
                seg = [(pts[j + 1] - pts[j]).length for j in range(len(pts) - 1)]
                tot = sum(seg) or 1.0
                out = []
                for k in range(m + 1):
                    t = tot * k / m
                    j = 0
                    while j < len(seg) - 1 and t > seg[j]:
                        t -= seg[j]
                        j += 1
                    out.append(pts[j].lerp(pts[j + 1], min(1.0, t / (seg[j] or 1.0))))
                return out
            m = d.get("steps", 10)
            top, bot = resample(d["top"], m), resample(d["bot"], m)
            rows = [top, [a.lerp(b, 0.5) for a, b in zip(top, bot)], bot]
            vs = [[B.bm.verts.new(q) for q in row] for row in rows]
            out = Vector(d.get("n", (0, 0, -1)))
            out.x *= sx
            for r in range(len(vs) - 1):
                for k in range(m):
                    f = B.bm.faces.new((vs[r][k], vs[r][k + 1], vs[r + 1][k + 1], vs[r + 1][k]))
                    f.material_index = mi
                    f.normal_update()
                    if f.normal.dot(out) < 0:
                        f.normal_flip()
            ob = B.object("strip%d" % i)
            mod = ob.modifiers.new("wrap", "SHRINKWRAP")
            mod.target = target
            mod.wrap_method = "NEAREST_SURFACEPOINT"
            mod.offset = d.get("offset", 0.006)
            dg = bpy.context.evaluated_depsgraph_get()
            me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg), depsgraph=dg)
            old = ob.data
            ob.modifiers.clear()
            ob.data = me
            bpy.data.meshes.remove(old)
            smooth(me, 80)
            obs.append(ob)
    return obs


def cutters(spec, mats):
    """The wheel arches: a cylinder per wheel and side (from the floor pan outwards), material 'well'."""
    wh = spec["wheels"]
    obs = []
    R = wh["r"] + wh["gap"]
    for z in wh["z"]:
        for sx in (-1, 1):
            B = Builder(mats)
            mi = B.slot("well")
            res = bmesh.ops.create_cone(B.bm, cap_ends=True, cap_tris=False, segments=28, radius1=R, radius2=R,
                                        depth=0.9)
            # Along x, from |x| = 0.42 outwards.
            m = Matrix.Translation((sx * (0.42 + 0.45), wh["r"], z)) @ Matrix.Rotation(math.pi / 2, 4, "Y")
            bmesh.ops.transform(B.bm, matrix=m, verts=res["verts"])
            for f in B.bm.faces:
                f.material_index = mi
            obs.append(B.object("cut"))
    return obs


def arch_lips(spec, body, B):
    wh = spec["wheels"]
    mi = B.slot(spec["paint"].get("lips", spec["paint"]["body"]), spec["paint"].get("lips_color"))
    R = wh["r"] + wh["gap"]
    for z0 in wh["z"]:
        for sx in (-1, 1):
            steps = 14
            # From the body's underside on one side of the arch over the top to the other.
            def end(zz):
                yb = catmull(body.b["bottom"], zz) + 0.03
                return math.asin(max(-0.6, min(0.6, (yb - wh["r"]) / R)))
            a0, a1 = end(z0 - R), math.pi - end(z0 + R)
            rings = []
            for i in range(steps + 1):
                a = a0 + (a1 - a0) * i / steps
                y = wh["r"] + R * math.sin(a)
                z = z0 - R * math.cos(a)
                xs = body.side_x(max(y, 0.05), z)
                ry, rz = math.sin(a), -math.cos(a)
                sec = [(-0.035, 0.0), (0.03, 0.004), (0.022, 0.05), (-0.01, 0.065)]   # (x out, radial out)
                ring = []
                for dx, dr in sec:
                    ring.append(B.bm.verts.new((sx * (xs + dx), wh["r"] + (R + dr) * ry, z0 + (R + dr) * rz)))
                rings.append(ring)
            for i in range(steps):
                for k in range(4):
                    k1 = (k + 1) % 4
                    f = B.bm.faces.new((rings[i][k], rings[i][k1], rings[i + 1][k1], rings[i + 1][k]))
                    f.material_index = mi
            for r in (rings[0], rings[-1]):
                f = B.bm.faces.new(r)
                f.material_index = mi


def apply_boolean(ob, cutter_obs):
    for c in cutter_obs:
        mod = ob.modifiers.new("arch", "BOOLEAN")
        mod.operation = "DIFFERENCE"
        mod.object = c
        try:
            mod.solver = "EXACT"
        except Exception:
            pass
        try:
            mod.material_mode = "TRANSFER"
        except Exception:
            pass
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    me = bpy.data.meshes.new_from_object(ev, preserve_all_data_layers=True, depsgraph=dg)
    old = ob.data
    ob.modifiers.clear()
    ob.data = me
    bpy.data.meshes.remove(old)
    for c in cutter_obs:
        bpy.data.objects.remove(c)


def paint_regions(spec, ob, mats, rng):
    """Faces of the body coloured by region: primer bonnet, rotten sills, the odd door, shut lines."""
    me = ob.data
    names = [m.name for m in me.materials]

    def slot(name, color):
        mat = mats.get(name, color)
        if mat.name not in names:
            me.materials.append(mat)
            names.append(mat.name)
        return names.index(mat.name)

    well = names.index("well") if "well" in names else -1
    regions = spec.get("regions", [])
    shut = spec.get("shutlines")
    noise = {}
    for p in me.polygons:
        if p.material_index == well:
            continue
        c, n = p.center, p.normal
        for r in regions:
            if not (r["z"][0] <= c.z <= r["z"][1]):
                continue
            if "up" in r and n.y < r["up"]:
                continue
            if "x" in r and not (r["x"][0] <= c.x <= r["x"][1]):
                continue
            if "side" in r and abs(n.x) < r["side"]:
                continue
            if "xsign" in r and c.x * r["xsign"] <= 0:
                continue
            if "y" in r:
                hi = r["y"][1]
                if "ragged" in r:
                    key = round(c.z * 7.0)
                    noise.setdefault(key, rng.uniform(-1, 1))
                    hi += r["ragged"] * noise[key]
                if not (r["y"][0] <= c.y <= hi):
                    continue
            p.material_index = slot(r["mat"], r.get("color"))
        if shut and abs(n.x) > 0.5 and shut["y"][0] <= c.y <= shut["y"][1]:
            for z in shut["z"]:
                if abs(c.z - z) < 0.007:
                    p.material_index = slot("shut", shut["color"])


# ------------------------------------------------------------------------------------------------------------ cabin


def build_cabin(spec, body, B):
    cb = spec["cabin"]
    prof = cb["profile"]
    base, top = cb["base"], cb["height"]
    hgt = top - base
    w0, w1 = cb["w0"], cb["w1"]
    cols = [0.0, 0.05, 0.18, 0.34, 0.5, 0.66, 0.82, 0.95, 1.0]
    roof = B.slot(spec["paint"]["roof"])
    body_mi = B.slot(spec["paint"]["body"])
    glass = B.slot("glass", cb.get("glass"))

    def width(y):
        t = max(0.0, min(1.0, (y - base) / hgt))
        return w0 + (w1 - w0) * t

    hmax = max(p[1] for p in prof)
    rows = [(z, base + h / hmax * hgt) for z, h in prof]
    # Which rows are glass (steep) and which roof.
    grid = []
    zmid = (prof[0][0] + prof[-1][0]) * 0.5
    for i, (z, y) in enumerate(rows):
        roofy = (y - base) / hgt
        line = []
        for f in cols:
            s = 2 * f - 1
            w = width(y)
            crown = cb["crown"] * (1 - s * s) * roofy ** 3
            dz = cb["wrap"] * s * s * (1 - roofy) * (1 if z < zmid else -1)
            line.append(B.bm.verts.new((s * w, y + crown, z + dz)))
        grid.append(line)
    for i in range(len(rows) - 1):
        (za, ya), (zb, yb) = rows[i], rows[i + 1]
        steep = abs(yb - ya) / max(1e-4, abs(zb - za)) > 0.45
        for j in range(len(cols) - 1):
            pillar = j == 0 or j == len(cols) - 2
            mi = glass if steep and not pillar else (roof if not steep else body_mi)
            f = B.bm.faces.new((grid[i][j], grid[i][j + 1], grid[i + 1][j + 1], grid[i + 1][j]))
            f.material_index = mi
    bmesh.ops.recalc_face_normals(B.bm, faces=B.bm.faces[:])
    # The sides: the frame in the body's colour, the panes inset and a little proud, split at the B pillar.
    for side, j in ((-1, 0), (1, len(cols) - 1)):
        ring = [grid[i][j] for i in range(len(rows))]
        f = B.bm.faces.new(ring)
        f.material_index = body_mi
        f.normal_update()
        if f.normal.x * side < 0:
            f.normal_flip()
        poly = [(v.co.z, v.co.y) for v in ring]
        inner = inset(poly, cb["frame"])
        for zc, below in ((cb["bpillar"] - 0.045, True), (cb["bpillar"] + 0.045, False)):
            part = clip(inner, zc, below)
            if len(part) < 3:
                continue
            vs = [B.bm.verts.new((side * (width(y) + 0.006), y, z)) for z, y in part]
            f = B.bm.faces.new(vs)
            f.material_index = glass
            f.normal_update()
            if f.normal.x * side < 0:
                f.normal_flip()


def inset(poly, d):
    """Moves a convex polygon's edges inwards by d (2D)."""
    n = len(poly)
    area = sum(poly[i][0] * poly[(i + 1) % n][1] - poly[(i + 1) % n][0] * poly[i][1] for i in range(n))
    sgn = 1 if area > 0 else -1
    out = []
    for i in range(n):
        p0, p1, p2 = Vector(poly[i - 1]), Vector(poly[i]), Vector(poly[(i + 1) % n])
        e1, e2 = (p1 - p0).normalized(), (p2 - p1).normalized()
        n1, n2 = Vector((-e1.y, e1.x)) * sgn, Vector((-e2.y, e2.x)) * sgn
        bis = (n1 + n2)
        if bis.length < 1e-6:
            bis = n1
        bis.normalize()
        k = d / max(0.2, bis.dot(n1))
        out.append(tuple(p1 + bis * k))
    return out


def clip(poly, z0, keep_below):
    """Sutherland-Hodgman against the line z = z0 (keeps z < z0 or z > z0)."""
    def inside(p):
        return p[0] < z0 if keep_below else p[0] > z0
    out = []
    n = len(poly)
    for i in range(n):
        a, b = poly[i], poly[(i + 1) % n]
        if inside(a):
            out.append(a)
        if inside(a) != inside(b):
            t = (z0 - a[0]) / (b[0] - a[0])
            out.append((z0, a[1] + (b[1] - a[1]) * t))
    return out


# ------------------------------------------------------------------------------------------------------------ parts


def part_box(B, p, body):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    sx, sy, sz = p["size"]
    bmesh.ops.scale(bm, vec=(sx, sy, sz), verts=bm.verts[:])
    bev = p.get("bevel", min(sx, sy, sz) * 0.3 if min(sx, sy, sz) >= 0.03 else 0.0)
    if bev > 0:
        bevel_all(bm, bev, p.get("segments", 1))
    return bm


def part_lamp(B, p):
    """A round lamp facing the end it sits at: a chrome bezel and the glowing lens."""
    bm = bmesh.new()
    r = p["r"]
    face = -1 if p["at"][2] < 0 else 1
    depth = p.get("depth", 0.05)
    res = bmesh.ops.create_cone(bm, cap_ends=True, segments=14, radius1=r * 1.22, radius2=r * 1.15, depth=depth)
    for f in bm.faces:
        f.material_index = 0
    bez = bmesh.new()
    res2 = bmesh.ops.create_cone(bez, cap_ends=True, segments=14, radius1=r, radius2=r * 0.9, depth=0.03)
    bmesh.ops.translate(bez, vec=(0, 0, depth * 0.5 - 0.003), verts=bez.verts[:])
    tmp = bpy.data.meshes.new("t")
    bez.to_mesh(tmp)
    n0 = len(bm.faces)
    bm.from_mesh(tmp)
    bm.faces.ensure_lookup_table()
    for f in bm.faces[n0:]:
        f.material_index = 1
    bpy.data.meshes.remove(tmp)
    bez.free()
    # The cone's axis is z; it points to +z (the tail) or -z (the nose).
    if face < 0:
        bmesh.ops.rotate(bm, cent=(0, 0, 0), matrix=Matrix.Rotation(math.pi, 3, "Y"), verts=bm.verts[:])
    return bm


def part_cyl(p):
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, segments=16, radius1=p["r"], radius2=p["r"], depth=p["h"])
    axis = p.get("axis", "y")
    if axis == "y":
        bmesh.ops.rotate(bm, cent=(0, 0, 0), matrix=Matrix.Rotation(math.pi / 2, 3, "X"), verts=bm.verts[:])
    elif axis == "x":
        bmesh.ops.rotate(bm, cent=(0, 0, 0), matrix=Matrix.Rotation(math.pi / 2, 3, "Y"), verts=bm.verts[:])
    bevel_all(bm, min(0.01, p["h"] * 0.25), 1, angle=60)
    return bm


def part_sphere(p):
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=14, v_segments=9, radius=p["r"])
    return bm


def part_cone(p):
    """A cone along y, its tip down (an ice-cream cone) or up."""
    bm = bmesh.new()
    down = p.get("tip", "down") == "down"
    bmesh.ops.create_cone(bm, cap_ends=True, segments=16, radius1=0.0 if down else p["r"],
                          radius2=p["r"] if down else 0.0, depth=p["len"])
    bmesh.ops.rotate(bm, cent=(0, 0, 0), matrix=Matrix.Rotation(-math.pi / 2, 3, "X"), verts=bm.verts[:])
    return bm


def part_poly(p):
    """A flat plate: points (z, y) in the side plane, 'thick' along x."""
    bm = bmesh.new()
    t = p.get("thick", 0.03) * 0.5
    a = [bm.verts.new((-t, y, z)) for z, y in p["pts"]]
    b = [bm.verts.new((t, y, z)) for z, y in p["pts"]]
    n = len(a)
    bm.faces.new(a)
    bm.faces.new(list(reversed(b)))
    for i in range(n):
        bm.faces.new((a[i], a[(i + 1) % n], b[(i + 1) % n], b[i]))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return bm


def part_band(p, body):
    """A painted band along the body's side at height y (h tall) from z0 to z1, a few millimetres proud (right side;
    mirror it for the left)."""
    bm = bmesh.new()
    y, h = p["y"], p["h"] * 0.5
    z0, z1 = p["z"]
    n = p.get("steps", 24)
    lo, hi = [], []
    for i in range(n + 1):
        z = z0 + (z1 - z0) * i / n
        lo.append(bm.verts.new((body.side_x(y - h, z) + 0.005, y - h, z)))
        hi.append(bm.verts.new((body.side_x(y + h, z) + 0.005, y + h, z)))
    for i in range(n):
        f = bm.faces.new((lo[i], lo[i + 1], hi[i + 1], hi[i]))
        f.normal_update()
        if f.normal.x < 0:
            f.normal_flip()
    return bm


def part_tyre(p):
    """A tyre lying flat: a torus with a squarish section."""
    bm = bmesh.new()
    R, t = p["r"] - p["t"], p["t"]
    seg, sides = 18, 8
    rings = []
    for i in range(seg):
        a = 2 * math.pi * i / seg
        ca, sa = math.cos(a), math.sin(a)
        ring = []
        for k in range(sides):
            b = 2 * math.pi * k / sides
            cb, sb = math.cos(b), math.sin(b)
            # squarish: superellipse section
            qx = math.copysign(abs(cb) ** 0.6, cb) * t
            qy = math.copysign(abs(sb) ** 0.6, sb) * t * 0.95
            rr = R + qx
            ring.append(bm.verts.new((rr * ca, qy, rr * sa)))
        rings.append(ring)
    for i in range(seg):
        for k in range(sides):
            a, b = rings[i][k], rings[i][(k + 1) % sides]
            c, d = rings[(i + 1) % seg][(k + 1) % sides], rings[(i + 1) % seg][k]
            bm.faces.new((a, b, c, d))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return bm


def part_tube(p):
    """A wire or pipe along points (square-ish section, mitred corners), capped."""
    bm = bmesh.new()
    pts = [Vector(q) for q in p["pts"]]
    r, sides = p["r"], p.get("sides", 6)
    rings = []
    for i, q in enumerate(pts):
        d0 = (q - pts[i - 1]) if i > 0 else (pts[1] - q)
        d1 = (pts[i + 1] - q) if i < len(pts) - 1 else d0
        d = (d0.normalized() + d1.normalized()).normalized()
        up = Vector((0, 1, 0)) if abs(d.y) < 0.9 else Vector((1, 0, 0))
        u = d.cross(up).normalized()
        v = u.cross(d).normalized()
        ring = []
        for k in range(sides):
            a = 2 * math.pi * k / sides
            ring.append(bm.verts.new(q + (u * math.cos(a) + v * math.sin(a)) * r))
        rings.append(ring)
    for i in range(len(rings) - 1):
        for k in range(sides):
            k1 = (k + 1) % sides
            bm.faces.new((rings[i][k], rings[i][k1], rings[i + 1][k1], rings[i + 1][k]))
    bm.faces.new(rings[0])
    bm.faces.new(list(reversed(rings[-1])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    return bm


def part_bar(p):
    """A bumper: a bar of h x d along plan points (x, z) at height y, bevelled."""
    bm = bmesh.new()
    pts = [Vector((q[0], 0.0, q[1])) for q in p["pts"]]
    h, d = p["h"] * 0.5, p["d"] * 0.5
    rings = []
    for i, q in enumerate(pts):
        d0 = (q - pts[i - 1]) if i > 0 else (pts[1] - q)
        d1 = (pts[i + 1] - q) if i < len(pts) - 1 else d0
        t = (d0.normalized() + d1.normalized()).normalized()
        nrm = Vector((-t.z, 0, t.x))   # in plan, perpendicular
        ring = [bm.verts.new(q + nrm * sx * d + Vector((0, sy * h, 0))) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        rings.append(ring)
    for i in range(len(rings) - 1):
        for k in range(4):
            k1 = (k + 1) % 4
            bm.faces.new((rings[i][k], rings[i][k1], rings[i + 1][k1], rings[i + 1][k]))
    bm.faces.new(rings[0])
    bm.faces.new(list(reversed(rings[-1])))
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])
    bevel_all(bm, 0.025, 2, angle=60)
    return bm


def build_parts(spec, body, B):
    for p in spec.get("parts", []):
        kind = p["kind"]
        mats = [p["mat"]]
        if kind == "lamp":
            mats = ["chrome", p["mat"]]
        idx = [B.slot(m, p.get("color") if m == p["mat"] else None) for m in mats]
        for mirror in ((False, True) if p.get("mirror") else (False,)):
            if kind == "box":
                bm = part_box(B, p, body)
            elif kind == "lamp":
                bm = part_lamp(B, p)
            elif kind == "cyl":
                bm = part_cyl(p)
            elif kind == "tyre":
                bm = part_tyre(p)
            elif kind == "tube":
                bm = part_tube(p)
            elif kind == "bar":
                bm = part_bar(p)
            elif kind == "band":
                bm = part_band(p, body)
            elif kind == "sphere":
                bm = part_sphere(p)
            elif kind == "cone":
                bm = part_cone(p)
            elif kind == "poly":
                bm = part_poly(p)
            else:
                raise SystemExit("unknown part kind " + kind)
            at = Vector(p.get("at", (0, p.get("y", 0), 0)))
            if kind == "bar":
                at = Vector((0, p["y"], 0))
            if kind in ("tube", "band"):
                at = Vector((0, 0, 0))
            if p.get("sidebody"):
                at.x = body.side_x(at.y, at.z) + p["size"][0] * 0.2
            xf = Matrix.Translation(at) @ rot_matrix(p.get("yaw", 0), p.get("pitch", 0), p.get("roll", 0))
            if kind == "bar":
                # The bar's points are absolute in plan; turn about its middle.
                mid = Vector((0, 0, sum(q[1] for q in p["pts"]) / len(p["pts"])))
                xf = Matrix.Translation(at + mid) @ rot_matrix(p.get("yaw", 0), p.get("pitch", 0), p.get("roll", 0)) \
                    @ Matrix.Translation(-mid)
            if mirror:
                xf = Matrix.Scale(-1, 4, (1, 0, 0)) @ xf
            bmesh.ops.transform(bm, matrix=xf, verts=bm.verts[:])
            if mirror:
                bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
            tmp = bpy.data.meshes.new("t")
            bm.to_mesh(tmp)
            bm.free()
            n0 = len(B.bm.faces)
            B.bm.from_mesh(tmp)
            B.bm.faces.ensure_lookup_table()
            for f in B.bm.faces[n0:]:
                f.material_index = idx[min(f.material_index, len(idx) - 1)] if kind == "lamp" else idx[0]
            bpy.data.meshes.remove(tmp)


def build_decals(spec, targets, mats, rng):
    """Rust patches: ragged discs shrink-wrapped onto the body (or the cabin), a few millimetres proud."""
    obs = []
    for i, d in enumerate(spec.get("decals", [])):
        B = Builder(mats)
        mi = B.slot(d["mat"], d.get("color"))
        n = Vector(d["n"]).normalized()
        u = n.cross(Vector((0, 0, 1)) if abs(n.z) < 0.9 else Vector((1, 0, 0))).normalized()
        v = n.cross(u).normalized()
        c = Vector(d["at"])
        k = 14
        rad = [d["r"] * rng.uniform(0.6, 1.15) for _ in range(k)]
        outer, mid = [], []
        for j in range(k):
            a = 2 * math.pi * j / k
            dirv = u * math.cos(a) + v * math.sin(a) * rng.uniform(0.7, 1.0)
            outer.append(B.bm.verts.new(c + dirv * rad[j]))
            mid.append(B.bm.verts.new(c + dirv * rad[j] * 0.55))
        cen = B.bm.verts.new(c)
        for j in range(k):
            j1 = (j + 1) % k
            B.face_from([outer[j], outer[j1], mid[j1], mid[j]], mi)
            B.face_from([mid[j], mid[j1], cen], mi)
        for f in B.bm.faces:
            f.normal_update()
            if f.normal.dot(n) < 0:
                f.normal_flip()
        ob = B.object("decal%d" % i)
        tgt = targets[d.get("on", "body")]
        mod = ob.modifiers.new("wrap", "SHRINKWRAP")
        mod.target = tgt
        mod.wrap_method = "NEAREST_SURFACEPOINT"
        mod.offset = 0.004
        dg = bpy.context.evaluated_depsgraph_get()
        me = bpy.data.meshes.new_from_object(ob.evaluated_get(dg), depsgraph=dg)
        old = ob.data
        ob.modifiers.clear()
        ob.data = me
        bpy.data.meshes.remove(old)
        smooth(me, 80)
        obs.append(ob)
    return obs


def face_from(self, verts, mi):
    f = self.bm.faces.new(verts)
    f.material_index = mi
    return f


Builder.face_from = face_from


# ------------------------------------------------------------------------------------------------------------- main


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(os.path.dirname(here))
    name = argv[0] if argv and not argv[0].startswith("--") else "car_rustbucket"

    def opt(key, default):
        return argv[argv.index(key) + 1] if key in argv else default

    spec_file = opt("--spec", os.path.join(here, "cars.json"))
    out = opt("--out", os.path.join(root, "assets", "cars", name + ".glb"))
    blend = opt("--blend", "")
    with open(spec_file, encoding="utf-8") as f:
        spec = json.load(f)[name]
    rng = random.Random(spec.get("seed", 1))

    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = Materials()
    modern = spec["body"].get("style") == "modern"
    body = ModernBody(spec) if modern else Body(spec)

    # The body: loft, arches cut, regions painted.
    wh = spec["wheels"]
    extra = []
    for z in spec.get("shutlines", {}).get("z", []):
        extra += [z - 0.006, z + 0.006]
    for r in spec.get("regions", []):
        extra += list(r["z"])
    B = Builder(mats)
    body.build(B, extra)
    body_ob = B.object("body")
    if spec["body"].get("subsurf", 0):
        subdivide(body_ob, spec["body"]["subsurf"])
    apply_boolean(body_ob, cutters(spec, mats))
    if modern:
        paint_glass(spec, body, body_ob, mats)
        refine_glass(spec, body, body_ob, mats)
    paint_regions(spec, body_ob, mats, rng)
    smooth(body_ob.data, spec["body"].get("smooth", 38))

    L = Builder(mats)
    arch_lips(spec, body, L)
    lips = L.object("lips", 50)

    cabin = None
    if "cabin" in spec:
        C = Builder(mats)
        build_cabin(spec, body, C)
        cabin = C.object("cabin")
        bm = bmesh.new()
        bm.from_mesh(cabin.data)
        bevel_all(bm, 0.018, 2, angle=25)
        bm.to_mesh(cabin.data)
        bm.free()
        smooth(cabin.data, 32)

    P = Builder(mats)
    build_parts(spec, body, P)
    parts = P.object("parts", 40)

    decals = build_decals(spec, {"body": body_ob, "cabin": cabin or body_ob}, mats, rng)
    decals += build_strips(spec, body_ob, mats)

    # Into Blender's z-up frame (game (x, y, z) -> Blender (x, -z, y)), triangulated.
    turn = Matrix.Rotation(math.pi / 2, 4, "X")
    tris = 0
    for ob in [o for o in (body_ob, lips, cabin, parts) if o] + decals:
        ob.data.transform(turn)
        bm = bmesh.new()
        bm.from_mesh(ob.data)
        bmesh.ops.triangulate(bm, faces=bm.faces[:])
        bm.to_mesh(ob.data)
        bm.free()
        tris += len(ob.data.polygons)
        print("  %-8s %5d" % (ob.name, len(ob.data.polygons)))
    print("%s: %d triangles, %d materials" % (name, tris, len(mats.mats)))

    os.makedirs(os.path.dirname(out), exist_ok=True)
    for ob in bpy.context.scene.objects:
        ob.select_set(True)
    bpy.ops.export_scene.gltf(filepath=out, export_format="GLB", export_yup=True, export_apply=True,
                              export_normals=True, export_texcoords=False, export_materials="EXPORT",
                              export_cameras=False, export_lights=False)
    print("wrote", out)
    if blend:
        bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(blend))


main()
