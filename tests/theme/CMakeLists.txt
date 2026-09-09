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

# ---------------------------------------------------------------------------
# DISPLAY-FREE, AND PROVEN (TEST_GATE_AUDIT.md §4.5). Every suite named here
# claims — in its own comment, or by running --headless, or by building nothing
# but a document — that it needs no X server. The claim was never enforced, so
# it drifted: a suite that quietly opened a display still passed on a developer
# box and would have failed the first CI runner without one, and every
# unnecessary X connection is one more thing four parallel jobs contend over.
# jah_no_display runs these with DISPLAY and WAYLAND_DISPLAY UNSET, which turns
# the claim into a gate. Verified 2026-09-09: each of these passes displayless
# with the same assertion count it has with a display.
jah_no_display(theme.manager)
