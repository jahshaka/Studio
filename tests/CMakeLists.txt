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

# EVERY TEST BOOTS THE DESKTOP ROUTE UNLESS IT SAYS OTHERWISE (VR-SETTING-1). The
# app's "Start in VR" preference is ON by default, so an app launched by a test
# with a fresh data root would ask the OpenXR runtime for its boot. This walk runs
# ONCE, after every directory below has registered its tests, and appends
# JAHSHAKA_VR=0 to each test's ENVIRONMENT — the one place, so no suite's own
# ENVIRONMENT line (which REPLACES the property) can forget it. A VR suite passes
# `--vr`, which wins over the environment (CliOptions::resolveVr). A test that
# names JAHSHAKA_VR itself is left alone: `JAHSHAKA_VR=` (empty) is how
# app.start_in_vr lets the preference decide.
function(_jah_vr_off_walk dir)
    get_property(_tests DIRECTORY "${dir}" PROPERTY TESTS)
    foreach(_t IN LISTS _tests)
        get_property(_env TEST "${_t}" DIRECTORY "${dir}" PROPERTY ENVIRONMENT)
        if(NOT _env MATCHES "(^|;)JAHSHAKA_VR=")
            set_property(TEST "${_t}" DIRECTORY "${dir}" APPEND PROPERTY ENVIRONMENT "JAHSHAKA_VR=0")
        endif()
    endforeach()
    get_property(_subs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(_s IN LISTS _subs)
        _jah_vr_off_walk("${_s}")
    endforeach()
endfunction()
if(CMAKE_VERSION VERSION_LESS 3.28)
    message(FATAL_ERROR "tests/: CMake 3.28+ is needed (set_property(TEST ... DIRECTORY), the JAHSHAKA_VR=0 walk)")
endif()
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _jah_vr_off_walk "${CMAKE_CURRENT_SOURCE_DIR}")

# THE ASan ENGINE SUITES' OPTIONS, in one place (PHOTON_SPEC §7 R1).
#
# `protect_shadow_gap=0` is LOAD-BEARING and was MEASURED, not guessed. With the
# hardware ray-query tier enabled (fork d014b064f+b028638c1 (was 0038/0039)), `vkCreateDevice`
# returns VK_ERROR_INITIALIZATION_FAILED under AddressSanitizer on NVIDIA
# 595.84 — on the very GPU where the same call succeeds without ASan, and with
# no validation error: the driver simply refuses. The cause is not the tier but
# ASan's SHADOW GAP: the driver's ray-tracing library maps at fixed addresses
# that fall inside the range ASan protects, exactly as CUDA does (the documented
# CUDA-under-ASan workaround is the same option). Turning the gap protection off
# lets the device come up and every ASan engine suite passes WITH the tier live,
# which is what gives the ray TU its sanitizer coverage.
#
# The alternative — booting these suites with JAHSHAKA_NO_RAY_QUERY=1 — was what
# this lane shipped first, and it is strictly worse: it buys the same green by
# making sure the new code is the one thing the sanitizer never sees.
#
# The cost is a real but narrow loss of coverage: ASan no longer traps a wild
# pointer that happens to land in the shadow gap. Every other check is unchanged.
set(JAH_ASAN_ENGINE_OPTIONS "ASAN_OPTIONS=detect_leaks=0:protect_shadow_gap=0")

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
#       scripts/sanitize.sh tsan (build-tsan-clang, clang required). MEMBERSHIP RULE: the suites
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
#       Every lsan-blocked row was run once more 2026-10-11 (RUNNER-A7A): all red for these blockers, now
#       registered (LSAN-L1..L4 — the registry file testing/defects.json went with TESTING-SIMPLE; each call names its ids). The app-spawning rows' harnesses
#       ignore the app's exit code and its report, so a green there proves nothing about the app.
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
#   jah_tsan_render(<test>...)   THE RENDER THREAD'S TSan ARM (ENGINE-UPGRADE-U3 decision 47): label tsan-render, run by
#   hand (JAH_TSAN_LABEL=tsan-render scripts/sanitize.sh tsan) — green = zero reports. Never the shipped lane.
function(jah_tsan_render)   # called in the directory that registers the row (a TEST property is per directory)
    foreach(_t IN LISTS ARGV)
        set_property(TEST ${_t} APPEND PROPERTY LABELS tsan-render)
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
#       MEASURED WITH CLANG 2026-10-16 (ENGINE-UPGRADE-U3 decision 49: clang 21's own libtsan, build-tsan with
#       CMAKE_CXX_COMPILER=clang++, lavapipe): the app BOOTS — and after ~14 minutes of its first boot the same
#       registry CHECK fires from the same interceptor (QThreadPool::start under QPixmap::scaled for a QLabel icon):
#       the recycled pthread_t is Qt's, uninstrumented, and both runtimes reject it. host.sync_getters and
#       host.error_door stay quarantined; the render thread's proofs run as ENGINE FIXTURES under TSan (label
#       tsan-render: frame.record_determinism, engine.message_order, frame.drag_latency, frame.interpolation,
#       view.window_on_render_thread, engine.error_door).
#       MEASURED AGAIN 2026-10-11 (RUNNER-A7A, every row run once in build-tsan with lavapipe): the registry CHECK
#       now fires in 4 of the 27 rows only; the app boots in the rest, and every boot reports a race in OUR
#       engine's LOD update (irisgl/engine/src/OgreMesh.cpp lodUpdateImpl on the updateAllLodsThread workers), and
#       none finishes inside its harness's boot wait or its TIMEOUT under TSan. Registered: TSAN-LOD-UPDATE-RACE,
#       TSAN-GCC15-THREAD-REGISTRY, TSAN-BOOT-TOO-SLOW (the registry file testing/defects.json went with TESTING-SIMPLE;
#       the ids are the ledger's); each call below names its ids.
#       TSAN-LOD-UPDATE-RACE FIXED 2026-10-12 (LOD-RACE-1): the race was lodSet writing the object memory
#       manager's shared padding dummy from two workers; the fork's lodSet skips padding slots (row
#       lod.padding_untouched), the race line is gone from open.frames under TSan, and the calls below name
#       the blockers that remain.
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
# THE STATISTIC (lead decision, TESTING-CLEANUP-2B fix round, 2026-10-10): "measured" is the run log's QUIET MEDIAN
# (PASS runs with no sibling ctest), and the TIMEOUT is never below the QUIET WORST measured UNDER THE SLOT (runs since
# 2026-10-09 18:00 — one gate at a time, the honest regime): max(6 x quiet median, slot-era worst), rounded up to 10 s.
# Never a contended worst (before the slot a "worst" was another lane's load), never the median alone.
# THE SAMPLE (lead, TESTING-CLEANUP-2B fix round after read 4): the median is over the run log's SLOT-ERA quiet PASS runs
# (the archive, testing/runs — a lane's own re-measure is a set of records IN it, never a replacement for it), and only
# runs taken the way the GATE runs the row: a row the gate runs COLD (a fresh rc tree's home: open.responsive,
# open.responsive.timing — their homes keep the shader cache, so only a tree's first run is cold) is measured on its
# cold runs; warm repeats in one home are not its sample. Each TIMEOUT comment names the sample: n, the median, the
# slot-era worst, cold/warm, the date, and what was on the box.
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
#
# RE-APPLIED 2026-09-27 (lane D6B-GATE-SHAPE; audit §9 H2): 171 registrations had drifted
# back above it — 900 s on 3-23 s suites, 600 s on sixteen gi suites, 300 s on ~100 engine
# rows whose policy value is the floor. Each was set to 6x the larger of the audit's gate
# (2026-09-26) and D6B's before-run (-j4 beside two other lanes' gates), rounded up to a
# minute, floor 120; none was widened. The contention class and the documented exceptions
# kept their values. (A row in a RESOURCE_LOCK family does not start its clock until its
# lock is free, so a family chain costs no row its budget.)
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

# THE FIXTURE PARSE (SHIPPED-BAKES-1): assimp is import-time only, so the
# "parse a file into iris::Meshes" helper is test support, not IrisGL —
# tests/support/testmesh.cpp, in this static library. Every suite whose sources
# include support/testmesh.h is linked to it automatically at the end of this
# file (jah_link_testmesh_users), so a new suite needs no line of its own.
add_library(jah_testmesh STATIC ${CMAKE_CURRENT_SOURCE_DIR}/support/testmesh.cpp)
set_target_properties(jah_testmesh PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
target_include_directories(jah_testmesh PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl)
jah_links_assimp(jah_testmesh)
target_link_libraries(jah_testmesh PUBLIC IrisGL Qt6::Core)

# THE MEASURING ROWS (TESTING-SIMPLE; DOCS/PROCESS.md, Testing). A suite that measures TIME or GPU BUDGET prints a number
# that reads as a red when anything else shares the GPU with it: these suites — and only these — are REGISTERED THROUGH
# scripts/gpu-exclusive.sh (the one lock on the card) on all its slots, and carry the label `measure`: they run at the
# sprint's ship only, alone on the card, and are never served. A suite joins the list by a verdict, never by convenience.
set(JAH_GPU_EXCLUSIVE "${CMAKE_SOURCE_DIR}/scripts/gpu-exclusive.sh")
set(JAH_GPU_EXCLUSIVE_SUITES
    # the timing suites of the push tier
    app.input_keys app.play_select shadergraph.bake_output gi.budget
    app.watchdog_stall
    # ENGINE-UPGRADE-U3: a 7 ms UI stall's dropped headset refreshes (a GUARD since stage 5) and the extract at K = 100
    vr.ui_stall gi.extract_cost vr.session_pace vr.xr_thread
    # POST-C-FIXES-1: GPU-budget suites that measured outside the lock (the verdicts:
    # gi.field_scroll flaked 1 of 2 on the base under a sibling's load; gi.rt_reflect_cost
    # read 1.213 ms against 0.90 and 8.76x against 7.5x inside a -j2 gate, 3/3 solo).
    gi.field_scroll gi.rt_reflect_cost
    # D6B-GATE-SHAPE (audit §4 R3): timing ratios that sat under RUN_SERIAL only.
    mirror.scale perf.epic_steady_state perf.drag_mirror_room perf.capture_off_is_free
    # the stage-close timing rows: the wall-clock benchmark, the stereo frame budget, and the
    # millisecond halves of the suites whose counts stay at push (tests/support/timingbars.h;
    # audit §5 — F17's seven and the six beside them).
    scenegraph.benchmark rigperf.benchmark gi.gather_cost vr.frame_budget
    open.responsive.timing archive.responsive.timing avatar.responsive.timing
    chain.hzb.timing engine.gpu_cull.timing gi.field_follows.timing ui.properties_filter.timing
    ui.components.timing meshbake.cards.timing app.update_check.timing app.shutdown_order.timing
    vr.warmup.timing scripting.e2e.editor_controls.timing
    # TESTING-DEBTS-1 T10: the first create's 1000 ms bar (the push row keeps the counts).
    threading.newproject_stall.timing
    # TESTING-CLEANUP-2 P10e/P10f: the selection's and the recorder's millisecond bars
    # (the gating row/pool arms keep the counts and print the milliseconds).
    ui.selection_cost.timing capture.first_recording.timing capture.offline_unique.timing
    # D1-SCALE-FIXTURES: the scale rows that print GPU or frame milliseconds or the UI
    # thread's gaps (tests/scale/CMakeLists.txt) — measurements by design, so a sibling's
    # GPU work inside their window would print someone else's number.
    scale.world scale.voxel_scroll scale.cluster_cut scale.decode scale.tlas scale.cpu_walks
    scale.library
    # ATOM-CLUSTER-CUT: the flat cut's milliseconds at 10k instances (D1's decision number).
    scale.cut_cost
    # ATOM-SHADOWS-1: the caster pass's CPU and GPU ms, cut against per-instance (paired arms).
    scale.shadow_cut
    # TEST-1 (the perf audit's F5): the frame's own GPU span against its monitor rows — a share
    # of two GPU times read in one frame, which a sibling's GPU work inside the span would inflate.
    scale.gpu_coverage)
# THE CARD'S SLOTS (TESTING-SIMPLE): the boot declaration and the width per class (tests/support/test_boot.cmake), the
# resource spec ctest schedules the GPU rows on (tests/cmake/vram_rows.cmake). No admission wraps a row: one ctest at a
# time holds the card (scripts/gpu-exclusive.sh), and inside it ctest keeps each class to its width.
include(${CMAKE_CURRENT_SOURCE_DIR}/support/test_boot.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/vram_rows.cmake)
jah_vram_resource_spec()

# THE BOOT DECLARATION ON THE ROW (TEST-NEEDS-1): JAHSHAKA_TEST_TIER / JAHSHAKA_TEST_NEEDS as
# `set:` ENVIRONMENT_MODIFICATION entries, appended at the end of the row's directory (after the
# site's own set_tests_properties, which would replace a list set now) — ctest's listing then
# shows exactly what the process boots, and source.row_declares_needs reads it there.
function(_jah_test_boot_register _row _tier _why _window)
    jah_test_boot_env(_env "${_tier}" ${ARGN})
    # TEST_WINDOW — the process boots its window at 1280x720 (services/testtier.h). Its OWN
    # declaration (TEST-NEEDS-1 fix round F6): a tier never implies it, so a row that gained a
    # tier keeps the screen-sized window its bars were measured at. The rows TEST-TIER-1 measured
    # at Low with the small window carry it.
    if(_window)
        list(APPEND _env "JAHSHAKA_TEST_WINDOW=set:1280x720")
    else()
        list(APPEND _env "JAHSHAKA_TEST_WINDOW=unset:")
    endif()
    # NEEDS_WHY "<claim>" — the one way a row that READS THE PICTURE declares NONE: its claim is
    # the picture with every switchable feature off (a "GI off" claim). Only beside NONE; the
    # lint (source.row_declares_needs) refuses NONE on a picture row without it, and records it.
    if(_why)
        if(NOT "NONE" IN_LIST ARGN)
            message(FATAL_ERROR "${_row}: NEEDS_WHY stands only beside NEEDS NONE")
        endif()
        list(APPEND _env "JAHSHAKA_TEST_NEEDS_WHY=set:${_why}")
    endif()
    set_property(GLOBAL PROPERTY JAH_TEST_BOOT_${_row} "${_env}")
    cmake_language(EVAL CODE "cmake_language(DEFER CALL _jah_test_boot_apply [[${_row}]])")
endfunction()
function(_jah_test_boot_apply _row)
    get_property(_env GLOBAL PROPERTY JAH_TEST_BOOT_${_row})
    set_property(TEST ${_row} APPEND PROPERTY ENVIRONMENT_MODIFICATION ${_env})
endfunction()
# A row that starts the APP declares TIER + NEEDS: every CLASS app row (the app, or a harness that
# spawns it), and a CLASS vr row whose command names the app target (a VR engine suite such as
# vr.session is an engine binary in the vr class). Every other row declares neither: an engine
# binary sets its GI in C++, and the selftest route binds no editor scene.
function(_jah_test_boot_declared _who _class _tier _cmd _out)
    set(_needs ${ARGN})
    set(_runs_app FALSE)
    if(_class STREQUAL "vr")
        foreach(_w IN LISTS _cmd)
            if(_w STREQUAL "${CMAKE_PROJECT_NAME}" OR _w MATCHES "TARGET_FILE:${CMAKE_PROJECT_NAME}>")
                set(_runs_app TRUE)
            endif()
        endforeach()
    endif()
    if(_class STREQUAL "app" OR _runs_app)
        jah_test_boot_check("${_who}" "${_tier}" ${_needs})
        set(${_out} TRUE PARENT_SCOPE)
    else()
        if(_tier OR _needs)
            message(FATAL_ERROR "${_who}: this row starts no app (CLASS ${_class}) and takes no "
                                "TIER/NEEDS — only a row that starts the app declares what it boots")
        endif()
        set(${_out} FALSE PARENT_SCOPE)
    endif()
endfunction()

# THE MEASURE LABEL (TESTING-SIMPLE): every measuring row carries the label `measure`, appended at the end of its directory
# (after the site's own set_tests_properties(... LABELS ...), which replaces the list) — it runs at the sprint's ship only
# (sprint.sh ship), alone on the card, and is never served or selected for a spec.
function(_jah_measure_label _row)
    get_test_property(${_row} LABELS _l)
    if(NOT _l)
        set(_l "")
    endif()
    list(APPEND _l measure)
    list(REMOVE_DUPLICATES _l)
    set_tests_properties(${_row} PROPERTIES LABELS "${_l}")
endfunction()
# THE DATA EDGES (RUNNER-A2B; scripts/gate_graph.py data_set): every row's JAH_DATA test property names the data sets it
# reads beside its executable — `engine-media` (irisgl/engine/media: what every RENDERING row resolves from beside its
# binary), `library-template` (app/content + app/templates: what an APP row's library starts from), `samples` (scenes/)
# or `samples/<name>` (one sample), `fixtures/<tests dir>` (tests/<dir>/fixtures), or `NONE` (a row that reads nothing).
# Declared ONCE per family: the row macros by CLASS (_jah_data: rendering classes -> engine-media; app/vr/headless ->
# library-template), their `DATA` clause for what a row adds, and jah_data_pass (the end of this file) by directory.
# A row that names no data file and declares nothing has UNKNOWN data: the runner never serves its green (rule 2).
function(_jah_data _row _class)
    set(_d ${ARGN})
    if(_class MATCHES "^(app|selftest|engine|vr)$")
        list(APPEND _d engine-media)
    endif()
    if(_class MATCHES "^(app|vr|headless)$")
        list(APPEND _d library-template)
    endif()
    if(_d)
        list(REMOVE_DUPLICATES _d)
        set_property(TEST ${_row} APPEND PROPERTY JAH_DATA ${_d})
    endif()
endfunction()

# jah_gpu_row(<row> CLASS app|selftest|engine|vr [TIER <tier> NEEDS <features…>|NONE [NEEDS_WHY "<claim>"] [TEST_WINDOW]] [TOKENS <k>]
#             [FRAMES <n>] COMMAND <exe-or-target> [args...] [WORKING_DIRECTORY <dir>] [DATA <sets…>])
# FRAMES <n> (TESTING-SPEED-A item 7; SPECS/TESTING_SPEED_SPEC.md §2 A8): THE FRAMES THE ROW RENDERS, stated — the test
# property JAHSHAKA_TEST_FRAMES (a row declared by add_test / jah_gpu_exclusive_test sets the same property with
# set_tests_properties). source.testing_rules reads the PROPERTY (ctest --show-only=json-v1) and reds a kept GPU row
# without it beyond tests/hygiene/frames_pending.txt (the ratchet Part B empties); a row renders seconds, never minutes.
# CLASS app|vr REQUIRES TIER + NEEDS (tests/support/test_boot.cmake): the row's process boots that
# World Mode with only the named features on.
# add_test() on the card's slots — the ONE way a row that boots Vulkan is registered (source.gpu_rows_closure fails a
# Vulkan row that is not): its class (JAH_CLASS) and its slots (RESOURCE_GROUPS, tests/cmake/vram_rows.cmake). A TARGET
# name as the first COMMAND word becomes $<TARGET_FILE:...>. A pool is registered by jah_add_pool.
# CLASS engine BOOTS WARM (ENGINE-UPGRADE-1 stage 1, BOOT; DOCS/PROCESS.md: "every engine fixture boots on the shared warm
# shader cache"): the row runs as `fresh_home.sh --warm --cache engine <this dir's build>/homes/<row> -- env
# JAHSHAKA_SHADER_CACHE_DIR=<home>/.local/share/Jahshaka/shadercache JAH_TEXTURE_CACHE=0 <exe> …` — the fixtures' one
# shared cache (<build>/tests/.shadercache-engine, keyed apart from the app rows' .shadercache: measured, the app rows'
# union reached 268 MB = the engine's 256 MB cap and its replay cost a fixture 0.4-0.8 s a boot) hard-linked in, a green
# run that grew it promoted, the home wiped after (a red one moved to the evidence); the fixture helper's hook (tests/support/enginetesthelpers.h applyHarnessDefaults) copies the
# variable into the config, and a fixture that states its own cache (shaderCacheDir / shaderCacheOff) still wins. The
# texture sidecar is OFF on engine rows (its key is a file's path; a shared one could serve a stale channel count). The
# variables ride the COMMAND, never ENVIRONMENT: a later set_tests_properties(… ENVIRONMENT …) replaces that property.
# COLD "<why>" — a row whose SUBJECT is the cold compile (a count of compiles a warm cache would hide, the cache itself):
# no wrapper, no cache, the reason stated where the row is.
function(jah_gpu_row _row)
    cmake_parse_arguments(G "TEST_WINDOW" "CLASS;TIER;TOKENS;WORKING_DIRECTORY;NEEDS_WHY;FRAMES;COLD" "NEEDS;COMMAND;DATA" ${ARGN})
    if(NOT G_CLASS OR NOT G_COMMAND)
        message(FATAL_ERROR "jah_gpu_row(${_row}): CLASS and COMMAND are required")
    endif()
    if(DEFINED G_FRAMES AND NOT G_FRAMES MATCHES "^[0-9]+$")
        message(FATAL_ERROR "jah_gpu_row(${_row}): FRAMES '${G_FRAMES}' is not a whole number of frames")
    endif()
    if(G_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "jah_gpu_row(${_row}): unexpected arguments '${G_UNPARSED_ARGUMENTS}'")
    endif()
    _jah_test_boot_declared("jah_gpu_row(${_row})" "${G_CLASS}" "${G_TIER}" "${G_COMMAND}" _declared ${G_NEEDS})
    jah_card_slots(${G_CLASS} _k TIER "${G_TIER}" NEEDS ${G_NEEDS})
    if(G_TOKENS AND G_CLASS STREQUAL "app")
        message(FATAL_ERROR "jah_gpu_row(${_row}): an app row's slots are its class's (tests/support/test_boot.cmake)")
    endif()
    if(G_TOKENS)
        set(_k ${G_TOKENS})
    endif()
    if(_k EQUAL 0)
        message(FATAL_ERROR "jah_gpu_row(${_row}): CLASS none — a row that boots no Vulkan is a plain "
                            "add_test() with jah_no_display()")
    endif()
    list(POP_FRONT G_COMMAND _exe)
    if(TARGET ${_exe})
        set(_exe "$<TARGET_FILE:${_exe}>")
    endif()
    set(_wd)
    if(G_WORKING_DIRECTORY)
        set(_wd WORKING_DIRECTORY "${G_WORKING_DIRECTORY}")
    endif()
    if(DEFINED G_COLD AND NOT G_CLASS STREQUAL "engine")
        message(FATAL_ERROR "jah_gpu_row(${_row}): COLD declares an ENGINE row cold; a ${G_CLASS} row's home is its own")
    endif()
    if("COLD" IN_LIST ARGN AND NOT G_COLD)
        message(FATAL_ERROR "jah_gpu_row(${_row}): COLD needs its reason (COLD \"<why the row must compile cold>\")")
    endif()
    if(G_CLASS STREQUAL "engine" AND NOT G_COLD)
        set(_home "${CMAKE_CURRENT_BINARY_DIR}/homes/${_row}")
        add_test(NAME ${_row} COMMAND sh "${JAH_FRESH_HOME}" --warm --cache engine "${_home}" --
                 env "JAHSHAKA_SHADER_CACHE_DIR=${_home}/.local/share/Jahshaka/shadercache" JAH_TEXTURE_CACHE=0
                 "${_exe}" ${G_COMMAND} ${_wd})
    else()
        add_test(NAME ${_row} COMMAND "${_exe}" ${G_COMMAND} ${_wd})
    endif()
    if(DEFINED G_FRAMES)
        set_property(TEST ${_row} PROPERTY JAHSHAKA_TEST_FRAMES ${G_FRAMES})
    endif()
    _jah_data(${_row} ${G_CLASS} ${G_DATA})
    _jah_vram_register(${_row} ${G_CLASS} ${_k})
    if(_declared)
        _jah_test_boot_register(${_row} "${G_TIER}" "${G_NEEDS_WHY}" "${G_TEST_WINDOW}" ${G_NEEDS})
    endif()
endfunction()

# jah_gpu_exclusive_test(NAME <suite> CLASS <class> [TIER <tier> NEEDS <features…>|NONE] RUN_TIMEOUT <s> COMMAND <exe-or-target> [args...]
#                        [WORKING_DIRECTORY <dir>])
# add_test() through the card's lock (scripts/gpu-exclusive.sh) on ALL the card's slots, labelled `measure`, whatever
# the row's class (JAH_CLASS keeps it). `set_tests_properties` cannot change a registered COMMAND, so the wrapper is
# applied at registration: a TARGET name as the first COMMAND word becomes $<TARGET_FILE:...> (it
# is an argument of the wrapper now, and add_test resolves target names only in the command
# position). A name outside JAH_GPU_EXCLUSIVE_SUITES is a configure error.
function(jah_gpu_exclusive_test)
    cmake_parse_arguments(G "TEST_WINDOW" "NAME;CLASS;TIER;RUN_TIMEOUT;WORKING_DIRECTORY;NEEDS_WHY;COLD" "NEEDS;COMMAND;DATA" ${ARGN})
    if(NOT G_NAME IN_LIST JAH_GPU_EXCLUSIVE_SUITES)
        message(FATAL_ERROR "jah_gpu_exclusive_test(${G_NAME}): not in JAH_GPU_EXCLUSIVE_SUITES "
                            "(tests/CMakeLists.txt) — the measuring list changes by a verdict only")
    endif()
    if(NOT G_RUN_TIMEOUT OR NOT G_COMMAND OR NOT G_CLASS)
        message(FATAL_ERROR "jah_gpu_exclusive_test(${G_NAME}): CLASS, RUN_TIMEOUT and COMMAND are required")
    endif()
    if(G_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "jah_gpu_exclusive_test(${G_NAME}): unexpected arguments '${G_UNPARSED_ARGUMENTS}' "
                            "(a measuring row takes ALL the card's slots: there is no TOKENS)")
    endif()
    _jah_test_boot_declared("jah_gpu_exclusive_test(${G_NAME})" "${G_CLASS}" "${G_TIER}" "${G_COMMAND}" _declared ${G_NEEDS})
    jah_card_slots(${G_CLASS} _k TIER "${G_TIER}" NEEDS ${G_NEEDS})   # validates the class; the row takes ALL slots
    list(POP_FRONT G_COMMAND _exe)
    if(TARGET ${_exe})
        set(_exe "$<TARGET_FILE:${_exe}>")
    endif()
    set(_wd)
    if(G_WORKING_DIRECTORY)
        set(_wd WORKING_DIRECTORY "${G_WORKING_DIRECTORY}")
    endif()
    # --run-timeout: the row's own budget, enforced by the wrapper from AFTER the lock's wait (a holder of the lock runs
    # it at once); ctest's TIMEOUT is the backstop: the budget + 30 s (timeout(1)'s kill grace).
    # A CLASS engine measure boots warm like every engine row (jah_gpu_row's CLASS engine paragraph; COLD "<why>" opts out)
    set(_warm)
    if(DEFINED G_COLD AND NOT G_CLASS STREQUAL "engine")
        message(FATAL_ERROR "jah_gpu_exclusive_test(${G_NAME}): COLD declares an ENGINE row cold")
    endif()
    if("COLD" IN_LIST ARGN AND NOT G_COLD)
        message(FATAL_ERROR "jah_gpu_exclusive_test(${G_NAME}): COLD needs its reason (COLD \"<why the row must compile cold>\")")
    endif()
    if(G_CLASS STREQUAL "engine" AND NOT G_COLD)
        set(_home "${CMAKE_CURRENT_BINARY_DIR}/homes/${G_NAME}")
        set(_warm sh "${JAH_FRESH_HOME}" --warm --cache engine "${_home}" --
                  env "JAHSHAKA_SHADER_CACHE_DIR=${_home}/.local/share/Jahshaka/shadercache" JAH_TEXTURE_CACHE=0)
    endif()
    add_test(NAME ${G_NAME} COMMAND "${JAH_GPU_EXCLUSIVE}" --run-timeout ${G_RUN_TIMEOUT}
             --label ${G_NAME}:${G_CLASS} ${_warm} "${_exe}" ${G_COMMAND} ${_wd})
    math(EXPR _timeout "${G_RUN_TIMEOUT} + 30")
    set_tests_properties(${G_NAME} PROPERTIES TIMEOUT ${_timeout})
    _jah_data(${G_NAME} ${G_CLASS} ${G_DATA})
    set_property(GLOBAL APPEND PROPERTY JAH_GPU_EXCLUSIVE_REGISTERED ${G_NAME})
    _jah_vram_register(${G_NAME} ${G_CLASS} all)
    if(_declared)
        _jah_test_boot_register(${G_NAME} "${G_TIER}" "${G_NEEDS_WHY}" "${G_TEST_WINDOW}" ${G_NEEDS})
    endif()
    cmake_language(EVAL CODE "cmake_language(DEFER CALL _jah_measure_label [[${G_NAME}]])")
endfunction()

# THE SERIAL FAMILIES (lane D6B-GATE-SHAPE; SPECS/audits/GATE_SUITES_AUDIT_2026-09-26.md §4
# R1/R2). RUN_SERIAL stops EVERY other suite of the gate while one runs: 54 rows carried it,
# 1,487 s of a 3,125 s gate at -j4, and 48 % of that gate's wall had ONE suite running with three
# slots idle. A group of suites that must not overlap EACH OTHER takes a ctest RESOURCE_LOCK
# instead: its members still run one at a time, and the other slots keep working through them.
# Like RUN_SERIAL, a RESOURCE_LOCK reaches only inside one ctest process — which is all these
# claims ever needed (neither is a timing claim; timing suites take the GPU lock above).
#   monado   — every suite that starts a Monado runtime (tests/vr). Each runner gives its
#              runtime a PRIVATE XDG_RUNTIME_DIR (run_vr_*.sh: mktemp -d /tmp/jahvr.XXXXXX), so
#              the old "one IPC socket" reason was void; two runtimes at once is unmeasured,
#              so the family stays mutually exclusive.
#   gi_chain — the cascade-chain suites (tests/gi, engine.gi_scan). Their reason was VRAM ("the
#              chain holds four voxel volumes"), which DEVPROCESS-1 measured false (2.3 of 16 GB
#              with three GPU processes live); they stay exclusive among themselves.
# THE COST PUTS A FAMILY FIRST. A locked family is a CHAIN (the monado one is ~12 min end to
# end), and a chain that starts late is the gate's tail: ctest starts rows in descending COST,
# so a family's cost sits above every measured suite's and the longest chain's highest of all.
set(JAH_COST_MONADO 100000)
set(JAH_COST_GI_CHAIN 90000)

# THE FRESH HOME (lane D6B-GATE-SHAPE; audit §9 H1): tests/support/fresh_home.sh is the ONE
# implementation of "this suite's home is virgin every run" — it replaced 27 hand-copied
# `sh -c "rm -rf …"` fixtures and tests/samples/freshhome.cmake. Two ways in:
#   * jah_fresh_home_fixture(<row> <fixture> <home> [WARM] [ARGS <flags…>] [THEN <cmd…>]
#                            [LABELS <labels>]) — a FIXTURES_SETUP row, for a home shared by a
#     sequence of suites (A writes, B reads) or a suite that is not a plain `--script` run.
#     Without WARM it wipes the caches too (a suite that asserts on a COLD shader cache);
#     ARGS are fresh_home.sh's --reset/--rm/--mkdir; THEN runs after the wipe.
#   * `${JAH_FRESH_HOME} --warm <home> -- <command…>` as a row's COMMAND — a one-script
#     row's default: the wipe happens inside the row (no extra ctest row), keeping the shader caches.
set(JAH_FRESH_HOME "${CMAKE_SOURCE_DIR}/tests/support/fresh_home.sh")
function(jah_fresh_home_fixture row fixture home)
    cmake_parse_arguments(F "WARM" "" "ARGS;THEN;LABELS" ${ARGN})
    set(_flags)
    if(F_WARM)
        list(APPEND _flags --warm)
    endif()
    set(_then)
    if(F_THEN)
        set(_then -- ${F_THEN})
    endif()
    # --setup: the wipe BEFORE and the THEN only — the wipe after is the consumer's exit (round 14 addendum 2)
    add_test(NAME ${row} COMMAND sh "${JAH_FRESH_HOME}" --setup ${_flags} ${F_ARGS} "${home}" ${_then})
    set_tests_properties(${row} PROPERTIES FIXTURES_SETUP ${fixture} TIMEOUT 60)
    # a home WIPE reads no data (fresh_home.sh is the harness, never data): DATA NONE (RUNNER-A2B)
    set_property(TEST ${row} APPEND PROPERTY JAH_DATA NONE)
    if(F_LABELS)
        set_tests_properties(${row} PROPERTIES LABELS "${F_LABELS}")
    endif()
endfunction()

# jah_timing_script(<in.js> <out.js>) — THE MILLISECOND BARS OF AN E2E SCRIPT, ARMED (lane
# D6B-GATE-SHAPE; tests/support/timingbars.h is the C++ half). A `--script` run cannot read its
# environment, so the stage-close `<suite>.timing` row runs a generated copy of the script whose
# first line defines JAH_TIMING_BARS; the push-tier row runs the script itself, where the
# variable is undefined and a millisecond bar only PRINTS its reading. The copy is rewritten at
# configure time and the source is a configure dependency, so the two never drift.
function(jah_timing_script _in _out)
    file(READ "${_in}" _body)
    file(WRITE "${_out}" "var JAH_TIMING_BARS = true;   // generated by jah_timing_script() from ${_in}\n${_body}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_in}")
endfunction()

function(jah_no_display)
    foreach(_t IN LISTS ARGV)
        set_property(TEST ${_t} APPEND PROPERTY ENVIRONMENT_MODIFICATION
                     "DISPLAY=unset:;WAYLAND_DISPLAY=unset:")
    endforeach()
endfunction()

# THE POOLS (lane SUITE-POOL-1; SPECS/audits/SUITE_REDUNDANCY_AUDIT_2026-09-27.md §5; the
# developer's read is docs/TESTING.md). The app runs ONE script per `--script` process, and a
# boot costs ~14 s engine-up on a cold cache (every gate after a rebuild), ~5.5 s warm, ~3.2 s
# headless — so a family of N verb suites paid N boots, and app suites were 77 % of the gate.
# A POOL is ONE ctest row, `pool.<pool>`, whose ONE process runs the family's scripts as named
# ARMS (`Jahshaka --scripts … --pool <pool>`, src/app/cli/scriptrunner.cpp): every arm starts in
# a fresh JavaScript realm; after every arm, green or red, the pool's baseline is restored (its
# BASELINE script, then the window's size, the project closed through `project.close`, the
# deferred deletes), so a later arm starts with NO project open and opens or creates what it
# needs. The driver (tests/support/run_pool.py) prints each arm's final `ARM <pool>.<arm>
# PASS|FAIL|CRASH|TIMEOUT <ms>` once the baseline behind it held; a dead or hung process is the
# CRASH/TIMEOUT of the arm that was running — or of the arm just before, when it died in the
# baseline — and the driver restarts and continues from the next arm. The row is red iff an arm is; its last lines name every arm's verdict;
# `JAH_POOL_ARMS=<pool>.<arm> ctest -R '^pool\.<pool>$'` is the solo retry of one arm.
#
#   jah_pool_arm(<pool> <arm> <script> <seconds> [HEADLESS] [FIRST])
#       — one arm, declared WHERE ITS SUITE'S COMMENT LIVES (the comment says what it claims).
#         Arms run in declaration order; FIRST puts the one arm whose claim is about the
#         PROCESS ("the Player page has never been shown") before every other (one per pool).
#   jah_add_pool(<pool> [HEADLESS] [TIER <tier> NEEDS <features…>|NONE] [CLASS app|vr] [BASELINE <script>] [LABELS <label…>]
#                [ENV <k=v…>] [LAUNCHER <cmd…>] [APP_ARGS <arg…>] [PROPS <property value…>])
#       — the row, after its arms (in the family's CMakeLists, so gate-scope's "a file under
#         tests/<dir>/ selects that dir's rows" keeps holding). LAUNCHER runs the driver inside
#         a fixture process (tests/vr/run_vr_app.sh --launch: a Monado runtime for the pool's
#         life); APP_ARGS are latched by every process the pool starts (--vr); PROPS are extra
#         ctest properties (a RESOURCE_LOCK, a SKIP_RETURN_CODE). BASELINE is a script the app
#         runs after EVERY arm, green or red (--pool-baseline): what the family's arms change in
#         the process and cannot put back on a red path.
#         TIER + NEEDS (TEST-TIER-1, TEST-NEEDS-1; tests/support/test_boot.cmake) are
#         REQUIRED on an engine-up pool: every process the pool starts boots that World Mode
#         (JAHSHAKA_TEST_TIER: every scene it binds, new or opened, the window 1280x720 —
#         services/testtier.h) with only the NEEDS features on (JAHSHAKA_TEST_NEEDS; NONE =
#         no arm reads a picture). The declaration is the UNION of what the pool's arms claim:
#         an arm whose claim needs a feature the pool does not keep MOVES to a pool that keeps
#         it, never the other way. A headless pool has no chain and takes neither. The
#         declaration is readable as the GLOBAL property JAH_POOL_<pool>_TIER
#         ("<tier> <needs…>" | headless).
#
# <seconds> is the arm's MEASURED time INSIDE its pool (whole seconds, rounded up — the pool's
# `ARM … <ms>` lines are the measurement). THE BUDGETS ARE DERIVED, NEVER TYPED: an arm is
# killed past 6x its measured time (never under 30 s), and the row's TIMEOUT is the sum of the
# arms' budgets plus one boot budget (300 s). A slower arm is re-measured, never re-budgeted.
# The home is fresh every run (fresh_home.sh --warm, as a one-script row); the arms SHARE it, so
# an arm never asserts on what another arm left behind — each pool was proved arm by arm against
# its solo suites before they were deleted (spikes/suite-pool-1/).
#
# THE RULE FOR A NEW TEST (docs/TESTING_GATE.md §8): a lane adds an ARM to its family's pool —
# a script and one jah_pool_arm line. A ROW of its own only for a new fixture or a process
# switch: a fresh home as the claim, an environment variable or flag the process latches at boot
# (--vr, --script-live, JAHSHAKA_NO_RAY_QUERY, JAHSHAKA_TEST_NO_EDITOR_BOOT…), a GPU-lock timing,
# a private display, or a crash-class churn.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(JAH_RUN_POOL "${CMAKE_SOURCE_DIR}/tests/support/run_pool.py")
set(JAH_POOL_BOOT_BUDGET 300)
function(jah_pool_arm pool arm script secs)
    cmake_parse_arguments(A "HEADLESS;FIRST" "" "" ${ARGN})
    if(NOT secs MATCHES "^[0-9]+$")
        message(FATAL_ERROR "jah_pool_arm(${pool} ${arm}): seconds '${secs}' is not a whole number")
    endif()
    if(NOT EXISTS "${script}")
        message(FATAL_ERROR "jah_pool_arm(${pool} ${arm}): no script at ${script}")
    endif()
    get_property(_names GLOBAL PROPERTY JAH_POOL_${pool}_NAMES)
    if("${arm}" IN_LIST _names)
        message(FATAL_ERROR "jah_pool_arm(${pool} ${arm}): the arm is declared twice")
    endif()
    get_property(_done GLOBAL PROPERTY JAH_POOL_${pool}_ADDED)
    if(_done)
        message(FATAL_ERROR "jah_pool_arm(${pool} ${arm}): declared after jah_add_pool(${pool})")
    endif()
    set(_mode engine)
    if(A_HEADLESS)
        set(_mode headless)
    endif()
    set_property(GLOBAL APPEND PROPERTY JAH_POOL_${pool}_NAMES "${arm}")
    if(A_FIRST)
        get_property(_first GLOBAL PROPERTY JAH_POOL_${pool}_FIRST)
        if(_first)
            message(FATAL_ERROR "jah_pool_arm(${pool} ${arm}): ${_first} is already the pool's FIRST arm")
        endif()
        set_property(GLOBAL PROPERTY JAH_POOL_${pool}_FIRST "${arm}")
        get_property(_arms GLOBAL PROPERTY JAH_POOL_${pool}_ARMS)
        list(PREPEND _arms "${arm}|${script}|${secs}|${_mode}")
        set_property(GLOBAL PROPERTY JAH_POOL_${pool}_ARMS "${_arms}")
    else()
        set_property(GLOBAL APPEND PROPERTY JAH_POOL_${pool}_ARMS "${arm}|${script}|${secs}|${_mode}")
    endif()
endfunction()

function(jah_add_pool pool)
    cmake_parse_arguments(P "HEADLESS;TEST_WINDOW" "BASELINE;CLASS;TIER;NEEDS_WHY" "NEEDS;LABELS;ENV;LAUNCHER;APP_ARGS;PROPS;DATA" ${ARGN})
    get_property(_arms GLOBAL PROPERTY JAH_POOL_${pool}_ARMS)
    if(NOT _arms)
        message(FATAL_ERROR "jah_add_pool(${pool}): no jah_pool_arm(${pool} …) before it")
    endif()
    set(_want engine)
    set(_modeflag)
    # THE CARD'S SLOTS: a pool takes its class's slots for its whole run (tests/support/test_boot.cmake). A HEADLESS pool
    # boots the NULL render system and takes none. CLASS defaults to app; a VR pool says `CLASS vr`.
    if(NOT P_CLASS)
        set(P_CLASS app)
    endif()
    set(_tier headless)
    set(_vram 0)
    if(P_HEADLESS)
        set(_want headless)
        set(_modeflag --headless)
        if(P_TIER OR P_NEEDS)
            message(FATAL_ERROR "jah_add_pool(${pool}): a HEADLESS pool has no chain and takes no TIER/NEEDS")
        endif()
    else()
        jah_test_boot_check("jah_add_pool(${pool})" "${P_TIER}" ${P_NEEDS})
        jah_card_slots(${P_CLASS} _vram TIER "${P_TIER}" NEEDS ${P_NEEDS})
        string(REPLACE ";" " " _tier "${P_TIER};${P_NEEDS}")
    endif()
    set(_home ${CMAKE_CURRENT_BINARY_DIR}/pool-home-${pool})
    file(MAKE_DIRECTORY ${_home}/run)
    set(_args)
    set(_timeout ${JAH_POOL_BOOT_BUDGET})
    set(_cost 0)
    foreach(_entry IN LISTS _arms)
        string(REPLACE "|" ";" _f "${_entry}")
        list(GET _f 0 _arm)
        list(GET _f 1 _script)
        list(GET _f 2 _secs)
        list(GET _f 3 _mode)
        if(NOT _mode STREQUAL _want)
            message(FATAL_ERROR "jah_add_pool(${pool}): arm ${_arm} is ${_mode}, the pool is ${_want}")
        endif()
        math(EXPR _budget "${_secs} * 6")
        if(_budget LESS 30)
            set(_budget 30)
        endif()
        math(EXPR _timeout "${_timeout} + ${_budget}")
        math(EXPR _cost "${_cost} + ${_secs}")
        list(APPEND _args --arm ${_arm} ${_script} ${_budget})
    endforeach()
    set(_env "HOME=${_home}" "JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka" ${P_ENV})
    list(REMOVE_ITEM _env "")
    set(_baseline)
    if(P_BASELINE)
        if(NOT EXISTS "${P_BASELINE}")
            message(FATAL_ERROR "jah_add_pool(${pool}): no BASELINE script at ${P_BASELINE}")
        endif()
        set(_baseline --baseline ${P_BASELINE})
    endif()
    set(_appargs)
    if(P_APP_ARGS)
        set(_appargs -- ${P_APP_ARGS})
    endif()
    add_test(NAME pool.${pool}
             COMMAND sh ${JAH_FRESH_HOME} --warm ${_home} -- ${P_LAUNCHER}
                     ${Python3_EXECUTABLE} ${JAH_RUN_POOL} --pool ${pool}
                     --app $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${_modeflag}
                     --boot-budget ${JAH_POOL_BOOT_BUDGET}
                     ${_baseline} ${_args} ${_appargs}
             WORKING_DIRECTORY ${_home}/run)
    set_tests_properties(pool.${pool} PROPERTIES
        ENVIRONMENT "${_env}"
        LABELS "pool;${P_LABELS}"
        TIMEOUT ${_timeout}
        COST ${_cost})
    if(P_PROPS)
        set_tests_properties(pool.${pool} PROPERTIES ${P_PROPS})
    endif()
    if(P_HEADLESS)
        _jah_data(pool.${pool} headless ${P_DATA})
    else()
        _jah_data(pool.${pool} ${P_CLASS} ${P_DATA})
    endif()
    set_property(GLOBAL PROPERTY JAH_POOL_${pool}_ADDED TRUE)
    set_property(GLOBAL PROPERTY JAH_POOL_${pool}_TIER "${_tier}")
    if(NOT P_HEADLESS)
        _jah_vram_register(pool.${pool} ${P_CLASS} ${_vram})
        _jah_test_boot_register(pool.${pool} "${P_TIER}" "${P_NEEDS_WHY}" "${P_TEST_WINDOW}" ${P_NEEDS})
    endif()
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
add_subdirectory(compute)
add_subdirectory(atom)
add_subdirectory(lights)
add_subdirectory(shadow)
add_subdirectory(planar)
add_subdirectory(ssr)
add_subdirectory(rtreflect)
add_subdirectory(hdr)
add_subdirectory(defaults)
add_subdirectory(sky)
add_subdirectory(looks)
add_subdirectory(distortion)
add_subdirectory(assets)
add_subdirectory(assetdelete)
add_subdirectory(assetpaths)
add_subdirectory(assetgc)
add_subdirectory(assetmigrate)
add_subdirectory(shadergraph)
add_subdirectory(materialpreview)
add_subdirectory(materialbundle)
add_subdirectory(materialmembers)
add_subdirectory(desktops)
add_subdirectory(drawers)
add_subdirectory(assetmeta)
add_subdirectory(assettray)
add_subdirectory(audiopeaks)
add_subdirectory(scripting)
add_subdirectory(capture)
add_subdirectory(api)
add_subdirectory(reopen)
add_subdirectory(services)
add_subdirectory(perf)
add_subdirectory(commands)
add_subdirectory(shortcuts)
add_subdirectory(settings)
add_subdirectory(shell)
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
# The command-line parser's contract (app.cli_options, ledger 150): what the
# app REFUSES, and why 0 is still a legal --mcp-port.
add_subdirectory(cli)
# app.reset_library — `app.resetLibrary()` puts the installation back to a
# first launch (owner review R10.2): the rows, the store's contents, the
# project folders, and the fresh-install bootstrap after them.
add_subdirectory(libraryreset)
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
# LOD-RACE-1: LodStrategy::lodSet and the ObjectMemoryManager's padding slots (pure OgreMain).
add_subdirectory(lod)
# The scene-graph BENCHMARK GATE (SCENEGRAPH_SPEC §6) — the measurement suite
# that exists BEFORE the swap, so v1 has a recorded baseline to be judged
# against. Labels `stage-close;quiet-box`: minutes, not seconds; the MERGE tier skips it.
add_subdirectory(benchmarks)
# THE SCALE SUITES (lane D1-SCALE-FIXTURES): phase E's measuring stick — one
# `scale.*` suite per wall W1-W14, label `scale-target` (printed, never gating).
# AFTER tests/atom: the world reads that directory's configure-time model extraction.
add_subdirectory(scale)
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
add_subdirectory(update)
# VR phase 1a's external-device route (SPECS/VR_SPEC.md §5/§6). Builds only where
# libopenxr-dev and the Vulkan render system exist; SKIPS where monado-service is
# not installed. Nothing here runs on a box with no VR stack.
add_subdirectory(vr)

# The tooling suites, and the measuring list's closure: every suite the list names is registered through
# jah_gpu_exclusive_test (app.input_keys and app.play_select need xdotool/curl/jq/Xvfb, vr.warmup.timing the VR stack —
# absent without them).
add_subdirectory(tooling)
get_property(_gpu_registered GLOBAL PROPERTY JAH_GPU_EXCLUSIVE_REGISTERED)
set(_gpu_optional app.input_keys app.play_select vr.warmup.timing vr.frame_budget)
foreach(_t IN LISTS JAH_GPU_EXCLUSIVE_SUITES)
    # The rows registered only when their tools exist (the private-Xvfb input drivers, the
    # VR stack) are exempt: a box without them registers neither the row nor the lock.
    if(NOT _t IN_LIST _gpu_registered AND NOT _t IN_LIST _gpu_optional)
        # FATAL on the box the list was measured on; a port that skips a suite is told, not stopped.
        set(_sev WARNING)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            set(_sev FATAL_ERROR)
        endif()
        message(${_sev} "JAH_GPU_EXCLUSIVE_SUITES names ${_t}, but it was not registered through "
                            "jah_gpu_exclusive_test() — it would measure beside other GPU rows")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# jah_link_testmesh_users — every target under tests/ whose own sources include
# support/testmesh.h links jah_testmesh (the fixture parse, SHIPPED-BAKES-1).
function(jah_link_testmesh_users _dir)
    get_property(_targets DIRECTORY "${_dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        if(_t STREQUAL "jah_testmesh")
            continue()
        endif()
        get_target_property(_type ${_t} TYPE)
        if(NOT _type STREQUAL "EXECUTABLE")
            continue()
        endif()
        get_target_property(_srcs ${_t} SOURCES)
        get_target_property(_sdir ${_t} SOURCE_DIR)
        foreach(_src IN LISTS _srcs)
            if(NOT _src MATCHES "\\.cpp$")
                continue()
            endif()
            if(NOT IS_ABSOLUTE "${_src}")
                set(_src "${_sdir}/${_src}")
            endif()
            if(NOT EXISTS "${_src}" OR NOT _src MATCHES "/tests/")
                continue()
            endif()
            file(STRINGS "${_src}" _hit REGEX "support/testmesh\\.h")
            if(_hit)
                target_link_libraries(${_t} PRIVATE jah_testmesh)
                break()
            endif()
        endforeach()
    endforeach()
    get_property(_subs DIRECTORY "${_dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sub IN LISTS _subs)
        jah_link_testmesh_users("${_sub}")
    endforeach()
endfunction()
jah_link_testmesh_users("${CMAKE_CURRENT_SOURCE_DIR}")

# jah_scope_path_defines — AN ABSOLUTE PATH IN A -D LIVES ON THE SUITE'S OWN SOURCES
# (SPEED-CPU, perf audit B2). A suite's target compiles its own test_*.cpp AND the
# Studio/irisgl sources it lists (243 of them, 2,278 copies), and a target-wide
# definition reached all of them: JAHSHAKA_TEST_MEDIA_DIR and _PLUGIN_DIR alone sat
# on 1,927 TUs that never read them. ccache's base_dir rewrites path ARGUMENTS but
# not -D values, so each of those TUs missed the direct mode in every other tree.
# This pass moves every target-wide definition whose value is an absolute path onto
# the target's sources under tests/ (where every reader is — src/ and irisgl/ read
# none of them), and drops Qt's QT_TESTCASE_{BUILD,SOURCE}DIR (nothing here calls
# QFINDTESTDATA). Two suites of one directory that share a source must agree on a
# moved value; they are told so at configure rather than compiled with whichever
# definition came last.
function(jah_scope_path_defines _dir)
    get_property(_targets DIRECTORY "${_dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_t IN LISTS _targets)
        get_target_property(_type ${_t} TYPE)
        if(NOT _type MATCHES "EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY")
            continue()
        endif()
        set_target_properties(${_t} PROPERTIES QT_SKIP_DEFAULT_TESTCASE_DIRS ON)
        get_target_property(_defs ${_t} COMPILE_DEFINITIONS)
        if(NOT _defs)
            continue()
        endif()
        set(_keep)
        set(_move)
        foreach(_d IN LISTS _defs)
            if(_d MATCHES "^[A-Za-z_][A-Za-z_0-9]*=\"?/")
                list(APPEND _move "${_d}")
            else()
                list(APPEND _keep "${_d}")
            endif()
        endforeach()
        if(NOT _move)
            continue()
        endif()
        set_property(TARGET ${_t} PROPERTY COMPILE_DEFINITIONS "${_keep}")
        get_target_property(_srcs ${_t} SOURCES)
        get_target_property(_sdir ${_t} SOURCE_DIR)
        foreach(_src IN LISTS _srcs)
            if(_src MATCHES "^\\$<")
                continue()
            endif()
            if(NOT IS_ABSOLUTE "${_src}")
                set(_src "${_sdir}/${_src}")
            endif()
            if(NOT _src MATCHES "^${CMAKE_SOURCE_DIR}/tests/")
                continue()
            endif()
            get_property(_have SOURCE "${_src}" DIRECTORY "${_sdir}" PROPERTY COMPILE_DEFINITIONS)
            foreach(_d IN LISTS _move)
                string(REGEX MATCH "^[A-Za-z_][A-Za-z_0-9]*" _name "${_d}")
                set(_clash FALSE)
                set(_same FALSE)
                foreach(_h IN LISTS _have)
                    if(_h STREQUAL _d)
                        set(_same TRUE)
                    elseif(_h MATCHES "^${_name}=")
                        set(_clash TRUE)
                    endif()
                endforeach()
                if(_clash)
                    message(FATAL_ERROR "jah_scope_path_defines: ${_src} is compiled by ${_t} with "
                                        "${_d} and by another target of its directory with another "
                                        "value of ${_name} — give the two suites one value")
                endif()
                if(NOT _same)
                    set_property(SOURCE "${_src}" DIRECTORY "${_sdir}" APPEND PROPERTY
                                 COMPILE_DEFINITIONS "${_d}")
                    list(APPEND _have "${_d}")
                endif()
            endforeach()
        endforeach()
    endforeach()
    get_property(_subs DIRECTORY "${_dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sub IN LISTS _subs)
        jah_scope_path_defines("${_sub}")
    endforeach()
endfunction()
jah_scope_path_defines("${CMAKE_CURRENT_SOURCE_DIR}")

# THE DATA FAMILIES BY DIRECTORY (RUNNER-A2B; the macros' block above says what JAH_DATA is): every row registered under
# tests/<dir>/ whose directory holds a fixtures/ reads it (fixtures/<dir>); the directories whose tests open the shipped
# samples through their compiled-in source path (JAHSHAKA_TEST_SOURCE_DIR "/scenes/…", a script's scenes/ path) read
# `samples`; the lint rows of tests/hygiene read the source tree through the selector's own rules and NOTHING else (NONE,
# unless their directory gave them a set). Needs CMake 3.28's TEST DIRECTORY scope.
set(JAH_DATA_SAMPLE_DIRS archiveasync openasync perf threading scripting meshbake)
set(JAH_DATA_NONE_DIRS hygiene)
function(jah_data_pass _dir)
    file(RELATIVE_PATH _rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_dir}")
    string(REGEX MATCH "^[^/]+" _top "${_rel}")
    get_property(_tests DIRECTORY "${_dir}" PROPERTY TESTS)
    foreach(_t IN LISTS _tests)
        set(_add)
        if(_rel AND EXISTS "${_dir}/fixtures")
            list(APPEND _add "fixtures/${_rel}")
        endif()
        if(_top IN_LIST JAH_DATA_SAMPLE_DIRS)
            list(APPEND _add samples)
        endif()
        get_property(_has TEST ${_t} DIRECTORY "${_dir}" PROPERTY JAH_DATA)
        if(NOT _has AND NOT _add AND _top IN_LIST JAH_DATA_NONE_DIRS)
            set(_add NONE)
        endif()
        if(_add)
            set_property(TEST ${_t} DIRECTORY "${_dir}" APPEND PROPERTY JAH_DATA ${_add})
        endif()
    endforeach()
    get_property(_subs DIRECTORY "${_dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sub IN LISTS _subs)
        jah_data_pass("${_sub}")
    endforeach()
endfunction()
jah_data_pass("${CMAKE_CURRENT_SOURCE_DIR}")

# THE MEASURES BY PRICE (TESTING-SIMPLE; DOCS/PROCESS.md, Testing): every guard of a named behaviour whose price
# (scripts/gate-times.txt, its lowest measurement of record) is over its budget — a fixture or a CPU row 10 s, an app row
# 60 s — every soak, churn and scale row, and the setup whose every consumer is one of them: labelled `measure`, run at the sprint's ship only, alone on the card, never
# served. A guard is never lost for its price: a row rewritten to its budget leaves this list (its line in gate-times.txt
# from its own solo run).
set(JAH_MEASURE_ROWS
    app.device_loss_end
    app.input_keys.fresh_home
    app.play_select.fresh_home
    app.shutdown_order.timing.fresh_home
    archive.responsive
    archive.responsive.fresh_home
    archive.responsive.timing.fresh_home
    atom.cluster_crack
    atom.cluster_cut
    atom.coverage_trace
    atom.dag_bound_bar
    atom.dag_chain_validate
    atom.decode_exact
    atom.dolly_gate
    atom.lod_bound_bar
    atom.lod_switch
    atom.lod_switch.fixture
    atom.occlusion_exact
    atom.removechild_leak
    avatar.responsive.timing.fresh_home
    bake.determinism
    bundle.writer
    engine.atom_draw
    engine.atom_parity
    engine.particles_pfx2_asan
    engine.planar_asan
    gi.cascades_asan
    gi.cast_shadow_gi
    gi.chain_converge_scenes
    gi.chain_converge_scenes.fresh_home
    gi.contact_occlusion
    gi.ddgi
    gi.far_sun_clutter
    gi.far_sun_share
    gi.field_alias
    gi.field_thin_wall
    gi.gather
    gi.gather_cards
    gi.gather_estimator
    gi.gather_plane
    gi.gather_plane_target
    gi.gather_plane_weight
    gi.gather_reference
    gi.gather_reference_target
    gi.gather_stable
    gi.hit_shade
    gi.probe_inputs
    gi.reflect_fog
    gi.reflect_mover
    gi.rt_alpha_tested
    gi.rt_reflect_asan
    gi.speckle
    gi.voxel_coverage
    gpu.cutout_soak
    hdr.drag_stable_asan
    hdr.meter_asan
    lights.shadows
    material.live_roundtrip
    math.parity
    mirror.settle_incremental
    mirror.settle_thumbnails
    open.responsive.timing.fresh_home
    perf.capture_bundle
    perf.capture_bundle.capture_bundle.fresh_home
    perf.capture_bundle.capture_quit.fresh_home
    perf.capture_off_is_free.fresh_home
    perf.freshhome.drag_mirror_room
    perf.freshhome.epic_steady_state
    perf.freshhome.hide_soak
    perf.hide_soak
    pool.gi_verbs_stage
    pool.world_light_stage
    samples.freshhome.session
    samples.mirror_room_boots
    samples.session
    scale.assets
    scale.atlas
    scale.bake
    scale.far_field
    scale.hit_list
    scale.levels
    scale.library.fixture
    scale.lights
    scale.occlusion
    scale.residency
    scene.reopen_fidelity
    scene.reopen_fidelity.fresh_home
    scripting.e2e.ogre_ports
    scripting.freshhome.ogre_ports
    shader.async_soak
    shader.live_compiles
    shader.sample_opens_quiet
    shadercache.app.churn
    shadercache.container
    shadercache.container_asan
    shadercache.engine_pipelines
    shadow.atom_parity
    shadow.cutout_caster.churn
    shadow.two_sided_caster.churn
    skeletal.gpu_parity
    ssr.engine_asan
    test_engine
    test_engine_asan
    threading.freshhome.import_soak
    threading.freshhome.newproject_stall_timing
    threading.gi_resolve
    threading.import_soak
    threading.import_soak.no_groupless_texture
    threading.mode
    vr.eye_grade
    vr.eye_grade.fresh_home
    vr.frame_budget.fresh_home
    vr.gather_stereo
    vr.session
)
set_property(GLOBAL PROPERTY JAH_MEASURE_SEEN "")
function(jah_measure_pass _dir)
    get_property(_tests DIRECTORY "${_dir}" PROPERTY TESTS)
    foreach(_t IN LISTS _tests)
        if(_t IN_LIST JAH_MEASURE_ROWS)
            set_property(TEST ${_t} DIRECTORY "${_dir}" APPEND PROPERTY LABELS measure)
            set_property(GLOBAL APPEND PROPERTY JAH_MEASURE_SEEN ${_t})
        endif()
    endforeach()
    get_property(_subs DIRECTORY "${_dir}" PROPERTY SUBDIRECTORIES)
    foreach(_sub IN LISTS _subs)
        jah_measure_pass("${_sub}")
    endforeach()
endfunction()
jah_measure_pass("${CMAKE_CURRENT_SOURCE_DIR}")
get_property(_seen GLOBAL PROPERTY JAH_MEASURE_SEEN)
foreach(_t IN LISTS JAH_MEASURE_ROWS)
    if(NOT _t IN_LIST _seen)
        message(WARNING "JAH_MEASURE_ROWS names ${_t}, which this build does not register")
    endif()
endforeach()


