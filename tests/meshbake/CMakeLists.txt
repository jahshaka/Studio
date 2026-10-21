# MESH BAKE (MESH_BAKE_SPEC.md phase 1). The bake replaces an assimp parse
# with a file read on every open, so the gate is EQUIVALENCE, not "it loads":
# every fixture is built both ways and compared field for field (vertex and
# index bytes, bounds, the picking TriMesh, the skeleton, every animation key,
# and the whole fragment tree), then determinism, staleness and every
# corruption mode, then the real import pipeline's store behaviour.
#
# Document-side by construction: no engine, no display.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia)
add_executable(test_mesh_bake test_mesh_bake.cpp
    # the cut curve's STOCK arm: upstream clusterlod unpatched, renamed (ATOM-UPGRADE stage 0; a reference, never a product path)
    cutcurve_stock.cpp
    ${CMAKE_SOURCE_DIR}/src/services/meshbakestore.cpp
    ${CMAKE_SOURCE_DIR}/src/services/primitiveassets.cpp
    ${CMAKE_SOURCE_DIR}/src/services/primitiveseed.cpp
    ${CMAKE_SOURCE_DIR}/src/export/exportcontentsource.cpp
    ${CMAKE_SOURCE_DIR}/src/services/import/assetimportservice.cpp
    # the member stamp: the import spine clears it for a User-intent import (IMPORT-INTENT-1)
    ${CMAKE_SOURCE_DIR}/src/services/memberstamp.cpp
    ${CMAKE_SOURCE_DIR}/src/services/projectmembership.cpp
    # The import record (SESSION_LOG_SPEC §5) is emitted from the one pipeline
    # entry point; JahLog is a leaf service (Qt + iris::Logger, nothing else).
    ${CMAKE_SOURCE_DIR}/src/services/jahlog.cpp
    ${CMAKE_SOURCE_DIR}/src/services/import/assetimporters.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/io/builtinmaterials.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetmetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/services/animationfile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/rigsignature.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/services/iesprofile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/thumbnailmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsstore.cpp
    # database.cpp's link closure (same set tests/importer links)
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    # the project's VR settings table: the writer/reader are generated from it (VR-WORLD-1)
    ${CMAKE_SOURCE_DIR}/src/services/vrworld.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp)
set_target_properties(test_mesh_bake PROPERTIES AUTOMOC ON CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_mesh_bake PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/engine/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/zip/src
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src
    # meshoptimizer.h for the stock arm (its entry points are libIrisGL's exported ones)
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/meshoptimizer/src)
target_compile_definitions(test_mesh_bake PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    # the Stanford dragon tests/atom extracts at configure time (atom.lod_bound_bar (archived: archive/tests/atom))
    JAH_CLUSTER_FIXTURE_DIR="${CMAKE_BINARY_DIR}/tests/atom/cluster-fixtures")
target_link_libraries(test_mesh_bake PRIVATE IrisGL
    Qt6::Widgets Qt6::Sql Qt6::Multimedia)
jah_links_assimp(test_mesh_bake)

# THE APP FOLDER BESIDE THE BINARY. This target links no .qrc, and the seeder
# reads a shipped mesh from the resources OR from the app folder — the same file
# either way (services/primitiveseed.cpp says why). A symlink is what gives this
# binary the second route; the app itself has both.
if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/app)
    file(CREATE_LINK ${CMAKE_SOURCE_DIR}/app ${CMAKE_CURRENT_BINARY_DIR}/app SYMBOLIC)
endif()

# EACH ROW ITS OWN HOME, WIPED PER RUN (TESTING-CLEANUP-2 H8e). The rows below used to share ONE
# `bake-home` that nothing wiped, and they run in parallel: nine rows seeding, baking and reaping
# one library and one store at once, and every run inheriting what the last one left. Each row now
# runs through fresh_home.sh on `bake-homes/<row>` (its HOME and its data root), so a row reads only
# what it wrote, this run.
set(_bake_home ${CMAKE_CURRENT_BINARY_DIR}/bake-homes)

# meshbake.quit_during_bake (archived: archive/tests/meshbake) — F5: a run that ends with an import's bake in flight cancels it inside
# its job and exits 0 (bake_quit.sh; the app, headless, its own data root).
set(_quit_home ${CMAKE_CURRENT_BINARY_DIR}/quit-during-bake-home)
file(MAKE_DIRECTORY ${_quit_home}/run)

# THE BAKE DOES NOT DEPEND ON ITS THREADS (IMPORT-SPEED-1) — `bake.determinism.*`.
# The bake runs its meshes, its chains/DAGs and every nearest-surface loop on the
# hardware's threads with ordered reductions; the claim that the OUTPUT is the
# serial one is this suite: a multi-mesh model and the Stanford dragon baked on 1
# thread and on every thread (twice) give byte-identical blobs, and a throw in any
# unit of the bake answers "no bake" and leaves the next bake whole.
# REBUILT TO ITS LAW (TEST-CUT-1; CLAUDE.md Testing: every row at most 60 s): every bake of the law kept — each subject
# once serial and twice on every thread, the ten throw cases (two widths x five units) each followed by a whole bake —
# as THREE rows of the same binary (`--determinism standin|dragon|throws`), each its own process and home; one row was
# 65 s; the parts' solo runs 8.3 / 15.8 / 42.5 s (2026-10-20). A CPU row renders nothing (FRAMES 0); over the 10 s CPU
# budget, so `measure` (tests/CMakeLists.txt). TIMEOUT max(6 x the solo run, the 120 s floor), rounded up.
set(_bake_timeout_standin 120)
set(_bake_timeout_dragon 120)
set(_bake_timeout_throws 260)
foreach(_part standin dragon throws)
    add_test(NAME bake.determinism.${_part}
             COMMAND sh "${JAH_FRESH_HOME}" "${_bake_home}/bake.determinism.${_part}" -- $<TARGET_FILE:test_mesh_bake>
                     --determinism ${_part}
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(bake.determinism.${_part} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home}/bake.determinism.${_part};JAHSHAKA_DATA_ROOT=${_bake_home}/bake.determinism.${_part}/.local/share/Jahshaka"
        TIMEOUT ${_bake_timeout_${_part}} LABELS "meshbake" JAHSHAKA_TEST_FRAMES 0)
    jah_no_display(bake.determinism.${_part})
endforeach()
jah_ship_row(bake.determinism.standin bake.determinism.dragon bake.determinism.throws)   # THE SHIP SET (TEST-CUT-1)

# bake.dag_reaches_root (ATOM-6 stage 2c; SHIP — the lead's promotion at the merge: the bake's end-to-end row for the
# cluster DAG: pruning, empty groups, the lock, monotone errors — 1.1 s): a card soup (2,000 quads in a
# 2 m sphere) and a kit-bash (200 closed 12-triangle boxes) bake to DAGs whose coarsest cut is empty or one cluster,
# errors monotone and under kMaxRelBound x the extent, no group terminal for lack of a target, no pruned piece
# re-locked. Red at the base (terminal at depth 0). A CPU row: FRAMES 0.
add_test(NAME bake.dag_reaches_root COMMAND $<TARGET_FILE:test_mesh_bake> --dag-reaches-root
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(bake.dag_reaches_root PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "meshbake" JAHSHAKA_TEST_FRAMES 0)
jah_no_display(bake.dag_reaches_root)
jah_ship_row(bake.dag_reaches_root)   # THE SHIP SET: the DAG build's end-to-end row (ATOM-6)

# meshbake.archive_bakes (FORWARD-ONLY-1 item 1): an ARCHIVE IMPORT bakes every
# model it brings (archives carry no bakes), so a fresh library that imports the
# shipped samples opens them from bakes with ZERO parses; and with the bakes gone
# the world opens with its models MISSING and a `model.missing` scene issue —
# never a parse. Two app runs over one scratch data root (archive/archive_bakes.sh).
set(_archive_home ${CMAKE_CURRENT_BINARY_DIR}/archive-bakes-home)
file(MAKE_DIRECTORY ${_archive_home}/run)
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
configure_file(archive/archive_bakes_import.js.in ${CMAKE_CURRENT_BINARY_DIR}/archive_bakes_import.js @ONLY)
jah_gpu_row(meshbake.archive_bakes SHIP CLASS app TIER low NEEDS NONE
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/archive/archive_bakes.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                 "${CMAKE_CURRENT_BINARY_DIR}/archive_bakes_import.js"
                 "${CMAKE_CURRENT_SOURCE_DIR}/archive/archive_bakes_open.js"
         WORKING_DIRECTORY ${_archive_home}/run)
# STAGE-CLOSE (TESTING-SPEED-B2; SPECS/TESTING_SPEED_ROWS.md MOVE): the ship and the stage close only.
set_tests_properties(meshbake.archive_bakes PROPERTIES
    ENVIRONMENT "HOME=${_archive_home};JAHSHAKA_DATA_ROOT=${_archive_home}/data"
    TIMEOUT 400 LABELS "meshbake")

# meshbake.shipped_bakes (archived: archive/tests/meshbake) (SHIPPED-BAKES-1's app arm): a FRESH data root boots —
# its seed BAKES every shipped mesh (the primitives, the Teapot, the preview
# docks' subjects, the VR controllers — one per row of src/data/primitives.h) — opens the Avatar
# space, imports a character and its walk clip and PLAYS the clip from its clip
# bake, opens the Materials preview, renders a thumbnail and (with Monado on the
# box) runs a VR session: `app.openStats` reads ZERO runtime parses. Then the
# CLIP BAKES ARE DELETED and the project's open rebuilds them from the clip's
# source — a bake build, still zero parses — with the character's clip back.
# Two app runs over one scratch data root (shipped/shipped_bakes.sh).
set(_shipped_home ${CMAKE_CURRENT_BINARY_DIR}/shipped-bakes-home)
file(MAKE_DIRECTORY ${_shipped_home}/run)

# FRESH HOMES (PROJECT-NAMES-1). A project name is unique in a library now (project.create
# refuses a taken one), so a row that creates a project in a home nothing wipes went red on its
# SECOND run in the same tree (an rc tree's solo retries, build-linux, a stage close) — and every
# such home grew without bound (ui.column_law (archived: archive/tests/ui)'s held 66 'ColumnLaw' projects). Each row below
# creates projects; its home is wiped at the start of every run (WARM: the shader caches stay).
if(TEST meshbake.archive_bakes)
    jah_fresh_home_fixture(meshbake.archive_bakes.fresh_home meshbakeArchiveBakesFreshHome "${CMAKE_CURRENT_BINARY_DIR}/archive-bakes-home" WARM LABELS meshbake)
    set_property(TEST meshbake.archive_bakes APPEND PROPERTY FIXTURES_REQUIRED meshbakeArchiveBakesFreshHome)
endif()

# THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B2; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, MEASURED — every engine frame (Engine::renderOneFrame) of every process the row starts,
# the boot's startup-gate frames included, counted on this tree's Debug build on 2026-10-12 (the count and its
# method: spikes/testing-speed-b2/frames_pass1.tsv). A pool's is the sum over its arms. A headless row renders
# the one empty frame a headless engine walks.
if(TEST meshbake.archive_bakes)
    set_property(TEST meshbake.archive_bakes PROPERTY JAHSHAKA_TEST_FRAMES 551)
endif()

# bake.locked_positions_kept (ATOM-7 stage 2; the builder's row — 1.6 s): A LOCKED POSITION SURVIVES. The card soup, the
# kit-bash and a rough faceted sheet (96 x 96 cells with UV charts: long group borders, sliver fans) bake with no group
# losing a position its level locked — clusterlod's report sums to 0 and so do the positions the DAG's own clusters say
# were lost (recomputed), each within its group's error (max d/err <= 1). Red before the verify's rule 4: the rough sheet
# lost 2 positions at d3 (at the stage-1 tip; the soup and the kit-bash lose none there).
add_test(NAME bake.locked_positions_kept COMMAND $<TARGET_FILE:test_mesh_bake> --locked-positions-kept
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(bake.locked_positions_kept PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "meshbake" JAHSHAKA_TEST_FRAMES 0)
jah_no_display(bake.locked_positions_kept)
jah_ship_row(bake.locked_positions_kept)   # THE SHIP SET: the border-crack law's end-to-end row (ATOM-7; the lead's promotion at the merge)

# bake.cut_curve (ATOM-UPGRADE stage 0; the builder's row): THE CUT CURVE IS MONOTONE AND ENDS EMPTY. The kit-bashed box
# set (30 overlapping boxes, 360 triangles, hard UV seams) bakes to a DAG whose cut — the runtime's rule over the stored
# errors, `test_mesh_bake --cut-curve`'s count — never grows as E grows and, once every finite group is affordable,
# draws nothing (the empty top: stage 5's law). Red at the base. A CPU row: FRAMES 0.
add_test(NAME bake.cut_curve COMMAND $<TARGET_FILE:test_mesh_bake> --cut-curve-law
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(bake.cut_curve PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "meshbake" JAHSHAKA_TEST_FRAMES 0)
jah_no_display(bake.cut_curve)

# bake.objective_is_geometry (ATOM-UPGRADE stage 1; the builder's row): THE RANKING CANNOT WRECK GEOMETRY THE QUADRIC
# PRICES CHEAP. The kit-bashed box set (2 x 2 faces) and a curved facade (a 10 m shell with an 8 x 6 UV atlas), in
# Bistro's units and atlas range, bake as shipped and positions-only; at every depth the shipped DAG's stored error is at
# most 2x the position quadric's. Red at the base (the facade 35x at depth 0: the UV weight ranked the collapses).
add_test(NAME bake.objective_is_geometry COMMAND $<TARGET_FILE:test_mesh_bake> --objective-is-geometry
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(bake.objective_is_geometry PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "meshbake" JAHSHAKA_TEST_FRAMES 0)
jah_no_display(bake.objective_is_geometry)

# bake.terms_exact (ATOM-UPGRADE stage 1; the builder's row): THE MEASUREMENT IS EXACT BOTH WAYS. On the curved shell
# (a quarter sphere, 6,144 triangles) every group's measured bound is within 1.1x of the dense reference. Red at the
# base (1.25: the sampled terms' margin).
add_test(NAME bake.terms_exact COMMAND $<TARGET_FILE:test_mesh_bake> --terms-exact
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(bake.terms_exact PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "meshbake" JAHSHAKA_TEST_FRAMES 0)
jah_no_display(bake.terms_exact)
