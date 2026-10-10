#!/usr/bin/env python3
"""gate-scope — the SCOPED tier: what a change CAN REACH, and why (MODULAR-GATE-1).

    scripts/gate-scope.sh <base>..<tip> [--build build-linux] [--run [--no-targets | --targets-only]] [--json] [-j N]
    scripts/gate-scope.sh <base>..<tip> --resume [...]          # --run, only the rows with no record at the tip
    scripts/gate-scope.sh --files path [path ...]
    scripts/gate-scope.sh --solo <suite> [...] [--times 3]     # the flake protocol, logged
    scripts/gate-scope.sh --attribute <row>[,<row>...] --batch <tag> --candidate <rc tree>:<tip> \
        --control <rc-base>:<d-build tip> --display :NN --lanes <lane>:<worktree>:<tip> [...]   # a batch red
    scripts/gate-scope.sh --record-times | --price-check [--build <dir>]   # the prices: re-priced / checked
    scripts/gate-scope.sh --merge-tier [-j N] | --merge-tier-serial | --stage-close-tier | --gate-jobs

A lane runs everything its change can reach and nothing it cannot — read from the build and
the diff, never guessed — and the full tiers stay where the process needs them (a stage
close, a fork pin bump, stage-close, the phase's push: PHOTON_ATOM_CONTRACT §7b). The tool turns a
git range into an exact `ctest -R '^(a|b|c)$'` selection with one rationale line per touched
path and one reason per selected row, estimates the wall time from THE RUN LOG (T8), and
`--run` writes every row's verdict into the run log (scripts/gate_runlog.py) as the row ends.

A RUN IS A GATE, AND THE BOX RUNS ONE AT A TIME (GATE-COST-1, SPECS/audits/GATE_COST_2026-10-09.md):
`--run` (scoped, a fallback, `--fork-tier`, `--targets-only`) queues for THE GATE SLOT
(scripts/vram_tokens.py; FIFO, no bound, its position printed) and holds it to its last process;
`--solo` and `--attribute` take it too — a whole-card hold always does (GATE-COST-2) — and a per-row
admission never does. Before anything, the no-op build (gate_runlog.prebuild: HEAD's binaries, BUILT_FROM
fresh). Inside it: the `hygiene` rows first, as their own CPU phase (P8); the GPU
rows; a row that got no admission re-queued at the end (P5); the timing rows serial on ONE whole-card
hold (P2); the verdict; then the target rows on the same display and card (P9). `--resume` runs only
the rows with no record at the tip (P6); a gate whose display dies stops and says so (P6).

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
only for a path with no rule, no symbol and no graph owner. A fork pin bump selects by the FORK
DIFF'S reach (FORK_FAMILIES); its one full tier runs at the merge into d-build (`--fork-tier`,
which ci_gate_check requires). THE RANGE IS THE LANE'S OWN: across a forward merge the scope
starts at the merge's d-build parent (own_base), and what came in is the batch gate's.

ONE GATE PER BATCH (BATCH-GATE-1; docs/TESTING_GATE.md §3c): builders run their named acceptance tests
+ their subject suite; the lead stacks the ready lanes on d-build as ONE candidate
(`scripts/lead/merge-dbuild-lane.sh batch`) and runs ONE scoped gate on `d-build..candidate` — the
candidate's merges have lane tips as second parents, so the union range scopes as a plain range. A
red row is attributed by `--attribute`: 3 solo runs per lane on that lane's OWN tip and 3 at the
candidate (the control); a row red at the candidate and on no lane's tip is a COMBINATION DEFECT. The `--joint` union this replaced is gone.
Tiers are contracts: the selection printed here is what runs, nothing
hand-picked out of it; a red a later tier finds that this selection missed is a defect of
this tool, fixed here and added to tests/hygiene/gate_selection_cases.json.
"""
import argparse, json, os, re, subprocess, sys, collections, time

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

# THE FORK'S FAMILIES (TEST-SELECTOR-1 T2; the merge read's D2): a fork pin bump inside a lane
# selects by what the fork's own diff reaches — each changed fork file, first match:
#   none    — what the engine never builds or stages: the other render systems, the upstream
#             samples and their media, the docs (irisgl/engine/CMakeLists.txt stages exactly
#             Hlms/{Common,Pbs,Unlit}, 2.0/scripts/materials/{Common,HDR,Tutorial_SSAO,
#             Tutorial_SMAA}, VCT, Compute/Tools, Compute/Algorithms/{IBL,IrradianceFields} and
#             packs/DebugPack.zip; nothing else of Samples/ reaches a process of ours), and the
#             HLSL/Metal twins of a shader (Vulkan reads the .glsl / .any);
#   engine  — EVERYTHING ELSE the engine builds or stages: the pixel family (ENGINE_FAMILY + atom)
#             and every executable that extracts the engine — the pin's selection before this lane.
#             The narrower arms the first version had are gone (the read's D2, the brief's error):
#             the Vulkan render system runs every rendering executable; the irradiance field (and
#             the VCT feeding it) shades every lit pixel at every tier (Types.h fieldDefault);
#             Compute/Tools is the voxelizer's clear.
# The only narrowing left is `none`. A fork file no family names is the MERGE tier.
FORK_FAMILIES = [
    (r"^(Docs/|Scripts/|\.github/|\.circleci/|.*\.md$|LICENSE|README|Other/|Samples/2\.0/|"
     r"RenderSystems/(Direct3D11|GL3Plus|GLES2|Metal)/|.*\.(hlsl|metal)$)", "none"),
    (r"^Samples/Media/(VCT/|Compute/Tools/|Compute/Algorithms/(IBL|IrradianceFields)/|Hlms/(Common|Pbs|Unlit)/|"
     r"packs/DebugPack\.zip$|2\.0/scripts/materials/(Common|HDR|Tutorial_SSAO|Tutorial_SMAA)/)", "engine"),
    (r"^Samples/Media/", "none"),
    (r"^(OgreMain|Components|PlugIns|RenderSystems/(Vulkan|NULL)|CMake|CMakeLists\.txt|Dependencies|DependenciesD)",
     "engine"),
]

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
    (r"^irisgl/(engine/|thirdparty/ogre-next|scripts/(build|prune)-ogre)", ENGINE_FAMILY, []),
    # HOW OGRE IS FOUND AND LINKED (TEST-SELECTOR-1; the audit's T3 list: 14 lane gates fell back
    # on this one file): every executable that links the engine, and the pixel family — the
    # fork family `ogre`, never the whole tier.
    (r"^cmake/IncludeOgre\.cmake$", ENGINE_FAMILY + ["atom"], []),
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
    # its only guards (the two cluster sweeps are `stage-close` rows: they ride a bake change through
    # STAGE_CLOSE_SUBJECTS below, never through this rule's directories — docs/TESTING_GATE.md §1d).
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
    # The video textures' player also runs on the offline recorder's stepped clock (VIDEO-REC-2).
    (r"^src/services/livevideo", ["services", "*headless-scripts", "capture"], ["video", "capture"]),
    (r"^src/services/", ["services", "*headless-scripts"], []),
    (r"^src/(data|io|commands)/", ["document", "commands", "reopen", "export", "samples", "assetpaths",
                                   "assetmigrate", "services", "*headless-scripts"], ["project", "scene", "node"]),
    (r"^src/viewport/", ["app", "input", "gizmo", "picking", "cameras", "sockets", "ui",
                         # the render driver, the frame monitor host, the screenshot grades and the
                         # VR pacing live here (GATE-SCOPE-2, 2026-09-17)
                         "perf", "hdr", "vr", "player", "thumbnails",
                         # the recorder's view and its hooks in the frame loop (VIDEO-REC-2)
                         "capture"],
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
    # THE VIDEO RECORDER (VIDEO-REC-1): its own pool, and the shell's button rows.
    (r"^src/modules/capture/", ["capture", "ui"], ["capture"]),
    (r"^src/(modules/publish|export)/", ["export", "ui"], ["project", "publish"]),
    # …and here because source.panel_rows_guarded and source.db_pointers_initialised
    # read src/ui and src/shell (the panels' rows and their database pointers).
    (r"^src/(ui|shell)/",
     ["ui", "app", "theme", "shortcuts", "desktops", "drawers", "hygiene"],
     ["editor", "app", "desktop"]),
    # THE STARTUP SHADER GATE also warms the video recorder (VIDEO-REC-2: its view and the
    # encoder probe), so the capture pool's first-recording arm rides with the app dirs.
    # (First match wins: this must sit BEFORE the generic ^src/app/ rule.)
    (r"^src/app/shaderbuildgate", ["app", "apppaths", "cli", "log", "shutdown", "hygiene", "threading",
                                   "api", "capture"], ["app", "capture"]),
    (r"^src/app/", ["app", "apppaths", "cli", "log", "shutdown", "hygiene", "threading", "api"], ["app"]),
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
    # EVERY OTHER SCRIPT IS THE RUNNER'S TOO (TESTING-CLEANUP-2B item 4, H8a): ci_gate_check.py / ci-gate-check.sh (the
    # judge: gate.ci_check, gate.verdict_door, …), kernel_xid.py (devprocess.kernel_journal, gate.cost),
    # check-trailers.sh (gate.batch), sanitize.sh and its lsan/tsan suppressions (the source lints) selected NO row —
    # a change to the judge gated on nothing. Their tests are the hygiene and tooling rows (display-free, the CPU
    # phase). In a tooling-only diff (§1.3.4) the same paths take Selection.tooling_path.
    (r"^scripts/", ["hygiene", "tooling"], []),
]

# A TOOLING-ONLY DIFF SELECTS NO PRODUCT ROW (TESTING_V3_SPEC §1.3.4; the owner 2026-10-10: "why do we still have
# 1 hour gates?" — batch A's three runner lanes selected 636 rows, 57 min). A change that touches NOTHING the
# product binary is built from cannot change the product rows' answers: running them re-proves the runner, not the
# product, and the runner is proved by its own rows. PRODUCT_INPUT is that set, a FILE RULE (never a hand-pick):
# the sources (src/, irisgl/ but its docs), the shipped content (app/, scenes/), the vendored code (thirdparty/),
# the build files (the top CMakeLists.txt, cmake/, .gitmodules), the installer and the extras. A diff with ONE such
# path is a product diff and takes the whole union below, exactly as before. A diff with none is TOOLING-ONLY:
#   * a path of the runner itself (TOOLING_OWN: scripts/, tests/hygiene/, tests/tooling/, testing/) selects the
#     `hygiene` and `tooling` rows (the lints and the runner's own suites, display-free) plus what its area rule
#     names among the non-app, non-compiled rows (a script's own self-test, e.g. pool.runner for vram_tokens) and
#     every row whose COMMAND runs the file;
#   * any other path — a test's source, a test CMakeLists, a shared test helper — goes through the ordinary walk:
#     what it reaches IS the rows whose declaration or command changed (the graph for a test source, the CMake
#     reader for a registration);
#   * docs and the like select what they always did (nothing; TESTING_GATE.md its guard);
#   * the smoke pair (ALWAYS_ON_CODE) is the PRODUCT's smoke: no product path moved, so it does not ride.
PRODUCT_INPUT = re.compile(r"^(src/|irisgl/(?!docs/|[^/]+\.md$)|app/|scenes/|thirdparty/|cmake/|extras/|deploy/|"
                           r"CMakeLists\.txt$|\.gitmodules$)")
TOOLING_OWN = re.compile(r"^(scripts/|tests/(hygiene|tooling)/|testing/)")
# A RUNNER'S OWN SELF-TEST THAT BOOTS THE APP (it is the tool's test, not a product row): selected by name in a
# tooling-only diff, where the area rules' directories pick only lint/script rows. pool.runner drives the pool
# runner (tests/support/run_pool.py), which takes its tokens through vram_tokens.py.
TOOL_SELF_TESTS = [
    (r"^scripts/(gpu-admit|vram_tokens)", ["pool.runner"]),
]


def product_paths(paths):
    """The touched paths the product is built from (§1.3.4): one of them makes the diff a product diff."""
    return [p for p in paths if PRODUCT_INPUT.match(p)]


# Cheap smoke suites always added when src/ or irisgl/ moved (a boot that renders + the
# contract of the scripting surface), ~15 s together.
ALWAYS_ON_CODE = ["app.startup_quiet", "api.contract"]
# THE STAGE-CLOSE ROWS (STAGE-CLOSE-1; D6B-GATE-SHAPE before it, under a daily-run name for a tier
# that never once ran — the owner, 2026-10-09: there is no such build). Two labels, read here alone:
#   `stage-close` — the row leaves the MERGE and PUSH tiers and runs in THE STAGE-CLOSE BATCH
#                   (`--stage-close-tier`): one batch in the gate slot, started BY THE LEAD at every
#                   stage close and before every push (rc-gate.sh JAH_GATE_TIER=stage-close), never
#                   by a timer. Minutes of one process whose push-time guard lives elsewhere, a churn
#                   twin (ten processes of a one-process gate row), or a millisecond bar.
#   `quiet-box`   — beside `stage-close` on the rows that MEASURE (the wall-clock benchmarks, the
#                   `<suite>.timing` millisecond rows, a GPU clock): never in a scoped gate (it
#                   shares the box by construction), and in the batch at -j1 on the whole card.
# A `stage-close` row WITHOUT `quiet-box` rides a scoped gate ONLY when the diff touches ITS OWN
# SUBJECT: a rule of STAGE_CLOSE_SUBJECTS names it, or the change is in its own test directory
# (Selection.stage_close_subjects). A broad rule (the engine family, every app row, a header's
# readers) that reaches it leaves it for the batch — a catch it would have made waits for the stage
# close (the owner accepted that, 2026-10-09).
# Anchored in the -LE: `shadercache` alone would drop the product-contract cache suites (code
# review 2026-09-10). `--timeout 120` is ctest's DEFAULT for the rows that set no TIMEOUT — a
# hang costs 2 min, not 25.
STAGE_CLOSE_LABELS = {"stage-close", "shadercache-attack"}
# Never selected by a scoped gate (the shader-cache attack is minutes under ASan). A row selected
# WITHOUT these labels is a gating row of that gate (ci_gate_check reads this set).
SCOPE_EXCLUDED_LABELS = {"quiet-box", "shadercache-attack"}
# Selected by a scoped gate only through its own subject (STAGE_CLOSE_SUBJECTS, its own test dir);
# once selected it gates like any row. NOT in SCOPE_EXCLUDED_LABELS on purpose: that set means
# "never gating", and a subject-selected stage-close row is that change's guard.
SUBJECT_ONLY_LABELS = {"stage-close"}
QUIET_BOX_LABEL = "quiet-box"

# THE STAGE-CLOSE SUBJECTS (STAGE-CLOSE-1 U2; the audit's condition S2): path regex -> the
# `stage-close` rows a change to that path reaches DIRECTLY. Matched against every touched path,
# and — for a fork pin bump — against every fork file as `irisgl/thirdparty/ogre-next/<file>`. A
# quiet-box row is never named here. `gate-scope.py --stage-close-rules` prints the table.
_FORK = r"irisgl/thirdparty/ogre-next/"
STAGE_CLOSE_SUBJECTS = [
    # THE BAKE'S SWEEPS: the cluster-DAG bake (irisgl/import: meshbake.cpp, clusterlod.cpp) and the
    # library it calls (meshoptimizer, its clusterlod patch stack).
    (r"^irisgl/(import/|thirdparty/meshoptimizer)", ["atom.cluster_cut", "atom.cluster_crack"]),
    # THE CONVERGE SWEEP and THE BOOT DETERMINISM: Photon (the voxel chain, the irradiance field, the
    # GI driver, the surface cache it reads) — mirror_room_boots' one real catch (PHOTON-I-1, ledger
    # §1623) was the GI rest rule — and the fork's VCT / irradiance-field code and media.
    (r"^(irisgl/engine/(src/(photon/|OgreGi\.|OgrePhotonView\.|(Ogre)?GpuVoxelGather\.|OgreVoxelReaderParity\.|"
     r"(Ogre)?SurfaceCache\.)|media/Photon/)|" + _FORK +
     r"(Components/Hlms/Pbs/(src|include)/Vct/|Samples/Media/(VCT|Compute/Algorithms/IrradianceFields)/))",
     ["gi.chain_converge_scenes", "samples.mirror_room_boots"]),
    # THE MIRROR ROOM'S OWN PICTURE: the sample, the planar reflections, the screen-probe gather.
    (r"^(scenes/Mirror Room\.zip$|irisgl/engine/src/(OgrePlanar|OgreScreenProbeGather|ScreenProbeGather)\.)",
     ["samples.mirror_room_boots"]),
    # THE CASTER SOAKS: the shadow and caster code (ours and the fork's Pbs caster) — the soak and
    # the ten-process churn twins of the shadow casters' gate rows.
    (r"^(irisgl/engine/(src/(OgreShadow|OgreAtomCasterPass)\.|media/Hlms/.*([Ss]hadow|[Cc]aster))|" + _FORK +
     r"(Components/Hlms/Pbs/src/OgreHlmsPbs\.cpp$|Samples/Media/Hlms/(Common|Pbs)/.*([Ss]hadow|[Cc]aster)))",
     ["gpu.cutout_soak", "shadow.cutout_caster.churn", "shadow.two_sided_caster.churn"]),
]

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
# gating suites (their exit code is the gate's) and prints the verdict; the target
# suites are THEIR OWN STEP after it (GATE-SPEED-1; GATE-COST-1 P9: after the verdict line,
# inside the gate, run-log tier `target`), whose result is reported and discarded. The MERGE
# and PUSH tiers drop them with -LE, which is the only shape ctest offers for
# "do not let these decide the tier"; their values are read from a lane's scoped
# run, where they are printed.
# `scale-target` (lane D1-SCALE-FIXTURES): phase E's measuring stick — one `scale.*`
# suite per wall (tests/scale), each printing today's number as a `target:` line with
# no bar yet. Split out exactly like a photon target: reported, never gating, until the
# part that closes a wall writes its bar and removes the label from that row.
TARGET_LABELS = {"photon-target", "scale-target"}

STAGE_CLOSE_LABEL_RE = "|".join(sorted(re.escape(l) for l in STAGE_CLOSE_LABELS | TARGET_LABELS))
# THE GATE'S PARALLEL WIDTH — THE ONE CONSTANT (GATE-SPEED-1 item 9). Every gate's parallel phase
# (the scoped selection, the MERGE tier, a fallback, a batch candidate's), rc-gate.sh's ctest width
# (`gate-scope.py --gate-jobs`) and the merge refusal's re-selection read THIS; the docs name it
# instead of quoting a number. Changing the box's width is this one line — and an owner decision
# (PHOTON_ATOM_CONTRACT §7b rule 3; the gate-speed audit's replay: -j6 21.7 min against -j4's 31.0
# on rc-smoke15a's durations, CPU contention unmeasured). `-j N` still overrides it per run.
GATE_JOBS = 4

# The MERGE tier at a given ctest parallelism (GATE_JOBS by default, docs/TESTING_GATE.md §1); the
# fallback must honour a caller's `-j` as the scoped command does (DEVPROCESS-1 item 2: the
# fallback used to print and RUN a hardcoded -j4 whatever the caller asked).
# THE TIMING ROWS ARE THEIR OWN SERIAL PHASE (TEST-SELECTOR-1, the merge read's W1 — the lead's
# decision): a row that measures takes EVERY VRAM token, and inside a -j4 phase it waited holding
# the admission's turnstile while its siblings drained (20 timing rows waited 1,104 s in one 69-min
# gate), stalling every admission on the box. So every tier and every scoped gate runs in two
# phases: the parallel phase WITHOUT the `timing` rows (label `timing`, appended to every
# jah_gpu_exclusive_test row), then the timing rows at -j1, the whole GPU theirs.
TIMING_LABEL = "timing"


def merge_tier(jobs=GATE_JOBS):
    """The MERGE tier's PARALLEL phase (its second phase is merge_tier_serial())."""
    return (f'ctest -j{jobs} --timeout 120 --output-on-failure '
            f'-LE "^({STAGE_CLOSE_LABEL_RE}|{TIMING_LABEL})$"')


def merge_tier_serial():
    """The MERGE tier's SERIAL phase: its timing rows, one at a time, after the parallel phase."""
    return (f'ctest -j1 --timeout 120 --output-on-failure -L "^{TIMING_LABEL}$" '
            f'-LE "^({STAGE_CLOSE_LABEL_RE})$"')


# THE STAGE-CLOSE BATCH (STAGE-CLOSE-1 U4; docs/TESTING_GATE.md §1d): every `stage-close` row (and
# the shader-cache attack), in TWO phases like the MERGE tier — the minutes-of-one-process rows and
# the churn twins at the gate's width, then every `quiet-box` or `timing` row at -j1 on ONE hold of
# the whole card. ctest ANDs repeated -L filters. Started by the lead (rc-gate.sh, or
# `gate-scope.sh --stage-close-tier --run`), never by a timer.
_STAGE_CLOSE_RX = "|".join(sorted(re.escape(l) for l in STAGE_CLOSE_LABELS))
_QUIET_RX = "|".join(sorted(re.escape(l) for l in (QUIET_BOX_LABEL, TIMING_LABEL)))


def stage_close_tier(jobs=GATE_JOBS):
    """The stage-close batch's PARALLEL phase (its second phase is stage_close_tier_serial())."""
    return f'ctest -j{jobs} --output-on-failure -L "^({_STAGE_CLOSE_RX})$" -LE "^({_QUIET_RX})$"'


def stage_close_tier_serial():
    """The stage-close batch's QUIET-BOX phase: the measuring rows, one at a time, on the whole card."""
    return f'ctest -j1 --output-on-failure -L "^({_STAGE_CLOSE_RX})$" -L "^({_QUIET_RX})$"'


def sh(cmd, cwd=None):
    cwd = cwd or ROOT
    # A failing git/ctest call must not read as "nothing touched" / "no suites": that was
    # a silently green empty gate (platform audit H4.2, 2026-09-10).
    r = subprocess.run(cmd, cwd=cwd, shell=True, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(f"gate-scope: command failed ({r.returncode}): {cmd}\n{r.stderr}")
        sys.exit(2)
    return r.stdout


def _git_try(args, cwd=None):
    """git's stdout (stripped), or None when the command fails (an unknown sha is an answer here)."""
    cwd = cwd or ROOT
    try:
        r = subprocess.run(["git"] + args, cwd=cwd, capture_output=True, text=True)
    except OSError:
        return None
    return r.stdout.strip() if r.returncode == 0 else None


_OWN_BASE_CACHE = {}
_LINE_CACHE = {}
INTEGRATION_REFS = ("d-build", "origin/d-build", "main/d-build", "o3de", "origin/o3de", "main/o3de", "ogre",
                    "origin/ogre", "main/ogre")


def integration_line(cwd=None):
    """Every commit on the first-parent line of the repo's integration branches (JAH_INTEGRATION_REFS,
    space-separated, overrides INTEGRATION_REFS): the commits a forward merge can bring in."""
    cwd = cwd or ROOT
    if cwd in _LINE_CACHE: return _LINE_CACHE[cwd]
    refs = os.environ.get("JAH_INTEGRATION_REFS", "").split() or INTEGRATION_REFS
    line = set()
    for r in refs:
        if _git_try(["rev-parse", "--verify", "-q", r + "^{commit}"], cwd):
            line |= set((_git_try(["rev-list", "--first-parent", r], cwd) or "").split())
    _LINE_CACHE[cwd] = line
    return line


def own_base(base, tip, cwd=None):
    """THE LANE'S OWN DIFF (TEST-SELECTOR-1 T1; audit ARCH_AUDIT_2026-09-30 testing-tooling T1).

    A two-dot `base..tip` across a FORWARD MERGE (d-build merged into the lane) is the lane's
    change PLUS every sibling change the merge carried in: d7-studio-fixes-1, a Studio-only lane,
    ran three full tiers on a fork pin its own ten commits never touched. The lane's own change is
    the diff from the d-build commit it last merged to its tip, so:
      * a one-commit range (`merge^1..merge`, a lane's merge INTO d-build) is itself;
      * else the base is the merge-base of base and tip (a base AHEAD of the lane — d-build moved
        on — is not the lane's change either), moved up to the second parent of the NEWEST FORWARD
        merge on the tip's first-parent line.
    A merge is FORWARD only when its second parent lies ON THE INTEGRATION LINE — the first-parent
    history of d-build (or o3de / ogre, local or remote: integration_line()). Any other merge — an
    internal fix branch merged into the lane — is the lane's own change (the merge read's
    scratch-repo replay: counting it as forward dropped laneA..laneC from the diff; over-selection
    is the safe side). Returns (own base sha, [(merge sha, its second parent)] of the forward merges
    seen, newest first) — or (base, []) when git cannot resolve the range (a replay's fake revisions)."""
    cwd = cwd or ROOT
    key = (base, tip, cwd)
    if key in _OWN_BASE_CACHE: return _OWN_BASE_CACHE[key]
    b, t = _git_try(["rev-parse", "--verify", "-q", base + "^{commit}"], cwd), \
        _git_try(["rev-parse", "--verify", "-q", tip + "^{commit}"], cwd)
    if not b or not t:
        _OWN_BASE_CACHE[key] = (base, []); return _OWN_BASE_CACHE[key]
    if _git_try(["rev-parse", "--verify", "-q", t + "^1"], cwd) == b:
        _OWN_BASE_CACHE[key] = (b, []); return _OWN_BASE_CACHE[key]
    mb = _git_try(["merge-base", b, t], cwd) or b
    own, fwd = mb, []
    for line in (_git_try(["rev-list", "--first-parent", "--merges", "--parents", f"{mb}..{t}"], cwd) or "").splitlines():
        shas = line.split()
        if len(shas) < 3: continue
        m, p2 = shas[0], shas[2]
        if p2 in integration_line(cwd):
            fwd.append((m, p2))
    if fwd:
        own = fwd[0][1]
    _OWN_BASE_CACHE[key] = (own, fwd)
    return own, fwd


def scope_range(rng):
    """(own range `<own base>..<tip>`, the range that came in through forward merges or None,
    the forward merges) — see own_base. A range git cannot resolve is returned unchanged."""
    if not rng or ".." not in rng:
        return rng, None, []
    base, tip = rng.split("..", 1)
    own, fwd = own_base(base, tip)
    b = _git_try(["rev-parse", "--verify", "-q", base + "^{commit}"])
    mb = (_git_try(["merge-base", b, tip]) if b else None) or b
    incoming = f"{mb}..{own}" if fwd and mb and own != mb else None
    return f"{own}..{tip}", incoming, fwd


def irisgl_pair(base, tip):
    """The irisgl revisions a Studio range compares: the pins at both ends, the old end moved to
    the lane's OWN base inside irisgl (a forward merge of d-build's irisgl is not the lane's
    change, and a base pin AHEAD of the lane's is not either — own_base, run in the submodule)."""
    old = sh(f"git rev-parse {base}:irisgl").strip()
    new = sh(f"git rev-parse {tip}:irisgl").strip()
    ig = os.path.join(ROOT, "irisgl")
    if old != new:
        o, _ = own_base(old, new, cwd=ig)
        if _git_try(["rev-parse", "--verify", "-q", o + "^{commit}"], ig): old = o
    return old, new


def touched_paths(rng):
    """Files changed in the Studio range, plus the irisgl submodule's own diff (prefixed) — the
    LANE'S OWN change (scope_range: what forward merges carried in is the batch gate's business)."""
    rng = scope_range(rng)[0]
    files = [l for l in sh(f"git diff --name-only {rng}").splitlines() if l]
    if not files:
        sys.stderr.write(f"gate-scope: range {rng} touches no file — refusing to scope an empty change\n")
        sys.exit(2)
    out = [f for f in files if f != "irisgl"]
    if "irisgl" in files:
        base, tip = rng.split("..", 1)
        old, new = irisgl_pair(base, tip)
        sub = sh(f"git diff --name-only {old} {new}", cwd=os.path.join(ROOT, "irisgl")).splitlines() if old != new else []
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
        if os.path.isfile(os.path.join(c, "CTestTestfile.cmake")):
            c = _cache_spelling(c, "CMAKE_CACHEFILE_DIR") or c
            bind_root(c)
            return c
    return cands[-1]          # nothing configured: keep the documented resolution for the error


def _cache_value(build, key):
    """A CMakeCache.txt entry of `build`, exactly as spelled there, or None."""
    try:
        for line in open(os.path.join(build, "CMakeCache.txt"), errors="replace"):
            if line.startswith(key + ":"):
                return line.split("=", 1)[1].strip()
    except OSError:
        pass
    return None


def _cache_spelling(build, key="CMAKE_CACHEFILE_DIR"):
    """THE BUILD'S OWN SPELLING OF A DIRECTORY (SYMLINK-ROOT-1). A build graph names files under the spelling
    the tree was CONFIGURED with — never resolved through symlinks — so a directory reached by another
    spelling of the same inode (the box's /home/jahshaka/Developer -> /mnt/work/Developer; python's getcwd()
    and realpath() return the resolved one) is respelled the configured way. None when the cache does not
    name it or names something that is not the same directory."""
    d = _cache_value(build, key)
    if not d or not os.path.isdir(d):
        return None
    if key == "CMAKE_CACHEFILE_DIR" and os.path.realpath(d) != os.path.realpath(build):
        return None
    return os.path.normpath(d)


def build_source_dir(build):
    """The source tree a build dir was configured from (CMakeCache's CMAKE_HOME_DIRECTORY), AS SPELLED THERE —
    never realpath'd: the build graph's nodes carry that spelling, and a resolved one misses every file."""
    return _cache_spelling(build, "CMAKE_HOME_DIRECTORY")


def bind_root(build):
    """THE TREE A SELECTION READS IS THE BUILD'S (TEST-1 fix round, CLIP-REF-1's finding). The build's
    dependency graph names files under the tree it was configured FROM; read from another checkout
    (d-build's merge judge, ci_gate_check, judging a lane's build) every touched path was "not in the
    build graph" and selection fell back to path rules alone — 448 rows where the lane's own gate
    selected 372, for one range and one build. So the root every path, git command and source read
    resolves against is the build's source dir (the scripts' own checkout only when that cannot be
    read). The RULES stay the running script's — which is what a judge is for — and the facts are
    the build's tree's: one range and one build select the same rows from any checkout."""
    global ROOT, TIMES_FILE
    # Adopted whenever it differs TEXTUALLY (SYMLINK-ROOT-1): the same tree reached through a symlink is
    # still a different spelling, and the graph only answers to the build's own one.
    src = build_source_dir(build)
    if not src or src == ROOT:
        return
    ROOT = src
    TIMES_FILE = os.path.join(ROOT, "scripts", "gate-times.txt")
    gate_runlog.ROOT = src


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
        frames, via, sites = [], set(), set()
        while True:
            if n.get("file") is not None:
                frames.append(files[n["file"]])
                # THE REGISTRATION SITES (T3): every file:line of the backtrace — the add_test, the
                # helper call that ran it (jah_gpu_row, jah_scale_row, a foreach's body line) —
                # so a changed CMake command resolves to the rows registered FROM INSIDE ITS SPAN,
                # whatever variables and helpers name them
                if n.get("line") is not None:
                    sites.add((os.path.normpath(files[n["file"]]), int(n["line"])))
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
            while run and run[0] in ("--run-timeout", "--label"):
                run = run[2:]
        if run and os.path.basename(run[0]) == "gpu-admit.sh":
            run = run[run.index("--") + 1:] if "--" in run else run[2:]
        script = None
        m = re.search(r"--script\s+(\S+)", " ".join(run))
        if m: script = m.group(1)
        files_in_argv = [os.path.normpath(c) for c in run[1:]
                         if c.endswith((".js", ".sh", ".py", ".js.in", ".cmake")) and os.path.isfile(c)]
        if not script:
            # a wrapper's own .js.in template counts too (`bash run.sh <app> <x.js.in> ...`): its
            # verbs are what the row tests, and a shell wrapper's text is not JS (LIVE-PERSIST-1)
            script = next((f for f in files_in_argv if f.endswith((".js", ".js.in"))), None)
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
            "sites": sites,
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


# THE PRICES (TESTING-CLEANUP-2B item 6). scripts/gate-times.txt prices EVERY row of the MERGE and STAGE-CLOSE
# tiers (a gate's estimate, the stage-close batch's plan, ctest's COST order): one line per row, `<row> <seconds>
# <source>`, the source NAMED — `quiet n=<k>` (the median of the row's PASS runs with no sibling ctest, >= 3 of
# them: THE price), `all n=<k>` (fewer quiet runs: the median of every PASS run), `fixture` (a home wipe), or the
# text a hand-priced line carried. The run log read is testing/runs AND testing/runs-archive, from PRICE_SINCE:
# before GATE-SPEED-1's L2 (2026-10-01 13:10) a record's seconds still held its admission wait. `--price-check`
# is the self-check: it lists every tier row without a price and fails while one is (source.testing_rules R2).
PRICE_SINCE = "2026-10-01T13:10"


def parse_times(path=None):
    """row -> (seconds, source text) from gate-times.txt (a data line: `<row> <seconds> [<source>…]`)."""
    out = {}
    try:
        for line in open(path or TIMES_FILE):
            if line.startswith("#"): continue
            parts = line.split(None, 2)
            if len(parts) >= 2:
                try: out[parts[0]] = (float(parts[1]), parts[2].strip() if len(parts) > 2 else "")
                except ValueError: pass
    except OSError:
        pass
    return out


def run_log_prices(since=PRICE_SINCE):
    """row -> (median PASS seconds, 'quiet n=<k>' | 'all n=<k>') over the run log and its archive since `since`.
    The quiet median when >= gate_runlog.QUIET_MIN quiet runs exist (box.other_ctests == 0), else every run's."""
    import glob, statistics
    d = gate_runlog.log_dir()
    files = glob.glob(os.path.join(d, "*.jsonl")) + glob.glob(os.path.join(os.path.dirname(d), "runs-archive", "**",
                                                                           "*.jsonl"), recursive=True)
    acc = collections.defaultdict(list)
    for f in files:
        for line in open(f, errors="replace"):
            try: r = json.loads(line)
            except ValueError: continue
            if r.get("verdict") != "PASS" or r.get("seconds") is None or r.get("kind") or (r.get("ts") or "") < since:
                continue
            k = r["suite"] if not r.get("arm") else f"{r['suite']}::{r['arm'].split('.', 1)[-1]}"
            acc[k].append((r["seconds"], (r.get("box") or {}).get("other_ctests") == 0))
    out = {}
    for k, v in acc.items():
        q = [x for x, quiet in v if quiet]
        if len(q) >= gate_runlog.QUIET_MIN:
            out[k] = (statistics.median(q), f"quiet n={len(q)}")
        else:
            out[k] = (statistics.median([x for x, _ in v]), f"all n={len(v)}")
    return out


def tier_rows(inv):
    """The rows the MERGE and STAGE-CLOSE tiers run: every registered row but the target tests."""
    return sorted(n for n, t in inv.items() if not (t["labels"] & TARGET_LABELS))


def unpriced(inv, prices=None):
    prices = parse_times() if prices is None else prices
    return [n for n in tier_rows(inv) if n not in prices]


def record_times(build):
    """Re-price scripts/gate-times.txt (item 6's rule above) for every tier row of `build`'s registration: the run
    log's price where the row has PASS runs, else the line it had, else a fixture's wipe; a row with none of these
    is left unpriced — `--price-check` names it."""
    inv = load_inventory(build)
    log = run_log_prices()
    old = parse_times()
    rows = tier_rows(inv)
    out, kinds = {}, collections.Counter()
    for n in rows:
        if n in log:
            out[n] = log[n]; kinds[log[n][1].split()[0]] += 1
        elif n in old:
            out[n] = old[n]; kinds["kept"] += 1
        elif inv[n].get("fixture_setup"):
            out[n] = (0.01, "fixture (a home wipe; no PASS record)"); kinds["fixture"] += 1
    # a pool's ARMS keep their own prices (`<pool>::<arm>`: a partial pool's estimate sums them)
    for k, v in list(log.items()) + [kv for kv in old.items() if kv[0] not in log]:
        r_, _, arm = k.partition("::")
        if arm and r_ in inv and arm in inv[r_]["arms"]:
            out[k] = v; kinds["arm"] += 1
    head = [l for l in open(TIMES_FILE) if l.startswith("#")] if os.path.exists(TIMES_FILE) else []
    with open(TIMES_FILE, "w") as f:
        f.writelines(head)
        for n in sorted(out):
            sec, src = out[n]
            f.write(f"{n} {sec:.2f}  {src}\n" if src else f"{n} {sec:.2f}\n")
    gone = sorted(set(old) - set(out))
    n_rows = sum(1 for n in out if "::" not in n)
    print(f"priced {n_rows} of {len(rows)} tier rows (+{len(out) - n_rows} pool-arm prices) into "
          f"{os.path.relpath(TIMES_FILE, ROOT)}: "
          + ", ".join(f"{k} {v}" for k, v in kinds.most_common())
          + (f"; dropped {len(gone)} line(s) that are no row of this build: {' '.join(gone[:12])}" if gone else ""))
    left = unpriced(inv, out)
    if left:
        print(f"UNPRICED ({len(left)}): " + " ".join(left))
    return 1 if left else 0


COST_SOURCE = {}   # suite -> "quiet" | "all" (the run log's median, gate_runlog.median_times) | "file"


def load_costs():
    """Per-suite seconds: scripts/gate-times.txt overlaid with the run log's medians (fresher; the
    quiet-box median where >= 3 records ran with no sibling ctest). COST_SOURCE says which."""
    costs = {}
    for n, (sec, _src) in parse_times().items():
        costs[n] = sec; COST_SOURCE[n] = "file"
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
        rng = scope_range(rng)[0]            # the lane's OWN change (T1), as touched_paths reads it
        self.rng = rng
        self.base, self.tip = rng.split("..", 1) if rng and ".." in rng else (None, None)
        self._ig = None

    def pair(self, path):
        if not self.base: return None, None
        if path.startswith("irisgl/"):
            if self._ig is None:
                self._ig = irisgl_pair(self.base, self.tip)
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
        self.full_tier = []          # rules that select the whole tier (an unreadable fork diff, a root setting)
        self.fork_bump = None        # {old, new, files} when the range moves the fork pin (T2)
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
        self._visited = set()
        # THE STAGE-CLOSE ROWS' ORIGINS (STAGE-CLOSE-1 U2): the directly touched path each
        # `stage-close` row was reached from, read by stage_close_subjects() after the walk
        self._origin = None
        self.sc_origins = collections.defaultdict(set)
        self.stage_close_left = {}         # row -> the broad reason that reached it, left for the batch
        self.tooling_only = False          # §1.3.4: no touched path is a product input (select() decides)

    # -- adding rows -----------------------------------------------------------------------
    def add(self, suites, why):
        for s in suites:
            r0 = s.split("::", 1)[0]
            if r0 in self.inv and self.inv[r0]["labels"] & SUBJECT_ONLY_LABELS:
                self.sc_origins[r0].add(self._origin)
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

    def stage_close_subjects(self, paths):
        """U2 (STAGE-CLOSE-1): a `stage-close` row stays in a scoped selection only when the change
        touches ITS OWN SUBJECT — a touched path in its own test directory (tests/<its dir>/: its
        source, its script, its registration) or a STAGE_CLOSE_SUBJECTS rule naming it. Every other
        reach (an area rule's directories, the engine family, every app row, a header's readers, a
        tests/support helper) leaves it in stage_close_left, for the batch. Then the subject rules
        ADD the rows they name (a broad rule need not have reached them)."""
        for n in [n for n in self.selected if self.inv[n]["labels"] & SUBJECT_ONLY_LABELS]:
            d = "tests/" + self.inv[n]["dir"] + "/"
            if any(o and o.startswith(d) for o in self.sc_origins.get(n, ())):
                continue
            self.stage_close_left[n] = self.selected.pop(n)
            self.whole.discard(n)
            self.arms.pop(n, None)
        reached = list(paths)
        if self.fork_bump and self.fork_bump.get("files"):
            reached += [_FORK + f for f in self.fork_bump["files"]]
        for p in reached:
            for pat, rows in STAGE_CLOSE_SUBJECTS:
                if not re.match(pat, p): continue
                named = [r for r in rows if r in self.inv and not (self.inv[r]["labels"] & SCOPE_EXCLUDED_LABELS)]
                for r in named:
                    self.stage_close_left.pop(r, None)
                self.add(named, f"{p}: stage-close subject rule {pat[:60]}{'…' if len(pat) > 60 else ''}")

    # -- the path walk ---------------------------------------------------------------------
    def path(self, p, depth=0, via=None):
        """Select for one reached path. `via` names why a path is reached indirectly (a symbol,
        a CMake command); a directly touched path has via=None."""
        if depth == 0: self._origin = p
        key = (p, depth > 0)
        if key in self._visited: return
        self._visited.add(key)
        tag = p if via is None else f"{p} (via {via})"
        notes = []
        if p.startswith(("src/", "irisgl/", "tests/")): self.code_moved = True

        # THE FORK PIN (TEST-SELECTOR-1 T2): inside a lane a pin bump selects by what the FORK'S
        # DIFF reaches (FORK_FAMILIES); §7b rule 4's full tier runs ONCE per bump, at the merge
        # into d-build (`--fork-tier`, which ci_gate_check requires of a range that moves the pin),
        # not on every fix round (contact-occlusion-1: eight full tiers in 8.6 h, one per round).
        if p == "irisgl/thirdparty/ogre-next" or p.startswith("irisgl/thirdparty/ogre-next/"):
            self.fork(p, tag, depth)
            return
        m_patch = re.match(r"^(irisgl/thirdparty/([^/]+)-patches)/.+\.patch$", p)
        if m_patch:
            # A VENDORED COMPONENT'S PATCH STACK (assimp-patches/, meshoptimizer-clusterlod-patches/),
            # applied at configure: it reaches what the files it patches reach — the vendored file
            # itself, or its staged copy (<build>/…/vendor-patched/<component>/…), whichever the
            # build graph compiles
            a, b = self.revs.texts(p) if self.revs.base else (None, None)
            text = b or a or ""
            if not text:
                try: text = open(os.path.join(ROOT, p), errors="replace").read()
                except OSError: text = ""
            names = sorted({os.path.basename(x) for x in re.findall(r"^\+\+\+ b/(\S+)", text, re.M)})
            comp = m_patch.group(2)
            roots = [os.path.join(ROOT, "irisgl", "thirdparty", comp)]
            if self.graph is not None:
                roots += [os.path.join(dp, comp) for dp, dns, _ in os.walk(self.graph.build)
                          if os.path.basename(dp) == "vendor-patched"]
            hit = 0
            for r_ in roots:
                for dp, _, fs in os.walk(r_):
                    for f in fs:
                        if f in names:
                            rel = os.path.relpath(os.path.join(dp, f), ROOT)
                            g = self.graph_rows(rel, f"{tag} (patches {rel})")
                            if g is not None:
                                hit += 1
                                if g[1]: self.expand(["*app-rows"], [], f"{tag}: patches {f}, compiled into the app")
                                for inc in sorted({os.path.relpath(self.graph.obj_src[o], ROOT)
                                                   for o in self.graph.includers(os.path.join(ROOT, rel))
                                                   if o in self.graph.obj_src}):
                                    if inc.startswith(("src/", "irisgl/")) and not inc.startswith("irisgl/thirdparty/"):
                                        self.rules_for(inc, f"{tag} (read by {inc})", kinds={"app", "other"})
            self.expand(["hygiene"], [], f"{tag}: a vendored patch")
            if not hit:
                self.full_tier.append(f"{p}: a patch whose patched file ({names}) no build graph node owns")
            if depth == 0: self.rationale.append((p, f"a vendored patch of {names}: {hit} compiled copy(ies)"))
            return
        if re.match(r"^(irisgl/)?thirdparty/[^/]+/(PROVENANCE|README|LICENSE|NOTICE|COPYING)[^/]*$", p) or (
                (p.startswith(("irisgl/thirdparty/", "thirdparty/"))) and p.endswith(".md") and
                p.count("/") == (3 if p.startswith("irisgl/") else 2)):
            # a vendored component's own notice/provenance text: compiled into nothing; the notices lint reads it
            self.expand(["hygiene"], [], f"{tag}: a vendored notice")
            if depth == 0: self.rationale.append((p, "a vendored component's notice/provenance → the source lints"))
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
                if p.startswith("irisgl/cmake/WrapHlmsPiece") or p == "cmake/IncludeOgre.cmake":
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

    def tooling_path(self, p):
        """A path of the runner in a TOOLING-ONLY diff (§1.3.4; PRODUCT_INPUT above): the tools' own rows — the
        `hygiene` and `tooling` rows, what the path's area rule names among the non-app, non-compiled rows, and
        every row whose command runs the file. Never a product row."""
        self._origin = p
        why = f"{p}: a tooling-only diff (TESTING_V3 §1.3.4) — the runner's own rows"
        before = set(self.selected)
        self.expand(["hygiene", "tooling"], [], why)
        r = self.rules_for(p, why, kinds={"other"})
        for pat, rows in TOOL_SELF_TESTS:
            if re.match(pat, p): self.add(rows, why + " [its self-test]")
        hit = self.argv_rows(p, why)
        n = len(set(self.selected) - before)
        self.rationale.append((p, f"TOOLING-ONLY (no product input in the diff): the hygiene + tooling rows"
                                  + (f", {r} (lint/script rows)" if r else "")
                                  + (f", {len(hit)} row(s) that run it" if hit else "") + f" — {n} new row(s)"))

    def fork_pins(self):
        """(old, new) ogre-next commits the range's irisgl ends pin, or (None, None)."""
        a, b = self.revs.pair("irisgl/x")
        if not a: return None, None
        ig = os.path.join(ROOT, "irisgl")
        return (_git_try(["rev-parse", f"{a}:thirdparty/ogre-next"], ig),
                _git_try(["rev-parse", f"{b}:thirdparty/ogre-next"], ig))

    def fork(self, p, tag, depth):
        """A fork pin bump, selected by the fork diff's reach (FORK_FAMILIES, first match per
        file). A fork file no family owns, or a diff this checkout cannot read, is the tier."""
        old, new = self.fork_pins()
        src = os.path.join(ROOT, "irisgl", "thirdparty", "ogre-next")
        files = None
        if old and new and old != new:
            out = _git_try(["diff", "--name-only", old, new], src)
            files = out.splitlines() if out is not None else None
        self.fork_bump = {"old": old, "new": new, "files": files}
        if files is None:
            self.full_tier.append(f"{p}: the fork pin moved ({(old or '?')[:9]} -> {(new or '?')[:9]}) and this "
                                  f"checkout cannot read the fork diff — fetch the fork, or the tier")
            if depth == 0: self.rationale.append((p, "fork pin bump, diff unreadable → the MERGE tier"))
            return
        hits = collections.Counter()
        for f in files:
            fam = next((fam for pat, fam in FORK_FAMILIES if re.match(pat, f)), None)
            if fam is None:
                self.full_tier.append(f"{p}: fork file {f} — no fork family owns it")
                hits["TIER"] += 1; continue
            hits[fam] += 1
            why = f"{tag}: fork {fam} ({f})"
            if fam == "none":
                continue
            # "engine": the pixel family + every executable that extracts the engine
            self.expand(ENGINE_FAMILY + ["atom"], [], why)
            self.engine_rows(why)
        if depth == 0:
            self.rationale.append((p, f"fork pin {(old or '?')[:9]} -> {(new or '?')[:9]}: {len(files)} fork file(s) "
                                      f"by family {dict(hits)} (the full tier runs once, at the merge: --fork-tier)"))

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
        if not os.path.exists(os.path.join(ROOT, p)) and not self.names_anywhere(os.path.basename(p)):
            # A tests/ FILE GONE FROM THIS TREE that nothing here runs or names (a replayed range
            # meets a file a later lane deleted — tests/support/no_xid_run.sh after TEST-SELECTOR-1
            # H4): retired, said out loud, like a gone row; never a fallback
            notes.append(f"retired: {os.path.basename(p)} exists nowhere in this tree"); return
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
        # A CMAKE SCRIPT RUN WITH -P (irisgl/cmake/ApplyVendorPatches.cmake, run by an
        # execute_process at configure): its reach is the reach of the commands that run it
        invokers = self.p_script_invokers(p)
        if invokers:
            hit = 0
            self._var_seen = set()
            for f, text, c in invokers:
                if self.cmake_command(f, os.path.dirname(f), c[0], c[3], c[4], c[3], text, tag, notes, depth=1):
                    hit += 1
            if hit:
                if depth == 0: self.rationale.append((p, f"a -P script: the {hit} command(s) that run it; "
                                                         + "; ".join(dict.fromkeys(notes))))
                return
        if not a and b:
            # A NEW build file: everything it registers and builds is new — its rows and the
            # executables built under its directory
            rows = [n for n, t in self.inv.items() if os.path.join(ROOT, p) in t["frames"]]
            self.add(rows, f"{tag}: a new build file's rows")
            n_exe = self.exes_under(d, tag)
            if depth == 0: self.rationale.append((p, f"a new build file → its {len(rows)} row(s), {n_exe} executable(s) built under {d}"))
            return
        self._cmake_tip_text = b or ""
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
        for side, text, nums in (("old", a or "", old_i), ("new", b or "", new_i)):
            if not nums: continue
            parsed = gate_graph.cmake_commands(text)
            blocks = _block_spans(parsed)
            for k, c in enumerate(parsed):
                if any(c[1] <= n <= c[2] for n in nums):
                    # the changed lines' text, a trailing comment cut (a token in a comment names nothing)
                    changed_text = "\n".join(re.sub(r'\s#[^"]*$', "", text.split("\n")[n - 1])
                                             for n in sorted(nums) if c[1] <= n <= c[2])
                    # a block's own line (if/else/endif, foreach/endforeach): the block it opens or
                    # closes — its body is what the condition or the loop reaches
                    blk = blocks.get(k)
                    if blk is None and c[0] in _BLOCK_LINES:
                        inside = [sp for sp in blocks.values() if sp[0] <= c[1] <= sp[1]]
                        blk = min(inside, key=lambda sp: sp[1] - sp[0]) if inside else None
                    cmds.append((c, changed_text, text, side, blk or (c[1], c[2]), parsed, blk))
        unresolved = []
        # a MODIFIED command is on both sides: the new side (read through ctest's backtraces) first,
        # and its old self is then the same command, not a second one to resolve
        cmds.sort(key=lambda x: 0 if x[3] == "new" else 1)
        # THE BACKTRACES ARE THIS BUILD'S (the merge read's W2): their file:line sites describe the
        # working tree's text, so they resolve the tip's lines only when the tip's file IS that text,
        # line for line — the changed lines themselves aside (the build registered them as built) —
        # and a replayed range, or a tree that moved on, takes the name-based reading below
        try:
            wt = open(os.path.join(ROOT, p), errors="replace").read().split("\n")
            bt_ok = len(wt) == len(b_lines) and all(x == y for i, (x, y) in enumerate(zip(wt, b_lines), 1)
                                                    if i not in new_l)
        except OSError:
            bt_ok = False
        resolved_new = set()
        for (name, first, last, args, fn), changed_text, whole, side, span, parsed, blk in cmds:
            key = (name, tuple(args.split()[:1]))
            if side == "old" and key in resolved_new:
                continue
            # THE REGISTRATION READER (T3): the rows ctest itself says were registered from inside
            # this command's span (a foreach/while/if: its whole block) — jah_gpu_row(vr.${_arm}),
            # jah_scale_row, an add_test in a loop — read from the json-v1 backtraces, so no row
            # name has to be spelled in the command. The new side only (an old line's rows are
            # gone from this inventory; gone_row resolves those).
            if side == "new" and bt_ok:
                bt = self.rows_registered_at(p, span[0], span[1])
                if bt:
                    self.add(bt, f"{tag}: registered at {os.path.basename(p)}:{span[0]}-{span[1]}")
                    notes.append(f"{name}() at line {first} registers {len(bt)} row(s) (ctest backtrace)")
                    resolved_new.add(key)
                    continue
            if name in ("endfunction", "endmacro"):
                # a function's closing line is the function: the nearest function/macro header above it
                hdr = [c for c in parsed if c[0] in ("function", "macro") and c[1] < first]
                if hdr and hdr[-1][3].split():
                    fname = hdr[-1][3].split()[0].lower()
                    if self.cmake_command(p, d, name, args, fname, changed_text, whole, tag, notes):
                        continue
            if blk is not None and name in _BLOCK_LINES:
                # THE BLOCK'S BODY (T3): an if() whose condition changed, or an endif() a deletion
                # took with it, reaches what the commands inside it reach — resolved one by one
                self._var_seen = set()
                inner = [c for c in parsed if blk[0] < c[1] and c[2] < blk[1] and c[0] not in _BLOCK_LINES]
                hit = [c[0] for c in inner
                       if self.cmake_command(p, d, c[0], c[3], c[4], c[3], whole, tag, notes, depth=1)]
                if hit:
                    notes.append(f"{name}() at line {first}: its block's {len(hit)} command(s)")
                    continue
            got = self.cmake_command(p, d, name, args, fn, changed_text, whole, tag, notes)
            if not got: unresolved.append(f"{name}() at line {first}")
        if unresolved:
            self.fallback.append(f"{tag}: CMake command(s) that name nothing a suite owns: {', '.join(unresolved)}")
            notes.append("unresolved: " + ", ".join(unresolved))
        if depth == 0: self.rationale.append((p, "; ".join(dict.fromkeys(notes)) or "CMake"))

    def p_script_invokers(self, p):
        """[(build file, its text, command)] of every command that runs <p> with `-P`."""
        base = os.path.basename(p)
        if not base.endswith(".cmake"): return []
        if not hasattr(self, "_cmake_files"):
            self._cmake_files = []
            for dp, dns, fs in os.walk(ROOT):
                dns[:] = [x for x in dns if not x.startswith(("build", ".")) and x != "thirdparty"]
                for f in fs:
                    if f == "CMakeLists.txt" or f.endswith(".cmake"):
                        try: self._cmake_files.append((os.path.relpath(os.path.join(dp, f), ROOT),
                                                       open(os.path.join(dp, f), errors="replace").read()))
                        except OSError: pass
        out = []
        pat = re.compile(r"-P\s+\S*" + re.escape(base) + r"\b")
        for f, text in self._cmake_files:
            if base not in text: continue
            for c in gate_graph.cmake_commands(text):
                if pat.search(c[3]): out.append((f, text, c))
        return out

    def rows_registered_at(self, p, first, last):
        """The rows whose ctest backtrace passes through <p> at a line in [first, last]."""
        ap = os.path.normpath(os.path.join(ROOT, p))
        return sorted(n for n, t in self.inv.items()
                      if any(f == ap and first <= ln <= last for f, ln in t.get("sites", ())))

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
                          "jah_gpu_exclusive_test", "jah_gpu_row", "jah_scale_row"}
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
            elif name in ("jah_gpu_row", "jah_scale_row"):
                named = a_[:1]                      # the row is the first argument
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
                        and "NAME" in ca and ca.index("NAME") + 1 < len(ca) else
                        ca[:1] if c[0] in ("jah_gpu_row", "jah_scale_row") else [])
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
            if "${" in a_[1]:
                # an arm named through a loop variable (foreach(KIND a b c) jah_pool_arm(p x_${KIND})):
                # the pool's arms the pattern matches
                rx = re.compile("^" + re.sub(r"\\\$\\\{\w+\\\}", ".+", re.escape(a_[1])) + "$")
                arms = [f"pool.{a_[0]}::{x}" for x in self.inv[f"pool.{a_[0]}"]["arms"] if rx.match(x)]
                self.add(arms or [f"pool.{a_[0]}"], f"{tag}: its arm(s)")
                notes.append(f"arms {a_[0]}.{a_[1]}: {len(arms) or 'the pool whole'}"); return True
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
        if not rows and name in self._TEST_REGISTRATION | {"add_executable", "set_target_properties", "add_dependencies"} \
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
            if not via:
                # A HELPER RUN THROUGH `cmake_language(DEFER CALL <helper> …)` (a deferred label or
                # timeout on a row): its rows are those of the function that defers it
                for outer in self.deferring_functions(helper):
                    via += [n for n, t in self.inv.items() if outer in t["via"]]
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
                if not hit and "${" in bn:
                    # a script configured per loop value (e2e_bundle_export_${KIND}.js): every match
                    rx = re.compile("^" + re.sub(r"\\\$\\\{\w+\\\}", ".+", re.escape(bn)) + "$")
                    hit = sorted({r for b_, rs in self.script_base.items() if rx.match(b_) for r in rs})
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
        # 6. a variable: follow its users in the same file (not when the change only listed rows: a
        # suite list's change is the rows it adds or drops) — THROUGH CHAINS, three hops deep
        # (TEST-SELECTOR-1: a staged header's path set -> file(COPY_FILE) -> the include directory
        # of the target that compiles it), each variable once. file(GLOB|READ|STRINGS <var>) sets
        # one too.
        if depth == 0:
            self._var_seen = set()
        a_ = args.split()
        setter = name in ("set", "list", "option", "string") or (
            name == "file" and a_[:1] and a_[0] in ("GLOB", "GLOB_RECURSE", "READ", "STRINGS"))
        if setter and depth < 3 and not rows:
            var = _set_var(name, a_)
            tip_text = getattr(self, "_cmake_tip_text", None)
            if (var and tip_text is not None and whole is not tip_text
                    and not re.search(r"\b" + re.escape(var) + r"\b", tip_text) and not self.names_anywhere(var)):
                # A VARIABLE DELETED WITH ITS USERS (an option() and the if() that read it): whatever
                # it reached is in this diff as its users' own deleted lines
                notes.append(f"variable {var} deleted with its users"); return True
            users = [c for c in gate_graph.cmake_commands(whole)
                     if re.search(r"\$\{" + re.escape(var) + r"\}|\b" + re.escape(var) + r"\b", c[3])
                     and not (c[0] == name and c[3].split()[:1] == a_[:1])] if var not in self._var_seen else []
            self._var_seen.add(var)
            for (un, uf, ul, ua, ufn) in users:
                if self.cmake_command(p, d, un, ua, ufn, ua, whole, tag, notes, depth=depth + 1): got = True
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
        if not got and depth < 3:
            for var in sorted(set(re.findall(r"\$\{(_\w+)\}", args)) - self._var_seen):
                self._var_seen.add(var)
                users = [c for c in gate_graph.cmake_commands(whole)
                         if "${" + var + "}" in c[3] and c[3] != args and c[0] != "set"]
                for (un, uf, ul, ua, ufn) in users:
                    if self.cmake_command(p, d, un, ua, ufn, ua, whole, tag, notes, depth=depth + 1): got = True
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

    def deferring_functions(self, helper):
        """The functions whose bodies run `DEFER CALL <helper>` (in any build file of the tree)."""
        self.cmake_calls(helper)            # fills _cmake_texts
        out = set()
        for t in self._cmake_texts:
            if "CALL " + helper not in t and "CALL\t" + helper not in t: continue
            for c in gate_graph.cmake_commands(t):
                if c[4] and re.search(r"DEFER\s+CALL\s+" + re.escape(helper) + r"\b", c[3], re.I):
                    out.add(c[4])
        return out

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


# string(<SUB> ...)'s OUTPUT variable, by argument index (the sub-command is index 0)
_STRING_OUT = {"APPEND": 1, "PREPEND": 1, "CONCAT": 1, "JOIN": 2, "SHA256": 1, "SHA1": 1, "MD5": 1,
               "SHA512": 1, "SHA224": 1, "SHA384": 1, "REPLACE": 3, "STRIP": 2, "TOLOWER": 2, "TOUPPER": 2,
               "LENGTH": 2, "SUBSTRING": 4, "FIND": 3, "TIMESTAMP": 1, "CONFIGURE": 2, "GENEX_STRIP": 2,
               "MAKE_C_IDENTIFIER": 2, "REPEAT": 3, "HEX": 2, "ASCII": -1}
_STRING_REGEX_OUT = {"REPLACE": 4, "MATCH": 3, "MATCHALL": 3}


def _set_var(name, a_):
    """The variable a set/list/option/string/file(GLOB|READ|STRINGS) command WRITES."""
    if not a_: return ""
    if name == "string":
        if a_[0] == "REGEX" and len(a_) > 1:
            k = _STRING_REGEX_OUT.get(a_[1], 3)
        else:
            k = _STRING_OUT.get(a_[0], 1)
        return a_[k] if -len(a_) <= k < len(a_) else ""
    if name in ("list", "file"):
        return a_[1] if len(a_) > 1 else ""
    return a_[0]


_BLOCK_OPEN = {"foreach": "endforeach", "while": "endwhile", "if": "endif"}
_BLOCK_LINES = {"if", "elseif", "else", "endif", "foreach", "endforeach", "while", "endwhile"}


def _block_spans(parsed):
    """command index -> (first line, last line) of the BLOCK a foreach/while/if opens (to its
    matching end command): the rows its body registers are that command's rows."""
    out, stack = {}, []
    for k, c in enumerate(parsed):
        if c[0] in _BLOCK_OPEN:
            stack.append((k, _BLOCK_OPEN[c[0]]))
        elif stack and c[0] == stack[-1][1]:
            k0, _ = stack.pop()
            out[k0] = (parsed[k0][1], c[2])
    return out


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
    S.tooling_only = bool(paths) and not product_paths(paths)
    for p in paths:
        if S.tooling_only and TOOLING_OWN.match(p):
            S.tooling_path(p)
        else:
            S.path(p)
    S.stage_close_subjects(paths)
    if S.code_moved and not S.tooling_only: S.add(ALWAYS_ON_CODE, "code moved: smoke + contract")
    if graph is not None: graph.save_syms()
    return S


ATTR_DEFECT_COMBINATION, ATTR_INCOMPLETE, ATTR_DEFECT_DBUILD = 3, 7, 5


def attribute(row_args, lane_specs, tag, times, tier, candidate=None, controls=None, display=None):
    """THE BATCH RED, ATTRIBUTED (BATCH-GATE-1; docs/TESTING_GATE.md §3c; TESTING_V3_SPEC §1.3.2). A batch
    candidate's gate went red on <rows>: each row runs `times` times SOLO in each lane's OWN worktree at its exact
    batch tip (the lanes' built trees), at the CANDIDATE's tip (its rc tree) and at every CONTROL (the base build
    rc-base at d-build's tip), every run a logged retry at THAT tip (`reason: attribute:<tag>`; `lanes` = the
    lane, the candidate's = the batch's list, a control's = its tree's name). The display is NAMED (`display`,
    :60-:99 with its X lock) and the environment's DISPLAY is never read. The whole card is held ONCE for the
    attribution through hold_card() — whatever the card's admission demands (it never assumes the gate slot is
    free). Only REAL verdicts count: a run that never got its admission (NOADMIT) or never ran (NOTRUN) leaves its
    cell INCOMPLETE. A row a tree's build does not REGISTER is ABSENT there (never run, never blamed). Per row, IN
    THIS ORDER:
      1. a control cell INCOMPLETE/ABORTED -> the row is INCOMPLETE (nothing is named without its baseline);
      2. red on a control (any solo) = a D-BUILD DEFECT: it names nobody (kind `defect`, registered);
      3. a lane is NAMED when ANY of its solos is red (the flake law: one red run is a red) — it drops out;
      4. any other cell INCOMPLETE/ABORTED -> INCOMPLETE;
      5. red at the candidate and green on every lane = a COMBINATION DEFECT (kind `combination`, registered):
         the batch is REFUSED;
      6. green at the candidate too = NOT REPRODUCED (kind `nondeterminism`, registered): the verdict door.
    Every registered finding is written as <workspace>/testing/defects.pending/<id>.json in TESTING_V3_SPEC §1.5's
    FULL schema (recheck, expires; uses/suspects/census for NOT REPRODUCED) — VERDICT-1's reader QUARANTINES a
    malformed entry (it never reaches the door). Returns 0 (no defect, complete), 3 (a combination defect), 7 (an
    INCOMPLETE or aborted attribution), 5 (a d-build defect only), 4 (a tree is unusable), 64 (usage)."""
    rows = []
    for arg in row_args or []:
        for r in arg.split(","):
            r = r.split(" :: ", 1)[0].strip()       # a pool arm is attributed by its row
            if r and r not in rows:
                rows.append(r)
    if not rows or not lane_specs or not tag or not candidate or not controls or not display:
        sys.stderr.write("gate-scope --attribute: give the red rows, --batch <tag>, --candidate <rc tree>:<tip>, "
                         "--control <base build>:<d-build tip> (required: a red names nobody without its baseline), "
                         "--display :NN and --lanes <lane>:<worktree>:<tip> [...]\n")
        return 64
    # THE DISPLAY IS NAMED, NEVER INHERITED (the lead's shell carries the owner's :0)
    m = re.match(r"^:([6-9][0-9])$", display)
    xroot = os.environ.get("JAH_X11_ROOT", "/tmp")
    if not m:
        sys.stderr.write(f"gate-scope --attribute: REFUSED — --display {display} is not a rig display (:60-:99)\n")
        return 64
    if not os.path.exists(os.path.join(xroot, f".X{m.group(1)}-lock")):
        sys.stderr.write(f"gate-scope --attribute: REFUSED — --display {display} has no X server "
                         f"(no {os.path.join(xroot, f'.X{m.group(1)}-lock')})\n")
        return 64
    specs = [(s, "lane") for s in lane_specs] + [("candidate:" + candidate, "candidate")]
    for c in controls:
        tree = c.rpartition(":")[0]
        specs.append((f"{os.path.basename(os.path.normpath(tree)) or 'control'}:{c}", "control"))
    trees, bad = [], []
    for spec, kind in specs:
        name, _, rest = spec.partition(":")
        wt, _, tip = rest.rpartition(":")
        if not (name and wt and tip):
            bad.append(f"{spec}: not <name>:<worktree>:<tip>"); continue
        wt = os.path.abspath(os.path.expanduser(wt))
        build = os.path.join(wt, "build-linux")
        head = gate_runlog._git(["rev-parse", "HEAD"], cwd=wt)
        want = gate_runlog._git(["rev-parse", "--verify", "-q", tip + "^{commit}"], cwd=wt)
        if not os.path.isdir(build):
            bad.append(f"{name}: no built tree at {build}")
        elif not want or head != want:
            # THE EXACT-TIP RULE: a record says which tree ran; a worktree moved past its tip is another tree
            bad.append(f"{name}: {wt} is at {head[:9] or '?'}, not the batch's tip {tip[:9]}")
        else:
            problem = gate_runlog.fork_pin_problem(root=wt)
            if problem:
                bad.append(f"{name}: {problem.splitlines()[0]}"); continue
            raw, irc = gate_graph.ctest_inventory(build)
            try:
                names = {t["name"] for t in json.loads(raw or "{}").get("tests", [])} if irc == 0 else None
            except ValueError:
                names = None
            if not names:
                bad.append(f"{name}: {build} lists no ctest rows (ctest --show-only exit {irc})")
            else:
                trees.append((name, wt, build, head, names, kind))
    if bad:
        for b in bad: sys.stderr.write(f"gate-scope --attribute: REFUSED — {b}\n")
        return 4
    lanes = [t for t in trees if t[5] == "lane"]
    ctrls = [t for t in trees if t[5] == "control"]
    cand = [t for t in trees if t[5] == "candidate"][0]
    reason = f"attribute:{tag}"
    print(f"gate-scope --attribute (batch {tag}): {len(rows)} row(s) x ({len(lanes)} lane(s) + the candidate + "
          f"{len(ctrls)} control(s)) x {times} solo run(s), each on its own tip, display {display}; the whole card "
          f"held once (hold_card: the gate slot first, GATE-COST-2)")
    gate_runlog.on_signals()
    vt = gate_runlog._vram()
    card, env = vt.hold_card(f"attribute {tag}: {','.join(rows)[:80]}", log=sys.stdout)
    env = dict(env, DISPLAY=display)
    print(f"gate-scope --attribute: the slot waited {vt.LAST_SLOT_WAIT_S or 0:.0f} s, the card drained in "
          f"{vt.LAST_WAIT_S:.0f} s")
    # F1 (GATE-COST-2 at the rebase): a hold whose drain timed out returns the SLOT only (card == [slot]), so
    # "no card" is not the test — the drain record is. Then: the phase record, the tool's JAH_VRAM_ALL (every
    # admission takes the card itself) and `fallback: drain-timeout` on every run, never an override
    fallback = None
    if vt.LAST_DRAIN_TIMEOUT and not env.get("JAH_VRAM_HELD"):
        gate_runlog.phase_record("drain-timeout", tier, tag, None, gate_runlog.tree_shas(), fallback="drain-timeout",
                                 **vt.LAST_DRAIN_TIMEOUT)
        env["JAH_VRAM_ALL"] = "1"
        fallback = "drain-timeout"
    cells = {}                          # (row, name) -> (state, reds, real runs, first failing check, last record)
    try:
        for row in rows:
            rx = "^" + re.escape(row) + "$"
            for name, wt, build, head, names, kind in trees:
                if row not in names:
                    cells[(row, name)] = ("ABSENT", 0, 0, None, None); continue
                seen = len(_attribution_records(row, head, reason))
                reds, real, first, never, last = 0, 0, None, [], None
                for _ in range(times):
                    rc = gate_runlog.run_ctest(
                        f"ctest -j1 --timeout 900 --output-on-failure --no-tests=error -R '{rx}'", build, tier,
                        [t[0] for t in lanes] if kind == "candidate" else [name], 1, reasons={row: reason},
                        retry=True, env=env, whole_card=False, root=wt, fallback=fallback)
                    if rc == gate_runlog.DISPLAY_LOST or rc < 0 or rc > 128:
                        cells[(row, name)] = ("ABORTED", reds, real, f"exit {rc}", last)
                        print(f"\n=== ATTRIBUTION ABORTED at {row} on {name} (exit {rc}): the display died or ctest "
                              f"was killed — the table below is partial ===")
                        raise _AttributionAborted()
                    recs = _attribution_records(row, head, reason)
                    new, seen = recs[seen:], len(recs)
                    v = new[-1].get("verdict") if new else None
                    if new: last = new[-1]
                    if v is None or v in ("NOADMIT", "NOTRUN"):
                        never.append(v or f"no record (exit {rc})")      # never ran: no verdict
                        continue
                    real += 1
                    if v != "PASS":
                        reds += 1
                        if first is None:
                            first = new[-1].get("failLine") or f"{v} ({new[-1].get('status')})"
                cells[(row, name)] = ("RAN" if real == times else "INCOMPLETE", reds, real,
                                      first if real == times or reds else
                                      f"{times - real} run(s) never ran ({', '.join(map(str, never))})", last)
    except _AttributionAborted:
        pass
    finally:
        gate_runlog._vram().release(card)
    print(f"\n=== ATTRIBUTION (batch {tag}) ===")
    fb = f" | fallback: {fallback}" if fallback else ""
    if fallback:
        # GATE-COST-2 round: visible WHERE THE LEAD READS — the drain timed out, the runs were not under one
        # whole-card hold: a timing row's cell here is not a measurement
        print(f"FALLBACK: {fallback} — the whole-card drain timed out; every run took the card itself (JAH_VRAM_ALL)")
    print("row | tree | red | the first failing check" + (" | fallback" if fallback else ""))
    out = {"combination": False, "defect": False, "incomplete": False}
    nil = ("ABORTED", 0, 0, None, None)
    unfinished = ("INCOMPLETE", "ABORTED")
    for row in rows:
        for name, *rest in trees:
            st, reds, real, first, _ = cells.get((row, name), nil)
            who = {"candidate": "CANDIDATE", "control": f"CONTROL {name}"}.get(rest[-1], name)
            if st == "ABSENT":
                print(f"{row} | {who} | ABSENT | the row is not registered in this build{fb}")
            elif st in unfinished:
                print(f"{row} | {who} | {st} {reds}/{real} red of {real} that ran | {first or '-'}{fb}")
            else:
                print(f"{row} | {who} | {reds}/{real} red | {first or '-'}{fb}")
        lc = [(t[0], cells.get((row, t[0]), nil)) for t in lanes]
        ctl = [(t, cells.get((row, t[0]), nil)) for t in ctrls]
        cc = cells.get((row, cand[0]), nil)
        named = [f"{n} ({c[1]}/{c[2]} red on its own tip)" for n, c in lc if c[1] > 0]
        if any(c[0] in unfinished for _, c in ctl):
            out["incomplete"] = True
            print(f"=> {row}: INCOMPLETE — a CONTROL never got its runs: no lane is named without the baseline; "
                  f"re-run the attribution")
        elif any(c[1] > 0 for _, c in ctl):
            out["defect"] = True
            t, c = [(t, c) for t, c in ctl if c[1] > 0][0]
            path = _register(tag, "defect", row, t[3], c[4],
                             f"red on d-build's own tip ({t[0]}: {c[1]}/{c[2]}; {c[3] or '-'}) — no lane makes it", fallback=fallback)
            print(f"=> {row}: D-BUILD DEFECT — red on the control {t[0]} ({c[1]}/{c[2]}) without any lane: it names "
                  f"nobody; registered {path}")
        elif named:
            print(f"=> {row}: NAMED {', '.join(named)} — any red solo names a lane; it drops out and the rest are "
                  f"RE-GATED as a new candidate")
        elif any(c[0] in unfinished for _, c in lc) or cc[0] in unfinished or cc[0] == "ABSENT":
            out["incomplete"] = True
            print(f"=> {row}: INCOMPLETE — a cell never got its runs (NOADMIT / NOTRUN / aborted, or the candidate "
                  f"lacks the row): no lane is named or cleared until it ran; re-run the attribution")
        elif cc[1] > 0:
            out["combination"] = True
            path = _register(tag, "combination", row, cand[3], cc[4],
                             f"red at the candidate ({cc[1]}/{cc[2]}; {cc[3] or '-'}) and green on every lane's own "
                             f"tip and on d-build's", fallback=fallback)
            print(f"=> {row}: COMBINATION DEFECT — red at the candidate ({cc[1]}/{cc[2]}) and green on every lane's own "
                  f"tip: the batch is REFUSED; registered {path}")
        else:
            path = _register(tag, "nondeterminism", row, cand[3], cc[4],
                             f"red in batch {tag}'s gate; green {cc[2]}/{cc[2]} at the candidate, on every lane and "
                             f"on d-build in the attribution", suspects=[t[0] for t in lanes], fallback=fallback)
            print(f"=> {row}: NOT REPRODUCED — green at the candidate {cc[2]}/{cc[2]} and everywhere else: a "
                  f"nondeterminism, registered {path}; it passes the verdict door with these solos recorded")
    if out["combination"]: return ATTR_DEFECT_COMBINATION
    if out["incomplete"]: return ATTR_INCOMPLETE
    if out["defect"]: return ATTR_DEFECT_DBUILD
    return 0


def _register(tag, kind, row, tip, rec, cause, suspects=None, fallback=None):
    """A finding REGISTERED, never printed only (TESTING_V3_SPEC §1.5): <registry dir>/defects.pending/<id>.json — the
    registry dir is that of testing/defects.json (JAH_DEFECTS_FILE moves it; JAH_DEFECTS_PENDING_DIR moves the
    pending dir alone), in the FULL schema VERDICT-1's reader requires (it QUARANTINES a malformed entry — the finding
    would never reach the door): {id, rows, kind, cause, first_seen {tip, pin, run}, state: open, found_by: gate, recheck (a DATE: the
    next day — the next batch — for NOT REPRODUCED, +7 days for a combination / d-build defect), expires (= recheck)},
    and for NOT REPRODUCED the single-use fields {uses: 1, suspects: [the batch's lanes], census: {from the record}}.
    A finding made while the whole-card drain had timed out carries `fallback` (GATE-COST-2: the reader sees the
    runs were not under one whole-card hold). Returns the path."""
    import datetime as _dt
    reg = os.environ.get("JAH_DEFECTS_FILE") or os.path.join(gate_runlog.workspace_root(), "testing", "defects.json")
    d = os.environ.get("JAH_DEFECTS_PENDING_DIR") or os.path.join(os.path.dirname(reg), "defects.pending")
    os.makedirs(d, exist_ok=True)
    did = re.sub(r"[^A-Za-z0-9._-]+", "-", f"{tag}-{kind}-{row}")
    rec = rec or {}
    t = rec.get("tip") or {}
    today = _dt.date.today()
    recheck = (today + _dt.timedelta(days=1 if kind == "nondeterminism" else 7)).isoformat()
    entry = {"id": did, "rows": [row], "kind": kind, "cause": cause,
             "first_seen": {"tip": tip, "pin": t.get("fork") or "", "run": rec.get("run") or ""},
             "state": "open", "found_by": "gate", "recheck": recheck, "expires": recheck}
    if kind == "nondeterminism":
        box = rec.get("box") or {}
        entry.update(uses=1, suspects=list(suspects or []),
                     census=box.get("census") if isinstance(box.get("census"), dict) else dict(box))
    if fallback:
        entry["fallback"] = fallback
    path = os.path.join(d, did + ".json")
    with open(path, "w") as f:
        json.dump(entry, f, indent=1, sort_keys=True)
        f.write("\n")
    return path


class _AttributionAborted(Exception):
    pass



def _attribution_records(row, head, reason):
    """The attribution records of `row` at `head` (oldest first): the row's own record per run."""
    d = gate_runlog.log_dir()
    out = []
    for f in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        if head[:9] not in f or not f.endswith(".jsonl"): continue
        for line in open(os.path.join(d, f), errors="replace"):
            try: r = json.loads(line)
            except ValueError: continue
            if (r.get("suite") == row and r.get("arm") is None and r.get("reason") == reason
                    and (r.get("tip") or {}).get("studio") == head):
                out.append(r)
    out.sort(key=lambda r: r.get("ts") or "")
    return out


def run_target_step(target_cmd, build, lane, log_range, labels, reasons, exclude=None):
    """THE TARGET STEP (GATE-COST-1 P9; TARGET-STEP-DISPLAY-1, ledger §1851): the selection's target
    rows, -j1, run-log tier `target`, AFTER the gating verdict and INSIDE the gate — on the gate's own
    display, under its slot, holding the whole card once (they used to run DETACHED after the gate's
    exit: the lane killed its Xvfb and the step carried on against a dead display, 159 "Malformed
    resolution string" records). Reported, never gating: its exit code is printed and dropped. A
    display that dies under it stops it like any run (gate_runlog's guard). Returns nothing."""
    print("\n=== target tests (label %s): reported, not gating — the gate's display, the whole card ==="
          % "/".join(sorted(TARGET_LABELS)))
    trc = gate_runlog.run_ctest(target_cmd, build, "target", lane, 1, reasons=reasons, rng=log_range,
                                labels=labels, gating=lambda n: False, whole_card=True, exclude=exclude)
    if trc == gate_runlog.DISPLAY_LOST or trc < 0 or trc > 128:
        print("=== target tests STOPPED (the display died, or their ctest was killed) — NOT part of any gate's verdict ===")
    else:
        print("=== target tests exited %d — NOT part of any gate's verdict ===" % trc)


def main():
    # A TIER FLAG THIS TOOL DOES NOT RUN is refused by name, never parsed as a range (STAGE-CLOSE-1: the
    # batch of the rows that leave the MERGE and PUSH tiers is `--stage-close-tier`; no alias is kept)
    for x in sys.argv[1:]:
        if re.match(r"^--[a-z-]+-tier(-serial)?$", x) and x not in (
                "--merge-tier", "--merge-tier-serial", "--fork-tier", "--stage-close-tier", "--stage-close-tier-serial"):
            sys.stderr.write(f"gate-scope: {x} is not a tier this tool runs — the rows the MERGE and PUSH tiers leave "
                             f"out run as the stage-close batch: --stage-close-tier (docs/TESTING_GATE.md §1d)\n")
            sys.exit(2)
    ap = argparse.ArgumentParser()
    ap.add_argument("range", nargs="?", help="git range base..tip (Studio repo)")
    ap.add_argument("--files", nargs="*", help="explicit touched paths instead of a range")
    ap.add_argument("--build", default="build-linux")
    ap.add_argument("--run", action="store_true", help="run the selection now (DISPLAY must be set); "
                    "every suite's verdict goes to the run log")
    ap.add_argument("-j", "--jobs", type=int, default=GATE_JOBS,
                    help=f"ctest parallelism of the parallel phase (default GATE_JOBS = {GATE_JOBS}, the one constant)")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--lane", action="append", default=None,
                    help="the record's `lanes` (default: the branch): repeat it or give a comma-separated list — a "
                         "batch candidate's gate names every lane in it (BATCH-GATE-1)")
    ap.add_argument("--tier", default=None, choices=gate_runlog.TIERS,
                    help="the run log's tier name (default: scoped; scoped-fallback / scoped-tier when a scoped "
                         "gate runs the whole tier)")
    ap.add_argument("--record-times", action="store_true",
                    help="re-price scripts/gate-times.txt: every tier row of --build, the run log's (and its archive's) "
                         "quiet median PASS seconds, the source named on each line")
    ap.add_argument("--price-check", action="store_true",
                    help="the prices' self-check: list every MERGE / STAGE-CLOSE tier row of --build with no line in "
                         "scripts/gate-times.txt; exit 1 while one is")
    ap.add_argument("--solo", metavar="SUITE", nargs="+",
                    help="the flake protocol: run each suite alone --times times, logged as retries")
    ap.add_argument("--times", type=int, default=3)
    ap.add_argument("--merge-tier", action="store_true",
                    help="print the MERGE tier's ctest command (at -j) and exit — the one source "
                         "docs/TESTING_GATE.md quotes instead of a copy of the -LE set")
    ap.add_argument("--attribute", metavar="ROW[,ROW...]", action="append", default=None,
                    help="a BATCH red (BATCH-GATE-1): run each row --times times solo on each lane's OWN tip "
                         "(--lanes), print the attribution table; a whole-card hold, so it takes the gate slot "
                         "first (GATE-COST-2)")
    ap.add_argument("--lanes", metavar="LANE:WORKTREE:TIP", nargs="+", default=None,
                    help="with --attribute: the batch's lanes, each its worktree (built) and its exact Studio tip")
    ap.add_argument("--candidate", metavar="RC_TREE:TIP", default=None,
                    help="with --attribute: the candidate's rc tree and tip — the CONTROL (red there and green on every "
                         "lane = a combination defect; green there = not reproduced)")
    ap.add_argument("--control", metavar="TREE:TIP", nargs="+", default=None,
                    help="with --attribute: CONTROL trees (d-build's built tree at its tip; several allowed) — a row red "
                         "on one names nobody and is a d-build defect")
    ap.add_argument("--display", metavar=":NN", default=None,
                    help="with --attribute (REQUIRED): the rig display, :60-:99 with its X lock — the environment's "
                         "DISPLAY is never read")
    ap.add_argument("--batch", metavar="TAG", default=None,
                    help="the batch tag: every record carries `batch: <tag>` (a candidate's --run, an --attribute — "
                         "whose reason is `attribute:<tag>`)")
    ap.add_argument("--fork-tier", action="store_true",
                    help="a range that moves the fork pin: run the MERGE tier (logged as tier `fork`) — §7b rule 4's "
                         "one full tier per bump, at the merge into d-build; ci_gate_check requires it")
    ap.add_argument("--merge-tier-serial", action="store_true",
                    help="print the MERGE tier's SERIAL phase (its timing rows at -j1, run after --merge-tier's "
                         "parallel phase) and exit")
    ap.add_argument("--stage-close-tier", action="store_true",
                    help="THE STAGE-CLOSE BATCH (the lead's, at every stage close and before every push): print "
                         "its parallel phase (at -j) and exit; with --run, run both phases in the gate slot "
                         "(run-log tier `stage-close`; the quiet-box/timing phase -j1 on one whole-card hold)")
    ap.add_argument("--stage-close-tier-serial", action="store_true",
                    help="print the stage-close batch's quiet-box phase (every `quiet-box`/`timing` stage-close "
                         "row, -j1, the whole card) and exit")
    ap.add_argument("--stage-close-rules", action="store_true",
                    help="print STAGE_CLOSE_SUBJECTS (which paths still bring which stage-close rows into a "
                         "scoped gate) and exit")
    ap.add_argument("--gate-jobs", action="store_true",
                    help="print GATE_JOBS, the gate's parallel width (rc-gate.sh reads it), and exit")
    ap.add_argument("--resume", action="store_true",
                    help="--run, but only the rows with no record at this tip (a gate that was killed or lost its "
                         "display: the rows it finished are in the run log already)")
    tg = ap.add_mutually_exclusive_group()
    tg.add_argument("--no-targets", action="store_true",
                    help="with --run: do not start the target tests' step after the gating verdict")
    tg.add_argument("--targets-only", action="store_true",
                    help="with --run: run ONLY the selection's target tests (-j1, run-log tier `target`), in the "
                         "foreground; reported, exit 0 whatever they read — the step --run starts by itself")
    if "--joint" in sys.argv[1:]:
        # RETIRED (BATCH-GATE-1): the union of two lanes' selections is what a batch candidate's ONE gate runs
        sys.stderr.write("gate-scope: --joint is retired (BATCH-GATE-1) — the batch gate IS the joint gate: stack the "
                         "lanes as one candidate with\n  scripts/lead/merge-dbuild-lane.sh batch <tag> "
                         "<lane>:<studio tip>:<irisgl tip> [...]\nand gate `d-build..candidate` once (the command it "
                         "prints); a red is attributed with `gate-scope.sh --attribute`\n")
        sys.exit(2)
    a = ap.parse_args()
    if a.resume:
        a.run = True
    if a.gate_jobs:
        print(GATE_JOBS); return
    if a.record_times or a.price_check:
        b_ = resolve_build(a.build)
        if a.record_times:
            sys.exit(record_times(b_))
        left = unpriced(load_inventory(b_))
        print(f"gate-times.txt: {len(left)} unpriced tier row(s)" + (": " + " ".join(left) if left else " — clean"))
        sys.exit(1 if left else 0)
    if a.merge_tier:
        print(merge_tier(a.jobs)); return
    if a.merge_tier_serial:
        print(merge_tier_serial()); return
    if a.stage_close_tier_serial:
        print(stage_close_tier_serial()); return
    if a.stage_close_rules:
        for pat, rows in STAGE_CLOSE_SUBJECTS:
            print(f"{pat}\n    -> {' '.join(rows)}")
        return
    if a.stage_close_tier and not a.run:
        print(stage_close_tier(a.jobs)); return
    gate_runlog.BATCH = a.batch            # `batch: <tag>` on every record (a candidate's gate, an attribution)
    if a.attribute:
        # each lane's OWN worktree and build (--lanes), never this checkout's
        sys.exit(attribute(a.attribute, a.lanes, a.batch, a.times, a.tier or "scoped", a.candidate, a.control,
                           a.display))
    if a.lanes or a.candidate or a.control or a.display:
        ap.error("--lanes / --candidate / --control / --display go with --attribute")
    build = resolve_build(a.build)
    # THE BUILT FORK MUST BE THE PIN (TESTING-DEBTS-1 T12): a run on an install built from another
    # fork commit is void (stale media) — refused before a suite runs, with the lines that fix it.
    # (A range that MOVES the pin selects by the fork diff's reach, and --fork-tier runs its one full
    # tier, §7b.4; this check is about the tree.)
    if a.run or a.solo:
        bad = gate_runlog.fork_pin_problem()
        if bad:
            sys.stderr.write("gate-scope: " + bad + "\n")
            sys.exit(4)
        # THE NO-OP BUILD FIRST (GATE-COST-2 F4): the rows run on HEAD's binaries, BUILT_FROM fresh — or not at all
        bad = gate_runlog.prebuild(build)
        if bad:
            sys.stderr.write("gate-scope: " + bad + "\n")
            sys.exit(5)
    slot, owed = [], []

    def solo_card(label):
        """A solo batch's whole card (hold_card: the slot first outside a gate), and on a drain timeout the
        phase record + the tool's fallback (round 2, C — both solo paths): (fds, env, fallback|None)."""
        card_, env_ = gate_runlog._vram().hold_card(label, log=sys.stdout)
        drained_ = gate_runlog._vram().LAST_DRAIN_TIMEOUT
        if not drained_ or env_.get("JAH_VRAM_HELD"):
            return card_, env_, None
        gate_runlog.phase_record("drain-timeout", a.tier or "solo", lane, log_range, gate_runlog.tree_shas(),
                                 fallback="drain-timeout", **drained_)
        # every admission of a run takes the card itself; the TOOL set it — a fallback, never an override (F2)
        env_["JAH_VRAM_ALL"] = "1"
        return card_, env_, "drain-timeout"

    def gate(what):
        """THE GATE SLOT (P1), once per gate run: queue (FIFO, no bound, the position printed), then hold
        it in THIS process to its end (the ctest trees below die with it: gate_runlog F2)."""
        if not slot:
            gate_runlog.on_signals()
            who = "+".join(gate_runlog.lane_list(a.lane)) or os.path.basename(gate_runlog.ROOT)
            fd = gate_runlog._vram().gate_slot(f"{who} {what}", log=sys.stdout)
            slot.append(fd)
        return None

    def lost(rc):
        """A run whose display died (P6) — or whose ctest was KILLED (a signal; 128 + it through a shell:
        F4) — ends the gate here, with its verdict line: no timing phase, no target step after it."""
        if rc == gate_runlog.DISPLAY_LOST:
            print("\n=== GATE VERDICT: ABORTED — the display died; nothing after it ran and no row was recorded "
                  "against it; re-run with --resume on a live display ===")
            sys.exit(gate_runlog.DISPLAY_LOST)
        if rc < 0 or rc > 128:
            sig = -rc if rc < 0 else rc - 128
            print(f"\n=== GATE ABORTED: ctest was killed (signal {sig}); the rows it finished are recorded ===\n"
                  f"=== GATE VERDICT: ABORTED — nothing after it ran; re-run with --resume ===")
            sys.exit(128 + sig)
        return rc

    def done_rows():
        """--resume: the rows with a record at this tip (not run again)."""
        if not a.resume:
            return None
        got = gate_runlog.recorded_rows()
        print(f"gate-scope --resume: {len(got)} row(s) already have a record at this tip")
        # A ROW AN ABORT DROPPED RED RE-RUNS AS A SOLO, never in the ordinary pass (GATE-COST-2 #9): out of the
        # pass here, 3x solo by owed_solo_pass() before the verdict; the abort record keeps droppedRed
        owed[:] = gate_runlog.owed_solos(gate_runlog.tree_shas())
        if owed:
            print(f"gate-scope --resume: {len(owed)} row(s) an abort dropped RED re-run as SOLOS (3x, tier solo), not "
                  f"in the ordinary pass: {' '.join(owed[:12])}")
        return got | set(owed)

    def owed_solo_pass(labels):
        """--resume's solos for the rows an abort dropped red: each 3x, tier `solo`, retry, on one whole-card
        hold inside the gate. Returns the worst exit code (0 when none was owed)."""
        if not owed:
            return 0
        print(f"\n=== the dropped reds' solos: {len(owed)} row(s), 3x each, the whole card held once ===")
        card, env_, fb_ = solo_card(f"{'+'.join(lane)} dropped-red solos")
        rc_ = 0
        try:
            for s_ in owed:
                for _ in range(3):
                    rc_ = lost(gate_runlog.run_ctest(
                        f"ctest -j1 --timeout 900 --output-on-failure --no-tests=error -R '^{re.escape(s_)}$'",
                        build, "solo", lane, 1, reasons={s_: "solo: dropped red by an abort"}, rng=log_range,
                        retry=True, env=env_, labels=labels, whole_card=False, fallback=fb_)) or rc_
        finally:
            gate_runlog._vram().release(card)
        return rc_

    if a.stage_close_tier:
        # THE STAGE-CLOSE BATCH (U4): the slot once, both phases, each row recorded as it ends
        sc_lane = gate_runlog.lane_list(a.lane) or ["stage-close"]
        sc_tier = a.tier or "stage-close"
        labels = {n: t["labels"] for n, t in load_inventory(build).items()}
        print(f"{stage_close_tier(a.jobs)}\n{stage_close_tier_serial()}")
        gate("stage-close"); skip = done_rows()
        t0 = __import__("time").time()
        rc = lost(gate_runlog.run_ctest(stage_close_tier(a.jobs), build, sc_tier, sc_lane, a.jobs, reasons={},
                                        labels=labels, exclude=skip))
        print("\n=== the quiet-box phase: every measuring stage-close row, -j1, the whole card held once ===")
        rc = lost(gate_runlog.run_ctest(stage_close_tier_serial(), build, sc_tier, sc_lane, 1, reasons={},
                                        labels=labels, exclude=skip, whole_card=True)) or rc
        print("\n=== GATE VERDICT: %s (exit %d) — the stage-close batch, %.0f s wall ===" % (
            "GREEN" if rc == 0 else "RED", rc, __import__("time").time() - t0))
        gate_runlog.trend_at_gate_end()
        sys.exit(rc)
    lane = gate_runlog.lane_list(a.lane) or [gate_runlog._git(["rev-parse", "--abbrev-ref", "HEAD"])]
    # the run log records the range by sha (HEAD moves; the record must not)
    log_range = a.range
    if a.range and ".." in a.range:
        b_, t_ = a.range.split("..", 1)
        log_range = "%s..%s" % (gate_runlog._git(["rev-parse", "--short=9", b_]) or b_,
                                gate_runlog._git(["rev-parse", "--short=9", t_]) or t_)
    if a.solo:
        rc = 0
        # THE FLAKE LAW'S LIST (L2): a solo retry clears a red only for a contention-class suite, and
        # only 3/3; any other red needs a recorded verdict (ci_gate_check --verdict) — said up front
        cl = gate_runlog.contention_list()
        for s in a.solo:
            if cl is None:
                print(f"gate-scope --solo: the defect registry {gate_runlog.defects_file()} is unreadable — "
                      f"the merge refusal will not accept these retries until it is back")
            elif s in cl:
                print(f"gate-scope --solo: {s} is contention-class ({cl[s][:100]}): {a.times}/{a.times} PASS clears its red")
            else:
                print(f"gate-scope --solo: {s} is NOT in the contention class ({gate_runlog.defects_file()}): "
                      f"the retries are logged, and its red still needs a recorded verdict "
                      f"(scripts/ci-gate-check.sh <range> --verdict \"{s}=<text>\")")
        # SOLO ON THE CARD, ONE DRAIN PER BATCH (G1+G2; GATE-COST-1 P2): the whole batch holds every VRAM
        # token once and each run's admissions are nested on it, so no sibling lane's GPU row runs beside
        # any of them — and the card drains once, not once per run. A WHOLE-CARD HOLD TAKES THE GATE SLOT
        # (GATE-COST-2): the batch queues FIFO with the gates before it drains (hold_card does it), so its drain
        # never starves the gate in the slot into NOADMIT.
        gate_runlog.on_signals()
        card, env, fallback = solo_card(f"{'+'.join(lane)} --solo {' '.join(a.solo)[:80]}")
        try:
            for s in a.solo:
                for _ in range(a.times):
                    rx = "^" + re.escape(s) + "$"
                    r = lost(gate_runlog.run_ctest(
                        f"ctest -j1 --timeout 900 --output-on-failure --no-tests=error -R '{rx}'",
                        build, a.tier or "solo", lane, 1, reasons={s: "solo retry"},
                        rng=log_range, retry=True, env=env, whole_card=False, fallback=fallback))
                    rc = rc or r
        finally:
            gate_runlog._vram().release(card)
        sys.exit(rc)
    if not (a.range or a.files):
        ap.error("give a range (base..tip) or --files")
    paths = a.files if a.files else touched_paths(a.range)
    own_rng, incoming, fwd = scope_range(a.range) if a.range and not a.files else (a.range, None, [])
    if fwd and not a.json:
        # THE LANE IS GATED ON ITS OWN DIFF (T1): what the forward merges carried in is the
        # siblings' change, gated in their own batch — the combination is the batch gate's.
        short = lambda r: "..".join(x[:9] for x in r.split(".."))
        print(f"gate-scope: {len(fwd)} forward merge(s) in {a.range} — the lane's OWN change is "
              f"{short(own_rng)} (from the newest one's second parent {fwd[0][1][:9]})")
        if incoming:
            print(f"  what came in through them ({short(incoming)}) is NOT this gate's: the combination is the batch "
                  f"gate's (scripts/lead/merge-dbuild-lane.sh batch)")
    if fwd:
        log_range = "..".join(x[:9] for x in own_rng.split(".."))

    costs = load_costs()
    S = select(paths, a.range if not a.files else None, build, a.jobs)
    inv, selected = S.inv, S.selected

    # The quiet-box measurements never ride a scoped gate (see STAGE_CLOSE_LABELS). A `stage-close`
    # row without `quiet-box` is here only through its own subject (select(): stage_close_subjects).
    quiet = [n for n in selected if inv[n]["labels"] & SCOPE_EXCLUDED_LABELS]
    for n in quiet: selected.pop(n)
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
    timing = [n for n in names if TIMING_LABEL in inv[n]["labels"]]
    par_names = [n for n in names if n not in timing]
    serial = sum(cost(n) for n in par_names if inv[n]["serial"])
    timing_s = sum(cost(n) for n in timing)
    wall = max((est - timing_s) / float(a.jobs), serial) + timing_s + 5
    tier_rows = [n for n, t in inv.items() if not (t["labels"] & (STAGE_CLOSE_LABELS | TARGET_LABELS))]
    tier_est = sum(costs.get(n, 10.0) for n in tier_rows)

    def ctest_for(suites, jobs):
        rx = "^(" + "|".join(re.escape(n) for n in suites) + ")$"
        return f"ctest -j{jobs} --timeout 120 --output-on-failure --no-tests=error -R '{rx}'"

    whole_tier = bool(S.fallback or S.full_tier)
    cmd = ctest_for(par_names, a.jobs) if par_names else ""
    if cmd and pool_env:
        cmd = f"JAH_POOL_ARMS='{pool_env}' {cmd}"
    timing_cmd = ctest_for(timing, 1) if timing else ""      # the serial phase (W1)
    target_cmd = ctest_for(targets, 1) if targets else ""

    if a.json:
        print(json.dumps({"paths": paths, "suites": names, "targets": targets, "fallback": S.fallback,
                          "full_tier": S.full_tier, "fork_bump": S.fork_bump, "reasons": {n: selected[n] for n in names},
                          "own_range": own_rng, "incoming": incoming, "forward_merges": [m for m, _ in fwd],
                          "arms": {r: {arm: S.arms[r][arm] for arm in subsets[r]} for r in subsets},
                          "pool_arms_env": pool_env,
                          "rationale": [{"path": p, "why": w} for p, w in S.rationale],
                          "graph": S.graph is not None, "tooling_only": S.tooling_only,
                          "estimated_seconds": est, "estimated_wall": wall,
                          "tier_rows": len(tier_rows), "tier_estimated_seconds": tier_est,
                          "command": merge_tier(a.jobs) if whole_tier else cmd,
                          "serial_command": merge_tier_serial() if whole_tier else timing_cmd,
                          "target_command": target_cmd}, indent=1))
        return
    print(f"gate-scope: {len(paths)} touched path(s)"
          + ("" if S.graph else "   [NO BUILD GRAPH: compiled rows by directory rules]"))
    for p, why in S.rationale: print(f"  {p}\n      -> {why}")
    if S.tooling_only:
        print("\nTOOLING-ONLY DIFF (TESTING_V3 §1.3.4): no touched path is a product input (gate-scope.py PRODUCT_INPUT) "
              "— the hygiene label + the tools' rows + the rows whose declaration or command changed; no product row, "
              "no smoke pair")

    def run_both(tier_name):
        """The MERGE tier, both phases (parallel, then the timing rows serial); the worse exit code."""
        labels = {n: t["labels"] for n, t in inv.items()}
        gate(tier_name); skip = done_rows()
        r1 = lost(gate_runlog.run_ctest(merge_tier(a.jobs), build, tier_name, lane, a.jobs, reasons={}, rng=log_range,
                                        labels=labels, exclude=skip))
        print("\n=== the timing phase (serial, after the parallel phase; the whole card held once) ===")
        r2 = lost(gate_runlog.run_ctest(merge_tier_serial(), build, tier_name, lane, 1, reasons={}, rng=log_range,
                                        labels=labels, exclude=skip, whole_card=True))
        r3 = owed_solo_pass(labels)
        gate_runlog.trend_at_gate_end(tier=tier_name, lane=lane)
        return r1 or r2 or r3

    def run_tier(reason):
        print(f"\n{merge_tier(a.jobs)}\n{merge_tier_serial()}")
        if a.run:
            # a SCOPED gate that ran the whole tier is logged as such — "scoped-fallback" (no rule,
            # no symbol, no graph owner) or "scoped-tier" (the tier by rule: a root setting, an unreadable
            # fork diff) — so the
            # run log tells it from the lead's MERGE tier runs
            sys.exit(run_both(a.tier or ("scoped-fallback" if reason == "fallback" else "scoped-tier")))
    if S.fork_bump:
        fb = S.fork_bump
        print(f"\nTHE FORK PIN MOVED ({(fb['old'] or '?')[:9]} -> {(fb['new'] or '?')[:9]}): this gate selects by the "
              f"fork diff's reach;\n  §7b rule 4's full tier runs ONCE per bump, at the merge into d-build:\n"
              f"  scripts/gate-scope.sh {a.range} --run --fork-tier   (ci_gate_check refuses the merge without it)")
        if a.fork_tier:
            print(f"\n{merge_tier(a.jobs)}\n{merge_tier_serial()}")
            if a.run:
                sys.exit(run_both(a.tier or "fork"))
            return
    elif a.fork_tier:
        sys.stderr.write("gate-scope: --fork-tier on a range that does not move the fork pin — nothing to do\n")
        sys.exit(2)
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
    if quiet: print(f"\n(quiet-box measurements left out — the stage-close batch's: {sorted(quiet)})")
    if S.stage_close_left:
        print(f"\n(stage-close rows a broad rule reached, left for the stage-close batch — no subject rule "
              f"names them and their own test dir did not move: {sorted(S.stage_close_left)})")
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
        if timing_cmd: print(f"{timing_cmd}    # the timing rows, serial, after it ({len(timing)}, ~{timing_s:.0f} s)")
    else:
        print("\nSCOPED tier: every selected suite is a TARGET test — this change gates on nothing "
              "of its own, and the targets below still run and report.")
    if target_cmd: print(f"\n{target_cmd}    # target tests: reported, NOT gating")
    if a.run and a.targets_only:
        # THE TARGET STEP (GATE-SPEED-1 item 2): the targets' own ctest, -j1, its own run-log tier. A
        # report: its exit code is printed and the step exits 0.
        if not target_cmd:
            print("\n=== target tests: none selected ==="); return
        labels = {n: t["labels"] for n, t in inv.items()}
        run_target_step(target_cmd, build, lane, log_range, labels, selected_targets, exclude=(gate("targets"), done_rows())[1])
        # F8: --targets-only --resume owes the dropped reds their solos too (the targets themselves never gate)
        src = owed_solo_pass(labels)
        gate_runlog.trend_at_gate_end(tier="target", lane=lane)
        if src:
            sys.exit(src)
        return
    if a.run:
        labels = {n: t["labels"] for n, t in inv.items()}
        reasons = dict(selected)
        for r in subsets:
            for arm in subsets[r]: reasons[f"{r}::{arm}"] = S.arms[r][arm]
        env = dict(os.environ, JAH_POOL_ARMS=pool_env) if pool_env else None
        gate(a.tier or "scoped"); skip = done_rows()
        rc = lost(gate_runlog.run_ctest(cmd.split(" ", 1)[1] if pool_env else cmd, build, a.tier or "scoped", lane,
                                        a.jobs, reasons=reasons, rng=log_range, labels=labels, env=env,
                                        exclude=skip)) if cmd else 0
        if timing_cmd:
            print("\n=== the timing phase: %d row(s), serial, the whole card held once ===" % len(timing))
            rc = lost(gate_runlog.run_ctest(timing_cmd, build, a.tier or "scoped", lane, 1, reasons=reasons,
                                            rng=log_range, labels=labels, exclude=skip, whole_card=True)) or rc
        rc = owed_solo_pass(labels) or rc
        # THE VERDICT IS THE GATING PHASES' (GATE-SPEED-1 item 2): printed, and every gating record
        # written, before any target runs; the exit code is this one whatever the targets read. THE
        # TARGETS RUN AFTER IT, INSIDE THE GATE (GATE-COST-1 P9): on the gate's display, under its slot,
        # on one whole-card hold — the lane reads its verdict from the line below, not from the exit.
        print("\n=== GATE VERDICT: %s (exit %d) — the gating phases only ===" % ("GREEN" if rc == 0 else "RED", rc))
        sys.stdout.flush()
        if target_cmd and not a.no_targets:
            run_target_step(target_cmd, build, lane, log_range, labels, selected_targets, exclude=skip)
        gate_runlog.trend_at_gate_end(tier=a.tier or "scoped", lane=lane)
        sys.exit(rc)


if __name__ == "__main__":
    try:
        main()
    except gate_runlog.GateSignal as e:
        # F2: a gate told to stop — every run below has stopped its ctest tree and released its card on the
        # way here; the slot goes with this process
        print(f"\n=== GATE ABORTED: signal {e.sig} — the ctest tree was stopped, the slot and the card released ===",
              flush=True)
        sys.exit(128 + e.sig)
    except gate_graph.GraphError as e:
        # H1: an unreadable graph is a refusal, never an empty (green) selection
        sys.stderr.write(f"gate-scope: REFUSED — {e}. Install binutils (nm) or fix the build dir; "
                         f"a selection from an unreadable graph would under-select silently.\n")
        sys.exit(4)
