#!/usr/bin/env python3
# source.gate_pace (COLD-BOOT-1 stage 4) - THE STREAMING PACE IS THE GATE's AND THE VIEWPORT's, NOWHERE ELSE.
#
# WHY: a frame on the Complete pace owes the whole picture - every consumer joins what it needs, so no view a user sees
# shows a frame with a permutation missing (the reveal law). The startup gate's frames are shown to nobody but the splash,
# so the gate renders on the Streaming pace (src/app/shaderbuildgate.cpp, gateFrame: a compile lands frames later, the gate
# waits for it on the published state); the editor's viewport sets its own pace (src/viewport/enginesceneviewport.cpp,
# the streaming of a world it is showing). Any other setter would let some view draw frames with work put off.
#
# THE LAW, over src/ with comments stripped:
#   (1) `FramePace::Streaming` and `setFramePace(` appear only in those two files;
#   (2) in the gate's file, setFramePace is called exactly once (gateFrame's body) and renderOneFrame( exactly once (the
#       same body): every frame of the gate is a gateFrame.
# A NEGATIVE CONTROL runs first: a synthetic tree with a third setter (red), a bare renderOneFrame in the gate (red), the
# lawful shape (green).
# usage: gate_pace.py <source dir>
import os, re, sys, tempfile

GATE = os.path.join('src', 'app', 'shaderbuildgate.cpp')
VIEWPORT = os.path.join('src', 'viewport', 'enginesceneviewport.cpp')
ALLOWED = {GATE, VIEWPORT}


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', text)


def check(root):
    bad = []
    for d, _, files in os.walk(os.path.join(root, 'src')):
        for f in files:
            if not f.endswith(('.cpp', '.h')):
                continue
            path = os.path.join(d, f)
            rel = os.path.relpath(path, root)
            code = strip_comments(open(path, errors='replace').read())
            if rel not in ALLOWED and ('FramePace::Streaming' in code or 'setFramePace(' in code):
                bad.append('(1) %s sets the frame pace (only the gate and the viewport may)' % rel)
            if rel == GATE:
                sets, frames = code.count('setFramePace('), code.count('renderOneFrame(')
                if sets != 1 or frames != 1:
                    bad.append('(2) %s: setFramePace( %d time(s), renderOneFrame( %d time(s) - every gate frame must be '
                               'gateFrame (one of each, in its body)' % (rel, sets, frames))
    return bad


def control():
    gate_ok = 'void gateFrame(E &e) { e.setFramePace(FramePace::Streaming); e.renderOneFrame(); }\n// renderOneFrame( in a comment\n'
    cases = [
        ('a third setter', {GATE: gate_ok, 'src/ui/x.cpp': 'void f(E &e) { e.setFramePace(FramePace::Streaming); }'}, True),
        ('a bare frame in the gate', {GATE: gate_ok + 'void g(E &e) { e.renderOneFrame(); }'}, True),
        ('the lawful shape', {GATE: gate_ok, VIEWPORT: 'P p() { return FramePace::Streaming; } void s(E *e) { e->setFramePace(p()); }'}, False),
    ]
    ok = True
    for name, files, red in cases:
        with tempfile.TemporaryDirectory() as t:
            for rel, text in files.items():
                os.makedirs(os.path.dirname(os.path.join(t, rel)), exist_ok=True)
                open(os.path.join(t, rel), 'w').write(text)
            got = bool(check(t))
        print('%s: control "%s": %s (expected %s)' % ('ok  ' if got == red else 'FAIL', name, 'red' if got else 'green',
                                                      'red' if red else 'green'), flush=True)
        ok = ok and got == red
    return ok


def main():
    root = sys.argv[1]
    if not control():
        print('source.gate_pace: FAIL (the negative control)', flush=True)
        return 1
    if not os.path.exists(os.path.join(root, GATE)):
        print('source.gate_pace: FAIL - %s not found (a law read over a file it never opened proves nothing)' % GATE)
        return 1
    bad = check(root)
    for b in bad:
        print('FAIL: ' + b, flush=True)
    print('source.gate_pace: %s' % ('FAIL (%d)' % len(bad) if bad else 'PASS'), flush=True)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
