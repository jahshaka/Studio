# Scene cameras (CAMERAS_SPEC phase 1 — document + verbs).
#
# cameras.document      the document half: the type-enum fix and its sweep
#                       points, the angle <-> focal-length binding in both
#                       directions, reflection of every setting, duplication,
#                       and the scene camera registry + active camera. Pure
#                       iris:: — no engine, no app, offscreen.
# cameras.play_routing  the ONE behavioural change phase 1 makes to rendering:
#                       SceneMirror::applyCamera renders through the active
#                       camera while PLAYING and through the caller's camera
#                       while editing. Real engine, real frames, pixels are the
#                       assertion. Spans the boundary like tests/mirror does.
# cameras.e2e           the real binary under --script: every verb, and the
#                       save/close/open round trip of every camera field
#                       through the real writer and the real reader.

find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)

# ---- cameras.exposure -----------------------------------------------------
# CAMERA_LENS_SPEC §4/§5 (P3 + P4): the per-VIEW post-chain globals listener and
# everything that rides it. Real engine, real frames. The headline is two views
# rendering two different exposures in the same frame — the mechanism the whole
# per-camera-look design rests on, and the two pre-existing defects it fixes
# (two on-screen views fighting over one exposure; SSAO marching one camera's
# projection in another's view). Spans the boundary like play_routing does, so
# it compiles the mirror beside itself.
add_executable(test_camera_exposure test_camera_exposure.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_camera_exposure PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_camera_exposure PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_camera_exposure PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_camera_exposure PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_camera_exposure JahshakaEngineMedia)
jah_gpu_row(cameras.exposure SHIP CLASS engine COMMAND test_camera_exposure
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(cameras.exposure PROPERTIES JAHSHAKA_TEST_FRAMES 1662)   # the frames it renders, counted 2026-10-12 (TESTING-SPEED-B4)
set_tests_properties(cameras.exposure PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 120 LABELS "mirror")

