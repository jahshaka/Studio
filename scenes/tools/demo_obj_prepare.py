#!/usr/bin/env python3
"""DEMO-SCENES-1: makes an OBJ scene directory readable by our model door on Linux.

Usage: python3 -I demo_obj_prepare.py <source dir> <out dir>

San Miguel's .mtl names every map Windows-style (`map_Kd textures\\foo.png`). On Linux that is ONE
file name with a backslash in it, so the import resolves none of the 265 diffuse / 55 normal / 2
specular maps and every material arrives white (measured: 1655 mesh nodes, 0 maps). The importer
fix belongs in irisgl/import/materialhelper.cpp, a bake-key file, so it is out of this lane's
scope and reported instead; this tool makes the input right: every file is HARD-LINKED into
<out dir> (same tree, no copy; a symlink would resolve outside the model's directory and the
import's texture containment would refuse it) and each .mtl is rewritten with forward slashes.
"""
import os, sys
src, out = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
links = rewrites = 0
for d, _, files in os.walk(src):
    rel = os.path.relpath(d, src)
    od = os.path.normpath(os.path.join(out, rel))
    os.makedirs(od, exist_ok=True)
    for f in files:
        s, o = os.path.join(d, f), os.path.join(od, f)
        if os.path.lexists(o): os.remove(o)
        if f.lower().endswith('.mtl'):
            text = open(s, 'rb').read().replace(b'\\', b'/')
            open(o, 'wb').write(text); rewrites += 1
        else:
            os.link(s, o); links += 1
print(f'{links} files linked, {rewrites} .mtl rewritten -> {out}')
