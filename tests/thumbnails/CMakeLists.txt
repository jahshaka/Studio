# Thumbnails through the engine (plan step 9): EngineThumbnailRenderer renders a
# document node into an offscreen View on the main thread. Output goes to bin/ so
# app/content/primitives/sphere.obj (relative to the executable) resolves.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_thumbnails test_thumbnails.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/enginethumbnailrenderer.cpp ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_thumbnails PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_thumbnails PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_thumbnails PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_thumbnails PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_thumbnails JahshakaEngineMedia)
add_test(NAME thumbnails.engine COMMAND ${CMAKE_BINARY_DIR}/bin/test_thumbnails WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(thumbnails.engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "mirror")

# Shader-asset thumbnails (VISUAL_PARITY_SPEC item 5): MaterialReader::
# parseShaderAsPbr turns a stored graph definition into the evaluator's
# PbrMaterial and EngineThumbnailRenderer renders it on the preview sphere.
# Links the REAL Database (throwaway SQLite file) plus materialreader.cpp's
# link closure — the materials evaluator slice (same set the materialpreview
# suite compiles) with tests/shadergraph/test_stubs.cpp standing in for
# texturemanager.cpp. Binary in bin/ so app/content/primitives/sphere.obj
# resolves next to the executable; its OWN working directory so the throwaway
# database and the ogre log never land in the shared bin/.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL Widgets Sql Multimedia Concurrent)
add_executable(test_shader_thumbnail test_shader_thumbnail.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/enginethumbnailrenderer.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp
    ${CMAKE_SOURCE_DIR}/src/io/materialreader.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetmetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/services/iesprofile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/services/thumbnailmanager.cpp
    ${CMAKE_SOURCE_DIR}/tests/shadergraph/test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/modules/materials/core/materialhelper.cpp
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
set_target_properties(test_shader_thumbnail PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_shader_thumbnail PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/modules/materials
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_shader_thumbnail PRIVATE EFFECT_BUILD_AS_LIB
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_shader_thumbnail PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL
    Qt6::Widgets Qt6::Sql Qt6::Multimedia Qt6::Concurrent)
add_dependencies(test_shader_thumbnail JahshakaEngineMedia)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/run)
add_test(NAME thumbnails.shader COMMAND ${CMAKE_BINARY_DIR}/bin/test_shader_thumbnail
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/run)
set_tests_properties(thumbnails.shader PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "mirror")
