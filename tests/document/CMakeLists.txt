# Characterisation tests for the iris:: scene DOCUMENT.
#
# THE NAME AND THE LINK LINE ARE STEP-14 RELICS, retired 2026-09-09
# (TEST_GATE_AUDIT.md §2). This suite was born to prove that constructing a
# document touched no GL: iris::Scene used to load a sky mesh and compile a sky
# shader in its constructor and LightNode used to build a QOpenGLTexture in
# its own, so the suite linked Qt6::OpenGL purely to assert that no context was
# current. The legacy GL renderer was DELETED at step 14 (2026-08-30) — there
# is no GL anywhere in this tree to be accidentally touched, the link against
# Qt6::OpenGL bought nothing, and "no GL context" was a precondition about a
# world that no longer exists. What the body actually does is CHARACTERISE the
# document model, which is what it is now called.
find_package(Qt6 REQUIRED COMPONENTS Gui)
add_executable(test_document_characterisation test_document_characterisation.cpp)
set_target_properties(test_document_characterisation PROPERTIES AUTOMOC OFF)
target_include_directories(test_document_characterisation PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    # iris headers expose bullet types; this is IrisGL's own include path
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_document_characterisation PRIVATE IrisGL Qt6::Gui)
# A model read from a Qt RESOURCE (smoke L10 item 4): the ":/" read path hands
# assimp bytes from memory, the one path where its 200-byte format sniff decides.
qt_add_resources(test_document_characterisation "document_fixtures"
    PREFIX "/document-fixtures" BASE fixtures FILES fixtures/commented_header.obj)
# This suite builds a DOCUMENT, and a document node is an engine node now
# (SPECS/SCENEGRAPH_SPEC.md D2), so it boots the HEADLESS engine — NULL render
# system, no display, no GPU (tests/support/documentgraph.h). jah_no_display
# below runs it with DISPLAY unset, which is the proof of that claim.
jah_document_graph(test_document_characterisation)
add_test(NAME document.characterisation COMMAND test_document_characterisation
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(document.characterisation PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "document" TIMEOUT 60)
jah_no_display(document.characterisation)

# document.mobility — THE RESOLUTION RULE (SPECS/REALTIME_REFLECTIONS_SPEC.md
# §3.3.2): which objects the renderer may bake into the room's lighting and
# which it has to handle per frame. Every driver with its own reason, rule 2's
# parent inheritance, the user's setting (including where a driver openly beats
# it), the "an editor drag does NOT promote" claim the whole design rests on —
# a promotion costs a from-scratch GI rebuild — and the play-scoped soft
# promotion. Document only: NULL render system, no display.
add_executable(test_mobility test_mobility.cpp)
set_target_properties(test_mobility PROPERTIES AUTOMOC OFF)
target_include_directories(test_mobility PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_mobility PRIVATE IrisGL Qt6::Gui)
jah_document_graph(test_mobility)
add_test(NAME document.mobility COMMAND test_mobility
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(document.mobility PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "document" TIMEOUT 60)
jah_no_display(document.mobility)

# The leak lane (scripts/sanitize.sh lsan): the document model itself — every
# node, mesh, material and animation type constructed and dropped. No Vulkan,
# no app process, so LeakSanitizer runs with detect_leaks=1 here.
# QUARANTINED (L2 LightNode::shadowMap, L5 KeyFrame keys) plus one leak of
# the suite's own (test_document_characterisation.cpp:386 news a Vector3DPropertyAnim
# it never deletes). Move to jah_lsan_lane once L2/L5 are fixed.
jah_lsan_blocked(document.characterisation)

# ENGINEERING_DEBT_SPEC A4.2: the ONE fixed-step simulation clock
# (irisgl/document/scenegraph/simulationclock.h). Header-only arithmetic —
# no engine, no Qt event loop, no display — asserting exactly the properties
# scripting.e2e.fixed_clock's byte-identical-across-frame-rates proof rests on:
# whole grid steps, time from an integer step count, rate independence, the
# catch-up bound.
add_executable(test_simulation_clock test_simulation_clock.cpp)
set_target_properties(test_simulation_clock PROPERTIES AUTOMOC OFF)
target_include_directories(test_simulation_clock PRIVATE ${CMAKE_SOURCE_DIR})
add_test(NAME document.simulation_clock COMMAND test_simulation_clock
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(document.simulation_clock PROPERTIES LABELS "document" TIMEOUT 60)
jah_no_display(document.simulation_clock)

# ENGINEERING_DEBT_SPEC A4.2, the restart half: six piled rigid bodies stepped
# on the clock grid, the world torn down and rebuilt as PlayBack does it, four
# times — every cycle lands every body on the same bits. Headless document
# graph, no display.
add_executable(test_physics_restart test_physics_restart.cpp)
set_target_properties(test_physics_restart PROPERTIES AUTOMOC OFF)
target_include_directories(test_physics_restart PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_physics_restart PRIVATE IrisGL Qt6::Gui)
jah_document_graph(test_physics_restart)
add_test(NAME document.physics_restart COMMAND test_physics_restart
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(document.physics_restart PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "document" TIMEOUT 120)
jah_no_display(document.physics_restart)

# document.light_defaults — SHADOWS ON BY DEFAULT and THE ONE SUN RESOLVER
# (SPECS/SUN_AND_LIGHT_DEFAULTS_SPEC.md). The constructor's shadow default, the
# scene's uninitialised master switch, and the whole contract of
# iris::Scene::sunLight(): pin, priority, the TIE, and "this scene has no sun"
# as a normal answer. Document only: NULL render system, no display.
add_executable(test_light_defaults test_light_defaults.cpp)
set_target_properties(test_light_defaults PROPERTIES AUTOMOC OFF)
target_include_directories(test_light_defaults PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_light_defaults PRIVATE IrisGL Qt6::Gui)
jah_document_graph(test_light_defaults)
add_test(NAME document.light_defaults COMMAND test_light_defaults
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(document.light_defaults PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "document" TIMEOUT 60)
jah_no_display(document.light_defaults)

# document.dirty_list — THE DOCUMENT'S HALF of the dirty-set mirror
# (SPECS/DIRTY_SET_MIRROR_SPEC.md §7.3): dedupe, the three kinds that cascade
# over a subtree, eviction (including a node marked and then freed before the
# list was read), scene membership, the overflow valve, and the material
# revision that carries what the node list cannot. Document only: NULL render
# system, no display, no engine.
add_executable(test_dirty_list test_dirty_list.cpp)
set_target_properties(test_dirty_list PROPERTIES AUTOMOC OFF)
target_include_directories(test_dirty_list PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_dirty_list PRIVATE IrisGL Qt6::Gui)
jah_document_graph(test_dirty_list)
add_test(NAME document.dirty_list COMMAND test_dirty_list
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(document.dirty_list PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "document" TIMEOUT 60)
jah_no_display(document.dirty_list)

# document.movement_epoch — THE VIEWER IS NOT THE VIEWED (nodegraph.h's
# transform-write epoch, lane ENGINE-7 item 1). Four O(scene) walks and three
# mirror gates hang off `iris::graph::transformWrites()`; a CAMERA used to bump
# it, so flying re-ran every one of them on every frame (RR2 measured 10.28 ms
# of the mirror's GI push on 92% of flying frames on an 8,404-node lattice,
# against 0.024 ms still). Asserts the rule, the camera exemption through every
# setter a camera is flown with, and the exception — a camera with CHILDREN
# moves them, so it counts again. Document only: NULL render system, no display.
add_executable(test_movement_epoch test_movement_epoch.cpp)
set_target_properties(test_movement_epoch PROPERTIES AUTOMOC OFF)
target_include_directories(test_movement_epoch PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_movement_epoch PRIVATE IrisGL Qt6::Gui)
jah_document_graph(test_movement_epoch)
add_test(NAME document.movement_epoch COMMAND test_movement_epoch
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(document.movement_epoch PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "document" TIMEOUT 60)
jah_no_display(document.movement_epoch)

# document.no_silent_setters — the STATIC half of the dirty-set law
# (SPECS/DIRTY_SET_MIRROR_SPEC.md §3.8 item 3), shaped like theme.no_raw_sheets.
# Since the mirror only looks at a node the DOCUMENT says changed, a setter that
# writes a reflected field and reports nothing is a change that never reaches
# the screen — not a crash, not visible in any pixel test, and healed a second
# or two later by the background verifier, so the only symptom is lateness. This
# greps every `void set*()` under irisgl/document/{scenegraph,materials} for a
# body that assigns a member and never reaches notifyChanged / markedParams /
# touch. It caught thirteen emitter setters and the whole camera lens surface.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME document.no_silent_setters
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/no_silent_setters.py
                     ${CMAKE_SOURCE_DIR})
    set_tests_properties(document.no_silent_setters PROPERTIES LABELS "document" TIMEOUT 60)
    jah_no_display(document.no_silent_setters)
endif()

# document.mesh_cache — ONE FILE IS PARSED ONCE (ADD-1). The parse cache behind
# `Mesh::loadMesh`: the sharing, the weak references that make it not a leak,
# the per-node skeleton clone that is the one thing a shared mesh could have
# aliased, and the rule that a failed load is never cached. Document only: no
# engine, no display.
add_executable(test_mesh_cache test_mesh_cache.cpp)
set_target_properties(test_mesh_cache PROPERTIES AUTOMOC OFF)
target_include_directories(test_mesh_cache PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_mesh_cache PRIVATE IrisGL Qt6::Gui)
qt_add_resources(test_mesh_cache "mesh_cache_fixtures"
    PREFIX "/document-fixtures" BASE fixtures FILES fixtures/commented_header.obj)
add_test(NAME document.mesh_cache COMMAND test_mesh_cache
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(document.mesh_cache PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "document" TIMEOUT 60)
jah_no_display(document.mesh_cache)
