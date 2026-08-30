# Thumbnails through the engine (plan step 9): EngineThumbnailRenderer renders a
# document node into an offscreen View on the main thread. Output goes to bin/ so
# app/content/primitives/sphere.obj (relative to the executable) resolves.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_thumbnails test_thumbnails.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/enginethumbnailrenderer.cpp ${CMAKE_SOURCE_DIR}/src/engine/scenemirror.cpp)
set_target_properties(test_thumbnails PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_thumbnails PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl/include ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_thumbnails PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_thumbnails PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_thumbnails JahshakaEngineMedia)
add_test(NAME thumbnails.engine COMMAND ${CMAKE_BINARY_DIR}/bin/test_thumbnails WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(thumbnails.engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "mirror")
