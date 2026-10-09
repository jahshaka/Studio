#!/usr/bin/env python3
"""DEMO-SCENES-1: reads an OBJ's .mtl (as DATA) and prints the polish table the scene script embeds.

Usage: python3 -I demo_mtl_table.py <file.mtl> <textures dir>   > table.json

One row per material: Kd, the maps (map_Kd / map_Bump / map_Ks / map_d, backslashes normalised), the
roughness the Blinn-Phong exponent means (GGX alpha = sqrt(2 / (Ns + 2)), perceptual roughness =
sqrt(alpha)), and whether the diffuse map's ALPHA is a cutout (any texel under 128 in a 128^2 read).
"""
import json, os, sys
from PIL import Image
Image.MAX_IMAGE_PIXELS = None

def cut_fraction(path):
    try:
        im = Image.open(path)
        if 'A' not in im.getbands(): return 0.0
        a = im.getchannel('A').resize((128, 128))
        px = a.tobytes()
        return sum(1 for v in px if v < 128) / len(px)
    except Exception:
        return 0.0

mtl, texdir = sys.argv[1], sys.argv[2]
rows, cur = {}, None
for line in open(mtl, encoding='latin-1'):
    t = line.strip().split(None, 1)
    if not t: continue
    k = t[0]; v = t[1] if len(t) > 1 else ''
    if k == 'newmtl': cur = rows.setdefault(v, {}); continue
    if cur is None: continue
    if k in ('Kd', 'Ks'): cur[k] = [float(x) for x in v.split()[:3]]
    elif k in ('Ns', 'd'): cur[k] = float(v.split()[0])
    elif k.lower() in ('map_kd', 'map_bump', 'bump', 'map_ks', 'map_d'):
        cur[k.lower().replace('bump', 'map_bump').replace('map_map_', 'map_')] = os.path.basename(v.split()[-1].replace('\\', '/'))
out = {}
for name, r in rows.items():
    ns = r.get('Ns', 16.0)
    alpha = (2.0 / (ns + 2.0)) ** 0.5
    row = {'kd': r.get('Kd', [0.8, 0.8, 0.8]), 'ks': r.get('Ks', [0, 0, 0]),
           'rough': round(alpha ** 0.5, 3), 'd': r.get('d', 1.0)}
    for key, short in (('map_kd', 'map'), ('map_bump', 'normal'), ('map_ks', 'spec'), ('map_d', 'opacity')):
        if key in r: row[short] = r[key]
    if 'map' in row:
        row['cut'] = round(cut_fraction(os.path.join(texdir, row['map'])), 3)
    out[name] = row
if not (len(sys.argv) > 3 and sys.argv[3] == '--js'):
    json.dump(out, sys.stdout, indent=0, sort_keys=True)

# ---- --js: the compact table make_demo_san_miguel.js embeds -------------------------------------
# Keyed by the diffuse map's file name (lower case, no extension) — the one identity a placed node
# still carries back to its MTL row (its baseColorMap asset's name). Rows that share one map merge
# (they differ only in a trace of Ks); untextured rows go to a list matched by Kd.
if len(sys.argv) > 3 and sys.argv[3] == '--js':
    maps, untex = {}, []
    for name in sorted(out):
        r = out[name]
        ks = round(max(r['ks']), 3)
        if 'map' not in r:
            untex.append({'name': name, 'kd': [round(v, 3) for v in r['kd']], 'ks': [round(v, 3) for v in r['ks']],
                          'r': r['rough'], 'd': r['d'], **({'n': r['normal']} if 'normal' in r else {})})
            continue
        key = os.path.splitext(r['map'])[0].lower()
        row = maps.setdefault(key, {'r': r['rough'], 'ks': ks, 'c': r.get('cut', 0.0), 'd': r['d']})
        row['ks'] = max(row['ks'], ks)
        if 'normal' in r: row['n'] = r['normal']
        if 'spec' in r: row['s'] = r['spec']
        row['d'] = min(row['d'], r['d'])
    print('var MTL_MAPS = ' + json.dumps(maps, sort_keys=True, separators=(',', ':')) + ';')
    print('var MTL_UNTEXTURED = ' + json.dumps(untex, separators=(',', ':')) + ';')
