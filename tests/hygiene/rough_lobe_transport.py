#!/usr/bin/env python3
# source.rough_lobe_transport (ROUGH-LOBE-HISTORY-1, V2 plan 1.8) - THE ROUGH LOBE's HISTORY IS CARRIED TO THE CURRENT VIEW,
# IN THE SHADER's OWN TEXT (irisgl engine/src/rayquery/rq_probe_integrate.comp; the law itself is engine.rough_lobe_transport's,
# a CPU row over the same functions transcribed).
#
# THE LAW, on the shader's code (comments stripped):
#   (1) ONE CALL SITE for each lobe evaluator (probeLobe, probeLobeBrute): NVIDIA stops inlining a large function called
#       twice (DOCS/traps/engine/hlms-and-shaders.md "ONE CALL SITE"); the two evaluations (V and V_prev) are a LOOP OF TWO
#       inside roughLobeNow.
#   (2) V_prev IS THE PIXEL's OWN EYE's: built through jahEyeBefore( eye ) (the per-view previous camera - multiview), never
#       a literal eye.
#   (3) THE HISTORY IS CARRIED BEFORE THE BLEND: the history read (roughHistoryPrev at histAt) has the lobe's change added
#       (roughLobeNow's out delta) - the bare blend mix( prevLobe, lobe, blend ) of a history read as it was is refused.
#   (4) gLobeFellBack IS THE V EVALUATION's: roughLobeNow sets it back from the first pass of its loop.
# A NEGATIVE CONTROL runs first: the shader's bare form (the history blended as read, one evaluation) must be refused.
import os, re, sys

ROOT = sys.argv[1] if len(sys.argv) > 1 else '.'
SHADER = os.path.join(ROOT, 'irisgl', 'engine', 'src', 'rayquery', 'rq_probe_integrate.comp')


def strip(text):
    """// and /* */ comments blanked, newlines kept"""
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith('//', i):
            j = text.find('\n', i)
            i = n if j < 0 else j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append('\n' * text.count('\n', i, j))
            i = j
        else:
            out.append(text[i])
            i += 1
    return ''.join(out)


def body_of(text, name):
    """the text of function `name`'s body (its definition: a return type before the name), or None"""
    m = re.search(r'\b(?:vec3|vec4|float|bool|void)\s+' + name + r'\s*\(', text)
    if not m:
        return None
    i = text.index('{', m.end())
    depth = 0
    for j in range(i, len(text)):
        if text[j] == '{':
            depth += 1
        elif text[j] == '}':
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    return None


def faults(src):
    code = strip(src)
    out = []
    for fn in ('probeLobe', 'probeLobeBrute'):
        defs = len(re.findall(r'\bvec3\s+' + fn + r'\s*\(', code))
        calls = len(re.findall(r'\b' + fn + r'\s*\(', code)) - defs
        if calls != 1:
            out.append('(1) %s has %d call sites (one: the loop of two inside roughLobeNow)' % (fn, calls))
    body = body_of(code, 'roughLobeNow') or ''
    if not re.search(r'\bfor\s*\(\s*int\s+\w+\s*=\s*0\s*;\s*\w+\s*<\s*\(\s*\w+\s*\?\s*2\s*:\s*1\s*\)', body):
        out.append('(1) roughLobeNow holds no loop of two (V, then V_prev with a history)')
    if 'probeLobe' not in body:
        out.append('(1) the lobe evaluators are not called inside roughLobeNow')
    if re.search(r'\bjahEyeBefore\s*\(\s*\d', code):
        out.append('(2) jahEyeBefore is called with a literal eye (one-eye-only)')
    m = re.search(r'\bJahEye\s+(\w+)\s*=\s*jahEyeBefore\s*\(\s*eye\s*\)', code)
    if not m:
        out.append("(2) no previous camera is taken as jahEyeBefore( eye ) in main")
    else:
        before = m.group(1)
        if not re.search(r'\broughLobeNow\s*\([^;]*\btoEyeOf\s*\(\s*' + before + r'\s*,', code):
            out.append("(2) roughLobeNow's V_prev is not toEyeOf( %s, ... ) (the pixel's own eye's previous camera)" % before)
    # (3) the history read, carried: unpackRgbShared( imageLoad( roughHistoryPrev, ... ) ) + <the out delta>
    if not re.search(r'unpackRgbShared\s*\(\s*imageLoad\s*\(\s*roughHistoryPrev\s*,\s*ivec3\s*\(\s*histAt\s*,\s*eye\s*\)\s*\)'
                     r'\s*\.x\s*\)\s*\+\s*\w+', code):
        out.append('(3) the history read at histAt is not carried by the lobe\'s change before the blend')
    sig = re.search(r'\bvec3\s+roughLobeNow\s*\(([^)]*)\)', code)
    if not sig or not re.search(r'\bout\s+vec3\s+\w+\s*$', sig.group(1).strip()):
        out.append('(3) roughLobeNow returns no delta (an out vec3)')
    # (4) the fallback flag restored from the V evaluation after the loop
    if not re.search(r'gLobeFellBack\s*=\s*\w+\s*;\s*return\b', body):
        out.append('(4) gLobeFellBack is not set back to the V evaluation\'s before roughLobeNow returns')
    return out


BARE = '''
bool gLobeFellBack = false;
vec3 probeLobe( vec3 N, vec3 V ) { gLobeFellBack = false; return V; }
vec3 probeLobeBrute( vec3 N, vec3 V ) { gLobeFellBack = false; return V; }
vec3 roughLobeNow( vec3 N, vec3 V, float roughness, vec3 diffuse )
{
	gLobeFellBack = true;
	const vec3 lobe = roughness > 1.5 ? probeLobeBrute( N, V ) : probeLobe( N, V );
	return lobe;
}
void main()
{
	const vec3 lobe = roughLobeNow( N, V, roughness, d );
	const vec3 prevLobe = prevCount > 0u ? unpackRgbShared( imageLoad( roughHistoryPrev, ivec3( histAt, eye ) ).x ) : vec3( 0.0 );
	vec3 meanLobe = mix( prevLobe, lobe, blend );
}
'''


def main():
    neg = faults(BARE)
    missed = [law for law in ('(1)', '(2)', '(3)', '(4)') if not any(f.startswith(law) for f in neg)]
    if missed:
        print('FAIL: negative control: the bare form passed law(s) %s: %s' % (' '.join(missed), neg))
        return 1
    print('ok: negative control - the bare form is refused (%d faults)' % len(neg))
    if not os.path.exists(SHADER):
        print('FAIL: %s is missing' % SHADER)
        return 1
    found = faults(open(SHADER, encoding='utf-8').read())
    for f in found:
        print('FAIL: rq_probe_integrate.comp: ' + f)
    print('source.rough_lobe_transport: %s' % ('FAILED' if found else 'PASSED'))
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main())
