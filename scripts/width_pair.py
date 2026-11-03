#!/usr/bin/env python3
"""width_pair.py - THE PAIRED WIDTH RUN (TEST-STREAMLINE-1 stage 4; card block C): may the small GPU rows share the card?

Today every GPU row takes all 8 vram slots (tests/support/test_boot.cmake JAH_SLOTS_*, URGENT #44), so GPU rows run one at a
time. #44's fault was a neighbour INFLATING a row's wall toward its 60-s cap (gi.card_lamp_shadow 11.18 s alone timed out at 60
s beside 24 GPU rows). This run decides the width by ONE paired measurement:

  THE ROWS   every GPU row (a RESOURCE_GROUPS vram claim) with NO TIMING LAW: not `measure`, not COLD (test-cold-rows.txt or
             JAHSHAKA_COLD_BY_SUBJECT: its subject is the cold compile), no armed millisecond bar (JAHSHAKA_TIMING_BARS=1),
             not one of TIMING_LAW_ROWS - and priced under 10 s in scripts/gate-times.txt (the small rows; an unpriced row
             is left out, named). The fixture setups ctest adds are run with them and judged with them.
  THE ARMS   the SAME rows, the same tree and build, at width 1, 2 and 3 - width W = `ctest -j W` with a resource spec of
             W x 8 slots (`--resource-spec-file`, the build's own spec untouched), so W GPU rows hold the card at once -
             in ABBA order (default 1,2,3,3,2,1: each width twice, symmetric in time, so a drift of the box over the run
             cancels in each width's pair).
  THE HOLDS  each (part, width) run is ONE hold of the card (`gpu-exclusive.sh --wait-max --label width-w<W>-p<k>`): the rows
             are packed into parts whose width-1 estimate (the price x RATIO) stays under PART_CAP_S, so no hold passes ~8
             min (CODING_RULES §7); the Unreal session's tickets fit between two holds.
  RECORDED   per run: the wall, each row's verdict and its wall (ctest's own), the VRAM peak (nvidia-smi sampled every
             SAMPLE_MS, the card's used MB; the idle reading before the hold's rows start beside it), the out-of-memory lines
             and Xid lines of the run's log; per row: its wall against its price and the 60-s cap.
  THE RULE   (the spec's stage 4) ADOPT THE WIDEST WIDTH whose every run gives every row the verdict width 1 gave it, with no
             out-of-device-memory line and no row past ROW_CAP_S (45 s: the quiet-box law under the 60-s cap). Width 1 itself
             must agree with itself (its two runs) - a row whose verdict differs between them is a finding before any width is
             adopted (exit 1). Timing-law and cold rows stay alone whatever is adopted.

usage: width_pair.py <build> --display :NN [--plan] [--order 1,2,3,3,2,1] [--out <dir>] [--lock <cmd>] [--prices <file>]
       [--row-cap-s 45] [--part-cap-s 480] [--nvidia-smi <exe>] [--wait-max 600]
  --plan    the rows, the exclusions with their reasons, the parts and the estimated card time - nothing run, no lock.
  exit      0 a width adopted (the line `ADOPT WIDTH <w>`); 1 width 1 unstable or the run incomplete; 2 refused (usage, a bad
            display, no rows); 75 the card stayed held (a hold's wait ran out: call again).
The evidence: <out>/runs.jsonl (one line per run), <out>/<run>/ctest.out + LastTest.log, <out>/summary.tsv.
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
        rows.append((t["name"], {p["name"]: p["value"] for p in t.get("properties", [])}))
    return rows


def cold_rows(build):
    f = os.path.join(build, "tests", "test-cold-rows.txt")
    try:
        return {ln.split("\t")[0] for ln in open(f) if ln.strip()}
    except OSError:
        return set()


def select(rows, price, cold):
    """(the rows, {reason: [rows]}) - THE ROWS of the docstring, each exclusion with its reason."""
    sel, out = [], {}
    for n, p in rows:
        if not p.get("RESOURCE_GROUPS"):
            continue                                    # takes no card: not a width question
        lab = set(p.get("LABELS") or [])
        env = " ".join(p.get("ENVIRONMENT") or [])
        why = ("measure" if "measure" in lab else
               "cold" if n in cold or p.get("JAHSHAKA_COLD_BY_SUBJECT") else
               "timing bars armed" if "JAHSHAKA_TIMING_BARS=1" in env else
               "a timing law" if n in TIMING_LAW_ROWS else
               "unpriced" if n not in price else
               f"priced {SMALL_S:.0f} s or more" if price[n] >= SMALL_S else None)
        if why:
            out.setdefault(why, []).append(n)
        else:
            sel.append(n)
    return sorted(sel), out


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
    """The card's used MB, sampled every SAMPLE_MS by ONE streaming nvidia-smi (`-lms`), its peak kept."""

    def __init__(self, exe):
        self.exe, self.peak, self.first, self.n, self.proc = exe, None, None, 0, None

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


def verdicts(text):
    """{row: (verdict, wall s)} from ctest's own result lines."""
    out = {}
    for ln in text.splitlines():
        m = RESULT.match(ln)
        if not m:
            continue
        s = m.group(2)
        v = ("PASS" if s.startswith("Passed") else "TIMEOUT" if "Timeout" in s else "NOTRUN" if "Not Run" in s else
             "SKIP" if "Skipped" in s else "FAIL" if "Failed" in s or "Exception" in s or "SEGFAULT" in s else "OTHER:" + s)
        out[m.group(1)] = (v, float(m.group(3)))
    return out


def run_one(build, out, w, k, part, lock, display, wait_max, smi):
    """One (width, part) run under one hold; its record."""
    tag = f"w{w}-p{k}-{time.strftime('%H%M%S')}"
    d = os.path.join(out, tag)
    os.makedirs(d, exist_ok=True)
    spec = os.path.join(d, "resources.json")
    json.dump({"version": {"major": 1, "minor": 0}, "local": [{"vram": [{"id": "card", "slots": SLOTS_PER_ROW * w}]}]},
              open(spec, "w"))
    lst = os.path.join(d, "rows.txt")
    open(lst, "w").write("\n".join(part) + "\n")
    cmd = lock + ["--wait-max", str(wait_max), "--label", f"width-w{w}-p{k}", "nice", "-n", "10", "ctest",
                  "--test-dir", build, "-j", str(w), "--resource-spec-file", spec, "--tests-from-file", lst]
    env = dict(os.environ, DISPLAY=display)
    print(f"width_pair: width {w} part {k}: {len(part)} rows - {' '.join(shlex.quote(c) for c in cmd)}", flush=True)
    t0 = time.monotonic()
    with VramSampler(smi) as vs:
        p = subprocess.run(cmd, env=env, capture_output=True, text=True)
    wall = time.monotonic() - t0
    text = p.stdout + p.stderr
    open(os.path.join(d, "ctest.out"), "w").write(text)
    log = ""
    lt = os.path.join(build, "Testing", "Temporary", "LastTest.log")
    if p.returncode != 75 and os.path.exists(lt):
        log = open(lt, errors="replace").read()
        open(os.path.join(d, "LastTest.log"), "w").write(log)
    v = verdicts(text)
    rec = {"tag": tag, "width": w, "part": k, "rc": p.returncode, "held": p.returncode != 75, "wall_s": round(wall, 2),
           "vram_idle_mb": vs.first, "vram_peak_mb": vs.peak, "vram_samples": vs.n,
           "oom": sorted({ln.strip()[:200] for ln in (text + log).splitlines() if OOM.search(ln)})[:20],
           "xid": sorted({ln.strip()[:200] for ln in text.splitlines() if XID.search(ln)})[:20],
           "rows": {n: list(x) for n, x in v.items()}, "asked": part}
    with open(os.path.join(out, "runs.jsonl"), "a") as fh:
        fh.write(json.dumps(rec) + "\n")
    print(f"width_pair: width {w} part {k}: rc {p.returncode}, {wall:.1f} s, {len(v)} verdicts, VRAM peak "
          f"{vs.peak if vs.peak is not None else '?'} MB (first {vs.first if vs.first is not None else '?'}), "
          f"{len(rec['oom'])} OOM line(s), {len(rec['xid'])} Xid line(s)", flush=True)
    return rec


def judge(recs, price, row_cap):
    """(the adopted width or None, [lines]) - THE RULE of the docstring, every reason printed."""
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
        return None, lines + ["NO WIDTH 1 RUN: nothing to compare against"]
    unstable = sorted(n for n, w in seen.items() if len({v for v, _ in w.get(1, [])}) > 1)
    for n in unstable:
        lines.append(f"WIDTH 1 DISAGREES WITH ITSELF: {n} " + " / ".join(v for v, _ in seen[n][1]))
    ok_w = [1]
    for w in widths:
        if w == 1:
            continue
        why = []
        for n in sorted(seen):
            base = {v for v, _ in seen[n].get(1, [])}
            got = [v for v, _ in seen[n].get(w, [])]
            if got and (len(base) != 1 or any(v not in base for v in got)):
                why.append(f"{n} {'/'.join(sorted(base))} at width 1, {'/'.join(got)} at width {w}")
            slow = [s for _, s in seen[n].get(w, []) if s > row_cap]
            if slow:
                why.append(f"{n} ran {max(slow):.1f} s at width {w}, past {row_cap:.0f} s (priced {price.get(n, 0):.2f} s)")
        oom = [ln for r in by_w[w] for ln in r["oom"]]
        if oom:
            why.append(f"{len(oom)} out-of-memory line(s): {oom[0]}")
        xid = [ln for r in by_w[w] for ln in r["xid"]]
        if xid:
            why.append(f"{len(xid)} Xid line(s): {xid[0]}")
        if any(r in missing for r in by_w[w]):
            why.append("a run incomplete")
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
    if unstable or any(r in missing for r in by_w[1]):
        return None, lines
    return adopt, lines


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
        rows.append(f"{w}\t{len(rs)}\t{walls}\t{peak}\t{first}\t{worst[0]}\t{worst[1]:.2f}\t{price.get(worst[0], 0):.2f}\t{med:.2f}")
        lines.append(f"  width {w}: {len(rs)} run(s), wall {walls} s; VRAM peak {peak} MB (first {first}); slowest row "
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
    order = [int(x) for x in opt(args, "--order", "1,2,3,3,2,1").split(",")]
    out = opt(args, "--out")
    lock = shlex.split(opt(args, "--lock", os.path.join(HERE, "gpu-exclusive.sh")))
    pfile = opt(args, "--prices", os.path.join(HERE, "gate-times.txt"))
    row_cap = float(opt(args, "--row-cap-s", ROW_CAP_S))
    part_cap = float(opt(args, "--part-cap-s", PART_CAP_S))
    smi = opt(args, "--nvidia-smi", "nvidia-smi")
    wait_max = int(opt(args, "--wait-max", "600"))
    if len(args) != 1 or args[0].startswith("-"):
        die("usage: width_pair.py <build> --display :NN [--plan] [--order 1,2,3,3,2,1] [--out <dir>] [--lock <cmd>] "
            "[--prices <file>] [--row-cap-s 45] [--part-cap-s 480] [--nvidia-smi <exe>] [--wait-max 600]")
    build = os.path.abspath(args[0])
    if 1 not in order or any(w < 1 for w in order):
        die(f"--order {order}: the widths are >= 1 and width 1 is in it (the base every width is judged against)")
    price = prices(pfile)
    if not price:
        die(f"no prices read from {pfile} (scripts/gate-times.txt: a row's price is what makes it small)")
    inv = inventory(build)
    sel, excl = select(inv, price, cold_rows(build))
    ps = parts(sel, price, part_cap) if sel else []
    est = sum(sum(price[n] for n in p) * RATIO / w for w in order for p in ps)
    print(f"width_pair: {len(sel)} rows with no timing law priced under {SMALL_S:.0f} s ({sum(price[n] for n in sel):.0f} s "
          f"serial), {len(ps)} part(s) under {part_cap:.0f} s each at width 1; order {','.join(map(str, order))}; "
          f"{len(ps) * len(order)} holds, ~{est / 60:.0f} min of card estimated (the width-W run at 1/W of width 1's)")
    for why, ns in sorted(excl.items()):
        print(f"  left out, {why}: {len(ns)}" + (": " + " ".join(sorted(ns)) if len(ns) <= 12 else ""))
    for k, p in enumerate(ps, 1):
        print(f"  part {k}: {len(p)} rows ~{sum(price[n] for n in p) * RATIO:.0f} s at width 1")
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
    out = os.path.abspath(out or os.path.join(build, "tests", ".width-pair", time.strftime("%Y%m%d-%H%M%S")))
    os.makedirs(out, exist_ok=True)
    recs = []
    for w in order:
        for k, p in enumerate(ps, 1):
            r = run_one(build, out, w, k, p, lock, display, wait_max, smi)
            recs.append(r)
            if not r["held"]:
                print("width_pair: the card stayed held - stopped; the records so far are kept, call again (exit 75)")
                return 75
    for ln in summary(recs, price, out):
        print(ln)
    adopt, lines = judge(recs, price, row_cap)
    for ln in lines:
        print("  " + ln)
    if adopt is None:
        print(f"width_pair: NO WIDTH ADOPTED - width 1 unstable or a run incomplete (evidence {out})")
        return 1
    print(f"width_pair: ADOPT WIDTH {adopt} (the widest with width 1's verdicts in every run, no out-of-memory, no row past "
          f"{row_cap:.0f} s; evidence {out})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
