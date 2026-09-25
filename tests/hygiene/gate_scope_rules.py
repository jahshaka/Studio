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

  6. TARGET TESTS RUN AND DO NOT GATE (PHOTON phase A, A1 section 0; lane
     FENCE-1). A suite labelled `photon-target` states the CORRECT number for a
     term the renderer gets wrong today, so it is RED until its part lands. The
     one thing that must never happen is a target deciding a lane's gate — and
     the one thing that must never happen INSTEAD is a target quietly not
     running, because then nobody sees the distance to the bar. So the tool
     keeps them out of the gating ctest command, puts them in a SECOND command
     of their own, and says both things in words. This case reads a file that
     selects a target suite and asserts all three: the gating command does not
     name it, the target section does, and the MERGE tier's -LE carries the
     label. It is a rule of the tool, not a number.

  7. THE FALLBACK HONOURS -j (lane DEVPROCESS-1). `--json -j2` on a path that
     falls back reports a MERGE-tier `command` at -j2, and the printed tier line
     follows `-j` too; `--run` runs that same string.

  8. The decode's own suite (engine.atom_parity) rides the Atom media, HlmsAtom and the pin.

  9. THE TIER DOC QUOTES THE SCRIPT (POST-C-FIXES-1): docs/TESTING_GATE.md carries
     no literal `-LE` label list — it names `gate-scope.py --merge-tier`, which
     prints the MERGE tier from NIGHTLY_LABELS | TARGET_LABELS — and an edit to
     the doc selects this guard.

Run: gate_scope_rules.py <source-dir> <build-dir>
"""

import json
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

    # 4. the honest empty selection: docs only. (Not docs/TESTING_GATE.md: case 9's
    # lint reads that one, so it selects the hygiene rows.)
    code, out, err = run([tool, "--files", "docs/BUILDING_LINUX.md", "--build", build], source)
    check(code == 0, "a docs-only change exits 0 (%d)" % code)
    check("NOTHING to gate" in out,
          "a docs-only change still selects nothing (the case lanes rely on)")
    check("MERGE TIER" not in out,
          "...and does not fall back to the merge tier")

    # 5. the invariant, on a file whose own rule is broad.
    code, out, err = run([tool, "--files", "src/io/scenereader.cpp", "--build", build], source)
    check(code == 0 and "NOTHING to gate" not in out,
          "a source file with no precise rule still gates (exit %d)" % code)

    # 6. TARGET TESTS: they run, they are reported, and they do not gate.
    # tests/gi/test_gi_chain_face.cpp registers BOTH rows of the chain-face pair,
    # so it selects a target suite and an ordinary one from one path.
    #
    # THE COMPARISONS ARE MADE ON UNESCAPED TEXT. The suite names inside a ctest
    # -R regex are `re.escape`d, so `gi.chain_face` appears as `gi\.chain_face`
    # and a plain substring test for the readable name fails on a correct
    # command. Stripping backslashes is the whole of it.
    def plain(text):
        return text.replace("\\", "")

    code, out, err = run([tool, "--files", "tests/gi/test_gi_chain_face.cpp",
                          "--build", build], source)
    check(code == 0, "a path that selects a target suite exits 0 (%d)" % code)
    gating, target_cmd, merge = "", "", ""
    for line in out.splitlines():
        if not line.startswith("ctest -j"):
            continue
        if "NOT gating" in line:
            target_cmd = line
        elif "-LE" in line:
            merge = line
        elif not gating:
            gating = line
    check("gi.chain_face_target" in out,
          "the target row is SELECTED (it must run, or nobody sees the distance to the bar)")
    check("TARGET TESTS" in out and "do NOT decide this gate" in out,
          "...and the tool says in words that it does not decide the gate")
    check(gating != "" and "gi.chain_face_target" not in plain(gating),
          "the GATING ctest command does not name the target row")
    check("gi.chain_face|" in plain(gating) or "gi.chain_face)" in plain(gating),
          "...while the ordinary row of the same pair IS in the gating command")
    check(target_cmd != "" and "gi.chain_face_target" in plain(target_cmd)
          and " -j1 " in target_cmd,
          "the target suites get a ctest command of their own, at -j1")

    # ...and the MERGE tier's own exclusion carries the label, from the same
    # definition: a tier command that filters a label the tool does not know
    # about (or the reverse) is how a stale exclusion survives a year. A path
    # with no rule is what prints the tier command.
    code2, out2, err2 = run([tool, "--files", "CMakeLists.txt", "--build", build], source)
    for line in out2.splitlines():
        if line.startswith("ctest -j4 ") and "-LE" in line:
            merge = line
    check("photon-target" in plain(merge) and "benchmark" in plain(merge),
          "the MERGE tier's -LE carries photon-target beside the nightly labels")

    # 7. THE FALLBACK HONOURS -j (DEVPROCESS-1 item 2). A lane beside other live
    # lanes runs `-j 2`; the fallback to the MERGE tier used to print and RUN a
    # hardcoded -j4 regardless. A path with no rule is what falls back.
    code, out, err = run([tool, "--files", "CMakeLists.txt", "--build", build, "--json", "-j", "2"], source)
    try:
        doc = json.loads(out)
    except ValueError:
        doc = {}
    check(code == 0 and doc.get("fallback"), "a path with no rule falls back (exit %d)" % code)
    command = doc.get("command", "")
    check(command.startswith("ctest -j2 ") and "-j4" not in command,
          "the fallback's JSON command carries the requested -j2 (%r)" % command[:40])
    check("ddgi_raster" not in command and " -E " not in command,
          "...and excludes no suite by name (the dead gi.ddgi_raster filter is deleted)")
    code, out, err = run([tool, "--files", "CMakeLists.txt", "--build", build, "-j", "3"], source)
    check(any(l.startswith("ctest -j3 ") and "-LE" in l for l in out.splitlines()),
          "...and so does the printed MERGE tier line (-j3)")

    # 8. THE DECODE'S OWN SUITE (PHOTON-HIT-SHADE-1 audit F2): the Atom media, HlmsAtom and
    # the fork pin (Ogre's Pbs pieces are the decode's text too) select engine.atom_parity;
    # a lane that changed all three gated without it once.
    for path in ("irisgl/engine/media/Hlms/Atom/Any/800.Atom_piece_ps.any",
                 "irisgl/engine/src/HlmsAtom.cpp", "irisgl/thirdparty/ogre-next"):
        code, out, err = run([tool, "--files", path, "--build", build], source)
        check(code == 0 and ("engine.atom_parity|" in plain(out) or "engine.atom_parity)" in plain(out)),
              "%s selects engine.atom_parity" % path)

    # 9. THE TIER DOC QUOTES THE SCRIPT, NEVER A COPY OF ITS SET (POST-C-FIXES-1).
    # docs/TESTING_GATE.md's MERGE row carried a literal
    # -LE "^(benchmark|shadercache-attack)$" that went stale the day TARGET_LABELS
    # joined the tier (it never named photon-target). The set lives in
    # gate-scope.py alone; the doc names `--merge-tier`, which prints the command.
    import re as _re
    doc_path = os.path.join(source, "docs", "TESTING_GATE.md")
    with open(doc_path, errors="replace") as fh:
        doc_lines = fh.read().splitlines()
    literal = [i + 1 for i, l in enumerate(doc_lines) if _re.search(r'-LE\s+["\']\^?\(', l)]
    check(not literal,
          "docs/TESTING_GATE.md quotes no literal -LE label list (lines %s) - it names "
          "`gate-scope.py --merge-tier`" % literal)
    check(any("--merge-tier" in l for l in doc_lines),
          "...and it names `gate-scope.py --merge-tier` as the tier's command")
    code, out, err = run([tool, "--merge-tier", "-j", "2"], source)
    check(code == 0 and out.strip().startswith("ctest -j2 ") and "photon-target" in plain(out)
          and "benchmark" in plain(out) and "shadercache-attack" in plain(out),
          "`--merge-tier -j 2` prints the MERGE tier at -j2 with every excluded label (%r)" % out.strip())
    code, out, err = run([tool, "--files", "docs/TESTING_GATE.md", "--build", build], source)
    check(code == 0 and "source.gate_scope_rules" in plain(out),
          "an edit to docs/TESTING_GATE.md selects this guard")

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
