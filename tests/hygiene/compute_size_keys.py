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
#
# CLAUSE (2), THE COMPUTE LAYER's RECORD KEY (COLD-BOOT-1 stage 3; the lead's condition 3 on P6: per VIEW, no per-run
# value): the input key a record is found by (irisgl engine/src/ComputeRecord.h, ComputeKeyInputs and
# computeRecordInputKey) reads EXACTLY what a template's parse reads - the template's bytes, the piece files, the C++
# pieces, the properties - plus the ROOT LAYOUT's bytes (ruled in by SHADER-SYSTEM-1's start preflight, F2a: a slot's type
# reaches no property, and two jobs of equal text with other slot types need two programs) - and nothing of a view: the
# struct holds exactly those five fields, and neither it nor the key function names a view, a camera, an eye, a viewport
# or a size. A new field is red until the lead rules it in here.
# Its negative control first: a synthetic struct with a view's width added is refused.
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


KEY_FIELDS = ['templateText', 'pieceFiles', 'cppPieces', 'properties', 'rootLayout']
VIEW_WORDS = re.compile(r'\b\w*(?:[Vv]iew|[Cc]amera|[Ee]ye|[Vv]iewport|[Ww]idth|[Hh]eight|[Rr]esolution|[Ss]ize)\w*\b')


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//[^\n]*', '', text)


def record_key_faults(header_text, source_text):
    """[why] - the record key's law broken (clause 2)"""
    faults = []
    h = strip_comments(header_text)
    m = re.search(r'struct\s+ComputeKeyInputs\s*\{(.*?)\};', h, re.S)
    if not m:
        return ['ComputeKeyInputs not found']
    body = m.group(1)
    fields = re.findall(r'(\w+)\s*;', body)
    if fields != KEY_FIELDS:
        faults.append('ComputeKeyInputs holds %s, the law %s' % (fields, KEY_FIELDS))
    words = sorted(set(VIEW_WORDS.findall(body)))
    if words:
        faults.append('ComputeKeyInputs names a view or a size: %s' % words)
    src = strip_comments(source_text)
    f = re.search(r'ComputeKey128\s+computeRecordInputKey\s*\([^)]*\)\s*\{(.*?)\n\}', src, re.S)
    if not f:
        faults.append('computeRecordInputKey not found')
    else:
        words = sorted(set(w for w in VIEW_WORDS.findall(f.group(1)) if w not in ('size',)))
        # .size() of the key's own containers is a length prefix, not a view's size
        words = [w for w in words if not re.search(r'\.' + re.escape(w) + r'\(\)', f.group(1))]
        if words:
            faults.append('computeRecordInputKey names a view or a size: %s' % words)
    return faults


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
    good_h = ('struct ComputeKeyInputs {\n std::string templateText;\n V pieceFiles;\n V cppPieces;\n P properties;\n'
              ' std::string rootLayout;\n};\n')
    bad_h = good_h.replace(' P properties;', ' P properties;\n unsigned viewWidth;')
    good_c = 'ComputeKey128 computeRecordInputKey(const ComputeKeyInputs &in) {\n    k.u64(in.pieceFiles.size());\n}\n'
    bad_c = good_c.replace('k.u64(in.pieceFiles.size());', 'k.u64(in.pieceFiles.size()); k.u64(eyeIndex);')
    rk = (not record_key_faults(good_h, good_c)) and bool(record_key_faults(bad_h, good_c)) and bool(record_key_faults(good_h, bad_c))
    return got == ['flagless_plain', 'missing'] and rk, (got, rk)


def main():
    if len(sys.argv) != 3:
        print('usage: compute_size_keys.py <source dir> <staged media dir>')
        return 64
    root, media = sys.argv[1], sys.argv[2]
    fails = 0
    ok, got = self_test()
    print(('  ok   ' if ok else '  FAIL ') + 'the negative controls: a flagless job reading no size and a missing source are refused, '
          'a job reading num_thread_groups_x and a flag-false job are not; a record key with a view width or an eye is '
          'refused, the five-field key is not (%s)' % (got,))
    fails += 0 if ok else 1
    jsons = sorted(glob.glob(os.path.join(root, 'irisgl', 'engine', 'media', '**', '*.material.json'), recursive=True))
    jobs = sum(len(json.load(open(f)).get('compute', {})) for f in jsons)
    bad = judge(jsons, index_files([media]))
    for f, name, why in bad:
        print('  FAIL %s: %s %s' % (os.path.relpath(f, root), name, why))
    print(('  ok   ' if not bad else '  FAIL ') + 'every compute job of %d in %d files keys no size it does not read (%d break the law)'
          % (jobs, len(jsons), len(bad)))
    fails += 1 if bad or not jsons else 0
    src = os.path.join(root, 'irisgl', 'engine', 'src')
    try:
        rk = record_key_faults(open(os.path.join(src, 'ComputeRecord.h')).read(),
                               open(os.path.join(src, 'ComputeRecord.cpp')).read())
    except OSError as e:
        rk = ['cannot read the record key: %s' % e]
    for f in rk:
        print('  FAIL (2) ' + f)
    print(('  ok   ' if not rk else '  FAIL ') + '(2) the compute record key reads the parse\'s inputs and the root layout, nothing of a view')
    fails += 1 if rk else 0
    print('source.compute_size_keys: ' + ('PASSED' if not fails else 'FAILED (%d)' % fails))
    sys.stdout.flush()
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
