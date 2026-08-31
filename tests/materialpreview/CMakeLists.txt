# The Effects/Materials Display preview on the engine: EngineMaterialPreviewScene
# — its own engine Scene + SceneMirror + the primitive/lights/camera document —
# driven headless into an offscreen View. Since the Materials Evaluator program
# the test also carries the graph/baker slice: a node graph is compiled and
# baked to a PNG map by GraphBaker and that baked-map material must reach
# pixels in the preview (widget-backed nodes need Widgets + AUTOMOC).
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL Widgets Sql Concurrent)
add_executable(test_engine_material_preview test_engine_material_preview.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/enginematerialpreviewscene.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp
    ${CMAKE_SOURCE_DIR}/tests/shadergraph/test_stubs.cpp
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
set_target_properties(test_engine_material_preview PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_engine_material_preview PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_engine_material_preview PRIVATE EFFECT_BUILD_AS_LIB
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_material_preview PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL
    Qt6::Widgets Qt6::Sql Qt6::Concurrent)
add_dependencies(test_engine_material_preview JahshakaEngineMedia)
add_test(NAME materialpreview.engine COMMAND ${CMAKE_BINARY_DIR}/bin/test_engine_material_preview WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(materialpreview.engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "mirror")
