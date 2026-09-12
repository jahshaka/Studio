# Headless test suites. No Qt, no app: each suite links exactly one library.
#
# THE ON-SCREEN SUITES' DISPLAY is a cache variable, not a literal. A handful of
# suites need a real window (the application self-tests, the WinIdChange one),
# and they used to pin ":0" — which on a developer machine means "the owner's
# live session": a parallel worktree build had no way to run its own gate
# without opening windows on somebody else's desktop. Configure with
# -DJAH_TEST_DISPLAY=:NN to point them at an Xvfb of your own.
#
# DEFAULT CHANGED 2026-09-06: empty = inherit ctest's own DISPLAY. The old ":0"
# default meant a plain `ctest` in ANY worktree opened windows on the owner's
# desktop no matter what DISPLAY the runner exported (three lanes hit it in one
# day — the per-test ENVIRONMENT pin overrides the process environment). An
# explicit value still pins; empty follows the environment, which is what every
# rig-run and every gate actually wants.
set(JAH_TEST_DISPLAY "" CACHE STRING
    "X display the on-screen test suites run on (Linux only; empty = inherit DISPLAY)")
if(JAH_TEST_DISPLAY STREQUAL "")
    set(JAH_TEST_DISPLAY_ENV "")
else()
    set(JAH_TEST_DISPLAY_ENV "DISPLAY=${JAH_TEST_DISPLAY};")
endif()

# THE SANITIZER LANES (deep audit 2026-09, area 3 finding 4: "no sanitizer has
# ever seen the app"). Two ctest LABELS, no duplicate test registrations and no
# second copy of any suite — `scripts/sanitize.sh <lsan|tsan>` supplies the
# sanitizer, the label says which suites the lane runs. A plain `ctest` is
# completely unaffected; neither lane is part of the default gate.
#
#   jah_lsan_lane(<test>...)   LeakSanitizer, over the ORDINARY Debug build.
#       No rebuild is involved: sanitize.sh LD_PRELOADs the standalone LSan
#       runtime, which intercepts malloc/free process-wide whether or not the
#       code was compiled with -fsanitize=address (CTest's ENVIRONMENT property
#       adds to the inherited environment rather than replacing it, so the
#       preload reaches every test process). The binaries under test are
#       therefore bit-identical to the ones the normal gate runs.
#       MEMBERSHIP RULE: only suites that never initialise Vulkan and never
#       spawn the app binary. The four ASan-built engine suites run with
#       detect_leaks=0 because the Vulkan loader's Mesa device-select layer
#       leaks ~6 dbus allocations that are not ours; that justification does not
#       extend to code which never loads a driver, and this lane is what makes
#       the difference visible.
#
#   jah_tsan_lane(<test>...)   ThreadSanitizer, in its OWN build directory
#       (TSan cannot combine with ASan and instruments every TU):
#       cmake -B build-tsan -DJAHSHAKA_TSAN=ON. MEMBERSHIP RULE: the suites
#       whose subject is an async contract — a runner, a heartbeat, a watchdog,
#       a shutdown order.
#
#   jah_lsan_blocked(<test>...) / jah_tsan_blocked(<test>...)   the quarantine.
#       A suite that is eligible for a lane and does NOT pass it today, for a
#       reason that is not the suite's fault and is too structural to fix in
#       passing. These get the label `lsan-blocked` / `tsan-blocked` instead:
#       they stay one command away (`ctest -L '^lsan-blocked$'`, and
#       sanitize.sh names them at the end of every run) but they do not hold the
#       gate permanently red. Every call says WHAT blocks it. Moving a suite
#       back into its lane is then a one-word edit. Nothing here is suppressed
#       and nothing is hidden — a quarantined suite is a defect with a name.
#
#       The leak blockers as of 2026-09-04, all found by this lane's first run:
#         L1  iris::Material never frees its QList<Property*> — every material
#             ever constructed leaks its ~19 properties (irisgl/document/
#             materials/material.h:53 + the empty ~Material at :74). NOT a
#             one-liner: duplicate() hands the same raw pointers to a second
#             material, so a qDeleteAll in the destructor double-frees.
#         L2  iris::LightNode::shadowMap is `new ShadowMap()` with no
#             destructor (irisgl/document/scenegraph/lightnode.cpp:243) —
#             12 bytes per light node, forever.
#         L3  every imported asset leaks its AssetNodeObject and the whole
#             mesh subtree hanging off it (src/services/import/
#             assetimporters.cpp:319 into AssetManager::addAsset) — the deep
#             audit's "clearAssetList() deletes nothing", independently
#             confirmed here.
#         L4  the materials module's node library owns raw NodeModel/
#             SocketModel/Connection pointers with no destructors anywhere
#             (src/modules/materials/models/library.cpp:38, graph/
#             nodegraph.cpp:74 and :140) — the whole library leaks per build.
#         L5  iris::KeyFrame<T>::addKey's Key<T>* and the KeyFrames a
#             PropertyAnim news up are never freed (irisgl/document/animation/
#             keyframeanimation.h:120, propertyanim.cpp:42-44).
function(jah_lsan_lane)
    foreach(_t IN LISTS ARGV)
        set_property(TEST ${_t} APPEND PROPERTY LABELS lsan)
    endforeach()
endfunction()

function(jah_lsan_blocked)
    foreach(_t IN LISTS ARGV)
        set_property(TEST ${_t} APPEND PROPERTY LABELS lsan-blocked)
    endforeach()
endfunction()

function(jah_tsan_lane)
    foreach(_t IN LISTS ARGV)
        set_property(TEST ${_t} APPEND PROPERTY LABELS tsan)
    endforeach()
endfunction()

#   jah_tsan_blocked(<test>...)   the race lane's quarantine, same idea as
#       jah_lsan_blocked. As of 2026-09-04 it holds every suite that drives the
#       REAL Jahshaka binary, for a reason that is not ours: GCC 15's libtsan
#       aborts the app during startup with
#           ThreadSanitizer: CHECK failed: sanitizer_thread_registry.cpp:186
#           "((live_.try_emplace(user_id, tid).second)) != (0)"
#       from the pthread_create interceptor under QThreadPool::start, reached
#       from QImage::convertToFormat_helper while ProjectManager's setupUi loads
#       a pixmap (src/ui/pages/projectmanager.cpp:71). It is TSan's own thread
#       registry rejecting a recycled pthread_t, it happens before the app ever
#       reaches its event loop, and nothing in our code can work around it.
#       Clang's TSan is the untried alternative (no clang on this box).
function(jah_tsan_blocked)
    foreach(_t IN LISTS ARGV)
        set_property(TEST ${_t} APPEND PROPERTY LABELS tsan-blocked)
    endforeach()
endfunction()

# THE DOCUMENT GRAPH'S ENGINE (SPECS/SCENEGRAPH_SPEC.md D2 / §3b v2).
#
# Since the scene-graph swap, an iris:: document node IS an Ogre scene node, so
# a suite that builds a document needs an ENGINE — even when it renders nothing.
# That engine is HEADLESS: RenderSystem_NULL, no display, no driver, no window
# (tests/support/documentgraph.h). It was a surfaceless VULKAN engine for one
# release — the v1 interim, which made every document suite need a reachable X
# server and a GPU; jah_no_display() below is the proof that it no longer does.
#
# What it takes: link JahshakaEngine, know where the plugins and Hlms media are,
# find Ogre's shared libraries at run time, and wait for the media staging.
# tests/support/documentgraph.h is the one line each suite's main() adds.
function(jah_document_graph _t)
    target_compile_definitions(${_t} PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    target_link_libraries(${_t} PRIVATE JahshakaEngine)
    set_target_properties(${_t} PROPERTIES
        BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    add_dependencies(${_t} JahshakaEngineMedia)
endfunction()

# DISPLAY-FREE, ENFORCED (SPECS/SCENEGRAPH_SPEC.md §3b). Strips DISPLAY and
# WAYLAND_DISPLAY from the test's environment, so a suite that claims to need no
# graphics stack is RUN without one on every single ctest invocation — including
# on this box, where a display is always reachable and the claim would otherwise
# be untested. Apply it to every document/headless suite; never to a suite that
# renders (they need a display, honestly and permanently).
# THE TIMEOUT POLICY (TEST_GATE_AUDIT.md §3, applied across tests/ 2026-09-09).
#
# Every row's TIMEOUT is 6x its measured seconds with a floor, and nothing else.
# It used to be whatever number felt safe when the row was written: 25 suites
# sat between 38x and 173x of their measured time and a hundred more above that,
# with 600 s and 900 s ceilings on suites that take one second. That is not
# safety, it is a hang costing ten minutes of a gate the owner wants to finish
# in ten.
#
# THE FLOOR IS 120 s, NOT THE AUDIT'S 60. Contention on this box is measured in
# multiples, not percentages: samples.cleanstart.Matcaps runs 18.7 s solo and
# 36.5 s at -j2, CLAUDE.md's gate facts record threading.newproject_stall at
# 1.8x headroom under four live Vulkan lanes, and most of these rows are three
# to seven seconds of which four are an engine boot. A 60 s floor on a five
# second boot-bound suite is twelve-fold headroom that one bad afternoon eats;
# 120 keeps the whole point of the change (a hang costs two minutes instead of
# ten) with margin nobody has to argue about.
#
# The CONTENTION CLASS — CLAUDE.md's solo-retry list — gets 300 s instead:
# open.responsive, archive.responsive, app.engine_selftest_validation,
# threading.{newproject_stall,mode,mode_serial}, scripting.e2e.{player_verbs,
# space_switch,sun_light}, gi.budget, app.watchdog_stall, claude.chat,
# ui.media_lazy, shadergraph.bake_output and the perf.* suites.
#
# DOCUMENTED EXCEPTIONS, each with its reason at its own registration:
# cameras.e2e (900, it times out under GI-rebuild contention starving the
# mode-2 compile pool) and app.input_keys (600, one long chain of synthesised
# keys with fixed sleeps between them).
#
# A new row takes 6x its own measured seconds, or the floor, whichever is more.
# ROWS THAT SET NO TIMEOUT (46 of them: the sub-second document suites, the
# wipe fixtures) get ctest's DEFAULT, which every documented gate command sets
# to `--timeout 120` (docs/TESTING_GATE.md) — never ctest's own 1500 s. The
# contention class is per row (engine_selftest_validation 180, ui.media_lazy
# 120, shadergraph.bake_output 120, claude.chat the default), not a flat 300.

# WHITE-BOX IMPORTER SUITES. assimp is IrisGL's PRIVATE dependency (ENGINEERING
# DEBT L4, 2026-09-10): nothing that links libIrisGL sees its headers or its
# symbols. The handful of suites that deliberately drive the vendored importer
# itself — asserting what assimp hands over (a patch regression, a raw-vs-preset
# comparison, the tps a clip file reports) before asserting what IrisGL makes of
# it — declare that here, explicitly, one line per target. A suite that only
# needs "a parsed model" goes through iris::SceneSource and needs nothing.
function(jah_links_assimp _t)
    target_include_directories(${_t} PRIVATE
        ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
        ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include)
    target_link_libraries(${_t} PRIVATE assimp)
endfunction()

function(jah_no_display)
    foreach(_t IN LISTS ARGV)
        set_property(TEST ${_t} APPEND PROPERTY ENVIRONMENT_MODIFICATION
                     "DISPLAY=unset:;WAYLAND_DISPLAY=unset:")
    endforeach()
endfunction()

add_subdirectory(engine)
add_subdirectory(document)
add_subdirectory(math)
add_subdirectory(mirror)
add_subdirectory(particles)
add_subdirectory(picking)
add_subdirectory(app)
add_subdirectory(gizmo)
add_subdirectory(thumbnails)
add_subdirectory(player)
add_subdirectory(skeletal)
add_subdirectory(gi)
add_subdirectory(lights)
add_subdirectory(shadow)
add_subdirectory(planar)
add_subdirectory(ssr)
add_subdirectory(looks)
add_subdirectory(distortion)
add_subdirectory(assets)
add_subdirectory(assetdelete)
add_subdirectory(assetpaths)
add_subdirectory(assetgc)
add_subdirectory(assetmigrate)
add_subdirectory(shadergraph)
add_subdirectory(materialpreview)
add_subdirectory(desktops)
add_subdirectory(drawers)
add_subdirectory(assetmeta)
add_subdirectory(audiopeaks)
add_subdirectory(scripting)
add_subdirectory(api)
add_subdirectory(reopen)
add_subdirectory(services)
add_subdirectory(perf)
add_subdirectory(commands)
add_subdirectory(shortcuts)
add_subdirectory(input)
add_subdirectory(mcp)
add_subdirectory(claudechat)
add_subdirectory(theme)
add_subdirectory(export)
add_subdirectory(importer)
add_subdirectory(meshbake)
add_subdirectory(importasync)
add_subdirectory(openasync)
add_subdirectory(archiveasync)
add_subdirectory(avatarasync)
add_subdirectory(shutdown)
add_subdirectory(ui)
add_subdirectory(avatar)
# The shipped sample scenes on a VIRGIN HOME, one test each — the owner's
# post-wipe first run (samples.cleanstart.*).
add_subdirectory(samples)
# The shader cache's container (SHADER_CACHE_SPEC §4.5). AFTER tests/engine:
# it reuses that directory's JAHSHAKA_ASAN option and its sanitised backend.
add_subdirectory(shadercache)
# Scene cameras (CAMERAS_SPEC phase 1): the document half, the play-routing
# seam in pixels, and the verb + round-trip e2e.
add_subdirectory(cameras)
# Sockets (CAMERAS_SPEC §5 / phase 4, owner decision D9): the socket maths and
# the avatar bone-name table, the whole engine-pose -> socket -> pixels chain,
# and the verbs + round trip in the real binary.
add_subdirectory(sockets)
# THREADING ADOPTION (SPECS/THREADING_ADOPTION_SPEC.md): the frame loop's
# scene gate and the staging-manager race it closes.
add_subdirectory(threading)
# The scene-graph BENCHMARK GATE (SCENEGRAPH_SPEC §6) — the measurement suite
# that exists BEFORE the swap, so v1 has a recorded baseline to be judged
# against. Label `benchmark`: minutes, not seconds; `ctest -LE benchmark` skips it.
add_subdirectory(benchmarks)
# The session log (SPECS/SESSION_LOG_SPEC.md): log.core (the sink, no display),
# log.events (verbs + event markers, real binary), log.engine (the Ogre
# listener + device rows) and log.perf (the periodic sampler).
add_subdirectory(log)
# Source-hygiene gates (no binary, no display): source.no_glsl_path guards the
# HLMS_ADOPTION P3 deletion of the document-side GLSL load path.
add_subdirectory(hygiene)
# GENERATED SHADER PIECES (HLMS_ADOPTION P5): the backend mechanism the shader
# graph's emitter targets, and the emitter's own gates.
add_subdirectory(pieces)
add_subdirectory(apppaths)
# WHO IS ALLOWED TO SEE A MODAL WINDOW (owner decision D3): the first-launch
# greeting's driven-session predicate, src/app/firstrun.h.
add_subdirectory(firstrun)
