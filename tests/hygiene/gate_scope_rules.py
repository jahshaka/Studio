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

THE CASES, each a rule of the tool rather than a number:
  1. A CODE PATH SELECTS ROWS: src/services/vrworld.cpp selects the vr app rows (its area rule) and NOT the compiled
     vr.session, whose executable the build graph says does not contain it; no smoke pair for a product path.
  2. `--build .` FROM INSIDE THE BUILD DIR MEANS THE BUILD DIR — the same selection as the absolute form.
  3. A DIRECTORY THAT IS NOT A BUILD DIR IS AN ERROR (exit 2, naming it), not an empty answer.
  4. THE HONEST EMPTY SELECTION SURVIVES: a docs-only change selects nothing and exits 0.
  5. A source file with no precise rule still reaches rows.
  8. The decode's own suite (engine.atom_parity) rides the Atom media and HlmsAtom; a fork pin without a readable diff
     reaches every row by rule.
  10. A suite registered through a helper (jah_gpu_exclusive_test) belongs to the directory that CALLS the helper.
  11. THE SUBJECTS SELECT THEIR SUITES (the suites audit's S1/S2).
  15. A symlinked root is the same tree (with case 2: `--build .` through a fresh symlink).
  16-26. THE SUBSYSTEM TABLE (speed_cases below): a representative file per table line, the core by what changed (a
      Types.h field add, OgreEngine.cpp's frame loop vs vrHaptic, a comment), the fork by component, the picture set,
      the tools' rows for the tools' paths, a fallback that is never the whole inventory.

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

    def sel(files, revs=None):
        real = gs.Revs
        if revs is not None:
            gs.Revs = revs
        try:
            return gs.select(files, "BASE..TIP" if revs is not None else None, build, graph=graph,
                             inv=copy.deepcopy(inv0), quiet_graph=True)
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
        bad = {f: [n for n in strictly(S, f) if not inv0[n].get("screenshot")][:3] for f in never
               if [n for n in strictly(S, f) if not inv0[n].get("screenshot")]}
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
    hdr_ = [n for n in strictly(S, "hdr") if not inv0[n].get("screenshot")]
    check(of(S, "gi") and not hdr_ and not nonshot and not S.fallback,
          "17. a Types.h field ADDED and used by OgrePhotonView.cpp -> the users' subsystem (gi %d + the screenshot rows), "
          "no hdr row, no other vr row (%s)" % (len(of(S, "gi")), (hdr_ + nonshot)[:4]))
    # 18. OgreEngine.cpp by the ENCLOSING FUNCTION: a frame-loop edit -> every engine fixture + the picture set; a VR edit
    ep = "irisgl/engine/src/OgreEngine.cpp"
    ea = open(os.path.join(source, ep), errors="replace").read()
    fx = None
    # (the frame's BODY is runFrame since ENGINE-UPGRADE-U3 stage 1: renderOneFrame is one turn of the render loop)
    for fn, needle in (("runFrame", "bool OgreEngine::runFrame(FrameInput &record, bool any) {\n"),
                       ("vrHaptic", "bool OgreEngine::vrHaptic(int hand, float amplitude01, float seconds) {\n")):
        check(needle in ea, "18. %s is in OgreEngine.cpp" % fn)
        if needle not in ea:
            continue
        S = sel([ep], revs=fake({ep: (ea, ea.replace(needle, needle + "    (void)0x5a; // tsa\n", 1))}))
        if fn == "runFrame":
            fx = S
            pics = [n for n, t in inv0.items() if t.get("picture")]
            check(len(S.selected) > 300 and all(p in S.selected for p in pics) and of(S, "gi") and of(S, "hdr"),
                  "18. an edit inside the frame body (runFrame) -> every engine fixture + the picture set + every rendering app "
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
          and not [p for p in pics if inv0[p]["labels"] & gs.NEVER_SELECTED],
          "21. the picture set: the hdr/ssr/looks/mirror fixtures + the screenshot app rows (%d) and arms (%d pools); no "
          "gi/photon row, no measure row" % (len(pics), len(arms)))
    # 22. the tools' rows for the tools' paths
    S = sel(["src/services/sceneeditservice.cpp"])
    hy = [n for n in S.selected if inv0[n]["labels"] & {"hygiene", "tooling"}]
    check(S.selected and not hy, "22. a product path (its rule names the hygiene dir) selects no hygiene/tooling row "
          "(%s)" % hy[:4])
    S = sel(["scripts/gate-times.txt"])
    check("source.row_budget" in S.selected, "22. scripts/gate-times.txt (the prices) selects the budget lint")
    S = sel(["tests/CMakeLists.txt"])
    check("app.startup_quiet" in S.selected and "api.contract" in S.selected,
          "22. the test harness (tests/CMakeLists.txt) selects the smoke pair")
    S = sel(["scripts/gate-scope.py"])
    check("source.testing_rules" in S.selected and "source.gate_scope_rules" in S.selected
          and not [n for n in S.selected if not inv0[n]["labels"] & {"hygiene", "tooling"}],
          "22. a tool path selects the tools' rows (hygiene/tooling) and nothing else")
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
    tail_ = va + "\nstatic int tsaUnnamedThing() { return 0x5a; }\n"
    S = sel([vp], revs=fake({vp: (va, tail_)}))
    apps = [n for n in S.selected if inv0[n]["app"] and not inv0[n].get("picture") and not inv0[n].get("picture_arms")]
    pics = [n for n, t in inv0.items() if t.get("picture")]
    check(all(p in S.selected for p in pics) and len(S.selected) > 200 and not apps,
          "25. an UNNAMED function of OgreView.cpp -> every engine fixture + the picture set (%d rows), no other app row "
          "(%s)" % (len(S.selected), apps[:4]))
    # round 15: a function the map does not name is read by its NAME's keyword first (reflect-fix-1's readSsrHistoryHdr)
    S = sel([vp], revs=fake({vp: (va, va + "\nstatic int readSsrTsaHistory() { return 0x5a; }\n")}))
    fams_ = {n.split(".")[0] for n in S.selected}
    check(S.selected and "ssr" in fams_ and not fams_ & {"vr", "particles", "ui", "hdr"} and len(S.selected) < 100,
          "25. round 15: a NEW OgreView function named by a keyword (readSsrTsaHistory: 'Ssr') -> the reflection rows "
          "only, never every engine fixture (%d rows: %s)" % (len(S.selected), sorted(fams_)[:8]))
    # round 21: THE SCENE'S ONE LIGHT LIST (P1C-LIGHT-LIST) has its table line: the light rows, scale.lights, the GI and
    # Atom (its three consumers) — never "no rule, no symbol, no graph owner"
    S = sel(["irisgl/engine/src/SceneLightList.cpp"])
    fams_ = {n.split(".")[0] for n in S.selected}
    check({"lights", "gi", "atom"} <= fams_ and not S.fallback and not S.full_tier
          and not fams_ & {"vr", "ui", "particles"},
          "26. round 21: SceneLightList.cpp -> the light rows + gi + atom, no fallback (%d rows: %s)"
          % (len(S.selected), sorted(fams_)[:10]))
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
    # 4. the honest empty selection (docs only); 5. a source file with no precise rule still reaches rows
    S = sel(["docs/BUILDING_LINUX.md"])
    check(not S.selected and not S.fallback, "4. a docs-only change selects nothing (%s)" % sorted(S.selected)[:3])
    check(bool(sel(["src/io/scenereader.cpp"]).selected), "5. src/io/scenereader.cpp (no precise rule) still reaches rows")
    # 8. the Atom media and HlmsAtom reach the decode's own rows; the fork pin with no diff to read reaches every row
    for path in ("irisgl/engine/media/Hlms/Atom/Any/800.Atom_piece_ps.any", "irisgl/engine/src/HlmsAtom.cpp"):
        S = sel([path])
        check([n for n in S.selected if n.startswith("engine.atom_")], "8. %s reaches the engine.atom_ rows" % path)
    check(bool(sel(["irisgl/thirdparty/ogre-next"]).full_tier), "8. the fork pin with no diff to read reaches every row")
    # 10. a row registered through a helper (jah_gpu_row's add_test is in tests/CMakeLists.txt) is its CALL SITE's: a
    # change to a test source selects its own row
    S = sel(["tests/gi/test_gi_cliff.cpp"])
    check("gi.cliff" in S.selected, "10. tests/gi/test_gi_cliff.cpp selects its own row gi.cliff (%d rows)" % len(S.selected))
    # 11. THE SUBJECTS SELECT THEIR GUARDS (the suites audit's S1/S2)
    for path, rows in (("irisgl/engine/src/OgreChain.cpp", ("engine.atom_draw", "engine.atom_parity", "defaults.exposure_plane")),
                       ("irisgl/import/meshbake.cpp", ("atom.cluster_cut", "atom.cluster_crack", "engine.lod_rule_parity")),
                       ("src/services/projectarchiver.cpp", ("archive.responsive",)),
                       ("src/services/avatarassets.cpp", ("avatar.responsive",)),
                       ("src/modules/avatar/api/avatarapi.cpp", ("avatar.responsive",))):
        S = sel([path])
        for r in rows:
            check(r in inv0 and r in S.selected and not S.fallback, "11. %s selects %s" % (path, r))
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
    if not os.path.isfile(os.path.join(build, "CTestTestfile.cmake")):
        print("gate_scope_rules: %s is not a configured build dir" % build)
        return 2
    rows_of = lambda out: set((json.loads(out) if out.strip().startswith("{") else {}).get("rows", []))
    # 1. a code path selects rows, the ones its rule names, and NOT a compiled row whose executable lacks the file
    code, out, err = run([tool, "--files", "src/services/vrworld.cpp", "--build", build, "--json"], source)
    r1 = rows_of(out)
    check(code == 0 and "ui.vr_panel" in r1, "1. a code path selects its rule's rows (ui.vr_panel; exit %d)" % code)
    check("vr.session" not in r1 and "app.startup_quiet" not in r1 and "api.contract" not in r1,
          "1. ...not the compiled vr.session (its executable does not contain vrworld.cpp), no smoke pair")
    # 2. THE TRAP: `--build .` from inside the build dir is the same answer; 15. so is a symlinked root
    link = tempfile.mkdtemp(prefix="gate-scope-link-")
    try:
        os.symlink(os.path.realpath(source), os.path.join(link, "tree"))
        os.symlink(os.path.realpath(build), os.path.join(link, "build"))
        code, out, err = run([os.path.join(link, "tree", "scripts", "gate-scope.py"), "--files", "src/services/vrworld.cpp",
                              "--build", ".", "--json"], os.path.join(link, "build"))
        check(code == 0 and rows_of(out) == r1 and "not in the build graph" not in out + err,
              "2/15. `--build .` from inside the build dir, through a symlinked root, selects the same %d rows" % len(r1))
    finally:
        shutil.rmtree(link, ignore_errors=True)
    # 3. a directory that registers no row is an ERROR, never an empty answer
    with tempfile.TemporaryDirectory() as empty:
        code, out, err = run([tool, "--files", "src/services/vrworld.cpp", "--build", empty], source)
        check(code == 2 and "registers no ctest suite" in err, "3. an unconfigured --build dir exits 2 and says so (%d)" % code)
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
