#!/usr/bin/env python3
"""source.ray_seam - THE CARD'S RAY API IS SPELLED IN ONE FILE (DOORS-0 stage 1, V2 plan 0.5).

The ray seam: every ray-query call of the engine's shaders goes through the names of irisgl
engine/src/rayquery/include/jah_rq.glsl, so a software tracer (WebGPU; a Mac without ray hardware) replaces one file
and touches no reader. THE LAW, comments stripped (webgpu_census.strip, the one stripper):
  (1) no token of the four families - rayQuery*EXT, gl_RayQuery*EXT, gl_RayFlags*EXT, accelerationStructureEXT - in
      any text the app compiles outside jah_rq.glsl: irisgl/engine's src, media and include (shaders, Hlms pieces and
      C++), and EVERY FOLDER THE ENGINE STAGES (the shaders of each `copy_directory` source of irisgl
      engine/CMakeLists.txt's media staging, resolved by the CMake files' own set() lines - so a newly staged folder is
      read without anyone remembering it; a source the row cannot resolve, and a staging line in a form it does not
      parse - unquoted, `-E copy` / `copy_if_different`, `file(COPY` - are RED); RED with every site;
  (2) jah_rq.glsl holds nothing but #defines, its include guard and comments - no function, so the seam adds no
      OpFunction and no OpName and the compiled words stay the card's own (DOORS-0's byte proof); RED with every other
      line; a missing seam is RED;
  (3) THE READING IS CHECKED, NOT ASSUMED: each path set reads at least its FLOOR of files (a wrong path reads nothing
      and would pass (1)); a count under the floor is RED - lowered in the same commit only when the files really went.
The self-test runs first, always: per family one call outside the seam reds and its commented copy counts nothing; a
function, a declaration and an include in the seam red; a missing seam reds; a staging line it cannot resolve reds; a
tree whose fork path is broken reads under its floor and reds.
Run: ray_seam.py <source-root>
"""
import os
import re
import sys
import tempfile

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
SHADER = ('.comp', '.glsl', '.vert', '.frag', '.any')
ENGINE = ('irisgl/engine', ('src', 'media', 'include'), SHADER + ('.cpp', '.h'))
ENGINE_CMAKE = 'irisgl/engine/CMakeLists.txt'
OGRE_CMAKE = 'cmake/IncludeOgre.cmake'
# THE FLOORS (3): the files each path set read at irisgl 23b2725c0 / fork 6828d2d78 (engine: shaders and C++ of
# src/media/include; staged: the shaders of the 9 folders the staging copies from outside irisgl/engine - the fork's
# Hlms Common/Pbs/Unlit, 2.0/scripts/materials Common/HDR/Tutorial_SSAO/Tutorial_SMAA, Compute/Tools, Compute/Algorithms/IBL)
FLOORS = {'engine': 335, 'staged': 115}
SEAM_LINE = re.compile(r'#\s*(define\s+\w+|ifndef\s+JAH_RQ_GLSL\s*$|endif\s*$)')
COPY = re.compile(r'copy_directory\s+"\$\{(\w+)\}([^"]*)"')
STAGE_FORM = re.compile(r'\bcopy_directory\b|-E\s+copy(_if_different)?\b|\bfile\s*\(\s*COPY\b')
SET = re.compile(r'^\s*set\(\s*(\w+)\s+"([^"]*)"', re.M)
DIRNAME = re.compile(r'get_filename_component\(\s*(\w+)\s+"\$\{(\w+)\}"\s+DIRECTORY\s*\)')


def staged(engine_cmake, ogre_cmake):
    """([the staged source folders relative to the root], [the copy_directory sources it could not resolve]): every
    `copy_directory "${VAR}/..."` of the engine's CMakeLists, VAR resolved through the set() and DIRECTORY lines of the
    two CMake files (CMAKE_SOURCE_DIR = the root, CMAKE_CURRENT_SOURCE_DIR = irisgl/engine)"""
    var = {'CMAKE_SOURCE_DIR': '', 'CMAKE_CURRENT_SOURCE_DIR': 'irisgl/engine'}
    pairs = SET.findall(ogre_cmake) + SET.findall(engine_cmake)
    dirs = DIRNAME.findall(engine_cmake)
    for _ in range(8):   # a value names others: resolve until nothing moves
        for name, value in pairs:
            v = re.sub(r'\$\{(\w+)\}', lambda m: var.get(m.group(1), '\0'), value)
            if '\0' not in v:
                var.setdefault(name, v.lstrip('/'))
        for name, of in dirs:
            if of in var:
                var.setdefault(name, os.path.dirname(var[of]))
    # a staging line the parser does not match (unquoted, a file copy, file(COPY)) is unresolved, never unseen
    code = [x.split('#', 1)[0] for x in engine_cmake.split('\n')]
    bad = ['(a staging line the row does not parse) ' + l.strip() for l in code if STAGE_FORM.search(l) and not COPY.search(l)]
    out = []
    for name, sub in COPY.findall(engine_cmake):
        if name in var:
            out.append((var[name] + sub).strip('/'))
        else:
            bad.append('${%s}%s' % (name, sub))
    return sorted(set(out)), bad


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


def judge(files, counts=None, unresolved=()):
    red = ['RED  %s:%d  %s  (the ray API outside the seam: spell it with jah_rq.glsl\'s names)' % s for s in outside(files)]
    faults = seam_faults(files)
    if faults is None:
        red.append('RED  %s is missing: the seam every ray call goes through' % SEAM)
    else:
        red += ['RED  %s:%d  %s  (the seam holds #defines only: no function, no declaration)' % (SEAM, ln, t)
                for ln, t in faults]
    red += ['RED  the staging copies %s and the row cannot resolve it: it reads nothing there (resolve the variable)' % u
            for u in unresolved]
    for name, n in (counts or {}).items():
        if n < FLOORS[name]:
            red.append('RED  the %s path set read %d files, under its floor %d: a wrong scan path reads nothing (lower '
                       'the floor in the same commit only if the files really went)' % (name, n, FLOORS[name]))
    return red


def _read(root, rel_dir, exts, files):
    n = 0
    for d, _, names in os.walk(os.path.join(root, rel_dir)):
        for f in names:
            if f.endswith(exts):
                p = os.path.join(d, f)
                rel = os.path.relpath(p, root).replace(os.sep, '/')
                if rel in files:
                    continue
                with open(p, encoding='utf-8', errors='replace') as fh:
                    files[rel] = strip(fh.read())
                n += 1
    return n


def read_tree(root):
    """(the stripped files [(rel, text)], the files read per path set, the unresolved staging sources)"""
    files = {}
    top, subs, exts = ENGINE
    counts = {'engine': sum(_read(root, os.path.join(top, s), exts, files) for s in subs)}

    def text(rel):
        p = os.path.join(root, rel)
        return open(p, encoding='utf-8', errors='replace').read() if os.path.isfile(p) else ''
    folders, bad = staged(text(ENGINE_CMAKE), text(OGRE_CMAKE))
    counts['staged'] = sum(_read(root, d, SHADER, files) for d in folders if not d.startswith(top + '/'))
    return sorted(files.items()), counts, bad


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
                    'irisgl/thirdparty/ogre-next/Samples/Media/Compute/Tools/X_cs.glsl'):
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
    # THE STAGING, RESOLVED: the engine's own forms (a fork folder by the media root's DIRECTORY, an engine folder, a
    # sub-folder of a set variable) resolve; a variable no set() line defines is unresolved, and unresolved reds
    ogre = 'set(OGRE_NEXT_SOURCE "${CMAKE_SOURCE_DIR}/irisgl/thirdparty/ogre-next"\n' \
           'set(OGRE_NEXT_HLMS_SOURCE_DIR "${OGRE_NEXT_SOURCE}/Samples/Media/Hlms")\n'
    eng = 'get_filename_component(_ogre_media_root "${OGRE_NEXT_HLMS_SOURCE_DIR}" DIRECTORY)\n' \
          'set(OGRE_NEXT_IBL_MEDIA_DIR "${_ogre_media_root}/Compute/Algorithms/IBL")\n' \
          'COMMAND ${CMAKE_COMMAND} -E copy_directory "${OGRE_NEXT_HLMS_SOURCE_DIR}/Pbs" "${JAHSHAKA_MEDIA_DIR}/Hlms/Pbs"\n' \
          'COMMAND ${CMAKE_COMMAND} -E copy_directory "${OGRE_NEXT_IBL_MEDIA_DIR}" "${JAHSHAKA_MEDIA_DIR}/Compute/IBL"\n' \
          'COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/media/Hlms/Atom" "x"\n' \
          'COMMAND ${CMAKE_COMMAND} -E copy_directory "${OGRE_NEXT_NEW_DIR}" "${JAHSHAKA_MEDIA_DIR}/New"\n'
    got, unres = staged(eng, ogre)
    want = ['irisgl/engine/media/Hlms/Atom', 'irisgl/thirdparty/ogre-next/Samples/Media/Compute/Algorithms/IBL',
            'irisgl/thirdparty/ogre-next/Samples/Media/Hlms/Pbs']
    if got != want or unres != ['${OGRE_NEXT_NEW_DIR}']:
        bad.append('the staging resolved %r unresolved %r, want %r and the new variable' % (got, unres, want))
    if not any('cannot resolve' in r for r in judge([(SEAM, seam)], unresolved=unres)):
        bad.append('an unresolved staging source did not red')
    # a staging line in a form the parser does not match (unquoted, a file copy, file(COPY)) is unresolved too, never
    # unseen; the same forms in a comment are not staging
    for line in ('COMMAND ${CMAKE_COMMAND} -E copy_directory ${OGRE_NEXT_IBL_MEDIA_DIR} "${JAHSHAKA_MEDIA_DIR}/I"',
                 'COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OGRE_NEXT_IBL_MEDIA_DIR}/a.glsl" "x"',
                 'file(COPY "${OGRE_NEXT_IBL_MEDIA_DIR}/b" DESTINATION "x")'):
        if len(staged(eng + line + '\n# ' + line + '\n', ogre)[1]) != 2:
            bad.append('the unmatched staging form %r was not unresolved exactly once' % line)
    # THE READING (3): a tree whose fork path is broken - the engine's staging names folders that are not there - reads
    # none of them and reds at its floor; the same tree with one staged file in place reads it
    with tempfile.TemporaryDirectory(prefix='ray-seam-') as t:
        os.makedirs(os.path.join(t, 'irisgl', 'engine', 'src'))
        os.makedirs(os.path.join(t, 'cmake'))
        open(os.path.join(t, ENGINE_CMAKE), 'w').write(eng.replace('"${OGRE_NEXT_NEW_DIR}"', '"${OGRE_NEXT_IBL_MEDIA_DIR}"'))
        open(os.path.join(t, OGRE_CMAKE), 'w').write(ogre)
        _, counts, _ = read_tree(t)
        red = judge([(SEAM, seam)], counts)
        if counts['staged'] != 0 or not any('staged path set read 0 files' in r for r in red):
            bad.append('a broken fork path did not red at the floor: %r %r' % (counts, red[:2]))
        d = os.path.join(t, 'irisgl', 'thirdparty', 'ogre-next', 'Samples', 'Media', 'Hlms', 'Pbs')
        os.makedirs(d)
        open(os.path.join(d, 'X_piece_ps.any'), 'w').write('float x;\n')
        files, counts, _ = read_tree(t)
        if counts['staged'] != 1 or 'irisgl/thirdparty/ogre-next/Samples/Media/Hlms/Pbs/X_piece_ps.any' not in dict(files):
            bad.append('a staged fork file was not read: %r' % counts)
    return bad


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    if not os.path.isdir(os.path.join(root, 'irisgl', 'engine', 'src')):
        print('FAIL: no %s/irisgl/engine/src - the row reads the tree root (the irisgl submodule checked out)' % root)
        print('source.ray_seam: FAILED (1)')
        return 1
    bad = self_test()
    for b in bad:
        print('FAIL the self-test: ' + b)
    if bad:
        print('source.ray_seam: FAILED (%d)' % len(bad))
        return 1
    print('ok   the self-test: %d families each red outside the seam (three path sets) and quiet in a comment; a '
          'function, a declaration and an include in the seam red; a missing seam reds; the staging resolved, an '
          'unresolved source reds; a broken fork path reds at its floor' % len(FAMILIES))
    files, counts, unresolved = read_tree(root)
    red = judge(files, counts, unresolved)
    for r in red:
        print(r)
    names = len(re.findall(r'^\s*#\s*define\s+(?!JAH_RQ_GLSL\b)\w+', dict(files).get(SEAM, ''), re.M))
    faults = seam_faults(files)
    print('read: %d files (engine %d, floor %d; staged outside irisgl/engine %d, floor %d), %d sites outside the seam, '
          'the seam %s (%d names)' % (len(files), counts['engine'], FLOORS['engine'], counts['staged'], FLOORS['staged'],
                                      len(outside(files)), 'missing' if faults is None else
                                      'defines only' if not faults else 'NOT defines only', names))
    print('source.ray_seam: %s' % ('FAILED (%d)' % len(red) if red else 'PASSED'))
    return 1 if red else 0


if __name__ == '__main__':
    sys.exit(main())
