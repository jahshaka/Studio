#!/bin/sh
# tooling.owner_home_guard - A TEST NEVER BOOTS THE APP ON THE LOGIN USER's OWN DATA ROOT (tests/support/owner_home_guard.sh,
# TEST-STREAMLINE-1 block A): fresh_home.sh (both command paths) and warm_app_boot.sh refuse, exit 2, a command that launches
# the app (a fake `Jahshaka` here: it only touches a marker in the toy's scratch) when the app's effective data root is the
# login user's ~/.local/share/Jahshaka; a test home, --data-root, an env word and a non-app command are never refused.
# No card, no display; nothing is written outside <scratch>. usage: owner_home_guard_toy.sh <source root> <scratch>
SRC=$1; T=$2/owner-home-guard.$$; rm -rf "$T"; mkdir -p "$T/bin"
O=$(getent passwd "$(id -un)" | cut -d: -f6)
printf '#!/bin/sh\n: > "%s/ran"\n' "$T" > "$T/bin/Jahshaka"; chmod +x "$T/bin/Jahshaka"
FH="$SRC/tests/support/fresh_home.sh"; WB="$SRC/tests/support/warm_app_boot.sh"; fails=0
case_() {   # <want: refused|ran> <what> <command...>
    want=$1 what=$2; shift 2; rm -f "$T/ran"
    out=$(env -u JAHSHAKA_DATA_ROOT -u XDG_DATA_HOME "$@" 2>&1); rc=$?
    if [ "$want" = refused ]; then ok=$([ $rc = 2 ] && [ ! -e "$T/ran" ] && echo "$out" | grep -q "REFUSED (exit 2): the app's data root would be the login user's own" && echo 1)
    else ok=$([ $rc = 0 ] && [ -e "$T/ran" ] && echo 1); fi
    [ "$ok" = 1 ] && echo "ok:   $what (rc $rc)" || { echo "FAIL: $what (rc $rc, ran $([ -e "$T/ran" ] && echo yes || echo no)) $out"; fails=$((fails + 1)); }
}
case_ refused "fresh_home: HOME the owner's, the app with no data root" env HOME="$O" sh "$FH" "$T/h1" -- "$T/bin/Jahshaka"
case_ refused "fresh_home: JAHSHAKA_DATA_ROOT the owner's" env HOME="$T/h2" JAHSHAKA_DATA_ROOT="$O/.local/share/Jahshaka" sh "$FH" "$T/h2" -- "$T/bin/Jahshaka"
case_ refused "fresh_home: an env word naming the owner's root" env HOME="$T/h3" sh "$FH" "$T/h3" -- env JAHSHAKA_DATA_ROOT="$O/.local/share/Jahshaka/" "$T/bin/Jahshaka"
case_ refused "fresh_home --promote: HOME the owner's" env HOME="$O" sh "$FH" --promote "$T/h4" -- "$T/bin/Jahshaka"
case_ refused "warm_app_boot: HOME the owner's" env HOME="$O" bash "$WB" "$T/bin/Jahshaka" "$T/w.png" "$T/no-sets" "$T"
case_ ran "fresh_home: HOME the test home" env HOME="$T/h5" sh "$FH" "$T/h5" -- "$T/bin/Jahshaka"
case_ ran "fresh_home: --data-root a test home beats the owner's HOME" env HOME="$O" sh "$FH" "$T/h6" -- "$T/bin/Jahshaka" --data-root "$T/h6/run"
case_ ran "fresh_home: an env word moves the root off the owner's HOME" env HOME="$O" sh "$FH" "$T/h7" -- env JAHSHAKA_DATA_ROOT="$T/h7/d" "$T/bin/Jahshaka"
case_ ran "fresh_home: a relative JAHSHAKA_DATA_ROOT (beside the app)" env HOME="$O" JAHSHAKA_DATA_ROOT=data sh "$FH" "$T/h8" -- "$T/bin/Jahshaka"
rm -f "$T/ran"; env HOME="$O" sh "$FH" "$T/h9" -- /bin/true; rc=$?
[ $rc = 0 ] && echo "ok:   fresh_home: a command that is not the app (an engine fixture's) is never judged (rc $rc)" || { echo "FAIL: a non-app command refused (rc $rc)"; fails=$((fails + 1)); }
rm -rf "$T"
[ $fails = 0 ] && echo "owner_home_guard_toy: PASSED" || { echo "owner_home_guard_toy: FAILED ($fails)"; exit 1; }
