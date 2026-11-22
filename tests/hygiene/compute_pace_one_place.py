#!/usr/bin/env python3
# source.compute_pace_one_place (SHADER-SYSTEM-1 7a, R2 (c)) - A COMPUTE PERMUTATION's LATENESS MEETS THE FRAME's PACE IN
# ONE PLACE.
#
# WHY: THE ONE ASYNC-SHADER RULE - a compile a frame needs may land a frame later only on a frame that does not owe the
# whole picture. Six engine sites put a compute dispatch off in an asynchronous scene (the GI's deferral, the surface
# cache's relight and accumulation, the voxel commit, the screen integrate, the atmosphere, the convolution) and the fork's
# HlmsCompute::dispatch put off every dispatch of an asynchronous workspace: each read "asynchronous" and most never read
# the pace, so a Complete frame (the editor's, a screenshot's) drew what the service had got to. Now the pace reaches the
# fork once, at the frame's head (Ogre::HlmsCompute::_jahSetFrameOwesPicture, OgreEngine::runFrame), and the fork answers
# every site: requestAsync says AsyncReady on a frame that owes its picture and dispatch() joins the request in flight.
#
# THE LAW, over irisgl/engine/src with comments stripped:
#   (1) `_jahSetFrameOwesPicture(` is called exactly once, in OgreEngine.cpp (the frame head);
#   (2) `compilesInBackground()` - the scene's "may this frame land a compile later" - is called only where the subject is
#       not a compute permutation (ALLOWED below, each with its reason): a compute site asks computeOnService() and lets
#       the fork decide by the pace;
#   (3) OgreScene::compilesInBackground's own body reads the pace through the fork (jahMayDefer), never mFramePace.
# A NEGATIVE CONTROL runs first: a synthetic tree with a second setter (red), a compute site calling compilesInBackground
# (red), a compilesInBackground that reads mFramePace (red), the lawful shape (green).
# usage: compute_pace_one_place.py <source dir>
import os, re, sys, tempfile

ENGINE = os.path.join('irisgl', 'engine', 'src')
FRAME_HEAD = os.path.join(ENGINE, 'OgreEngine.cpp')
SCENE = os.path.join(ENGINE, 'OgreScene.cpp')
# the callers of compilesInBackground() whose subject is NOT a compute permutation -> why
ALLOWED = {
    os.path.join(ENGINE, 'OgreRayQuery.cpp'): 'the ray tier\'s pipelines (aheadReady): stage 7\'s law, not a compute job',
    os.path.join(ENGINE, 'EnginePrivate.h'): 'its declaration',
    SCENE: 'its definition',
}


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', text)


def body_of(code, signature):
    m = re.search(signature, code)
    if not m:
        return None
    i = code.find('{', m.end())
    depth, j = 1, i + 1
    while j < len(code) and depth:
        depth += {'{': 1, '}': -1}.get(code[j], 0)
        j += 1
    return code[i + 1:j - 1]


def check(root):
    bad = []
    setters = []
    for d, _, files in os.walk(os.path.join(root, ENGINE)):
        for f in files:
            if not f.endswith(('.cpp', '.h')):
                continue
            path = os.path.join(d, f)
            rel = os.path.relpath(path, root)
            code = strip_comments(open(path, errors='replace').read())
            setters += [rel] * code.count('_jahSetFrameOwesPicture(')
            if 'compilesInBackground()' in code and rel not in ALLOWED:
                bad.append('(2) %s calls compilesInBackground(): a compute site asks computeOnService() and the fork '
                           'decides by the pace' % rel)
            if rel == SCENE:
                body = body_of(code, r'OgreScene::compilesInBackground\s*\(\s*\)\s*const')
                if body is None:
                    bad.append('(3) %s: no OgreScene::compilesInBackground body found' % rel)
                elif 'mFramePace' in body or 'jahMayDefer' not in body:
                    bad.append('(3) %s: compilesInBackground reads the pace itself, not through the fork (jahMayDefer)'
                               % rel)
    if setters != [FRAME_HEAD]:
        bad.append('(1) _jahSetFrameOwesPicture( called %d time(s) (%s): exactly once, in %s' %
                   (len(setters), ', '.join(setters) or 'nowhere', FRAME_HEAD))
    return bad


def self_test():
    lawful = {
        'OgreEngine.cpp': 'void f() { Ogre::HlmsCompute::_jahSetFrameOwesPicture(pace == FramePace::Complete); }\n',
        'OgreScene.cpp': 'bool OgreScene::compilesInBackground() const {\n return computeOnService() && '
                         'Ogre::HlmsCompute::jahMayDefer();\n}\n',
        'OgreRayQuery.cpp': 'void g() { if (scene->compilesInBackground()) return; }\n',
        'OgreSky.cpp': '// compilesInBackground() in a comment is not a call\nvoid h() { if (computeOnService()) {} }\n',
    }
    cases = [
        ('the lawful shape', {}, True),
        ('a second setter', {'OgreSky.cpp': 'void h() { Ogre::HlmsCompute::_jahSetFrameOwesPicture(true); }\n'}, False),
        ('a compute site asking compilesInBackground',
         {'OgreSky.cpp': 'void h() { if (compilesInBackground()) {} }\n'}, False),
        ('compilesInBackground reading mFramePace',
         {'OgreScene.cpp': 'bool OgreScene::compilesInBackground() const {\n return mFramePace != '
                           'FramePace::Complete;\n}\n'}, False),
    ]
    ok = True
    for name, override, green in cases:
        with tempfile.TemporaryDirectory() as t:
            os.makedirs(os.path.join(t, ENGINE))
            for f, text in {**lawful, **override}.items():
                open(os.path.join(t, ENGINE, f), 'w').write(text)
            got = not check(t)
            print('self-test: %s -> %s (%s)' % (name, 'green' if got else 'red', 'ok' if got == green else 'WRONG'))
            ok = ok and got == green
    return ok


def main():
    if len(sys.argv) != 2:
        print('usage: compute_pace_one_place.py <source dir>')
        return 64
    if not self_test():
        print('source.compute_pace_one_place: FAILED (1)')
        return 1
    bad = check(sys.argv[1])
    for b in bad:
        print('FAIL: ' + b)
    print('source.compute_pace_one_place: %s' % ('FAILED (%d)' % len(bad) if bad else 'PASSED'))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
