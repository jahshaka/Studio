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
# LABEL `benchmark`: this suite is minutes, not seconds. `ctest -LE benchmark`
# skips it when you want the fast gate; the full gate runs it.
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
    ${CMAKE_SOURCE_DIR}/irisgl/thirdparty/assimp/include
    ${CMAKE_BINARY_DIR}/irisgl/thirdparty/assimp/include
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
set_tests_properties(scenegraph.benchmark PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 1200 LABELS "benchmark")
