#!/usr/bin/env python3
"""gate_runlog — THE RECORD OF WHAT RAN (DOCS/PROCESS.md; TESTING-SIMPLE). One record per row (and per pool arm) as the
row ends, appended to <workspace>/testing/runs/<date>-<tier>-<tip9>.jsonl (JAH_RUN_LOG_DIR overrides): suite, arm,
verdict, seconds, labels, the row's inputs (its declaration and data hashes), the tip (studio, irisgl, fork and the
build's BUILT_FROM stamp), the build config, a red's first failing line and its output's path, and THE ROW's LAW NUMBERS
(`laws`, green or red: every `LAW <name> <value> <unit> [<op> <bar>]` line the row printed - tests/support/lawline.h - so a
regression's range reads from the records by the number, not only by the first FAIL; HARNESS-3 (b)). A green record at an
equal input at an ancestor serves the row (ci_gate_check.py serve). It decides nothing: a red is printed and recorded.

    scripts/gate_runlog.py run --tier spec|merge|ship --lane <name> [--build build-linux] -- ctest -j8 ...
    scripts/gate_runlog.py clocks          # the GPU's clock state now (exit 3 when it reads locked)
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
    return os.environ.get("JAH_RUN_LOG_DIR") or os.path.join(workspace_root(), "testing", "runs")


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
    """{studio, irisgl, dirty, fork_dirty} from <build>/BUILT_FROM (cmake/BuiltFrom.cmake writes it as every build's last
    step)."""
    try:
        kv = dict(l.split("=", 1) for l in open(os.path.join(build, "BUILT_FROM")).read().splitlines() if "=" in l)
    except OSError:
        return None
    return {"studio": kv.get("studio", ""), "irisgl": kv.get("irisgl", ""), "dirty": kv.get("dirty") == "1",
            "fork_dirty": kv.get("fork_dirty") != "0"}


def stale_build(shas):
    """None when the record's build is its tip's; else why not."""
    b = (shas or {}).get("built")
    if not b:
        return "built from an UNKNOWN commit (no BUILT_FROM stamp)"
    if b.get("dirty"):
        return "built from a DIRTY tree"
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


def run_ctest(cmd, build, tier, lane, echo=True):
    """Run one ctest command (argv) verbose in `build`; append every row's records as it ends; return (rc, reds)."""
    if tier not in TIERS:
        raise ValueError(f"gate_runlog: tier '{tier}' is not one of {', '.join(TIERS)}")
    shas = tree_shas(_git(["rev-parse", "--show-toplevel"], build) or ROOT)
    shas["built"] = built_from(build)
    facts, config, config_why = row_facts(build)
    run_id = f"{datetime.datetime.now().strftime('%Y%m%dT%H%M%S')}-{shas['studio'][:9]}"
    display = os.environ.get("DISPLAY")
    vcmd = [cmd[0], "-V"] + [c for c in cmd[1:] if c != "--output-on-failure"]
    p = subprocess.Popen(vcmd, cwd=build, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1,
                         errors="replace")
    buf, size, pre, reds, lawbuf, late = {}, {}, {}, [], {}, {}
    deps = None   # the last `Failed test dependencies:` names, for the next result line
    for line in p.stdout:
        ln = line.rstrip("\n")
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
        base = {"schema": SCHEMA, "run": run_id, "tier": tier, "lanes": [lane], "tip": shas, "suite": name,
                "labels": sorted(f.get("labels") or []), "declaration": f.get("declaration"),
                "data_hash": f.get("data_hash"), "config": config, "display": display, "nulls": nulls,
                "ts": datetime.datetime.now().astimezone().isoformat(timespec="seconds"), "source": "run"}
        v = verdict_of(status)
        row = dict(base, arm=None, verdict=v, status=status.strip("* "), seconds=secs)
        # NOT RUN BEHIND ITS FIXTURE (URGENT #123): a row ctest did not run because a row it needs is red is RECORDED so,
        # the rows it waited on named - ci_gate_check.py serve reads it served-red while they stay red at this tree, so a
        # plan never lists it to run again for nothing (8B's master check re-ran its part seven times: ~25 min of card)
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
        recs = [row] + [dict(base, arm=a, verdict=av, status=av, seconds=s) for a, av, s in arms_of(text)]
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
    build = os.path.abspath(a.build)
    rc, _ = run_ctest(cmd, build, a.tier, a.lane)
    sys.exit(rc)


if __name__ == "__main__":
    main()
