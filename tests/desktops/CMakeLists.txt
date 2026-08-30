# Multiple-desktops storage tests (DESKTOPS_SPEC.md): builds the REAL
# src/data/database/database.cpp against a throwaway SQLite file — migration,
# desktop filter, move round-trip, freeform position round-trip. Headless
# (offscreen platform); never touches the user's live JahLibrary.db.
find_package(Qt6 REQUIRED COMPONENTS Widgets Sql)
add_executable(test_desktops_db test_desktops_db.cpp
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
set_target_properties(test_desktops_db PROPERTIES AUTOMOC ON)
target_include_directories(test_desktops_db PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl/include ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_desktops_db PRIVATE IrisGL Qt6::Widgets Qt6::Sql)
add_test(NAME desktops.db COMMAND test_desktops_db
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(desktops.db PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktops")
