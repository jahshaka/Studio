#!/bin/sh
# fresh_home.sh — A SUITE'S HOME, FRESH FOR EVERY RUN (lane D6B-GATE-SHAPE; the suites audit
# SPECS/audits/GATE_SUITES_AUDIT_2026-09-26.md §9 H1) — AND GONE AFTER IT (TESTING-SPEED-A item 7). The ONE
# implementation of the wipe: it replaced 27 hand-copied `sh -c "rm -rf …"` fixtures and tests/samples/freshhome.cmake,
# and it is every pool's and one-script row's default.
#
#   fresh_home.sh [--warm] [--keep-home] [--reset <dir>]... [--rm <file>]... [--mkdir <dir>]... <home>
#                 [-- <command> [args...]]
#   fresh_home.sh --after <exit code> <home>     the wipe after alone, for a FIXTURE home: its one CONSUMER row's
#                 admission (scripts/vram_tokens.py, JAH_WIPE_AFTER=<home>) calls it with the row's exit code
#
# WHY. A suite home that survives its run ACCUMULATES: measured on the main tree,
# e2e-home-full_surface held 61 projects and a 220 MB store, e2e-home-export_web 1.1 GB, and
# open.responsive's create arm went red by RUN COUNT, not by tree (DOCS/traps/GATE_AND_RIG.md).
# A suite that asserts a list length or a first launch asserts it against a library the
# previous run left behind. AND A HOME THAT SURVIVES A GREEN RUN IS 5 GB PER TREE (2026-10-12: bloom-1's build held 144
# homes, 5.1 GB, 111 private shader caches; 11,949 images under build-linux/tests).
#
#   <home>          removed and recreated with <home>/run — the library database, the asset
#                   store, the projects, the settings: everything a run writes.
#   --warm          ...and the boot's shader cache comes WARM from THE TREE'S ONE SHARED CACHE
#                   (<build>/tests/.shadercache — the build tree's tests/ dir above <home>): hard-linked into
#                   <home>/.local/share/Jahshaka/shadercache (the cache writes by rename, never in place, so a link is
#                   never written through; its writer lock file is never linked). A green run whose cache grew
#                   PROMOTES it as the shared one (a rename). A cold cache costs ~4 s a boot and is its own
#                   measurement — a suite that asserts ON the cold cache (the VR warm-up, the shadow cache probes) runs
#                   without --warm.
#   --keep-home     (with a command) keep the home after the run — the old behaviour; a fixture row's own use.
#   --reset <dir>   also removed and recreated (a suite's export target beside its home).
#   --rm <file>     also removed.
#   --mkdir <dir>   also created.
#   -- <command>    run, in the directory the caller started in — re-entered by path, because the wipe may have
#                   deleted it (a ctest WORKING_DIRECTORY inside <home>). THE WIPE AFTER (item 7, the default): when the
#                   command exits 0 the home is deleted (its empty run/ re-created: a row's working directory); on any other exit it MOVES to <build>/tests/.evidence/<home's
#                   name>/, replacing that home's previous evidence (one red kept per row). Without a command it is a
#                   fixture row (a FIXTURES_SETUP home is wiped at its next setup).
set -eu
warm=0; keep=0; after=""
resets=""; rms=""; mkdirs=""
while [ $# -gt 0 ]; do
    case "$1" in
        --warm)  warm=1; shift ;;
        --keep-home) keep=1; shift ;;
        --after) after="$2"; warm=1; shift 2 ;;
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
[ $# -ge 1 ] || { echo "fresh_home.sh: usage: [--warm] [--keep-home] [--reset d] [--rm f] [--mkdir d] <home> [-- cmd...]" >&2; exit 2; }
home="$1"; shift
# A wipe of a shallow path is a bug, never a request (the suites' own env sets HOME=<home>,
# so $HOME is no guide): an absolute path at least four components deep.
case "$home" in
    /*/*/*/*) ;;
    *) echo "fresh_home.sh: refusing to wipe '$home' (not an absolute path 4+ deep)" >&2; exit 2 ;;
esac
start="$(/bin/pwd -P 2>/dev/null || echo /)"

# THE TREE'S tests/ DIR: the nearest ancestor of <home> named `tests` whose parent is a configured build (CMakeCache.txt)
# — the shared cache and the evidence live there; none (a home outside a build tree) = no sharing, no evidence move
tests_dir=""
d="$(dirname "$home")"
while [ "$d" != "/" ] && [ -n "$d" ]; do
    if [ "$(basename "$d")" = tests ] && [ -f "$(dirname "$d")/CMakeCache.txt" ]; then tests_dir="$d"; break; fi
    d="$(dirname "$d")"
done
cache_rel=.local/share/Jahshaka/shadercache
shared=""
[ -n "$tests_dir" ] && shared="$tests_dir/.shadercache"

# THE WIPE AFTER (item 7): green -> a grown cache promoted, the home deleted; red -> the home moved to the evidence
wipe_after() {
rc="$1"
if [ "$rc" = 0 ]; then
    # a GREEN run: its cache grew past the shared one -> it becomes the shared one (a rename on one filesystem)
    if [ "$warm" = 1 ] && [ -d "$home/$cache_rel" ]; then
        mine=$(du -sk "$home/$cache_rel" 2>/dev/null | cut -f1); theirs=0
        [ -d "$shared" ] && theirs=$(du -sk "$shared" 2>/dev/null | cut -f1)
        if [ "${mine:-0}" -gt "${theirs:-0}" ]; then
            rm -f "$home/$cache_rel/cache.lock"
            # THE PROMOTION UNDER THE TREE'S CACHE LOCK, EXCLUSIVE (fix round item 7): no --warm links from a cache half
            # swapped, two promotions never interleave, and the swap's leftovers (a .shadercache-next.* of a run that died
            # mid-swap) go under the same lock; a lock not had in 60 s = no promotion (the run stays green)
            (
                flock -w 60 9 || exit 0
                for o in "$tests_dir"/.shadercache-next.*; do [ -e "$o" ] && rm -rf "$o"; done
                theirs=0; [ -d "$shared" ] && theirs=$(du -sk "$shared" 2>/dev/null | cut -f1)
                [ "${mine:-0}" -gt "${theirs:-0}" ] || exit 0
                nxt="$tests_dir/.shadercache-next.$$"
                mv "$home/$cache_rel" "$nxt" \
                    && { [ ! -d "$shared" ] || mv "$shared" "$nxt.old"; } \
                    && mv "$nxt" "$shared" && rm -rf "$nxt.old"
                exit 0
            ) 9>"$tests_dir/.shadercache.lock" || true
        fi
    fi
    rm -rf "$home"
else
    # a RED run: the home is the evidence — moved, replacing that home's previous evidence (one red kept per row)
    ev="$tests_dir/.evidence/$(basename "$home")"
    mkdir -p "$tests_dir/.evidence"
    rm -rf "$ev"
    mv "$home" "$ev" 2>/dev/null || rm -rf "$home"
fi
# A ROW'S WORKING DIRECTORY SURVIVES THE WIPE (round 5): a pool's ctest WORKING_DIRECTORY is <home>/run (and a row may
# start inside its home): only a configure creates them, so a wipe that took them made the row's NEXT run "Not Run:
# Failed to change working directory". The empty <home>/run — and the caller's start dir when it lies in the home — are
# re-created: no bytes left, the cwd kept.
mkdir -p "$home/run"
case "$start" in "$home"/*|"$home") mkdir -p "$start" ;; esac
}

if [ -n "$after" ]; then
    # --after: no wipe BEFORE — the consumer has run; only the after step (a home outside a build tree is left alone)
    [ -n "$tests_dir" ] && [ -d "$home" ] && wipe_after "$after"
    exit 0
fi


if [ "$warm" = 1 ] && [ -d "$home" ]; then
    keepd="$(mktemp -d "$(dirname "$home")/.fresh-home-keep.XXXXXX")"   # same filesystem: a rename
    for c in .nv .cache; do
        if [ -e "$home/$c" ]; then
            mkdir -p "$keepd/$(dirname "$c")"
            mv "$home/$c" "$keepd/$c"
        fi
    done
    rm -rf "$home"
    mkdir -p "$home"
    for c in .nv .cache; do
        if [ -e "$keepd/$c" ]; then
            mkdir -p "$home/$(dirname "$c")"
            mv "$keepd/$c" "$home/$c"
        fi
    done
    rm -rf "$keepd"
else
    rm -rf "$home"
fi
mkdir -p "$home/run"
if [ "$warm" = 1 ] && [ -n "$shared" ] && [ -d "$shared" ]; then
    # THE SHARED CACHE, LINKED (never the writer's lock: a linked lock would be one flock for every home)
    mkdir -p "$home/$cache_rel"
    # THE LINK PASS UNDER THE CACHE LOCK, SHARED (fix round item 7): many --warm homes link at once, a promotion waits for
    # them and they for it — never a half-swapped cache; a lock not had in 60 s or a file gone = a colder cache, never a
    # failed run
    (
        flock -s -w 60 9 || exit 0
        [ -d "$shared" ] || exit 0
        ( cd "$shared" && find . -type f ! -name cache.lock -print ) | while IFS= read -r f; do
            mkdir -p "$home/$cache_rel/$(dirname "$f")"
            ln "$shared/$f" "$home/$cache_rel/$f" 2>/dev/null || cp -p "$shared/$f" "$home/$cache_rel/$f" 2>/dev/null || true
        done
        exit 0
    ) 9>"$tests_dir/.shadercache.lock" || true
fi

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
if [ "$keep" = 1 ] || [ -z "$tests_dir" ]; then
    exec "$@"
fi
set +e
"$@"
rc=$?
set -e
cd / 2>/dev/null || true
wipe_after "$rc"
exit "$rc"
