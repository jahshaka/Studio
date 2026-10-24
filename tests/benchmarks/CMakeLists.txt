# The scene-graph BENCHMARK — a MEASURE (SPECS/SCENEGRAPH_SPEC.md; the header comment of bench_scenegraph.cpp is
# the reference):
#   ./bench_scenegraph --assert          the measure's own physics: every metric exists, its dispersion holds;
#                                        every median against baseline-linux.json, bar max(10 %, 3 x cv)
#   ./bench_scenegraph --record <file>   ...and the JSON result file (the evidence)
#   ./bench_scenegraph --baseline-from baseline-linux.json <r1> <r2> <r3>   the baseline, from three quiet runs
# baseline-linux.json is recorded by hand on a quiet box from one build (SCENEGRAPH-BASELINE-2 stage 4) and
# rewritten only by the spec that earns an improvement; the reparent's law in counts is mirror.reparent_local (archived: archive/tests/mirror)'s.
#
# It links exactly what the mirror suite links (IrisGL + JahshakaEngine + the
# mirror TU, which is not part of either library), because "document edit ->
# engine-visible" is by definition a both-sides measurement.
#
# TWO ROWS PER BENCH SINCE 2026-09-09 (TEST_GATE_AUDIT.md §5 step 1):
#
#   <bench>.benchmark  --assert  LABELS "stage-close;quiet-box" STAGE-CLOSE, RUN_SERIAL, the GPU lock
#   <bench>.smoke      --smoke   LABELS "benchmark-smoke"   MERGE, parallel
#
# The full `--assert` run is a minute of scene-graph measurement on a quiet box (52 s, 2026-10-19; five minutes
# while a 50k reparent cost 5 s), RUN_SERIAL because a parallel ctest
# schedule turns its dispersion and baseline bars into random reds. A merge
# gate paid the whole of that wall time (287 s recorded, 518 s in a live -j2
# gate) for a verdict its own comment already disowns on a loaded box. What a
# merge gate needs is the STRUCTURAL half — every document node reached the
# engine, the static tags landed, a frame rendered — which is what `--smoke`
# runs, at one scale, in about fifteen seconds, with no dispersion bound, so it neither needs nor deserves RUN_SERIAL.
#
# The MERGE tier's -LE (`gate-scope.py --merge-tier`, label `stage-close`) therefore skips
# the minutes and KEEPS the structure; the stage-close tier (`--stage-close-tier`) gets the
# verdict. (The rows were labelled `benchmark` until D6B-GATE-SHAPE, when that label
# was also the tree's stage-close marker for rows that are not benchmarks at all.)
find_package(Qt6 REQUIRED COMPONENTS Gui)

# The fingerprint the result file records. Resolved at CONFIGURE time — a
# record whose commit is unknown says nothing about which build it measured.
execute_process(COMMAND git -C "${CMAKE_SOURCE_DIR}" rev-parse --short=8 HEAD
                OUTPUT_VARIABLE JAH_BENCH_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET)
if(NOT JAH_BENCH_COMMIT)
    set(JAH_BENCH_COMMIT "unknown")
endif()

add_executable(bench_scenegraph bench_scenegraph.cpp
               ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(bench_scenegraph PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(bench_scenegraph PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(bench_scenegraph PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/"
    BENCH_GIT_COMMIT="${JAH_BENCH_COMMIT}"
    BENCH_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    BENCH_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
    BENCH_BASELINE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/baseline-linux.json")
target_link_libraries(bench_scenegraph PRIVATE IrisGL JahshakaEngine Qt6::Gui)
add_dependencies(bench_scenegraph JahshakaEngineMedia)

jah_gpu_row(scenegraph.smoke SHIP CLASS engine COMMAND bench_scenegraph --smoke
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/smoke")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/smoke")
# NOT RUN_SERIAL, deliberately: --smoke asserts nothing about time, so a
# parallel schedule cannot change its answer. Its own working directory because
# both rows write an Ogre log.
set_tests_properties(scenegraph.smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "benchmark-smoke")

# THE RIG-PERF BENCH (SPECS/AVATAR_RIG_PERF_SPEC.md P0 / P3).
#
# Same shape as bench_scenegraph — one binary, `--assert` for ctest and
# `--record <file>` for the committed numbers — but a different subject: four
# five-piece characters, and the three structural counts (SkeletonInstances,
# setClipStates calls per frame, bone matrices streamed per pass) the sharing
# program claims to reduce. P0 records them; P3 arms the equalities.
add_executable(bench_rigperf bench_rigperf.cpp
               ${CMAKE_SOURCE_DIR}/irisgl/mirror/scenemirror.cpp)
set_target_properties(bench_rigperf PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    BUILD_RPATH "${OGRE_NEXT_PREFIX}/lib" INSTALL_RPATH "${OGRE_NEXT_PREFIX}/lib")
target_include_directories(bench_rigperf PRIVATE ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/irisgl
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/bullet3/src)
target_compile_definitions(bench_rigperf PRIVATE
    JAHSHAKA_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    JAHSHAKA_TEST_PLUGIN_DIR="${OGRE_NEXT_PLUGIN_DIR}"
    JAHSHAKA_TEST_MEDIA_DIR="${JAHSHAKA_MEDIA_DIR}/"
    BENCH_GIT_COMMIT="${JAH_BENCH_COMMIT}"
    BENCH_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
    BENCH_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
target_link_libraries(bench_rigperf PRIVATE IrisGL JahshakaEngine Qt6::Gui)
add_dependencies(bench_rigperf JahshakaEngineMedia)

jah_gpu_row(rigperf.smoke SHIP CLASS engine COMMAND bench_rigperf --smoke
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/rigsmoke")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/rigsmoke")
set_tests_properties(rigperf.smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "benchmark-smoke")

# FRAMES — THE FRAMES EACH ROW RENDERS (TESTING-SPEED-B3; SPECS/TESTING_SPEED_SPEC.md §2 A8): the test property
# JAHSHAKA_TEST_FRAMES, measured 2026-10-12 at the lane's tip by spikes/testing-speed-b3/framecount (an LD_PRELOAD meter of
# Ogre::Root::_fireFrameEnded, summed over the row's processes); 0 = a row that renders nothing.
foreach(_row_frames
        "rigperf.smoke=39"
        "scenegraph.smoke=370"
        )
    string(REPLACE "=" ";" _rf "${_row_frames}")
    list(GET _rf 0 _r)
    list(GET _rf 1 _n)
    if(TEST ${_r})
        set_tests_properties(${_r} PROPERTIES JAHSHAKA_TEST_FRAMES ${_n})
    endif()
endforeach()
