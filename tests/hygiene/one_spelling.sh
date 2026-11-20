#!/bin/bash
# tooling.one_spelling - A TREE IS CONFIGURED UNDER ITS ONE SPELLING (CODING_RULES §4; URGENT #129; WORKFLOW-STREAMLINE-1 6.5):
# cmake/OneSpelling.cmake in a toy project (NONE languages, `cmake -S` only, no build), under a scratch HOME whose Developer is
# a symlink to a scratch target - the box's layout in miniature:
#   (1) a FIRST configure whose source dir is spelled through the target is REFUSED, naming the link's spelling;
#   (2) ...and one whose build dir is, with the source under the link;
#   (3) the same project under the link configures, from an absolute -S and from `cd <link>/... && cmake -S .`;
#   (4) JAH_ONE_SPELLING_LINK=none switches the check off (a box with another layout);
#   (5) a HOME whose Developer is a real directory has no second spelling: nothing triggers;
#   (6) a configured build re-configured under the target is still refused (the re-configure guard).
# usage: one_spelling.sh <the tree's cmake/OneSpelling.cmake>
set -u
M=$(cd "$(dirname "${1:?OneSpelling.cmake}")" && pwd)/$(basename "$1")
T=$(mktemp -d "${TMPDIR:-/tmp}/one-spelling.XXXXXX"); trap 'rm -rf "$T"' EXIT
cd "$T" || exit 1
unset JAH_ONE_SPELLING_LINK
fails=0
want() { if [ "$1" = 1 ]; then echo "  ok   $2"; else echo "  FAIL $2"; fails=$((fails + 1)); fi; }
mkdir -p "$T/real/Developer/toy" "$T/home" "$T/flat/Developer/toy"
ln -s "$T/real/Developer" "$T/home/Developer"
for d in "$T/real/Developer/toy" "$T/flat/Developer/toy"; do
    printf 'cmake_minimum_required(VERSION 3.20)\nproject(toy NONE)\ninclude("%s")\n' "$M" > "$d/CMakeLists.txt"
done
cfg() { local o=$1; shift; env HOME="$T/home" "$@" > "$T/$o.out" 2>&1; echo $?; }
L=$T/home/Developer/toy R=$T/real/Developer/toy
r=$(cfg c1 cmake -S "$R" -B "$T/b1")
want "$([ "$r" != 0 ] && grep -q 'CMAKE_SOURCE_DIR is' "$T/c1.out" && tr -d '\n ' < "$T/c1.out" | grep -qF "$(printf '%s' "$L" | tr -d ' ')" && echo 1)" \
    "(1) a first configure with the source spelled through the target is refused, naming $L (rc $r)"
r=$(cfg c2 cmake -S "$L" -B "$R/b2")
want "$([ "$r" != 0 ] && grep -q 'CMAKE_BINARY_DIR is' "$T/c2.out" && echo 1)" "(2) ...and with the build dir spelled through the target (rc $r)"
r=$(cfg c3 cmake -S "$L" -B "$L/b3"); r2=$(cd "$L" && cfg c3b cmake -S . -B b3b)
want "$([ "$r" = 0 ] && [ "$r2" = 0 ] && echo 1)" "(3) under the link it configures: absolute -S (rc $r), cd + -S . (rc $r2)"
r=$(cfg c4 env JAH_ONE_SPELLING_LINK=none cmake -S "$R" -B "$T/b4")
want "$([ "$r" = 0 ] && echo 1)" "(4) JAH_ONE_SPELLING_LINK=none: no check (rc $r)"
r=$(HOME="$T/flat" cmake -S "$T/flat/Developer/toy" -B "$T/b5" > "$T/c5.out" 2>&1; echo $?)
want "$([ "$r" = 0 ] && echo 1)" "(5) a HOME whose Developer is a real directory: nothing triggers (rc $r)"
r=$(cfg c6 cmake -S "$L" -B "$T/b6"); r2=$(cfg c6b env JAH_ONE_SPELLING_LINK=none cmake -S "$R" -B "$T/b6")
want "$([ "$r" = 0 ] && [ "$r2" != 0 ] && grep -q 'second spelling' "$T/c6b.out" && echo 1)" \
    "(6) a configured build re-configured under the target is still refused (rc $r then $r2)"
echo "tooling.one_spelling: $([ $fails = 0 ] && echo PASS || echo "FAIL ($fails)")"; exit $((fails > 0))
