#!/usr/bin/env python3
"""source.gate_scope_rules — THE SCOPING TOOL'S OWN GUARD (lane STUDIO-SMALL-A).

`scripts/gate-scope.py` decides which suites a lane runs, so a defect in it is
invisible by construction: the wrong answer is a SHORTER gate, and a shorter
gate passes. One such defect shipped and cost SQUARE-1 a green gate that tested
nothing (ledger §620 item 5):

    cd build-linux
    python3 ../scripts/gate-scope.py <range> --build . --run
        gate-scope: 1 touched path(s)
          src/services/vrworld.cpp
              -> tests/player; tests/ui; ... ; rule ^src/services/vrworld
        SCOPED tier: NOTHING to gate — every touched path is docs/scripts/data
        $ echo $?
        0

The mechanism: `--build .` was resolved against the REPO ROOT, `ctest
--show-only=json-v1` in an unconfigured directory prints an empty inventory and
exits 0 (measured), and `add()` keeps only names the inventory knows — so every
suite the rules had just chosen was filtered out, silently, and the tool
reported an empty gate as success while printing the rationale for sixteen test
directories it had decided to skip.

THE FOUR CASES, and each one is a rule of the tool rather than a number:

  1. A CODE PATH SELECTS SUITES. `src/services/vrworld.cpp` selects the vr
     app suites (its area rule) and always the smoke pair — and NOT the compiled
     vr.session, whose executable the build graph says does not contain it.
  2. `--build .` FROM INSIDE THE BUILD DIR MEANS THE BUILD DIR — the same
     selection as the absolute form, byte for byte. FAILS BEFORE the fix.
  3. A DIRECTORY THAT IS NOT A BUILD DIR IS AN ERROR, not an empty answer
     (exit 2, naming the directory). FAILS BEFORE the fix (exit 0).
  4. THE HONEST EMPTY SELECTION SURVIVES: a docs-only change still selects
     nothing and still exits 0 — that is the case the lanes rely on, and the
     fix must not turn it into a fallback to the merge tier.

  5. AND THE INVARIANT BEHIND CASE 1: a change that moved CODE can never select
     nothing, because ALWAYS_ON_CODE adds app.startup_quiet + api.contract to
     every code path. The tool now exits 3 rather than printing an empty
     selection when that is violated; this case asserts the positive half on a
     file with no area rule of its own (`src/io/scenereader.cpp`).

  6. TARGET TESTS RUN AND DO NOT GATE (PHOTON phase A, A1 section 0; lane
     FENCE-1). A suite labelled `photon-target` states the CORRECT number for a
     term the renderer gets wrong today, so it is RED until its part lands. The
     one thing that must never happen is a target deciding a lane's gate — and
     the one thing that must never happen INSTEAD is a target quietly not
     running, because then nobody sees the distance to the bar. So the tool
     keeps them out of the gating ctest command, puts them in a SECOND command
     of their own, and says both things in words. This case reads a file that
     selects a target suite and asserts all three: the gating command does not
     name it, the target section does, and the MERGE tier's -LE carries the
     label. It is a rule of the tool, not a number.

  7. THE FALLBACK HONOURS -j (lane DEVPROCESS-1). `--json -j2` on a path that
     falls back reports a MERGE-tier `command` at -j2, and the printed tier line
     follows `-j` too; `--run` runs that same string.

  8. The decode's own suite (engine.atom_parity) rides the Atom media and HlmsAtom; the fork
     pin selects the whole MERGE tier by rule (MODULAR-GATE-1; contract §7b rule 4).

  9. THE TIER DOC QUOTES THE SCRIPT (POST-C-FIXES-1): docs/TESTING_GATE.md carries
     no literal `-LE` label list — it names `gate-scope.py --merge-tier`, which
     prints the MERGE tier from STAGE_CLOSE_LABELS | TARGET_LABELS — and an edit to
     the doc selects this guard.

  10. A SUITE REGISTERED THROUGH A HELPER FUNCTION (jah_gpu_exclusive_test)
      belongs to the directory that CALLS the helper: a change to
      tests/gi/test_gi_budget.cpp selects gi.budget (open.responsive's case until TESTING-SPEED-B3 deleted it). FAILS
      BEFORE the fix (the backtrace's add_test frame is tests/CMakeLists.txt).

  11. THE SUBJECTS SELECT THEIR SUITES (D6B-GATE-SHAPE; the suites audit's S1/S2). A change
      to the engine chain selects the Atom suites and the exposure-default picture; a change
      to the mesh bake selects the cluster suites and the LOD-rule parity; the archiver and
      the avatar import/switch select their `*.responsive` harnesses. Each FAILED before
      (measured by the audit: OgreChain.cpp selected 415 suites and no tests/atom row).

  12. THE STAGE-CLOSE LABELS SAY WHAT THEY MEAN (D6B-GATE-SHAPE; audit §8; STAGE-CLOSE-1).
      `stage-close` leaves the MERGE/PUSH tiers for the lead's stage-close batch; `quiet-box`
      beside it keeps a MEASUREMENT out of every scoped gate. A change to
      tests/ui/test_selection_cost.cpp selects ui.selection_cost (the counts) and NOT
      ui.selection_cost.timing (its millisecond bars; open.responsive's case until TESTING-SPEED-B3 deleted it);
      `--stage-close-tier` names `stage-close`
      and `--stage-close-tier-serial` runs the measuring rows at -j1 (the subject-only
      selection itself: gate.stage_close_select).

THE ROW'S OWN COST (TESTING-CLEANUP-2B item 3, H8b). It runs ~34 selections a run. RED ON BASE (b0a3b1f3c): 21 of
250 runs TIMED OUT at 120 s (median 44 s, p90 113 s) — one selection of an engine source spawned 366 `nm`
processes every run (an empty archive member was never cached, gate_graph._nm); measured on the M.2 at load 15:
161 s beside gate.selection, 35 s alone. After the fix: 25-28 s at the same load (an engine source 13 s -> 1.2 s).

  16-24. THE SUBSYSTEM TABLE (TESTING-SPEED-A; speed_cases below): a representative file per table line, the core by
      what changed (a Types.h field add, OgreEngine.cpp's frame loop vs vrHaptic, a comment), the fork by component, the
      picture set, the tools' rows for the tools' paths, a lane's own rows, a fallback that is never the tier.

Run: gate_scope_rules.py <source-dir> <build-dir>
"""

import json
import re
import os
import subprocess
import sys
import shutil
import tempfile

FAILURES = []


def run(args, cwd):
    p = subprocess.run([sys.executable] + args, cwd=cwd, capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def check(ok, what):
    print(("  ok: " if ok else "  FAIL: ") + what)
    if not ok:
        FAILURES.append(what)


def speed_cases(source, build):
    """16-24 (TESTING-SPEED-A item 4-6; SPECS/TESTING_SPEED_SPEC.md §2 A4, A6, A7): THE SUBSYSTEM TABLE. RED ON THE BASE
    (0beeac4d3): no table, no picture set, no lane rows — OgreChain.cpp selected 415 rows (gi among them), OgreEngine.cpp
    any edit every row, OgreLod the whole engine family, gate-times.txt the hygiene label."""
    import copy
    import importlib.util
    spec = importlib.util.spec_from_file_location("gate_scope_t", os.path.join(source, "scripts", "gate-scope.py"))
    gs = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gs)
    if not hasattr(gs, "ENGINE_SUBSYSTEMS"):
        check(False, "16. scripts/gate-scope.py carries THE SUBSYSTEM TABLE (ENGINE_SUBSYSTEMS)")
        return
    gs.bind_root(build)
    inv0 = gs.load_inventory(build)
    graph = gs.gate_graph.NinjaGraph.load(build)
    if graph is not None and graph.deps_objects == 0:
        graph = None
    fam = lambda n: {inv0[n]["dir"], n.split(".", 1)[0]}

    def sel(files, lane=None, revs=None):
        real = gs.Revs
        if revs is not None:
            gs.Revs = revs
        try:
            return gs.select(files, "BASE..TIP" if revs is not None else None, build, 4, graph=graph,
                             inv=copy.deepcopy(inv0), quiet_graph=True, lane=lane)
        finally:
            gs.Revs = real

    def of(S, f):
        return sorted(n for n in S.selected if f in fam(n))

    def strictly(S, f):
        """rows of family f by BOTH their directory and their name (cloud_2d.* live in tests/gi; the ray-traced
        reflection rows are named gi.rt_* in tests/rtreflect): a `never` check reads these"""
        return sorted(n for n in S.selected if inv0[n]["dir"] == f and n.split(".", 1)[0] == f)

    # 16. one representative file per line of the table: its rows, and NOT a gi row for a bloom file
    for path, want, never in (
            ("irisgl/engine/src/OgreChain.cpp", ["hdr", "ssr", "looks"], ["gi"]),
            ("irisgl/engine/media/Hlms/Jahshaka/JahDisplayEncode_ps.glsl", ["hdr"], ["gi"]),
            ("irisgl/engine/src/OgrePhotonView.cpp", ["gi"], ["hdr"]),
            ("irisgl/engine/src/OgreAtomPass.cpp", ["atom"], ["gi"]),
            ("irisgl/engine/src/OgreShadow.cpp", ["shadow", "lights"], ["gi", "hdr"]),
            ("irisgl/engine/src/OgreSky.cpp", ["sky"], ["gi", "hdr"]),
            ("irisgl/engine/src/OgreParticles.cpp", ["particles"], ["gi", "hdr"]),
            ("irisgl/engine/src/OgrePlanar.cpp", ["ssr", "mirror"], ["gi"]),
            ("irisgl/engine/src/OgreVrSession.cpp", ["vr"], ["gi", "hdr"]),
            ("irisgl/engine/src/OgreSkeleton.cpp", ["skeletal"], ["gi", "hdr"]),
            ("irisgl/engine/src/OgreDecals.cpp", ["looks"], ["gi"]),
            ("irisgl/engine/src/OgreGpuCull.cpp", ["atom", "scale"], ["gi", "hdr"]),
            ("irisgl/engine/src/OgreShaderCache.cpp", ["shadercache"], ["gi", "hdr"])):
        S = sel([path])
        got = {f: of(S, f) for f in want}
        bad = {f: strictly(S, f)[:3] for f in never if strictly(S, f)}
        check(all(got.values()) and not bad and not S.fallback and not S.full_tier,
              "16. %s -> %s (%s), never %s (%s)" % (path, "+".join(want), {k: len(v) for k, v in got.items()},
                                                   "/".join(never), bad))
    check([p for p, _, _ in gs.ENGINE_SUBSYSTEMS if re.search(p, "media/Photon/Voxel/Voxelizer_cs.glsl")][:1]
          and gs.ENGINE_SUBSYSTEMS[[i for i, (p, _, _) in enumerate(gs.ENGINE_SUBSYSTEMS)
                                    if re.search(p, "media/Hlms/Jahshaka/JahVoxelGather_cs.glsl")][0]][1][0] == "gi",
          "16. the table is FIRST MATCH, specific -> generic: a GI shader in media/Hlms/Jahshaka is the gi line's")

    # 17. THE CORE, by what changed: a Types.h field ADDED and used by one engine TU -> that TU's subsystem only
    tpath, upath = "irisgl/engine/include/jahshaka/engine/Types.h", "irisgl/engine/src/OgrePhotonView.cpp"
    ta = open(os.path.join(source, tpath), errors="replace").read()
    ua = open(os.path.join(source, upath), errors="replace").read()
    anchor = "struct GatherStatus {\n"
    tb = ta.replace(anchor, anchor + "    float tsaProbeField = 0.0f;\n", 1)
    ub = ua + "\nstatic float tsaProbeUse(const jahshaka::engine::GatherStatus &s) { return s.tsaProbeField; }\n"
    texts = {tpath: (ta, tb), upath: (ua, ub)}

    def fake(texts_):
        import tempfile

        class FakeRevs(gs.Revs):
            def __init__(self, rng):
                super().__init__(rng); self.base, self.tip = "BASE", "TIP"
            def pair(self, p): return ("BASE", "TIP")
            def texts(self, p):
                if p in texts_: return texts_[p]
                try: t = open(os.path.join(source, p), errors="replace").read()
                except OSError: t = ""
                return t, t
            def diff_u0(self, p):
                if p not in texts_: return ""
                a, b = texts_[p]
                with tempfile.TemporaryDirectory() as d:
                    open(os.path.join(d, "a"), "w").write(a); open(os.path.join(d, "b"), "w").write(b)
                    return subprocess.run(["git", "diff", "--no-index", "-U0", "a", "b"], cwd=d, capture_output=True,
                                          text=True).stdout
        return FakeRevs
    check(anchor in ta, "17. the anchor `struct GatherStatus {` is in Types.h")
    S = sel([tpath, upath], revs=fake(texts))
    nonshot = [n for n in strictly(S, "vr") if not inv0[n].get("screenshot") and not inv0[n].get("picture_arms")]
    check(of(S, "gi") and not strictly(S, "hdr") and not nonshot and not S.fallback,
          "17. a Types.h field ADDED and used by OgrePhotonView.cpp -> the users' subsystem (gi %d + the screenshot rows), "
          "no hdr row, no other vr row (%s)" % (len(of(S, "gi")), (strictly(S, "hdr") + nonshot)[:4]))
    # 18. OgreEngine.cpp by the ENCLOSING FUNCTION: a frame-loop edit -> every engine fixture + the picture set; a VR edit
    ep = "irisgl/engine/src/OgreEngine.cpp"
    ea = open(os.path.join(source, ep), errors="replace").read()
    fx = None
    for fn, needle in (("renderOneFrame", "void OgreEngine::renderOneFrame() {\n"),
                       ("vrHaptic", "bool OgreEngine::vrHaptic(int hand, float amplitude01, float seconds) {\n")):
        check(needle in ea, "18. %s is in OgreEngine.cpp" % fn)
        if needle not in ea:
            continue
        S = sel([ep], revs=fake({ep: (ea, ea.replace(needle, needle + "    (void)0x5a; // tsa\n", 1))}))
        if fn == "renderOneFrame":
            fx = S
            pics = [n for n, t in inv0.items() if t.get("picture")]
            check(len(S.selected) > 300 and all(p in S.selected for p in pics) and of(S, "gi") and of(S, "hdr"),
                  "18. an edit inside renderOneFrame -> every engine fixture + the picture set + every rendering app "
                  "row (%d rows)" % len(S.selected))
        else:
            check(of(S, "vr") and not of(S, "gi") and not of(S, "hdr") and len(S.selected) < 120,
                  "18. an edit inside vrHaptic -> the vr rows only (%d rows, gi %d, hdr %d)"
                  % (len(S.selected), len(of(S, "gi")), len(of(S, "hdr"))))
    check(gs.gate_graph.enclosing_functions("bool X::init(int a) {\n  auto f = [&]() {\n    a++;\n  };\n}\n",
                                            "bool X::init(int a) {\n  auto f = [&]() {\n    a += 2;\n  };\n}\n",
                                            [(3, 1, 3, 1)]) == {"init": 2},
          "18. the walk steps OUT of a lambda to its member function, and `init` stays `init` (no generic-word filter)")
    # 19. a comment-only edit selects nothing
    ca = open(os.path.join(source, "irisgl/engine/src/OgreChain.cpp"), errors="replace").read()
    S = sel(["irisgl/engine/src/OgreChain.cpp"], revs=fake({"irisgl/engine/src/OgreChain.cpp": (ca, ca + "\n// tsa\n")}))
    check(not S.selected, "19. a comment-only edit of an engine TU selects nothing (%s)" % sorted(S.selected)[:4])
    # 20. the fork BY COMPONENT
    def fork_tok(f):
        ls_ = gs.fork_lines(f)
        return ls_[0][1] if ls_ else None
    check(fork_tok("OgreMain/src/OgreLodStrategy.cpp") == ["engine", "atom", "scale"]
          and fork_tok("RenderSystems/Vulkan/src/OgreVulkanDevice.cpp") == ["*engine-fixtures"]
          and fork_tok("Docs/src/manual.md") == "none" and "*picture" in fork_tok("Components/Hlms/Pbs/src/OgreHlmsPbs.cpp")
          and fork_tok("Components/Atmosphere/src/OgreAtmosphere2Npr.cpp") == ["sky"] and fork_tok("weird/x") is None,
          "20. the fork by component: OgreLod -> engine/atom/scale, RenderSystem_Vulkan -> every engine fixture, Docs -> "
          "none, Hlms/Pbs -> the picture set, Atmosphere -> sky, an unnamed path -> the tier")
    S = sel([])
    S.expand(["*engine-fixtures"], [], "t")
    check(len(S.selected) > 200, "20. every engine fixture is the engine-linking compiled rows (%d)" % len(S.selected))
    # 21. THE PICTURE SET
    pics = sorted(n for n, t in inv0.items() if t.get("picture"))
    arms = sorted(n for n, t in inv0.items() if t.get("picture_arms"))
    check(any(p.startswith("hdr.") for p in pics) and any(inv0[p]["app"] for p in pics) and arms
          and not [p for p in pics if p.split(".")[0] in ("gi", "photon")]
          and not [p for p in pics if inv0[p]["labels"] & gs.STAGE_CLOSE_LABELS],
          "21. the picture set: the hdr/ssr/looks/mirror fixtures + the screenshot app rows (%d) and arms (%d pools); no "
          "gi/photon row, no stage-close row" % (len(pics), len(arms)))
    # 22. the tools' rows for the tools' paths
    S = sel(["src/services/sceneeditservice.cpp"])
    hy = [n for n in S.selected if inv0[n]["labels"] & {"hygiene", "tooling"}]
    check(S.selected and not hy, "22. a product path (its rule names the hygiene dir) selects no hygiene/tooling row "
          "(%s)" % hy[:4])
    S = sel(["scripts/gate-times.txt"])
    check(not S.selected, "22. scripts/gate-times.txt (a price file) selects nothing (%s)" % sorted(S.selected)[:4])
    S = sel(["tests/CMakeLists.txt"])
    check("app.startup_quiet" in S.selected and "api.contract" in S.selected,
          "22. the test harness (tests/CMakeLists.txt) selects the smoke pair")
    S = sel(["scripts/gate-scope.py"])
    check("source.testing_rules" in S.selected and "source.gate_scope_rules" in S.selected
          and not [n for n in S.selected if not inv0[n]["labels"] & {"hygiene", "tooling"}],
          "22. a tool path selects the tools' rows (hygiene/tooling) and nothing else")
    # 23. a lane's own rows: selected for that lane only
    lr = sorted(n for n, t in inv0.items() if any(l.startswith("lane:") for l in t["labels"]))
    if lr:
        lab = [l for l in inv0[lr[0]]["labels"] if l.startswith("lane:")][0][5:]
        f_ = os.path.relpath(next(iter(inv0[lr[0]]["argv_files"]), os.path.join(source, "tests", "lanes", lab, "x.py")),
                             gs.ROOT)
        mine, other = sel([f_], lane=lab), sel([f_], lane="SOMEONE-ELSE")
        check(lr[0] in mine.selected and lr[0] not in other.selected,
              "23. %s (label lane:%s) is selected for its lane's range, never another's" % (lr[0], lab))
        S = sel([], lane=None)
        S.expand(["*engine-fixtures", "*picture"], [], "t")
        check(lr[0] not in S.selected, "23. ...and no broad token (every engine fixture, the picture set) takes it")
    else:
        print("  (23: this build registers no tests/lanes row)")
    # 25. (fix round item 11) OgreScene.cpp / OgreView.cpp BY THE FUNCTION: every top-level function named in its map; a
    # mapped edit reaches its map's rows; an UNNAMED function reaches every engine fixture + the picture set, NOT the app rows
    for cp, m_ in gs.CORE_FUNCTION_MAPS.items():
        lines_ = gs.gate_graph.strip_cxx(open(os.path.join(source, cp), errors="replace").read())
        outer_ = set(gs.gate_graph._outer_scopes(lines_)) - {None}
        check(not (outer_ - set(m_)), "25. every top-level function of %s is named in its map (%d; unnamed: %s)"
              % (os.path.basename(cp), len(outer_), sorted(outer_ - set(m_))[:6]))
    vp = "irisgl/engine/src/OgreView.cpp"
    va = open(os.path.join(source, vp), errors="replace").read()
    nd = re.search(r"void OgreView::updateParticles\([^)]*\)[^{]*\{\n", va)
    if nd:
        S = sel([vp], revs=fake({vp: (va, va.replace(nd.group(0), nd.group(0) + "    (void)0x5a; // tsa\n", 1))}))
        check(of(S, "particles") and not of(S, "gi") and not of(S, "hdr") and len(S.selected) < 80,
              "25. an edit inside OgreView::updateParticles -> the particles rows only (%d rows)" % len(S.selected))
    tail_ = va + "\nstatic int tsaUnnamedProbe() { return 0x5a; }\n"
    S = sel([vp], revs=fake({vp: (va, tail_)}))
    apps = [n for n in S.selected if inv0[n]["app"] and not inv0[n].get("picture") and not inv0[n].get("picture_arms")]
    pics = [n for n, t in inv0.items() if t.get("picture")]
    check(all(p in S.selected for p in pics) and len(S.selected) > 200 and not apps,
          "25. an UNNAMED function of OgreView.cpp -> every engine fixture + the picture set (%d rows), no other app row "
          "(%s)" % (len(S.selected), apps[:4]))
    # 26. (fix round items 3, 5) src/viewport/ reaches the picture set; a Jahshaka Hlms PIECE reaches Atom besides its
    # line; the fork's Hlms Pbs/Common lines reach Atom
    S = sel(["src/viewport/enginerenderdriver.cpp"])
    check(all(p in S.selected for p in pics), "26. src/viewport/ -> the whole picture set (%d of %d)"
          % (sum(p in S.selected for p in pics), len(pics)))
    S = sel(["irisgl/engine/media/Hlms/Jahshaka/JahFog_piece_vs_piece_ps.any"])
    check(of(S, "sky") and [n for n in S.selected if n.startswith("engine.atom_")] and of(S, "atom"),
          "26. a Jahshaka Hlms piece (JahFog_piece_*) -> its line (sky) + atom + engine.atom_ (HlmsAtom compiles it)")
    check(all(t_ in (fork_tok(f_) or []) for f_ in ("Components/Hlms/Pbs/src/OgreHlmsPbs.cpp",
                                                    "Samples/Media/Hlms/Pbs/Any/Main/800.PixelShader_piece_ps.any")
              for t_ in ("atom", "engine.atom_")),
          "26. the fork's Hlms Pbs/Common lines reach atom + engine.atom_")
    # 27. THE PRICE RULE (the lead, B4's read): --record-times never overwrites a HAND (solo median) price; the run log's
    # median is written only from records that ran ALONE (the census: no other ctest; the ctest at -j1), never a gate's
    import tempfile as _tf
    tdir = _tf.mkdtemp(prefix="price-rule-")
    try:
        runs_ = os.path.join(tdir, "runs"); os.makedirs(runs_)
        base_ = os.path.join(tdir, "baseline.json"); json.dump({"quiet_load_max": 5.0}, open(base_, "w"))
        tf = os.path.join(tdir, "gate-times.txt")
        open(tf, "w").write("# header\napi.contract 3.00  solo median n=4 (the lead, 2026-10-12)\n")
        recs = []
        def rec(row, sec, jobs, oc):
            return {"suite": row, "arm": None, "verdict": "PASS", "seconds": sec, "ts": "2026-10-12T12:00:00+02:00",
                    "box": {"jobs": jobs, "load_mean": 0.5,
                            "census": {"other_ctests": oc, "builds": 0, "gpu_competitors": []}}}
        recs += [rec("api.contract", 30.0, 1, 0)] * 3          # solo records of a HAND-priced row: the hand price stays
        recs += [rec("app.help_exits", 0.5, 1, 0)] * 3         # solo records, no hand price: written solo-quiet
        recs += [rec("app.cli_options", 70.0, 4, 0)] * 3       # a gate's runs beside siblings (-j4): never a price
        recs += [rec("app.first_launch", 9.0, 1, 2)] * 3       # another ctest on the box: never a price
        with open(os.path.join(runs_, "2026-10-12-x.jsonl"), "w") as f:
            f.writelines(json.dumps(r_) + "\n" for r_ in recs)
        env_keep = {k: os.environ.get(k) for k in ("JAH_RUN_LOG_DIR", "JAH_BOX_BASELINE")}
        os.environ["JAH_RUN_LOG_DIR"], os.environ["JAH_BOX_BASELINE"] = runs_, base_
        tf_keep = gs.TIMES_FILE
        gs.TIMES_FILE = tf
        try:
            import io, contextlib
            with contextlib.redirect_stdout(io.StringIO()):
                gs.record_times(build)
            got = gs.parse_times(tf)
        finally:
            gs.TIMES_FILE = tf_keep
            for k, v in env_keep.items():
                if v is None: os.environ.pop(k, None)
                else: os.environ[k] = v
        check(got.get("api.contract") == (3.0, "solo median n=4 (the lead, 2026-10-12)"),
              "27. --record-times leaves a solo-median (hand) line untouched (%s)" % (got.get("api.contract"),))
        check((got.get("app.help_exits") or (0, ""))[1].startswith("solo-quiet n=3"),
              "27. ...writes the run log's median from records that ran ALONE (%s)" % (got.get("app.help_exits"),))
        check("app.cli_options" not in got and "app.first_launch" not in got,
              "27. ...and never from a gate's runs beside siblings (-j4) or with another ctest on the box (%s %s)"
              % (got.get("app.cli_options"), got.get("app.first_launch")))
    finally:
        import shutil as _sh
        _sh.rmtree(tdir, ignore_errors=True)
    # 24. a fallback is its directory's subsystem, never the tier
    S = sel(["tests/gi/tsa_new_dir/input.bin"])
    gi_rows = [n for n, t in inv0.items() if t["dir"] == "gi"]
    check(not S.full_tier and set(S.selected) <= set(gi_rows) and S.selected,
          "24. a new tests/ data path selects ITS DIRECTORY's rows (%d of tests/gi), never the whole tier"
          % len(S.selected))
    S = sel(["newtopdir/tool.cpp"])
    check(S.fallback and not S.full_tier and not S.selected,
          "24. ...and a top-level path no directory rule owns selects nothing more (never the tier)")


def main(source, build):
    tool = os.path.join(source, "scripts", "gate-scope.py")
    if not os.path.isfile(tool):
        print("gate_scope_rules: cannot find %s" % tool)
        return 2
    if not os.path.isfile(os.path.join(build, "CTestTestfile.cmake")):
        print("gate_scope_rules: %s is not a configured build dir" % build)
        return 2

    # 1. a code path selects suites, and the ones its rule names.
    code, out, err = run([tool, "--files", "src/services/vrworld.cpp", "--build", build], source)
    check(code == 0, "a code path exits 0 (%d)%s" % (code, ("\n" + err) if code else ""))
    check("SCOPED tier:" in out and "NOTHING to gate" not in out,
          "a code path selects a scoped tier rather than nothing")
    # MODULAR-GATE-1: the rule picks the APP rows (the vr_session pool runs the app — its
    # verbs_session arm was the vr.verbs_session row until SUITE-POOL-1); a compiled
    # row runs only when the build graph says its executable contains the file — test_vr_session
    # does not compile vrworld.cpp, so vr.session is not in (it used to be, by directory).
    # (TESTING-SPEED-A for TESTING-SPEED-B batch 4: pool.vr_session moves to stage-close — the VR panel row stands in)
    check("ui.vr_panel" in out, "the vr app suites are in it (the file's own area rule)")
    check("vr.session " not in out and "vr.session|" not in out.replace("\\", "") and "vr.session)" not in out.replace("\\", ""),
          "...and the compiled vr.session is not: its executable does not contain vrworld.cpp (the graph)")
    # TESTING-SPEED-A item 5: the smoke pair rides a change to the test harness only (case 22), never a product path
    check("app.startup_quiet" not in out and "api.contract" not in out,
          "the smoke pair rides no product path")
    absolute_form = out

    # 2. THE TRAP: `--build .` from inside the build dir is the same answer.
    code, out, err = run([tool, "--files", "src/services/vrworld.cpp", "--build", "."], build)
    check(code == 0, "`--build .` from inside the build dir exits 0 (%d)" % code)
    check("NOTHING to gate" not in out,
          "`--build .` from inside the build dir does NOT report an empty gate "
          "(the SQUARE-1 trap: it used to, with exit 0)")
    # The two runs print the same selection; the cost table can differ by the
    # dir the times were read from, so compare the ctest line.
    # The SCOPED command, with its pool-arm prefix when arms are selected (`JAH_POOL_ARMS='…' ctest
    # -j…`). The first cut matched only a bare `ctest -j` line, so it compared the TARGET rows'
    # command whenever the scoped one carried the prefix — and read "" once the selection had no
    # target row (DAG-LOCK-1 took atom.dag_bound's label off).
    def ctest_line(text):
        for line in text.splitlines():
            if re.match(r"^(JAH_POOL_ARMS='[^']*' )?ctest -j", line):
                return line
        return ""
    check(ctest_line(out) == ctest_line(absolute_form) and ctest_line(out) != "",
          "the CWD-relative and absolute forms select the same suites")

    # 3. a directory that registers no suite is an ERROR.
    with tempfile.TemporaryDirectory() as empty:
        code, out, err = run([tool, "--files", "src/services/vrworld.cpp", "--build", empty], source)
        check(code == 2, "an unconfigured --build dir exits 2 (%d)" % code)
        check("registers no ctest suite" in err,
              "...and says so, naming the directory")
        check("NOTHING to gate" not in out,
              "...instead of reporting an empty gate")

    # 4. the honest empty selection: docs only. (Not docs/TESTING_GATE.md: case 9's
    # lint reads that one, so it selects the hygiene rows.)
    code, out, err = run([tool, "--files", "docs/BUILDING_LINUX.md", "--build", build], source)
    check(code == 0, "a docs-only change exits 0 (%d)" % code)
    check("NOTHING to gate" in out,
          "a docs-only change still selects nothing (the case lanes rely on)")
    check("MERGE TIER" not in out,
          "...and does not fall back to the merge tier")

    # 5. the invariant, on a file whose own rule is broad.
    code, out, err = run([tool, "--files", "src/io/scenereader.cpp", "--build", build], source)
    check(code == 0 and "NOTHING to gate" not in out,
          "a source file with no precise rule still gates (exit %d)" % code)

    # 6. TARGET TESTS: they run, they are reported, and they do not gate.
    # tests/gi/test_gi_chain_face.cpp registers BOTH rows of the chain-face pair,
    # so it selects a target suite and an ordinary one from one path.
    #
    # THE COMPARISONS ARE MADE ON UNESCAPED TEXT. The suite names inside a ctest
    # -R regex are `re.escape`d, so `gi.chain_face` appears as `gi\.chain_face`
    # and a plain substring test for the readable name fails on a correct
    # command. Stripping backslashes is the whole of it.
    def plain(text):
        return text.replace("\\", "")

    code, out, err = run([tool, "--files", "tests/gi/test_gi_chain_face.cpp",
                          "--build", build], source)
    check(code == 0, "a path that selects a target suite exits 0 (%d)" % code)
    gating, target_cmd, merge = "", "", ""
    for line in out.splitlines():
        if not line.startswith("ctest -j"):
            continue
        if "NOT gating" in line:
            target_cmd = line
        elif "-LE" in line:
            merge = line
        elif not gating:
            gating = line
    check("gi.chain_face_target" in out,
          "the target row is SELECTED (it must run, or nobody sees the distance to the bar)")
    check("TARGET TESTS" in out and "do NOT decide this gate" in out,
          "...and the tool says in words that it does not decide the gate")
    check(gating != "" and "gi.chain_face_target" not in plain(gating),
          "the GATING ctest command does not name the target row")
    check("gi.chain_face|" in plain(gating) or "gi.chain_face)" in plain(gating),
          "...while the ordinary row of the same pair IS in the gating command")
    check(target_cmd != "" and "gi.chain_face_target" in plain(target_cmd)
          and " -j1 " in target_cmd,
          "the target suites get a ctest command of their own, at -j1")

    # ...and the MERGE tier's own exclusion carries the label, from the same
    # definition: a tier command that filters a label the tool does not know
    # about (or the reverse) is how a stale exclusion survives a year. A path
    # with no rule is what prints the tier command.
    code2, out2, err2 = run([tool, "--files", "CMakeLists.txt", "--build", build], source)
    for line in out2.splitlines():
        if line.startswith("ctest -j") and "-LE" in line:
            merge = line
    check("photon-target" in plain(merge) and "stage-close" in plain(merge),
          "the MERGE tier's -LE carries photon-target beside the stage-close labels")

    # 7. THE FALLBACK HONOURS -j (DEVPROCESS-1 item 2). A lane beside other live
    # lanes runs `-j 2`; the fallback to the MERGE tier used to print and RUN a
    # hardcoded -j4 regardless. A path with no rule is what falls back.
    code, out, err = run([tool, "--files", "CMakeLists.txt", "--build", build, "--json", "-j", "2"], source)
    try:
        doc = json.loads(out)
    except ValueError:
        doc = {}
    # TESTING-SPEED-A item 1: the top-level build file without a diff is THE TIER BY RULE (a fallback never is)
    check(code == 0 and doc.get("full_tier") and not doc.get("fallback"),
          "the top-level CMakeLists.txt without a range is the MERGE tier by rule (exit %d)" % code)
    command = doc.get("command", "")
    check(command.startswith("ctest -j2 ") and "-j4" not in command,
          "the tier's JSON command carries the requested -j2 (%r)" % command[:40])
    check("ddgi_raster" not in command and " -E " not in command,
          "...and excludes no suite by name (the dead gi.ddgi_raster filter is deleted)")
    code, out, err = run([tool, "--files", "CMakeLists.txt", "--build", build, "-j", "3"], source)
    check(any(l.startswith("ctest -j3 ") and "-LE" in l for l in out.splitlines()),
          "...and so does the printed MERGE tier line (-j3)")

    # 8. THE DECODE'S OWN SUITE (PHOTON-HIT-SHADE-1 audit F2): the Atom media, HlmsAtom and
    # the fork pin (Ogre's Pbs pieces are the decode's text too) select engine.atom_parity;
    # a lane that changed all three gated without it once.
    for path in ("irisgl/engine/media/Hlms/Atom/Any/800.Atom_piece_ps.any",
                 "irisgl/engine/src/HlmsAtom.cpp"):
        code, out, err = run([tool, "--files", path, "--build", build], source)
        check(code == 0 and ("engine.atom_parity|" in plain(out) or "engine.atom_parity)" in plain(out)),
              "%s selects engine.atom_parity" % path)
    # the fork pin reaches everything: the MERGE tier BY RULE (PHOTON_ATOM_CONTRACT §7b rule 4),
    # which carries engine.atom_parity — not a fallback, and said so
    code, out, err = run([tool, "--files", "irisgl/thirdparty/ogre-next", "--build", build, "--json"], source)
    try:
        doc = json.loads(out)
    except ValueError:
        doc = {}
    check(code == 0 and doc.get("full_tier") and not doc.get("fallback")
          and "-LE" in doc.get("command", ""),
          "irisgl/thirdparty/ogre-next selects the MERGE tier by rule (the tier carries engine.atom_parity)")

    # 9. THE TIER DOC QUOTES THE SCRIPT, NEVER A COPY OF ITS SET (POST-C-FIXES-1).
    # docs/TESTING_GATE.md's MERGE row carried a literal
    # -LE "^(benchmark|<the shader-cache attack label>)$" that went stale the day TARGET_LABELS
    # joined the tier (it never named photon-target). The set lives in
    # gate-scope.py alone; the doc names `--merge-tier`, which prints the command.
    import re as _re
    doc_path = os.path.join(source, "docs", "TESTING_GATE.md")
    with open(doc_path, errors="replace") as fh:
        doc_lines = fh.read().splitlines()
    literal = [i + 1 for i, l in enumerate(doc_lines) if _re.search(r'-LE\s+["\']\^?\(', l)]
    check(not literal,
          "docs/TESTING_GATE.md quotes no literal -LE label list (lines %s) - it names "
          "`gate-scope.py --merge-tier`" % literal)
    check(any("--merge-tier" in l for l in doc_lines),
          "...and it names `gate-scope.py --merge-tier` as the tier's command")
    code, out, err = run([tool, "--merge-tier", "-j", "2"], source)
    check(code == 0 and out.strip().startswith("ctest -j2 ") and "photon-target" in plain(out)
          and "stage-close" in plain(out)
          and "benchmark" not in plain(out),
          "`--merge-tier -j 2` prints the MERGE tier at -j2 with every excluded label (%r)" % out.strip())
    code, out, err = run([tool, "--files", "docs/TESTING_GATE.md", "--build", build], source)
    check(code == 0 and "source.gate_scope_rules" in plain(out),
          "an edit to docs/TESTING_GATE.md selects this guard")

    # 10. A SUITE REGISTERED THROUGH A HELPER BELONGS TO ITS CALL SITE'S DIR
    # (POST-C-FIXES-1). jah_gpu_exclusive_test's add_test lives in tests/CMakeLists.txt,
    # so ctest's backtrace put every GPU-lock suite in dir "tests" and a change to the
    # suite's OWN source selected everything in its directory except the suite itself
    # (open.responsive for tests/openasync/test_open_responsive.cpp - measured; deleted by TESTING-SPEED-B3).
    # (TESTING-SPEED-A for TESTING-SPEED-B batch 2: open.responsive is deleted; the two gi GPU-lock rows carry the case)
    for path, suite in (("tests/gi/test_gi_budget.cpp", "gi.budget"),
                        ("tests/gi/test_gi_field_scroll.cpp", "gi.field_scroll")):
        code, out, err = run([tool, "--files", path, "--build", build, "--json"], source)
        try:
            doc = json.loads(out)
        except ValueError:
            doc = {}
        check(code == 0 and suite in doc.get("suites", []),
              "%s selects its own GPU-lock suite %s (%d suites selected)" % (path, suite, len(doc.get("suites", []))))

    # 11. THE SUBJECTS SELECT THEIR SUITES (audit S1/S2). Only suites this build registers
    # are asked for: the Atom rows exist only where the Vulkan stack does, and a box without
    # them must not red on their absence.
    inventory = set()
    try:
        # from a copy of the CTestTestfile tree: a listing in the build dir truncates the
        # LastTest.log of the gate this guard runs inside (gate_graph.ctest_inventory)
        sys.path.insert(0, os.path.join(source, "scripts"))
        import gate_graph
        raw = gate_graph.ctest_inventory(build)[0]
        inventory = {t["name"] for t in json.loads(raw).get("tests", [])}
    except (ValueError, OSError):
        pass
    for path, suites in (
            ("irisgl/engine/src/OgreChain.cpp", ("engine.atom_draw", "engine.atom_parity", "defaults.exposure_plane")),
            ("irisgl/import/meshbake.cpp", ("atom.cluster_cut", "atom.cluster_crack", "engine.lod_rule_parity")),
            # (archive.responsive is KEPT: the archiver's only merge-tier guard — SPECS/TESTING_SPEED_ROWS.md at 5db89728)
            ("src/services/projectarchiver.cpp", ("archive.responsive",)),
            ("src/services/avatarassets.cpp", ("avatar.responsive",)),
            ("src/modules/avatar/api/avatarapi.cpp", ("avatar.responsive",)),
            ("src/modules/avatar/avatarmodule.cpp", ("avatar.responsive",))):
        code, out, err = run([tool, "--files", path, "--build", build, "--json"], source)
        try:
            doc = json.loads(out)
        except ValueError:
            doc = {}
        chosen = set(doc.get("suites", [])) | set(doc.get("targets", []))
        for suite in suites:
            if suite not in inventory:
                # A MISSING ROW IS A RED, never a skip (fix round 3): the subject's guard must exist in the build
                check(False, "%s's guard %s is registered in this build (it is not)" % (path, suite))
                continue
            check(code == 0 and not doc.get("fallback") and suite in chosen,
                  "%s selects %s (%d suites selected)" % (path, suite, len(chosen)))

    # 12. THE STAGE-CLOSE LABELS (D6B-GATE-SHAPE).
    # (TESTING-SPEED-A for TESTING-SPEED-B batch 2: open.responsive is deleted — ui.selection_cost and its quiet-box
    # millisecond twin carry the case)
    code, out, err = run([tool, "--files", "tests/ui/test_selection_cost.cpp",
                          "--build", build, "--json"], source)
    try:
        doc = json.loads(out)
    except ValueError:
        doc = {}
    chosen = set(doc.get("suites", [])) | set(doc.get("targets", []))
    check("ui.selection_cost" in chosen and "ui.selection_cost.timing" not in chosen,
          "the push row is scoped, its quiet-box millisecond twin is not")
    code, out, err = run([tool, "--stage-close-tier"], source)
    check(code == 0 and "-L " in out and "stage-close" in plain(out) and "quiet-box" in plain(out),
          "`--stage-close-tier` prints the stage-close batch's parallel phase (%r)" % out.strip())
    code, out, err = run([tool, "--stage-close-tier-serial"], source)
    check(code == 0 and "stage-close" in plain(out) and "quiet-box" in plain(out) and " -j1 " in out,
          "`--stage-close-tier-serial` prints its quiet-box phase at -j1 (%r)" % out.strip())

    # 13. THE TREND CHECK (TEST-1): `gate_runlog.py trend` names the first tip of a step and not a spike.
    sys.path.insert(0, os.path.join(source, "scripts"))
    import gate_runlog as grl
    check(grl.target_value("0.380 (bar 0.90) 1080p FULL-res") == 0.38
          and grl.target_value("12/255 (bar 2/255) - the sealed room") == 12 / 255
          and grl.target_value("[march: glossy floor 0.2, x] grain 9.52x the still case's (bar 1.5)") == 9.52
          and grl.target_value("scale.shadow_cut world caster CPU ratio 0.177 (bar 0.33)") == 0.177,
          "the target value parser reads the leading number, a ratio, or the last number before `bar`")
    def series(vals):                  # a straight line of tips t0 < t1 < ... (each descends from all before)
        rows = [("2026-10-01T%02d" % i, "t%02d" % i, "lane%d" % i, v, 1) for i, v in enumerate(vals)]
        return rows, {r[1]: {x[1] for x in rows[:i]} for i, r in enumerate(rows)}
    rows, anc = series([1.00, 1.02, 0.99, 1.01, 1.00, 3.5, 1.01, 0.98, 1.02, 1.00])
    check(grl.trend_steps(rows, anc) == [], "one contended spike in a flat series is not a step")
    rows, anc = series([1.00, 1.02, 0.99, 1.01, 1.00, 1.60, 1.61, 1.59, 1.62, 1.60])
    st = grl.trend_steps(rows, anc)
    check(len(st) == 1 and st[0]["kind"] == "STEP" and rows[st[0]["i"]][1] == "t05",
          "a level change is ONE step named at its first tip (%r)" % [(rows[x["i"]][1], x["kind"]) for x in st])
    rows, anc = series([1.00, 1.02, 0.99, 1.01, 1.00, 1.60])
    st = grl.trend_steps(rows, anc)
    check(len(st) == 1 and st[0]["kind"] == "UNCONFIRMED" and rows[st[0]["i"]][1] == "t05",
          "the gate that brings a step prints it UNCONFIRMED (one reading)")
    # two lanes branched from t04: lane A steps up, lane B does not -> B is never compared with A
    base, _ = series([1.00, 1.02, 0.99, 1.01, 1.00])
    a = [("2026-10-02T%02d" % i, "a%d" % i, "A", 1.6, 1) for i in range(3)]
    b = [("2026-10-02T%02dZ" % i, "b%d" % i, "B", 1.0, 1) for i in range(3)]
    rows = sorted(base + a + b)
    anc = {r[1]: set() for r in rows}
    for i, r in enumerate(base): anc[r[1]] = {x[1] for x in base[:i]}
    for lane in (a, b):
        for i, r in enumerate(lane): anc[r[1]] = {x[1] for x in base} | {x[1] for x in lane[:i]}
    st = grl.trend_steps(rows, anc)
    check([rows[x["i"]][1] for x in st] == ["a0"],
          "parallel lanes: lane A's step is named once at a0, and lane B's tips stay in band (%r)"
          % [rows[x["i"]][1] for x in st])

    # 13b. EVERY TARGET LINE IS ITS OWN SERIES (V2-P0B; the deep audit's T6): the log kept a row's FIRST
    # `target:` line only, so gi.chain_face_target read "0.0000 MET" while its [sky-low] arm was red.
    face = "the face of cascade 0 is a boundary in a data structure and not in the world"
    out3 = ("target: 0.0000 (bar 0.0500) %s [flat, chain, cam x=0] -- MET\nFAIL: x\n"
            "target: 0.2100 (bar 0.0500) %s [sky-low, chain, cam x=0]\n"
            "target: 12.5 (bar none yet) W1 ms: c0 rebuild\n" % (face, face))
    lines3 = grl._suite_facts(out3)[0]
    check(lines3 is not None and len(lines3) == 3, "a row printing 3 target lines records 3 (%r)" % lines3)
    logs = tempfile.mkdtemp(prefix="runlog-targets-")
    old_dir = os.environ.get("JAH_RUN_LOG_DIR")
    os.environ["JAH_RUN_LOG_DIR"] = logs
    try:
        for i in range(3):
            grl.append_records([{"suite": "gi.chain_face_target", "arm": None, "verdict": "FAIL", "gating": False,
                                 "ts": "2026-10-08T0%d:00:00" % i, "tip": {"studio": "%040d" % i},
                                 "box": {}, "targets": lines3}], "target", "%040d" % i)
        grl.append_records([{"suite": "gi.old_row", "arm": None, "verdict": "PASS", "ts": "2026-10-08T00:00:00",
                             "tip": {"studio": "%040d" % 0}, "box": {}, "target": "0.380 (bar 0.90) 1080p"}],
                           "scoped", "%040d" % 0)
        ser = grl.trend_series(30, ["gi.chain_face_target"])
        keys = {k[1]: rows for k, rows in ser.items()}
        check(len(keys) == 3, "the trend sees the row's 3 target lines as 3 series (%r)" % sorted(keys))
        low = [rows for k, rows in keys.items() if "sky-low" in k]
        check(len(low) == 1 and [x[3] for x in low[0]] == [0.21] * 3,
              "the [sky-low] line red on the row's SECOND line reads 0.21 (not the first line's 0.0000 MET)")
        check([k[1] for k in grl.trend_series(30, ["gi.old_row"])] == ["# (bar #) #p"],
              "a record from before V2-P0B (one `target` line) reads as the 1-target case")
        pro = grl.promotions(30, "%040d" % 2)
        check(len(pro) == 1 and "[flat, chain" in pro[0][1],
              "PROMOTE: the line MET at its last 3 tips, not the red [sky-low] line nor a `bar none` line (%r)" % pro)
    finally:
        if old_dir is None: os.environ.pop("JAH_RUN_LOG_DIR", None)
        else: os.environ["JAH_RUN_LOG_DIR"] = old_dir
        shutil.rmtree(logs, ignore_errors=True)

    # 14. ONE BUILD, ONE SELECTION, FROM ANY CHECKOUT (TEST-1 fix round, CLIP-REF-1's finding): the
    # merge judge runs d-build's copy of the scripts against a LANE's build, and the build's graph
    # names files under the lane's tree — read against the script's own checkout every touched path
    # was "not in the build graph" (448 rows against the lane's own 372). The scripts copied to a
    # foreign root must select exactly what they select at home, through the graph.
    foreign = tempfile.mkdtemp(prefix="gate-scope-root-")
    try:
        os.makedirs(os.path.join(foreign, "scripts"))
        for f in ("gate-scope.py", "gate_graph.py", "gate_runlog.py", "vram_tokens.py", "kernel_xid.py"):
            if os.path.isfile(os.path.join(source, "scripts", f)):
                shutil.copy(os.path.join(source, "scripts", f), os.path.join(foreign, "scripts", f))
        probe = ["--files", "irisgl/engine/src/OgreFrameMonitor.cpp", "src/scripting/modules/perfapi.cpp",
                 "--build", build, "--json"]
        c1, o1, e1 = run([tool] + probe, source)
        c2, o2, e2 = run([os.path.join(foreign, "scripts", "gate-scope.py")] + probe, foreign)
        try:
            d1, d2 = json.loads(o1), json.loads(o2)
        except ValueError:
            d1, d2 = {}, {}
        s1 = set(d1.get("suites", [])) | set(d1.get("targets", []))
        s2 = set(d2.get("suites", [])) | set(d2.get("targets", []))
        check(c1 == 0 and c2 == 0 and s1 and s1 == s2 and "not in the build graph" not in o2 + e2,
              "the same files and build select the same %d rows from a foreign checkout (%d there; "
              "only here %s, only there %s)" % (len(s1), len(s2), sorted(s1 - s2)[:5], sorted(s2 - s1)[:5]))
    finally:
        shutil.rmtree(foreign, ignore_errors=True)

    # 15. A SYMLINKED ROOT IS THE SAME TREE (SYMLINK-ROOT-1, the box's /home/jahshaka/Developer ->
    # /mnt/work/Developer move): the build graph names files under the spelling the tree was CONFIGURED
    # with; python's getcwd()/realpath() return the resolved one, so a run reached through the other
    # spelling read "not in the build graph" for every file and fell back to path rules (528 rows against
    # 457). The tree reached as configured, resolved, and through a fresh symlink — scripts, cwd and
    # --build all spelled that way, `--build .` from inside the build dir included — selects the same rows.
    link = tempfile.mkdtemp(prefix="gate-scope-link-")
    try:
        os.symlink(os.path.realpath(source), os.path.join(link, "tree"))
        os.symlink(os.path.realpath(build), os.path.join(link, "build"))
        files = ["--files", "irisgl/engine/src/OgreFrameMonitor.cpp", "src/scripting/modules/perfapi.cpp", "--json"]
        forms = {"as configured": (source, build),
                 "resolved": (os.path.realpath(source), os.path.realpath(build)),
                 "through a symlink": (os.path.join(link, "tree"), os.path.join(link, "build"))}
        sel = {}
        for name, (src, bld) in forms.items():
            for how, args, cwd in (("absolute", ["--build", bld], src), ("`--build .`", ["--build", "."], bld)):
                c, o, e = run([os.path.join(src, "scripts", "gate-scope.py")] + files + args, cwd)
                try:
                    d = json.loads(o)
                except ValueError:
                    d = {}
                sel[name + ", " + how] = (c, set(d.get("suites", [])) | set(d.get("targets", [])),
                                          "not in the build graph" in o + e)
        ref = sel["as configured, absolute"][1]
        for k, (c, s, missed) in sel.items():
            check(c == 0 and ref and s == ref and not missed,
                  "the tree %s (%s) selects the same %d rows through the graph (exit %d, %d rows%s; "
                  "only here %s, only there %s)" % (k.split(", ")[0], k.split(", ")[1], len(ref), c, len(s),
                                                    ", NOT IN THE BUILD GRAPH" if missed else "",
                                                    sorted(ref - s)[:5], sorted(s - ref)[:5]))
    finally:
        shutil.rmtree(link, ignore_errors=True)

    speed_cases(source, build)

    if FAILURES:
        print("source.gate_scope_rules: FAILED (%d)" % len(FAILURES))
        return 1
    print("source.gate_scope_rules: PASSED")
    return 0


if __name__ == "__main__":
    # every check line as it happens (DOOR-HYGIENE-1): under ctest stdout is a pipe, block-buffered — a row killed at its
    # TIMEOUT printed NOTHING of the checks it had passed (VOXEL-READOUT-1's self-audit, 2026-10-12)
    sys.stdout.reconfigure(line_buffering=True)
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
