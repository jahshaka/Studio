# Sockets (CAMERAS_SPEC §5 / phase 4 — owner decision D9).
#
# sockets.document  the socket maths against hand-computed matrices, the
#                   bind-pose fallback (no engine at all), every refusal, the
#                   stale cases, duplication, the scene's attachment
#                   registry, and the avatar module's bone-name table. Pure
#                   iris:: + avatarsockets.cpp, offscreen.
# sockets.render    the same thing through the WHOLE chain — engine pose ->
#                   mirror read-back -> resolver -> applyCamera -> pixels. A
#                   camera on an animated bone must photograph a different shot
#                   as the rig moves, and a detached one must not.
# sockets.e2e       the verbs in the real binary: node.addSocket / sockets /
#                   attachToSocket / detachFromSocket / removeSocket,
#                   avatar.addSockets, camera.screenshot, the refusals, undo,
#                   and the save/close/open round trip through the real writer
#                   and reader.

find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)

# ---- sockets.e2e ----------------------------------------------------------
# The real app binary, --script, with HOME redirected to a scratch dir so the
# run gets a fresh library and can never touch the user's.
set(_sockets_home ${CMAKE_CURRENT_BINARY_DIR}/sockets-home)
file(MAKE_DIRECTORY ${_sockets_home}/run)
# The script is CONFIGURED, not copied: a --script run has no way to read the
# environment, and the rigged fixture lives in the source tree, which a build
# tree has no fixed relation to.
set(JAHSHAKA_SOCKETS_RIG "${CMAKE_SOURCE_DIR}/tests/avatar/fixtures/rig2.glb")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_sockets.js.in
               ${CMAKE_CURRENT_BINARY_DIR}/e2e_sockets.js @ONLY)
jah_gpu_row(sockets.e2e SHIP CLASS app TIER low NEEDS NONE NEEDS_WHY "socket verbs, camera.screenshot returns a shot (no picture claim)"
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/e2e_sockets.js
         WORKING_DIRECTORY ${_sockets_home}/run)
set_tests_properties(sockets.e2e PROPERTIES
    ENVIRONMENT "HOME=${_sockets_home};JAHSHAKA_DATA_ROOT=${_sockets_home}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 300)

# FRESH HOMES (PROJECT-NAMES-1). A project name is unique in a library now (project.create
# refuses a taken one), so a row that creates a project in a home nothing wipes went red on its
# SECOND run in the same tree (an rc tree's solo retries, build-linux, a stage close) — and every
# such home grew without bound (ui.column_law's held 66 'ColumnLaw' projects). Each row below
# creates projects; its home is wiped at the start of every run (WARM: the shader caches stay).
if(TEST sockets.e2e)
    jah_fresh_home_fixture(sockets.e2e.fresh_home socketsE2eFreshHome "${CMAKE_CURRENT_BINARY_DIR}/sockets-home" WARM LABELS "scripting")
    set_property(TEST sockets.e2e APPEND PROPERTY FIXTURES_REQUIRED socketsE2eFreshHome)
endif()

# THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B2; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, MEASURED — every engine frame (Engine::renderOneFrame) of every process the row starts,
# the boot's startup-gate frames included, counted on this tree's Debug build on 2026-10-12 (the count and its
# method: spikes/testing-speed-b2/frames_pass1.tsv). A pool's is the sum over its arms. A headless row renders
# the one empty frame a headless engine walks.
if(TEST sockets.e2e)
    set_property(TEST sockets.e2e PROPERTY JAHSHAKA_TEST_FRAMES 509)
endif()
