# PbrGraphEvaluator tests: shader graph -> iris::PbrMaterial on the CPU.
# No GL, no engine. Compiles the graph-model slice of src/shadergraph directly
# (the module is not a library); texturemanager.cpp is replaced by test_stubs.cpp
# because the real one links the Studio database.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)
add_executable(test_pbr_evaluator
    test_pbr_evaluator.cpp
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/pbrgraphevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/sockethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/generator/shadercontext.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/graph/nodegraph.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/connectionmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/library.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/nodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/properties.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/socketmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/pbrmasternode.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/test.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorpickerwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorview.cpp)
set_target_properties(test_pbr_evaluator PROPERTIES
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(test_pbr_evaluator PRIVATE EFFECT_BUILD_AS_LIB
    JAHSHAKA_TEST_APP_DIR="${CMAKE_SOURCE_DIR}/app/shadergraph/")
target_include_directories(test_pbr_evaluator PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_pbr_evaluator PRIVATE IrisGL Qt6::Widgets Qt6::Sql)
add_test(NAME shadergraph.pbr_evaluator COMMAND test_pbr_evaluator WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadergraph.pbr_evaluator PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "shadergraph")

# Node-library regressions (2026-08-31 audit): Truncate-poisoned saves load,
# bare PropertyNode doesn't crash, vector constants keep fractions, drawer
# names are node titles. Same slice as test_pbr_evaluator plus the full
# LibraryV1 registry and every node family it constructs.
add_executable(test_node_regressions
    test_node_regressions.cpp
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/pbrgraphevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/sockethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/generator/shadercontext.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/graph/nodegraph.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/connectionmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/library.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/libraryv1.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/nodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/properties.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/socketmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/pbrmasternode.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/test.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/math.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/object.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/vector.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/texture.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorpickerwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorview.cpp)
set_target_properties(test_node_regressions PROPERTIES
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(test_node_regressions PRIVATE EFFECT_BUILD_AS_LIB)
target_include_directories(test_node_regressions PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_node_regressions PRIVATE IrisGL Qt6::Widgets Qt6::Sql)
add_test(NAME shadergraph.node_regressions COMMAND test_node_regressions WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadergraph.node_regressions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "shadergraph")

# §3b migration (MATERIALS_EVALUATOR_SPEC §6): old-format graphs — properties
# + PropertyNode instances — load as real constant/texture nodes; re-saves
# stop writing "properties"; the shipped presets round-trip. Same slice as
# test_node_regressions plus fixture dirs.
add_executable(test_panel_migration
    test_panel_migration.cpp
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/pbrgraphevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/sockethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/generator/shadercontext.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/graph/nodegraph.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/connectionmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/library.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/libraryv1.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/nodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/properties.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/socketmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/pbrmasternode.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/test.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/math.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/object.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/vector.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/texture.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorpickerwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorview.cpp)
set_target_properties(test_panel_migration PROPERTIES
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(test_panel_migration PRIVATE EFFECT_BUILD_AS_LIB
    JAHSHAKA_TEST_APP_DIR="${CMAKE_SOURCE_DIR}/app/shadergraph/"
    JAHSHAKA_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/")
target_include_directories(test_panel_migration PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_panel_migration PRIVATE IrisGL Qt6::Widgets Qt6::Sql)
add_test(NAME shadergraph.panel_migration COMMAND test_panel_migration WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadergraph.panel_migration PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "shadergraph")

# §3a selection (MATERIALS_EVALUATOR_SPEC §6): GraphNodeScene selection API +
# NodePropertiesPanel behavior. Adds the scene/panel/widget layer to the
# node slice.
add_executable(test_selection
    test_selection.cpp
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/pbrgraphevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/sockethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/undoredo.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/generator/shadercontext.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/graph/graphnode.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/graph/graphnodescene.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/graph/nodegraph.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/graph/socket.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/graph/socketconnection.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/dialogs/searchdialog.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/connectionmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/library.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/libraryv1.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/nodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/properties.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/socketmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/pbrmasternode.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/test.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/math.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/object.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/vector.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/texture.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/widgets/graphicsview.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/widgets/listwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/widgets/materialsettingswidget.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/widgets/nodepropertiespanel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/widgets/treewidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorpickerwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorview.cpp)
set_target_properties(test_selection PROPERTIES
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(test_selection PRIVATE EFFECT_BUILD_AS_LIB)
target_include_directories(test_selection PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_selection PRIVATE IrisGL Qt6::Widgets Qt6::Sql)
add_test(NAME shadergraph.selection COMMAND test_selection WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadergraph.selection PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "shadergraph")

# graph.selectNode/selectedNode/deselect verbs end-to-end: the real app binary
# running --script --headless (document verbs; HOME redirected like the
# scripting e2e suites).
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
configure_file(e2e_graph_selection.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_graph_selection.js @ONLY)
set(_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-selection)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME shadergraph.selection_verbs
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_graph_selection.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(shadergraph.selection_verbs PROPERTIES
    ENVIRONMENT "HOME=${_home}"
    LABELS "shadergraph"
    TIMEOUT 120)

# One-shot §3b tool: re-saves a shipped preset through the real migration
# (NOT a test — invoked by hand; the re-saved presets are committed).
add_executable(resave_presets
    resave_presets.cpp
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/pbrgraphevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/sockethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/generator/shadercontext.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/graph/nodegraph.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/connectionmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/library.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/libraryv1.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/nodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/properties.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/models/socketmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/pbrmasternode.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/test.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/math.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/object.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/vector.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/nodes/texture.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorpickerwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/colorview.cpp)
set_target_properties(resave_presets PROPERTIES
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(resave_presets PRIVATE EFFECT_BUILD_AS_LIB)
target_include_directories(resave_presets PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(resave_presets PRIVATE IrisGL Qt6::Widgets Qt6::Sql)

# Fuzzy matcher for the tab-search node palette: pure QString logic.
add_executable(test_fuzzy_search test_fuzzy_search.cpp)
target_include_directories(test_fuzzy_search PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(test_fuzzy_search PROPERTIES
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_fuzzy_search PRIVATE Qt6::Core)
add_test(NAME shadergraph.fuzzy_search COMMAND test_fuzzy_search WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadergraph.fuzzy_search PROPERTIES TIMEOUT 60 LABELS "shadergraph")
