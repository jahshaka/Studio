#!/usr/bin/env bash
#
# source.one_asset_name_write — AN ASSET'S NAME IS WRITTEN ONLY BY assettags::write (STUDIO-HYGIENE-1 stage 3).
#
# WHY. The name law lives there and only there: a SHIPPED PRESET cannot be renamed (its name is its identity:
# a renamed row is one guid with two names for ever), and NOTHING ELSE may take a preset's name (a preset is
# found by name, so an asset called "Gold PBR" can never be reached). Two writers went round it: the Assets
# panel's rename (Database::renameAsset — the preset refusal skipped from the UI, the old extension appended to
# the typed name) and the avatar save's name sync (a bare updateAssetMetadata — an avatar could take a preset's
# name). Both go through assettags now, and renameAsset is deleted.
#
# THE RULE: no call of renameAsset anywhere under src/; outside the database itself, the only CALL of
# updateAssetMetadata is services/assettags.h's. (A comment naming either is not a call.)
#
# $1 = the repo root
set -u
ROOT="${1:?usage: one_asset_name_write.sh <source-root>}"
cd "$ROOT" || { echo "source.one_asset_name_write: no such root $ROOT"; exit 1; }

calls() {   # $1 = the function name: file:line:code of every CALL (the code before any // names it)
    grep -rnE "(\.|->)$1[[:space:]]*\(" src \
        | awk -v fn="$1" -F: '{ code = $0; sub(/^[^:]*:[^:]*:/, "", code); sub(/\/\/.*/, "", code);
                                if (code ~ ("(\\.|->)" fn "[[:space:]]*\\(")) print }' || true
}
fail=0
hits=$(calls renameAsset)
if [ -n "$hits" ]; then
    echo "source.one_asset_name_write: FAIL — a renameAsset call (the name written past the law)"
    echo "$hits" | sed 's/^/    /'; fail=1
fi
hits=$(calls updateAssetMetadata | grep -vE '^src/services/assettags\.h:|^src/data/database/database\.(h|cpp):' || true)
if [ -n "$hits" ]; then
    echo "source.one_asset_name_write: FAIL — updateAssetMetadata called outside assettags::write"
    echo "$hits" | sed 's/^/    /'; fail=1
fi
if [ $fail = 0 ]; then
    echo "source.one_asset_name_write: ok — every asset name write goes through assettags::write"
else
    echo "    Rename through assettags::rename / assettags::write (services/assettags.h); its"
    echo "    nameRefusal says why a name is refused, in the one sentence every door shows."
fi
exit $fail
