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
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_structural_undo PRIVATE IrisGL Qt6::Gui Qt6::Widgets Qt6::Sql)
# A document node IS an engine node (SCENEGRAPH_SPEC D2), so even this suite
# boots the headless engine — NULL render system, no display, no GPU.
jah_document_graph(test_structural_undo)
add_test(NAME commands.structural_undo COMMAND test_structural_undo
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(commands.structural_undo PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "commands")
jah_no_display(commands.structural_undo)

# ---------------------------------------------------------------------------
# commands.undo_clear_cost — CLOSE-1: clearing an undo stack costs MEMORY, not
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
add_executable(test_undo_clear_cost
    test_undo_clear_cost.cpp
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/services/sceneeditservice.h
    ${CMAKE_SOURCE_DIR}/src/commands/deletescenenodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/staticstate.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/structuralundo.cpp
    ${CMAKE_SOURCE_DIR}/src/services/undoservice.cpp
    # the real library, and database.cpp's link closure (the assets.delete
    # round-trip suite's list)
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetmetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/services/animationfile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/rigsignature.cpp
    ${CMAKE_SOURCE_DIR}/src/services/iesprofile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/io/builtinmaterials.cpp)
set_target_properties(test_undo_clear_cost PROPERTIES
    AUTOMOC ON AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(test_undo_clear_cost PRIVATE JAH_REAL_DATABASE)
target_include_directories(test_undo_clear_cost PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_undo_clear_cost PRIVATE
    IrisGL Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Multimedia)

# THE COMMIT COUNT — one commit is one fdatasync, which is the unit the owner's
# 33 s was spent in — comes from sqlite3_commit_hook on the handle Qt's own
# QSQLITE driver opened (the tests/assettray idiom): no instrumentation in the
# production path. Without the header the suite still checks every RULE and the
# wall-clock bound; it just cannot assert the counts.
find_path(JAH_SQLITE3_INCLUDE sqlite3.h)
find_library(JAH_SQLITE3_LIB sqlite3)
if (JAH_SQLITE3_INCLUDE AND JAH_SQLITE3_LIB)
    target_compile_definitions(test_undo_clear_cost PRIVATE JAH_HAVE_SQLITE3)
    target_include_directories(test_undo_clear_cost PRIVATE ${JAH_SQLITE3_INCLUDE})
    target_link_libraries(test_undo_clear_cost PRIVATE ${JAH_SQLITE3_LIB})
else()
    message(STATUS "commands.undo_clear_cost: no sqlite3 headers — commit counts not asserted")
endif()

jah_document_graph(test_undo_clear_cost)
add_test(NAME commands.undo_clear_cost COMMAND test_undo_clear_cost
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(commands.undo_clear_cost PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "commands")
jah_no_display(commands.undo_clear_cost)
