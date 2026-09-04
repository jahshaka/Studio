# Threaded-import suite (the UI-freeze fix): AssetImportService's
# prepare/commit split + ImportBatchRunner — prepare (assimp parse, texture
# extraction, hashing) really runs on a worker thread, commit lands on the
# thread owning the default DB connection, progress callbacks arrive per
# stage, and a mid-import cancel rolls back to zero rows and zero orphan CAS
# objects. Headless, throwaway store/db; the sync (verb) path stays covered
# by scripting.e2e.import_pipeline.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia Concurrent)
add_executable(test_import_async test_import_async.cpp
    ${CMAKE_SOURCE_DIR}/src/services/import/assetimportservice.cpp
    ${CMAKE_SOURCE_DIR}/src/services/import/assetimporters.cpp
    ${CMAKE_SOURCE_DIR}/src/services/import/importbatchrunner.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/pages/importviewertail.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetmetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/services/iesprofile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/thumbnailmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    # database.cpp's link closure (same as tests/assetmigrate)
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_import_async PROPERTIES AUTOMOC ON CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(test_import_async PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/zip/src
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_import_async PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_import_async PRIVATE IrisGL
    Qt6::Widgets Qt6::Sql Qt6::Multimedia Qt6::Concurrent)
add_test(NAME import.async COMMAND test_import_async
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(import.async PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                     TIMEOUT 120 LABELS "importer")

# import.shutdown — the owner-reported ZOMBIE: quitting while (or right
# after) a threaded import ran left the process alive headless with an
# orphaned "loading" dialog. Spawns the REAL binary (like mcp.e2e) under a
# scratch HOME, starts the interactive batch through editor.importAssets,
# calls app.quit() mid-batch and after a batch, and asserts the PROCESS is
# gone within the exit budget with code 0.
find_package(Qt6 REQUIRED COMPONENTS Core Network)
add_executable(test_import_shutdown test_import_shutdown.cpp)
set_target_properties(test_import_shutdown PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_import_shutdown PRIVATE Qt6::Core Qt6::Network)
target_compile_definitions(test_import_shutdown PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")

set(_shutdown_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-shutdown)
file(MAKE_DIRECTORY ${_shutdown_home}/run)
add_test(NAME import.shutdown COMMAND test_import_shutdown
         WORKING_DIRECTORY ${_shutdown_home}/run)
set_tests_properties(import.shutdown PROPERTIES
    ENVIRONMENT "HOME=${_shutdown_home}"
    TIMEOUT 480 LABELS "importer")

# The race lane (scripts/sanitize.sh tsan). import.async IS the threaded-import
# contract — prepare on a worker, commit on the DB thread, progress callbacks
# crossing between them — so it is the single highest-value suite in the lane.
# import.shutdown drives the same runner through a mid-batch quit, which is
# where a cancel/teardown race would live — but import.shutdown is
# QUARANTINED there for now: the app cannot start at all under GCC 15's
# libtsan (see jah_tsan_blocked in tests/CMakeLists.txt).
jah_tsan_lane(import.async)
jah_tsan_blocked(import.shutdown)

# The leak lane (scripts/sanitize.sh lsan): in-process, no Vulkan, and it
# allocates the whole import closure (assimp scene, CAS blobs, thumbnails).
# import.shutdown is NOT eligible at all — it spawns the app, which loads a
# driver. import.async is eligible but QUARANTINED (L3 — every imported asset
# leaks its AssetNodeObject and the mesh subtree under it: 21 KB in 248
# allocations here, from two imports).
jah_lsan_blocked(import.async)
