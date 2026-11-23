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


# THE ENVIRONMENT (WORKFLOW-STREAMLINE-1 5.3): the records' and now's fingerprint, made by the tree's own gate_runlog from faked
# fields (JAH_ENV_FINGERPRINT: the box never read) - the card's hash (every field) and the CPU's (the compiler and Qt)
import gate_runlog as grl  # noqa: E402


def fp(**kw):
    os.environ["JAH_ENV_FINGERPRINT"] = ",".join(f"{k}={v}" for k, v in dict({"driver": "d1", "compiler": "GNU 15", "qt": "6.10",
                                                                               "kernel": "k1"}, **kw).items())
    grl._ENV.clear()
    return grl.env_fingerprint("/nonexistent")


ENV = fp()


def rec(verdict, cfg="cfg", decl="d", data="h", env=ENV):
    return {"suite": "gi.row", "verdict": verdict, "failLine": "FAIL: the law", "tip": {"studio": "a" * 40}, "config": {"hash": cfg},
            "declaration": {"hash": decl}, "data_hash": data, "run": "run-1", "seconds": 3.0, "tier": "lane", "env": env}


FACT = {"declaration": {"hash": "d"}, "data_hash": "h", "labels": ["gi"]}
CFG = {"hash": "cfg", "build_type": "Debug"}
fails = []


def want(name, got, kind, why_has=None):
    ok = got[0] == kind and (why_has is None or (got[1] or "").find(why_has) >= 0)
    print(("ok   " if ok else "FAIL ") + "%s: %s%s" % (name, got[0], (" (%s)" % got[1]) if got[1] else ""))
    if not ok:
        fails.append(name)


C = lambda r, reach, fact=FACT, cfg=CFG, env=ENV: cgc.classify(r, fact, "gi.row", reach, "fk", "fk", cfg, None, env)
for v in ("FAIL", "TIMEOUT", "CRASH"):
    want("a %s at THIS tree, the same configuration, declaration and data: SERVED-RED" % v, C(rec(v), Reach(True)), "red")
# AN ANCESTOR's RED (TEST-STREAMLINE-1, the stage-1 audit's finding 2): it runs only where the diff since REACHES the row - the
# fix may be in it; where it does not, the input is unchanged and the red is SERVED-RED (RED ON BASE: it always ran)
want("a FAIL at an ANCESTOR's tree whose diff since reaches the row runs (the fix may be in it)",
     C(rec("FAIL"), Reach(False, reaches={"gi.row"})), "run", "reaches it")
want("a FAIL at an ANCESTOR's tree whose diff since does NOT reach the row: SERVED-RED (an unchanged input)",
     C(rec("FAIL"), Reach(False)), "red")
want("a FAIL at this tree, its build configuration changed: runs", C(rec("FAIL", cfg="other"), Reach(True)), "run",
     "configuration")
want("a FAIL at this tree, its declaration changed: runs", C(rec("FAIL", decl="d2"), Reach(True)), "run", "declaration")
want("a FAIL at this tree, its data changed: runs", C(rec("FAIL", data="h2"), Reach(True)), "run", "data")
want("a FAIL at this tree of a row out of the inventory: runs", C(rec("FAIL"), Reach(True), fact=None), "run")
want("a PASS at this tree serves as before", C(rec("PASS"), Reach(True)), "served")
want("a PASS at an ancestor the range does not reach serves as before", C(rec("PASS"), Reach(False)), "served")
want("a PASS at an ancestor the range reaches runs", C(rec("PASS"), Reach(False, reaches={"gi.row"})), "run", "reaches")
# THE ENVIRONMENT IS AN INPUT (5.3; #167's driver one field): a card row's record answers only at an equal whole fingerprint, the
# field that moved named; a CPU row's (the label `code`) at an equal compiler and Qt (RED ON BASE: the kernel and the compiler unread)
CPU = dict(FACT, labels=["code"])
want("a PASS of a card row taken on another driver runs, the driver named", C(rec("PASS", env=fp(driver="d0")), Reach(True)), "run",
     "the driver d0 -> d1")
want("a FAIL of a card row taken on another kernel is not served red: it runs", C(rec("FAIL", env=fp(kernel="k0")), Reach(True)),
     "run", "the kernel k0 -> k1")
# THE CARD IS AN INPUT (URGENT #188, the doors-0 ground read: the two boxes' driver is equal and their kernels differ by a build, so
# at equal kernels one card's records and six were served on another): two environments equal but for the device are NOT equal
want("a PASS of a card row taken on another CARD (every other field equal) runs, the gpu named",
     C(rec("PASS", env=fp(gpu="NVIDIA GeForce RTX 4080 SUPER [10de:2702]")), Reach(True),
       env=fp(gpu="NVIDIA GeForce RTX 5060 Ti [10de:2d04]")), "run", "the gpu NVIDIA GeForce RTX 4080 SUPER [10de:2702] -> ")
want("a FAIL of a card row taken on another CARD is not served red: it runs",
     C(rec("FAIL", env=fp(gpu="g-4080")), Reach(True), env=fp(gpu="g-5060")), "run", "the gpu g-4080 -> g-5060")
def toy_pci(devs):
    """A sysfs and an NVIDIA proc tree of the test's making: {bus id: (class, vendor, device, subvendor, subdevice, model)}."""
    import tempfile
    t = tempfile.mkdtemp(prefix="serve-red-pci-")
    for bus, (cls, *ids, model) in devs.items():
        os.makedirs(os.path.join(t, "pci", bus))
        for n, v in zip(("class", "vendor", "device", "subsystem_vendor", "subsystem_device"), (cls, *ids)):
            open(os.path.join(t, "pci", bus, n), "w").write(v + "\n")
        if model:
            os.makedirs(os.path.join(t, "nv", bus))
            open(os.path.join(t, "nv", bus, "information"), "w").write(f"Model: \t\t {model}\nIRQ:   \t\t 16\n")
    try:
        return grl._gpu(os.path.join(t, "pci"), os.path.join(t, "nv"))
    finally:
        import shutil
        shutil.rmtree(t, ignore_errors=True)


# THE CARD's READ (#188): files only - every display-class device in bus order, the model from the NVIDIA proc file, the ids alone
# for another vendor's; a network card is no card; no display device is unreadable (None -> "?", never equal)
got = toy_pci({"0000:01:00.0": ("0x030000", "0x10de", "0x2d04", "0x1043", "0x8a11", "NVIDIA GeForce RTX 5060 Ti"),
               "0000:05:00.0": ("0x020000", "0x8086", "0x125c", "0x8086", "0x0000", None),
               "0000:0b:00.0": ("0x038000", "0x1002", "0x13c0", "0x1002", "0x0123", None)})
want("the card read from sysfs: the NVIDIA model + its ids; another vendor's display device by its ids; no network card",
     ("ok" if got == "NVIDIA GeForce RTX 5060 Ti [10de:2d04 1043:8a11];[1002:13c0 1002:0123]" else "bad", got), "ok")
got = toy_pci({"0000:01:00.0": ("0x030000", "0x10de", "0x2702", "0x10de", "0x18f1", "NVIDIA GeForce RTX 4080 SUPER")})
want("another board's card reads another value (the 4080 SUPER)", ("ok" if "[10de:2702 10de:18f1]" in got else "bad", got), "ok")
want("no display-class device: unreadable (None)", ("ok" if toy_pci({}) is None else "bad", None), "ok")


def before_gpu(e):
    """`e` as a record written before the gpu field carries it: its fields without it, its card hash over the six it had."""
    import hashlib
    f = {k: v for k, v in e["fields"].items() if k != "gpu"}
    old = ("driver", "compiler", "qt", "vulkan", "layers", "kernel")
    return dict(e, fields=f, card=hashlib.sha256("\n".join(f"{k}={f[k]}" for k in old).encode()).hexdigest()[:16])


want("a PASS of a card row whose record predates the gpu field runs (another environment: a new box re-runs every card row once)",
     C(rec("PASS", env=before_gpu(fp())), Reach(True)), "run", "the gpu not recorded -> ")
want("a PASS of a CPU row whose record predates the gpu field still serves (its cpu hash is the compiler and Qt's)",
     C(rec("PASS", env=before_gpu(fp())), Reach(True), fact=CPU), "served")
want("a PASS of a CPU row taken on another card still serves (the card is not a CPU field)",
     C(rec("PASS", env=fp(gpu="g-4080")), Reach(True), fact=CPU, env=fp(gpu="g-5060")), "served")
want("a PASS of a CPU row taken under another kernel and driver still serves", C(rec("PASS", env=fp(kernel="k0", driver="d0")),
     Reach(True), fact=CPU), "served")
want("a PASS of a CPU row taken under another compiler runs, the compiler named", C(rec("PASS", env=fp(compiler="GNU 14")),
     Reach(True), fact=CPU), "run", "the compiler GNU 14 -> GNU 15")
want("a record with no fingerprint (written before it) never serves", C(dict(rec("PASS"), env=None), Reach(True)), "run",
     "no environment fingerprint")
# AN UNREADABLE FIELD IS NEVER EQUAL (#167's law; the branch audit's B7): a field read as "?" on either side - even "?" against
# "?" - answers nothing: a card row with any such field runs, a CPU row with its compiler or Qt such runs (its kernel not read)
U = fp(driver="?")
want("a PASS of a card row whose driver was unreadable, now unreadable too: runs (unknown is never equal)",
     C(rec("PASS", env=U), Reach(True), env=U), "run", "unreadable")
want("a FAIL of a card row whose driver was unreadable, now too: not served red, it runs",
     C(rec("FAIL", env=U), Reach(True), env=U), "run", "unreadable")
U = fp(compiler="?")
want("a PASS of a CPU row whose compiler was unreadable, now too: runs", C(rec("PASS", env=U), Reach(True), fact=CPU, env=U), "run",
     "unreadable")
U = fp(kernel="?")
want("a PASS of a CPU row whose KERNEL was unreadable (not a CPU field): still serves", C(rec("PASS", env=U), Reach(True), fact=CPU,
     env=U), "served")
ENV = fp()
# NO VERDICT (5.2): a TIMEOUT marked no_verdict (cold, or at its cap edge: gate_runlog.no_verdict) is never served red: it runs
want("a TIMEOUT at THIS tree marked NO VERDICT (cold): it runs, never SERVED-RED",
     C(dict(rec("TIMEOUT"), no_verdict="cold: cold at k: no warm-up recorded"), Reach(True)), "run", "NO VERDICT")
for v in ("NOTRUN", "NOADMIT"):
    want("a %s is not a result: never served-red (it runs)" % v, C(rec(v), Reach(True)), "run")
# NOT RUN BEHIND ITS FIXTURE (URGENT #123): a row ctest did not run because a row it needs is red stands as a red while
# that row is red at the same commit - its part is never listed to run for nothing - and runs once that row is not
behind = dict(rec("NOTRUN"), behind=["gi.fixture"], failLine="NOT RUN behind gi.fixture")
B = lambda r, reach, at: cgc.classify(r, FACT, "gi.row", reach, "fk", "fk", CFG, at, ENV)
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
    open(os.path.join(lane, "CMakeCache.txt"), "w").write(base_cache)   # (each case its own clean state: the final read's B3)


variant("DISABLE_BREAKPAD:UNINITIALIZED=OFF", "a switch differs (DISABLE_BREAKPAD OFF; the flags equal): NOT equal")
sw_rec = cases[-1][1]
variant("CMAKE_CXX_FLAGS:STRING=-O2", "the flags differ (CMAKE_CXX_FLAGS -O2): NOT equal")
variant("JAH_COVERAGE:BOOL=ON", "THE COVERAGE BUILD (JAH_COVERAGE ON; TEST-STREAMLINE-1 stage 2): NOT equal - it never serves the Debug tree")
variant("CMAKE_BUILD_TYPE:STRING=Release", "a real switch: the build type (Release vs Debug): NOT equal")
variant("JAH_TOY_SWITCH:UNINITIALIZED=ON", "a real -D on the configure line (JAH_TOY_SWITCH ON): NOT equal")
# A PROBE ENTRY IS NO SWITCH (the census of 2026-11-23: quickjs-ng's COMPILER_SUPPORTS_Wformat=2 probe, re-read by CMake as an
# UNINITIALIZED entry whose value is "2:INTERNAL=1", made 315 of 429 records "differ"; gate_graph.misread_internal, the lead's
# ruling): this build's cache WITH it, the record's WITHOUT it (every record made before it) - EQUAL, served again (red on
# photon-7's key, and with the predicate removed: spikes/audit-fixes/red-once/gate.serve_red-switches.txt)
open(os.path.join(parent, "CMakeCache.txt"), "a").write("COMPILER_SUPPORTS_Wformat:UNINITIALIZED=2:INTERNAL=1\n")
cp = dict(gg.build_config(parent)[0], keys=gg.config_keys(parent))
cases[:] = [(n, r, cp, w) for n, r, _c, w in cases]
cases.append(("a record made BEFORE the probe and this rule (its cache without the entry) against this build's cache WITH it "
              "(COMPILER_SUPPORTS_Wformat:UNINITIALIZED=2:INTERNAL=1): EQUAL - the census's 315 stand again", recd, cp, True))
# ...AND THE OTHER DIRECTION (the confirm read of 2d9a18552: 19,627 records of the last 20 days STORED the probe among their options -
# the old key read the mis-read line as a switch): such a record (its options with COMPILER_SUPPORTS_Wformat "2:INTERNAL=1", hashed
# so) against this build, which types the line INTERNAL and keeps it out of its options - EQUAL, by _switches' drop (red with that
# drop removed: spikes/audit-fixes/red-once/gate.serve_red-stored-probe.txt)
st_opts = dict(cl["options"], COMPILER_SUPPORTS_Wformat="2:INTERNAL=1")
st_rec = {"hash": gg._sha256(gg._canon_json({"keys": gg.config_keys(lane), "options": st_opts}).encode()), "options": st_opts}
cases.append(("a record that STORED the probe among its options (the old key) against a build that types it INTERNAL: EQUAL",
              st_rec, cp, True))
open(os.path.join(lane, "CMakeCache.txt"), "w").write(base_cache + "JAH_COVERAGE:BOOL=OFF\n")
v_ = gg.build_config(lane)[0]
cases.append(("the option at OFF (the default build, byte for byte): EQUAL - its arrival re-keys no record", {"hash": v_["hash"], "options": v_["options"]}, cp, True))
open(os.path.join(lane, "CMakeCache.txt"), "w").write(base_cache)
why_ = gg.config_why(sw_rec, cp) if hasattr(gg, "config_why") else None
print(("ok   " if why_ and "a switch differs: DISABLE_BREAKPAD" in why_ else "FAIL ") + "config_why names the differing term (the switch "
      "and both values), never the equal build type: " + str(why_))
if not why_ or "a switch differs: DISABLE_BREAKPAD" not in why_:
    fails.append("config_why: " + str(why_))
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
