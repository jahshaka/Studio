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
    TIMEOUT 180 LABELS "meshbake")

# THE MEASURED LOD BOUND (ATOM P1's AT-A5) — `atom.error_bound`. Its own test name
# because it is its own claim: for every level of every shipped mesh, an AREA-ONLY
# sampling eight times as dense as the bake's finds no point of that level further
# from level 0 than the length the bake stored. It also prints the bound-versus-
# quadric ratio table, which is the evidence AT-A5 was missing.
#
# IT IS A REGRESSION CHECK, NOT AN INDEPENDENT DERIVATION: the dense re-measure walks
# the same exact removed vertices, so where that term carries the maximum it reads
# 1.0000 by construction; what it judges is the sampling margin and the pipeline
# (sampler, grid, floor, monotonicity). The physics bar is atom.lod_bound_bar below.
# In tests/meshbake because the subject is the BAKE and needs no engine; the name
# is the design's (SPECS/atom/A2_HONEST_GEOMETRY_AND_EVERY_ASSET_DESIGN.md §1.1).
add_test(NAME atom.error_bound COMMAND test_mesh_bake --error-bound
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(atom.error_bound PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home};JAHSHAKA_DATA_ROOT=${_bake_home}/.local/share/Jahshaka"
    TIMEOUT 600 LABELS "meshbake")

# THE LOD BOUND'S PHYSICS BAR (ATOM-LOD-BOUND-1) — `atom.lod_bound_bar`. Every
# level of the shipped primitives, the Stanford dragon and a scan stand-in (a floor,
# columns and 600 debris islands, generated by the suite) is (a) at least a dense
# two-sided reference distance, (b) at most twice the simplifier's own error, and
# on the stand-in (c) no dropped island is bigger than its level's bound and level 1
# is affordable by 150 m on a 1080-line view. Chains only (no DAG/SDF/cards), so it
# is the bound's own claim; the dragon's chain dominates the run.
add_test(NAME atom.lod_bound_bar COMMAND test_mesh_bake --bound-bar
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(atom.lod_bound_bar PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home};JAHSHAKA_DATA_ROOT=${_bake_home}/.local/share/Jahshaka"
    TIMEOUT 600 LABELS "meshbake")
jah_no_display(atom.lod_bound_bar)

# THE SIGNED DISTANCE FIELD (ATOM P2 / SUB-S5-SDF) — `meshbake.sdf`. The zero
# crossing against level 0's real surface, the sign inside and outside, the
# resolution's tie to the chain's honesty, the format round trip, and the refusal
# for a skinned mesh.
add_test(NAME meshbake.sdf COMMAND test_mesh_bake --sdf
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(meshbake.sdf PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home};JAHSHAKA_DATA_ROOT=${_bake_home}/.local/share/Jahshaka"
    TIMEOUT 300 LABELS "meshbake")

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
    TIMEOUT 300 LABELS "meshbake")

add_test(NAME meshbake.rebake_generation COMMAND test_mesh_bake --rebake-generation
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(meshbake.rebake_generation PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home};JAHSHAKA_DATA_ROOT=${_bake_home}/.local/share/Jahshaka"
    TIMEOUT 180 LABELS "meshbake")

add_test(NAME meshbake.hemisphere_winding COMMAND test_mesh_bake --hemisphere-winding
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(meshbake.hemisphere_winding PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;HOME=${_bake_home};JAHSHAKA_DATA_ROOT=${_bake_home}/.local/share/Jahshaka"
    TIMEOUT 120 LABELS "meshbake")

jah_no_display(meshbake.primitives_baked)
jah_no_display(meshbake.rebake_generation)
jah_no_display(meshbake.hemisphere_winding)

jah_no_display(meshbake.roundtrip)
jah_no_display(meshbake.cards)
jah_no_display(atom.error_bound)
jah_no_display(meshbake.sdf)
