#!/usr/bin/env bash
#
# source.settings_keys — every settings key has ONE owner (STUDIO-CRUD-1 item 10).
#
# A preference is declared once, with its one default, in src/data/settingkeys.h
# (or by a subsystem's own key function: ThemeManager::settingsKey,
# framepacing::settingsKey, loadingcover::settingsKey, …) and read through
# SettingsManager::get. What this refuses is the pattern that made the table
# necessary: a key LITERAL read WITH A DEFAULT — getValue("key", default) —
# at more than one site, where the defaults drift apart one edit at a time;
# and a literal read of a key the table declares, which is a second owner.
#
# $1 = the repo root
set -u

ROOT="${1:?usage: settings_keys.sh <source-root>}"
cd "$ROOT" || { echo "source.settings_keys: no such root $ROOT"; exit 1; }

failures=0

# Every getValue("literal", …) site outside comments, as "key<TAB>file:line".
sites=$(grep -rnE --include='*.cpp' --include='*.h' \
            'getValue\(\s*(QStringLiteral\()?"[^"]+"' src 2>/dev/null \
        | grep -vE '^[^:]+:[0-9]+:\s*//' \
        | sed -E 's/^([^:]+:[0-9]+):.*getValue\(\s*(QStringLiteral\()?"([^"]+)".*/\3\t\1/')

dups=$(printf '%s\n' "$sites" | cut -f1 | sort | uniq -d)
if [ -n "$dups" ]; then
    echo "FAIL: a settings key literal is read with a default at more than one site —"
    echo "      declare it once in src/data/settingkeys.h and read it with SettingsManager::get:"
    for k in $dups; do printf '%s\n' "$sites" | awk -F'\t' -v k="$k" '$1 == k { print "        " $1 "  " $2 }'; done
    failures=$((failures + 1))
else
    echo "ok:   no settings key literal is read with a default at two sites"
fi

# A key the table declares must not also be read or written as a literal —
# through SettingsManager (getValue/setValue) OR straight on a QSettings
# (->value / ->setValue / .value / .setValue), which is how a widget handed a
# raw QSettings* used to slip past this check (the Claude chat's model key).
declared=$(grep -oE 'SettingKey<[^>]+>\s+\w+\{\s*"[^"]+"' src/data/settingkeys.h \
           | sed -E 's/.*"([^"]+)"/\1/' | sort -u)
second=""
for k in $declared; do
    hits=$(grep -rnE --include='*.cpp' --include='*.h' \
               "(getValue|setValue|->value|\.value|->setValue|\.setValue)\(\s*(QString(Literal|::fromLatin1)\()?\"$k\"" src 2>/dev/null \
           | grep -vE '^[^:]+:[0-9]+:\s*//')
    [ -n "$hits" ] && second="$second$hits"$'\n'
done
if [ -n "$second" ]; then
    echo "FAIL: a key declared in src/data/settingkeys.h is also read or written as a literal:"
    printf '%s' "$second" | sed 's/^/        /'
    failures=$((failures + 1))
else
    echo "ok:   the declared keys ($(echo $declared | wc -w)) are read and written only through the table"
fi

if [ $failures -ne 0 ]; then
    echo "source.settings_keys: $failures check(s) failed"
    exit 1
fi
echo "source.settings_keys: all checks passed"
