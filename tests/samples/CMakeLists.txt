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
    jah_fresh_home_fixture(samples.freshhome.${_slug} samples_home_${_slug} "${_home}" LABELS scripting)

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
        LABELS "scripting"
        TIMEOUT 480)
endforeach()
jah_ship_row(samples.cleanstart.Matcaps samples.cleanstart.Mirror_Room samples.cleanstart.Particles samples.cleanstart.Physics samples.cleanstart.Showroom samples.cleanstart.Showroom_2 samples.cleanstart.Skeletal_Animation samples.cleanstart.Tornado samples.cleanstart.World_Background)   # THE SHIP SET (TEST-CUT-1)

# THE MULTI-ARCHIVE SESSION (code review of the cleanup, 2026-09-10): seven
# archives imported into ONE home and opened back to back in ONE process — the
# shape the cleanstart fold lost (cross-archive dedup, catalog growth, live
# scene switches across the process-wide-state class). ~35 s, no pixel probes. STAGE-CLOSE
# (TESTING-SPEED-B2): seven archives in one process is the stage close's, not a merge's.
set(_shome ${CMAKE_CURRENT_BINARY_DIR}/home-session)
file(MAKE_DIRECTORY ${_shome}/run)
  # 6x measured (D6B fix round F1; spikes/d6b-gate-shape/f1-timeouts.txt)

# samples.mirror_room_boots.boot1 / .boot2 — REFLECT-STATE-1: the Mirror Room sample's picture is ONE picture across
# boots (ATOM-DECODE-CLASS-1 recorded two). Each row is ONE boot on a virgin data root: the scene's offscreen picture
# after 600 fixed-clock frames; boot 2 requires it byte-identical to boot 1's (the runner: scripts/mirror_room_boots.sh).
# REBUILT TO ITS LAW (TEST-CUT-1; CLAUDE.md Testing: every row at most 60 s). The law is an equality of pictures across
# boots, and an equality needs TWO boots — not ten (the row was ten in one process chain: 322 s, 11,348 frames). Each boot
# keeps its own frames whole — the open's own polls and app.waitForTextures(), then the 600-frame settle the picture is
# taken after (~1,130 frames a boot, unchanged) — and the two run as two rows, a fixture chain: boot 1 sets up the
# picture, boot 2 requires it (DEPENDS + FIXTURES_REQUIRED), so neither row is over the minute. Measured once each
# (2026-10-20, solo, the card lock held): 31.8 s / 1,122 frames and 32.1 s / 1,124. TIMEOUT 6 x the solo run, rounded up.
jah_gpu_row(samples.mirror_room_boots.boot1 SHIP CLASS app TIER document
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mirror_room_boots.sh $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mirror_room_boot.js.in ${CMAKE_CURRENT_BINARY_DIR}/mirror-room-boots 1
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(samples.mirror_room_boots.boot1 PROPERTIES LABELS "scripting" TIMEOUT 200
    FIXTURES_SETUP samples_mirror_room_boot1)
jah_gpu_row(samples.mirror_room_boots.boot2 SHIP CLASS app TIER document
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mirror_room_boots.sh $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mirror_room_boot.js.in ${CMAKE_CURRENT_BINARY_DIR}/mirror-room-boots 2
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(samples.mirror_room_boots.boot2 PROPERTIES LABELS "scripting" TIMEOUT 200
    FIXTURES_REQUIRED samples_mirror_room_boot1 DEPENDS samples.mirror_room_boots.boot1)

# THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B2; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, MEASURED — every engine frame (Engine::renderOneFrame) of every process the row starts,
# the boot's startup-gate frames included, counted on this tree's Debug build on 2026-10-12 (the count and its
# method: spikes/testing-speed-b2/frames_pass1.tsv). A pool's is the sum over its arms. A headless row renders
# the one empty frame a headless engine walks.
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
if(TEST samples.mirror_room_boots.boot1)
    set_property(TEST samples.mirror_room_boots.boot1 PROPERTY JAHSHAKA_TEST_FRAMES 1122)
    set_property(TEST samples.mirror_room_boots.boot2 PROPERTY JAHSHAKA_TEST_FRAMES 1124)
endif()
