#!/usr/bin/env python3
# source.brick_list_bounded (NONDET-1 fix items D, READ-8 R8-1..R8-3) - THE FEEDBACK'S DATA-DRIVEN READS ARE BOUNDED IN
# THE SHADER (CODING_RULES section 2: every data-driven GPU loop is bounded in the shader, its count clamped at its law).
#
# WHY: the feedback's brick list is filled on the GPU (SELECT / REGION, PhotonBrickFeedback_cs.glsl) and read by the
# group index of four indirect dispatches (the bounce, the copy-back, the directional levels' steps 0 and 1) and the ray
# tier's sides gather (rq_voxel_sides.comp). A read past the list's buffer, or a brick past the volume, is a read or a
# write outside its resource: without robustBufferAccess (the fork does not enable it) its value is undefined. Nothing
# on the host can prove a GPU-written list in range, so the shader does.
#
# THE LAW, three parts:
#   (1) every read of a list by the group index (`<list>[gl_WorkGroupID.y]`) in the Photon voxel media and the ray jobs
#       is preceded by a return when the index is at or past the list's length (`<list>.length()`), and followed by a
#       return when the brick it unpacks (or the texel that brick gives) is at or past the volume's size;
#   (2) the ray tier's side-gather scratch (`jahSideGather`, a row a listed slot) is read and written only in a file that
#       bounds the row by its length;
#   (3) PhotonBrickFeedback_cs.glsl: the box loops' count is min( fbFlags.y, 16u ) (the boxes' array), ACCUM's list
#       counts are clamped to the source's bricks, and the brick ACCUM unpacks is bounded by the source's dims.
# A NEGATIVE CONTROL runs first: a synthetic unguarded read must be refused, a guarded one must pass.
import os, re, sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else '.'
ENGINE = os.path.join(ROOT, 'irisgl', 'engine')
READ = re.compile(r'\b(\w+)\[\s*gl_WorkGroupID\.y\s*\]')
BOUND_AFTER = re.compile(r'(greaterThanEqual\(\s*\w*([Bb]rick|[Tt]exel)\w*|\w*([Bb]rick|[Tt]exel)\w*(\.\w)?\s*>=)')


def strip_comments(text):
    """the text with // comments blanked (line numbers kept)"""
    return '\n'.join(re.sub(r'//.*', '', line) for line in text.split('\n'))


def check_list_reads(path, text):
    """(reads found, faults) for law (1)"""
    lines = strip_comments(text).split('\n')
    bad, found = [], 0
    for n, line in enumerate(lines):
        for m in READ.finditer(line):
            buf = m.group(1)
            if buf in ('gl_NumWorkGroups',):
                continue
            found += 1
            before = '\n'.join(lines[max(0, n - 4):n])
            guard = re.search(r'gl_WorkGroupID\.y\s*>=\s*uint\(\s*' + re.escape(buf) + r'\.length\(\)\s*\)', before)
            if not guard or 'return' not in before[guard.start():]:
                bad.append('%s:%d %s[gl_WorkGroupID.y] read with no return at or past %s.length() just before it' %
                           (path, n + 1, buf, buf))
            after = lines[n + 1:n + 20]
            ok = False
            for k, a in enumerate(after):
                if BOUND_AFTER.search(a) and 'return' in '\n'.join(after[k:k + 4]):
                    ok = True
                    break
            if not ok:
                bad.append('%s:%d the brick unpacked from %s[gl_WorkGroupID.y] is not bounded by the volume (no return '
                           'on a brick or texel at or past its size in the next lines)' % (path, n + 1, buf))
    return found, bad


def check_side_gather(path, text):
    body = strip_comments(text)
    if re.search(r'jahSideGather\[(?!\])', body) and 'jahSideGather.length()' not in body:
        return ['%s: jahSideGather indexed by a list slot with no bound by jahSideGather.length()' % path]
    return []


def check_feedback(path, text):
    body = strip_comments(text)
    bad = []
    if re.search(r'<\s*fbFlags\.y\b', body):
        bad.append('%s: a loop bounded by fbFlags.y itself (the boxes are fbBoxLo[16]: min( fbFlags.y, 16u ))' % path)
    if len(re.findall(r'min\(\s*fbFlags\.y\s*,\s*16u\s*\)', body)) < 3:
        bad.append('%s: the three box loops (PRE, REGION twice) do not all read min( fbFlags.y, 16u )' % path)
    if not re.search(r'const uint count = min\(', body) or not re.search(r'const uint carried = min\(', body):
        bad.append('%s: ACCUM\'s count and carried are not clamped to the source\'s bricks' % path)
    if not re.search(r'greaterThanEqual\(\s*o\s*,\s*fbSrcDims\.xyz\s*\)', body):
        bad.append('%s: the brick ACCUM unpacks is not bounded by fbSrcDims' % path)
    return bad


def self_test():
    bare = ('void main()\n{\n\tconst uint p = jahFbList[gl_WorkGroupID.y];\n'
            '\tconst uint3 jahBrick = uint3( p & 1023u, ( p >> 10u ) & 1023u, p >> 20u );\n}\n')
    guarded = ('void main()\n{\n\tif( gl_WorkGroupID.y >= uint( jahFbList.length() ) )\n\t\treturn;\n'
               '\tconst uint p = jahFbList[gl_WorkGroupID.y];\n'
               '\tconst uint3 jahBrick = uint3( p & 1023u, ( p >> 10u ) & 1023u, p >> 20u );\n'
               '\tif( any( greaterThanEqual( jahBrick, dims ) ) )\n\t\treturn;\n}\n')
    n1, b1 = check_list_reads('control', bare)
    n2, b2 = check_list_reads('control', guarded)
    side = check_side_gather('control', 'x = jahSideGather[k];')
    return n1 == 1 and len(b1) == 2 and n2 == 1 and not b2 and len(side) == 1


def main():
    if not self_test():
        print('FAIL: the negative control - an unguarded list read in a synthetic job was not refused (or a guarded one was)')
        return 1
    print('ok   the negative control: an unguarded list read is refused, a guarded one passes')
    files = []
    vox = os.path.join(ENGINE, 'media', 'Photon', 'Voxel')
    files += ['media/Photon/Voxel/' + f for f in sorted(os.listdir(vox)) if f.endswith(('.glsl', '.any'))]
    rq = os.path.join(ENGINE, 'src', 'rayquery')
    files += ['src/rayquery/' + f for f in sorted(os.listdir(rq)) if f.endswith('.comp')]
    bad, reads = [], 0
    for rel in files:
        text = open(os.path.join(ENGINE, rel), encoding='utf-8', errors='replace').read()
        n, b = check_list_reads(rel, text)
        reads += n
        bad += b + check_side_gather(rel, text)
    fb = 'media/Photon/Voxel/PhotonBrickFeedback_cs.glsl'
    bad += check_feedback(fb, open(os.path.join(ENGINE, fb)).read())
    if reads < 5:
        bad.append('only %d list reads by the group index found (the bounce, the copy-back, steps 0 and 1, the sides '
                   'gather: 5) - the guard reads stale names' % reads)
    for b in bad:
        print('FAIL ' + b)
    print('%s: %d list reads by the group index over %d files; %d unbounded' %
          ('FAILED' if bad else 'PASSED', reads, len(files), len(bad)))
    return 1 if bad else 0


sys.exit(main())
