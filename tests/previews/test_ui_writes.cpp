/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// previews.ui_writes — A UI WRITE NEVER FAILS BECAUSE A PREVIEW WAS WRITTEN (PREVIEWS-1 stage 1, the
// lead's G1-G3). The library has a second writing connection (the preview writer); a UI connection
// that holds a READ SNAPSHOT when the writer commits fails its next write AT ONCE with 517
// SQLITE_BUSY_SNAPSHOT (spikes/previews-1/stage1/probe.out). A temp library, the real Database, the
// real writer; no engine, no display. Each shape reads, lets the writer store a preview, then writes:
//
//   1. A DbTransaction that READS FIRST: its write succeeds and its commit holds (IMMEDIATE: the
//      writer waits behind it and lands after). RED on a deferred BEGIN: the write fails with 517.
//   2. THE BATCH (a script run's, Database::beginBatch) that reads first: its write succeeds (the
//      batch begins at its first WRITE, IMMEDIATE); a preview submitted while the batch holds the lock
//      lands once the batch ends. RED on the eager deferred batch: the write fails with 517.
//   3. THE IMPORT CLAIM's statement sequence (assetimportservice.cpp relistUnlistedMatch): a COUNT and
//      a LIMIT 1 read, then setAssetListed. With the reads left ACTIVE (the base's shape) the write
//      fails with 517 - the hazard, measured here as the negative control; with the reads SCOPED (the
//      product's shape since G3) it succeeds. The product function's own window cannot be driven
//      from a row (no seam between its read and its write): source.no_live_read_at_write guards it.
//   4. A STALE LIVE READ (probe case 6: no BEGIN can close it): a DbTransaction opened behind it FAILS
//      LOUDLY AND WRITES NOTHING - its commit() answers false, the failure is logged by name, every
//      write in its scope is refused (through Database's funnel, a raw exec, a nested guard), every
//      table is byte-equal after it, and the next transaction on the connection works. RED when the
//      BEGIN is deferred: the BEGIN passes, a write fails alone and commit() answers true.
//   5. A BEGIN THAT TIMES OUT behind ANOTHER writer, which lets go before the scope's statements run
//      (the UI connection's busy timeout set to 50 ms for the case): the scope still writes NOTHING -
//      without the refusal its statements would autocommit, half a unit landing under a commit() that
//      answers false (the door of 182e8c0ec: red, spikes/previews-1/stage1/ui_writes-refused-red.txt).

#include <cstdio>
#include <functional>
#include <memory>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>

#include "data/database/database.h"
#include "services/previewwriter.h"
#include "../support/testsql.h"

namespace {

int failures = 0;
QStringList warnings;

void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

void captureWarnings(QtMsgType type, const QMessageLogContext &, const QString &text)
{
    if (type == QtWarningMsg) warnings.append(text);
}

bool waitFor(const std::function<bool()> &done)
{
    QElapsedTimer cap;
    cap.start();
    while (!done()) {
        if (cap.elapsed() > 20000) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return true;
}

/// The suite's UI connection (the Database's: main sets it once it is open).
db::Connection *gLibrary = nullptr;

void addAsset(const QString &guid, int listed = 1)
{
    testsql::run(*gLibrary, QStringLiteral("INSERT INTO assets (guid, type, name, listed) VALUES (?, 1, ?, ?)"), guid,
                 guid, listed);
}

QString storedKey(const QString &guid)
{
    return testsql::text(*gLibrary, QStringLiteral("SELECT hash FROM thumbnails WHERE guid = ?"), guid);
}

QString nameOf(const QString &guid)
{
    return testsql::text(*gLibrary, QStringLiteral("SELECT name FROM assets WHERE guid = ?"), guid);
}

/// Every table's every row, hashed: "byte-equal" for the whole library - read on a FRESH connection
/// of its own (the UI connection's own view is the stale snapshot under test). Each value by its quote().
QByteArray libraryDigest(const QString &path)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    db::Connection fresh = testsql::open(path, "digest", /*readOnly*/ true);
    const QStringList tables =
        testsql::column(fresh, QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name"));
    for (const QString &table : tables) {
        const QStringList columns =
            testsql::column(fresh, QStringLiteral("SELECT 'quote(\"' || name || '\")' FROM pragma_table_info(?) ORDER BY cid"),
                            table);
        hash.addData(table.toUtf8());
        for (const QString &row : testsql::column(fresh, QStringLiteral("SELECT %1 FROM \"%2\" ORDER BY rowid")
                                                             .arg(columns.join(QStringLiteral(" || '\x1f' || ")), table)))
            hash.addData(row.toUtf8());
    }
    return hash.result();
}

QImage solid(QRgb colour)
{
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(colour);
    return image;
}

/// Submits a preview of `guid` and returns once it LANDED or was DROPPED behind the UI's lock
/// (whichever this code does); `landed` says which.
bool previewLandsOrWaits(previews::Writer &writer, const QString &guid, const QString &key, QStringList &drops,
                         bool *landed)
{
    // SHARED: when this returns on a drop, the completion still arrives later.
    const auto done = std::make_shared<bool>(false);
    const int before = drops.size();
    writer.submit(guid, solid(qRgb(9, 9, 9)), key, [done, guid](const previews::Writer::Landed &l) {
        *done = true;
        if (!l.ok) std::printf("     the preview of %s did not land: %s\n", qUtf8Printable(guid), qUtf8Printable(l.reason));
    });
    const bool settled = waitFor([&] { return *done || drops.size() > before; });
    *landed = *done;
    return settled;
}

}   // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("JahLibrary.db"));
    Database db;
    check(db.initializeDatabase(path), "a temp library opens");
    gLibrary = &db.connection();
    db.createAllTables();
    for (const char *g : { "t1", "p1", "t2", "p2", "p3", "u1", "p4", "t4", "p5" }) addAsset(QString::fromLatin1(g));
    addAsset(QStringLiteral("gone"), 0);

    previews::Writer writer(path, 100);   // a short budget: a drop is seen in 100 ms
    QStringList drops;
    writer.setDropHook([&drops](const QString &guid, const QString &) { drops.append(guid); });
    qInstallMessageHandler(captureWarnings);

    // 1. A DbTransaction that reads first.
    {
        bool committed = false, wrote = false, landed = false;
        {
            DbTransaction tx(db.connection());
            const QString before = nameOf(QStringLiteral("t1"));   // the read: a finished statement
            check(previewLandsOrWaits(writer, QStringLiteral("p1"), QStringLiteral("00000000000000p1"), drops, &landed),
                  "1: the preview landed or waited behind the transaction");
            db::Error we;
            wrote = testsql::exec(db.connection(), QStringLiteral("UPDATE assets SET name = 'tx-wrote' WHERE guid = 't1'"), &we);
            if (!wrote) std::printf("     1: the write failed: %s\n", qUtf8Printable(dbqt::describe(we)));
            committed = tx.commit();
            Q_UNUSED(before);
        }
        std::printf("     1: the preview %s inside the transaction\n", landed ? "LANDED" : "waited");
        check(wrote, "1: the transaction's write succeeds after a preview was written beside it");
        check(committed && nameOf(QStringLiteral("t1")) == QStringLiteral("tx-wrote"), "1: its commit holds the write");
        check(waitFor([&] { return storedKey(QStringLiteral("p1")) == QStringLiteral("00000000000000p1"); }),
              "1: the preview is stored after the transaction");
    }

    // 2. The batch that reads first.
    {
        bool landed = false;
        db.beginBatch();
        const AssetRecord read = db.fetchAsset(QStringLiteral("t2"));   // the run's first statement: a read
        check(read.guid == QStringLiteral("t2"), "2: the batch's first statement is a read");
        check(previewLandsOrWaits(writer, QStringLiteral("p2"), QStringLiteral("00000000000000p2"), drops, &landed),
              "2: a preview lands (a batch that only read holds no lock) or waits");
        const bool wrote = db.updateAssetAsset(QStringLiteral("t2"), QByteArrayLiteral("{\"written\":1}"));
        check(wrote, "2: the batch's write succeeds after a preview was written beside it");
        drops.clear();
        bool landed3 = false;
        check(previewLandsOrWaits(writer, QStringLiteral("p3"), QStringLiteral("00000000000000p3"), drops, &landed3),
              "2: a preview submitted after the batch's write settles");
        check(!landed3, "2: ...it WAITS: the batch holds the lock from its first write");
        check(db.endBatch(), "2: the batch commits");
        check(db.fetchAssetData(QStringLiteral("t2")) == QByteArrayLiteral("{\"written\":1}"), "2: the batch's write is stored");
        check(waitFor([&] { return storedKey(QStringLiteral("p3")) == QStringLiteral("00000000000000p3"); }),
              "2: the waiting preview lands once the batch ends");
    }

    // 3. The import claim's statement sequence: live reads (the negative control), then scoped ones.
    {
        bool landed = false;
        db::Error e;
        // LIVE: each stepped ONCE and left un-reset (a statement holds its snapshot until reset or finalized).
        db::Statement any(db.connection(), "SELECT COUNT(*) FROM assets WHERE listed = 0");
        any.step(&e);
        db::Statement match(db.connection(), "SELECT guid FROM assets WHERE listed = 0 LIMIT 1");
        match.step(&e);
        check(previewLandsOrWaits(writer, QStringLiteral("p4"), QStringLiteral("00000000000000p4"), drops, &landed) && landed,
              "3: a preview lands while the claim's two reads are live");
        const bool liveWrote = db.setAssetListed(QStringLiteral("gone"), true);
        check(!liveWrote, "3: NEGATIVE CONTROL - with the reads LIVE the write fails at once (517): the hazard is real");
        any.reset();
        match.reset();
        QString guid;
        {
            testsql::integer(db.connection(), QStringLiteral("SELECT COUNT(*) FROM assets WHERE listed = 0"));
            guid = testsql::text(db.connection(), QStringLiteral("SELECT guid FROM assets WHERE listed = 0 LIMIT 1"));
        }
        check(previewLandsOrWaits(writer, QStringLiteral("p4"), QStringLiteral("00000000000000p6"), drops, &landed) && landed,
              "3: a preview lands after the scoped reads");
        check(guid == QStringLiteral("gone") && db.setAssetListed(guid, true), "3: with the reads SCOPED the write succeeds");
    }

    // 4. A stale live read: the transaction behind it fails loudly.
    {
        bool landed = false;
        db::Error staleError;
        db::Statement stale(db.connection(), "SELECT name FROM assets WHERE guid = 'u1' LIMIT 1");
        stale.step(&staleError);   // LIVE: stepped once, not reset
        check(previewLandsOrWaits(writer, QStringLiteral("p5"), QStringLiteral("00000000000000p5"), drops, &landed) && landed,
              "4: a preview lands while a read is live");
        warnings.clear();
        const QByteArray before = libraryDigest(path);
        bool committed = true, funnel = true, raw = true, nested = true;
        {
            DbTransaction tx(db.connection());
            funnel = db.updateAssetAsset(QStringLiteral("t4"), QByteArrayLiteral("{\"half\":1}"));
            raw = testsql::exec(db.connection(), QStringLiteral("UPDATE assets SET name = 'behind-stale' WHERE guid = 't4'"));
            {
                DbTransaction inner(db.connection());
                nested = testsql::exec(db.connection(), QStringLiteral("UPDATE assets SET name = 'nested' WHERE guid = 'u1'"))
                         && inner.commit();
            }
            committed = tx.commit();
        }
        stale.reset();
        const bool named = !warnings.filter(QStringLiteral("could NOT BEGIN")).isEmpty()
                           && !warnings.filter(QStringLiteral("stale snapshot")).isEmpty();
        for (const QString &w : warnings) std::printf("     4: warned: %s\n", qUtf8Printable(w));
        check(!committed, "4: the transaction behind a stale live read answers commit() FALSE");
        check(!funnel && !raw && !nested,
              "4: every write in its scope is REFUSED - Database's funnel, a raw exec, a nested guard");
        check(libraryDigest(path) == before, "4: A FAILED UNIT WRITES NOTHING - every table is byte-equal after it");
        bool next = false;
        {
            DbTransaction tx(db.connection());
            next = testsql::exec(db.connection(), QStringLiteral("UPDATE assets SET name = 'after' WHERE guid = 't4'"))
                   && tx.commit();
        }
        check(next && nameOf(QStringLiteral("t4")) == QStringLiteral("after"),
              "4: the next transaction on the connection works (the refusal ended with its scope)");
        check(named, "4: ...and the failure is logged by name (a read left active holds a stale snapshot)");
    }

    // 5. A BEGIN that times out behind another writer: nothing written, even after it lets go.
    {
        testsql::exec(db.connection(), QStringLiteral("PRAGMA busy_timeout = 50"));
        db::Connection other = testsql::open(path, "other");
        check(testsql::exec(other, QStringLiteral("BEGIN IMMEDIATE")), "5: another connection holds the write lock");
        const QByteArray before = libraryDigest(path);
        bool committed = true, funnel = true, raw = true;
        warnings.clear();
        {
            DbTransaction tx(db.connection());   // its BEGIN times out at 50 ms
            testsql::exec(other, QStringLiteral("COMMIT"));   // the lock is free before the scope writes
            funnel = db.updateAssetAsset(QStringLiteral("t4"), QByteArrayLiteral("{\"half\":2}"));
            raw = testsql::exec(db.connection(), QStringLiteral("UPDATE assets SET name = 'half-a-unit' WHERE guid = 't4'"));
            committed = tx.commit();
        }
        check(!warnings.filter(QStringLiteral("could NOT BEGIN")).isEmpty(), "5: the BEGIN failed (timed out), by name");
        check(!committed && !funnel && !raw, "5: every write of the scope is refused although the lock is free");
        check(libraryDigest(path) == before, "5: A FAILED UNIT WRITES NOTHING - every table is byte-equal after it");
        other = db::Connection();
        testsql::exec(db.connection(), QStringLiteral("PRAGMA busy_timeout = 5000"));
        bool next = false;
        {
            DbTransaction tx(db.connection());
            next = testsql::exec(db.connection(), QStringLiteral("UPDATE assets SET name = 'after-5' WHERE guid = 't4'"))
                   && tx.commit();
        }
        check(next && nameOf(QStringLiteral("t4")) == QStringLiteral("after-5"), "5: the next transaction works");
    }

    qInstallMessageHandler(nullptr);
    db.closeDatabase();
    std::printf("previews.ui_writes: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
