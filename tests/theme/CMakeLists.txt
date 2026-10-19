# App theme (THEME_AUDIT.md): the appearance/theme setting round-trip and the
# StyleSheet kill switch. Links the real ThemeManager, which pulls in the
# qlementine style library.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(test_theme_manager test_theme_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/thememanager.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/stylesheet.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/style/classicsheets.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsmanager.cpp
    ${CMAKE_SOURCE_DIR}/src/data/settingsstore.cpp
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
    ${CMAKE_SOURCE_DIR}/src/data/settingsstore.cpp
    ${CMAKE_SOURCE_DIR}/src/data/constants.cpp)
set_target_properties(test_style_contracts PROPERTIES AUTOMOC ON AUTOUIC ON CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# AUTOUIC looks for .ui files beside the including source
set_property(TARGET test_style_contracts PROPERTY AUTOUIC_SEARCH_PATHS
    ${CMAKE_SOURCE_DIR}/src/ui/controls)
target_include_directories(test_style_contracts PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src)
# Qt6::Test for the synthetic mouse/key gestures the activation contract needs
# (a right-click must raise a menu, never activate).
find_package(Qt6 REQUIRED COMPONENTS Test)
target_link_libraries(test_style_contracts PRIVATE Qt6::Widgets Qt6::Test qlementine)
# Its data root is wiped per run (TESTING-CLEANUP-2 H8e): the startup theme path it asserts wants a
# root with no appearance/theme key, and a root nothing wipes holds whatever the last run wrote.
set(_style_contracts_root ${CMAKE_CURRENT_BINARY_DIR}/style-contracts-root)
add_test(NAME theme.style_contracts
         COMMAND sh "${JAH_FRESH_HOME}" "${_style_contracts_root}" -- $<TARGET_FILE:test_style_contracts>
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
jah_gpu_row(theme.sheets CLASS app TIER low NEEDS NONE COMMAND test_theme_sheets
         WORKING_DIRECTORY ${_theme_sheets_home}/run)
set_tests_properties(theme.sheets PROPERTIES JAHSHAKA_TEST_FRAMES 591)   # the frames it renders, counted 2026-10-12 (TESTING-SPEED-B4)
# TESTING-CLEANUP-2B item 2: TIMEOUT 430 = 6 x the run log's quiet median 71.1 s (43 quiet PASS runs since 2026-10-01, worst 196.7 s);
# declared 300 was below the policy, honest only with the +900 s widening (now deleted).
set_tests_properties(theme.sheets PROPERTIES
    ENVIRONMENT "HOME=${_theme_sheets_home};JAHSHAKA_DATA_ROOT=${_theme_sheets_home}/.local/share/Jahshaka"
    TIMEOUT 430 LABELS "ui")
# TSan, run 2026-10-11 (RUNNER-A7A): red — TSAN-BOOT-TOO-SLOW, TSAN-GCC15-THREAD-REGISTRY (testing/defects.json, recheck 2026-11-15)
jah_tsan_blocked(theme.sheets)   # spawns the app
# LSan (LEAK-1, 2026-10-19): the app still leaks ~13 KB at exit under this row's script (the preferences page's
# world-settings widgets, ThemeManager::applyAtStartup and qlementine polish allocations — spikes/leak-1/RESUME.md);
# its harness reads the child's exit (quitAndReap), so the row moves to jah_lsan_lane when those have owners.
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

# FRESH HOMES (PROJECT-NAMES-1). A project name is unique in a library now (project.create
# refuses a taken one), so a row that creates a project in a home nothing wipes went red on its
# SECOND run in the same tree (an rc tree's solo retries, build-linux, a stage close) — and every
# such home grew without bound (ui.column_law's held 66 'ColumnLaw' projects). Each row below
# creates projects; its home is wiped at the start of every run (WARM: the shader caches stay).
if(TEST theme.sheets)
    jah_fresh_home_fixture(theme.sheets.fresh_home themeSheetsFreshHome "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-theme-sheets" WARM LABELS "ui")
    set_property(TEST theme.sheets APPEND PROPERTY FIXTURES_REQUIRED themeSheetsFreshHome)
endif()
