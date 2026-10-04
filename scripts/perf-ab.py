#!/usr/bin/env python3
"""perf-ab.py — ONE COMMAND FROM AN IDEA TO A MEASURED FRAME NUMBER (lane TEST-1, the perf audit's A1).

    scripts/perf-ab.py <fixture> --arm A=<arm=value[,arm=value]> --arm B=<...>
                       [--tier epic] [--res 1080p[,4k|WxH]] [--warm 120] [--frames 240] [--abba 4]
                       [--settle 30] [--vr] [--lock-clocks 2100,2550] [--build build-linux] [--out DIR]

Before it, every measuring lane wrote its own rig: a capture driver, a parser, a clock dance and a
"measuring door" in engine code (116 such scripts in 39 spike dirs; process-tooling.md A1). This is
the one rig. In order, it:

  1. takes its own memory scope (~/Developer/scripts/lead/scoped.sh 40G), its OWN Xvfb (a free display
     in 60-99, 1920x1080x24, killed by the pid it recorded), a scratch HOME and --data-root, and the
     WHOLE CARD (scripts/gpu-exclusive.sh: every VRAM token, the run log's timing class);
  2. locks the GPU clocks for the run and restores them on every exit path (gpu-exclusive.sh
     --lock-clocks, plan 9cl CLOCK-TRAP-1) — where the box refuses `sudo -n nvidia-smi` the header
     says NOT LOCKED and every number is provisional;
  3. opens the fixture, sets the tier, sizes the viewport and WARMS UP (frames, never seconds);
  4. alternates the arms in ONE app process, ABBA (A B B A, repeated --abba times), each block a
     frame-counted perf.capture({frames}) window after --settle frames on the new arm — an arm is a
     registered measurement switch (engine.arm(name, value); `engine.arms()` lists them), latched by
     the engine at the top of the next frame;
  5. writes ONE table: per pass and per monitor row (CacheWork), GPU ms median and p95 per arm with a
     bootstrap 95 % CI of the median, the CPU stages, the frame totals (the frame's own GPU span,
     the top-level passes, the unattributed remainder, the CPU frame), and A against B as a delta and
     a percentage with its CI. The tree's three shas, its dirt and the clock state head the table.

THE FIXTURE is one of:
  sample:<Name>       a shipped sample (project.openSample — "Showroom 2", "Matcaps")
  template:<t>        a new world: empty | basic | world (project.create's template)
  script:<file.js>    a script that builds the scene in a new empty world; it may define
                      perfAbStep(i), called before every frame the harness draws (to animate it).
                      scripts/perf-ab-fixtures/ holds the shipped ones.

THE EVIDENCE lands in ~/Developer/spikes/<branch>/ab-<timestamp>/ (or --out): driver.js, app.log,
the capture bundles, table.json and report.txt (the table, also printed).

Exit: 0 = measured and tabled; 1 = the run or a bundle failed (the reason is printed); 64 = usage.
"""
import argparse
import datetime
import json
import math
import os
import random
import re
import shutil
import signal
import statistics
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCOPED = os.path.expanduser("~/Developer/scripts/lead/scoped.sh")
MONADO_MANIFEST = "/usr/share/openxr/1/openxr_monado.json"
RES = {"720p": (1280, 720), "1080p": (1920, 1080), "1440p": (2560, 1440), "4k": (3840, 2160)}
BOOT = 2000          # bootstrap resamples per CI


# ---------------------------------------------------------------------------------------------------
# the command line
# ---------------------------------------------------------------------------------------------------
def parse_arm(text):
    """'A=reflect.motion=0,rayquery.tlasRefit=1' -> ('A', {'reflect.motion': 0.0, ...})."""
    label, _, body = text.partition("=")
    if not label or not body:
        raise argparse.ArgumentTypeError("an arm is LABEL=<arm>=<value>[,<arm>=<value>...]: " + text)
    values = {}
    for item in body.split(","):
        name, _, val = item.partition("=")
        try:
            values[name.strip()] = float(val)
        except ValueError:
            raise argparse.ArgumentTypeError("arm %s: '%s' is not <arm>=<number>" % (label, item))
    return label, values


def parse_res(text):
    out = []
    for r in text.split(","):
        r = r.strip().lower()
        if r in RES:
            out.append((r, RES[r]))
            continue
        m = re.fullmatch(r"(\d+)x(\d+)", r)
        if not m:
            raise argparse.ArgumentTypeError("a resolution is 720p|1080p|1440p|4k|WxH: " + r)
        out.append((r, (int(m.group(1)), int(m.group(2)))))
    return out


# ---------------------------------------------------------------------------------------------------
# the app's half: one driver script
# ---------------------------------------------------------------------------------------------------
def fixture_js(fixture):
    kind, _, what = fixture.partition(":")
    stamp = "perf-ab " + datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    if kind == "sample":
        # the import by its own completion, then the sliced open with a frame in the loop
        return ("if (project.openSample(%s) !== true) throw new Error('project.openSample: ' + app.lastError());\n"
                "var imported = project.waitArchive();\n"
                "if (imported.ok !== true) throw new Error('the sample import: ' + JSON.stringify(imported));\n"
                "while (project.openState() === 'opening') editor.frame(1);\n"
                % json.dumps(what))
    if kind == "template":
        if what not in ("empty", "basic", "world"):
            raise SystemExit("perf-ab: template:<empty|basic|world>, not '%s'" % what)
        return "project.create(%s, { template: %s });\n" % (json.dumps(stamp), json.dumps(what))
    if kind == "script":
        path = what if os.path.isabs(what) else os.path.join(ROOT, what) if not os.path.exists(what) else what
        if not os.path.isfile(path):
            raise SystemExit("perf-ab: no fixture script at %s" % what)
        return ("project.create(%s, { template: 'empty' });\n// ---- the fixture: %s\n%s\n// ---- end of the fixture\n"
                % (json.dumps(stamp), path, open(path).read()))
    raise SystemExit("perf-ab: a fixture is sample:<Name> | template:<t> | script:<file.js>, not '%s'" % fixture)


def driver_js(a, arms, order, bundles):
    names = sorted({n for _, v in arms for n in v})
    return """// perf-ab.py's driver (generated %(when)s) — see the harness's header.
var ARMS = %(arms)s;
var ORDER = %(order)s;
var RES = %(res)s;
var NAMES = %(names)s;
var WARM = %(warm)d, SETTLE = %(settle)d, FRAMES = %(frames)d, VR = %(vr)s, TIER = %(tier)s;
var BUNDLES = %(bundles)s;
function log(o) { console.log("PERFAB " + JSON.stringify(o)); }
%(fixture)s
var step = (typeof perfAbStep === "function") ? perfAbStep : function () {};
var frameNo = 0;
function frames(n) { for (var i = 0; i < n; ++i) { step(frameNo++); editor.frame(1); } }
if (TIER) world.mode({ mode: TIER });
var defaults = {};
engine.arms().forEach(function (x) { defaults[x.name] = x["default"]; });
NAMES.forEach(function (n) {
    if (!(n in defaults)) throw new Error("no arm named " + n + " (engine.arms(): " + Object.keys(defaults).join(", ") + ")");
});
function setArm(label) {
    NAMES.forEach(function (n) {
        var v = (n in ARMS[label]) ? ARMS[label][n] : defaults[n];
        if (engine.arm(n, v) === null) throw new Error("engine.arm(" + n + ", " + v + "): " + app.lastError());
    });
}
function sizeViewport(w, h) {
    // THE VIEWPORT IS ASKED FOR, THE WINDOW IS WHAT MOVES: resize the main window by the viewport's
    // shortfall until the viewport is the size or stops moving (the header says what it took).
    for (var t = 0; t < 4; ++t) {
        var vs = editor.viewportState();
        if (vs.width === w && vs.height === h) break;
        var win = app.window();
        app.resizeWindow(win.width + (w - vs.width), win.height + (h - vs.height));
        frames(3);
    }
    var v = editor.viewportState();
    return [v.width, v.height];
}
frames(10);
if (VR && vr.begin({}) !== true) throw new Error("vr.begin: " + app.lastError());
try {
    for (var r = 0; r < RES.length; ++r) {
        var tag = RES[r][0], got = RES[r][1] ? sizeViewport(RES[r][1][0], RES[r][1][1]) : [editor.viewportState().width, editor.viewportState().height];
        log({ res: tag, viewport: got, tier: world.mode() });
        setArm(ORDER[0]);
        frames(WARM);
        for (var b = 0; b < ORDER.length; ++b) {
            var L = ORDER[b];
            setArm(L);
            frames(SETTLE);
            var out = BUNDLES + "/" + tag + "-" + b + "-" + L;
            var c = perf.capture({ frames: FRAMES, out: out, label: "perf-ab-" + L });
            if (c.started !== true) throw new Error("perf.capture: " + JSON.stringify(c) + " " + app.lastError());
            frames(FRAMES);
            var st = perf.status();
            if (st.recording) { perf.stop(); st = perf.status(); }
            log({ res: tag, block: b, arm: L, bundle: st.lastBundle, frames: st.frames });
        }
    }
} finally {
    NAMES.forEach(function (n) { engine.arm(n, defaults[n]); });
    if (VR) vr.end();
    frames(1);
}
log({ done: true });
""" % {
        "when": datetime.datetime.now().isoformat(timespec="seconds"),
        "arms": json.dumps({l: v for l, v in arms}),
        "order": json.dumps(order),
        "res": json.dumps([[t, list(wh) if wh else None] for t, wh in a.res]),
        "names": json.dumps(names),
        "warm": a.warm, "settle": a.settle, "frames": a.frames,
        "vr": "true" if a.vr else "false",
        "tier": json.dumps(a.tier) if a.tier else "null",
        "bundles": json.dumps(bundles),
        "fixture": fixture_js(a.fixture),
    }


# ---------------------------------------------------------------------------------------------------
# the box's half
# ---------------------------------------------------------------------------------------------------
def git(args, cwd=ROOT):
    try:
        return subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True, timeout=20).stdout.strip()
    except (OSError, subprocess.TimeoutExpired):
        return ""


def shas():
    ig = os.path.join(ROOT, "irisgl")
    return {"studio": git(["rev-parse", "HEAD"]), "irisgl": git(["rev-parse", "HEAD"], ig),
            "fork": git(["rev-parse", "HEAD"], os.path.join(ig, "thirdparty", "ogre-next")),
            "dirty": bool(git(["status", "--porcelain", "--untracked-files=no", "--ignore-submodules=dirty"]))
            or bool(git(["status", "--porcelain", "--untracked-files=no", "--ignore-submodules=dirty"], ig))}


def clock_state():
    sys.path.insert(0, os.path.join(ROOT, "scripts"))
    try:
        import gate_runlog
        return gate_runlog.gpu_clocks()
    except Exception as e:
        return {"state": "unknown", "error": str(e)}


def free_display():
    for n in range(60, 100):
        if not os.path.exists("/tmp/.X%d-lock" % n) and not os.path.exists("/tmp/.X11-unix/X%d" % n):
            return n
    raise SystemExit("perf-ab: no free X display in 60-99")


def start_xvfb():
    """OUR OWN display, its pid recorded at spawn (never a wrapper's $!), the rig's one geometry."""
    n = free_display()
    p = subprocess.Popen(["Xvfb", ":%d" % n, "-screen", "0", "1920x1080x24", "-nolisten", "tcp"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(100):
        if os.path.exists("/tmp/.X11-unix/X%d" % n):
            return n, p
        if p.poll() is not None:
            break
        time.sleep(0.1)
    p.kill()
    raise SystemExit("perf-ab: Xvfb :%d did not come up" % n)


# ---------------------------------------------------------------------------------------------------
# the analysis
# ---------------------------------------------------------------------------------------------------
def frame_rows(rec):
    """One frames.jsonl record -> {row key: ms} (a key that repeats in a frame is summed)."""
    out = {}

    def add(k, v):
        if v is None or v < 0:
            return
        out[k] = out.get(k, 0.0) + float(v)
    add("frame  GPU span (frameGpuMs)", rec.get("frameGpuMs"))
    add("frame  GPU top-level passes (gpuMs)", rec.get("gpuMs"))
    if rec.get("frameGpuMs", -1) is not None and rec.get("frameGpuMs", -1) >= 0:
        add("frame  GPU unattributed", rec.get("unattributedGpuMs"))
    add("frame  CPU (totalMs)", rec.get("totalMs"))
    for p in rec.get("passes") or []:
        add("pass   %s/%s/%s" % (p.get("workspace", ""), p.get("node", ""), p.get("pass", "")), p.get("gpuMs"))
    for w in rec.get("cacheWork") or []:
        add("row    %s" % w.get("detail", ""), w.get("gpuMs"))
    for s in rec.get("stages") or []:
        add("stage  %s (CPU)" % s.get("name", ""), s.get("ms"))
    return out


def read_bundle(path):
    f = os.path.join(path, "frames.jsonl")
    recs = []
    with open(f) as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                recs.append(json.loads(line))
            except ValueError:
                continue
    return recs


def pct(v, q):
    if not v:
        return float("nan")
    s = sorted(v)
    k = (len(s) - 1) * q
    lo, hi = int(math.floor(k)), int(math.ceil(k))
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def boot_ci(a, b=None, rng=None):
    """A bootstrap 95 % CI of median(a), or of median(b)/median(a) - 1 when `b` is given."""
    rng = rng or random.Random(1)
    out = []
    for _ in range(BOOT):
        ra = [a[rng.randrange(len(a))] for _ in a]
        ma = statistics.median(ra)
        if b is None:
            out.append(ma)
        else:
            rb = [b[rng.randrange(len(b))] for _ in b]
            if ma > 0:
                out.append(statistics.median(rb) / ma - 1.0)
    return (pct(out, 0.025), pct(out, 0.975)) if out else (float("nan"), float("nan"))


def analyse(blocks, labels):
    """blocks: [(res, arm, bundle)] -> {res: [row dicts]}."""
    per = {}
    for res, arm, bundle in blocks:
        for rec in read_bundle(bundle):
            for k, v in frame_rows(rec).items():
                per.setdefault(res, {}).setdefault(k, {}).setdefault(arm, []).append(v)
    tables = {}
    a_lab, b_lab = labels[0], labels[1]
    for res, rows in per.items():
        out = []
        for k, arms in rows.items():
            a, b = arms.get(a_lab, []), arms.get(b_lab, [])
            row = {"row": k}
            for lab, v in ((a_lab, a), (b_lab, b)):
                row[lab] = {"n": len(v), "median": statistics.median(v) if v else None,
                            "p95": pct(v, 0.95) if v else None, "ci": boot_ci(v) if len(v) >= 5 else None}
            if a and b and statistics.median(a) > 0:
                ma, mb = statistics.median(a), statistics.median(b)
                row["deltaMs"] = mb - ma
                row["deltaPct"] = 100.0 * (mb / ma - 1.0)
                lo, hi = boot_ci(a, b) if len(a) >= 5 and len(b) >= 5 else (float("nan"), float("nan"))
                row["deltaPctCi"] = (100.0 * lo, 100.0 * hi)
            out.append(row)
        # the frame rows first, then by the size of the move
        out.sort(key=lambda r: (0 if r["row"].startswith("frame") else 1, -abs(r.get("deltaMs", 0.0))))
        tables[res] = out
    return tables


def fmt(v, w=8, p=3):
    return ("%*.*f" % (w, p, v)) if isinstance(v, (int, float)) and v == v else " " * (w - 1) + "-"


def render(header, tables, labels, top):
    a_lab, b_lab = labels
    lines = ["perf-ab — %s" % header["when"],
             "tree     studio %s  irisgl %s  fork %s%s" % (header["shas"]["studio"][:9], header["shas"]["irisgl"][:9],
                                                          header["shas"]["fork"][:9],
                                                          "  (DIRTY TREE)" if header["shas"]["dirty"] else ""),
             "clocks   %s" % header["clocks"],
             "fixture  %s   tier %s   frames %d/block, settle %d, warm %d, order %s"
             % (header["fixture"], header["tier"] or "(scene's)", header["frames"], header["settle"], header["warm"],
                " ".join(header["order"])),
             "arms     " + "   ".join("%s: %s" % (l, ", ".join("%s=%g" % kv for kv in sorted(v.items())))
                                     for l, v in header["arms"]),
             "viewport " + "   ".join("%s -> %s" % (r, "x".join(str(x) for x in v)) for r, v in header["viewports"].items())]
    for res, rows in tables.items():
        lines.append("")
        lines.append("== %s ==   (ms; median [95 %% CI of the median]  p95;  delta = %s - %s)" % (res, b_lab, a_lab))
        lines.append("%-58s %8s %19s %8s | %8s %19s %8s | %8s %7s %17s" % (
            "row", a_lab + " med", "CI", "p95", b_lab + " med", "CI", "p95", "delta", "%", "% CI"))
        shown = 0
        for r in rows:
            if shown >= top and not r["row"].startswith("frame"):
                break
            A, B = r[a_lab], r[b_lab]
            ci = lambda c: ("[%s,%s]" % (fmt(c[0], 7), fmt(c[1], 7))) if c else " " * 17 + "-"
            dci = r.get("deltaPctCi")
            lines.append("%-58s %s %19s %s | %s %19s %s | %s %s %17s" % (
                r["row"][:58], fmt(A["median"]), ci(A["ci"]), fmt(A["p95"]), fmt(B["median"]), ci(B["ci"]),
                fmt(B["p95"]), fmt(r.get("deltaMs")), fmt(r.get("deltaPct"), 7, 1),
                ("[%s,%s]" % (fmt(dci[0], 7, 1), fmt(dci[1], 7, 1))) if dci else "-"))
            shown += 1
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("fixture")
    ap.add_argument("--arm", action="append", type=parse_arm, required=True,
                    help="LABEL=<arm>=<value>[,<arm>=<value>] — exactly two (engine.arms() lists the names)")
    ap.add_argument("--tier", choices=("low", "medium", "high", "epic"), default=None)
    ap.add_argument("--res", type=parse_res, default=[("as-is", None)],
                    help="the viewport size(s): 720p|1080p|1440p|4k|WxH, comma-separated (default: the window's)")
    ap.add_argument("--warm", type=int, default=120)
    ap.add_argument("--frames", type=int, default=240, help="frames per captured block")
    ap.add_argument("--settle", type=int, default=30, help="frames on a new arm before its block is captured")
    ap.add_argument("--abba", type=int, default=4, help="ABBA repetitions (4 = 16 blocks)")
    ap.add_argument("--vr", action="store_true", help="a VR session (Monado's simulated headset) drives the frames")
    ap.add_argument("--lock-clocks", default="2100,2550", help="MIN,MAX MHz, or 'none'")
    ap.add_argument("--build", default="build-linux")
    ap.add_argument("--out", default=None)
    ap.add_argument("--top", type=int, default=40, help="rows printed per table (the frame rows always)")
    ap.add_argument("--full-priority", action="store_true",
                    help="no nice/ionice (only when the lead's brief says the measurement needs it)")
    ap.add_argument("--dry-run", action="store_true", help="write the driver and print the command; run nothing")
    a = ap.parse_args()
    if len(a.arm) != 2 or a.arm[0][0] == a.arm[1][0]:
        ap.error("give exactly two arms with different labels (--arm A=... --arm B=...)")
    labels = [a.arm[0][0], a.arm[1][0]]
    order = []
    for _ in range(max(1, a.abba)):
        order += [labels[0], labels[1], labels[1], labels[0]]

    binary = os.path.join(ROOT, a.build, "bin", "Jahshaka")
    if not os.access(binary, os.X_OK):
        raise SystemExit("perf-ab: no app binary at %s (build the tree first)" % binary)
    ts = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    lane = git(["rev-parse", "--abbrev-ref", "HEAD"]) or "perf-ab"
    out = os.path.abspath(a.out or os.path.expanduser("~/Developer/spikes/%s/ab-%s" % (lane, ts)))
    bundles = os.path.join(out, "bundles")
    home, run = os.path.join(out, "home"), os.path.join(out, "run")
    for d in (bundles, home, run):
        os.makedirs(d, exist_ok=True)
    driver = os.path.join(out, "driver.js")
    with open(driver, "w") as f:
        f.write(driver_js(a, a.arm, order, bundles))

    cmd = []
    if os.access(SCOPED, os.X_OK):
        cmd += [SCOPED, "40G", "--"]
    if not a.full_priority:
        cmd += ["nice", "-n", "19", "ionice", "-c", "3"]
    cmd += [os.path.join(ROOT, "scripts", "gpu-exclusive.sh"), "--label", "perf-ab " + a.fixture]
    if a.lock_clocks != "none":
        cmd += ["--lock-clocks", a.lock_clocks]
    if a.vr:
        cmd += [os.path.join(ROOT, "tests", "vr", "run_vr_app.sh"), "--launch", MONADO_MANIFEST, "--"]
    cmd += [binary, "--data-root", os.path.join(home, "data"), "--script", driver]
    if a.vr:
        cmd.insert(cmd.index(binary) + 1, "--vr")
    if a.dry_run:
        print("driver %s\n%s" % (driver, " ".join(cmd)))
        return 0

    before = clock_state()
    xn, xvfb = start_xvfb()
    log = os.path.join(out, "app.log")
    env = dict(os.environ, DISPLAY=":%d" % xn, HOME=home)
    budget = 600 + len(a.res) * (a.warm + len(order) * (a.settle + a.frames)) // 5
    t0 = time.time()
    rc = None
    try:
        with open(log, "w") as lf:
            p = subprocess.Popen(cmd, cwd=run, env=env, stdout=lf, stderr=subprocess.STDOUT, start_new_session=True)
            try:
                rc = p.wait(timeout=budget)
            except subprocess.TimeoutExpired:
                os.killpg(p.pid, signal.SIGTERM)
                try:
                    p.wait(timeout=60)
                except subprocess.TimeoutExpired:
                    os.killpg(p.pid, signal.SIGKILL)
                rc = "timeout"
    finally:
        xvfb.terminate()
        try:
            xvfb.wait(timeout=10)
        except subprocess.TimeoutExpired:
            xvfb.kill()
    after = clock_state()
    text = open(log, errors="replace").read()
    events = [json.loads(m) for m in re.findall(r"^PERFAB (\{.*\})\s*$", text, re.M)]
    clock_lines = re.findall(r"^gpu-clocks: .*$", text, re.M)
    blocks = [(e["res"], e["arm"], e["bundle"]) for e in events if "block" in e and e.get("bundle")]
    viewports = {e["res"]: e["viewport"] for e in events if "viewport" in e}
    done = any(e.get("done") for e in events)
    if not done or rc not in (0,):
        sys.stderr.write("perf-ab: the run did not finish (exit %s, %d of %d blocks) — %s\n"
                         % (rc, len(blocks), len(order) * len(a.res), log))
        for l in text.splitlines()[-15:]:
            sys.stderr.write("   " + l + "\n")
        if not blocks:
            return 1
    missing = [b for _, _, b in blocks if not os.path.isfile(os.path.join(b, "frames.jsonl"))]
    if missing:
        sys.stderr.write("perf-ab: %d bundle(s) without frames.jsonl: %s\n" % (len(missing), missing[:3]))
        return 1
    header = {"when": datetime.datetime.now().isoformat(timespec="seconds"), "shas": shas(),
              "clocks": "; ".join(clock_lines) + " | before %s, after %s" % (before.get("state"), after.get("state"))
              if clock_lines else "before %s, after %s (no lock asked)" % (before.get("state"), after.get("state")),
              "fixture": a.fixture, "tier": a.tier, "frames": a.frames, "settle": a.settle, "warm": a.warm,
              "order": order, "arms": a.arm, "viewports": viewports, "wallS": round(time.time() - t0, 1),
              "exit": rc}
    tables = analyse(blocks, labels)
    report = render(header, tables, labels, a.top)
    with open(os.path.join(out, "report.txt"), "w") as f:
        f.write(report)
    with open(os.path.join(out, "table.json"), "w") as f:
        json.dump({"header": header, "tables": tables}, f, indent=1, default=str)
    sys.stdout.write(report)
    sys.stdout.write("evidence: %s\n" % out)
    return 0 if done and rc == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
