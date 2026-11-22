#!/usr/bin/env python3
"""source.webgpu_census - WHAT WEBGPU REFUSES, AND EVERY ONE-EYE-ONLY GATE, ONLY FALLS; MULTIVIEW ONLY RISES (DOORS-0 stage 0; V2 plan 0.5).

The doors (V2 2026-11-20, the owner): no new shader feature WebGPU refuses, no new raw Vulkan outside the ray tier / the
gather / the VR session, no new one-eye-only gate. Each class below is a comment-stripped count over its path set under
irisgl/engine, held at a CEILING taken at photon-7's tip (8075ee1f2, irisgl 4576b132; the census: spikes/doors-0/GROUND.md
B3, re-measured). THE LAW, both ways:
  count > ceiling  RED - every site of the class is printed (which one is new needs history: "ceiling N, found M");
  count < ceiling  RED - "lower the ceiling to M in the same commit", so a fall is recorded and cannot creep back;
  THE ONE FLOOR, MULTIVIEW (the target, CODING_RULES § 1): the law reversed - count < floor RED with every site (a per-eye
  pass lost), count > floor RED "raise the floor to M in the same commit" (a rise recorded, never undone silently);
  a class at 0 stays 0. The raw Vulkan class is held PER FILE (a file not in its table has ceiling 0); the three files
  the doors exempt (OgreRayQuery.cpp the ray tier, OgreScreenProbeGather.cpp the gather, OgreVrSession.cpp VR) are
  counted and printed, never held.
COMMENTS DO NOT COUNT: strip() blanks // and /* */ comments outside "string" and 'c' literals, keeping every newline (so
file:line stays true); #if 0 blocks DO count (a guard reads text, not the preprocessor). The self-test runs first, always:
the stripper's cases, then per class one synthetic violation at a path of its set must red, and the law's three cases.
Run: webgpu_census.py <source-root> [--counts]   (--counts prints the measured table, never fails: a builder's aid)
"""
import os
import re
import sys

# ---- the path sets (relative to irisgl/engine) ---------------------------------------------------------------------
# SHADERS: the GLSL Vulkan compiles - the build-time ray/compute modules and every Hlms/compositor GLSL piece. The media's
# .metal / .hlsl are other back ends (never compiled here); .material/.program/.compositor/.json carry no shader text
# except stage declarations, which the stage class reads ('shaders+decl').
SHADER_SRC = ('.comp', '.glsl', '.vert', '.frag')
SHADER_MEDIA = ('.glsl', '.any')
CPP = ('.cpp', '.h')
MEDIA_DECL = ('.material', '.program', '.compositor', '.json')


def in_set(rel, pset):
    src, media = rel.startswith('src/'), rel.startswith('media/')
    if pset == 'shaders':
        return (src and rel.endswith(SHADER_SRC)) or (media and rel.endswith(SHADER_MEDIA))
    if pset == 'shaders+decl':
        return in_set(rel, 'shaders') or (media and rel.endswith(MEDIA_DECL))
    if pset == 'cpp':
        return src and rel.endswith(CPP)
    if pset == 'src':
        return src and rel.endswith(CPP + SHADER_SRC)
    raise ValueError(pset)


# ---- THE TABLE: (class, regex, path set, ceiling, the plan item that lowers it) --------------------------------------
CLASSES = [
    ('device address', r'GL_EXT_buffer_reference\w*|\blayout\s*\([^)\n]*\bbuffer_reference\b', 'shaders', 67,
     'stage 4 (4.1 the stores final form: a bound buffer indexed by pool and offset); D5'),
    ('push constants', r'\bpush_constant\b', 'shaders', 5,
     'WebGPU program (64-B immediates); atomdraw'),
    ('image atomics', r'\bimageAtomic\w*\s*\(', 'shaders', 5,
     'WebGPU program (HdrMeter, Voxelizer, rq_reflect_filter to buffer atomics)'),
    ('read-write two-channel image', r'RW_RG', 'shaders', 6,
     'WebGPU program (split read and write images)'),
    ('texel buffers', r'\b[iu]?(samplerBuffer|imageBuffer|textureBuffer)\b', 'shaders', 3,
     'WebGPU program (storage buffers)'),
    ('early fragment tests', r'\bearly_fragment_tests\b', 'shaders', 2,
     'WebGPU program (Atom PS, JahFog)'),
    ('binding arrays', r'\b[iu]?(sampler|texture|image)\w*\s+\w+\s*\[', 'shaders', 123,
     'WebGPU program (texture arrays / atlases)'),
    ('subgroups', r'GL_KHR_shader_subgroup\w*|\bsubgroup\w+\s*\(|\bgl_(Num)?Subgroup\w*', 'shaders', 77,
     'WebGPU program (a subgroup use states its result is independent of the size)'),
    ('64-bit ints', r'\b(u?int64_t|[iu]64vec[234])\b|GL_\w*int64\w*', 'shaders', 0, 'keep 0'),
    ('fp16 types', r'\b(float16_t|f16vec[234]|f16mat\w*)\b|GL_EXT_shader_explicit_arithmetic_types\w*'
     r'|GL_EXT_shader_16bit_storage|GL_AMD_gpu_shader_half_float\w*', 'shaders', 0, 'keep 0'),
    ('nonuniform', r'\bnonuniformEXT\b|GL_EXT_nonuniform_qualifier', 'shaders', 0, 'keep 0'),
    ('geometry/tessellation stages', r'\b(EmitVertex|EndPrimitive|EmitStreamVertex|gl_TessLevel(Outer|Inner)'
     r'|gl_TessCoord|gl_PatchVerticesIn|max_vertices|geometry_program\w*|tessellation_(hull|domain)_program\w*)\b',
     'shaders+decl', 0, 'keep 0'),
    ('draw parameters', r'\bgl_(DrawID|BaseVertex|BaseInstance)(ARB)?\b|GL_ARB_shader_draw_parameters', 'shaders', 0,
     'keep 0'),
    ('scalar layout', r'GL_EXT_scalar_block_layout|\blayout\s*\([^)\n]*\bscalar\b', 'shaders', 0, 'keep 0'),
    # MULTIVIEW IS THE TARGET (CODING_RULES § 1, the owner and the lead 2026-11-21: both eyes in ONE pass): its count is a
    # FLOOR that only RISES - a fall is the red (a per-eye pass lost), a rise raises the floor in the same commit (FLOORS)
    ('multiview (the target)', r'\bgl_ViewIndex\b|GL_EXT_multiview', 'shaders', 9,
     'a FLOOR - only rises: every pass made per eye inside one pass adds its sites (CODING_RULES § 1)'),
    # a negated stereo VALUE or ACCESSOR (`!mStereo`, `!d.stereo`, `!view->stereo()`, `!(p.stereo.w > 0.5)`) is a gate; a negated
    # CALL whose name merely ends in Stereo and that TAKES ARGUMENTS (`!reflectChainMatchesStereo(stereo, eyeLayers)`) is a
    # predicate over the stereo value, true or false for either shape - never a gate (the lead, 2026-11-21: the census's first
    # false positive, OgreRayQuery.cpp's shape refusal); the lookahead refuses only that form, an accessor's `()` still counts
    # (16 for one commit of PHOTON-7-9A - the Photon term view's mono-only gate, OgreView.cpp - and back to 15 by its final
    # read's M4: the term's composite is one multiview quad, so the view runs on either eye shape and the gate is gone)
    ('one-eye-only gates', r'!(?!=)\s*\(*\s*(\w+\s*(\.|->)\s*)*(?!\w+Stereo\s*\(\s*[^)\s])\w*[sS]tereo\b', 'src', 15,
     'NO-RAY-CUT-1 (OgreView screenIntegrate, OgreChain ssr)'),
    # 10 -> 9 at PHOTON-7-9A: the reflection has no mode, so `fx.reflectSources = ReflectSources::Rays` is deleted
    ('one-eye-only gates: applyVrViewPolicy assignments', 'VR_POLICY', 'include', 9,
     'each pass made per layer deletes its line (bloom, ssao, smaa, hzb, refractions, distortion)'),
]
# THE CLASSES HELD AS A FLOOR (the count only rises): the rest are ceilings (the count only falls)
FLOORS = ('multiview (the target)',)
# The raw Vulkan class: \bvk[A-Z]\w*\( in engine/src C++, held per file. Growth is allowed only in the exempt three.
RAW_VK = r'\bvk[A-Z]\w*\('
RAW_VK_EXEMPT = ('src/OgreRayQuery.cpp', 'src/OgreScreenProbeGather.cpp', 'src/OgreVrSession.cpp')
RAW_VK_CEILINGS = {
    'src/OgreAtomCasterPass.cpp': 63,    # Atom's caster pass: an Ogre compute job (D7)
    'src/OgreAtomIdPass.cpp': 38,        # Atom's id pass: an Ogre compute job (D7)
    'src/VkDescriptorPools.h': 5,
    'src/OgreGpuCheckpoints.cpp': 3,
    'src/OgreEngine.cpp': 1,
    'src/OgreAlphaMasks.cpp': 1,
}
RAW_VK_LOWER = 'D7: a new compute pass is an Ogre compute job, never raw Vulkan'

RW_RG = re.compile(r'\blayout\s*\(([^)]*)\)\s*((?:\w+\s+)*?)[iu]?image\w+\s+\w+')   # the format anywhere in it
VR_POLICY_FILE = 'include/jahshaka/engine/Types.h'
VR_POLICY = re.compile(r'\binline\s+void\s+applyVrViewPolicy\s*\([^)]*\)\s*\{')
ASSIGN = re.compile(r'\b\w+\s*\.\s*\w+\s*=(?!=)')


# ---- the comment stripper ------------------------------------------------------------------------------------------
CHAR = re.compile(r"'(\\.|[^\\'\n])'")


def strip(text):
    """the text with every // and /* */ comment blanked (newlines kept); literals are skipped, never stripped"""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '/' and text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            i = j
        elif c == '/' and text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append('\n' * text.count('\n', i, j))
            i = j
        elif c == '"':
            j = i + 1
            while j < n and text[j] not in '"\n':
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif c == "'" and CHAR.match(text, i):
            m = CHAR.match(text, i)
            out.append(m.group(0))
            i = m.end()
        else:
            out.append(c)
            i += 1
    return ''.join(out)


# ---- counting ------------------------------------------------------------------------------------------------------
def sites(rel, code, rx):
    """[(rel, line, the line's text)] of every match of the class `rx` in the stripped `code`"""
    lines = code.split('\n')
    if rx == 'RW_RG':
        hits = [m.start() for m in RW_RG.finditer(code) if re.search(r'\brg\d\w*\b', m.group(1))
                and re.search(r'\buniform\b', m.group(2)) and not re.search(r'\b(readonly|writeonly)\b', m.group(2))]
    elif rx == 'VR_POLICY':
        hits = []
        m = VR_POLICY.search(code) if rel == VR_POLICY_FILE else None
        if m:
            depth, j = 1, m.end()
            while j < len(code) and depth:
                depth += {'{': 1, '}': -1}.get(code[j], 0)
                j += 1
            hits = [m.end() + a.start() for a in ASSIGN.finditer(code[m.end():j])]
    else:
        hits = [m.start() for m in re.finditer(rx, code)]
    out = []
    for h in hits:
        ln = code.count('\n', 0, h)
        out.append((rel, ln + 1, lines[ln].strip()))
    return out


def census(files):
    """{class: [sites]} and {raw-vk file: [sites]} over `files` [(rel, stripped code)]"""
    got = {c[0]: [] for c in CLASSES}
    vk = {}
    for rel, code in files:
        for name, rx, pset, _, _ in CLASSES:
            if (pset == 'include' and rel == VR_POLICY_FILE) or (pset != 'include' and in_set(rel, pset)):
                got[name] += sites(rel, code, rx)
        if in_set(rel, 'cpp'):
            s = sites(rel, code, RAW_VK)
            if s:
                vk[rel] = s
    return got, vk


def judge(got, vk):
    """the red lines of the law (empty = green)"""
    red = []

    def hold(what, found, ceiling, lower, every):
        if found > ceiling:
            red.append('RED  %s: ceiling %d, found %d (%s) - every site:' % (what, ceiling, found, lower))
            red.extend('       %s:%d  %s' % s for s in every)
        elif found < ceiling:
            red.append('RED  %s: ceiling %d, found %d - lower the ceiling to %d in the same commit' %
                       (what, ceiling, found, found))
    def floor(what, found, held, why, every):
        if found < held:
            red.append('RED  %s: floor %d, found %d - the count only rises (%s); a site of it was lost - every site:' %
                       (what, held, found, why))
            red.extend('       %s:%d  %s' % s for s in every)
        elif found > held:
            red.append('RED  %s: floor %d, found %d - raise the floor to %d in the same commit' % (what, held, found, found))
    for name, _, _, ceiling, lower in CLASSES:
        (floor if name in FLOORS else hold)(name, len(got[name]), ceiling, lower, got[name])
    for rel in sorted(set(vk) | set(RAW_VK_CEILINGS)):
        if rel not in RAW_VK_EXEMPT:
            hold('raw Vulkan in ' + rel, len(vk.get(rel, [])), RAW_VK_CEILINGS.get(rel, 0), RAW_VK_LOWER,
                 vk.get(rel, []))
    return red


def read_tree(engine):
    files = []
    for top in ('src', 'media', 'include'):
        for d, _, names in os.walk(os.path.join(engine, top)):
            for f in names:
                rel = os.path.relpath(os.path.join(d, f), engine).replace(os.sep, '/')
                if in_set(rel, 'shaders+decl') or in_set(rel, 'src') or rel == VR_POLICY_FILE:
                    with open(os.path.join(d, f), encoding='utf-8', errors='replace') as fh:
                        files.append((rel, strip(fh.read())))
    return sorted(files)   # one order on every box: the listings diff cleanly


# ---- the self-test: the stripper, one violation per class, the law's three cases ------------------------------------
VIOLATIONS = {
    'device address': ('src/rayquery/x.comp', '#extension GL_EXT_buffer_reference : require\n'),
    'push constants': ('media/Hlms/x_ps.glsl', 'layout( push_constant ) uniform P { uint a; } p;\n'),
    'image atomics': ('src/rayquery/x.comp', 'void main() { imageAtomicAdd( img, ivec2( 0 ), 1u ); }\n'),
    'read-write two-channel image': ('src/rayquery/x.comp',
                                     'layout( set = 0, binding = 2, rg16f ) uniform restrict image2D rw;\n'),
    'texel buffers': ('media/Hlms/x_ps.any', 'uniform usamplerBuffer tb;\n'),
    'early fragment tests': ('media/Hlms/x_ps.glsl', 'layout( early_fragment_tests ) in;\n'),
    'binding arrays': ('media/Hlms/x_ps.glsl', 'uniform texture2D maps[ 4 ];\n'),
    'subgroups': ('src/rayquery/x.comp', 'uint s = subgroupAdd( 1u );\n'),
    '64-bit ints': ('src/rayquery/x.comp', 'uint64_t a = 0;\n'),
    'fp16 types': ('media/Hlms/x_ps.any', 'f16vec4 c;\n'),
    'nonuniform': ('src/rayquery/x.comp', 'vec4 c = texture( t[ nonuniformEXT( i ) ], uv );\n'),
    'geometry/tessellation stages': ('media/Hlms/x.material', 'geometry_program X\n'),
    'draw parameters': ('media/Hlms/x_vs.glsl', 'uint d = gl_DrawID;\n'),
    'scalar layout': ('src/rayquery/x.comp', 'layout( scalar, set = 0, binding = 1 ) buffer B { vec3 v[]; };\n'),
    'multiview (the target)': ('media/Hlms/x_vs.glsl', 'int eye = gl_ViewIndex;\n'),
    'one-eye-only gates': ('src/OgreX.cpp', 'if( !d.view->isStereo() ) draw();\n'),
    'one-eye-only gates: applyVrViewPolicy assignments': (
        VR_POLICY_FILE, 'inline void applyVrViewPolicy(PostFxDesc &fx) { fx.bloom = false; }\n'
                        'inline void other(PostFxDesc &fx) { fx.ssao = false; }\n'),   # the body's end bounds it
}
NEAR_MISSES = [   # (class, path, text) that must count nothing: the regexes' edges
    ('read-write two-channel image', 'src/rayquery/x.comp', 'layout( rg16f, binding = 1 ) uniform readonly image2D r;\n'
     'layout( rgba16f, binding = 2 ) uniform image2D w;\nlayout( r32ui, binding = 3 ) uniform uimage2D a;\n'),
    ('one-eye-only gates', 'src/OgreX.cpp', 'if( a != stereo || !monoStereoX ) f();\n'),
    ('one-eye-only gates', 'src/OgreX.cpp', 'const bool w = !fooMatchesStereo(a, b) || !x->barStereo( s );\n'),
    ('subgroups', 'media/Hlms/x.material', 'subgroup 2\n'),
    ('raw Vulkan', 'src/OgreX.cpp', 'err = "vkCreateX (pipeline) failed"; myvkFoo( 1 );\n'),
]
MUST_COUNT = [   # (class, path, text, sites): the forms a regex edge must never drop (the lead, 2026-11-21)
    ('one-eye-only gates', 'src/OgreX.cpp', 'if( !view->stereo() ) a();\n'
     'if( !mStereo && !d.stereo ) b();\n'
     'restTakes = !noSample && !( p.stereo.w > 0.5 && prevCount > 0u );\n', 4),
]
STRIP_CASES = [   # (text, the stripped text)
    ('a // vkCmdX(\nb', 'a \nb'),
    ('a /* x\n y */ b', 'a \n b'),
    ('s = "// not a comment"; // gone', 's = "// not a comment"; '),
    ('s = "/*"; t; /* gone */', 's = "/*"; t; '),
    ('/* a // b */ c', ' c'),
    ('// a /* b\nc */ d', '\nc */ d'),
    ("c = '\"'; // gone", "c = '\"'; "),
    ('s = "a\\"// b"; x', 's = "a\\"// b"; x'),
]


def at_ceilings():
    """a census exactly at every ceiling (synthetic sites): the law's green point"""
    got = {name: [('f', 1, '')] * ceiling for name, _, _, ceiling, _ in CLASSES}
    vk = {rel: [('f', 1, '')] * c for rel, c in RAW_VK_CEILINGS.items()}
    return got, vk


def reds_of(red, what):
    return [r for r in red if r.startswith('RED  %s: ' % what)]


def self_test():
    """the failures of the self-test (empty = it holds)"""
    bad = []
    for text, want in STRIP_CASES:
        if strip(text) != want:
            bad.append('strip(%r) = %r, want %r' % (text, strip(text), want))
    if judge(*at_ceilings()):
        bad.append('a census at every ceiling is not green: %r' % judge(*at_ceilings())[:2])
    for name, rel, text in NEAR_MISSES:
        got, vk = census([(rel, strip(text))])
        if (vk if name == 'raw Vulkan' else got[name]):
            bad.append('the near miss of %s (%s: %r) counted' % (name, rel, text))
    for name, rel, text, n in MUST_COUNT:
        got, _ = census([(rel, strip(text))])
        if len(got[name]) != n:
            bad.append('the must-count case of %s (%s: %r) counted %d, want %d' % (name, rel, text, len(got[name]), n))
    # per class: ONE violation in a scratch file at a path of its set, added to a census at the ceilings, reds that
    # class alone ("ceiling N, found N+1", its site listed); the same text commented out counts nothing; one site
    # fewer than the ceiling reds with the ceiling to lower to
    for name, _, _, ceiling, _ in CLASSES:
        rel, text = VIOLATIONS[name]
        got, vk = at_ceilings()
        add, _ = census([(rel, strip(text))])
        got[name] = got[name] + add[name]
        red = judge(got, vk)
        if name in FLOORS:
            # A FLOOR, BOTH WAYS: one site MORE reds only with the floor to raise (a rise is the target, recorded); one site
            # FEWER reds with every remaining site listed (a per-eye pass lost)
            if reds_of(red, name) != red or len(red) != 1 or 'raise the floor to %d' % (ceiling + 1) not in red[0]:
                bad.append('the floor %s: one site more did not red it alone with the floor to raise: %r' % (name, red[:2]))
            got, vk = at_ceilings()
            got[name] = got[name][1:]
            red = judge(got, vk)
            if not reds_of(red, name) or 'the count only rises' not in red[0] or len(red) != ceiling:
                bad.append('the floor %s: one site fewer did not red with every site: %r' % (name, red[:2]))
            quiet, _ = census([(rel, strip('/* ' + text.rstrip('\n') + ' */\n// ' + text))])
            if quiet[name]:
                bad.append('the control of %s counted a commented-out site' % name)
            continue
        if len(red) < 2 or reds_of(red, name) != red[:1] or 'found %d' % (ceiling + 1) not in red[0] \
                or ('%s:1 ' % rel) not in red[-1]:
            bad.append('the control of %s (%s: %r) did not red it alone: %r' % (name, rel, text, red[:3]))
        quiet, _ = census([(rel, strip('/* ' + text.rstrip('\n') + ' */\n// ' + text))])
        if quiet[name]:
            bad.append('the control of %s counted a commented-out site' % name)
        if ceiling:
            got, vk = at_ceilings()
            got[name] = got[name][1:]
            if 'lower the ceiling to %d' % (ceiling - 1) not in ' '.join(reds_of(judge(got, vk), name)):
                bad.append('one site under the ceiling of %s did not red' % name)
    # raw Vulkan per file: a call in a new file reds (ceiling 0), one more in a held file reds, the exempt three are
    # never held, one fewer reds; the call written in a comment counts nothing
    call = 'void f() { vkCmdDispatch( cb, 1, 1, 1 ); } // vkCmdDraw(\n'
    held = 'src/OgreAtomIdPass.cpp'
    for rel, want in (('src/OgreNewPass.cpp', 'ceiling 0, found 1'),
                      (held, 'found %d' % (RAW_VK_CEILINGS[held] + 1)), ('src/OgreRayQuery.cpp', None)):
        got, vk = at_ceilings()
        _, add = census([(rel, strip(call))])
        vk[rel] = vk.get(rel, []) + add.get(rel, [])
        red = judge(got, vk)
        if len(add.get(rel, [])) != 1 or (want is None and red) or \
                (want is not None and not (reds_of(red, 'raw Vulkan in ' + rel) and want in red[0])):
            bad.append('the raw Vulkan control in %s: %r' % (rel, red[:2]))
    got, vk = at_ceilings()
    vk[held] = vk[held][1:]
    if not reds_of(judge(got, vk), 'raw Vulkan in ' + held):
        bad.append('one raw Vulkan call under a file\'s ceiling did not red')
    return bad


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    engine = os.path.join(root, 'irisgl', 'engine')
    if not os.path.isdir(os.path.join(engine, 'src')):
        print('FAIL: no %s/src - the row reads the tree root (the irisgl submodule checked out)' % engine)
        return 1
    files = read_tree(engine)
    got, vk = census(files)
    if '--counts' in sys.argv:
        for name, _, pset, ceiling, _ in CLASSES:
            nf = len({s[0] for s in got[name]})
            print('%-52s %5d sites %3d files   %s %d' % (name, len(got[name]), nf, 'floor' if name in FLOORS else 'ceiling', ceiling))
        for rel in sorted(vk):
            print('%-52s %5d sites            %s' % ('raw Vulkan ' + rel, len(vk[rel]),
                                                    'exempt' if rel in RAW_VK_EXEMPT else
                                                    'ceiling %d' % RAW_VK_CEILINGS.get(rel, 0)))
        return 0
    bad = self_test()
    for b in bad:
        print('FAIL the self-test: ' + b)
    if bad:
        return 1
    print('ok   the self-test: the stripper (%d cases), %d classes fed one violation each (and its commented copy), '
          'the law both ways, the policy body, raw Vulkan per file, %d must-count form(s), %d near misses'
          % (len(STRIP_CASES), len(VIOLATIONS), len(MUST_COUNT), len(NEAR_MISSES)))
    red = judge(got, vk)
    for r in red:
        print(r)
    # every run lists the one-eye-only gates and prints the exempt three's counts (held by no one, seen in every log)
    for s in got['one-eye-only gates']:
        print('eye  %s:%d  %s' % s)
    for rel in RAW_VK_EXEMPT:
        print('free raw Vulkan in %s: %d (exempt, not held: D7)' % (rel, len(vk.get(rel, []))))
    held = sum(len(got[c[0]]) for c in CLASSES) + sum(len(s) for r, s in vk.items() if r not in RAW_VK_EXEMPT)
    print('%s: %d files read, %d classes + raw Vulkan in %d files; %d sites held' %
          ('FAILED' if red else 'PASSED', len(files), len(CLASSES), len(vk), held))
    return 1 if red else 0


if __name__ == '__main__':
    sys.exit(main())
