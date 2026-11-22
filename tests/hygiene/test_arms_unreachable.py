#!/usr/bin/env python3
# source.test_arms_unreachable (SHADER-SYSTEM-1, the lead's ruling (b) on COLD-BOOT-1's final read) - A TEST ARM IS A
# FIXTURE's DOOR: THE APP CANNOT SET ONE.
#
# WHY: nine arms of the engine's registry exist only to force a state a test cannot otherwise reach - shaders.asyncFail
# (every object's own permutation FAILS at its request: the fork's HlmsAsyncCompiler::_jahFailRequests), frames.
# incompleteHold (every scene's frame is noted incomplete: PublishedFrame::Completeness term bit 4), rays.buildHold and
# rays.buildHoldIds (a ray build held in flight), rays.buildFail (a ray build forced to fail) and textures.residencyHold
# (every texture bind awaited), rayquery.proceedPublishAll, rayquery.stepBound and rayquery.nanRay (the ray query loops' bound:
# every loop publishing, the bound lowered so a loop is cut, a NaN ray the input guard refuses - GATHER-HANG-1 stage 2). Set in the app they break its picture on purpose. A fixture sets them through
# Engine::setArm directly; the app's one door onto the registry is the scripting verb engine.arm
# (src/scripting/modules/engineapi.cpp), which REFUSES a test arm by name.
# THE RULE THAT TELLS THEM APART: a TEST arm makes the engine MISBEHAVE ON PURPOSE (a failure, a hold, a wait no correct
# frame has); a MEASURING arm SELECTS BETWEEN TWO CORRECT PATHS (an A/B the lead runs in the app) and stays settable.
#
# THE LAW, over the tree with comments stripped:
#   (1) the kArms rows of the nine (irisgl/engine/src/OgreFrameMonitor.cpp) end with
#       the TEST flag `true`, and the registry's list() hands it on (`a.test = kArms[i].test`);
#   (2) in src/, `setArm(` is called only by the verb's file and by jahlab (src/lab/: the lab's own door);
#   (3) the verb refuses a test arm BEFORE it sets: `.test` is read in engineapi.cpp ahead of its `setArm(`;
#   (4) `_jahFailRequests` appears nowhere in src/;
#   (5) IRISGL's OWN CALLERS (the final read's leftover: Stillness.cpp sets fixed measuring arms through Engine::setArm):
#       every `setArm("<name>"` in irisgl/engine names no TEST arm (the names are the arm table's X(Id, "name") rows,
#       EnginePrivate.h), and a `setArm(` whose name is not a literal is only a forwarder - inside a function itself
#       named setArm, in a file whose every other call is a literal;
#   (6) THE ENGINE's SCRIPT DOOR (SCRIPT-IN-ENGINE-1: the command engine.arm, irisgl/engine/src/script/commands_engine.cpp)
#       sets an arm by a name that is no literal only after reading the arm's `.test` AND the host's `testDoors` (a TEST
#       arm opens only in a host started with --test-doors: jahengine's test rows) - the verb's law (3), the flag added.
# A NEGATIVE CONTROL runs first: synthetic trees with a test arm unflagged, a second src/ caller of setArm, a verb that sets
# without refusing, a src/ call of _jahFailRequests, an irisgl caller of a TEST arm by its name, a non-literal irisgl caller that is no
# forwarder (each red), and the lawful shape (green).
# usage: test_arms_unreachable.py <source dir>
import os, re, sys, tempfile

TEST_ARMS = ('RayBuildHold', 'RayBuildHoldIds', 'RayBuildFail', 'ShaderAsyncFail', 'TextureResidencyHold',
             'FrameIncompleteHold', 'ProceedPublishAll', 'StepBound', 'NanRay')
MONITOR = os.path.join('irisgl', 'engine', 'src', 'OgreFrameMonitor.cpp')
VERB = os.path.join('src', 'scripting', 'modules', 'engineapi.cpp')
LAB = os.path.join('src', 'lab') + os.sep
TABLE = os.path.join('irisgl', 'engine', 'src', 'EnginePrivate.h')
ENGINE = os.path.join('irisgl', 'engine')
SCRIPT_DOOR = os.path.join('irisgl', 'engine', 'src', 'script', 'commands_engine.cpp')


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
    bad += check_irisgl(root)
    return bad


def check_irisgl(root):
    bad = []
    table = strip_comments(read(root, TABLE) or '')
    names = dict(re.findall(r'X\(\s*(\w+)\s*,\s*"([^"]+)"\s*\)', table))
    test_names = set(names.get(a) for a in TEST_ARMS) - {None}
    if len(test_names) != len(TEST_ARMS):
        return ['(5) the arm table (%s) does not name every TEST arm (%d of %d)' % (TABLE, len(test_names), len(TEST_ARMS))]
    for d, _, files in os.walk(os.path.join(root, ENGINE)):
        for f in files:
            if not f.endswith(('.cpp', '.h')):
                continue
            rel = os.path.relpath(os.path.join(d, f), root)
            src = strip_comments(read(root, rel) or '')
            if rel == MONITOR:
                continue   # the registry: Engine::setArm's own definition
            for m in re.finditer(r'(?<![\w:])setArm\(\s*([^,)]*)', src):
                arg = m.group(1).strip()
                lit = re.fullmatch(r'"([^"]*)"', arg)
                if lit:
                    if lit.group(1) in test_names:
                        bad.append('(5) %s sets the TEST arm "%s" (a fixture\'s door, never irisgl\'s own code)' %
                                   (rel, lit.group(1)))
                    continue
                if arg.startswith('const '):
                    continue   # a declaration or the definition of a setArm (its parameter list)
                if rel == SCRIPT_DOOR:
                    before = src[:m.start()]
                    if '.test' not in before or 'testDoors' not in before:
                        bad.append('(6) %s sets an arm without reading its .test and the host\'s testDoors first' % rel)
                    continue
                # A FORWARDER: the non-literal name is the parameter of a setArm defined just above it, in this file
                fwd = [d for d in re.finditer(r'\bsetArm\(\s*const\s[^,]*?(\w+)\s*,', src)
                       if d.start() < m.start() <= d.start() + 400 and d.group(1) == arg]
                if not fwd:
                    bad.append('(5) %s calls setArm( with a name that is no literal (%s) outside a setArm forwarder' %
                               (rel, arg))
    return bad


LAWFUL_MONITOR = '''constexpr ArmDef kArms[] = {
    { ArmId::ReflectMotion, 1.0, 0.0, 1.0, "a measuring arm" },
    { ArmId::RayBuildHold, 0.0, 0.0, 1.0, "A TEST DOOR", true },
    { ArmId::RayBuildHoldIds, 0.0, 0.0, 1.0, "A TEST DOOR", true },
    { ArmId::RayBuildFail, 0.0, 0.0, 1.0, "A TEST DOOR", true },
    { ArmId::ShaderAsyncFail, 0.0, 0.0, 1.0, "A TEST DOOR", true },
    { ArmId::TextureResidencyHold, 0.0, 0.0, 1.0, "A TEST DOOR", true },
    { ArmId::FrameIncompleteHold, 0.0, 0.0, 1.0, "A TEST DOOR", true },
    { ArmId::ProceedPublishAll, 0.0, 0.0, 1.0, "A TEST DOOR", true },
    { ArmId::StepBound, 0.0, 0.0, 4096.0, "A TEST DOOR", true },
    { ArmId::NanRay, 0.0, 0.0, 1.0, "A TEST DOOR", true },
};
void list() { a.test = kArms[i].test; }
'''
LAWFUL_VERB = '''QVariant EngineApi::arm() {
    for (const ArmInfo &a : e.arms()) if (a.name == key && a.test) { return; }   // refused
    armed = e.setArm(key, v);
}
'''


LAWFUL_TABLE = '\n'.join('    X(%s, "test.%s") \\' % (a, a) for a in TEST_ARMS) + '\n    X(GiFreeze, "gi.freeze")\n'
LAWFUL_STILLNESS = '''struct M {
    bool setArm(const char *name, double value) { bool ok = false; e.request([&](Engine &e) { ok = e.setArm(name, value); }); return ok; }
    void restore() { setArm("gi.freeze", 0.0); }
};
'''
STILLNESS = os.path.join(ENGINE, 'src', 'Stillness.cpp')
LAWFUL_DOOR = '''Value arm(Call &c) {
    for (const ArmInfo &a : e->arms()) if (a.name == name) { test = a.test; }
    if (test && !st->testDoors) return c.fail("a test arm");
    e->request([&](Engine &x) { ok = x.setArm(name, value); });
}
'''


def synthetic(monitor, verb, extra=None):
    root = tempfile.mkdtemp(prefix='test-arms-')
    files = {MONITOR: monitor, VERB: verb, os.path.join(LAB, 'labsession.cpp'): 'void f() { e->setArm("x", 1.0); }',
             TABLE: LAWFUL_TABLE, STILLNESS: LAWFUL_STILLNESS, SCRIPT_DOOR: LAWFUL_DOOR}
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
        ('an irisgl caller of a TEST arm by its name',
         synthetic(LAWFUL_MONITOR, LAWFUL_VERB, {STILLNESS: LAWFUL_STILLNESS.replace('"gi.freeze"', '"test.RayBuildFail"')}),
         True),
        ('a non-literal irisgl caller that is no forwarder',
         synthetic(LAWFUL_MONITOR, LAWFUL_VERB,
                   {os.path.join(ENGINE, 'src', 'Other.cpp'): 'void go(const char *n) { engine.setArm(n, 1.0); }'}), True),
        ('the script door that never reads testDoors',
         synthetic(LAWFUL_MONITOR, LAWFUL_VERB, {SCRIPT_DOOR: LAWFUL_DOOR.replace(' && !st->testDoors', '')}), True),
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
        print('  ok: the TEST arms (%s) carry the flag, the verb refuses them, no other src/ caller sets an arm, irisgl'
              ' sets none of them' %
              ', '.join(TEST_ARMS))
    fails += len(bad)
    sys.stdout.flush()
    print('source.test_arms_unreachable: ' + ('PASSED' if not fails else 'FAILED (%d)' % fails))
    return 0 if not fails else 1


if __name__ == '__main__':
    sys.exit(main())
