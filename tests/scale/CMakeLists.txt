# THE SCALE SUITES (lane D1-SCALE-FIXTURES; SPECS/briefs/D1-SCALE-FIXTURES.md) —
# phase E's measuring stick. One `scale.*` suite per wall of
# SPECS/audits/V2_ATOM_PHOTON_STRATEGY_2026-09-25.md §3 (W1-W14), each PRINTING
# today's number as a `target:` line with the wall's anchor in its source
# comment, and NO bar yet: the part that closes a wall turns its row into a bar
# and removes the label.
#
# THE LABEL `scale-target` is in scripts/gate-scope.py's TARGET_LABELS: a scoped
# gate RUNS these and reports their exit code without letting it decide; the
# MERGE and PUSH tiers drop them with -LE. A suite that times the GPU or the
# frame is registered through the GPU-timing lock (jah_gpu_exclusive_test).
#
# THE FIXTURES are built at test time, never checked in:
#   the WORLD   — scale_world.cpp (a document through the product's mirror and
#                 tier tables; ~20 meshes through the product's bake);
#   the ASSETS  — enginetest::proceduralShell (tests/support/proceduralshell.h)
#                 at 250 k / 1 M / 5 M / 10 M triangles through the same bake. The
#                 big bakes are W11 itself (minutes and gigabytes), so they are made
#                 ONCE PER BAKE FORMAT FOR THE WHOLE BOX (re-baked in place when stale) by
#                 the fixture row
#                 `scale.assets` (archived: archive/tests/scale) (the tool `scale_assets_gen`; a no-op once cached)
#                 into THE SHARED SCALE CACHE; a row whose shell is missing FAILS and
#                 says how to bake it — no row ever measures a smaller asset instead;
#   the LIBRARY — test_scale_library.cpp generates it through the import door.
# THE SHARED SCALE CACHE (V2-P0A): $JAH_SCALE_ASSET_CACHE, else JAH_SCALE_ASSET_CACHE_ROOT
# (default ~/Developer/testing/scale-cache — outside every tree, so a new worktree reads the
# shells the last one baked), one sub-directory per bake FORMAT VERSION (v<kFormatVersion>,
# scale_world.cpp cacheDir; ~1.1 GB): every reader checks a blob's fingerprint from its header,
# and a blob another bake producer made is STALE — scale.assets (archived: archive/tests/scale) re-bakes it in place.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL Core Network Sql)
find_package(X11 REQUIRED)

# The two shipped high-poly models, from the tests/atom configure-time extraction
# (tests/atom/CMakeLists.txt: the Matcaps dragon and the Physics model).
set(_scale_model_dir ${CMAKE_BINARY_DIR}/tests/atom/cluster-fixtures)
set(JAH_SCALE_ASSET_CACHE_ROOT "$ENV{HOME}/Developer/testing/scale-cache" CACHE PATH
    "The shared scale-suite bake cache (one per box; per-producer sub-directories)")

set(_scale_world_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/scale_world.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scale_x11.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)

function(jah_scale_target _t)
    set_target_properties(${_t} PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
        INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_include_directories(${_t} PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/irisgl ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src
        ${CMAKE_SOURCE_DIR}/irisgl/engine/include ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${_t} PRIVATE
        JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        SCALE_MODEL_DIR="${_scale_model_dir}"
        SCALE_ASSET_CACHE_ROOT="${JAH_SCALE_ASSET_CACHE_ROOT}"
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    target_link_libraries(${_t} PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL ${X11_LIBRARIES})
    add_dependencies(${_t} JahshakaEngineMedia)
endfunction()

add_executable(test_scale test_scale.cpp ${_scale_world_sources})
jah_scale_target(test_scale)

# ---------------------------------------------------------------------------
# THE MERGE TIER'S SCALE ROWS (TESTING-SPEED-B3, the ROWS table and the lead's verdicts on the profile): atom.decode_exact (archived: archive/tests/atom)
# and atom.removechild_leak (archived: archive/tests/atom). scale.occlusion (archived: archive/tests/scale), atom.coverage_trace (archived: archive/tests/atom) and the bars that TIME the world (scale.cpu_walks,
# scale.shadow_cut (archived: archive/tests/scale), scale.gpu_coverage (archived: archive/tests/scale), scale.library (archived: archive/tests/scale)) are stage-close: measurements of a whole world. scale.library (archived: archive/tests/scale)'s
# setup row scale.library.fixture (archived: archive/tests/scale) keeps its label (the ROWS table: KEEP — a no-op once the template exists; ctest adds it
# to whatever names scale.library (archived: archive/tests/scale)).
# The `scale-target` rows print, never gate, and are untouched.
#
# THE ROWS — one per wall. Label `scale-target` on every one (TARGET_LABELS). The rows
# that time the GPU or the frame go through the GPU-timing lock (their names are in
# JAH_GPU_EXCLUSIVE_SUITES, tests/CMakeLists.txt, with the verdict there); the others
# count things, read pictures or measure memory and share the GPU like any suite.
# TIMEOUTS: 6x the measured solo seconds (the rig, Debug, 2026-09-26), floor 120; a
# locked row's RUN_TIMEOUT is its own budget, the lock's wait is added by the helper.
# Measured solo seconds (the rig, Debug, 2026-09-26) -> 6x, floor 120. The rows that
# load the 1 M / 5 M / 10 M shells are sized for the 10 M one's deserialize + upload.
set(SCALE_T_WORLD 1300)       # 210 s (still + the 30 s fly: 1,928 frames)
set(SCALE_T_VOXEL 1100)       # 135 s solo (V2-P0A: the 2 km walk + the four teleport sets, ~7,000 frames)
set(SCALE_T_LIGHTS 560)       # 93 s solo: five arms (the guard's + N = 8/16/32/64), two of them toggled
set(SCALE_T_CUT 600)          # 12 s on the 5 M shell; the 10 M one and the owed 1/5/10 M table
set(SCALE_T_LEVELS 300)       # 8 s on the 5 M shell
set(SCALE_T_CUT_COST 900)     # the world's boot + five poses (ATOM-CLUSTER-CUT)
set(SCALE_T_RESIDENCY 600)    # the 250 k cold, then the four shells warm (the 10 M read dominates)
set(SCALE_T_DECODE 840)       # 139 s (four material arms, GI to rest after each)
set(SCALE_T_OCCLUSION 600)    # 34 s solo; 99 s in a -j4 gate (D6B fix round F1: 6x the worst)
set(SCALE_T_TLAS 300)         # 42-51 s
set(SCALE_T_ATLAS 320)        # 53 s
set(SCALE_T_FAR 120)          # ~12 s (two pictures)
set(SCALE_T_BAKE 300)         # 48 s (a 250 k shell baked: W11 itself)
set(SCALE_T_HIT 430)          # 71 s
set(SCALE_T_WALKS 340)        # 47-57 s
set(SCALE_T_SHADOW 450)       # both worlds in one process: 48 s + 24.4 s measured, x 6
# scale.library (archived: archive/tests/scale): 48-110 s over a GENERATED library (6 x 110 s). The generation is its own row
# since GATE-SPEED-1 (scale.library.fixture (archived: archive/tests/scale) below): 10,000 imports + 25 creates + 475 archive
# imports, 184-200 s measured in a fresh rc tree and up to ~14 min on a loaded box — the
# one-time fixture cost, whose budget (30 min) is the fixture row's, never the lock's.
set(SCALE_T_LIBRARY 660)
set(SCALE_T_LIBRARY_FIXTURE 1800)
# VRAM (GATE-ADMIT-1, nvidia-smi per pid in the two-tier run): test_scale peaked 1.9 GB -> an
# untimed test_scale row takes 2 tokens; a locked (timing) row takes all of them.
function(jah_scale_row _name _mode _timeout _locked)
    if(_locked)
        jah_gpu_exclusive_test(NAME ${_name} CLASS engine RUN_TIMEOUT ${_timeout} COMMAND test_scale ${_mode}
                               WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    else()
        jah_gpu_row(${_name} CLASS engine TOKENS 2 COMMAND test_scale ${_mode} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(${_name} PROPERTIES TIMEOUT ${_timeout})
    endif()
    set_tests_properties(${_name} PROPERTIES LABELS "scale" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()
# W7 IS CLOSED (ATOM-OCCLUSION-1): its row is a BAR — the two-pass id pass within 1.25x of the
# reference cull and its id image the frustum-only one at every pose — and not a target.
# STAGE-CLOSE (TESTING-SPEED-B3, the lead's verdict): case-bound — 3 arms x 8 poses of id readbacks ARE the
# measurement (45 s quiet); a scale property. atom.occlusion_exact (archived: archive/tests/atom) is the merge tier's occlusion guard.
jah_scale_row(scale.cpu_walks    --cpu-walks     ${SCALE_T_WALKS}        TRUE)   # W13
# THE SHIP SET (TEST-CUT-1): the scale prefix's end-to-end row — the 10k-node world's CPU walks, timed under the lock.
# (scale.library (archived: archive/tests/scale) was the first pick; its setup scale.library.fixture (archived: archive/tests/scale) generates a 10,000-asset template, 180 s in a
# fresh tree, so its true price is over the minute: archived.)
jah_ship_row(scale.cpu_walks)
# ...A BAR SINCE ATOM-CPU-WALKS-1 (the card candidates, the words and the ray tier's
# frame <= 0.5 ms still / 1.5 ms on a mover frame): the part that closed the wall
# removes the label, so the row gates. The dirty scan stays a printed target inside it.
set_tests_properties(scale.cpu_walks PROPERTIES LABELS "scale")
# scale.gpu_coverage (archived: archive/tests/scale) — EVERY BUSY GPU MILLISECOND HAS A ROW (lane TEST-1, the perf audit's F5): on
# the lattice at Epic, still and panning, the GPU work no monitor row names against the frame's BUSY
# span (its own GPU span less the GPU's wait for submissions, MonitorRecord::gpuIdleMs — printed as a
# target, never gated); the median frame's unattributed share stays under 5 %. A BAR, so it gates
# (label `scale`); locked: it reads GPU time. Measured 208 s after the admission in Debug
# (the lattice's settle is most of it); the run budget is ~3x that.
set(SCALE_T_COVERAGE 600)

# scale.assets (archived: archive/tests/scale) — THE SHELLS' FIXTURE ROW (V2-P0A): bakes every shell the rows read (250 k,
# 1 M, 5 M, 10 M) into the shared cache when it is missing or STALE (another bake producer's,
# re-baked in place), and otherwise reports the bake records from their side files without
# reading a blob. It sweeps the pieces a killed baker left first. CPU only (no Vulkan, no
# display); a lock per shell, so two trees' rows never bake one shell twice. The four took
# 25 min at nice 19 beside a live gate (the 10 M 1,015 s at a 3.9 GB peak, 4 bake threads):
# RUN_SERIAL with its 4 PROCESSORS, so no gate row shares its CPU.
# ctest adds this row to any selection naming a row that requires it.
set(SCALE_T_ASSETS 3600)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/removechild_leak")

# FRAMES — THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B3; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, measured 2026-10-12 at the lane's tip by spikes/testing-speed-b3/framecount (an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded, summed over the row's processes); 0 = a row that renders nothing.
foreach(_row_frames
        "scale.cpu_walks=517"
        )
    string(REPLACE "=" ";" _rf "${_row_frames}")
    list(GET _rf 0 _r)
    list(GET _rf 1 _n)
    if(TEST ${_r})
        set_tests_properties(${_r} PROPERTIES JAHSHAKA_TEST_FRAMES ${_n})
    endif()
endforeach()
