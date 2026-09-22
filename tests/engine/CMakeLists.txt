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
    add_test(NAME ${target} COMMAND ${target}
             # The backend writes its log to the working directory.
             WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # 6x measured with a 120 s floor, like every other row since
    # TEST_GATE_AUDIT.md §3: test_engine measures 11.5 s and its sanitised twin
    # 20 s, so 600 was a fifty-fold ceiling and a hang cost ten minutes of a
    # gate the owner wants to finish in ten.
    set_tests_properties(${target} PROPERTIES TIMEOUT 120)
endfunction()

jahshaka_engine_test(test_engine)
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
set_tests_properties(engine.headless PROPERTIES TIMEOUT 300 LABELS "document")
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
add_test(NAME test_engine_recreate COMMAND test_engine_recreate WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# meshbake.shadow_vao_alias — THE MIXED SHADOW-VAO LIST (ATOM P1's AT-A11,
# ogre-patch 0088), as its own ctest name so gate-scope can select it: a 6-level
# mesh must hold ONE optimized shadow VAO with its coarse levels aliasing the
# normal ones, must destroy without the double free the pin used to throw, and
# `createMesh` must REFUSE a hand-built level that names a vertex the mesh has not.
# The same arm also runs inside test_engine_recreate's three cycles, because a
# destroy that used to throw belongs in the suite that destroys three engines.
add_test(NAME meshbake.shadow_vao_alias COMMAND test_engine_recreate --shadow-vao
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(meshbake.shadow_vao_alias PROPERTIES TIMEOUT 300 LABELS "engine")
set_tests_properties(test_engine_recreate PROPERTIES TIMEOUT 120)

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
add_test(NAME decals.engine COMMAND test_decals WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(decals.engine PROPERTIES TIMEOUT 120 LABELS "engine")

# THE LOD SWITCH BAND IS THE PASS'S, NOT THE PROCESS'S (ogre-patch 0075 as
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
add_test(NAME engine.lod_hysteresis COMMAND test_engine_lod_hysteresis
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.lod_hysteresis PROPERTIES TIMEOUT 300 LABELS "engine")

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
    add_test(NAME engine.window COMMAND test_engine_window
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
add_test(NAME engine.frame_catch COMMAND test_engine_frame_catch
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.frame_catch PROPERTIES TIMEOUT 120 LABELS "engine")

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
add_test(NAME engine.gi_scan COMMAND test_engine_gi_scan
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# RUN_SERIAL like every cascade suite (it holds a voxel volume) and 300 s: the
# 1k-item arm voxelises once in a Debug build.
set_tests_properties(engine.gi_scan PROPERTIES TIMEOUT 300 LABELS "engine" RUN_SERIAL ON)

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
add_test(NAME engine.gpu_scene COMMAND test_engine_gpu_scene
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.gpu_scene PROPERTIES TIMEOUT 300 LABELS "engine")

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
        JAHSHAKA_OGRE_PATCH_SERIES="${JAHSHAKA_OGRE_PATCH_SERIES}")
    set_target_properties(JahshakaEngineAsan PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_compile_options(JahshakaEngineAsan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(JahshakaEngineAsan PUBLIC -fsanitize=address)

    jahshaka_engine_test(test_engine_asan)
    target_link_libraries(test_engine_asan PRIVATE JahshakaEngineAsan)
    # LeakSanitizer is OFF: the only leaks it finds (6 allocations, ~4 KB) are inside
    # libdbus-1, reached from the Vulkan loader's Mesa device-select layer — not ours.
    # Use-after-free / overflow detection is the point here and stays on.
    set_tests_properties(test_engine_asan PROPERTIES
        ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}")
    target_compile_options(test_engine_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_engine_asan PRIVATE -fsanitize=address)
endif()
