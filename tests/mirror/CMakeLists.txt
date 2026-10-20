# FRAMES (TESTING-SPEED-B1, 2026-10-12): every row here states JAHSHAKA_TEST_FRAMES — the frames its process(es) render,
# COUNTED on the Debug tree (an LD_PRELOAD count of Ogre::Root::_updateAllRenderTargets per row; an _asan twin = its
# twin's count; 0 = no engine frame): spikes/testing-speed-b1/frames.json, framecount/.
# SceneMirror tests: iris:: document -> engine, verified by pixels. Links IrisGL
# (Qt) AND JahshakaEngine — the only suite that spans the boundary, on purpose.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_scene_mirror test_scene_mirror.cpp ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
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
jah_gpu_row(mirror.document_to_engine SHIP CLASS engine COMMAND test_scene_mirror WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.document_to_engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "mirror")
set_tests_properties(mirror.document_to_engine PROPERTIES JAHSHAKA_TEST_FRAMES 856)

   # EnginePrivate.h: the engine's own node for a NodeId
   # OgreNext: the engine's cached derived transform, read after a frame

   # the mirror syncs; the engine renders nothing (metered 2026-10-18)

# mirror.vr_proxies — THE WEARER, DRAWN AT THE DESK AND IN EVERY EYE (VR_SPEC
# §5 phase 4, lane VR-4). The two HAND markers the editor's VR preview shows
# (there is no head marker, by the owner's rule), the two-bit helper rule that
# decides which picture draws which furniture, and the proof that a helper-less
# picture — the Player's desktop window, a thumbnail, a user's screenshot — is
# byte-identical to one with no session at all.
#
# NEEDS NO RUNTIME: setVrProxies takes a VrStatus as data, so the suite hands it
# three poses and never asks OpenXR anything. The runtime half is vr.session.
# THE APP'S OWN MODEL RESOURCES, compiled into this suite (claim H, phase 4b
# stage 1): the mirror's default controller-model path is a qrc path
# (":/content/vr/meta-quest-touch-pro/left.obj"), so the suite that asserts the
# model is drawn must carry the same resource the app ships or it would be
# testing a path the product does not use.
qt6_add_resources(_vr_proxies_qrc ${CMAKE_SOURCE_DIR}/app/models.qrc)
