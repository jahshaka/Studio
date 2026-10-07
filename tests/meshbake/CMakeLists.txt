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
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_mesh_bake PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    # the Stanford dragon tests/atom extracts at configure time (atom.lod_bound_bar)
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

set(_bake_home ${CMAKE_CURRENT_BINARY_DIR}/bake-home)
file(MAKE_DIRECTORY ${_bake_home})
add_test(NAME meshbake.roundtrip COMMAND test_mesh_bake
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# HOME is redirected so the fixture store and settings can never address the
# developer's real library, and the store root is overridden inside the test
# on top of that.
set_tests_properties(meshbake.roundtrip PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home};JAHSHAKA_DATA_ROOT=${_bake_home}/.local/share/Jahshaka"
    TIMEOUT 120 LABELS "meshbake")

# ---------------------------------------------------------------------------
# DISPLAY-FREE, AND PROVEN (TEST_GATE_AUDIT.md §4.5). Every suite named here
# claims — in its own comment, or by running --headless, or by building nothing
# but a document — that it needs no X server. The claim was never enforced, so
# it drifted: a suite that quietly opened a display still passed on a developer
# box and would have failed the first CI runner without one, and every
# unnecessary X connection is one more thing four parallel jobs contend over.
# jah_no_display runs these with DISPLAY and WAYLAND_DISPLAY UNSET, which turns
# the claim into a gate. Verified 2026-09-09: each of these passes displayless
# with the same assertion count it has with a display.
# SURFACE CARDS (SURFACE-CACHE phase 1). Its own test name because it is its
# own claim — the card GENERATOR's policy and coverage against the shipped
# meshes — and because it prints a bake-time table the assessment lacked.
add_test(NAME meshbake.cards COMMAND test_mesh_bake --cards
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(meshbake.cards PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home};JAHSHAKA_DATA_ROOT=${_bake_home}/.local/share/Jahshaka"
    TIMEOUT 120 LABELS "meshbake")
# meshbake.cards.timing — the bake's two wall-clock bars (the 4x-triangles-not-4x-time
# ratio and the 4,000 ms ceiling), nightly (lane D6B-GATE-SHAPE; audit §5 FIX-TIME):
# the same binary with JAHSHAKA_TIMING_BARS=1, quiet box, the GPU-timing lock.
jah_gpu_exclusive_test(NAME meshbake.cards.timing CLASS none RUN_TIMEOUT 180 COMMAND test_mesh_bake --cards
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(meshbake.cards.timing PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home};JAHSHAKA_DATA_ROOT=${_bake_home}/.local/share/Jahshaka;JAHSHAKA_TIMING_BARS=1"
    LABELS "meshbake;nightly;quiet-box")

# THE LOD BOUND'S PHYSICS BAR (ATOM-LOD-BOUND-1) — `atom.lod_bound_bar`. Every
# level of the shipped primitives, the Stanford dragon and a scan stand-in (a floor,
# columns and 600 debris islands, generated by the suite) is (a) at least a dense
# two-sided reference distance, (b) at most twice the simplifier's own error, and
# on the stand-in (c) no dropped island is bigger than its level's bound and level 1
# is affordable by 150 m on a 1080-line view. Chains only (no DAG/SDF/cards), so it
# is the bound's own claim; the dragon's chain dominates the run.
# ITS SECOND ARM IS THE MEASURED BOUND (ATOM P1's AT-A5; test_mesh_bake.cpp section 9; it
# was the atom.error_bound row): through the BAKE's own route (MeshBake::buildFromFile) every
# subject carries one bound and one quadric per level, the bounds non-decreasing, every level
# inside its stored bound under an 8x-dense re-measure, and the bound-versus-quadric ratio
# table — the evidence AT-A5 was missing — is printed. A regression check, not an independent
# derivation (the dense re-measure walks the same exact removed vertices).
add_test(NAME atom.lod_bound_bar COMMAND test_mesh_bake --bound-bar
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(atom.lod_bound_bar PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home};JAHSHAKA_DATA_ROOT=${_bake_home}/.local/share/Jahshaka"
    TIMEOUT 120 LABELS "meshbake")
jah_no_display(atom.lod_bound_bar)

# THE SAME BAR ON THE CLUSTER DAG'S GROUPS (ATOM-CLUSTER-CUT) — `atom.dag_bound_bar`:
# every group of every subject's DAG is (a) at least its dense reference (8x the
# samples against whole grids, the per-facet reference pattern, the island caps, no
# margin). (b) at most 2x clusterlod's own error (2.5x where an island dropped) and
# (c) on the stand-in no dropped island bigger than its group's error are GATED by the
# row `atom.dag_bound_target` — a BAR since DAG-LOCK-1 gave the DAG's build the chain's
# displacement lock (it was a photon-target row until then; the stand-in read 24 of 30
# groups over 2.5x without the lock).
add_test(NAME atom.dag_bound_bar COMMAND test_mesh_bake --dag-bar
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(atom.dag_bound_bar PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home};JAHSHAKA_DATA_ROOT=${_bake_home}/.local/share/Jahshaka"
    TIMEOUT 180 LABELS "meshbake")
jah_no_display(atom.dag_bound_bar)
add_test(NAME atom.dag_bound_target COMMAND test_mesh_bake --dag-bar --target
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(atom.dag_bound_target PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home};JAHSHAKA_DATA_ROOT=${_bake_home}/.local/share/Jahshaka"
    TIMEOUT 900 LABELS "meshbake")
jah_no_display(atom.dag_bound_target)

# THE SIGNED DISTANCE FIELD (ATOM P2 / SUB-S5-SDF) — `meshbake.sdf`. The zero
# crossing against level 0's real surface, the sign inside and outside, the
# resolution's tie to the chain's honesty, the format round trip, and the refusal
# for a skinned mesh.
add_test(NAME meshbake.sdf COMMAND test_mesh_bake --sdf
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(meshbake.sdf PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home};JAHSHAKA_DATA_ROOT=${_bake_home}/.local/share/Jahshaka"
    TIMEOUT 120 LABELS "meshbake")

# ATOM P2 — THE SEED AND WHAT IT OWES (the design's §2.3 acceptance).
#
#   meshbake.primitives_baked   a FRESH library seeded the way a launch seeds one
#                               (PrimitiveAssets::seedAll): every row created with
#                               its reserved guid and its platform mark, a chain of
#                               >= 2 levels for every mesh the bake's floor lets it,
#                               cards that name levels the chain HAS, an SDF and a
#                               measured bound per level — and a SECOND seed that
#                               bakes nothing.
#   meshbake.rebake_generation  BAKE-RETIRE-1: a bake's file name is generation-
#                               independent and the store ingests OR IGNORE, so a
#                               re-bake after a kFormatVersion bump used to be
#                               silently dropped and a baked asset with no parse
#                               behind it drew NOTHING. The previous generation is
#                               planted by hand — no test home is old enough to have
#                               one — and the new one must win, with the superseded
#                               object unlinked and reapable.
#   meshbake.hemisphere_winding HEMISPHERE-CAP-1, as a property of EVERY shipped
#                               mesh: no face may be wound against its own declared
#                               vertex normals (the cap's 32 were).
add_test(NAME meshbake.primitives_baked COMMAND test_mesh_bake --primitives-baked
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(meshbake.primitives_baked PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home};JAHSHAKA_DATA_ROOT=${_bake_home}/.local/share/Jahshaka"
    TIMEOUT 120 LABELS "meshbake")

add_test(NAME meshbake.rebake_generation COMMAND test_mesh_bake --rebake-generation
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(meshbake.rebake_generation PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home};JAHSHAKA_DATA_ROOT=${_bake_home}/.local/share/Jahshaka"
    TIMEOUT 120 LABELS "meshbake")

add_test(NAME meshbake.hemisphere_winding COMMAND test_mesh_bake --hemisphere-winding
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(meshbake.hemisphere_winding PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home};JAHSHAKA_DATA_ROOT=${_bake_home}/.local/share/Jahshaka"
    TIMEOUT 120 LABELS "meshbake")

# THE BAKE DOES NOT DEPEND ON ITS THREADS (IMPORT-SPEED-1) — `bake.determinism`.
# The bake runs its meshes, its chains/DAGs and every nearest-surface loop on the
# hardware's threads with ordered reductions; the claim that the OUTPUT is the
# serial one is this suite: a multi-mesh model and the Stanford dragon baked on 1
# thread and on every thread (twice) give byte-identical blobs.
add_test(NAME bake.determinism COMMAND test_mesh_bake --determinism
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(bake.determinism PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home};JAHSHAKA_DATA_ROOT=${_bake_home}/.local/share/Jahshaka"
    TIMEOUT 480 LABELS "meshbake")
jah_no_display(bake.determinism)

jah_no_display(meshbake.primitives_baked)
jah_no_display(meshbake.rebake_generation)
jah_no_display(meshbake.hemisphere_winding)

jah_no_display(meshbake.roundtrip)
jah_no_display(meshbake.cards meshbake.cards.timing)
jah_no_display(meshbake.sdf)

# meshbake.archive_bakes (FORWARD-ONLY-1 item 1): an ARCHIVE IMPORT bakes every
# model it brings (archives carry no bakes), so a fresh library that imports the
# shipped samples opens them from bakes with ZERO parses; and with the bakes gone
# the world opens with its models MISSING and a `model.missing` scene issue —
# never a parse. Two app runs over one scratch data root (archive/archive_bakes.sh).
set(_archive_home ${CMAKE_CURRENT_BINARY_DIR}/archive-bakes-home)
file(MAKE_DIRECTORY ${_archive_home}/run)
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
configure_file(archive/archive_bakes_import.js.in ${CMAKE_CURRENT_BINARY_DIR}/archive_bakes_import.js @ONLY)
jah_gpu_row(meshbake.archive_bakes CLASS app
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/archive/archive_bakes.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                 "${CMAKE_CURRENT_BINARY_DIR}/archive_bakes_import.js"
                 "${CMAKE_CURRENT_SOURCE_DIR}/archive/archive_bakes_open.js"
         WORKING_DIRECTORY ${_archive_home}/run)
set_tests_properties(meshbake.archive_bakes PROPERTIES
    ENVIRONMENT "HOME=${_archive_home};JAHSHAKA_DATA_ROOT=${_archive_home}/data"
    TIMEOUT 400 LABELS "meshbake")

# meshbake.shipped_bakes (SHIPPED-BAKES-1's app arm): a FRESH data root boots —
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
configure_file(shipped/shipped_bakes_run1.js.in ${CMAKE_CURRENT_BINARY_DIR}/shipped_bakes_run1.js @ONLY)
jah_gpu_row(meshbake.shipped_bakes CLASS vr
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/shipped/shipped_bakes.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                 "${CMAKE_CURRENT_BINARY_DIR}/shipped_bakes_run1.js"
                 "${CMAKE_CURRENT_SOURCE_DIR}/shipped/shipped_bakes_run2.js"
                 "${CMAKE_SOURCE_DIR}/tests/vr/run_vr_app.sh"
                 "/usr/share/openxr/1/openxr_monado.json"
                 "${CMAKE_SOURCE_DIR}/src/data/primitives.h"
         WORKING_DIRECTORY ${_shipped_home}/run)
set_tests_properties(meshbake.shipped_bakes PROPERTIES
    ENVIRONMENT "${JAH_TEST_DISPLAY_ENV}HOME=${_shipped_home};JAHSHAKA_DATA_ROOT=${_shipped_home}/data"
    RESOURCE_LOCK monado
    TIMEOUT 400 LABELS "meshbake")

# FRESH HOMES (PROJECT-NAMES-1). A project name is unique in a library now (project.create
# refuses a taken one), so a row that creates a project in a home nothing wipes went red on its
# SECOND run in the same tree (an rc tree's solo retries, build-linux, the nightly) — and every
# such home grew without bound (ui.column_law's held 66 'ColumnLaw' projects). Each row below
# creates projects; its home is wiped at the start of every run (WARM: the shader caches stay).
if(TEST meshbake.archive_bakes)
    jah_fresh_home_fixture(meshbake.archive_bakes.fresh_home meshbakeArchiveBakesFreshHome "${CMAKE_CURRENT_BINARY_DIR}/archive-bakes-home" WARM LABELS "meshbake")
    set_property(TEST meshbake.archive_bakes APPEND PROPERTY FIXTURES_REQUIRED meshbakeArchiveBakesFreshHome)
endif()
if(TEST meshbake.shipped_bakes)
    jah_fresh_home_fixture(meshbake.shipped_bakes.fresh_home meshbakeShippedBakesFreshHome "${CMAKE_CURRENT_BINARY_DIR}/shipped-bakes-home" WARM LABELS "meshbake")
    set_property(TEST meshbake.shipped_bakes APPEND PROPERTY FIXTURES_REQUIRED meshbakeShippedBakesFreshHome)
endif()
