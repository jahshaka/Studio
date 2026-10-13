#!/usr/bin/env python3
"""gate-scope — the SCOPED tier: what a change CAN REACH, and why (MODULAR-GATE-1).

    scripts/gate-scope.sh <base>..<tip> [--build build-linux] [--run [--no-targets | --targets-only]] [--json] [-j N]
    scripts/gate-scope.sh <base>..<tip> --resume [...]          # --run, only the rows with no record at the tip
    scripts/gate-scope.sh --files path [path ...]
    scripts/gate-scope.sh --solo <suite> [...] [--times 3]     # the flake protocol, logged
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
(scripts/vram_tokens.py; by class — a gate before a measurement —, no bound, its position printed) and holds it to its last process;
`--solo` takes it too — a whole-card hold always does (GATE-COST-2) — and a per-row
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
starts at the merge's d-build parent (own_base), and what came in is its own merge's.

THE MERGE RUN (RUNNER-A6; docs/TESTING_GATE.md §3c): `runner.sh select` asks this tool's selection (through
ci_gate_check --plan) for what a change reaches; `runner.sh merge` runs it as one queue job and attributes a red itself.
Tiers are contracts: the selection printed here is what runs, nothing
hand-picked out of it; a red a later tier finds that this selection missed is a defect of
this tool, fixed here and added to tests/hygiene/gate_selection_cases.json.
"""
import argparse, json, os, re, subprocess, sys, collections, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gate_graph                                   # noqa: E402  the build graph, symbols, CMake
import gate_runlog                                  # noqa: E402  THE RUN LOG (T8)
from gate_graph import CXX_EXT, HEADER_EXT          # noqa: E402

# JAH_SCOPE_ROOT (round 15): another tree's selection by THIS selector — a replay of a lane's range over its own build
# (the lane's tree and build, this file's rules); never set by a gate
ROOT = os.environ.get("JAH_SCOPE_ROOT") or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

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
# THE SUBSYSTEM TABLE (TESTING-SPEED-A item 4; SPECS/TESTING_SPEED_SPEC.md §2 A4) — THE ONLY SELECTOR OF ENGINE CODE.
# The engine's facade pulls EVERY engine object into all 252 engine-linking executables (nm on rc-base: OgreChain /
# OgrePhotonView / OgreSky / OgreParticles / OgreVrSession / OgreEngine symbols in 252/252), so the build graph cannot
# narrow an engine TU and must not widen it: an irisgl/engine/src/** file or an engine media file is selected by THIS
# table — the FIRST line that matches the path under irisgl/engine/ (the lines run specific -> generic: a GI shader in
# media/Hlms/Jahshaka is the gi line's, never the generic Jahshaka medium's picture set).
# A TOKEN is a test DIRECTORY (tests/<token>/) or a ROW PREFIX (`<token>.*` rows anywhere: cloud_2d.* in tests/gi,
# decals.engine in tests/engine; a DOTTED token is a name prefix: `engine.atom_`), every kind of row; two special tokens:
#   *engine-fixtures — every compiled row whose executable links the engine archive (the frame loop's reach)
#   *picture         — THE PICTURE SET (picture_set()): the kept rows of hdr ssr looks mirror and the app rows /
#                      pool arms whose script takes a screenshot (editor.screenshot, vr.eyeScreenshot, screenshot()
#   *screenshots     — the picture set's SCRIPT part alone (the screenshot app rows and arms, not the families)
# THE CORE HAS NO "RUN EVERYTHING" RULE (the owner 2026-10-12 17:2x): the shared headers (EnginePrivate.h, Types.h,
# engine/include/**) go through header_symbols (the identifiers changed -> the TUs that use them -> their lines here);
# OgreEngine.cpp goes through OGRE_ENGINE_FUNCTIONS by the enclosing function of each changed line. THE WHOLE-ENGINE
# GUARD at every merge is the six hash lines.
ENGINE_SUBSYSTEMS = [
    # the post chain: bloom, the display encode, the HDR meter, the screen-space passes it composes
    # (+ the exposure picture and the visibility buffer's draw — the chain composes both: the suites audit's S2 subjects,
    # defaults.exposure_plane and engine.atom_draw/parity, gate_selection_cases.json "S2 the chain")
    (r"media/Hlms/Jahshaka/Jah(Bloom|DisplayEncode|HdrMeter|Srgb|Screen)|src/OgreChain\.cpp$",
     ["hdr", "ssr", "looks", "*picture", "defaults", "engine.atom_draw", "engine.atom_parity"],
     "the post chain (bloom, encode, meter)"),
    # THE SCENE'S ONE LIGHT LIST (P1C-LIGHT-LIST; round 21): every world-lighting consumer reads its lights from it — the
    # voxels' light injection and the cards' relight (gi), Atom's hit decode (atom); the light rows and scale.lights
    (r"SceneLightList", ["lights", "scale.lights", "gi", "atom"], "the scene's one light list"),
    # Photon: the voxel chain, the cascades, the gather, the cards, the probes, the irradiance field
    # (+ the screenshot rows: the GI lights every pixel a picture row compares — CONTACT-OCCLUSION-1's GI change went red on
    # pool.world_sky's clouds_2d arm and atom.occlusion_exact_app, gate_selection_cases.json; the hdr/ssr/looks fixtures,
    # the picture set's families, are not the GI's)
    (r"^media/Photon/|^src/photon/|Photon|Voxel|Cascade|Gather|Card|Probe|Irradiance|Ifd|Vct|EnvironmentCones|"
     r"^src/OgreGi\.cpp$|SurfaceCache|JahshakaIbl|JahshakaPcc", ["gi", "*screenshots"],
     "Photon (voxels, cascades, gather, cards, probes)"),
    # Atom: the visibility buffer, the GPU scene, the atom passes and their media
    (r"Atom|GpuScene|^media/Hlms/Atom/|^src/atomdraw/", ["atom", "scale", "shadow", "engine.atom_"],
     "Atom (the visibility buffer; engine.atom_* are its decode's own fixtures)"),
    # shadows: the shadow node, the casters, the sun contact
    (r"Shadow|SunContact|rq_sun_contact", ["shadow", "lights"], "shadows"),
    (r"src/OgreLights\.cpp$", ["lights", "shadow"], "the lights"),
    # sky: the atmosphere, the clouds, the sun disc, the sky capture, height fog
    (r"Sky|Atmosphere|Atmo|Cloud|SunDisc|Fog", ["sky", "cloud_2d"], "the sky (atmosphere, clouds, sun disc, fog)"),
    (r"Particle", ["particles"], "particles"),
    # reflections: planar, SSR, the ray-traced reflection
    (r"Planar|Reflect|Ssr|rq_reflect", ["ssr", "rtreflect", "mirror", "planar"], "reflections (planar, SSR, RT)"),
    (r"Vr|OpenXr", ["vr"], "VR"),
    (r"Skelet|Skin|Anim", ["skeletal", "avatar"], "skinning and animation"),
    (r"Decal|Distortion|Look|Hdr|Bloom|Dither", ["hdr", "looks", "distortion", "decals"], "the looks and decals"),
    (r"Mirror", ["mirror", "pieces"], "the mirror"),
    # culling and LOD: the GPU cull, the HZB, the cluster cut, the level rule
    (r"Lod|Cull|Hzb|ClusterCut|LevelRule", ["engine", "atom", "scale"], "culling and LOD"),
    # the ray-query core every ray user shares
    (r"^src/rayquery/|RayQuery", ["gi", "rtreflect", "shadow"], "the ray-query core"),
    (r"ShaderCache", ["shadercache"], "the shader cache"),
    (r"TextureCache", ["engine", "assets", "thumbnails", "openasync"], "the texture cache"),
    (r"FrameMonitor|OverlayHud", ["perf", "engine"], "the frame monitor"),
    (r"LogBridge", ["log"], "the log bridge"),
    (r"Sockets", ["sockets"], "sockets"),
    (r"VideoReadback|VideoNv12", ["capture", "engine"], "video readback"),
    (r"Compute", ["compute"], "the compute jobs"),
    (r"Grid|Clips", ["engine", "gizmo"], "the grid and clip planes"),
    (r"src/OgreMaterials\.cpp$|src/OgreMesh\.cpp$", ["engine", "pieces", "materialpreview", "atom", "*picture"],
     "materials and meshes (every lit pixel)"),
    (r"VkDescriptorPools|src/EnginePrivate\.h$", ["*engine-fixtures", "*picture"],
     "the Vulkan descriptor pools / the private header's includers (every engine fixture)"),
    (r"^media/Hlms/Jahshaka/", ["*picture"], "a Jahshaka Hlms medium (the picture set)"),
    (r"^CMakeLists\.txt$", [], "the engine's CMake: read by what its changed commands name"),
    # the engine DIRECTORY as a whole (a CMake setting of the engine target its commands cannot narrow, reached through
    # the fallback's directory walk): what links the engine
    (r"^(src/)?$", ["*engine-fixtures"], "the engine directory as a whole"),
]

# A JAHSHAKA HLMS PIECE reaches Atom besides its first line (HlmsAtom compiles the Jahshaka Hlms library; fix round item 5)
PIECE_RX = r"^media/Hlms/.*_piece"
PIECE_TOKENS = ["atom", "engine.atom_"]

# OgreEngine.cpp BY THE FUNCTION CHANGED (TESTING-SPEED-A item 4): gate_graph.enclosing_functions names the TOP-LEVEL
# function (or class) of each changed line — out of lambdas and local structs, `init` returning `init` — and this map
# says what it reaches. EVERY function of OgreEngine.cpp is named (the lead reads the map at the read); a name the map
# does not know, or a line outside every function (an include, a namespace-scope constant), reaches every engine
# fixture + the picture set + every rendering app row (the frame loop's own reach), printed as such.
_ALL = ["*engine-fixtures", "*picture", "*vulkan-scripts"]   # everything that renders: the fixtures, the app rows
OGRE_ENGINE_FUNCTIONS = {
    # the frame loop, present, the window, init, the views and scenes, the Hlms registration: everything that renders
    **{f: _ALL for f in ("init", "renderOneFrame", "closeRenderFrame", "frameCloseStep", "createView",
                         "createOffscreenView", "createOffscreen", "createStereoPairView", "destroyView", "createScene",
                         "destroyScene", "drainPendingSceneDestroys", "scenesFeedingEnabledViews", "updateScene",
                         "advanceResources", "hasEnabledViews", "listViews", "setVsync", "ensureHlms",
                         "registerCommonMaterials", "tellEveryHlms", "create", "OgreEngine", "~OgreEngine",
                         "setFixedFrameDelta", "fixedFrameDelta", "documentGraphScene", "blankSceneManager",
                         "viewNameTaken", "setTransformWriteCounter", "computeDeferralsAtFrameStart",
                         "asyncViewCount", "noteAsyncView", "isAlive")},
    # the device's faults: loss, OOM, the injected frame faults (tests/engine's fault rows)
    **{f: ["engine", "threading"] for f in ("latchDeviceLost", "latchOutOfMemoryInFrame", "deviceLost",
                                            "raiseFrameFault", "setFrameFault", "InRecordFaultListener", "gpuFault",
                                            "vulkanOomCount", "vulkanLastOom", "lastError", "takeLastError")},
    # the shadow nodes
    **{f: ["shadow", "lights"] for f in ("createShadowNode", "setShadowMeshOptimization", "shadowMeshOptimization")},
    # the VR session
    **{f: ["vr"] for f in ("vrAvailable", "vrConnect", "vrProbe", "beginVrSession", "endVrSession",
                           "endLostOrStoppedVrSession", "vrState", "vrStatus", "vrEyeScreenshot", "setVrMirrorView",
                           "vrInjectInput", "vrHandJoints", "vrBindingBlocks", "vrInjectJoints", "vrHaptic",
                           "setVrOrigin", "vrView")},
    # texture streaming and the texture cache
    **{f: ["engine", "assets", "thumbnails", "openasync"] for f in (
        "texturesDoneStreaming", "collectTextureStreaming", "drainTextureStreaming", "waitForTextureLoads",
        "textureLoadRequests", "textureMetadataCacheEntries", "textureChannelCacheEntries", "saveTextureCache",
        "textureManagerOf", "countPendingTextures", "textureMultiLoadThreads", "textureWaitAdvances")},
    # the shader build: async compiles, the shader cache
    **{f: ["shadercache", "threading"] for f in (
        "startAsyncShaders", "setAsyncShaderThreads", "setAsyncShaderPlaceholdersOnly", "asyncShaderStats",
        "waitForAsyncShaders", "shaderCacheStats", "saveShaderCache", "flushShaderCache", "clearShaderCache",
        "setCompileObserver", "shaderBuildProgress")},
    # the statistics the monitor and the perf rows read
    **{f: ["perf", "threading", "engine"] for f in ("renderStats", "textureMemory", "memoryStats", "reclaimMemory",
                                                    "objectCounts", "threading")},
    # the validation layer's report
    **{f: ["engine"] for f in ("validation", "namesValidation", "collectLayerLibrary")},
}

# THE OTHER TWO CORE TUs BY THE FUNCTION CHANGED (TESTING-SPEED-A item 4, the same walk as OgreEngine.cpp): the scene and
# the view hold a hundred functions each, most of them one subsystem's (setNodeCastShadow is the shadows', updateGi the
# GI's) — the enclosing function's NAME is read against these keywords (every matching line adds its tokens); a function
# no keyword names, or a line at namespace scope, reaches everything that renders (_ALL), said.
CORE_BY_FUNCTION = ("src/OgreScene.cpp", "src/OgreView.cpp")
_BY_FUNCTION = {"irisgl/engine/src/OgreEngine.cpp"} | {"irisgl/engine/" + f for f in CORE_BY_FUNCTION}
# OgreScene.cpp AND OgreView.cpp BY THE FUNCTION CHANGED (TESTING-SPEED-A item 4; fix round item 11): every top-level
# function of each file is NAMED (as OgreEngine.cpp's are — the lead reads the maps at the read); a name a map does not
# know, or a namespace-scope line, reaches every engine fixture + the picture set (_CORE: the scene and the view are what
# every engine fixture and picture row draw through) — NOT the app rows (*vulkan-scripts: the frame loop's own reach).
_CORE = ["*engine-fixtures", "*picture"]
_CHAIN = ["hdr", "looks", "ssr", "defaults", "*picture"]
_REFL = ["ssr", "rtreflect", "mirror", "planar"]
_NODES = ["engine", "mirror", "pieces", "atom"]
OGRE_SCENE_FUNCTIONS = {
    **{f: _CORE for f in ("OgreScene", "~OgreScene", "destroy", "sceneManager", "nativeSceneManager")},
    "name": ["engine"],
    **{f: ["lights", "shadow"] for f in ("setShadowSettings", "applyNodeCasterBit", "setNodeLightMask", "nodeLightMask",
                                         "setNodeCastShadow", "nodeCastShadow", "setLight", "removeLight", "ogreLight",
                                         "nodeOfLight")},
    **{f: ["lights", "gi", "*picture"] for f in ("setAmbient", "setAmbientSh", "applyAmbientSh")},
    **{f: ["gi", "sky", "lights"] for f in ("applyVctEnvironment", "applyCascadeEnvironment", "rayEnvironment",
                                            "noteEnvironmentChanged", "setEnvironmentLight")},
    **{f: _REFL for f in ("refreshEnvmapScale", "hasAuthoredReflectionMap", "envmapScaleForPass")},
    **{f: _NODES for f in ("removeNode", "createNode", "adoptNode", "setNodeParent", "setNodeTransform", "registryNode",
                           "releaseNode", "node", "track", "nodeWorldPose", "itemVisibilityFlags",
                           "applyNodeVisibilityFlags", "inheritedShown", "applyShownSubtree", "setNodeVisible",
                           "setNodeVisibleUnder", "setNodeVisibleImpl")},
    "particleVisibilityBits": ["particles", "engine"],
    **{f: ["gi", "atom", "engine"] for f in ("writeIsSceneMovement", "setNodeMovable", "nodeMovable", "mobilityStatus")},
    **{f: ["engine", "gizmo"] for f in ("setNodeHelper", "nodeHelper", "setNodeBackdrop", "nodeBackdrop")},
    **{f: ["vr"] for f in ("setNodeVrHelper", "nodeVrHelper", "setVrProxyNodes", "vrProxyNodes", "setVrRayNodes",
                           "vrRayNodes", "setVrHandBoneNodes", "vrHandBoneNodes")},
    **{f: ["engine", "pieces", "perf"] for f in ("detachItem", "itemCount", "addObjectCounts")},
    "dropSkeletonFollowers": ["skeletal", "avatar"],
    **{f: ["gi", "rtreflect", "shadow"] for f in ("rayTracingResolved", "setRayTracing")},
    **{f: ["gi"] for f in ("walkCardLights", "updateSurfaceCache", "cardEligible", "cardColumnKey", "readCardTexel",
                           "readCardAt", "dumpCardAtlas")},
    "gpuSlotChanged": ["atom", "engine", "scale"],
    "query": ["engine", "picking"],
}
OGRE_VIEW_FUNCTIONS = {
    **{f: _CORE for f in ("OgreView", "~OgreView", "setScene", "detachScene", "attachWorkspace", "detachWorkspace",
                          "rebuildWorkspaceDef", "rebuildDetachedWorkspaceDef", "destroy", "createRtt", "passPreExecute",
                          "workspacePreUpdate", "workspacePosUpdate", "syncGlobalsListener", "applyPendingResize",
                          "applyPendingResizeImpl", "resize", "rebuildRtt", "stallDevice", "setEnabled", "setCamera",
                          "setCameraNode", "sanitizeSamples", "setSampleCount", "setOffscreenContract", "setBackground",
                          "readPixels", "readPixelsHdr")},
    **{f: ["engine"] for f in ("name", "scene", "width", "height", "isOffscreen", "isEnabled", "sampleCount", "target",
                               "targetSamples", "targetTexture", "viewAspect", "background", "cameraWorldPosition",
                               "addWorkspaceListener", "removeWorkspaceListener", "workspaceGeneration",
                               "attachBlankWorkspace", "detachBlankWorkspace")},
    **{f: _CHAIN for f in ("orthoPostFxAdapted", "chainDesc", "writeLiveClearColour", "applyFixedExposure",
                           "pipTonemapEffective", "pipAllowed", "setPip", "pip", "syncPip", "pipGeneration",
                           "pipTexFactors", "destroyPip", "applyLetterboxAndPip", "applyPip", "setPostFx", "postFx",
                           "resetExposureHistory", "seedExposureHistory", "measuredExposureScale", "destroyBlankChain",
                           "applyLensShift", "applyLetterbox", "readChainTexture", "chainLocal", "chainTexture")},
    **{f: ["atom", "engine", "scale"] for f in ("hzbLevelsFor", "finishAtomOcclusion", "setLodHysteresisOffscreen",
                                                "incompleteDraws", "sceneShapeMoved")},
    **{f: ["engine", "gizmo"] for f in ("overlaysAllowed", "setOverlay", "overlay", "setHelpersVisible")},
    **{f: ["vr"] for f in ("setVrHelpersVisible", "setStereo", "setHiddenAreaMask")},
    "setVrSsrOverride": ["vr"] + _REFL,
    **{f: ["engine", "perf"] for f in ("framesPresented", "notePresented", "blankFramesPresented")},
    **{f: ["shadow", "lights"] for f in ("shadows", "setShadows", "dropWorkspaceForShadowRebuild",
                                         "recreateWorkspaceAfterShadowRebuild", "shadowNodeInstance")},
    **{f: _REFL for f in ("readReflectionHdr", "readSsrHistoryHdr")},
    "updateParticles": ["particles"],
    "updateGi": ["gi"],
    **{f: ["shadercache", "threading"] for f in ("setAsyncShaders", "asyncShaders", "computeHlms", "warmUpShaders")},
    **{f: ["capture", "engine"] for f in ("requestFrameCapture", "excludeFromCapture", "capturesWith",
                                          "ensureCaptureWorkspace", "beginFrameCapture", "downloadLayer0", "mapTicket",
                                          "recordFrameCapture", "frameCaptureState", "takeFrameCapture",
                                          "destroyCaptureWorkspace", "releaseFrameCapture")},
}
# A FUNCTION THE MAPS DO NOT NAME (a NEW function, round 15): its NAME's keywords, first match — only a name no keyword
# reads falls to _CORE / _ALL
FUNCTION_NAME_KEYWORDS = [
    (r"Ssr|Reflect|Planar|Mirror", _REFL),
    (r"Shadow|Caster", ["shadow", "lights"]),
    (r"Gi|Card|Probe|Voxel|Cascade|Gather|Photon|Irradiance|Ddgi|Vct", ["gi"]),
    (r"Atom|Lod|Hzb|Occlusion|Cluster", ["atom", "engine", "scale"]),
    (r"Bloom|Exposure|Tonemap|Chain|Pip|Letterbox|PostFx|Lut|Grade", _CHAIN),
    (r"Vr|Stereo|HiddenArea", ["vr"]),
    (r"Capture", ["capture", "engine"]),
    (r"Particle", ["particles"]),
    (r"Light|Ambient|Sun", ["lights", "*picture"]),
    (r"Sky|Atmosphere|Environment", ["sky", "gi"]),
    (r"Shader|Hlms|Pso", ["shadercache", "threading"]),
]


def function_keywords(fn):
    """(tokens, keyword) of a function name by FUNCTION_NAME_KEYWORDS (camel-case words, case-sensitive on the capital),
    or None."""
    name = fn.split("::")[-1]
    name = name[:1].upper() + name[1:]
    for rx, toks in FUNCTION_NAME_KEYWORDS:
        m = re.search(r"(?:" + rx + r")(?![a-z])", name)
        if m:
            return toks, m.group(0)
    return None


CORE_FUNCTION_MAPS = {"irisgl/engine/src/OgreScene.cpp": OGRE_SCENE_FUNCTIONS,
                      "irisgl/engine/src/OgreView.cpp": OGRE_VIEW_FUNCTIONS}

# THE FORK BY COMPONENT (TESTING-SPEED-A item 4): a pin move selects by what the FORK DIFF's files reach — each changed
# fork file takes the FIRST line that matches it (specific -> generic). Every top-level directory of
# irisgl/thirdparty/ogre-next is named; a fork file no line names is the MERGE tier (said).
FORK_COMPONENTS = [
    # what the engine never builds or stages: the other render systems, the upstream samples, docs, tools, tests
    (r"^(Docs/|Scripts/|Tools/|Tests/|Other/|build/|\.github/|\.circleci/|.*\.md$|LICENSE|README|AUTHORS|BUGS|COPYING|"
     r"appveyor\.yml|Samples/2\.0/|Samples/CMakeLists\.txt|RenderSystems/(Direct3D11|GL3Plus|GLES2|Metal)/|"
     r".*\.(hlsl|metal)$)", "none"),
    (r"^Samples/Media/(VCT|Compute/Algorithms/IrradianceFields|Compute/Tools)/", ["gi"]),
    (r"^Samples/Media/Compute/Algorithms/IBL/", ["gi", "sky"]),
    # (+ atom / engine.atom_: HlmsAtom compiles the Pbs/Common library too — fix round item 5, audit F2)
    (r"^Samples/Media/Hlms/(Common|Pbs)/", ["lights", "pieces", "materialpreview", "looks", "hdr", "*picture", "atom",
                                            "engine.atom_"]),
    (r"^Samples/Media/Hlms/Unlit/", ["engine", "*picture"]),
    (r"^Samples/Media/2\.0/scripts/materials/(Common|HDR|Tutorial_SSAO|Tutorial_SMAA)/", ["hdr", "looks", "*picture"]),
    (r"^Samples/Media/packs/DebugPack\.zip$", ["engine"]),
    (r"^Samples/Media/", "none"),
    (r"^OgreMain/(src|include)/(OgreLod|.*/OgreLod)|^Components/MeshLodGenerator/", ["engine", "atom", "scale"]),
    (r"^OgreMain/(src|include)/.*(Shadow)", ["shadow", "lights"]),
    (r"^OgreMain/(src|include)/.*(ParticleSystem|Particle)", ["particles"]),
    (r"^OgreMain/(src|include)/(Animation/|.*Skelet)", ["skeletal", "avatar"]),
    (r"^OgreMain/(src|include)/Compositor/", ["*engine-fixtures", "*picture"]),
    # (round 15: the fork's components the reflect-fix-1 pin move named — never "links Ogre → everything")
    # buffer READ-BACK (BufferPacked::readRequest, the async tickets, the staging buffers): the rows that read GPU
    # buffers back — the GI's readouts, Atom's counters, the frame monitor, the capture
    (r"^OgreMain/(src|include)/Vao/(OgreBufferPacked|OgreAsyncTicket|OgreStagingBuffer|OgreVaoManager)|"
     r"^RenderSystems/(Vulkan|NULL)/(src|include)/Vao/(Ogre\w*(AsyncTicket|StagingBuffer|VaoManager))",
     ["gi", "atom", "perf", "capture", "engine"]),
    # the node / object memory managers (live counts, pool compaction): the scene and the mirror
    (r"^OgreMain/(src|include)/(.*/)?Ogre(Array|Node|Object|Bone)MemoryManager", ["mirror", "engine", "scale", "pieces"]),
    # the Hlms core's per-draw render state (a depth write, a blend, a cull): every picture + the depth/reflection rows
    (r"^OgreMain/(src|include)/OgreHlms(\.cpp|\.h|Datablock)", ["*picture", "ssr", "rtreflect", "mirror", "planar",
                                                               "shadow", "atom", "materialpreview"]),
    (r"^OgreMain/", ["*engine-fixtures"]),
    (r"^Components/Hlms/Pbs/(src|include)/(Vct|IrradianceField)", ["gi"]),
    (r"^Components/Hlms/(Pbs|Common)/", ["lights", "pieces", "materialpreview", "looks", "hdr", "*picture", "atom",
                                         "engine.atom_"]),
    (r"^Components/Hlms/Unlit/", ["engine", "*picture"]),
    (r"^Components/Atmosphere/", ["sky"]),
    (r"^Components/PlanarReflections/", ["ssr", "rtreflect", "mirror", "planar"]),
    (r"^Components/(Overlay|Imgui|Paging|Property|SceneFormat|Volume)/", ["engine"]),
    (r"^Components/CMakeLists\.txt$", ["*engine-fixtures"]),
    (r"^PlugIns/ParticleFX", ["particles"]),
    (r"^PlugIns/", ["engine"]),
    # the Vulkan render system's pass / attachment state (a depth attachment, a render pass): every picture + depth/reflection
    (r"^RenderSystems/Vulkan/(src|include)/Ogre(VulkanRenderSystem|VulkanRenderPassDescriptor)",
     ["*picture", "ssr", "rtreflect", "mirror", "planar", "shadow", "atom"]),
    (r"^RenderSystems/(Vulkan|NULL)/|^RenderSystems/CMakeLists\.txt$", ["*engine-fixtures"]),
    (r"^(CMake/|CMakeLists\.txt$|Dependencies)", ["*engine-fixtures"]),
]


AREA_RULES = [
    # --- engine: THE SUBSYSTEM TABLE selects irisgl/engine/** (ENGINE_SUBSYSTEMS above; Selection.engine_path), the fork
    # by component (FORK_COMPONENTS; Selection.fork). What builds or links Ogre reaches every engine fixture:
    (r"^irisgl/scripts/(build|prune)-ogre", ["*engine-fixtures"], []),
    (r"^cmake/IncludeOgre\.cmake$", ["*engine-fixtures"], []),
    (r"^irisgl/mirror/",
     ["mirror", "skeletal", "sockets", "cameras", "gizmo", "player", "thumbnails",
      "materialpreview", "samples", "picking", "particles",
      # THE MIRROR PUSHES EVERY ENVIRONMENT DESC EACH FRAME (GATE-SCOPE-2, 2026-09-17): the
      # GI/planar/shadow/post/sky/fog/ray settings and their staleness machine live in
      # scenemirror.cpp, so a mirror edit can move any of these pictures.
      "gi", "rtreflect", "lights", "looks", "planar", "ssr", "shadow", "hdr", "sky", "distortion", "vr",
      # (TESTING-SPEED-A) every app row (*vulkan-scripts) is reached ONLY by the frame loop's paths — the mirror's
      # pictures are the PICTURE set's
      "*picture"], []),
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
    # The media-staging CMake (WrapHlmsPiece: plain GLSL wrapped into Hlms pieces) changes what every engine shader
    # SEES: every engine fixture and the picture set (never the whole tier).
    (r"^irisgl/cmake/WrapHlmsPiece", ["*engine-fixtures", "*picture"], []),
    # THE PRESENT PATH ON THE STUDIO SIDE (TESTING-SPEED-A): the engine host creates the window and drives the frame —
    # with OgreEngine.cpp's frame loop, src/viewport/** and src/app/** the only paths that reach every app row
    (r"^src/bridge/enginehost\.", ["*vulkan-scripts", "player", "thumbnails", "materialpreview"], ["player"]),
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
    # THE WORLD MODES' POST CHAIN (TESTING-SPEED-A item 4, the spec's bloom line): worldmodes.cpp sets what the chain
    # shows — the hdr/ssr/looks families and the picture set, never the GI (BLOOM-1: zero gi rows)
    (r"^src/services/worldmodes\.", ["hdr", "ssr", "looks", "*picture"], []),
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
    # (+ *picture: the render driver and the screenshot grades draw what every picture row compares; the hashes do not
    # cover viewport code — fix round item 3)
    (r"^src/viewport/", ["app", "input", "gizmo", "picking", "cameras", "sockets", "ui", "*picture",
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
    # THE PRICE FILE SELECTS NOTHING (TESTING-SPEED-A item 5): scripts/gate-times.txt is the rows' measured seconds,
    # read by the estimate — a lane that adds five prices changed no tool and no row
    (r"^scripts/gate-times\.txt$", [], []),
    (r"^scripts/gate[-_]", ["hygiene"], []),
    # THE GPU-TIMING LOCK's wrapper (DEVPROCESS-1): its own tooling suite, devprocess.gpu_lock.
    (r"^scripts/gpu-exclusive", ["tooling"], []),
    # THE VRAM BUDGET's helper (GATE-ADMIT-1): its tooling suite (devprocess.vram_admit), the
    # pool driver's own test that imports it (pool.runner, tests/app) and the closure (hygiene).
    (r"^scripts/(gpu-admit|vram_tokens)", ["tooling", "app", "hygiene"], []),
    # EVERY OTHER SCRIPT IS THE RUNNER'S TOO (TESTING-CLEANUP-2B item 4, H8a): ci_gate_check.py / ci-gate-check.sh (the
    # judge: gate.ci_check, gate.verdict_door, …), kernel_xid.py (devprocess.kernel_journal, gate.cost),
    # check-trailers.sh (the hygiene rows), sanitize.sh and its lsan/tsan suppressions (the source lints) selected NO row —
    # a change to the judge gated on nothing. Their tests are the hygiene and tooling rows (display-free, the CPU
    # phase). In a tooling-only diff (§1.3.4) the same paths take Selection.tooling_path.
    (r"^scripts/", ["hygiene", "tooling"], []),
]

# A TOOLING-ONLY DIFF SELECTS NO PRODUCT ROW (TESTING_V3_SPEC §1.3.4; the owner 2026-10-10: "why do we still have
# 1 hour gates?" — batch A's three runner lanes selected 636 rows, 57 min). A change that touches NOTHING the
# product binary is built from cannot change the product rows' answers: running them re-proves the runner, not the
# product, and the runner is proved by its own rows. The rule FAILS CLOSED (fix round D): NON_PRODUCT lists the
# paths that are NOT product inputs — scripts/, tests/, docs/, testing/, .claude/, irisgl/docs/, any *.md — and EVERY
# other path is product (src/, irisgl/, app/, scenes/, thirdparty/, the build files, a top-level directory nobody
# has classified yet). A diff with ONE product path takes the whole union below, exactly as before. A diff with none
# is TOOLING-ONLY:
#   * a path of the runner itself (TOOLING_OWN: scripts/, tests/hygiene/, tests/tooling/, testing/) selects the
#     `hygiene` and `tooling` rows (the lints and the runner's own suites, display-free) plus what its area rule
#     names among the non-app, non-compiled rows (a script's own self-test, e.g. pool.runner for vram_tokens) and
#     every row whose COMMAND runs the file;
#   * any other path — a test's source, a test CMakeLists, a shared test helper — goes through the ordinary walk:
#     what it reaches IS the rows whose declaration or command changed (the graph for a test source, the CMake
#     reader for a registration);
#   * docs and the like select what they always did (nothing; TESTING_GATE.md its guard);
#   * the smoke pair (ALWAYS_ON_CODE) is the PRODUCT's smoke: no product path moved, so it does not ride.
NON_PRODUCT = re.compile(r"^(scripts/|tests/|docs/|testing/|\.claude/|irisgl/docs/)|\.md$")
TOOLING_OWN = re.compile(r"^(scripts/|tests/(hygiene|tooling)/|testing/)")
# A RUNNER'S OWN SELF-TEST THAT BOOTS THE APP (it is the tool's test, not a product row): selected by name in a
# tooling-only diff, where the area rules' directories pick only lint/script rows. pool.runner drives the pool
# runner (tests/support/run_pool.py), which takes its tokens through vram_tokens.py.
TOOL_SELF_TESTS = [
    (r"^scripts/(gpu-admit|vram_tokens)", ["pool.runner"]),
]


def fork_lines(f):
    """[(pattern, tokens | "none")] — the FIRST FORK_COMPONENTS line a changed fork file matches (the lines run specific ->
    generic: OgreMain/src/OgreLod* is the LOD line's, not OgreMain's every engine fixture); [] = no line names it (the
    MERGE tier)."""
    for pat, toks in FORK_COMPONENTS:
        if re.match(pat, f):
            return [(pat, toks)]
    return []


def product_paths(paths):
    """The touched paths the product is built from (§1.3.4): one of them makes the diff a product diff."""
    return [p for p in paths if not NON_PRODUCT.search(p)]


# THE SMOKE PAIR (a boot that renders + the contract of the scripting surface, ~15 s together) — selected by a change
# to THE TEST HARNESS only (TESTING-SPEED-A item 5, SPECS/TESTING_SPEED_SPEC.md §2 A6): the helpers every row runs
# through, the rows' macros, the CMake helpers. A product change is guarded at every merge by the six hash lines and by
# what it reaches; it never selects the pair by itself.
ALWAYS_ON_CODE = ["app.startup_quiet", "api.contract"]
# the Studio paths that sit on the subsystem table (their AREA_RULES line is their whole reach — no build graph)
STUDIO_TABLE_PATHS = (r"^src/services/worldmodes\.",)
HARNESS_PATHS = re.compile(r"^(tests/support/|tests/CMakeLists\.txt$|tests/cmake/)")
# THE TOOLING ROWS ARE THE TOOLS' (TESTING-SPEED-A item 5): the `hygiene` and `tooling` labelled rows (the lints, the
# selector's and the judge's own suites, pool.runner) are selected by a change under these paths ONLY — never by a
# product path (the builder's self-audit runs the hygiene label in the lane; a product merge does not re-run it)
TOOLING_PATHS = re.compile(r"^(scripts/(?!gate-times\.txt$)|testing/|tests/hygiene/|tests/support/|"
                           r"tests/CMakeLists\.txt$|tests/cmake/|\.claude/|docs/TESTING_GATE\.md$)")
TOOLING_LABELS = {"hygiene", "tooling"}
# A LANE'S OWN ROWS (TESTING-SPEED-A item 6): tests/lanes/<LANE>/ registers rows labelled `lane:<LANE>` (jah_lane_row) —
# selected for that lane's own range only (Selection.lane), never for anyone else's
LANE_LABEL = "lane:"
# THE STAGE-CLOSE ROWS (STAGE-CLOSE-1; D6B-GATE-SHAPE before it, under a daily-run name for a tier
# that never once ran — the owner, 2026-10-09: there is no such build). Two labels, read here alone:
#   `stage-close` — the row leaves the MERGE and PUSH tiers and runs in THE STAGE-CLOSE BATCH
#                   (`--stage-close-tier`): one batch in the gate slot, started BY THE LEAD at every
#                   stage close (`runner.sh stage-close`; the ship runs them too), never
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
# (TESTING-CLEANUP-2B item 11: shadercache.container_asan's own attack label — a second way of
# saying "not in the merge tier" — is retired; the row is a `stage-close` row like every minutes-long one.)
STAGE_CLOSE_LABELS = {"stage-close"}
# Never selected by a scoped gate. A row selected WITHOUT these labels is a gating row of that gate
# (ci_gate_check reads this set).
SCOPE_EXCLUDED_LABELS = {"quiet-box"}
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
    # THE SHADER CACHE'S CONTAINER UNDER ASAN (TESTING-CLEANUP-2B item 11): the cache container's reader and writer
    # (a corrupt file must never read past a buffer it sized from a number in the file).
    (r"^irisgl/engine/src/OgreShaderCache\.", ["shadercache.container_asan"]),
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
# (the scoped selection, the MERGE tier, a fallback, a merge run's), the runner's ctest width
# (`gate-scope.py --gate-jobs`) and the merge refusal's re-selection read THIS; the docs name it
# instead of quoting a number. Changing the box's width is this one line — and an owner decision
# (PHOTON_ATOM_CONTRACT §7b rule 3; the gate-speed audit's replay: -j6 21.7 min against -j4's 31.0
# on rc-smoke15a's durations, CPU contention unmeasured). `-j N` still overrides it per run.
# THE WIDTH PER PASS (TESTING-SPEED-A item 3; SPECS/TESTING_SPEED_SPEC.md §2 A10: 20 cores, 121 GB, 16 GB VRAM — a test
# is one engine and one frame loop, so the parallelism is ACROSS processes): fixtures (CLASS engine|selftest, 1-2
# tokens, <= 1 GB each) at -j8, the app rows and pools at -j4, a measuring row at -j1 on the whole card. ctest's
# resource spec (the VRAM tokens, tests/cmake/vram_rows.cmake) stays the bound under every width — never this table.
# The runner's merge (`--gate-jobs` prints it as JSON) and this tool's own --run read it.
GATE_JOBS = {"fixture": 8, "app": 4, "measuring": 1}

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


def merge_tier(jobs=GATE_JOBS["app"]):
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
# the whole card. ctest ANDs repeated -L filters. Started by the lead (`runner.sh stage-close`, or
# `gate-scope.sh --stage-close-tier --run`), never by a timer.
_STAGE_CLOSE_RX = "|".join(sorted(re.escape(l) for l in STAGE_CLOSE_LABELS))
_QUIET_RX = "|".join(sorted(re.escape(l) for l in (QUIET_BOX_LABEL, TIMING_LABEL)))


def stage_close_tier(jobs=GATE_JOBS["app"]):
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


# THE WHOLE RANGE (RUNNER-A2): a SERVING asks "does anything in A..C reach the row" — every change between the record and
# the tip, forward merges included — never "what is the lane's own change". ci_gate_check's serving sets this around its
# selections; with it own_base answers the merge-base, whatever merges the range holds.
WHOLE_RANGES = False


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
    key = (base, tip, cwd, WHOLE_RANGES)
    if key in _OWN_BASE_CACHE: return _OWN_BASE_CACHE[key]
    b, t = _git_try(["rev-parse", "--verify", "-q", base + "^{commit}"], cwd), \
        _git_try(["rev-parse", "--verify", "-q", tip + "^{commit}"], cwd)
    if not b or not t:
        _OWN_BASE_CACHE[key] = (base, []); return _OWN_BASE_CACHE[key]
    if _git_try(["rev-parse", "--verify", "-q", t + "^1"], cwd) == b:
        _OWN_BASE_CACHE[key] = (b, []); return _OWN_BASE_CACHE[key]
    mb = _git_try(["merge-base", b, t], cwd) or b
    own, fwd = mb, []
    if WHOLE_RANGES:
        _OWN_BASE_CACHE[key] = (own, []); return _OWN_BASE_CACHE[key]
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
    LANE'S OWN change (scope_range: what forward merges carried in is their own merge's business)."""
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


def _is_wipe(cmd):
    """A fixture setup's command is a home WIPE: tests/support/fresh_home.sh, tests/samples/freshhome.cmake, or an
    `sh -c "rm -rf …"` (the three forms tests/CMakeLists.txt's fresh-home section names)."""
    cmd = list(cmd or [])
    if any(str(c).endswith(("fresh_home.sh", "freshhome.cmake")) for c in cmd):
        return True
    return len(cmd) > 2 and os.path.basename(str(cmd[0])) == "sh" and cmd[1] == "-c" and "rm -rf" in str(cmd[2])


def _relocated(raw, build):
    """THE INVENTORY OF A MOVED BUILD DIR (RUNNER-A2): ctest names the paths the build was CONFIGURED with (its
    CMakeCache's CMAKE_CACHEFILE_DIR / CMAKE_HOME_DIRECTORY) — a build dir copied or renamed since (rc-base is
    an rc tree's renamed tree; a symlinked overlay of it) still names the old tree, and no row's command then matches
    the graph's executables (every compiled row read as `other`: measured on rc-base, 619 other + 132 app, no compiled
    row). The configured build dir is read as this build dir, the configured source root as ROOT (the tree the
    selection reads)."""
    roots = gate_graph.build_roots(build)
    if not raw or not roots or not roots.get("build"):
        return raw
    here = os.path.abspath(build).rstrip("/")
    if roots["build"] == here:
        return raw
    pairs = [(roots["build"], here)]
    if roots.get("root") and roots["root"] != ROOT and not here.startswith(roots["root"] + "/"):
        pairs.append((roots["root"], ROOT))
    for old, new in pairs:                 # the build dir first: it sits inside the old root
        raw = re.sub(re.escape(json.dumps(old)[1:-1]) + r'(?=[/"\\\s;:])', lambda m, n=json.dumps(new)[1:-1]: n, raw)
    return raw


def load_inventory(build):
    # listed from a copy of the CTestTestfile tree: a listing in the build dir itself truncates
    # the LastTest.log of a gate running there (gate_graph.ctest_inventory)
    if not os.path.isfile(os.path.join(build, "CTestTestfile.cmake")):
        raw, rc = "{}", 0
    else:
        raw, rc = gate_graph.ctest_inventory(build)
        if rc != 0:
            sys.stderr.write(f"gate-scope: ctest --show-only failed ({rc}) for {build}\n"); sys.exit(2)
    raw = _relocated(raw, build)
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
            # A HOME WIPE vs A RUN FIXTURE (TESTING-CLEANUP-2B fix round 3 F3): `fixture_setup` means a fixture's
            # WIPE (fresh_home.sh, samples' freshhome.cmake, an `sh -c "rm -rf …"`) — ~10 ms, never a gating row; a
            # real run that ALSO sets up a fixture (threading.newproject_stall for its inspector, material_tabs for
            # its relaunch) is `run_fixture` and stays a gating row (R1, the estimate, max_rows)
            "fixture_setup": bool(props.get("FIXTURES_SETUP")) and _is_wipe(cmd),
            "run_fixture": bool(props.get("FIXTURES_SETUP")) and not _is_wipe(cmd),
            "labels": set(props.get("LABELS", []) or []),
            "frame_count": props.get("JAHSHAKA_TEST_FRAMES"),     # FRAMES <n>: the frames the row renders (item 7)
            "kind": "other", "exes": set(),
        }
    picture_set(inv)
    return inv


# THE PICTURE SET (TESTING-SPEED-A item 4; SPECS/TESTING_SPEED_SPEC.md §2 A4): the rows whose subject is the picture the
# viewport shows — the kept rows of these directories / row prefixes, and every app row and pool ARM whose script takes
# a screenshot (a pool is reached when any of its arms is: the arm, not the pool). `kept`: not stage-close, not a target.
PICTURE_FAMILIES = ("hdr", "ssr", "looks", "mirror")
# ...less the PHOTON family's own pictures (gi.*, photon.*: the GI's picture is the gi subsystem's — reached when Photon
# is; a bloom change selects ZERO gi rows, SPECS/TESTING_SPEED_SPEC.md §2 A4's BLOOM-1 replay)
PICTURE_NOT = ("gi", "photon")
PICTURE_VERBS = re.compile(r"\beditor\.screenshot\b|\bvr\.eyeScreenshot\b|\bscreenshot\s*\(")


def _names_picture(path, memo):
    if path not in memo:
        try:
            memo[path] = bool(PICTURE_VERBS.search(open(path, errors="replace").read()))
        except OSError:
            memo[path] = False
    return memo[path]


def picture_set(inv):
    """Marks inv[row]["picture"] (the row whole) and inv[row]["picture_arms"] (a pool's screenshot arms)."""
    memo = {}
    for n, t in inv.items():
        kept = not (t["labels"] & (STAGE_CLOSE_LABELS | TARGET_LABELS)) and not (
            t["dir"] in PICTURE_NOT or n.split(".", 1)[0] in PICTURE_NOT)
        fam = t["dir"] in PICTURE_FAMILIES or n.split(".", 1)[0] in PICTURE_FAMILIES
        arms = sorted(a for a, scr in t["arms"].items() if _names_picture(scr, memo)) if kept else []
        shot = kept and not t["arms"] and bool(t["script"]) and _names_picture(t["script"], memo)
        t["picture"] = kept and not t["arms"] and (fam or shot)
        t["screenshot"] = shot             # the script part alone (`*screenshots`)
        t["picture_arms"] = arms


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
# <source>`, the source NAMED — `quiet n=<k>` (the median of the row's PASS runs on a QUIET box — gate_runlog.quiet:
# no sibling ctest, no build, no GPU competitor, the load under the box's figure —, >= 3 of them: THE price), `all n=<k>` (fewer quiet runs: the median of every PASS run), `fixture` (a home wipe), or the
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


LOG_SOURCES = ("solo-quiet n=", "quiet n=", "all n=", "fixture")    # the sources record_times writes; any other = a HAND price


def solo_quiet(r, load_max=None):
    """THE PRICE RULE (the lead, TESTING-SPEED-B4's read): a record prices its row only when it ran ALONE — the census
    shows no other ctest, no build, no GPU competitor, the load under the box's figure (gate_runlog.quiet) AND no gate
    sibling (its ctest ran at -j1: box.jobs == 1). A census that cannot tell is no price."""
    if not gate_runlog.quiet(r, load_max)[0]:
        return False
    return ((r or {}).get("box") or {}).get("jobs") == 1


def run_log_prices(since=PRICE_SINCE):
    """row -> (median PASS seconds, 'solo-quiet n=<k>') over the run log and its archive since `since` — ONLY from the
    records that ran alone (solo_quiet), and only when >= gate_runlog.QUIET_MIN of them exist; a row without them has
    no run-log price (a median of gate runs beside siblings measures the box, not the row: four app rows priced
    70/50/37/31 s under contention ran 20/18/11/11 s alone)."""
    import glob, statistics
    d = gate_runlog.log_dir()
    files = glob.glob(os.path.join(d, "*.jsonl")) + glob.glob(os.path.join(os.path.dirname(d), "runs-archive", "**",
                                                                           "*.jsonl"), recursive=True)
    acc = collections.defaultdict(list)
    lm = gate_runlog.quiet_load_max()
    since_at = gate_runlog.instant(since)            # INSTANTS, never the ISO text (F20's class; the DST switch misorders text)
    for f in files:
        for line in open(f, errors="replace"):
            try: r = json.loads(line)
            except ValueError: continue
            if r.get("verdict") != "PASS" or r.get("seconds") is None or r.get("kind") or gate_runlog.instant(r.get("ts")) < since_at:
                continue
            k = r["suite"] if not r.get("arm") else f"{r['suite']}::{r['arm'].split('.', 1)[-1]}"
            acc[k].append((r["seconds"], solo_quiet(r, lm)))
    out = {}
    for k, v in acc.items():
        q = [x for x, qt in v if qt]
        if len(q) >= gate_runlog.QUIET_MIN:
            out[k] = (statistics.median(q), f"solo-quiet n={len(q)}")
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
    hand = lambda v: bool(str(v[1] or "").strip()) and not str(v[1]).startswith(LOG_SOURCES)
    for n in rows:
        if n in old and hand(old[n]):
            # A HAND PRICE ("solo median ...", a measurement with its why) IS NEVER OVERWRITTEN by the run log
            out[n] = old[n]; kinds["hand"] += 1
        elif n in log:
            out[n] = log[n]; kinds[log[n][1].split()[0]] += 1
        elif n in old:
            out[n] = old[n]; kinds["kept"] += 1
        elif inv[n].get("fixture_setup"):
            out[n] = (0.01, "fixture (a home wipe; no PASS record)"); kinds["fixture"] += 1
    # a pool's ARMS keep their own prices (`<pool>::<arm>`: a partial pool's estimate sums them)
    for k, v in list(log.items()) + [kv for kv in old.items() if kv[0] not in log or hand(kv[1])]:
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
    quiet median where >= 3 records ran on a quiet box, gate_runlog.quiet). COST_SOURCE says which."""
    costs = {}
    pt = parse_times()
    for n, (sec, _src) in pt.items():
        costs[n] = sec; COST_SOURCE[n] = "file"
    costs.update(gate_runlog.median_times(days=14, sources=COST_SOURCE))
    # THE PRICE RULE: a HAND price (a row's own solo median, measured alone) beats the run log's median of gate runs
    for n, (sec, src) in pt.items():
        if str(src or "").strip() and not str(src).startswith(LOG_SOURCES):
            costs[n] = sec; COST_SOURCE[n] = "file"
    return costs


def cost_sources(keys):
    """'<n> quiet, <n> all, <n> file, <n> assumed' over the keys an estimate summed."""
    c = {"quiet": 0, "all": 0, "file": 0, "assumed": 0}
    for k in keys: c[COST_SOURCE.get(k, "assumed")] += 1
    return (f"costs: {c['quiet']} quiet medians (gate_runlog.quiet, >= 3 records), {c['all']} "
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

    def __init__(self, inv, graph, app_exe, revs, jobs, lane=None):
        self.inv, self.graph, self.app_exe, self.revs, self.jobs = inv, graph, app_exe, revs, jobs
        # THE LANE WHOSE RANGE THIS IS (TESTING-SPEED-A item 6): its own `lane:<LANE>` rows are selectable, nobody else's
        self.lane = (lane or "").lower() or None
        self.selected = {}
        self.fallback = []
        self.full_tier = []          # rules that select the whole tier (an unreadable fork diff, a root setting)
        self.fork_bump = None        # {old, new, files} when the range moves the fork pin (T2)
        self.rationale = []
        self._moved = set()          # the code paths whose change was not comment-only (code_moved)
        self.skipped_ubiquitous = set()
        self.by_dir = collections.defaultdict(list)
        self.by_prefix = collections.defaultdict(list)
        for n, t in inv.items():
            self.by_dir[t["dir"]].append(n)
            self.by_prefix[n.split(".", 1)[0]].append(n)
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
        self.tools_dropped = set()         # hygiene/tooling rows a product path reached: not run, still reached
        self._symbol_ids = {}              # file -> the changed identifiers it names (OgreEngine.cpp's function walk)

    @property
    def code_moved(self):
        return bool(self._moved)
    # -- adding rows -----------------------------------------------------------------------
    def data_edges(self, paths, facts):
        """A CHANGED DATA FILE IS A CHANGED SOURCE (RUNNER-A2B): every row with a DATA EDGE to a touched path (a file it
        names, a directory it walks, a set its CMake declares — gate_graph.data_set) runs; a path the data edges own is
        no FALLBACK (no rule owned it, its readers do)."""
        # the engine's own paths are THE SUBSYSTEM TABLE's (TESTING-SPEED-A item 4): the `engine-media` set every rendering
        # row declares would reach them all — the table selects, the data hash still keeps a row from being SERVED
        hit = gate_graph.data_rows(facts, [p for p in paths if not p.startswith("irisgl/engine/")])
        owned = set()
        for n, p in hit.items():
            if n in self.inv:      # in a tooling-only diff too: a row whose OWN data moved is not the product's, it is the row's
                self.add([n], f"{p}: a data edge (the row reads it)")
            owned.add(p)
        for p in paths:
            if any(p == d or p.startswith(d + "/") for f in facts.values() for d in f.get("data_dirs") or ()):
                owned.add(p)
        if owned and self.fallback:
            self.fallback = [e for e in self.fallback if e.split(":", 1)[0].split(" (via", 1)[0] not in owned]
        if hit:
            self.rationale.append((sorted(set(hit.values()))[0], f"DATA EDGES: {len(hit)} row(s) read the touched data"))

    def add(self, suites, why):
        for s in suites:
            r0 = s.split("::", 1)[0]
            if r0 in self.inv and not self.own_lane_row(r0):
                continue
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

    def own_lane_row(self, n):
        """False for a row labelled `lane:<X>` when X is not this selection's lane (TESTING-SPEED-A item 6)."""
        ll = [l for l in self.inv[n]["labels"] if l.startswith(LANE_LABEL)]
        return not ll or bool(self.lane) and any(l[len(LANE_LABEL):].lower() == self.lane for l in ll)

    def arm_subsets(self):
        """pool row -> the arms selected, for every pool NOT selected whole."""
        return {r: sorted(a) for r, a in self.arms.items() if r not in self.whole and r in self.selected}

    def expand(self, dirs, modules, why, own_api=False, kinds=None):
        """An area rule's families. `kinds` limits the dirs to row kinds (the graph owns the
        compiled rows of a C/C++ path, so its rules pick only app and other rows)."""
        for d in dirs:
            if d == "*merge-tier": self.fallback.append(why); continue
            if d == "*picture":
                self.add(sorted(n for n, t in self.inv.items() if t.get("picture")), f"{why} [the picture set]")
                self.add(sorted(f"{n}::{a}" for n, t in self.inv.items() for a in t.get("picture_arms") or ()),
                         f"{why} [the picture set: its screenshot arm]")
                continue
            if d == "*screenshots":
                self.add(sorted(n for n, t in self.inv.items() if t.get("picture") and t.get("screenshot")),
                         f"{why} [the picture set's screenshot rows]")
                self.add(sorted(f"{n}::{a}" for n, t in self.inv.items() for a in t.get("picture_arms") or ()),
                         f"{why} [the picture set: its screenshot arm]")
                continue
            if d == "*engine-fixtures":
                self.add(sorted(self.engine_fixture_rows()), f"{why} [every engine fixture]")
                continue
            if d in ("*vulkan-scripts", "*headless-scripts", "*all-scripts", "*app-rows"):
                for n, t in self.inv.items():
                    if t["kind"] != "app": continue
                    if d == "*vulkan-scripts" and t["headless"]: continue
                    if d == "*headless-scripts" and not t["headless"]: continue
                    self.add([n], why)
                continue
            # a token is a test DIRECTORY or a ROW PREFIX (`cloud_2d.*` lives in tests/gi, `decals.engine` in tests/engine)
            rows = set(self.by_dir.get(d, [])) | set(self.by_prefix.get(d, []))
            if "." in d:                       # a dotted token: the rows whose NAME starts with it (engine.atom_)
                rows |= {n for n in self.inv if n.startswith(d)}
            self.add(sorted(n for n in rows if kinds is None or self.inv[n]["kind"] in kinds), why)
        for m in modules:
            if m in self.ubiquitous and not own_api:
                self.skipped_ubiquitous.add(m); continue
            self.add([n for n, ms in self.mods.items() if m in ms], f"{why} [module {m}]")

    def rules_for(self, p, why_prefix, kinds=None):
        """Apply the first matching AREA_RULE. Returns the rule text, or None when none matched. An irisgl/engine/ path
        answers by THE SUBSYSTEM TABLE (TESTING-SPEED-A item 4): every row kind (the graph does not select engine code)."""
        if p.startswith("irisgl/engine/"):
            return self.engine_table(p, why_prefix)
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

    def engine_fixture_rows(self):
        """THE ENGINE FIXTURES: every non-app row whose executable extracts the engine archive (the graph: any engine
        object's reach — the facade pulls them all), or without a graph every row admitted as CLASS engine|selftest."""
        if not hasattr(self, "_engine_fixtures"):
            out = set()
            if self.graph is not None:
                if not hasattr(self, "_engine_exes"):
                    art = self.graph.targets.get("JahshakaEngine")
                    mem = sorted(self.graph.members(art)) if art else []
                    self._engine_exes = self.graph.reach_objects(mem)
                for e in self._engine_exes:
                    if e != self.app_exe:
                        out |= {n for n in self.exe_rows.get(e, []) if self.inv[n]["kind"] != "app"}
            else:
                for n, t in self.inv.items():
                    if t["kind"] != "app" and re.search(r"--label \S+:(engine|selftest)\b", " ".join(t["cmd"])):
                        out.add(n)
            self._engine_fixtures = out
        return self._engine_fixtures

    def engine_table(self, p, why):
        """THE SUBSYSTEM TABLE for one irisgl/engine/ path: the FIRST ENGINE_SUBSYSTEMS line that matches it (relative to
        irisgl/engine/; the lines run specific -> generic). Returns the rule text, or None when no line names it."""
        rel = p[len("irisgl/engine/"):]
        hit = [(pat, toks, what) for pat, toks, what in ENGINE_SUBSYSTEMS if re.search(pat, rel)][:1]   # FIRST match
        if not hit:
            return None
        for pat, toks, what in hit:
            self.expand(toks, [], f"{why}: subsystem {what}")
        extra = ""
        if re.match(PIECE_RX, rel):
            # A JAHSHAKA HLMS PIECE IS ALSO ATOM'S (fix round item 5; the PHOTON-HIT-SHADE-1 / audit F2 miss): HlmsAtom
            # compiles the Jahshaka Hlms library, so every *_piece* medium reaches the atom decode's rows whatever line
            # named it first
            self.expand(PIECE_TOKENS, [], f"{why}: a Jahshaka Hlms piece (HlmsAtom compiles the library)")
            extra = " + the piece's atom decode -> " + " ".join(PIECE_TOKENS)
        return "subsystem " + " + ".join(f"{what} -> {' '.join(toks) or 'its CMake commands'}" for _, toks, what in hit) + extra

    def ogre_engine(self, p, tag, notes, depth=0):
        """irisgl/engine/src/OgreEngine.cpp BY THE FUNCTION CHANGED (TESTING-SPEED-A item 4): the enclosing top-level
        function of every changed line (gate_graph.enclosing_functions) through OGRE_ENGINE_FUNCTIONS; an unmapped name
        or a namespace-scope line reaches everything that renders (_ALL), said. Returns False when there is no diff
        to read (the whole file then reaches _ALL)."""
        ids = self._symbol_ids.get(p) if depth > 0 else None
        if ids:
            # REACHED THROUGH A SYMBOL (a header's changed identifier that OgreEngine.cpp names): the functions whose lines
            # name it — the same walk over the file as it is
            try:
                text = open(os.path.join(ROOT, p), errors="replace").read()
            except OSError:
                text = ""
            lines = gate_graph.strip_cxx(text)
            outer = gate_graph._outer_scopes(lines)
            rx = re.compile(r"\b(" + "|".join(re.escape(t) for t in sorted(ids)) + r")\b")
            fns = {}
            for k, ln in enumerate(lines):
                if rx.search(ln):
                    fns[outer[k]] = fns.get(outer[k], 0) + 1
        elif depth > 0:
            fns = {None: 1}                  # an includer with no symbol to follow: the file as a whole
        else:
            a, b = self.revs.texts(p) if self.revs.base else (None, None)
            if a is None or b is None:
                core_ = p in CORE_FUNCTION_MAPS
                self.expand(_CORE if core_ else _ALL, [], f"{tag}: {os.path.basename(p)} with no diff to read")
                notes.append(f"{os.path.basename(p)}, no diff to read → "
                             + ("every engine fixture + the picture set" if core_ else "everything that renders"))
                return
            fns = gate_graph.enclosing_functions(a, b, gate_graph.hunks_of(self.revs.diff_u0(p)))
        if not fns:
            notes.append("comments/whitespace only → nothing reached")
            return
        core = CORE_FUNCTION_MAPS.get(p)
        fmap, mapname = (core, os.path.basename(p)[:-4].replace("Ogre", "OGRE_").upper() + "_FUNCTIONS") if core else \
            (OGRE_ENGINE_FUNCTIONS, "OGRE_ENGINE_FUNCTIONS")
        for fn in sorted(fns, key=lambda x: (x is None, x or "")):
            toks = fmap.get(fn)
            kw = function_keywords(fn) if (toks is None and fn) else None
            if kw:
                # A FUNCTION THE MAP DOES NOT NAME (a new one: readSsrHistoryHdr) IS READ BY ITS NAME'S KEYWORDS (round 15)
                toks = kw[0]
                what = f"{fn}() (not in {mapname}) by its name's keyword '{kw[1]}' -> {' '.join(toks)}"
            elif toks is None:
                # the core's unnamed reach: OgreEngine.cpp's is everything that renders (_ALL, the app rows too);
                # OgreScene/OgreView's every engine fixture + the picture set (_CORE), never the app rows
                toks = _CORE if core else _ALL
                what = ("a namespace-scope line" if fn is None else f"{fn}() is named by no line of {mapname}") + \
                    (" → every engine fixture + the picture set" if core else " → everything that renders")
            else:
                what = f"{fn}() -> {' '.join(toks)}"
            self.expand(toks, [], f"{tag}: {what}")
            notes.append(what)

    def engine_path(self, p, tag, depth, notes):
        """ENGINE CODE AND MEDIA (TESTING-SPEED-A item 4): the subsystem table is the selector — never the graph, never
        the engine's data edges. A header goes through its symbols first; a fallback to "every includer" is the
        includers' subsystems (each includer through this table, a Studio includer through its own rule)."""
        if p == "irisgl/engine/src/OgreEngine.cpp" or p[len("irisgl/engine/"):] in CORE_BY_FUNCTION:
            self.ogre_engine(p, tag, notes, depth)
            return
        if p.endswith(HEADER_EXT) and self.revs.base and depth == 0:
            if self.header_symbols(p, tag, notes):
                return
            # THE INCLUDERS' SUBSYSTEMS (never the family): who reads the header, each by its own table line / rule
            srcs = []
            if self.graph is not None:
                srcs = sorted({os.path.relpath(self.graph.obj_src[o], ROOT)
                               for o in self.graph.includers(os.path.join(ROOT, p)) if o in self.graph.obj_src})
            srcs = [x for x in srcs if x.startswith(("src/", "irisgl/")) and not x.startswith("irisgl/thirdparty/")]
            for src in srcs:
                if src.startswith("irisgl/engine/"):
                    self.path(src, 1, via=f"includes {os.path.basename(p)}")
                else:
                    self.rules_for(src, f"{tag} (read by {src})", kinds={"app", "other"})
            notes.append(f"the includers' subsystems ({len(srcs)} reading source(s))")
            if not srcs:
                r = self.engine_table(p, tag)
                notes.append(r or "no includer, no table line → nothing reached")
            return
        r = self.engine_table(p, tag)
        if r:
            notes.append(r)
        else:
            self._fallback(p, tag, notes)

    def _fallback(self, p, tag, notes, why="no rule, no symbol, no graph owner"):
        """A PATH NOBODY OWNS (TESTING-SPEED-A item 1): recorded as a fallback (printed, and gate.selection refuses a lane
        that has one) — and it selects THE SUBSYSTEM OF ITS DIRECTORY by AREA_RULES (the nearest parent directory a rule
        matches), NEVER the whole tier."""
        self.fallback.append(f"{tag}: {why}")
        parts = p.split("/")
        if parts[:2] == ["tests", "lanes"] and len(parts) > 3:
            parts = ["tests"] + parts[2:]      # tests/lanes/<LANE>/: the lane's own directory
        if parts[0] == "tests" and len(parts) > 2 and parts[1] in self.by_dir:
            # a tests/<dir>/ path: that directory's rows ARE its subsystem
            self.add(self.by_dir[parts[1]], f"{tag}: its directory tests/{parts[1]}")
            notes.append(f"NO RULE → its directory's rows: tests/{parts[1]}")
            return
        d = os.path.dirname(p)
        while d:
            r = self.rules_for(d + "/", f"{tag} (its directory {d}/)")
            if r:
                notes.append(f"NO RULE → its directory's subsystem: {r}")
                return
            d = os.path.dirname(d)
        notes.append("NO RULE and no rule for any parent directory → nothing more (never the whole tier)")

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
        key = (p, depth > 0) if p not in _BY_FUNCTION else (p, depth > 0, via)
        if key in self._visited: return
        self._visited.add(key)
        tag = p if via is None else f"{p} (via {via})"
        notes = []
        if p.startswith(("src/", "irisgl/", "tests/")) and not p.startswith("irisgl/thirdparty/") and (
                p.endswith(CXX_EXT) or p.startswith("irisgl/engine/")):
            self._moved.add(p)               # CODE moved (a comment-only or deleted file is discarded again below)

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

        if depth == 0 and any(re.match(pat, p) for pat in STUDIO_TABLE_PATHS) and is_cxx:
            # A STUDIO FILE ON THE SUBSYSTEM TABLE (worldmodes.cpp: the chain's settings) is selected by its line alone —
            # like engine code (the spec's bloom line); its compiled readers (test_gi_tiers links it) are not asked
            if self.revs.base and self._deleted(p):
                self._moved.discard(p); self.rationale.append((p, "deleted")); return
            a, b = self.revs.texts(p) if self.revs.base else (None, None)
            if a is not None and b and a and gate_graph.strip_cxx(a) == gate_graph.strip_cxx(b):
                self._moved.discard(p); self.rationale.append((p, "comments/whitespace only → nothing reached")); return
            r = self.rules_for(p, tag)
            self.rationale.append((p, f"the subsystem table (a Studio line): {r}"))
            return
        if p.startswith("irisgl/engine/") and not is_cmake:
            # THE SUBSYSTEM TABLE (TESTING-SPEED-A item 4): engine code and media are selected by the table only
            if is_cxx and self.revs.base and depth == 0:
                if self._deleted(p):
                    self._moved.discard(p)
                    self.rationale.append((p, "deleted → what used it is in this diff"))
                    return
                a, b = self.revs.texts(p)
                if a is not None and b and a and gate_graph.cxx_changed_identifiers(
                        a, b, gate_graph.hunks_of(self.revs.diff_u0(p)))[1] == 0:
                    # comments/whitespace only, by the hunks (an ADDED comment line shifts the stripped text's length)
                    self._moved.discard(p)
                    self.rationale.append((p, "comments/whitespace only → nothing reached"))
                    return
            self.engine_path(p, tag, depth, notes)
            if depth == 0: self.rationale.append((p, "; ".join(notes)))
            return

        if is_cmake:
            # a CMake helper an area rule owns (WrapHlmsPiece: the engine's media staging) keeps
            # its rule; every other build file is read by what its changed commands name
            if any(re.match(pat, p) for pat, _, _ in AREA_RULES):
                r = self.rules_for(p, tag)
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
            self._moved.discard(p)
            self.expand(["hygiene"], [], f"{tag}: deleted")
            self.rationale.append((p, "deleted → what used it is in this diff"))
            return
        if is_cxx or graph_input:
            # comments/whitespace only → nothing reached (the lints still ride)
            if is_cxx and self.revs.base and depth == 0:
                a, b = self.revs.texts(p)
                if a is not None and b and a and gate_graph.strip_cxx(a) == gate_graph.strip_cxx(b):
                    self._moved.discard(p)
                    self.expand(["hygiene"], [], f"{tag}: comment/whitespace only")
                    self.rationale.append((p, "comments/whitespace only → no code reached"))
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
        if r: pass
        elif not hit_argv:
            g = None if is_cxx else self.graph_rows(p, tag)     # a build input (.ui, .qrc, a shader source)
            if g is not None:
                notes.append(f"graph: {g[0]} executable(s)" + (" incl. the app" if g[1] else ""))
                if g[1]:
                    self.expand(["*app-rows"], [], f"{tag}: the app relinked and no rule narrows it")
                    notes.append("the app relinked, no rule narrows it → every app row")
            else:
                self._fallback(p, tag, notes)
        if depth == 0: self.rationale.append((p, "; ".join(notes)))

    def tooling_path(self, p):
        """A path of the runner in a TOOLING-ONLY diff (§1.3.4; NON_PRODUCT above): the tools' own rows — the
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
        """A fork pin bump, selected BY COMPONENT (FORK_COMPONENTS, TESTING-SPEED-A item 4: every line that matches a
        changed fork file adds its tokens; a `none` line ends the file). A fork file no line names, or a diff this
        checkout cannot read, is the tier (said)."""
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
            lines = fork_lines(f)
            if not lines:
                self.full_tier.append(f"{p}: fork file {f} — no fork component line names it")
                hits["TIER"] += 1; continue
            for pat, toks in lines:
                key = "none" if toks == "none" else " ".join(toks)
                hits[key] += 1
                if toks != "none":
                    self.expand(toks, [], f"{tag}: fork {f} (component {pat[:48]})")
        if depth == 0:
            self.rationale.append((p, f"fork pin {(old or '?')[:9]} -> {(new or '?')[:9]}: {len(files)} fork file(s) "
                                      f"by component {dict(hits)} (+ the six hash lines at the merge)"))

    def _deleted(self, p):
        """True when the range deletes the file (it is at the base and not at the tip)."""
        a, b = self.revs.texts(p)
        return bool(a) and not b

    def tests_path(self, p, tag, notes, hit_argv):
        parts = p.split("/")
        d = parts[1] if len(parts) > 2 else ""
        if d == "lanes" and len(parts) > 3:
            d = parts[2]                       # tests/lanes/<LANE>/: the lane's own rows (their dir is <LANE>)
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
        self._fallback(p, tag, notes, "no suite owns this tests/ path")

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
        new_ids = set()
        if p.startswith("irisgl/engine/"):
            # AN ENGINE HEADER'S NEW SCOPE SELECTS BY ITS NAME; A NEW NAME'S USERS ARE IN THIS CHANGE (TESTING-SPEED-A item 4)
            ids, new_ids = gate_graph.new_scope_ids(a, b, gate_graph.hunks_of(self.revs.diff_u0(p)), ids)
            # AN ENUM'S COUNT CONSTANT MOVED WITH ITS APPEND (round 15: `kPhotonViewCount` 8 -> 9 beside PhotonView's new
            # GatherHits): `k<Enum>Count` of an enum that only GREW is part of the append — its users read the new
            # length, and the new member's users are in this change
            for t in sorted(ids):
                m_ = re.fullmatch(r"k(\w+)Count", t)
                if m_ and m_.group(1) not in ids and re.search(r"\benum\s+(class\s+|struct\s+)?" + re.escape(m_.group(1))
                                                              + r"\b", b) and new_ids:
                    ids, new_ids = ids - {t}, new_ids | {t}
            if new_ids:
                notes.append(f"new identifier(s) {sorted(new_ids)[:6]}: their users are in this change")
        naming = gate_graph.files_naming(ROOT, ids)
        if new_ids:
            touched = getattr(self, "_paths", set())
            naming = {f: (v - new_ids if f not in touched else v) for f, v in naming.items()}
            naming = {f: v for f, v in naming.items() if v}
        if p.startswith("irisgl/engine/"):
            # A DATA MEMBER declared on a changed line (`std::vector<GatherRay> rays;`, `unsigned raysFrame = 0u;`) is a
            # USE only as a member — `.rays`, `->rays`, `::rays`; a parameter or local of the same word is another
            # name (round 15: OgreRayQuery.cpp's `const std::vector<float> &rays` is not GatherStatus::rays)
            hl, hla = set(), set()
            for os_, oc_, ns, nc in gate_graph.hunks_of(self.revs.diff_u0(p)):
                hl |= set(range(ns - 1, ns - 1 + nc))
                hla |= set(range(os_ - 1, os_ - 1 + oc_))
            mrx = re.compile(r"^\s*[\w:<>,\s\*&]+?[\s\*&>]([a-z]\w*)\s*(?:=[^;]*|\{[^;]*\}|\[[^;]*\])?\s*;")
            # (round 21: a member REMOVED counts too — SurfaceCache.h's `lights` of the relight view)
            members = set()
            for txt_, rows_ in ((gate_graph.strip_cxx(b), hl), (gate_graph.strip_cxx(a), hla)):
                for k in rows_:
                    if k < len(txt_):
                        m_ = mrx.search(txt_[k])
                        if m_:
                            members.add(m_.group(1))
            members &= set(ids | new_ids)
            if members:
                txts = {}
                for f in list(naming):
                    hit = naming[f] & members
                    if hit and f.endswith((".cpp", ".cc", ".mm", ".h", ".hpp")):
                        txts[f] = txts.get(f) or "\n".join(gate_graph.strip_cxx(_read(os.path.join(ROOT, f)) or ""))
                        naming[f] = naming[f] - {t for t in hit if not re.search(r"(\.|->|::)\s*" + re.escape(t) + r"\b",
                                                                                 txts[f])}
                naming = {f: v for f, v in naming.items() if v}
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
        for f in reached:
            self._symbol_ids.setdefault(f, set()).update(t for t in naming[f] if t != "(companion)")
        shown = sorted(ids)[:12]
        notes.append(f"symbols {shown}{' …' if len(ids) > 12 else ''} named by {len(reached)} file(s)")
        # The files that NAME a changed identifier are the reached paths, each through its own
        # graph reach and rules; a naming header through every source that reads it with this one.
        for f in reached:
            if f.endswith(HEADER_EXT):
                if p.startswith("irisgl/engine/"):
                    # AN ENGINE HEADER'S NAMER (Engine.h's default `photonViewRefusal(PhotonView)`): a declaration's users
                    # name the identifier themselves (found above); an inline body's callers name the FUNCTION — the
                    # wrappers' names are followed one level, never every reader of the namer
                    wr = gate_graph.body_wrappers(_read(os.path.join(ROOT, f)), naming[f] - {"(companion)"})
                    if wr:
                        users = gate_graph.files_naming(ROOT, wr)
                        for g_ in sorted(u for u in users if u != f and not u.endswith(HEADER_EXT)):
                            self._symbol_ids.setdefault(g_, set()).update(users[g_])
                            self.path(g_, 1, via=f"calls {os.path.basename(f)}'s {sorted(users[g_])[0]}(), which "
                                                 f"names {sorted(naming[f])[0]}")
                    continue
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
            if p == "CMakeLists.txt":
                # THE TOP-LEVEL CMake (TESTING-SPEED-A item 1): one of the two places the whole tier is kept for
                self.full_tier.append(f"{tag}: the top-level build file with no diff to read (--files)")
                if depth == 0: self.rationale.append((p, "the top-level build file without a range → the MERGE tier"))
                return
            self._fallback(p, tag, notes, "a build file with no diff to read (--files)")
            if depth == 0: self.rationale.append((p, "; ".join(notes)))
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
        if unresolved and p.startswith("irisgl/engine/"):
            # THE ENGINE'S CMake (TESTING-SPEED-A item 4: the whole-family arm is gone): a command no reader resolves (a
            # media staging, a shader compile) reaches the subsystems of the engine files its changed text names, by the
            # table; naming none, the engine directory as a whole (every engine fixture) — a rule, never a fallback
            a_, b_ = self.revs.texts(p) if self.revs.base else ("", "")
            txt = "\n".join(l[1:] for l in (self.revs.diff_u0(p) if self.revs.base else "").splitlines()
                            if l[:1] in "+-" and not l.startswith(("+++", "---")))
            named = sorted({m.group(1) for m in re.finditer(r"((?:media|src|include)/[\w./${}-]+\.\w+)", txt)})
            got = [r for r in (self.engine_table("irisgl/engine/" + f, f"{tag} (names {f})") for f in named) if r]
            if not got:
                self.expand(["*engine-fixtures"], [], f"{tag}: the engine's build ({', '.join(unresolved)[:80]})")
            notes.append(f"the engine's CMake: {', '.join(unresolved)[:120]} -> "
                         + (f"the subsystems of {len(got)} named file(s)" if got else "every engine fixture"))
            unresolved = []
        if unresolved:
            if p == "CMakeLists.txt":
                self.full_tier.append(f"{tag}: a top-level CMake command that names nothing a suite owns: "
                                      f"{', '.join(unresolved)}")
            else:
                self._fallback(p, tag, notes, f"CMake command(s) that name nothing a suite owns: {', '.join(unresolved)}")
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
        "test_create_loop": "app.create_loop's executable (SUITE-POOL-1; open.responsive carried its check until TESTING-SPEED-B3)",
        "app.create_loop.fresh_home": "app.create_loop's fixture (SUITE-POOL-1; open.responsive carried its check until TESTING-SPEED-B3)",
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


_DATA_FACTS = {}


def data_facts(build):
    """{row: {data_dirs, ...}} of the build's rows (gate_graph.row_facts: the DATA EDGES — RUNNER-A2B), once per build
    state (the inventory's own cache key: a rebuild re-reads it)."""
    key = os.path.abspath(build)
    if key not in _DATA_FACTS:
        try:
            # the DIRS only (hashed=False): a selection reads the data edges, never the hashes (DOOR-HYGIENE-1: hashing
            # every row's data was 1.1 s of each 1.9 s call — the tool's own guard row ran out its bound under load)
            _DATA_FACTS[key] = gate_graph.row_facts(build, hashed=False)[0]
        except (OSError, ValueError):
            _DATA_FACTS[key] = {}
    return _DATA_FACTS[key]


def _read(path):
    try:
        return open(path, errors="replace").read()
    except OSError:
        return ""


def select(paths, rng, build, jobs, graph=None, inv=None, quiet_graph=False, lane=None):
    """The whole selection as data (gate.selection replays call this in-process). `lane`: the lane whose range this is —
    its own `lane:<LANE>` rows are selectable (TESTING-SPEED-A item 6)."""
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
    S = Selection(inv, graph, app_exe, Revs(rng), jobs, lane=lane)
    S.tooling_only = bool(paths) and not product_paths(paths)
    S._paths = set(paths)
    for p in paths:
        if S.tooling_only and TOOLING_OWN.match(p):
            S.tooling_path(p)
        else:
            S.path(p)
    S.data_edges(paths, data_facts(build))
    S.stage_close_subjects(paths)
    tooling_paths(S, paths)
    if graph is not None: graph.save_syms()
    return S


def tooling_paths(S, paths):
    """THE TOOLS' ROWS FOR THE TOOLS' PATHS (TESTING-SPEED-A item 5; SPECS/TESTING_SPEED_SPEC.md §2 A6): the `hygiene` and
    `tooling` rows ride a change under TOOLING_PATHS only — a product path that reached one (a lint's directory rule, a
    header's companion) does not keep it; the smoke pair rides a change to the test harness (HARNESS_PATHS) only."""
    if not any(TOOLING_PATHS.match(p) for p in paths):
        gone = [n for n in S.selected if S.inv[n]["labels"] & TOOLING_LABELS]
        S.tools_dropped = set(gone)          # still REACHED for serving (ci_gate_check.Reach): never served across it
        for n in gone:
            S.selected.pop(n, None)
            S.whole.discard(n)
            S.arms.pop(n, None)
        if gone:
            S.rationale.append(("(the tools' rows)", f"{len(gone)} hygiene/tooling row(s) a product path reached are the "
                                                     f"tools' — not this change's (the self-audit ran the hygiene label)"))
    harness = [p for p in paths if HARNESS_PATHS.match(p)]
    if harness:
        S.add(ALWAYS_ON_CODE, f"{harness[0]}: the test harness moved — the smoke pair")


ATTR_DEFECT_COMBINATION, ATTR_INCOMPLETE, ATTR_DEFECT_DBUILD = 3, 7, 5


def _hold_state(vt, env, tier, lane, rng):
    """THE FALLBACK OF THE HOLD JUST TAKEN — the first hold_card of a --solo run AND every re-hold after a
    yield_card (item 15: the re-hold drains again, and a re-drain that times out returns the SLOT only, with no
    JAH_VRAM_HELD — the callers used to keep the FIRST hold's fallback, so the rows after the yield ran on per-row
    admission with no JAH_VRAM_ALL, no stamp and no phase record, and a clean re-drain stayed stamped). Reads
    vt.LAST_DRAIN_TIMEOUT (hold_card resets it on every call): a timeout -> the phase record, the TOOL's JAH_VRAM_ALL
    (every admission takes the card itself — a fallback, never an override, F2) and "drain-timeout"; else (env, None)
    with no JAH_VRAM_ALL of ours. Returns (env, fallback|None); env is a copy."""
    env = dict(env)
    d = vt.LAST_DRAIN_TIMEOUT
    if not d or env.get("JAH_VRAM_HELD"):
        return env, None
    gate_runlog.phase_record("drain-timeout", tier, lane, rng, gate_runlog.tree_shas(), fallback="drain-timeout", **d)
    env["JAH_VRAM_ALL"] = "1"
    return env, "drain-timeout"


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
    ap.add_argument("-j", "--jobs", type=int, default=GATE_JOBS["app"],
                    help=f"ctest parallelism of this tool's own parallel phase (default GATE_JOBS['app'] = "
                         f"{GATE_JOBS['app']}; the runner's merge runs a width per pass: GATE_JOBS = {GATE_JOBS})")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--lane", action="append", default=None,
                    help="the record's `lanes` (default: the branch): repeat it or give a comma-separated list — a "
                         "gate names the lanes it is for")
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
                    help="print GATE_JOBS as JSON, the width per pass (the runner's merge run reads it), and exit")
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
        sys.stderr.write("gate-scope: --joint is retired — `runner.sh merge <lane>` runs what each merge reaches\n")
        sys.exit(2)
    a = ap.parse_args()
    if a.resume:
        a.run = True
    if a.gate_jobs:
        print(json.dumps(GATE_JOBS, sort_keys=True)); return
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
        env_, fb_ = _hold_state(gate_runlog._vram(), env_, a.tier or "solo", lane, log_range)
        return card_, env_, fb_

    def gate(what):
        """THE GATE SLOT (P1), once per gate run: queue (class 1, no bound, the position printed), then hold
        it in THIS process to its end (the ctest trees below die with it: gate_runlog F2)."""
        if not slot:
            gate_runlog.on_signals()
            who = "+".join(gate_runlog.lane_list(a.lane)) or os.path.basename(gate_runlog.ROOT)
            fd = gate_runlog._vram().gate_slot(f"{who} {what}", log=sys.stdout, cls=1)
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
                        # retry=True KEPT: gate_runlog.is_solo (the one solo predicate) reads it — this record's tier is
                        # `solo` already; the flag makes it a solo whatever the tier argument becomes
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
        # (GATE-COST-2): the batch queues (class 2, behind any waiting gate, and yields to one between runs) before it drains (hold_card does it), so its drain
        # never starves the gate in the slot into NOADMIT.
        gate_runlog.on_signals()
        card, env, fallback = solo_card(f"{'+'.join(lane)} --solo {' '.join(a.solo)[:80]}")
        try:
            for s in a.solo:
                for _ in range(a.times):
                    # A MEASUREMENT YIELDS TO A GATE BETWEEN ITS RUNS (item 13: the slot is by priority)
                    card, env_y = gate_runlog._vram().yield_card(card, env, f"{'+'.join(lane)} --solo", sys.stdout)
                    if env_y is not env:
                        # THE RE-HOLD IS A NEW HOLD (item 15): its drain decides the fallback, as solo_card's did
                        env, fallback = _hold_state(gate_runlog._vram(), env_y, a.tier or "solo", lane, log_range)
                    rx = "^" + re.escape(s) + "$"
                    r = lost(gate_runlog.run_ctest(
                        f"ctest -j1 --timeout 900 --output-on-failure --no-tests=error -R '{rx}'",
                        build, a.tier or "solo", lane, 1, reasons={s: "solo retry"},
                        # retry=True KEPT: this solo records under the CALLER's tier (a.tier) — gate_runlog.is_solo (the
                        # one solo predicate) knows it as a solo by this flag only
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
        # siblings' change, gated in their own merge — the combination is that merge's.
        short = lambda r: "..".join(x[:9] for x in r.split(".."))
        print(f"gate-scope: {len(fwd)} forward merge(s) in {a.range} — the lane's OWN change is "
              f"{short(own_rng)} (from the newest one's second parent {fwd[0][1][:9]})")
        if incoming:
            print(f"  what came in through them ({short(incoming)}) is NOT this gate's: the combination is the merge's "
                  f"(runner.sh merge)")
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

    whole_tier = bool(S.full_tier)
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
        print("\nTOOLING-ONLY DIFF (TESTING_V3 §1.3.4): every touched path is NON_PRODUCT (gate-scope.py) "
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
    if S.full_tier:
        print("\nTHE MERGE TIER BY RULE (the change reaches every suite):")
        for f in S.full_tier: print(f"  {f}")
        run_tier("rule"); return
    if S.fallback:
        # NEVER THE WHOLE TIER (TESTING-SPEED-A item 1): a path nobody owns selected its directory's subsystem
        print("\nFALLBACK (a touched path has no rule, no symbol and no graph owner — its DIRECTORY's subsystem is "
              "selected, never the whole tier; a rule for it belongs in AREA_RULES):")
        for f in S.fallback: print(f"  {f}")
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
        print("\nSCOPED tier: NOTHING to gate — every touched path is docs/data/a price with no owning suite, or a "
              "comment (the six hash lines are every merge's whole-engine guard)")
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
