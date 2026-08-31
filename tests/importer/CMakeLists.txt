# GLB importer regression suite (importer fix phases 0-2): embedded-texture
# extension correctness + real-PbrMaterial import through the REAL path
# (AssetHelper::extractTexturesAndMaterialFromMesh), tangent pass-through,
# tick->second animation extraction (SKELETAL_PLAYBACK_SPEC S1/S2/S5),
# relative skeletal sources, engine tolerant texture reads, and double-import
# transform stability. Engine sections need a reachable DISPLAY (Vulkan),
# platform stays offscreen.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia OpenGL)
add_executable(test_importer_glb test_importer_glb.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetmetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    # database.cpp's link closure (same as tests/assetmeta)
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    # transform-panel echo regression (the double-import scale corruption)
    ${CMAKE_SOURCE_DIR}/src/ui/panels/transformeditor.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/dragspinbox.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/transformscenenodecommand.cpp
    ${CMAKE_SOURCE_DIR}/src/services/undoservice.cpp)
set_target_properties(test_importer_glb PROPERTIES AUTOMOC ON CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_importer_glb PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_importer_glb PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_importer_glb PRIVATE IrisGL JahshakaEngine
    Qt6::Widgets Qt6::Sql Qt6::Multimedia Qt6::OpenGL)
add_dependencies(test_importer_glb JahshakaEngineMedia)
add_test(NAME importer.glb COMMAND test_importer_glb
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(importer.glb PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 600 LABELS "importer")
