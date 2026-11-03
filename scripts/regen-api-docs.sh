#!/usr/bin/env bash
# regen-api-docs.sh <build dir> [--data-root <dir>] — THE ONE WAY TO REGENERATE docs/SCRIPTING.md (the generated verb
# reference; never hand-edited). `Jahshaka --dump-api-docs` boots the whole app (src/app/main.cpp: the scripting registry
# is filled by the MainWindow's module boot) on the NULL render system and PROVES it touched no GPU - the offscreen platform
# before the boot, a headless engine and no Vulkan driver mapped after it, else exit 3 (TEST-STREAMLINE-1, the lead's ruling
# 2026-11-03; it ran under the card's lock until then): so no lock and no display - a data root (a throwaway one in $TMPDIR
# unless --data-root names one: never inside the tree), and the tree's docs/SCRIPTING.md written.
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
own=0
if [ -z "$root" ]; then
    root="$(mktemp -d "${TMPDIR:-/tmp}/regen-api-docs.XXXXXX")"
    own=1
fi
mkdir -p "$root"
out="$root/SCRIPTING.md"
rm -f "$out"
# (run from bin: media resolves from the binary's directory)
( cd "$bin" && env -u DISPLAY -u WAYLAND_DISPLAY timeout -k 10 120 nice -n 10 ./Jahshaka --data-root "$root" \
      --dump-api-docs "$out" ) > "$root/regen-api-docs.log" 2>&1
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
