#!/bin/sh
# fresh_home.sh — A SUITE'S HOME, FRESH FOR EVERY RUN (lane D6B-GATE-SHAPE; the suites audit
# SPECS/audits/GATE_SUITES_AUDIT_2026-09-26.md §9 H1). The ONE implementation of the wipe:
# it replaced 27 hand-copied `sh -c "rm -rf …"` fixtures and tests/samples/freshhome.cmake,
# and it is every pool's and one-script row's default.
#
#   fresh_home.sh [--warm] [--reset <dir>]... [--rm <file>]... [--mkdir <dir>]... <home>
#                 [-- <command> [args...]]
#
# WHY. A suite home that survives its run ACCUMULATES: measured on the main tree,
# e2e-home-full_surface held 61 projects and a 220 MB store, e2e-home-export_web 1.1 GB, and
# open.responsive's create arm went red by RUN COUNT, not by tree (DOCS/traps/GATE_AND_RIG.md).
# A suite that asserts a list length or a first launch asserts it against a library the
# previous run left behind.
#
#   <home>          removed and recreated with <home>/run — the library database, the asset
#                   store, the projects, the settings: everything a run writes.
#   --warm          ...EXCEPT the caches a boot compiles into (<home>/.local/share/Jahshaka/
#                   shadercache, <home>/.nv, <home>/.cache): a cold cache costs ~4 s a boot and
#                   is its own measurement — a suite that asserts ON the cold cache (the VR
#                   warm-up, the shadow cache probes) runs without --warm.
#   --reset <dir>   also removed and recreated (a suite's export target beside its home).
#   --rm <file>     also removed.
#   --mkdir <dir>   also created.
#   -- <command>    run in place of this shell (exec), in the directory the caller started
#                   in — re-entered by path, because the wipe may have deleted it (a ctest
#                   WORKING_DIRECTORY inside <home>). Without a command it is a fixture row.
set -eu
warm=0
resets=""; rms=""; mkdirs=""
while [ $# -gt 0 ]; do
    case "$1" in
        --warm)  warm=1; shift ;;
        --reset) resets="$resets
$2"; shift 2 ;;
        --rm)    rms="$rms
$2"; shift 2 ;;
        --mkdir) mkdirs="$mkdirs
$2"; shift 2 ;;
        --)      echo "fresh_home.sh: no <home> before --" >&2; exit 2 ;;
        *)       break ;;
    esac
done
[ $# -ge 1 ] || { echo "fresh_home.sh: usage: [--warm] [--reset d] [--rm f] [--mkdir d] <home> [-- cmd...]" >&2; exit 2; }
home="$1"; shift
# A wipe of a shallow path is a bug, never a request (the suites' own env sets HOME=<home>,
# so $HOME is no guide): an absolute path at least four components deep.
case "$home" in
    /*/*/*/*) ;;
    *) echo "fresh_home.sh: refusing to wipe '$home' (not an absolute path 4+ deep)" >&2; exit 2 ;;
esac
start="$(/bin/pwd -P 2>/dev/null || echo /)"

if [ "$warm" = 1 ] && [ -d "$home" ]; then
    keep="$(mktemp -d "$(dirname "$home")/.fresh-home-keep.XXXXXX")"   # same filesystem: a rename
    for c in .local/share/Jahshaka/shadercache .nv .cache; do
        if [ -e "$home/$c" ]; then
            mkdir -p "$keep/$(dirname "$c")"
            mv "$home/$c" "$keep/$c"
        fi
    done
    rm -rf "$home"
    mkdir -p "$home"
    for c in .local/share/Jahshaka/shadercache .nv .cache; do
        if [ -e "$keep/$c" ]; then
            mkdir -p "$home/$(dirname "$c")"
            mv "$keep/$c" "$home/$c"
        fi
    done
    rm -rf "$keep"
else
    rm -rf "$home"
fi
mkdir -p "$home/run"

old_ifs="$IFS"; IFS='
'
for d in $resets; do [ -n "$d" ] && rm -rf "$d" && mkdir -p "$d"; done
for f in $rms;    do [ -n "$f" ] && rm -f "$f"; done
for d in $mkdirs; do [ -n "$d" ] && mkdir -p "$d"; done
IFS="$old_ifs"

[ $# -gt 0 ] || exit 0
[ "$1" = "--" ] || { echo "fresh_home.sh: expected -- before the command, got '$1'" >&2; exit 2; }
shift
[ $# -gt 0 ] || { echo "fresh_home.sh: -- with no command" >&2; exit 2; }
mkdir -p "$start" 2>/dev/null || true
cd "$start"
exec "$@"
