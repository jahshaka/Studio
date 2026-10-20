# App theme (THEME_AUDIT.md): the appearance/theme setting round-trip and the
# StyleSheet kill switch. Links the real ThemeManager, which pulls in the
# qlementine style library.
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Qt6::Test for the synthetic mouse/key gestures the activation contract needs
# (a right-click must raise a menu, never activate).
find_package(Qt6 REQUIRED COMPONENTS Test)

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
jah_gpu_row(theme.sheets SHIP CLASS app TIER low NEEDS NONE COMMAND test_theme_sheets
         WORKING_DIRECTORY ${_theme_sheets_home}/run)
set_tests_properties(theme.sheets PROPERTIES JAHSHAKA_TEST_FRAMES 591)   # the frames it renders, counted 2026-10-12 (TESTING-SPEED-B4)
# TESTING-CLEANUP-2B item 2: TIMEOUT 430 = 6 x the run log's quiet median 71.1 s (43 quiet PASS runs since 2026-10-01, worst 196.7 s);
# declared 300 was below the policy, honest only with the +900 s widening (now deleted).
set_tests_properties(theme.sheets PROPERTIES
    ENVIRONMENT "HOME=${_theme_sheets_home};JAHSHAKA_DATA_ROOT=${_theme_sheets_home}/.local/share/Jahshaka"
    TIMEOUT 430 LABELS "ui")
# TSan, run 2026-10-11 (RUNNER-A7A): red — TSAN-BOOT-TOO-SLOW, TSAN-GCC15-THREAD-REGISTRY (testing/defects.json, recheck 2026-11-15)
jah_tsan_blocked(theme.sheets)   # spawns the app
jah_lsan_lane(theme.sheets)   # spawns the app: its harness reads the child's exit (quitAndReap) — green = the app leaked nothing

# theme.no_raw_sheets (archived: archive/tests/theme) — the static half of the no-raw-stylesheet law (theme review SF-3):
# the live walk cannot see menus and dialogs built on demand, so the source is linted too.
find_package(Python3 COMPONENTS Interpreter QUIET)

# FRESH HOMES (PROJECT-NAMES-1). A project name is unique in a library now (project.create
# refuses a taken one), so a row that creates a project in a home nothing wipes went red on its
# SECOND run in the same tree (an rc tree's solo retries, build-linux, a stage close) — and every
# such home grew without bound (ui.column_law (archived: archive/tests/ui)'s held 66 'ColumnLaw' projects). Each row below
# creates projects; its home is wiped at the start of every run (WARM: the shader caches stay).
if(TEST theme.sheets)
    jah_fresh_home_fixture(theme.sheets.fresh_home themeSheetsFreshHome "${CMAKE_CURRENT_BINARY_DIR}/e2e-home-theme-sheets" WARM LABELS "ui")
    set_property(TEST theme.sheets APPEND PROPERTY FIXTURES_REQUIRED themeSheetsFreshHome)
endif()
