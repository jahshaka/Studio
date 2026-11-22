#!/bin/bash
# tooling.build_ogre_spelling - build-ogre.sh BUILDS UNDER THE TREE's ONE SPELLING, whichever spelling it was called by (CODING_RULES
# §4; URGENT #208, a RECURRENCE: run from /mnt/work/... it configured the Ogre install prefix under that spelling - SHADER-SYSTEM-1
# 2026-11-21, ATOM-TESTS-BACK-1 2026-11-22). The script under test runs in a toy irisgl (a fork clone tagged jahshaka-stack-v1),
# under a scratch HOME whose Developer is a symlink to a scratch target - the box's layout in miniature - with a stub `cmake` first
# on PATH that records its -S, -B and -DCMAKE_INSTALL_PREFIX and stops the script (no build). The link is cmake/OneSpelling.cmake's:
# JAH_ONE_SPELLING_LINK, else $HOME/Developer; `none` switches it off.
#   (1) called through the link: the source, the build dir and the prefix under the link;
#   (2) called from the TARGET's spelling (cd there, ./scripts/build-ogre.sh): the same three, under the link, said in one line
#       (red before #208);
#   (3) called by an absolute target path from elsewhere: under the link;
#   (4) OGRE_PREFIX given in the target's spelling: under the link;
#   (5) JAH_ONE_SPELLING_LINK=none: the spelling it was called by, untouched;
#   (6) a HOME whose Developer is a real directory: no second spelling, nothing re-spelled.
# usage: build_ogre_spelling.sh <irisgl/scripts/build-ogre.sh>
set -u
B=$(cd "$(dirname "${1:?build-ogre.sh}")" && pwd)/$(basename "$1")
T=$(mktemp -d "${TMPDIR:-/tmp}/build-ogre-spelling.XXXXXX"); trap 'rm -rf "$T"' EXIT
cd "$T" || exit 1
unset JAH_ONE_SPELLING_LINK OGRE_PREFIX OGRE_SOURCE
fails=0
want() { if [ "$1" = 1 ]; then echo "  ok   $2"; else echo "  FAIL $2"; fails=$((fails + 1)); fi; }
mkdir -p "$T/real/Developer" "$T/home" "$T/flat/Developer" "$T/bin"
ln -s "$T/real/Developer" "$T/home/Developer"
toy() {   # toy <dir>: an irisgl with the script under test and a fork clone the anchor check accepts
    mkdir -p "$1/scripts" "$1/thirdparty/ogre-next"; cp "$B" "$1/scripts/build-ogre.sh"
    local f=$1/thirdparty/ogre-next
    git -C "$f" init -q; echo 'project(ogre NONE)' > "$f/CMakeLists.txt"; git -C "$f" add CMakeLists.txt
    git -C "$f" -c user.name=t -c user.email=t@t commit -qm anchor; git -C "$f" tag jahshaka-stack-v1
}
toy "$T/real/Developer/wt/irisgl"; toy "$T/flat/Developer/wt/irisgl"
cat > "$T/bin/cmake" <<'K'
#!/bin/sh
# the stub: the configure's three paths, then a stop (the script is `set -e`: nothing is built)
s= b= p=; prev=
for a in "$@"; do
    case $prev in -S) s=$a ;; -B) b=$a ;; esac
    case $a in -DCMAKE_INSTALL_PREFIX=*) p=${a#-DCMAKE_INSTALL_PREFIX=} ;; esac
    prev=$a
done
printf 'S=%s\nB=%s\nPREFIX=%s\n' "$s" "$b" "$p" > "$TOY_CMAKE_LOG"; exit 3
K
chmod +x "$T/bin/cmake"
run() {   # run <name> <cwd> <home> <script> [env...]: the script once, its three paths in $T/<name>.log
    local n=$1 d=$2 h=$3 s=$4; shift 4
    (cd "$d" && env HOME="$h" PATH="$T/bin:$PATH" TOY_CMAKE_LOG="$T/$n.log" JOBS=1 "$@" bash "$s" > "$T/$n.out" 2>&1)
    sed -n 's/^PREFIX=//p' "$T/$n.log" 2> /dev/null
}
all() { grep -q "^S=$2/thirdparty/ogre-next$" "$T/$1.log" && grep -q "^B=$2/thirdparty/ogre-next/build$" "$T/$1.log" \
            && grep -q "^PREFIX=$2/thirdparty/ogre-next-install$" "$T/$1.log" && echo 1; }
L=$T/home/Developer/wt/irisgl R=$T/real/Developer/wt/irisgl
p=$(run c1 "$L" "$T/home" ./scripts/build-ogre.sh)
want "$(all c1 "$L")" "(1) called through the link: the prefix $p"
p=$(run c2 "$R" "$T/home" ./scripts/build-ogre.sh)
want "$([ -n "$(all c2 "$L")" ] && grep -qF "building under the tree's one spelling $L" "$T/c2.out" && echo 1)" \
    "(2) called from the target's spelling: the prefix $p (the link's spelling: $L/...), said in one line"
p=$(run c3 / "$T/home" "$R/scripts/build-ogre.sh")
want "$(all c3 "$L")" "(3) called by an absolute target path from /: the prefix $p"
p=$(run c4 "$R" "$T/home" ./scripts/build-ogre.sh OGRE_PREFIX="$T/real/Developer/opt")
want "$([ "$p" = "$T/home/Developer/opt" ] && echo 1)" "(4) OGRE_PREFIX in the target's spelling: the prefix $p"
p=$(run c5 "$R" "$T/home" ./scripts/build-ogre.sh JAH_ONE_SPELLING_LINK=none)
want "$(all c5 "$R")" "(5) JAH_ONE_SPELLING_LINK=none: the spelling it was called by, the prefix $p"
p=$(run c6 "$T/flat/Developer/wt/irisgl" "$T/flat" ./scripts/build-ogre.sh)
want "$(all c6 "$T/flat/Developer/wt/irisgl")" "(6) a HOME whose Developer is a real directory: nothing re-spelled, the prefix $p"
echo "tooling.build_ogre_spelling: $([ $fails = 0 ] && echo PASS || echo "FAIL ($fails)")"; exit $((fails > 0))
