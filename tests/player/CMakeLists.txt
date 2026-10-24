# The player on the engine: EnginePlayerScene — the EDITOR's Scene and mirror
# (lane PLAYER-1: there is one of each) + the document camera + PlayBack —
# driven headless into a second offscreen View. Links the player's simulation classes (PlayBack and its
# controllers) with link stubs for the Studio globals they reach into. Output
# goes to bin/ so asset paths relative to the executable resolve.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL Widgets)

# EDITOR/PLAYER PARITY: the same document through EnginePlayerScene and through
# the editor viewport's own mirror calls, into two identical offscreen views —
# the pixels must match. Same sources, same stubs; only the driver differs.
add_executable(test_editor_player_parity test_editor_player_parity.cpp test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/player/engineplayerscene.cpp
    ${CMAKE_SOURCE_DIR}/src/player/playervr.cpp
    # ...and the project's VR settings it flies at (lane VR-WORLD-1).
    ${CMAKE_SOURCE_DIR}/src/services/vrworld.cpp
    ${CMAKE_SOURCE_DIR}/src/player/playback.cpp
    ${CMAKE_SOURCE_DIR}/src/services/jahlog.cpp
    ${CMAKE_SOURCE_DIR}/src/player/playermousecontroller.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/cameracontrollerbase.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/keyboardstate.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/cameraspeed.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsstore.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_editor_player_parity PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_editor_player_parity PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_editor_player_parity PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/"
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(test_editor_player_parity PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL Qt6::Widgets)
add_dependencies(test_editor_player_parity JahshakaEngineMedia)
jah_gpu_row(player.editor_parity SHIP CLASS engine COMMAND ${CMAKE_BINARY_DIR}/bin/test_editor_player_parity
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(player.editor_parity PROPERTIES JAHSHAKA_TEST_FRAMES 123)   # the frames it renders, counted 2026-10-12 (TESTING-SPEED-B4)
# TESTING-CLEANUP-2B item 7, re-based after read 4 (2026-10-10; the sample per tests/CMakeLists.txt's policy — the run
# log's SLOT-ERA quiet PASS runs, since 2026-10-09 18:00, taken as the gate runs the row): WARM, n=5 — gate
# records 5.7 / 5.7 / 7.0 / 6.3 s and this lane's 8.1 s solo (15:25, 2 builds on the box); median 6.3 -> 6x = 40, under
# the 120 s FLOOR; slot-era worst 8.1 -> TIMEOUT 60 (URGENT #48: was 120) (unchanged).
set_tests_properties(player.editor_parity PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "mirror")

