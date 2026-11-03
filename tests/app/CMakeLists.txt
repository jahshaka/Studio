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
set(_selftest_home ${CMAKE_CURRENT_BINARY_DIR}/selftest-home)
file(MAKE_DIRECTORY ${_selftest_home})
set(JAH_SELFTEST_ENV "${JAH_SELFTEST_ENV}HOME=${_selftest_home};JAHSHAKA_DATA_ROOT=${_selftest_home}/.local/share/Jahshaka;")

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
# THE HOME IS WIPED BEFORE EVERY RUN AND THE SHADER CACHE COMES WARM (URGENT #48's follow-up, the lead's ruling (a),
# 2026-10-24): fresh_home.sh --warm - the home virgin (selftest-data used to persist across runs of one build dir, and a
# JahLibrary.db left by an older build carries an older schema: the self-test exited 4 on main's build-linux,
# 2026-10-20; a row owns its own state), the boot's shader cache the tree's one shared app cache, a green run's grown
# cache promoted. COLD it no longer fits the 60 s cap: on the photon-7r merge's tree it compiled 355 shaders under the
# layer and timed out at pose 1. WARM the layer still sees every vkCreateShaderModule and every pipeline creation, so the
# Vulkan API guard is whole; the cold path's own work (glslang's CPU, the async workers' concurrent creations) is TSan's
# (the cold validation of a subset: the leftover list). The wipe fixture (app.engine_selftest_validation.fresh_root) is
# deleted: the wrapper wipes inside the row.
# ONE ROW PER PROOF (the lead's ruling, 2026-10-28): the combined app.engine_selftest_validation ran 54-60 s - the layer's
# own cost on six poses - so it is TWO ROWS, a half of the poses each (JAHSHAKA_SELFTEST_POSES, selftestrunner.cpp):
# .default the default scene's pose 1, pose 2 and motion (with the on-screen view's resizes and the broken-ground arm),
# .fixture pose pair B and the mover. THE FOUR ARE FOUR, a property of the full set, is the six-hash step's.
foreach(_half default fixture)
    jah_gpu_row(app.engine_selftest_validation.${_half} SHIP CLASS selftest
             COMMAND sh ${JAH_FRESH_HOME} --warm ${_selftest_home} --
                     bash "${CMAKE_CURRENT_SOURCE_DIR}/engine_selftest.sh"
                     "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${CMAKE_CURRENT_BINARY_DIR}" ${_half}
             WORKING_DIRECTORY "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>")
    set_tests_properties(app.engine_selftest_validation.${_half} PROPERTIES
        ENVIRONMENT "${JAH_SELFTEST_ENV}VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation;JAHSHAKA_SELFTEST_SHORT=1"
        TIMEOUT 60 LABELS "app")  # the 60 s cap (URGENT #48); the short run (PHOTON-7 stage 10: the fixed frames a quarter)
endforeach()

# app.frame_step — A LOCKSTEP FRAME IS ONE GRID STEP (TEST-STREAMLINE-1 red (b); src/viewport/framestep.h, its law in
# test_frame_step.cpp): n scripted frames advance the document clock by exactly n steps at any wall pace (the wall rule fed
# as the negative control), a scripted dt is >= 0 and within the bound, the timeline preview moves by whole steps. A CPU
# code row: the header and the document's clock only.
add_executable(test_frame_step test_frame_step.cpp)
set_target_properties(test_frame_step PROPERTIES CXX_STANDARD 17 AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(test_frame_step PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR})
add_test(NAME app.frame_step COMMAND test_frame_step)
set_tests_properties(app.frame_step PROPERTIES TIMEOUT 60 LABELS "app")
jah_no_display(app.frame_step)

# app.engine_selftest_shapes.{alone,after_default} — FIXTURE B IS A FUNCTION OF ITS OWN SCENE (URGENT #157; the law and
# its cause: engine_selftest_shapes.sh). .alone runs the fixture half alone and keeps its B1 / B2 / mover pictures (the
# setup); .after_default runs the full six-pose shape and its three must be BYTE-EQUAL to them: whatever the default half
# leaves behind (an engine member past detachScene, a cache, the window's layout - the carrier #157 measured: the editor
# view 748 wide after pose 2's resizes, 733 alone) reds the pair. Both without the validation layer (the same
# configuration on both sides), both on the short run, each its own wiped home with the shader cache warm. Not SHIP: a
# feature's end when its diff reaches it, and the LONG pass.
set(_shapes_home ${CMAKE_CURRENT_BINARY_DIR}/selftest-shapes-home)
foreach(_arm alone after_default)
    jah_gpu_row(app.engine_selftest_shapes.${_arm} CLASS selftest
             COMMAND sh ${JAH_FRESH_HOME} --warm ${_shapes_home}-${_arm} --
                     bash "${CMAKE_CURRENT_SOURCE_DIR}/engine_selftest_shapes.sh"
                     "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${CMAKE_CURRENT_BINARY_DIR}" ${_arm}
             WORKING_DIRECTORY "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>")
    set_tests_properties(app.engine_selftest_shapes.${_arm} PROPERTIES
        ENVIRONMENT "${JAH_TEST_DISPLAY_ENV}QT_QPA_PLATFORM=xcb;HOME=${_shapes_home}-${_arm};JAHSHAKA_DATA_ROOT=${_shapes_home}-${_arm}/.local/share/Jahshaka;JAHSHAKA_SELFTEST_SHORT=1"
        TIMEOUT 60 LABELS "app")
endforeach()
set_tests_properties(app.engine_selftest_shapes.alone PROPERTIES FIXTURES_SETUP selftest_shapes)
set_tests_properties(app.engine_selftest_shapes.after_default PROPERTIES FIXTURES_REQUIRED selftest_shapes)

# app.startup_quiet (archived: archive/tests/app) — the boot log stays clean (STABILITY_PROGRAM_SPEC lanes
# 6a/6b/6c). Three noise sources were fixed together and all three are only
# observable in a REAL boot, so this is the one suite that reads the boot log:
#
#   6b  "duplicate connection name 'qt_sql_default_connection'" — Database::
#       closeDatabase() read the connection name after invalidating the handle,
#       so removeDatabase("") no-op'd and the boot's schema check leaked the
#       DEFAULT connection into MainWindow::setupProjectDB.
#   6c  "invalid parameters in SMAA.material(83)" — fork c290052de (was 0012).
#   6a  Qt Multimedia on the startup path (iris::Scene and VideoPreviewWidget
#       both built a QMediaPlayer in their constructors). ui.media_lazy (archived: archive/tests/ui) is the
#       precise guard; this one catches anything ELSE that starts constructing
#       media objects at boot. Keyed on Qt's own "qt.multimedia.ffmpeg: Using
#       Qt multimedia with FFmpeg" banner, which the backend prints when it is
#       first instantiated — a one-way guard (if a future Qt silences the
#       banner this assertion weakens, ui.media_lazy (archived: archive/tests/ui) does not).
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
jah_gpu_row(app.test_needs_boot CLASS app TIER epic NEEDS photon bloom
         COMMAND sh ${JAH_FRESH_HOME} --warm ${_tnb_home} --
                 bash "${CMAKE_CURRENT_SOURCE_DIR}/test_needs_boot.sh"
                 "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
                 "${CMAKE_CURRENT_SOURCE_DIR}/test_needs_boot.js"
         WORKING_DIRECTORY ${_tnb_home}/run)
set_tests_properties(app.test_needs_boot PROPERTIES
    ENVIRONMENT "HOME=${_tnb_home};JAHSHAKA_DATA_ROOT=${_tnb_home}/.local/share/Jahshaka"
    TIMEOUT 60
    LABELS "app")

# app.start_in_vr (archived: archive/tests/app) — THE VR PREFERENCES (VR-START-1): Start in VR (on) and the headset runtime (WiVRn);
# a policy refusal opens no OpenXR runtime and ends in the editor plus one notice or dialog.
# Three boots of one fresh home (start_in_vr.sh). `JAHSHAKA_VR=` (empty) keeps the tests/ walk
# from adding JAHSHAKA_VR=0: this row is the one that lets the preference decide.
set(_siv_home ${CMAKE_CURRENT_BINARY_DIR}/start-in-vr-home)
file(MAKE_DIRECTORY ${_siv_home}/run)

# app.input_keys (archived: archive/tests/app) — EVERY INPUT-SYNTHESIS ASSERTION THE EDITOR HAS, ON ONE BOOT.
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
# REVIVED AS A `code` ROW (TEST-STREAMLINE-1 3a item 1): stage 3 groups rows on run_pool.py, so its own suite runs on
# every stage again. The real-app half is the NULL headless app (no display, no card: jah_no_display; no warm cache -
# the NULL render system compiles nothing), the stand-in half pure CPU. A fresh home every run (fresh_home.sh, no --warm).
set(_home ${CMAKE_CURRENT_BINARY_DIR}/pool-home-runner)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME pool.runner
         COMMAND sh ${JAH_FRESH_HOME} ${_home} --
                 ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/support/test_run_pool.py
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${JAH_RUN_POOL}
                 ${CMAKE_CURRENT_SOURCE_DIR}/pool_selftest
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(pool.runner PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    JAHSHAKA_HEADLESS TRUE   # every app process it starts is --headless: the NULL render system (testing_rules R13)
    LABELS "pool;scripting;tooling" TIMEOUT 60)
jah_no_display(pool.runner)

# THE CLEAN-STATE LAW (TEST-STREAMLINE-1 3a item 4; tests/support/clean_state.py says it): every grouped row gives the
# same verdict and the same numbers - its `ok:`/`FAIL:` lines to the character, bar a home path, a guid, a Date.now()
# stamp and a wall - whichever order its group runs in, and alone. One process per order, a fresh home each; the reset
# between arms is the pool's BASELINE script clean_state/clean_library_baseline.js - the existing verb app.resetLibrary
# (both storages cleared: in a driven session it re-seeds the primitives and seeds no presets - a fresh home's boot).
#   pool.clean_state.law   the law's own test: toy arms on the NULL headless app - a library row left behind caught by
#                          the next arm's verdict, a moved number caught with equal verdicts, the reset making it hold.
#   pool.clean_state.docs  THE PERMANENT ROW, its CPU half: scripting.e2e.one_asset + .preset_apply (NULL headless, one
#                          home) in both orders - each first once, which is its alone run. RED before (2026-11-03, by
#                          hand): preset_apply FAILED after one_asset (its library's material rows), and three printed
#                          numbers moved with the order (a list's order, two process-wide counters' absolutes) - each
#                          script now prints its law's own number. The card groups' half is block B (B2).
set(_home ${CMAKE_CURRENT_BINARY_DIR}/clean-state-home)
file(MAKE_DIRECTORY ${_home}/law/run ${_home}/docs/run)   # (each order's home is made under the row's run dir, wiped with it)
add_test(NAME pool.clean_state.law
         COMMAND sh ${JAH_FRESH_HOME} ${_home}/law --
                 sh ${CMAKE_CURRENT_SOURCE_DIR}/clean_state/clean_state_law.sh ${Python3_EXECUTABLE}
                 ${CMAKE_SOURCE_DIR}/tests/support/clean_state.py ${JAH_RUN_POOL}
                 $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_CURRENT_SOURCE_DIR}/clean_state
         WORKING_DIRECTORY ${_home}/law/run)
add_test(NAME pool.clean_state.docs
         COMMAND sh ${JAH_FRESH_HOME} ${_home}/docs --
                 ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/support/clean_state.py
                 --app $<TARGET_FILE:${CMAKE_PROJECT_NAME}> --driver ${JAH_RUN_POOL} --pool docs --headless
                 --baseline ${CMAKE_CURRENT_SOURCE_DIR}/clean_state/clean_library_baseline.js
                 --arm one_asset ${CMAKE_BINARY_DIR}/tests/scripting/e2e_one_asset.js 60
                 --arm preset_apply ${CMAKE_SOURCE_DIR}/tests/scripting/scripts/e2e_preset_apply.js 60
         WORKING_DIRECTORY ${_home}/docs/run)
foreach(_row pool.clean_state.law pool.clean_state.docs)
    set_tests_properties(${_row} PROPERTIES
        JAHSHAKA_HEADLESS TRUE   # every app process is --headless: the NULL render system (testing_rules R13)
        LABELS "pool;scripting;tooling" TIMEOUT 60)
    jah_no_display(${_row})
endforeach()

# app.seed_home_template — THE 17 SHIPPED MESHES SEEDED ONCE PER BAKE KEY (TEST-STREAMLINE-1 3a item 5; the reader's P6;
# seed_home_template.sh says the four laws): the warm-up's template made by one NULL headless boot and made once per key,
# fresh_home.sh --warm copying it (the boot then seeds nothing, the store the template's objects by content hash), the
# control home seeding, and a moved bake key never served (the home seeds; the next make re-makes). No display, no card.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/seed-home-template)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME app.seed_home_template
         COMMAND sh ${JAH_FRESH_HOME} ${_home} --
                 sh ${CMAKE_CURRENT_SOURCE_DIR}/seed_home_template.sh $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/tests ${_home}/run/toy
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(app.seed_home_template PROPERTIES
    JAHSHAKA_HEADLESS TRUE   # every boot is --headless: the NULL render system (testing_rules R13)
    LABELS "scripting;tooling" TIMEOUT 60)
jah_no_display(app.seed_home_template)

# app.render_route — THE ONE RENDER-SYSTEM DECISION AND THE HEADLESS HOST's VIEW, on the CPU (TEST-STREAMLINE-1 3a, the
# reader's P4 + P5; test_render_route.cpp says the laws): the route table and its once-per-process latch, the
# --render-offscreen CLI, and EngineViewWidget's declared offscreen View on a NULL engine (no native attempt, the
# NULL refusal in one line). The NULL render system and the offscreen platform: no display, no card.
find_package(Qt6 REQUIRED COMPONENTS Widgets)
add_executable(test_render_route test_render_route.cpp
    ${CMAKE_SOURCE_DIR}/src/bridge/renderroute.cpp
    ${CMAKE_SOURCE_DIR}/src/app/cli/clioptions.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/engineviewwidget.cpp
    ${CMAKE_SOURCE_DIR}/src/viewport/engineviewwidget.h)
set_target_properties(test_render_route PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_render_route PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl ${CMAKE_SOURCE_DIR}/tests/support)
target_compile_definitions(test_render_route PRIVATE
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}" JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_render_route PRIVATE JahshakaEngine Qt6::Widgets)
add_dependencies(test_render_route JahshakaEngineMedia)
add_test(NAME app.render_route COMMAND ${CMAKE_BINARY_DIR}/bin/test_render_route
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
set_tests_properties(app.render_route PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "scripting")
jah_no_display(app.render_route)

# app.render_offscreen_host — THE HEADLESS HOST's DOOR THROUGH THE REAL APP, on the CPU (TEST-STREAMLINE-1 3a;
# render_offscreen_host.sh says the three laws): the NULL route's DEVICE row, --render-offscreen refused with DISPLAY
# unset (exit 3, one line, no document-only run in its place), and without --headless (a CLI error). DISPLAY unset,
# no card; the host booting Vulkan offscreen is card block B's.
set(_home ${CMAKE_CURRENT_BINARY_DIR}/render-offscreen-host-home)
file(MAKE_DIRECTORY ${_home}/run)
add_test(NAME app.render_offscreen_host
         COMMAND sh ${JAH_FRESH_HOME} ${_home} --
                 sh ${CMAKE_CURRENT_SOURCE_DIR}/render_offscreen_host.sh $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${_home}
         WORKING_DIRECTORY ${_home}/run)
set_tests_properties(app.render_offscreen_host PROPERTIES
    ENVIRONMENT "HOME=${_home};JAHSHAKA_DATA_ROOT=${_home}/.local/share/Jahshaka"
    JAHSHAKA_HEADLESS TRUE   # every boot is --headless (the third refuses before any boot): no shader cache (testing_rules R13)
    LABELS "scripting" TIMEOUT 60)
jah_no_display(app.render_offscreen_host)

# THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B2; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, MEASURED — every engine frame (Engine::renderOneFrame) of every process the row starts,
# the boot's startup-gate frames included, counted on this tree's Debug build on 2026-10-12 (the count and its
# method: spikes/testing-speed-b2/frames_pass1.tsv). A pool's is the sum over its arms. A headless row renders
# the one empty frame a headless engine walks.
# (the halves' counts SPLIT from the combined row's measured 1633: the boot's ~1003 each - the broken-ground arm fails
# before the frame pump - plus each half's own printed frames, default 336 / fixture 294; PHOTON-7 11-2, 2026-10-28)
if(TEST app.engine_selftest_validation.default)
    set_property(TEST app.engine_selftest_validation.default PROPERTY JAHSHAKA_TEST_FRAMES 1339)
endif()
if(TEST app.engine_selftest_validation.fixture)
    set_property(TEST app.engine_selftest_validation.fixture PROPERTY JAHSHAKA_TEST_FRAMES 1297)
endif()
# (URGENT #157, 2026-11-03: the Ogre frame count at each arm's last traced frame, the short run, by hand on the card -
# spikes/test-streamline-1/157/run/guard-green; the boot's frames included)
if(TEST app.engine_selftest_shapes.alone)
    set_property(TEST app.engine_selftest_shapes.alone PROPERTY JAHSHAKA_TEST_FRAMES 1136)
endif()
if(TEST app.engine_selftest_shapes.after_default)
    set_property(TEST app.engine_selftest_shapes.after_default PROPERTY JAHSHAKA_TEST_FRAMES 1522)
endif()
if(TEST app.test_needs_boot)
    set_property(TEST app.test_needs_boot PROPERTY JAHSHAKA_TEST_FRAMES 710)
endif()
