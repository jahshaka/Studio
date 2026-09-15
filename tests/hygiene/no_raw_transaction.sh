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
#   src/data/database/database.cpp  db.transaction()        (the batch itself)
#   src/services/assetmigration.cpp conn  (a ScopedConnection over a recovery DB)
#   src/data/database/database.h    DbTransaction's own BEGIN — it IS the guard
ALLOWED='^src/data/database/database\.cpp:[0-9]+:.*(exportConnection|dbe|batchTxLive = db)\.transaction\(\)|^src/services/assetmigration\.cpp:[0-9]+:.*conn\.transaction\(\)|^src/data/database/database\.h:[0-9]+:.*database\.transaction\(\)'

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
