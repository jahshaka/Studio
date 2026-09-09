# app.data_root — THE DATA-ROOT OVERRIDE (ENGINEERING_DEBT_SPEC ADDENDUM 6,
# WINDOWS_BUILD_SPEC §6.2 W9). `--data-root <dir>` / `JAHSHAKA_DATA_ROOT`
# redirects the library database, the asset store, the shader cache AND the
# settings file together — the last of which is the half a scratch `HOME` could
# never buy, because under QT_DEBUG jahsettings.ini is derived from
# applicationDirPath() and is therefore SHARED by every run of a build tree.
#
# Runs the REAL binary four times (both override forms, the precedence between
# them, and no override at all) and asserts the reported paths, the files that
# appear under the root, and that the shared settings file beside the binary is
# byte-for-byte unchanged. Document verbs only -> --headless, no display.
set(_dr_home ${CMAKE_CURRENT_BINARY_DIR}/data-root-home)
file(MAKE_DIRECTORY ${_dr_home}/run)
add_test(NAME app.data_root
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/data_root.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
         WORKING_DIRECTORY ${_dr_home}/run)
set_tests_properties(app.data_root PROPERTIES
    ENVIRONMENT "HOME=${_dr_home}"
    TIMEOUT 300
    LABELS "app")
# --headless boots the NULL render system: the whole point of this suite is
# paths, never a pixel. Run with DISPLAY stripped so that stays true.
jah_no_display(app.data_root)
