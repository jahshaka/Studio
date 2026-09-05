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
    ${CMAKE_SOURCE_DIR}/src/commands/structuralundo.cpp)
set_target_properties(test_structural_undo PROPERTIES
    AUTOMOC ON AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_structural_undo PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    # iris headers expose assimp and bullet types; these are IrisGL's own include paths
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
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
