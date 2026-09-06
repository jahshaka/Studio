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
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300 LABELS "threading")

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
    ENVIRONMENT "HOME=${_thr_home_par}"
    FIXTURES_REQUIRED threading_home_mode
    FIXTURES_SETUP threading_shot_parallel
    LABELS "threading" RUN_SERIAL TRUE TIMEOUT 480)

# JAHSHAKA_SCENE_THREADS is the PRIMARY tier's measurement escape hatch
# (src/bridge/sceneworkerthreads.h:52-56) — pinning it to 1 gives the editor
# scene one worker, which is exactly the condition under which Ogre takes the
# serial compile branch (OgreRenderQueue.cpp:388 needs > 1).
add_test(NAME threading.mode_serial
         COMMAND $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                 --script ${CMAKE_CURRENT_BINARY_DIR}/mode.js
         WORKING_DIRECTORY ${_thr_home_ser}/run)
set_tests_properties(threading.mode_serial PROPERTIES
    ENVIRONMENT "HOME=${_thr_home_ser};JAHSHAKA_SCENE_THREADS=1"
    FIXTURES_REQUIRED threading_home_mode_serial
    FIXTURES_SETUP threading_shot_serial
    LABELS "threading" RUN_SERIAL TRUE TIMEOUT 480)

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
