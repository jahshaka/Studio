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
    Qt6::Widgets Qt6::Sql Qt6::Multimedia
    meshoptimizer)   # the cut curve's STOCK arm: the clusterizer + partitioner libIrisGL no longer pulls in
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
# 65 s. A CPU row renders nothing (FRAMES 0); each under the CPU row's 10 s since TEST-STREAMLINE-1 5c (no `measure`).
# THE THROWS SPLIT BY PROOF (TEST-STREAMLINE-1 5c): the throws part ran 17.92 s solo against a CPU row's 10 s, 5.26 s of it
# the one-thread reference bake the standin part already makes - so the standin part WRITES its one-thread blob (once its
# three blobs agree) to ${_bake_ref} (FIXTURES_SETUP bakeSerialReference) and the throws run as two rows that READ it
# (FIXTURES_REQUIRED): .early (units 1, 3, 40) and .late (units 400, 2000), every throw and every whole bake after it
# compared with the same one-thread bytes, refused when the model is not the one the reference baked. The reference is
# removed by bake.determinism.reference.clean (FIXTURES_CLEANUP). Run one alone: ctest -R bake.determinism.throws.late.
set(_bake_ref ${_bake_home}/serial-reference)
foreach(_part standin dragon throws.early throws.late)
    string(REPLACE "throws." "throws:" _arg ${_part})
    set(_ref_arg "")
    if(NOT _part STREQUAL "dragon")
        set(_ref_arg "${_bake_ref}")
    endif()
    add_test(NAME bake.determinism.${_part}
             COMMAND sh "${JAH_FRESH_HOME}" "${_bake_home}/bake.determinism.${_part}" -- $<TARGET_FILE:test_mesh_bake>
                     --determinism ${_arg} ${_ref_arg}
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(bake.determinism.${_part} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home}/bake.determinism.${_part};JAHSHAKA_DATA_ROOT=${_bake_home}/bake.determinism.${_part}/.local/share/Jahshaka"
        TIMEOUT 60 LABELS "meshbake" JAHSHAKA_TEST_FRAMES 0)
    jah_no_display(bake.determinism.${_part})
endforeach()
set_tests_properties(bake.determinism.standin PROPERTIES FIXTURES_SETUP bakeSerialReference)
set_tests_properties(bake.determinism.throws.early bake.determinism.throws.late PROPERTIES FIXTURES_REQUIRED bakeSerialReference)
add_test(NAME bake.determinism.reference.clean COMMAND ${CMAKE_COMMAND} -E rm -rf "${_bake_ref}")
set_tests_properties(bake.determinism.reference.clean PROPERTIES TIMEOUT 60 LABELS "meshbake" JAHSHAKA_TEST_FRAMES 0
                     FIXTURES_CLEANUP bakeSerialReference)
jah_no_display(bake.determinism.reference.clean)

# bake.dag_reaches_root (ATOM-6 stage 2c; the bake's end-to-end row for the cluster DAG, (the ship set left at draft B, TEST-STREAMLINE-1 4a: LONG only); rewritten for ATOM-2's
# builder, the law kept): a card soup (2,000 quads in a 2 m sphere) and a kit-bash (200 closed 12-triangle boxes) bake
# to DAGs that END — the top cut draws nothing over a non-empty root, the stored errors monotone, no group terminal,
# the builder under its depth cap. A CPU row: FRAMES 0.
add_test(NAME bake.dag_reaches_root COMMAND $<TARGET_FILE:test_mesh_bake> --dag-reaches-root
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(bake.dag_reaches_root PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "meshbake" JAHSHAKA_TEST_FRAMES 0)
jah_no_display(bake.dag_reaches_root)

# meshbake.archive_bakes.import / .rebuild / .missing (FORWARD-ONLY-1 item 1): an ARCHIVE IMPORT bakes every model it
# brings (archives carry no bakes), so a fresh library that imports the shipped samples opens them from bakes with ZERO
# parses; with the bakes gone the open rebuilds them from their sources; with the sources gone too the world opens with
# its models MISSING and a `model.missing` scene issue — never a parse (archive/archive_bakes.sh). SPLIT BY PROOF, ONE
# ROW A LAUNCH (ORPHAN-REDS-1, 2026-11-03, URGENT #136's form): the three launches in one row were 34.53 s solo, 65.6 s at
# the shared box's x1.9 - a fixture chain on one data root now, each part its launch's laws: .import (run 1, FIXTURES_SETUP
# meshbakeArchiveImported) -> .rebuild (run 2, meshbakeArchiveRebuilt) -> .missing (run 3). The home is wiped at the
# fixture's next setup.
# CODE ROWS ON THE NULL RENDER SYSTEM (TEST-STREAMLINE-1 3a item 6): every verb the three launches drive is a document verb
# (project.importArchive, assets.bakeAll, project.open/close/list, app.openStats, editor.checkScene - docs/SCRIPTING.md's
# needs column) and the bake is CPU work, so each launch is `--headless` (visible on the command: archive_bakes.sh hands
# its trailing arguments to every launch): no display, no card, no shader cache (measured by hand on NULL, DISPLAY unset,
# 2026-11-03: 6.19 / 5.35 / 3.38 s, every law green; on the card they were 18.75 / 7.39 / 5.52). Still SHIP rows.
# WHAT THE NULL ROUTE NO LONGER RUNS, and was never these rows' law (the 3a audit): the ENGINE's consumption of a rebuilt
# bake (its GPU upload) and the old three-launch row's engine teardown. Every law kept is document-side and cannot pass
# vacuously on NULL (bakeHits > 0, bakeBuilds > 0, missing.length > 0).
set(_archive_home ${CMAKE_CURRENT_BINARY_DIR}/archive-bakes-home)
file(MAKE_DIRECTORY ${_archive_home}/run)
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
configure_file(archive/archive_bakes_import.js.in ${CMAKE_CURRENT_BINARY_DIR}/archive_bakes_import.js @ONLY)
foreach(_part import rebuild missing)
    add_test(NAME meshbake.archive_bakes.${_part}
             COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/archive/archive_bakes.sh"
                     "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                     "${CMAKE_CURRENT_BINARY_DIR}/archive_bakes_import.js"
                     "${CMAKE_CURRENT_SOURCE_DIR}/archive/archive_bakes_open.js" ${_part} --headless
             WORKING_DIRECTORY ${_archive_home}/run)
    jah_ship_row(meshbake.archive_bakes.${_part})   # THE SHIP SET (as it was: jah_gpu_row ... SHIP)
    set_tests_properties(meshbake.archive_bakes.${_part} PROPERTIES
        ENVIRONMENT "HOME=${_archive_home};JAHSHAKA_DATA_ROOT=${_archive_home}/data"
        TIMEOUT 60 LABELS "meshbake")
    jah_no_display(meshbake.archive_bakes.${_part})
endforeach()
set_tests_properties(meshbake.archive_bakes.import PROPERTIES FIXTURES_SETUP meshbakeArchiveImported)
set_tests_properties(meshbake.archive_bakes.rebuild PROPERTIES FIXTURES_REQUIRED meshbakeArchiveImported
    FIXTURES_SETUP meshbakeArchiveRebuilt)
set_tests_properties(meshbake.archive_bakes.missing PROPERTIES FIXTURES_REQUIRED meshbakeArchiveRebuilt)

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
# creates projects; its home is wiped at the start of every run (no WARM: the archive chain is the NULL render system's,
# which keeps no shader cache - TEST-STREAMLINE-1 3a item 6).
if(TEST meshbake.archive_bakes.import)
    jah_fresh_home_fixture(meshbake.archive_bakes.fresh_home meshbakeArchiveBakesFreshHome "${CMAKE_CURRENT_BINARY_DIR}/archive-bakes-home" LABELS meshbake)
    foreach(_part import rebuild missing)
        set_property(TEST meshbake.archive_bakes.${_part} APPEND PROPERTY FIXTURES_REQUIRED meshbakeArchiveBakesFreshHome)
    endforeach()
endif()

# THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B2; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, MEASURED — every engine frame (Engine::renderOneFrame) of every process the row starts,
# the boot's startup-gate frames included, counted on this tree's Debug build on 2026-10-12 (the count and its
# method: spikes/testing-speed-b2/frames_pass1.tsv). A pool's is the sum over its arms. A headless row renders
# the one empty frame a headless engine walks.
# (meshbake.archive_bakes.*: NULL headless since TEST-STREAMLINE-1 3a item 6 - no card, no frames to count)

# bake.locked_positions_kept (ATOM-7 stage 2; (the ship set left at draft B, TEST-STREAMLINE-1 4a: LONG only); rewritten for ATOM-2's builder, the law kept): A LOCKED POSITION
# SURVIVES. The card soup, the kit-bash and a rough faceted sheet (96 x 96 cells with UV charts: long group borders,
# sliver fans) bake with no group's output losing a position its level locked — recomputed from the DAG itself (a
# group's external positions: its members' edges twinned by a pool cluster outside it).
add_test(NAME bake.locked_positions_kept COMMAND $<TARGET_FILE:test_mesh_bake> --locked-positions-kept
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(bake.locked_positions_kept PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "meshbake" JAHSHAKA_TEST_FRAMES 0)
jah_no_display(bake.locked_positions_kept)

# bake.cut_curve (ATOM-UPGRADE stage 0; the builder's row): THE CUT CURVE IS MONOTONE AND ENDS EMPTY. The kit-bashed box
# set (30 overlapping boxes, 360 triangles, hard UV seams) bakes to a DAG whose cut — the runtime's rule over the stored
# errors, `test_mesh_bake --cut-curve`'s count — never grows as E grows and, once every finite group is affordable,
# draws nothing (the empty top: stage 5's law). Red at the base. A CPU row: FRAMES 0.
add_test(NAME bake.cut_curve COMMAND $<TARGET_FILE:test_mesh_bake> --cut-curve-law
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(bake.cut_curve PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "meshbake" JAHSHAKA_TEST_FRAMES 0)
jah_no_display(bake.cut_curve)

# THE ATOM-2 ROWS (ATOM-2 part A; the builder's rows, each a `--atom2 <row>` verb, CPU only: FRAMES 0). The five below
# were promoted at the ATOM-2 merge as the DAG builder's end-to-end laws (the ship set left at draft B, TEST-STREAMLINE-1 4a: LONG only). The fixtures: the
# Bistro-units kit-bashed box set, a 10 m curved shell with a 4 x 3 atlas, the card soup, the kit-bash, the rough sheet
# and a 100-triangle ring (tests/meshbake/atom2_rows.h says what each row asserts). The builder's other three rows
# (atom2.adjacency_by_position, atom2.borders_move, atom2.locked_positions_kept — the last one
# bake.locked_positions_kept's law over the same fixtures) were archived at the merge: archive/tests/ATOM-2/.
set(_atom2_rows ladder_never_forced ends_empty one_step_drift_bound cards_merge_within_budget level_table_is_a_cut)
foreach(_row IN LISTS _atom2_rows)
    add_test(NAME atom2.${_row} COMMAND $<TARGET_FILE:test_mesh_bake> --atom2 ${_row}
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(atom2.${_row} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "meshbake" JAHSHAKA_TEST_FRAMES 0)
    jah_no_display(atom2.${_row})
endforeach()
