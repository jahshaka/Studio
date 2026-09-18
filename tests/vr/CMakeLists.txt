# vr.spike_1a — VR phase 1a's spike, as a gate suite (SPECS/VR_SPEC.md §6).
#
# The SUBJECT is the external-device route: an OpenXR runtime creates the
# VkInstance and the VkDevice, Ogre renders on them, the frame reaches the
# runtime's swapchain. It is the only test in the tree that would catch a pin
# bump undoing ogre-patch 0068 (the validateDevice veto, the exported extension
# list + feature chain).
#
# It compiles `xr_spike.cpp` beside this file — ONE source, which `standalone/`
# also builds as a plain binary (that is how phase 1b points it at WiVRn and a
# real headset without building Studio). A second copy would rot; phase 2 replaces
# both with the engine's VrSession and its `vr.session` suite.
#
# EVERYTHING BELOW THE FIRST GUARD IS OPTIONAL. Without libopenxr-dev, without
# the Vulkan render system, or on macOS, none of the RUNTIME suites is built and
# none is registered; without monado-service on the box the registered ones SKIP
# (exit 77). A box with no VR stack is a supported box.
#
# `vr.grab_maths` (stage 1, immediately below) is the exception and is
# registered FIRST, before any guard: it is pure arithmetic with no runtime, no
# display and no GPU in it, so it runs everywhere — see its own note.

# ---------------------------------------------------------------------------
# vr.grab_maths — THE GRAB'S AND THE LOCOMOTION'S ARITHMETIC
# (SPECS/VR_INPUT_SPEC.md §5.1/§6, stage 1).
#
# REGISTERED BEFORE EVERY GUARD IN THIS FILE, deliberately: it needs no OpenXR,
# no Vulkan render system, no runtime, no display and no GPU — it is
# src/modules/vr/vrgrab.h and nothing else, compiled against IrisGL's maths. A
# box with no VR stack at all (and macOS, and a CI container) still runs it, and
# it is the only test in the tree that would catch a sign in the pose algebra
# before somebody found it wearing a headset. Same shape and same reasons as
# `player.vr` in tests/player, which asserts the rig's half.
add_executable(test_vr_grab "${CMAKE_CURRENT_SOURCE_DIR}/test_vr_grab.cpp")
set_target_properties(test_vr_grab PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(test_vr_grab PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    # ...AND THE ENGINE'S PUBLIC HEADERS (stage 3): the bare-hand rules a
    # session and a drawer BOTH apply — the profile test, the two press
    # hysteresis bands, the bone table and a bone's transform — live in
    # jahshaka/engine/Types.h, which is header-only maths with no Ogre, no
    # Qt and nothing to link. Asserting them here keeps this suite what it
    # is: the one VR test that runs on a box with no VR stack at all.
    ${CMAKE_SOURCE_DIR}/irisgl/engine/include)
target_link_libraries(test_vr_grab PRIVATE IrisGL Qt6::Gui)
add_test(NAME vr.grab_maths COMMAND ${CMAKE_BINARY_DIR}/bin/test_vr_grab
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(vr.grab_maths PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "services")
jah_no_display(vr.grab_maths)

if(NOT UNIX OR APPLE)
    return()
endif()

find_package(OpenXR QUIET)
find_package(Vulkan QUIET)
if(NOT OpenXR_FOUND OR NOT Vulkan_FOUND OR NOT OGRE_NEXT_VULKAN_RS_LIB)
    message(STATUS "Jahshaka: vr.spike_1a NOT built — openxr=${OpenXR_FOUND} "
                   "vulkan=${Vulkan_FOUND} vulkanRs='${OGRE_NEXT_VULKAN_RS_LIB}'")
    return()
endif()

set(JAH_VR_SPIKE_SRC "${CMAKE_CURRENT_SOURCE_DIR}/xr_spike.cpp")
if(NOT EXISTS "${JAH_VR_SPIKE_SRC}")
    message(STATUS "Jahshaka: vr.spike_1a NOT built — no ${JAH_VR_SPIKE_SRC}")
    return()
endif()

add_executable(test_vr_spike_1a "${JAH_VR_SPIKE_SRC}")
set_target_properties(test_vr_spike_1a PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_vr_spike_1a PRIVATE
    "${OGRE_NEXT_PREFIX}/include/OGRE-Next"
    "${OGRE_NEXT_PREFIX}/include/OGRE-Next/Hlms/Common"
    "${OGRE_NEXT_PREFIX}/include/OGRE-Next/Hlms/Pbs"
    "${OGRE_NEXT_PREFIX}/include/OGRE-Next/Hlms/Unlit"
    # Both Vulkan header roots — the install flattens the render system's headers
    # but they include their siblings as `Vao/...`, which only resolves under
    # .../include (the trap recorded in irisgl/engine/CMakeLists.txt).
    "${OGRE_NEXT_PREFIX}/include/OGRE-Next/RenderSystems/Vulkan"
    "${OGRE_NEXT_PREFIX}/include/OGRE-Next/RenderSystems/Vulkan/include"
    ${Vulkan_INCLUDE_DIRS})
target_link_directories(test_vr_spike_1a PRIVATE "${OGRE_NEXT_PREFIX}/lib")
target_link_libraries(test_vr_spike_1a PRIVATE
    OgreNextMain OgreNextHlmsPbs OgreNextHlmsUnlit
    "${OGRE_NEXT_VULKAN_RS_LIB}" ${Vulkan_LIBRARIES} OpenXR::openxr_loader)
target_compile_definitions(test_vr_spike_1a PRIVATE
    JAH_OGRE_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    # The TREE'S OWN staged media, so the suite fails when the media a patch
    # needs is missing rather than reading the engine source tree behind the
    # staging step's back.
    JAH_OGRE_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}")
add_dependencies(test_vr_spike_1a JahshakaEngineMedia)

# The manifest is named EXPLICITLY and the runtime's name is asserted inside the
# spike: the USER-level manifest (~/.config/openxr/1/) is WiVRn's and is
# rewritten whenever the owner plugs a headset in (VR_SPEC §2.6, §7 item 10).
set(JAH_MONADO_MANIFEST "/usr/share/openxr/1/openxr_monado.json"
    CACHE FILEPATH "OpenXR manifest of the simulated-HMD runtime the VR gate uses")

add_test(NAME vr.spike_1a
         COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/run_vr_spike.sh"
                 "$<TARGET_FILE:test_vr_spike_1a>" "${JAH_MONADO_MANIFEST}" 60
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# ---------------------------------------------------------------------------
# vr.session — PHASE 2's suite: the session inside the engine (VR_SPEC §6).
#
# Links JahshakaEngine, like every other engine suite, and nothing else: the
# OpenXR loader is linked INTO that library by the one TU that uses it
# (jahshaka_engine_vr), so this file does not name OpenXR at all.
add_executable(test_vr_session "${CMAKE_CURRENT_SOURCE_DIR}/test_vr_session.cpp")
set_target_properties(test_vr_session PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_link_libraries(test_vr_session PRIVATE JahshakaEngine)
target_compile_definitions(test_vr_session PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
add_dependencies(test_vr_session JahshakaEngineMedia)

add_test(NAME vr.session
         COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/run_vr_session.sh"
                 "$<TARGET_FILE:test_vr_session>" "${JAH_MONADO_MANIFEST}"
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(vr.session PROPERTIES
    LABELS "engine"
    TIMEOUT 600
    # A second Vulkan instance (the runtime's compositor) and one IPC socket:
    # never two of these at once.
    RUN_SERIAL TRUE
    SKIP_RETURN_CODE 77
    ENVIRONMENT "${JAH_TEST_DISPLAY_ENV}JAHSHAKA_DATA_ROOT=${CMAKE_CURRENT_BINARY_DIR}/vr-session-home")

# ---------------------------------------------------------------------------
# vr.verbs_session — the `vr.*` verbs through the APP, against the simulated
# runtime (VR_SPEC §6 / §4.6). The other half of scripting.e2e.vr_verbs, which
# proves the same verbs refuse cleanly with no runtime at all.
#
# It runs the real binary with `--vr --script`, inside the same monado wrapper
# the suites above use, with its own HOME and data root like every other
# app-spawning suite.
set(_vr_verbs_home "${CMAKE_CURRENT_BINARY_DIR}/vr-verbs-home")
# THE WORKING DIRECTORY IS THE SUITE'S OWN BUILD DIRECTORY, not a subdirectory
# of the scratch HOME: ctest chdir's into it BEFORE the command runs, so a run
# whose home was swept between a configure and a gate does not start at all
# ("Failed to change working directory ... Not Run", measured on this lane's own
# gate after a cleanup). The build directory always exists — the test binary is
# in it — and HOME still isolates the library, the settings and the data root.
add_test(NAME vr.verbs_session
         COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/run_vr_app.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${JAH_MONADO_MANIFEST}"
                 "${CMAKE_SOURCE_DIR}/tests/scripting/scripts/e2e_vr_session_verbs.js"
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(vr.verbs_session PROPERTIES
    LABELS "scripting"
    TIMEOUT 600
    RUN_SERIAL TRUE
    SKIP_RETURN_CODE 77
    ENVIRONMENT "${JAH_TEST_DISPLAY_ENV}HOME=${_vr_verbs_home};JAHSHAKA_DATA_ROOT=${_vr_verbs_home}/.local/share/Jahshaka")

# ---------------------------------------------------------------------------
# vr.player_session — THE PLAYER IN THE HEADSET (SPECS/VR_SPEC.md §4.5, phase 3).
#
# The same wrapper as vr.verbs_session, pointed at the Player's own script: the
# app with `--vr`, a real session, the Player page up, the mirror on the
# player's own view, the rig placed on the play camera and flown by the verb the
# fly keys call. The no-runtime half is `scripting.e2e.player_vr`; the rig's
# arithmetic is `player.vr`, which needs nothing at all.
set(_vr_player_home "${CMAKE_CURRENT_BINARY_DIR}/vr-player-home")
add_test(NAME vr.player_session
         COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/run_vr_app.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${JAH_MONADO_MANIFEST}"
                 "${CMAKE_SOURCE_DIR}/tests/scripting/scripts/e2e_player_vr_session.js"
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(vr.player_session PROPERTIES
    LABELS "scripting"
    # 600, like every other app-spawning suite: this one is PACED BY THE
    # RUNTIME (Monado's null compositor runs the second session of a process at
    # 20 Hz), so it is longer than a desktop suite — it measured 16 s — but it
    # is not in the class that needs 900.
    TIMEOUT 600
    RUN_SERIAL TRUE
    SKIP_RETURN_CODE 77
    ENVIRONMENT "${JAH_TEST_DISPLAY_ENV}HOME=${_vr_player_home};JAHSHAKA_DATA_ROOT=${_vr_player_home}/.local/share/Jahshaka")

# ---------------------------------------------------------------------------
# vr.no_picture_start — THE OWNER'S FAILED WiVRn SMOKE, AS A SUITE (lane VR-3b,
# 2026-09-17; VR_SPEC §6).
#
# The same wrapper and the same app as vr.player_session, with the pump's two
# test hooks armed: the first frames of every session in the run are answered
# "no picture" (which is what WiVRn answers while it synchronises, and what no
# simulated runtime will do on request), and after 200 accepted frames the
# runtime is asked to take the session away so the real STOPPING event arrives.
# Between them they reproduce both of the owner's failed runs — the frame loop
# that stopped because nothing was enabled, and the session that was stopped
# with nobody noticing — in the app, through the Player page and the real driver.
set(_vr_nopic_home "${CMAKE_CURRENT_BINARY_DIR}/vr-nopicture-home")
add_test(NAME vr.no_picture_start
         COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/run_vr_app.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${JAH_MONADO_MANIFEST}"
                 "${CMAKE_SOURCE_DIR}/tests/scripting/scripts/e2e_vr_no_picture_start.js")
set_tests_properties(vr.no_picture_start PROPERTIES
    LABELS "scripting"
    TIMEOUT 600
    RUN_SERIAL TRUE
    SKIP_RETURN_CODE 77
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    ENVIRONMENT "${JAH_TEST_DISPLAY_ENV}HOME=${_vr_nopic_home};JAHSHAKA_DATA_ROOT=${_vr_nopic_home}/.local/share/Jahshaka;JAHSHAKA_VR_TEST_NO_RENDER_FRAMES=40;JAHSHAKA_VR_TEST_STOP_AFTER_FRAMES=200")

# ---------------------------------------------------------------------------
# vr.input_session — LOCOMOTION AGAINST A REAL RUNTIME (stage 1,
# SPECS/VR_INPUT_SPEC.md §6/§10).
#
# The rig half of the VR interaction, and the one half that CANNOT be asserted
# without a session: Engine::setVrOrigin is a no-op without one, so with no
# runtime the thumbstick walks nobody (the headless suite asserts exactly that
# refusal). Here Monado's simulated headset gives the session a real rig, the
# left stick walks the wearer and the snap turn turns their room about their own
# head — measured, not assumed.
#
# The CONTROLLERS are injected (`vr.inject` + `vr.step`): Monado's simulated rig
# reports poses and no buttons and no stick at all (VR_INPUT_SPEC §17), so this
# is also the suite that proves the injection route composes with a live
# session — including the part that only a session has: an injected sample is
# read by the session INSIDE its frame, so the script renders one between
# writing a hand and stepping the interaction. JAHSHAKA_VR_TEST_INJECT is what
# lets it inject at all: the engine refuses to fake a hand whose runtime has a
# real interaction profile bound, so an injection can never fool a smoke on a
# real headset.
set(_vr_input_home "${CMAKE_CURRENT_BINARY_DIR}/vr-input-home")
add_test(NAME vr.input_session
         COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/run_vr_app.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${JAH_MONADO_MANIFEST}"
                 "${CMAKE_SOURCE_DIR}/tests/scripting/scripts/e2e_vr_input_session.js"
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(vr.input_session PROPERTIES
    LABELS "scripting"
    TIMEOUT 600
    RUN_SERIAL TRUE
    SKIP_RETURN_CODE 77
    ENVIRONMENT "${JAH_TEST_DISPLAY_ENV}HOME=${_vr_input_home};JAHSHAKA_DATA_ROOT=${_vr_input_home}/.local/share/Jahshaka;JAHSHAKA_VR_TEST_INJECT=1")

set_tests_properties(vr.spike_1a PROPERTIES
    LABELS "engine"
    TIMEOUT 600
    # The runtime is a SECOND Vulkan instance (its compositor) beside ours, and
    # it owns an IPC socket: never two of these at once (the VRAM law).
    RUN_SERIAL TRUE
    SKIP_RETURN_CODE 77
    ENVIRONMENT "${JAH_TEST_DISPLAY_ENV}JAHSHAKA_DATA_ROOT=${CMAKE_CURRENT_BINARY_DIR}/vr-home")
