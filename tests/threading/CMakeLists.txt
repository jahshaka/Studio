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
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(test_staging_race PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/")
target_link_libraries(test_staging_race PRIVATE IrisGL JahshakaEngine Qt6::Gui)
add_dependencies(test_staging_race JahshakaEngineMedia)
add_test(NAME threading.staging_race COMMAND test_staging_race
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

# threading.mode / .mode_serial / .mode_pixels (gate G1-c) — the mode-2 smoke.
#
# THREE ctest entries for ONE subject, because the subject is a COMPARISON: the
# same scene opened once with the editor scene's real worker pool and once with
# it pinned to a single thread, and the claim is that threaded and serial shader
# compilation produce IDENTICAL pixels. The script's header explains what it
# asserts and why Matcaps; what belongs here is why it is wired this way.
#
#   * TWO VIRGIN HOMEs, one per run. Both halves must COMPILE, and a shared HOME
#     would leave the second run reading the first one's shader cache — turning
#     the "it really compiled" assertion into a no-op and, worse, making the
#     serial half not serial-compile at all.
#   * ENGINE UP (no --headless): the whole subject is shader compilation during
#     a rendered frame.
#   * RUN_SERIAL on both halves. They are not timing gates, but they both open a
#     project at Epic and compile a few dozen shaders; letting four of those run
#     at once is how the contention-sensitive suites got their reputation.
#   * The pixel compare is `cmake -E compare_files` on the two PNGs — byte for
#     byte, no tolerance. Anything else would be a different claim.
set(SOURCE_DIR ${CMAKE_SOURCE_DIR})
configure_file(mode.js.in ${CMAKE_CURRENT_BINARY_DIR}/mode.js @ONLY)

set(_thr_home_par ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-threading_mode)
set(_thr_home_ser ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-threading_mode_serial)

# Reuses the samples suite's wipe script rather than copying it, exactly like
# perf.epic_steady_state does.
add_test(NAME threading.freshhome.mode
         COMMAND ${CMAKE_COMMAND} -DHOME_DIR=${_thr_home_par}
                 -P ${CMAKE_SOURCE_DIR}/tests/samples/freshhome.cmake)
set_tests_properties(threading.freshhome.mode PROPERTIES
    FIXTURES_SETUP threading_home_mode LABELS "threading")

add_test(NAME threading.freshhome.mode_serial
         COMMAND ${CMAKE_COMMAND} -DHOME_DIR=${_thr_home_ser}
                 -P ${CMAKE_SOURCE_DIR}/tests/samples/freshhome.cmake)
set_tests_properties(threading.freshhome.mode_serial PROPERTIES
    FIXTURES_SETUP threading_home_mode_serial LABELS "threading")

add_test(NAME threading.mode
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/mode.js
         WORKING_DIRECTORY ${_thr_home_par}/run)
set_tests_properties(threading.mode PROPERTIES
    ENVIRONMENT "HOME=${_thr_home_par};JAHSHAKA_DATA_ROOT=${_thr_home_par}/.local/share/Jahshaka"
    FIXTURES_REQUIRED threading_home_mode
    FIXTURES_SETUP threading_shot_parallel
    LABELS "threading" RUN_SERIAL TRUE TIMEOUT 300)

# JAHSHAKA_SCENE_THREADS is the PRIMARY tier's measurement escape hatch
# (src/bridge/sceneworkerthreads.h:52-56) — pinning it to 1 gives the editor
# scene one worker, which is exactly the condition under which Ogre takes the
# serial compile branch (OgreRenderQueue.cpp:388 needs > 1).
add_test(NAME threading.mode_serial
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/mode.js
         WORKING_DIRECTORY ${_thr_home_ser}/run)
set_tests_properties(threading.mode_serial PROPERTIES
    ENVIRONMENT "HOME=${_thr_home_ser};JAHSHAKA_DATA_ROOT=${_thr_home_ser}/.local/share/Jahshaka;JAHSHAKA_SCENE_THREADS=1"
    FIXTURES_REQUIRED threading_home_mode_serial
    FIXTURES_SETUP threading_shot_serial
    LABELS "threading" RUN_SERIAL TRUE TIMEOUT 300)

add_test(NAME threading.mode_pixels
         COMMAND ${CMAKE_COMMAND} -E compare_files
                 ${_thr_home_par}/run/threading_mode.png
                 ${_thr_home_ser}/run/threading_mode.png)
set_tests_properties(threading.mode_pixels PROPERTIES
    FIXTURES_REQUIRED "threading_shot_parallel;threading_shot_serial"
    LABELS "threading" TIMEOUT 60)

# The race lane again: all three of these drive the REAL Jahshaka binary, which
# cannot start under GCC 15's libtsan at all (jah_tsan_blocked in
# tests/CMakeLists.txt documents the upstream sanitizer bug). They are eligible
# on subject and quarantined on toolchain — threading.staging_race is the member
# that actually carries this program's coverage in the lane.
jah_tsan_blocked(threading.mode threading.mode_serial)

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
configure_file(import_soak.js.in ${CMAKE_CURRENT_BINARY_DIR}/import_soak.js @ONLY)
configure_file(texture_wait_watchdog.js.in ${CMAKE_CURRENT_BINARY_DIR}/texture_wait_watchdog.js @ONLY)
configure_file(newproject_stall.js.in ${CMAKE_CURRENT_BINARY_DIR}/newproject_stall.js @ONLY)

set(_thr_home_soak ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-threading_import_soak)
set(_thr_home_stall ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-threading_newproject_stall)

add_test(NAME threading.freshhome.import_soak
         COMMAND ${CMAKE_COMMAND} -DHOME_DIR=${_thr_home_soak}
                 -P ${CMAKE_SOURCE_DIR}/tests/samples/freshhome.cmake)
set_tests_properties(threading.freshhome.import_soak PROPERTIES
    FIXTURES_SETUP threading_home_import_soak LABELS "threading")

# NOT RUN_SERIAL (TEST_GATE_AUDIT.md §2, 2026-09-09). threading.mode and
# threading.mode_serial keep theirs and the reason is written above them — four
# Epic opens compiling shaders at once is the measured origin of the whole
# contention class. This suite is different in kind: its assertions are
# PER-ITERATION BUDGETS with 30 s of headroom each, and a soak that a parallel
# schedule can starve inside a 30 s budget would be reporting a real defect.
# What it cost was a serial island of 12 s per gate.
add_test(NAME threading.import_soak
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/import_soak.js
         WORKING_DIRECTORY ${_thr_home_soak}/run)
set_tests_properties(threading.import_soak PROPERTIES
    ENVIRONMENT "HOME=${_thr_home_soak};JAHSHAKA_DATA_ROOT=${_thr_home_soak}/.local/share/Jahshaka"
    FIXTURES_REQUIRED threading_home_import_soak
    LABELS "threading" TIMEOUT 300)   # 20 iterations x a 30 s budget each: the timeout must not undercut the suite's own budget (code review 2026-09-10)

add_test(NAME threading.freshhome.newproject_stall
         COMMAND ${CMAKE_COMMAND} -DHOME_DIR=${_thr_home_stall}
                 -P ${CMAKE_SOURCE_DIR}/tests/samples/freshhome.cmake)
set_tests_properties(threading.freshhome.newproject_stall PROPERTIES
    FIXTURES_SETUP threading_home_newproject_stall LABELS "threading")

add_test(NAME threading.newproject_stall
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/newproject_stall.js
         WORKING_DIRECTORY ${_thr_home_stall}/run)
set_tests_properties(threading.newproject_stall PROPERTIES
    ENVIRONMENT "HOME=${_thr_home_stall};JAHSHAKA_DATA_ROOT=${_thr_home_stall}/.local/share/Jahshaka"
    FIXTURES_REQUIRED threading_home_newproject_stall
    LABELS "threading" RUN_SERIAL TRUE TIMEOUT 300)

# threading.texture_wait_watchdog — the give-up path itself, under the engine's
# JAH_TEXTURE_WAIT_FAULT hook (the script's header says why a hook is the only
# way). Its own HOME: the run deliberately renders frames whose textures are
# declared stuck, and nothing it writes should be handed to another suite.
# TIMEOUT is the real assertion here — with the pre-fix unbounded wait this run
# would never end.
set(_thr_home_wd ${CMAKE_CURRENT_BINARY_DIR}/e2e-home-threading_texture_wait_watchdog)

add_test(NAME threading.freshhome.texture_wait_watchdog
         COMMAND ${CMAKE_COMMAND} -DHOME_DIR=${_thr_home_wd}
                 -P ${CMAKE_SOURCE_DIR}/tests/samples/freshhome.cmake)
set_tests_properties(threading.freshhome.texture_wait_watchdog PROPERTIES
    FIXTURES_SETUP threading_home_texture_wait_watchdog LABELS "threading")

add_test(NAME threading.texture_wait_watchdog
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/texture_wait_watchdog.js
         WORKING_DIRECTORY ${_thr_home_wd}/run)
set_tests_properties(threading.texture_wait_watchdog PROPERTIES
    ENVIRONMENT "HOME=${_thr_home_wd};JAHSHAKA_DATA_ROOT=${_thr_home_wd}/.local/share/Jahshaka;JAH_TEXTURE_WAIT_FAULT=1;JAH_TEXTURE_WAIT_MS=300"
    FIXTURES_REQUIRED threading_home_texture_wait_watchdog
    LABELS "threading" TIMEOUT 300)

# The SEGV's precondition, greped off the logs both runs just wrote
# (nogrouplesstexture.cmake carries the mechanism). FIXTURES_REQUIRED on the run
# it inspects, so it can never pass by looking at an empty directory.
add_test(NAME threading.import_soak.no_groupless_texture
         COMMAND ${CMAKE_COMMAND} -DLOG_DIR=${_thr_home_soak}/run/logs
                 -P ${CMAKE_SOURCE_DIR}/tests/threading/nogrouplesstexture.cmake)
set_tests_properties(threading.import_soak.no_groupless_texture PROPERTIES
    DEPENDS threading.import_soak LABELS "threading" TIMEOUT 60)

add_test(NAME threading.newproject_stall.no_groupless_texture
         COMMAND ${CMAKE_COMMAND} -DLOG_DIR=${_thr_home_stall}/run/logs
                 -P ${CMAKE_SOURCE_DIR}/tests/threading/nogrouplesstexture.cmake)
set_tests_properties(threading.newproject_stall.no_groupless_texture PROPERTIES
    DEPENDS threading.newproject_stall LABELS "threading" TIMEOUT 60)

# Same toolchain quarantine as threading.mode: the real binary cannot start
# under GCC 15's libtsan.
jah_tsan_blocked(threading.import_soak threading.newproject_stall
                 threading.texture_wait_watchdog)
