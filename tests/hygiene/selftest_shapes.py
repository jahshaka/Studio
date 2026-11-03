#!/usr/bin/env python3
"""source.selftest_shapes - THE SELF-TEST's TWO SHAPES RUN THE SAME FIXTURE (TEST-STREAMLINE-1 3c, URGENT #157).

JAHSHAKA_SELFTEST_POSES=fixture runs the fixture half alone; unset runs the default poses and then the same half. The
clean-state law's first case (card block B: the mover's hash equal in both) only means something if the half is the
SAME in both shapes - its scripts, their substitutions and its frame counts a function of the run's flags (shortRun,
sunContactArm), never of which shape it is. The law on the source (src/app/cli/selftestrunner.cpp):
  S1 the fixture half's block (`if (runFixture) {` to its brace) reads neither `runDefault` nor `posesSel`;
  S2 every substitution of a fixture script there (`.replace(`) is guarded by shortRun / sunContactArm alone;
  S3 its scripts are the file's constants (kFixtureBScript, kFixtureBNoRaysScript, kFixtureMoverScript), not texts built
     from the shape.
A guard of an existing property (green at the base), fed a violation first. The picture's proof is block B.

Run: selftest_shapes.py <source-dir>
"""
import re
import sys

FAIL = []


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAIL.append(what)


def block(text, head):
    i = text.find(head)
    if i < 0:
        return None
    j = text.index("{", i)
    depth = 0
    for k in range(j, len(text)):
        if text[k] == "{":
            depth += 1
        elif text[k] == "}":
            depth -= 1
            if depth == 0:
                return text[j:k + 1]
    return None


def laws(text):
    code = re.sub(r"//[^\n]*", "", text)
    b = block(code, "if (runFixture) {")
    out = [(b is not None, "S0 the fixture half's block is found")]
    if b is None:
        return out
    out.append(("runDefault" not in b and "posesSel" not in b, "S1 the fixture half reads neither runDefault nor posesSel"))
    guards = re.findall(r"if \(([^)]*)\)\s*\w+\.replace\(", b)
    bad = [g for g in guards if set(re.findall(r"[A-Za-z_]\w*", g)) - {"shortRun", "sunContactArm"}]
    out.append((guards and not bad, "S2 every fixture-script substitution is guarded by shortRun / sunContactArm alone (%d; "
                "other: %s)" % (len(guards), bad)))
    srcs = re.findall(r"QByteArray \w+\((\w+)\);", b)
    out.append((sorted(set(srcs)) == ["kFixtureBNoRaysScript", "kFixtureBScript", "kFixtureMoverScript"],
                "S3 its scripts are the file's three constants (%s)" % sorted(set(srcs))))
    return out


def main(src):
    text = open(src + "/src/app/cli/selftestrunner.cpp", errors="replace").read()
    muts = [("S1", "if (shortRun) fixtureMover.replace(", "if (shortRun || !runDefault) fixtureMover.replace("),
            ("S3", "QByteArray fixtureB2(kFixtureBNoRaysScript);", "QByteArray fixtureB2(runDefault ? kFixtureBNoRaysScript : kX);")]
    for law, a, b in muts:
        red = [w for ok, w in laws(text.replace(a, b, 1)) if not ok and w.startswith(law)]
        check(bool(red) and a in text, "the lint is fed a violation of %s and reds" % law)
    for ok, what in laws(text):
        check(ok, what)
    print("source.selftest_shapes: %s" % ("FAILED (%d)" % len(FAIL) if FAIL else "all ok"))
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
