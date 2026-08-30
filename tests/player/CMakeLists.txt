# The player on the engine (plan step 11): EnginePlayerScene — the second engine
# Scene + SceneMirror + document camera + PlayBack — driven headless into an
# offscreen View. Links the player's simulation classes (PlayBack and its
# controllers) with link stubs for the Studio globals they reach into. Output
# goes to bin/ so asset paths relative to the executable resolve.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL Widgets)
add_executable(test_engine_player test_engine_player.cpp test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/player/engineplayerscene.cpp
    ${CMAKE_SOURCE_DIR}/src/player/playback.cpp
    ${CMAKE_SOURCE_DIR}/src/player/playermousecontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/cameracontrollerbase.cpp
    ${CMAKE_SOURCE_DIR}/src/core/keyboardstate.cpp
    ${CMAKE_SOURCE_DIR}/src/core/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/engine/scenemirror.cpp)
set_target_properties(test_engine_player PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_engine_player PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl/include ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_engine_player PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_player PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL Qt6::Widgets)
add_dependencies(test_engine_player JahshakaEngineMedia)
add_test(NAME player.engine COMMAND ${CMAKE_BINARY_DIR}/bin/test_engine_player WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(player.engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "mirror")
