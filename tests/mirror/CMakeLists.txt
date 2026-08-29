# SceneMirror tests: iris:: document -> engine, verified by pixels. Links IrisGL
# (Qt) AND JahshakaEngine — the only suite that spans the boundary, on purpose.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_scene_mirror test_scene_mirror.cpp ${CMAKE_SOURCE_DIR}/src/engine/scenemirror.cpp)
set_target_properties(test_scene_mirror PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_scene_mirror PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl/include ${CMAKE_SOURCE_DIR}/irisgl/src
    ${CMAKE_SOURCE_DIR}/irisgl/src/assimp/include ${CMAKE_BINARY_DIR}/irisgl/src/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/src/bullet3/src)
target_compile_definitions(test_scene_mirror PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_scene_mirror PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_scene_mirror JahshakaEngineMedia)
add_test(NAME mirror.document_to_engine COMMAND test_scene_mirror WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.document_to_engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "mirror")
