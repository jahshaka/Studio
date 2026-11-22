#!/usr/bin/env python3
# source.test_arms_unreachable (SHADER-SYSTEM-1, the lead's ruling (b) on COLD-BOOT-1's final read) - A TEST ARM IS A
# FIXTURE's DOOR: THE APP CANNOT SET ONE.
#
# WHY: two arms of the engine's registry exist only to force a state a test cannot otherwise reach - shaders.asyncFail
# (every object's own permutation FAILS at its request: the fork's HlmsAsyncCompiler::_jahFailRequests) and
# frames.incompleteHold (every scene's frame is noted incomplete: PublishedFrame::Completeness term bit 4). Set in the app
# they break its picture on purpose. A fixture sets them through Engine::setArm directly; the app's one door onto the
# registry is the scripting verb engine.arm (src/scripting/modules/engineapi.cpp), which REFUSES a test arm by name.
#
# THE LAW, over the tree with comments stripped:
#   (1) the kArms rows of shaders.asyncFail and frames.incompleteHold (irisgl/engine/src/OgreFrameMonitor.cpp) end with
#       the TEST flag `true`, and the registry's list() hands it on (`a.test = kArms[i].test`);
#   (2) in src/, `setArm(` is called only by the verb's file and by jahlab (src/lab/: the lab's own door);
#   (3) the verb refuses a test arm BEFORE it sets: `.test` is read in engineapi.cpp ahead of its `setArm(`;
#   (4) `_jahFailRequests` appears nowhere in src/.
# A NEGATIVE CONTROL runs first: synthetic trees with a test arm unflagged, a second src/ caller of setArm, a verb that sets
# without refusing, a src/ call of _jahFailRequests (each red), and the lawful shape (green).
# usage: test_arms_unreachable.py <source dir>
import os, re, sys, tempfile

TEST_ARMS = ('ShaderAsyncFail', 'FrameIncompleteHold')
MONITOR = os.path.join('irisgl', 'engine', 'src', 'OgreFrameMonitor.cpp')
VERB = os.path.join('src', 'scripting', 'modules', 'engineapi.cpp')
LAB = os.path.join('src', 'lab') + os.sep


def strip_comments(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', text)


def read(root, rel):
    try:
        return open(os.path.join(root, rel), errors='replace').read()
    except OSError:
        return None


def check(root):
    bad = []
    mon = read(root, MONITOR)
    if mon is None:
        return ['(1) %s is missing' % MONITOR]
    code = strip_comments(mon)
    for arm in TEST_ARMS:
        m = re.search(r'\{\s*ArmId::%s\s*,(.*?)\}\s*,' % arm, code, flags=re.S)
        if not m:
            bad.append('(1) no kArms row for ArmId::%s' % arm)
        elif not re.search(r',\s*true\s*$', m.group(1)):
            bad.append('(1) the kArms row of ArmId::%s does not carry the TEST flag (true, last)' % arm)
    if not re.search(r'a\.test\s*=\s*kArms\[i\]\.test', code):
        bad.append('(1) the registry\'s list() does not hand the TEST flag on (a.test = kArms[i].test)')
    for d, _, files in os.walk(os.path.join(root, 'src')):
        for f in files:
            if not f.endswith(('.cpp', '.h')):
                continue
            rel = os.path.relpath(os.path.join(d, f), root)
            src = strip_comments(read(root, rel) or '')
            if '_jahFailRequests' in src:
                bad.append('(4) %s calls _jahFailRequests (a fixture\'s door)' % rel)
            if 'setArm(' not in src or rel.startswith(LAB):
                continue
            if rel != VERB:
                bad.append('(2) %s calls setArm( (only the verb engine.arm and jahlab may)' % rel)
                continue
            at = src.find('setArm(')
            if '.test' not in src[:at]:
                bad.append('(3) %s sets an arm without refusing a TEST arm first (.test read before setArm()' % rel)
    return bad


LAWFUL_MONITOR = '''constexpr ArmDef kArms[] = {
    { ArmId::ReflectMotion, 1.0, 0.0, 1.0, "a measuring arm" },
    { ArmId::ShaderAsyncFail, 0.0, 0.0, 1.0, "A TEST DOOR", true },
    { ArmId::FrameIncompleteHold, 0.0, 0.0, 1.0, "A TEST DOOR", true },
};
void list() { a.test = kArms[i].test; }
'''
LAWFUL_VERB = '''QVariant EngineApi::arm() {
    for (const ArmInfo &a : e.arms()) if (a.name == key && a.test) { return; }   // refused
    armed = e.setArm(key, v);
}
'''


def synthetic(monitor, verb, extra=None):
    root = tempfile.mkdtemp(prefix='test-arms-')
    files = {MONITOR: monitor, VERB: verb, os.path.join(LAB, 'labsession.cpp'): 'void f() { e->setArm("x", 1.0); }'}
    files.update(extra or {})
    for rel, text in files.items():
        os.makedirs(os.path.dirname(os.path.join(root, rel)), exist_ok=True)
        with open(os.path.join(root, rel), 'w') as fh:
            fh.write(text)
    return root


def self_test():
    cases = [
        ('a test arm unflagged', synthetic(LAWFUL_MONITOR.replace('"A TEST DOOR", true },\n    { ArmId::FrameIncompleteHold',
                                                                   '"A TEST DOOR" },\n    { ArmId::FrameIncompleteHold'),
                                           LAWFUL_VERB), True),
        ('a second src/ caller of setArm', synthetic(LAWFUL_MONITOR, LAWFUL_VERB,
                                                     {os.path.join('src', 'app', 'x.cpp'): 'void g() { e.setArm("y", 1); }'}),
         True),
        ('the verb sets without refusing', synthetic(LAWFUL_MONITOR, 'void arm() { armed = e.setArm(key, v); }'), True),
        ('a src/ call of _jahFailRequests', synthetic(LAWFUL_MONITOR, LAWFUL_VERB,
                                                      {os.path.join('src', 'app', 'y.cpp'): 'void h() { c->_jahFailRequests(true); }'}),
         True),
        ('the lawful shape', synthetic(LAWFUL_MONITOR, LAWFUL_VERB), False),
    ]
    ok = True
    for name, root, want_red in cases:
        red = bool(check(root))
        good = red == want_red
        ok = ok and good
        print('  %s: the negative control - %s is %s' % ('ok' if good else 'FAIL', name, 'red' if red else 'green'))
    return ok


def main():
    if len(sys.argv) != 2:
        print('usage: test_arms_unreachable.py <source dir>')
        print('source.test_arms_unreachable: FAILED (1)')
        return 2
    fails = 0
    if not self_test():
        fails += 1
    bad = check(sys.argv[1])
    for b in bad:
        print('  FAIL: ' + b)
    if not bad:
        print('  ok: the two TEST arms (%s) carry the flag, the verb refuses them, no other src/ caller sets an arm' %
              ', '.join(TEST_ARMS))
    fails += len(bad)
    sys.stdout.flush()
    print('source.test_arms_unreachable: ' + ('PASSED' if not fails else 'FAILED (%d)' % fails))
    return 0 if not fails else 1


if __name__ == '__main__':
    sys.exit(main())
