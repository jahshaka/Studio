#!/usr/bin/env python3
# source.compute_size_keys (COLD-BOOT-1 stage 2) - NO COMPUTE PERMUTATION IS KEYED ON A SIZE NOBODY READS.
#
# WHY: a compute job told its texture sizes (the fork's default, HlmsComputeJob mInformHlmsOfTextureSize = true,
# OgreHlmsComputeJob.cpp:71) puts textureN_width/height/depth/mipmaps, uavN_*_with_lod and num_thread_groups_x/y/z into
# its PROPERTIES (:211-222, :523-528, :650) - and every property is part of the permutation's key. A job dispatched per
# VIEW (the editor, the recorder, a PiP, each eye of a headset) then compiles one permutation per distinct size: a per-run
# value in a key (CODING_RULES 2). Not told them, the dispatch takes the job's own counts (OgreHlmsCompute.cpp, BUGS-6),
# so the picture is the same either way. Measured at the base: no template or piece of ours reads any of those names.
#
# THE LAW: every job in irisgl's engine/media/**/*.material.json sets "inform_shader_of_texture_size" : false UNLESS its
# source or one of its pieces reads a size property (the names above). A file a job names that this script cannot find is
# red too (a law read over a file it never opened proves nothing).
# The files are read where the ENGINE reads them: the build's staged media (<build>/bin/media - the fork's, irisgl's and the
# pieces the build generates from the ray jobs' includes, e.g. JahGeomRows_piece_all.any).
# usage: compute_size_keys.py <source dir> <staged media dir>
# A NEGATIVE CONTROL runs first: synthetic jobs - flagless and reading nothing (red), flagless and reading num_thread_groups_x
# (green), flag false (green), a missing source (red).
import glob, json, os, re, sys, tempfile

SIZE_READ = re.compile(r'num_thread_groups_[xyz]|\b(?:texture|uav)\d+_(?:width|height|depth|mipmaps)\b|_(?:width|height|depth)_with_lod\b')


def index_files(roots):
    """base name (with and without its extension) -> paths, over the media roots (Vulkan's GLSL and the .any pieces)"""
    idx = {}
    for r in roots:
        for p in glob.glob(os.path.join(r, '**', '*'), recursive=True):
            if not os.path.isfile(p) or os.sep + 'HLSL' + os.sep in p or os.sep + 'Metal' + os.sep in p:
                continue
            b = os.path.basename(p)
            if b.endswith(('.hlsl', '.metal')):
                continue
            idx.setdefault(b, []).append(p)
            idx.setdefault(os.path.splitext(b)[0], []).append(p)
    return idx


def judge(jsons, idx):
    """[(file, job, why)] - the jobs that break the law"""
    bad = []
    for f in jsons:
        with open(f) as fh:
            d = json.load(fh)
        for name, job in d.get('compute', {}).items():
            if job.get('inform_shader_of_texture_size', True) is False:
                continue
            files = [job.get('source', '')] + list(job.get('pieces', []))
            reads, missing = False, []
            for n in files:
                paths = idx.get(n) or idx.get(n + '.glsl') or idx.get(n + '.any')
                if not paths:
                    missing.append(n)
                    continue
                for p in paths:
                    with open(p, errors='replace') as fh:
                        if SIZE_READ.search(fh.read()):
                            reads = True
            if missing:
                bad.append((f, name, 'names a file not found: ' + ', '.join(missing)))
            elif not reads:
                bad.append((f, name, 'keys its permutation on texture sizes and group counts, and no file of it reads one'))
    return bad


def self_test():
    with tempfile.TemporaryDirectory() as t:
        open(os.path.join(t, 'Plain_cs.glsl'), 'w').write('void main() { uint x = 1u; }\n')
        open(os.path.join(t, 'Sized_cs.glsl'), 'w').write('uint n = @value( num_thread_groups_x )u;\n')
        j = os.path.join(t, 'synthetic.material.json')
        json.dump({'compute': {
            'flagless_plain': {'source': 'Plain_cs'},
            'flagless_sized': {'source': 'Sized_cs'},
            'flag_false': {'source': 'Plain_cs', 'inform_shader_of_texture_size': False},
            'missing': {'source': 'Nowhere_cs', 'inform_shader_of_texture_size': True}}}, open(j, 'w'))
        got = sorted(b[1] for b in judge([j], index_files([t])))
        return got == ['flagless_plain', 'missing'], got


def main():
    if len(sys.argv) != 3:
        print('usage: compute_size_keys.py <source dir> <staged media dir>')
        return 64
    root, media = sys.argv[1], sys.argv[2]
    fails = 0
    ok, got = self_test()
    print(('  ok   ' if ok else '  FAIL ') + 'the negative control: a flagless job reading no size and a missing source are refused, '
          'a job reading num_thread_groups_x and a flag-false job are not (refused: %s)' % got)
    fails += 0 if ok else 1
    jsons = sorted(glob.glob(os.path.join(root, 'irisgl', 'engine', 'media', '**', '*.material.json'), recursive=True))
    jobs = sum(len(json.load(open(f)).get('compute', {})) for f in jsons)
    bad = judge(jsons, index_files([media]))
    for f, name, why in bad:
        print('  FAIL %s: %s %s' % (os.path.relpath(f, root), name, why))
    print(('  ok   ' if not bad else '  FAIL ') + 'every compute job of %d in %d files keys no size it does not read (%d break the law)'
          % (jobs, len(jsons), len(bad)))
    fails += 1 if bad or not jsons else 0
    print('source.compute_size_keys: ' + ('PASSED' if not fails else 'FAILED (%d)' % fails))
    sys.stdout.flush()
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
