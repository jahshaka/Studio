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
    set_tests_properties(${target} PROPERTIES TIMEOUT 60 LABELS "engine")
endfunction()

jahshaka_engine_test(test_engine)
jah_ship_row(test_engine)   # THE SHIP SET (TEST-CUT-1)
# TESTING-CLEANUP-2B item 2: TIMEOUT 60 (URGENT #48: was 240) = 6 x the run log's quiet median 39.1 s (40 quiet PASS runs since 2026-10-01, worst 46.7 s);
# declared 120 was below the policy, honest only with the +900 s widening (now deleted).
# budget 20 s: the measurement (TESTING-SPEED-B3) — 96 cases, 2,882 frames, 15.9-16.1 s at load 6 (42-51 s loaded at the
# base; 39 s quiet): every auto-exposure wait is FRAMES now (settleExposure: rendered until the measured exposure stops
# moving — 192 frames to converge measured_exposure's chain — in place of wall-clock renderFor seconds with an 8 ms sleep
# a frame), and the heavy-frame case builds its ~640 M triangles a frame from a 2.5 M shell.
set_tests_properties(test_engine PROPERTIES TIMEOUT 60)
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
# THE ENGINE's ROW IN FAST (the lead, TEST-STREAMLINE-1 4a): test_engine is `measure` by its price (15.99 s over the
# fixture's 10 s, JAH_MEASURE_ROWS), so FAST would carry no engine row; this one runs the engine's whole lifecycle end to end -
# create -> render -> destroy, three times in one process (the startup order and the teardown that mirrors it) - at 4.18 s.
jah_gpu_row(test_engine_recreate SHIP CLASS engine COMMAND test_engine_recreate WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# meshbake.shadow_vao_alias (archived: archive/tests/meshbake) — THE SHADOW-VAO LIST OF A LOD CHAIN (ATOM P1's AT-A11;
# PHOTON-SCENE-SWITCH-1), as its own ctest name so gate-scope can select it: a
# 6-level mesh must hold one SHRUNK shadow VAO per level over one vertex buffer
# (never a mixed list — the caster pipeline takes the first VAO's layout), must
# destroy without the double free the pin used to throw, and
# `createMesh` must REFUSE a hand-built level that names a vertex the mesh has not.
# The same arm also runs inside test_engine_recreate's three cycles, because a
# destroy that used to throw belongs in the suite that destroys three engines.
set_tests_properties(test_engine_recreate PROPERTIES TIMEOUT 60 LABELS "engine")

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
set_tests_properties(decals.engine PROPERTIES TIMEOUT 60 LABELS "engine")

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

# engine.draw_order — THE PICTURE NEVER DEPENDS ON CREATION ORDER (GIZMO-ORDER-1): two coincident on-top parts drawn
# in a stated order (Scene::setNodeDrawOrder) give the same bytes in both creation orders and the higher order shows;
# a blended on-top part still draws after an ordered opaque one; the verb refuses by name. A law in exact codes; 16 frames.
add_executable(test_engine_draw_order test_engine_draw_order.cpp)
set_target_properties(test_engine_draw_order PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_draw_order PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_draw_order PRIVATE JahshakaEngine)
add_dependencies(test_engine_draw_order JahshakaEngineMedia)
jah_gpu_row(engine.draw_order CLASS engine FRAMES 16 COMMAND test_engine_draw_order
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.draw_order PROPERTIES TIMEOUT 60 LABELS "engine")

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
jah_gpu_row(frame.interpolation CLASS engine FRAMES 34 COMMAND test_engine_interpolation   # +14 the rewound-serial case (ONE-FRAME-1: 2 lockstep + 12 free, counted by reading)
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
jah_gpu_row(frame.drag_latency CLASS engine FRAMES 400 COMMAND test_engine_drag_latency
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.drag_latency PROPERTIES TIMEOUT 60 LABELS "engine")
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
jah_gpu_row(frame.streaming_picture CLASS engine FRAMES 60
         COLD "the hole IS a cold compile: the unlit cube's permutation must be building while frames are presented (the fixture's shaderCacheOff)"
         COMMAND test_engine_streaming_picture
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.streaming_picture PROPERTIES TIMEOUT 60 LABELS "engine")
# sky.ibl_async_lands — AN ASYNCHRONOUS SCENE's SKY CHANGE GETS ITS REFLECTION (COLD-BOOT-1): an offscreen view in
# asynchronous mode on the Streaming pace, no shader cache (the convolution's permutations cold), an atmosphere sky. Laws
# in frames: the convolution deferred at least once; once the background compiler is empty the environment lands within
# 5 frames (lag + 2, derived in the source); no try after it. RED before the kept workspace (each try's clones cancelled
# their own requests: the startup gate's livelock, spikes/cold-boot-1/S4arm).
add_executable(test_engine_ibl_async_lands test_engine_ibl_async_lands.cpp)
set_target_properties(test_engine_ibl_async_lands PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_ibl_async_lands PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_ibl_async_lands PRIVATE JahshakaEngine)
add_dependencies(test_engine_ibl_async_lands JahshakaEngineMedia)
jah_gpu_row(sky.ibl_async_lands CLASS engine FRAMES 200
         COLD "the convolution's permutations must be cold in the process: the fixture's shaderCacheOff"
         COMMAND test_engine_ibl_async_lands
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(sky.ibl_async_lands PROPERTIES TIMEOUT 60 LABELS "engine")
# sky.atmosphere_deferred_layout (SHADER-SYSTEM-1 7.0; the train's validation red) - A VIEW DRAWN WHILE THE ATMOSPHERE's
# TABLES ARE DEFERRED IS VALID VULKAN AND SAMPLES A DEFINED PLACEHOLDER: no shader cache, an asynchronous offscreen view on
# the Streaming pace, the service held 20 ms a job, an atmosphere sky; under the validation layer (its presence proved in
# the binary, JAH_EXPECT_VALIDATION): the deferral met, the sky pixel (0,0,0) before the first build, the tables land, and
# no "Validation Error" in the run (test_engine_atmosphere_deferred_layout.cpp says the laws).
add_executable(test_engine_atmosphere_deferred_layout test_engine_atmosphere_deferred_layout.cpp)
set_target_properties(test_engine_atmosphere_deferred_layout PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_atmosphere_deferred_layout PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_atmosphere_deferred_layout PRIVATE JahshakaEngine)
add_dependencies(test_engine_atmosphere_deferred_layout JahshakaEngineMedia)
jah_gpu_row(sky.atmosphere_deferred_layout CLASS engine FRAMES 200
         COLD "the table jobs' permutations must be cold in the process: the fixture's shaderCacheOff"
         COMMAND test_engine_atmosphere_deferred_layout
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(sky.atmosphere_deferred_layout PROPERTIES TIMEOUT 60 LABELS "engine"
    ENVIRONMENT "VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation;JAH_EXPECT_VALIDATION=1"
    FAIL_REGULAR_EXPRESSION "Validation Error")
# engine.smaa_up_front (SHADER-SYSTEM-1 7a, the SMAA item) - NO SMAA PROGRAM IS COMPILED IN A FRAME: its own shader
# cache, emptied at every run (an app's first launch), an offscreen Live view compiling in the background with the chain allowed offscreen; SMAA asked at preset 2, then 0, after
# the chain settled: 0 shaders compiled off the background compiler in either phase (every preset compiled where the
# engine parses the materials). Prints the SMAA pipelines each phase built in its frames (test_engine_smaa_up_front.cpp).
add_executable(test_engine_smaa_up_front test_engine_smaa_up_front.cpp)
set_target_properties(test_engine_smaa_up_front PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_smaa_up_front PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_smaa_up_front PRIVATE JahshakaEngine)
add_dependencies(test_engine_smaa_up_front JahshakaEngineMedia)
jah_gpu_row(engine.smaa_up_front CLASS engine FRAMES 380
         COLD "its subject is the SMAA programs compiling: they must be cold in the process (the fixture's own cache, emptied at its start)"
         COMMAND test_engine_smaa_up_front
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.smaa_up_front PROPERTIES TIMEOUT 60 LABELS "engine")
# engine.smaa_preset_picture (SHADER-SYSTEM-1 7a, R1/R3) - AN SMAA PRESET CHANGE REACHES THE PICTURE: four fresh engines
# (0->3, 3->0, 2->0, off->3), each switched picture equal to the fresh picture of its preset, exact codes
# (test_engine_smaa_up_front.cpp `picture`).
jah_gpu_row(engine.smaa_preset_picture CLASS engine FRAMES 32
         COMMAND test_engine_smaa_up_front picture
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.smaa_preset_picture PROPERTIES TIMEOUT 60 LABELS "engine")
# sky.clones_share_pipelines (SHADER-SYSTEM-1 7a, R1) - A SECOND SCENE's SKY BUILDS NO LOW-LEVEL PIPELINE: the per-scene
# clones of the sky, sun disc and height fog materials share one pipeline (the low-level key is the content), with
# scene A's builds as the control (test_engine_sky_clones_share.cpp).
add_executable(test_engine_sky_clones_share test_engine_sky_clones_share.cpp)
set_target_properties(test_engine_sky_clones_share PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_sky_clones_share PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_sky_clones_share PRIVATE JahshakaEngine)
add_dependencies(test_engine_sky_clones_share JahshakaEngineMedia)
jah_gpu_row(sky.clones_share_pipelines CLASS engine FRAMES 8
         COMMAND test_engine_sky_clones_share
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(sky.clones_share_pipelines PROPERTIES TIMEOUT 60 LABELS "engine")
# sky.ibl_complete_joins (SHADER-SYSTEM-1 7a, R2 (b)) - A COMPLETE FRAME's CONVOLUTION BUILDS NO PIPELINE: an asynchronous
# scene requests its convolution clones' pipelines as a set when they are made and a Complete frame joins them (counted);
# a synchronous scene is the control (test_engine_ibl_complete_joins.cpp).
add_executable(test_engine_ibl_complete_joins test_engine_ibl_complete_joins.cpp)
set_target_properties(test_engine_ibl_complete_joins PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_ibl_complete_joins PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_ibl_complete_joins PRIVATE JahshakaEngine)
add_dependencies(test_engine_ibl_complete_joins JahshakaEngineMedia)
jah_gpu_row(sky.ibl_complete_joins CLASS engine FRAMES 4
         COMMAND test_engine_ibl_complete_joins
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(sky.ibl_complete_joins PROPERTIES TIMEOUT 60 LABELS "engine")
# engine.placeholders_door (SHADER-SYSTEM-1 7.0, the landing's red gi.card_build_late.pso) - THE PLACEHOLDERS-ONLY TEST DOOR
# REACHES THE COMPILER FROM ANY CALL ORDER: set before any view, the compiler holds it after a view and a frame; off after
# the setter(false); the control, set with the view existing (test_engine_placeholders_door.cpp). 4 frames.
add_executable(test_engine_placeholders_door test_engine_placeholders_door.cpp)
set_target_properties(test_engine_placeholders_door PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_placeholders_door PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_placeholders_door PRIVATE JahshakaEngine)
add_dependencies(test_engine_placeholders_door JahshakaEngineMedia)
jah_gpu_row(engine.placeholders_door CLASS engine FRAMES 4
         COMMAND test_engine_placeholders_door
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.placeholders_door PROPERTIES TIMEOUT 60 LABELS "engine")
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
jah_gpu_row(frame.monitor_pop CLASS engine FRAMES 280
         COLD "the save arm needs shaders this session compiled: its own cache dir, wiped every run (a warm cache saves nothing)"
         COMMAND test_engine_monitor_pop
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.monitor_pop PROPERTIES TIMEOUT 60 LABELS "engine")
jah_tsan_render(engine.error_door frame.monitor_pop)

# frame.stillness_monitor (LAB-SCRIPTS-1's feature end, the lead's ruling (b)) — A STILLNESS RUN LEAVES THE FRAME MONITOR AT
# THE LEVEL IT FOUND in the app's shape (render thread, host records): the host's record and the applied level equal
# before, during and after a run, twice in one session, and a host's capture left running (test_engine_stillness_monitor.cpp
# says the laws). An empty scene's 3 frames, then three runs' frames.
add_executable(test_engine_stillness_monitor test_engine_stillness_monitor.cpp)
set_target_properties(test_engine_stillness_monitor PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_engine_stillness_monitor PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_stillness_monitor PRIVATE JahshakaEngine)
add_dependencies(test_engine_stillness_monitor JahshakaEngineMedia)
jah_gpu_row(frame.stillness_monitor CLASS engine FRAMES 19
         COMMAND test_engine_stillness_monitor
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.stillness_monitor PROPERTIES TIMEOUT 60 LABELS "engine")

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
jah_gpu_row(engine.gpuscene_identity CLASS engine COMMAND test_engine_gpu_walk_parity --compose-identity
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.gpuscene_identity PROPERTIES TIMEOUT 60 LABELS "engine"
    JAHSHAKA_TEST_FRAMES 18)   # 1 + 6 warm + 10 + 1
jah_tsan_render(engine.gpuscene_identity)

option(JAHSHAKA_ASAN "Also build the engine tests with AddressSanitizer" OFF)
if(JAHSHAKA_ASAN)
    # A private, sanitised copy of the backend: the app's JahshakaEngine stays clean. OUT OF THE DEFAULT BUILD (ENGINE-HEADERS-1
    # stage 0: 53 of the 376 objects an edit of Types.h recompiled): built through the `*_asan` executables' on-demand targets.
    add_library(JahshakaEngineAsan STATIC EXCLUDE_FROM_ALL ${JAHSHAKA_ENGINE_SOURCES})   # (the script host: its own archive)
    target_include_directories(JahshakaEngineAsan
        PUBLIC ${CMAKE_SOURCE_DIR}/irisgl/engine/include
        PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src ${JAHSHAKA_ENGINE_BUILD_ID_DIR})
    # IrisCore as the real library links it (NOQT-CORE-1): the caches' file I/O and core/threadbudget.h's include root.
    target_link_libraries(JahshakaEngineAsan PRIVATE OgreNext ${X11_LIBRARIES} IrisCore)   # (qjs is the script host's: JahshakaScript)
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
    jah_on_demand(test_engine_asan)   # out of the default build: built by the rows that run it (tests/CMakeLists.txt)
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
    # TESTING-CLEANUP-2B item 2: TIMEOUT 60 (URGENT #48: was 430) = 6 x the run log's quiet median 71.1 s (40 quiet PASS runs since 2026-10-01, worst 88.4 s);
    # declared 420 was below the policy, honest only with the +900 s widening (now deleted).
    # STAGE-CLOSE (TESTING-SPEED-B3): an ASAN twin keeps its row and moves to the stage close; test_engine
    # (the same suite, uninstrumented) is the merge tier's.
        # TIMEOUT 60 (URGENT #48: was 120) = the floor (6 x the solo runs 9.3 / 18.1 s are under it; TEST-CUT-1 stage 3, 2026-10-20)
        set_tests_properties(test_engine_asan.part${_part} PROPERTIES
            ENVIRONMENT "${JAH_ASAN_ENGINE_OPTIONS}" TIMEOUT 60 LABELS "engine")
    endforeach()
    target_compile_options(test_engine_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(test_engine_asan PRIVATE -fsanitize=address)

    # jahengine_asan (SCRIPT-IN-ENGINE-1 2f) - THE SCRIPTED ENGINE UNDER AddressSanitizer: jahengine's own main.cpp over
    # the script host's ASan twin (JahshakaScriptAsan: the host's archive, JAHSHAKA_ENGINE_SCRIPT_SOURCES, sanitised too -
    # a use-after-free in a command or the registry is a red, not only one in the engine) over JahshakaEngineAsan, and the
    # MCP adapter's twin (jahengine links it). Built ON DEMAND (out of the default build) by the rows that run it -
    # atom.component_asan (tests/atom: component.js on jahengine_asan, label `long`).
    add_library(JahshakaScriptAsan STATIC EXCLUDE_FROM_ALL ${JAHSHAKA_ENGINE_SCRIPT_SOURCES})
    target_include_directories(JahshakaScriptAsan PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src)
    target_link_libraries(JahshakaScriptAsan PUBLIC JahshakaEngineAsan PRIVATE qjs)
    set_target_properties(JahshakaScriptAsan PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
                                                        CXX_STANDARD_REQUIRED ON)
    target_compile_options(JahshakaScriptAsan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    get_target_property(_mcp_sources JahshakaEngineMcp SOURCES)   # (absolute: irisgl/engine/CMakeLists.txt names them so)
    add_library(JahshakaEngineMcpAsan STATIC EXCLUDE_FROM_ALL ${_mcp_sources})
    target_include_directories(JahshakaEngineMcpAsan PUBLIC ${CMAKE_SOURCE_DIR}/irisgl/engine/mcp/include)
    target_link_libraries(JahshakaEngineMcpAsan PUBLIC JahshakaScriptAsan)
    set_target_properties(JahshakaEngineMcpAsan PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
                                                           CXX_STANDARD_REQUIRED ON)
    target_compile_options(JahshakaEngineMcpAsan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    add_executable(jahengine_asan ${CMAKE_SOURCE_DIR}/irisgl/engine/jahengine/main.cpp)
    set_target_properties(jahengine_asan PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
        BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "$ORIGIN")
    # jahengine's own two (irisgl/engine/CMakeLists.txt), ON ITS SOURCE in this directory: a target-wide path define is
    # moved onto the target's sources under tests/ and dropped elsewhere (tests/CMakeLists.txt, jah_scope_path_defines)
    set_property(SOURCE ${CMAKE_SOURCE_DIR}/irisgl/engine/jahengine/main.cpp APPEND PROPERTY COMPILE_DEFINITIONS
                 JAHENGINE_OGRE_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHENGINE_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
    target_link_libraries(jahengine_asan PRIVATE JahshakaScriptAsan JahshakaEngineMcpAsan)
    add_dependencies(jahengine_asan JahshakaEngineMedia)
    target_compile_options(jahengine_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(jahengine_asan PRIVATE -fsanitize=address)
    jah_on_demand(jahengine_asan)   # out of the default build: built by the rows that run it (tests/CMakeLists.txt)
endif()

# engine.chain_reflect_sources / engine.reflect_chain_shape — THE REFLECTION CHAIN's RULES as pure functions (PHOTON-7-9A;
# irisgl/engine/src/ReflectChainRules.h, test_chain_reflect.cpp says the laws): the hybrid's screen sources (mono and the
# SSR row on; a stereo chain none - VR's one explicit difference), the raster march, the screen trace's source and the
# composed picture over every combination; and the stereo shape a recording must find (a mismatch is refused and
# counted, never a silent frame). The header only: no device, no Ogre - CPU code rows, one per law.
add_executable(test_chain_reflect test_chain_reflect.cpp)
set_target_properties(test_chain_reflect PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(test_chain_reflect PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src)
add_test(NAME engine.chain_reflect_sources COMMAND test_chain_reflect sources)
add_test(NAME engine.reflect_chain_shape COMMAND test_chain_reflect shape)
set_tests_properties(engine.chain_reflect_sources engine.reflect_chain_shape PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.chain_reflect_sources)
jah_no_display(engine.reflect_chain_shape)

# engine.route_declarations.forget / .exit — THE ROUTE DECLARATIONS' LIFETIME (PHOTON-7-9A, the reader's audit of R7;
# test_route_declarations.cpp says the laws): a destroyed view's pending declarations are dropped (no read after free),
# each a FAIL by name; one still pending at the process's end exits 1 by name. The helper header only, no engine created:
# CPU code rows, one per law.
add_executable(test_route_declarations test_route_declarations.cpp)
set_target_properties(test_route_declarations PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(test_route_declarations PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/include)
add_test(NAME engine.route_declarations.forget COMMAND test_route_declarations forget)
add_test(NAME engine.route_declarations.exit COMMAND test_route_declarations exit)
set_tests_properties(engine.route_declarations.forget engine.route_declarations.exit PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.route_declarations.forget)
jah_no_display(engine.route_declarations.exit)

# FRAMES — THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B3; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, measured 2026-10-12 at the lane's tip by spikes/testing-speed-b3/framecount (an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded, summed over the row's processes); 0 = a row that renders nothing.
foreach(_row_frames
        "decals.engine=60"
        "test_engine=2903"              # +21: the monitor reason case's L turned in its own box (PHOTON-7 stage 10)
        "test_engine_asan.part1=490"    # TEST-CUT-1: its plain twin's half (test_engine --part 1/2, the meter, 2026-10-20)
        "test_engine_asan.part2=2437"   # (test_engine --part 2/2: the heavy-frame and the monitor cases are here; +21 the L)
        "test_engine_recreate=48"
        )
    string(REPLACE "=" ";" _rf "${_row_frames}")
    list(GET _rf 0 _r)
    list(GET _rf 1 _n)
    if(TEST ${_r})
        set_tests_properties(${_r} PROPERTIES JAHSHAKA_TEST_FRAMES ${_n})
    endif()
endforeach()

# engine.moving_sample — THE MOVING SAMPLE INDEX's ONE RULE as a pure function (TEST-STREAMLINE-1 3c, URGENT #157;
# test_moving_sample.cpp says the law): a scene bind and a size change restart a view's moving index, a camera cut and a
# held frame keep it, N frames on scene A then a bind to B index exactly as a fresh view on B. The header only: no device,
# no Ogre - a CPU code row.
add_executable(test_moving_sample test_moving_sample.cpp)
set_target_properties(test_moving_sample PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(test_moving_sample PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src)
add_test(NAME engine.moving_sample COMMAND test_moving_sample)
set_tests_properties(engine.moving_sample PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.moving_sample)

# script.host — THE ENGINE'S SCRIPT HOST, ITS LAWS ON THE CPU (SCRIPT-IN-ENGINE-1 stage 1; test_script_host.cpp says
# them): the value's kinds, the documented commands, the strict call (an unknown name, a surplus argument, an undeclared
# member, a missing required argument, a non-integral integer FAIL by name), fail against refuse. No card, no display:
# a code row - the engine commands' laws on ONE headless (NULL) engine's real scenes and nodes (2b; the
# engine.exit_only_teardown pattern: the harness's config, its exit-only teardown).
add_executable(test_script_host test_script_host.cpp)
set_target_properties(test_script_host PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_link_libraries(test_script_host PRIVATE JahshakaScript)
# the route declaration's judgement (irisgl/engine/src/script/RouteDeclaration.h: pure, engine-private) proved on the CPU
target_include_directories(test_script_host PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src ${CMAKE_SOURCE_DIR}/tests/support)
target_compile_definitions(test_script_host PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
add_dependencies(test_script_host JahshakaEngineMedia)
add_test(NAME script.host COMMAND test_script_host WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(script.host PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(script.host)

# script.commands — THE ENGINE COMMANDS' FAMILY CASES, ON THE CPU (SCRIPT-IN-ENGINE-1 stage 2; test_script_commands.cpp says
# its generic laws, script_commands.h the fixture): every family registered for real (validate empty, every command tagged
# and documented, >= 31), a test door refused at run time, then ONE CASES FILE A FAMILY (each its builder's own: the batch
# never meets in one test file) on a host with no engine and one on ONE headless (NULL) engine. No card, no display.
add_executable(test_script_commands test_script_commands.cpp script_record_shapes.cpp
    commands_cases_scene.cpp commands_cases_node.cpp commands_cases_light.cpp commands_cases_material.cpp
    commands_cases_effects.cpp commands_cases_view.cpp commands_cases_picture.cpp commands_cases_gi.cpp
    commands_cases_gi_read.cpp commands_cases_engine.cpp commands_cases_atom.cpp commands_cases_status.cpp)
set_target_properties(test_script_commands PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_link_libraries(test_script_commands PRIVATE JahshakaScript)
target_include_directories(test_script_commands PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src ${CMAKE_SOURCE_DIR}/tests/support)
target_compile_definitions(test_script_commands PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
add_dependencies(test_script_commands JahshakaEngineMedia)
add_test(NAME script.commands COMMAND test_script_commands WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(script.commands PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(script.commands)

# engine.setters_refuse / engine.setters_refuse.posted — THE ENGINE's SETTERS REFUSE BY NAME
# (SCRIPT-IN-ENGINE-1 2e e1; OPEN_REDS:44; test_setters_refuse.cpp says the laws). C++ on purpose (the script host shields
# scripts; only C++ reaches the engine's own path - the mirror's). ONE headless (NULL) engine a row, no card, no display:
# the ten node setters, the VR node lists, a missing IES profile and colour ramp in lockstep; the ten POSTED
# (EngineConfig::hostRecords) published as refusals.
add_executable(test_setters_refuse test_setters_refuse.cpp)
set_target_properties(test_setters_refuse PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_link_libraries(test_setters_refuse PRIVATE JahshakaEngine)
target_include_directories(test_setters_refuse PRIVATE ${CMAKE_SOURCE_DIR})   # irisgl/mirror/scenecommands.h: the app's route
target_compile_definitions(test_setters_refuse PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
add_dependencies(test_setters_refuse JahshakaEngineMedia)
add_test(NAME engine.setters_refuse COMMAND test_setters_refuse setters WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME engine.setters_refuse.posted COMMAND test_setters_refuse posted WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.setters_refuse engine.setters_refuse.posted PROPERTIES
    TIMEOUT 60 LABELS "engine")
jah_no_display(engine.setters_refuse)
jah_no_display(engine.setters_refuse.posted)

# script.view_ownership (SCRIPT-IN-ENGINE-1 2b b2) - THE OWNERSHIP LAW's views half (a view needs a device): a view a
# script's command made is destroyed at its host's end; a view the host's owner handed in survives it and is not the
# script's to destroy. The read: a view's name is unique among live views. No frame drawn.
add_executable(test_script_view_ownership test_script_view_ownership.cpp)
set_target_properties(test_script_view_ownership PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_script_view_ownership PRIVATE ${CMAKE_SOURCE_DIR}/tests/support)
target_compile_definitions(test_script_view_ownership PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_script_view_ownership PRIVATE JahshakaScript)
add_dependencies(test_script_view_ownership JahshakaEngineMedia)
jah_gpu_row(script.view_ownership CLASS engine FRAMES 0 COMMAND test_script_view_ownership
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(script.view_ownership PROPERTIES TIMEOUT 60 LABELS "engine")

# script.batch_equals_single (SCRIPT-IN-ENGINE-1 2a a4) — ONE engine.node.setTransforms CALL PUTS N NODES WHERE N
# engine.node.setTransform CALLS PUT THEM: twin scenes of 64 cubes (+ one set back to the identity, every part left out),
# placed each way, their pictures byte-equal; the control: the cubes are drawn. A script run by jahengine. 32 frames.
jah_gpu_row(script.batch_equals_single CLASS engine FRAMES 32
         COMMAND jahengine --script "${CMAKE_CURRENT_SOURCE_DIR}/script_batch_equals_single.js" --name script.batch_equals_single
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(script.batch_equals_single PROPERTIES TIMEOUT 60 LABELS "engine")

# script.radiance_picture (SCRIPT-IN-ENGINE-1 stage 2 B1; RULED 6 and 10) — THE RADIANCE PICTURE READS THE SCENE's
# LINEAR RADIANCE: a Lambert patch under a uniform ambient reads albedo x ambient within the half float's rounding
# (2^-11 relative), the patch above zero (the control), frame 16 equal to frame 60 (the settle's bound). A script run by
# jahengine (readPixels' content "radiance", engine.radiance.mean). 60 frames.
jah_gpu_row(script.radiance_picture CLASS engine FRAMES 60
         COMMAND jahengine --script "${CMAKE_CURRENT_SOURCE_DIR}/script_radiance_picture.js" --name script.radiance_picture
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(script.radiance_picture PROPERTIES TIMEOUT 60 LABELS "engine")

# script.trace_rays_mask (SCRIPT-IN-ENGINE-1 2f, the train's card: P4's red, the lead's ruling) — engine.gi.traceRays's
# INSTANCE MASK IS ITS BITS: a ray down onto a floor 5 m below hits at 5 m with mask 31 (within a millimetre), and misses
# with mask 0 (the control). A script run by jahengine. 4 frames.
jah_gpu_row(script.trace_rays_mask CLASS engine FRAMES 4
         COMMAND jahengine --script "${CMAKE_CURRENT_SOURCE_DIR}/script_trace_rays_mask.js" --name script.trace_rays_mask
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(script.trace_rays_mask PROPERTIES TIMEOUT 60 LABELS "engine")

# script.reflector_attach_quiet (SCRIPT-IN-ENGINE-1 2f, the train's card: P14, the lead's ruling) — A MESH ATTACHED TO A
# REFLECTOR THAT CANNOT RE-ARM LEAVES NO ERROR: engine.node.attachMesh on a flagged reflector node made two-sided answers
# true, leaves the engine's error sink empty and publishes no refusal (OgreMaterials.cpp attachMesh's restore: the mirror
# retries its pushes), its re-arm truly failed (active planes 0); THE CONTROL: the plate un-flagged and armed again is
# refused in the reflector's own words. (A verb that DISARMS an armed reflector leaves the reflector's refusal by design:
# CULL-TWIN-DEBTS-1, engine.planar_asan.) A script run by jahengine. 10 frames (3 + 3 + 3 + 1).
jah_gpu_row(script.reflector_attach_quiet CLASS engine FRAMES 10
         COMMAND jahengine --script "${CMAKE_CURRENT_SOURCE_DIR}/script_reflector_attach_quiet.js" --name script.reflector_attach_quiet
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(script.reflector_attach_quiet PROPERTIES TIMEOUT 60 LABELS "engine")

# script.json — THE SCRIPT VALUE IN JSON, ITS LAWS ON THE CPU (MCP-ON-HOST-1 stage 1; test_script_json.cpp says them):
# every kind round-trips with its kind, numbers by their bits, strict UTF-8, the text's shape, bytes as {"$bytes"} capped
# at 1 MiB. The codec alone (jahshaka/engine/ScriptJson.h): no device, a code row.
add_executable(test_script_json test_script_json.cpp)
set_target_properties(test_script_json PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_link_libraries(test_script_json PRIVATE JahshakaScript)
add_test(NAME script.json COMMAND test_script_json)
set_tests_properties(script.json PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(script.json)

# mcp.schema — THE SIGNATURE AS A SCHEMA, ON THE CPU (MCP-ON-HOST-1 stage 1; test_mcp_schema.cpp says the laws): every
# Param form to its expected text, every registered command (no engine) to a structurally valid schema with no unmapped
# field, the three real signatures byte for byte, an unmappable field a named fault. No device: a code row.
add_executable(test_mcp_schema test_mcp_schema.cpp)
set_target_properties(test_mcp_schema PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_link_libraries(test_mcp_schema PRIVATE JahshakaEngineMcp)
add_test(NAME mcp.schema COMMAND test_mcp_schema)
set_tests_properties(mcp.schema PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(mcp.schema)

# mcp.session — THE MCP SESSION ON THE CPU (MCP-ON-HOST-1 stage 2; test_mcp_session.cpp says the laws): the protocol
# (revision 2026-07-28), the five tools, the lazy engine host, cancel and progress - the transport-free Session over
# synthetic hosts. No device: a code row.
add_executable(test_mcp_session test_mcp_session.cpp)
set_target_properties(test_mcp_session PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_link_libraries(test_mcp_session PRIVATE JahshakaEngineMcp JahshakaEngine)
add_test(NAME mcp.session COMMAND test_mcp_session)
set_tests_properties(mcp.session PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(mcp.session)

# engine.hit_list_law — THE REFLECTION REST's RECORD ALLOWANCE as a pure function (URGENT #157; test_hit_list_law.cpp says
# the law): a function of the view's target (its hit list's creation capacity) and the rest's batching count, never of
# how large the list once grew; a list smaller than its creation size caps it. The header only: a CPU code row.
# IT IS ALSO THE ROW OF irisgl 4576b132's detachScene RESET (mHitListRows = 0), and no row of its own exists, for a stated
# reason (the landing audit's F7, the lead's ruling (a), 2026-11-04; card/te9: the host arm hit_list_rebind could not grow
# the list - a 924 x 565 view's demand ~652k records under 825,344 - and was deleted):
# - a SAME-SHAPE re-attach keeps the old workspace definition's rows: OgreView::setScene rebuilds the definition only on
#   sceneShapeMoved() (OgreView.cpp:1027), and hitListRows is not shape (OgreChain.cpp:695; the list's rows,
#   OgreChain.cpp:2177);
# - the allowance reads min(the list as built, the creation capacity) (HitListLaw.h, this row), so the PICTURE never
#   depends on the rows;
# - the reset shows only at the next definition rebuild after a shape change in the new scene.
# The history-dependent list SIZE (memory, not picture) is the lead's declared item; its fixer is a builder (the list
# re-made at max(0, create) on a re-attach).
# engine.compute_record — THE COMPUTE LAYER's RECORD AND ITS TWO KEYS (COLD-BOOT-1 stage 3; test_compute_record.cpp says
# why): the round trip through compute.bin's bytes, the input key moving with exactly the content a parse reads (its
# negative control: the old permutation key's fields merge two jobs differing only in a C++ piece), the pairing law (a
# record whose source does not hash to its program key is rejected), the program key. CPU only: no card, no engine.
add_executable(test_compute_record test_compute_record.cpp)
set_target_properties(test_compute_record PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(test_compute_record PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src)
target_link_libraries(test_compute_record PRIVATE JahshakaEngine)
add_test(NAME engine.compute_record COMMAND test_compute_record)
set_tests_properties(engine.compute_record PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.compute_record)

# engine.compute_layer_prune — THE COMPUTE LAYER's STALE PRUNE RUNS WHERE THE COMPUTE MEDIA IS FOUND (SHADER-SYSTEM-1
# stage 3F, the start preflight's F1; test_compute_layer_prune.cpp says why): the engine's order - the cache's load, then
# the compute media's folders, then the prewarm - keeps a record whose files are staged unchanged and prunes the one whose
# file changed (red before: a prune at load found no file and dropped every record). A NULL Root and a real ShaderCache
# on a scratch directory: no card, no display.
add_executable(test_compute_layer_prune test_compute_layer_prune.cpp)
set_target_properties(test_compute_layer_prune PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
set_source_files_properties(test_compute_layer_prune.cpp PROPERTIES COMPILE_DEFINITIONS QT_NO_KEYWORDS)
target_include_directories(test_compute_layer_prune PRIVATE
    "${CMAKE_SOURCE_DIR}/irisgl/engine/src" "${JAHSHAKA_ENGINE_BUILD_ID_DIR}")
target_compile_definitions(test_compute_layer_prune PRIVATE JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}")
target_link_libraries(test_compute_layer_prune PRIVATE JahshakaEngine OgreNext)
add_test(NAME engine.compute_layer_prune COMMAND test_compute_layer_prune
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.compute_layer_prune PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.compute_layer_prune)

# engine.compute_store_chain — THE COMPUTE LAYER END TO END ON THE CPU (SHADER-SYSTEM-1 stage 3F (c);
# test_compute_store_chain.cpp says why): the engine's adapter between the fork's HlmsCompute records and a real
# ShaderCache, three NULL launches in the engine's order - a built permutation's record is saved, the next launch loads it
# (stale 0, rejected 0: the fork's keys are the ones irisgl re-derives) and dispatches with NO parse; an edited template
# prunes it and parses (the negative control). No card, no display.
add_executable(test_compute_store_chain test_compute_store_chain.cpp)
set_target_properties(test_compute_store_chain PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
set_source_files_properties(test_compute_store_chain.cpp PROPERTIES COMPILE_DEFINITIONS QT_NO_KEYWORDS)
target_include_directories(test_compute_store_chain PRIVATE
    "${CMAKE_SOURCE_DIR}/irisgl/engine/src" "${JAHSHAKA_ENGINE_BUILD_ID_DIR}")
target_compile_definitions(test_compute_store_chain PRIVATE JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}")
target_link_libraries(test_compute_store_chain PRIVATE JahshakaEngine OgreNext)
add_test(NAME engine.compute_store_chain COMMAND test_compute_store_chain
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.compute_store_chain PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.compute_store_chain)

# engine.ray_builder_slots — THE RAY PIPELINE BUILDERS' SLOTS (COLD-BOOT-1 stage 4; RayBuilderSlots.h): the width bounds
# the builders at once; the reflection pair's second builder is taken only if a slot is free NOW (a tier one wide builds
# the pair serially - no width deadlocks); a width below one is one. CPU only: no card, no engine.
add_executable(test_ray_builder_slots test_ray_builder_slots.cpp)
set_target_properties(test_ray_builder_slots PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(test_ray_builder_slots PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src)
find_package(Threads REQUIRED)
target_link_libraries(test_ray_builder_slots PRIVATE Threads::Threads)
add_test(NAME engine.ray_builder_slots COMMAND test_ray_builder_slots)
set_tests_properties(engine.ray_builder_slots PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.ray_builder_slots)

# engine.ray_ahead_door — THE RAY CONSUMER's DOOR (SHADER-SYSTEM-1 stage 7, call 3; RayAheadDoor.h): a build still running
# in a scene on the background compiler is never joined, at any pace; a synchronous scene joins it on purpose; a finished
# build is joined at once. CPU only: no card, no engine.
add_executable(test_ray_ahead_door test_ray_ahead_door.cpp)
set_target_properties(test_ray_ahead_door PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(test_ray_ahead_door PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src)
add_test(NAME engine.ray_ahead_door COMMAND test_ray_ahead_door)
set_tests_properties(engine.ray_ahead_door PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.ray_ahead_door)

# engine.build_burst — THE BUILDS' BURST (SHADER-SYSTEM-1 stage 7; BuildBurst.h): the notice's "n of m" - a burst begins
# when the pending count rises from 0, counts its own landings from its own base, and reads m of m when it ends. CPU only.
add_executable(test_build_burst test_build_burst.cpp)
set_target_properties(test_build_burst PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(test_build_burst PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src)
add_test(NAME engine.build_burst COMMAND test_build_burst)
set_tests_properties(engine.build_burst PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.build_burst)

# engine.exit_only_teardown — THE FIXTURES' EXIT (COLD-BOOT-1's added item; spikes/exit-race/VERIFY.md): a fixture that
# takes the harness's exit-only teardown ends at its engine's death - a handler registered after the boot never runs, and
# the status is what the run returned; its negative control (no engine: the late handler runs). A headless (NULL) engine
# in a forked child: no card, no display.
add_executable(test_exit_only_teardown test_exit_only_teardown.cpp)
set_target_properties(test_exit_only_teardown PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_exit_only_teardown PRIVATE ${CMAKE_SOURCE_DIR}/tests/support)
target_compile_definitions(test_exit_only_teardown PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_exit_only_teardown PRIVATE JahshakaEngine)
add_dependencies(test_exit_only_teardown JahshakaEngineMedia)
add_test(NAME engine.exit_only_teardown COMMAND test_exit_only_teardown WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.exit_only_teardown PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.exit_only_teardown)

add_executable(test_hit_list_law test_hit_list_law.cpp)
set_target_properties(test_hit_list_law PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(test_hit_list_law PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src)
add_test(NAME engine.hit_list_law COMMAND test_hit_list_law)
set_tests_properties(engine.hit_list_law PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.hit_list_law)

# engine.compute_job_slots (NONDET-1 1a, fork items iii and iii-b) — A COMPUTE JOB'S SLOT THAT CHANGES TYPE KEEPS ITS
# LISTENERS TRUE: a buffer over a texture slot stops the job listening to that texture; a texture over a buffer slot calls
# nothing on the buffer (the slot is a union). The fork's HlmsComputeJob on the NULL render system: a CPU code row.
add_executable(test_compute_job_slots test_compute_job_slots.cpp)
set_target_properties(test_compute_job_slots PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_compute_job_slots PRIVATE JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}")
target_link_libraries(test_compute_job_slots PRIVATE OgreNext)
add_test(NAME engine.compute_job_slots COMMAND test_compute_job_slots WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.compute_job_slots PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.compute_job_slots)

# engine.compute_record_hit (SHADER-SYSTEM-1 stage 3F (b), the fork half of COLD-BOOT-1 stage 3) — THE COMPUTE CODE
# RECORDS in the fork's HlmsCompute (test_compute_record_hit.cpp says each law): no hook = upstream exactly; a clone of
# equal inputs hits its record (no parse); the hook's key is the only key; a record whose source does not hash to its
# program key is not filed; clearShaderCache clears the file memo; equal text with slots of other types never shares a
# program (F2a); the render system's properties are the key's (F2b); a second launch's prewarmed code serves the first
# dispatch with no parse, and one made BEFORE that code is published joins its code job (no parse: the warm boot's
# atmosphere dispatches); a shared job's asynchronous publish applies and consumes none of its values - each program gets them whole at
# its own first dispatch (stage 7a: the voxel clear's fClearValue ITEM_NOT_FOUND). Four NULL Roots in turn: a CPU code row.
add_executable(test_compute_record_hit test_compute_record_hit.cpp)
set_target_properties(test_compute_record_hit PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_compute_record_hit PRIVATE JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}")
target_link_libraries(test_compute_record_hit PRIVATE OgreNext)
add_test(NAME engine.compute_record_hit COMMAND test_compute_record_hit WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.compute_record_hit PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.compute_record_hit)

# engine.gpu_params_autos (SHADER-SYSTEM-1 7a, R3) — A PROGRAM's AUTO CONSTANTS SURVIVE A COPY BY NAME (the fork's
# copyMatchingNamedConstantsFrom: a program reload, a twin program; test_gpu_params_autos.cpp says each law). CPU, no Root.
add_executable(test_gpu_params_autos test_gpu_params_autos.cpp)
set_target_properties(test_gpu_params_autos PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_link_libraries(test_gpu_params_autos PRIVATE OgreNext)
add_test(NAME engine.gpu_params_autos COMMAND test_gpu_params_autos WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.gpu_params_autos PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.gpu_params_autos)

# engine.shader_cache_keys (SHADER-SYSTEM-1 stage 5) — ONLY A PRODUCER DROPS A LAYER (test_shader_cache_keys.cpp says
# why): the four layer keys as one function of their inputs - the pipeline key's text byte-identical to the one it
# replaced (F5's golden case), each key moving on each of its producer terms and on no other key's, none holding the
# fork's commit or a media tree (NEGATIVE CONTROL: today's composition moves on both), a preprocessor change dropping
# compute records and not recipes; the engine's media digest (law 5: content, the file set, '/' paths, bytewise order).
# CPU only: no card, no engine.
add_executable(test_shader_cache_keys test_shader_cache_keys.cpp)
set_target_properties(test_shader_cache_keys PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_shader_cache_keys PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src)
target_link_libraries(test_shader_cache_keys PRIVATE JahshakaEngine OgreNext)
add_test(NAME engine.shader_cache_keys COMMAND test_shader_cache_keys)
set_tests_properties(engine.shader_cache_keys PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.shader_cache_keys)

# engine.shader_cache_uses.{microcode,hlms,compute} (SHADER-SYSTEM-1 F10, the prune's law of the final read's rulings) —
# AGE ALONE NEVER DROPS AN ENTRY (test_shader_cache_uses.cpp says why): per layer, through its real path and a real
# ShaderCache - 100 launches that use nothing drop nothing; over the budget the least recently used go and the just-used
# stay, the backstop wipes only past this launch's own entries; a launch that compiled nothing writes no layer and no
# manifest (the sidecar alone); a torn sidecar prunes nothing and is rewritten whole; and in the hlms row the load seeds
# the template checksum with the preprocessor group's salt (the layer's guard of a changed preprocessor). NULL Roots: CPU
# code rows.
add_executable(test_shader_cache_uses test_shader_cache_uses.cpp)
set_target_properties(test_shader_cache_uses PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
set_source_files_properties(test_shader_cache_uses.cpp PROPERTIES COMPILE_DEFINITIONS QT_NO_KEYWORDS)
target_include_directories(test_shader_cache_uses PRIVATE
    "${CMAKE_SOURCE_DIR}/irisgl/engine/src" "${JAHSHAKA_ENGINE_BUILD_ID_DIR}" "${CMAKE_SOURCE_DIR}")   # "irisgl/core/files.h"
target_compile_definitions(test_shader_cache_uses PRIVATE JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}")
target_link_libraries(test_shader_cache_uses PRIVATE JahshakaEngine OgreNext IrisCore)
foreach(_layer microcode hlms compute)
    add_test(NAME engine.shader_cache_uses.${_layer} COMMAND test_shader_cache_uses --${_layer}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(engine.shader_cache_uses.${_layer} PROPERTIES TIMEOUT 60 LABELS "engine")
    jah_no_display(engine.shader_cache_uses.${_layer})
endforeach()
# engine.shader_cache_temps (NOQT-CORE-1 stage 1, the engine link) - A KILLED WRITE's TEMP IS SWEPT AT THE NEXT OPEN, A
# LIVE WRITER's NEVER (test_shader_cache_uses.cpp, law 6): forked writers through iris::files' staged primitives, one
# killed and one alive across a real ShaderCache's open on a NULL Root. CPU code row.
add_test(NAME engine.shader_cache_temps COMMAND test_shader_cache_uses --temps WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.shader_cache_temps PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.shader_cache_temps)

# engine.template_checksum_salt (SHADER-SYSTEM-1 stage 5, ruling 2) — THE PREPROCESSOR GUARD IS OGRE's OWN RE-PARSE
# (test_template_checksum_salt.cpp says why): the fork's per-Hlms salt moves the template checksum (zero = upstream's), and
# the checksum covers a library's pieces and the template - what the hlms layer's key no longer hashes as a media tree. A
# real HlmsUnlit on the NULL render system over scratch folders: a CPU code row.
add_executable(test_template_checksum_salt test_template_checksum_salt.cpp)
set_target_properties(test_template_checksum_salt PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_compile_definitions(test_template_checksum_salt PRIVATE JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}")
target_link_libraries(test_template_checksum_salt PRIVATE OgreNext)
add_test(NAME engine.template_checksum_salt COMMAND test_template_checksum_salt
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(engine.template_checksum_salt PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.template_checksum_salt)

# engine.cascade_resolution_law (NONDET-1 D1) — A CASCADE IS BUILT AT A MULTIPLE OF 8 in 16..256, rounded up (Types.h
# giCascadeResolution: the voxel passes run whole bricks with no bound check). The header only: a CPU code row.
add_executable(test_cascade_resolution_law test_cascade_resolution_law.cpp)
set_target_properties(test_cascade_resolution_law PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(test_cascade_resolution_law PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/include)
add_test(NAME engine.cascade_resolution_law COMMAND test_cascade_resolution_law)
set_tests_properties(engine.cascade_resolution_law PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.cascade_resolution_law)

# engine.param_array (NONDET-1 D2) — AN ARRAY PARAMETER OF OURS LONGER THAN 16 ELEMENTS REACHES ITS SHADER WHOLE
# (PhotonParamArray: Ogre's setManualValue keeps 64 bytes and drops the rest). A real ShaderParams::Param: a CPU code row.
add_executable(test_param_array test_param_array.cpp)
set_target_properties(test_param_array PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    BUILD_RPATH   "${OGRE_NEXT_PREFIX}/lib"
    INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_param_array PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src)
target_link_libraries(test_param_array PRIVATE OgreNext)
add_test(NAME engine.param_array COMMAND test_param_array)
set_tests_properties(engine.param_array PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.param_array)

# engine.region_grid_law (NONDET-1 H2) — A PASS OVER A REGION DISPATCHES ONLY THE TEXELS THE REGION HOLDS: an empty or
# past-the-end box is no dispatch (photon/voxel/PhotonRegionGrid.h, the arithmetic setRegionGrid sets its job from). The
# header only: a CPU code row.
add_executable(test_region_grid_law test_region_grid_law.cpp)
set_target_properties(test_region_grid_law PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(test_region_grid_law PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src)
add_test(NAME engine.region_grid_law COMMAND test_region_grid_law)
set_tests_properties(engine.region_grid_law PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.region_grid_law)

# THE SCENE'S CLOCK (ONE-FRAME-1 stage 1; test_frame_clock.cpp says each law): the pure value type every engine scene
# keeps (irisgl/engine/src/SceneClock.h), driven directly — one binary, one row per proof, CPU only (no engine, no card).
#   frame.clock_once         a serial's steps apply once; an empty frame 0 (red on the base: +0.016667 s); a reassert
#                            keeps the steps, a new document's epoch re-primes (bug 2: red on 8060febd, 0 steps);
#                            the harness steps only never-stamped scenes.
#   frame.two_scenes         two scenes' clocks are independent; a join's count is consumed by the next frame, once.
#   frame.particle_scale     the particles' time = steps x period x particleTimeScale (0.25, 4), the scene's unscaled.
#   frame.clock_drawn_only   an undrawn frame keeps the scene's steps and hands it 0; 36,000 owed arrive as 8 (bug 1).
add_executable(test_frame_clock test_frame_clock.cpp)
set_target_properties(test_frame_clock PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_frame_clock PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src
    ${CMAKE_SOURCE_DIR}/irisgl/engine/include)
foreach(_pair "clock_once;clock-once" "two_scenes;two-scenes" "particle_scale;particle-scale" "clock_drawn_only;drawn-only")
    list(GET _pair 0 _row)
    list(GET _pair 1 _flag)
    add_test(NAME frame.${_row} COMMAND test_frame_clock --${_flag} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    set_tests_properties(frame.${_row} PROPERTIES TIMEOUT 60 LABELS "engine")
    jah_no_display(frame.${_row})
endforeach()
# frame.meter_time — THE HDR METER ADAPTS IN ITS VIEWER's TIME (ONE-FRAME-1 stage 1; test_frame_meter.cpp says the law):
# a live view adapts one presented step a frame (stepped or not, any particle scale); a picture has no history (equal
# exposure after 2 and 200 frames).
# A card row (the meter runs on a view).
add_executable(test_frame_meter test_frame_meter.cpp)
set_target_properties(test_frame_meter PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_frame_meter PRIVATE ${CMAKE_SOURCE_DIR}/tests/support)
target_compile_definitions(test_frame_meter PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_frame_meter PRIVATE JahshakaEngine)
add_dependencies(test_frame_meter JahshakaEngineMedia)
jah_gpu_row(frame.meter_time CLASS engine FRAMES 206 COMMAND test_frame_meter
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.meter_time PROPERTIES TIMEOUT 60 LABELS "engine")
# frame.clock_integration — A FRAME CONSUMES THE STEPS OF THE SCENES IT DRAWS, AND ONLY THEIRS (ONE-FRAME-1, the stage-1
# audit's bug 1; test_frame_integration.cpp says the law): through the real drain and drawn set. A card row.
add_executable(test_frame_integration test_frame_integration.cpp)
set_target_properties(test_frame_integration PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_frame_integration PRIVATE ${CMAKE_SOURCE_DIR}/tests/support)
target_compile_definitions(test_frame_integration PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_frame_integration PRIVATE JahshakaEngine)
add_dependencies(test_frame_integration JahshakaEngineMedia)
jah_gpu_row(frame.clock_integration CLASS engine FRAMES 5 COMMAND test_frame_integration
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.clock_integration PROPERTIES TIMEOUT 60 LABELS "engine")
# THE PARTICLES RUN ON THEIR SCENE's TIME, AND ONLY IN ITS FRAME (ONE-FRAME-1; test_frame_particles.cpp says the laws).
# Card rows (PFX2 on a drawn scene).
#   frame.particles_per_scene    two scenes, +1 and +3 steps a hand-over: 40 and 120 particles (red with the fork's
#                                override line reverted: both on Ogre's one frame time).
#   frame.particles_once_a_frame a pose resolve between frames advances the particles nothing.
add_executable(test_frame_particles test_frame_particles.cpp)
set_target_properties(test_frame_particles PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_frame_particles PRIVATE ${CMAKE_SOURCE_DIR}/tests/support)
target_compile_definitions(test_frame_particles PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_frame_particles PRIVATE JahshakaEngine)
add_dependencies(test_frame_particles JahshakaEngineMedia)
jah_gpu_row(frame.particles_per_scene CLASS engine FRAMES 21 COMMAND test_frame_particles --per-scene
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.particles_per_scene PROPERTIES TIMEOUT 60 LABELS "engine")
jah_gpu_row(frame.particles_once_a_frame CLASS engine FRAMES 11 COMMAND test_frame_particles --resolve
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(frame.particles_once_a_frame PROPERTIES TIMEOUT 60 LABELS "engine")
# engine.rough_lobe_transport — THE ROUGH LOBE's HISTORY IS CARRIED TO THE CURRENT VIEW BEFORE IT IS BLENDED (ROUGH-LOBE-HISTORY-1,
# V2 plan 1.8; test_rough_lobe_transport.cpp says the laws): on irisgl's RoughLobeCarry.h (the shader's one-line update as a
# pure function), the carried mean's expectation is L_t on a linear walk, young and at the floor (the bare blend's closed-form
# lags n dL / 2 and (N - 1) dL are its negative control), and without a carry the history and the mean keep their bits.
# source.rough_lobe_transport pins the shader's lines to the header (and holds the per-eye and one-call-site laws). A CPU
# code row.
add_executable(test_rough_lobe_transport test_rough_lobe_transport.cpp)
set_target_properties(test_rough_lobe_transport PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(test_rough_lobe_transport PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src)
add_test(NAME engine.rough_lobe_transport COMMAND test_rough_lobe_transport)
set_tests_properties(engine.rough_lobe_transport PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.rough_lobe_transport)

# engine.stillness_history_column — THE STILLNESS RECORD CARRIES THE HISTORY's FLOOR h (ROUGH-LOBE-HISTORY-1; the law in
# test_stillness_history_column.cpp): StillnessReport::tsv's history_frames column (GatherStatus::historyFrames) beside
# settle_frames, in both parts of a window - what gi.walk_rough.k<k>'s lag law reads (it inverted the settle before). A CPU
# code row on the record's text: it links the engine, opens no device.
add_executable(test_stillness_history_column test_stillness_history_column.cpp)
set_target_properties(test_stillness_history_column PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
                      BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_link_libraries(test_stillness_history_column PRIVATE JahshakaEngine)
add_test(NAME engine.stillness_history_column COMMAND test_stillness_history_column)
set_tests_properties(engine.stillness_history_column PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.stillness_history_column)
# engine.ray_view_cache — THE RAY TIER's VIEW CACHE as a pure structure (GATHER-HANG-1 stage 1; RayQueryViewCache.h says
# the law, test_ray_view_cache.cpp its thirteen cases): one view per (texture, kind) on one image with one set of
# parameters, retired once when its image goes (LostResidency, Deleted, another image or other parameters, close),
# image 0 refused, a failed make caches nothing, one registration per texture. The header only: no device, no Ogre -
# a CPU code row. Red first: cases 2-5 and 7-9 on stage 0's arm J1 (spikes/gather-hang-1/stage1/), 10-11 on irisgl
# 4baf20de's orphan (spikes/gather-hang-1/stage1-gaps/).
add_executable(test_ray_view_cache test_ray_view_cache.cpp)
set_target_properties(test_ray_view_cache PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(test_ray_view_cache PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src)
add_test(NAME engine.ray_view_cache COMMAND test_ray_view_cache)
set_tests_properties(engine.ray_view_cache PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.ray_view_cache)

# engine.ray_input_guard — A RAY JOB WITH A NULL INPUT VIEW REFUSES ITS DISPATCH AND SAYS SO ONCE (GATHER-HANG-1 stage 2;
# RayInputGuard.h says the law): refused and counted every time, said the first time per job; the forced non-resident
# input (textureView's image 0 -> the cache's null view -> the job refuses). The headers only: a CPU code row. Red first
# on the base's silent decline (spikes/gather-hang-1/stage2/guard-red-first.txt). AND THE ORDER (6b): every ray job of
# the tier calls the guard before the state its frame advances, read in OgreRayQuery.cpp and OgreScreenProbeGather.cpp,
# its job list derived from them (a 12th job red until named; red first on the base:
# spikes/gather-hang-1/stage2/guard-jobs-red-first.txt).
add_executable(test_ray_input_guard test_ray_input_guard.cpp)
set_target_properties(test_ray_input_guard PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(test_ray_input_guard PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src)
add_test(NAME engine.ray_input_guard
         COMMAND test_ray_input_guard "${CMAKE_SOURCE_DIR}/irisgl/engine/src/OgreRayQuery.cpp"
                 "${CMAKE_SOURCE_DIR}/irisgl/engine/src/OgreScreenProbeGather.cpp")
set_tests_properties(engine.ray_input_guard PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.ray_input_guard)

# engine.chain_gi_routes — THE CHAIN'S DIFFUSE-GI ROUTE as a pure function (ROUTE-ROWS-1;
# irisgl/engine/src/GiChainRules.h, test_chain_gi_routes.cpp says the law): ONE WORLD, ONE DIFFUSE PRODUCER PER VIEW - the
# gather wherever the GI is on and the scene traces, in every kind of view (the Live/StillPicture contract's GI meaning gave
# a live offscreen picture of a ray scene the integrate); the integrate only where the scene does not trace, mono; never
# both. The header only: no device, no Ogre - a CPU code row.
add_executable(test_chain_gi_routes test_chain_gi_routes.cpp)
set_target_properties(test_chain_gi_routes PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(test_chain_gi_routes PRIVATE ${CMAKE_SOURCE_DIR}/irisgl/engine/src)
add_test(NAME engine.chain_gi_routes COMMAND test_chain_gi_routes)
set_tests_properties(engine.chain_gi_routes PROPERTIES TIMEOUT 60 LABELS "engine")
jah_no_display(engine.chain_gi_routes)
