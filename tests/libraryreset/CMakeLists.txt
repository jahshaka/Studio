# app.reset_library — `app.resetLibrary()` (owner review R10.2,
# services/libraryreset.h): the whole library goes and the app is a first
# launch again. Runs the REAL binary on two scratch data roots — one fresh, one
# populated — and compares their censuses, then checks the store, the projects
# root and the database on disk. Document verbs only -> --headless, no display.
set(_rl_home ${CMAKE_CURRENT_BINARY_DIR}/reset-library-home)
file(MAKE_DIRECTORY ${_rl_home}/run)
add_test(NAME app.reset_library
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/reset_library.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                 "${CMAKE_SOURCE_DIR}/tests/scripting/fixtures/tiny.png"
         WORKING_DIRECTORY ${_rl_home}/run)
# HOME only: the two data roots are passed per run with --data-root (the suite
# is ABOUT what lives under a data root), and an ambient JAHSHAKA_DATA_ROOT
# would put both of them in one place.
set_tests_properties(app.reset_library PROPERTIES
    ENVIRONMENT "HOME=${_rl_home}"
    TIMEOUT 300
    LABELS "app")
set_property(TEST app.reset_library APPEND PROPERTY ENVIRONMENT_MODIFICATION
             "JAHSHAKA_DATA_ROOT=unset:")
jah_no_display(app.reset_library)
