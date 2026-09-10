# SceneMirror tests: iris:: document -> engine, verified by pixels. Links IrisGL
# (Qt) AND JahshakaEngine — the only suite that spans the boundary, on purpose.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
# builtinmaterials.cpp is the ONE Studio source this suite compiles, and it is
# deliberately self-contained (no Constants, no Database): the legacy-material
# conversion is what a reopened old scene depends on, so the mirror suite drives
# it directly rather than asserting on its output second-hand.
add_executable(test_scene_mirror test_scene_mirror.cpp ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp
    ${CMAKE_SOURCE_DIR}/src/io/builtinmaterials.cpp)
set_target_properties(test_scene_mirror PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_scene_mirror PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_scene_mirror PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_scene_mirror PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_scene_mirror JahshakaEngineMedia)
add_test(NAME mirror.document_to_engine COMMAND test_scene_mirror WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.document_to_engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "mirror")

# mirror.skinned_outline — the SELECTION SILHOUETTE over a skinned character.
# The shell is a second Item over the same mesh; on HlmsUnlit (which has no
# skeletal path) it drew the BIND POSE forever, so an animating character grew a
# solid selection-coloured twin that read as "the character renders twice".
# Pixel gate: with the character bent 90 degrees, the space its bind pose
# occupied is empty. Plus one-item-per-node, stopped and playing.
add_executable(test_skinned_outline test_skinned_outline.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_skinned_outline PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_skinned_outline PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_skinned_outline PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_skinned_outline PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_skinned_outline JahshakaEngineMedia)
add_test(NAME mirror.skinned_outline COMMAND test_skinned_outline WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.skinned_outline PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "mirror")
