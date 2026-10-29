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
jah_gpu_row(threading.staging_race SHIP CLASS engine COMMAND test_staging_race
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
set_tests_properties(threading.staging_race PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "threading")

# The race lane (scripts/sanitize.sh tsan): this suite IS an async contract —
# a worker building document nodes while the render loop runs — so it is a
# member by the membership rule at the top of tests/CMakeLists.txt. Read its
# source header for what the lane does and does not prove: Ogre-Next is a
# linked shared library and is NOT instrumented, so TSan slows and re-orders
# the soak rather than reporting the (Ogre-internal) accesses the pre-fix build
# raced on.
jah_tsan_lane(threading.staging_race)

# threading.mode (archived: archive/tests/threading) (gate G1-c) and threading.gi_resolve (archived: archive/tests/threading) — THE TWO COMPILE PAIRS, ONE ROW EACH
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
# The gi_resolve script's own header carries the defect it guards (the one frame threading.mode (archived: archive/tests/threading) does not
# exercise: world.refreshGi() EXACTLY ONE rendered frame after the compile burst) and what fixed it
# (Ogre fork 1bccc3f93+a98e2b0af: BufferPacked's creation serial in the descriptor slots).
# TSan, run 2026-10-11 (RUNNER-A7A): red — TSAN-BOOT-TOO-SLOW (testing/defects.json, recheck 2026-11-15)
# — the real binary cannot start under GCC 15's libtsan; threading.staging_race carries the lane's coverage.

# threading.import_soak (archived: archive/tests/threading) / threading.newproject_stall (archived: archive/tests/threading) — THE TWO HANGS
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
configure_file(texture_wait_watchdog.js.in ${CMAKE_CURRENT_BINARY_DIR}/texture_wait_watchdog.js @ONLY)

# threading.texture_wait_watchdog — the give-up path itself, under the engine's
# JAH_TEXTURE_WAIT_FAULT hook (the script's header says why a hook is the only
# way). Its own HOME: the run deliberately renders frames whose textures are
# declared stuck, and nothing it writes should be handed to another suite.
# TIMEOUT is the real assertion here — with the pre-fix unbounded wait this run
# would never end.
set(_thr_home_wd ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-threading_texture_wait_watchdog)

jah_fresh_home_fixture(threading.freshhome.texture_wait_watchdog threading_home_texture_wait_watchdog "${_thr_home_wd}" WARM LABELS "threading")

jah_gpu_row(threading.texture_wait_watchdog WARM_HOME "${_thr_home_wd}" SHIP CLASS app TIER low NEEDS NONE
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/texture_wait_watchdog.js
         WORKING_DIRECTORY ${_thr_home_wd}/run)
set_tests_properties(threading.texture_wait_watchdog PROPERTIES
    ENVIRONMENT "HOME=${_thr_home_wd};JAHSHAKA_DATA_ROOT=${_thr_home_wd}/.local/share/Jahshaka;JAH_TEXTURE_WAIT_FAULT=1;JAH_TEXTURE_WAIT_MS=300"
    FIXTURES_REQUIRED threading_home_texture_wait_watchdog
    LABELS "threading" TIMEOUT 60)

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

# Same toolchain quarantine as threading.mode (archived: archive/tests/threading): the real binary cannot start
# under GCC 15's libtsan.
# TSan, run 2026-10-11 (RUNNER-A7A): red — TSAN-BOOT-TOO-SLOW, TSAN-GCC15-THREAD-REGISTRY (testing/defects.json, recheck 2026-11-15)
jah_tsan_blocked(
                 threading.texture_wait_watchdog)

# FRAMES — THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B3; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, measured 2026-10-12 at the lane's tip by spikes/testing-speed-b3/framecount (an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded, summed over the row's processes); 0 = a row that renders nothing.
foreach(_row_frames
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
