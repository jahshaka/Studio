#!/bin/sh
# app.seed_home_template — THE 17 SHIPPED MESHES SEEDED ONCE PER SEED KEY, NOT ONCE PER WARM HOME (TEST-STREAMLINE-1 3a
# item 5 + its audit F4; the reader's P6; tests/support/seed_home.sh says the mechanism). The NULL headless app: no display,
# no card. A toy tree (a CMakeCache.txt, tests/seed-key-inputs.txt = the build's list, its root a scratch COPY of the
# listed sources so a case can change one):
#   1. the warm-up's step: ONE boot makes the template, its seed's result every planned row; again at the key, nothing;
#   2. fresh_home.sh --warm (the real one) COPIES the template into a home: its boot CREATES NOTHING (the seed's result
#      `created` 0), its store the template's objects by content hash, unchanged by the boot;
#   3. the control (of 2's zero): a home with no template seeds every row it planned (`created` == `planned` > 0, the
#      report's ONE count of the shipped meshes), and the seed key lists that many sources;
#   4. A CHANGED SHIPPED SOURCE (audit F4): the seed key moves - the template is not copied (said), that home seeds, and
#      the next make re-makes the template;
#   5. THE APP's OWN CHECK (audit F4): a home whose stored Cube source is not the shipped mesh re-seeds that row at its
#      boot, by name - a template that slipped past its stamp still yields this build's meshes.
# Every boot's facts are RESULTS its script prints on stdout from app.bootReport() (seed.created, seed.reseeded by name);
# no case reads a log line.
# LAW: a seed is made once per input (the seed key), never per launch, and never serves another input's meshes.
# Usage: seed_home_template.sh <Jahshaka> <source dir> <build tests dir> <scratch dir>
app=$1; src=$2; btests=$3; s=$4
seedsh="$src/tests/support/seed_home.sh"; fh="$src/tests/support/fresh_home.sh"
fail=0
check() { if [ "$1" = 0 ]; then echo "ok: $2"; else echo "FAIL: $2"; fail=1; fi; }
export JAH_WARM_CHECK=0          # (the toy tree has no warm-up stamps: fresh_home's #140 check is not this row's)
rm -rf "$s"; mkdir -p "$s/build/tests" "$s/src"
: > "$s/build/CMakeCache.txt"
[ -f "$btests/seed-key-inputs.txt" ] || { echo "FAIL: no seed-key-inputs.txt in $btests (configure)"; exit 1; }
# the toy's own list: the build's inputs, rooted at a scratch copy of every listed file
sed "s#^root .*#root $s/src#" "$btests/seed-key-inputs.txt" > "$s/build/tests/seed-key-inputs.txt"
sed -n 's/^file //p' "$btests/seed-key-inputs.txt" | while read -r f; do
    mkdir -p "$s/src/$(dirname "$f")"; cp "$src/$f" "$s/src/$f"
done
nmesh=$(grep -c '^file app/.*\.obj$' "$s/build/tests/seed-key-inputs.txt")
T="$s/build/tests"
cat > "$s/boot.js" <<'JS'
var seed = app.bootReport().seed;
var names = [];
for (var i = 0; i < seed.reseeded.length; ++i) names.push(seed.reseeded[i]);
console.log("seed.created " + seed.created);
console.log("seed.planned " + seed.planned);
console.log("seed.reseeded " + names.join(","));
0
JS
boot() {   # <home>: the NULL headless app on <home>'s data root; its results (stdout) in <home>.out, its log in <home>.log
    mkdir -p "$1/run"
    ( cd "$1/run" && env -u DISPLAY -u WAYLAND_DISPLAY HOME="$1" JAHSHAKA_DATA_ROOT="$1/.local/share/Jahshaka" JAHSHAKA_VR=0 \
          "$app" --headless --script "$s/boot.js" ) > "$1.out" 2> "$1.log"
}
created() { sed -n 's/^seed\.created \([0-9][0-9]*\)$/\1/p' "$1.out" | head -1; }    # <home>: its seed's `created`
planned() { sed -n 's/^seed\.planned \([0-9][0-9]*\)$/\1/p' "$1.out" | head -1; }    # <home>: the rows its seed planned
reseeded() { sed -n 's/^seed\.reseeded //p' "$1.out" | head -1; }                      # <home>: its re-seeded names
objects() { ( cd "$1" 2>/dev/null && find AssetStore/objects -type f | sort ); }

out=$(sh "$seedsh" make "$app" "$T"); rc=$?
echo "$out" | grep -q "the template made at the seed key .* ([0-9]* shipped meshes, one NULL headless boot)"
check $? "1. the warm-up's step: ONE boot makes the template (its seed's result: every planned row) (exit $rc)"
[ -f "$T/.seedhome/data/JahLibrary.db" ] && [ "$(head -1 "$T/.seedhome/seed-key")" = "$(sh "$seedsh" key "$T")" ]
check $? "1. ...stamped with the build's seed key"
sh "$seedsh" make "$app" "$T" | grep -q "the template is at the seed key .* (nothing to make)"
check $? "1. ...and a second call at the key makes nothing"

sh "$fh" --setup --warm "$T/h1" > "$s/fh1.log" 2>&1
[ -f "$T/h1/.local/share/Jahshaka/JahLibrary.db" ]
check $? "2. fresh_home.sh --warm copied the template's library into the home"
[ "$(objects "$T/h1/.local/share/Jahshaka")" = "$(objects "$T/.seedhome/data")" ] && [ -n "$(objects "$T/.seedhome/data")" ]
check $? "2. ...the template's objects, by content hash ($(objects "$T/.seedhome/data" | wc -l) files)"
boot "$T/h1"
[ "$(created "$T/h1")" = 0 ]
check $? "2. its boot CREATES NOTHING: seed.created $(created "$T/h1") (every row found seeded; case 3 is this zero's control)"
[ "$(objects "$T/h1/.local/share/Jahshaka")" = "$(objects "$T/.seedhome/data")" ]
check $? "2. ...and after the boot its store still holds exactly the template's objects (nothing re-baked)"

mkdir -p "$s/h2"; boot "$s/h2"
P=$(planned "$s/h2")   # THE ONE COUNT of the shipped meshes: the rows the seed planned (app.bootReport().seed.planned)
[ -n "$P" ] && [ "$P" -gt 0 ] && [ "$(created "$s/h2")" = "$P" ]
check $? "3. the control: a home with no template seeds every planned row at its boot: seed.created $(created "$s/h2") of seed.planned ${P:-none}"
[ "$nmesh" = "$P" ] && [ "$(planned "$T/h1")" = "$P" ]
check $? "3. ...the seed key lists every planned row's source ($nmesh of $P), and the template's home plans the same $(planned "$T/h1")"

k0=$(sh "$seedsh" key "$T")
printf '# a shipped mesh edited\n' >> "$s/src/app/content/primitives/cube.obj"
k1=$(sh "$seedsh" key "$T")
[ -n "$k0" ] && [ "$k0" != "$k1" ]
check $? "4. a CHANGED shipped source (cube.obj) moves the seed key"
out=$(sh "$fh" --setup --warm "$T/h3" 2>&1)
[ ! -e "$T/h3/.local/share/Jahshaka/JahLibrary.db" ] && echo "$out" | grep -q "at another seed key than the build's - not copied"
check $? "4. ...the template is not copied, said"
boot "$T/h3"
[ "$(created "$T/h3")" = "$P" ]
check $? "4. ...that home's boot seeds every planned row: seed.created $(created "$T/h3") of $P"
sh "$seedsh" make "$app" "$T" | grep -q "the template made at the seed key $k1 ($P shipped meshes"
check $? "4. ...and the next make re-makes the template at the new key"

sh "$fh" --setup --warm "$T/h4" > /dev/null 2>&1
cube=$(sha256sum < "$src/app/content/primitives/cube.obj" | cut -c1-64)
obj=$(find "$T/h4/.local/share/Jahshaka/AssetStore/objects" -name "$cube.obj" | head -1)
# (a store object is read-only: the stand-in for a template made from another build's cube.obj)
[ -n "$obj" ] && chmod u+w "$obj" && printf '# not the shipped mesh\n' >> "$obj"
boot "$T/h4"
[ -n "$obj" ] && [ "$(reseeded "$T/h4")" = Cube ] && [ "$(created "$T/h4")" = 1 ]
check $? "5. the app's own check: a stored Cube source that is not the shipped mesh is RE-SEEDED at the boot, by name: seed.reseeded '$(reseeded "$T/h4")', seed.created $(created "$T/h4")"

rm -rf "$s"
[ $fail = 0 ] && echo "app.seed_home_template: all ok" || echo "app.seed_home_template: FAILED"
exit $fail
