#!/usr/bin/env bash
#
# app.data_root — --data-root/JAHSHAKA_DATA_ROOT redirects the DATA AND THE
# SETTINGS together, and the un-overridden locations stay untouched.
#
# ENGINEERING_DEBT_SPEC ADDENDUM 6 / WINDOWS_BUILD_SPEC §6.2 W9. The defect this
# gates: under QT_DEBUG `jahsettings.ini` is derived from applicationDirPath(),
# i.e. it sits beside the BINARY and is SHARED by every run of a build tree. A
# scratch HOME moves the library database and the asset store but cannot move
# that file, so an app-spawning suite rewrote the developer's `[assets] storeId`
# and window geometry out from under them during a gate.
#
# The assertions, in the order they are made:
#   1. with --data-root, app.dataRoot() reports the root AND overridden:true;
#   2. the LIBRARY DATABASE is created under it;
#   3. the SETTINGS FILE is under it — the half HOME= could never buy;
#   4. the settings file BESIDE THE BINARY is byte-for-byte unchanged across
#      the run (the owner's file, in a Debug build);
#   5. JAHSHAKA_DATA_ROOT alone does the same thing, and --data-root WINS over
#      it when both are given.
#
# $1 = the Jahshaka binary
set -u

# THE ONE SUITE THAT MUST NOT INHERIT JAHSHAKA_DATA_ROOT (lead law, 2026-09-10).
# Arm 6 runs the binary with NO override at all and asserts that the settings
# file is then where it has always been. Every agent launch, Xvfb rig and gate
# wrapper on this box exports JAHSHAKA_DATA_ROOT for hygiene, so an INHERITED
# one silently deletes that assertion — and the whole gate would have to be run
# as `env -u JAHSHAKA_DATA_ROOT ctest` forever to avoid it. Scrubbed here, once,
# so no caller has to know. Arms 2 and 5 set the variable per command, which is
# unaffected by this; the registration carries the same scrub as a property so
# the contract is visible where the test is declared.
unset JAHSHAKA_DATA_ROOT

BIN="$1"
BINDIR="$(cd "$(dirname "$BIN")" && pwd)"
SHARED_INI="$BINDIR/jahsettings.ini"
fail=0

# THE WORKING DIRECTORY SURVIVES BETWEEN RUNS (ctest sets it to a fixed scratch
# dir), so every artifact this script makes has to be cleared FIRST. Learned by
# failing: run A found the shared file present and saved shared.ini.bak; run B
# found it ABSENT, and the stale backup from run A then "restored" a file that
# was supposed to stay gone. A test whose verdict depends on what the previous
# run left behind is not a gate.
rm -f shared.ini.bak report.js makeproject.js run1.log run2.log run3.log run4.log run5.log
rm -rf root-cli root-env root-wins root-project

check() { if [ "$1" = "0" ]; then echo "ok:   $2"; else echo "FAIL: $2"; fail=1; fi }

# The shared settings file, as it is right now. It may not exist (a clean build
# tree); "does not exist" is a state to preserve exactly as much as any other.
if [ -f "$SHARED_INI" ]; then
    BEFORE="$(sha256sum "$SHARED_INI" | cut -d' ' -f1)"
else
    BEFORE="absent"
fi

run_and_report() {   # $1 = scratch root, rest = extra argv/env already exported
    cat > report.js <<'JS'
var d = app.dataRoot();
console.log("ROOT=" + d.root);
console.log("OVERRIDDEN=" + d.overridden);
console.log("SETTINGS=" + d.settingsFile);
console.log("DB=" + d.database);
console.log("STORE=" + d.assetStore);
console.log("PROJECTS=" + d.projects);
JS
}

# ---- 1-4: --data-root ------------------------------------------------------
ROOT1="$PWD/root-cli"
run_and_report
"$BIN" --headless --data-root "$ROOT1" --script report.js > run1.log 2>&1
rc=$?
check $rc "--data-root run exits 0"
grep -q "ROOT=$ROOT1" run1.log
check $? "app.dataRoot().root is the directory that was asked for"
grep -q "OVERRIDDEN=true" run1.log
check $? "app.dataRoot().overridden is true"
grep -q "SETTINGS=$ROOT1/jahsettings.ini" run1.log
check $? "the SETTINGS FILE moved under the data root (HOME= cannot do this)"
grep -q "DB=$ROOT1/" run1.log
check $? "the library database is under the data root"
grep -q "STORE=$ROOT1/AssetStore" run1.log
check $? "the asset store is under the data root"
grep -q "PROJECTS=$ROOT1" run1.log
check $? "the PROJECTS root is under the data root too (rig hygiene, smoke S-extra2)"
test -f "$ROOT1/jahsettings.ini"
check $? "...and the settings file was really written there"
ls "$ROOT1"/*.db >/dev/null 2>&1
check $? "...and the database file exists under it"

if [ -f "$SHARED_INI" ]; then
    AFTER="$(sha256sum "$SHARED_INI" | cut -d' ' -f1)"
else
    AFTER="absent"
fi
[ "$BEFORE" = "$AFTER" ]
check $? "the settings file beside the binary is untouched ($BEFORE)"

# ---- 5: the environment variable, and the CLI winning over it --------------
ROOT2="$PWD/root-env"
JAHSHAKA_DATA_ROOT="$ROOT2" "$BIN" --headless --script report.js > run2.log 2>&1
check $? "JAHSHAKA_DATA_ROOT run exits 0"
grep -q "ROOT=$ROOT2" run2.log
check $? "JAHSHAKA_DATA_ROOT alone chooses the root"
grep -q "SETTINGS=$ROOT2/jahsettings.ini" run2.log
check $? "...and the settings file follows it too"

ROOT3="$PWD/root-wins"
JAHSHAKA_DATA_ROOT="$ROOT2" "$BIN" --headless --data-root "$ROOT3" --script report.js > run3.log 2>&1
check $? "both-given run exits 0"
grep -q "ROOT=$ROOT3" run3.log
check $? "--data-root wins over JAHSHAKA_DATA_ROOT"

# ---- 5b: A PROJECT REALLY LANDS THERE --------------------------------------
# The path is one assertion; the FOLDER is the one that matters. Before this,
# --data-root moved the database, the store, the cache and the settings and left
# project.create writing into the developer's ~/Documents/Jahshaka/Projects —
# nineteen empty project folders appeared there in one night of scripted runs.
ROOT4="$PWD/root-project"
DOCS_BEFORE="$(ls "$HOME/Documents/Jahshaka/Projects" 2>/dev/null | wc -l)"
cat > makeproject.js <<'JS'
var guid = project.create("DataRootProject");
console.log("GUID=" + guid);
console.log("PATH=" + project.current().folder);
JS
"$BIN" --headless --data-root "$ROOT4" --script makeproject.js > run5.log 2>&1
check $? "a project-creating run with --data-root exits 0"
grep -q "PATH=$ROOT4/Projects/" run5.log
check $? "project.create writes its folder UNDER the data root"
PGUID="$(grep '^GUID=' run5.log | head -1 | cut -d= -f2)"
test -n "$PGUID" && test -d "$ROOT4/Projects/$PGUID"
check $? "...and the folder is really there ($ROOT4/Projects/$PGUID)"
DOCS_AFTER="$(ls "$HOME/Documents/Jahshaka/Projects" 2>/dev/null | wc -l)"
[ "$DOCS_BEFORE" = "$DOCS_AFTER" ]
check $? "...and NOTHING was created in the un-overridden Documents projects folder"

# ---- 6: NO override — the historical location, unchanged -------------------
# This run DOES write the shared file (that is the behaviour being asserted), so
# it is bracketed by a byte-for-byte save/restore: a hygiene gate that leaves a
# shared file dirtier than it found it would be its own counter-example.
if [ -f "$SHARED_INI" ]; then cp -p "$SHARED_INI" shared.ini.bak; fi
"$BIN" --headless --script report.js > run4.log 2>&1
check $? "un-overridden run exits 0"
grep -q "OVERRIDDEN=false" run4.log
check $? "with no override, app.dataRoot().overridden is false"
grep -q "SETTINGS=$BINDIR/jahsettings.ini" run4.log
check $? "with no override the settings file is where it has always been (Debug: beside the binary)"
grep -q "PROJECTS=$HOME/Documents" run4.log
check $? "with no override the projects root is the historical Documents folder (nothing moved)"

if [ -f shared.ini.bak ]; then
    cp -p shared.ini.bak "$SHARED_INI"
elif [ "$BEFORE" = "absent" ]; then
    rm -f "$SHARED_INI"
fi
if [ -f "$SHARED_INI" ]; then
    RESTORED="$(sha256sum "$SHARED_INI" | cut -d' ' -f1)"
else
    RESTORED="absent"
fi
[ "$BEFORE" = "$RESTORED" ]
check $? "the shared settings file is restored byte for byte after the un-overridden run"

if [ $fail -eq 0 ]; then echo "ALL PASS"; else echo "FAILURES"; fi
exit $fail
