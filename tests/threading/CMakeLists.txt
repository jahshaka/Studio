# THE THREADING SUITES (SPECS/THREADING_ADOPTION_SPEC.md).
#
# threading.staging_race (gate G3-c) — the explicit frame loop's scene gate:
# renderOneFrame updates exactly the scenes an enabled View draws, and never the
# document's staging managers, which the import worker writes while it runs. The
# suite's own header carries the mechanism, the two parts, and the honest note
# about what TSan can and cannot see here.
#
# Links IrisGL AND JahshakaEngine, like tests/mirror: the subject spans the
# boundary on purpose (document nodes on one thread, the engine's frame on the
# other). Needs a reachable display — it renders through Vulkan, because the
# gate is about which scenes a REAL frame walks.
find_package(Qt6 REQUIRED COMPONENTS Gui)
add_executable(test_staging_race test_staging_race.cpp)
set_target_properties(test_staging_race PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(test_staging_race PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_staging_race PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_staging_race PRIVATE IrisGL JahshakaEngine Qt6::Gui)
add_dependencies(test_staging_race JahshakaEngineMedia)
jah_gpu_row(threading.staging_race CLASS engine COMMAND test_staging_race
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(threading.staging_race PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "threading")

# The race lane (scripts/sanitize.sh tsan): this suite IS an async contract —
# a worker building document nodes while the render loop runs — so it is a
# member by the membership rule at the top of tests/CMakeLists.txt. Read its
# source header for what the lane does and does not prove: Ogre-Next is a
# linked shared library and is NOT instrumented, so TSan slows and re-orders
# the soak rather than reporting the (Ogre-internal) accesses the pre-fix build
# raced on.
jah_tsan_lane(threading.staging_race)

# threading.mode (gate G1-c) and threading.gi_resolve — THE TWO COMPILE PAIRS, ONE ROW EACH
# (TESTING-SPEED-B3; they were three ctest entries each: the parallel half, the _serial half and a
# _pixels byte compare between their fixtures).
#
# The subject is a COMPARISON: the same scene opened once with the editor scene's real worker pool and
# once with it pinned to a single thread (JAHSHAKA_SCENE_THREADS=1, read once at scene creation:
# src/bridge/sceneworkerthreads.h), and the claim is that threaded and serial shader compilation produce
# IDENTICAL pixels. tests/threading/pair.sh runs BOTH halves in sequence in one row:
#   * TWO VIRGIN HOMEs, one per half (fresh_home.sh, the one wipe). Both halves must COMPILE, and a
#     shared HOME would leave the second reading the first one's shader cache — turning the "it really
#     compiled" assertion into a no-op and the serial half into no serial compile at all.
#   * ENGINE UP (no --headless): the whole subject is shader compilation during a rendered frame.
#   * BOTH HALVES EVERY RUN, nothing stored: the gi_resolve defect (COLDGI) showed in the SERIAL half,
#     and a stored hash of either half would be a frozen picture against forward-building.
#   * the compare is `cmp` on the two 640x360 PNGs — byte for byte, no tolerance.
# STAGE-CLOSE (the ROWS table: a pair of Epic opens compiling cold is the ship's guard; the merge tier's
# probe of the mode-2 flag is the lead's follow-up). RESOURCE_LOCK threading: the two pair rows never
# compile beside each other (GATE-SPEED-1's verdict, spikes/gate-speed-1/); the COST starts the chain early.
set(JAH_COST_THREADING 80000)
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
configure_file(mode.js.in ${CMAKE_CURRENT_BINARY_DIR}/mode.js @ONLY)
configure_file(gi_resolve.js.in ${CMAKE_CURRENT_BINARY_DIR}/gi_resolve.js @ONLY)
# FRAMES: each half boots, opens Matcaps at Epic and renders a handful of frames before its shot; the
# figure is both halves' (measured: spikes/testing-speed-b3/profile.txt).
foreach(_pair mode gi_resolve)
    jah_gpu_row(threading.${_pair} CLASS app TIER epic NEEDS photon bloom ssao smaa planar
             COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/pair.sh "${JAH_FRESH_HOME}"
                     $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_CURRENT_BINARY_DIR}/${_pair}.js
                     ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-threading_${_pair}
                     ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-threading_${_pair}_serial
                     threading_${_pair}.png
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    set_tests_properties(threading.${_pair} PROPERTIES
        LABELS "threading;stage-close" RESOURCE_LOCK threading COST ${JAH_COST_THREADING})
endforeach()
# TIMEOUTs: the two halves' old quiet medians summed, x 6 (mode 47 + 54 s, gi_resolve 41 + 52 s).
set_tests_properties(threading.mode PROPERTIES TIMEOUT 610)
set_tests_properties(threading.gi_resolve PROPERTIES TIMEOUT 560)
# The gi_resolve script's own header carries the defect it guards (the one frame threading.mode does not
# exercise: world.refreshGi() EXACTLY ONE rendered frame after the compile burst) and what fixed it
# (Ogre fork 1bccc3f93+a98e2b0af: BufferPacked's creation serial in the descriptor slots).
# TSan, run 2026-10-11 (RUNNER-A7A): red — TSAN-LOD-UPDATE-RACE, TSAN-BOOT-TOO-SLOW (testing/defects.json, recheck 2026-11-15)
# — the real binary cannot start under GCC 15's libtsan; threading.staging_race carries the lane's coverage.
jah_tsan_blocked(threading.mode threading.gi_resolve)

# threading.import_soak / threading.newproject_stall — THE TWO HANGS
# (defect wave 2026-09-08; the mechanisms are in the scripts' own headers).
#
# Both drive the REAL binary through --script with the engine up, because both
# subjects only exist in the GUI path: a thumbnail render inside an import, and
# the environment push inside project creation. Both get a VIRGIN HOME, for
# opposite reasons — the soak so its twenty imports cannot dedupe against
# another suite's asset store, the stall so its untimed warm-up create is
# genuinely the first world of the process (a shared shader cache would hide
# what the timed one is really paying).
#
# RUN_SERIAL on both: newproject_stall asserts a wall-clock budget, and
# import_soak's iteration budget is a hang detector that four concurrent
# Vulkan apps would turn into a flake.
# STAGE-CLOSE (TESTING-SPEED-B3, the ROWS table): the soak, the stall's count row and the watchdog are
# whole-process soaks of a fixed hang — the ship's run; their fresh homes and the two log inspectors
# follow them (an inspector left in the merge tier would pull its parent back in through its fixture).
configure_file(import_soak.js.in ${CMAKE_CURRENT_BINARY_DIR}/import_soak.js @ONLY)
configure_file(texture_wait_watchdog.js.in ${CMAKE_CURRENT_BINARY_DIR}/texture_wait_watchdog.js @ONLY)
configure_file(newproject_stall.js.in ${CMAKE_CURRENT_BINARY_DIR}/newproject_stall.js @ONLY)

set(_thr_home_soak ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-threading_import_soak)
set(_thr_home_stall ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-threading_newproject_stall)

jah_fresh_home_fixture(threading.freshhome.import_soak threading_home_import_soak "${_thr_home_soak}" LABELS "threading;stage-close")

# NOT RUN_SERIAL (TEST_GATE_AUDIT.md §2, 2026-09-09) and not in the threading lock either.
# The four compile halves above hold `RESOURCE_LOCK threading` (four Epic opens compiling
# shaders at once is the measured origin of the contention class). This suite is different in kind: its assertions are
# PER-ITERATION BUDGETS with 30 s of headroom each, and a soak that a parallel
# schedule can starve inside a 30 s budget would be reporting a real defect.
# What it cost was a serial island of 12 s per gate.
jah_gpu_row(threading.import_soak CLASS app TIER low NEEDS NONE NEEDS_WHY "the import hang, the script switches Photon off itself"
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/import_soak.js
         WORKING_DIRECTORY ${_thr_home_soak}/run)
set_tests_properties(threading.import_soak PROPERTIES
    ENVIRONMENT "HOME=${_thr_home_soak};JAHSHAKA_DATA_ROOT=${_thr_home_soak}/.local/share/Jahshaka"
    FIXTURES_REQUIRED threading_home_import_soak
    LABELS "threading;stage-close" TIMEOUT 300)   # 20 iterations x a 30 s budget each: the timeout must not undercut the suite's own budget (code review 2026-09-10)

jah_fresh_home_fixture(threading.freshhome.newproject_stall threading_home_newproject_stall "${_thr_home_stall}" LABELS "threading;stage-close")

# The push row: the counts, no lock (its millisecond bar is the .timing twin's, below).
jah_gpu_row(threading.newproject_stall CLASS app TIER epic NEEDS photon bloom ssao smaa planar
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/newproject_stall.js
         WORKING_DIRECTORY ${_thr_home_stall}/run)
set_tests_properties(threading.newproject_stall PROPERTIES
    ENVIRONMENT "HOME=${_thr_home_stall};JAHSHAKA_DATA_ROOT=${_thr_home_stall}/.local/share/Jahshaka"
    FIXTURES_REQUIRED threading_home_newproject_stall
    TIMEOUT 300   # the lock's RUN_TIMEOUT kept (measured 16.6-45.7 s solo: 6x = 275)
    LABELS "threading;stage-close")

# threading.newproject_stall.timing — the first create's 1000 ms bar, stage-close (TESTING-DEBTS-1 T10,
# D6B-GATE-SHAPE's pattern): the push row above keeps the counts (GI armed, restored, no
# texture-wait timeout) and prints the milliseconds; this generated copy (jah_timing_script) asserts
# them, on its own virgin home, the quiet box and the GPU-timing lock. The push row's lock and
# RUN_SERIAL moved here with the bar: they were for the wall clock (the verdict: base 2026-09-28,
# solo, 704 / 686 / 1407 ms beside two lanes — spikes/testing-debts-1/).
set(_thr_home_stall_t ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-threading_newproject_stall_timing)
jah_timing_script(${CMAKE_CURRENT_SOURCE_DIR}/newproject_stall.js.in
                  ${CMAKE_CURRENT_BINARY_DIR}/newproject_stall.timing.js)
jah_fresh_home_fixture(threading.freshhome.newproject_stall_timing threading_home_newproject_stall_timing
                       "${_thr_home_stall_t}" LABELS "threading;stage-close")
jah_gpu_exclusive_test(NAME threading.newproject_stall.timing CLASS app TIER epic NEEDS photon bloom ssao smaa planar RUN_TIMEOUT 300
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/newproject_stall.timing.js
         WORKING_DIRECTORY ${_thr_home_stall_t}/run)
set_tests_properties(threading.newproject_stall.timing PROPERTIES
    ENVIRONMENT "HOME=${_thr_home_stall_t};JAHSHAKA_DATA_ROOT=${_thr_home_stall_t}/.local/share/Jahshaka"
    FIXTURES_REQUIRED threading_home_newproject_stall_timing
    LABELS "threading;stage-close;quiet-box" RUN_SERIAL TRUE)

# threading.texture_wait_watchdog — the give-up path itself, under the engine's
# JAH_TEXTURE_WAIT_FAULT hook (the script's header says why a hook is the only
# way). Its own HOME: the run deliberately renders frames whose textures are
# declared stuck, and nothing it writes should be handed to another suite.
# TIMEOUT is the real assertion here — with the pre-fix unbounded wait this run
# would never end.
set(_thr_home_wd ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-threading_texture_wait_watchdog)

jah_fresh_home_fixture(threading.freshhome.texture_wait_watchdog threading_home_texture_wait_watchdog "${_thr_home_wd}" LABELS "threading;stage-close")

jah_gpu_row(threading.texture_wait_watchdog CLASS app TIER low NEEDS NONE
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/texture_wait_watchdog.js
         WORKING_DIRECTORY ${_thr_home_wd}/run)
set_tests_properties(threading.texture_wait_watchdog PROPERTIES
    ENVIRONMENT "HOME=${_thr_home_wd};JAHSHAKA_DATA_ROOT=${_thr_home_wd}/.local/share/Jahshaka;JAH_TEXTURE_WAIT_FAULT=1;JAH_TEXTURE_WAIT_MS=300"
    FIXTURES_REQUIRED threading_home_texture_wait_watchdog
    LABELS "threading;stage-close" TIMEOUT 240)

# The SEGV's precondition, greped off the logs both runs just wrote
# (nogrouplesstexture.cmake carries the mechanism, and fails on an empty directory).
# DEPENDS on the run it inspects (ordering: it reads THIS gate's logs, and it still
# runs when that run went red — the grep is its own claim) AND FIXTURES_REQUIRED on
# the run's fresh-home wipe (D6B-GATE-SHAPE: the comment used to claim a fixture the
# row did not have, so a -R of the inspector alone read the PREVIOUS run's logs; now
# the wipe runs first and an inspector without its run fails on the empty directory
# instead of passing on a stale one).
# TESTING-CLEANUP-2C item 3: AND FIXTURES_REQUIRED on its parent's RUN fixture (the parent is that fixture's
# FIXTURES_SETUP): a `--solo` / `-R` of the inspector alone used to run the home wipe and then grep an EMPTY directory
# ("no logs — did the run happen?", batch C2: 0/3 solo); now ctest runs the parent first, in the same home. The cost,
# stated: when the parent run FAILS, ctest does not run its fixture's dependents — the inspector reads "Not Run"
# beside the parent's red instead of grepping a crashed run's logs (the logs stay in the home for the triage).
# source.testing_rules R6 guards the shape: a row that DEPENDS on another row requires a fixture that row sets up.
add_test(NAME threading.import_soak.no_groupless_texture
         COMMAND ${CMAKE_COMMAND} -DLOG_DIR=${_thr_home_soak}/run/logs
                 -P ${CMAKE_SOURCE_DIR}/tests/threading/nogrouplesstexture.cmake)
set_tests_properties(threading.import_soak.no_groupless_texture PROPERTIES
    DEPENDS threading.import_soak FIXTURES_REQUIRED "threading_home_import_soak;threading_run_import_soak"
    LABELS "threading;stage-close" TIMEOUT 60)

add_test(NAME threading.newproject_stall.no_groupless_texture
         COMMAND ${CMAKE_COMMAND} -DLOG_DIR=${_thr_home_stall}/run/logs
                 -P ${CMAKE_SOURCE_DIR}/tests/threading/nogrouplesstexture.cmake)
set_tests_properties(threading.newproject_stall.no_groupless_texture PROPERTIES
    DEPENDS threading.newproject_stall FIXTURES_REQUIRED "threading_home_newproject_stall;threading_run_newproject_stall"
    LABELS "threading;stage-close" TIMEOUT 60)

# Same toolchain quarantine as threading.mode: the real binary cannot start
# under GCC 15's libtsan.
# TSan, run 2026-10-11 (RUNNER-A7A): red — TSAN-LOD-UPDATE-RACE, TSAN-BOOT-TOO-SLOW, TSAN-GCC15-THREAD-REGISTRY (testing/defects.json, recheck 2026-11-15)
jah_tsan_blocked(threading.import_soak threading.newproject_stall
                 threading.texture_wait_watchdog)
set_property(TEST threading.import_soak APPEND PROPERTY FIXTURES_SETUP threading_run_import_soak)   # its inspector's run (item 3 above)
set_property(TEST threading.newproject_stall APPEND PROPERTY FIXTURES_SETUP threading_run_newproject_stall)   # its inspector's run (item 3 above)

# FRAMES — THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B3; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, measured 2026-10-12 at the lane's tip by spikes/testing-speed-b3/framecount (an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded, summed over the row's processes); 0 = a row that renders nothing.
foreach(_row_frames
        "threading.gi_resolve=1371"
        "threading.import_soak=550"
        "threading.import_soak.no_groupless_texture=0"
        "threading.mode=1355"
        "threading.newproject_stall=494"
        "threading.newproject_stall.no_groupless_texture=0"
        "threading.newproject_stall.timing=500"
        "threading.staging_race=255"
        "threading.texture_wait_watchdog=473"
        )
    string(REPLACE "=" ";" _rf "${_row_frames}")
    list(GET _rf 0 _r)
    list(GET _rf 1 _n)
    if(TEST ${_r})
        set_tests_properties(${_r} PROPERTIES JAHSHAKA_TEST_FRAMES ${_n})
    endif()
endforeach()
