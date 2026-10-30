#!/usr/bin/env python3
# source.half_store_rounded (REDS-1 item 5) - EVERY HALF-FLOAT STORE ROUNDS TO NEAREST FIRST.
#
# WHY: an rgba16f image store CONVERTS BY TRUNCATION on NVIDIA 595.x (Vulkan leaves the f32 -> f16 store's rounding to the
# implementation). Measured on gi.card_truth's lamp-lit wall: 1,086 stored texels lay in (-1.25, 0] half-float steps of
# the analytic, none above - every value half a step low (-0.024..-0.049 % by binade), a bias at the stores' own quantum
# (gi.card_truth T1, the voxel light store's series limit, the hold rule written for round-to-nearest). The helper
# jahRoundHalf (irisgl engine/src/rayquery/include/jah_half_round.glsl; the Hlms piece JahHalfRound) adds half a step
# on the float's bits so the truncating store rounds to nearest.
#
# THE LAW, two halves:
#   (1) THE RAY JOBS: every imageStore into an image declared rgba16f / rg16f / r16f in engine/src/rayquery/*.comp
#       carries jahRoundHalf in its stored value.
#   (2) THE HLMS COMPUTE MEDIA whose formats are bound at run time (no qualifier in the text): the stores named below -
#       the Photon voxel light store (the injection, the bounce, the directional levels) and the cards' relight - write
#       through jahRoundHalf or a store macro that is it (jahStoreLight / jahStoreDirect, whose float branch must be
#       jahRoundHalf), and each job that writes them lists the piece JahHalfRound.
# A NEGATIVE CONTROL runs first (--self-test always): a synthetic job with an unrounded half store must be refused.
import json, os, re, sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else '.'
ENGINE = os.path.join(ROOT, 'irisgl', 'engine')

def half_images(text):
    return {m.group(2) for m in re.finditer(
        r'layout\(\s*(rgba16f|rg16f|r16f)\s*,[^)]*\)\s*uniform\s+(?:\w+\s+)*image\w+\s+(\w+)', text)}

def stores(text, names, verbs=('imageStore',)):
    """(name, line, the stored value's text) for every store into one of `names`"""
    out = []
    for m in re.finditer(r'\b(' + '|'.join(verbs) + r')\(\s*(\w+)\b', text):
        if m.group(2) not in names:
            continue
        i = text.index('(', m.start())
        depth, commas, j = 0, [], i
        while j < len(text):
            c = text[j]
            if c in '([':
                depth += 1
            elif c in ')]':
                depth -= 1
                if depth == 0:
                    break
            elif c == ',' and depth == 1:
                commas.append(j)
            j += 1
        value = text[commas[-1] + 1:j] if commas else ''
        out.append((m.group(2), text[:m.start()].count('\n') + 1, value))
    return out

ROUNDED = re.compile(r'\b(jahRoundHalf|jahStoreLight|jahStoreDirect)\s*\(')

def check_ray_job(path, text):
    bad = []
    for name, line, value in stores(text, half_images(text)):
        if not ROUNDED.search(value):
            bad.append('%s:%d imageStore( %s, ... ) stores an unrounded value into a half-float image' % (path, line, name))
    return bad

# (2) the Hlms compute media: file -> the images it stores into whose bound format is half float
HLMS_STORES = {
    'media/Photon/Voxel/LightInjection_piece_cs.any':
        ('LightInjection_cs', {'lightVoxel', 'directVoxel', 'backVoxel', 'directBackVoxel', 'dirOut0', 'dirOut1', 'dirOut2',
                               'halfLightX', 'halfLightY', 'halfLightZ', 'halfDirectX', 'halfDirectY', 'halfDirectZ'}),
    'media/Photon/Voxel/LightVctBounceInject_piece_cs.any':
        ('LightVctBounceInject_cs', {'lightVoxel', 'backScratch', 'halfLightX', 'halfLightY', 'halfLightZ'}),
    'media/Photon/Voxel/AnisotropicMipVctStep0_piece_cs.any':
        ('AnisotropicMipVctStep0_cs', {'outLightHigherMip0', 'outLightHigherMip1', 'outLightHigherMip2'}),
    'media/Photon/Voxel/AnisotropicMipVctStep1_piece_cs.any':
        ('AnisotropicMipVctStep1_cs', {'outLightHigherMip0', 'outLightHigherMip1', 'outLightHigherMip2'}),
    'media/Hlms/Jahshaka/JahCardLight_cs.glsl':
        ('JahCardLight_cs', {'cardRadiance', 'cardGather', 'cardIndirect', 'cardFlat'}),
}
MACRO_FLOAT = re.compile(r'#define\s+(jahStoreLight|jahStoreDirect)\(\s*x\s*\)\s*(.*)')

def check_hlms(path, text, pieces):
    bad = []
    names = HLMS_STORES[path][1]
    found = stores(text, names, ('imageStore', 'OGRE_imageWrite3D4', 'OGRE_imageWrite3D1'))
    if not found:
        bad.append('%s: none of its listed half-float stores found (%s) - the guard reads stale names' % (path, sorted(names)))
    for name, line, value in found:
        if not ROUNDED.search(value):
            bad.append('%s:%d a store into %s holds an unrounded value' % (path, line, name))
    for m in MACRO_FLOAT.finditer(text):
        body = m.group(2).strip()
        if not (body.startswith('toSRGB') or body.startswith('jahRoundHalf')):
            bad.append('%s: #define %s( x ) %s - its float branch must be jahRoundHalf( x )' % (path, m.group(1), body))
    if 'JahHalfRound_piece_all.any' not in pieces:
        bad.append('%s: its job (%s) does not list JahHalfRound_piece_all.any' % (path, HLMS_STORES[path][0]))
    if path.endswith('.any') and '@insertpiece( JahHalfRound )' not in text:
        bad.append('%s: no @insertpiece( JahHalfRound ) - jahRoundHalf is undefined there' % path)
    return bad

def job_pieces(source):
    """the piece list of the compute job whose source is `source`, from the engine's material JSONs"""
    for js in ('media/Photon/Voxel/Voxelizer.material.json', 'media/Hlms/Jahshaka/JahshakaCompute.material.json'):
        text = open(os.path.join(ENGINE, js)).read()
        for m in re.finditer(r'"source"\s*:\s*"' + re.escape(source) + r'"\s*,\s*"pieces"\s*:\s*(\[[^\]]*\])', text):
            return json.loads(m.group(1))
    return []

def self_test():
    job = ('layout( rgba16f, set = 0, binding = 1 ) uniform writeonly image2D outImg;\n'
           'void main() { imageStore( outImg, ivec2( 0 ), vec4( 1.0 ) ); }\n')
    good = job.replace('vec4( 1.0 ) )', 'jahRoundHalf( vec4( 1.0 ) ) )')
    return len(check_ray_job('control.comp', job)) == 1 and not check_ray_job('control.comp', good)

def main():
    if not self_test():
        print('FAIL: the negative control - an unrounded half-float store in a synthetic job was not refused')
        return 1
    print('ok   the negative control: an unrounded half-float store is refused, a rounded one passes')
    bad, ray_stores = [], 0
    rq = os.path.join(ENGINE, 'src', 'rayquery')
    for f in sorted(os.listdir(rq)):
        if f.endswith('.comp'):
            text = open(os.path.join(rq, f)).read()
            ray_stores += len(stores(text, half_images(text)))
            bad += check_ray_job('engine/src/rayquery/' + f, text)
    for path, (source, names) in sorted(HLMS_STORES.items()):
        bad += check_hlms(path, open(os.path.join(ENGINE, path)).read(), job_pieces(source))
    for b in bad:
        print('FAIL ' + b)
    print('%s: %d half-float stores in the ray jobs, %d Hlms media files with their jobs; %d unrounded' %
          ('FAILED' if bad else 'PASSED', ray_stores, len(HLMS_STORES), len(bad)))
    return 1 if bad else 0

sys.exit(main())
