#!/bin/sh
# seed_home.sh — THE SHIPPED MESHES SEEDED ONCE PER BAKE KEY, NOT ONCE PER WARM HOME (TEST-STREAMLINE-1 stage 3a item 5).
#
# WHY. Every fresh app home's first boot imports and bakes the 17 shipped meshes (src/services/primitiveseed.cpp seedAll,
# its "seed: 17 shipped meshes imported and baked on a worker in N ms" line: 0.93-0.99 s measured) before the row's script
# runs - in every --warm row, on every run, for bytes that are a function of the BAKE KEY alone. So the warm-up makes them
# ONCE, as a template library, and fresh_home.sh --warm COPIES it into each home it makes:
#
#   seed_home.sh make <Jahshaka> <tests dir>    the warm-up's step (scripts/warm-app-cache.sh): when <tests>/.seedhome is
#                    missing or stamped with another SEED KEY than the build's (`key` below), ONE boot of the NULL headless
#                    app on an empty data root (no
#                    display, no card, ~3 s) - its library (JahLibrary.db + AssetStore) IS the template, its seed line
#                    checked ("seed: 17 ..."), stamped with the key and swapped in whole under <tests>/.seedhome.lock.
#                    At the key already: nothing, said. Exit 1 when the boot or its seed failed (no template, said).
#   seed_home.sh copy <tests dir> <data root>   fresh_home.sh --warm's step: the template COPIED (never linked - a home
#                    writes its library in place) into a data root that holds no library yet, ONLY when its stamp is the
#                    build's seed key; otherwise nothing is copied and the boot seeds as it always did (said). A stale
#                    template is never served: a moved key means another seed.
#   seed_home.sh key <tests dir>                THE SEED KEY: what made the library - THE BINARY THAT SEEDS IT, by the
#                    build's own content identity (<build>/BUILT_FROM `content=`: every tracked and untracked file of Studio,
#                    irisgl and the fork by git's blob id, cmake/built_from.py - so the importer that mints the guids
#                    (assetimporters.cpp), the guid form (irisgl/core/guid.cpp), the store, every source of the app is in it,
#                    no hand list to go stale: REPRICE-1's final read), and what that content leaves out - the vendored
#                    assimp's pin - plus <tests>/seed-key-inputs.txt's lines (tests/CMakeLists.txt: the bake producer, the
#                    shipped meshes and the seed code by content; inside `content` now, kept by the list). NOTHING - no key,
#                    no template made or copied - when the list is missing, or the stamp is missing, unsettled (content ?),
#                    STALE (a binary not built from that content), or claims neither `all` nor the app (the list's `app`).
#
# A HOME MADE FROM THE TEMPLATE boots with its 17 rows, their sources and their bakes already in the store: seedAll plans
# every row SEEDED and creates none, no line - and it checks each row's STORED SOURCE against the shipped bytes (a row
# holding another mesh is re-seeded, said by name) and its bake by MeshBakeStore::isFresh, so even a template that slipped
# past its stamp yields this build's meshes, never the old ones.
set -u
cmd=${1:-}
lib_files="JahLibrary.db AssetStore"
seed_key() {   # <tests dir>: the seed key, or nothing
    inputs="$1/seed-key-inputs.txt"
    [ -f "$inputs" ] || return 0
    bf="$1/../BUILT_FROM"
    built=$(sed -n 's/^content=\([0-9a-f]\{16\}\)$/\1/p' "$bf" 2>/dev/null | head -1)
    { [ -n "$built" ] && grep -qx 'stale=0' "$bf" && grep -qx 'stale_fork=0' "$bf"; } || return 0
    # THE STAMP MUST CLAIM THE APP (REPRICE-1's confirming read): a by-reach stamp names the whole tree's content while it
    # claims only the targets it built - the content is the seeding binary's only when the stamp claims `all` or the app
    app=$(sed -n 's/^app //p' "$inputs" | head -1); claims=",$(sed -n 's/^targets=//p' "$bf" | head -1),"
    case "$claims" in *,all,*|*",$app,"*) [ -n "$app" ] || return 0 ;; *) return 0 ;; esac
    root=$(sed -n 's/^root //p' "$inputs" | head -1)
    {
        echo "built $built"
        while read -r kind val; do
            case "$kind" in
                producer) echo "producer $val" ;;
                git)      echo "git $val $(git -C "$root/$val" rev-parse HEAD 2>/dev/null || echo none)" ;;
                file)     if [ -f "$root/$val" ]; then echo "file $val $(sha256sum < "$root/$val" | cut -c1-64)"
                          else echo "file $val missing"; fi ;;
            esac
        done < "$inputs"
    } | sha256sum | cut -c1-64
}
case "$cmd" in
key)
    seed_key "${2:?seed_home.sh key <tests dir>}"
    ;;
make)
    app=${2:?seed_home.sh make <Jahshaka> <tests dir>}; t=${3:?seed_home.sh make <Jahshaka> <tests dir>}
    want=$(seed_key "$t")
    [ -n "$want" ] || { echo "seed_home: no seed key (no $t/seed-key-inputs.txt - configure the tree - or the build's BUILT_FROM names no settled content: build it) - no template made"; exit 1; }
    (
        flock -w 120 9 || { echo "seed_home: the template's lock was not had in 120 s - no template made"; exit 1; }
        have=$(cat "$t/.seedhome/seed-key" 2>/dev/null | head -1)
        if [ "$have" = "$want" ] && [ -f "$t/.seedhome/data/JahLibrary.db" ]; then
            echo "seed_home: the template is at the seed key ${want%%|*} (nothing to make)"; exit 0
        fi
        tmp="$t/.seedhome.make.$$"
        rm -rf "$tmp"; mkdir -p "$tmp/run"
        printf '0\n' > "$tmp/zero.js"
        # THE NULL HEADLESS BOOT: no display (a CPU boot by construction), its own HOME and data root, nothing of the tree's
        ( cd "$tmp/run" && env -u DISPLAY -u WAYLAND_DISPLAY HOME="$tmp" JAHSHAKA_DATA_ROOT="$tmp/data" JAHSHAKA_VR=0 \
              "$app" --headless --script "$tmp/zero.js" ) > "$tmp/boot.log" 2>&1
        rc=$?
        n=$(sed -n 's/.*seed: \([0-9][0-9]*\) shipped meshes imported and baked.*/\1/p' "$tmp/boot.log" | head -1)
        if [ "$rc" != 0 ] || [ -z "$n" ] || [ ! -f "$tmp/data/JahLibrary.db" ]; then
            echo "seed_home: the template's boot failed (exit $rc, seed line '${n:-none}') - no template made; its log:"
            tail -5 "$tmp/boot.log" | sed 's/^/  | /'
            rm -rf "$tmp"; exit 1
        fi
        mkdir -p "$tmp/out/data"
        for f in $lib_files; do cp -a "$tmp/data/$f" "$tmp/out/data/"; done
        printf '%s\n%s shipped meshes\n' "$want" "$n" > "$tmp/out/seed-key"
        rm -rf "$t/.seedhome.old"
        [ -d "$t/.seedhome" ] && mv "$t/.seedhome" "$t/.seedhome.old"
        mv "$tmp/out" "$t/.seedhome"
        rm -rf "$t/.seedhome.old" "$tmp"
        echo "seed_home: the template made at the seed key $want ($n shipped meshes, one NULL headless boot)"
        exit 0
    ) 9>"$t/.seedhome.lock"
    ;;
copy)
    t=${2:?seed_home.sh copy <tests dir> <data root>}; root=${3:?seed_home.sh copy <tests dir> <data root>}
    [ -d "$t/.seedhome" ] || exit 0                       # (before the key: a tree with no template pays nothing)
    want=$(seed_key "$t")                       # no template (a tree no warm-up made one for): the boot seeds
    (
        flock -s -w 60 9 || exit 0                         # a template being swapped: this home seeds itself
        have=$(cat "$t/.seedhome/seed-key" 2>/dev/null | head -1)
        if [ -z "$want" ] || [ "$have" != "$want" ]; then
            echo "seed_home: the template is at another seed key than the build's - not copied (the boot seeds)"; exit 0
        fi
        [ -e "$root/JahLibrary.db" ] && exit 0             # a library already there is the row's own
        mkdir -p "$root"
        for f in $lib_files; do cp -a "$t/.seedhome/data/$f" "$root/"; done
        exit 0
    ) 9>"$t/.seedhome.lock"
    ;;
*)
    echo "usage: seed_home.sh make <Jahshaka> <tests dir> | copy <tests dir> <data root> | key <tests dir>" >&2; exit 2 ;;
esac
