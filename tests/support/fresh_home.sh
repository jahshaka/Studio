#!/bin/sh
# fresh_home.sh — A SUITE'S HOME, FRESH FOR EVERY RUN (lane D6B-GATE-SHAPE; the suites audit
# SPECS/audits/GATE_SUITES_AUDIT_2026-09-26.md §9 H1) — AND GONE AFTER IT (TESTING-SPEED-A item 7). The ONE
# implementation of the wipe: it replaced 27 hand-copied `sh -c "rm -rf …"` fixtures and tests/samples/freshhome.cmake,
# and it is every pool's and one-script row's default.
#
#   fresh_home.sh [--warm [--cache <key>]] [--keep-home] [--reset <dir>]... [--rm <file>]... [--mkdir <dir>]... <home>
#                 [-- <command> [args...]]
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
#                   never written through; its writer lock file is never linked), and the shared cache's GENERATION
#                   (its manifest's hash) stamped beside it (<home>/.shadercache-seed). A GREEN run's cache REPLACES the
#                   shared one (a rename, under the shared cache's lock) when (1) a layer both hold carries ANOTHER KEY
#                   (the shared one is stale: an engine rebuild, a media edit), or (2) the keys are equal and the shared
#                   cache is still the generation this home was seeded from (the home's cache is that union plus what
#                   this run compiled); (3) equal keys and ANOTHER home promoted since the seed: SKIPPED and logged (its
#                   variants stay; this home's new ones compile once on a later run - a cost, never a wrong cache).
#                   SIZE DECIDES NOTHING (urgent #36: a replace-by-size kept a stale 70 MB union for a day - after a key
#                   change no single fixture's fresh cache outgrew it, and every engine row compiled cold). A cold cache
#                   costs ~4 s a boot and is its own measurement — a suite that asserts ON the cold cache (the VR
#                   warm-up, the shadow cache probes) runs without --warm.
#   --cache <key>   (with --warm) the tree's shared cache NAMED <key>: <build>/tests/.shadercache-<key>, its own lock —
#                   a set of rows that boots a different shader set keeps its own union (ENGINE-UPGRADE-1: the engine
#                   fixtures' `engine` cache beside the app rows' 268 MB one, whose replay cost a fixture 0.4-0.8 s a boot
#                   and which sits at the engine's 256 MB cap). [a-z0-9_] only.
#   --keep-home     (with a command) keep the home after the run — the old behaviour; a fixture row's own use.
#   --setup         a FIXTURES_SETUP row (jah_fresh_home_fixture): wipe before, run THEN, exit with its code — no wipe
#                   after (the home is wiped at its next setup), no cache promotion.
#   --reset <dir>   also removed and recreated (a suite's export target beside its home).
#   --rm <file>     also removed.
#   --mkdir <dir>   also created.
#   JAH_KEEP_HOME=1 (the environment; TOOLING-6 item 3) a DIAGNOSIS run: a green run's home is KEPT where it is (its
#                   grown cache still promoted), its path printed — a passing row's output and files are read without
#                   repeating the run by hand; the next run of the row wipes it as always. Never set by a row or a verb.
#   -- <command>    run, in the directory the caller started in — re-entered by path, because the wipe may have
#                   deleted it (a ctest WORKING_DIRECTORY inside <home>). THE WIPE AFTER (item 7, the default): when the
#                   command exits 0 the home is deleted (its empty run/ re-created: a row's working directory); on any other exit it MOVES to <build>/tests/.evidence/<home's
#                   name>/, replacing that home's previous evidence (one red kept per row). Without a command it is a
#                   fixture row (a FIXTURES_SETUP home is wiped at its next setup).
set -eu
warm=0; keep=0; setup=0; key=""
resets=""; rms=""; mkdirs=""
while [ $# -gt 0 ]; do
    case "$1" in
        --warm)  warm=1; shift ;;
        --keep-home) keep=1; shift ;;
        --cache) key="${2:-}"; shift 2
                 case "$key" in ""|*[!a-z0-9_]*) echo "fresh_home.sh: --cache needs a key of [a-z0-9_]" >&2; exit 2 ;; esac ;;
        --setup) setup=1; shift ;;
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
[ -n "$tests_dir" ] && shared="$tests_dir/.shadercache${key:+-$key}"
# A CACHE'S GENERATION: the hash of its manifest (every save rewrites it: `saved <ms>`) and of the startup gate's
# global-pass record beside it (warm-pass.key, src/app/shaderbuildgate.cpp, URGENT #104: a run whose pass completed
# over a warm cache saves no layer but writes the record - that run must promote it, or every row re-runs the pass),
# `none` without a manifest
generation() {
    if [ -f "$1/cache-manifest.txt" ]; then
        cat "$1/cache-manifest.txt" "$1/warm-pass.key" 2>/dev/null | sha256sum | cut -c1-64
    else
        echo none
    fi
}
# A LAYER BOTH CACHES HOLD UNDER ANOTHER KEY (exit 0): the manifests' file lines carry each file's layer key (the fifth
# field); the layers are pipeline.cache, microcode.cache and the hlms.N.bin files (one key)
keys_differ() {
    awk 'FNR == 1 { f++ }
         $1 == "file" { l = $2; sub(/^hlms\..*/, "hlms", l); layer[f, l] = 1; k[f, l, $5] = 1 }
         END { for (x in k) { split(x, p, SUBSEP); o = 3 - p[1]
                              if (layer[o, p[2]] && !((o, p[2], p[3]) in k)) d = 1 }
               exit d ? 0 : 1 }' "$1/cache-manifest.txt" "$2/cache-manifest.txt"
}

# THE WIPE AFTER (item 7): green -> a grown cache promoted, the home deleted; red -> the home moved to the evidence
wipe_after() {
rc="$1"
if [ "$rc" = 0 ]; then
    # a GREEN run: its cache replaces the shared one by the three rules (the --warm help above; size decides nothing)
    if [ "$warm" = 1 ] && [ -n "$shared" ] && [ -f "$home/$cache_rel/cache-manifest.txt" ]; then
        rm -f "$home/$cache_rel/cache.lock"
        seed=$(cat "$home/.shadercache-seed" 2>/dev/null || echo none)
        # THE PROMOTION UNDER THE TREE'S CACHE LOCK, EXCLUSIVE (fix round item 7): no --warm links from a cache half
        # swapped, two promotions never interleave, and the swap's leftovers (a .shadercache-next.* of a run that died
        # mid-swap) go under the same lock; a lock not had in 60 s = no promotion (the run stays green)
        (
            flock -w 60 9 || { echo "fresh_home.sh: the shared cache's lock not had in 60 s - no promotion" >&2; exit 0; }
            for o in "$shared"-next.*; do [ -e "$o" ] && rm -rf "$o"; done
            mine=$(generation "$home/$cache_rel"); theirs=$(generation "$shared")
            [ "$mine" = "$theirs" ] && exit 0   # the run saved nothing: the home holds the shared cache as linked
            why=""
            if [ "$theirs" = none ]; then
                why="there was no shared cache"
            elif keys_differ "$home/$cache_rel" "$shared"; then
                why="a layer's key differs (the shared cache was stale)"
            elif [ "$theirs" != "$seed" ]; then
                echo "fresh_home.sh: the shared cache NOT replaced - another home promoted since this one was seeded" \
                     "(its variants stay; this run's new ones compile once on a later run)" >&2
                exit 0
            fi
            nxt="$shared-next.$$"
            mv "$home/$cache_rel" "$nxt" \
                && { [ ! -d "$shared" ] || mv "$shared" "$nxt.old"; } \
                && mv "$nxt" "$shared" && rm -rf "$nxt.old"
            [ -z "$why" ] || echo "fresh_home.sh: the shared cache REPLACED by this home's - $why" >&2
            exit 0
        ) 9>"$shared.lock" || true
    fi
    if [ "${JAH_KEEP_HOME:-}" = 1 ]; then
        echo "fresh_home.sh: JAH_KEEP_HOME=1 — the green run's home is kept: $home" >&2
    else
        rm -rf "$home"
    fi
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
        generation "$shared" > "$home/.shadercache-seed"   # the generation linked (the promotion's rule 2)
        ( cd "$shared" && find . -type f ! -name cache.lock -print ) | while IFS= read -r f; do
            mkdir -p "$home/$cache_rel/$(dirname "$f")"
            ln "$shared/$f" "$home/$cache_rel/$f" 2>/dev/null || cp -p "$shared/$f" "$home/$cache_rel/$f" 2>/dev/null || true
        done
        exit 0
    ) 9>"$shared.lock" || true
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
# A FIXTURE'S SETUP (--setup, jah_fresh_home_fixture's row; round 14 addendum 2): the wipe BEFORE, then its THEN, then
# its exit code — never the wipe after (the home is wiped at its next setup)
# and no cache promotion (the setup compiled nothing): a THEN that writes a file the consumer reads keeps it
if [ "$keep" = 1 ] || [ "$setup" = 1 ] || [ -z "$tests_dir" ]; then
    exec "$@"
fi
set +e
"$@"
rc=$?
set -e
cd / 2>/dev/null || true
wipe_after "$rc"
exit "$rc"
