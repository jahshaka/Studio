# Engine abstraction tests — headless, framework-free, link JahshakaEngine ONLY.
#
# Runtime requirements (both documented in spikes/headless-vulkan/README.md):
#  * A reachable X server (DISPLAY=:0, XWayland or Xvfb) even though every test is
#    offscreen: Ogre's VulkanXcbSupport connects at PLUGIN LOAD and throws if it
#    cannot. Not fixable from our side.
#  * A Vulkan driver. For a no-GPU CI box the same tests pass under lavapipe
#    (~30 s per test):  VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json
#  * ...except test_engine_headless, which boots EngineConfig::headless (the NULL
#    RenderSystem) and therefore needs NEITHER: it runs with DISPLAY unset. What
#    it cannot do is render — the NULL RenderSystem cannot create the RTT depth
#    buffer and its readback ticket never sets TextureBox::data, which is why
#    headless engines refuse both kinds of View instead of pretending.
#
# The teardown defect this suite guards (a MeshPtr outliving Root) reproduced in
# only 3 of 9 plain runs, so a single green run proves little:
#     ctest --repeat until-fail:5 --output-on-failure
# JAHSHAKA_ASAN=ON adds a second, sanitised build of the suite (test_engine_asan)
# compiled straight from the backend sources, leaving the app's engine library alone.

function(jahshaka_engine_test target)
    add_executable(${target} test_engine.cpp ${ARGN})
    # Never Qt: the root sets CMAKE_AUTOMOC ON globally.
    set_target_properties(${target} PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
        CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        # Ogre's shared libraries live in the install prefix; nothing else puts it on the path.
        BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
        INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(${target} PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    add_dependencies(${target} JahshakaEngineMedia)
    jah_gpu_row(${target} CLASS engine COMMAND ${target}
             # The backend writes its log to the working directory.
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # 6x measured with a 120 s floor, like every other row since
    # TEST_GATE_AUDIT.md §3: test_engine measures 11.5 s and its sanitised twin
    # 20 s, so 600 was a fifty-fold ceiling and a hang cost ten minutes of a
    # gate the owner wants to finish in ten.
    # LABELLED like every engine row (D6B-GATE-SHAPE; audit §8: the three engine API rows
    # were the tree's only unlabelled engine suites, invisible to -L engine).
    set_tests_properties(${target} PROPERTIES TIMEOUT 120 LABELS "engine")
endfunction()

jahshaka_engine_test(test_engine)
# TESTING-CLEANUP-2B item 2: TIMEOUT 240 = 6 x the run log's quiet median 39.1 s (40 quiet PASS runs since 2026-10-01, worst 46.7 s);
# declared 120 was below the policy, honest only with the +900 s widening (now deleted).
# budget 20 s: the measurement (TESTING-SPEED-B3) — 96 cases, 2,882 frames, 15.9-16.1 s at load 6 (42-51 s loaded at the
# base; 39 s quiet): every auto-exposure wait is FRAMES now (settleExposure: rendered until the measured exposure stops
# moving — 192 frames to converge measured_exposure's chain — in place of wall-clock renderFor seconds with an 8 ms sleep
# a frame), and the heavy-frame case builds its ~640 M triangles a frame from a 2.5 M shell.
set_tests_properties(test_engine PROPERTIES TIMEOUT 240)
target_link_libraries(test_engine PRIVATE JahshakaEngine)

# THE HEADLESS BOOT (SPECS/SCENEGRAPH_SPEC.md §3b): EngineConfig::headless, i.e.
# RenderSystem_NULL. The only suite in the tree that runs with NO DISPLAY AT ALL
# — ENVIRONMENT_MODIFICATION strips DISPLAY from its environment, so a green run
# is the proof that document-side work needs no graphics stack. Everything
# else here (and every pixel suite) still needs a reachable X server and a
# driver, which is exactly the distinction this suite exists to hold.
add_executable(test_engine_headless test_engine_headless.cpp)
set_target_properties(test_engine_headless PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_headless PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_headless PRIVATE JahshakaEngine)
add_dependencies(test_engine_headless JahshakaEngineMedia)
add_test(NAME engine.headless COMMAND test_engine_headless
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.headless PROPERTIES TIMEOUT 120 LABELS "document")
jah_no_display(engine.headless)

# Create/destroy/create in one process — needs the OgreVulkanDevice.cpp patch (OGRE_PLATFORM_DEPS.md).
add_executable(test_engine_recreate test_engine_recreate.cpp)
set_target_properties(test_engine_recreate PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_recreate PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_recreate PRIVATE JahshakaEngine)
add_dependencies(test_engine_recreate JahshakaEngineMedia)
jah_gpu_row(test_engine_recreate CLASS engine COMMAND test_engine_recreate WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# meshbake.shadow_vao_alias — THE SHADOW-VAO LIST OF A LOD CHAIN (ATOM P1's AT-A11;
# PHOTON-SCENE-SWITCH-1), as its own ctest name so gate-scope can select it: a
# 6-level mesh must hold one SHRUNK shadow VAO per level over one vertex buffer
# (never a mixed list — the caster pipeline takes the first VAO's layout), must
# destroy without the double free the pin used to throw, and
# `createMesh` must REFUSE a hand-built level that names a vertex the mesh has not.
# The same arm also runs inside test_engine_recreate's three cycles, because a
# destroy that used to throw belongs in the suite that destroys three engines.
jah_gpu_row(meshbake.shadow_vao_alias CLASS engine COMMAND test_engine_recreate --shadow-vao
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(meshbake.shadow_vao_alias PROPERTIES TIMEOUT 120 LABELS "engine")
set_tests_properties(test_engine_recreate PROPERTIES TIMEOUT 120 LABELS "engine")

# Decals (DECALS_SPEC.md): pixel-asserted projected textures, and the permanent
# home of the phase-0 spike's UV / -Y / pool findings.
add_executable(test_decals test_decals.cpp)
set_target_properties(test_decals PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_decals PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_decals PRIVATE JahshakaEngine)
add_dependencies(test_decals JahshakaEngineMedia)
jah_gpu_row(decals.engine CLASS engine COMMAND test_decals WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(decals.engine PROPERTIES TIMEOUT 120 LABELS "engine")

# THE LOD SWITCH BAND IS THE PASS'S, NOT THE PROCESS'S (fork 5230c9390+8282f6d70 (was 0075) as
# amended by ATOM-3-FIX; the render audit's A7). Its own binary because it needs
# a second scene pass on one scene — a PiP inset — and because it sets the
# offscreen-band latch before the engine exists.
add_executable(test_engine_lod_hysteresis test_engine_lod_hysteresis.cpp)
set_target_properties(test_engine_lod_hysteresis PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_lod_hysteresis PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_lod_hysteresis PRIVATE JahshakaEngine)
add_dependencies(test_engine_lod_hysteresis JahshakaEngineMedia)
jah_gpu_row(engine.lod_hysteresis CLASS engine COMMAND test_engine_lod_hysteresis
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.lod_hysteresis PROPERTIES TIMEOUT 120 LABELS "engine")

# The ON-SCREEN (swapchain) path — Linux/X11 only, and the only suite in the tree
# that binds a View to a native window. It never MAPS that window, so it is safe
# on a shared display; it still needs a real X server, like every other engine
# suite. Deep audit area 7 F1/F3.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(test_engine_window test_engine_window.cpp)
    set_target_properties(test_engine_window PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(test_engine_window PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    target_link_libraries(test_engine_window PRIVATE JahshakaEngine ${X11_LIBRARIES})
    add_dependencies(test_engine_window JahshakaEngineMedia)
    jah_gpu_row(engine.window CLASS engine COMMAND test_engine_window
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(engine.window PROPERTIES TIMEOUT 120 LABELS "engine")
endif()

# A FRAME THAT THREW STILL CLOSES (lane FRAME-CATCH-1): the monitor's record,
# the frame after it, and the device-lost latch — driven by the engine's own
# frame-fault hook (Engine::setFrameFault). Its own binary because it latches a
# (faked) lost device, which is one-way for the process that does it. The VR
# half of the same fix — the eye swapchain images an unclosed frame never
# releases — is `vr.session` under Monado.
add_executable(test_engine_frame_catch test_engine_frame_catch.cpp)
set_target_properties(test_engine_frame_catch PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_frame_catch PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_frame_catch PRIVATE JahshakaEngine)
add_dependencies(test_engine_frame_catch JahshakaEngineMedia)
jah_gpu_row(engine.frame_catch CLASS engine COMMAND test_engine_frame_catch
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.frame_catch PROPERTIES TIMEOUT 120 LABELS "engine")

# AN OUT-OF-MEMORY IS NOT A DEVICE LOSS (lane FORK-OOM-1): the Vulkan render
# system's own failure path, driven through Engine::setFrameFault. Its own
# binary because its negative control REALLY latches the device and ends the
# process like a host. Exit 77 = no Vulkan render system linked (nothing to
# prove), never a pass.
add_executable(test_engine_oom_is_not_loss test_engine_oom_is_not_loss.cpp)
set_target_properties(test_engine_oom_is_not_loss PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_oom_is_not_loss PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_oom_is_not_loss PRIVATE JahshakaEngine)
add_dependencies(test_engine_oom_is_not_loss JahshakaEngineMedia)
# One engine at a time (the in-record child runs and exits before this process
# boots its own), so the engine class's tokens cover it.
jah_gpu_row(engine.oom_is_not_loss CLASS engine COMMAND test_engine_oom_is_not_loss
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.oom_is_not_loss PROPERTIES TIMEOUT 120 LABELS "engine"
    SKIP_RETURN_CODE 77)

# WHAT COUNTS AS SCENE MOVEMENT (lane VR-SCAN-1): the movement epoch's
# precision, which is a 90 Hz bill in a VR session — the controller proxies and
# the pointing ray are written inside every frame that locates a hand, and a
# caster test that keyed on Ogre's default-set LAYER_SHADOW_CASTER bit rather
# than on a caster CHANNEL re-ran the full item scan for each of them. Both
# sides are asserted (furniture free, a real mover still counted) and the scan's
# cost is measured in-process; JAH_VR_SCAN_BENCH=1 adds the 5k/10k arms, which
# the gate does not run.
# engine.frame_input_drain / engine.frame_input_settle — THE FRAME'S INPUT, DRAINED ONCE (ENGINE-UPGRADE-1 U0 3a):
# FrameInput through Engine::submitFrame, applied at the top of renderOneFrame. Laws in counts (FrameInputStatus): one
# submit then N frames = N drains (N-1 empty); two submits before a frame = one drain, the scene part replaced; an
# unchanged record drains to nothing; a camera move is the per-view part only. --settle: one record or the identical
# record every frame reach giAtRest in the same frames with the same GI scans (the record changes WHEN, not WHAT).
add_executable(test_engine_frame_input test_engine_frame_input.cpp)
set_target_properties(test_engine_frame_input PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_frame_input PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_frame_input PRIVATE JahshakaEngine)
add_dependencies(test_engine_frame_input JahshakaEngineMedia)
jah_gpu_row(engine.frame_input_drain CLASS engine FRAMES 14 COMMAND test_engine_frame_input --drain
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.frame_input_drain PROPERTIES TIMEOUT 60 LABELS "engine")
jah_gpu_row(engine.frame_input_settle CLASS engine FRAMES 8 COMMAND test_engine_frame_input --settle
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/frame_input_settle")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/frame_input_settle")

# frame.record_determinism — THE RECORD AND ITS SERIAL ARE DETERMINISTIC (ENGINE-UPGRADE-U3 stage 1, the one-thread
# arm; stage 4 adds the two-thread arm on the same script). The render loop's hand-over stamps one serial per frame that
# applied a record (F1: the merge stays — two submits are one hand-over) and an empty frame carries the last (F2); a
# document node moved by its record over 60 frames in TWO processes reads the identical serial sequence and the
# byte-identical picture. Laws in frames and bytes (the closed-form serial, the readback's bytes).
add_executable(test_engine_record_determinism test_engine_record_determinism.cpp)
set_target_properties(test_engine_record_determinism PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_record_determinism PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_record_determinism PRIVATE JahshakaEngine)
add_dependencies(test_engine_record_determinism JahshakaEngineMedia)
# Two engines in sequence (one per child process: Root is a singleton), never at once: the engine class's slots cover it.
jah_gpu_row(frame.record_determinism CLASS engine FRAMES 128 COMMAND test_engine_record_determinism
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/record_determinism")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/record_determinism")
set_tests_properties(frame.record_determinism PROPERTIES TIMEOUT 60 LABELS "engine")
set_tests_properties(engine.frame_input_settle PROPERTIES TIMEOUT 60 LABELS "engine"
                     RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# engine.message_order — ONE ORDER ACROSS STREAMS (ENGINE-UPGRADE-U3 decision 46 (1)): a scene message and an engine
# message of one record land in the order they were posted (the shadow resolution's last word). A law in values.
add_executable(test_engine_message_order test_engine_message_order.cpp)
set_target_properties(test_engine_message_order PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_message_order PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_message_order PRIVATE JahshakaEngine)
add_dependencies(test_engine_message_order JahshakaEngineMedia)
jah_gpu_row(engine.message_order CLASS engine FRAMES 7 COMMAND test_engine_message_order
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.message_order PROPERTIES TIMEOUT 60 LABELS "engine")
jah_tsan_render(frame.record_determinism engine.message_order)   # the render thread's TSan arm (U3 decision 47)

# frame.interpolation — THE OWNER'S MODE DRAWS BETWEEN THE GRID STATES (ENGINE-UPGRADE-U3 stage 5): a 60 Hz mover at
# a 90 Hz display period, three poses per two steps, x = (2/3) j a closed form; lockstep frames draw the newest
# state exactly. frame.interpolation_apply (`--apply`, measure): 10,000 nodes' apply on the pool vs the frame's thread.
add_executable(test_engine_interpolation test_engine_interpolation.cpp)
set_target_properties(test_engine_interpolation PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_interpolation PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_interpolation PRIVATE JahshakaEngine)
add_dependencies(test_engine_interpolation JahshakaEngineMedia)
jah_gpu_row(frame.interpolation CLASS engine FRAMES 20 COMMAND test_engine_interpolation
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.interpolation PROPERTIES TIMEOUT 60 LABELS "engine")
jah_gpu_row(frame.interpolation_apply CLASS engine FRAMES 64 COMMAND test_engine_interpolation --apply
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.interpolation_apply PROPERTIES TIMEOUT 120 LABELS "engine;measure")

# frame.drag_latency — A RECORD IS ON SCREEN THE NEXT FRAME (ENGINE-UPGRADE-U3 stage 5): free-running, 120 records,
# each applied by the frame of the hand-over after its submit. A law in frames (hand-overs).
add_executable(test_engine_drag_latency test_engine_drag_latency.cpp)
set_target_properties(test_engine_drag_latency PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_drag_latency PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_drag_latency PRIVATE JahshakaEngine)
add_dependencies(test_engine_drag_latency JahshakaEngineMedia)
jah_gpu_row(frame.drag_latency CLASS engine FRAMES 400 COMMAND test_engine_drag_latency
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.drag_latency PROPERTIES TIMEOUT 60 LABELS "engine")
# frame.runtime_pace — A HEADSET FASTER THAN THE PANEL IS PACED BY ITS RUNTIME (ENGINE-UPGRADE-U3 decision 53): free frames
# under a fake runtime at 11 ms (vr.fakeRuntimePace) are never padded to the panel's 16.7 ms; with the session's period
# sent the interpolation draws in-between weights. Its bars are the decision's (mean interval <= 11.5 ms, 90 frames).
add_executable(test_engine_runtime_pace test_engine_runtime_pace.cpp)
set_target_properties(test_engine_runtime_pace PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_runtime_pace PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_runtime_pace PRIVATE JahshakaEngine)
add_dependencies(test_engine_runtime_pace JahshakaEngineMedia)
jah_gpu_row(frame.runtime_pace CLASS engine FRAMES 300 COMMAND test_engine_runtime_pace
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.runtime_pace PROPERTIES TIMEOUT 60 LABELS "engine")
# THE RENDER THREAD'S CPU MAP (ENGINE-UPGRADE-U4 stage 1, the deep read's P1-P3): one fixture, a carded-cube world
# written by the host's records (the group alone, or K cubes); each row one phase, frame-counted, read off the snapshot
# or the inspect door. frame.drain_cost is the GUARD (a 2,000-node world: every record applied and named, the drain's
# three parts printed); the 10k rows are `measure`, by hand (gi.trace_sun_cost is registered with the gi rows).
add_executable(test_engine_cpu_map test_engine_cpu_map.cpp)
set_target_properties(test_engine_cpu_map PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_cpu_map PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_cpu_map PRIVATE JahshakaEngine)
add_dependencies(test_engine_cpu_map JahshakaEngineMedia)
jah_gpu_row(frame.drain_cost CLASS engine FRAMES 79 COMMAND test_engine_cpu_map --drain 2000
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.drain_cost PROPERTIES TIMEOUT 120 LABELS "engine")
jah_gpu_exclusive_test(NAME frame.drain_cost_10k CLASS engine RUN_TIMEOUT 600 COMMAND test_engine_cpu_map --drain 10000
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.drain_cost_10k PROPERTIES LABELS "engine")
jah_gpu_exclusive_test(NAME engine.gpuscene_compose CLASS engine RUN_TIMEOUT 600 COMMAND test_engine_cpu_map --compose 10000
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.gpuscene_compose PROPERTIES LABELS "engine")
# engine.error_door — THE HOST'S ERROR DOOR NEVER TOUCHES THE FRAME'S SINK (ENGINE-UPGRADE-U3 decisions 44 + 49): the D1
# proof as an engine fixture (the app rows cannot boot under TSan); free-running, 300 refused messages, the host's door
# read every frame — and in the TSan arm, zero reports.
add_executable(test_engine_error_door test_engine_error_door.cpp)
set_target_properties(test_engine_error_door PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_error_door PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_error_door PRIVATE JahshakaEngine)
add_dependencies(test_engine_error_door JahshakaEngineMedia)
jah_gpu_row(engine.error_door CLASS engine FRAMES 620 COMMAND test_engine_error_door
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.error_door PROPERTIES TIMEOUT 60 LABELS "engine")
# frame.monitor_pop — THE MONITOR'S RECORDS ARE POPPED, NEVER REQUESTED (FRAME-MONITOR-DOOR-1): the hand-back door as an
# engine fixture (the form the TSan arm runs; perf.capture_free_tick is the owner's path and cannot boot under TSan):
# free-running, a capture on, the host popping after every frame while the render thread pushes; then lockstep. Laws in
# counts and frames: every recorded record popped once, in frame order, the stop's tail in one pop, nothing dropped; the
# capture's snapshot through one request in both modes; and (stage 4) a shader-cache save posted while frames draw free —
# its copy made on the render thread, the writer serializing beside the frames (its own cold cache, the TSan subject).
add_executable(test_engine_monitor_pop test_engine_monitor_pop.cpp)
set_target_properties(test_engine_monitor_pop PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_monitor_pop PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_monitor_pop PRIVATE JahshakaEngine)
add_dependencies(test_engine_monitor_pop JahshakaEngineMedia)
jah_gpu_row(frame.monitor_pop CLASS engine FRAMES 280 COMMAND test_engine_monitor_pop
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.monitor_pop PROPERTIES TIMEOUT 60 LABELS "engine")
jah_tsan_render(engine.error_door frame.monitor_pop)
jah_tsan_render(frame.drag_latency frame.interpolation)   # the free-running fixtures: the host reads published() against a publishing render thread (decision 54)

add_executable(test_engine_gi_scan test_engine_gi_scan.cpp)
set_target_properties(test_engine_gi_scan PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_gi_scan PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_gi_scan PRIVATE JahshakaEngine)
add_dependencies(test_engine_gi_scan JahshakaEngineMedia)
jah_gpu_row(engine.gi_scan CLASS engine COMMAND test_engine_gi_scan
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# In the gi_chain family like every cascade suite (tests/CMakeLists.txt) and
# 300 s: the 1k-item arm voxelises once in a Debug build.
set_tests_properties(engine.gi_scan PROPERTIES TIMEOUT 120 LABELS "engine"
                     RESOURCE_LOCK gi_chain COST ${JAH_COST_GI_CHAIN})

# THE GPU SCENE'S TABLES (lane GPU-SCENE-1; ENGINE V2's V2-2 pulled forward into
# phase A of the Atom and Photon builds). Its own binary because it reads the
# device table back — which flushes the render system's recorded commands — and
# because it drives a table GROW, so it wants a scene it can make big without
# slowing every other arm in test_engine.
add_executable(test_engine_gpu_scene test_engine_gpu_scene.cpp)
set_target_properties(test_engine_gpu_scene PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_gpu_scene PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_gpu_scene PRIVATE JahshakaEngine)
add_dependencies(test_engine_gpu_scene JahshakaEngineMedia)
jah_gpu_row(engine.gpu_scene CLASS engine COMMAND test_engine_gpu_scene
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.gpu_scene PROPERTIES TIMEOUT 120 LABELS "engine")

# THE ASYNC COMPUTE QUEUE ON THE FORK (ENGINE-UPGRADE-ASYNC stage 1): the fork's Component
# VulkanAsyncComputeQueue — the family it took, an empty bracket per frame on the second queue
# (timeline semaphores G and C), the three arms' one picture, the compute queue's command
# buffers bounded by its slots; the fallback with no compute queue requested; the bracket under
# the Khronos validation layer with synchronization validation on, zero messages. Its own
# binary because it reaches the render system's device (links the Vulkan RS, like
# engine.descriptor_pools) and because the two other rows set the process environment.
find_package(Vulkan QUIET)
if(OGRE_NEXT_VULKAN_RS_LIB AND Vulkan_FOUND)
    add_executable(test_engine_async_queue test_engine_async_queue.cpp)
    set_target_properties(test_engine_async_queue PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
    target_compile_definitions(test_engine_async_queue PRIVATE
        JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
        JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    target_include_directories(test_engine_async_queue PRIVATE
        "${OGRE_NEXT_PREFIX}/include/OGRE-Next/RenderSystems/Vulkan"
        "${OGRE_NEXT_PREFIX}/include/OGRE-Next/RenderSystems/Vulkan/include"
        ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(test_engine_async_queue PRIVATE JahshakaEngine OgreNext
        "${OGRE_NEXT_VULKAN_RS_LIB}" ${Vulkan_LIBRARIES})
    add_dependencies(test_engine_async_queue JahshakaEngineMedia)
    jah_gpu_row(engine.async_queue CLASS engine COMMAND test_engine_async_queue
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(engine.async_queue PROPERTIES TIMEOUT 120 LABELS "engine")
    jah_gpu_row(engine.async_queue_nofamily CLASS engine COMMAND test_engine_async_queue --no-family
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(engine.async_queue_nofamily PROPERTIES TIMEOUT 120 LABELS "engine"
        ENVIRONMENT "JAH_VK_ASYNC_COMPUTE=0")
    # STAGE 2 (FORK-RECORD): the fixture dispatch in the bracket one frame behind, read by the
    # graphics queue after waitAt — the closed form in order, async and toggled on/off; the
    # frame-data rule's counts; and the runtime-active graphics-only checks (each refused
    # command names itself, the bracket still closes, the next frames render).
    jah_gpu_row(engine.async_record CLASS engine COMMAND test_engine_async_queue --record
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(engine.async_record PROPERTIES TIMEOUT 120 LABELS "engine")
    jah_gpu_row(engine.async_graphics_check CLASS engine COMMAND test_engine_async_queue --graphics-check
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(engine.async_graphics_check PROPERTIES TIMEOUT 120 LABELS "engine")
    set(_jah_vvl_log "${CMAKE_CURRENT_BINARY_DIR}/async-queue-validation.log")
    jah_gpu_row(engine.async_queue_validation CLASS engine
             COMMAND test_engine_async_queue --validation "${_jah_vvl_log}"
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(engine.async_queue_validation PROPERTIES TIMEOUT 120 LABELS "engine"
        ENVIRONMENT "VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation;VK_KHRONOS_VALIDATION_VALIDATE_SYNC=true;VK_KHRONOS_VALIDATION_DEBUG_ACTION=VK_DBG_LAYER_ACTION_LOG_MSG;VK_KHRONOS_VALIDATION_REPORT_FLAGS=error,warn;VK_KHRONOS_VALIDATION_LOG_FILENAME=${_jah_vvl_log}")
endif()

# engine.gpu_substrate — THE SUBSTRATE (ENGINE-UPGRADE-2 stage 1): one writer per SceneManager,
# one walk per frame (the pre-frame GI readers and the frame's own read share it), Atom's
# words step once a frame, one instance-table copy per frame in flight. Counts, not clocks.
add_executable(test_engine_gpu_substrate test_engine_gpu_substrate.cpp)
set_target_properties(test_engine_gpu_substrate PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_gpu_substrate PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_gpu_substrate PRIVATE JahshakaEngine)
add_dependencies(test_engine_gpu_substrate JahshakaEngineMedia)
jah_gpu_row(engine.gpu_substrate CLASS engine COMMAND test_engine_gpu_substrate
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.gpu_substrate PROPERTIES TIMEOUT 60 LABELS "engine")

# engine.readd_named — THE 10k RE-ADD IS PAID IN WHAT IT NAMED (ENGINE-UPGRADE-2B stage 5): the reparent bench's
# fragment at the engine's seams (a 500-item subtree re-parented and its items re-created, ONE frame) at 1k and 10k:
# named / compared / flags recomputed <= 500 and no full walk (red before at 1k: the cap cliff). See the source.
add_executable(test_engine_readd_named test_engine_readd_named.cpp)
set_target_properties(test_engine_readd_named PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_readd_named PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_readd_named PRIVATE JahshakaEngine)
add_dependencies(test_engine_readd_named JahshakaEngineMedia)
jah_gpu_row(engine.readd_named CLASS engine COMMAND test_engine_readd_named
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.readd_named PROPERTIES TIMEOUT 60 LABELS "engine"
    JAHSHAKA_TEST_FRAMES 122)   # counted from the source (2 scales x (60 settle + the fragment's one) )

# engine.gpu_walk_parity — THE NAMED WALK'S GUARD (ENGINE-UPGRADE-2B stage 2): with the arm
# gpuscene.walkParity, every slot is checked against the table after every frame's compose; every
# writer kind of a slot's transform or flags word leaves 0 misses. See the source.
add_executable(test_engine_gpu_walk_parity test_engine_gpu_walk_parity.cpp)
set_target_properties(test_engine_gpu_walk_parity PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_gpu_walk_parity PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_gpu_walk_parity PRIVATE JahshakaEngine)
add_dependencies(test_engine_gpu_walk_parity JahshakaEngineMedia)
jah_gpu_row(engine.gpu_walk_parity CLASS engine COMMAND test_engine_gpu_walk_parity
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.gpu_walk_parity PROPERTIES TIMEOUT 60 LABELS "engine"
    JAHSHAKA_TEST_FRAMES 110)   # the frames it renders, counted (decision 18.7: the two U4 steps and the death sequence added)
# engine.gpuscene_identity — THE PARALLEL COMPOSE IS THE SERIAL ONE, BYTE FOR BYTE (ENGINE-UPGRADE-U4 stage 4): 8 workers,
# 1,024 moving cubes, the compose's digest (every composed slot's index, row and tail in replay order) compared between
# the compose inside the compare's task and the serial arm (gpuscene.serialCompose), ABBA. Also a clang TSan row.
jah_gpu_row(engine.gpuscene_identity CLASS engine COMMAND test_engine_gpu_walk_parity --compose-identity
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.gpuscene_identity PROPERTIES TIMEOUT 60 LABELS "engine"
    JAHSHAKA_TEST_FRAMES 18)   # 1 + 6 warm + 10 + 1
jah_tsan_render(engine.gpuscene_identity)
# engine.gpu_walk_parity_free — THE INTERPOLATED POSE IS THE TABLE'S (ENGINE-UPGRADE-U3 decision 56): FREE-RUNNING on the
# render thread, a mover drawn between its grid states at 90 over 60 and then stopped: 0 misses moving and stopped.
jah_gpu_row(engine.gpu_walk_parity_free CLASS engine COMMAND test_engine_gpu_walk_parity --free-running
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.gpu_walk_parity_free PROPERTIES TIMEOUT 60 LABELS "engine"
    JAHSHAKA_TEST_FRAMES 39)   # 6 + 2 + 24 free + 6 free + 1

option(JAHSHAKA_ASAN "Also build the engine tests with AddressSanitizer" OFF)
if(JAHSHAKA_ASAN)
    # A private, sanitised copy of the backend: the app's JahshakaEngine stays clean.
    add_library(JahshakaEngineAsan STATIC ${JAHSHAKA_ENGINE_SOURCES})
    target_include_directories(JahshakaEngineAsan
        PUBLIC ${CMAKE_SOURCE_DIR}/irisgl/engine/include
        PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src ${JAHSHAKA_ENGINE_BUILD_ID_DIR})
    target_link_libraries(JahshakaEngineAsan PRIVATE OgreNext ${X11_LIBRARIES})
    # Same shader-cache fingerprint terms as the real library (irisgl/engine/
    # CMakeLists.txt) — OgreShaderCache.cpp will not compile without them. The
    # engine BUILD ID now arrives as a generated header on the include path
    # (LIGHTING_FIX fix 10), so this copy depends on the same custom target
    # rather than baking a second, configure-time value.
    add_dependencies(JahshakaEngineAsan JahshakaEngineBuildId)
    # The ray-query tier is NEW code, so the sanitised twin must carry it too
    # (PHOTON_SPEC §7 R1's gate: test_engine and test_engine_asan both green).
    jahshaka_engine_ray_query(JahshakaEngineAsan)
    # ...and the VR session TU, for the same reason (VR_SPEC §6's gate:
    # test_engine and test_engine_asan both green with the new TU compiled in).
    jahshaka_engine_vr(JahshakaEngineAsan)
    target_compile_definitions(JahshakaEngineAsan PRIVATE
        JAHSHAKA_OGRE_FORK_COMMIT="${JAHSHAKA_OGRE_FORK_COMMIT}")
    set_target_properties(JahshakaEngineAsan PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_options(JahshakaEngineAsan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(JahshakaEngineAsan PUBLIC -fsanitize=address)

    jahshaka_engine_test(test_engine_asan)
    target_link_libraries(test_engine_asan PRIVATE JahshakaEngineAsan)
    # LeakSanitizer is OFF: the only leaks it finds (6 allocations, ~4 KB) are inside
    # libdbus-1, reached from the Vulkan loader's Mesa device-select layer — not ours.
    # Use-after-free / overflow detection is the point here and stays on.
    # TESTING-CLEANUP-2B item 2: TIMEOUT 430 = 6 x the run log's quiet median 71.1 s (40 quiet PASS runs since 2026-10-01, worst 88.4 s);
    # declared 420 was below the policy, honest only with the +900 s widening (now deleted).
    # STAGE-CLOSE (TESTING-SPEED-B3): an ASAN twin keeps its row and moves to the stage close; test_engine
    # (the same suite, uninstrumented) is the merge tier's.
    set_tests_properties(test_engine_asan PROPERTIES
        ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}" TIMEOUT 430 LABELS "engine")
    target_compile_options(test_engine_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_engine_asan PRIVATE -fsanitize=address)
endif()

# engine.media_check (FORWARD-ONLY-1 D2): the staged tonemap media check fires at
# engine init and is sticky — a copy of the media whose HDR/FinalToneMapping pass
# has no fragment program refuses every view with the error; the real media works.
add_executable(test_engine_media_check test_engine_media_check.cpp)
set_target_properties(test_engine_media_check PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_media_check PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_media_check PRIVATE JahshakaEngine)
add_dependencies(test_engine_media_check JahshakaEngineMedia)
jah_gpu_row(engine.media_check CLASS engine COMMAND test_engine_media_check
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.media_check PROPERTIES TIMEOUT 180 LABELS "engine")

# FRAMES — THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B3; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, measured 2026-10-12 at the lane's tip by spikes/testing-speed-b3/framecount (an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded, summed over the row's processes); 0 = a row that renders nothing.
foreach(_row_frames
        "decals.engine=60"
        "engine.async_queue=1920"             # 300 x 3 arms + 20 + 1,000 (counted from the source)
        "engine.async_queue_nofamily=600"
        "engine.async_queue_validation=319"   # 10 + (60 + 2) + (120 + 2) + (120 + 2) + 3 (the shared target), counted from the source
        "engine.async_record=736"             # 10 + 3 x (240 + 2)
        "engine.async_graphics_check=34"     # 10 + 2 x (5 + 1 + 3 + 3), counted from the source
        "engine.frame_catch=27"
        "engine.gi_scan=162"
        "engine.gpu_scene=58"
        "engine.gpu_substrate=103"  # measured (a temporary renderOneFrame counter: + the graphics-build frame 1 + 8)
        "engine.headless=0"
        "engine.lod_hysteresis=265"
        "engine.media_check=0"
        "engine.oom_is_not_loss=20"   # counted from the source (it _exits: no meter report)
        "engine.window=65"
        "meshbake.shadow_vao_alias=3"
        "test_engine=2882"
        "test_engine_asan=2882"   # its plain twin's (the ASan runtime refuses the frame meter)
        "test_engine_recreate=48"
        )
    string(REPLACE "=" ";" _rf "${_row_frames}")
    list(GET _rf 0 _r)
    list(GET _rf 1 _n)
    if(TEST ${_r})
        set_tests_properties(${_r} PROPERTIES JAHSHAKA_TEST_FRAMES ${_n})
    endif()
endforeach()
