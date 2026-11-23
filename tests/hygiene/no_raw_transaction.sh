#!/usr/bin/env bash
#
# source.no_raw_transaction — EVERY TRANSACTION BEGINS IMMEDIATE, AT ONE DOOR (PREVIEWS-1 stage 1; NOQT-DATABASE-1):
# the std-only layer's guard, db::Transaction's constructor (src/data/db/transaction.cpp), which the DbTransaction
# guard (database.h), the gesture batch (Database::resumeBatchIfNeeded), the preview writer's unit
# (Database::writePreview) and the export's unit all are. The library has a second WRITING connection (the preview
# writer, services/previewwriter.h): a DEFERRED transaction whose first statement is a read fails its first write with
# 517 SQLITE_BUSY_SNAPSHOT once the writer has committed (spikes/previews-1/stage1/probe.out, case 4); IMMEDIATE
# takes the lock at the BEGIN, so no other connection commits inside it (case 5). A raw `BEGIN` anywhere else in
# src/ is red - the door's own line, anchored to ITS function's lines (the stage-1 audit's L4), is the one allowed;
# the query log's NOTE of a unit's BEGIN (`noteQuery(`, Database::writePreview: previews.writer case j counts it) runs
# nothing and is not judged.
#
# CLOSE-2's gesture batch keeps a transaction open for a whole script run, and it stands down for a nested owner
# ONLY when that owner announces itself by constructing a DbTransaction (database.h): the guard must still call the
# batch-yield hook. (The QtSql `.transaction()` half of this row is gone with QtSql: NOQT-DATABASE-1 stage 3.)
#
# $1 = the repo root. The last line is the verdict.
set -u

ROOT="${1:?usage: no_raw_transaction.sh <source-root>}"
cd "$ROOT" || { echo "source.no_raw_transaction: FAILED (1) - no such root $ROOT"; exit 1; }

failures=0

span() {   # span <file> <start ERE> <end ERE>: "first last" lines of the first block from start to end
    awk -v s="$2" -v e="$3" 'f == 0 && $0 ~ s { f = NR } f && NR > f && $0 ~ e { print f, NR; exit }' "$1"
}
read -r lt0 lt1 <<< "$(span src/data/db/transaction.cpp '^Transaction::Transaction\\(' '^}')"
allowed_begin() {   # allowed_begin <file> <line> <text>
    case "$1" in
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
done <<< "$(grep -rnE '"BEGIN' src/ 2>/dev/null | grep -vE ':[0-9]+:[[:space:]]*(//|\*|/\*)' | grep -v 'noteQuery(' || true)"
if [ -n "$begins" ]; then
    echo "FAIL: a raw BEGIN outside the door (db::Transaction's constructor)."
    echo "$begins"
    failures=$((failures + 1))
else
    echo "ok:   every BEGIN is at the door (db::Transaction)"
fi
for door in 'src/data/db/transaction.cpp:connection\.exec\("BEGIN IMMEDIATE", &e\)' \
            'src/data/database/database.h:tx\.emplace\(conn, ' \
            'src/data/database/database.cpp:batchTx = std::make_unique<db::Transaction>\(\*mConnection, ' \
            'src/data/database/database.cpp:db::Transaction unit\(conn, "the preview writer"\)'; do
    file=${door%%:*}; pattern=${door#*:}
    if grep -qE "$pattern" "$file"; then
        echo "ok:   $file begins IMMEDIATE (at the door)"
    else
        echo "FAIL: $file no longer begins its transaction IMMEDIATE at the door"
        failures=$((failures + 1))
    fi
done

# ...and the guard itself must still be the thing that yields the batch.
if grep -q 'sBatchYield(conn)' src/data/database/database.h; then
    echo "ok:   DbTransaction still stands the batch down when it opens"
else
    echo "FAIL: DbTransaction no longer calls the batch-yield hook (database.h)"
    failures=$((failures + 1))
fi

if [ "$failures" -ne 0 ]; then
    echo "source.no_raw_transaction: FAILED ($failures)"
    exit 1
fi
echo "source.no_raw_transaction: PASSED"
