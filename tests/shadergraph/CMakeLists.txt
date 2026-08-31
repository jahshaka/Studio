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
target_link_libraries(test_node_regressions PRIVATE IrisGL Qt6::Widgets Qt6::Sql Qt6::Concurrent)
add_test(NAME shadergraph.node_regressions COMMAND test_node_regressions WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadergraph.node_regressions PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "shadergraph")

# Materials Evaluator parity suite (MATERIALS_EVALUATOR_SPEC section 6): the
# audit's probe table as fixtures, one hand-computed case per op node,
# sockethelper coercion parity, fake-fragment-context approximations, and the
# graph.bakeInfo classifier. Same slice as test_node_regressions.
add_executable(test_baker_parity
    test_baker_parity.cpp
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/bakeprogram.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/graphbaker.cpp
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
