# Library asset DELETION round-trip (the 2026-09-03 silent-no-op defect):
# builds the REAL src/data/database/database.cpp + the CAS helpers against a
# throwaway SQLite file and asserts that a delete cleans assets + asset_files +
# project_assets, decrements files.refcount, and — critically — reports FALSE
# instead of silently succeeding when the connection cannot run the statement.
# Headless (offscreen platform); never touches the user's live JahLibrary.db.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia)
add_executable(test_asset_delete test_asset_delete.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    # database.cpp's link closure (Project, guids, asset helpers)
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetmetadata.cpp
    ${CMAKE_SOURCE_DIR}/src/services/iesprofile.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetcas.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assetstorepaths.cpp
    ${CMAKE_SOURCE_DIR}/src/services/videoutils.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_asset_delete PROPERTIES AUTOMOC ON)
target_include_directories(test_asset_delete PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_asset_delete PRIVATE IrisGL Qt6::Widgets Qt6::Sql Qt6::Multimedia)
add_test(NAME assets.delete_roundtrip COMMAND test_asset_delete
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(assets.delete_roundtrip PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "assets")

# The leak lane (scripts/sanitize.sh lsan).
jah_lsan_lane(assets.delete_roundtrip)
