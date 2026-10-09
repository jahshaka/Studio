#!/usr/bin/env python3
"""DEMO-SCENES-1: converts the Amazon Lumberyard Bistro textures into what our import path reads.

Usage (run with -I; the source tree is DATA, nothing in it is executed):
    python3 -I demo_bistro_textures.py <Bistro_v5_2/Textures> <out dir> [jobs] [max size, default 1024]
    python3 -I demo_bistro_textures.py --js <out dir>      # the table make_demo_bistro.js embeds

Why it exists: the Bistro ships BC1/BC3/BC5 (DXT1/DXT5/ATI2) .dds and a few .tga, and neither our
image plugins (Qt here carries gif/ico/jpeg/svg only; png is built in) nor the import path decode
either. Pillow does (its DDS plugin decodes BC1-BC5/BC7), so every texture becomes a full-resolution
PNG, and NVIDIA's Falcor packing is unpacked into OUR layout:
  *_BaseColor  RGB = base colour, A = opacity            -> <name>.png (RGBA kept: alpha = the cutout)
  *_Specular   R = occlusion, G = roughness, B = metal   -> <name>_roughness.png (G), <name>_metallic.png (B)
                                                            (occlusion has no slot in our material: dropped)
  *_Normal     DirectX (green = -Y), two channels (BC5)  -> <name>.png, green flipped to OpenGL/glTF (+Y),
                                                            blue left at 255 (the shader rebuilds Z)
  *_Emissive   RGB emissive colour                      -> <name>.png
THE SIZE CAP (measured): our texture path stores what it is given uncompressed (RGBA8 + mips; it neither
reads BC-compressed DDS nor compresses at import), so the Bistro's 339 2048^2 maps at full size are ~7.7 GB
of VRAM — the validation open ran the 16 GB card out of memory mid-frame (9.2 GB held by Ogre). Every map
is therefore capped at <max size> (default 1024: ~1.9 GB), the one lossy step of this tool.
A manifest.json beside the output records per texture its size, the alpha cutout fraction (BaseColor)
and the mean emissive colour, which the scene script's table is generated from.
"""
import json, os, sys
from multiprocessing import Pool
from PIL import Image
Image.MAX_IMAGE_PIXELS = None

MAX = 1024

def convert(args):
    src, out = args
    base = os.path.splitext(os.path.basename(src))[0]
    rec = {'src': os.path.basename(src)}
    try:
        im = Image.open(src)
        im.load()
        rec['sourceSize'] = list(im.size)
        if max(im.size) > MAX:
            k = MAX / max(im.size)
            im = im.resize((max(1, round(im.size[0] * k)), max(1, round(im.size[1] * k))), Image.LANCZOS)
        rec['size'] = list(im.size)
        kind = base.rsplit('_', 1)[-1].lower()
        if kind == 'specular':
            rgb = im.convert('RGB')
            r, g, b = rgb.split()
            g.save(os.path.join(out, base + '_roughness.png'), optimize=False, compress_level=3)
            b.save(os.path.join(out, base + '_metallic.png'), optimize=False, compress_level=3)
            small = rgb.resize((64, 64))
            px = list(small.getdata())
            rec['meanRough'] = sum(p[1] for p in px) / len(px) / 255.0
            rec['meanMetal'] = sum(p[2] for p in px) / len(px) / 255.0
        elif kind == 'normal':
            rgb = im.convert('RGB')
            r, g, _ = rgb.split()
            g = g.point(lambda v: 255 - v)
            # Z is a constant 255: HlmsPbs samples two channels and rebuilds Z itself
            # (DOCS/traps/ENGINE.md, 'normal maps are two-channel'), so a computed Z is never read.
            b = Image.new('L', r.size, 255)
            Image.merge('RGB', (r, g, b)).save(os.path.join(out, base + '.png'), compress_level=3)
        else:
            img = im.convert('RGBA') if 'A' in im.getbands() else im.convert('RGB')
            if kind == 'basecolor' and img.mode == 'RGBA':
                a = img.getchannel('A').resize((128, 128))
                ap = list(a.getdata())
                rec['cut'] = sum(1 for v in ap if v < 128) / len(ap)
                if rec['cut'] == 0 and min(ap) == 255:
                    img = img.convert('RGB')   # an opaque alpha carries nothing
            if kind == 'emissive':
                small = img.convert('RGB').resize((64, 64))
                px = list(small.getdata())
                rec['meanEmissive'] = [sum(p[i] for p in px) / len(px) / 255.0 for i in range(3)]
            img.save(os.path.join(out, base + '.png'), compress_level=3)
        rec['ok'] = True
    except Exception as e:  # report, never stop the batch
        rec['ok'] = False; rec['error'] = str(e)
    return base, rec

def _set_max(m):
    global MAX
    MAX = m

def main():
    src, out = sys.argv[1], sys.argv[2]
    jobs = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    global MAX
    MAX = int(sys.argv[4]) if len(sys.argv) > 4 else 1024
    os.makedirs(out, exist_ok=True)
    files = sorted(os.path.join(src, f) for f in os.listdir(src)
                   if f.lower().endswith(('.dds', '.tga')))
    with Pool(jobs, initializer=_set_max, initargs=(MAX,)) as p:
        manifest = dict(p.map(convert, [(f, out) for f in files], chunksize=4))
    json.dump(manifest, open(os.path.join(out, 'manifest.json'), 'w'), indent=1, sort_keys=True)
    bad = [k for k, v in manifest.items() if not v['ok']]
    print(f'converted {len(manifest) - len(bad)} / {len(manifest)}; failed: {bad[:10]}')

def table_js(out):
    """--js <out dir>: the per-material table make_demo_bistro.js embeds, keyed by the material's
    texture prefix (`Foo` of Foo_BaseColor), lower case: c = cutout fraction of the base colour's
    alpha, e = mean emissive colour (present only where an _Emissive map exists), s = the Specular
    pack was split (roughness + metal maps exist)."""
    m = json.load(open(os.path.join(out, 'manifest.json')))
    rows = {}
    for base, rec in m.items():
        if not rec.get('ok'): continue
        prefix, kind = base.rsplit('_', 1)
        row = rows.setdefault(prefix.lower(), {})
        kind = kind.lower()
        if kind == 'basecolor': row['c'] = round(rec.get('cut', 0.0), 3)
        elif kind == 'emissive': row['e'] = [round(v, 3) for v in rec.get('meanEmissive', [0, 0, 0])]
        elif kind == 'specular': row['s'] = 1
    print('var BISTRO_MAPS = ' + json.dumps(rows, sort_keys=True, separators=(',', ':')) + ';')

if __name__ == '__main__':
    if sys.argv[1] == '--js': table_js(sys.argv[2])
    else: main()
