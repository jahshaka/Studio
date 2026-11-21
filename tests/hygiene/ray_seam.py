#!/usr/bin/env python3
"""source.ray_seam - THE CARD'S RAY API IS SPELLED IN ONE FILE (DOORS-0 stage 1, V2 plan 0.5).

The ray seam: every ray-query call of the engine's shaders goes through the names of irisgl
engine/src/rayquery/include/jah_rq.glsl, so a software tracer (WebGPU; a Mac without ray hardware) replaces one file
and touches no reader. THE LAW, comments stripped (webgpu_census.strip, the one stripper):
  (1) no token of the four families - rayQuery*EXT, gl_RayQuery*EXT, gl_RayFlags*EXT, accelerationStructureEXT - in
      any text the app compiles outside jah_rq.glsl: irisgl/engine's src, media and include (shaders, Hlms pieces and
      C++), and the fork's Hlms media; RED with every site;
  (2) jah_rq.glsl holds nothing but #defines, its include guard and comments - no function, so the seam adds no
      OpFunction and no OpName and the compiled words stay the card's own (the byte proof:
      ~/Developer/spikes/doors-0/stage1/seam_proof.sh); RED with every other line; a missing seam is RED.
The self-test runs first, always: per family one call outside the seam reds and its commented copy counts nothing; a
function and a declaration in the seam red; the seam's own defines count nothing.
Run: ray_seam.py <source-root>
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from webgpu_census import strip  # noqa: E402  (the one comment stripper; its cases are the census's self-test)

SEAM = 'irisgl/engine/src/rayquery/include/jah_rq.glsl'
FAMILIES = [
    ('rayQuery*EXT', r'\brayQuery\w*EXT\b'),
    ('gl_RayQuery*EXT', r'\bgl_RayQuery\w*EXT\b'),
    ('gl_RayFlags*EXT', r'\bgl_RayFlags\w*EXT\b'),
    ('accelerationStructureEXT', r'\baccelerationStructureEXT\b'),
]
TOKEN = re.compile('|'.join(rx for _, rx in FAMILIES))
# (directory under the root, the sub-directories read, the extensions read)
SCAN = [
    ('irisgl/engine', ('src', 'media', 'include'), ('.comp', '.glsl', '.vert', '.frag', '.any', '.cpp', '.h')),
    ('irisgl/thirdparty/ogre-next/Samples/Media/Hlms', ('',), ('.glsl', '.any')),
]
SEAM_LINE = re.compile(r'#\s*(define\s+\w+|ifndef\s+JAH_RQ_GLSL\s*$|endif\s*$)')


def outside(files):
    """[(rel, line, text)] of every family token outside the seam, in the stripped texts"""
    out = []
    for rel, code in files:
        if rel == SEAM:
            continue
        lines = code.split('\n')
        for m in TOKEN.finditer(code):
            ln = code.count('\n', 0, m.start())
            out.append((rel, ln + 1, lines[ln].strip()))
    return out


def seam_faults(files):
    """[(line, text)] of every line of the stripped seam that is not a #define, its guard or blank; None: no seam"""
    code = dict(files).get(SEAM)
    if code is None:
        return None
    return [(i + 1, l.strip()) for i, l in enumerate(code.split('\n')) if l.strip() and not SEAM_LINE.match(l.strip())]


def judge(files):
    red = ['RED  %s:%d  %s  (the ray API outside the seam: spell it with jah_rq.glsl\'s names)' % s for s in outside(files)]
    faults = seam_faults(files)
    if faults is None:
        red.append('RED  %s is missing: the seam every ray call goes through' % SEAM)
    else:
        red += ['RED  %s:%d  %s  (the seam holds #defines only: no function, no declaration)' % (SEAM, ln, t)
                for ln, t in faults]
    return red


def read_tree(root):
    files = []
    for top, subs, exts in SCAN:
        for sub in subs:
            for d, _, names in os.walk(os.path.join(root, top, sub)):
                for f in names:
                    if f.endswith(exts):
                        p = os.path.join(d, f)
                        with open(p, encoding='utf-8', errors='replace') as fh:
                            files.append((os.path.relpath(p, root).replace(os.sep, '/'), strip(fh.read())))
    return sorted(files)


def self_test():
    """the failures of the self-test (empty = it holds)"""
    bad = []
    seam = strip('// the seam\n#ifndef JAH_RQ_GLSL\n#define JAH_RQ_GLSL\n#define JahRq rayQueryEXT\n'
                 '#define jahRqProceed( q ) rayQueryProceedEXT( q )\n#define JAH_RQ_HIT_TRIANGLE '
                 'gl_RayQueryCommittedIntersectionTriangleEXT\n#define JAH_RQ_FLAGS_NONE gl_RayFlagsNoneEXT\n'
                 '#define JahTlas accelerationStructureEXT /* a binding or an address */\n#endif\n')
    if judge([(SEAM, seam)]):
        bad.append('the seam alone is not green: %r' % judge([(SEAM, seam)])[:2])
    calls = {
        'rayQuery*EXT': 'while( rayQueryProceedEXT( q ) ) {}\n',
        'gl_RayQuery*EXT': 'bool h = t == gl_RayQueryCommittedIntersectionTriangleEXT;\n',
        'gl_RayFlags*EXT': 'uint f = gl_RayFlagsOpaqueEXT;\n',
        'accelerationStructureEXT': 'layout( set = 0, binding = 0 ) uniform accelerationStructureEXT tlas;\n',
    }
    for name, rx in FAMILIES:
        call = calls[name]
        for rel in ('irisgl/engine/src/rayquery/rq_x.comp', 'irisgl/engine/media/Hlms/Jahshaka/X_piece_ps.any',
                    'irisgl/thirdparty/ogre-next/Samples/Media/Hlms/Pbs/Any/X_piece_all.any'):
            red = judge([(SEAM, seam), (rel, strip('void f()\n{\n' + call + '}\n'))])
            if len(red) != 1 or ('%s:3 ' % rel) not in red[0] or not re.search(rx, red[0]):
                bad.append('the control of %s at %s did not red it alone: %r' % (name, rel, red[:2]))
        quiet = strip('/* ' + call.rstrip('\n') + ' */\n// ' + call)
        if judge([(SEAM, seam), ('irisgl/engine/src/rayquery/rq_x.comp', quiet)]):
            bad.append('the control of %s counted a commented-out call' % name)
    for line in ('bool jahRqHit( rayQueryEXT q ) { return true; }', 'rayQueryEXT jahRqGlobal;', '#include "x.glsl"'):
        red = judge([(SEAM, seam.replace('#endif', line + '\n#endif'))])
        if len(red) != 1 or SEAM not in red[0] or line not in red[0]:
            bad.append('the seam holding %r did not red it alone: %r' % (line, red[:2]))
    if not any('is missing' in r for r in judge([])):
        bad.append('a missing seam did not red')
    return bad


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    if not os.path.isdir(os.path.join(root, 'irisgl', 'engine', 'src')):
        print('FAIL: no %s/irisgl/engine/src - the row reads the tree root (the irisgl submodule checked out)' % root)
        return 1
    bad = self_test()
    for b in bad:
        print('FAIL the self-test: ' + b)
    if bad:
        return 1
    print('ok   the self-test: %d families each red outside the seam (three path sets) and quiet in a comment; a '
          'function, a declaration and an include in the seam red; a missing seam reds' % len(FAMILIES))
    files = read_tree(root)
    red = judge(files)
    for r in red:
        print(r)
    names = len(re.findall(r'^\s*#\s*define\s+(?!JAH_RQ_GLSL\b)\w+', dict(files).get(SEAM, ''), re.M))
    print('%s: %d files read, %d sites outside the seam, the seam %s (%d names)' %
          ('FAILED' if red else 'PASSED', len(files), len(outside(files)),
           'missing' if seam_faults(files) is None else 'defines only' if not seam_faults(files) else 'NOT defines only',
           names))
    return 1 if red else 0


if __name__ == '__main__':
    sys.exit(main())
