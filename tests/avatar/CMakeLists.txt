# Avatar module, Part 0 (AVATAR_MODULE_SPEC).
#
# avatar.document — ITEM ZERO (a skinned SINGLE-MESH file must get bone scene
#   nodes and a moving pose; pre-fix that was 0 of N bones, silently) plus the
#   engine-free AvatarPreviewModel the `avatar` verbs drive: clips and their
#   display names, transport, the two toggles, the bone tree. No engine at all.
# avatar.preview — the same model through AvatarPreviewScene + BoneOverlay into
#   an offscreen View: the four (mesh, skeleton) combinations, pixel-asserted.
#
# The rigged fixture is GENERATED, not downloaded: fixtures/make_rig_glb.py
# writes a ~3 KB two-bone skinned GLB with two named clips, one of them called
# "mixamo.com" so the display-name rule is exercised by a real file. Same for
# the MOCAP fixtures: fixtures/make_bvh_fixtures.py writes two plain-ASCII
# .bvh files (one on rig2.glb's joint names, one on a foreign rig) for the
# avatar.document B-sections and avatar.preview's S10 — the .bvh importer only
# exists because irisgl/CMakeLists.txt names ASSIMP_BUILD_BVH_IMPORTER in the
# assimp allowlist, and those sections are what keeps that option honest.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL Widgets Sql Multimedia)

# assethelper.cpp's link closure — the module's load route (D0.1 A) goes
# through it. Same set tests/assetmeta links.
set(AVATAR_LOAD_SOURCES
    ${CMAKE_SOURCE_DIR}/src/modules/avatar/avatarpreviewmodel.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetmetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp)

add_executable(test_avatar_document test_avatar_document.cpp ${AVATAR_LOAD_SOURCES})
set_target_properties(test_avatar_document PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_avatar_document PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_avatar_document PRIVATE JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_avatar_document PRIVATE IrisGL Qt6::Gui Qt6::Widgets Qt6::Sql Qt6::Multimedia)
add_test(NAME avatar.document COMMAND test_avatar_document WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(avatar.document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300 LABELS "avatar")

add_executable(test_avatar_preview test_avatar_preview.cpp ${AVATAR_LOAD_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/bridge/avatarpreviewscene.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/boneoverlay.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_avatar_preview PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_avatar_preview PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_avatar_preview PRIVATE JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_avatar_preview PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL
    Qt6::Widgets Qt6::Sql Qt6::Multimedia)
add_dependencies(test_avatar_preview JahshakaEngineMedia)
add_test(NAME avatar.preview COMMAND test_avatar_preview WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(avatar.preview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "avatar")
