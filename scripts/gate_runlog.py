#!/usr/bin/env python3
"""gate_runlog — THE RECORD OF WHAT RAN (DOCS/PROCESS.md; TESTING-SIMPLE). One record per row (and per pool arm) as the
row ends, appended to <workspace>/local/runs/<date>-<tier>-<tip9>.jsonl (the box-local root, BOX-LOCAL-1: JAH_RUN_LOG_DIR,
then JAH_LOCAL_ROOT override): suite, arm,
verdict, seconds, labels, the row's inputs (its declaration and data hashes), the tip (studio, irisgl, fork and the
build's BUILT_FROM stamp), the build config, a red's first failing line and its output's path, and THE ROW's LAW NUMBERS
(`laws`, green or red: every `LAW <name> <value> <unit> [<op> <bar>]` line the row printed - tests/support/lawline.h - so a
regression's range reads from the records by the number, not only by the first FAIL; HARNESS-3 (b)). A green record at an
equal input at an ancestor serves the row (ci_gate_check.py serve). It decides nothing: a red is printed and recorded.

    scripts/gate_runlog.py run --tier spec|merge|ship --lane <name> [--build build-linux] -- ctest -j8 ...
    scripts/gate_runlog.py clocks          # the GPU's clock state now (exit 3 when it reads locked)

ctest runs INSIDE --build (its cwd): a ctest `--test-dir` is REFUSED by name, before anything runs (DEV_ERRORS 52: three holds
void - a relative one was read from inside the build and ctest failed to start). A run's id is unique per call: the second,
the tip and the call's pid (URGENT #209: two solo calls in one second shared an id, and the solo test read neither as solo).
A PRICE's FACTS (DEV_ERRORS 49: rows priced from runs that compiled shaders in their frames, or at load 12): each row record
carries `inframe` (its output's lines saying a frame compiled a shader: Ogre's ` - its shader COMPILED in the frame` and
`HlmsCompute: job '<name>' parsed its template in the frame (`, fork OgreHlms.cpp:3035 / OgreHlmsCompute.cpp:381; counted
past the output cap), `coldRow` (the row is declared COLD: tests/test-cold-rows.txt - its in-frame compiles are its subject),
`load` (the box's 1-minute load averaged over the row: /proc/loadavg sampled at its start, every 2 s and at its end;
$JAH_LOADAVG_FILE a toy's) and `ncpu` (our CPUs: this process's affinity). The lead's `spec.sh reprice` refuses such a run.
"""
import argparse
import datetime
import json
import math
import os
import re
import shlex
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCHEMA = 4                        # 4: `laws` (HARNESS-3 (b))
TIERS = ("spec", "merge", "ship")
MEASURING = {"measure"}          # the label of a row that measures: run at the sprint's ship only, never served

_START = re.compile(r"^\s*Start\s+\d+:\s+(\S+)\s*$")
_RESULT = re.compile(r"^\s*\d+/\d+\s+Test\s+#(\d+):\s+(\S+)\s+\.*\s*(.*?)\s+([\d.]+)\s+sec\s*$")
_VLINE = re.compile(r"^(\d+): ?(.*)$")
# ctest's line before a row it does not run because a row it needs went red (a FIXTURES_SETUP or a DEPENDS parent):
# `Failed test dependencies: <row> [<row>...]` - unprefixed, right before that row's `***Not Run` result (URGENT #123)
_DEPS = re.compile(r"^Failed test dependencies:\s*(.*?)\s*$")
_PREAMBLE = re.compile(r"^(Test command: |Working Directory: |Environment variables: ?$|Environment variable "
                       r"modifications: ?$|Test timeout computed to be: )")
_VNOISE = re.compile(r"^(test \d+|UpdateCTestConfiguration .*|Constructing a list of tests|Done constructing a list "
                     r"of tests|Updating test list for fixtures|Added \d+ tests? to meet fixture requirements|"
                     r"Checking test dependency graph(\.\.\.| end))\s*$")
# the pools' arm lines (tests/support/run_pool.py): `ARM-BEGIN <pool>.<arm>`, then `ARM <pool>.<arm> PASS|FAIL <ms>`;
# an arm that began and never ended is a CRASH
_ARM = re.compile(r"^\s*ARM\s+(\S+)\s+(PASS|FAIL|CRASH|TIMEOUT|SKIP)\b(?:\s+(\d+(?:\.\d+)?)\s*(ms|s)?)?")
_ARM_BEGIN = re.compile(r"^\s*ARM-BEGIN\s+(\S+)\s*$")
# a red's first failing line: our `FAIL: <what>`, a fixture's `  FAIL (3) <law>` (HARNESS-3 (b): the engine fixtures' check
# lines were never matched - their reds recorded no fail line), gtest's `[  FAILED  ]`, QTest's `FAIL!  :`, a script's assert
_FAILLINE = re.compile(r"^\s*(?:\|\s*)*(FAIL(?:[:(!]|\s+\S).*|\[\s+FAILED\s+\].*)$")
_SCRIPTFAIL = re.compile(r"([^/\s]+\.js:\d+: Error: .*)$")
OUTPUT_CAP = 262144
# THE LAW LINE (tests/support/lawline.h): `LAW <name> <value> <unit> [<op> <bar>]` - collected past OUTPUT_CAP too
_LAW = re.compile(r"^\s*LAW\s+([A-Za-z0-9_.:+\-]+)\s+(\S+)\s+(\S+)(?:\s+(<=|<|>=|>|==)\s+(\S+))?\s*$")
_NONFINITE = ("nan", "inf", "infinity")
# A FRAME THAT COMPILED (DEV_ERRORS 49; spikes/hlms-cache-outofdate/DIAGNOSIS.md "How a row can DETECT a half-cold run"): Ogre's
# line for a permutation whose shader compiled inside a frame, and HlmsCompute's for a job whose template was parsed inside one.
# "built in the frame from CACHED shader code - a PSO only" is warm and never matches.
_INFRAME = re.compile(r" - its shader COMPILED in the frame|HlmsCompute: job '.*' parsed its template in the frame \(")


# THE VERDICTS A RECORD WRITES that are not green - the one list (verdict_of below writes all but NOADMIT, the admission
# door's; the lead's spec.sh --reds reads it: the stage-1 audit's finding 7)
RED_VERDICTS = ("FAIL", "TIMEOUT", "CRASH", "NOTRUN", "NOADMIT")


def verdict_of(status):
    s = status.lower()
    if "passed" in s: return "PASS"
    if "timeout" in s: return "TIMEOUT"
    if "not run" in s or "disabled" in s or "skipped" in s: return "NOTRUN"
    if "exception" in s or "segfault" in s or "abort" in s or "signal" in s: return "CRASH"
    return "FAIL"


def fail_line(text):
    script = None
    for line in (text or "").splitlines():
        m = _FAILLINE.match(line)
        if m:
            return m.group(1).strip()[:300]
        if script is None:
            m = _SCRIPTFAIL.search(line)
            if m:
                script = m.group(1).strip()[:300]
    return script


def _number(text):
    """(value, ok): a finite float, or None for nan/inf (ok), or None for a malformed number (not ok)."""
    try:
        v = float(text)
    except ValueError:
        return None, False
    return (v if math.isfinite(v) else None), True


def laws_of(text):
    """({name: {value, unit[, op, bar][, text]}}, [malformed LAW lines]) of a row's output. A non-finite value is kept as
    its text (value null); a name printed twice keeps both (the second as <name>#2)."""
    laws, bad = {}, []
    for line in (text or "").splitlines():
        if not line.lstrip().startswith("LAW"):
            continue
        m = _LAW.match(line)
        v, ok = _number(m.group(2)) if m else (None, False)
        b, bok = _number(m.group(5)) if m and m.group(4) else (None, True)
        if not m or not ok or not bok:
            if line.split()[0] == "LAW":
                bad.append(line.strip()[:200])
            continue
        rec = {"value": v, "unit": m.group(3)}
        if v is None:
            rec["text"] = m.group(2)
        if m.group(4):
            rec["op"], rec["bar"] = m.group(4), b
        key, n = m.group(1), 2
        while key in laws:
            key, n = f"{m.group(1)}#{n}", n + 1
        laws[key] = rec
    return laws, bad


def arms_of(text):
    """[(arm, verdict, seconds)] of a pool row's output."""
    arms, begun = [], []
    for line in (text or "").splitlines():
        m = _ARM_BEGIN.match(line)
        if m:
            begun.append(m.group(1)); continue
        m = _ARM.match(line)
        if m:
            secs = None
            if m.group(3):
                secs = float(m.group(3)) if m.group(4) == "s" else float(m.group(3)) / 1000.0
            arms.append((m.group(1), m.group(2), secs))
    ended = {a for a, _, _ in arms}
    return arms + [(a, "CRASH", None) for a in dict.fromkeys(begun) if a not in ended]


def workspace_root():
    """The workspace that holds the lead repo: the parent of the Studio repo's main tree (the common git dir is
    <workspace>/jahshaka/.git for the main tree and every worktree)."""
    try:
        cd = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"], cwd=ROOT,
                            capture_output=True, text=True).stdout.strip()
        if cd:
            return os.path.dirname(os.path.dirname(cd))
    except OSError:
        pass
    return os.path.dirname(ROOT)


def log_dir():
    return (os.environ.get("JAH_RUN_LOG_DIR")
            or os.path.join(os.environ.get("JAH_LOCAL_ROOT") or os.path.join(workspace_root(), "local"), "runs"))


def _git(args, cwd=None):
    try:
        r = subprocess.run(["git"] + args, cwd=cwd or ROOT, capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else ""
    except OSError:
        return ""


def tree_shas(root=None):
    """The tree that ran: {studio, irisgl, fork, studio_dirty, irisgl_dirty, fork_dirty} (the applied assimp stack is not
    dirt; the fork's working tree, hidden by irisgl's --ignore-submodules=dirty, is read apart, untracked files too)."""
    root = root or ROOT
    ig = os.path.join(root, "irisgl")
    fk = os.path.join(ig, "thirdparty", "ogre-next")
    dirty = lambda d: bool(_git(["status", "--porcelain", "--untracked-files=no", "--ignore-submodules=dirty"], d))
    i_dirty = dirty(ig)
    return {"studio": _git(["rev-parse", "HEAD"], root), "irisgl": _git(["rev-parse", "HEAD"], ig),
            "fork": _git(["rev-parse", "HEAD"], fk),
            "studio_dirty": dirty(root) or i_dirty, "irisgl_dirty": i_dirty,
            "fork_dirty": bool(_git(["status", "--porcelain", "--untracked-files=normal"], fk))}


def built_from(build):
    """{studio, irisgl, dirty, fork_dirty, content, stale} from <build>/BUILT_FROM (cmake/BuiltFrom.cmake writes it as every
    build's last step). `stale` (URGENT #187, cmake/built_from.py): this pass's binaries miss a source ninja could not see (an old
    mtime) - "?" (the map unread) is stale too; a stamp written before the field carries none (False)."""
    try:
        kv = dict(l.split("=", 1) for l in open(os.path.join(build, "BUILT_FROM")).read().splitlines() if "=" in l)
    except OSError:
        return None
    return {"studio": kv.get("studio", ""), "irisgl": kv.get("irisgl", ""), "dirty": kv.get("dirty") == "1",
            "fork_dirty": kv.get("fork_dirty") != "0", "content": kv.get("content"),
            "stale": kv.get("stale") not in (None, "0"), "targets": [t for t in kv.get("targets", "").split(",") if t]}


def coverage(build):
    """THE STAMP COVERS A ROW OR ITS RECORD IS NOT ITS TIP's (ENGINE-HEADERS-1 stage 0, build by reach): a function row ->
    the targets that row needs (scripts/row_targets.py: its command's artefacts, JAH_BUILD_TARGETS, its fixtures' rows) that
    the build's stamp does not claim at its state (BUILT_FROM `targets=`); [] when covered. A build that cannot be read
    answers every row uncovered (["?"]): a record it cannot vouch for never serves."""
    b = built_from(build) or {}
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import row_targets
        nj, doc = row_targets.Ninja.load(build), row_targets.ctest_doc(build)
        if nj is None or doc is None:
            raise OSError("no build.ninja or ctest listing")
    except (OSError, ValueError, ImportError):
        return lambda name: ["?"]
    pl = row_targets.pulls(doc)
    by = {t["name"]: t for t in doc.get("tests", [])}

    def of(name):
        if name not in by:
            return ["?"]
        need = set()
        for m in [name, *sorted(pl.get(name, ()))]:
            need |= row_targets.own_targets(by[m], nj)[0]
        return row_targets.uncovered(b.get("targets"), need, nj)
    return of


def stale_build(shas):
    """None when the record's build is its tip's; else why not."""
    b = (shas or {}).get("built")
    if not b:
        return "built from an UNKNOWN commit (no BUILT_FROM stamp)"
    if b.get("dirty"):
        return "built from a DIRTY tree"
    if b.get("stale"):
        return "built while a source ninja could not see (an old mtime) was older than its objects: STALE (build again)"
    if b.get("uncovered"):
        return "built by a pass that did not build this row's targets (%s): its binaries are not the tip's" % ", ".join(
            b["uncovered"][:6])
    if b.get("studio") != shas.get("studio") or b.get("irisgl") != shas.get("irisgl"):
        return "built from studio %s / irisgl %s, not the tip" % ((b.get("studio") or "?")[:9], (b.get("irisgl") or "?")[:9])
    return None


def gpu_clocks():
    """'locked?' = idle at >= 1500 MHz (an idle card that does not clock down), 'free', 'busy', or 'unknown'."""
    try:
        r = subprocess.run(["nvidia-smi", "--query-gpu=clocks.gr,clocks_event_reasons.gpu_idle,pstate",
                            "--format=csv,noheader,nounits"], capture_output=True, text=True, timeout=10)
        if r.returncode != 0: return {"state": "unknown"}
        gr, idle, pstate = [p.strip() for p in r.stdout.strip().splitlines()[0].split(",")]
        mhz, idle = int(float(gr)), idle.lower().startswith("active")
        return {"state": ("locked?" if mhz >= 1500 else "free") if idle else "busy", "graphics_mhz": mhz,
                "idle": idle, "pstate": pstate}
    except (OSError, subprocess.TimeoutExpired, ValueError, IndexError):
        return {"state": "unknown"}


CODE_LABEL = "code"              # the derived label of a row that takes no card (tests/CMakeLists.txt jah_code_label_pass)
_NVRM = re.compile(r"\bKernel Module\b.*?\s(\d+\.\d+(?:\.\d+)?)\s")
# THE ENVIRONMENT FINGERPRINT (WORKFLOW-STREAMLINE-1 5.3; it generalises URGENT #167, where the driver 595.91.07 -> 595.99.02
# after 16:51's crash let PHOTON-7-9A's call be served from the faulted run's records). A record's verdict is the environment's
# too: every record carries `env` = {card, cpu, fields}; a CARD row (no `code` label) is served - green or red - only at an
# equal `card` hash (the driver, the compiler VERSION, Qt, the Vulkan loader and its layers, the kernel, the card), a CPU row at an equal
# `cpu` hash (the compiler and Qt alone). Read once a process, from files (no card call): the driver from
# /proc/driver/nvidia/version (else nvidia-smi), the compiler's id and version from the build's CMakeFiles/<v>/
# CMakeCXXCompiler.cmake (its PATH, /usr/bin/c++, names no version: a GCC update kept every key), Qt from the cache's Qt6_DIR
# (Qt6ConfigVersionImpl.cmake's PACKAGE_VERSION), the loader from libvulkan.so.1's real name, each layer manifest's name,
# implementation and API versions (VVL 1.4.341's false VUID moved a validation row's verdict: GATE_AND_RIG), the kernel's
# release, THE CARD (URGENT #188: the two boxes' driver is equal and their kernels differ by one build, so at equal kernels one
# card's records and six would serve on another): every display-class PCI device (sysfs class 0x03xxxx, in bus order) as
# "<model> [vendor:device subvendor:subdevice]" - the ids from /sys/bus/pci/devices/*/ (the subsystem ids name the board, its
# memory size among them), the model from /proc/driver/nvidia/gpus/<bus id>/information (another vendor's: the ids alone);
# files only, measured 0.3 ms, no nvidia-smi and no card call. A field that cannot be read is "?" - NEVER equal, on either
# side (ci_gate_check.env_differs, #167's law); a record that predates a field is another environment (its card hash
# differs: a new field re-runs every card row once, as a new box does). JAH_ENV_FINGERPRINT (the suites'
# one override, "driver=toy-A,kernel=k1"): the fields named, every other "-" - a suite never reads the real box.
ENV_FIELDS = ("driver", "compiler", "qt", "vulkan", "layers", "kernel", "gpu")
CPU_FIELDS = ("compiler", "qt")
_ENV = {}


def _first(path, rx):
    try:
        m = re.search(rx, open(path, errors="replace").read(), re.M)
        return m.group(1) if m else None
    except OSError:
        return None


def _driver():
    v = _first("/proc/driver/nvidia/version", _NVRM.pattern)
    if v is None:
        try:
            r = subprocess.run(["nvidia-smi", "--query-gpu=driver_version", "--format=csv,noheader"], capture_output=True,
                               text=True, timeout=10)
            v = (r.stdout.strip().splitlines() or [None])[0] if r.returncode == 0 else None
        except (OSError, subprocess.TimeoutExpired):
            v = None
    return v


def _gpu(pci="/sys/bus/pci/devices", nv="/proc/driver/nvidia/gpus"):
    """THE CARD as the fingerprint's `gpu` field (the header above), or None (no display-class device readable)."""
    out = []
    for d in sorted(os.listdir(pci)) if os.path.isdir(pci) else ():
        try:
            rd = lambda n: open(os.path.join(pci, d, n)).read().strip()   # noqa: E731
            if not rd("class").startswith("0x03"):
                continue
            ids = "%s:%s %s:%s" % tuple(rd(n)[2:] for n in ("vendor", "device", "subsystem_vendor", "subsystem_device"))
        except OSError:
            continue
        model = _first(os.path.join(nv, d, "information"), r"^Model:\s*(.*?)\s*$")
        out.append(f"{model} [{ids}]" if model else f"[{ids}]")
    return ";".join(out) or None


def _layers():
    out = []
    for d in ("/usr/share/vulkan/explicit_layer.d", "/usr/share/vulkan/implicit_layer.d", "/etc/vulkan/explicit_layer.d",
              "/etc/vulkan/implicit_layer.d"):
        for f in sorted(os.listdir(d)) if os.path.isdir(d) else ():
            try:
                doc = json.load(open(os.path.join(d, f)))
            except (OSError, ValueError):
                continue
            for l in ([doc["layer"]] if "layer" in doc else doc.get("layers") or []):
                out.append(f"{l.get('name')}:{l.get('implementation_version')}:{l.get('api_version')}")
    return ";".join(sorted(out)) or None


def env_fields(build):
    """{field: value} of ENV_FIELDS for `build` (the header above); cached per build a process."""
    if build in _ENV:
        return _ENV[build]
    ov = os.environ.get("JAH_ENV_FINGERPRINT")
    if ov is not None:
        got = dict(kv.split("=", 1) for kv in ov.split(",") if "=" in kv)
        f = {k: got.get(k, "-") for k in ENV_FIELDS}
    else:
        cache = os.path.join(build or "", "CMakeCache.txt")
        comp = None
        for p in sorted(os.listdir(os.path.join(build, "CMakeFiles"))) if build and os.path.isdir(os.path.join(build, "CMakeFiles")) else ():
            c = os.path.join(build, "CMakeFiles", p, "CMakeCXXCompiler.cmake")
            i, v = _first(c, r'^set\(CMAKE_CXX_COMPILER_ID "([^"]*)"\)'), _first(c, r'^set\(CMAKE_CXX_COMPILER_VERSION "([^"]*)"\)')
            if v:
                comp = f"{i} {v}"
        qd = _first(cache, r"^Qt6_DIR:PATH=(.*)$")
        qt = None
        for n in ("Qt6ConfigVersionImpl.cmake", "Qt6ConfigVersion.cmake"):
            qt = qt or (_first(os.path.join(qd, n), r'^set\(PACKAGE_VERSION "([^"]*)"\)') if qd else None)
        lv = None
        for lib in ("/usr/lib/x86_64-linux-gnu/libvulkan.so.1", "/usr/lib64/libvulkan.so.1", "/usr/lib/libvulkan.so.1"):
            if os.path.exists(lib):
                lv = os.path.basename(os.path.realpath(lib)).replace("libvulkan.so.", "")
                break
        f = {"driver": _driver(), "compiler": comp, "qt": qt, "vulkan": lv, "layers": _layers(), "kernel": os.uname().release,
             "gpu": _gpu()}
        f = {k: (v if v else "?") for k, v in f.items()}
    _ENV[build] = f
    return f


def env_fingerprint(build):
    """{card, cpu, fields}: the hash of every field (a card row's key) and of CPU_FIELDS alone (a CPU row's)."""
    import hashlib
    f = env_fields(build)
    h = lambda ks: hashlib.sha256("\n".join(f"{k}={f[k]}" for k in ks).encode()).hexdigest()[:16]   # noqa: E731
    return {"card": h(ENV_FIELDS), "cpu": h(CPU_FIELDS), "fields": f}


# A TIMEOUT THAT MEASURED THE ENVIRONMENT IS NO VERDICT (WORKFLOW-STREAMLINE-1 5.2, the owner 2026-11-20; 144 TIMEOUTs since 10-13
# were 2.5 h of card with no answer, each served red at its tree until a `--rerun-red --why`). A TIMEOUT record carries
# `no_verdict` (its reason) when
#   (a) COLD: its row booted on a cold cache - the caller (spec.sh's gate) checks the warm-up's STAMP for the boots the row's class
#       needs (scripts/warm-app-cache.sh --check: the key the stamp names against the build's key now) before the row's batch
#       and hands the answer over as JAH_ROW_COLD ({row: why} for the batch's cold rows); or
#   (b) AT THE CAP EDGE: its price (scripts/gate-times.txt) is at least CAP_EDGE of its cap (the TIMEOUT's own wall: ctest killed it
#       there). CAP_EDGE = 1/1.3: a sibling's full-width build raised every 60-s row's wall ~30 % (URGENT #121, measured
#       2026-10-29), so a row priced over cap/1.3 can cross its cap by load alone - its TIMEOUT measured the load, not the row.
# A no-verdict record is never served red (ci_gate_check.py); the caller runs the row once more, warm (JAH_ROW_RETRY=1: that run's
# TIMEOUT is a verdict unless it is cold again - the cap edge is judged once).
CAP_EDGE = 1 / 1.3
_PRICES = []


def prices():
    """{row: seconds} of the tree's scripts/gate-times.txt (read once a process; a malformed line skipped)."""
    if not _PRICES:
        out = {}
        try:
            for l in open(os.path.join(ROOT, "scripts", "gate-times.txt"), errors="replace"):
                w = l.split()
                if len(w) >= 2 and not w[0].startswith("#"):
                    try:
                        out[w[0]] = float(w[1])
                    except ValueError:
                        pass
        except OSError:
            pass
        _PRICES.append(out)
    return _PRICES[0]


def no_verdict(row, secs):
    """Why a TIMEOUT of `row` after `secs` s is no verdict, or None (it is a red)."""
    try:
        cold = json.loads(os.environ.get("JAH_ROW_COLD") or "{}").get(row)
    except (ValueError, AttributeError):
        cold = None
    if cold:
        return "cold: " + str(cold)[:200]
    if os.environ.get("JAH_ROW_RETRY") == "1":
        return None
    p = prices().get(row)
    if p and secs and p >= CAP_EDGE * secs:
        return f"cap edge: priced {p:g} s, its cap {secs:.1f} s (a 30 % load inflation, #121, crosses it)"
    return None


def takes_card(labels):
    """A row takes the card unless it carries the derived label `code` (a tree before it: every row does)."""
    labels = [labels] if isinstance(labels, str) else (labels or ())
    return CODE_LABEL not in labels


def row_facts(build):
    """({row: {labels, declaration, data_hash, data_why}}, config, config_why) of a build dir."""
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import gate_graph
        facts, _ = gate_graph.row_facts(build)
        cfg, why = gate_graph.build_config(build)
        return facts, cfg, why
    except (OSError, ValueError) as e:
        return {}, None, f"the build could not be read ({e.__class__.__name__})"


def append_records(records, tier, tip):
    path = os.path.join(log_dir(), f"{datetime.date.today().isoformat()}-{tier}-{(tip or 'notip')[:9]}.jsonl")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "a") as f:
        for r in records:
            f.write(json.dumps(r, sort_keys=True) + "\n")
    return path


EVIDENCE_RUNS = 40          # THE EVIDENCE CAP (TEST-STREAMLINE-1 1f): the newest output/<run>/ dirs kept
EVIDENCE_CACHE_REL = os.path.join(".local", "share", "Jahshaka", "shadercache")


def evidence_keep_homes(tests):
    """THE HOMES WHOSE CACHE IS THEIR ROW's SUBJECT, from the REGISTRATION (the stage-1 audit's finding 10: a marker the homes
    written before the rule lack): every row whose ENVIRONMENT carries JAH_EVIDENCE_CACHE=keep (tests/CMakeLists.txt sets it on
    the shadercache-labelled and COLD rows) - the basenames of its HOME, its JAHSHAKA_DATA_ROOT's home (the dir above
    .local/share/...) and its command's fresh_home.sh <home>. `tests`: ctest's json-v1 list."""
    keep = set()
    for t in tests or []:
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        env = props.get("ENVIRONMENT") or []
        env = [env] if isinstance(env, str) else env
        if "JAH_EVIDENCE_CACHE=keep" not in env:
            continue
        for e in env:
            k, _, v = e.partition("=")
            if k == "HOME" and v:
                keep.add(os.path.basename(v.rstrip("/")))
            elif k == "JAHSHAKA_DATA_ROOT" and ".local" in v:
                keep.add(os.path.basename(v.split("/.local")[0].rstrip("/")))
        cmd = [str(c) for c in t.get("command") or []]
        at = next((i for i, c in enumerate(cmd) if c.endswith("fresh_home.sh")), None)
        if at is not None:
            pos = [c for c in cmd[at + 1:cmd.index("--") if "--" in cmd else len(cmd)] if not c.startswith("-")]
            if pos:
                keep.add(os.path.basename(pos[-1].rstrip("/")))
    return keep


def prune_evidence(build, keep_runs=EVIDENCE_RUNS, keep_homes=None):
    """THE EVIDENCE CAP, at the start of every call that writes evidence (TEST-STREAMLINE-1 1f; photon-2's tests/.evidence held
    21 GB): (1) every red home there sheds its shader cache (its manifest kept as shadercache-manifest.txt) unless it is marked
    .evidence-keeps-cache (a row whose subject is the cache: fresh_home.sh's JAH_EVIDENCE_CACHE=keep) - the home's own logs and
    pictures stay, one home per row (fresh_home replaces it); (2) output/<run>/ keeps the newest `keep_runs` runs, and in an
    older run the file of a row whose NEWEST red log it is (never the last word on a still-red row). Returns (homes shed, runs
    removed, bytes freed)."""
    import shutil
    ev = os.path.join(build, "tests", ".evidence")
    if not os.path.isdir(ev):
        return 0, 0, 0
    def size(p):   # the bytes a removal FREES: a file's only when this is its last link (the stage-1 audit's finding 11)
        n = 0
        for dp, _, fs in os.walk(p):
            for f in fs:
                try:
                    st = os.lstat(os.path.join(dp, f))
                    n += st.st_size if st.st_nlink == 1 else 0
                except OSError:
                    pass
        return n
    if keep_homes is None:      # the registration's (evidence_keep_homes); an unreadable build keeps by the marker alone
        try:
            import gate_graph
            keep_homes = evidence_keep_homes(json.loads(gate_graph.ctest_inventory(build)[0] or "{}").get("tests", []))
        except Exception:
            keep_homes = set()
    shed = freed = gone = 0
    for name in sorted(os.listdir(ev)):
        home = os.path.join(ev, name)
        cache = os.path.join(home, EVIDENCE_CACHE_REL)
        if name == "output" or not os.path.isdir(cache) or name in keep_homes \
                or os.path.exists(os.path.join(home, ".evidence-keeps-cache")):
            continue
        man = os.path.join(cache, "cache-manifest.txt")
        if os.path.isfile(man):
            shutil.copyfile(man, os.path.join(home, "shadercache-manifest.txt"))
        freed += size(cache)
        shutil.rmtree(cache, ignore_errors=True)
        shed += 1
    out = os.path.join(ev, "output")
    if os.path.isdir(out):
        runs = sorted((r for r in os.listdir(out) if os.path.isdir(os.path.join(out, r))), reverse=True)   # <time>-<sha>: newest first
        newest = {}
        for r in runs:
            for f in os.listdir(os.path.join(out, r)):
                newest.setdefault(f, r)
        for r in runs[keep_runs:]:
            d = os.path.join(out, r)
            for f in os.listdir(d):
                if newest.get(f) != r:
                    p = os.path.join(d, f)
                    freed += size(p) if os.path.isdir(p) else (os.lstat(p).st_size if os.lstat(p).st_nlink == 1 else 0)
                    shutil.rmtree(p, ignore_errors=True) if os.path.isdir(p) else os.remove(p)
            if not os.listdir(d):
                os.rmdir(d)
                gone += 1
    return shed, gone, freed


def _row_output(build, run_id, row, text):
    """<build>/tests/.evidence/output/<run>/<row>.log holding a red row's output; its path, or None."""
    try:
        d = os.path.join(build, "tests", ".evidence", "output", run_id)
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, re.sub(r"[^A-Za-z0-9._:-]", "_", row) + ".log")
        with open(path, "w") as f:
            f.write((text or "") + "\n")
        return path
    except OSError:
        return None


def _load1():
    """The box's 1-minute load average now (/proc/loadavg, or $JAH_LOADAVG_FILE), or None."""
    try:
        with open(os.environ.get("JAH_LOADAVG_FILE") or "/proc/loadavg") as f:
            return float(f.read().split()[0])
    except (OSError, ValueError, IndexError):
        return None


def _cold_rows(build):
    """The rows declared COLD (<build>/tests/test-cold-rows.txt, one `<row>\t...` line each): their first frames compile by law."""
    try:
        return {l.split("\t", 1)[0].strip() for l in open(os.path.join(build, "tests", "test-cold-rows.txt"), errors="replace")
                if l.strip()}
    except OSError:
        return set()


def refused_test_dir(cmd):
    """DEV_ERRORS 52: the --test-dir argument a ctest command carries, or None (ctest runs inside --build already)."""
    return next((c for c in cmd if c == "--test-dir" or c.startswith("--test-dir=")), None)


def run_ctest(cmd, build, tier, lane, echo=True):
    """Run one ctest command (argv) verbose in `build`; append every row's records as it ends; return (rc, reds)."""
    if tier not in TIERS:
        raise ValueError(f"gate_runlog: tier '{tier}' is not one of {', '.join(TIERS)}")
    try:   # the evidence cap (prune_evidence), before this call writes any
        shed, gone, freed = prune_evidence(build)
        if shed or gone:
            print(f"gate_runlog: the evidence cap - {shed} red home(s) shed their shader cache, {gone} old output run(s) "
                  f"removed, {freed / 1048576:.0f} MB freed", flush=True)
    except OSError as e:
        print(f"gate_runlog: the evidence cap failed ({e}) - nothing removed", flush=True)
    shas = tree_shas(_git(["rev-parse", "--show-toplevel"], build) or ROOT)
    shas["built"] = built_from(build)
    (shas["built"] or {}).pop("targets", None)   # (the claim is judged per row below; a record keeps what it misses)
    covers = coverage(build) if shas["built"] else (lambda name: [])
    facts, config, config_why = row_facts(build)
    run_id = f"{datetime.datetime.now().strftime('%Y%m%dT%H%M%S')}-{shas['studio'][:9]}-p{os.getpid()}"   # #209: one per call
    display = os.environ.get("DISPLAY")
    vcmd = [cmd[0], "-V"] + [c for c in cmd[1:] if c != "--output-on-failure"]
    p = subprocess.Popen(vcmd, cwd=build, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1,
                         errors="replace")
    buf, size, pre, reds, lawbuf, late, armbuf = {}, {}, {}, [], {}, {}, {}
    inframe, cold = {}, _cold_rows(build)
    ncpu = len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else os.cpu_count()
    loads, started, last = [], {}, 0.0    # (monotonic, load) samples of the whole call; a row's start index by name
    deps = None   # the last `Failed test dependencies:` names, for the next result line
    for line in p.stdout:
        ln = line.rstrip("\n")
        now_m = time.monotonic()
        if now_m - last >= 2.0:
            last = now_m
            ld = _load1()
            if ld is not None:
                loads.append(ld)
        ms = _START.match(ln)
        if ms:
            started[ms.group(1)] = len(loads) - 1 if loads else 0
        md = _DEPS.match(ln)
        if md:
            deps = md.group(1).split()
        m = _VLINE.match(ln)
        if m:
            i, txt = m.group(1), m.group(2)
            st = pre.get(i, True)
            if st:
                if txt.startswith("Test timeout computed to be: "):
                    pre[i] = False; continue
                if _PREAMBLE.match(txt):
                    pre[i] = "env" if txt.startswith("Environment variable") else True; continue
                if st == "env" and txt.startswith(" "):
                    continue
                pre[i] = False
            if txt.lstrip().startswith("LAW"):
                lawbuf.setdefault(i, []).append(txt)
            if _INFRAME.search(txt):                        # past the output cap too (DEV_ERRORS 49)
                inframe[i] = inframe.get(i, 0) + 1
            if _ARM.match(txt) or _ARM_BEGIN.match(txt):   # a pool's arm lines, past the cap too (TE7: run_pool echoes
                armbuf.setdefault(i, []).append(txt)        # every arm's output, so a pool's output passes 256 KB)
            if size.get(i, 0) < OUTPUT_CAP:
                buf.setdefault(i, []).append(txt)
                size[i] = size.get(i, 0) + len(txt) + 1
            elif i not in late and fail_line(txt):
                late[i] = fail_line(txt)        # a red's first failing line printed past the cap (HARNESS-3 (b))
            continue
        if _VNOISE.match(ln) or _START.match(ln):
            continue
        m = _RESULT.match(ln)
        if echo and (m or not ln.strip().startswith(("Start ", "test "))):
            sys.stdout.write(line); sys.stdout.flush()
        if not m:
            continue
        i, name, status, secs = m.group(1), m.group(2), m.group(3), float(m.group(4))
        behind, deps = deps, None
        text = "\n".join(buf.pop(i, []))
        laws, badlaws = laws_of("\n".join(lawbuf.pop(i, [])))
        nframe = inframe.pop(i, 0)
        ld = _load1()
        mine = loads[started.pop(name, len(loads)):] + ([ld] if ld is not None else [])
        armtext = "\n".join(armbuf.pop(i, []))
        late_fail = late.pop(i, None)
        pre.pop(i, None); size.pop(i, None)
        f = facts.get(name) or {}
        nulls = {}
        if not f:
            nulls["declaration"] = nulls["data_hash"] = "the row is not in the build's ctest inventory"
        elif f.get("data_hash") is None:
            nulls["data_hash"] = f.get("data_why") or "no data hash"
        if config is None:
            nulls["config"] = config_why
        unc = covers(name)
        tip = dict(shas, built=dict(shas["built"], uncovered=unc)) if unc else shas
        base = {"schema": SCHEMA, "run": run_id, "tier": tier, "lanes": [lane], "tip": tip, "suite": name,
                "labels": sorted(f.get("labels") or []), "declaration": f.get("declaration"),
                "data_hash": f.get("data_hash"), "config": config, "display": display, "nulls": nulls,
                "env": env_fingerprint(build),
                "ts": datetime.datetime.now().astimezone().isoformat(timespec="seconds"), "source": "run"}
        v = verdict_of(status)
        row = dict(base, arm=None, verdict=v, status=status.strip("* "), seconds=secs, inframe=nframe,
                   load=round(sum(mine) / len(mine), 2) if mine else None, ncpu=ncpu)
        if name in cold:
            row["coldRow"] = True
        # NOT RUN BEHIND ITS FIXTURE (URGENT #123): a row ctest did not run because a row it needs is red is RECORDED so,
        # the rows it waited on named - ci_gate_check.py serve reads it served-red while they stay red at this tree, so a
        # plan never lists it to run again for nothing (8B's master check re-ran its part seven times: ~25 min of card)
        if v == "TIMEOUT" and no_verdict(name, secs):   # 5.2: it measured the cache or the load, not the row
            row["no_verdict"] = no_verdict(name, secs)
        if v == "NOTRUN" and behind:
            row["behind"] = behind
            row["status"] = "behind " + ", ".join(behind)
        if laws:
            row["laws"] = laws
        if badlaws:
            row["lawsMalformed"] = badlaws[:5]
        if v != "PASS":
            reds.append(name)
            row["failLine"] = (("NOT RUN behind " + ", ".join(row["behind"])) if row.get("behind")
                               else fail_line(text) or late_fail)
            row["log"] = _row_output(build, run_id, name, text)
            if echo and text:
                sys.stdout.write("\n".join(text.splitlines()[-40:]) + "\n"); sys.stdout.flush()
        recs = [row] + [dict(base, arm=a, verdict=av, status=av, seconds=s) for a, av, s in arms_of(armtext)]
        append_records(recs, tier, shas["studio"])
    return p.wait(), reds


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="run a ctest command and record every row")
    r.add_argument("--tier", required=True, choices=TIERS)
    r.add_argument("--lane", required=True)
    r.add_argument("--build", default="build-linux")
    r.add_argument("ctest", nargs=argparse.REMAINDER)
    sub.add_parser("clocks", help="the GPU's clock state now (exit 3 when it reads locked)")
    a = ap.parse_args()
    if a.cmd == "clocks":
        st = gpu_clocks()
        print(json.dumps(st, sort_keys=True))
        sys.exit(3 if st.get("state") == "locked?" else 0)
    cmd = a.ctest[1:] if a.ctest and a.ctest[0] == "--" else a.ctest
    if not cmd:
        ap.error("give the ctest command after --")
    if len(cmd) == 1:
        cmd = shlex.split(cmd[0])
    td = refused_test_dir(cmd)
    if td:
        print(f"gate_runlog: REFUSED - the ctest command carries {td!r}: gate_runlog runs ctest INSIDE --build ({a.build}) "
              "already, so a --test-dir is read from there and the call runs nothing (DEV_ERRORS 52: three holds void). Drop "
              "--test-dir and name the build with --build <dir>; nothing ran, nothing recorded", file=sys.stderr)
        sys.exit(2)
    build = os.path.abspath(a.build)
    rc, _ = run_ctest(cmd, build, a.tier, a.lane)
    sys.exit(rc)


if __name__ == "__main__":
    main()
