#!/usr/bin/env python3
"""gate.serve_red (URGENT #54) - A RED AT THE SAME INPUT IS SERVED-RED, NEVER RE-RUN: ci_gate_check.py serve's decision
(classify / red_stands) on synthetic records. Re-running a red at the same input is a quiet retry that could flip it green
by chance, so a red RESULT (FAIL, TIMEOUT, CRASH) recorded at THIS tree with the same build configuration, declaration and
data is served-red (reported with its record, neither served nor run); a red of an ANCESTOR's tree runs (the diff may have
fixed it); a PASS serves as before; NOTRUN / NOADMIT are not results - except a NOTRUN BEHIND a row it needs (URGENT #123):
served-red while that row is red at the same commit, run once it is not or at a new tree. No build, no git, no display.
usage: gate_serve_red.py <source dir>"""
import os
import sys

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "..")
sys.path.insert(0, os.path.join(src, "scripts"))
import ci_gate_check as cgc  # noqa: E402


class Reach:
    """A Reach of the test's making: whether a record's tree is this one, and what A..C reaches."""
    def __init__(self, same, reaches=(), fork=()):
        self.same, self.reaches, self.fork, self.tip = same, set(reaches), set(fork), "c" * 40

    def same_tree(self, a):
        return self.same

    def of(self, a):
        return self.reaches


def rec(verdict, cfg="cfg", decl="d", data="h"):
    return {"verdict": verdict, "failLine": "FAIL: the law", "tip": {"studio": "a" * 40}, "config": {"hash": cfg},
            "declaration": {"hash": decl}, "data_hash": data, "run": "run-1", "seconds": 3.0, "tier": "lane"}


FACT = {"declaration": {"hash": "d"}, "data_hash": "h", "labels": ["gi"]}
CFG = {"hash": "cfg", "build_type": "Debug"}
fails = []


def want(name, got, kind, why_has=None):
    ok = got[0] == kind and (why_has is None or (got[1] or "").find(why_has) >= 0)
    print(("ok   " if ok else "FAIL ") + "%s: %s%s" % (name, got[0], (" (%s)" % got[1]) if got[1] else ""))
    if not ok:
        fails.append(name)


C = lambda r, reach, fact=FACT, cfg=CFG: cgc.classify(r, fact, "gi.row", reach, "fk", "fk", cfg)
for v in ("FAIL", "TIMEOUT", "CRASH"):
    want("a %s at THIS tree, the same configuration, declaration and data: SERVED-RED" % v, C(rec(v), Reach(True)), "red")
want("a FAIL at an ANCESTOR's tree runs (the diff may have fixed it)", C(rec("FAIL"), Reach(False)), "run", "ancestor")
want("a FAIL at this tree, its build configuration changed: runs", C(rec("FAIL", cfg="other"), Reach(True)), "run",
     "configuration")
want("a FAIL at this tree, its declaration changed: runs", C(rec("FAIL", decl="d2"), Reach(True)), "run", "declaration")
want("a FAIL at this tree, its data changed: runs", C(rec("FAIL", data="h2"), Reach(True)), "run", "data")
want("a FAIL at this tree of a row out of the inventory: runs", C(rec("FAIL"), Reach(True), fact=None), "run")
want("a PASS at this tree serves as before", C(rec("PASS"), Reach(True)), "served")
want("a PASS at an ancestor the range does not reach serves as before", C(rec("PASS"), Reach(False)), "served")
want("a PASS at an ancestor the range reaches runs", C(rec("PASS"), Reach(False, reaches={"gi.row"})), "run", "reaches")
for v in ("NOTRUN", "NOADMIT"):
    want("a %s is not a result: never served-red (it runs)" % v, C(rec(v), Reach(True)), "run")
# NOT RUN BEHIND ITS FIXTURE (URGENT #123): a row ctest did not run because a row it needs is red stands as a red while
# that row is red at the same commit - its part is never listed to run for nothing - and runs once that row is not
behind = dict(rec("NOTRUN"), behind=["gi.fixture"], failLine="NOT RUN behind gi.fixture")
B = lambda r, reach, at: cgc.classify(r, FACT, "gi.row", reach, "fk", "fk", CFG, at)
want("NOT RUN behind a fixture red at this tree: SERVED-RED", B(behind, Reach(True), {"gi.fixture": [rec("FAIL")]}), "red")
want("NOT RUN behind a fixture that TIMED OUT at this tree: SERVED-RED", B(behind, Reach(True),
     {"gi.fixture": [rec("TIMEOUT")]}), "red")
want("NOT RUN behind a fixture re-run GREEN at this tree since: runs", B(behind, Reach(True),
     {"gi.fixture": [rec("FAIL"), rec("PASS")]}), "run", "no longer red")
want("NOT RUN behind a fixture with no record at this tree: runs", B(behind, Reach(True), {}), "run", "no longer red")
want("NOT RUN behind, recorded at an ANCESTOR's tree: runs (a new tip starts clean)", B(behind, Reach(False),
     {"gi.fixture": [rec("FAIL")]}), "run", "ancestor")
want("NOT RUN behind, its declaration changed: runs", B(dict(behind, declaration={"hash": "d2"}), Reach(True),
     {"gi.fixture": [rec("FAIL")]}), "run", "declaration")
# A PATH THE ROW REMOVES OR MAKES IS NEVER ITS DATA (URGENT #137b): the fixture row `fresh_home.sh --setup --rm <build>/x/out.jbundle
# <home>` - its consumers write out.jbundle - keeps ONE data hash whatever that file holds (it was hashed as the row's data, so the
# row was never served and `spec.sh test --all` stopped NO PROGRESS); a real data argument still moves the hash
import tempfile  # noqa: E402
import gate_graph as gg  # noqa: E402
t = tempfile.mkdtemp(prefix="serve-red-data-")
root, build = os.path.join(t, "src"), os.path.join(t, "src", "build-linux")
os.makedirs(os.path.join(build, "x")); os.makedirs(os.path.join(root, "tests", "x"))
out, data = os.path.join(build, "x", "out.jbundle"), os.path.join(root, "tests", "x", "case.js")
open(data, "w").write("one")
roots = {"root": root, "build": build, "cur_root": root, "cur_build": build}
row = {"name": "x.fresh_home", "properties": [],
       "command": ["/bin/sh", os.path.join(root, "tests", "support", "fresh_home.sh"), "--setup", "--rm", out, data,
                   os.path.join(build, "x", "home")]}
hashes = []
for content in (None, "first export", "second export"):
    if content is not None:
        open(out, "w").write(content)
    d = gg.data_set(row, roots, {})
    hashes.append((d["hash"], sorted(d["files"])))
if not (len({h for h, _ in hashes}) == 1 and all(not any("out.jbundle" in f for f in fs) for _, fs in hashes)
        and any("case.js" in f for f in hashes[0][1])):
    fails.append("a path after --rm is hashed as the row's data: %s" % hashes)
else:
    print("ok: a path the row removes (--rm) is never its data: one hash whatever the file holds; its real data argument kept")
open(data, "w").write("two")
if gg.data_set(row, roots, {})["hash"] == hashes[0][0]:
    fails.append("a real data argument's change does not move the row's data hash")
# A RECORD SERVES ANOTHER TREE AT EQUAL INPUT (URGENT #154): #129's guard put the tree's own path into every build's options
# (JAH_CONFIGURED_SOURCE_DIR) and configure_file writes it into a row's script, so a lane's records served no tree but its own -
# a landing at the very tip the lane had proved planned 329 rows. Two toy trees, one commit: the configuration and the data equal.
import shutil  # noqa: E402
for tree in ("lane", "parent"):
    r_, b_ = os.path.join(t, tree), os.path.join(t, tree, "build-linux")
    os.makedirs(os.path.join(b_, "tests", "s")); os.makedirs(os.path.join(r_, "scenes"))
    open(os.path.join(r_, "scenes", "S.zip"), "w").write("the sample")
    open(os.path.join(b_, "tests", "s", "row.js"), "w").write('var ZIP = "%s/scenes/S.zip";\nvar OUT = "%s/out";\n' % (r_, b_))
    open(os.path.join(b_, "CMakeCache.txt"), "w").write(
        "CMAKE_HOME_DIRECTORY:INTERNAL=%s\nCMAKE_CACHEFILE_DIR:INTERNAL=%s\nCMAKE_BUILD_TYPE:STRING=Debug\n"
        "CMAKE_CXX_FLAGS:STRING=-pipe\nDISABLE_BREAKPAD:UNINITIALIZED=ON\nJAH_CONFIGURED_SOURCE_DIR:INTERNAL=%s\n"
        "JAH_XVFB:FILEPATH=/usr/bin/Xvfb\n%s" % (r_, b_, r_, "JAH_LAB_OLD_MODEL:FILEPATH=/data/old.fbx\n" if tree == "parent" else ""))
lane, parent = os.path.join(t, "lane", "build-linux"), os.path.join(t, "parent", "build-linux")
cl, cp = gg.build_config(lane)[0], dict(gg.build_config(parent)[0], keys=gg.config_keys(parent))
recd = {"hash": cl["hash"], "options": cl["options"]}
cases = [("a lane's record, its tree's path the only difference (and a stale location in the parent's cache): EQUAL", recd, cp, True),
         ("the same record without its options: unknown, never equal", {"hash": cl["hash"]}, cp, False)]
base_cache = open(os.path.join(lane, "CMakeCache.txt")).read()


def variant(line, name):
    """The lane's record had its cache carried `line` (the last entry of a name wins)."""
    open(os.path.join(lane, "CMakeCache.txt"), "w").write(base_cache + line + "\n")
    v = gg.build_config(lane)[0]
    cases.append((name, {"hash": v["hash"], "options": v["options"]}, cp, False))


variant("DISABLE_BREAKPAD:UNINITIALIZED=OFF", "a switch differs (DISABLE_BREAKPAD OFF; the flags equal): NOT equal")
variant("CMAKE_CXX_FLAGS:STRING=-O2", "the flags differ (CMAKE_CXX_FLAGS -O2): NOT equal")
for name, rc_, cfg_, want_ in cases:
    got_ = gg.config_equal(rc_, cfg_)
    print(("ok   " if got_ == want_ else "FAIL ") + "config_equal: " + name)
    if got_ != want_:
        fails.append("config_equal: " + name)
rowj = lambda tree: {"name": "s.row", "properties": [{"name": "JAH_DATA", "value": "samples/S"}],  # noqa: E731
                     "command": ["/bin/sh", os.path.join(t, tree, "build-linux", "tests", "s", "row.js")]}
dh = [gg.data_set(rowj(tr), gg.build_roots(os.path.join(t, tr, "build-linux")), {})["hash"] for tr in ("lane", "parent")]
if not dh[0] or dh[0] != dh[1]:
    fails.append("a configured copy holding its tree's path hashes apart in two trees: %s" % dh)
else:
    print("ok   a configured copy that names its tree's path hashes EQUAL in two trees (the paths stripped)")
open(os.path.join(t, "parent", "build-linux", "tests", "s", "row.js"), "a").write("var MORE = 1;\n")
if gg.data_set(rowj("parent"), gg.build_roots(parent), {})["hash"] == dh[0]:
    fails.append("a configured copy's real change does not move the data hash")
shutil.rmtree(t, ignore_errors=True)
print("gate.serve_red:", "FAIL (%d)" % len(fails) if fails else "PASS")
sys.exit(1 if fails else 0)
