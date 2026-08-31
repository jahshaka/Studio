# Web export tests (WEB_EXPORT_AUDIT phases 0-1): document -> GLB writer +
# export-folder assembly, validated structurally. Document-only (no GL, no
# engine) — runs offscreen. The viewer resources come from viewer.qrc, compiled
# into this test exactly as into the app.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_web_export test_web_export.cpp
    ${CMAKE_SOURCE_DIR}/src/export/gltfexporter.cpp
    ${CMAKE_SOURCE_DIR}/src/export/exportservice.cpp
    ${CMAKE_SOURCE_DIR}/src/export/previewlauncher.cpp
    ${CMAKE_SOURCE_DIR}/src/export/viewer/viewer.qrc)
set_target_properties(test_web_export PROPERTIES AUTOMOC OFF AUTORCC ON)
target_include_directories(test_web_export PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_web_export PRIVATE IrisGL Qt6::Gui Qt6::OpenGL)
add_test(NAME export.web COMMAND test_web_export
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(export.web PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "export")
