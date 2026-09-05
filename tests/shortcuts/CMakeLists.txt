# Editor controls (EDITOR_SHORTCUTS_SPEC): unit tests of the ShortcutRegistry
# service (defaults, persistence, conflict refusal) and — from phase C — the
# SnapSettings the gizmos and grid read. Links only the services under test.
#
# From AVATAR_LOCOMOTION_SPEC Stage 1 it also gates the PLAY-MODE KEY PATH
# (§8.3): while the scene plays, W must not change the gizmo mode and must
# reach the gameplay input state instead — and when it is stopped, nothing
# about the editor's shortcuts changes. That is why inputmap.cpp is compiled
# in here: the stub viewport in the test calls iris::gameplayClaimsKey, the
# same function both real viewports call.
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)
add_executable(test_shortcut_registry test_shortcut_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/services/shortcutregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/snapsettings.cpp
    ${CMAKE_SOURCE_DIR}/irisgl/document/input/inputmap.cpp)
set_target_properties(test_shortcut_registry PROPERTIES AUTOMOC ON CXX_STANDARD 17)
target_include_directories(test_shortcut_registry PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl)
target_link_libraries(test_shortcut_registry PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME shortcuts.registry COMMAND test_shortcut_registry
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(shortcuts.registry PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "services")

# The leak lane (scripts/sanitize.sh lsan): the registry owns QAction/QShortcut
# objects whose parenting is exactly the kind of thing this lane checks.
jah_lsan_lane(shortcuts.registry)
