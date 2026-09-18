#!/usr/bin/env python3
"""source.gate_scope_rules — THE SCOPING TOOL'S OWN GUARD (lane STUDIO-SMALL-A).

`scripts/gate-scope.py` decides which suites a lane runs, so a defect in it is
invisible by construction: the wrong answer is a SHORTER gate, and a shorter
gate passes. One such defect shipped and cost SQUARE-1 a green gate that tested
nothing (ledger §620 item 5):

    cd build-linux
    python3 ../scripts/gate-scope.py <range> --build . --run
        gate-scope: 1 touched path(s)
          src/services/vrworld.cpp
              -> tests/player; tests/ui; ... ; rule ^src/services/vrworld
        SCOPED tier: NOTHING to gate — every touched path is docs/scripts/data
        $ echo $?
        0

The mechanism: `--build .` was resolved against the REPO ROOT, `ctest
--show-only=json-v1` in an unconfigured directory prints an empty inventory and
exits 0 (measured), and `add()` keeps only names the inventory knows — so every
suite the rules had just chosen was filtered out, silently, and the tool
reported an empty gate as success while printing the rationale for sixteen test
directories it had decided to skip.

THE FOUR CASES, and each one is a rule of the tool rather than a number:

  1. A CODE PATH SELECTS SUITES. `src/services/vrworld.cpp` selects the vr
     suites (its area rule) and always the smoke pair.
  2. `--build .` FROM INSIDE THE BUILD DIR MEANS THE BUILD DIR — the same
     selection as the absolute form, byte for byte. FAILS BEFORE the fix.
  3. A DIRECTORY THAT IS NOT A BUILD DIR IS AN ERROR, not an empty answer
     (exit 2, naming the directory). FAILS BEFORE the fix (exit 0).
  4. THE HONEST EMPTY SELECTION SURVIVES: a docs-only change still selects
     nothing and still exits 0 — that is the case the lanes rely on, and the
     fix must not turn it into a fallback to the merge tier.

  5. AND THE INVARIANT BEHIND CASE 1: a change that moved CODE can never select
     nothing, because ALWAYS_ON_CODE adds app.startup_quiet + api.contract to
     every code path. The tool now exits 3 rather than printing an empty
     selection when that is violated; this case asserts the positive half on a
     file with no area rule of its own (`src/io/scenereader.cpp`).

Run: gate_scope_rules.py <source-dir> <build-dir>
"""

import os
import subprocess
import sys
import tempfile

FAILURES = []


def run(args, cwd):
    p = subprocess.run([sys.executable] + args, cwd=cwd, capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAILURES.append(what)


def main(source, build):
    tool = os.path.join(source, "scripts", "gate-scope.py")
    if not os.path.isfile(tool):
        print("gate_scope_rules: cannot find %s" % tool)
        return 2
    if not os.path.isfile(os.path.join(build, "CTestTestfile.cmake")):
        print("gate_scope_rules: %s is not a configured build dir" % build)
        return 2

    # 1. a code path selects suites, and the ones its rule names.
    code, out, err = run([tool, "--files", "src/services/vrworld.cpp", "--build", build], source)
    check(code == 0, "a code path exits 0 (%d)%s" % (code, ("\n" + err) if code else ""))
    check("SCOPED tier:" in out and "NOTHING to gate" not in out,
          "a code path selects a scoped tier rather than nothing")
    check("vr.session" in out, "the vr suites are in it (the file's own area rule)")
    check("app.startup_quiet" in out and "api.contract" in out,
          "the smoke pair rides every code path")
    absolute_form = out

    # 2. THE TRAP: `--build .` from inside the build dir is the same answer.
    code, out, err = run([tool, "--files", "src/services/vrworld.cpp", "--build", "."], build)
    check(code == 0, "`--build .` from inside the build dir exits 0 (%d)" % code)
    check("NOTHING to gate" not in out,
          "`--build .` from inside the build dir does NOT report an empty gate "
          "(the SQUARE-1 trap: it used to, with exit 0)")
    # The two runs print the same selection; the cost table can differ by the
    # dir the times were read from, so compare the ctest line.
    def ctest_line(text):
        for line in text.splitlines():
            if line.startswith("ctest -j"):
                return line
        return ""
    check(ctest_line(out) == ctest_line(absolute_form) and ctest_line(out) != "",
          "the CWD-relative and absolute forms select the same suites")

    # 3. a directory that registers no suite is an ERROR.
    with tempfile.TemporaryDirectory() as empty:
        code, out, err = run([tool, "--files", "src/services/vrworld.cpp", "--build", empty], source)
        check(code == 2, "an unconfigured --build dir exits 2 (%d)" % code)
        check("registers no ctest suite" in err,
              "...and says so, naming the directory")
        check("NOTHING to gate" not in out,
              "...instead of reporting an empty gate")

    # 4. the honest empty selection: docs only.
    code, out, err = run([tool, "--files", "docs/TESTING_GATE.md", "--build", build], source)
    check(code == 0, "a docs-only change exits 0 (%d)" % code)
    check("NOTHING to gate" in out,
          "a docs-only change still selects nothing (the case lanes rely on)")
    check("MERGE TIER" not in out,
          "...and does not fall back to the merge tier")

    # 5. the invariant, on a file whose own rule is broad.
    code, out, err = run([tool, "--files", "src/io/scenereader.cpp", "--build", build], source)
    check(code == 0 and "NOTHING to gate" not in out,
          "a source file with no precise rule still gates (exit %d)" % code)

    if FAILURES:
        print("source.gate_scope_rules: FAILED (%d)" % len(FAILURES))
        return 1
    print("source.gate_scope_rules: PASSED")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
