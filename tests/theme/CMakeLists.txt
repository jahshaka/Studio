# App theme (THEME_AUDIT.md): the appearance/theme setting round-trip and the
# StyleSheet kill switch. Links the real ThemeManager, which pulls in the
# qlementine style library.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(test_theme_manager test_theme_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_theme_manager PROPERTIES AUTOMOC ON CXX_STANDARD 17)
target_include_directories(test_theme_manager PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_theme_manager PRIVATE Qt6::Widgets qlementine)
add_test(NAME theme.manager COMMAND test_theme_manager
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(theme.manager PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "services")

# The leak lane (scripts/sanitize.sh lsan): ThemeManager installs a QStyle and
# palettes — ownership hand-offs Qt does not warn about.
jah_lsan_lane(theme.manager)
