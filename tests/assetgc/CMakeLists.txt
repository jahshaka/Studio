# THE STORE GC's adversarial suite (deep audit 2026-09, area 6). A fixture
# store seeded with every orphan class AND a live look-alike for each: the
# dry run must report exactly the orphans, the real run must remove exactly
# them, and every live artifact must come back byte-for-byte — including the
# pinned copy-on-write object whose refcount is 0. Also the sidecar lifecycle
# (invariant I2) and rebuildCatalog's tombstone guard.
# Headless; throwaway files only; the store root is overridden for the whole
# run so nothing can ever address the developer's real library.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia)
add_executable(test_asset_gc test_asset_gc.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetgc.cpp
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
    ${CMAKE_SOURCE_DIR}/src/services/iesprofile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_asset_gc PROPERTIES AUTOMOC ON)
target_include_directories(test_asset_gc PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_asset_gc PRIVATE IrisGL Qt6::Widgets Qt6::Sql Qt6::Multimedia)
add_test(NAME assets.gc COMMAND test_asset_gc
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(assets.gc PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "assets")

# The leak lane (scripts/sanitize.sh lsan).
jah_lsan_lane(assets.gc)
