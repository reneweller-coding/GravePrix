"""Bakes the stands' spectators (the Forge's part crowd, src/Forge.cpp buildCrowd): Quaternius' CC0 base bodies (the
same as src/People.cpp's) posed standing (Idle_Loop) and seated (Sitting_Idle_Loop), the pose applied, decimated to a
few hundred triangles; every vertex keeps the body part it belongs to (its strongest bone), so the game paints the
clothes and marks the arms and legs for the shader's cheering. Writes assets/models/people3d/crowd_<sex>_<pose>.txt:

    v x y z nx ny nz region      (game space: metres, y up, the figure faces -z, its feet at y = 0)
    t a b c

Regions: 0 head and neck, 1 torso, 2 upper arm, 3 forearm, 4 hand, 5 hips, 6 thigh, 7 shin, 8 foot.

    blender -b --factory-startup --python-exit-code 1 -P tools/crowd_templates.py -- QUATERNIUS_DIR OUT_DIR [TRIS]
"""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, kdtree

argv = sys.argv[sys.argv.index("--") + 1:]
src, out = argv[0], argv[1]
target = int(argv[2]) if len(argv) > 2 else 260

REGION = {"Head": 0, "neck_01": 0, "spine_01": 1, "spine_02": 1, "spine_03": 1, "clavicle_l": 1, "clavicle_r": 1,
          "upperarm_l": 2, "upperarm_r": 2, "lowerarm_l": 3, "lowerarm_r": 3, "pelvis": 5, "thigh_l": 6,
          "thigh_r": 6, "calf_l": 7, "calf_r": 7, "foot_l": 8, "foot_r": 8, "ball_l": 8, "ball_r": 8}


def region_of(name):
    if name in REGION:
        return REGION[name]
    if name.startswith(("hand_", "index", "middle", "pinky", "ring", "thumb")):
        return 4
    return 1


def turn(arm, bone, axis, degrees):
    """Turns a bone about an axis of the armature's space (x sideways, y back, z up) round its head."""
    pb = arm.pose.bones.get(bone)
    if pb is None:
        return
    h = pb.head.copy()
    r = Matrix.Rotation(math.radians(degrees), 4, axis)
    pb.matrix = Matrix.Translation(h) @ r @ Matrix.Translation(-h) @ pb.matrix
    bpy.context.view_layer.update()


def bake(sex, pose):
    """pose: Idle_Loop (standing, arms down) or Sitting_Idle_Loop (seated, hands on the thighs); set by hand: the
    animation library's rig names its bones differently (the game retargets, Blender does not)."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=os.path.join(src, f"Superhero_{sex}_FullBody.gltf"))
    arm = next(o for o in bpy.data.objects if o.type == "ARMATURE")
    body = max((o for o in bpy.data.objects if o.type == "MESH"), key=lambda o: len(o.data.vertices))
    for o in list(bpy.data.objects):
        if o.type == "MESH" and o is not body:
            bpy.data.objects.remove(o, do_unlink=True)
    # Which side each arm is on (its hand's x), so it swings down on its own side.
    for side in ("l", "r"):
        hand = arm.pose.bones.get("hand_" + side)
        sgn = 1.0 if hand is not None and hand.head.x > 0 else -1.0
        turn(arm, "upperarm_" + side, "Y", 72.0 * sgn)
        turn(arm, "lowerarm_" + side, "X", -12.0)
    if pose != "Idle_Loop":
        for side in ("l", "r"):
            turn(arm, "thigh_" + side, "X", -88.0)
            turn(arm, "calf_" + side, "X", 86.0)
            turn(arm, "lowerarm_" + side, "X", -55.0)
        turn(arm, "spine_01", "X", 6.0)
    # Each vertex's strongest bone, before the pose is applied.
    names = {g.index: g.name for g in body.vertex_groups}
    region = []
    for v in body.data.vertices:
        best, w = 1, -1.0
        for g in v.groups:
            if g.weight > w:
                w, best = g.weight, region_of(names.get(g.group, ""))
        region.append(best)
    # The pose applied, in world space (the armature's own turn and scale included).
    dg = bpy.context.evaluated_depsgraph_get()
    ev = body.evaluated_get(dg)
    posed = bpy.data.meshes.new_from_object(ev, depsgraph=dg)
    posed.transform(body.matrix_world)
    full = [v.co.copy() for v in posed.vertices]
    tree = kdtree.KDTree(len(full))
    for i, co in enumerate(full):
        tree.insert(co, i)
    tree.balance()
    # Decimated to the target, triangulated, normals smooth.
    bm = bmesh.new()
    bm.from_mesh(posed)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.002)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    me = bpy.data.meshes.new("lo")
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new("lo", me)
    bpy.context.scene.collection.objects.link(ob)
    mod = ob.modifiers.new("dec", "DECIMATE")
    mod.ratio = min(1.0, target / max(1, len(me.polygons)))
    mod.use_collapse_triangulate = True
    dg = bpy.context.evaluated_depsgraph_get()
    lo = bpy.data.meshes.new_from_object(ob.evaluated_get(dg), depsgraph=dg)
    bm = bmesh.new()
    bm.from_mesh(lo)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    bm.to_mesh(lo)
    bm.free()
    lo.calc_loop_triangles()
    # Game space: Blender's z up is the game's y; the face to -z.
    zs = [v.co.z for v in lo.vertices]
    floor, top = min(zs), max(zs)
    scale = (1.75 if pose == "Idle_Loop" else 1.75) / max(0.5, (top - floor) if pose == "Idle_Loop" else 1.0)
    if pose != "Idle_Loop":
        scale = 1.0 / getattr(bake, "body_height", 1.0) * 1.75
    else:
        bake.body_height = top - floor
    head = [full[i] for i, r in enumerate(region) if r == 0]
    hx = sum(c.x for c in head) / len(head)
    hy = sum(c.y for c in head) / len(head)
    # The nose sticks out the way the face looks: the farthest head vertex from the head's middle, sideways.
    nose = max(head, key=lambda c: (c.x - hx) ** 2 + (c.y - hy) ** 2)
    face_minus_y = True   # the toes point to -y
    cx = sum(v.co.x for v in lo.vertices) / len(lo.vertices)
    cy = sum(v.co.y for v in lo.vertices) / len(lo.vertices)
    path = os.path.join(out, f"crowd_{sex.lower()}_{'stand' if pose == 'Idle_Loop' else 'sit'}.txt")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# {sex} {pose}: {len(lo.loop_triangles)} triangles (tools/crowd_templates.py, Quaternius CC0)\n")
        for v in lo.vertices:
            _, idx, _ = tree.find(v.co)
            k = 1.0 if face_minus_y else -1.0
            x, y, z = (v.co.x - cx) * scale * k, (v.co.z - floor) * scale, (v.co.y - cy) * scale * k
            n = v.normal
            f.write("v %.4f %.4f %.4f %.3f %.3f %.3f %d\n" % (x, y, z, n.x * k, n.z, n.y * k, region[idx]))
        for t in lo.loop_triangles:
            f.write("t %d %d %d\n" % tuple(t.vertices))
    print(path, len(lo.vertices), "verts", len(lo.loop_triangles), "tris", "face -y" if face_minus_y else "face +y")


os.makedirs(out, exist_ok=True)
for sex in ("Male", "Female"):
    bake.body_height = 1.0
    for pose in ("Idle_Loop", "Sitting_Idle_Loop"):
        bake(sex, pose)
