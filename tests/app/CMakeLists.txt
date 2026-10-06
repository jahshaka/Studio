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
jah_gpu_row(app.engine_selftest_validation CLASS selftest
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/engine_selftest.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${CMAKE_CURRENT_BINARY_DIR}"
         WORKING_DIRECTORY "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>")
set_tests_properties(app.engine_selftest_validation PROPERTIES
    ENVIRONMENT "${JAH_SELFTEST_ENV}VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation"
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
jah_gpu_row(app.startup_quiet CLASS app
         COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/startup_quiet.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                 "${CMAKE_CURRENT_SOURCE_DIR}/startup_quiet.js"
         WORKING_DIRECTORY ${_sq_home}/run)
set_tests_properties(app.startup_quiet PROPERTIES
    ENVIRONMENT "HOME=${_sq_home};JAHSHAKA_DATA_ROOT=${_sq_home}/.local/share/Jahshaka"
    TIMEOUT 300
    LABELS "app")

# app.start_in_vr — THE VR PREFERENCES (VR-START-1): Start in VR (on) and the headset runtime (WiVRn);
# a policy refusal opens no OpenXR runtime and ends in the editor plus one notice or dialog.
# Three boots of one fresh home (start_in_vr.sh). `JAHSHAKA_VR=` (empty) keeps the tests/ walk
# from adding JAHSHAKA_VR=0: this row is the one that lets the preference decide.
set(_siv_home ${CMAKE_CURRENT_BINARY_DIR}/start-in-vr-home)
file(MAKE_DIRECTORY ${_siv_home}/run)
jah_gpu_row(app.start_in_vr CLASS app
         COMMAND sh ${JAH_FRESH_HOME} --warm ${_siv_home} --
                 bash "${CMAKE_CURRENT_SOURCE_DIR}/start_in_vr.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                 "${CMAKE_CURRENT_SOURCE_DIR}/start_in_vr.js"
         WORKING_DIRECTORY ${_siv_home}/run)
set_tests_properties(app.start_in_vr PROPERTIES
    ENVIRONMENT "HOME=${_siv_home};JAHSHAKA_DATA_ROOT=${_siv_home}/.local/share/Jahshaka;JAHSHAKA_VR=;JAHSHAKA_TEST_TIER=low"
    TIMEOUT 300
    LABELS "app")

# app.shader_gate_warm — THE STARTUP SHADER GATE KNOWS A WARM CACHE (TEST-TIER-1): a cold boot
# (--clear-shader-cache) compiles and runs the GI warm-up; the second boot in the same home compiles
# ZERO programs and skips it. A ROW of its own: its claim is two boots of one home, the first
# with its shader cache wiped (a pool process cannot reboot). The home is fresh every run; the
# row's own first boot is what makes the cache cold. THREE ARMS (TESTING-DEBTS-1 T7): the
# document's tier (Epic: the race the gate had is the Epic lighting arm's), a Low test process
# (JAHSHAKA_TEST_TIER=low: the gate warms the born tier) and rays off (JAHSHAKA_NO_RAY_QUERY=1).
set(_sgw_home ${CMAKE_CURRENT_BINARY_DIR}/shader-gate-warm-home)
file(MAKE_DIRECTORY ${_sgw_home}/run)
jah_gpu_row(app.shader_gate_warm CLASS app
         COMMAND sh ${JAH_FRESH_HOME} --warm ${_sgw_home} --
                 bash "${CMAKE_CURRENT_SOURCE_DIR}/shader_gate_warm.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                 "${CMAKE_CURRENT_SOURCE_DIR}/startup_quiet.js"
         WORKING_DIRECTORY ${_sgw_home}/run)
set_tests_properties(app.shader_gate_warm PROPERTIES
    ENVIRONMENT "HOME=${_sgw_home};JAHSHAKA_DATA_ROOT=${_sgw_home}/.local/share/Jahshaka"
    TIMEOUT 350   # 6x the measured 57.6 s (three arms x two boots; spikes/testing-debts-1/t7-run1.log)
    LABELS "app")

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
    if(JAH_XDOTOOL AND JAH_CURL AND JAH_JQ AND JAH_XVFB)
        set(_ik_home ${CMAKE_CURRENT_BINARY_DIR}/input-keys-home)
        file(MAKE_DIRECTORY ${_ik_home}/run)
        # PORT 0 = EPHEMERAL. The app prints "MCP: port N" and the script reads
        # it back; two suites both naming 8751 is what made -j4 unsafe
        # (TEST_GATE_AUDIT.md §4.1).
        # DEVPROCESS-1: registered through the GPU-timing lock; TIMEOUT = RUN_TIMEOUT + the lock's 900 s wait.
        jah_gpu_exclusive_test(NAME app.input_keys CLASS app TIER low RUN_TIMEOUT 600
                 COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/input_keys.sh"
                         "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" 0
                 WORKING_DIRECTORY ${_ik_home}/run)
        # TIMEOUT is a DOCUMENTED EXCEPTION to the 6x policy, like the suite it
        # replaces was: this row is one long chain of synthesised keys with
        # fixed sleeps between them, and a loaded box stretches every one of
        # them. 600 s against a ~40 s measured run.
        set_tests_properties(app.input_keys PROPERTIES
            # JAHSHAKA_TEST_TIER=low (TEST-TIER-1): no claim here needs the shipped picture — the same verdict and ok count at Low (spikes/test-tier-1/solo-okcount.txt)
            ENVIRONMENT "HOME=${_ik_home};JAHSHAKA_DATA_ROOT=${_ik_home}/.local/share/Jahshaka;JAHSHAKA_TEST_TIER=low"
            RUN_SERIAL TRUE
            LABELS "app")
    else()
        message(STATUS "app.input_keys skipped: needs xdotool, curl, jq and Xvfb")
    endif()
endif()

# app.play_select — A REAL CLICK AND A REAL GIZMO DRAG ON A PLAYING SCENE
# (PLAY-SELECT-1, owner R13). The verb-level half of the lane is
# scripting.e2e.play_select; what only a live window can answer is whether the
# gesture ARRIVES — the routing under test is QMouseEvent delivery into the
# viewport widget, which no function call can stand in for. Same shape as
# app.input_keys above: its own private Xvfb (never JAH_TEST_DISPLAY, which may
# name a developer's session), an ephemeral MCP port read back from the log, and
# THE GPU-TIMING LOCK like input_keys (lane D6B-GATE-SHAPE; audit §4 R5): its
# gestures are a chain of fixed sleeps whose ARRIVAL is the timing class. It was
# RUN_SERIAL "because synthesised input goes wherever the server has focus" —
# void: the server is its own.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(JAH_XDOTOOL AND JAH_CURL AND JAH_JQ AND JAH_XVFB)
        set(_ps_home ${CMAKE_CURRENT_BINARY_DIR}/play-select-home)
        file(MAKE_DIRECTORY ${_ps_home}/run)
        jah_gpu_exclusive_test(NAME app.play_select CLASS app TIER low RUN_TIMEOUT 400
                 COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/play_select.sh"
                         "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" 0
                 WORKING_DIRECTORY ${_ps_home}/run)
        # Like app.input_keys, a DOCUMENTED exception to the 6x policy: a chain
        # of synthesised gestures with fixed sleeps between them, every one of
        # which a loaded box stretches. 400 s against a ~50 s measured run.
        set_tests_properties(app.play_select PROPERTIES
            # JAHSHAKA_TEST_TIER=low (TEST-TIER-1): no claim here needs the shipped picture — the same verdict and ok count at Low (spikes/test-tier-1/solo-okcount.txt)
            ENVIRONMENT "HOME=${_ps_home};JAHSHAKA_DATA_ROOT=${_ps_home}/.local/share/Jahshaka;JAHSHAKA_TEST_TIER=low"
            LABELS "app")
    else()
        message(STATUS "app.play_select skipped: needs xdotool, curl, jq and Xvfb")
    endif()
endif()

# ---- app.scene_switch_after_play -------------------------------------------
# crash-1788594910 regression gate: switching scenes with play/simulate state
# still armed must not crash (PlayBack::stopScene + updateSceneAnimation ran
# against the closed project's torn-down scene). Real binary, scratch HOME.
jah_pool_arm(player scene_switch_after_play ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_scene_switch_after_play.js 10)

# ---- app.multiselect_outline ------------------------------------------------
# EVERY member of a selection SET is outlined, not just the primary
# (EDITOR_MULTISELECT_SPEC §2.3/D4 a). Two cubes, one per half of the frame,
# probed in their own half — a secondary that quietly stopped drawing would pass
# every verb suite in the program and fail only here. The hull<->wireframe
# style-toggle trick, so the gizmo cancels out. It also carries the two claims of
# the old app.selection_outline (lane D6B-GATE-SHAPE; audit verdict MERGE): an
# Add-menu primitive IS outlined (the isBuiltIn regression gate of 2026-09-06 — its
# "one selected" arm) and the GROUND never is (its ground arm).
jah_pool_arm(editor_view multiselect_outline ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_multiselect_outline.js 11)

# ---- app.close_shot ---------------------------------------------------------
# A PROJECT'S DESKTOP TILE IS THE PRESENTED FRAME (CLOSE-SHOT-2): read back from
# the window (View::requestFrameCapture), drawn once without the grid, outline and
# gizmo — no second render, no settle. The frame as shown against the capture, the
# save's tile against a clean capture, and the close's tile showing an edit.
jah_pool_arm(editor_view close_shot ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_close_shot.js 12)

# ---- app.window_fits --------------------------------------------------------
# A FRESH PROFILE'S WINDOW FITS ON ITS SCREEN (hygiene lane, 2026-09-09).
# MainWindow::fitToScreen is only reachable when there is no stored geometry, so
# the suite gets an empty data root and asks app.window() what the boot produced.
# --headless: offscreen QPA, no engine, no display at all (jah_no_display), and
# the offscreen screen is smaller than the .ui's authored 1612x1530 — which is
# the condition the clamp exists for.
jah_pool_arm(doc window_fits ${CMAKE_CURRENT_SOURCE_DIR}/scripts/e2e_window_fits.js 1 HEADLESS)

# pool.runner — THE POOL RUNNER'S OWN TEST (tests/support/test_run_pool.py; the runner is
# src/app/cli/scriptrunner.cpp, so the row lives with the app's): the real app,
# headless, over five fixture arms (a fresh realm and a closed project between arms, a thrown
# assertion and a numeric completion value as FAIL, the pool going on, JAH_POOL_ARMS's subset
# and its unknown-arm FAIL), and the driver against a stand-in app for what the real one cannot
# be asked to do (a SIGSEGV mid-arm, a hang past the budget, a lost baseline — one arm each).
set(_home ${CMAKE_CURRENT_BINARY_DIR}/pool-home-runner)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME pool.runner
         COMMAND sh ${JAH_FRESH_HOME} --warm ${_home} --
                 ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/support/test_run_pool.py
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${JAH_RUN_POOL}
                 ${CMAKE_CURRENT_SOURCE_DIR}/pool_selftest
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(pool.runner PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    LABELS "pool;scripting" TIMEOUT 180)
jah_no_display(pool.runner)

# app.sky_swap_presented — NO PRESENTED FRAME LOSES THE SKY ENVIRONMENT (SKY-SWAP-1,
# the owner's smoke bug #3: the flicker in 'were'). Every sky re-capture used to
# present ONE frame with a black environment (the swap destroyed the old cube
# while the Atom decode twins still sampled it). scripting.e2e.sky_swap_transient
# reads offscreen renders, which never see the presented frame; this row reads
# the X server's copy of the viewport (xwd of the root) while the UI thread is
# frozen on each frame after a change — the sun disc into the probes, the cloud
# altitude, clouds off/on and an observer-band crossing, at Medium and at Epic
# with rays off (Epic as shipped hides the flash under its traced reflections).
# Its own private Xvfb (301-340), like play_select: the capture reads the whole
# root. Not in the GPU-timing lock: it measures no time — app.blockUiThread holds
# each frame on screen, and the reads are pixel means.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(JAH_XWD xwd)
    find_program(JAH_XDPYINFO xdpyinfo)
    find_program(JAH_XVFB Xvfb)
    if(JAH_XWD AND JAH_XDPYINFO AND JAH_XVFB)
        set(_ssp_home ${CMAKE_CURRENT_BINARY_DIR}/sky-swap-presented-home)
        file(MAKE_DIRECTORY ${_ssp_home}/run)
        jah_gpu_row(app.sky_swap_presented CLASS app
                 COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/sky_swap_presented.py"
                         "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${_ssp_home}/out"
                 WORKING_DIRECTORY ${_ssp_home}/run)
        set_tests_properties(app.sky_swap_presented PROPERTIES
            ENVIRONMENT "HOME=${_ssp_home};JAHSHAKA_DATA_ROOT=${_ssp_home}/.local/share/Jahshaka"
            SKIP_RETURN_CODE 77
            # measured 123-290 s (two tiers x eight arms x five held frames, each a 1 s
            # UI freeze; three lanes on the box); 900 = 3x the worst run
            TIMEOUT 900
            LABELS "app")
    else()
        message(STATUS "app.sky_swap_presented skipped: needs xwd, xdpyinfo and Xvfb")
    endif()
endif()
