#!/bin/bash
# tooling.row_env - A ROW RUN BY HAND CARRIES ITS REGISTRATION (URGENT #147): scripts/row-env.sh on a toy ctest build (a
# scratch cmake project, CPU only; the GPU row's lock is a private lock file and its "GPU" a shell):
#   (1) it PRINTS the row as registered: ENVIRONMENT, ENVIRONMENT_MODIFICATION, the working directory, the command;
#   (2) --run applies them as ctest does (set, an op in order, unset) in the row's working directory;
#   (3) --run --scratch writes NOTHING under the build: the outputs land in the scratch, an input file the row names is
#       copied in, a fresh_home.sh --warm row's shared cache copied with its stamps (the home warm in the scratch);
#   (4) a row that requires fixtures is REFUSED (ctest runs their setups); a GPU row (JAH_CLASS) without an explicit
#       DISPLAY is REFUSED, with one it runs under the tree's gpu-exclusive.sh (JAH_GPU_LOCK_HELD=1 inside).
# usage: row_env.sh <the tree's scripts/row-env.sh>
set -u
RE=${1:?row-env.sh}
T=$(mktemp -d "${TMPDIR:-/tmp}/row-env.XXXXXX")
trap 'rm -rf "$T"' EXIT
export HOME="$T" JAH_GPU_LOCK="$T/lock"
unset JAH_GPU_LOCK_HELD JAH_GPU_HOLD
fails=0
want() { if [ "$1" = 1 ]; then echo "  ok   $2"; else echo "  FAIL $2"; fails=$((fails + 1)); fi; }
FH=$(cd "$(dirname "$RE")/../tests/support" && pwd)/fresh_home.sh
mkdir -p "$T/src"
cat > "$T/src/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.20)
project(toy NONE)
enable_testing()
file(MAKE_DIRECTORY \${CMAKE_BINARY_DIR}/tests/w \${CMAKE_BINARY_DIR}/tests/.shadercache-engine)
file(WRITE \${CMAKE_BINARY_DIR}/tests/in.txt "the input\n")
file(WRITE \${CMAKE_BINARY_DIR}/tests/.shadercache-engine/blob "a pipeline\n")
add_test(NAME toy.env COMMAND sh -c "echo \"\$A|\$B|\$C|\$(pwd)|\$(cat \${CMAKE_BINARY_DIR}/tests/in.txt)\" > out.txt"
         WORKING_DIRECTORY \${CMAKE_BINARY_DIR}/tests/w)
set_tests_properties(toy.env PROPERTIES ENVIRONMENT "A=1"
    ENVIRONMENT_MODIFICATION "B=set:2;B=string_append:x;C=unset:")
add_test(NAME toy.fx COMMAND true)
set_tests_properties(toy.fx PROPERTIES FIXTURES_REQUIRED home)
add_test(NAME toy.gpu COMMAND sh -c "echo \"held=\$JAH_GPU_LOCK_HELD\" > \${CMAKE_BINARY_DIR}/tests/w/gpu.txt")
set_tests_properties(toy.gpu PROPERTIES JAH_CLASS engine)
add_test(NAME toy.warm COMMAND sh "$FH" --warm --cache engine \${CMAKE_BINARY_DIR}/tests/homes/toy.warm --
         sh -c "ls \${CMAKE_BINARY_DIR}/tests/homes/toy.warm/.local/share/Jahshaka/shadercache > \${CMAKE_BINARY_DIR}/tests/w/warm.txt"
         WORKING_DIRECTORY \${CMAKE_BINARY_DIR}/tests/w)
EOF
cmake -S "$T/src" -B "$T/b" > "$T/cmake.out" 2>&1 || { echo "FAIL: the toy build: $(tail -3 "$T/cmake.out")"; exit 1; }
B=$(cd "$T/b" && /bin/pwd -P)
# (1) the print
bash "$RE" "$B" toy.env > "$T/p.out" 2>&1
want "$(grep -q '^  ENVIRONMENT A=1$' "$T/p.out" && grep -q 'ENVIRONMENT_MODIFICATION B=set:2' "$T/p.out" \
        && grep -q 'ENVIRONMENT_MODIFICATION C=unset:' "$T/p.out" && grep -q "working directory: $B/tests/w" "$T/p.out" && echo 1)" \
     "(1) the row as registered: ENVIRONMENT, ENVIRONMENT_MODIFICATION, its working directory"
# (2) --run as ctest does
C=leak bash "$RE" "$B" toy.env --run > "$T/r.out" 2>&1
want "$([ "$(cat "$B/tests/w/out.txt" 2>/dev/null)" = "1|2x||$B/tests/w|the input" ] && echo 1)" \
     "(2) --run: ENVIRONMENT set, the ops in order (set, string_append), unset, in the row's directory ($(cat "$B/tests/w/out.txt" 2>/dev/null))"
rm -f "$B/tests/w/out.txt"
# (3) --scratch: nothing under the build written
touch "$T/mark"; sleep 0.01
bash "$RE" "$B" toy.env --run --scratch "$T/s" > "$T/s.out" 2>&1
newer=$(find "$B" -newer "$T/mark" -not -type d | grep -v 'Testing/Temporary' | head -3)
want "$([ -z "$newer" ] && [ ! -e "$B/tests/w/out.txt" ] && [ "$(cat "$T/s/tests/w/out.txt" 2>/dev/null)" = "1|2x||$T/s/tests/w|the input" ] && echo 1)" \
     "(3) --scratch: the output in the scratch, the input copied in, nothing under the build written ($(cat "$T/s/tests/w/out.txt" 2>/dev/null); $newer)"
touch "$T/mark2"; sleep 0.01
bash "$RE" "$B" toy.warm --run --scratch "$T/s2" > "$T/w.out" 2>&1
newer=$(find "$B" -newer "$T/mark2" -not -type d | grep -v 'Testing/Temporary' | head -3)
want "$(grep -qx blob "$T/s2/tests/w/warm.txt" 2>/dev/null && [ -z "$newer" ] && echo 1)" \
     "(3) --scratch, a fresh_home.sh --warm --cache engine row: its shared cache copied, the home warm on it in the scratch, the build untouched ($(cat "$T/s2/tests/w/warm.txt" 2>/dev/null); $newer; $(tail -1 "$T/w.out"))"
# (4) refusals and the lock
bash "$RE" "$B" toy.fx --run > "$T/f.out" 2>&1; rc=$?
want "$([ $rc = 2 ] && grep -q 'REFUSED.*fixtures home' "$T/f.out" && echo 1)" "(4) a row requiring fixtures is refused (exit $rc)"
env -u DISPLAY bash "$RE" "$B" toy.gpu --run > "$T/g0.out" 2>&1; rc=$?
want "$([ $rc = 2 ] && grep -q 'explicit DISPLAY' "$T/g0.out" && echo 1)" "(4) a GPU row without an explicit DISPLAY is refused (exit $rc)"
DISPLAY=:99 bash "$RE" "$B" toy.gpu --run > "$T/g.out" 2>&1
want "$(grep -qx 'held=1' "$B/tests/w/gpu.txt" 2>/dev/null && echo 1)" "(4) a GPU row runs under the tree's gpu-exclusive.sh ($(cat "$B/tests/w/gpu.txt" 2>/dev/null))"
[ "$fails" = 0 ] && { echo "tooling.row_env: PASS"; exit 0; }
echo "tooling.row_env: FAILED ($fails)"; exit 1
