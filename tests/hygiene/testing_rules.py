#!/usr/bin/env python3
"""source.testing_rules — THE TESTING RULES THAT CAN BE READ FROM THE TREE (MODULAR-GATE-1; T6;
PHOTON_ATOM_CONTRACT §7b "the rules are source.* lint suites").

A rule a lead enforces by reading diffs does not scale; a rule a lint enforces does. The rules that are facts of the
tree and the build's registration, each with its name in the failure line:

  R1 A MEASUREMENT IS LABELLED AND HOLDS THE CARD. Every row that measures time — a `<suite>.timing` twin, a
     `.benchmark`, a row whose environment arms JAHSHAKA_TIMING_BARS — runs through scripts/gpu-exclusive.sh and carries
     the label `measure` (it runs at the sprint's ship only, alone on the card); tests/CMakeLists.txt's configure check
     proves the list's members are registered through the lock, this the other direction.
  R2 EVERY GUARD IS PRICED. Every row but a `measure` or target row has a line in scripts/gate-times.txt (its seconds):
     source.row_budget holds each against its budget, and an unpriced row cannot be held.
  R4 NO RUN_SERIAL WITHOUT ITS VERDICT. A RUN_SERIAL setting in a tests CMakeLists carries ITS row's reason: a comment
     naming RUN_SERIAL in the row's own block, or a comment block naming RUN_SERIAL and the row, or the row is one of
     the lock's rows.
  R5 EVERY ROW HAS A SUBJECT THAT SELECTS IT. A compiled row is the build graph's. An app row must be reachable from a
     change to what it tests: its script calls an API module that has an api file, or its directory is named by an
     AREA_RULE. A lint or script row must run a file of the tree or sit in a directory a rule names.
  R6 A ROW THAT READS ANOTHER ROW'S RUN REQUIRES IT: a row that DEPENDS on another registered row also
     FIXTURES_REQUIRES a fixture that row SETS UP, so `-R <row>` runs the parent first in the same home.
  R7 A GPU ROW STATES THE FRAMES IT RENDERS: every row on the card's slots (JAH_CLASS) carries JAHSHAKA_TEST_FRAMES
     (read from the property); the rows without it are listed in tests/hygiene/frames_pending.txt — a row without
     FRAMES the list does not name is RED, and a listed row that states FRAMES (or is gone) is RED until the list drops
     it (the ratchet only tightens; `--trim-frames-pending` drops them).
  R10 ONE FIXTURE CONFIG (FIXTURE-CONFIG-1). No test source under tests/ constructs a
     `jahshaka::engine::EngineConfig` except tests/support/enginetesthelpers.h: every engine fixture takes its config
     from `enginetest::testEngineConfig(logFile, EngineOverrides)`, the one place the harness sets what every fixture
     shares (the BOOT lane's shader cache). Read from the source with comments and strings blanked: an EngineConfig
     temporary (`EngineConfig{}`, `EngineConfig()`, `new EngineConfig`), a variable default- or brace-constructed
     (`EngineConfig cfg;`, `EngineConfig cfg{...}`, `EngineConfig cfg = {...}`), or a write to a field only an
     EngineConfig has (pluginDir, hlmsMediaDir, shaderCacheDir, asyncShaderThreads, optimizeShadowMeshes, logSink,
     headless, logFile, vr.enabled) is RED. A variable or a return INITIALISED from a call (the helper, a suite's own
     wrapper of it) is not a construction. RED ON BASE b96b95c00: 814 sites in 196 files.
  R11 THE ENGINE'S RECORD IS THE FIXTURES' ALONE (ENGINE-UPGRADE-U3 stage 2). No file under src/ or irisgl/mirror
     includes jahshaka/engine/EngineInspect.h — the test door onto the engine's own record of what a host set; the host
     keeps what it set.
And tests/support/fresh_home.sh behaves (a green run wipes, a red run keeps its evidence, concurrent warm runs).

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
MEASURE = "measure"


def measures(n, t):
    """A row that MEASURES: a `.timing` twin, a `.benchmark`, or one whose environment arms the
    timing bars (JAHSHAKA_TIMING_BARS=1, tests/support/timingbars.h). A fixture's setup row
    measures nothing; a soak (open.crash_soak) is a `measure` row for its minutes, not a bar."""
    if t.get("fixture_setup"): return False
    return (n.endswith((".timing", ".benchmark")) or ".timing." in n
            or any("JAHSHAKA_TIMING_BARS=1" in e for e in t.get("env", [])))


def r1_bad(inv):
    return sorted(n for n, t in inv.items() if measures(n, t)
                  and not (any(c.endswith(LOCK) for c in t["cmd"]) and MEASURE in t["labels"]))




def r2_bad(inv, priced):
    return sorted(n for n, t in inv.items() if MEASURE not in t["labels"] and n not in priced)


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


def _props(t):
    return {p["name"]: p["value"] for p in t.get("properties", [])}


def r7_bad(tests, pending):
    """(new, stale): `new` = GPU rows (jah_gpu_row's and jah_gpu_exclusive_test's: JAH_CLASS set, not a pool) without the
    FRAMES property that frames_pending.txt does not name; `stale` = listed rows that state FRAMES now or are not
    registered."""
    gpu = {t["name"]: _props(t) for t in tests
           if _props(t).get("JAH_CLASS") and not t["name"].startswith("pool.")}
    lacking = {n for n, P in gpu.items() if FRAMES_PROPERTY not in P}
    names = {t["name"] for t in tests}
    new = sorted(lacking - set(pending))
    stale = sorted(n for n in pending if n not in lacking or n not in names)
    return new, stale


def r7_grown(pending, base_pending):
    """THE RATCHET NEVER LOOSENS (fix round item 6): the names frames_pending.txt lists that its copy at the merge-base
    with main does not — a row added to the list is a NEW row without FRAMES (red), whatever the list says. `base_pending`
    None = no copy at the merge-base (the lane that writes the list): nothing to compare."""
    return [] if base_pending is None else sorted(set(pending) - set(base_pending))


def _base_pending(source):
    """frames_pending.txt at `git merge-base HEAD ogre` (main; None when there is no merge-base or no copy)."""
    mb = subprocess.run(["git", "merge-base", "HEAD", "ogre"], cwd=source, capture_output=True, text=True).stdout.strip()
    if not mb:
        return None, None
    p = subprocess.run(["git", "show", f"{mb}:tests/hygiene/frames_pending.txt"], cwd=source, capture_output=True,
                       text=True)
    if p.returncode != 0:
        return mb, None
    return mb, [x for x in (l.split("#", 1)[0].strip() for l in p.stdout.splitlines()) if x]


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
          and r1_bad({"x.timing": row(["/s/scripts/gpu-exclusive.sh", "/b/x"])}) == ["x.timing"]
          and not r1_bad({"x.timing": row(["/s/scripts/gpu-exclusive.sh", "/b/x"], labels={"measure"})})
          and r1_bad({"y": row(["/b/y"], env=["JAHSHAKA_TIMING_BARS=1"])}) == ["y"]
          and not r1_bad({"y.timing.fresh_home": row(["/b/y"], fixture_setup=True)}),
          "R1 flags a measuring row outside the lock or unlabelled, not a labelled one inside it or a fixture's setup")
    check(r2_bad({"m": row([], labels={"engine"})}, set()) == ["m"] and not r2_bad({"m": row([], labels={"engine"})}, {"m"})
          and not r2_bad({"q": row([], labels={"measure"})}, set()),
          "R2 flags an unpriced guard, never a measure row")
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
    G = lambda name, **p: dict(T(name, JAH_CLASS="engine", **p), command=["/b/test_x"])
    new, stale = r7_bad([G("a"), G("b", JAHSHAKA_TEST_FRAMES="60"), T("c")], ["a"])
    new2, stale2 = r7_bad([G("a"), G("n")], ["a"])
    new3, stale3 = r7_bad([G("a", JAHSHAKA_TEST_FRAMES="0")], ["a", "gone"])
    X = lambda name, **p: dict(T(name, JAH_CLASS="app", **p), command=["/s/scripts/gpu-exclusive.sh", "--label", name, "--"])
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
    check(r6_bad([T("a"), T("a.twin", DEPENDS=["a"], FIXTURES_REQUIRED=["home_a"])]) == ["a.twin (DEPENDS a)"]
          and not r6_bad([T("a", FIXTURES_SETUP=["run_a"]), T("a.twin", DEPENDS=["a"], FIXTURES_REQUIRED=["home_a", "run_a"])])
          and not r6_bad([T("w", FIXTURES_SETUP="home"), T("r", DEPENDS=["w"], FIXTURES_REQUIRED="home")]),
          "R6 flags a row that DEPENDS on a run it does not require (a fixture the run sets up clears it)")


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
        # A KEYED CACHE (ENGINE-UPGRADE-1): --cache <key> promotes into and links from .shadercache-<key>, never the
        # default one; a bad key is refused
        hk = os.path.join(td, "x", "home-k")
        wk = "mkdir -p %s/%s && head -c 8192 /dev/zero > %s/%s/k.bin" % (hk, cache, hk, cache)
        r = subprocess.run(["sh", fh, "--warm", "--cache", "engine", hk, "--", "sh", "-c", wk], capture_output=True, text=True)
        rk = subprocess.run(["sh", fh, "--warm", "--cache", "engine", hk, "--", "sh", "-c",
                             "test -f %s/%s/k.bin && test ! -f %s/%s/a.bin" % (hk, cache, hk, cache)], capture_output=True)
        bad = subprocess.run(["sh", fh, "--warm", "--cache", "../x", hk], capture_output=True)
        check(r.returncode == 0 and os.path.isfile(os.path.join(td, ".shadercache-engine", "k.bin"))
              and not os.path.isfile(os.path.join(td, ".shadercache", "k.bin")) and rk.returncode == 0
              and bad.returncode == 2,
              "fresh_home: --cache <key> keeps its own shared cache (.shadercache-<key>: promoted there, linked from there, "
              "the default one untouched) and refuses a key outside [a-z0-9_] (%d %d %d)" % (r.returncode, rk.returncode, bad.returncode))
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

    priced = {l.split()[0] for l in open(os.path.join(source, "scripts", "gate-times.txt"), errors="replace")
              if l.strip() and not l.startswith("#")}
    bad = r2_bad(inv, priced)
    check(not bad, "R2 every guard is priced in scripts/gate-times.txt (%d unpriced: %s)" % (len(bad), bad[:8]))

    files = subprocess.run(["git", "ls-files"], cwd=source, capture_output=True, text=True).stdout.split()
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
    tracked = {os.path.normpath(os.path.join(source, f)) for f in files}
    harness = gs.Selection(inv, graph, os.path.join(build, "bin", "Jahshaka"), gs.Revs(None)).mods
    bad = r5_bad(inv, rule_dirs, gs.api_modules(), tracked, gs.script_modules, harness)
    check(not bad, "R5 every app/lint row has a subject that selects it (%d without: %s)" % (len(bad), bad[:10]))

    # FIX ROUND 3 F3: `fixture_setup` means a home WIPE; a real run that also sets up a fixture is `run_fixture` and stays
    # a gating row (R1, the estimate, max_rows). RED ON 9eb5343e2: the two threading parents read as wipes.
    if not hasattr(gs, "_is_wipe"):
        gs._is_wipe = lambda cmd: False                  # a base before F3: the checks below read red, not a crash
    check(gs._is_wipe(["sh", "/s/tests/support/fresh_home.sh", "--warm", "/h"]) and gs._is_wipe(["sh", "-c", "rm -rf /h"])
          and not gs._is_wipe(["/b/bin/Jahshaka", "--script", "x.js"]),
          "a fixture setup is a WIPE only when its command is fresh_home.sh / freshhome.cmake / sh -c rm -rf")
    runfx = [n for n in ("threading.newproject_stall", "threading.import_soak") if n in inv]
    check(runfx and all(inv[n].get("run_fixture") and not inv[n].get("fixture_setup") for n in runfx)
          and not [n for n, t in inv.items() if t.get("fixture_setup") and t.get("app")],
          "the threading parents are RUN fixtures (gating rows), and no app row reads as a wipe (%s)" % runfx)
    raw, rc = gs.gate_graph.ctest_inventory(build)
    bad = r6_bad(json.loads(raw or "{}").get("tests", [])) if rc == 0 else ["ctest --show-only failed (%d)" % rc]
    check(not bad, "R6 a row that DEPENDS on another row's run requires a fixture that run sets up %s" % bad[:8])
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
              "main (%s) does not (%s)" % ((mb or "none")[:9] if base_pending is not None else
                                             "no copy there: the lane that writes it", grown[:8]))
    bad = r10_bad(r10_files(source))
    check(not bad, "R10 one fixture config: no test source builds an EngineConfig outside %s (%d sites: %s)"
          % (R10_HELPER, len(bad), bad[:8]))
    inspect = []
    for root in ("src", os.path.join("irisgl", "mirror")):
        for dirpath, _dirs, names in os.walk(os.path.join(source, root)):
            for n in names:
                if n.endswith((".cpp", ".h")):
                    f = os.path.join(dirpath, n)
                    if "EngineInspect.h" in open(f, errors="replace").read():
                        inspect.append(os.path.relpath(f, source))
    check(not inspect, "R11 no host source includes jahshaka/engine/EngineInspect.h (the fixtures' door): %s" % inspect[:8])
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
