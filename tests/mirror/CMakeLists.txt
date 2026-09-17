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

# mirror.scale — the SHAPE of a still frame's mirror cost at scale (MIRROR_SCALE
# lane, 2026-09-13). The render review measured `host.mirror` at 52 ms per STILL
# frame on an 8,404-node lattice; this suite is the regression gate, asserting
# ratios and allocation counts rather than milliseconds (a wall-clock budget on
# a shared box is a flake generator).
add_executable(test_mirror_scale test_mirror_scale.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_mirror_scale PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_mirror_scale PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_mirror_scale PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_mirror_scale PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_mirror_scale JahshakaEngineMedia)
add_test(NAME mirror.scale COMMAND test_mirror_scale WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# RUN_SERIAL: it is a TIMING suite. The ratios are load-tolerant by construction
# (every arm pays the same contention) but a suite sharing a core with four
# Vulkan gates measures the scheduler, not the walk.
set_tests_properties(mirror.scale PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300 RUN_SERIAL TRUE LABELS "mirror")

# mirror.dirty_equals_full — THE DIFFERENTIAL TEST of the dirty-set mirror
# (SPECS/DIRTY_SET_MIRROR_SPEC.md §7.1). For every class of mutation the
# document supports: make the change, run the DIRTY sync that ships, then run
# the WHOLE WALK behind it as an oracle and demand it pushes nothing. A single
# push is a change the document failed to report — the one way this design can
# fail, and the one no pixel test would catch.
add_executable(test_dirty_mirror test_dirty_mirror.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_dirty_mirror PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_dirty_mirror PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_dirty_mirror PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_dirty_mirror PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_dirty_mirror JahshakaEngineMedia)
add_test(NAME mirror.dirty_equals_full COMMAND test_dirty_mirror
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.dirty_equals_full PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180 LABELS "mirror")

# mirror.grid_floor — THE GROUND GRID ON TOP OF THE FLOOR (GIZMO-2 item 5, owner
# §370). The grid sits at exactly y=0 now and wins against the coplanar floor
# through its material's depth bias instead of being pushed underneath it; a box
# standing on the floor still covers it, and a view with the editor's furniture
# off draws none of it. Pixels, through the real mirror and the real engine.
add_executable(test_grid_floor test_grid_floor.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_grid_floor PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_grid_floor PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_grid_floor PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_grid_floor PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_grid_floor JahshakaEngineMedia)
add_test(NAME mirror.grid_floor COMMAND test_grid_floor WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.grid_floor PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "mirror")

# mirror.ground_horizon — A HIDE IS NOT A DELETE (DRAG-1; render audit IRISGL
# I-2). Hiding the default floor used to DETACH the 4 km horizon plane and
# release its material, so the ground went from infinite to its own 100 m square
# and showing it again had to rebuild the mesh and re-fit its UV map — the
# owner's "hiding the plane changed its size". The horizon follows visibility
# now; the DELETE branch still releases. Pixels plus the engine's object census.
add_executable(test_ground_horizon test_ground_horizon.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_ground_horizon PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_ground_horizon PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_ground_horizon PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_ground_horizon PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_ground_horizon JahshakaEngineMedia)
add_test(NAME mirror.ground_horizon COMMAND test_ground_horizon
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.ground_horizon PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "mirror")

# mirror.sun_night — WHEN THE SUN GOES OUT (SKY-SMALL / SKY-NIGHT-1). The disc
# and the sun's PSSM passes used to be dropped on a RELATIVE tint test, which
# with SKY-DENSITY-1's physical transmittance crossed at +3.6 degrees of sun
# elevation at haze 6 and +6.3 at haze 10 — the disc popping off a visibly-up
# sun. The rule is absolute now (one 8-bit output code of radiance, derived in
# scenemirror.cpp); this is the sweep that pins it, per haze, as pixels.
add_executable(test_sun_night test_sun_night.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_sun_night PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_sun_night PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_sun_night PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_sun_night PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_sun_night JahshakaEngineMedia)
add_test(NAME mirror.sun_night COMMAND test_sun_night WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.sun_night PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 300 LABELS "mirror")

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
add_executable(test_vr_proxies test_vr_proxies.cpp ${_vr_proxies_qrc}
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_vr_proxies PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_vr_proxies PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_vr_proxies PRIVATE
    # The VR controller MODELS are read from the source tree (claim H): this
    # binary is not the app and carries none of the app's qrc resources.
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_vr_proxies PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL)
add_dependencies(test_vr_proxies JahshakaEngineMedia)
add_test(NAME mirror.vr_proxies COMMAND test_vr_proxies
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(mirror.vr_proxies PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 180 LABELS "mirror")
