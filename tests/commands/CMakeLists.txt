# commands.structural_undo — UNDO v1.5 (SPECS/SCENEGRAPH_SPEC.md §3; scene-graph
# audit F5; scripting audit F3). See test_structural_undo.cpp for what it pins.
#
# Compiles the five undo commands and their two shared halves, with link stubs
# for the services they notify (test_stubs.cpp) — the tests/ui idiom. No Sql, no
# io/ layer, no database: this suite is about sibling indices and SCENE_STATIC
# bookkeeping, and it must stay fast enough to run on every gate.
find_package(Qt6 REQUIRED COMPONENTS Gui Widgets Sql)
add_executable(test_structural_undo
    test_structural_undo.cpp
    test_stubs.cpp
    # AUTOMOC needs the header in the source list to emit SceneEditService's
    # metaobject (the suite constructs one for the rebuild-fallback case).
    ${CMAKE_SOURCE_DIR}/src/services/sceneeditservice.h
    ${CMAKE_SOURCE_DIR}/src/commands/addscenenodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/deletescenenodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/reparentscenenodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/setnodepropertycommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/transformscenenodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/staticstate.cpp
    # The sun PIN is an ordinary World-panel row now (SKY_LIGHT_SPEC.md §5:
    # SunLightLinkCommand is deleted with the sky's steering).
    ${CMAKE_SOURCE_DIR}/src/commands/scenepropertycommand.cpp
    ${CMAKE_SOURCE_DIR}/src/services/worldmodes.cpp
    # the VR settings table the sceneprops rows are generated from (VR-WORLD-1)
    ${CMAKE_SOURCE_DIR}/src/services/vrworld.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/structuralundo.cpp)
set_target_properties(test_structural_undo PROPERTIES
    AUTOMOC ON AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_structural_undo PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    # iris headers expose bullet types; this is IrisGL's own include path
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src
    # worldmodes.cpp reads the ENGINE's tier table (giQualityFacts, a
    # header-only function in jahshaka/engine/Types.h) so a tier's description
    # cannot drift from what the tier does — render audit A5. Header only; this
    # target links no engine.
    ${CMAKE_SOURCE_DIR}/irisgl/engine/include
)
target_link_libraries(test_structural_undo PRIVATE IrisGL Qt6::Gui Qt6::Widgets Qt6::Sql)
# A document node IS an engine node (SCENEGRAPH_SPEC D2), so even this suite
# boots the headless engine — NULL render system, no display, no GPU.
jah_document_graph(test_structural_undo)
add_test(NAME commands.structural_undo COMMAND test_structural_undo
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
jah_ship_row(commands.structural_undo)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(commands.structural_undo PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "commands")
jah_no_display(commands.structural_undo)

# ---------------------------------------------------------------------------
# commands.undo_clear_cost (archived: archive/tests/commands) — CLOSE-1: clearing an undo stack costs MEMORY, not
# disk syncs.
#
# The owner's project close froze the UI thread for 33,156 ms because
# DeleteSceneNodeCommand's DESTRUCTOR ran Database::deleteAsset — one
# transaction, one fdatasync — per command, and QUndoStack::clear() destroys
# hundreds at once. Unlike its sibling above this suite compiles the REAL
# database.cpp (and reuses the same stubs with JAH_REAL_DATABASE, which stands
# the Database stub down): the subject IS the destructor's effect on the
# library, so a stubbed database would prove nothing.
find_package(Qt6 REQUIRED COMPONENTS Gui Widgets Sql Multimedia)

# THE COMMIT COUNT — one commit is one fdatasync, which is the unit the owner's
# 33 s was spent in — comes from sqlite3_commit_hook on the handle Qt's own
# QSQLITE driver opened (the tests/assettray idiom): no instrumentation in the
# production path. Without the header the suite still checks every RULE and the
# wall-clock bound; it just cannot assert the counts.
find_path(JAH_SQLITE3_INCLUDE sqlite3.h)
find_library(JAH_SQLITE3_LIB sqlite3)

# ---------------------------------------------------------------------------
# commands.db_batch (archived: archive/tests/commands) — CLOSE-2: ONE GESTURE, ONE COMMIT.
#
# The create-side half of CLOSE-1. An asset row written outside a transaction
# is autocommitted, and an autocommit is a real transaction (journal, write,
# fdatasync, unlink): 300 scripted primitives paid it 300+ times. The suite
# pins the counted batch scope (Database::beginBatch/DbBatch), its stand-down
# for a real transaction owner — the property that keeps the import rollback
# honest — and ResetMaterialCommand's one commit per Reset.
#
# Same shape as its sibling above: the REAL database.cpp, and SQLite's own
# commit hook as ground truth.
find_package(Qt6 REQUIRED COMPONENTS Gui Widgets Sql Multimedia)

# ---------------------------------------------------------------------------
# commands.preset_copy (archived: archive/tests/commands) — PRESET-EDIT-1: the first edit of a preset a project
# holds is ONE undo step (the copy, the pin move and the use edges together).
#
# The same idiom as commands.pin_asset (archived: archive/tests/commands) below it: the REAL Database and the REAL
# content-addressed store on a throwaway SQLite file, because a pin, an edge
# and a row are catalog facts a stub could not tell the truth about.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia)

# ---------------------------------------------------------------------------
# commands.pin_asset (archived: archive/tests/commands) — MATERIAL_BUNDLE_SPEC phase 3 / the materials audit's F5:
# an apply that PINS is an apply that UNPINS on undo.
#
# The pin was a bare service call after the undo macro closed, so an undone
# apply left the material in the project's tray with its textures. This drives
# the command against the REAL Database and the REAL content-addressed store on
# a throwaway SQLite file — the tests/materialbundle idiom, same source list —
# because a pin is a catalog fact and a stub could not tell the truth about it.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia)
