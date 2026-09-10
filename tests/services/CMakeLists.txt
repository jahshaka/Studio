# Service-layer unit tests (APP_ARCHITECTURE_AUDIT §9 Phase 1).
#
# services.core — links ONLY the QObject-free/minimally-Qt services it tests
# (UndoService + SelectionService), no MainWindow, no engine, no document
# construction (SelectionService is exercised through null SceneNodePtrs; the
# guard and re-emit contracts do not depend on a live node). Runs offscreen.
find_package(Qt6 REQUIRED COMPONENTS Gui Widgets)
add_executable(test_services test_services.cpp
    ${CMAKE_SOURCE_DIR}/src/services/undoservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/selectionservice.cpp)
set_target_properties(test_services PROPERTIES AUTOMOC ON)
target_include_directories(test_services PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
   )
# IrisGL since the selection became a SET (EDITOR_MULTISELECT_SPEC §2.1):
# SelectionService orders its members in DOCUMENT pre-order, which means it
# calls into the scene graph. This suite still builds no nodes — it exercises
# the guard and re-emit contracts through null pointers, exactly as before.
target_link_libraries(test_services PRIVATE IrisGL Qt6::Gui Qt6::Widgets)
target_include_directories(test_services PRIVATE
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
add_test(NAME services.core COMMAND test_services
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(services.core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "services")

# services.selection_set — the SET's own contracts (EDITOR_MULTISELECT_SPEC §5).
# Real document nodes, so it boots the headless document graph like every other
# document suite; no display, no GPU.
add_executable(test_selection_set test_selection_set.cpp
    ${CMAKE_SOURCE_DIR}/src/services/selectionservice.cpp
    # ...and the COPY NAMING rule (owner decision 2026-09-09), which is the
    # other half of "what a set edit produces". Another leaf: QString plus
    # SceneNode::children, no services, no engine.
    ${CMAKE_SOURCE_DIR}/src/services/nodenaming.cpp)
set_target_properties(test_selection_set PROPERTIES AUTOMOC ON
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_selection_set PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_selection_set PRIVATE IrisGL Qt6::Gui Qt6::Widgets)
jah_document_graph(test_selection_set)
add_test(NAME services.selection_set COMMAND test_selection_set
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(services.selection_set PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "services")
jah_no_display(services.selection_set)

# services.looks — the looks catalogue (POST_LOOKS_SPEC.md §4.1). Links the
# presentation table and IrisGL (the document half of the catalogue lives
# there, with SceneMirror and the scene reader as its other consumers). No
# engine, no display: this is a table-agreement and validator test.
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(test_looks_registry test_looks_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/services/looks.cpp)
set_target_properties(test_looks_registry PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_looks_registry PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl)
target_link_libraries(test_looks_registry PRIVATE IrisGL Qt6::Core)
add_test(NAME services.looks COMMAND test_looks_registry
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(services.looks PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     LABELS "services")
jah_no_display(services.looks)

# engine.error_pump — STABILITY_PROGRAM_SPEC Lane 1. The only suite in the tree
# that links BOTH the engine and a Studio service, because that is exactly the
# seam it tests: Engine::takeLastError() on one side, EngineErrorPump on the
# other. Needs a reachable DISPLAY like every engine suite (Ogre's XCB support
# object connects at plugin load) even though the view is offscreen.
add_executable(test_engine_error_pump test_engine_error_pump.cpp
    ${CMAKE_SOURCE_DIR}/src/services/engineerrorpump.cpp
    # The pump logs into JahLog's `engine` category now (SESSION_LOG_SPEC §5):
    # same text, same rate limiting, now timestamped and frame-numbered.
    ${CMAKE_SOURCE_DIR}/src/services/jahlog.cpp)
set_target_properties(test_engine_error_pump PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_engine_error_pump PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl)
target_compile_definitions(test_engine_error_pump PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_error_pump PRIVATE IrisGL JahshakaEngine Qt6::Gui)
add_dependencies(test_engine_error_pump JahshakaEngineMedia)
add_test(NAME engine.error_pump COMMAND test_engine_error_pump
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(engine.error_pump PROPERTIES TIMEOUT 120 LABELS "services")

# services.memory — session-registry OWNERSHIP (deep audit 2026-09, area 3).
# Links only the two registries it tests plus IrisGL (the SceneNodePtr payload
# an asset pins). No engine, no database, no display; runs offscreen.
find_package(Qt6 REQUIRED COMPONENTS Gui)
add_executable(test_memory_ownership test_memory_ownership.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/thumbnailmanager.cpp)
set_target_properties(test_memory_ownership PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_memory_ownership PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    # iris headers expose assimp and bullet types; these are IrisGL's own include paths
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_memory_ownership PRIVATE IrisGL Qt6::Gui)
# This suite builds a DOCUMENT, and a document node is an engine node now
# (SPECS/SCENEGRAPH_SPEC.md D2), so it boots the HEADLESS engine — NULL render
# system, no display, no GPU (tests/support/documentgraph.h). jah_no_display
# below runs it with DISPLAY unset, which is the proof of that claim.
jah_document_graph(test_memory_ownership)
add_test(NAME services.memory COMMAND test_memory_ownership
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(services.memory PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "services")
jah_no_display(services.memory)

# services.animation_undo — the keyframe domain's undo (verb-coverage audit
# F16). The RESTORE SEMANTICS have to be proved with a plain QUndoStack: a
# --script run is one open macro and QUndoStack refuses to undo into one, so
# scripting.e2e.anim can only prove the commands were pushed. Document only:
# no engine rendering, no display (but it builds a document, so it boots the
# headless graph like every other document suite).
add_executable(test_animation_undo test_animation_undo.cpp
    ${CMAKE_SOURCE_DIR}/src/services/animationedits.cpp
    ${CMAKE_SOURCE_DIR}/src/commands/animationcommands.cpp)
set_target_properties(test_animation_undo PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_animation_undo PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_animation_undo PRIVATE IrisGL Qt6::Gui Qt6::Widgets)
jah_document_graph(test_animation_undo)
add_test(NAME services.animation_undo COMMAND test_animation_undo
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(services.animation_undo PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "services")
jah_no_display(services.animation_undo)

# services.ogre_samples — the seeded ogre.cfg and the launch guards
# (SPECS/OGRE_SAMPLES_TAB_SPEC.md §7.3/§9.2). Pure file + string work: links
# the service and the two-line settings manager it reads its override from,
# no engine, no document, no display. The headless e2e proves the VERBS refuse;
# this proves the file they would write is windowed rather than fullscreen,
# which is the value that decides whether "Launch Original" is a side-by-side
# or a takeover of the owner's screen.
add_executable(test_ogre_samples test_ogre_samples.cpp
    ${CMAKE_SOURCE_DIR}/src/services/ogresamples.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp)
set_target_properties(test_ogre_samples PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_ogre_samples PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_ogre_samples PRIVATE Qt6::Gui Qt6::Widgets)
add_test(NAME services.ogre_samples COMMAND test_ogre_samples
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(services.ogre_samples PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "services")
jah_no_display(services.ogre_samples)

# The sanitizer lanes (scripts/sanitize.sh).
#   tsan  services.memory registers and drops session assets the way the app
#         does from more than one thread's callbacks.
#   lsan  services.core and services.memory are the ownership suites: whatever
#         AssetManager/ThumbnailManager fail to free shows up here first, and
#         neither one loads a driver.
jah_tsan_lane(services.memory)
# services.memory is QUARANTINED (L2 LightNode::shadowMap — 24 bytes, its
# only leak; the registry ownership it actually tests is clean).
jah_lsan_lane(services.core)
jah_lsan_blocked(services.memory)

# ---------------------------------------------------------------------------
# DISPLAY-FREE, AND PROVEN (TEST_GATE_AUDIT.md §4.5). Every suite named here
# claims — in its own comment, or by running --headless, or by building nothing
# but a document — that it needs no X server. The claim was never enforced, so
# it drifted: a suite that quietly opened a display still passed on a developer
# box and would have failed the first CI runner without one, and every
# unnecessary X connection is one more thing four parallel jobs contend over.
# jah_no_display runs these with DISPLAY and WAYLAND_DISPLAY UNSET, which turns
# the claim into a gate. Verified 2026-09-09: each of these passes displayless
# with the same assertion count it has with a display.
jah_no_display(services.core)
# services.clipboard — the clipboard's two leaves (CLIPBOARD_SPEC P0): the
# envelope codec and the key-aware asset walk, plus the in-memory backend.
# Links the three leaf TUs and IrisGL (the walk's material key table IS
# iris::PbrMaterial::mapRowNames(), which is the assertion that matters). No
# database, no engine, no display.
find_package(Qt6 REQUIRED COMPONENTS Gui)
add_executable(test_clipboard test_clipboard.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetrefs.cpp
    ${CMAKE_SOURCE_DIR}/src/io/clipboardformat.cpp
    ${CMAKE_SOURCE_DIR}/src/services/clipboardbackend.cpp)
set_target_properties(test_clipboard PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_clipboard PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_clipboard PRIVATE IrisGL Qt6::Gui)
add_test(NAME services.clipboard COMMAND test_clipboard
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(services.clipboard PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "services")
jah_no_display(services.clipboard)
