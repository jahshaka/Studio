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
# THE ONLY SAMPLE GATE (TEST_GATE_AUDIT.md §2, 2026-09-09). It absorbed
# scripting.e2e.samples (exact-colour material probes for five samples in one
# shared session, plus the Particles save round trip) and the seven
# samples.scale.* rows (scene.bounds / editor.camera against the scene-scale
# convention). Both re-imported archives this suite had already imported into a
# virgin home; every assertion they made is in scripts/cleanstart.js.in now,
# where it runs per sample against the owner's real first run.

set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
set(_samples "Matcaps" "Mirror Room" "Particles" "Physics" "Showroom" "Showroom 2" "Skeletal Animation" "Tornado" "World Background")

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
    jah_fresh_home_fixture(samples.freshhome.${_slug} samples_home_${_slug} "${_home}" LABELS "scripting")

    jah_gpu_row(samples.cleanstart.${_slug} CLASS app TIER document
             COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                     --script ${CMAKE_CURRENT_BINARY_DIR}/cleanstart_${_slug}.js
             WORKING_DIRECTORY ${_home}/run)
    # HOME is the whole point: a virgin one per sample. The suite needs a
    # reachable display like every other engine-up e2e (offscreen rendering
    # still connects to the X server at plugin load).
    set_tests_properties(samples.cleanstart.${_slug} PROPERTIES
        ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
        FIXTURES_REQUIRED samples_home_${_slug}
        LABELS "scripting"
        TIMEOUT 480)
endforeach()

# THE MULTI-ARCHIVE SESSION (code review of the cleanup, 2026-09-10): seven
# archives imported into ONE home and opened back to back in ONE process — the
# shape the cleanstart fold lost (cross-archive dedup, catalog growth, live
# scene switches across the process-wide-state class). ~35 s, no pixel probes.
configure_file(scripts/session.js.in ${CMAKE_CURRENT_BINARY_DIR}/session.js @ONLY)
set(_shome ${CMAKE_CURRENT_BINARY_DIR}/home-session)
file(MAKE_DIRECTORY ${_shome}/run)
jah_fresh_home_fixture(samples.freshhome.session samples_home_session "${_shome}" LABELS "scripting")
jah_gpu_row(samples.session CLASS app TIER epic NEEDS photon
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/session.js
         WORKING_DIRECTORY ${_shome}/run)
set_tests_properties(samples.session PROPERTIES
    ENVIRONMENT "HOME=${_shome};JAHSHAKA_DATA_ROOT=${_shome}/.local/share/Jahshaka"
    FIXTURES_REQUIRED samples_home_session
    LABELS "scripting"
    TIMEOUT 1020)  # 6x measured (D6B fix round F1; spikes/d6b-gate-shape/f1-timeouts.txt)

# samples.mirror_room_boots — REFLECT-STATE-1: the Mirror Room sample's picture is ONE picture
# across boots (ATOM-DECODE-CLASS-1 recorded two). Ten boots, each on a virgin data root, the
# scene's offscreen picture after 600 fixed-clock frames, byte-identical (the runner:
# scripts/mirror_room_boots.sh). 251 s measured (25 s a boot): NIGHTLY.
jah_gpu_row(samples.mirror_room_boots CLASS app TIER document
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mirror_room_boots.sh $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mirror_room_boot.js.in ${CMAKE_CURRENT_BINARY_DIR}/mirror-room-boots 10
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(samples.mirror_room_boots PROPERTIES LABELS "scripting;nightly" TIMEOUT 1800)

# samples.archive_fidelity.Tornado — LIVE-PERSIST-1: the SHIPPED Tornado archive, opened on a clean
# machine (a virgin data root, HOME and XDG_CACHE_HOME — no piece cache anywhere), keeps its live
# graph materials: every layer re-emits its pieces from its graph, the shells keep their baked
# cut-out, and the torn silhouette at tornado.jpg's camera (detached flaps + see-through, the mean
# of four pinned shader times) is within 25 % of the reference still and 10 % of the in-memory scene
# make_tornado.js builds. The archive that shipped before this lane opens smooth: 0 px / 0.00x.
jah_gpu_row(samples.archive_fidelity.Tornado CLASS app TIER document
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/archive_fidelity.sh $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/archive_fidelity_tornado.js.in
                 ${CMAKE_SOURCE_DIR}/scenes/Tornado.zip
                 ${CMAKE_CURRENT_BINARY_DIR}/archive-fidelity-Tornado
                 ${CMAKE_CURRENT_SOURCE_DIR}/data/tornado_reference.jpg
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/tornado_metrics.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(samples.archive_fidelity.Tornado PROPERTIES LABELS "scripting" TIMEOUT 480)
