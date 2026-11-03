#!/usr/bin/env python3
"""width_pair.py - THE PAIRED WIDTH RUN (TEST-STREAMLINE-1 stage 4; card block C): may the small GPU rows share the card?

Today every GPU row takes all 8 vram slots (tests/support/test_boot.cmake JAH_SLOTS_*, URGENT #44), so GPU rows run one at a
time. #44's fault was a neighbour INFLATING a row's wall toward its 60-s cap (gi.card_lamp_shadow 11.18 s alone timed out at 60
s beside 24 GPU rows). This run decides the width by ONE paired measurement:

  THE ROWS   every GPU row (a RESOURCE_GROUPS vram claim) with NO TIMING LAW: not `measure`, not COLD (test-cold-rows.txt or
             JAHSHAKA_COLD_BY_SUBJECT: its subject is the cold compile), no armed millisecond bar (JAHSHAKA_TIMING_BARS=1),
             not one of TIMING_LAW_ROWS, not a DECLARED OPEN RED (a row in the first column of THE OPEN REDS table of
             SPECS/PHOTON_7_SPEC.md, --open-reds: the table spec.sh land reads, by the same rule; a red at width 1 judged
             against itself proves nothing about a width) - and priced under 10 s in scripts/gate-times.txt (the small rows;
             an unpriced row is left out, named). The fixture setups ctest adds are run with them and judged with them.
  THE ARMS   the SAME rows, the same tree and build, at width 1, 2 and 3 - width W = `ctest -j W` with a resource spec of
             W x 8 slots (`--resource-spec-file`, the build's own spec untouched), so W GPU rows hold the card at once -
             in a PALINDROME order (default 1,2,3,2,1, the lead's ruling for block C: widths 1 and 2 each twice, symmetric in time,
             so a drift of the box over the run cancels in their pairs; width 3's one run is judged on every bar below - its
             verdicts, OOM / Xid lines and walls against width 1's pair (the budgets, the 45-s cap, x1.5 width 1's median),
             its total wall against width 2's mean, its VRAM peak - one run against the paired widths under it;
             `--order 1,2,3,3,2,1` pairs every width).
  THE HOLDS  each (part, width) run is ONE hold of the card (`gpu-exclusive.sh --wait-max --label width-w<W>-p<k>`): the rows
             are packed into parts whose width-1 estimate (the price x RATIO) stays under PART_CAP_S, so no hold passes ~8
             min (CODING_RULES §7); the Unreal session's tickets fit between two holds.
  RECORDED   per run: the wall (the lock's wait included) and ctest's own "Total Test time" (the wall gain's), each row's
             verdict and its wall (ctest's own), the VRAM peak (nvidia-smi sampled every SAMPLE_MS, the card's used MB, counted
             only once the hold's command started; its first reading, before the rows start, beside it) and the card's
             memory.total, the out-of-memory lines of the run's OWN log (`ctest -V --output-log <run>/ctest.log`: every
             row's output - never the build's shared Testing/Temporary/LastTest.log, which any other ctest in the build
             truncates) and the Xid lines the lock prints; per row: its verdict, its FAIL lines and its wall against its
             price and the 60-s cap.
  THE TREE   the run holds the tree's check mark (URGENT #153: jah-check.pid in the tree's git dir, the mark spec.sh test
             and the pre-commit hook read) for its whole length - no other check runs and no commit lands in the tree
             while it measures; a live mark of another check REFUSES the run (exit 2) before any hold. The build's
             .ninja_log is stamped at the start and read before every hold and after the last: a build during the run
             VOIDS it (exit 1, named; spec.py's #153b rule - the mark cannot stop a `cmake --build`).
  THE RULE   (the spec's stage 4; the bars the lead ruled on the 4-5 audit, 2026-11-03) ADOPT THE WIDEST WIDTH whose every run
             gives every row the OUTCOME width 1 gave it (a green row PASS; a red row its verdict AND its FAIL lines, equal
             line for line - a law red at width 1 that turns into a device loss at width 3 is still FAIL, and is refused), with no out-of-device-memory line, and where every run of width W
             keeps EVERY ROW under its class budget (tests/hygiene/row_budget.py budget_of, the one definition: an engine
             fixture or a CPU row 10 s), under INFLATE x its width-1 wall (1.5: #44's fault was a row inflating toward its
             cap, and a width that inflates every row by one factor breaks the class budgets while every verdict holds; an
             excess under INFLATE_FLOOR_S, 0.5 s, is the walls' resolution, never an inflation) and
             under ROW_CAP_S (45 s: the quiet-box law under the 60-s cap); whose TOTAL WALL (ctest's own "Total Test time",
             never the lock's wait) is at least GAIN (15 %) under the next narrower width's (a wider width with no gain is
             more risk for nothing); and whose VRAM PEAK, sampled only while the hold runs, stays under VRAM_HEADROOM (90 %)
             of the card's memory.total (nvidia-smi; a peak or a total unread refuses the width - an unmeasured headroom is
             no headroom). A width is never adopted over a refused narrower one. Width 1 itself must agree with itself (its
             two runs) - a row whose verdict differs between them is a finding before any width is adopted (exit 1).
             Timing-law and cold rows stay alone whatever is adopted.

usage: width_pair.py <build> --display :NN [--plan] [--order 1,2,3,2,1] [--out <dir>] [--lock <cmd>] [--prices <file>]
       [--row-cap-s 45] [--part-cap-s 480] [--nvidia-smi <exe>] [--wait-max 600] [--budget-scale 1] [--open-reds <spec>]
       [--tree <dir>]
  --open-reds  the spec whose THE OPEN REDS table names the declared reds (default <workspace>/SPECS/PHOTON_7_SPEC.md;
               unreadable = refused, exit 2: an unread list is not an empty one)
  --tree       the tree whose check mark the run holds (default the build's CMAKE_HOME_DIRECTORY)
  --budget-scale  every class budget x this factor, in (0, 1] (the toys' seconds-long rows); above 1 refused (it loosens a
               law), anything but 1 refused on the card (the lock the tree's gpu-exclusive.sh)
  --plan    the rows, the exclusions with their reasons, the parts and the estimated card time - nothing run, no lock.
  exit      0 a width adopted (the line `ADOPT WIDTH <w>`); 1 width 1 unstable or faulting alone (an OOM / Xid line),
            the run incomplete or VOID; 2 refused (usage, a bad
            display, no rows); 75 the card stayed held (a hold's wait ran out: call again).
The evidence: <out>/runs.jsonl (one line per run), <out>/<run>/ctest.out + ctest.log, <out>/summary.tsv.
"""
import json
import os
import re
import shlex
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SLOTS_PER_ROW = 8          # tests/support/test_boot.cmake: every GPU class takes the card's 8 slots (#44)
SMALL_S = 10.0             # "the small rows": priced under 10 s (the spec's 151 rows priced under 10 s)
ROW_CAP_S = 45.0           # the adoption bar: no row past 45 s (CODING_RULES §5: under 45 s on a quiet box, the cap 60 s)
CAP_S = 60.0               # every row's cap (CLAUDE.md Testing)
PART_CAP_S = 480.0         # one hold's estimated wall at width 1 (the 8-min call estimate; a hold under ~10 min)
RATIO = 1.1                # price -> wall before a run measured one (lib/sprint.py SHIP_RATIO_DEFAULT)
INFLATE = 1.5              # a row's wall at width W over INFLATE x its width-1 wall refuses W (the lead's ruling, 2026-11-03)
INFLATE_FLOOR_S = 0.5      # ... when it is ALSO this much over it: the walls' resolution (ctest prints 0.01 s; a process start's
                           # jitter is tens of ms), so a fixture setup at 0.01 s read 0.02 s is not a x2 inflation - the set's
                           # setups (fresh_home 0.00-0.02 s) would refuse every width on noise; a row over 1 s is judged at x1.5
GAIN = 0.15                # width W's total wall at least 15 % under the next narrower width's, or W is refused (the same)
VRAM_HEADROOM = 0.90       # width W's VRAM peak under 90 % of the card's memory.total, or W is refused (the same)
SAMPLE_MS = 250
# A ROW WITH A TIMING LAW the labels cannot see: gi.walk_court's hitch law reads the frame's wall (the spec's stage 4: "only
# the 7 GPU measure rows and gi.walk_court carry timing laws").
TIMING_LAW_ROWS = {"gi.walk_court"}
OOM = re.compile(r"OUT_OF_DEVICE_MEMORY|OUT_OF_HOST_MEMORY|out of (device|video) memory", re.I)
XID = re.compile(r"\bXid\b")
RESULT = re.compile(r"^\s*\d+/\d+ Test\s+#\d+: (\S+) \.*\s*(.*?)\s+([\d.]+) sec\s*$")


def die(msg, rc=2):
    print(f"width_pair: {msg}", file=sys.stderr)
    sys.exit(rc)


def opt(args, name, default=None):
    if name in args:
        i = args.index(name)
        if i + 1 >= len(args):
            die(f"{name} needs a value")
        v = args[i + 1]
        del args[i:i + 2]
        return v
    return default


def prices(path):
    out = {}
    try:
        lines = open(path).read().splitlines()
    except OSError:
        return out
    for ln in lines:
        p = ln.split()
        if len(p) >= 2 and not ln.startswith("#"):
            try:
                out[p[0]] = float(p[1])
            except ValueError:
                pass
    return out


def inventory(build):
    """[(name, {property: value})] of the build, listed in a COPY of its CTestTestfile tree (a listing in the build truncates
    a running ctest's LastTest.log): the tree's scripts/gate_graph.ctest_inventory when present, else ctest in a copy."""
    sys.path.insert(0, HERE)
    try:
        import gate_graph  # noqa: E402
        raw, rc = gate_graph.ctest_inventory(build)
    except ImportError:
        import shutil
        import tempfile
        tmp = tempfile.mkdtemp(prefix="width-inv-")
        for dp, _, fs in os.walk(build):
            if "CTestTestfile.cmake" in fs:
                d = os.path.join(tmp, os.path.relpath(dp, build))
                os.makedirs(d, exist_ok=True)
                shutil.copy(os.path.join(dp, "CTestTestfile.cmake"), d)
        p = subprocess.run(["ctest", "--show-only=json-v1"], cwd=tmp, capture_output=True, text=True)
        raw, rc = p.stdout, p.returncode
        shutil.rmtree(tmp, ignore_errors=True)
    if rc != 0:
        die(f"cannot list the rows of {build} (ctest --show-only rc {rc})")
    rows = []
    for t in json.loads(raw).get("tests", []):
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        props["_command"] = t.get("command") or []      # budget_of reads it (an app row is the one whose command is the app)
        rows.append((t["name"], props))
    return rows


def cold_rows(build):
    f = os.path.join(build, "tests", "test-cold-rows.txt")
    try:
        return {ln.split("\t")[0] for ln in open(f) if ln.strip()}
    except OSError:
        return set()


def class_budgets(rows, scale=1.0):
    """{row: its class budget, s} - tests/hygiene/row_budget.py budget_of (the one definition source.row_budget and the
    stale-price refusal read), times `scale` (the toys)."""
    import importlib.util
    spm = importlib.util.spec_from_file_location("row_budget_of_tree",
                                                 os.path.join(os.path.dirname(HERE), "tests", "hygiene", "row_budget.py"))
    mod = importlib.util.module_from_spec(spm)
    spm.loader.exec_module(mod)
    return {n: mod.budget_of(n, p, p.get("_command") or ()) * scale for n, p in rows}


def vram_total(exe):
    """The card's memory.total in MB (nvidia-smi), or None when unread."""
    try:
        p = subprocess.run([exe, "--query-gpu=memory.total", "--format=csv,noheader,nounits"], capture_output=True,
                           text=True, timeout=30)
        return float(p.stdout.strip().splitlines()[0].split(",")[0])
    except (OSError, ValueError, IndexError, subprocess.TimeoutExpired):
        return None


def open_reds(path):
    """{row: its line} of THE OPEN REDS table of `path` (lib/land.py declared_rows' rule: the table under the LAST heading
    starting "## THE OPEN REDS", a row by its EXACT name in the first column), or None when the file is unreadable."""
    try:
        lines = open(path, errors="replace").read().splitlines()
    except OSError:
        return None
    starts = [i for i, ln in enumerate(lines) if ln.startswith("## THE OPEN REDS")]
    table = {}
    if starts:
        i = starts[-1] + 1
        while i < len(lines) and not lines[i].startswith("|"):
            i += 1
        for j in range(i, len(lines)):
            if not lines[j].startswith("|"):
                break
            cell = lines[j].strip("|").split("|")[0].strip()
            if cell and cell != "row" and not set(cell) <= set("-: "):
                table.setdefault(cell, j + 1)
    return table


def tree_of(build):
    """The build's source tree (CMakeCache.txt CMAKE_HOME_DIRECTORY), or None."""
    try:
        for ln in open(os.path.join(build, "CMakeCache.txt"), errors="replace"):
            if ln.startswith("CMAKE_HOME_DIRECTORY:"):
                return ln.split("=", 1)[1].strip()
    except OSError:
        pass
    return None


class CheckMark:
    """URGENT #153's mark (scripts/lead/lib/spec.py CheckMark: `<pid> <time>` in jah-check.pid of the tree's git dir), held
    for the run: spec.sh test refuses a second check beside it and the pre-commit hook refuses a commit. A live mark of
    another process refuses this run (SystemExit 2)."""

    def __init__(self, tree):
        p = subprocess.run(["git", "-C", tree, "rev-parse", "--absolute-git-dir"], capture_output=True, text=True)
        if p.returncode != 0 or not p.stdout.strip():
            die(f"--tree {tree}: not a git tree (the run holds its check mark, URGENT #153)")
        self.path = os.path.join(p.stdout.strip(), "jah-check.pid")

    def __enter__(self):
        try:
            words = open(self.path).read().split()
            other = int(words[0])
            os.kill(other, 0)
            if other != os.getpid():
                die(f"REFUSED: another check is running in this tree (pid {other}, since "
                    f"{words[1] if len(words) > 1 else '?'}; its mark {self.path}) - the width run measures alone in its tree")
        except (OSError, ValueError, IndexError):
            pass
        with open(self.path, "w") as f:
            f.write(f"{os.getpid()} {time.strftime('%Y-%m-%dT%H:%M:%S')}\n")
        return self

    def __exit__(self, *exc):
        try:
            if open(self.path).read().split()[0] == str(os.getpid()):
                os.remove(self.path)
        except (OSError, IndexError):
            pass
        return False


def _names(v):
    return [v] if isinstance(v, str) else list(v or [])


NEEDS_EXCLUDED = "its fixtures need an excluded row"


def select(rows, price, cold, declared=None):
    """(the rows, {reason: [rows]}, {row: [(the excluded row its fixtures need, its reason)]}) - THE ROWS of the docstring,
    each exclusion with its reason. `declared`: {row: line}. A row whose FIXTURE CLOSURE (the setup and cleanup rows of every
    fixture it requires, theirs in turn - what ctest adds to its run) holds an excluded row is left out too (the final CPU
    audit's finding 2: shadercache.app.warm brought shadercache.app.cold, the cold compile, into every hold of its part)."""
    sel, out, declared = [], {}, declared or {}
    why_of, props = {}, dict(rows)
    of_fixture = {}
    for n, p in rows:
        for key in ("FIXTURES_SETUP", "FIXTURES_CLEANUP"):
            for f in _names(p.get(key)):
                of_fixture.setdefault(f, []).append(n)
    for n, p in rows:
        if not p.get("RESOURCE_GROUPS"):
            continue                                    # takes no card: not a width question
        lab = set(p.get("LABELS") or [])
        env = " ".join(p.get("ENVIRONMENT") or [])
        why = ("measure" if "measure" in lab else
               "cold" if n in cold or p.get("JAHSHAKA_COLD_BY_SUBJECT") else
               "timing bars armed" if "JAHSHAKA_TIMING_BARS=1" in env else
               "a timing law" if n in TIMING_LAW_ROWS else
               "a declared open red" if n in declared else
               "unpriced" if n not in price else
               f"priced {SMALL_S:.0f} s or more" if price[n] >= SMALL_S else None)
        if why:
            out.setdefault(why, []).append(n)
            why_of[n] = why
        else:
            sel.append(n)
    needs = {}
    for n in sel:
        seen, todo = set(), list(_names(props[n].get("FIXTURES_REQUIRED")))
        while todo:
            f = todo.pop()
            if f in seen:
                continue
            seen.add(f)
            for m in of_fixture.get(f, []):
                if m != n and m in why_of:
                    needs.setdefault(n, []).append((m, why_of[m]))
                todo += _names(props[m].get("FIXTURES_REQUIRED"))
    for n in needs:
        out.setdefault(NEEDS_EXCLUDED, []).append(n)
    return sorted(m for m in sel if m not in needs), out, {n: sorted(set(v)) for n, v in needs.items()}


def parts(sel, price, cap):
    """The rows packed into parts whose width-1 estimate (price x RATIO) is under `cap`, longest first (each part balanced)."""
    total = sum(price[n] for n in sel) * RATIO
    k = max(1, int(-(-total // cap)))
    bins = [[0.0, []] for _ in range(k)]
    for n in sorted(sel, key=lambda n: -price[n]):
        b = min(bins, key=lambda b: b[0])
        b[0] += price[n] * RATIO
        b[1].append(n)
    return [sorted(b[1]) for b in bins if b[1]]


class VramSampler:
    """The card's used MB, sampled every SAMPLE_MS by ONE streaming nvidia-smi (`-lms`), its peak kept - counted only once
    `held` exists (the hold's command writes it as it starts): a sample taken while the lock is still waited for reads the
    PREVIOUS holder's memory (the Unreal session's, a sibling's), never this run's."""

    def __init__(self, exe, held):
        self.exe, self.held, self.peak, self.first, self.n, self.proc = exe, held, None, None, 0, None

    def __enter__(self):
        try:
            self.proc = subprocess.Popen([self.exe, "--query-gpu=memory.used", "--format=csv,noheader,nounits",
                                          f"-lms={SAMPLE_MS}"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        except OSError as e:
            print(f"width_pair: no VRAM sampler ({self.exe}: {e}) - the peak is unread", flush=True)
            return self
        threading.Thread(target=self._read, daemon=True).start()
        return self

    def _read(self):
        for ln in self.proc.stdout:
            try:
                v = float(ln.strip().split(",")[0])
            except (ValueError, IndexError):
                continue
            if not os.path.exists(self.held):
                continue
            self.n += 1
            self.first = v if self.first is None else self.first
            self.peak = v if self.peak is None else max(self.peak, v)

    def __exit__(self, *a):
        if self.proc:
            self.proc.terminate()
            try:
                self.proc.wait(5)
            except subprocess.TimeoutExpired:
                self.proc.kill()


RESULT_ID = re.compile(r"^\s*\d+/\d+ Test\s+#(\d+): (\S+) ")
VLINE = re.compile(r"^(\d+): ?(.*)$")
PREAMBLE = re.compile(r"^(Test command: |Working Directory: |Environment variables: ?$|Environment variable "
                      r"modifications: ?$|Test timeout computed to be: |  ?\S+=)")


def fail_lines(text):
    """A red row's FAIL lines in order (scripts/gate_runlog.py's pattern: our `FAIL: <what>`, a fixture's `  FAIL (3)
    <law>`, gtest's `[  FAILED  ]`, QTest's `FAIL!  :`), else its first script error line; [] when it printed none."""
    sys.path.insert(0, HERE)
    import gate_runlog  # noqa: E402
    got = [m.group(1).strip()[:300] for m in map(gate_runlog._FAILLINE.match, (text or "").splitlines()) if m]
    if not got:
        one = gate_runlog.fail_line(text)
        got = [one] if one else []
    return got


def verdicts(text):
    """{row: (outcome, wall s)} from ctest's own result lines; the outcome of a green row is "PASS", of a red one its
    verdict and its FAIL lines (`FAIL[<line> | <line>]`) - read from the run's -V log, every row's output by its test id."""
    out, body = {}, {}
    for ln in text.splitlines():
        m = VLINE.match(ln)
        if m:
            body.setdefault(m.group(1), []).append(m.group(2))
            continue
        m = RESULT.match(ln)
        if not m:
            continue
        s = m.group(2)
        v = ("PASS" if s.startswith("Passed") else "TIMEOUT" if "Timeout" in s else "NOTRUN" if "Not Run" in s else
             "SKIP" if "Skipped" in s else "FAIL" if "Failed" in s or "Exception" in s or "SEGFAULT" in s else "OTHER:" + s)
        if v != "PASS":
            mi = RESULT_ID.match(ln)
            txt = "\n".join(x for x in body.get(mi.group(1) if mi else "", []) if not PREAMBLE.match(x))
            fl = fail_lines(txt)
            v = f"{v}[{' | '.join(fl)}]" if fl else v
        out[m.group(1)] = (v, float(m.group(3)))
    return out


TOTAL_TIME = re.compile(r"^Total Test time \(real\) =\s*([\d.]+) sec", re.M)


def run_one(build, out, w, k, part, lock, display, wait_max, smi, total_mb=None):
    """One (width, part) run under one hold; its record."""
    tag = f"w{w}-p{k}-{time.strftime('%H%M%S')}"
    d = os.path.join(out, tag)
    os.makedirs(d, exist_ok=True)
    spec = os.path.join(d, "resources.json")
    json.dump({"version": {"major": 1, "minor": 0}, "local": [{"vram": [{"id": "card", "slots": SLOTS_PER_ROW * w}]}]},
              open(spec, "w"))
    lst = os.path.join(d, "rows.txt")
    open(lst, "w").write("\n".join(part) + "\n")
    held = os.path.join(d, "held")
    # the hold's command marks its start (the VRAM sampler counts from there), then IS the ctest
    cmd = lock + ["--wait-max", str(wait_max), "--label", f"width-w{w}-p{k}", "sh", "-c", ': > "$0"; exec "$@"', held,
                  "nice", "-n", "10", "ctest", "-V", "--output-log", os.path.join(d, "ctest.log"),
                  "--test-dir", build, "-j", str(w), "--resource-spec-file", spec, "--tests-from-file", lst]
    env = dict(os.environ, DISPLAY=display)
    print(f"width_pair: width {w} part {k}: {len(part)} rows - {' '.join(shlex.quote(c) for c in cmd)}", flush=True)
    t0 = time.monotonic()
    with VramSampler(smi, held) as vs:
        p = subprocess.run(cmd, env=env, capture_output=True, text=True)
    wall = time.monotonic() - t0
    text = p.stdout + p.stderr
    open(os.path.join(d, "ctest.out"), "w").write(text)
    # THE RUN's OWN LOG (ctest --output-log, -V: every row's output) - never the build's shared LastTest.log
    lp = os.path.join(d, "ctest.log")
    log = open(lp, errors="replace").read() if os.path.exists(lp) else ""
    v = verdicts(log or text)
    tt = TOTAL_TIME.search(text)
    rec = {"tag": tag, "width": w, "part": k, "rc": p.returncode, "held": p.returncode != 75, "wall_s": round(wall, 2),
           "ctest_wall_s": float(tt.group(1)) if tt else None,
           "vram_idle_mb": vs.first, "vram_peak_mb": vs.peak, "vram_samples": vs.n, "vram_total_mb": total_mb,
           "log": lp if log else None,
           "oom": sorted({ln.strip()[:200] for ln in (log or text).splitlines() if OOM.search(ln)})[:20],
           "xid": sorted({ln.strip()[:200] for ln in text.splitlines() if XID.search(ln)})[:20],
           "rows": {n: list(x) for n, x in v.items()}, "asked": part}
    with open(os.path.join(out, "runs.jsonl"), "a") as fh:
        fh.write(json.dumps(rec) + "\n")
    print(f"width_pair: width {w} part {k}: rc {p.returncode}, {wall:.1f} s (ctest {rec['ctest_wall_s']} s), {len(v)} verdicts, VRAM peak "
          f"{vs.peak if vs.peak is not None else '?'} MB (first {vs.first if vs.first is not None else '?'}), "
          f"{len(rec['oom'])} OOM line(s), {len(rec['xid'])} Xid line(s)", flush=True)
    return rec


def _median(xs):
    xs = sorted(xs)
    return (xs[len(xs) // 2] if len(xs) % 2 else (xs[len(xs) // 2 - 1] + xs[len(xs) // 2]) / 2) if xs else None


def total_wall(rs):
    """A width's TOTAL WALL: ctest's own time of every part, summed per pass and averaged over its passes (None when a run
    has no reading)."""
    if not rs or any(r.get("ctest_wall_s") is None for r in rs):
        return None
    parts = len({r["part"] for r in rs})
    return sum(r["ctest_wall_s"] for r in rs) * parts / len(rs)


def ninja_stamp(build):
    """(size, mtime_ns) of the build's .ninja_log, or None - it moves when and only when ninja runs a step (spec.py
    _ninja_stamp, URGENT #153b)."""
    try:
        st = os.stat(os.path.join(build, ".ninja_log"))
        return (st.st_size, st.st_mtime_ns)
    except OSError:
        return None


def judge(recs, price, row_cap, budget=None, inflate=INFLATE, gain=GAIN, headroom=VRAM_HEADROOM):
    """(the adopted width or None, [lines], {row: why no width covers it}) - THE RULE of the docstring, every reason printed.
    `budget`: {row: class budget s}."""
    budget = budget or {}
    lines, by_w = [], {}
    for r in recs:
        by_w.setdefault(r["width"], []).append(r)
    widths = sorted(by_w)
    # every run's verdict per row, per width
    seen = {}
    for r in recs:
        for n, (v, s) in r["rows"].items():
            seen.setdefault(n, {}).setdefault(r["width"], []).append((v, s))
    missing = [r for r in recs if not r["held"] or any(n not in r["rows"] for n in r["asked"])]
    for r in missing:
        lines.append(f"INCOMPLETE {r['tag']}: " + ("the card stayed held (exit 75)" if not r["held"] else
                     "no verdict for " + ", ".join(n for n in r["asked"] if n not in r["rows"])))
    if 1 not in by_w:
        return None, lines + ["NO WIDTH 1 RUN: nothing to compare against"], {}
    unstable = sorted(n for n, w in seen.items() if len({v for v, _ in w.get(1, [])}) > 1)
    for n in unstable:
        lines.append(f"WIDTH 1 DISAGREES WITH ITSELF: {n} " + " / ".join(v for v, _ in seen[n][1]))
    # A ROW's OWN FAULT ALONE (the final CPU audit's LOW): an out-of-memory or Xid line in a WIDTH-1 run is a row alone on the
    # card - a budget finding or a device fault (GATE_AND_RIG: never environmental), no width question; judged at width 2+
    # only, it refused every wider width and adopted width 1 with the fault unnamed.
    w1fault = [ln for r in by_w[1] for ln in r["oom"] + r["xid"]]
    if w1fault:
        lines.append(f"WIDTH 1 HAS {len(w1fault)} OUT-OF-MEMORY / Xid LINE(S) WITH ONE ROW ON THE CARD: {w1fault[0]} - a row's "
                     f"own fault (a budget finding or a device fault, never environmental), fixed before any width is judged")
    # THE CLASS BUDGET AT WIDTH 1 (the final CPU audit's finding 1): a row past its class budget ALONE is a stale price or a
    # budget red (card block D's), never the width's fault - judged at width 2 only, it refused every wider width with a wrong
    # cause. It is named here and left out of every width's judgement, and no adopted width covers it.
    over1 = {}
    for n in sorted(seen):
        b, w1walls = budget.get(n), [s for _, s in seen[n].get(1, [])]
        if b is not None and w1walls and max(w1walls) > b:
            over1[n] = max(w1walls)
            lines.append(f"OVER ITS CLASS BUDGET AT WIDTH 1: {n} ran {over1[n]:.2f} s alone, past its {b:g} s (priced "
                         f"{price.get(n, 0):.2f} s) - a stale price or a budget red, reported and left out of every width's "
                         f"judgement (no width covers it)")
    ok_w = [1]
    for i, w in enumerate(widths):
        if w == 1:
            continue
        why = []
        for n in sorted(seen):
            if n in over1:
                continue
            base = {v for v, _ in seen[n].get(1, [])}
            got = [v for v, _ in seen[n].get(w, [])]
            if got and (len(base) != 1 or any(v not in base for v in got)):
                why.append(f"{n} {'/'.join(sorted(base))} at width 1, {'/'.join(got)} at width {w}")
            walls = [s for _, s in seen[n].get(w, [])]
            slow = [s for s in walls if s > row_cap]
            if slow:
                why.append(f"{n} ran {max(slow):.1f} s at width {w}, past {row_cap:.0f} s (priced {price.get(n, 0):.2f} s)")
            b = budget.get(n)
            if b is not None and walls and max(walls) > b:
                why.append(f"{n} ran {max(walls):.2f} s at width {w}, past its class budget {b:g} s")
            w1 = _median([s for _, s in seen[n].get(1, [])])
            if w1 is not None and walls and max(walls) > inflate * w1 and max(walls) - w1 > INFLATE_FLOOR_S:
                why.append(f"{n} ran {max(walls):.2f} s at width {w}, x{max(walls) / w1:.2f} its width-1 wall {w1:.2f} s "
                           f"(the bar x{inflate:g})")
        oom = [ln for r in by_w[w] for ln in r["oom"]]
        if oom:
            why.append(f"{len(oom)} out-of-memory line(s): {oom[0]}")
        xid = [ln for r in by_w[w] for ln in r["xid"]]
        if xid:
            why.append(f"{len(xid)} Xid line(s): {xid[0]}")
        if any(r in missing for r in by_w[w]):
            why.append("a run incomplete")
        # THE WALL GAIN over the next narrower width that ran
        prev = widths[i - 1]
        tw, tp = total_wall(by_w[w]), total_wall(by_w[prev])
        if tw is None or tp is None:
            why.append(f"the total wall unread at width {w if tw is None else prev} (no 'Total Test time' line)")
        elif tw > (1.0 - gain) * tp:
            why.append(f"total wall {tw:.2f} s, {100 * (1 - tw / tp):.0f} % under width {prev}'s {tp:.2f} s "
                       f"(the bar: {100 * gain:.0f} % under)")
        # THE VRAM HEADROOM
        peaks = [r.get("vram_peak_mb") for r in by_w[w]]
        total = next((r.get("vram_total_mb") for r in by_w[w] if r.get("vram_total_mb")), None)
        if total is None or any(pk is None for pk in peaks):
            why.append("the VRAM " + ("total" if total is None else "peak") + " unread (nvidia-smi): no headroom measured")
        elif max(peaks) >= headroom * total:
            why.append(f"VRAM peak {max(peaks):.0f} MB, past {100 * headroom:.0f} % of the card's {total:.0f} MB")
        if why:
            lines.append(f"WIDTH {w} REFUSED: " + "; ".join(why))
        else:
            ok_w.append(w)
    # the adopted width: the widest whose every narrower width also held (a width is never adopted over a refused one)
    adopt = 1
    for w in widths:
        if w in ok_w:
            adopt = w
        else:
            break
    uncovered = {n: f"over its class budget at width 1 ({over1[n]:.2f} s alone)" for n in over1}
    if unstable or w1fault or any(r in missing for r in by_w[1]):
        return None, lines, uncovered
    return adopt, lines, uncovered


def summary(recs, price, out):
    """The per-width table: wall, VRAM peak, the rows' inflation against their price toward the cap."""
    rows = ["width\truns\twall_s(each)\tvram_peak_mb\tvram_first_mb\tworst_row\tworst_s\tworst_price_s\tmedian_ratio"]
    lines = []
    for w in sorted({r["width"] for r in recs}):
        rs = [r for r in recs if r["width"] == w]
        walls = "+".join(f"{r['wall_s']:.0f}" for r in rs)
        peak = max((r["vram_peak_mb"] for r in rs if r["vram_peak_mb"] is not None), default=None)
        first = min((r["vram_idle_mb"] for r in rs if r["vram_idle_mb"] is not None), default=None)
        ratios, worst = [], ("-", 0.0)
        for r in rs:
            for n, (v, s) in r["rows"].items():
                if price.get(n):
                    ratios.append(s / price[n])
                if s > worst[1]:
                    worst = (n, s)
        ratios.sort()
        med = ratios[len(ratios) // 2] if ratios else 0.0
        tw = total_wall(rs)
        rows.append(f"{w}\t{len(rs)}\t{walls}\t{peak}\t{first}\t{worst[0]}\t{worst[1]:.2f}\t{price.get(worst[0], 0):.2f}\t{med:.2f}")
        lines.append(f"  width {w}: {len(rs)} run(s), wall {walls} s (ctest's total a pass {tw if tw is None else round(tw, 2)} s); VRAM peak {peak} MB (first {first}); slowest row "
                     f"{worst[0]} {worst[1]:.1f} s (priced {price.get(worst[0], 0):.2f} s, cap {CAP_S:.0f} s); median "
                     f"wall/price x{med:.2f}")
    open(os.path.join(out, "summary.tsv"), "w").write("\n".join(rows) + "\n")
    return lines


def main(argv):
    args = list(argv)
    plan = "--plan" in args
    if plan:
        args.remove("--plan")
    display = opt(args, "--display")
    order = [int(x) for x in opt(args, "--order", "1,2,3,2,1").split(",")]
    out = opt(args, "--out")
    lock = shlex.split(opt(args, "--lock", os.path.join(HERE, "gpu-exclusive.sh")))
    pfile = opt(args, "--prices", os.path.join(HERE, "gate-times.txt"))
    row_cap = float(opt(args, "--row-cap-s", ROW_CAP_S))
    part_cap = float(opt(args, "--part-cap-s", PART_CAP_S))
    smi = opt(args, "--nvidia-smi", "nvidia-smi")
    wait_max = int(opt(args, "--wait-max", "600"))
    bscale = float(opt(args, "--budget-scale", "1"))
    # THE CLASS BUDGET IS THE LAW (the final CPU audit's LOW): a scale above 1 loosens it - refused everywhere; on the card (the
    # tree's own gpu-exclusive.sh as the lock) the budget is exactly budget_of's - the scale is the toys' knob only
    if not 0 < bscale <= 1:
        die(f"--budget-scale {bscale:g}: above 1 (or not positive) loosens every class budget - a toy's knob, in (0, 1]")
    if bscale != 1 and os.path.realpath(lock[0]) == os.path.realpath(os.path.join(HERE, "gpu-exclusive.sh")):
        die(f"--budget-scale {bscale:g} on the card (the lock {lock[0]}): the class budget is the law there, the scale is 1")
    reds_file = opt(args, "--open-reds")
    tree = opt(args, "--tree")
    if len(args) != 1 or args[0].startswith("-"):
        die("usage: width_pair.py <build> --display :NN [--plan] [--order 1,2,3,2,1] [--out <dir>] [--lock <cmd>] "
            "[--prices <file>] [--row-cap-s 45] [--part-cap-s 480] [--nvidia-smi <exe>] [--wait-max 600] [--budget-scale 1] "
            "[--open-reds <spec>] [--tree <dir>]")
    build = os.path.abspath(args[0])
    if 1 not in order or any(w < 1 for w in order):
        die(f"--order {order}: the widths are >= 1 and width 1 is in it (the base every width is judged against)")
    price = prices(pfile)
    if not price:
        die(f"no prices read from {pfile} (scripts/gate-times.txt: a row's price is what makes it small)")
    if not reds_file:
        sys.path.insert(0, HERE)
        import gate_runlog  # noqa: E402
        reds_file = os.path.join(gate_runlog.workspace_root(), "SPECS", "PHOTON_7_SPEC.md")
    declared = open_reds(reds_file)
    if declared is None:
        die(f"--open-reds {reds_file}: unreadable - the declared open reds leave the set, and an unread list is not an empty one")
    inv = inventory(build)
    sel, excl, needs = select(inv, price, cold_rows(build), declared)
    ps = parts(sel, price, part_cap) if sel else []
    est = sum(sum(price[n] for n in p) * RATIO / w for w in order for p in ps)
    print(f"width_pair: {len(sel)} rows with no timing law priced under {SMALL_S:.0f} s ({sum(price[n] for n in sel):.0f} s "
          f"serial), {len(ps)} part(s) under {part_cap:.0f} s each at width 1; order {','.join(map(str, order))}; "
          f"{len(ps) * len(order)} holds, ~{est / 60:.0f} min of card estimated (the width-W run at 1/W of width 1's)")
    for why, ns in sorted(excl.items()):
        if why == "a declared open red":   # always by name, each with its table line: a red leaves a set only with its reason
            print(f"  left out, {why} (THE OPEN REDS of {reds_file}; a red at width 1 judged against itself proves nothing): "
                  f"{len(ns)}: " + " ".join(f"{n} (:{declared[n]})" for n in sorted(ns)))
            continue
        if why == NEEDS_EXCLUDED:          # always by name, each with the excluded row ctest would add to its hold
            print(f"  left out, {why} (ctest adds a required fixture's setup and cleanup to the run): {len(ns)}: " +
                  " ".join(f"{n} (needs " + ", ".join(f"{m}: {r}" for m, r in needs[n]) + ")" for n in sorted(ns)))
            continue
        print(f"  left out, {why}: {len(ns)}" + (": " + " ".join(sorted(ns)) if len(ns) <= 12 else ""))
    for k, p in enumerate(ps, 1):
        print(f"  part {k}: {len(p)} rows ~{sum(price[n] for n in p) * RATIO:.0f} s at width 1")
    budget = class_budgets([(n, p) for n, p in inv if n in set(sel)], bscale)
    tight = sorted(n for n in sel if price[n] > budget[n] / INFLATE)
    if tight:   # for these the class budget, not the x1.5 bar, binds at a wider width (the final CPU audit's finding 1)
        print(f"  priced over their class budget / {INFLATE:g} (the budget binds before the x{INFLATE:g} bar at width 2+): "
              f"{len(tight)}: " + " ".join(f"{n} {price[n]:.2f}/{budget[n]:g}" for n in tight))
    cls = sorted({p.get("JAH_CLASS", "?") for n, p in inv if n in set(sel)})
    boots = [b for c, bs in (("engine", ("engine-off", "engine-on")), ("app", ("app-off", "app-on"))) if c in cls for b in bs]
    print(f"  the classes {', '.join(cls)}: THE WARM-UP FIRST, one call each (GATE_AND_RIG 'the warm-up before an app row'): "
          + "; ".join(f"scripts/warm-app-cache.sh {build} --only {b}" for b in boots))
    if not sel:
        die("no row to run (no GPU row with no timing law priced under 10 s)")
    if plan:
        return 0
    if not display or not re.fullmatch(r":(6[0-9]|[7-9][0-9])", display):
        die(f"--display {display!r}: a rig display in :60-:99 (CODING_RULES §7; never :0, never unset)")
    tree = tree or tree_of(build)
    if not tree:
        die(f"{build}: no CMAKE_HOME_DIRECTORY in its CMakeCache.txt - name the tree (--tree): the run holds its check mark")
    with CheckMark(tree):
        return measure(build, out, order, ps, budget, price, smi, lock, display, wait_max, row_cap, excl)


def measure(build, out, order, ps, budget, price, smi, lock, display, wait_max, row_cap, excl):
    """The holds, the summary and THE RULE (main's, under the tree's check mark)."""
    out = os.path.abspath(out or os.path.join(build, "tests", ".width-pair", time.strftime("%Y%m%d-%H%M%S")))
    os.makedirs(out, exist_ok=True)
    total_mb = vram_total(smi)
    print(f"width_pair: the card's memory.total {total_mb if total_mb is not None else 'UNREAD'} MB (the headroom bar "
          f"{100 * VRAM_HEADROOM:.0f} %)", flush=True)
    recs = []
    built0 = ninja_stamp(build)

    def rebuilt(where):
        """THE BUILD GUARD (the final CPU audit's finding 4; spec.py's #153b rule): the check mark stops a commit and a second
        check, never a `cmake --build`; a build mid-run would judge width 1 and width 3 on different binaries."""
        if ninja_stamp(build) == built0:
            return False
        print(f"width_pair: VOID {where}: the tree's build ran during the width run ({build}/.ninja_log moved) - the widths "
              f"would be judged on different binaries; the records so far are kept, nothing is judged; never build a tree "
              f"while its check runs (exit 1)", flush=True)
        return True

    for w in order:
        for k, p in enumerate(ps, 1):
            if rebuilt(f"before width {w} part {k}"):
                return 1
            r = run_one(build, out, w, k, p, lock, display, wait_max, smi, total_mb)
            recs.append(r)
            if not r["held"]:
                print("width_pair: the card stayed held - stopped; the records so far are kept, call again (exit 75)")
                return 75
    if rebuilt("after the last hold"):
        return 1
    for ln in summary(recs, price, out):
        print(ln)
    adopt, lines, uncovered = judge(recs, price, row_cap, budget)
    for ln in lines:
        print("  " + ln)
    if adopt is None:
        print(f"width_pair: NO WIDTH ADOPTED - width 1 unstable or a run incomplete (evidence {out})")
        return 1
    # WHAT THE WIDTH COVERS (the final CPU audit's finding 3): ONLY the rows it measured - the rows every run judged, less the
    # rows over their budget alone. Every other GPU row (a declared open red until it is green and measured, a timing-law,
    # cold or measure row, a row priced 10 s or more, unpriced, or needing an excluded row's fixture) keeps the WHOLE card
    # (8 x W slots: alone) - its VRAM and its neighbours were never measured. The registration commit reads these two files.
    judged = set.intersection(*[set(r["rows"]) for r in recs]) if recs else set()
    asked = {n for r in recs for n in r["asked"]}         # the GPU rows (a card-free fixture setup ctest added is no width's)
    covered = sorted(n for n in judged & asked if n not in uncovered)
    alone = dict(uncovered)
    for why, ns in excl.items():
        for n in ns:
            alone.setdefault(n, why)
    open(os.path.join(out, "adopted-rows.txt"), "w").write("".join(f"{n}\n" for n in covered))
    open(os.path.join(out, "alone-rows.txt"), "w").write("".join(f"{n}\t{alone[n]}\n" for n in sorted(alone)))
    print(f"width_pair: width {adopt} covers the {len(covered)} rows it measured ({out}/adopted-rows.txt) and no other: "
          f"{len(alone)} GPU rows keep the whole card, 8 x W slots, alone ({out}/alone-rows.txt - the declared open reds until "
          f"green and measured, the timing-law, cold and measure rows, the rows over their budget alone, the 10-s rows)")
    print(f"width_pair: ADOPT WIDTH {adopt} (the widest with width 1's verdicts in every run, no out-of-memory, every row under "
          f"its class budget, x{INFLATE:g} its width-1 wall and {row_cap:.0f} s, {100 * GAIN:.0f} % of wall gained per width, "
          f"the VRAM peak under {100 * VRAM_HEADROOM:.0f} %; evidence {out})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
