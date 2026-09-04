# samples.cleanstart.* — the owner's post-wipe first run, one test per shipped
# sample, each on a HOME of its own.
#
# The shipped samples are the ONE data artifact Jahshaka ships, and the owner's
# database is wiped for a clean start: "the sample opens" has to mean "the
# sample opens on a machine that has never run this app". So every sample gets
# its own scratch HOME (a fresh library, a fresh asset store, no leftovers from
# the sample before it) and its own ctest test, which also means a red sample
# names itself instead of taking the other five down with it.
#
# Each test drives the REAL binary with --script, engine-up (no --headless):
# it imports the archive through project.importArchive (what the sample
# dialog's double-click runs), opens it, renders 120 fixed-dt frames, reads
# app.engineErrors(), round-trips the document through save/close/reopen, runs
# assets.bakeAll and reopens again. See scripts/cleanstart.js.in for what each
# step asserts and why.
#
# Complementary to scripting.e2e.samples, which keeps the exact-colour material
# probes in one shared session; this suite owns the clean-start contract and is
# the only coverage Showroom has.

set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
set(_samples "Matcaps" "Particles" "Physics" "Showroom" "Skeletal Animation" "World Background")

foreach(SAMPLE IN LISTS _samples)
    string(REPLACE " " "_" _slug "${SAMPLE}")
    configure_file(scripts/cleanstart.js.in
                   ${CMAKE_CURRENT_BINARY_DIR}/cleanstart_${_slug}.js @ONLY)
    set(_home ${CMAKE_CURRENT_BINARY_DIR}/home-${_slug})
    file(MAKE_DIRECTORY ${_home}/run)

    # VIRGIN, EVERY RUN. A HOME that survived the previous ctest is not a clean
    # start: its library already holds the sample, its store already holds the
    # objects, and the import half of this gate would be testing a re-import.
    # The setup fixture wipes and recreates the directory before the test runs.
    add_test(NAME samples.freshhome.${_slug}
             COMMAND ${CMAKE_COMMAND} -DHOME_DIR=${_home}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/freshhome.cmake)
    set_tests_properties(samples.freshhome.${_slug} PROPERTIES
        FIXTURES_SETUP samples_home_${_slug}
        LABELS "scripting")

    add_test(NAME samples.cleanstart.${_slug}
             COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                     --script ${CMAKE_CURRENT_BINARY_DIR}/cleanstart_${_slug}.js
             WORKING_DIRECTORY ${_home}/run)
    # HOME is the whole point: a virgin one per sample. The suite needs a
    # reachable display like every other engine-up e2e (offscreen rendering
    # still connects to the X server at plugin load).
    set_tests_properties(samples.cleanstart.${_slug} PROPERTIES
        ENVIRONMENT "HOME=${_home}"
        FIXTURES_REQUIRED samples_home_${_slug}
        LABELS "scripting"
        TIMEOUT 480)
endforeach()
