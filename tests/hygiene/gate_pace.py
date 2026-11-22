#!/usr/bin/env python3
# source.gate_pace (COLD-BOOT-1 stage 4) - THE STREAMING PACE IS THE GATE's AND THE VIEWPORT's, NOWHERE ELSE.
#
# WHY: a frame on the Complete pace owes the whole picture - every consumer joins what it needs, so no view a user sees
# shows a frame with a permutation missing (the reveal law). The startup gate's frames are shown to nobody but the splash,
# so the gate renders on the Streaming pace (src/app/shaderbuildgate.cpp, gateFrame: a compile lands frames later, the gate
# waits for it on the published state); the editor's viewport sets its own pace (src/viewport/enginesceneviewport.cpp,
# the streaming of a world it is showing). Any other setter would let some view draw frames with work put off.
#
# ...AND THE GATE GIVES BACK WHAT IT SET (the leak of stage 4's gate): the pace is IN FORCE UNTIL SET AGAIN (Engine::
# setFramePace; the engine never lowers it), so a gate that set Streaming and left it made every later frame of the
# session Streaming - the editor's, a script's, the selftest's six (its B2 moved: the lighting arm built one stage a frame
# instead of whole); and its kept warm view stayed asynchronous.
#
# THE LAW, over src/ with comments stripped:
#   (1) `FramePace::Streaming` and `setFramePace(` appear only in those two files;
#   (2) in the gate's file, renderOneFrame( exactly once and setFramePace(FramePace::Streaming) exactly once (gateFrame's
#       body): every frame of the gate is a gateFrame;
#   (3) in the gate's file, a DESTRUCTOR's body (the gate's scope guard: every way out) sets the pace back -
#       setFramePace(FramePace::Complete) - and the warm view's compiles back - setAsyncShaders(false) - and the gate's
#       file sets the pace nowhere else.
# A NEGATIVE CONTROL runs first: a synthetic tree with a third setter (red), a bare renderOneFrame in the gate (red), a
# gate that never gives the pace back (red), one that gives it back outside a destructor (red), the lawful shape (green).
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
                bad.extend(gate_law(rel, code))
    return bad


def destructor_bodies(code):
    """The bodies of every `~Name() { ... }` in `code` (braces matched)."""
    bodies = []
    for m in re.finditer(r'~\w+\s*\(\s*\)\s*\{', code):
        depth, i = 1, m.end()
        while i < len(code) and depth:
            depth += {'{': 1, '}': -1}.get(code[i], 0)
            i += 1
        bodies.append(code[m.end():i - 1])
    return bodies


def gate_law(rel, code):
    bad = []
    nospace = re.sub(r'\s+', '', code)
    sets = nospace.count('setFramePace(')
    streaming = nospace.count('setFramePace(FramePace::Streaming)')
    complete = nospace.count('setFramePace(FramePace::Complete)')
    frames = nospace.count('renderOneFrame(')
    if streaming != 1 or frames != 1:
        bad.append('(2) %s: setFramePace(FramePace::Streaming) %d time(s), renderOneFrame( %d time(s) - every gate frame '
                   'must be gateFrame (one of each, in its body)' % (rel, streaming, frames))
    guards = [re.sub(r'\s+', '', b) for b in destructor_bodies(code)]
    if not any('setFramePace(FramePace::Complete)' in g and 'setAsyncShaders(false)' in g for g in guards):
        bad.append('(3) %s: no destructor gives the pace back (setFramePace(FramePace::Complete)) and the warm view\'s '
                   'compiles back (setAsyncShaders(false)) - the gate must leave the engine as it found it on every way '
                   'out' % rel)
    if sets != streaming + 1 or complete != 1:
        bad.append('(3) %s: setFramePace( %d time(s) - only gateFrame\'s Streaming and the scope guard\'s Complete' %
                   (rel, sets))
    return bad


def control():
    frame = 'void gateFrame(E &e) { e.setFramePace(FramePace::Streaming); e.renderOneFrame(); }\n// renderOneFrame( in a comment\n'
    guard = 'struct S { E &e; V *v; ~S() { e.setFramePace( FramePace::Complete ); if (v) { v->setAsyncShaders(false); } } };\n'
    gate_ok = frame + guard
    cases = [
        ('a third setter', {GATE: gate_ok, 'src/ui/x.cpp': 'void f(E &e) { e.setFramePace(FramePace::Streaming); }'}, True),
        ('a bare frame in the gate', {GATE: gate_ok + 'void g(E &e) { e.renderOneFrame(); }'}, True),
        ('a gate that never gives the pace back', {GATE: frame}, True),
        ('the pace given back outside a destructor',
         {GATE: frame + 'void end(E &e, V *v) { e.setFramePace(FramePace::Complete); v->setAsyncShaders(false); }'}, True),
        ('the warm view left asynchronous', {GATE: frame + 'struct S { E &e; ~S() { e.setFramePace(FramePace::Complete); } };'}, True),
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
