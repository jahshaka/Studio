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

# ---- cameras.document -----------------------------------------------------
# Compiles the export walker beside the test: classifyNode is one of the sweep
# sites the type-enum fix re-routes, and it is pure document logic.
add_executable(test_camera_document test_camera_document.cpp
    ${CMAKE_SOURCE_DIR}/src/export/walkers/scenewalker.cpp
    # scenewalker's inventory walk reads material texture slots.
    ${CMAKE_SOURCE_DIR}/src/export/walkers/materialtexturereader.cpp)
set_target_properties(test_camera_document PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17)
target_include_directories(test_camera_document PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_camera_document PRIVATE IrisGL Qt6::Gui Qt6::OpenGL)
# This suite builds a DOCUMENT, and a document node is an engine node now
# (SPECS/SCENEGRAPH_SPEC.md D2), so it boots the HEADLESS engine — NULL render
# system, no display, no GPU (tests/support/documentgraph.h). jah_no_display
# below runs it with DISPLAY unset, which is the proof of that claim.
jah_document_graph(test_camera_document)
add_test(NAME cameras.document COMMAND test_camera_document
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(cameras.document PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "document")
jah_no_display(cameras.document)

# ---- cameras.lens ---------------------------------------------------------
# THE LENS MATH (CAMERA_LENS_SPEC §3, P1/P2) against hand-computed values:
# filmback fits, the anamorphic squeeze, the lens-shift conversions (including
# through Ogre's near/focal scaling) and the depth-of-field optics. Pure iris::,
# no engine, no display — the golden table is the point, so nothing that could
# vary is allowed anywhere near it.
add_executable(test_camera_lens test_camera_lens.cpp)
set_target_properties(test_camera_lens PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17)
target_include_directories(test_camera_lens PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_camera_lens PRIVATE IrisGL Qt6::Gui Qt6::OpenGL)
# A document node is an engine node (SPECS/SCENEGRAPH_SPEC.md D2), so even this
# arithmetic suite boots the headless engine to make a CameraNode.
jah_document_graph(test_camera_lens)
add_test(NAME cameras.lens COMMAND test_camera_lens
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(cameras.lens PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "document")
jah_no_display(cameras.lens)

# ---- cameras.play_routing -------------------------------------------------
add_executable(test_camera_play_routing test_camera_play_routing.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_camera_play_routing PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_camera_play_routing PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_camera_play_routing PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_camera_play_routing PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_camera_play_routing JahshakaEngineMedia)
add_test(NAME cameras.play_routing COMMAND test_camera_play_routing
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(cameras.play_routing PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600 LABELS "mirror")

# ---- cameras.body ---------------------------------------------------------
# The camera BODY + FRUSTUM wires (phase 2b) and the PiP inset (phase 2c), in
# pixels: real backend, real frames, byte-exact off switches.
add_executable(test_camera_body test_camera_body.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_camera_body PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_camera_body PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_camera_body PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_camera_body PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_camera_body JahshakaEngineMedia)
add_test(NAME cameras.body COMMAND test_camera_body
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(cameras.body PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600 LABELS "mirror")

# ---- cameras.pip ----------------------------------------------------------
# THE PiP TONEMAP (CAMERAS_SPEC §7.2 Route C, POST_CHAIN_SPEC §14's fourth
# consumer): the inset renders into its own RGBA16F target and goes through the
# SAME tonemapper the main chain uses, so a graded world is graded in the inset
# and a PIPPED CAMERA'S OWN exposure appears in its preview and nowhere else.
# Real backend, real frames, through the host route (SceneMirror::applyPip).
add_executable(test_camera_pip test_camera_pip.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_camera_pip PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_camera_pip PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_camera_pip PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_camera_pip PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_camera_pip JahshakaEngineMedia)
add_test(NAME cameras.pip COMMAND test_camera_pip
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(cameras.pip PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 600 LABELS "mirror")

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
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_camera_exposure PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_camera_exposure PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_camera_exposure JahshakaEngineMedia)
add_test(NAME cameras.exposure COMMAND test_camera_exposure
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(cameras.exposure PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 900 LABELS "mirror")

# ---- cameras.e2e ----------------------------------------------------------
# The real app binary, --script, with HOME redirected to a scratch dir so the
# run gets a fresh library and can never touch the user's (the same contract
# every scripting.e2e.* suite has).
set(_home ${CMAKE_CURRENT_BINARY_DIR}/cameras-home)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME cameras.e2e
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_cameras.js
         WORKING_DIRECTORY ${_home}/run)
# TIMEOUT, not RESOURCE_LOCK — recorded decision (N7, ENGINEERING_DEBT_SPEC
# re-audit). The observed failure is a TIMEOUT under GI-rebuild contention: the
# engine compiles shaders and PSOs on a parallel pool
# (OGRE_SHADER_COMPILATION_THREADING_MODE=2) and a concurrent gi.* suite
# voxelizing a scene starves it, so this app-spawning script run takes minutes
# it normally does not (JOURNAL 2026-09-08). It is not a wrong answer, it is a
# slow one.
#
# A RESOURCE_LOCK would express the contention directly, but a lock is only
# real when EVERY participant carries the same name — the lock would have to be
# added to tests/gi/CMakeLists.txt too, and it would serialize this suite
# against the longest-running family in the tree for a hazard that costs
# nothing but wall clock. The cheap, honest fix is to let the slow case finish:
# 900 s is what cameras.exposure (the same engine, the same compile pool)
# already allows. If cameras.e2e ever fails with an ASSERTION under contention
# rather than a timeout, that reasoning is void and the lock is the answer.
set_tests_properties(cameras.e2e PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 900)

# ---- cameras.e2e.lens -----------------------------------------------------
# CAMERA_LENS_SPEC §3 (P1 + P2) through the verbs in the real binary: the preset
# tables, camera.filmback / camera.lens / camera.focusInfo, the new
# camera.settings keys, focus TRACKING resolved on a real synced frame, and the
# save/close/open round trip of every new field through the real writer and
# reader. Same scratch-HOME contract as cameras.e2e.
set(_lens_home ${CMAKE_CURRENT_BINARY_DIR}/lens-home)
file(MAKE_DIRECTORY ${_lens_home}/run)
add_test(NAME cameras.e2e.lens
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_camera_lens.js
         WORKING_DIRECTORY ${_lens_home}/run)
set_tests_properties(cameras.e2e.lens PROPERTIES
    ENVIRONMENT "HOME=${_lens_home};JAHSHAKA_DATA_ROOT=${_lens_home}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 300)

# ---- cameras.e2e.postfx ---------------------------------------------------
# CAMERA_LENS_SPEC §4 + §5 through the verbs in the real binary: the exposure
# block as camera.settings keys, camera.postFx's tri-state overrides and its
# refusals, camera.clearPostOverride, the reflected (keyframeable) half, and the
# save/close/open round trip through the real writer and reader. Same
# scratch-HOME contract as cameras.e2e.
set(_post_home ${CMAKE_CURRENT_BINARY_DIR}/postfx-home)
file(MAKE_DIRECTORY ${_post_home}/run)
add_test(NAME cameras.e2e.postfx
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_camera_postfx.js
         WORKING_DIRECTORY ${_post_home}/run)
set_tests_properties(cameras.e2e.postfx PROPERTIES
    ENVIRONMENT "HOME=${_post_home};JAHSHAKA_DATA_ROOT=${_post_home}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 300)

# ---- cameras.e2e.pilot ----------------------------------------------------
# Phase 3 (D4/D8) and the phase-2c preview preference, through the verbs, in
# the real binary. Same scratch-HOME contract as cameras.e2e.
set(_pilot_home ${CMAKE_CURRENT_BINARY_DIR}/pilot-home)
file(MAKE_DIRECTORY ${_pilot_home}/run)
add_test(NAME cameras.e2e.pilot
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_camera_pilot.js
         WORKING_DIRECTORY ${_pilot_home}/run)
set_tests_properties(cameras.e2e.pilot PROPERTIES
    ENVIRONMENT "HOME=${_pilot_home};JAHSHAKA_DATA_ROOT=${_pilot_home}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 300)

# ---- cameras.e2e.axis_lock ------------------------------------------------
# THE AXIS-VIEW ROTATION LOCK (owner report 2026-09-08) through the verbs, in
# the real binary: editor.camera().rotationLocked following editor.setView for
# all six axis views, the orthographic projection and the view-plane grid
# (editor.overlays().gridPlane) surviving a pan, the perspective round trip
# restoring the remembered pose, BOTH camera controllers armed, the pilot
# exception, and F-focus centring instead of turning. The GESTURES themselves
# are pinned by input.axis_view_lock, which drives the controllers directly.
# Same scratch-HOME contract as cameras.e2e.
set(_axis_home ${CMAKE_CURRENT_BINARY_DIR}/axislock-home)
file(MAKE_DIRECTORY ${_axis_home}/run)
add_test(NAME cameras.e2e.axis_lock
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_axis_view_lock.js
         WORKING_DIRECTORY ${_axis_home}/run)
set_tests_properties(cameras.e2e.axis_lock PROPERTIES
    ENVIRONMENT "HOME=${_axis_home};JAHSHAKA_DATA_ROOT=${_axis_home}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 300)
