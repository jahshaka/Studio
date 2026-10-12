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
# STAGE-CLOSE (TESTING-SPEED-B2; SPECS/TESTING_SPEED_ROWS.md): the subject is the COLD open from a
# virgin home with a cold shader cache — nine processes on purpose (a pool cannot give an arm a
# virgin home, and would save no boot) — so the nine rows and their setups run at the stage close
# and the ship, never in a merge.
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
    jah_fresh_home_fixture(samples.freshhome.${_slug} samples_home_${_slug} "${_home}" LABELS scripting stage-close)

    jah_gpu_row(samples.cleanstart.${_slug} CLASS app TIER document
             COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                     --script ${CMAKE_CURRENT_BINARY_DIR}/cleanstart_${_slug}.js
             WORKING_DIRECTORY ${_home}/run
             DATA "samples/${SAMPLE}")
    # HOME is the whole point: a virgin one per sample. The suite needs a
    # reachable display like every other engine-up e2e (offscreen rendering
    # still connects to the X server at plugin load).
    set_tests_properties(samples.cleanstart.${_slug} PROPERTIES
        ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
        FIXTURES_REQUIRED samples_home_${_slug}
        LABELS "scripting;stage-close"
        TIMEOUT 480)
endforeach()

# THE MULTI-ARCHIVE SESSION (code review of the cleanup, 2026-09-10): seven
# archives imported into ONE home and opened back to back in ONE process — the
# shape the cleanstart fold lost (cross-archive dedup, catalog growth, live
# scene switches across the process-wide-state class). ~35 s, no pixel probes. STAGE-CLOSE
# (TESTING-SPEED-B2): seven archives in one process is the stage close's, not a merge's.
configure_file(scripts/session.js.in ${CMAKE_CURRENT_BINARY_DIR}/session.js @ONLY)
set(_shome ${CMAKE_CURRENT_BINARY_DIR}/home-session)
file(MAKE_DIRECTORY ${_shome}/run)
jah_fresh_home_fixture(samples.freshhome.session samples_home_session "${_shome}" LABELS scripting stage-close)
jah_gpu_row(samples.session CLASS app TIER epic NEEDS photon
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/session.js
         WORKING_DIRECTORY ${_shome}/run)
set_tests_properties(samples.session PROPERTIES
    ENVIRONMENT "HOME=${_shome};JAHSHAKA_DATA_ROOT=${_shome}/.local/share/Jahshaka"
    FIXTURES_REQUIRED samples_home_session
    LABELS "scripting;stage-close"
    TIMEOUT 1020)  # 6x measured (D6B fix round F1; spikes/d6b-gate-shape/f1-timeouts.txt)

# samples.mirror_room_boots — REFLECT-STATE-1: the Mirror Room sample's picture is ONE picture
# across boots (ATOM-DECODE-CLASS-1 recorded two). Ten boots, each on a virgin data root, the
# scene's offscreen picture after 600 fixed-clock frames, byte-identical (the runner:
# scripts/mirror_room_boots.sh). 251 s measured (25 s a boot): STAGE-CLOSE.
jah_gpu_row(samples.mirror_room_boots CLASS app TIER document
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mirror_room_boots.sh $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mirror_room_boot.js.in ${CMAKE_CURRENT_BINARY_DIR}/mirror-room-boots 10
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
# TESTING-CLEANUP-2B item 2: TIMEOUT 1940 = 6 x the run log's quiet median 322.1 s (31 quiet PASS runs since 2026-10-01, worst 838.8 s);
# declared 1800 was below the policy, honest only with the +900 s widening (now deleted).
set_tests_properties(samples.mirror_room_boots PROPERTIES LABELS "scripting;stage-close" TIMEOUT 1940)

# samples.archive_fidelity.Tornado — LIVE-PERSIST-1, rewritten by TESTING-SPEED-B2 against the ARCHIVE'S OWN
# INVARIANTS (no reference picture: tests/samples/data/tornado_reference.jpg and its metric deleted, forward-only):
# the SHIPPED Tornado archive, opened on a clean machine (a virgin data root, HOME and XDG_CACHE_HOME — no piece
# cache anywhere), closes (every dependency and file of its manifest is in the zip), imports exactly its assets and
# objects, keeps its live graph materials (every layer re-emits its pieces from its graph, the shells keep their
# baked cut-out) after the import AND after a close and reopen, is SEE-THROUGH from its own camera (the named failure:
# a sample that loses its cut-out opens with no holes), and is a FIXED POINT: exported, imported and exported again,
# the same archive (never held against the shipped zip).
# THE HOLES, measured (2026-10-12, spikes/testing-speed-b2/tornado-cutout-off.txt): the shipped archive 0.0165 see-through;
# the Top Shell's cut-out OFF (its map solid) 0.00003 (2 stray px) — so the "> 0" bar does NOT yet separate them: the
# lead's call.
# STAGE-CLOSE: a whole-archive round trip (SPECS/TESTING_SPEED_ROWS.md).
jah_gpu_row(samples.archive_fidelity.Tornado CLASS app TIER document
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/archive_fidelity.sh $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/archive_fidelity_tornado.js.in
                 ${CMAKE_SOURCE_DIR}/scenes/Tornado.zip
                 ${CMAKE_CURRENT_BINARY_DIR}/archive-fidelity-Tornado
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/archive_invariants.py
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(samples.archive_fidelity.Tornado PROPERTIES LABELS "scripting;stage-close" TIMEOUT 480)

# THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B2; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, MEASURED — every engine frame (Engine::renderOneFrame) of every process the row starts,
# the boot's startup-gate frames included, counted on this tree's Debug build on 2026-10-12 (the count and its
# method: spikes/testing-speed-b2/frames_pass1.tsv). A pool's is the sum over its arms. A headless row renders
# the one empty frame a headless engine walks.
if(TEST samples.archive_fidelity.Tornado)
    set_property(TEST samples.archive_fidelity.Tornado PROPERTY JAHSHAKA_TEST_FRAMES 791)
endif()
if(TEST samples.cleanstart.Matcaps)
    set_property(TEST samples.cleanstart.Matcaps PROPERTY JAHSHAKA_TEST_FRAMES 1470)
endif()
if(TEST samples.cleanstart.Mirror_Room)
    set_property(TEST samples.cleanstart.Mirror_Room PROPERTY JAHSHAKA_TEST_FRAMES 1547)
endif()
if(TEST samples.cleanstart.Particles)
    set_property(TEST samples.cleanstart.Particles PROPERTY JAHSHAKA_TEST_FRAMES 1808)
endif()
if(TEST samples.cleanstart.Physics)
    set_property(TEST samples.cleanstart.Physics PROPERTY JAHSHAKA_TEST_FRAMES 1466)
endif()
if(TEST samples.cleanstart.Showroom)
    set_property(TEST samples.cleanstart.Showroom PROPERTY JAHSHAKA_TEST_FRAMES 1301)
endif()
if(TEST samples.cleanstart.Showroom_2)
    set_property(TEST samples.cleanstart.Showroom_2 PROPERTY JAHSHAKA_TEST_FRAMES 1300)
endif()
if(TEST samples.cleanstart.Skeletal_Animation)
    set_property(TEST samples.cleanstart.Skeletal_Animation PROPERTY JAHSHAKA_TEST_FRAMES 1449)
endif()
if(TEST samples.cleanstart.Tornado)
    set_property(TEST samples.cleanstart.Tornado PROPERTY JAHSHAKA_TEST_FRAMES 1302)
endif()
if(TEST samples.cleanstart.World_Background)
    set_property(TEST samples.cleanstart.World_Background PROPERTY JAHSHAKA_TEST_FRAMES 1466)
endif()
if(TEST samples.mirror_room_boots)
    set_property(TEST samples.mirror_room_boots PROPERTY JAHSHAKA_TEST_FRAMES 11348)
endif()
if(TEST samples.session)
    set_property(TEST samples.session PROPERTY JAHSHAKA_TEST_FRAMES 1046)
endif()
