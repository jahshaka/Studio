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
#                 at 1 M / 5 M / 10 M triangles through the same bake. The big
#                 bakes are W11 itself (tens of minutes to hours in Debug), so
#                 they are made ONCE by the tool `scale_assets_gen`
#                 (EXCLUDE_FROM_ALL) into the bake cache; a suite whose asset is
#                 not cached measures the largest one that is and SAYS so;
#   the LIBRARY — test_scale_library.cpp generates it through the import door.
# The bake cache: $JAH_SCALE_ASSET_CACHE, else ${CMAKE_CURRENT_BINARY_DIR}/asset-cache
# (keyed by the bake's fingerprint: a producer change misses it, never reads stale).
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL Core Network)
find_package(X11 REQUIRED)

# The two shipped high-poly models, from the tests/atom configure-time extraction
# (tests/atom/CMakeLists.txt: the Matcaps dragon and the Physics model).
set(_scale_model_dir ${CMAKE_BINARY_DIR}/tests/atom/cluster-fixtures)
set(_scale_cache_dir ${CMAKE_CURRENT_BINARY_DIR}/asset-cache)

set(_scale_world_sources
    ${CMAKE_CURRENT_SOURCE_DIR}/scale_world.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scale_x11.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp
    ${CMAKE_SOURCE_DIR}/src/services/worldmodes.cpp)

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
        SCALE_ASSET_CACHE_DIR="${_scale_cache_dir}"
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    target_link_libraries(${_t} PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL ${X11_LIBRARIES})
    add_dependencies(${_t} JahshakaEngineMedia)
endfunction()

add_executable(test_scale test_scale.cpp ${_scale_world_sources})
jah_scale_target(test_scale)

# scale_assets_gen — THE LARGE ASSETS, baked once into the cache (a TOOL, never a
# suite): `scale_assets_gen 1000000 5000000 10000000`. Prints the bake table (s per
# M triangles, the DAG stage apart, blob bytes, levels, peak RSS) — W11's row.
add_executable(scale_assets_gen EXCLUDE_FROM_ALL scale_assets_gen.cpp ${_scale_world_sources})
jah_scale_target(scale_assets_gen)

# scale.library — W14 (test_scale_library.cpp): the real binary over MCP on a
# generated 10,000-asset / 500-project data root (made through assets.importFile and
# project.create, once, into library-template/; copied per run) against an empty one.
add_executable(test_scale_library test_scale_library.cpp)
set_target_properties(test_scale_library PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_scale_library PRIVATE ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(test_scale_library PRIVATE Qt6::Core Qt6::Gui Qt6::Network)
target_compile_definitions(test_scale_library PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    SCALE_LIBRARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")

# ---------------------------------------------------------------------------
# THE ROWS — one per wall. Label `scale-target` on every one (TARGET_LABELS). The rows
# that time the GPU or the frame go through the GPU-timing lock (their names are in
# JAH_GPU_EXCLUSIVE_SUITES, tests/CMakeLists.txt, with the verdict there); the others
# count things, read pictures or measure memory and share the GPU like any suite.
# TIMEOUTS: 6x the measured solo seconds (the rig, Debug, 2026-09-26), floor 120; a
# locked row's RUN_TIMEOUT is its own budget, the lock's wait is added by the helper.
# Measured solo seconds (the rig, Debug, 2026-09-26) -> 6x, floor 120. The rows that
# load the 1 M / 5 M / 10 M shells are sized for the 10 M one's deserialize + upload.
set(SCALE_T_WORLD 1300)       # 210 s (still + the 30 s fly: 1,928 frames)
set(SCALE_T_VOXEL 1100)       # 180 s (the 200 m walk + 8 teleports: 1,697 frames)
set(SCALE_T_LIGHTS 120)       # 11 s
set(SCALE_T_CUT 600)          # 12 s on the 5 M shell; the 10 M one and the owed 1/5/10 M table
set(SCALE_T_LEVELS 300)       # 8 s on the 5 M shell
set(SCALE_T_CUT_COST 900)     # the world's boot + five poses (ATOM-CLUSTER-CUT)
set(SCALE_T_RESIDENCY 600)    # 9 s over 1 M + 5 M; every cached shell loaded in turn
set(SCALE_T_DECODE 840)       # 139 s (four material arms, GI to rest after each)
set(SCALE_T_OCCLUSION 210)    # 34 s
set(SCALE_T_TLAS 300)         # 42-51 s
set(SCALE_T_ATLAS 320)        # 53 s
set(SCALE_T_FAR 120)          # ~12 s (two pictures)
set(SCALE_T_BAKE 300)         # 48 s (a 250 k shell baked: W11 itself)
set(SCALE_T_HIT 430)          # 71 s
set(SCALE_T_WALKS 340)        # 47-57 s
# scale.library: 108-110 s over a GENERATED library; a tree without one first generates it
# (10,000 imports at ~84 ms each = ~14 min, 25 creates, 475 archive imports ~15 s) — the
# one-time fixture cost, the reason this row's budget is 30 minutes and not 6 x 110 s.
set(SCALE_T_LIBRARY 1800)
function(jah_scale_row _name _mode _timeout _locked)
    if(_locked)
        jah_gpu_exclusive_test(NAME ${_name} RUN_TIMEOUT ${_timeout} COMMAND test_scale ${_mode}
                               WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    else()
        add_test(NAME ${_name} COMMAND test_scale ${_mode} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(${_name} PROPERTIES TIMEOUT ${_timeout})
    endif()
    set_tests_properties(${_name} PROPERTIES LABELS "scale-target" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()
jah_scale_row(scale.world        --world         ${SCALE_T_WORLD}        TRUE)   # the fixture: still + 30 s fly
jah_scale_row(scale.voxel_scroll --voxel-scroll  ${SCALE_T_VOXEL}        TRUE)   # W1 (+ the 200 m walk, the 60 m teleport)
jah_scale_row(scale.lights       --lights        ${SCALE_T_LIGHTS}       FALSE)  # W2 (a voxel digest: exact, not timed)
jah_scale_row(scale.cluster_cut  --cluster-cut   ${SCALE_T_CUT}          TRUE)   # W3 (+ the owed id/decode ms)
jah_scale_row(scale.levels       --levels        ${SCALE_T_LEVELS}       FALSE)  # W4
jah_scale_row(scale.cut_cost     --cut-cost      ${SCALE_T_CUT_COST}     TRUE)   # D1: the flat cut's ms at 10k
jah_scale_row(scale.residency    --residency     ${SCALE_T_RESIDENCY}    FALSE)  # W5 (the engine's pool stats)
jah_scale_row(scale.decode       --decode        ${SCALE_T_DECODE}       TRUE)   # W6
jah_scale_row(scale.occlusion    --occlusion     ${SCALE_T_OCCLUSION}    FALSE)  # W7 (triangle counts)
jah_scale_row(scale.tlas         --tlas          ${SCALE_T_TLAS}         TRUE)   # W8
jah_scale_row(scale.atlas        --atlas         ${SCALE_T_ATLAS}        FALSE)  # W9 (card counts)
jah_scale_row(scale.far_field    --far-field     ${SCALE_T_FAR}          FALSE)  # W10 (two pictures)
jah_scale_row(scale.bake         --bake          ${SCALE_T_BAKE}         FALSE)  # W11 (CPU only: no GPU, no display)
jah_scale_row(scale.hit_list     --hit-list      ${SCALE_T_HIT}          FALSE)  # W12 (record counts)
jah_scale_row(scale.cpu_walks    --cpu-walks     ${SCALE_T_WALKS}        TRUE)   # W13
jah_no_display(scale.bake)

# W14 — the real binary over MCP, the UI thread's gaps: locked (open.responsive's class).
jah_gpu_exclusive_test(NAME scale.library RUN_TIMEOUT ${SCALE_T_LIBRARY} COMMAND test_scale_library
                       WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(scale.library PROPERTIES LABELS "scale-target")
