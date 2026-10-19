#!/usr/bin/env bash
#
# The on-demand sanitizer lanes (deep audit 2026-09, area 3 finding 4:
# "no sanitizer has ever seen the app").
#
#   ./scripts/sanitize.sh lsan [ctest args...]    leaks, document/service suites
#   ./scripts/sanitize.sh tsan [ctest args...]    data races, async suites
#
# NEITHER LANE IS PART OF THE DEFAULT GATE. A plain `ctest` in build-linux runs
# exactly what it always ran; these lanes are slower (LSan ~1.3x, TSan 5-15x on
# the suites that drive the whole app) and are meant to be run deliberately —
# after threading or ownership work, and before a release.
#
# ---------------------------------------------------------------- lsan --------
# LeakSanitizer needs no rebuild: its runtime intercepts malloc/free from
# LD_PRELOAD, so the ORDINARY Debug binaries are the ones under test (identical
# code to the ones the normal gate runs — no second build, no second copy of a
# suite, nothing to drift). CTest's per-test ENVIRONMENT property *adds to* the
# environment it inherits, so exporting the preload here reaches every test
# process the lane starts.
#
# Membership is the ctest label `lsan`, set next to each suite's add_test (see
# tests/CMakeLists.txt): the document/service suites; the app-spawning suites
# are eligible (their harness reads the child's exit, tests/support/
# mcpharness.h quitAndReap: a green app row means the APP leaked nothing).
# The four ASan-built engine fixtures stay out (detect_leaks=0 there).
#
# ---------------------------------------------------------------- tsan --------
# ThreadSanitizer cannot coexist with AddressSanitizer and instruments every
# translation unit, so it gets its OWN build directory (default build-tsan-clang/, built with clang),
# configured with -DJAHSHAKA_TSAN=ON. Ogre-Next and Qt stay uninstrumented (we
# link their installed shared libraries); scripts/tsan.supp covers what that
# costs. Membership is the ctest label `tsan`: the async-contract suites
# (importasync, openasync, archive.responsive, shutdown, services.memory,
# threading.staging_race).
#
# WHAT THIS LANE CANNOT SEE, said here as well as in tsan.supp because it is the
# lane's biggest limitation: Ogre-Next is an INSTALLED SHARED LIBRARY we do not
# compile, so none of its accesses are instrumented. A race living entirely
# inside the engine — which is where the scene-manager worker pools live, and
# where THREADING_ADOPTION_SPEC P1 puts shader compilation — is invisible to
# this lane however green it is. threading.staging_race is a member
# because its subject is OUR thread building document nodes beside the render
# loop; the pre-fix use-after-free it regression-tests was two Ogre-internal
# accesses, and it was caught by RUNNING it (a SIGSEGV in
# Ogre::Node::updateAllTransforms), not by a TSan report.
#
# Race reports go to $BUILD/tsan-logs/tsan.<pid> (TSAN_OPTIONS=log_path), because
# most of these suites spawn the real binary as a CHILD process — its reports
# would otherwise vanish into a captured stdout. The script prints a summary at
# the end whether or not ctest passed.
#
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

usage() {
    cat <<'EOF'
usage: scripts/sanitize.sh <lsan|tsan> [extra ctest arguments...]

  lsan   run the leak lane over the ordinary build dir  (env JAH_BUILD_DIR, default build-linux)
  tsan   configure/build/run the race lane              (env JAH_TSAN_BUILD_DIR, default build-tsan-clang)

Environment:
  JAH_BUILD_DIR       build dir the lsan lane uses            (default build-linux)
  JAH_TSAN_BUILD_DIR  build dir the tsan lane configures      (default build-tsan-clang; clang required)
  JAH_SAN_NO_BUILD=1  skip the build step (run what is there)
  JAH_SAN_BUILD_ONLY=1  configure/build only, run nothing (the ship builds the tsan
                      lane under ionice, then runs it without: IONICE-1)
  JAH_SAN_BUILD_JOBS  the build width, an explicit override. Default: THE MEMORY
                      LAW — 12 compile jobs box-wide divided by
                      1 + the other live ninja processes, never below 2
  DISPLAY             REQUIRED by the lsan lane: its app-spawning rows boot
                      Jahshaka — your own Xvfb, never a shared session, under
                      the card lock (scripts/gpu-exclusive.sh).
EOF
}

BUILD_DIR="${JAH_BUILD_DIR:-build-linux}"
TSAN_BUILD_DIR="${JAH_TSAN_BUILD_DIR:-build-tsan-clang}"

# The flags every configure in this tree needs (CLAUDE.md "Build").
COMMON_CMAKE_ARGS=(
    -G Ninja
    -DCMAKE_BUILD_TYPE=Debug
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    -DDISABLE_BREAKPAD=ON
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
)

die() { echo "sanitize: $*" >&2; exit 1; }

# THE BUILD WIDTH (THE MEMORY LAW, DOCS/traps/GATE_AND_RIG.md; the runner's rule): the box's ~12 compile
# jobs are shared by every live build — max(2, 12 // (1 + n)) with n the other live `ninja` processes (two Debug builds
# at -j$(nproc) took the box down on 2026-09-25). JAH_SAN_BUILD_JOBS overrides it.
build_jobs() {
    if [ -n "${JAH_SAN_BUILD_JOBS:-}" ]; then echo "$JAH_SAN_BUILD_JOBS"; return; fi
    local n j
    n=$(pgrep -x ninja | wc -l)
    j=$(( 12 / (1 + n) )); [ "$j" -ge 2 ] || j=2
    echo "$j"
}

# ---------------------------------------------------------------------------
# The leak lane.
# ---------------------------------------------------------------------------
run_lsan() {
    local lsan_lib
    lsan_lib="$(${CC:-gcc} -print-file-name=liblsan.so 2>/dev/null)"
    [ -f "$lsan_lib" ] || die "no standalone LeakSanitizer runtime (liblsan.so) for ${CC:-gcc}"

    # Honesty check. A preload that silently fails to take (wrong ABI, a static
    # binary, an LD_PRELOAD stripped by the loader) would leave the whole lane
    # green while checking nothing at all, which is worse than no lane. Prove
    # the runtime is live on a program that leaks 4321 bytes on purpose.
    local canary_src canary_bin
    canary_src="$(mktemp --suffix=.c)"
    canary_bin="$(mktemp -u)"
    printf '#include <stdlib.h>\nint main(void){ volatile void *p = malloc(4321); (void)p; return 0; }\n' > "$canary_src"
    if ! ${CC:-gcc} -g -O0 -o "$canary_bin" "$canary_src" 2>/dev/null; then
        rm -f "$canary_src"
        die "cannot compile the leak canary (no working ${CC:-gcc}?)"
    fi
    LD_PRELOAD="$lsan_lib" LSAN_OPTIONS="exitcode=23" "$canary_bin" >/dev/null 2>&1
    local canary_rc=$?
    rm -f "$canary_src" "$canary_bin"
    [ "$canary_rc" = "23" ] || die "the LeakSanitizer preload did not take (canary exit $canary_rc, expected 23) — the lane would be meaningless"
    echo "sanitize: LeakSanitizer runtime $lsan_lib (canary ok)"

    [ -f "$BUILD_DIR/CMakeCache.txt" ] || die "$BUILD_DIR is not configured (see docs/BUILDING_LINUX.md)"
    [ -n "${DISPLAY:-}" ] || [ -n "${JAH_SAN_BUILD_ONLY:-}" ] \
        || die "no DISPLAY: the lane's app-spawning rows boot Jahshaka (your own Xvfb, under scripts/gpu-exclusive.sh)"
    if [ -z "${JAH_SAN_NO_BUILD:-}" ]; then
        echo "sanitize: building $BUILD_DIR at -j$(build_jobs)"
        cmake --build "$BUILD_DIR" -j"$(build_jobs)" > "$BUILD_DIR/sanitize-lsan-build.log" 2>&1 \
            || { grep -E 'error:' "$BUILD_DIR/sanitize-lsan-build.log" | head -40; die "build failed (see $BUILD_DIR/sanitize-lsan-build.log)"; }
    fi
    [ -n "${JAH_SAN_BUILD_ONLY:-}" ] && { echo "sanitize: built $BUILD_DIR (JAH_SAN_BUILD_ONLY)"; return 0; }

    # suppressions   only verified-external one-time allocations (scripts/lsan.supp).
    # exitcode=23    a leak fails its ctest entry; 23 keeps it distinct from a
    #                suite's own non-zero exits.
    # print_suppressions=1  every run states what it suppressed and how much:
    #                a suppression file nobody audits is a lie with a filename.
    export LD_PRELOAD="$lsan_lib"
    export LSAN_OPTIONS="suppressions=$REPO_ROOT/scripts/lsan.supp:print_suppressions=1:exitcode=23"
    # THE DRIVER STAYS NAMED (LEAK-1). The app-spawning rows boot the real app, and the Vulkan loader dlclose()s the
    # ICD at vkDestroyInstance: every allocation the driver keeps then prints as `<unknown module>`, which no
    # suppression can name and no reader can attribute. Kept loaded, its frames say libnvidia-glcore (lsan.supp).
    export VK_LOADER_DISABLE_DYNAMIC_LIBRARY_UNLOADING=1
    echo "sanitize: LSAN_OPTIONS=$LSAN_OPTIONS"

    # -L takes a REGEX: anchor it (an unanchored `lsan` matches every label that contains it).
    ( cd "$BUILD_DIR" && ctest -L '^lsan$' --output-on-failure "$@" )
    local rc=$?
    unset LD_PRELOAD LSAN_OPTIONS VK_LOADER_DISABLE_DYNAMIC_LIBRARY_UNLOADING

    return $rc
}

# ---------------------------------------------------------------------------
# The race lane.
# ---------------------------------------------------------------------------
run_tsan() {
    if [ ! -f "$TSAN_BUILD_DIR/CMakeCache.txt" ]; then
        echo "sanitize: configuring $TSAN_BUILD_DIR (-DJAHSHAKA_TSAN=ON)"
        mkdir -p "$TSAN_BUILD_DIR"
        # THE ARM BUILDS WITH CLANG (ENGINE-UPGRADE-U3 decision 49): clang's own libtsan, against the libstdc++ of the
        # GCC the rest of the tree (and the Ogre install) was built with — clang picks the NEWEST GCC directory it finds,
        # which on this box (GCC 16's runtime libs without its libstdc++) does not link.
        # CLANG IS REQUIRED (decision 62): no silent fall back to GCC's libtsan, whose registry CHECK aborts the app.
        command -v clang++ > /dev/null 2>&1 || die "the race lane builds with clang: clang++ is not installed"
        local gccdir="/usr/lib/gcc/$(gcc -dumpmachine)/$(gcc -dumpversion | cut -d. -f1)"
        local pin=""
        [ -d "$gccdir" ] && pin="--gcc-install-dir=$gccdir"
        local tsan_cc=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
                       "-DCMAKE_C_FLAGS=$pin" "-DCMAKE_CXX_FLAGS=$pin" "-DCMAKE_EXE_LINKER_FLAGS=$pin"
                       "-DCMAKE_SHARED_LINKER_FLAGS=$pin")
        echo "sanitize: the race lane builds with clang ($pin)"
        cmake -S . -B "$TSAN_BUILD_DIR" "${COMMON_CMAKE_ARGS[@]}" "${tsan_cc[@]}" -DJAHSHAKA_TSAN=ON \
            ${JAH_TEST_DISPLAY:+-DJAH_TEST_DISPLAY="$JAH_TEST_DISPLAY"} \
            > "$TSAN_BUILD_DIR/configure.log" 2>&1 \
            || { tail -30 "$TSAN_BUILD_DIR/configure.log"; die "configure failed"; }
    fi
    grep -q "JAHSHAKA_TSAN:BOOL=ON" "$TSAN_BUILD_DIR/CMakeCache.txt" \
        || die "$TSAN_BUILD_DIR was not configured with -DJAHSHAKA_TSAN=ON"
    grep -q "CMAKE_CXX_COMPILER:.*clang" "$TSAN_BUILD_DIR/CMakeCache.txt" \
        || die "$TSAN_BUILD_DIR was configured with another compiler: the race lane is clang's (remove the directory)"

    # Only what the lane runs: the app (five of the suites drive the real
    # binary) plus the in-process suites. Building the engine's pixel suites
    # under TSan would cost half an hour and prove nothing.
    local targets=(Jahshaka test_import_async test_import_shutdown test_open_responsive
                   test_archive_responsive test_shutdown_order test_watchdog_stall
                   test_memory_ownership test_staging_race)
    # THE RENDER THREAD'S ARM (ENGINE-UPGRADE-U3 decision 47): JAH_TSAN_LABEL=tsan-render runs the rows labelled so —
    # measured by price, run by hand, never part of the shipped lane — and builds their targets.
    local label="${JAH_TSAN_LABEL:-tsan}"
    if [ "$label" = "tsan-render" ]; then
        targets=(Jahshaka test_engine_record_determinism test_window_render_thread test_engine_message_order
                 test_engine_drag_latency test_engine_interpolation test_engine_error_door
                 test_engine_monitor_pop test_engine_gpu_walk_parity)   # ...and engine.gpuscene_identity's (U4 stage 4)
    fi
    if [ -z "${JAH_SAN_NO_BUILD:-}" ]; then
        echo "sanitize: building ${targets[*]} at -j$(build_jobs)"
        cmake --build "$TSAN_BUILD_DIR" -j"$(build_jobs)" --target "${targets[@]}" \
            > "$TSAN_BUILD_DIR/sanitize-tsan-build.log" 2>&1 \
            || { grep -E 'error:' "$TSAN_BUILD_DIR/sanitize-tsan-build.log" | head -40; die "build failed (see $TSAN_BUILD_DIR/sanitize-tsan-build.log)"; }
    fi
    [ -n "${JAH_SAN_BUILD_ONLY:-}" ] && { echo "sanitize: built $TSAN_BUILD_DIR (JAH_SAN_BUILD_ONLY)"; return 0; }

    local logdir="$REPO_ROOT/$TSAN_BUILD_DIR/tsan-logs"
    rm -rf "$logdir"; mkdir -p "$logdir"
    # ignore_noninstrumented_modules=1 is what makes this lane readable. TSan
    # cannot see the synchronisation inside Qt (QMutex/QWaitCondition are
    # futex-based and libQt6Core is not instrumented), so without it EVERY
    # cross-thread new/delete of a QEvent or QArrayData is reported as a race:
    # 62 reports in the first run of this lane, none of them ours. With it, an
    # access is only reported when at least one side is in code we compiled.
    # report_thread_leaks=0: every thread in these processes belongs to Qt
    # (QThreadPool, QDBusConnection) and is left idle rather than joined at
    # exit, by Qt's design. A thread that was not joined is not a race.
    # LAVAPIPE FOR THE ONE VULKAN SUITE IN THE LANE (threading.staging_race).
    # TSan and the proprietary NVIDIA Vulkan driver do not coexist on this box:
    # the process dies with `ThreadSanitizer:DEADLYSIGNAL / nested bug in the
    # same thread, aborting` inside vkCreateDevice, before any of our code runs.
    # Mesa's llvmpipe ICD is instrumentation-friendly and renders identical
    # pixels (CLAUDE.md's recorded fact, and the reason no-GPU CI works), so the
    # lane picks it when it is installed rather than quarantining the suite.
    # Harmless for the lane's other members — none of them creates a device.
    # Override with VK_DRIVER_FILES to test a different ICD deliberately.
    if [ -z "${VK_DRIVER_FILES:-}" ] && [ -f /usr/share/vulkan/icd.d/lvp_icd.json ]; then
        export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.json
        echo "sanitize: VK_DRIVER_FILES=$VK_DRIVER_FILES (lavapipe — TSan cannot survive the NVIDIA driver)"
    fi
    export TSAN_OPTIONS="suppressions=$REPO_ROOT/scripts/tsan.supp:log_path=$logdir/tsan:history_size=4:second_deadlock_stack=1:ignore_noninstrumented_modules=1:report_thread_leaks=0"
    echo "sanitize: TSAN_OPTIONS=$TSAN_OPTIONS"
    [ -n "${DISPLAY:-}" ] || echo "sanitize: WARNING - no DISPLAY; the suites that spawn Jahshaka will fail"

    ( cd "$TSAN_BUILD_DIR" && ctest -L "^${label}\$" --output-on-failure "$@" )
    local rc=$?

    local tblocked
    tblocked="$( cd "$TSAN_BUILD_DIR" && ctest -N -L '^tsan-blocked$' 2>/dev/null \
                 | sed -n 's/^ *Test *#[0-9]*: //p' )"
    if [ -n "$tblocked" ]; then
        echo
        echo "=== quarantined suites (label tsan-blocked) — cannot run under TSan today ==="
        echo "$tblocked" | sed 's/^/  /'
        echo "  why: tests/CMakeLists.txt, above jah_tsan_blocked()"
    fi

    echo
    echo "=== ThreadSanitizer reports ($logdir) ==="
    local reports
    reports="$(grep -l "WARNING: ThreadSanitizer" "$logdir"/tsan.* 2>/dev/null)"
    if [ -z "$reports" ]; then
        echo "none"
    else
        # One line per distinct race: the summary line names the file:line.
        grep -h "^SUMMARY: ThreadSanitizer" "$logdir"/tsan.* 2>/dev/null | sort | uniq -c | sort -rn
        echo
        echo "full stacks: $logdir/"
    fi
    unset TSAN_OPTIONS
    return $rc
}

case "${1:-}" in
    lsan) shift; run_lsan "$@" ;;
    tsan) shift; run_tsan "$@" ;;
    -h|--help|help|"") usage; exit 1 ;;
    *) usage; die "unknown lane '$1'" ;;
esac
