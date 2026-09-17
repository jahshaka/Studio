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
# EVERYTHING HERE IS OPTIONAL. Without libopenxr-dev, without the Vulkan render
# system, or on macOS, nothing is built and nothing is registered; without
# monado-service on the box the registered test SKIPS (exit 77). A box with no VR
# stack is a supported box.

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

set_tests_properties(vr.spike_1a PROPERTIES
    LABELS "engine"
    TIMEOUT 600
    # The runtime is a SECOND Vulkan instance (its compositor) beside ours, and
    # it owns an IPC socket: never two of these at once (the VRAM law).
    RUN_SERIAL TRUE
    SKIP_RETURN_CODE 77
    ENVIRONMENT "${JAH_TEST_DISPLAY_ENV}JAHSHAKA_DATA_ROOT=${CMAKE_CURRENT_BINARY_DIR}/vr-home")
