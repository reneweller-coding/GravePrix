"""Blender half of make_people3d.py: the Quaternius base body (CC0) decimated, seams welded, triangulated; writes the
positions (glTF axes) and triangles to an .npz per ratio. Runs in Blender 5.2:

    blender --background --factory-startup --python tools/people3d_decimate.py -- BODY.gltf OUT_PREFIX 0.2,0.6
"""
import sys

import bmesh
import bpy
import numpy as np

argv = sys.argv[sys.argv.index("--") + 1:]
src, prefix, ratios = argv[0], argv[1], [float(r) for r in argv[2].split(",")]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=src)
meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
body = max(meshes, key=lambda o: len(o.data.polygons))
for r in ratios:
    bm = bmesh.new()
    bm.from_mesh(body.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
    me = bpy.data.meshes.new("dec")
    bm.to_mesh(me)
    bm.free()
    ob = bpy.data.objects.new("dec", me)
    bpy.context.scene.collection.objects.link(ob)
    ob.matrix_world = body.matrix_world
    mod = ob.modifiers.new("d", "DECIMATE")
    mod.ratio = r
    dg = bpy.context.evaluated_depsgraph_get()
    ev = ob.evaluated_get(dg)
    m2 = bpy.data.meshes.new_from_object(ev)
    bm = bmesh.new()
    bm.from_mesh(m2)
    bmesh.ops.triangulate(bm, faces=bm.faces)
    mw = ob.matrix_world
    v = np.array([tuple(mw @ x.co) for x in bm.verts], dtype=np.float32)
    t = np.array([[x.index for x in f.verts] for f in bm.faces], dtype=np.int32)
    bm.free()
    g = np.stack([v[:, 0], v[:, 2], -v[:, 1]], axis=1)   # Blender Z-up to glTF Y-up
    np.savez(f"{prefix}_{r:.2f}.npz", verts=g, tris=t)
    print("DEC", r, len(g), len(t))
