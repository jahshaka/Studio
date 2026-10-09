#!/usr/bin/env python3
"""gate.stage_close_select / gate.stage_close_labels — THE STAGE-CLOSE ROWS (lane STAGE-CLOSE-1;
docs/TESTING_GATE.md §1d). The owner (2026-10-09): there is no such daily build, so there are no such rows — the label that left the MERGE and PUSH tiers is `stage-close`, its rows run as ONE batch in
the gate slot that the lead starts at every stage close and before every push, and a row rides a
scoped gate only when the diff touches its OWN subject.

  select — on a TOY inventory (in-process, no build graph: the rules alone):
    1. a broad rule (the engine family) that reaches a `stage-close` row leaves it for the batch,
       while its plain sibling in the same directory is selected;
    2. its subject rule (STAGE_CLOSE_SUBJECTS: the bake -> the cluster sweeps, a shadow/caster
       file -> the cutout soak and the churn twins) selects it — and only it;
    3. a change in the row's OWN test directory keeps it; a tests/support helper does not;
    4. a quiet-box row is never selected by a subject rule; no subject rule names one;
    5. the stage-close batch (both printed phases, run through a toy ctest project) selects every
       `stage-close` / `shadercache-attack` row and no other, the quiet-box and timing ones only in the
       -j1 phase; the MERGE tier selects none of them;
    6. the retired tier flag is refused, and the refusal names `--stage-close-tier`.
    7. THE STAGE-CLOSE JUDGE (ci_gate_check.py --stage-close <tip>) refuses a push / stage close while a
       stage-close row has no record at the tip or is red (a prose verdict is refused by the door; a later
       green run at the tip does not erase the red), and accepts when every row is green (a private run log
       and an empty defect registry, the toy build).
  labels — the tree: no retired label or word (OLD below) in tests/, scripts/ or docs/ (git ls-files); and in
    this build the shadow casters' gate rows run ONE process without the label, their `.churn` twins
    TEN with `stage-close` + `engine`.

Run: gate_stage_close_test.py <select|labels> <source-dir> <build-dir>
"""
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

FAILURES = []
OLD = "night" + "ly"          # the retired word, never spelled whole in this tree


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAILURES.append(what)


def load_gs(source):
    sys.path.insert(0, os.path.join(source, "scripts"))
    spec = importlib.util.spec_from_file_location("gate_scope", os.path.join(source, "scripts", "gate-scope.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def toy_row(d, labels, kind="other", app=False):
    return {"pool": None, "arms": {}, "dir": d, "reldir": "tests/" + d, "cmd": [], "app": app,
            "headless": False, "script": None, "argv_files": set(), "via": set(), "frames": set(),
            "sites": set(), "serial": False, "labels": set(labels), "kind": kind, "exes": set()}


TOY_INV = {
    "atom.cluster_cut": ("atom", {"meshbake", "stage-close"}),
    "atom.cluster_crack": ("atom", {"engine", "stage-close"}),
    "engine.atom_parity": ("atom", {"engine"}),
    "gpu.cutout_soak": ("shadow", {"engine", "stage-close"}),
    "shadow.cutout_caster": ("shadow", {"engine"}),
    "shadow.cutout_caster.churn": ("shadow", {"engine", "stage-close"}),
    "gi.chain_converge_scenes": ("scripting", {"scripting", "stage-close"}),
    "gi.gather_cost": ("gi", {"stage-close", "quiet-box", "timing"}),
    "gi.budget": ("gi", {"engine", "timing"}),
}


def select_cases(source):
    gs = load_gs(source)

    def sel(paths):
        inv = {n: toy_row(d, l) for n, (d, l) in TOY_INV.items()}
        S = gs.Selection(inv, None, None, gs.Revs(None), 4)
        for p in paths:
            S.path(p)
        S.stage_close_subjects(paths)
        # main()'s own drop: the quiet-box measurements never ride a scoped gate
        gating = {n for n in S.selected if not (inv[n]["labels"] & gs.SCOPE_EXCLUDED_LABELS)}
        return S, gating

    print("1. a broad rule leaves the stage-close rows for the batch")
    S, g = sel(["irisgl/engine/src/OgreChain.cpp"])
    check("engine.atom_parity" in g and "shadow.cutout_caster" in g,
          "the engine family selects the plain rows of atom/ and shadow/ (%s)" % sorted(g))
    sc = {n for n, (d, l) in TOY_INV.items() if "stage-close" in l}
    check(not (g & sc), "...and no stage-close row (%s)" % sorted(g & sc))
    check({"atom.cluster_cut", "atom.cluster_crack", "gpu.cutout_soak", "shadow.cutout_caster.churn"}
          <= set(S.stage_close_left), "...which are left for the batch, said by name (%s)" % sorted(S.stage_close_left))

    print("2. the subject rules")
    S, g = sel(["irisgl/import/meshbake.cpp"])
    check({"atom.cluster_cut", "atom.cluster_crack"} <= g, "a bake change selects the cluster sweeps (%s)" % sorted(g & sc))
    check(not ({"gpu.cutout_soak", "shadow.cutout_caster.churn", "gi.chain_converge_scenes"} & g),
          "...and no other stage-close row")
    S, g = sel(["irisgl/engine/src/OgreShadow.cpp"])
    check({"gpu.cutout_soak", "shadow.cutout_caster.churn"} <= g and not ({"atom.cluster_cut", "atom.cluster_crack"} & g),
          "a shadow change selects the cutout soak and the churn twin, not the sweeps (%s)" % sorted(g & sc))
    S, g = sel(["irisgl/engine/src/photon/voxel/PhotonVoxelLighting.cpp"])
    check("gi.chain_converge_scenes" in g and not ({"gpu.cutout_soak", "atom.cluster_cut"} & g),
          "a Photon voxel change selects the converge sweep (%s)" % sorted(g & sc))
    S, g = sel(["irisgl/engine/src/OgreGi.cpp", "src/ui/foo.cpp"])
    check("gi.chain_converge_scenes" in g, "...whatever else the change touched")

    print("3. the row's own test directory")
    S, g = sel(["tests/shadow/scripts/shadow_casters.sh"])
    check({"gpu.cutout_soak", "shadow.cutout_caster.churn"} <= g,
          "a change in tests/shadow keeps the shadow dir's stage-close rows (%s)" % sorted(g & sc))
    check(not ({"atom.cluster_cut", "atom.cluster_crack", "gi.chain_converge_scenes"} & g),
          "...and no other directory's")
    S, g = sel(["tests/atom/fixtures/x.txt"])
    check({"atom.cluster_cut", "atom.cluster_crack"} <= g, "a file of tests/atom keeps the cluster sweeps (%s)" % sorted(g & sc))

    print("4. quiet-box rows")
    named = {r for _, rows in gs.STAGE_CLOSE_SUBJECTS for r in rows}
    check("gi.gather_cost" not in sel(["irisgl/engine/src/OgreGi.cpp"])[1],
          "a GI change never puts the quiet-box gi.gather_cost in a scoped gate")
    check(not any(n.endswith((".timing", ".benchmark")) for n in named) and "gi.gather_cost" not in named
          and "open.crash_soak" not in named, "no subject rule names a measuring row (%s)" % sorted(named))
    check(gs.SUBJECT_ONLY_LABELS == {"stage-close"} and not (gs.SUBJECT_ONLY_LABELS & gs.SCOPE_EXCLUDED_LABELS),
          "a subject-selected stage-close row GATES (its label is not in SCOPE_EXCLUDED_LABELS)")

    print("5. the batch selects every stage-close row and no other (a toy ctest project)")
    scratch = tempfile.mkdtemp(prefix="stage-close-test-")
    try:
        rows = {"sc.long": "stage-close;engine", "sc.quiet": "stage-close;quiet-box", "sc.timing": "stage-close;quiet-box;timing",
                "sc.attack": "shadercache-attack", "plain": "engine", "push.timing": "engine;timing",
                "tgt": "photon-target", "q.only": "quiet-box"}
        cm = ["cmake_minimum_required(VERSION 3.20)", "project(toy NONE)", "enable_testing()"]
        for n, l in rows.items():
            cm += ['add_test(NAME %s COMMAND true)' % n, 'set_tests_properties(%s PROPERTIES LABELS "%s")' % (n, l)]
        src, b = os.path.join(scratch, "s"), os.path.join(scratch, "b")
        os.makedirs(src)
        open(os.path.join(src, "CMakeLists.txt"), "w").write("\n".join(cm) + "\n")
        r = subprocess.run(["cmake", "-S", src, "-B", b], capture_output=True, text=True)
        check(r.returncode == 0, "the toy project configures")

        def listed(cmd):
            argv = [a.strip('"') for a in re.findall(r'"[^"]*"|\S+', cmd)]
            argv = [a for a in argv if not a.startswith("-j") and a != "--output-on-failure"]
            argv = [a for i, a in enumerate(argv) if a != "--timeout" and (i == 0 or argv[i - 1] != "--timeout")]
            out = subprocess.run(argv + ["--show-only=json-v1"], cwd=b, capture_output=True, text=True).stdout
            return {t["name"] for t in json.loads(out or "{}").get("tests", [])}
        p1, p2 = listed(gs.stage_close_tier(4)), listed(gs.stage_close_tier_serial())
        check(p1 == {"sc.long", "sc.attack"}, "the parallel phase: the minutes-of-one-process rows (%s)" % sorted(p1))
        check(p2 == {"sc.quiet", "sc.timing"}, "the -j1 phase: the quiet-box and timing stage-close rows (%s)" % sorted(p2))
        check(" -j1 " in gs.stage_close_tier_serial() and not (p1 & p2), "...at -j1, no row in both phases")
        m = listed(gs.merge_tier(4)) | listed(gs.merge_tier_serial())
        check(not (m & (p1 | p2)) and {"plain", "push.timing"} <= m,
              "the MERGE tier runs none of them, and still runs the plain and push-timing rows (%s)" % sorted(m))

        print("7. THE STAGE-CLOSE JUDGE blocks a push / stage close (ci_gate_check.py --stage-close, the toy build)")
        judge_cases(source, b, scratch, sorted(p1 | p2))
    finally:
        shutil.rmtree(scratch, ignore_errors=True)

    print("6. the retired flag")
    tool = os.path.join(source, "scripts", "gate-scope.py")
    r = subprocess.run([sys.executable, tool, "--" + OLD + "-tier"], cwd=source, capture_output=True, text=True)
    check(r.returncode == 2 and "--stage-close-tier" in r.stderr,
          "`--%s-tier` is refused (exit %d) and the refusal names --stage-close-tier (%r)"
          % (OLD, r.returncode, r.stderr.strip()[-160:]))
    r = subprocess.run([sys.executable, tool, "--stage-close-tier", "-j", "3"], cwd=source, capture_output=True, text=True)
    check(r.returncode == 0 and r.stdout.startswith("ctest -j3 ") and "stage" in r.stdout,
          "`--stage-close-tier -j 3` prints the batch's parallel phase (%r)" % r.stdout.strip())


def judge_cases(source, toy_build, scratch, rows):
    tip = subprocess.run(["git", "rev-parse", "HEAD"], cwd=source, capture_output=True, text=True).stdout.strip()
    pin = subprocess.run(["git", "rev-parse", "HEAD:irisgl"], cwd=source, capture_output=True, text=True).stdout.strip()
    logs = os.path.join(scratch, "runs"); os.makedirs(logs)
    reg = os.path.join(scratch, "defects.json"); open(reg, "w").write(json.dumps({"defects": []}))
    env = dict(os.environ, JAH_RUN_LOG_DIR=logs, JAH_DEFECTS_FILE=reg)

    def rec(suite, verdict, ts):
        with open(os.path.join(logs, "2026-10-09-stage-close-%s.jsonl" % tip[:9]), "a") as f:
            f.write(json.dumps({"schema": 1, "suite": suite, "arm": None, "verdict": verdict, "ts": ts,
                                "tip": {"studio": tip, "irisgl": pin, "studio_dirty": False, "irisgl_dirty": False}}) + "\n")

    def judge(*extra):
        r = subprocess.run([sys.executable, os.path.join(source, "scripts", "ci_gate_check.py"), "--stage-close", tip,
                            "--build", toy_build] + list(extra), cwd=source, capture_output=True, text=True, env=env)
        return r.returncode, r.stdout + r.stderr
    rc, out = judge()
    check(rc == 1 and "no record" in out, "no record at the tip: REFUSED (exit %d)" % rc)
    for n in rows:
        rec(n, "PASS", "2026-10-09T10:00:00+00:00")
    rc, out = judge()
    check(rc == 0 and "%d green" % len(rows) in out, "every stage-close row green: accepted (exit %d)" % rc)
    rec(rows[0], "FAIL", "2026-10-09T11:00:00+00:00")
    rc, out = judge()
    check(rc == 1 and "RED " + rows[0] in out, "one red stage-close row BLOCKS (exit %d)" % rc)
    rc, out = judge("--verdict", rows[0] + "=a toy verdict")
    check(rc == 1 and "VERDICT REFUSED" in out, "...and a prose verdict does not answer it: the door refuses (exit %d)" % rc)
    rec(rows[0], "PASS", "2026-10-09T12:00:00+00:00")
    rec(rows[0], "PASS", "2026-10-09T12:05:00+00:00")
    rc, out = judge()
    check(rc == 1, "a later green run at the same tip does not erase the red (exit %d)" % rc)


def label_cases(source, build):
    print("1. no retired label or word in tests/, scripts/, docs/")
    files = subprocess.run(["git", "ls-files", "tests", "scripts", "docs"], cwd=source, capture_output=True,
                           text=True).stdout.split()
    hits = []
    # the archive excepted: recorded data snapshots (the gate-report fixtures quote the box's old run log)
    files = [f for f in files if not f.startswith("tests/hygiene/fixtures/gate_report/")]
    for f in files:
        try:
            for k, line in enumerate(open(os.path.join(source, f), errors="replace"), 1):
                if OLD in line.lower():
                    hits.append("%s:%d" % (f, k))
        except (IsADirectoryError, FileNotFoundError):
            pass
    check(len(files) > 100 and not hits, "%d tracked files, none says %r (%s)" % (len(files), OLD, hits[:8]))

    print("2. the shadow casters' split (this build's registrations)")
    sys.path.insert(0, os.path.join(source, "scripts"))
    import gate_graph
    raw = gate_graph.ctest_inventory(build)[0]
    tests = {t["name"]: t for t in json.loads(raw).get("tests", [])}
    if "shadow.cutout_caster" not in tests:
        print("  (skip: the shadow rows are not registered in this build)")
        return
    for row in ("shadow.cutout_caster", "shadow.two_sided_caster"):
        for name, runs, want in ((row, "1", False), (row + ".churn", "10", True)):
            t = tests.get(name)
            if t is None:
                check(False, "%s is registered" % name); continue
            props = {p["name"]: p["value"] for p in t.get("properties", [])}
            labels = set(props.get("LABELS", []))
            cmd = t.get("command", [])
            check(cmd[-1:] == [runs] and ("stage-close" in labels) == want and "engine" in labels,
                  "%s runs %s process(es), labels %s" % (name, cmd[-1] if cmd else "?", sorted(labels)))


def main():
    if len(sys.argv) < 4 or sys.argv[1] not in ("select", "labels"):
        print(__doc__); return 2
    mode, source, build = sys.argv[1], os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3])
    if mode == "select":
        select_cases(source)
    else:
        label_cases(source, build)
    name = "gate.stage_close_" + ("select" if mode == "select" else "labels")
    if FAILURES:
        print("%s: FAILED (%d)" % (name, len(FAILURES))); return 1
    print("%s: PASSED" % name); return 0


if __name__ == "__main__":
    sys.exit(main())
