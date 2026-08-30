# The Effects/Materials Display preview on the engine: EngineMaterialPreviewScene
# — its own engine Scene + SceneMirror + the primitive/lights/camera document —
# driven headless into an offscreen View. The class is widget-free, so nothing
# of Studio beyond the mirror links in. Output goes to bin/ so app/shadergraph
# (relative to the executable) resolves the legacy primitive meshes.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_engine_material_preview test_engine_material_preview.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/enginematerialpreviewscene.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/scenemirror.cpp)
set_target_properties(test_engine_material_preview PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_engine_material_preview PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl/include ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_engine_material_preview PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_material_preview PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_engine_material_preview JahshakaEngineMedia)
add_test(NAME materialpreview.engine COMMAND ${CMAKE_BINARY_DIR}/bin/test_engine_material_preview WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(materialpreview.engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "mirror")
