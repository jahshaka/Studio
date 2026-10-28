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
# A VIRGIN HOME ON A WARM SHADER CACHE (URGENT #48's follow-up, the lead's ruling (b), 2026-10-24): each row's home is
# wiped before every run by its own wrapper (fresh_home.sh --warm) and its boot's shader cache seeded from the tree's
# shared app cache - a cold compile no longer fits the 60 s cap (samples.cleanstart.Particles timed out at 60.33 s on
# the photon-7r merge's tree; it was priced 52.59 s cold). Every import, open, save, reopen and bake assertion stays.
# THE OWNER'S COLD FIRST RUN is samples.cold_open's (below): the smallest sample on a virgin home and a cold cache, the
# open's law alone. Nine processes on purpose (a pool cannot give an arm a virgin home).
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
    # The wrapper wipes and recreates the directory before the app starts (its
    # setup fixture samples.freshhome.* deleted with the cold cache, #48's follow-up).
    jah_gpu_row(samples.cleanstart.${_slug} CLASS app TIER document
             COMMAND sh ${JAH_FRESH_HOME} --warm ${_home} -- $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                     --script ${CMAKE_CURRENT_BINARY_DIR}/cleanstart_${_slug}.js
             WORKING_DIRECTORY ${_home}/run
             DATA "samples/${SAMPLE}")
    # HOME is the whole point: a virgin one per sample. The suite needs a
    # reachable display like every other engine-up e2e (offscreen rendering
    # still connects to the X server at plugin load).
    set_tests_properties(samples.cleanstart.${_slug} PROPERTIES
        ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
        LABELS "scripting"
        TIMEOUT 60)
endforeach()

# samples.cold_open — THE OWNER'S FIRST RUN AFTER A WIPE (URGENT #48's follow-up, the lead's ruling (b)): the smallest
# shipped sample (Showroom 2) imported and opened from a VIRGIN home on a COLD shader cache (fresh_home.sh WITHOUT
# --warm: the home's cache wiped with it), the open's law alone - imported, opened, 120 frames, no engine error the
# suite does not tolerate (scripts/cold_open.js.in). Measured once: 47.34 s (the boot's cold compile is most of it).
set(_chome ${CMAKE_CURRENT_BINARY_DIR}/home-cold-open)
file(MAKE_DIRECTORY ${_chome}/run)
configure_file(scripts/cold_open.js.in ${CMAKE_CURRENT_BINARY_DIR}/cold_open.js @ONLY)
jah_gpu_row(samples.cold_open CLASS app TIER document
         COMMAND sh ${JAH_FRESH_HOME} ${_chome} -- $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/cold_open.js
         WORKING_DIRECTORY ${_chome}/run
         DATA "samples/Showroom 2")
set_tests_properties(samples.cold_open PROPERTIES
    ENVIRONMENT "HOME=${_chome};JAHSHAKA_DATA_ROOT=${_chome}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 60
    # the owner's cold first run is the subject: source.testing_rules R13 lets this root boot cold
    JAHSHAKA_COLD_BY_SUBJECT TRUE)
# (its frames, counted once in a scratch tree off the tip with a per-process renderOneFrame counter, never committed:
# 706 - the startup gate's 523 and the open's and the script's 120 among them)
set_property(TEST samples.cold_open PROPERTY JAHSHAKA_TEST_FRAMES 706)
jah_ship_row(samples.cleanstart.Matcaps samples.cleanstart.Mirror_Room samples.cleanstart.Particles samples.cleanstart.Physics samples.cleanstart.Showroom samples.cleanstart.Showroom_2 samples.cleanstart.Skeletal_Animation samples.cleanstart.Tornado samples.cleanstart.World_Background)   # THE SHIP SET (TEST-CUT-1)

# THE MULTI-ARCHIVE SESSION (code review of the cleanup, 2026-09-10): seven
# archives imported into ONE home and opened back to back in ONE process — the
# shape the cleanstart fold lost (cross-archive dedup, catalog growth, live
# scene switches across the process-wide-state class). ~35 s, no pixel probes. STAGE-CLOSE
# (TESTING-SPEED-B2): seven archives in one process is the stage close's, not a merge's.
set(_shome ${CMAKE_CURRENT_BINARY_DIR}/home-session)
file(MAKE_DIRECTORY ${_shome}/run)

# samples.mirror_room_boots.boot1 / .boot2 — REFLECT-STATE-1: the Mirror Room sample's picture is ONE picture across
# boots (ATOM-DECODE-CLASS-1 recorded two). Each row is ONE boot on a virgin data root: the scene's offscreen picture
# after 600 fixed-clock frames; boot 2 requires it byte-identical to boot 1's (the runner: scripts/mirror_room_boots.sh).
# REBUILT TO ITS LAW (TEST-CUT-1; CLAUDE.md Testing: every row at most 60 s). The law is an equality of pictures across
# boots, and an equality needs TWO boots — not ten (the row was ten in one process chain: 322 s, 11,348 frames). Each boot
# keeps its own frames whole — the open's own polls and app.waitForTextures(), then the 600-frame settle the picture is
# taken after (~1,130 frames a boot, unchanged) — and the two run as two rows, a fixture chain: boot 1 sets up the
# picture, boot 2 requires it (DEPENDS + FIXTURES_REQUIRED), so neither row is over the minute. Measured once each
# (2026-10-20, solo, the card lock held): 31.8 s / 1,122 frames and 32.1 s / 1,124. TIMEOUT 6 x the solo run, rounded up.
# THE SHADER CACHE WARM (PHOTON-7 stage 11, the lead's ruling): the data root stays virgin (settings, library, store) and
# the boots take the tree's shared shader cache (fresh_home.sh --warm) - the cache is not this law's; a compile-timed
# picture is COLD-BOOT-2's class, with its own cold row (mirror_room_boots.sh's header says why). 30.9 s / 26.0 s warm.
jah_gpu_row(samples.mirror_room_boots.boot1 SHIP CLASS app TIER document
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mirror_room_boots.sh $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mirror_room_boot.js.in ${CMAKE_CURRENT_BINARY_DIR}/mirror-room-boots 1
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(samples.mirror_room_boots.boot1 PROPERTIES LABELS "scripting" TIMEOUT 60
    FIXTURES_SETUP samples_mirror_room_boot1)
jah_gpu_row(samples.mirror_room_boots.boot2 SHIP CLASS app TIER document
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mirror_room_boots.sh $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mirror_room_boot.js.in ${CMAKE_CURRENT_BINARY_DIR}/mirror-room-boots 2
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(samples.mirror_room_boots.boot2 PROPERTIES LABELS "scripting" TIMEOUT 60
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
