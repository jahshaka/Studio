#!/bin/sh
# seed_home.sh — THE SHIPPED MESHES SEEDED ONCE PER BAKE KEY, NOT ONCE PER WARM HOME (TEST-STREAMLINE-1 stage 3a item 5).
#
# WHY. Every fresh app home's first boot imports and bakes the 17 shipped meshes (src/services/primitiveseed.cpp seedAll,
# its "seed: 17 shipped meshes imported and baked on a worker in N ms" line: 0.93-0.99 s measured) before the row's script
# runs - in every --warm row, on every run, for bytes that are a function of the BAKE KEY alone. So the warm-up makes them
# ONCE, as a template library, and fresh_home.sh --warm COPIES it into each home it makes:
#
#   seed_home.sh make <Jahshaka> <tests dir>    the warm-up's step (scripts/warm-app-cache.sh): when <tests>/.seedhome is
#                    missing or stamped with another bake key than <tests>/bake-key.txt (the configure's - irisgl's
#                    JAHSHAKA_MESH_BAKE_PRODUCER_ID: kFormatVersion's file and every bake-producing source by content,
#                    DOCS/traps/ENGINE.md "THE BAKE KEY"), ONE boot of the NULL headless app on an empty data root (no
#                    display, no card, ~3 s) - its library (JahLibrary.db + AssetStore) IS the template, its seed line
#                    checked ("seed: 17 ..."), stamped with the key and swapped in whole under <tests>/.seedhome.lock.
#                    At the key already: nothing, said. Exit 1 when the boot or its seed failed (no template, said).
#   seed_home.sh copy <tests dir> <data root>   fresh_home.sh --warm's step: the template COPIED (never linked - a home
#                    writes its library in place) into a data root that holds no library yet, ONLY when its stamp is the
#                    build's bake key; otherwise nothing is copied and the boot seeds as it always did (said). A stale
#                    template is never served: a moved key means another bake.
#   seed_home.sh key <tests dir>                the build's bake key (bake-key.txt), or nothing.
#
# A HOME MADE FROM THE TEMPLATE boots with its 17 rows, their sources and their bakes already in the store: seedAll plans
# every row SEEDED (its bake re-checked by MeshBakeStore::isFresh, which rebuilds a stale one - so even a template that
# slipped past its stamp yields the right bytes, never a stale bake) and creates none, no line.
set -u
cmd=${1:-}
lib_files="JahLibrary.db AssetStore"
case "$cmd" in
key)
    t=${2:?seed_home.sh key <tests dir>}
    cat "$t/bake-key.txt" 2>/dev/null | head -1
    ;;
make)
    app=${2:?seed_home.sh make <Jahshaka> <tests dir>}; t=${3:?seed_home.sh make <Jahshaka> <tests dir>}
    want=$(cat "$t/bake-key.txt" 2>/dev/null | head -1)
    [ -n "$want" ] || { echo "seed_home: no bake key at $t/bake-key.txt (configure the tree) - no template made"; exit 1; }
    (
        flock -w 120 9 || { echo "seed_home: the template's lock was not had in 120 s - no template made"; exit 1; }
        have=$(cat "$t/.seedhome/bake-key" 2>/dev/null | head -1)
        if [ "$have" = "$want" ] && [ -f "$t/.seedhome/data/JahLibrary.db" ]; then
            echo "seed_home: the template is at the bake key ${want%%|*} (nothing to make)"; exit 0
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
        printf '%s\n%s shipped meshes\n' "$want" "$n" > "$tmp/out/bake-key"
        rm -rf "$t/.seedhome.old"
        [ -d "$t/.seedhome" ] && mv "$t/.seedhome" "$t/.seedhome.old"
        mv "$tmp/out" "$t/.seedhome"
        rm -rf "$t/.seedhome.old" "$tmp"
        echo "seed_home: the template made at the bake key ${want%%|*} ($n shipped meshes, one NULL headless boot)"
        exit 0
    ) 9>"$t/.seedhome.lock"
    ;;
copy)
    t=${2:?seed_home.sh copy <tests dir> <data root>}; root=${3:?seed_home.sh copy <tests dir> <data root>}
    want=$(cat "$t/bake-key.txt" 2>/dev/null | head -1)
    [ -d "$t/.seedhome" ] || exit 0                       # no template (a tree no warm-up made one for): the boot seeds
    (
        flock -s -w 60 9 || exit 0                         # a template being swapped: this home seeds itself
        have=$(cat "$t/.seedhome/bake-key" 2>/dev/null | head -1)
        if [ -z "$want" ] || [ "$have" != "$want" ]; then
            echo "seed_home: the template is at another bake key than the build's - not copied (the boot seeds)"; exit 0
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
