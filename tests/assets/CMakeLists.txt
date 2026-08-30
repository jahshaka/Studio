# The Assets page viewer on the engine: EngineAssetScene — the third engine
# Scene + SceneMirror + the preview document and orbit camera — driven headless
# into an offscreen View. The class is database-free, so nothing of Studio
# beyond the mirror links in. Output goes to bin/ so app/content/primitives
# (relative to the executable) resolves.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_engine_asset_viewer test_engine_asset_viewer.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/engineassetscene.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/scenemirror.cpp)
set_target_properties(test_engine_asset_viewer PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_engine_asset_viewer PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl/include ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_engine_asset_viewer PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_asset_viewer PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_engine_asset_viewer JahshakaEngineMedia)
add_test(NAME assets.engine COMMAND ${CMAKE_BINARY_DIR}/bin/test_engine_asset_viewer WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(assets.engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "mirror")
