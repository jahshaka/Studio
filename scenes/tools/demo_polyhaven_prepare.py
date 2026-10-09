#!/usr/bin/env python3
"""DEMO-SCENES-1: the one Poly Haven map our material cannot take as shipped.

Usage: python3 -I demo_polyhaven_prepare.py <poly-haven extracted dir> <out dir> [<alpha dir>]

A Poly Haven ground material ships its roughness PACKED: *_arm_4k.jpg = R ambient occlusion,
G roughness, B metalness (the glTF ORM layout). A model's glTF names it as the metallicRoughness
texture and the import splits it; a material we bind BY HAND (the Namaqualand ground on the World
template's floors) needs the single-channel roughness picture itself. This writes
<out>/<material>_rough_4k.png (G) for every materials/<name>/textures/<name>_arm_4k.jpg. Occlusion has
no slot in our material and is not written; metalness is 0 for every ground here.

THE CUTOUTS (measured): Poly Haven's 4k glTF of wild_rooibos_bush and cheiridopsis_succulent marks the
leaves/twigs/flower materials alphaMode BLEND but binds a JPG base colour — no alpha anywhere in the
glTF's files, so the leaf cards draw as solid black quads (the atlas background). The coverage ships
separately as the asset's "Alpha" map (api.polyhaven.com/files/<asset> -> Alpha/4k/png; fetched into
<alpha dir>, md5-checked). With an <alpha dir>, this writes <out>/<model>_diff_alpha_2k.png = the diffuse
with that alpha (2048^2: the cutout reads at any distance a bush is seen from, at a quarter the VRAM).
"""
import os, sys
from PIL import Image
Image.MAX_IMAGE_PIXELS = None
src, out = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
os.makedirs(out, exist_ok=True)
n = 0
mats = os.path.join(src, 'materials')
for name in sorted(os.listdir(mats)):
    arm = os.path.join(mats, name, 'textures', name + '_arm_4k.jpg')
    if not os.path.isfile(arm): continue
    Image.open(arm).convert('RGB').getchannel('G').save(os.path.join(out, name + '_rough_4k.png'), compress_level=3)
    n += 1
print(f'{n} roughness maps -> {out}')

if len(sys.argv) > 3:
    alpha_dir = os.path.abspath(sys.argv[3])
    k = 0
    for f in sorted(os.listdir(alpha_dir)):
        if not f.endswith('_alpha_4k.png'): continue
        model = f[:-len('_alpha_4k.png')]
        diff = os.path.join(src, 'models', model, 'textures', model + '_diff_4k.jpg')
        if not os.path.isfile(diff): continue
        rgb = Image.open(diff).convert('RGB').resize((2048, 2048), Image.LANCZOS)
        a = Image.open(os.path.join(alpha_dir, f))
        if a.mode in ('I;16', 'I'):            # 16-bit grey: scale to 8 bits
            a = a.convert('I').point(lambda v: v / 257).convert('L')
        elif a.mode != 'L':                       # 16/8-bit RGB: the coverage is in every channel
            a = a.convert('RGB').getchannel('R')
        a = a.resize((2048, 2048), Image.LANCZOS)
        rgba = rgb.copy(); rgba.putalpha(a)
        rgba.save(os.path.join(out, model + '_diff_alpha_2k.png'), compress_level=3)
        k += 1
    print(f'{k} cutout base colours -> {out}')
