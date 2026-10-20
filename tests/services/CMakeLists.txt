# Service-layer unit tests (APP_ARCHITECTURE_AUDIT §9 Phase 1).
#
# services.core — links ONLY the QObject-free/minimally-Qt services it tests
# (UndoService + SelectionService), no MainWindow, no engine, no document
# construction (SelectionService is exercised through null SceneNodePtrs; the
# guard and re-emit contracts do not depend on a live node). Runs offscreen.
find_package(Qt6 REQUIRED COMPONENTS Gui Widgets)
add_executable(test_services test_services.cpp
    ${CMAKE_SOURCE_DIR}/src/services/undoservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/uistep.cpp
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
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
add_test(NAME services.core COMMAND test_services
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
jah_ship_row(services.core)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(services.core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "services")

# services.looks — the looks catalogue (POST_LOOKS_SPEC.md §4.1). Links the
# presentation table and IrisGL (the document half of the catalogue lives
# there, with SceneMirror and the scene reader as its other consumers). No
# engine, no display: this is a table-agreement and validator test.
find_package(Qt6 REQUIRED COMPONENTS Core)

   # the frames it renders, counted 2026-10-12 (TESTING-SPEED-B4)

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
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_memory_ownership PRIVATE IrisGL Qt6::Gui)
# This suite builds a DOCUMENT, and a document node is an engine node now
# (SPECS/SCENEGRAPH_SPEC.md D2), so it boots the HEADLESS engine — NULL render
# system, no display, no GPU (tests/support/documentgraph.h). jah_no_display
# below runs it with DISPLAY unset, which is the proof of that claim.
jah_document_graph(test_memory_ownership)
add_test(NAME services.memory COMMAND test_memory_ownership
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
jah_ship_row(services.memory)   # THE SHIP SET (TEST-CUT-1)
set_tests_properties(services.memory PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "services")
jah_no_display(services.memory)

# The sanitizer lanes (scripts/sanitize.sh).
#   tsan  services.memory registers and drops session assets the way the app
#         does from more than one thread's callbacks.
#   lsan  services.core and services.memory are the ownership suites: whatever
#         AssetManager/ThumbnailManager fail to free shows up here first, and
#         neither one loads a driver.
jah_tsan_lane(services.memory)
jah_lsan_lane(services.core services.memory)

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

