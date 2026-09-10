# The scene-graph BENCHMARK GATE (SPECS/SCENEGRAPH_SPEC.md §6).
#
# One binary, two modes (the header comment of bench_scenegraph.cpp is the
# reference):
#   ./bench_scenegraph --assert          gating: stability bounds + numbers exist
#   ./bench_scenegraph --record <file>   emits the JSON result/baseline file
#
# WHAT CTEST RUNS IS --assert ONLY, and deliberately so. A recorded number is
# only comparable against numbers taken the same way on the same box; a gate
# that rewrote the baseline every run would compare a machine to itself under
# whatever load it happened to be under. The committed baseline
# (tests/benchmarks/baseline-linux.json) is produced by hand, from a build made
# WITHOUT any sanitizer, on a quiet box — see that file's `config` block.
#
# It links exactly what the mirror suite links (IrisGL + JahshakaEngine + the
# mirror TU, which is not part of either library), because "document edit ->
# engine-visible" is by definition a both-sides measurement.
#
# TWO ROWS PER BENCH SINCE 2026-09-09 (TEST_GATE_AUDIT.md §5 step 1):
#
#   <bench>.benchmark  --assert  LABELS "benchmark"        NIGHTLY, RUN_SERIAL
#   <bench>.smoke      --smoke   LABELS "benchmark-smoke"  MERGE, parallel
#
# The full `--assert` run is a TIMING VERDICT: five minutes of scene-graph
# measurement compared against a committed baseline, RUN_SERIAL because a
# parallel ctest schedule turns its regress clause into random reds. A merge
# gate paid the whole of that wall time (287 s recorded, 518 s in a live -j2
# gate) for a verdict its own comment already disowns on a loaded box. What a
# merge gate needs is the STRUCTURAL half — every document node reached the
# engine, the static tags landed, a frame rendered — which is what `--smoke`
# runs, at one scale, in about fifteen seconds, with no baseline compare and no
# dispersion bound, so it neither needs nor deserves RUN_SERIAL.
#
# `ctest -LE benchmark` therefore still skips the minutes and now KEEPS the
# structure; a nightly / pre-tag run drops -LE and gets the verdict.
find_package(Qt6 REQUIRED COMPONENTS Gui)

# The fingerprint the baseline file records. Resolved at CONFIGURE time — a
# baseline whose commit is unknown cannot be compared against anything.
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

add_test(NAME scenegraph.benchmark COMMAND bench_scenegraph --assert
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# RUN_SERIAL: wall-clock measurements — a parallel ctest schedule distorts the
# scale-1000/200 regress clause into random reds (2026-09-05 gate: the violating
# metric set differed every run while the gated improve clauses held in all
# four). Serial keeps ctest's own suites off its back; OTHER processes on the
# box still can — a red here under external load is retried on a quiet box
# before it is believed (gate protocol).
set_tests_properties(scenegraph.benchmark PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 1200 LABELS "benchmark"
    RUN_SERIAL TRUE)

add_test(NAME scenegraph.smoke COMMAND bench_scenegraph --smoke
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/smoke")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/smoke")
# NOT RUN_SERIAL, deliberately: --smoke asserts nothing about time, so a
# parallel schedule cannot change its answer. Its own working directory because
# both rows write an Ogre log.
set_tests_properties(scenegraph.smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 300 LABELS "benchmark-smoke")

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

add_test(NAME rigperf.benchmark COMMAND bench_rigperf --assert
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
# RUN_SERIAL for the same reason scenegraph.benchmark is: it times wall clock,
# and a parallel ctest schedule turns the dispersion bound into a coin flip.
set_tests_properties(rigperf.benchmark PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600 LABELS "benchmark"
    RUN_SERIAL TRUE)

add_test(NAME rigperf.smoke COMMAND bench_rigperf --smoke
         WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/rigsmoke")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/rigsmoke")
set_tests_properties(rigperf.smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "benchmark-smoke")
