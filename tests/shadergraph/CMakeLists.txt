# PbrGraphEvaluator tests: shader graph -> iris::PbrMaterial on the CPU.
# No GL, no engine. Compiles the graph-model slice of src/shadergraph directly
# (the module is not a library); texturemanager.cpp is replaced by test_stubs.cpp
# because the real one links the Studio database.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Concurrent)
add_executable(test_pbr_evaluator
    test_pbr_evaluator.cpp
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/bakeprogram.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/graphbaker.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/pbrgraphevaluator.cpp
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
target_link_libraries(test_pbr_evaluator PRIVATE IrisGL Qt6::Widgets Qt6::Sql Qt6::Concurrent)
add_test(NAME shadergraph.pbr_evaluator COMMAND test_pbr_evaluator WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadergraph.pbr_evaluator PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "shadergraph")

# Node-library regressions (2026-08-31 audit): Truncate-poisoned saves load,
# bare PropertyNode doesn't crash, vector constants keep fractions, drawer
# names are node titles. Same slice as test_pbr_evaluator plus the full
# LibraryV1 registry and every node family it constructs.
add_executable(test_node_regressions
    test_node_regressions.cpp
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/bakeprogram.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/graphbaker.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/pbrgraphevaluator.cpp
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
target_link_libraries(test_node_regressions PRIVATE IrisGL Qt6::Widgets Qt6::Sql Qt6::Concurrent)
add_test(NAME shadergraph.node_regressions COMMAND test_node_regressions WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadergraph.node_regressions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "shadergraph")

# Materials Evaluator parity suite (MATERIALS_EVALUATOR_SPEC section 6): the
# audit's probe table as fixtures, one hand-computed case per op node,
# graph.bakeInfo classifier. Same slice as test_node_regressions.
add_executable(test_baker_parity
    test_baker_parity.cpp
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/bakeprogram.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/graphbaker.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/pbrgraphevaluator.cpp
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
set_target_properties(test_baker_parity PROPERTIES
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(test_baker_parity PRIVATE EFFECT_BUILD_AS_LIB)
target_include_directories(test_baker_parity PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_baker_parity PRIVATE IrisGL Qt6::Widgets Qt6::Sql Qt6::Concurrent)
add_test(NAME shadergraph.baker_parity COMMAND test_baker_parity WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadergraph.baker_parity PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "shadergraph")

# Per-texel baker pixel suite (MATERIALS_EVALUATOR_SPEC section 6):
# gradient/checker/tint/alpha-packing/normal-passthrough/cache-hit probes
# over real PNG output, plus the section-2 perf budgets.
add_executable(test_bake_output
    test_bake_output.cpp
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/bakeprogram.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/graphbaker.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/pbrgraphevaluator.cpp
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
set_target_properties(test_bake_output PROPERTIES
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(test_bake_output PRIVATE EFFECT_BUILD_AS_LIB)
target_include_directories(test_bake_output PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_bake_output PRIVATE IrisGL Qt6::Widgets Qt6::Sql Qt6::Concurrent)
add_test(NAME shadergraph.bake_output COMMAND test_bake_output WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadergraph.bake_output PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "shadergraph")

# Fuzzy matcher for the tab-search node palette: pure QString logic.
add_executable(test_fuzzy_search test_fuzzy_search.cpp)
target_include_directories(test_fuzzy_search PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(test_fuzzy_search PROPERTIES
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_fuzzy_search PRIVATE Qt6::Core)
add_test(NAME shadergraph.fuzzy_search COMMAND test_fuzzy_search WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadergraph.fuzzy_search PROPERTIES TIMEOUT 60 LABELS "shadergraph")
# §3b migration (MATERIALS_EVALUATOR_SPEC §6): old-format graphs — properties
# + PropertyNode instances — load as real constant/texture nodes; re-saves
# stop writing "properties"; the shipped presets round-trip. Same slice as
# test_node_regressions plus fixture dirs.
add_executable(test_panel_migration
    test_panel_migration.cpp
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/pbrgraphevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/bakeprogram.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/graphbaker.cpp
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
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/bakeprogram.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/graphbaker.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/undoredo.cpp
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

# graph.settings/setBlendMode verbs end-to-end (Unreal-parity blend modes,
# IMAGE_PLANE_SPEC §9): master-panel blend state round-trips through the verbs
# and lands on the evaluated material's alphaMode.
configure_file(e2e_blend_settings.js.in ${CMAKE_CURRENT_BINARY_DIR}/e2e_blend_settings.js @ONLY)
set(_home_blend ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-blend)
file(MAKE_DIRECTORY ${_home_blend}/run)
add_test(NAME shadergraph.blend_verbs
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_blend_settings.js --headless
         WORKING_DIRECTORY ${_home_blend}/run)
set_tests_properties(shadergraph.blend_verbs PROPERTIES
    ENVIRONMENT "HOME=${_home_blend}"
    LABELS "shadergraph"
    TIMEOUT 120)

# One-shot §3b tool: re-saves a shipped preset through the real migration
# (NOT a test — invoked by hand; the re-saved presets are committed).
add_executable(resave_presets
    resave_presets.cpp
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/pbrgraphevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/bakeprogram.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/graphbaker.cpp
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



# The leak lane (scripts/sanitize.sh lsan): the CPU evaluator allocates a
# BakeProgram, per-texel buffers and a QImage per baked map — all pure document
# work, no driver anywhere near it.
# The five suites that build a NodeLibrary are QUARANTINED (L4 — the node
# library/graph own raw pointers and nothing has a destructor; 88 KB in one
# run of baker_parity — plus L1 underneath).
jah_lsan_lane(shadergraph.fuzzy_search shadergraph.selection)
jah_lsan_blocked(shadergraph.pbr_evaluator shadergraph.node_regressions
                 shadergraph.baker_parity shadergraph.bake_output
                 shadergraph.panel_migration)
