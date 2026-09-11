# App theme (THEME_AUDIT.md): the appearance/theme setting round-trip and the
# StyleSheet kill switch. Links the real ThemeManager, which pulls in the
# qlementine style library.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(test_theme_manager test_theme_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/classicsheets.cpp
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

# theme.style_contracts — three contracts between our widgets and the Qlementine
# style that the theme sweep surfaced once the Classic sheets stopped hiding
# them: a widget's QProxyStyle never owns the app style (HFloatSliderWidget's
# proxy used to take the application's QlementineStyle and delete it with the
# first destroyed slider row), and a tab's size/text rect/paint agree under a
# stale QTabBar position (the "T…" Tray dock tab), and icon-over-caption items
# are laid out icon ABOVE text (Qlementine squeezed the Materials/palette
# captions beside the icon). Offscreen; real slider rows,
# the real startup theme path (private data root: no appearance/theme key).
add_executable(test_style_contracts test_style_contracts.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/controls/hfloatsliderwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/classicsheets.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_style_contracts PROPERTIES AUTOMOC ON AUTOUIC ON CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# AUTOUIC looks for .ui files beside the including source
set_property(TARGET test_style_contracts PROPERTY AUTOUIC_SEARCH_PATHS
    ${CMAKE_SOURCE_DIR}/src/ui/controls)
target_include_directories(test_style_contracts PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_style_contracts PRIVATE Qt6::Widgets qlementine)
set(_style_contracts_root ${CMAKE_CURRENT_BINARY_DIR}/style-contracts-root)
add_test(NAME theme.style_contracts COMMAND test_style_contracts
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(theme.style_contracts PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;JAHSHAKA_DATA_ROOT=${_style_contracts_root}"
    LABELS "services")
jah_no_display(theme.style_contracts)

# theme.sheets — THE LIVE THEME WALK (theme sweep, lane 16): boots the real app
# over MCP, tours every page and every app.dialogs() dialog, and asserts that
# under Qlementine no widget carries a raw stylesheet outside the named
# allowlist (app.styleSheets classifies theme-owned vs raw); then relaunches
# on Classic and spot-checks that the archived sheets land byte-for-byte.
# Needs a display (the engine app); scratch HOME + data root like every other
# app-spawning suite.
find_package(Qt6 REQUIRED COMPONENTS Core Network)
add_executable(test_theme_sheets test_theme_sheets.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/classicsheets.cpp)
set_target_properties(test_theme_sheets PROPERTIES CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(test_theme_sheets PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
target_include_directories(test_theme_sheets PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(test_theme_sheets PRIVATE
    JAHSHAKA_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_BINARY="$<TARGET_FILE:${CMAKE_PROJECT_NAME}>")
set(_theme_sheets_home ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-theme-sheets)
file(MAKE_DIRECTORY ${_theme_sheets_home}/run)
add_test(NAME theme.sheets COMMAND test_theme_sheets
         WORKING_DIRECTORY ${_theme_sheets_home}/run)
set_tests_properties(theme.sheets PROPERTIES
    ENVIRONMENT "HOME=${_theme_sheets_home};JAHSHAKA_DATA_ROOT=${_theme_sheets_home}/.local/share/Jahshaka"
    TIMEOUT 300 LABELS "ui")
jah_tsan_blocked(theme.sheets)   # spawns the app
jah_lsan_blocked(theme.sheets)   # spawns the app

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

# theme.no_raw_sheets — the static half of the no-raw-stylesheet law (theme review SF-3):
# the live walk cannot see menus and dialogs built on demand, so the source is linted too.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME theme.no_raw_sheets
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/no_raw_sheets.py ${CMAKE_SOURCE_DIR})
    set_tests_properties(theme.no_raw_sheets PROPERTIES LABELS "theme" TIMEOUT 60)
    jah_no_display(theme.no_raw_sheets)
endif()
