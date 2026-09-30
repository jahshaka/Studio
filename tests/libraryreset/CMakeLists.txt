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

# app.library_generation (FORWARD-ONLY-1, services/librarygeneration.h): a fresh
# data root is not wiped, a library of an OLDER generation (PRAGMA user_version)
# is wiped at startup, and an older library whose data root another instance
# holds (<db>.lock) is refused with exit 4 and left untouched. Headless boots.
set(_gen_home ${CMAKE_CURRENT_BINARY_DIR}/library-generation-home)
file(MAKE_DIRECTORY ${_gen_home}/run)
add_test(NAME app.library_generation
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/generation.sh" "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
         WORKING_DIRECTORY ${_gen_home}/run)
set_tests_properties(app.library_generation PROPERTIES
    ENVIRONMENT "HOME=${_gen_home}" TIMEOUT 300 LABELS "app")
set_property(TEST app.library_generation APPEND PROPERTY ENVIRONMENT_MODIFICATION
             "JAHSHAKA_DATA_ROOT=unset:")
jah_no_display(app.library_generation)
