#!/usr/bin/env bash
# regen-api-docs.sh <build dir> [--data-root <dir>] — THE ONE WAY TO REGENERATE docs/SCRIPTING.md (the generated verb
# reference; never hand-edited). `Jahshaka --dump-api-docs` boots the whole app and engine (src/app/main.cpp: the
# scripting registry is filled by the MainWindow's module boot - every StudioModule registers its verbs there - so the
# docs cannot be written before the window and the engine host exist), so it is an app instance on the card: this runs
# it under scripts/gpu-exclusive.sh, on the caller's own Xvfb (an explicit DISPLAY, never :0), with a data root (a
# throwaway one in $TMPDIR unless --data-root names one: never inside the tree), and writes the tree's docs/SCRIPTING.md.
# (PHOTON-7 stage 5: a builder ran the dump on its display outside the lock - the tool fault this closes.)
set -u
build="${1:?usage: regen-api-docs.sh <build dir> [--data-root <dir>]}"
shift
root=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --data-root) root="${2:?--data-root needs a directory}"; shift 2 ;;
        *) echo "regen-api-docs.sh: unknown argument $1" >&2; exit 64 ;;
    esac
done
here="$(cd "$(dirname "$0")" && pwd)"
tree="$(cd "$here/.." && pwd)"
bin="$(cd "$build" && pwd)/bin"
[ -x "$bin/Jahshaka" ] || { echo "regen-api-docs.sh: no Jahshaka in $bin" >&2; exit 66; }
[ -n "${DISPLAY:-}" ] || { echo "regen-api-docs.sh: set DISPLAY to your own Xvfb (never :0)" >&2; exit 64; }
[ "$DISPLAY" = ":0" ] && { echo "regen-api-docs.sh: DISPLAY=:0 is the owner's session - use your own Xvfb" >&2; exit 64; }
own=0
if [ -z "$root" ]; then
    root="$(mktemp -d "${TMPDIR:-/tmp}/regen-api-docs.XXXXXX")"
    own=1
fi
mkdir -p "$root"
out="$root/SCRIPTING.md"
rm -f "$out"
# (run from bin: media resolves from the binary's directory)
( cd "$bin" && "$here/gpu-exclusive.sh" --wait-max 400 --run-timeout 120 --label regen-api-docs \
      nice -n 19 ./Jahshaka --data-root "$root" --dump-api-docs "$out" ) > "$root/regen-api-docs.log" 2>&1
rc=$?
if [ $rc -ne 0 ] || [ ! -s "$out" ]; then
    echo "regen-api-docs.sh: the dump failed (exit $rc) - $root/regen-api-docs.log:" >&2
    tail -5 "$root/regen-api-docs.log" >&2
    exit 1
fi
if cmp -s "$out" "$tree/docs/SCRIPTING.md"; then
    echo "regen-api-docs.sh: docs/SCRIPTING.md is current"
else
    cp "$out" "$tree/docs/SCRIPTING.md"
    echo "regen-api-docs.sh: docs/SCRIPTING.md regenerated ($(git -C "$tree" diff --numstat docs/SCRIPTING.md | awk '{print "+" $1 " -" $2}'))"
fi
[ "$own" = 1 ] && rm -rf "$root"
exit 0
