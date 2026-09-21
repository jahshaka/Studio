# The player on the engine: EnginePlayerScene — the EDITOR's Scene and mirror
# (lane PLAYER-1: there is one of each) + the document camera + PlayBack —
# driven headless into a second offscreen View. Links the player's simulation classes (PlayBack and its
# controllers) with link stubs for the Studio globals they reach into. Output
# goes to bin/ so asset paths relative to the executable resolve.
find_package(Qt6 REQUIRED COMPONENTS Gui OpenGL Widgets)
add_executable(test_engine_player test_engine_player.cpp test_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/player/engineplayerscene.cpp
    # The player owns its VR mode since phase 3 (EnginePlayerScene::vr()), so
    # every target that compiles the scene needs its implementation — a link
    # error otherwise, and a STALE BINARY if the gate runs without a build.
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
    ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(test_engine_player PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_engine_player PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
   ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_engine_player PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_engine_player PRIVATE IrisGL JahshakaEngine Qt6::Gui Qt6::OpenGL Qt6::Widgets)
add_dependencies(test_engine_player JahshakaEngineMedia)
add_test(NAME player.engine COMMAND ${CMAKE_BINARY_DIR}/bin/test_engine_player WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(player.engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "mirror")

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
add_test(NAME player.editor_parity COMMAND ${CMAKE_BINARY_DIR}/bin/test_editor_player_parity
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(player.editor_parity PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "mirror")

# ---- player.playback_routing (A1.6) --------------------------------------
# The play/edit STATE MACHINE — PlaybackService's routing into the viewport,
# per call and per flag. No engine, no GL, no display: it subclasses the
# SHIPPING HeadlessEditorViewport (the --headless stand-in) and records what
# the service asked for, which is why the interface's size costs this suite
# nothing. AUTOMOC is on for the one target in this directory that needs it:
# PlaybackService is a QObject and the suite asserts on its mode signals.
# ieditorviewport.h is listed as a SOURCE so AUTOMOC generates the moc for
# EditorViewportEvents — HeadlessEditorViewport's constructor creates one, and
# nothing else in this target would pull its vtable in.
add_executable(test_playback_routing test_playback_routing.cpp
    ${CMAKE_SOURCE_DIR}/src/services/playbackservice.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/ieditorviewport.h)
set_target_properties(test_playback_routing PROPERTIES
    AUTOMOC ON AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_playback_routing PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_link_libraries(test_playback_routing PRIVATE IrisGL Qt6::Gui Qt6::OpenGL Qt6::Widgets)
add_test(NAME player.playback_routing COMMAND ${CMAKE_BINARY_DIR}/bin/test_playback_routing
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(player.playback_routing PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "services")
jah_no_display(player.playback_routing)

# ---- player.vr (SPECS/VR_SPEC.md §4.5, phase 3) ---------------------------
# THE RIG'S ARITHMETIC: where a wearer stands when the Player enters VR, and
# where the fly keys take them. Pure functions over iris maths (player/
# vrorigin.h) — no engine, no display, no OpenXR, no headset — because every
# decision in them is a sign or an axis whose only other witness is a person
# feeling sick inside a headset. Qt for the maths types and nothing else.
add_executable(test_player_vr test_player_vr.cpp)
set_target_properties(test_player_vr PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
target_include_directories(test_player_vr PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl)
target_link_libraries(test_player_vr PRIVATE IrisGL Qt6::Gui)
add_test(NAME player.vr COMMAND ${CMAKE_BINARY_DIR}/bin/test_player_vr
         WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set_tests_properties(player.vr PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "services")
jah_no_display(player.vr)
