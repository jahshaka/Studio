# Application-level self-test: the real Jahshaka binary, engine viewport, default
# scene, one screenshot. On Linux it needs a display (Ogre-Next renders through
# xcb): DISPLAY=:0 on the build machine. On macOS the viewport renders offscreen
# (no window backend yet) and forcing xcb aborts Qt at startup — no env needed.
# The display comes from the JAH_TEST_DISPLAY cache variable (tests/CMakeLists.txt).
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(JAH_SELFTEST_ENV "${JAH_TEST_DISPLAY_ENV}QT_QPA_PLATFORM=xcb;")
else()
    set(JAH_SELFTEST_ENV "")
endif()
# THE LAST TWO SUITES THAT WROTE THE SHARED SETTINGS FILE (hygiene lane,
# 2026-09-09). These two run with their WORKING_DIRECTORY set to the BINARY's
# directory — deliberately, so `media/` and the relative ogre log resolve the
# way a developer's own launch resolves them — and they carried no scratch
# HOME, so under QT_DEBUG they wrote (and re-rolled `[assets] storeId` in)
# build-linux/bin/jahsettings.ini: the developer's own file, which is the
# ENGINEERING_DEBT_SPEC ADDENDUM 6 defect exactly. Measured after a full 249-
# suite gate: these were the only two left doing it.
#
# The working directory is UNCHANGED (nothing about asset resolution moves);
# only the data root is redirected, which takes the settings file, the library
# database and the asset store with it.
set(_selftest_root ${CMAKE_CURRENT_BINARY_DIR}/selftest-data)
file(MAKE_DIRECTORY ${_selftest_root})
set(JAH_SELFTEST_ENV "${JAH_SELFTEST_ENV}JAHSHAKA_DATA_ROOT=${_selftest_root};")

# THE SELF-TEST, ONCE — under the validation layer (TEST_GATE_AUDIT.md §2).
# There used to be a second row, app.engine_selftest, running the identical
# command WITHOUT the layer. It was a strict subset: this one fails on the
# process return code too, so everything the plain row could catch this row
# catches, and both wrote into the same data root, which is one of the things
# -j4 could not have. The `--engine-selftest` binary path itself is unchanged
# (it is also how the sha256 smoke is taken by hand).
#
# Any "Validation Error" in the output fails the test. This is what caught the
# stale depth buffer on resize. A SECOND arm (smoke L10 item 3) re-runs the
# self-test with its ground deliberately broken and requires exit 1: the
# self-test used to pass a groundless default scene because the sky alone
# satisfies its centre-pixel check (engine_selftest.sh says why, in full).
# OUT OF THE GPU-TIMING LOCK (lane D6B-GATE-SHAPE; audit §4 R4, FIX-LOCK): it measures no time
# (a hash-free selftest under the layer; it was in the lock by flake history only).
# budget 50 s: the measurement (TESTING-SPEED-B2, the lead 2026-10-12); the cold path is the subject — the boot lane lowers it.
# THE DATA ROOT IS WIPED BEFORE EVERY RUN (2026-10-20): selftest-data persists across runs of one build dir, and a
# JahLibrary.db left by an older build carries an older schema — the app's reset keeps the storages, wants their sidecar
# directory, an older AssetStore has none, the library is REFUSED and the self-test exits 4 ("the self-test exited 4" on
# main's build-linux in an 8-wide run; green in a fresh ship tree). A row owns its own state (shadercache.app 67cefee33,
# api.contract cd0587fdc: the same fault).
jah_fresh_home_fixture(app.engine_selftest_validation.fresh_root selftest_data_root "${_selftest_root}" LABELS "app")
jah_ship_row(app.engine_selftest_validation.fresh_root)
jah_gpu_row(app.engine_selftest_validation SHIP CLASS selftest
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/engine_selftest.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${CMAKE_CURRENT_BINARY_DIR}"
         WORKING_DIRECTORY "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>")
set_tests_properties(app.engine_selftest_validation PROPERTIES
    ENVIRONMENT "${JAH_SELFTEST_ENV}VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation"
    FIXTURES_REQUIRED selftest_data_root
    TIMEOUT 660 LABELS "app")  # 6x measured (D6B fix round F1; spikes/d6b-gate-shape/f1-timeouts.txt)

# app.startup_quiet — the boot log stays clean (STABILITY_PROGRAM_SPEC lanes
# 6a/6b/6c). Three noise sources were fixed together and all three are only
# observable in a REAL boot, so this is the one suite that reads the boot log:
#
#   6b  "duplicate connection name 'qt_sql_default_connection'" — Database::
#       closeDatabase() read the connection name after invalidating the handle,
#       so removeDatabase("") no-op'd and the boot's schema check leaked the
#       DEFAULT connection into MainWindow::setupProjectDB.
#   6c  "invalid parameters in SMAA.material(83)" — fork c290052de (was 0012).
#   6a  Qt Multimedia on the startup path (iris::Scene and VideoPreviewWidget
#       both built a QMediaPlayer in their constructors). ui.media_lazy is the
#       precise guard; this one catches anything ELSE that starts constructing
#       media objects at boot. Keyed on Qt's own "qt.multimedia.ffmpeg: Using
#       Qt multimedia with FFmpeg" banner, which the backend prints when it is
#       first instantiated — a one-way guard (if a future Qt silences the
#       banner this assertion weakens, ui.media_lazy does not).
#
# TWO runs under ONE scratch HOME on purpose: the 6b warning needs a database
# to already exist, because main()'s schema check (FORWARD-ONLY-1) opens one
# only when there is one — a single fresh-HOME boot could never see it.
# Deliberately NOT pinned to DISPLAY=:0 (unlike the selftests above): it
# inherits the session display exactly like the scripting e2e suites, so it
# runs on whatever Xvfb the gate uses.
set(_sq_home ${CMAKE_CURRENT_BINARY_DIR}/startup-quiet-home)
file(MAKE_DIRECTORY ${_sq_home}/run)

# app.test_needs_boot — A TEST PROCESS BOOTS WHAT ITS ROW NEEDS (TEST-NEEDS-1; services/testtier.h,
# worldmodes::applyTestTier). Three boots of one home under their own JAHSHAKA_TEST_TIER /
# JAHSHAKA_TEST_NEEDS — `high photon` (Photon High, bloom/SSAO/SMAA/planar off), `low none`
# (everything switchable off), `epic photon bloom` (both on, the rest off) — each asserting a NEW
# scene and an OPENED one saved with everything on, and app.testTier() naming the declaration;
# then the refusals (an unknown word, `none` beside another, a list with no tier: exit 2). The
# row's own declaration is its heaviest boot (the tokens it must hold).
set(_tnb_home ${CMAKE_CURRENT_BINARY_DIR}/test-needs-boot-home)
file(MAKE_DIRECTORY ${_tnb_home}/run)
jah_gpu_row(app.test_needs_boot SHIP CLASS app TIER epic NEEDS photon bloom
         COMMAND sh ${JAH_FRESH_HOME} --warm ${_tnb_home} --
                 bash "${CMAKE_CURRENT_SOURCE_DIR}/test_needs_boot.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_needs_boot.js"
         WORKING_DIRECTORY ${_tnb_home}/run)
set_tests_properties(app.test_needs_boot PROPERTIES
    ENVIRONMENT "HOME=${_tnb_home};JAHSHAKA_DATA_ROOT=${_tnb_home}/.local/share/Jahshaka"
    TIMEOUT 300
    LABELS "app")

# app.start_in_vr — THE VR PREFERENCES (VR-START-1): Start in VR (on) and the headset runtime (WiVRn);
# a policy refusal opens no OpenXR runtime and ends in the editor plus one notice or dialog.
# Three boots of one fresh home (start_in_vr.sh). `JAHSHAKA_VR=` (empty) keeps the tests/ walk
# from adding JAHSHAKA_VR=0: this row is the one that lets the preference decide.
set(_siv_home ${CMAKE_CURRENT_BINARY_DIR}/start-in-vr-home)
file(MAKE_DIRECTORY ${_siv_home}/run)

# app.input_keys — EVERY INPUT-SYNTHESIS ASSERTION THE EDITOR HAS, ON ONE BOOT.
#
# This was three ctest rows until 2026-09-09: app.pacing_undo (the frame-pacing
# loop, deep audit area 7 F8, plus the Ctrl+Z routing of area 1),
# app.multiselect_keys (the four edit chords in both spaces,
# EDITOR_MULTISELECT_SPEC §2.6/§5) and app.navigation_keys (the arrows fly, the
# letters are free). Each started its own Xvfb, booted its own Jahshaka under
# --mcp-port, hunted the main window and clicked it — three identical ~10 s
# preambles — and all three were RUN_SERIAL, so the gate paid for them end to
# end (TEST_GATE_AUDIT.md §5 step 6). They are three PARTS of one question, and
# input_keys.sh asks all three in one window with every assertion and its
# wording intact.
#
# RUN_SERIAL STAYS. Key ARRIVAL is timing-sensitive under load — the
# WM-less-Xvfb arrival class in CLAUDE.md's gate facts — and this is now one
# serial row instead of three.
#
# NOTE THE MISSING DISPLAY= : this is the one suite that does NOT take
# JAH_TEST_DISPLAY. It synthesises input, and JAH_TEST_DISPLAY may name a
# developer's live session — so it starts and owns a private Xvfb instead (the
# script explains it fully). Linux only, and registered only when the four tools
# it drives exist, so a box without them says so at configure time instead of
# failing mysteriously.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(JAH_XDOTOOL xdotool)
    find_program(JAH_CURL curl)
    find_program(JAH_JQ jq)
    find_program(JAH_XVFB Xvfb)
endif()

# pool.runner — THE POOL RUNNER'S OWN TEST (tests/support/test_run_pool.py; the runner is
# src/app/cli/scriptrunner.cpp, so the row lives with the app's): the real app,
# headless, over five fixture arms (a fresh realm and a closed project between arms, a thrown
# assertion and a numeric completion value as FAIL, the pool going on, JAH_POOL_ARMS's subset
# and its unknown-arm FAIL), and the driver against a stand-in app for what the real one cannot
# be asked to do (a SIGSEGV mid-arm, a hang past the budget, a lost baseline — one arm each).
set(_home ${CMAKE_CURRENT_BINARY_DIR}/pool-home-runner)
file(MAKE_DIRECTORY ${_home}/run)

# THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B2; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, MEASURED — every engine frame (Engine::renderOneFrame) of every process the row starts,
# the boot's startup-gate frames included, counted on this tree's Debug build on 2026-10-12 (the count and its
# method: spikes/testing-speed-b2/frames_pass1.tsv). A pool's is the sum over its arms. A headless row renders
# the one empty frame a headless engine walks.
if(TEST app.engine_selftest_validation)
    set_property(TEST app.engine_selftest_validation PROPERTY JAHSHAKA_TEST_FRAMES 2196)
endif()
if(TEST app.test_needs_boot)
    set_property(TEST app.test_needs_boot PROPERTY JAHSHAKA_TEST_FRAMES 710)
endif()
