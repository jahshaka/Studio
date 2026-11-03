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
# THE OWNER'S COLD FIRST RUN is samples.cold_open.*'s (below; two launches, one row each): the smallest sample on a virgin home and a cold cache, the
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

# samples.cold_open.import / .open / .clean — THE OWNER'S FIRST RUN AFTER A WIPE (URGENT #48's follow-up, the lead's ruling
# (b)): the smallest shipped sample (Showroom 2) imported and opened from a VIRGIN home on a COLD shader cache, the open's
# law alone - imported, opened, 120 frames, no engine error the suite does not tolerate. SPLIT BY PROOF, ONE ROW A LAUNCH
# (ORPHAN-REDS-1, 2026-11-03, URGENT #136's form): the one-launch row was 47.34 s quiet, 56.18 s at load, and TIMED OUT at
# 60.08 s (§3667: the boot's cold compile at the cap). A fixture chain on one home:
#   .fresh_home  the wipe (jah_fresh_home_fixture WITHOUT WARM: the home and its shader cache) - samplesColdOpenHome;
#   .import  launch 1 (scripts/cold_open_import.js.in) on that virgin home, --clear-shader-cache stated too (so its
#            cold-on prime below compiles cold on any home): the boot compiles cold (the subject), the archive imports,
#            no unexpected engine error; the clean quit saves what it compiled - a RUN fixture, FIXTURES_SETUP
#            samplesColdOpen (its wipe is the fixture before it: a setup whose command is fresh_home.sh reads as a
#            WIPE to the selector, never as a row - gate-scope.py _is_wipe);
#   .open    launch 2 (scripts/cold_open_open.js.in): the app on that home, untouched (its cache = the startup set launch 1
#            compiled, none of the sample's own permutations - the one-launch row's open on the same cache state): the
#            project opens, 120 frames, the frame loop ran, no unexpected engine error;
#   .clean   the FIXTURES_CLEANUP of both fixtures: the home wiped after every part that ran (no launch, no card;
#            URGENT #133).
# Both launches are cold by subject (source.testing_rules R13): never the tree's shared cache.
# THE DRIVER's CACHE IS THE ROW's OWN, NEVER THE SHARED ONE (URGENT #152, 2026-11-03): the subject is a wiped APP home
# (its shader cache, VkPipelineCache included, gone) on a machine whose GPU driver cache survived the wipe - the owner's
# first run. A virgin home's ray pipelines (rq_reflect ~80 s cold in the driver) come from the NVIDIA driver's
# per-executable disk cache alone, and the shared one (~/.cache/nvidia, every tree's and every concurrent row's
# "Jahshaka") was measured to lose them between two holds 12 min apart while app rows ran (both launches then timed out
# at 60.6 s; the cause unverified - spikes/orphan-reds-1/). So the row names its own driver cache, outside the wiped home
# (it survives the wipe, as the owner's does), written by these two rows only, one at a time (the fixture chain), and
# PRIMED by the warm-up's cold-on boot, uncapped (COLD below: its command runs there once a shader key, to its end - the
# driver stores its compiles during the run). The variables ride the COMMAND (the cold-on run reads no ENVIRONMENT
# property); ENVIRONMENT repeats the roots for ctest and the hygiene rules. Launch 2 boots on launch 1's saved
# VkPipelineCache, so the driver's cache only serves what launch 1 compiled.
set(_chome ${CMAKE_CURRENT_BINARY_DIR}/home-cold-open)
set(_cdriver ${CMAKE_CURRENT_BINARY_DIR}/cold-open-driver-cache)
file(MAKE_DIRECTORY ${_chome}/run ${_cdriver})
set(_cenv env HOME=${_chome} JAHSHAKA_DATA_ROOT=${_chome}/.local/share/Jahshaka __GL_SHADER_DISK_CACHE_PATH=${_cdriver})
configure_file(scripts/cold_open_import.js.in ${CMAKE_CURRENT_BINARY_DIR}/cold_open_import.js @ONLY)
configure_file(scripts/cold_open_open.js.in ${CMAKE_CURRENT_BINARY_DIR}/cold_open_open.js @ONLY)
jah_fresh_home_fixture(samples.cold_open.fresh_home samplesColdOpenHome "${_chome}" LABELS scripting)
jah_gpu_row(samples.cold_open.import CLASS app TIER document
         COLD "a virgin home's boot compiles the startup set cold (the subject) - its driver cache is the row's own, primed here"
         COMMAND ${_cenv} $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --clear-shader-cache
                 --script ${CMAKE_CURRENT_BINARY_DIR}/cold_open_import.js
         WORKING_DIRECTORY ${_chome}/run
         DATA "samples/Showroom 2")
set_tests_properties(samples.cold_open.import PROPERTIES
    ENVIRONMENT "HOME=${_chome};JAHSHAKA_DATA_ROOT=${_chome}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 60
    FIXTURES_REQUIRED samplesColdOpenHome
    FIXTURES_SETUP samplesColdOpen
    # the owner's cold first run is the subject: source.testing_rules R13 lets this root boot cold
    JAHSHAKA_COLD_BY_SUBJECT TRUE)
jah_gpu_row(samples.cold_open.open CLASS app TIER document
         COMMAND ${_cenv} $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --script ${CMAKE_CURRENT_BINARY_DIR}/cold_open_open.js
         WORKING_DIRECTORY ${_chome}/run
         DATA "samples/Showroom 2")
set_tests_properties(samples.cold_open.open PROPERTIES
    ENVIRONMENT "HOME=${_chome};JAHSHAKA_DATA_ROOT=${_chome}/.local/share/Jahshaka"
    LABELS "scripting"
    TIMEOUT 60
    FIXTURES_REQUIRED "samplesColdOpenHome;samplesColdOpen"
    # its cache is launch 1's (the subject's second half), never the shared warm one
    JAHSHAKA_COLD_BY_SUBJECT TRUE)
# (fresh_home.sh with no command: the home wiped and its empty run/ re-made - the rows' working directory must exist for
# ctest to start them; a bare rm -rf of the home left the next .import "Not Run")
add_test(NAME samples.cold_open.clean COMMAND sh ${JAH_FRESH_HOME} ${_chome})
set_tests_properties(samples.cold_open.clean PROPERTIES TIMEOUT 60 LABELS "scripting" FIXTURES_CLEANUP "samplesColdOpenHome;samplesColdOpen")
jah_no_display(samples.cold_open.clean)
# (their frames, counted once per launch by a per-process Ogre::Root::_fireFrameEnded counter - an LD_PRELOAD meter in a
# scratch dir, never committed - 2026-11-03: 584 (the cold boot's startup gate) and 155 (the warm boot, the open, the
# script's 120); the one-launch row's 706 was the startup gate's 523, the open's and the script's 120 among them)
set_property(TEST samples.cold_open.import PROPERTY JAHSHAKA_TEST_FRAMES 584)
set_property(TEST samples.cold_open.open PROPERTY JAHSHAKA_TEST_FRAMES 155)
jah_ship_row(samples.cleanstart.Tornado)   # THE SHIP SET (TEST-CUT-1)

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
jah_gpu_row(samples.mirror_room_boots.boot1 CLASS app TIER document
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mirror_room_boots.sh $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_CURRENT_SOURCE_DIR}/scripts/mirror_room_boot.js.in ${CMAKE_CURRENT_BINARY_DIR}/mirror-room-boots 1
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(samples.mirror_room_boots.boot1 PROPERTIES LABELS "scripting" TIMEOUT 60
    FIXTURES_SETUP samples_mirror_room_boot1)
jah_gpu_row(samples.mirror_room_boots.boot2 CLASS app TIER document
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
