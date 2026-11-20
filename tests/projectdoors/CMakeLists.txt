# tests/projectdoors — THE PROJECT'S DOORS (STUDIO-HYGIENE-1): what the open, the import and the save
# accept and refuse, driven through the verbs on the headless harness (the NULL render system: no display,
# no GPU).

# project.refuses_unreadable — a file this build cannot read is refused, never half-read (stage 6):
# a v3 manifest at import, a v3 scene and an unparseable scene at open; the v2 source opens (the control).
# Two --headless processes and a python step that rewrites the first one's export (project_doors.sh).
set(_home ${CMAKE_CURRENT_BINARY_DIR}/projectdoors-home-refuse)
jah_fresh_home_fixture(project.refuses_unreadable.fresh_home projectDoorsRefuseHome "${_home}" LABELS "scripting")   # NULL render system (--headless): no shader cache - no warm-up product (TEST-STREAMLINE-1 1b)
file(MAKE_DIRECTORY ${_home}/run)
# The JS half and `--headless` stand VISIBLY on the command line: gate-scope reads the row's verbs from its .js
# argument and its kind (a headless script) from the flag, which the wrapper forwards to both processes.
add_test(NAME project.refuses_unreadable
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/project_doors.sh
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_CURRENT_SOURCE_DIR}/project_doors.js
                 ${CMAKE_CURRENT_SOURCE_DIR}/rewrite_archive.py --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(project.refuses_unreadable PROPERTIES
    JAHSHAKA_HEADLESS TRUE   # project_doors.sh runs the app --headless only: the NULL render system (testing_rules R13)
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED projectDoorsRefuseHome
    LABELS "scripting"
    TIMEOUT 60)
jah_no_display(project.refuses_unreadable)

# project.remove_folder — the one delete (stage 2): a project with a web export removed through project.remove
# (the tile's Delete is the same function) — the row gone, the open project and an unknown guid refused, and
# after the process the folder gone with nothing left aside. Two --headless processes (project_remove.sh).
set(_home ${CMAKE_CURRENT_BINARY_DIR}/projectdoors-home-remove)
jah_fresh_home_fixture(project.remove_folder.fresh_home projectDoorsRemoveHome "${_home}" LABELS "scripting")   # NULL render system (--headless): no shader cache - no warm-up product (TEST-STREAMLINE-1 1b)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME project.remove_folder
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/project_remove.sh
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_CURRENT_SOURCE_DIR}/project_remove.js --headless
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(project.remove_folder PROPERTIES
    JAHSHAKA_HEADLESS TRUE   # project_remove.sh runs the app --headless only: the NULL render system (testing_rules R13)
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    FIXTURES_REQUIRED projectDoorsRemoveHome
    LABELS "scripting"
    TIMEOUT 60)
jah_no_display(project.remove_folder)
