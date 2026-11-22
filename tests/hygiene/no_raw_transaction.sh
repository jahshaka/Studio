#!/usr/bin/env bash
#
# source.no_raw_transaction — a transaction on the MAIN connection is a
# DbTransaction guard, never a raw QSqlDatabase::transaction().
#
# CLOSE-2's gesture batch (Database::beginBatch) keeps a transaction open for
# the whole of a script run, and it stands down for a nested owner ONLY when
# that owner announces itself by constructing a DbTransaction (database.h). A
# raw `conn.transaction()` cannot: with the batch holding the connection the
# BEGIN simply fails, the caller's own `if (started)` reads false, its writes
# ride the batch's commit, and its rollback path does nothing. That is how
# AssetGc::sweep would have committed dropped `files` rows while leaving their
# objects on disk (assetgc.cpp, round 2 H1).
#
# AND EVERY TRANSACTION ON IT BEGINS IMMEDIATE, AT ONE OF TWO DOORS (PREVIEWS-1 stage 1): the
# DbTransaction guard (database.h) and the batch (Database::resumeBatchIfNeeded). The std-only layer's
# guard (db::Transaction's constructor, src/data/db/transaction.cpp: NOQT-DATABASE-1) is the third
# door, the one the first two move onto. The library has a
# second WRITING connection (the preview writer, services/previewwriter.h): a DEFERRED transaction
# whose first statement is a read fails its first write with 517 SQLITE_BUSY_SNAPSHOT once the
# writer has committed (spikes/previews-1/stage1/probe.out, case 4); IMMEDIATE takes the lock at the
# BEGIN, so no other connection commits inside it (case 5). A raw `BEGIN` anywhere else is red; the
# preview writer's own BEGIN IMMEDIATE (Database::writePreview, on ITS connection) is listed.
#
# Raw calls on a SIDE connection are fine — a connection nobody else is using
# cannot collide with the batch, and the guard count is per connection. They
# are listed here by file:symbol so that adding one is a deliberate act.
#
# $1 = the repo root
set -u

ROOT="${1:?usage: no_raw_transaction.sh <source-root>}"
cd "$ROOT" || { echo "source.no_raw_transaction: no such root $ROOT"; exit 1; }

failures=0

# The allowed raw calls, each on a connection that TU opened itself:
#   src/data/database/database.cpp  exportConnection / dbe  (bundle writers)
#   src/services/assetmigration.cpp conn  (a ScopedConnection over a recovery DB)
ALLOWED='^src/data/database/database\.cpp:[0-9]+:.*(exportConnection|dbe)\.transaction\(\)|^src/services/assetmigration\.cpp:[0-9]+:.*conn\.transaction\(\)'

# Comments are not calls: a line whose first non-space characters are // or *
# is prose about the rule, which several of these files carry.
hits=$(grep -rnE '\.transaction\(\)' src/ 2>/dev/null \
       | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)' \
       | grep -vE "$ALLOWED" || true)

if [ -n "$hits" ]; then
    echo "FAIL: raw QSqlDatabase::transaction() outside the allowed side-connection sites."
    echo "      Use a DbTransaction guard (data/database/database.h) so the editor's"
    echo "      gesture batch stands down for it — see the header of this script."
    echo "$hits"
    failures=$((failures + 1))
else
    echo "ok:   no raw .transaction() on the main connection"
fi

# The raw BEGINs: exactly the two doors and the preview writer's own connection - each anchored to ITS
# FUNCTION's lines (the stage-1 audit's L4: a pattern alone would admit the same text anywhere in the file).
span() {   # span <file> <start ERE> <end ERE>: "first last" lines of the first block from start to end
    awk -v s="$2" -v e="$3" 'f == 0 && $0 ~ s { f = NR } f && NR > f && $0 ~ e { print f, NR; exit }' "$1"
}
read -r tx0 tx1 <<< "$(span src/data/database/database.h '^class DbTransaction' '^};')"
read -r bt0 bt1 <<< "$(span src/data/database/database.cpp '^bool Database::resumeBatchIfNeeded\\(' '^}')"
read -r wp0 wp1 <<< "$(span src/data/database/database.cpp '^Database::PreviewWrite Database::writePreview\\(' '^}')"
read -r lt0 lt1 <<< "$(span src/data/db/transaction.cpp '^Transaction::Transaction\\(' '^}')"
allowed_begin() {   # allowed_begin <file> <line> <text>
    case "$1" in
        src/data/database/database.h)
            [ -n "$tx0" ] && [ "$2" -gt "$tx0" ] && [ "$2" -lt "$tx1" ] \
                && printf '%s' "$3" | grep -qE 'begin\.exec\(QStringLiteral\("BEGIN IMMEDIATE"\)\)' ;;
        src/data/database/database.cpp)
            { [ -n "$bt0" ] && [ "$2" -gt "$bt0" ] && [ "$2" -lt "$bt1" ] \
                  && printf '%s' "$3" | grep -qE 'batchTxLive = begin\.exec\(QStringLiteral\("BEGIN IMMEDIATE"\)\)'; } \
            || { [ -n "$wp0" ] && [ "$2" -gt "$wp0" ] && [ "$2" -lt "$wp1" ] \
                  && printf '%s' "$3" | grep -qE 'QStringLiteral\("BEGIN IMMEDIATE"\)'; } ;;
        src/data/db/transaction.cpp)
            [ -n "$lt0" ] && [ "$2" -gt "$lt0" ] && [ "$2" -lt "$lt1" ] \
                && printf '%s' "$3" | grep -qE 'connection\.exec\("BEGIN IMMEDIATE", &e\)' ;;
        *) return 1 ;;
    esac
}
begins=""
while IFS= read -r hit; do
    [ -n "$hit" ] || continue
    file=${hit%%:*}; rest=${hit#*:}; line=${rest%%:*}; text=${rest#*:}
    allowed_begin "$file" "$line" "$text" || begins="$begins$hit
"
done <<< "$(grep -rnE '"BEGIN' src/ 2>/dev/null | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)' || true)"
if [ -n "$begins" ]; then
    echo "FAIL: a raw BEGIN outside the two doors (DbTransaction, the batch) and the preview writer's own connection."
    echo "$begins"
    failures=$((failures + 1))
else
    echo "ok:   every BEGIN is at a door (DbTransaction, the batch) or on the preview writer's own connection"
fi
for door in 'src/data/database/database.h:begin\.exec\(QStringLiteral\("BEGIN IMMEDIATE"\)\)' \
            'src/data/database/database.cpp:batchTxLive = begin\.exec\(QStringLiteral\("BEGIN IMMEDIATE"\)\)' \
            'src/data/db/transaction.cpp:connection\.exec\("BEGIN IMMEDIATE", &e\)'; do
    file=${door%%:*}; pattern=${door#*:}
    if grep -qE "$pattern" "$file"; then
        echo "ok:   $file begins IMMEDIATE"
    else
        echo "FAIL: $file no longer begins its transaction IMMEDIATE"
        failures=$((failures + 1))
    fi
done

# ...and the guard itself must still be the thing that yields the batch.
if grep -q 'sBatchYield(database)' src/data/database/database.h; then
    echo "ok:   DbTransaction still stands the batch down when it opens"
else
    echo "FAIL: DbTransaction no longer calls the batch-yield hook (database.h)"
    failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
    echo "source.no_raw_transaction: $failures failure(s)"
    exit 1
fi
echo "source.no_raw_transaction: ok"
