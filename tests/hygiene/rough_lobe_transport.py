#!/usr/bin/env python3
# source.rough_lobe_transport (ROUGH-LOBE-HISTORY-1, V2 plan 1.8) - THE ROUGH LOBE's HISTORY IS CARRIED TO THE CURRENT VIEW,
# IN THE SHADER's OWN TEXT (irisgl engine/src/rayquery/rq_probe_integrate.comp). The estimator's law is proved on the
# update's pure function (irisgl engine/src/RoughLobeCarry.h) by engine.rough_lobe_transport; this row pins the shader's
# lines to that function and holds the laws only the shader's text can show.
#
# THE LAW, on the shader's code (comments stripped):
#   (1) ONE CALL SITE for each lobe evaluator (probeLobe, probeLobeBrute): NVIDIA stops inlining a large function called
#       twice (DOCS/traps/engine/hlms-and-shaders.md "ONE CALL SITE"); the two views are a LOOP OF TWO (a literal bound)
#       inside roughLobeNow, Vprev FIRST and V LAST (so gLobeFellBack is V's evaluation's).
#   (2) EACH EYE's OWN PREVIOUS CAMERA (multiview): Vprev and the carry's gate are built from jahEyeBefore( eye ) against
#       jahEyeNow( eye ), the pixel's own eye (the dispatch's z) - never a literal eye.
#   (3) THE UPDATE IS RoughLobeCarry.h's: the delta is roughLobeDelta's (carry ? now - atPrev : the literal 0), the history
#       is carried WHERE IT IS READ (roughHistoryPrev at histAt, + the delta: roughLobeCarried, unclamped), and the blend
#       takes the carried history (roughLobeMean's mix).
#   (4) AT REST NOTHING IS EVALUATED TWICE: the carry is gated on a history AND on the eye's view's BITS (eyeViewMoved,
#       floatBitsToUint) - never on two evaluations agreeing; the bits are exactly those the view vector reads (the position
#       for a perspective eye, the forward for an orthographic one), so a PURE TURN keeps the literal +0.0.
# A NEGATIVE CONTROL runs first: the shader's bare form (the history blended as read, one evaluation) must fail every law.
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
    if not re.search(r'\bfor\s*\(\s*int\s+(\w+)\s*=\s*0\s*;\s*\1\s*<\s*2\s*;', body):
        out.append('(1) roughLobeNow holds no loop of two with a literal bound')
    if not re.search(r'\b\w+\s*==\s*0\s*\?\s*Vprev\s*:\s*V\b', body):
        out.append('(1) roughLobeNow does not evaluate Vprev first and V last')
    if 'probeLobe' not in body:
        out.append('(1) the lobe evaluators are not called inside roughLobeNow')
    if re.search(r'\bjahEye(?:Before|Now)\s*\(\s*\d', code):
        out.append('(2) jahEyeBefore / jahEyeNow is called with a literal eye (one-eye-only)')
    mb = re.search(r'\bJahEye\s+(\w+)\s*=\s*jahEyeBefore\s*\(\s*eye\s*\)', code)
    mn = re.search(r'\bJahEye\s+(\w+)\s*=\s*jahEyeNow\s*\(\s*eye\s*\)', code)
    if not re.search(r'\bint\s+eye\s*=\s*int\s*\(\s*gl_GlobalInvocationID\.z\s*\)', code):
        out.append("(2) the pixel's eye is not the dispatch's z")
    if not mb or not mn:
        out.append("(2) no previous / current camera is taken as jahEyeBefore( eye ) / jahEyeNow( eye ) in main")
    else:
        before, now = mb.group(1), mn.group(1)
        call = re.search(r'\broughLobeNow\s*\(([^;]*)\)\s*;', code[mb.end():])
        args = call.group(1) if call else ''
        if not re.search(r'\btoEyeOf\s*\(\s*' + before + r'\s*,', args):
            out.append("(2) the carried roughLobeNow's Vprev is not toEyeOf( %s, ... )" % before)
        if not re.search(r'\beyeViewMoved\s*\(\s*' + now + r'\s*,\s*' + before + r'\s*\)', args):
            out.append("(2) the carry's gate is not eyeViewMoved( %s, %s )" % (now, before))
        if not re.search(r'prevCount\s*>\s*0u\s*&&\s*eyeViewMoved', args):
            out.append('(4) the carry is not gated on a history (prevCount > 0u) and the moved view')
    sig = re.search(r'\bvec3\s+roughLobeNow\s*\(([^)]*)\)', code)
    delta = re.search(r'\bout\s+vec3\s+(\w+)\s*$', sig.group(1).strip()) if sig else None
    if not delta:
        out.append('(3) roughLobeNow returns no delta (an out vec3)')
    elif not re.search(r'\b' + delta.group(1) + r'\s*=\s*carry\s*\?\s*now\s*-\s*\w+\s*:\s*vec3\s*\(\s*0\.0\s*\)\s*;', body):
        out.append("(3) the delta is not roughLobeDelta's: carry ? now - atPrev : vec3( 0.0 )")
    if not re.search(r'unpackRgbShared\s*\(\s*imageLoad\s*\(\s*roughHistoryPrev\s*,\s*ivec3\s*\(\s*histAt\s*,\s*eye\s*\)\s*\)'
                     r'\s*\.x\s*\)\s*\+\s*lobeDelta\b', code):
        out.append('(3) the history read at histAt is not carried by the delta where it is read (roughLobeCarried)')
    if not re.search(r'\bvec3\s+meanLobe\s*=\s*mix\s*\(\s*prevLobe\s*,\s*lobe\s*,\s*blend\s*\)', code):
        out.append("(3) the blend is not roughLobeMean's: mix( prevLobe (the carried history), lobe, blend )")
    moved = re.search(r'\bbool\s+eyeViewMoved\s*\([^)]*\)\s*\{([^}]*)\}', code)
    mb_ = moved.group(1) if moved else ''
    if 'floatBitsToUint' not in mb_:
        out.append("(4) eyeViewMoved does not compare the cameras' bits")
    # (4) A PURE TURN IS NO MOVE: the perspective branch compares the position alone, the orthographic the forward alone -
    # a perspective camera that only turned keeps the literal +0.0 (the same V at every point)
    if not re.search(r'\.perspective\s*<\s*0\.5\s*\?[^:]*\bfwd\b[^:]*:[^;]*\bcamPos\b[^;]*;', mb_) or \
            re.search(r':[^;]*\bfwd\b[^;]*;', mb_.split('?', 1)[-1] if '?' in mb_ else ''):
        out.append("(4) eyeViewMoved's perspective branch does not read the position alone (a pure turn would carry)")
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
