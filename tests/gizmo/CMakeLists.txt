# Gizmo overlay through the engine. Output goes to bin/ so the gizmos' asset paths
# (relative to the executable: app/models/axis_*.obj) resolve.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL)
add_executable(test_gizmo_overlay test_gizmo_overlay.cpp test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/gizmooverlay.cpp ${CMAKE_SOURCE_DIR}/src/bridge/scenemirror.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/gizmo.cpp ${CMAKE_SOURCE_DIR}/src/viewport/gizmomeshes.cpp ${CMAKE_SOURCE_DIR}/src/viewport/translationgizmo.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/rotationgizmo.cpp ${CMAKE_SOURCE_DIR}/src/viewport/scalegizmo.cpp)
set_target_properties(test_gizmo_overlay PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_gizmo_overlay PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl/include ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_gizmo_overlay PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_gizmo_overlay PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL Qt6::Widgets)
add_dependencies(test_gizmo_overlay JahshakaEngineMedia)
add_test(NAME gizmo.overlay COMMAND ${CMAKE_BINARY_DIR}/bin/test_gizmo_overlay WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(gizmo.overlay PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "mirror")
