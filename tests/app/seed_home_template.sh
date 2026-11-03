#!/bin/sh
# app.seed_home_template — THE 17 SHIPPED MESHES SEEDED ONCE PER BAKE KEY, NOT ONCE PER WARM HOME (TEST-STREAMLINE-1 3a
# item 5; the reader's P6; tests/support/seed_home.sh says the mechanism). On the NULL headless app: no display, no card.
#   1. the warm-up's step on a toy tree (a CMakeCache.txt, tests/bake-key.txt = the build's): ONE boot makes the template,
#      its seed line "seed: 17 shipped meshes"; a second call at the key makes nothing;
#   2. fresh_home.sh --warm (the real one) on a home under that toy tree COPIES the template's library into the home;
#      its boot logs NO seed line, and its store holds exactly the template's objects (the same content hashes);
#   3. the control: a home with no template boots and logs "seed: 17 shipped meshes";
#   4. A MOVED BAKE KEY: the template is not copied (said) - that home's boot seeds - and the next make re-makes it.
# LAW: a seed is made once per input (the bake key), never per launch.
# Usage: seed_home_template.sh <Jahshaka> <source dir> <build tests dir> <scratch dir>
app=$1; src=$2; btests=$3; s=$4
seedsh="$src/tests/support/seed_home.sh"; fh="$src/tests/support/fresh_home.sh"
fail=0
check() { if [ "$1" = 0 ]; then echo "ok: $2"; else echo "FAIL: $2"; fail=1; fi; }
export JAH_WARM_CHECK=0          # (the toy tree has no warm-up stamps: fresh_home's #140 check is not this row's)
rm -rf "$s"; mkdir -p "$s/build/tests"
: > "$s/build/CMakeCache.txt"
cp "$btests/bake-key.txt" "$s/build/tests/bake-key.txt" || { echo "FAIL: no bake-key.txt in $btests (configure)"; exit 1; }
T="$s/build/tests"
printf 'console.log("ok: booted"); 0\n' > "$s/boot.js"
boot() {   # <home>: the NULL headless app on <home>'s data root; its output in <home>.log
    mkdir -p "$1/run"
    ( cd "$1/run" && env -u DISPLAY -u WAYLAND_DISPLAY HOME="$1" JAHSHAKA_DATA_ROOT="$1/.local/share/Jahshaka" JAHSHAKA_VR=0 \
          "$app" --headless --script "$s/boot.js" ) > "$1.log" 2>&1
}
objects() { ( cd "$1" 2>/dev/null && find AssetStore/objects -type f | sort ); }

out=$(sh "$seedsh" make "$app" "$T"); rc=$?
echo "$out" | grep -q "the template made at the bake key .* (17 shipped meshes, one NULL headless boot)"
check $? "1. the warm-up's step: ONE boot makes the template, its seed line 17 shipped meshes (exit $rc)"
[ -f "$T/.seedhome/data/JahLibrary.db" ] && [ "$(head -1 "$T/.seedhome/bake-key")" = "$(head -1 "$T/bake-key.txt")" ]
check $? "1. ...stamped with the build's bake key"
sh "$seedsh" make "$app" "$T" | grep -q "the template is at the bake key .* (nothing to make)"
check $? "1. ...and a second call at the key makes nothing"

sh "$fh" --setup --warm "$T/h1" > "$s/fh1.log" 2>&1
[ -f "$T/h1/.local/share/Jahshaka/JahLibrary.db" ]
check $? "2. fresh_home.sh --warm copied the template's library into the home"
[ "$(objects "$T/h1/.local/share/Jahshaka")" = "$(objects "$T/.seedhome/data")" ] && [ -n "$(objects "$T/.seedhome/data")" ]
check $? "2. ...the template's objects, by content hash ($(objects "$T/.seedhome/data" | wc -l) files)"
boot "$T/h1"
grep -q "ok: booted" "$T/h1.log" && ! grep -q "seed: [0-9]* shipped meshes" "$T/h1.log"
check $? "2. its boot logs NO seed line (the 17 rows found seeded)"
[ "$(objects "$T/h1/.local/share/Jahshaka")" = "$(objects "$T/.seedhome/data")" ]
check $? "2. ...and after the boot its store still holds exactly the template's objects (nothing re-baked)"

mkdir -p "$s/h2"; boot "$s/h2"
grep -q "seed: 17 shipped meshes imported and baked" "$s/h2.log"
check $? "3. the control: a home with no template seeds the 17 at its boot"

echo "a-moved-bake-key" > "$T/bake-key.txt"
out=$(sh "$fh" --setup --warm "$T/h3" 2>&1)
[ ! -e "$T/h3/.local/share/Jahshaka/JahLibrary.db" ] && echo "$out" | grep -q "at another bake key than the build's - not copied"
check $? "4. a MOVED bake key: the template is not copied, said"
boot "$T/h3"
grep -q "seed: 17 shipped meshes imported and baked" "$T/h3.log"
check $? "4. ...that home's boot seeds the 17"
sh "$seedsh" make "$app" "$T" | grep -q "the template made at the bake key a-moved-bake-key (17 shipped meshes"
check $? "4. ...and the next make re-makes the template at the new key"

rm -rf "$s"
[ $fail = 0 ] && echo "app.seed_home_template: all ok" || echo "app.seed_home_template: FAILED"
exit $fail
