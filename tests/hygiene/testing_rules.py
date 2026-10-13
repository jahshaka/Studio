#!/usr/bin/env python3
"""source.testing_rules — THE TESTING RULES THAT CAN BE READ FROM THE TREE (MODULAR-GATE-1; T6;
PHOTON_ATOM_CONTRACT §7b "the rules are source.* lint suites").

A rule a lead enforces by reading diffs does not scale to a team; a rule a lint enforces does.
Five of §7b's rules (and R6, TESTING-CLEANUP-2C's) are facts of the tree and the build's registration, so they are checked
here, each with its name in the failure line:

  R1 TIMING INSIDE THE LOCK. Every row that measures time — a `<suite>.timing` twin, a
     `.benchmark`, a row whose environment arms JAHSHAKA_TIMING_BARS — runs through scripts/gpu-exclusive.sh (the GPU-timing lock, TESTING_GATE §4).
     tests/CMakeLists.txt's configure check proves the list's members are registered through the
     lock; this proves the other direction: nothing that measures is outside the list.
  R2 EVERY TIER ROW IS PRICED (TESTING-CLEANUP-2B item 6; was: every stage-close row). Every row of the MERGE
     and STAGE-CLOSE tiers (every registered row but the target tests) has a line in scripts/gate-times.txt, its
     source named (gate-scope.py --record-times / --price-check): a gate's estimate and the stage-close batch's
     plan are read from it, and an unpriced row was costed as a silent 10 s. RED ON BASE (b0a3b1f3c): 164 of the
     724 tier rows had no price (the gate.* and source.* rows, the layered pools, the new gi/shadow rows).
  R3 NO COPIED TIER. No tracked file other than scripts/gate-scope.py carries a `ctest ... -LE`
     label set: the tiers are printed by `gate-scope.py --merge-tier` / `--stage-close-tier`, and a
     copy goes stale (the old rc script's did: it lacked scale-target and still named benchmark).
  R4 NO RUN_SERIAL WITHOUT ITS VERDICT. A RUN_SERIAL setting in a tests CMakeLists carries ITS
     row's reason: a comment naming RUN_SERIAL in the row's own block (the comments right above
     its registration through the setting), or a comment block naming RUN_SERIAL and the row, or
     the row is one of the GPU-lock rows (the lock IS the verdict, TESTING_GATE §4).
  R5 EVERY ROW HAS A SUBJECT THAT SELECTS IT (the suites audit's S1/S2, closed for good). A
     compiled row is the build graph's (its own sources select it). An app row — a --script suite,
     a harness that spawns the app — must be reachable from a change to what it tests: its script
     calls an API module that has an api file, or its directory is named by an AREA_RULE (the app
     groups do not count: they are catch-alls, and S1's harnesses hid inside them). A lint or
     script row must run a file of the tree (touching it selects the row) or sit in a directory a
     rule names.
  R6 A ROW THAT READS ANOTHER ROW'S RUN REQUIRES IT (TESTING-CLEANUP-2C item 3). A row that DEPENDS on another
     registered row (an inspector of the run's logs, a relaunch that reads the session a run left) also
     FIXTURES_REQUIRES a fixture that row SETS UP, so `-R <row>` / `--solo <row>` runs the parent first in the same
     home — DEPENDS only orders a run that already selected both. RED ON 314770a52:
     threading.{import_soak,newproject_stall}.no_groupless_texture (batch C2: the twin 0/3 solo, "no logs — did the
     run happen?").

  R7 A GPU ROW STATES THE FRAMES IT RENDERS (TESTING-SPEED-A item 7; SPECS/TESTING_SPEED_SPEC.md §2 A8). Every row
     registered by jah_gpu_row (its command opens with scripts/gpu-admit.sh) carries the test property
     JAHSHAKA_TEST_FRAMES — READ FROM THE PROPERTY (ctest --show-only=json-v1), never the CMake text, so a row declared
     by add_test / jah_gpu_exclusive_test + set_tests_properties counts as well. THE RATCHET: the rows without it are
     listed in tests/hygiene/frames_pending.txt (343 jah_gpu_row call sites at TESTING-SPEED-A's land; every Part B
     batch removes its own) — a row without FRAMES that the list does not name is RED (a NEW row is never pending), and
     a listed row that now states its FRAMES (or exists no more) is RED until the list drops it (the ratchet only
     tightens). Never an exclusion: the list shrinks to zero.
  R8 A LANE ROW IS LABELLED (item 6). Every row registered under tests/lanes/<LANE>/ carries the label `lane:<LANE>`
     (jah_lane_row adds it) — gate-scope keeps such a row out of every selection but its own lane's; an unlabelled one
     would leak into everyone's merge.
  R10 ONE FIXTURE CONFIG (FIXTURE-CONFIG-1). No test source under tests/ constructs a
     `jahshaka::engine::EngineConfig` except tests/support/enginetesthelpers.h: every engine fixture takes its config
     from `enginetest::testEngineConfig(logFile, EngineOverrides)`, the one place the harness sets what every fixture
     shares (the BOOT lane's shader cache). Read from the source with comments and strings blanked: an EngineConfig
     temporary (`EngineConfig{}`, `EngineConfig()`, `new EngineConfig`), a variable default- or brace-constructed
     (`EngineConfig cfg;`, `EngineConfig cfg{...}`, `EngineConfig cfg = {...}`), or a write to a field only an
     EngineConfig has (pluginDir, hlmsMediaDir, shaderCacheDir, asyncShaderThreads, optimizeShadowMeshes, logSink,
     headless, logFile, vr.enabled) is RED. A variable or a return INITIALISED from a call (the helper, a suite's own
     wrapper of it) is not a construction. RED ON BASE b96b95c00: 814 sites in 196 files.

Run: testing_rules.py <source-dir> <build-dir>
"""
import glob
import importlib.util
import json
import os
import re
import subprocess
import sys

FAILURES = []


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAILURES.append(what)


LOCK = "gpu-exclusive.sh"
TIER_COPY = re.compile(r"ctest\b[^\n]*\s-LE\s+[\"']?\^?\(")


def measures(n, t):
    """A row that MEASURES: a `.timing` twin, a `.benchmark`, or one whose environment arms the
    timing bars (JAHSHAKA_TIMING_BARS=1, tests/support/timingbars.h). A fixture's setup row
    measures nothing; a soak (open.crash_soak) is quiet-box for its minutes, not a bar."""
    if t.get("fixture_setup"): return False
    return (n.endswith((".timing", ".benchmark")) or ".timing." in n
            or any("JAHSHAKA_TIMING_BARS=1" in e for e in t.get("env", [])))


def r1_bad(inv):
    return sorted(n for n, t in inv.items() if measures(n, t) and not any(c.endswith(LOCK) for c in t["cmd"]))


TARGETS = {"photon-target", "scale-target"}


def r2_bad(inv, priced):
    return sorted(n for n, t in inv.items() if not (t["labels"] & TARGETS) and n not in priced)


def r3_bad(name, text):
    return [f"{name}:{k}" for k, line in enumerate(text.splitlines(), 1)
            if TIER_COPY.search(line) and "gate-scope" not in line]


def r4_bad(name, text, gpu_rows):
    """A RUN_SERIAL setting's reason must belong to ITS row (the second Fable read, (b)): a comment
    naming RUN_SERIAL in the row's own block — the comment block right above the row's first
    registration line (add_test / a jah_*_test helper / set_tests_properties naming it) through
    the setting — or a comment block anywhere in the file that names RUN_SERIAL AND the row."""
    lines, bad = text.split("\n"), []
    comment = lambda l: l.split("#", 1)[1] if "#" in l else ""
    # contiguous comment blocks: (first, last, text)
    blocks, k = [], 0
    while k < len(lines):
        if lines[k].lstrip().startswith("#"):
            j = k
            while j + 1 < len(lines) and lines[j + 1].lstrip().startswith("#"): j += 1
            blocks.append((k, j, "\n".join(lines[k:j + 1]))); k = j + 1
        else:
            k += 1
    for k, line in enumerate(lines):
        code = line.split("#", 1)[0]
        if not re.search(r"\bRUN_SERIAL\s+(TRUE|ON|1|YES)\b", code): continue
        row = None          # the row this setting belongs to: the nearest set_tests_properties(<name>
        for j in range(k, max(-1, k - 12), -1):
            m = re.search(r"set_tests_properties\(\s*([\w.\-]+)", lines[j])
            if m: row = m.group(1); break
        if row in gpu_rows: continue
        reasoned = False
        if row:
            rx = re.compile(r"\b" + re.escape(row) + r"\b")
            first = next((j for j in range(len(lines)) if rx.search(lines[j].split("#", 1)[0])), k)
            start = first
            while start > 0 and lines[start - 1].lstrip().startswith("#"): start -= 1
            reasoned = any("RUN_SERIAL" in comment(l) for l in lines[start:k + 1])
            reasoned = reasoned or any("RUN_SERIAL" in t and rx.search(t) for _, _, t in blocks)
        if not reasoned:
            bad.append("%s:%d (%s)" % (name, k + 1, row))
    return bad


def r5_bad(inv, rule_dirs, api_mods, tracked, script_modules, harness_mods):
    bad = []
    for n, t in sorted(inv.items()):
        if t["kind"] == "compiled": continue       # its own sources select it (the build graph)
        scripts = ({t["script"]} if t["script"] else set()) | set(t.get("arms", {}).values())
        mods = set(harness_mods.get(n, ()))
        for s_ in scripts: mods |= script_modules(s_)
        if t["kind"] == "app":
            ok = bool(mods & api_mods) or t["dir"] in rule_dirs
        else:
            ok = any(os.path.normpath(f) in tracked for f in t["argv_files"] | scripts) or t["dir"] in rule_dirs
        if not ok: bad.append(n)
    return bad


def _list(v):
    return [v] if isinstance(v, str) else list(v or [])


def r6_bad(tests):
    """[`row (DEPENDS parent)`] for every row that DEPENDS on a registered row whose FIXTURES_SETUP it does not require.
    `tests`: ctest's json-v1 `tests` list."""
    props = {t["name"]: {p["name"]: p["value"] for p in t.get("properties", [])} for t in tests}
    bad = []
    for n, P in sorted(props.items()):
        req = set(_list(P.get("FIXTURES_REQUIRED")))
        for d in _list(P.get("DEPENDS")):
            if d in props and not (set(_list(props[d].get("FIXTURES_SETUP"))) & req):
                bad.append("%s (DEPENDS %s)" % (n, d))
    return bad


FRAMES_PROPERTY = "JAHSHAKA_TEST_FRAMES"
ADMIT = "gpu-admit.sh"
EXCLUSIVE = "gpu-exclusive.sh"      # jah_gpu_exclusive_test's rows render too (fix round): the same FRAMES rule


def _props(t):
    return {p["name"]: p["value"] for p in t.get("properties", [])}


def r7_bad(tests, pending):
    """(new, stale): `new` = GPU rows (jah_gpu_row's: the command opens with gpu-admit.sh; jah_gpu_exclusive_test's: with
    gpu-exclusive.sh) without the FRAMES property that frames_pending.txt does not name; `stale` = listed rows that state
    FRAMES now or are not registered."""
    gpu = {t["name"]: _props(t) for t in tests if (t.get("command") or [""])[0].endswith((ADMIT, EXCLUSIVE))}
    lacking = {n for n, P in gpu.items() if FRAMES_PROPERTY not in P}
    names = {t["name"] for t in tests}
    new = sorted(lacking - set(pending))
    stale = sorted(n for n in pending if n not in lacking or n not in names)
    return new, stale


def r7_grown(pending, base_pending):
    """THE RATCHET NEVER LOOSENS (fix round item 6): the names frames_pending.txt lists that its copy at the merge-base
    with d-build does not — a row added to the list is a NEW row without FRAMES (red), whatever the list says. `base_pending`
    None = no copy at the merge-base (the lane that writes the list): nothing to compare."""
    return [] if base_pending is None else sorted(set(pending) - set(base_pending))


def _base_pending(source):
    """frames_pending.txt at `git merge-base HEAD d-build` (None when there is no d-build, no merge-base or no copy)."""
    mb = subprocess.run(["git", "merge-base", "HEAD", "d-build"], cwd=source, capture_output=True, text=True).stdout.strip()
    if not mb:
        return None, None
    p = subprocess.run(["git", "show", f"{mb}:tests/hygiene/frames_pending.txt"], cwd=source, capture_output=True,
                       text=True)
    if p.returncode != 0:
        return mb, None
    return mb, [x for x in (l.split("#", 1)[0].strip() for l in p.stdout.splitlines()) if x]


def r8_bad(tests, frames_of):
    """[row] registered under tests/lanes/<LANE>/ (its backtrace's file) without the label lane:<LANE>."""
    bad = []
    for t in tests:
        f = frames_of(t) or ""
        m = re.search(r"/tests/lanes/([^/]+)/", f)
        if m and ("lane:" + m.group(1)) not in _list(_props(t).get("LABELS")):
            bad.append(t["name"])
    return sorted(bad)


R10_HELPER = os.path.join("tests", "support", "enginetesthelpers.h")
R10_EXTS = (".cpp", ".cc", ".cxx", ".h", ".hpp")
_R10_BLANK = re.compile(r'R"([^(\s]*)\((?:.|\n)*?\)\1"|//[^\n]*|/\*(?:.|\n)*?\*/|"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'')
_R10_TYPE = re.compile(r"(?<![\w])EngineConfig\b")
_R10_TEMP = re.compile(r"\s*[({]")
_R10_VAR = re.compile(r"\s*\w+\s*(;|\{|\[|,|=\s*\{)")
_R10_NEW = re.compile(r"\bnew\s+(?:\w+::)*$")
_R10_FIELD = re.compile(r"(?:\.|->)\s*(pluginDir|hlmsMediaDir|shaderCacheDir|asyncShaderThreads|optimizeShadowMeshes|logSink|"
                        r"headless|logFile|vr\s*\.\s*enabled)\s*=(?!=)")


def _r10_blank(text):
    """The source with every comment and string literal (raw strings too) blanked, newlines kept (line numbers hold)."""
    return _R10_BLANK.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)


def r10_bad(files):
    """[file:line] of every EngineConfig built outside the helper. `files`: {relative path: text}."""
    bad = []
    for name, text in sorted(files.items()):
        if os.path.normpath(name) == R10_HELPER or not name.endswith(R10_EXTS):
            continue
        code = _r10_blank(text)
        hits = set()
        for m in _R10_TYPE.finditer(code):
            rest = code[m.end():m.end() + 200]
            if _R10_TEMP.match(rest) or _R10_VAR.match(rest) or _R10_NEW.search(code[max(0, m.start() - 40):m.start()]):
                hits.add(code.count("\n", 0, m.start()) + 1)
        for m in _R10_FIELD.finditer(code):
            hits.add(code.count("\n", 0, m.start()) + 1)
        bad += ["%s:%d" % (name, n) for n in sorted(hits)]
    return bad


def r10_files(source):
    """{path relative to the source: text} of every C++ source under tests/."""
    out = {}
    for root, _dirs, names in os.walk(os.path.join(source, "tests")):
        for n in names:
            if n.endswith(R10_EXTS):
                p = os.path.join(root, n)
                out[os.path.relpath(p, source)] = open(p, errors="replace").read()
    return out


def the_rules_catch():
    """Each rule, fed one violation and its correction: a lint that cannot fail guards nothing."""
    print("the rules catch (synthetic inputs):")
    row = lambda cmd, **k: dict(dict(cmd=cmd, labels=set(), env=[], fixture_setup=False, kind="app",
                                     script=None, arms={}, argv_files=set(), dir="nowhere"), **k)
    check(r1_bad({"x.timing": row(["/b/x"])}) == ["x.timing"]
          and not r1_bad({"x.timing": row(["/s/scripts/gpu-exclusive.sh", "/b/x"])})
          and r1_bad({"y": row(["/b/y"], env=["JAHSHAKA_TIMING_BARS=1"])}) == ["y"]
          and not r1_bad({"y.timing.fresh_home": row(["/b/y"], fixture_setup=True)}),
          "R1 flags a measuring row outside the lock, not one inside it or a fixture's setup")
    check(r2_bad({"n": row([], labels={"stage-close"})}, set()) == ["n"] and not r2_bad({"n": row([], labels={"stage-close"})}, {"n"})
          and r2_bad({"m": row([], labels={"engine"})}, set()) == ["m"]
          and not r2_bad({"t": row([], labels={"photon-target"})}, set()),
          "R2 flags an unpriced tier row (stage-close or merge), never a target test")
    check(r3_bad("rc.sh", 'ctest -j4 -LE "^(stage-close|photon-target)$"') == ["rc.sh:1"]
          and not r3_bad("rc.sh", 'TIER="$(python3 scripts/gate-scope.py --merge-tier)"'),
          "R3 flags a copied -LE tier, not the command that prints it")
    bare = 'add_test(NAME a.b COMMAND x)\nset_tests_properties(a.b PROPERTIES\n    RUN_SERIAL TRUE)'
    other = '# RUN_SERIAL: x.y compiles shaders\nadd_test(NAME x.y COMMAND x)\n' + bare
    check(r4_bad("t", bare, set()) == ["t:3 (a.b)"] and not r4_bad("t", "# RUN_SERIAL: it compiles shaders\n" + bare, set())
          and not r4_bad("t", bare, {"a.b"}) and r4_bad("t", other, set()) == ["t:5 (a.b)"]
          and not r4_bad("t", "# RUN_SERIAL on a.b: it compiles shaders\n\n\n" + bare, set()),
          "R4 flags a RUN_SERIAL with no verdict of ITS OWN (its block's comment, one naming it, or the lock clears it; "
          "another row's reason does not)")
    check(r5_bad({"h": row(["/b/h"])}, set(), {"avatar"}, set(), lambda s: set(), {}) == ["h"]
          and not r5_bad({"h": row(["/b/h"])}, set(), {"avatar"}, set(), lambda s: set(), {"h": {"avatar"}}),
          "R5 flags an app row nothing selects (a harness's own verbs clear it)")
    T = lambda name, **p: {"name": name, "properties": [{"name": k, "value": v} for k, v in p.items()]}
    G = lambda name, **p: dict(T(name, **p), command=["/s/scripts/gpu-admit.sh", "1", "--label", name + ":engine", "--"])
    new, stale = r7_bad([G("a"), G("b", JAHSHAKA_TEST_FRAMES="60"), T("c")], ["a"])
    new2, stale2 = r7_bad([G("a"), G("n")], ["a"])
    new3, stale3 = r7_bad([G("a", JAHSHAKA_TEST_FRAMES="0")], ["a", "gone"])
    X = lambda name, **p: dict(T(name, **p), command=["/s/scripts/gpu-exclusive.sh", "--label", name, "--"])
    new4, _ = r7_bad([X("e"), X("f", JAHSHAKA_TEST_FRAMES="10")], [])
    check(new4 == ["e"] and r7_grown(["a", "b"], ["a"]) == ["b"] and not r7_grown(["a"], ["a", "c"])
          and not r7_grown(["a", "b"], None),
          "R7 covers jah_gpu_exclusive_test rows too (a gpu-exclusive.sh row without FRAMES is flagged), and the ratchet "
          "never loosens: a name the merge-base copy does not list is red (a shorter list is fine)")
    kept_, drop_ = trim_lines(["# head", "vr.session", "vr.session_undithered", "distortion.engine",
                               "distortion.engine_two"],
                              [G("vr.session", JAHSHAKA_TEST_FRAMES="90"), G("vr.session_undithered"),
                               G("distortion.engine"), G("distortion.engine_two", JAHSHAKA_TEST_FRAMES="4")])
    check(drop_ == {"vr.session", "distortion.engine_two"}
          and kept_ == ["# head", "vr.session_undithered", "distortion.engine"],
          "R7 the trim reads the DECLARED property by EXACT name: vr.session (declares FRAMES) goes, its prefix-sibling "
          "vr.session_undithered (does not) stays; distortion.engine stays, distortion.engine_two goes (%s)" % sorted(drop_))
    check(not new and not stale and new2 == ["n"] and not stale2 and not new3 and stale3 == ["a", "gone"],
          "R7 flags a GPU row without FRAMES that the ratchet does not list (a NEW row), and a listed row that states "
          "FRAMES or is gone (the ratchet tightens); a CPU row needs none")
    where = {"x": "/s/tests/lanes/LANE-A/CMakeLists.txt", "y": "/s/tests/lanes/LANE-A/CMakeLists.txt",
             "z": "/s/tests/gi/CMakeLists.txt"}
    check(r8_bad([T("x", LABELS=["lane:LANE-A"]), T("y", LABELS=["gi"]), T("z", LABELS=["gi"])],
                 lambda t: where[t["name"]]) == ["y"],
          "R8 flags a tests/lanes/<LANE>/ row without its lane:<LANE> label, not a labelled one or a family row")
    check(r6_bad([T("a"), T("a.twin", DEPENDS=["a"], FIXTURES_REQUIRED=["home_a"])]) == ["a.twin (DEPENDS a)"]
          and not r6_bad([T("a", FIXTURES_SETUP=["run_a"]), T("a.twin", DEPENDS=["a"], FIXTURES_REQUIRED=["home_a", "run_a"])])
          and not r6_bad([T("w", FIXTURES_SETUP="home"), T("r", DEPENDS=["w"], FIXTURES_REQUIRED="home")]),
          "R6 flags a row that DEPENDS on a run it does not require (a fixture the run sets up clears it)")
    h = "tests/support/enginetesthelpers.h"
    check(r10_bad({"tests/a/x.cpp": "int f() {\n    EngineConfig cfg;\n    cfg.pluginDir = P;\n}"}) == ["tests/a/x.cpp:2", "tests/a/x.cpp:3"]
          and r10_bad({"tests/a/y.cpp": "auto c = jahshaka::engine::EngineConfig{};"}) == ["tests/a/y.cpp:1"]
          and r10_bad({"tests/a/n.cpp": "auto *c = new jahshaka::engine::EngineConfig;"}) == ["tests/a/n.cpp:1"]
          and r10_bad({"tests/a/z.cpp": "auto c = testEngineConfig(\"l\");\nc.headless = true;\nc.vr.enabled = true;"})
          == ["tests/a/z.cpp:2", "tests/a/z.cpp:3"]
          and not r10_bad({h: "EngineConfig cfg;\ncfg.pluginDir = P;"})
          and not r10_bad({"tests/a/w.cpp": "// EngineConfig cfg;\nconst char *s = \"EngineConfig c; c.logFile = x;\";\n"
                                            "EngineConfig testConfig() { return enginetest::testEngineConfig(\"l\"); }\n"
                                            "EngineConfig cfg = config(\"l\");\nconst auto cfg2 = enginetest::testEngineConfig("
                                            "\"l\", enginetest::EngineOverrides().headless());\nbool b = cfg.headless == true;"}),
          "R10 flags an EngineConfig built outside the helper (a default/brace variable, a temporary, a new, a field write), "
          "not the helper itself, a comment, a string, a wrapper of the helper or a read")


def fresh_home_behaves(source):
    """THE WIPE AFTER, RUN (fix round item 7): tests/support/fresh_home.sh on a toy build tree — green: the home gone, a
    grown cache promoted as the tree's one shared cache; red: the home moved to the evidence; TWELVE concurrent --warm
    runs, each promoting a bigger cache while the others link: every run exits 0, no .shadercache-next.* is left, the
    shared cache is whole (one of the runs' caches, complete)."""
    import shutil
    import tempfile
    fh = os.path.join(os.path.abspath(source), "tests", "support", "fresh_home.sh")
    t = tempfile.mkdtemp(prefix="fresh-home-test-")
    try:
        b = os.path.join(t, "deep", "build")
        os.makedirs(os.path.join(b, "tests", "x"))
        open(os.path.join(b, "CMakeCache.txt"), "w").close()
        td = os.path.join(b, "tests")
        cache = ".local/share/Jahshaka/shadercache"
        home = os.path.join(td, "x", "home-a")
        w = "mkdir -p %s/%s && head -c 4096 /dev/zero > %s/%s/a.bin" % (home, cache, home, cache)
        r = subprocess.run(["sh", fh, "--warm", home, "--", "sh", "-c", w], capture_output=True, text=True)
        gone = lambda h: os.path.isdir(h) and os.listdir(h) == ["run"] and not os.listdir(os.path.join(h, "run"))
        check(r.returncode == 0 and gone(home) and os.path.isfile(os.path.join(td, ".shadercache", "a.bin")),
              "fresh_home: a green run deletes its home (an empty <home>/run kept: a row's cwd) and PROMOTES its grown cache as the shared one (%d %s)"
              % (r.returncode, r.stderr[-120:]))
        r = subprocess.run(["sh", fh, home, "--", "sh", "-c", "touch %s/run/x.png; exit 3" % home], capture_output=True)
        check(r.returncode == 3 and gone(home)
              and os.path.isfile(os.path.join(td, ".evidence", "home-a", "run", "x.png")),
              "fresh_home: a red run keeps its exit code and MOVES its home to the evidence")
        # ROUND 21: A HYGIENE ROW'S TOY TREE LIVES OUTSIDE THE BUILD TREE — gate.tokens_by_needs run with BIN = a scratch
        # "build"; the scratch is walked for CTestTestfile.cmake WHILE it runs (as gate_graph.ctest_inventory walks the
        # real build under parallel ctest): no toy file ever appears there (NONDET-TESTING-RULES-TOY-RACE-1)
        tbn = os.path.join(os.path.abspath(source), "tests", "hygiene", "tokens_by_needs.cmake")
        if os.path.isfile(tbn):
            sb = os.path.join(td, "walked-build")
            os.makedirs(sb)
            env_ = dict(os.environ, TMPDIR=os.path.join(td, "toy-tmp"))
            os.makedirs(env_["TMPDIR"])
            pr = subprocess.Popen(["cmake", "-DSRC=" + os.path.join(os.path.abspath(source), "tests"), "-DBIN=" + sb,
                                   "-P", tbn], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env_, text=True)
            seen = set()
            while pr.poll() is None:
                for dp, _, fs in os.walk(sb):
                    if "CTestTestfile.cmake" in fs:
                        seen.add(os.path.relpath(dp, sb))
            outp = pr.stdout.read()
            check(pr.returncode == 0 and not seen and not os.listdir(env_["TMPDIR"]),
                  "round 21: gate.tokens_by_needs builds its toy OUTSIDE the walked build tree (TMPDIR) and removes it: "
                  "a walk of the build during its run sees no toy CTestTestfile (rc %s, seen %s%s)"
                  % (pr.returncode, sorted(seen)[:3], "" if pr.returncode == 0 else "; " + outp[-200:]))
        # ROUND 14 ADDENDUM 2: A FIXTURE'S SETUP keeps what its THEN wrote (the consumer reads it); the CONSUMER's green exit
        # (--after) wipes the home
        fx = os.path.join(td, "x", "e2e-home-fixture")
        r = subprocess.run(["sh", fh, "--setup", fx, "--", "sh", "-c", "echo standin > %s/lod_standin.obj" % fx],
                           capture_output=True, text=True)
        kept = os.path.isfile(os.path.join(fx, "lod_standin.obj"))
        c = subprocess.run(["sh", "-c", "test -f %s/lod_standin.obj" % fx])
        a = subprocess.run(["sh", fh, "--after", "0", fx], capture_output=True, text=True)
        check(r.returncode == 0 and kept and c.returncode == 0 and a.returncode == 0
              and not os.path.exists(os.path.join(fx, "lod_standin.obj")),
              "fresh_home: a FIXTURE SETUP (--setup) keeps the file its THEN wrote for the consumer; the consumer's green "
              "exit (--after 0) wipes the home (kept %s)" % kept)
        # ROUND 5: A ROW'S WORKING DIRECTORY SURVIVES THE WIPE — a toy pool whose ctest WORKING_DIRECTORY is <home>/run
        ph = os.path.join(td, "x", "pool-home-toy")
        os.makedirs(os.path.join(ph, "run"))
        r = subprocess.run(["sh", fh, ph, "--", "sh", "-c", "pwd > %s/run/where; touch %s/run/lib.db" % (ph, ph)],
                           cwd=os.path.join(ph, "run"), capture_output=True, text=True)
        left = sorted(os.listdir(ph)) if os.path.isdir(ph) else None
        check(r.returncode == 0 and left == ["run"] and os.listdir(os.path.join(ph, "run")) == [],
              "fresh_home: a green pool's home is wiped AROUND its working directory — <home>/run re-created empty, the "
              "next ctest run can change into it (left %s)" % left)
        r = subprocess.run(["sh", fh, ph, "--", "sh", "-c", "exit 4"], cwd=os.path.join(ph, "run"), capture_output=True)
        check(r.returncode == 4 and os.path.isdir(os.path.join(ph, "run"))
              and os.path.isdir(os.path.join(td, ".evidence", "pool-home-toy")),
              "fresh_home: ...a red pool's home goes to the evidence and its working directory is re-created too")
        procs = []
        for k in range(12):
            h = os.path.join(td, "x", "home-c%d" % k)
            cmd = ("test -f %s/%s/a.bin || exit 7; mkdir -p %s/%s/d%d && head -c %d /dev/zero > %s/%s/d%d/f.bin"
                   % (h, cache, h, cache, k, 8192 * (k + 2), h, cache, k))
            procs.append(subprocess.Popen(["sh", fh, "--warm", h, "--", "sh", "-c", cmd], stdout=subprocess.DEVNULL,
                                          stderr=subprocess.PIPE, text=True))
        rcs = [p_.wait() for p_ in procs]
        left = [n for n in os.listdir(td) if n.startswith(".shadercache-next")]
        whole = os.path.isfile(os.path.join(td, ".shadercache", "a.bin"))
        check(rcs == [0] * 12 and not left and whole,
              "fresh_home: 12 concurrent --warm runs promote and link under the cache lock — every run green (it saw the "
              "shared cache whole), no .shadercache-next.* orphan, the shared cache whole (%s, left %s)" % (rcs, left))
    finally:
        shutil.rmtree(t, ignore_errors=True)


def main(source, build):
    spec = importlib.util.spec_from_file_location("gate_scope", os.path.join(source, "scripts", "gate-scope.py"))
    gs = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gs)
    the_rules_catch()
    fresh_home_behaves(source)
    print("the tree:")
    inv = gs.load_inventory(build)
    graph = gs.gate_graph.NinjaGraph.load(build)
    if graph is not None and graph.deps_objects == 0:
        graph = None
    gs.classify_rows(inv, graph, build)

    bad = r1_bad(inv)
    check(not bad, "R1 timing inside the lock: every measuring row runs through %s %s" % (LOCK, bad[:8]))

    check(gs.TARGET_LABELS == TARGETS, "R2's target labels are the selector's (%s)" % sorted(gs.TARGET_LABELS))
    priced = set(gs.parse_times(os.path.join(source, "scripts", "gate-times.txt")))
    bad = r2_bad(inv, priced)
    check(not bad, "R2 every tier row is priced in scripts/gate-times.txt (%d unpriced: %s)" % (len(bad), bad[:8]))

    files = subprocess.run(["git", "ls-files"], cwd=source, capture_output=True, text=True).stdout.split()
    bad = []
    for f in files:
        if f in ("scripts/gate-scope.py", "tests/hygiene/testing_rules.py") \
                or f.startswith(("irisgl/thirdparty", "thirdparty/")) \
                or not f.endswith((".sh", ".py", ".md", ".txt", ".cmake", ".yml", ".yaml", ".json")):
            continue
        try:
            bad += r3_bad(f, open(os.path.join(source, f), errors="replace").read())
        except (OSError, IsADirectoryError):
            continue
    check(not bad, "R3 no copied tier: no `ctest ... -LE (...)` outside scripts/gate-scope.py %s" % bad[:8])

    gpu_rows = {n for n, t in inv.items() if any(c.endswith(LOCK) for c in t["cmd"])}
    bad = []
    for cm in glob.glob(os.path.join(source, "tests", "**", "CMakeLists.txt"), recursive=True):
        bad += r4_bad(os.path.relpath(cm, source), open(cm, errors="replace").read(), gpu_rows)
    check(not bad, "R4 no RUN_SERIAL without its verdict comment (or the GPU lock) %s" % bad[:8])

    rule_dirs = set()
    for pat_, dirs, modules in gs.AREA_RULES:
        rule_dirs |= {d for d in dirs if not d.startswith("*")}
    # THE SUBSYSTEM TABLE NAMES DIRECTORIES TOO (TESTING-SPEED-A item 4): the engine lines, OgreEngine.cpp's functions,
    # the core TUs' keywords and the fork's components select their tokens' rows
    for toks in ([t for _, t, _w in getattr(gs, "ENGINE_SUBSYSTEMS", [])] + list(getattr(gs, "OGRE_ENGINE_FUNCTIONS", {}).values())
                 + [t for m_ in getattr(gs, "CORE_FUNCTION_MAPS", {}).values() for t in m_.values()]
                 + [t for _, t in getattr(gs, "FORK_COMPONENTS", []) if t != "none"]):
        rule_dirs |= {d for d in toks if not d.startswith("*")}
    # a LANE'S OWN ROW (label lane:<LANE>) is selected by its own directory, tests/lanes/<LANE>/ (item 6)
    inv = {n: t for n, t in inv.items() if not any(l.startswith("lane:") for l in t["labels"])}
    tracked = {os.path.normpath(os.path.join(source, f)) for f in files}
    harness = gs.Selection(inv, graph, os.path.join(build, "bin", "Jahshaka"), gs.Revs(None), 4).mods
    bad = r5_bad(inv, rule_dirs, gs.api_modules(), tracked, gs.script_modules, harness)
    check(not bad, "R5 every app/lint row has a subject that selects it (%d without: %s)" % (len(bad), bad[:10]))

    # FIX ROUND 3 F3: `fixture_setup` means a home WIPE; a real run that also sets up a fixture is `run_fixture` and stays
    # a gating row (R1, the estimate, max_rows). RED ON 9eb5343e2: the two threading parents read as wipes.
    if not hasattr(gs, "_is_wipe"):
        gs._is_wipe = lambda cmd: False                  # a base before F3: the checks below read red, not a crash
    check(gs._is_wipe(["sh", "/s/tests/support/fresh_home.sh", "--warm", "/h"]) and gs._is_wipe(["sh", "-c", "rm -rf /h"])
          and not gs._is_wipe(["/s/scripts/gpu-admit.sh", "2", "--", "/b/bin/Jahshaka", "--script", "x.js"]),
          "a fixture setup is a WIPE only when its command is fresh_home.sh / freshhome.cmake / sh -c rm -rf")
    runfx = [n for n in ("threading.newproject_stall", "threading.import_soak") if n in inv]
    check(runfx and all(inv[n].get("run_fixture") and not inv[n].get("fixture_setup") for n in runfx)
          and not [n for n, t in inv.items() if t.get("fixture_setup") and t.get("app")],
          "the threading parents are RUN fixtures (gating rows), and no app row reads as a wipe (%s)" % runfx)
    raw, rc = gs.gate_graph.ctest_inventory(build)
    bad = r6_bad(json.loads(raw or "{}").get("tests", [])) if rc == 0 else ["ctest --show-only failed (%d)" % rc]
    check(not bad, "R6 a row that DEPENDS on another row's run requires a fixture that run sets up %s" % bad[:8])
    # ROUND 14 ADDENDUM 2: THE WIPE AFTER is the home's LAST use only — never on a row that sets up a fixture (its files are
    # another row's input: threading.mode's shot, a parent run's logs) and never on a home wipe's own setup row
    def _props(t):
        return {p.get("name"): p.get("value") for p in t.get("properties", []) or []}
    wipe_bad = []
    for t in (json.loads(raw or "{}").get("tests", []) if rc == 0 else []):
        pr = _props(t)
        env = " ".join(str(x) for x in (pr.get("ENVIRONMENT_MODIFICATION") or []))
        if "JAH_WIPE_AFTER" in env and pr.get("FIXTURES_SETUP"):
            wipe_bad.append(t.get("name"))
    check(not wipe_bad, "R9 no row that SETS UP a fixture wipes its home on exit (JAH_WIPE_AFTER): its files are another "
          "row's input %s" % wipe_bad[:8])

    tests = json.loads(raw or "{}").get("tests", []) if rc == 0 else []
    pend_path = os.path.join(source, "tests", "hygiene", "frames_pending.txt")
    pending = [x.split("#", 1)[0].strip() for x in open(pend_path, errors="replace")] if os.path.isfile(pend_path) else None
    pending = [x for x in (pending or []) if x] if pending is not None else None
    if pending is None:
        check(False, "R7 tests/hygiene/frames_pending.txt exists (the FRAMES ratchet)")
    else:
        new, stale = r7_bad(tests, pending)
        check(not new and not stale, "R7 every GPU row (jah_gpu_row, jah_gpu_exclusive_test) states its FRAMES (%d "
              "pending in the ratchet; NEW without FRAMES: %s; listed but stating FRAMES or gone — drop them from "
              "frames_pending.txt: %s)" % (len(pending), new[:8], stale[:8]))
        mb, base_pending = _base_pending(source)
        grown = r7_grown(pending, base_pending)
        check(not grown, "R7 the ratchet never loosens: frames_pending.txt lists no name its copy at the merge-base with "
              "d-build (%s) does not (%s)" % ((mb or "none")[:9] if base_pending is not None else
                                             "no copy there: the lane that writes it", grown[:8]))
    j_ = json.loads(raw or "{}") if rc == 0 else {}
    bt = j_.get("backtraceGraph") or {}

    def file_of(t):
        n_ = (bt.get("nodes") or [])[t["backtrace"]] if "backtrace" in t else None
        while n_ is not None:
            f_ = (bt.get("files") or [])[n_["file"]] if n_.get("file") is not None else ""
            if "/tests/lanes/" in f_:
                return f_
            n_ = (bt.get("nodes") or [])[n_["parent"]] if "parent" in n_ else None
        return ""
    bad = r8_bad(tests, file_of)
    check(not bad, "R8 every tests/lanes/<LANE>/ row carries its lane:<LANE> label %s" % bad[:8])

    bad = r10_bad(r10_files(source))
    check(not bad, "R10 one fixture config: no test source builds an EngineConfig outside %s (%d sites: %s)"
          % (R10_HELPER, len(bad), bad[:8]))

    if FAILURES:
        print("source.testing_rules: FAILED (%d)" % len(FAILURES))
        return 1
    print("source.testing_rules: PASSED")
    return 0


def trim_lines(lines, tests):
    """(kept lines, dropped names): frames_pending.txt's lines minus every EXACT name r7_bad calls stale (declares
    FRAMES now, or is no row)."""
    names = [l.split("#", 1)[0].strip() for l in lines]
    _new, stale = r7_bad(tests, [n for n in names if n])
    drop = set(stale)
    return [l for l, n in zip(lines, names) if not (n and n in drop)], drop


def trim_frames_pending(source, build):
    """THE RATCHET TRIMMED BY WHAT IS DECLARED (the lead, B4's read): frames_pending.txt loses every name whose row now
    declares JAHSHAKA_TEST_FRAMES at the tip (ctest --show-only=json-v1) or is no longer registered — EXACT names, never a
    hand list or a prefix match (B4's hand list missed distortion.engine, engine.particles_pfx2, vr.session and vr.warmup:
    each a prefix of a sibling). Comments stay; nothing is ever added. Prints what it dropped; returns 0."""
    spec = importlib.util.spec_from_file_location("gs", os.path.join(source, "scripts", "gate-scope.py"))
    gs = importlib.util.module_from_spec(spec); spec.loader.exec_module(gs)
    raw, rc = gs.gate_graph.ctest_inventory(build)
    if rc != 0:
        print("trim: ctest --show-only failed (%d) — nothing written" % rc); return 1
    tests = json.loads(raw or "{}").get("tests", [])
    path = os.path.join(source, "tests", "hygiene", "frames_pending.txt")
    kept, drop = trim_lines(open(path, errors="replace").read().splitlines(), tests)
    open(path, "w").write("\n".join(kept) + "\n")
    print("trim: dropped %d name(s) that declare FRAMES or are gone: %s" % (len(drop), " ".join(sorted(drop))))
    return 0


if __name__ == "__main__":
    if sys.argv[1:2] == ["--trim-frames-pending"]:
        sys.exit(trim_frames_pending(os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3])))
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
