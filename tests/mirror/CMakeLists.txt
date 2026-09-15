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
