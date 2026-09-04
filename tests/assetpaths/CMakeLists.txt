# Phase-0 suite of the ASSET PIPELINE program (ASSET_PIPELINE_SPEC §3.4):
# AssetStorePaths unit (the single path authority) + the dependency-export
# regression (selectDep direction + all-rows) against the REAL
# src/data/database/database.cpp on a throwaway SQLite file, + the new
# dependencies indices. Headless (offscreen platform); never touches the
# user's live JahLibrary.db.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia)
add_executable(test_asset_paths test_asset_paths.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstore.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    # database.cpp's link closure (Project, guids, asset helpers)
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetmetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/services/iesprofile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_asset_paths PROPERTIES AUTOMOC ON)
target_include_directories(test_asset_paths PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_asset_paths PRIVATE IrisGL Qt6::Widgets Qt6::Sql Qt6::Multimedia)
add_test(NAME assets.paths COMMAND test_asset_paths
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(assets.paths PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "assets")

# assets.torn_write — STABILITY_PROGRAM_SPEC Lane 2. The CAS store's atomicity:
# a content-addressed name is a claim about bytes, so an object must exist whole
# or not at all, including through a kill -9 in the middle of the copy. Unix
# only: the deterministic mid-copy kill needs fork + a FIFO on a second
# filesystem (/dev/shm).
if(UNIX AND NOT APPLE)
    add_executable(test_asset_torn_write test_asset_torn_write.cpp
        ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
        ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
        ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp)
    set_target_properties(test_asset_torn_write PROPERTIES AUTOMOC ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    target_include_directories(test_asset_torn_write PRIVATE
        ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl)
    target_link_libraries(test_asset_torn_write PRIVATE IrisGL Qt6::Widgets Qt6::Sql)
    add_test(NAME assets.torn_write COMMAND test_asset_torn_write
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(assets.torn_write PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "assets")
endif()

# The leak lane (scripts/sanitize.sh lsan). assets.torn_write is NOT in it: it
# kills a child mid-write on purpose, and a SIGKILLed process never reaches
# LeakSanitizer's atexit check, so the entry would be decoration.
jah_lsan_lane(assets.paths)
