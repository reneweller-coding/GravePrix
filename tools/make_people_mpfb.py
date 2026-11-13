"""Builds Grave Prix's skinned people from MPFB (MakeHuman for Blender; its assets CC0): a body with skin, eyes, hair
and real clothes (a suit, overalls, a jacket over jeans, a blouse and a skirt, shoes, gloves) on MPFB's game engine
rig, renamed as the clips want it ("root", "Head"), with an "A_TPose" clip that stands it in the second animation
library's T-pose (src/Skin.cpp retargets every clip from it).

The game's shader has no textures, so every vertex takes its texture's colour (the texture averaged down first; the
brows and the hair laid onto the skin under them as well), and its alpha says what it is: 255 kept as it is, 200 the
top (a jacket, a shirt), 170 reflective tape on overalls, 140 the legs (trousers, overalls, a skirt), 100 the hair
and the brows, 50 the skin; the game tints those by the outfit (src/People.cpp) and lights the tape on marshals. Joined into one mesh, decimated to a few thousand triangles (the podium's two to
more), written as assets/models/people3d/look_<name>.glb.

    blender -b --factory-startup --python-exit-code 1 -P tools/make_people_mpfb.py -- OUT_DIR [NAME ...]
        [--preview DIR]

--preview renders each look in its rest pose on the CPU (Cycles, vertex colours only), as the game will colour it
before the tints.
"""
import argparse
import math
import os
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TPOSE = os.path.join(ROOT, "assets", "models", "people3d", "anims2.glb")

KEEP, TOP, STRIPE, LEGS, HAIR, SKIN = 255, 200, 170, 140, 100, 50


def look(gender, age, skin, hair, clothes, tris=3200, muscle=0.5, weight=0.5, height=0.5, race=(0.05, 0.05, 0.9),
         brows="eyebrow003", stripes=False):
    """A look: MakeHuman's macros (age 0.5 is 25 years, 1.0 is 90), a skin, hair ("" none), clothes (the first is
    split into top and legs; shoes and gloves keep their colours), the triangles it keeps, reflective tape across the
    legs' chest and shins."""
    return {"phenotype": {"gender": gender, "age": age, "muscle": muscle, "weight": weight, "proportions": 0.6,
                          "height": height, "cupsize": 0.5, "firmness": 0.5,
                          "race": {"african": race[0], "asian": race[1], "caucasian": race[2]}},
            "skin": "%s/%s.mhmat" % (skin, skin), "hair": "%s/%s.mhclo" % (hair, hair) if hair else "",
            "eyebrows": "%s/%s.mhclo" % (brows, brows), "clothes": clothes, "tris": tris, "stripes": stripes}


# The podium's two (more triangles: they stand close to the camera) and the people by the road and in the crowds.
LOOKS = {
    # Mortimer Graves: old, thin, tall; a black suit, white shirt and tie, black gloves (the top hat is the game's).
    "graves": look(1.0, 0.9, "old_caucasian_male", "short02", ["male_elegantsuit01", "toigo_gloves_short", "shoes04"],
                   tris=14000, weight=0.3, height=0.8, muscle=0.35),
    # Ricky, the commentator: a jacket over a shirt and jeans (the game makes the jacket loud).
    "ricky": look(1.0, 0.62, "middleage_caucasian_male", "short04", ["male_casualsuit05", "shoes01"], tris=14000,
                  weight=0.65, height=0.55),
    # Overalls over a T-shirt: the marshals (orange, reflective stripes), the mechanics (blue), the workers (yellow).
    "overalls": look(1.0, 0.55, "middleage_caucasian_male", "short03", ["male_worksuit01", "shoes03"], muscle=0.6,
                     weight=0.6, stripes=True),
    # A black suit (the undertakers, the mourners).
    "suit": look(1.0, 0.75, "middleage_caucasian_male", "short02", ["male_elegantsuit01", "shoes04"], weight=0.45,
                 height=0.6),
    # A shabby jacket over jeans, old (the tramps).
    "tramp": look(1.0, 0.85, "old_caucasian_male", "cortu_short_messy_hair", ["male_casualsuit05", "shoes02"],
                  weight=0.35),
    # A blouse and a skirt (the widows and the female mourners).
    "widow": look(0.0, 0.72, "old_caucasian_female", "bob02",
                  ["female_elegantsuit01", "shoes03"], weight=0.4, height=0.45),
    # The fans: a T-shirt and jeans (the game gives the shirt the team's colour).
    "fan_male": look(1.0, 0.45, "young_caucasian_male", "short03", ["male_casualsuit06", "shoes05"], weight=0.55),
    "fan_female": look(0.0, 0.42, "young_caucasian_female", "ponytail01", ["female_casualsuit01", "shoes06"],
                       weight=0.45),
}

# Each bone of the rig and the joint it points at, as in the animation library.
POINTS = {"pelvis": "spine_01", "spine_01": "spine_02", "spine_02": "spine_03", "spine_03": "neck_01",
          "neck_01": "Head"}
for s in "lr":
    POINTS.update({"clavicle_" + s: "upperarm_" + s, "upperarm_" + s: "lowerarm_" + s, "lowerarm_" + s: "hand_" + s,
                   "hand_" + s: "middle_01_" + s, "thigh_" + s: "calf_" + s, "calf_" + s: "foot_" + s,
                   "foot_" + s: "ball_" + s, "ball_" + s: "ball_leaf_" + s})
    for f in ("index", "middle", "ring", "pinky", "thumb"):
        POINTS.update({"%s_01_%s" % (f, s): "%s_02_%s" % (f, s), "%s_02_%s" % (f, s): "%s_03_%s" % (f, s),
                       "%s_03_%s" % (f, s): "%s_04_leaf_%s" % (f, s)})


def mpfb():
    bpy.ops.preferences.addon_enable(module="bl_ext.user_default.mpfb")
    from bl_ext.user_default.mpfb.services.humanservice import HumanService
    return HumanService


def cc0(spec):
    """Every asset the look uses must say it is CC0 (the game is published; MPFB's packs mix in AGPL hair)."""
    data = os.path.join(bpy.utils.user_resource("EXTENSIONS"), ".user", "user_default", "mpfb", "data")
    if not os.path.isdir(data):
        print("CC0 check skipped: no", data)
        return
    files = [os.path.join(data, "skins", spec["skin"]), os.path.join(data, "eyebrows", spec["eyebrows"])]
    files += [os.path.join(data, "clothes", c, c + ".mhclo") for c in spec["clothes"]]
    if spec["hair"]:
        files.append(os.path.join(data, "hair", spec["hair"]))
    for f in files:
        text = open(f, encoding="utf-8", errors="replace").read() if os.path.exists(f) else ""
        if "cc0" not in text.lower():
            raise SystemExit("not CC0 (or missing): " + f)


def build(spec):
    """The human with rig, skin, eyes, brows, hair and clothes: (armature, meshes, basemesh)."""
    cc0(spec)
    HumanService = mpfb()
    info = {
        "name": "person", "phenotype": spec["phenotype"], "rig": "game_engine",
        "eyes": "low-poly/low-poly.mhclo", "eyebrows": spec["eyebrows"], "eyelashes": "",
        "tongue": "", "teeth": "", "hair": spec["hair"], "proxy": "", "targets": [],
        "clothes": ["%s/%s.mhclo" % (c, c) for c in spec["clothes"]],
        "skin_mhmat": spec["skin"], "skin_material_type": "MAKESKIN", "eyes_material_type": "MAKESKIN",
        "clothes_material_type": "MAKESKIN", "skin_material_settings": {}, "eyes_material_settings": {},
        "expressions": [], "alternative_materials": {},
    }
    settings = HumanService.get_default_deserialization_settings()
    settings["subdiv_levels"] = 0
    basemesh = HumanService.deserialize_from_dict(info, settings)
    arm = basemesh.parent
    meshes = [o for o in bpy.data.objects if o.type == "MESH"]
    return arm, meshes, basemesh


def select_only(o):
    for s in bpy.context.selected_objects:
        s.select_set(False)
    o.select_set(True)
    bpy.context.view_layer.objects.active = o


def flatten(meshes):
    """Shape keys baked in, the masks (the body hidden under the clothes) applied."""
    for o in meshes:
        select_only(o)
        if o.data.shape_keys:
            bpy.ops.object.shape_key_remove(all=True, apply_mix=True)
        for m in list(o.modifiers):
            if m.type in ("MASK", "SUBSURF"):
                bpy.ops.object.modifier_apply(modifier=m.name)


def kind_of(o):
    from bl_ext.user_default.mpfb.services.objectservice import ObjectService
    return ObjectService.get_object_type(o) or ""


def texture(o, limit=256):
    """The object's colour texture: (full RGBA array, averaged-down RGBA array), sRGB values, row 0 at the bottom; the
    colours averaged by their alpha (cut-out hair keeps its strands' colour). None if it has none."""
    for slot in o.material_slots:
        m = slot.material
        if not m or not m.node_tree:
            continue
        imgs = [n.image for n in m.node_tree.nodes
                if n.type == "TEX_IMAGE" and n.image and n.image.colorspace_settings.name == "sRGB"]
        imgs.sort(key=lambda i: 0 if "diffuse" in i.name.lower() or "color" in i.name.lower() else 1)
        for img in imgs:
            w, h = img.size
            if w == 0 or h == 0:
                continue
            px = np.empty(w * h * 4, dtype=np.float32)
            img.pixels.foreach_get(px)
            full = px.reshape(h, w, 4)
            f = max(1, max(w, h) // limit)
            hh, ww = (h // f) * f, (w // f) * f
            blk = full[:hh, :ww].reshape(hh // f, f, ww // f, f, 4)
            a = blk[..., 3].sum(axis=(1, 3))
            rgb = (blk[..., :3] * blk[..., 3:4]).sum(axis=(1, 3)) / np.maximum(a[..., None], 1e-6)
            small = np.concatenate([rgb, (a / (f * f))[..., None]], axis=2)
            return full, small
    return None


def sample(img, uv):
    """Nearest texels of an RGBA array at UVs (n, 2), wrapped."""
    h, w = img.shape[:2]
    x = np.clip((np.mod(uv[:, 0], 1.0) * w).astype(int), 0, w - 1)
    y = np.clip((np.mod(uv[:, 1], 1.0) * h).astype(int), 0, h - 1)
    return img[y, x]


def loops_uv(me):
    n = len(me.loops)
    uv = np.zeros(n * 2, dtype=np.float32)
    if me.uv_layers.active:
        me.uv_layers.active.data.foreach_get("uv", uv)
    vi = np.zeros(n, dtype=np.int64)
    me.loops.foreach_get("vertex_index", vi)
    return uv.reshape(n, 2), vi


def vertex_colours(o, tex):
    """Each vertex's colour: its loops' texels averaged (RGBA, alpha the texture's)."""
    me = o.data
    nv = len(me.vertices)
    out = np.zeros((nv, 4), dtype=np.float32)
    if tex is None:
        out[:] = (0.5, 0.5, 0.5, 1.0)
        return out
    uv, vi = loops_uv(me)
    c = sample(tex[1], uv)
    cnt = np.bincount(vi, minlength=nv).astype(np.float32)
    for k in range(4):
        out[:, k] = np.bincount(vi, weights=c[:, k], minlength=nv) / np.maximum(cnt, 1)
    return out


def tri_bvh(o):
    """A BVH of the object's triangles in world space and, per triangle, its three UVs."""
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.transform(o.matrix_world)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bm.faces.ensure_lookup_table()
    uvl = bm.loops.layers.uv.active
    uvs = [[l[uvl].uv.copy() for l in f.loops] if uvl else [Vector((0, 0))] * 3 for f in bm.faces]
    pts = [[l.vert.co.copy() for l in f.loops] for f in bm.faces]
    tree = BVHTree.FromBMesh(bm)
    bm.free()
    return tree, uvs, pts


def lay_on_skin(body, cols, roles, cover, tex, reach, strength):
    """The brows or the hair laid onto the skin under them: each skin vertex within reach of the cover's triangles
    takes the cover's texel there (by its alpha), and the hair's role when it is mostly covered."""
    if tex is None:
        return
    tree, uvs, pts = tri_bvh(cover)
    full = tex[1]
    mw = body.matrix_world
    from mathutils.geometry import barycentric_transform
    for i, v in enumerate(body.data.vertices):
        co = mw @ v.co
        hit = tree.find_nearest(co, reach)
        if hit[0] is None:
            continue
        loc, _, fi, _ = hit
        p = pts[fi]
        u = uvs[fi]
        b = barycentric_transform(loc, p[0], p[1], p[2], Vector((u[0].x, u[0].y, 0)), Vector((u[1].x, u[1].y, 0)),
                                  Vector((u[2].x, u[2].y, 0)))
        t = sample(full, np.array([[b.x, b.y]], dtype=np.float32))[0]
        a = float(t[3]) * strength
        if a < 0.15:
            continue
        cols[i, :3] = cols[i, :3] * (1 - a) + t[:3] * a
        if a > 0.45:
            roles[i] = HAIR


def thin_hair(o, tex, keep=0.33):
    """Hair cards drawn solid: the faces mostly clear in the texture go."""
    if tex is None:
        return
    me = o.data
    uv, vi = loops_uv(me)
    a = sample(tex[1], uv)[:, 3]
    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    gone = []
    li = 0
    for f in bm.faces:
        n = len(f.loops)
        if a[li:li + n].mean() < keep:
            gone.append(f)
        li += n
    bmesh.ops.delete(bm, geom=gone, context="FACES")
    bm.to_mesh(me)
    bm.free()
    print("HAIR faces gone", len(gone))


def loose_parts(me):
    """Each vertex's connected part (an id)."""
    n = len(me.vertices)
    parent = list(range(n))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for e in me.edges:
        a, b = find(e.vertices[0]), find(e.vertices[1])
        if a != b:
            parent[a] = b
    return [find(i) for i in range(n)]


def garment_roles(o, hip_z):
    """The first garment's parts: those whose span is centred below the hips are the legs (trousers, overalls, a
    skirt), the rest the top (a jacket, a shirt, a blouse). The span, not the mean: an overall's bib holds more
    vertices than its legs."""
    me = o.data
    part = loose_parts(me)
    zs = {}
    for i, v in enumerate(me.vertices):
        zs.setdefault(part[i], []).append((o.matrix_world @ v.co).z)
    mid = {p: (min(z) + max(z)) / 2 for p, z in zs.items()}
    print("PARTS", o.name, "hip %.2f" % hip_z, sorted(((len(z), round(min(z), 2), round(max(z), 2), round(mid[p], 2))
                                                     for p, z in zs.items()), reverse=True)[:6])
    return [LEGS if mid[part[i]] < hip_z else TOP for i in range(len(me.vertices))]


def stripes(o):
    """Reflective tape on the legs' clothes (an overall: across the bib and round the shins), cut in after the
    decimation: the mesh bisected at the bands' edges and split there, so the colour stops sharply; the faces between
    take the STRIPE role (the weights and colours of the new vertices interpolated along the cut edges)."""
    me = o.data
    inv = o.matrix_world.inverted()
    top = max((o.matrix_world @ v.co).z for v in me.vertices)
    bm = bmesh.new()
    bm.from_mesh(me)
    col = bm.verts.layers.float_color.get("Col")

    def legs(f):
        return all(abs(v[col][3] * 255 - LEGS) < 12 for v in f.verts)

    bands = [(0.725 * top, 0.755 * top), (0.222 * top, 0.25 * top)]
    for lo, hi in bands:
        for z in (lo, hi):
            faces = [f for f in bm.faces if legs(f)]
            edges = list({e for f in faces for e in f.edges})
            verts = list({v for f in faces for v in f.verts})
            res = bmesh.ops.bisect_plane(bm, geom=verts + edges + faces, plane_co=inv @ Vector((0, 0, z)),
                                         plane_no=(inv.to_3x3() @ Vector((0, 0, 1))).normalized())
            cut = [e for e in res["geom_cut"] if isinstance(e, bmesh.types.BMEdge)]
            bmesh.ops.split_edges(bm, edges=cut)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    marked = 0
    for f in bm.faces:
        z = (o.matrix_world @ f.calc_center_median()).z
        if legs(f) and any(lo < z < hi for lo, hi in bands):
            for v in f.verts:
                c = v[col]
                v[col] = (c[0], c[1], c[2], STRIPE / 255.0)
            marked += 1
    bm.to_mesh(me)
    bm.free()
    print("STRIPES faces", marked)


def joints(arm):
    return {pb.name: arm.matrix_world @ pb.head for pb in arm.pose.bones}


def basis(j):
    up = (j["Head"] - j["pelvis"]).normalized()
    side = (j["thigh_l"] - j["thigh_r"]).normalized()
    fwd = side.cross(up).normalized()
    side = up.cross(fwd).normalized()
    return Matrix((side, up, fwd)).transposed()


def library_tpose():
    """The animation library's T-pose: its joints in world space, imported and removed again."""
    before = set(bpy.data.objects)
    actions_before = set(bpy.data.actions)
    bpy.ops.import_scene.gltf(filepath=TPOSE)
    new = [o for o in bpy.data.objects if o not in before]
    arm = next(o for o in new if o.type == "ARMATURE")
    act = next(a for a in bpy.data.actions if a not in actions_before and a.name.startswith("A_TPose"))
    arm.animation_data.action = act
    if hasattr(arm.animation_data, "action_slot") and act.slots:
        arm.animation_data.action_slot = act.slots[0]
    bpy.context.scene.frame_set(int(act.frame_range[0]))
    bpy.context.view_layer.update()
    j = joints(arm)
    for o in new:
        bpy.data.objects.remove(o, do_unlink=True)
    for a in list(bpy.data.actions):
        if a not in actions_before:
            bpy.data.actions.remove(a)
    return j


def tpose(arm, lib):
    """Bones renamed as the clips want them; each bone (parents first) turned to point where the library's does,
    keyed as the clip "A_TPose"."""
    for b in arm.data.bones:
        if b.name == "Root":
            b.name = "root"
        elif b.name == "head":
            b.name = "Head"
    bpy.context.view_layer.update()
    turn = basis(joints(arm)) @ basis(lib).inverted()
    inv = arm.matrix_world.to_3x3().inverted()
    order = []

    def walk(b):
        order.append(b.name)
        for c in b.children:
            walk(c)

    for b in arm.data.bones:
        if b.parent is None:
            walk(b)
    for pb in arm.pose.bones:
        pb.rotation_mode = "QUATERNION"
    for name in order:
        if name not in POINTS or name not in lib or POINTS[name] not in lib:
            continue
        pb = arm.pose.bones[name]
        j = joints(arm)
        tip = j[POINTS[name]] if POINTS[name] in j else arm.matrix_world @ pb.tail
        have = inv @ (tip - j[name])
        want = inv @ (turn @ (lib[POINTS[name]] - lib[name]))
        if have.length < 1e-5 or want.length < 1e-5:
            continue
        q = have.normalized().rotation_difference(want.normalized())
        head = pb.head.copy()
        pb.matrix = Matrix.Translation(head) @ q.to_matrix().to_4x4() @ Matrix.Translation(-head) @ pb.matrix
        bpy.context.view_layer.update()
    arm.animation_data_create()
    act = bpy.data.actions.new("A_TPose")
    arm.animation_data.action = act
    for frame in (0, 1):
        for pb in arm.pose.bones:
            pb.keyframe_insert("rotation_quaternion", frame=frame)
            pb.keyframe_insert("location", frame=frame)
    return act


def set_colours(o, cols, roles):
    """The colours (sRGB values written as they are: the game reads them as its own colours) and the roles as alpha,
    on the vertices."""
    me = o.data
    for a in list(me.color_attributes):
        me.color_attributes.remove(a)
    attr = me.color_attributes.new("Col", "FLOAT_COLOR", "POINT")
    data = np.concatenate([np.clip(cols[:, :3], 0, 1), (np.asarray(roles, dtype=np.float32) / 255.0)[:, None]], axis=1)
    attr.data.foreach_set("color", data.astype(np.float32).ravel())
    me.color_attributes.active_color = attr
    me.color_attributes.render_color_index = 0


def make(name, out_dir, preview_dir):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mpfb()
    lib = library_tpose()
    spec = LOOKS[name]
    arm, meshes, body = build(spec)
    flatten(meshes)
    hip_z = (arm.matrix_world @ arm.pose.bones["pelvis"].head).z
    first = None
    for c in spec["clothes"]:
        first = next((o for o in meshes if c.lower() in (o.name + " " + o.data.name + " " + " ".join(
            s.material.name for s in o.material_slots if s.material)).lower()), None)
        break
    kinds = {o.name: kind_of(o) for o in meshes}
    texs = {o.name: texture(o) for o in meshes}
    cols, roles = {}, {}
    for o in meshes:
        cols[o.name] = vertex_colours(o, texs[o.name])
        k = kinds[o.name]
        if k == "Hair":
            roles[o.name] = [HAIR] * len(o.data.vertices)
        elif o is first:
            roles[o.name] = garment_roles(o, hip_z)
        elif o is body:
            roles[o.name] = [SKIN] * len(o.data.vertices)
        else:
            roles[o.name] = [KEEP] * len(o.data.vertices)
    # The brows and the hair laid onto the skin (the scalp between the hair's cards takes their colour).
    for o in meshes:
        k = kinds[o.name]
        if k == "Eyebrows":
            lay_on_skin(body, cols[body.name], roles[body.name], o, texs[o.name], 0.006, 0.9)
        elif k == "Hair":
            lay_on_skin(body, cols[body.name], roles[body.name], o, texs[o.name], 0.03, 1.0)
    for o in list(meshes):
        if kinds[o.name] in ("Eyebrows", "Eyelashes"):
            meshes.remove(o)
            bpy.data.objects.remove(o, do_unlink=True)
    for o in meshes:
        set_colours(o, cols[o.name], roles[o.name])
        if kinds[o.name] == "Hair":
            thin_hair(o, texs[o.name])
    # One mesh, one material, no UVs: one glTF primitive the game skins.
    for s in bpy.context.selected_objects:
        s.select_set(False)
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.join()
    me = body.data
    me.materials.clear()
    mat = bpy.data.materials.new("person")
    me.materials.append(mat)
    while me.uv_layers:
        me.uv_layers.remove(me.uv_layers[0])
    for a in ("sharp_edge", "sharp_face"):
        if a in me.attributes:
            me.attributes.remove(me.attributes[a])
    select_only(body)
    try:
        bpy.ops.mesh.customdata_custom_splitnormals_clear()
    except RuntimeError:
        pass
    # Decimated (before the armature in the stack), triangulated, smooth.
    bm = bmesh.new()
    bm.from_mesh(me)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bm.to_mesh(me)
    bm.free()
    before = len(me.polygons)
    mod = body.modifiers.new("thin", "DECIMATE")
    mod.ratio = min(1.0, spec["tris"] / max(1, before))
    mod.use_collapse_triangulate = True
    mod.use_symmetry = True
    mod.symmetry_axis = "X"
    while body.modifiers.find("thin") > 0:
        bpy.ops.object.modifier_move_up(modifier="thin")
    bpy.ops.object.modifier_apply(modifier="thin")
    if spec["stripes"]:
        stripes(body)
    me = body.data
    me.polygons.foreach_set("use_smooth", [True] * len(me.polygons))
    me.update()
    print("LOOK", name, "tris", before, "->", len(me.polygons), "verts", len(me.vertices))
    tpose(arm, lib)
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "look_%s.glb" % name)
    kw = dict(filepath=path, export_format="GLB", export_apply=True, export_skins=True, export_animations=True,
              export_animation_mode="ACTIONS", export_yup=True, export_morph=False, export_anim_slide_to_zero=True,
              export_materials="NONE", export_texcoords=False)
    try:
        bpy.ops.export_scene.gltf(export_vertex_color="ACTIVE", **kw)
    except TypeError:
        bpy.ops.export_scene.gltf(export_colors=True, **kw)
    print("WROTE", path, os.path.getsize(path))
    if preview_dir:
        preview(os.path.join(preview_dir, "look_%s.png" % name), arm, body)


def preview(path, arm, body):
    """The look in its rest pose from the front three-quarters, on the CPU (Cycles), vertex colours only."""
    arm.data.pose_position = "REST"
    scn = bpy.context.scene
    scn.render.engine = "CYCLES"
    scn.cycles.device = "CPU"
    scn.cycles.samples = 24
    scn.render.resolution_x, scn.render.resolution_y = 420, 760
    world = bpy.data.worlds.new("w")
    world.color = (0.35, 0.36, 0.4)
    scn.world = world
    mat = body.data.materials[0]
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
    ca = nt.nodes.new("ShaderNodeVertexColor")
    ca.layer_name = "Col"
    # The colours are sRGB values stored as they are: shown as the game shows them.
    gamma = nt.nodes.new("ShaderNodeGamma")
    gamma.inputs[1].default_value = 2.2
    nt.links.new(ca.outputs["Color"], gamma.inputs[0])
    nt.links.new(gamma.outputs[0], bsdf.inputs["Base Color"])
    bsdf.inputs["Roughness"].default_value = 0.8
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    scn.collection.objects.link(cam)
    cam.location = (1.6, -3.4, 1.1)
    cam.rotation_euler = (math.radians(88), 0, math.radians(25))
    cam.data.lens = 55
    scn.camera = cam
    for nm, loc, energy in (("key", (3, -3, 4), 900), ("rim", (-3, 3, 3), 500), ("fill", (-3, -4, 1), 250)):
        light = bpy.data.objects.new(nm, bpy.data.lights.new(nm, "AREA"))
        light.data.energy = energy
        light.data.size = 3
        light.location = loc
        light.rotation_euler = (Vector((0, 0, 1)) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
        scn.collection.objects.link(light)
    scn.render.filepath = path
    bpy.ops.render.render(write_still=True)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("names", nargs="*")
    ap.add_argument("--preview", default="")
    a = ap.parse_args(argv)
    for name in a.names or sorted(LOOKS):
        make(name, os.path.abspath(a.out), os.path.abspath(a.preview) if a.preview else "")


main()
