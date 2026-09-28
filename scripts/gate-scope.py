#!/usr/bin/env python3
"""gate-scope — the SCOPED tier: what a change CAN REACH, and why (MODULAR-GATE-1).

    scripts/gate-scope.sh <base>..<tip> [--build build-linux] [--run] [--json] [-j N]
    scripts/gate-scope.sh --files path [path ...]
    scripts/gate-scope.sh --solo <suite> [...] [--times 3]     # the flake protocol, logged
    scripts/gate-scope.sh --record-times                       # gate-times.txt from the run log
    scripts/gate-scope.sh --merge-tier [-j N] | --nightly-tier

A lane runs everything its change can reach and nothing it cannot — read from the build and
the diff, never guessed — and the full tiers stay where the process needs them (a stage
close, a fork pin bump, nightly, the phase's push: PHOTON_ATOM_CONTRACT §7b). The tool turns a
git range into an exact `ctest -R '^(a|b|c)$'` selection with one rationale line per touched
path and one reason per selected row, estimates the wall time from THE RUN LOG (T8), and
`--run` writes every row's verdict into the run log (scripts/gate_runlog.py).

THE THREE SELECTORS (the Selection class below has the detail):
  1. REBUILT ARTEFACTS — a compiled row runs iff the build graph (build.ninja + ninja's deps
     log + the libraries' link references, scripts/gate_graph.py) says one of its executables
     contains code from, or includes, a touched file;
  2. SYMBOLS — a header hunk selects through the identifiers it declares or changes, via the
     files that name them; a comment-only change to C/C++ reaches nothing;
  3. REACH — the rows that run the app (--script suites, harnesses that spawn it) follow the
     AREA RULES' families and the scripts' API modules; when the app relinked and no rule
     narrows a path, every app row.
Build files are read by what their changed commands name. The FALLBACK to the MERGE tier is
only for a path with no rule, no symbol and no graph owner; a fork pin bump selects the MERGE
tier BY RULE. Tiers are contracts: the selection printed here is what runs, nothing
hand-picked out of it; a red a later tier finds that this selection missed is a defect of
this tool, fixed here and added to tests/hygiene/gate_selection_cases.json.
"""
import argparse, json, os, re, subprocess, sys, collections

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gate_graph                                   # noqa: E402  the build graph, symbols, CMake
import gate_runlog                                  # noqa: E402  THE RUN LOG (T8)
from gate_graph import CXX_EXT, HEADER_EXT          # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ---- AREA RULES: path prefix (or regex) -> (test dirs, script modules) --------------
# MODULAR-GATE-1: for a C/C++ path (and any other build input the graph knows) the rules pick
# only the APP rows and the source-reading OTHER rows of their dirs — the compiled rows are the
# build graph's (Selection.graph_rows). For runtime data the graph cannot see (engine media,
# scenes, sample content) a rule's dirs still pick every row. The '*merge-tier' rules for the
# build files, irisgl/CMakeLists and 'any other src/ file' are GONE: the build files are read
# by what their changed commands name, and a src/ file no rule narrows selects every app row.
# A touched path picks the FIRST rule whose prefix matches (most specific first). Test
# dirs are tests/<dir>; modules are the `<module>.` prefixes an e2e script calls. The
# special dir "*vulkan-scripts" means every app-spawning suite that is NOT --headless
# (anything that renders), "*headless-scripts" the --headless ones, "*all-scripts" both.
# THE ENGINE FAMILY: the test dirs any change to the engine's pixels or boundary selects.
# `rtreflect` is tests/rtreflect (the gi.rt_reflect family): it was MISSING from this list,
# so no engine change ever selected the ray-traced reflection suites — DRAG-1 changed what
# the ray arm reads from the voxels and two scoped gates came back green while
# gi.rt_reflect was red. The entries are test DIRECTORY names, not ctest labels, so a
# suite whose dir is not named is invisible however it is labelled.
# `atom` and `defaults` (D6B-GATE-SHAPE; audit S2): the visibility buffer IS the product's
# opaque path (engine.atom_draw) and defaults.exposure_plane is an exposure picture — an
# OgreChain.cpp change selected 415 suites and none of tests/atom before.
ENGINE_FAMILY = ["engine", "gi", "rtreflect", "lights", "looks", "distortion", "planar", "ssr", "shadow",
                 "shadercache", "compute", "hdr", "sky", "pieces", "vr", "atom", "defaults",
                 "mirror", "cameras", "samples", "picking", "skeletal", "particles", "thumbnails",
                 "materialpreview", "player", "sockets", "threading", "perf", "gizmo", "assets", "log",
                 "shutdown", "openasync", "*vulkan-scripts"]

AREA_RULES = [
    # --- engine: anything that changes pixels or the boundary ---------------------------
    # THE VISIBILITY-BUFFER DECODE (tests/atom: engine.atom_parity and its twins, the
    # cluster suites): HlmsAtom is a derived HlmsPbs, so its pixels move with ITS OWN
    # media and C++, the GPU scene tables it reads, the atom pass, EVERY Hlms piece the
    # engine stages (PBS's pieces are its text too) and the fork pin (Ogre's Pbs pieces,
    # the Forward+ hook). PHOTON-HIT-SHADE-1 changed all of these and the generic rule
    # below did not select engine.atom_parity (audit F2): its byte-identity rested on a
    # hand run.
    (r"^irisgl/(engine/media/Hlms/|engine/src/(HlmsAtom|OgreAtomPass|AtomPass|OgreGpuScene|GpuScene)\.|"
     r"thirdparty/ogre-next)",
     ENGINE_FAMILY + ["atom"], []),
    (r"^irisgl/(engine/|thirdparty/ogre-next|scripts/build-ogre)", ENGINE_FAMILY, []),
    (r"^irisgl/mirror/",
     ["mirror", "skeletal", "sockets", "cameras", "gizmo", "player", "thumbnails",
      "materialpreview", "samples", "picking", "particles",
      # THE MIRROR PUSHES EVERY ENVIRONMENT DESC EACH FRAME (GATE-SCOPE-2, 2026-09-17): the
      # GI/planar/shadow/post/sky/fog/ray settings and their staleness machine live in
      # scenemirror.cpp, so a mirror edit can move any of these pictures.
      "gi", "rtreflect", "lights", "looks", "planar", "ssr", "shadow", "hdr", "sky", "distortion", "vr",
      "*vulkan-scripts"], []),
    # `hygiene` rides irisgl/import, irisgl/document and irisgl/core because
    # source.bake_key_guard (BAKEKEY-1) watches files in all three: the mesh
    # bake's format version is HAND-bumped, and the lane that has to bump it is
    # exactly a lane whose scoped selection comes from these rules. Five
    # display-free shell scripts, well under a second.
    # `atom` and `compute` (D6B-GATE-SHAPE; audit S2): irisgl/import/meshbake.cpp carries the
    # cluster-DAG bake, and atom.cluster_cut / atom.cluster_crack / engine.lod_rule_parity are
    # its only guards — nightly-labelled or not, a change to the bake selects them (§1 of
    # docs/TESTING_GATE.md: a `nightly` row still rides the scoped gate of its own subject).
    (r"^irisgl/import/",
     ["importer", "importasync", "meshbake", "avatar", "skeletal", "assetdelete", "assetgc",
      "assetmeta", "assetmigrate", "assetpaths", "assets", "samples", "thumbnails", "hygiene",
      "atom", "compute"],
     ["assets", "avatar", "anim"]),
    (r"^irisgl/document/(physics|animation)/",
     ["document", "skeletal", "avatar", "particles", "player", "cameras", "samples",
      "*headless-scripts"], ["node", "anim", "avatar", "player", "scene"]),
    (r"^irisgl/document/",
     # `ui` and `theme` are here because THE PANELS DISPLAY THE DOCUMENT: the
     # properties column reads a document object's values and its suites pin
     # them, so a document change can red a ui suite and this rule selected
     # none. DRAG-1 changed the unauthored material's values and
     # ui.material_panel went red at a gate that had never run it.
     ["document", "math", "input", "gizmo", "picking", "commands", "skeletal", "sockets",
      "cameras", "mirror", "samples", "reopen", "export", "meshbake", "hygiene",
      "ui", "theme", "*headless-scripts"],
     []),
    (r"^irisgl/core/",
     ["document", "math", "input", "gizmo", "cameras", "hygiene", "*headless-scripts"], []),
    (r"^irisgl/docs/", [], []),                                   # documentation: no suite at all
    # The media-staging CMake (WrapHlmsPiece: plain GLSL wrapped into Hlms pieces) changes what
    # the engine's shaders SEE — the engine family, not the whole tier (DEVPROCESS-2, 2026-09-24:
    # eight lane gates fell back to the MERGE tier this phase on paths like this one).
    # The wrapped pieces are Hlms text HlmsAtom's decode compiles too: the atom dir with it.
    (r"^irisgl/cmake/WrapHlmsPiece", ENGINE_FAMILY + ["atom"], []),
    # --- Studio ---------------------------------------------------------------------------
    (r"^src/scripting/modules/([a-z]+)api\.(cpp|h)$", ["api"], ["$1"]),   # $1 = module name
    # THE THREADED AVATAR IMPORT/SWITCH (D6B-GATE-SHAPE; audit S1): avatar.responsive's subject
    # is avatarapi.cpp + the module + services/avatarassets.cpp, and no path selected it.
    (r"^src/modules/avatar/api/avatarapi\.(cpp|h)$", ["api", "avatarasync"], ["$module"]),
    (r"^src/(modules/[a-z]+/(api/)?[a-z]+api|player/api/playerapi)\.(cpp|h)$", ["api"], ["$module"]),
    (r"^src/scripting/(mcp/|claude/)", ["mcp", "claudechat", "api"], ["app"]),
    (r"^src/scripting/", ["api", "*all-scripts", "mcp"], []),
    (r"^src/services/import/", ["importer", "importasync", "assetdelete", "assetgc", "assetmeta",
                                "assetmigrate", "assetpaths", "assets", "drawers", "thumbnails",
                                "meshbake", "samples"], ["assets", "project"]),
    (r"^src/services/avatarassets",
     ["assetdelete", "assetgc", "assetmeta", "assetmigrate", "assetpaths", "assets", "drawers",
      "thumbnails", "importer", "importasync", "export", "avatar", "reopen", "samples",
      "avatarasync"],
     ["assets", "project", "avatar"]),
    (r"^src/services/(asset|projectassets|thumbnail|meshbake|audiopeaks|videoutils|rigsignature|animationfile|extentmeasure)",
     ["assetdelete", "assetgc", "assetmeta", "assetmigrate", "assetpaths", "assets", "drawers",
      "thumbnails", "importer", "importasync", "export", "avatar", "reopen", "samples"],
     ["assets", "project", "avatar"]),
    (r"^src/services/(player|playback)", ["player"], ["player", "scene"]),
    (r"^src/services/(shortcut|input)", ["shortcuts", "input", "app"], ["input", "editor", "app"]),
    # sceneedit/selection/undo sit under EVERY scene.add*/node.* verb, so the headless
    # scripts (~1 min at -j4) ride along as insurance the ubiquitous-module filter removes.
    # `hygiene` rides this rule because source.one_material_resolve watches
    # exactly these files — "there is ONE resolveMaterial and ONE apply" is a
    # claim about sceneeditservice.cpp — and a lane that rewrote the apply
    # path gated without it (BUNDLE-P3, the GATE-SCOPE-2 lesson again). The
    # nine hygiene suites are display-free lints costing under a second.
    (r"^src/services/(selection|sceneedit|undo|nodenaming|outline|scenefolders|scenenodehelper)",
     ["services", "commands", "app", "input", "hygiene", "*headless-scripts"],
     ["editor", "scene", "node"]),
    (r"^src/services/(looks|worldmodes|sunlink|planarreflectors|gibounds|lightbindings|iesprofile|sceneextents)",
     ["services", "looks", "planar", "lights", "gi"], ["world", "scene", "node"]),
    # THE THREADED ARCHIVER (D6B-GATE-SHAPE; audit S1): projectarchiver.cpp is archive.responsive's
    # subject, and the generic project rule below never named tests/archiveasync.
    (r"^src/services/projectarchiv",
     ["archiveasync", "services", "app", "apppaths", "openasync", "export", "hygiene"],
     ["app", "project"]),
    (r"^src/services/(project|sceneopen|apppaths|sessionheader|sessionmarkers|jahlog|loadtimeline|perfsampler|framepacing|mainthread|engineerror|ogresamples|shutdown)",
     ["services", "app", "apppaths", "log", "perf", "shutdown", "openasync", "hygiene", "threading"],
     ["app", "project"]),
    # THE PROJECT'S VR SETTINGS TABLE (VR-WORLD-1): the panel section, both VR
    # verbs, the undo rows and the file's own keys are generated from it, so a
    # vrworld-only edit has to gate the VR suites and the panel's — the generic
    # services rule below reaches neither (the Fable read, item 7).
    (r"^src/services/vrworld", ["vr", "player", "ui", "document", "services", "reopen"],
     ["vr", "world", "player"]),
    # THE LIBRARY RESET (RESET-LIBRARY-1): it deletes the catalog, the store's
    # contents and the project folders and then re-runs the fresh-install
    # bootstrap, so its own suite, the data-root suite and the asset suites are
    # what tell you it still leaves a first launch behind — the generic
    # services rule below reaches none of them.
    (r"^src/services/libraryreset", ["libraryreset", "apppaths", "assets", "services"],
     ["app", "assets", "project"]),
    (r"^src/services/", ["services", "*headless-scripts"], []),
    (r"^src/(data|io|commands)/", ["document", "commands", "reopen", "export", "samples", "assetpaths",
                                   "assetmigrate", "services", "*headless-scripts"], ["project", "scene", "node"]),
    (r"^src/viewport/", ["app", "input", "gizmo", "picking", "cameras", "sockets", "ui",
                         # the render driver, the frame monitor host, the screenshot grades and the
                         # VR pacing live here (GATE-SCOPE-2, 2026-09-17)
                         "perf", "hdr", "vr", "player", "thumbnails"],
     ["editor", "camera", "input", "perf", "vr"]),
    (r"^src/(bridge|player)/", ["player", "thumbnails", "materialpreview", "assets", "avatar", "app"],
     ["player", "avatar", "materials", "assets"]),
    # A MATERIAL IS A BUNDLE (MATERIAL_BUNDLE_SPEC phase 1), so the module's
    # code owns the bundle MODEL's suite, the asset suites its definition is
    # stored through, and the thumbnails rendered from it — an edit to
    # core/graphdefinition.cpp used to gate without the model suite at all
    # (the GATE-SCOPE-2 lesson again).
    (r"^src/modules/materials/", ["shadergraph", "pieces", "materialpreview", "ui",
                                  "materialbundle", "assets", "assetdelete", "assetgc",
                                  "assettray", "thumbnails"],
     ["materials", "material", "graph"]),
    (r"^src/modules/avatar/", ["avatar", "skeletal", "ui", "avatarasync"], ["avatar", "anim"]),
    (r"^src/modules/vr/", ["vr", "player", "app"], ["vr", "player"]),
    (r"^src/(modules/publish|export)/", ["export", "ui"], ["project", "publish"]),
    # …and here because source.panel_rows_guarded and source.db_pointers_initialised
    # read src/ui and src/shell (the panels' rows and their database pointers).
    (r"^src/(ui|shell)/",
     ["ui", "app", "theme", "shortcuts", "desktops", "drawers", "hygiene"],
     ["editor", "app", "desktop"]),
    (r"^src/app/", ["app", "apppaths", "log", "shutdown", "hygiene", "threading", "api"], ["app"]),
    # --- data, docs, build ---------------------------------------------------------------
    (r"^docs/SCRIPTING\.md$", ["api"], []),
    # THE TIER DOC'S OWN GUARD (POST-C-FIXES-1): docs/TESTING_GATE.md must quote the
    # MERGE tier through `gate-scope.py --merge-tier`, never a copy of its -LE set
    # (a copy went stale once: it lacked photon-target). source.gate_scope_rules case 9
    # greps it, so an edit to the doc selects the hygiene rows.
    (r"^docs/TESTING_GATE\.md$", ["hygiene"], []),
    (r"^(docs/|README|LICENSE|\.claude/|\.github/|[A-Za-z_\-]+\.md$|\.gitignore$|\.gitmodules$)", [], []),   # no suite at all
    (r"^(scenes/|app/content/|app/samples/)", ["samples", "reopen", "assets"], ["project"]),
    (r"^app/", ["ui", "theme", "app"], []),
    # THE TOOL'S OWN GUARD (source.gate_scope_rules): scripts/gate-scope.* is the
    # one path under scripts/ that a suite reads, so an edit to it selects the
    # hygiene rows (seven display-free shell/python lints, well under a second
    # together). Without this line an edit to the scoping tool selected NOTHING
    # and its own guard never ran — the rule class GATE-SCOPE-2 audited.
    # MODULAR-GATE-1: the selector's two libraries (gate_graph.py, gate_runlog.py) are the
    # tool too, and gate.selection (the replay of recorded lanes) lives beside the rule guard.
    (r"^scripts/gate[-_]", ["hygiene"], []),
    # THE GPU-TIMING LOCK's wrapper (DEVPROCESS-1): its own tooling suite, devprocess.gpu_lock.
    (r"^scripts/gpu-exclusive", ["tooling"], []),
    # THE VRAM BUDGET's helper (GATE-ADMIT-1): its tooling suite (devprocess.vram_admit), the
    # pool driver's own test that imports it (pool.runner, tests/app) and the closure (hygiene).
    (r"^scripts/(gpu-admit|vram_tokens)", ["tooling", "app", "hygiene"], []),
    (r"^scripts/", [], []),
]

# Cheap smoke suites always added when src/ or irisgl/ moved (a boot that renders + the
# contract of the scripting surface), ~15 s together.
ALWAYS_ON_CODE = ["app.startup_quiet", "api.contract"]
# THE NIGHTLY TIER (D6B-GATE-SHAPE; audit §8 — `benchmark` used to be overloaded as this
# marker, so open.crash_soak and gi.gather_cost carried a label that said the wrong thing).
#   `nightly`     — every row the MERGE and PUSH tiers leave out: minutes of one process whose
#                   push-time guard lives elsewhere, or a millisecond bar that needs a quiet box.
#   `quiet-box`   — beside `nightly` on the rows that MEASURE (the wall-clock benchmarks, the
#                   `<suite>.timing` millisecond rows, a GPU clock). A scoped gate never runs
#                   them: it shares the box with other lanes by construction.
# A `nightly` row WITHOUT `quiet-box` still rides the scoped gate of its own subject — it is
# that change's guard (atom.cluster_cut for a bake change: audit §3c's condition for the move).
# Anchored in the -LE: `shadercache` alone would drop the product-contract cache suites (code
# review 2026-09-10). `--timeout 120` is ctest's DEFAULT for the rows that set no TIMEOUT — a
# hang costs 2 min, not 25.
NIGHTLY_LABELS = {"nightly", "shadercache-attack"}
# Never selected by a scoped gate (the shader-cache attack is minutes under ASan).
SCOPE_EXCLUDED_LABELS = {"quiet-box", "shadercache-attack"}

# TARGET TESTS (PHOTON phase A, A1 §0; the label's ONE definition lives here).
#
# A target test states the CORRECT number for a term the renderer gets wrong
# today, and it cannot pass until the PART named in its header lands. It is not
# a disabled test and it is not a bracket around an artefact: it RUNS in every
# scoped selection and PRINTS its value ("target: <value> (bar <bar>)"), so the
# distance to the bar is visible on every lane and a target that goes green
# early is noticed instead of sitting red for a month. What it does not do is
# decide a gate: the lane that fixes the term DELETES the label from the
# suite's CMake, and that deletion IS the part's acceptance.
#
# So the exclusion is from PASS/FAIL, never from the run. gate-scope runs the
# gating suites first (their exit code is the gate's), then the target suites in
# a second ctest invocation whose result is reported and discarded. The MERGE
# and PUSH tiers drop them with -LE, which is the only shape ctest offers for
# "do not let these decide the tier"; their values are read from a lane's scoped
# run, where they are printed.
# `scale-target` (lane D1-SCALE-FIXTURES): phase E's measuring stick — one `scale.*`
# suite per wall (tests/scale), each printing today's number as a `target:` line with
# no bar yet. Split out exactly like a photon target: reported, never gating, until the
# part that closes a wall writes its bar and removes the label from that row.
TARGET_LABELS = {"photon-target", "scale-target"}

NIGHTLY_LABEL_RE = "|".join(sorted(re.escape(l) for l in NIGHTLY_LABELS | TARGET_LABELS))
# The MERGE tier at a given ctest parallelism. -j4 is the tier's contract on a quiet box
# (docs/TESTING_GATE.md §1); a lane beside other live lanes asks for -j2 with `-j 2`, and
# the fallback must honour that as the scoped command does (DEVPROCESS-1 item 2: the
# fallback used to print and RUN a hardcoded -j4 whatever the caller asked).
def merge_tier(jobs=4):
    return (f'ctest -j{jobs} --timeout 120 --output-on-failure '
            f'-LE "^({NIGHTLY_LABEL_RE})$"')


# The NIGHTLY tier: every `nightly` row, one at a time (they are minutes of one process or a
# measurement that wants the box), on a quiet box, by the lead.
def nightly_tier():
    rx = "|".join(sorted(re.escape(l) for l in NIGHTLY_LABELS))
    return f'ctest -j1 --output-on-failure -L "^({rx})$"'


def sh(cmd, cwd=ROOT):
    # A failing git/ctest call must not read as "nothing touched" / "no suites": that was
    # a silently green empty gate (platform audit H4.2, 2026-09-10).
    r = subprocess.run(cmd, cwd=cwd, shell=True, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(f"gate-scope: command failed ({r.returncode}): {cmd}\n{r.stderr}")
        sys.exit(2)
    return r.stdout


def touched_paths(rng):
    """Files changed in the Studio range, plus the irisgl submodule's own diff (prefixed)."""
    files = [l for l in sh(f"git diff --name-only {rng}").splitlines() if l]
    if not files:
        sys.stderr.write(f"gate-scope: range {rng} touches no file — refusing to scope an empty change\n")
        sys.exit(2)
    out = [f for f in files if f != "irisgl"]
    if "irisgl" in files:
        base, tip = rng.split("..", 1)
        old = sh(f"git rev-parse {base}:irisgl").strip()
        new = sh(f"git rev-parse {tip}:irisgl").strip()
        sub = sh(f"git diff --name-only {old} {new}", cwd=os.path.join(ROOT, "irisgl")).splitlines()
        out += ["irisgl/" + s for s in sub if s]
    return sorted(set(out))


def resolve_build(arg):
    """WHERE THE BUILD DIR IS, and the trap this function exists to close.

    `ctest --show-only=json-v1` in a directory that is NOT a configured build
    dir prints an EMPTY inventory and exits 0 (measured: the Studio repo root
    does exactly this). Every suite name a rule produces is then filtered out
    by `add()` — it only keeps names the inventory knows — so the selection came
    out empty and the tool said "NOTHING to gate ... docs/scripts/data" and
    exited 0. A SILENT EMPTY GATE, with the rationale lines above it listing
    sixteen test dirs it had just decided to skip (ledger §620 item 5, found by
    SQUARE-1 the hard way: `gate-scope.py <range> --build . --run` run from
    INSIDE the build dir, where `--build .` was resolved against the repo root).

    Two halves, and both are needed. This one makes `--build .` from inside the
    build dir MEAN the build dir: the argument is tried against the CWD first
    and then against the repo root (the documented form, `--build build-linux`
    from the tree top), and the first candidate that actually carries a
    CTestTestfile.cmake wins. `load_inventory` below is the other half: a build
    dir that registers no suite is now an error instead of an empty answer.
    """
    cands = [arg] if os.path.isabs(arg) else [os.path.abspath(arg), os.path.join(ROOT, arg)]
    for c in cands:
        if os.path.isfile(os.path.join(c, "CTestTestfile.cmake")): return c
    return cands[-1]          # nothing configured: keep the documented resolution for the error


def load_inventory(build):
    # listed from a copy of the CTestTestfile tree: a listing in the build dir itself truncates
    # the LastTest.log of a gate running there (gate_graph.ctest_inventory)
    if not os.path.isfile(os.path.join(build, "CTestTestfile.cmake")):
        raw, rc = "{}", 0
    else:
        raw, rc = gate_graph.ctest_inventory(build)
        if rc != 0:
            sys.stderr.write(f"gate-scope: ctest --show-only failed ({rc}) for {build}\n"); sys.exit(2)
    j = json.loads(raw or "{}")
    if not j.get("tests"):
        # See resolve_build: an unconfigured directory answers with an empty
        # inventory and a zero exit status, and an empty inventory selects
        # nothing however many rules fired.
        sys.stderr.write(
            f"gate-scope: {build} registers no ctest suite — that is not a configured build "
            f"dir.\n            Point --build at one (an absolute path, a path relative to "
            f"the repo root, or\n            '.' from inside the build dir itself). Refusing "
            f"to scope against an empty inventory.\n")
        sys.exit(2)
    bt = j["backtraceGraph"]; files = bt["files"]; nodes = bt["nodes"]; commands = bt.get("commands", [])
    inv = {}
    for t in j["tests"]:
        # THE SUITE'S OWN DIRECTORY IS WHERE IT WAS REGISTERED, NOT WHERE add_test RAN
        # (POST-C-FIXES-1). A suite registered through a helper function has its
        # add_test in the file that DEFINES the helper: every jah_gpu_exclusive_test
        # suite (tests/CMakeLists.txt) came out as dir "tests", which no rule names —
        # so open.responsive, gi.budget and the rest of the GPU-lock list were never
        # selected by a change to their own sources. Walk the backtrace outward and
        # take the first frame that lies in a tests/<dir>/ (the call site); the
        # add_test frame is kept only when no such frame exists.
        n = nodes[t["backtrace"]]
        frames, via = [], set()
        while True:
            if n.get("file") is not None:
                frames.append(files[n["file"]])
            if n.get("command") is not None and n["command"] < len(commands):
                via.add(commands[n["command"]].lower())     # the helper functions it came through
            if "parent" not in n:
                break
            n = nodes[n["parent"]]
        def _in_test_dir(f):
            r = os.path.relpath(os.path.dirname(f), ROOT).split(os.sep)
            return len(r) >= 2 and r[0] == "tests"
        cm = next((f for f in frames if _in_test_dir(f)), frames[0] if frames else ROOT)
        d = os.path.relpath(os.path.dirname(cm), ROOT)          # tests/<dir>
        cmd = t.get("command", [])   # absent for a not-yet-built executable (partial build dir)
        props = {p["name"]: p["value"] for p in t.get("properties", [])}
        # THE SCRIPTS A ROW RUNS: the --script argument, and every argument that is a file of the
        # source tree or a configured script (a .js the build dir generated from a .js.in, a
        # wrapper .sh, a lint .py). The audit (§7): wrapper-run app rows never got module
        # selection because only cmd[1] of a bash row was read.
        # THE WRAPPERS ARE NOT THE ROW'S SUBJECT (GATE-ADMIT-1): the GPU lock and the VRAM budget
        # (scripts/gpu-exclusive.sh, scripts/gpu-admit.sh <k> --label <l> --) are peeled off
        # before the row's script and argv files are read; `cmd` itself keeps them (the lock
        # rules read it).
        run = list(cmd)
        if run and os.path.basename(run[0]) == "gpu-exclusive.sh":
            run = run[1:]
        if run and os.path.basename(run[0]) == "gpu-admit.sh":
            run = run[run.index("--") + 1:] if "--" in run else run[2:]
        script = None
        m = re.search(r"--script\s+(\S+)", " ".join(run))
        if m: script = m.group(1)
        files_in_argv = [os.path.normpath(c) for c in run[1:]
                         if c.endswith((".js", ".sh", ".py", ".js.in", ".cmake")) and os.path.isfile(c)]
        if not script:
            script = next((f for f in files_in_argv if f.endswith(".js")), None)
        if not script and run and run[0].endswith(("bash", "/sh", "sh")) and files_in_argv:
            script = files_in_argv[0]
        # A POOL row (SUITE-POOL-1: `run_pool.py --pool <p> --arm <arm> <script> <budget> ...`):
        # its arms and their scripts, read from the command line the pool's CMake built — the
        # selector's unit for an app family (TESTING_V2 T2/T3). No name is hard-wired.
        pool, arms = None, {}
        if "--pool" in cmd:
            k = cmd.index("--pool")
            pool = cmd[k + 1] if k + 1 < len(cmd) else None
            for k, c in enumerate(cmd):
                if c == "--arm" and k + 2 < len(cmd):
                    arms[cmd[k + 1]] = os.path.normpath(cmd[k + 2])
        inv[t["name"]] = {
            "pool": pool, "arms": arms,
            "dir": d.split("/")[-1] if d.startswith("tests") else d,
            "reldir": d,
            "cmd": [os.path.normpath(c) if os.path.isabs(c) else c for c in cmd],
            "app": any(c.endswith("bin/Jahshaka") for c in cmd),
            "headless": "--headless" in cmd,
            "script": script,
            "argv_files": set(files_in_argv),
            "via": via,
            "frames": set(os.path.normpath(f) for f in frames),
            "serial": bool(props.get("RUN_SERIAL")),
            "env": list(props.get("ENVIRONMENT", []) or []),
            "fixture_setup": bool(props.get("FIXTURES_SETUP")),
            "labels": set(props.get("LABELS", []) or []),
            "kind": "other", "exes": set(),
        }
    return inv


def classify_rows(inv, graph, build):
    """Row kinds, read from the graph: `app` (runs bin/Jahshaka, or a harness compiled with
    JAHSHAKA_BINARY that spawns it), `compiled` (runs one of the tree's own executables),
    `other` (a lint, a python/cmake check, a shell script over the source tree)."""
    app_exe = os.path.normpath(os.path.join(build, "bin", "Jahshaka"))
    for t in inv.values():
        exes = {c for c in t["cmd"] if graph and c in graph.exes}
        t["exes"] = exes
        if t["app"] or app_exe in exes or (graph and exes & graph.app_driving):
            t["kind"] = "app"
        elif exes:
            t["kind"] = "compiled"
        else:
            t["kind"] = "other"
    return app_exe


TIMES_FILE = os.path.join(ROOT, "scripts", "gate-times.txt")


def record_times():
    """Refresh scripts/gate-times.txt from THE RUN LOG (T8): each suite's median PASS seconds
    over the last 14 days of gate runs, every tier and lane — never one gate's output."""
    times = gate_runlog.median_times(days=14)
    if not times:
        sys.stderr.write(f"gate-scope: the run log ({gate_runlog.log_dir()}) holds no PASS record "
                         f"in the last 14 days — nothing to record\n")
        sys.exit(2)
    old = {}
    if os.path.exists(TIMES_FILE):
        for line in open(TIMES_FILE):
            p = line.split()
            if len(p) == 2 and not line.startswith("#"): old[p[0]] = p[1]
    with open(TIMES_FILE, "w") as f:
        f.write("# suite seconds — the run log's median PASS time over 14 days "
                "(gate-scope.sh --record-times; testing/runs/README.md)\n")
        for n in sorted(set(old) | set(times)):
            f.write(f"{n} {times[n]:.2f}\n" if n in times else f"{n} {old[n]}\n")
    print(f"recorded {len(times)} suite times from the run log into {os.path.relpath(TIMES_FILE, ROOT)}")


COST_SOURCE = {}   # suite -> "quiet" | "all" (the run log's median, gate_runlog.median_times) | "file"


def load_costs():
    """Per-suite seconds: scripts/gate-times.txt overlaid with the run log's medians (fresher; the
    quiet-box median where >= 3 records ran with no sibling ctest). COST_SOURCE says which."""
    costs = {}
    if os.path.exists(TIMES_FILE):
        for line in open(TIMES_FILE):
            if line.startswith("#"): continue
            parts = line.split()
            if len(parts) == 2:
                costs[parts[0]] = float(parts[1]); COST_SOURCE[parts[0]] = "file"
    costs.update(gate_runlog.median_times(days=14, sources=COST_SOURCE))
    return costs


def cost_sources(keys):
    """'<n> quiet, <n> all, <n> file, <n> assumed' over the keys an estimate summed."""
    c = {"quiet": 0, "all": 0, "file": 0, "assumed": 0}
    for k in keys: c[COST_SOURCE.get(k, "assumed")] += 1
    return (f"costs: {c['quiet']} quiet-box medians (box.other_ctests == 0, >= 3 records), {c['all']} "
            f"all-record medians, {c['file']} from gate-times.txt, {c['assumed']} assumed 10 s")


def api_modules():
    """Every scripting module that has an API file (src/scripting/modules/<m>api.cpp,
    src/modules/*/api/<m>api.cpp, src/player/api/<m>api.cpp)."""
    out = set()
    for pat in ("src/scripting/modules/*api.cpp", "src/modules/*/*api.cpp", "src/modules/*/api/*api.cpp",
                "src/player/api/*api.cpp"):
        import glob as _g
        for f in _g.glob(os.path.join(ROOT, pat)):
            out.add(os.path.basename(f)[:-len("api.cpp")])
    return out


def script_modules(path):
    """The `<module>.` prefixes a JS script calls (a shell wrapper's text is not JS)."""
    if not path or not path.endswith((".js", ".js.in")): return set()
    try: txt = open(path).read()
    except OSError: return set()
    return set(re.findall(r"\b([a-z]+)\.[a-zA-Z_]+\(", txt))


# A build-registration edit that only adds or removes SOURCE FILES from a list (a new
# .cpp beside its header, a deleted dead file) says nothing on its own: the files it
# names are in the same diff and scope precisely (2026-09-11).
_SOURCE_LISTS = {"set", "list", "add_executable", "add_library", "target_sources", "qt_add_resources",
                 "qt6_add_resources", "qt_add_executable", "qt_add_library", "source_group", None}
_LIST_ENTRY = re.compile(r'^"?[\w${}./+\-]+\.(?:cpp|cc|cxx|c|h|hh|hpp|ui|qrc|mm|js|js\.in|sh)"?\)?$')


def _git_show(rev, path):
    """A file's text at a revision of the Studio repo or (for irisgl/...) of the irisgl submodule;
    '' when it does not exist there."""
    try:
        if path.startswith("irisgl/"):
            r = subprocess.run(["git", "show", f"{rev}:{path[len('irisgl/'):]}"], cwd=os.path.join(ROOT, "irisgl"),
                               capture_output=True, text=True, errors="replace")
        else:
            r = subprocess.run(["git", "show", f"{rev}:{path}"], cwd=ROOT, capture_output=True, text=True,
                               errors="replace")
        return r.stdout if r.returncode == 0 else ""
    except OSError:
        return ""


class Revs:
    """The two sides of the range, in each repo (Studio; irisgl at the pins the range names)."""
    def __init__(self, rng):
        self.rng = rng
        self.base, self.tip = rng.split("..", 1) if rng and ".." in rng else (None, None)
        self._ig = None

    def pair(self, path):
        if not self.base: return None, None
        if path.startswith("irisgl/"):
            if self._ig is None:
                self._ig = (sh(f"git rev-parse {self.base}:irisgl").strip(), sh(f"git rev-parse {self.tip}:irisgl").strip())
            return self._ig
        return self.base, self.tip

    def texts(self, path):
        a, b = self.pair(path)
        if not a: return None, None
        return _git_show(a, path), _git_show(b, path)

    def diff_u0(self, path):
        a, b = self.pair(path)
        if not a: return ""
        if path.startswith("irisgl/"):
            return sh(f"git diff -U0 {a} {b} -- {path[len('irisgl/'):]}", cwd=os.path.join(ROOT, "irisgl"))
        return sh(f"git diff -U0 {a} {b} -- {path}")


def _changed_line_numbers(diff_text):
    """(old line numbers removed, new line numbers added) of a -U0 diff."""
    old, new = set(), set()
    o = n = 0
    for line in diff_text.splitlines():
        m = re.match(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@", line)
        if m:
            o, n = int(m.group(1)), int(m.group(3)); continue
        if line.startswith(("+++", "---")): continue
        if line.startswith("-"): old.add(o); o += 1
        elif line.startswith("+"): new.add(n); n += 1
    return old, new


class Selection:
    """One scoped selection: what a change can reach, and why (MODULAR-GATE-1).

    THREE SELECTORS, in the order a path meets them:
      1. REBUILT ARTEFACTS (compiled rows). A C/C++ file's reach is the build graph's: the
         executables whose objects — or whose linked libraries' objects — were compiled from it
         or read it as a header (build.ninja + ninja's deps log). A compiled row runs iff one of
         its executables is in that set. The area rules' test DIRECTORIES no longer pick
         compiled rows for such a path: the graph is exact where the rules guessed.
      2. SYMBOLS (shared headers). A header hunk selects through the identifiers it touches (the
         names on its changed lines and the struct/class/enum/function they sit in, comments and
         whitespace stripped): the files of src/, irisgl/ and tests/ that NAME one of them are
         the reached paths, each through its own graph reach and rules. A comment-only change to
         any C/C++ file reaches nothing (the source lints still ride).
      3. REACH (app rows). The app relinks for almost any change, so the rows that run it (the
         --script suites, the harnesses that spawn it) are chosen by the AREA RULES' families
         and the scripts' API modules — and when the graph says the app relinked and no rule
         narrows a path, by every app row (the app's reach, un-narrowed; never the whole tier).
    CMake edits are read by what their changed commands name (rows, targets, sources,
    sub-directories, helper functions, variables); a directory-scope setting reaches every
    target under that directory. The FALLBACK to the MERGE tier is left for a path with no rule,
    no symbol and no graph owner — and printed with its reason.
    """

    def __init__(self, inv, graph, app_exe, revs, jobs):
        self.inv, self.graph, self.app_exe, self.revs, self.jobs = inv, graph, app_exe, revs, jobs
        self.selected = {}
        self.fallback = []
        self.full_tier = []          # rules that select the whole tier (a fork bump, a root setting)
        self.rationale = []
        self.code_moved = False
        self.skipped_ubiquitous = set()
        self.by_dir = collections.defaultdict(list)
        for n, t in inv.items(): self.by_dir[t["dir"]].append(n)
        self.mods = {n: script_modules(t["script"]) for n, t in inv.items() if t["script"] and not t["arms"]}
        # A HARNESS that drives the app over MCP (compiled with JAHSHAKA_BINARY; open.responsive,
        # avatar.responsive, scripting.e2e.texture_missing, ...) calls verbs by name in its own
        # source ("avatar.importAvatar(..."): those are its modules, so an API change selects it
        # like a script (the suites audit's S1 class, closed by the selector, not by a rule)
        if graph is not None:
            known = api_modules()
            for n, t in inv.items():
                if t["kind"] != "app" or t["script"] or t["arms"]: continue
                ms = set()
                for e in t["exes"] & graph.app_driving:
                    for o in graph.members(e):
                        src = graph.obj_src.get(o, "")
                        if "/tests/" in src and src.endswith(CXX_EXT):
                            try: ms |= set(re.findall(r"\b([a-z]+)\.[a-zA-Z_]+\(", open(src, errors="replace").read()))
                            except OSError: pass
                if ms & known: self.mods[n] = ms & known
        # an ARM is `<row>::<arm>` wherever a script is mapped: its modules and its script select
        # the arm alone; a rule's directory or group selects the pool row whole
        for n, t in inv.items():
            for arm, scr in t["arms"].items():
                self.mods[f"{n}::{arm}"] = script_modules(scr)
        self.whole = set()                 # rows selected whole (a pool: every arm)
        self.arms = collections.defaultdict(dict)   # pool row -> {arm: reason}
        freq = collections.Counter(m for ms in self.mods.values() for m in ms)
        self.ubiquitous = {m for m, c in freq.items() if c > 0.4 * max(1, len(self.mods))}
        self.script_rows = collections.defaultdict(list)       # abs script/argv file -> rows
        for n, t in inv.items():
            if t["arms"]:
                for arm, scr in t["arms"].items():
                    self.script_rows[scr].append(f"{n}::{arm}")
                files = t["argv_files"] - set(t["arms"].values())
            else:
                files = ({t["script"]} if t["script"] else set()) | t["argv_files"]
            for f in files:
                self.script_rows[os.path.normpath(f)].append(n)
        self.script_base = collections.defaultdict(list)
        for f, ns in self.script_rows.items():
            self.script_base[os.path.basename(f).replace(".js.in", "").replace(".js", "")] += ns
        self.exe_rows = collections.defaultdict(list)
        for n, t in inv.items():
            for e in t["exes"]: self.exe_rows[e].append(n)
        self.row_names = set(inv)
        self.owned = set()
        self._visited = set()

    # -- adding rows -----------------------------------------------------------------------
    def add(self, suites, why):
        for s in suites:
            if why.startswith("tests/"):
                self.owned.add(s.split("::", 1)[0])     # the lane's own guard (--joint)
            if "::" in s:
                row, arm = s.split("::", 1)
                if row in self.inv and row not in self.whole:
                    self.arms[row].setdefault(arm, why)
                    self.selected.setdefault(row, why)
                continue
            if s in self.inv:
                self.selected.setdefault(s, why)
                self.whole.add(s)

    def arm_subsets(self):
        """pool row -> the arms selected, for every pool NOT selected whole."""
        return {r: sorted(a) for r, a in self.arms.items() if r not in self.whole and r in self.selected}

    def expand(self, dirs, modules, why, own_api=False, kinds=None):
        """An area rule's families. `kinds` limits the dirs to row kinds (the graph owns the
        compiled rows of a C/C++ path, so its rules pick only app and other rows)."""
        for d in dirs:
            if d == "*merge-tier": self.fallback.append(why); continue
            if d in ("*vulkan-scripts", "*headless-scripts", "*all-scripts", "*app-rows"):
                for n, t in self.inv.items():
                    if t["kind"] != "app": continue
                    if d == "*vulkan-scripts" and t["headless"]: continue
                    if d == "*headless-scripts" and not t["headless"]: continue
                    self.add([n], why)
                continue
            self.add([n for n in self.by_dir.get(d, []) if kinds is None or self.inv[n]["kind"] in kinds], why)
        for m in modules:
            if m in self.ubiquitous and not own_api:
                self.skipped_ubiquitous.add(m); continue
            self.add([n for n, ms in self.mods.items() if m in ms], f"{why} [module {m}]")

    def rules_for(self, p, why_prefix, kinds=None):
        """Apply the first matching AREA_RULE. Returns the rule text, or None when none matched."""
        for pat, dirs, modules in AREA_RULES:
            m = re.match(pat, p)
            if not m: continue
            mm = []
            for x in modules:
                if x == "$1": mm.append(m.group(1))
                elif x == "$module": mm.append(re.sub(r"api\.(cpp|h)$", "", os.path.basename(p)))
                else: mm.append(x)
            self.expand(dirs, mm, f"{why_prefix}: rule {pat}",
                        own_api=any(x in ("$1", "$module") for x in modules), kinds=kinds)
            return f"rule {pat}" + (f" modules={mm}" if mm else "")
        return None

    def graph_rows(self, p, why):
        """Selector 1: the rows whose executables the graph says this file reaches. Returns
        (n executables, app relinked?) or None when the graph does not know the file."""
        if not self.graph: return None
        ap = os.path.join(ROOT, p)
        if not self.graph.known(ap): return None
        exes = self.graph.reach_refined(ap)
        for e in exes:
            if e == self.app_exe: continue
            self.add(self.exe_rows.get(e, []), f"{why} [relinks {os.path.basename(e)}]")
        return len(exes), self.app_exe in exes

    def engine_rows(self, tag):
        """U2: ENGINE RUNTIME DATA (Hlms pieces, GLSL, compositor scripts, material JSON) is read
        by every executable that extracts the engine archive — the graph knows which (any engine
        object's reach; the facade pulls them all), so no directory list is consulted. Returns
        the executable count, or None without a graph."""
        if not self.graph: return None
        if not hasattr(self, "_engine_exes"):
            art = self.graph.targets.get("JahshakaEngine")
            mem = sorted(self.graph.members(art)) if art else []
            # every member: an executable that extracts any engine object runs the engine
            self._engine_exes = self.graph.reach_objects(mem)
        for e in self._engine_exes:
            if e != self.app_exe: self.add(self.exe_rows.get(e, []), f"{tag} [reads engine media: {os.path.basename(e)}]")
        return len(self._engine_exes)

    def argv_rows(self, p, why):
        """Rows whose command line or script IS this file (a wrapper, a lint, a configured .js)."""
        ap = os.path.normpath(os.path.join(ROOT, p))
        hit = list(self.script_rows.get(ap, []))
        base = os.path.basename(p).replace(".js.in", "").replace(".js", "")
        if not hit and p.endswith((".js", ".js.in")) and base in self.script_base:
            hit = list(self.script_base[base])      # a .js.in configured into the build dir
        self.add(hit, f"{why}: its command line runs it")
        return hit

    # -- the path walk ---------------------------------------------------------------------
    def path(self, p, depth=0, via=None):
        """Select for one reached path. `via` names why a path is reached indirectly (a symbol,
        a CMake command); a directly touched path has via=None."""
        key = (p, depth > 0)
        if key in self._visited: return
        self._visited.add(key)
        tag = p if via is None else f"{p} (via {via})"
        notes = []
        if p.startswith(("src/", "irisgl/", "tests/")): self.code_moved = True

        # THE FORK PIN: an Ogre change reaches everything (PHOTON_ATOM_CONTRACT §7b rule 4).
        if p == "irisgl/thirdparty/ogre-next" or p.startswith("irisgl/thirdparty/ogre-next/"):
            self.full_tier.append(f"{p}: the fork pin moved — an Ogre change reaches every suite (§7b rule 4)")
            self.expand(ENGINE_FAMILY + ["atom"], [], f"{p}: fork pin")
            if depth == 0: self.rationale.append((p, "fork pin bump → the MERGE tier by rule (§7b.4)"))
            return
        if p.startswith("irisgl/thirdparty/") or p.startswith("thirdparty/"):
            # vendored code: compiled into the tree (graph) or a submodule gitlink (no graph node)
            g = self.graph_rows(p, tag)
            if g is None:
                self.full_tier.append(f"{p}: a vendored component moved — nothing narrower owns it")
            if depth == 0: self.rationale.append((p, "vendored" + (f": graph {g[0]} executables" if g else " → the tier")))
            return

        base = os.path.basename(p)
        is_cmake = base == "CMakeLists.txt" or p.endswith(".cmake")
        is_cxx = p.endswith(CXX_EXT) and not p.startswith(("irisgl/thirdparty/", "thirdparty/"))

        if is_cmake:
            # a CMake helper an area rule owns (WrapHlmsPiece: the engine's media staging) keeps
            # its rule; every other build file is read by what its changed commands name
            if any(re.match(pat, p) for pat, _, _ in AREA_RULES):
                r = self.rules_for(p, tag)
                if p.startswith("irisgl/cmake/WrapHlmsPiece"):
                    n = self.engine_rows(tag)
                    if n is not None: r += f"; every executable that extracts the engine ({n})"
                if depth == 0: self.rationale.append((p, r))
                return
            self.cmake(p, tag, depth)
            return

        # every row that runs this very file (a script, a wrapper, a lint)
        hit_argv = self.argv_rows(p, tag)
        if hit_argv: notes.append(f"{len(hit_argv)} row(s) run it")
        # A DATA FILE OUTSIDE tests/ that tests load by name (app/content/primitives/cube.obj, read
        # by seven gi/atom tests): the test sources that name it are reached paths (the Fable read,
        # small item a) — a runtime read is no build edge
        if (not is_cxx and not p.startswith(("tests/", "docs/", "scripts/", ".")) and not p.endswith(".md")
                and depth == 0):
            users = [u for u in self.naming_tests(os.path.basename(p)) if u.endswith(CXX_EXT + (".js", ".js.in", ".sh"))]
            for u in users: self.path(u, 1, via=f"loads {os.path.basename(p)}")
            if users: notes.append(f"loaded by name in {len(users)} test file(s)")

        # a build input that is not C/C++ (a .ui form, a .qrc, a shader the build compiles into
        # SPIR-V) is the graph's too; runtime data (engine media, scenes) is not in it
        graph_input = (not is_cxx and self.graph is not None and not p.startswith("tests/")
                       and "/media/" not in p and self.graph.known(os.path.join(ROOT, p))
                       and bool(self.graph.reach(os.path.join(ROOT, p))))
        if is_cxx and self.revs.base and depth == 0 and self._deleted(p):
            # A DELETED C/C++ file: whatever used it had to change in this same diff (or the
            # build breaks), so it reaches nothing of its own; the source lints ride
            self.expand(["hygiene"], [], f"{tag}: deleted")
            self.rationale.append((p, "deleted → what used it is in this diff (the source lints ride)"))
            return
        if is_cxx or graph_input:
            # comments/whitespace only → nothing reached (the lints still ride)
            if is_cxx and self.revs.base and depth == 0:
                a, b = self.revs.texts(p)
                if a is not None and b and a and gate_graph.strip_cxx(a) == gate_graph.strip_cxx(b):
                    self.expand(["hygiene"], [], f"{tag}: comment/whitespace only")
                    self.rationale.append((p, "comments/whitespace only → no code reached (the source lints ride)"))
                    return
            if is_cxx and self.revs.base and depth == 0:
                self.expand(["hygiene"], [], f"{tag}: source text changed")   # the source lints read it
            if p.endswith(HEADER_EXT) and self.revs.base and depth == 0:
                if self.header_symbols(p, tag, notes):
                    self.rationale.append((p, "; ".join(notes)))
                    return
            g = self.graph_rows(p, tag)
            if g is not None and p.endswith(HEADER_EXT) and self.graph is not None:
                # EVERY INCLUDER of a header, for the app rows too: each reading source's own
                # rule — the superset of any symbol narrowing (a symbol's namers read the header)
                srcs = sorted({os.path.relpath(self.graph.obj_src[o], ROOT)
                               for o in self.graph.includers(os.path.join(ROOT, p)) if o in self.graph.obj_src})
                for src in srcs:
                    if src.startswith(("src/", "irisgl/")) and not src.startswith("irisgl/thirdparty/"):
                        self.rules_for(src, f"{tag} (read by {src})", kinds={"app", "other"})
                notes.append(f"{len(srcs)} reading source(s)' rules (app/other rows)")
                for f in getattr(self, "_script_namers", []):
                    self.path(f, 1, via=f"{os.path.basename(p)} symbol (a script)")
                self._script_namers = []
            if g is not None:
                n_exe, app = g
                notes.append(f"graph: {n_exe} executable(s)" + (" incl. the app" if app else ""))
                r = self.rules_for(p, tag, kinds={"app", "other"}) if not p.startswith("tests/") else None
                if r: notes.append(r + " (app/other rows)")
                elif app and not p.startswith("tests/"):
                    self.expand(["*app-rows"], [], f"{tag}: the app relinked and no rule narrows it")
                    notes.append("the app relinked, no rule narrows it → every app row")
                if depth == 0: self.rationale.append((p, "; ".join(notes)))
                return
            notes.append("not in the build graph")
            if self.graph is None:
                # no graph (an unbuilt dir): the tests CMake files that list this source, as before
                for d, files in cmake_src_refs().items():
                    if p in files: self.add(self.by_dir.get(d, []), f"{tag}: compiled into tests/{d}")
        if p.startswith("tests/"):
            self.tests_path(p, tag, notes, hit_argv)
            if depth == 0: self.rationale.append((p, "; ".join(notes) or "fallback"))
            return
        r = self.rules_for(p, tag)
        if r: notes.append(r)
        if p.startswith("irisgl/engine/") or p.startswith("irisgl/cmake/WrapHlmsPiece"):
            n = self.engine_rows(tag)
            if n is not None: notes.append(f"engine runtime data: every executable that extracts the engine ({n})")
        if r: pass
        elif not hit_argv:
            g = None if is_cxx else self.graph_rows(p, tag)     # a build input (.ui, .qrc, a shader source)
            if g is not None:
                notes.append(f"graph: {g[0]} executable(s)" + (" incl. the app" if g[1] else ""))
                if g[1]:
                    self.expand(["*app-rows"], [], f"{tag}: the app relinked and no rule narrows it")
                    notes.append("the app relinked, no rule narrows it → every app row")
            else:
                self.fallback.append(f"{tag}: no rule, no symbol, no graph owner")
                notes.append("NO RULE, no symbol, no graph owner → merge tier")
        if depth == 0: self.rationale.append((p, "; ".join(notes)))

    def _deleted(self, p):
        """True when the range deletes the file (it is at the base and not at the tip)."""
        a, b = self.revs.texts(p)
        return bool(a) and not b

    def tests_path(self, p, tag, notes, hit_argv):
        parts = p.split("/")
        d = parts[1] if len(parts) > 2 else ""
        if hit_argv:
            return
        base = os.path.basename(p).replace(".js.in", "").replace(".js", "").replace(".sh", "")
        if base in self.script_base and p.endswith((".js", ".js.in", ".sh")):
            self.add(self.script_base[base], f"{tag}: script"); notes.append("script"); return
        if d in self.by_dir:
            # a fixture, a data file, a helper script of a directory: its rows
            self.add(self.by_dir[d], f"{tag}: tests/{d}"); notes.append(f"tests/{d}"); return
        if p.endswith(HEADER_EXT):
            # a shared test header the graph does not know (a platform-only include)
            name = os.path.basename(p)
            owners = sorted(dd for dd in self.by_dir if header_included_by_dir(name, dd))
            if owners:
                for dd in owners: self.add(self.by_dir[dd], f"{tag}: included by tests/{dd}")
                notes.append("included by " + ", ".join(f"tests/{dd}" for dd in owners)); return
        # a shared helper (tests/support/*.js, *.sh) that other scripts source by name
        users = self.naming_tests(os.path.basename(p))
        if users:
            for u in users: self.path(u, 1, via=f"names {os.path.basename(p)}")
            notes.append(f"named by {len(users)} test file(s)"); return
        self.fallback.append(f"{tag}: no suite owns this tests/ path")
        notes.append("no suite owns it → fallback")

    def naming_tests(self, fname):
        try:
            r = subprocess.run(["git", "grep", "-l", "-F", fname, "--", "tests"], cwd=ROOT,
                               capture_output=True, text=True)
        except OSError:
            return []
        return [f for f in r.stdout.split() if not f.endswith(fname)]

    # An identifier named by more files than this is too common to select by (a member called
    # `bound` or `positions`): the header then selects every includer, the graph's answer.
    SYMBOL_SPECIFICITY = 60

    def header_symbols(self, p, tag, notes):
        """Selector 2. Returns True when the header's change was resolved through its symbols;
        False sends the header to the graph (every object that includes it)."""
        a, b = self.revs.texts(p)
        if a is None: return False
        if not a:
            # A NEW header: everything that includes it is in this diff, and the graph has it
            notes.append("a new header → its includers (all in this change)")
            return False
        ids, changed = gate_graph.cxx_changed_identifiers(a, b, gate_graph.hunks_of(self.revs.diff_u0(p)))
        if changed == 0:
            self.expand(["hygiene"], [], f"{tag}: comment/whitespace only")
            notes.append("comments/whitespace only → no code reached")
            return True
        if not ids:
            notes.append("no specific identifier on the changed lines → every includer")
            return False
        naming = gate_graph.files_naming(ROOT, ids)
        self._script_namers = sorted(f for f in naming if f.endswith((".js", ".js.in")))
        per_id = collections.Counter(t for toks in naming.values() for t in toks)
        common = sorted(t for t in ids if per_id[t] > self.SYMBOL_SPECIFICITY)
        if common:
            notes.append(f"symbol(s) {common[:6]} too common to select by (> {self.SYMBOL_SPECIFICITY} files) "
                         f"→ every includer")
            return False
        # USED INSIDE ITS OWN HEADER (a #define, a constexpr, an inline helper another inline
        # helper calls — enginetesthelpers.h's testCameraDescLookAt behind testCameraLookAt): its
        # users name the WRAPPER, not it, so no name-search can find them — every includer
        # (the Fable read, U1). Counted on the header's code text: an occurrence that is not the
        # identifier's own declaration is a use.
        # The count is taken OUTSIDE the changed lines: a use on a changed line is new code in
        # this very diff (StageMs used by the stageMs member added beside it), and its own
        # identifiers are already in the set.
        changed_new = set()
        for _os, _oc, ns, nc in gate_graph.hunks_of(self.revs.diff_u0(p)):
            changed_new |= set(range(ns - 1, ns - 1 + nc))
        kept = [l for k, l in enumerate(gate_graph.strip_cxx(b)) if k not in changed_new]
        # ...and never on the identifier's OWN declaration line (`struct MeshCardDesc {`, a
        # member's `float halfDepth = 0.0f;`): a declaration is not a use, and counting it sent
        # every member-default change to every includer (the second Fable read, precision (a))
        def used_inside(t):
            rx = re.compile(r"\b" + re.escape(t) + r"\b")
            decl = re.compile(r"\b(?:struct|class|union|enum(?:\s+class|\s+struct)?)\s+(?:\w+\s+)*?" + re.escape(t) + r"\b")
            for l in kept:
                if not rx.search(l) or decl.search(l): continue
                if t in gate_graph.declared_names(l, False)[0]: continue
                return True
            return False
        inner = sorted(t for t in ids if used_inside(t))
        if inner:
            notes.append(f"symbol(s) {inner[:6]} used inside the header itself (a wrapper's callers name "
                         f"the wrapper) → every includer")
            return False
        # the header's own companion source always counts (x.h -> x.cpp)
        stem = os.path.splitext(p)[0]
        for ext in (".cpp", ".cc", ".mm"):
            if os.path.exists(os.path.join(ROOT, stem + ext)): naming.setdefault(stem + ext, set()).add("(companion)")
        # A NAME COUNTS ONLY WHERE THIS HEADER IS READ: a source that names `DefaultMaterial`
        # but never includes this header is naming another declaration (measured: without this
        # an irisglfwd.h hunk reached the engine through its own DefaultMaterial). The graph's
        # deps log says who reads the header; scripts (verbs by name) always count. A naming
        # HEADER counts through EVERY object that reads both it and this header (U1: a
        # header-only helper — voxel_lab.h names GiVoxelVolume — has no .cpp companion; its code
        # is compiled into its includers).
        pa = os.path.join(ROOT, p)
        both = {}
        if self.graph is not None and self.graph.known(pa):
            p_readers = self.graph.includers(pa)
            readers = {os.path.relpath(self.graph.obj_src.get(o, ""), ROOT) for o in p_readers}
            for f in [f for f in naming if f.endswith(HEADER_EXT) and f != p]:
                objs = self.graph.includers(os.path.join(ROOT, f)) & p_readers
                # generated sources (the moc TU includes every Q_OBJECT header of its target) are
                # not readers of their own: the header they wrap is already on this path
                both[f] = sorted({r for r in (os.path.relpath(self.graph.obj_src[o], ROOT) for o in objs
                                              if o in self.graph.obj_src)
                                  if not r.startswith("..") and "_autogen/" not in r
                                  and not r.startswith(os.path.relpath(self.graph.build, ROOT) + "/")})
            def reads(f):
                if f.endswith((".js", ".js.in")): return True
                if f.endswith(HEADER_EXT): return bool(both.get(f))
                return f in readers
            naming = {f: v for f, v in naming.items() if reads(f)}
        reached = sorted(f for f in naming if f != p)
        shown = sorted(ids)[:12]
        notes.append(f"symbols {shown}{' …' if len(ids) > 12 else ''} named by {len(reached)} file(s)")
        # The files that NAME a changed identifier are the reached paths, each through its own
        # graph reach and rules; a naming header through every source that reads it with this one.
        for f in reached:
            if f.endswith(HEADER_EXT):
                for src in both.get(f, []):
                    self.path(src, 1, via=f"reads {os.path.basename(f)}, which names {sorted(naming[f])[0]}")
            else:
                self.path(f, 1, via=f"{os.path.basename(p)} symbol {sorted(naming[f])[0]}")
        self.expand(["hygiene"], [], f"{tag}: source text changed")
        return True

    # -- CMake -----------------------------------------------------------------------------
    def cmake(self, p, tag, depth):
        """A build file, read by what its CHANGED commands name (see cmake_command)."""
        notes = []
        d = os.path.dirname(p)
        parts = p.split("/")
        if not self.revs.base:
            # --files: no diff to read. A test dir's CMakeLists registers its dir's rows; any
            # other build file cannot be resolved without its diff.
            if p.startswith("tests/") and len(parts) == 3 and parts[1] in self.by_dir:
                self.add(self.by_dir[parts[1]], f"{tag}: tests/{parts[1]} registration")
                if depth == 0: self.rationale.append((p, f"tests/{parts[1]}'s registration → its rows"))
                return
            self.fallback.append(f"{tag}: a build file with no diff to read (--files)")
            if depth == 0: self.rationale.append((p, "a build file without a range → merge tier"))
            return
        a, b = self.revs.texts(p)
        if not a and b:
            # A NEW build file: everything it registers and builds is new — its rows and the
            # executables built under its directory
            rows = [n for n, t in self.inv.items() if os.path.join(ROOT, p) in t["frames"]]
            self.add(rows, f"{tag}: a new build file's rows")
            n_exe = self.exes_under(d, tag)
            if depth == 0: self.rationale.append((p, f"a new build file → its {len(rows)} row(s), {n_exe} executable(s) built under {d}"))
            return
        old_l, new_l = _changed_line_numbers(self.revs.diff_u0(p))
        a_lines, b_lines = (a or "").split("\n"), (b or "").split("\n")
        # list-only lines (a source added/removed) say nothing: the files are in the diff — but
        # ONLY inside a source list. The same shape on an add_test's COMMAND (a script path on its
        # own line) is a changed ROW (the Fable read, small item c).
        def interesting(lines, nums):
            spans = gate_graph.cmake_commands("\n".join(lines))
            out = set()
            for n in nums:
                t = lines[n - 1].strip() if 0 < n <= len(lines) else ""
                if not t or t.startswith("#"): continue
                if _LIST_ENTRY.match(t):
                    owner = next((c[0] for c in spans if c[1] <= n <= c[2]), None)
                    if owner in _SOURCE_LISTS: continue
                out.add(n)
            return out
        old_i, new_i = interesting(a_lines, old_l), interesting(b_lines, new_l)
        if not old_i and not new_i:
            if depth == 0: self.rationale.append((p, "source-list / comment edit only → scoped by the files it names"))
            return
        cmds = []
        for text, nums in ((a or "", old_i), (b or "", new_i)):
            if not nums: continue
            for c in gate_graph.cmake_commands(text):
                if any(c[1] <= n <= c[2] for n in nums):
                    # the changed lines' text, a trailing comment cut (a token in a comment names nothing)
                    changed_text = "\n".join(re.sub(r'\s#[^"]*$', "", text.split("\n")[n - 1])
                                             for n in sorted(nums) if c[1] <= n <= c[2])
                    cmds.append((c, changed_text, text))
        unresolved = []
        for (name, first, last, args, fn), changed_text, whole in cmds:
            got = self.cmake_command(p, d, name, args, fn, changed_text, whole, tag, notes)
            if not got: unresolved.append(f"{name}() at line {first}")
        if unresolved:
            self.fallback.append(f"{tag}: CMake command(s) that name nothing a suite owns: {', '.join(unresolved)}")
            notes.append("unresolved: " + ", ".join(unresolved))
        if depth == 0: self.rationale.append((p, "; ".join(dict.fromkeys(notes)) or "CMake"))

    def exes_under(self, d, tag):
        """Every executable built under a source directory (its build dir), and its rows."""
        if not self.graph: return 0
        bsub = os.path.join(self.graph.build, d) if d else self.graph.build
        exes = {e for e in self.graph.exes if e.startswith(bsub + "/")}
        for e in exes:
            if e != self.app_exe: self.add(self.exe_rows.get(e, []), f"{tag}: built under {d}")
        if self.app_exe in exes:
            self.expand(["*app-rows"], [], f"{tag}: the app is built under {d or '.'}")
        return len(exes)

    def target_changed(self, t, tag, notes):
        """A build target's objects changed (a flag, a property, a link): its executables' rows."""
        art = self.graph.targets[t]
        if self.graph.kind.get(art) == "static":
            # an archive's objects, through the link graph: where they are referenced
            mem = self.graph.members(art)
            exes = self.graph.reach_objects(mem)
            vendored = all("/thirdparty/" in self.graph.obj_src.get(o, "/thirdparty/") for o in mem)
        else:
            exes = self.graph.target_reach(t) or set()
            vendored = False
        for e in exes:
            if e == self.app_exe: continue
            self.add(self.exe_rows.get(e, []), f"{tag}: target {t}")
        if self.app_exe in exes:
            refs = []
            if vendored:
                # a vendored archive enters our code through the objects that call it: the
                # app rows are those objects' families (the mesh simplifier -> the bake's)
                refs = sorted({self.graph.obj_src[o] for o in
                               self.graph.first_party_referrers(art, ("/thirdparty/",)) if o in self.graph.obj_src})
                for r_ in refs:
                    self.path(os.path.relpath(r_, ROOT), 1, via=f"calls into {t}")
                notes.append(f"target {t} (vendored) enters our code through "
                             f"{[os.path.relpath(r_, ROOT) for r_ in refs]}")
            if not refs:
                self.expand(["*app-rows"], [], f"{tag}: target {t} is in the app")
        notes.append(f"target {t}: {len(exes)} executable(s)")

    _STRUCTURAL = {"endfunction", "endmacro", "else", "elseif", "endif", "foreach", "endforeach",
                   "while", "endwhile", "return", "break", "continue"}

    # the commands that register or decorate a TEST (their first arguments are row names)
    _TEST_REGISTRATION = {"add_test", "set_tests_properties", "jah_fresh_home_fixture",
                          "jah_no_display", "jah_tsan_blocked", "jah_lsan_blocked", "jah_tsan_lane",
                          "jah_gpu_exclusive_test"}
    # what a retired suite's check became, printed with its `retired:` reason
    RETIRED = {
        "app.create_loop": "SUITE-POOL-1; its one unique check (the Properties column after 40 "
                           "creates) is open.responsive's full-library arm",
        "test_create_loop": "app.create_loop's executable (SUITE-POOL-1; open.responsive carries its check)",
        "app.create_loop.fresh_home": "app.create_loop's fixture (SUITE-POOL-1; open.responsive carries its check)",
        "atom.error_bound": "SUITE-POOL-1; merged into atom.lod_bound_bar (test_mesh_bake --bound-bar)",
    }

    def gone_row(self, name, args, toks, whole=""):
        """(`arm`, [pool::arm…]) when the row a test command registers or names is gone and its
        script is now an arm's; (`retired`, why) when what it names exists nowhere in this tree;
        None when it names something live (or cannot tell — the other resolutions then run)."""
        a_ = args.split()
        arm_names = {f"{t['pool']}.{a}" for t in self.inv.values() for a in t["arms"]}
        live = lambda n: n in self.row_names or n in arm_names or self.names_anywhere(n)
        if name in self._TEST_REGISTRATION:
            if name in ("add_test", "jah_gpu_exclusive_test"):
                k = a_.index("NAME") + 1 if "NAME" in a_ else len(a_)
                named = a_[k:k + 1]
            elif name == "set_tests_properties":
                named = a_[:a_.index("PROPERTIES")] if "PROPERTIES" in a_ else a_[:1]
            else:
                named = [x for x in a_ if "." in x and not x.startswith(("$", '"'))][:4]
            named = [n for n in named if n and "$" not in n]
            if not named or any(live(n) for n in named):
                return None             # a live row: the ordinary resolutions own it
            # its script (on this command, or on the gone row's own registration in the same
            # file) is now a pool ARM's → that arm
            cands = [toks]
            for c in gate_graph.cmake_commands(whole or ""):
                ca = c[3].split()
                regs = ([ca[ca.index("NAME") + 1]] if c[0] in ("add_test", "jah_gpu_exclusive_test")
                        and "NAME" in ca and ca.index("NAME") + 1 < len(ca) else [])
                if set(regs) & set(named):
                    cands.append(set(re.findall(r"[A-Za-z0-9_.+\-/${}]+", c[3])))
            for tk in cands:
                scripts = {os.path.basename(t).replace(".js.in", "").replace(".js", "")
                           for t in tk if t.endswith((".js", ".js.in"))}
                arms = sorted({r for b in scripts for r in self.script_base.get(b, []) if "::" in r})
                if arms: return ("arm", arms)
            return ("retired", "; ".join(f"{n} — {self.RETIRED.get(n, 'no row, arm or fixture of that name')}"
                                         for n in named))
        # a target command: the target must be gone from the build graph AND from every build
        # file of the tree
        tgt = a_[0] if a_ else ""
        if not tgt or "$" in tgt or not self.graph or tgt in self.graph.targets or self.names_anywhere(tgt):
            return None
        return ("retired", f"{tgt} — {self.RETIRED.get(tgt, 'no target of that name in this tree')}")

    def names_anywhere(self, token):
        """Whether any build file of THIS tree still names the token (a fixture, a target, a
        row registered under a variable the inventory cannot see)."""
        if not hasattr(self, "_all_cmake"):
            texts = []
            for dp, dns, fs in os.walk(ROOT):
                dns[:] = [x for x in dns if not x.startswith(("build", ".")) and x != "thirdparty"]
                for f in fs:
                    if f == "CMakeLists.txt" or f.endswith(".cmake"):
                        try: texts.append(open(os.path.join(dp, f), errors="replace").read())
                        except OSError: pass
            self._all_cmake = "\n".join(texts)
        return re.search(r"(?<![\w.\-])" + re.escape(token) + r"(?![\w.\-])", self._all_cmake) is not None

    def cmake_command(self, p, d, name, args, fn, changed_text, whole, tag, notes, depth=0):
        """Resolve one changed CMake command to what it names. True when something owned it.

        In order: rows the command names (anywhere in it: an add_test's NAME sits on another line
        than its changed COMMAND); a helper function's body or header (the rows registered
        through it, the targets it is called on); a target the command CHANGES (target_*(),
        set_target_properties(), a helper called on a target); sources it names; a
        sub-directory; a variable (its user commands, once); and last a directory-scope
        setting, which reaches every target under the directory."""
        if name in ("message",):
            notes.append("message() only"); return True
        # the whole command for anything but a list: an add_test's NAME sits on another line than
        # its changed COMMAND; a suite list's change is only the names it adds or drops
        full = changed_text if name in ("set", "list") else changed_text + "\n" + args
        toks = set(re.findall(r"[A-Za-z0-9_.+\-/${}]+", full))
        toks = {t.replace("${CMAKE_PROJECT_NAME}", "Jahshaka") for t in toks}
        got = False
        # 0. a pool's arm or row (SUITE-POOL-1's helpers): the arm alone, or the pool whole
        a_ = args.split()
        if name == "jah_pool_arm" and len(a_) >= 2 and f"pool.{a_[0]}" in self.inv:
            self.add([f"pool.{a_[0]}::{a_[1]}"], f"{tag}: its arm")
            notes.append(f"arm {a_[0]}.{a_[1]}"); return True
        if name == "jah_add_pool" and a_ and f"pool.{a_[0]}" in self.inv:
            self.add([f"pool.{a_[0]}"], f"{tag}: the pool's row")
            notes.append(f"pool {a_[0]} (every arm)"); return True
        # 1. rows named outright (an add_test, a set_tests_properties, a suite list)
        rows = sorted(t for t in toks if t in self.row_names)
        if rows:
            self.add(rows, f"{tag}: names the row"); notes.append(f"names {len(rows)} row(s)"); got = True
        # 1b. A ROW THAT IS GONE (SUITE-POOL-1; a replayed or long-lived diff meets a tree where
        # its row no longer exists): a registration whose SCRIPT is now a pool arm's resolves to
        # that ARM ("the row became an arm"); one naming a suite or a test target that exists
        # nowhere any more — no row, no arm, no fixture, no target — resolves to NOTHING, said
        # out loud (`retired: <name>`), never a fallback and never silent.
        if not rows and name in self._TEST_REGISTRATION | {"add_executable", "set_target_properties"} \
                or (not rows and name.startswith("target_")):
            gone = self.gone_row(name, args, toks, whole)
            if gone is not None:
                kind, what = gone
                if kind == "arm":
                    self.add(what, f"{tag}: the row became an arm")
                    notes.append(f"the row became an arm: {', '.join(what)}")
                else:
                    notes.append(f"retired: {what}")
                return True
        # 2. a helper function: its body (fn) or its own header line
        helper = fn if fn else (args.split()[0].lower() if name in ("function", "macro") and args.split() else None)
        if helper:
            via = [n for n, t in self.inv.items() if helper in t["via"]]
            if via:
                self.add(via, f"{tag}: inside {helper}()"); notes.append(f"{helper}(): {len(via)} row(s)")
            calls = self.cmake_calls(helper)
            for tgt in calls:
                if self.graph and tgt in self.graph.targets:
                    self.target_changed(tgt, f"{tag}: {helper}({tgt})", notes)
            if via or calls or p.startswith("tests/"):
                notes.append(f"{helper}() called on {len(calls)} target(s)")
                got = True
        if name in self._STRUCTURAL and (fn or got):
            return True
        # 3. a target this command changes (target_*(), set_target_properties(), a helper on it)
        a0 = [x.replace("${CMAKE_PROJECT_NAME}", "Jahshaka") for x in args.split()[:1]]
        if (self.graph and a0 and a0[0] in self.graph.targets and not rows
                and name not in ("add_test", "set_tests_properties", "add_custom_command")):
            self.target_changed(a0[0], tag, notes); got = True
        # 4. source files named (set_source_files_properties, a list outside a target)
        srcs = []
        for t in toks:
            if not t.endswith(CXX_EXT) or "$" in t: continue
            for cand in (os.path.normpath(os.path.join(d, t)), os.path.normpath(t)):
                if os.path.isfile(os.path.join(ROOT, cand)): srcs.append(cand); break
        for s_ in sorted(set(srcs)):
            self.path(s_, 1, via=f"{os.path.basename(p)} {name}()")
        if srcs: notes.append(f"{len(set(srcs))} source(s) named"); got = True
        # 4b. a configured script (configure_file(x.js.in x.js)), or a timing twin's generated copy
        # (jah_timing_script(<in> <out>.timing.js), D6B-GATE-SHAPE): the rows that run either file
        if name in ("configure_file", "jah_timing_script"):
            a_ = args.split()
            for x in a_[:2]:
                bn = os.path.basename(x).replace(".js.in", "").replace(".js", "").replace(".in", "")
                hit = self.script_base.get(bn, [])
                if hit:
                    self.add(hit, f"{tag}: configures {os.path.basename(x)}")
                    notes.append(f"configures the script of {len(hit)} row(s)"); got = True
        # 5. a sub-directory
        if name.startswith("add_subdirectory"):
            a_ = args.split()
            sd = (a_[1] if name == "add_subdirectory_with_folder" and len(a_) > 1 else (a_[0] if a_ else "")).strip('"')
            sub = os.path.normpath(os.path.join(d, sd))
            reg = [n for n, t in self.inv.items() if t["reldir"] == sub or t["reldir"].startswith(sub + "/")]
            self.add(reg, f"{tag}: add_subdirectory({sd})")
            n_exe = self.exes_under(sub, f"{tag}: add_subdirectory({sd})")
            notes.append(f"add_subdirectory({sd}): {len(reg)} row(s), {n_exe} executable(s)"); got = True
        # 6. a variable: follow its users in the same file, once (not when the change only
        # listed rows: a suite list's change is the rows it adds or drops)
        if name in ("set", "list", "option", "string") and depth == 0 and not rows:
            a_ = args.split()
            var = a_[1] if name in ("list", "string") and len(a_) > 1 else (a_[0] if a_ else "")
            users = [c for c in gate_graph.cmake_commands(whole)
                     if re.search(r"\$\{" + re.escape(var) + r"\}|\b" + re.escape(var) + r"\b", c[3])
                     and not (c[0] == name and c[3].split()[:1] == a_[:1])]
            for (un, uf, ul, ua, ufn) in users:
                if self.cmake_command(p, d, un, ua, ufn, ua, whole, tag, notes, depth=1): got = True
            if users: notes.append(f"variable {var}: {len(users)} user command(s)")
            # ...and the configured scripts that substitute it (@VAR@ in a .in beside this file)
            if var and not got:
                for dp, _, fs in os.walk(os.path.join(ROOT, d or ".")):
                    for f in fs:
                        if not f.endswith(".in"): continue
                        try:
                            if "@" + var + "@" not in open(os.path.join(dp, f), errors="replace").read(): continue
                        except OSError:
                            continue
                        hit = self.script_base.get(f.replace(".js.in", "").replace(".in", ""), [])
                        if hit:
                            self.add(hit, f"{tag}: {var} is substituted into {f}")
                            notes.append(f"{var} substituted into {f}: {len(hit)} row(s)"); got = True
                    if d in ("", "."): break
        # 6b. a local variable the command reads (${_home} in a file(MAKE_DIRECTORY)): the
        # commands that read it too — the row whose ENVIRONMENT names that home
        if not got and depth == 0:
            for var in sorted(set(re.findall(r"\$\{(_\w+)\}", args))):
                users = [c for c in gate_graph.cmake_commands(whole)
                         if "${" + var + "}" in c[3] and c[3] != args and c[0] != "set"]
                for (un, uf, ul, ua, ufn) in users:
                    if self.cmake_command(p, d, un, ua, ufn, ua, whole, tag, notes, depth=1): got = True
                if users and got: notes.append(f"{name}() reads {var}, which {len(users)} command(s) use")
        # 7. a directory-scope setting: every target under this directory
        if not got and depth == 0 and (name in gate_graph.DIRECTORY_SCOPE or name in self._STRUCTURAL
                        or name in ("if", "option", "set", "list", "string")):
            under = d or "."
            if under in (".", "cmake", "tests", "irisgl", "irisgl/cmake") or p.startswith("cmake/"):
                self.full_tier.append(f"{tag}: {name}() at directory scope of {under} reaches every target under it")
            elif under.startswith("tests/"):
                dd = under.split("/")[1]
                self.add(self.by_dir.get(dd, []), f"{tag}: {name}() at directory scope of {under}")
                self.exes_under(under, tag)
            else:
                self.exes_under(under, tag)
            notes.append(f"{name}() at directory scope of {under}"); got = True
        return got

    def cmake_calls(self, fn):
        """The first arguments of every call of a CMake helper across the tree's build files."""
        if not hasattr(self, "_cmake_texts"):
            self._cmake_texts = []
            for top in ("CMakeLists.txt", "cmake", "tests", "irisgl/CMakeLists.txt", "irisgl/cmake",
                        "irisgl/engine", "src"):
                full = os.path.join(ROOT, top)
                if os.path.isfile(full):
                    self._cmake_texts.append(open(full, errors="replace").read()); continue
                for dp, _, fs in os.walk(full):
                    for f in fs:
                        if f == "CMakeLists.txt" or f.endswith(".cmake"):
                            try: self._cmake_texts.append(open(os.path.join(dp, f), errors="replace").read())
                            except OSError: pass
        out = []
        pat = re.compile(r"^\s*" + re.escape(fn) + r"\s*\(\s*([^\s)]+)", re.M | re.I)
        for t in self._cmake_texts:
            out += [m.group(1).replace("${CMAKE_PROJECT_NAME}", "Jahshaka") for m in pat.finditer(t)]
        return sorted(set(o for o in out if not o.startswith(("_", "${"))))


def cmake_src_refs():
    """tests/<dir> -> src/irisgl files its CMakeLists compiles (the no-graph fallback only)."""
    refs = collections.defaultdict(set)
    tests = os.path.join(ROOT, "tests")
    for d in os.listdir(tests):
        cm = os.path.join(tests, d, "CMakeLists.txt")
        if not os.path.exists(cm): continue
        for m in re.finditer(r"\$\{CMAKE_SOURCE_DIR\}/((?:src|irisgl)/[A-Za-z0-9_/.\-]+)", open(cm).read()):
            refs[d].add(m.group(1))
    return refs


def header_included_by_dir(name, d):
    """True when a source under tests/<d>/ includes the header `name` (by basename)."""
    pat = re.compile(r'^\s*#\s*include\s*[<"](?:[^">]*/)?' + re.escape(name) + r'[">]', re.M)
    rel = d.split("tests/", 1)[1] if "tests/" in d else d
    if not rel or rel.startswith(".."): return False
    root = os.path.join(ROOT, "tests", rel)
    for dirpath, _, files in os.walk(root):
        for f in files:
            if f.endswith((".cpp", ".h", ".hpp")):
                try:
                    with open(os.path.join(dirpath, f), errors="replace") as fh:
                        if pat.search(fh.read()): return True
                except OSError:
                    pass
    return False


def select(paths, rng, build, jobs, graph=None, inv=None, quiet_graph=False):
    """The whole selection as data (gate.selection replays call this in-process)."""
    inv = inv if inv is not None else load_inventory(build)
    if graph is None:
        graph = gate_graph.NinjaGraph.load(build)
    if graph is not None and graph.deps_objects == 0:
        if not quiet_graph:
            sys.stderr.write("gate-scope: the build dir has no ninja deps log (not built yet?) — the graph "
                             "selector is OFF and compiled rows go by the area rules' directories\n")
        graph = None
    if graph is not None:
        gate_graph.require_nm()
    app_exe = classify_rows(inv, graph, build)
    if graph is None:
        for t in inv.values():
            if t["kind"] == "compiled": t["kind"] = "other"     # the rules' dirs pick them, as before
    S = Selection(inv, graph, app_exe, Revs(rng), jobs)
    for p in paths:
        S.path(p)
    if S.code_moved: S.add(ALWAYS_ON_CODE, "code moved: smoke + contract")
    if graph is not None: graph.save_syms()
    return S


def joint(range_a, range_b, build, jobs):
    """THE JOINT SUITES (TESTING_V2 T4, the simple form): two lanes that touched one file family
    are merged on the UNION of their selections, and the rows BOTH select — the shared map in
    practice: the subjects both changes reach — are named, because a combination is what neither
    lane's own gate could see. Returns a dict (the --json form)."""
    graph = gate_graph.NinjaGraph.load(build)
    inv0 = load_inventory(build)
    import copy as _copy
    out = {}
    sels = []
    for rng in (range_a, range_b):
        S = select(touched_paths(rng), rng, build, jobs, graph=graph, inv=_copy.deepcopy(inv0), quiet_graph=True)
        sels.append(S)
    A, B = sels
    whole = bool(A.fallback or A.full_tier or B.fallback or B.full_tier)
    gating = lambda n: not (inv0[n]["labels"] & (SCOPE_EXCLUDED_LABELS | TARGET_LABELS))
    sa = {n for n in A.selected if gating(n)}
    sb = {n for n in B.selected if gating(n)}
    shared_paths = sorted(set(touched_paths(range_a)) & set(touched_paths(range_b)))
    # THE JOINT ROWS: the lanes' OWN guards (the rows their test-side changes selected: a test
    # source, a script, a registration) that the OTHER lane's change also reaches. Each guard
    # passed on its own lane's tree; the merge is the first tree where the other change is in it.
    # (Plain "selected by both" is the engine's whole reach for two engine lanes — hundreds of
    # rows that say nothing about the combination.)
    own = lambda S: {n for n in S.owned if n in S.selected}
    both = sorted((own(A) & sb) | (own(B) & sa))
    union = sorted(sa | sb)
    out = {"ranges": [range_a, range_b], "shared_paths": shared_paths, "joint": both,
           "union": union, "whole_tier": whole,
           "command": merge_tier(jobs) if whole else
           ("ctest -j%d --timeout 120 --output-on-failure --no-tests=error -R '^(%s)$'"
            % (jobs, "|".join(re.escape(n) for n in union)) if union else "")}
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("range", nargs="?", help="git range base..tip (Studio repo)")
    ap.add_argument("--files", nargs="*", help="explicit touched paths instead of a range")
    ap.add_argument("--build", default="build-linux")
    ap.add_argument("--run", action="store_true", help="run the selection now (DISPLAY must be set); "
                    "every suite's verdict goes to the run log")
    ap.add_argument("-j", "--jobs", type=int, default=4,
                    help="ctest parallelism (default 4 — the tier's contract; a lane beside other live lanes runs 2)")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--lane", default=None, help="the lane/stage name the run log records (default: the branch)")
    ap.add_argument("--tier", default=None, choices=gate_runlog.TIERS,
                    help="the run log's tier name (default: scoped; scoped-fallback / scoped-tier when a scoped "
                         "gate runs the whole tier; joint for --joint)")
    ap.add_argument("--record-times", action="store_true",
                    help="refresh scripts/gate-times.txt from the run log (median PASS seconds, 14 days)")
    ap.add_argument("--solo", metavar="SUITE", nargs="+",
                    help="the flake protocol: run each suite alone --times times, logged as retries")
    ap.add_argument("--times", type=int, default=3)
    ap.add_argument("--merge-tier", action="store_true",
                    help="print the MERGE tier's ctest command (at -j) and exit — the one source "
                         "docs/TESTING_GATE.md quotes instead of a copy of the -LE set")
    ap.add_argument("--joint", nargs=2, metavar=("RANGE_A", "RANGE_B"),
                    help="the joint suites of two lanes merged together: the union of both selections, "
                         "naming the rows BOTH select (the shared map) and the paths both touched")
    ap.add_argument("--nightly-tier", action="store_true",
                    help="print the NIGHTLY tier's ctest command (every `nightly` row, -j1) and exit")
    a = ap.parse_args()
    if a.record_times:
        record_times(); return
    if a.merge_tier:
        print(merge_tier(a.jobs)); return
    if a.nightly_tier:
        print(nightly_tier()); return
    build = resolve_build(a.build)
    # THE BUILT FORK MUST BE THE PIN (TESTING-DEBTS-1 T12): a run on an install built from another
    # fork commit is void (stale media) — refused before a suite runs, with the lines that fix it.
    # (A range that MOVES the pin still selects the MERGE tier by rule, §7b.4; this is the tree.)
    if a.run or a.solo:
        bad = gate_runlog.fork_pin_problem()
        if bad:
            sys.stderr.write("gate-scope: " + bad + "\n")
            sys.exit(4)
    if a.joint:
        J = joint(a.joint[0], a.joint[1], build, a.jobs)
        if a.json:
            print(json.dumps(J, indent=1)); return
        print(f"gate-scope --joint: {a.joint[0]}  +  {a.joint[1]}")
        print(f"\npaths BOTH changes touched ({len(J['shared_paths'])}):")
        for p in J["shared_paths"]: print(f"  {p}")
        print(f"\nTHE JOINT ROWS — each lane's own guards that the other lane's change also reaches (the "
              f"shared map; the combination neither lane's gate saw) ({len(J['joint'])}):")
        for n in J["joint"]: print(f"  {n}")
        print(f"\nthe merge gate = the UNION of both selections: "
              + ("the MERGE tier (one side selects it)" if J["whole_tier"] else f"{len(J['union'])} row(s)"))
        print(f"\n{J['command']}")
        if a.run and J["command"]:
            lane = a.lane or "joint"
            sys.exit(gate_runlog.run_ctest(J["command"], build, a.tier or "joint", lane, a.jobs,
                                           reasons={n: ("joint: both" if n in J["joint"] else "joint: union")
                                                    for n in J["union"]}))
        return
    lane = a.lane or gate_runlog._git(["rev-parse", "--abbrev-ref", "HEAD"])
    # the run log records the range by sha (HEAD moves; the record must not)
    log_range = a.range
    if a.range and ".." in a.range:
        b_, t_ = a.range.split("..", 1)
        log_range = "%s..%s" % (gate_runlog._git(["rev-parse", "--short=9", b_]) or b_,
                                gate_runlog._git(["rev-parse", "--short=9", t_]) or t_)
    if a.solo:
        rc = 0
        for s in a.solo:
            for _ in range(a.times):
                rx = "^" + re.escape(s) + "$"
                r = gate_runlog.run_ctest(f"ctest -j1 --timeout 900 --output-on-failure --no-tests=error -R '{rx}'",
                                          build, a.tier or "scoped", lane, 1, reasons={s: "solo retry"},
                                          rng=log_range, retry=True)
                rc = rc or r
        sys.exit(rc)
    if not (a.range or a.files):
        ap.error("give a range (base..tip) or --files")
    paths = a.files if a.files else touched_paths(a.range)

    costs = load_costs()
    S = select(paths, a.range if not a.files else None, build, a.jobs)
    inv, selected = S.inv, S.selected

    # The quiet-box measurements never ride a scoped gate (see NIGHTLY_LABELS). A `nightly`
    # row without `quiet-box` stays: its subject changed, and it is that change's guard.
    nightly = [n for n in selected if inv[n]["labels"] & SCOPE_EXCLUDED_LABELS]
    for n in nightly: selected.pop(n)
    # TARGET TESTS ARE SPLIT OUT, NOT DROPPED (see TARGET_LABELS).
    targets = sorted(n for n in selected if inv[n]["labels"] & TARGET_LABELS)
    selected_targets = {n: selected.pop(n) for n in targets}
    names = sorted(selected)
    # POOLS SELECTED BY ARM (TESTING_V2 T3 §2.3): one environment variable carries every partial
    # pool's arms for the whole ctest line (run_pool.py reads its own pool's entries; a pool
    # absent from it runs every arm)
    subsets = {r: arms for r, arms in S.arm_subsets().items() if r in selected}
    pool_env = ",".join(f"{inv[r]['pool']}.{arm}" for r in sorted(subsets) for arm in subsets[r])

    def cost(n):
        if n in subsets:   # the selected arms plus one boot
            return 10.0 + sum(costs.get(f"{n}::{arm}", 10.0) for arm in subsets[n])
        return costs.get(n, 10.0)
    est = sum(cost(n) for n in names)
    serial = sum(cost(n) for n in names if inv[n]["serial"])
    wall = max(est / float(a.jobs), serial) + 5
    tier_rows = [n for n, t in inv.items() if not (t["labels"] & (NIGHTLY_LABELS | TARGET_LABELS))]
    tier_est = sum(costs.get(n, 10.0) for n in tier_rows)

    def ctest_for(suites, jobs):
        rx = "^(" + "|".join(re.escape(n) for n in suites) + ")$"
        return f"ctest -j{jobs} --timeout 120 --output-on-failure --no-tests=error -R '{rx}'"

    whole_tier = bool(S.fallback or S.full_tier)
    cmd = ctest_for(names, a.jobs) if names else ""
    if cmd and pool_env:
        cmd = f"JAH_POOL_ARMS='{pool_env}' {cmd}"
    target_cmd = ctest_for(targets, 1) if targets else ""

    if a.json:
        print(json.dumps({"paths": paths, "suites": names, "targets": targets, "fallback": S.fallback,
                          "full_tier": S.full_tier, "reasons": {n: selected[n] for n in names},
                          "arms": {r: {arm: S.arms[r][arm] for arm in subsets[r]} for r in subsets},
                          "pool_arms_env": pool_env,
                          "rationale": [{"path": p, "why": w} for p, w in S.rationale],
                          "graph": S.graph is not None,
                          "estimated_seconds": est, "estimated_wall": wall,
                          "tier_rows": len(tier_rows), "tier_estimated_seconds": tier_est,
                          "command": merge_tier(a.jobs) if whole_tier else cmd,
                          "target_command": target_cmd}, indent=1))
        return
    print(f"gate-scope: {len(paths)} touched path(s)"
          + ("" if S.graph else "   [NO BUILD GRAPH: compiled rows by directory rules]"))
    for p, why in S.rationale: print(f"  {p}\n      -> {why}")

    def run_tier(reason):
        tier = merge_tier(a.jobs)
        print(f"\n{tier}")
        if a.run:
            # a SCOPED gate that ran the whole tier is logged as such — "scoped-fallback" (no rule,
            # no symbol, no graph owner) or "scoped-tier" (the tier by rule: a fork pin) — so the
            # run log tells it from the lead's MERGE tier runs
            sys.exit(gate_runlog.run_ctest(tier, build, a.tier or ("scoped-fallback" if reason == "fallback"
                                                                   else "scoped-tier"), lane, a.jobs,
                                           reasons={}, rng=log_range, labels={n: t["labels"] for n, t in inv.items()}))
    if S.full_tier and not S.fallback:
        print("\nTHE MERGE TIER BY RULE (the change reaches every suite):")
        for f in S.full_tier: print(f"  {f}")
        run_tier("rule"); return
    if S.fallback:
        print("\nFALLBACK → MERGE TIER (a touched path has no rule, no symbol and no graph owner):")
        for f in S.fallback + S.full_tier: print(f"  {f}")
        run_tier("fallback"); return
    if S.skipped_ubiquitous:
        print(f"\n(modules called by >40% of scripts select nothing on their own: {sorted(S.skipped_ubiquitous)})")
    if nightly: print(f"\n(quiet-box / nightly measurements left out: {sorted(nightly)})")
    if targets:
        print(f"\nTARGET TESTS (label {'/'.join(sorted(TARGET_LABELS))}) — they RUN and PRINT their "
              f"value, and they do NOT decide this gate:")
        for n in targets: print(f"  {costs.get(n, 0):7.1f}  {n}   <- {selected_targets[n]}")
    if not names and not targets:
        # THE ONLY HONEST EMPTY SELECTION is a change that moved no code.
        if S.code_moved:
            sys.stderr.write(
                "gate-scope: a code path selected NO suite — the rules fired (see the rationale "
                "above) but\n            named nothing this build dir registers. That is a rule "
                "or inventory defect, not\n            an empty change; refusing to report a "
                "green empty gate.\n")
            sys.exit(3)
        print("\nSCOPED tier: NOTHING to gate — every touched path is docs/scripts/data with no owning suite "
              "(a code path always adds app.startup_quiet + api.contract)")
        return
    if names:
        print(f"\nSCOPED tier: {len(names)} of {len(tier_rows)} tier row(s), ~{est:.0f} of ~{tier_est:.0f} "
              f"suite-seconds, ~{wall/60:.1f} min wall at -j{a.jobs} (serial islands {serial:.0f} s); "
              f"{cost_sources([k for n in names for k in ([f'{n}::{a_}' for a_ in subsets[n]] if n in subsets else [n])])}")
        for n in names:
            print(f"  {cost(n) if n in subsets else costs.get(n, 0):7.1f}  {n}   <- {selected[n]}")
            for arm in subsets.get(n, []):
                print(f"           arm {inv[n]['pool']}.{arm}   <- {S.arms[n][arm]}")
        print(f"\n{cmd}")
    else:
        print("\nSCOPED tier: every selected suite is a TARGET test — this change gates on nothing "
              "of its own, and the targets below still run and report.")
    if target_cmd: print(f"\n{target_cmd}    # target tests: reported, NOT gating")
    if a.run:
        labels = {n: t["labels"] for n, t in inv.items()}
        reasons = dict(selected)
        for r in subsets:
            for arm in subsets[r]: reasons[f"{r}::{arm}"] = S.arms[r][arm]
        env = dict(os.environ, JAH_POOL_ARMS=pool_env) if pool_env else None
        rc = gate_runlog.run_ctest(cmd.split(" ", 1)[1] if pool_env else cmd, build, a.tier or "scoped", lane,
                                   a.jobs, reasons=reasons, rng=log_range, labels=labels, env=env) if cmd else 0
        if target_cmd:
            # THE TARGETS' RUN IS A REPORT. Its exit code is printed and thrown away.
            print("\n=== target tests (label %s): reported, not gating ==="
                  % "/".join(sorted(TARGET_LABELS)))
            trc = gate_runlog.run_ctest(target_cmd, build, a.tier or "scoped", lane, 1,
                                        reasons=selected_targets, rng=log_range, labels=labels,
                                        gating=lambda n: False)
            print("=== target tests exited %d — NOT part of this gate's verdict ===" % trc)
        sys.exit(rc)


if __name__ == "__main__":
    try:
        main()
    except gate_graph.GraphError as e:
        # H1: an unreadable graph is a refusal, never an empty (green) selection
        sys.stderr.write(f"gate-scope: REFUSED — {e}. Install binutils (nm) or fix the build dir; "
                         f"a selection from an unreadable graph would under-select silently.\n")
        sys.exit(4)
