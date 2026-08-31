# Phase-2 suite of the ASSET PIPELINE program (ASSET_PIPELINE_SPEC §3.1.3):
# content-addressed store migrate → verify → rebuildCatalog round-trip on a
# fixture library — dedup, 2-char fan-out, refcount triggers, sidecars,
# legacy-tree retention, view_filter IN (2,3), lock refusal, bit-rot
# detection, idempotent reruns. Headless; throwaway files only.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia)
add_executable(test_asset_migrate test_asset_migrate.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetmigration.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstore.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    # database.cpp's link closure (Project, guids, asset helpers)
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetmetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_asset_migrate PROPERTIES AUTOMOC ON)
target_include_directories(test_asset_migrate PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_asset_migrate PRIVATE IrisGL Qt6::Widgets Qt6::Sql Qt6::Multimedia)
add_test(NAME assets.migrate COMMAND test_asset_migrate
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(assets.migrate PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "assets")
