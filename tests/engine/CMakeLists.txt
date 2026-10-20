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
    cmake_parse_arguments(E "NO_ROW" "" "" ${ARGN})
    add_executable(${target} test_engine.cpp ${E_UNPARSED_ARGUMENTS})
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
    if(E_NO_ROW)
        return()
    endif()
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
jah_ship_row(test_engine)   # THE SHIP SET (TEST-CUT-1)
# TESTING-CLEANUP-2B item 2: TIMEOUT 240 = 6 x the run log's quiet median 39.1 s (40 quiet PASS runs since 2026-10-01, worst 46.7 s);
# declared 120 was below the policy, honest only with the +900 s widening (now deleted).
# budget 20 s: the measurement (TESTING-SPEED-B3) — 96 cases, 2,882 frames, 15.9-16.1 s at load 6 (42-51 s loaded at the
# base; 39 s quiet): every auto-exposure wait is FRAMES now (settleExposure: rendered until the measured exposure stops
# moving — 192 frames to converge measured_exposure's chain — in place of wall-clock renderFor seconds with an 8 ms sleep
# a frame), and the heavy-frame case builds its ~640 M triangles a frame from a 2.5 M shell.
set_tests_properties(test_engine PROPERTIES TIMEOUT 240)
target_link_libraries(test_engine PRIVATE JahshakaEngine)

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
jah_gpu_row(test_engine_recreate SHIP CLASS engine COMMAND test_engine_recreate WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# meshbake.shadow_vao_alias (archived: archive/tests/meshbake) — THE SHADOW-VAO LIST OF A LOD CHAIN (ATOM P1's AT-A11;
# PHOTON-SCENE-SWITCH-1), as its own ctest name so gate-scope can select it: a
# 6-level mesh must hold one SHRUNK shadow VAO per level over one vertex buffer
# (never a mixed list — the caster pipeline takes the first VAO's layout), must
# destroy without the double free the pin used to throw, and
# `createMesh` must REFUSE a hand-built level that names a vertex the mesh has not.
# The same arm also runs inside test_engine_recreate's three cycles, because a
# destroy that used to throw belongs in the suite that destroys three engines.
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
jah_gpu_row(decals.engine SHIP CLASS engine COMMAND test_decals WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(decals.engine PROPERTIES TIMEOUT 120 LABELS "engine")

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
jah_gpu_row(frame.record_determinism SHIP CLASS engine FRAMES 128 COMMAND test_engine_record_determinism
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/record_determinism")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/record_determinism")
set_tests_properties(frame.record_determinism PROPERTIES TIMEOUT 60 LABELS "engine")

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
jah_gpu_row(engine.message_order SHIP CLASS engine FRAMES 7 COMMAND test_engine_message_order
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.message_order PROPERTIES TIMEOUT 60 LABELS "engine")
jah_tsan_render(frame.record_determinism engine.message_order)   # the render thread's TSan arm (U3 decision 47)

# frame.interpolation — THE OWNER'S MODE DRAWS BETWEEN THE GRID STATES (ENGINE-UPGRADE-U3 stage 5): a 60 Hz mover at
# a 90 Hz display period, three poses per two steps, x = (2/3) j a closed form; lockstep frames draw the newest
# state exactly. frame.interpolation_apply (archived: archive/tests/frame) (`--apply`, measure): 10,000 nodes' apply on the pool vs the frame's thread.
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
jah_gpu_row(frame.interpolation SHIP CLASS engine FRAMES 20 COMMAND test_engine_interpolation
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.interpolation PROPERTIES TIMEOUT 60 LABELS "engine")

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
jah_gpu_row(frame.drag_latency SHIP CLASS engine FRAMES 400 COMMAND test_engine_drag_latency
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.drag_latency PROPERTIES TIMEOUT 60 LABELS "engine")
# frame.drain_budget — A FREE FRAME'S DRAIN IS BOUNDED PER FRAME, THE REST CARRIED IN ORDER (BUGS-7 stage 1): a batch of
# 20,000 parented creates applies in ONE lockstep frame, and free-running over several frames — the serial advancing and
# the backlog shrinking every frame, none refused (the order held), the longest drain under half the whole batch's.
add_executable(test_engine_drain_budget test_engine_drain_budget.cpp)
set_target_properties(test_engine_drain_budget PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_drain_budget PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_drain_budget PRIVATE JahshakaEngine)
add_dependencies(test_engine_drain_budget JahshakaEngineMedia)
jah_gpu_row(frame.drain_budget CLASS engine FRAMES 40 COMMAND test_engine_drain_budget
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.drain_budget PROPERTIES TIMEOUT 60 LABELS "engine")
# frame.streaming_picture — A FRAME WITH A HOLE IS PRESENTED, NEVER A HELD PLATE (BUGS-7 stage 2): a WINDOW view on the
# rig with the background compiler and no shader cache; an empty scene's plate, then a PBS cube (its placeholder) and an
# unlit cube (the hole: unlit has no placeholder) — each frame photographed from the X root: geometry within 3 frames,
# never the plate after. RED on the held frame (the plate stays until the hole's permutations land).
find_package(X11 REQUIRED)
add_executable(test_engine_streaming_picture test_engine_streaming_picture.cpp streaming_picture_x11.cpp)
set_target_properties(test_engine_streaming_picture PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_streaming_picture PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_include_directories(test_engine_streaming_picture PRIVATE ${X11_INCLUDE_DIR})
target_link_libraries(test_engine_streaming_picture PRIVATE JahshakaEngine ${X11_LIBRARIES})
add_dependencies(test_engine_streaming_picture JahshakaEngineMedia)
jah_gpu_row(frame.streaming_picture SHIP CLASS engine FRAMES 60 COMMAND test_engine_streaming_picture
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.streaming_picture PROPERTIES TIMEOUT 60 LABELS "engine")
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
jah_gpu_row(engine.error_door SHIP CLASS engine FRAMES 620 COMMAND test_engine_error_door
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.error_door PROPERTIES TIMEOUT 60 LABELS "engine")
# frame.monitor_pop — THE MONITOR'S RECORDS ARE POPPED, NEVER REQUESTED (FRAME-MONITOR-DOOR-1): the hand-back door as an
# engine fixture (the form the TSan arm runs; perf.capture_free_tick (archived: archive/tests/perf) is the owner's path and cannot boot under TSan):
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
jah_gpu_row(frame.monitor_pop SHIP CLASS engine FRAMES 280 COMMAND test_engine_monitor_pop
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.monitor_pop PROPERTIES TIMEOUT 60 LABELS "engine")
jah_tsan_render(engine.error_door frame.monitor_pop)

jah_tsan_render(frame.drag_latency frame.interpolation)   # the free-running fixtures: the host reads published() against a publishing render thread (decision 54)

# THE GPU SCENE'S TABLES (lane GPU-SCENE-1; ENGINE V2's V2-2 pulled forward into
# phase A of the Atom and Photon builds). Its own binary because it reads the
# device table back — which flushes the render system's recorded commands — and
# because it drives a table GROW, so it wants a scene it can make big without
# slowing every other arm in test_engine.

# THE ASYNC COMPUTE QUEUE ON THE FORK (ENGINE-UPGRADE-ASYNC stage 1): the fork's Component
# VulkanAsyncComputeQueue — the family it took, an empty bracket per frame on the second queue
# (timeline semaphores G and C), the three arms' one picture, the compute queue's command
# buffers bounded by its slots; the fallback with no compute queue requested; the bracket under
# the Khronos validation layer with synchronization validation on, zero messages. Its own
# binary because it reaches the render system's device (links the Vulkan RS, like
# engine.descriptor_pools (archived: archive/tests/engine)) and because the two other rows set the process environment.
find_package(Vulkan QUIET)

# engine.gpu_walk_parity (archived: archive/tests/engine) — THE NAMED WALK'S GUARD (ENGINE-UPGRADE-2B stage 2): with the arm
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
# engine.gpuscene_identity — THE PARALLEL COMPOSE IS THE SERIAL ONE, BYTE FOR BYTE (ENGINE-UPGRADE-U4 stage 4): 8 workers,
# 1,024 moving cubes, the compose's digest (every composed slot's index, row and tail in replay order) compared between
# the compose inside the compare's task and the serial arm (gpuscene.serialCompose), ABBA. Also a clang TSan row.
# RAY-FEED-OBSERVER-1: the ray tier live on the view, the ray feed's traced set folded into the digest (a re-level, an
# alpha slot, a rigged slot and a mesh swap in the scene); its lane halves owe the ordered body for <= 10 % of the movers.
jah_gpu_row(engine.gpuscene_identity SHIP CLASS engine COMMAND test_engine_gpu_walk_parity --compose-identity
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.gpuscene_identity PROPERTIES TIMEOUT 60 LABELS "engine"
    JAHSHAKA_TEST_FRAMES 18)   # 1 + 6 warm + 10 + 1
jah_tsan_render(engine.gpuscene_identity)

option(JAHSHAKA_ASAN "Also build the engine tests with AddressSanitizer" OFF)
if(JAHSHAKA_ASAN)
    # A private, sanitised copy of the backend: the app's JahshakaEngine stays clean.
    add_library(JahshakaEngineAsan STATIC ${JAHSHAKA_ENGINE_SOURCES})
    target_include_directories(JahshakaEngineAsan
        PUBLIC ${CMAKE_SOURCE_DIR}/irisgl/engine/include
        PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src ${JAHSHAKA_ENGINE_BUILD_ID_DIR}
                ${CMAKE_SOURCE_DIR}/irisgl)   # core/threadbudget.h, as the real library (THREADING-1)
    target_link_libraries(JahshakaEngineAsan PRIVATE OgreNext ${X11_LIBRARIES})
    # Same shader-cache fingerprint terms as the real library (irisgl/engine/
    # CMakeLists.txt) — OgreShaderCache.cpp will not compile without them. The
    # engine BUILD ID now arrives as a generated header on the include path
    # (LIGHTING_FIX fix 10), so this copy depends on the same custom target
    # rather than baking a second, configure-time value.
    add_dependencies(JahshakaEngineAsan JahshakaEngineBuildId)
    # ...and the generated headers are its objects' inputs, as the real library's (TOOLING-2 stage 3).
    jahshaka_engine_generated_deps(JahshakaEngineAsan)
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

    jahshaka_engine_test(test_engine_asan NO_ROW)
    target_link_libraries(test_engine_asan PRIVATE JahshakaEngineAsan)
    # TWO ROWS, HALF THE CASES EACH (TEST-CUT-1; CLAUDE.md Testing: every row at most 60 s). THE LAW is test_engine's own
    # 96 cases, every one under AddressSanitizer — a use-after-free or an overflow in the backend is a red — and it needs
    # every case once, not one process: each case builds what it reads on the shared engine and tears it down, which is
    # what makes test_engine's single-case filter work. `--part k/2` runs the k-th contiguous half in the list's order
    # (part 2 ends with teardown_is_clean). The FRAMES are each half's (its plain twin's, counted once per part by
    # spikes/testing-speed-b3/framecount: 490 + 2,416); the price is each part's own solo run: 9.3 / 18.1 s.
    foreach(_part 1 2)
        jah_gpu_row(test_engine_asan.part${_part} SHIP CLASS engine COMMAND test_engine_asan --part ${_part}/2
                 WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    # LeakSanitizer is OFF: the only leaks it finds (6 allocations, ~4 KB) are inside
    # libdbus-1, reached from the Vulkan loader's Mesa device-select layer — not ours.
    # Use-after-free / overflow detection is the point here and stays on.
    # TESTING-CLEANUP-2B item 2: TIMEOUT 430 = 6 x the run log's quiet median 71.1 s (40 quiet PASS runs since 2026-10-01, worst 88.4 s);
    # declared 420 was below the policy, honest only with the +900 s widening (now deleted).
    # STAGE-CLOSE (TESTING-SPEED-B3): an ASAN twin keeps its row and moves to the stage close; test_engine
    # (the same suite, uninstrumented) is the merge tier's.
        # TIMEOUT 120 = the floor (6 x the solo runs 9.3 / 18.1 s are under it; TEST-CUT-1 stage 3, 2026-10-20)
        set_tests_properties(test_engine_asan.part${_part} PROPERTIES
            ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}" TIMEOUT 120 LABELS "engine")
    endforeach()
    target_compile_options(test_engine_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_engine_asan PRIVATE -fsanitize=address)
endif()

# FRAMES — THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B3; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, measured 2026-10-12 at the lane's tip by spikes/testing-speed-b3/framecount (an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded, summed over the row's processes); 0 = a row that renders nothing.
foreach(_row_frames
        "decals.engine=60"
        "test_engine=2882"
        "test_engine_asan.part1=490"    # TEST-CUT-1: its plain twin's half (test_engine --part 1/2, the meter, 2026-10-20)
        "test_engine_asan.part2=2416"   # (test_engine --part 2/2: the heavy-frame and the monitor cases are here)
        "test_engine_recreate=48"
        )
    string(REPLACE "=" ";" _rf "${_row_frames}")
    list(GET _rf 0 _r)
    list(GET _rf 1 _n)
    if(TEST ${_r})
        set_tests_properties(${_r} PROPERTIES JAHSHAKA_TEST_FRAMES ${_n})
    endif()
endforeach()
