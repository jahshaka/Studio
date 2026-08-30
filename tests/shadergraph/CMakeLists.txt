# PbrGraphEvaluator tests: shader graph -> iris::PbrMaterial on the CPU.
# No GL, no engine. Compiles the graph-model slice of src/shadergraph directly
# (the module is not a library); texturemanager.cpp is replaced by test_stubs.cpp
# because the real one links the Studio database.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)
add_executable(test_pbr_evaluator
    test_pbr_evaluator.cpp
    test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/shadergraph/core/pbrgraphevaluator.cpp
    ${CMAKE_SOURCE_DIR}/src/shadergraph/core/sockethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/shadergraph/generator/shadercontext.cpp
    ${CMAKE_SOURCE_DIR}/src/shadergraph/graph/nodegraph.cpp
    ${CMAKE_SOURCE_DIR}/src/shadergraph/models/connectionmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/shadergraph/models/library.cpp
    ${CMAKE_SOURCE_DIR}/src/shadergraph/models/nodemodel.cpp
    ${CMAKE_SOURCE_DIR}/src/shadergraph/models/properties.cpp
    ${CMAKE_SOURCE_DIR}/src/shadergraph/models/socketmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/shadergraph/nodes/pbrmasternode.cpp
    ${CMAKE_SOURCE_DIR}/src/shadergraph/nodes/test.cpp
    ${CMAKE_SOURCE_DIR}/src/misc/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/colorpickerwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/colorview.cpp)
set_target_properties(test_pbr_evaluator PROPERTIES
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(test_pbr_evaluator PRIVATE EFFECT_BUILD_AS_LIB
    JAHSHAKA_TEST_APP_DIR="${CMAKE_SOURCE_DIR}/app/shadergraph/")
target_include_directories(test_pbr_evaluator PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl/include ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_pbr_evaluator PRIVATE IrisGL Qt6::Widgets Qt6::Sql)
add_test(NAME shadergraph.pbr_evaluator COMMAND test_pbr_evaluator WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadergraph.pbr_evaluator PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "shadergraph")

# Fuzzy matcher for the tab-search node palette: pure QString logic.
add_executable(test_fuzzy_search test_fuzzy_search.cpp)
set_target_properties(test_fuzzy_search PROPERTIES
    CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_fuzzy_search PRIVATE Qt6::Core)
add_test(NAME shadergraph.fuzzy_search COMMAND test_fuzzy_search WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(shadergraph.fuzzy_search PROPERTIES TIMEOUT 60 LABELS "shadergraph")
