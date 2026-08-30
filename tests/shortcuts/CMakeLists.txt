# Editor controls (EDITOR_SHORTCUTS_SPEC): unit tests of the ShortcutRegistry
# service (defaults, persistence, conflict refusal) and — from phase C — the
# SnapSettings the gizmos and grid read. Links only the services under test.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(test_shortcut_registry test_shortcut_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/services/shortcutregistry.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/snapsettings.cpp)
set_target_properties(test_shortcut_registry PROPERTIES AUTOMOC ON CXX_STANDARD 17)
target_include_directories(test_shortcut_registry PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_shortcut_registry PRIVATE Qt6::Widgets)
add_test(NAME shortcuts.registry COMMAND test_shortcut_registry
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(shortcuts.registry PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "services")
