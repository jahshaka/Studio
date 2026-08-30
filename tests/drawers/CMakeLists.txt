# Asset-drawers storage tests (ASSET_DRAWERS_SPEC.md §2/§5): builds the REAL
# src/data/database/database.cpp against a throwaway SQLite file — the guarded
# parent-column migration, drawer CRUD, nesting, delete-reassigns-to-
# Uncategorized, and cycle rejection. Headless (offscreen platform); never
# touches the user's live JahLibrary.db.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)
add_executable(test_drawers_db test_drawers_db.cpp
    ${CMAKE_SOURCE_DIR}/src/data/database/database.cpp
    # database.cpp's link closure (Project, guids, asset helpers)
    ${CMAKE_SOURCE_DIR}/src/data/project.cpp
    ${CMAKE_SOURCE_DIR}/src/data/guidmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/services/subscriber.cpp
    ${CMAKE_SOURCE_DIR}/src/services/assethelper.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/io/scenewriter.cpp
    ${CMAKE_SOURCE_DIR}/src/io/assetiobase.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_drawers_db PROPERTIES AUTOMOC ON)
target_include_directories(test_drawers_db PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_drawers_db PRIVATE IrisGL Qt6::Widgets Qt6::Sql)
add_test(NAME drawers.db COMMAND test_drawers_db
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(drawers.db PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "drawers")
