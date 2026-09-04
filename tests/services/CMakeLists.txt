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
target_link_libraries(test_services PRIVATE Qt6::Gui Qt6::Widgets)
add_test(NAME services.core COMMAND test_services
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(services.core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "services")

# engine.error_pump — STABILITY_PROGRAM_SPEC Lane 1. The only suite in the tree
# that links BOTH the engine and a Studio service, because that is exactly the
# seam it tests: Engine::takeLastError() on one side, EngineErrorPump on the
# other. Needs a reachable DISPLAY like every engine suite (Ogre's XCB support
# object connects at plugin load) even though the view is offscreen.
add_executable(test_engine_error_pump test_engine_error_pump.cpp
    ${CMAKE_SOURCE_DIR}/src/services/engineerrorpump.cpp)
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
set_tests_properties(engine.error_pump PROPERTIES TIMEOUT 600 LABELS "services")

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
add_test(NAME services.memory COMMAND test_memory_ownership
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(services.memory PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "services")

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
