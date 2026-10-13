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
        "engine.gpu_substrate=94"   # counted from the source (4 + 8 + 2 x 31 + 4 + 4 + 12)
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
