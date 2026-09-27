#!/usr/bin/env python3
"""gate_runlog — THE RUN LOG (TESTING_V2 T8, lane MODULAR-GATE-1).

Every gate run appends ONE machine-readable record per suite (and per arm, once the pools
print arm verdicts) to `<workspace>/testing/runs/<date>-<tier>-<tip>.jsonl` — written by the
gate scripts themselves, never by hand, so the testing rules can be re-read against facts
(the owner, 2026-09-27: "log the tests as they run so we can streamline again based on fact").
The field list is `testing/runs/README.md`; the one writer is `run_ctest()` below.

As a library: gate-scope.py `--run` / `--solo` call run_ctest(). As a command (the tier
scripts — rc-gate.sh — and anyone running a whole tier):

    scripts/gate_runlog.py run --tier merge --lane rc-d --jobs 4 [--build build-linux] -- ctest -j4 ...
    scripts/gate_runlog.py import <ctest-output.log> --tier merge --tip <sha> --lane <name>
    scripts/gate_runlog.py times [--days 14]          # suite -> median PASS seconds (gate-scope's estimate)
    scripts/gate_runlog.py longest [--days 7] [-n 10] # the ten longest suites/arms this week
    scripts/gate_runlog.py load-reds [--days 7]       # red in a gate, green solo at the same tip

`run` streams ctest's output through (the caller still sees and may redirect every line),
samples the load average every 2 s, adds `--output-junit` to read each suite's own output
(`gpu_ms:` and `target:` lines, the pools' `ARM <name> PASS|FAIL|CRASH` lines) and exits with
ctest's exit code. `import` turns an existing ctest output log into records marked
`"source": "import"` (the before-run logs of a lane that predates the log).
"""
import argparse
import datetime
import json
import os
import re
import shlex
import statistics
import subprocess
import sys
import tempfile
import threading
import time
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCHEMA = 1

# `  12/653 Test  #45: gi.foo ..........   Passed   12.34 sec`
_RESULT = re.compile(r"^\s*\d+/\d+\s+Test\s+#\d+:\s+(\S+)\s+\.*\s*(.*?)\s+([\d.]+)\s+sec\s*$")
# The pools' arm lines (SUITE-POOL-1's runner, tests/support/run_pool.py): `ARM-BEGIN <pool>.<arm>`
# when an arm starts, `ARM <pool>.<arm> PASS|FAIL <ms> [why]` when it ends; an arm that began
# and never ended is a CRASH (the runner restarts the app and goes on).
_ARM = re.compile(r"^\s*ARM\s+(\S+)\s+(PASS|FAIL|CRASH|TIMEOUT|SKIP)\b(?:\s+(\d+(?:\.\d+)?)\s*(ms|s)?)?")
_ARM_BEGIN = re.compile(r"^\s*ARM-BEGIN\s+(\S+)\s*$")
_GPU = re.compile(r"^\s*gpu_ms:\s*([0-9.]+)")
_TARGET = re.compile(r"^\s*target:\s*(.+?)\s*$")


def verdict_of(status):
    s = status.lower()
    if "passed" in s: return "PASS"
    if "timeout" in s: return "TIMEOUT"
    if "not run" in s or "disabled" in s: return "NOTRUN"
    if "exception" in s or "segfault" in s or "abort" in s or "signal" in s: return "CRASH"
    return "FAIL"


def workspace_root():
    """The workspace that holds the docs repo: the parent of the Studio repo's main tree
    (`git rev-parse --git-common-dir` is <workspace>/jahshaka/.git for the main tree and every
    worktree, on the Linux box and on the Mac)."""
    try:
        cd = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
                            cwd=ROOT, capture_output=True, text=True).stdout.strip()
        if cd:
            return os.path.dirname(os.path.dirname(cd))
    except OSError:
        pass
    return os.path.dirname(ROOT)


def log_dir():
    return os.environ.get("JAH_RUN_LOG_DIR") or os.path.join(workspace_root(), "testing", "runs")


def _git(args, cwd=ROOT):
    try:
        r = subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True)
        return r.stdout.strip() if r.returncode == 0 else ""
    except OSError:
        return ""


def tree_shas():
    studio = _git(["rev-parse", "HEAD"])
    irisgl = _git(["rev-parse", "HEAD"], cwd=os.path.join(ROOT, "irisgl"))
    fork = _git(["rev-parse", "HEAD"], cwd=os.path.join(ROOT, "irisgl", "thirdparty", "ogre-next"))
    dirty = bool(_git(["status", "--porcelain", "--untracked-files=no", "--ignore-submodules=dirty"]))
    return {"studio": studio, "irisgl": irisgl, "fork": fork, "studio_dirty": dirty}


def gpu_clocks():
    """The GPU's clock state at the run: nvidia-smi exposes no 'locked' flag for
    --lock-gpu-clocks, so it is INFERRED from its signature — an idle GPU that does not clock
    down (measured on this box: 645 MHz idle free, 2550 MHz idle under `-lgc 2100,2550`).
    'locked?' = idle at >= 1500 MHz, 'free' = idle below it, 'busy' = not idle (cannot tell),
    'unknown' without nvidia-smi (the Mac, lavapipe CI). The raw numbers ride along."""
    try:
        r = subprocess.run(["nvidia-smi", "--query-gpu=clocks.gr,clocks_event_reasons.gpu_idle,pstate",
                            "--format=csv,noheader,nounits"], capture_output=True, text=True, timeout=10)
        if r.returncode != 0: return {"state": "unknown"}
        gr, idle, pstate = [p.strip() for p in r.stdout.strip().splitlines()[0].split(",")]
        mhz = int(float(gr))
        idle = idle.lower().startswith("active")
        state = ("locked?" if mhz >= 1500 else "free") if idle else "busy"
        return {"state": state, "graphics_mhz": mhz, "idle": idle, "pstate": pstate}
    except (OSError, subprocess.TimeoutExpired, ValueError, IndexError):
        return {"state": "unknown"}


def other_ctests():
    """ctest processes on the box that are not ours (the contention context of a run)."""
    try:
        r = subprocess.run(["pgrep", "-x", "ctest"], capture_output=True, text=True)
        me = os.getpid()
        return len([p for p in r.stdout.split() if int(p) != me])
    except OSError:
        return None


class LoadSampler(threading.Thread):
    def __init__(self, period=2.0):
        super().__init__(daemon=True)
        self.period, self.samples, self._stop = period, [], threading.Event()

    def run(self):
        while not self._stop.is_set():
            self.samples.append((time.time(), os.getloadavg()[0]))
            self._stop.wait(self.period)

    def stop(self):
        self._stop.set()

    def mean(self, t0, t1):
        v = [l for t, l in self.samples if t0 - self.period <= t <= t1 + self.period]
        return round(sum(v) / len(v), 2) if v else None


def _junit_outputs(path):
    out = {}
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError):
        return out
    for tc in root.iter("testcase"):
        so = tc.find("system-out")
        out[tc.get("name")] = so.text if so is not None and so.text else ""
    return out


def _suite_facts(text):
    gpu, target, arms, begun = None, None, [], []
    for line in (text or "").splitlines():
        m = _ARM_BEGIN.match(line)
        if m: begun.append(m.group(1)); continue
        m = _GPU.match(line)
        if m:
            try: gpu = float(m.group(1))
            except ValueError: pass
        m = _TARGET.match(line)
        if m and target is None: target = m.group(1)[:200]
        m = _ARM.match(line)
        if m:
            secs = None
            if m.group(3):
                secs = float(m.group(3)) if m.group(4) == "s" else float(m.group(3)) / 1000.0
            arms.append((m.group(1), m.group(2), secs))
    ended = {a for a, _, _ in arms}
    arms += [(a, "CRASH", None) for a in dict.fromkeys(begun) if a not in ended]
    return gpu, target, arms


def _file_for(tier, tip, date=None):
    d = date or datetime.date.today().isoformat()
    return os.path.join(log_dir(), f"{d}-{tier}-{(tip or 'notip')[:9]}.jsonl")


def _prior_counts(path):
    counts = {}
    try:
        with open(path) as f:
            for line in f:
                try: r = json.loads(line)
                except ValueError: continue
                k = (r.get("suite"), r.get("arm"))
                counts[k] = counts.get(k, 0) + 1
    except OSError:
        pass
    return counts


def append_records(records, tier, tip):
    path = _file_for(tier, tip)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    prior = _prior_counts(path)
    with open(path, "a") as f:
        for r in records:
            k = (r["suite"], r.get("arm"))
            r["retries"] = prior.get(k, 0)
            prior[k] = r["retries"] + 1
            f.write(json.dumps(r, sort_keys=True) + "\n")
    return path


def run_ctest(cmd, cwd, tier, lane, jobs, reasons=None, gating=None, rng=None, retry=False,
              labels=None, echo=True):
    """Run a ctest command line (a string, as gate-scope prints it), stream its output, and
    append one record per suite (+ per arm) to the run log. Returns ctest's exit code."""
    reasons = reasons or {}
    junit = tempfile.NamedTemporaryFile(prefix="gate-junit-", suffix=".xml", delete=False).name
    full = (f"{cmd} --output-junit {junit} "
            f"--test-output-size-passed 262144 --test-output-size-failed 262144")
    shas = tree_shas()
    box0 = {"jobs": jobs, "display": os.environ.get("DISPLAY"), "gpu_clocks": gpu_clocks(),
            "other_ctests": other_ctests(), "host": os.uname().nodename}
    sampler = LoadSampler(); sampler.start()
    run_id = f"{datetime.datetime.now().strftime('%Y%m%dT%H%M%S')}-{shas['studio'][:9]}"
    seen = []
    p = subprocess.Popen(full, cwd=cwd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, bufsize=1, errors="replace")
    for line in p.stdout:
        if echo:
            sys.stdout.write(line); sys.stdout.flush()
        m = _RESULT.match(line.rstrip("\n"))
        if m:
            seen.append((m.group(1), m.group(2), float(m.group(3)), time.time(), os.getloadavg()))
    rc = p.wait()
    sampler.stop()
    outputs = _junit_outputs(junit)
    try: os.unlink(junit)
    except OSError: pass
    recs = []
    for name, status, secs, t_end, load in seen:
        gpu, target, arms = _suite_facts(outputs.get(name, ""))
        base = {"schema": SCHEMA, "run": run_id, "ts": datetime.datetime.fromtimestamp(t_end).astimezone().isoformat(timespec="seconds"),
                "suite": name, "tier": tier, "lane": lane, "range": rng, "tip": shas,
                "reason": reasons.get(name, tier), "gating": (gating(name) if gating else True),
                "retry": retry, "labels": sorted((labels or {}).get(name, [])),
                "box": dict(box0, load=[round(x, 2) for x in load],
                            load_mean=sampler.mean(t_end - secs, t_end)),
                "source": "run"}
        recs.append(dict(base, arm=None, verdict=verdict_of(status), status=status.strip("* "),
                         seconds=secs, gpu_ms=gpu, target=target))
        for arm, v, s in arms:
            recs.append(dict(base, arm=arm, verdict=v, status=v, seconds=s, gpu_ms=None, target=None))
    if recs:
        path = append_records(recs, tier, shas["studio"])
        if echo:
            print(f"\nrun log: {len(recs)} record(s) -> {path}")
    return rc


def inventory_labels(build):
    """suite -> labels, read BEFORE the run (`ctest --show-only` truncates LastTest.log, so never
    after one whose log is still wanted)."""
    try:
        r = subprocess.run(["ctest", "--show-only=json-v1"], cwd=build, capture_output=True, text=True)
        out = {}
        for t in json.loads(r.stdout).get("tests", []):
            props = {p["name"]: p["value"] for p in t.get("properties", [])}
            out[t["name"]] = props.get("LABELS", []) or []
        return out
    except (OSError, ValueError):
        return {}


def import_log(path, tier, tip, lane, jobs=None, reason=None):
    recs = []
    for line in open(path, errors="replace"):
        m = _RESULT.match(line.rstrip("\n"))
        if not m: continue
        recs.append({"schema": SCHEMA, "run": f"import-{os.path.basename(path)}", "ts": None,
                     "suite": m.group(1), "arm": None, "tier": tier, "lane": lane, "range": None,
                     "tip": {"studio": tip}, "reason": reason or f"imported from {path}",
                     "gating": True, "retry": False, "labels": [],
                     "verdict": verdict_of(m.group(2)), "status": m.group(2).strip("* "),
                     "seconds": float(m.group(3)), "gpu_ms": None, "target": None,
                     "box": {"jobs": jobs}, "source": "import"})
    if recs:
        p = append_records(recs, tier, tip)
        print(f"imported {len(recs)} record(s) -> {p}")
    else:
        print("no ctest result lines in", path)


def median_times(days=14, verdicts=("PASS",)):
    """suite -> median wall seconds over the run log's last `days` days (PASS rows by default:
    a red's time is its failure's, not the suite's)."""
    d = log_dir()
    cutoff = (datetime.date.today() - datetime.timedelta(days=days)).isoformat()
    acc = {}
    if not os.path.isdir(d): return {}
    for f in sorted(os.listdir(d)):
        if not f.endswith(".jsonl") or f[:10] < cutoff: continue
        for line in open(os.path.join(d, f)):
            try: r = json.loads(line)
            except ValueError: continue
            if r.get("arm") or r.get("verdict") not in verdicts or r.get("seconds") is None: continue
            acc.setdefault(r["suite"], []).append(r["seconds"])
    return {k: statistics.median(v) for k, v in acc.items()}


def _records(days):
    d = log_dir()
    cutoff = (datetime.date.today() - datetime.timedelta(days=days)).isoformat()
    if not os.path.isdir(d): return
    for f in sorted(os.listdir(d)):
        if not f.endswith(".jsonl") or f[:10] < cutoff: continue
        for line in open(os.path.join(d, f)):
            try: r = json.loads(line)
            except ValueError: continue
            r["_file"] = f
            yield r


def query_longest(days=7, n=10):
    """The n longest suites/arms by median wall seconds (PASS runs) over the last `days` days."""
    acc = {}
    for r in _records(days):
        if r.get("verdict") != "PASS" or r.get("seconds") is None: continue
        k = r["suite"] if not r.get("arm") else f"{r['suite']} :: {r['arm']}"
        acc.setdefault(k, []).append(r["seconds"])
    rows = sorted(((statistics.median(v), len(v), k) for k, v in acc.items()), reverse=True)[:n]
    for med, cnt, k in rows: print(f"{med:8.1f} s  x{cnt:<3d} {k}")


def query_load_reds(days=7):
    """Suites that went red in a gate and green on a solo retry at the same tip: the contention
    class, with the load and the sibling gates each red ran beside."""
    by_file = {}
    for r in _records(days):
        by_file.setdefault(r["_file"], []).append(r)
    for f, recs in sorted(by_file.items()):
        for r in recs:
            if r.get("arm") or r.get("retry") or r.get("verdict") == "PASS": continue
            solo = [x for x in recs if x["suite"] == r["suite"] and x.get("retry") and not x.get("arm")]
            if solo and all(x["verdict"] == "PASS" for x in solo):
                b = r.get("box", {})
                print(f"{f[:10]} {r['suite']:45s} {r['verdict']:7s} load {b.get('load_mean')} "
                      f"(siblings {b.get('other_ctests')}, -j{b.get('jobs')})  solo {len(solo)}/{len(solo)} PASS")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run", help="run a ctest command and log every suite")
    r.add_argument("--tier", required=True)
    r.add_argument("--lane", default=None)
    r.add_argument("--jobs", type=int, default=None)
    r.add_argument("--build", default="build-linux")
    r.add_argument("ctest", nargs=argparse.REMAINDER)
    i = sub.add_parser("import", help="records from an existing ctest output log")
    i.add_argument("log"); i.add_argument("--tier", required=True); i.add_argument("--tip", required=True)
    i.add_argument("--lane", default=None); i.add_argument("--jobs", type=int, default=None)
    t = sub.add_parser("times", help="median PASS seconds per suite from the log")
    t.add_argument("--days", type=int, default=14)
    q = sub.add_parser("longest", help="the longest suites/arms by median PASS seconds")
    q.add_argument("--days", type=int, default=7); q.add_argument("-n", type=int, default=10)
    q2 = sub.add_parser("load-reds", help="red in a gate, green solo at the same tip")
    q2.add_argument("--days", type=int, default=7)
    a = ap.parse_args()
    if a.cmd == "run":
        cmd = a.ctest[1:] if a.ctest and a.ctest[0] == "--" else a.ctest
        if not cmd: ap.error("give the ctest command after --")
        build = a.build if os.path.isabs(a.build) else os.path.join(os.getcwd(), a.build) \
            if os.path.isdir(os.path.join(os.getcwd(), a.build)) else os.path.join(ROOT, a.build)
        lane = a.lane or _git(["rev-parse", "--abbrev-ref", "HEAD"])
        jobs = a.jobs
        if jobs is None:
            m = re.search(r"-j\s*(\d+)", " ".join(cmd)); jobs = int(m.group(1)) if m else None
        # one argument = a command string exactly as gate-scope.py --merge-tier prints it (its -LE
        # alternation carries `|`, so it must reach the shell as ONE string); several = an argv
        line = cmd[0] if len(cmd) == 1 else shlex.join(cmd)
        sys.exit(run_ctest(line, build, a.tier, lane, jobs, labels=inventory_labels(build)))
    if a.cmd == "import":
        import_log(a.log, a.tier, a.tip, a.lane, a.jobs); return
    if a.cmd == "longest":
        query_longest(a.days, a.n); return
    if a.cmd == "load-reds":
        query_load_reds(a.days); return
    if a.cmd == "times":
        for k, v in sorted(median_times(a.days).items()): print(f"{k} {v:.2f}")


if __name__ == "__main__":
    main()
