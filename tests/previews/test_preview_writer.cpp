/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// previews.writer — THE ONE PREVIEW WRITER (PREVIEWS-1 stage 1; services/previewwriter.h): a worker
// with its own connection to a TEMP library (the real Database, its own file), no engine, no display.
//
//   a. bytes AND key land in ONE transaction: `assets.thumbnail` = the PNG of the image submitted,
//      the `thumbnails` row {guid, hash = key, last_written}; the library's thumbnail hook is told;
//      a write for a guid with no asset row fails with its reason and leaves no `thumbnails` row;
//   b. a NULL image is refused at submit (nothing queued, no completion) and an EMPTY PNG at the
//      write: both leave bytes and key unchanged. NEGATIVE CONTROL: the unguarded writer
//      (Database::updateAssetThumbnail with empty bytes, the base's erase-on-fail path) BLANKS them;
//   c. a write submitted while the UI connection holds a write transaction RETURNS AT ONCE (submit
//      returns on the very thread holding the lock), has not landed, and lands after the UI commits;
//   d. two writes for one guid complete in SUBMIT ORDER, the second stored (the first landed, or was
//      superseded while it waited);
//   e. PAST ITS BUDGET (a short one here; the product's is Writer::kBudgetMs) a write is DROPPED, the
//      drop hook told, RE-QUEUED AT THE HEAD (the lead's ruling A5): (i) QUIET - however many drops,
//      ONE log line when it first waits and ONE when it lands, its tries counted; (ii) two locked-out
//      writes of two guids complete in submit order; (iii) BOUNDED PER GUID - a newer submit of a
//      waiting guid supersedes it (its completion says so) and the newer is stored;
//   f. THE UI CONNECTION's busy timeout, read (PRAGMA busy_timeout; spec UNVERIFIED): a UI write that
//      meets the writer WAITS rather than failing, and the writer is one row a transaction, so a UI
//      write submitted while 20 previews drain lands at once, unrefused;
//   g. the writer's end DRAINS: a write submitted just before its destruction is stored;
//   h. THE LOCK IS HELD FOR THE ROW ONLY, read as an ORDER: the writer stamps one serial when a picture's
//      encode ends and when its transaction begins; each write's encode is before its BEGIN (an encode
//      moved inside the transaction reverses them - red by construction, no race).
//   i. THE IN-PLACE EDIT RACE, DRIVEN (the lead's ruling: the blob's stamp is a KEY INPUT, never a mark): a
//      preview whose key was taken BEFORE Database::updateAssetAsset rewrote an Object's blob, and stored
//      AFTER it (the writer waiting behind the UI's edit transaction), reads STALE: the key computed now
//      (previews::sourceOf the catalog row, as PreviewService::state) differs from the stored one. NEGATIVE
//      CONTROL: the key without the stamp (stage 1's) does not move across the edit - that row would read ok
//      with the picture of the blob it no longer holds. i2. the SIDECAR REBUILD (AssetMigration::rebuildCatalog)
//      writes the stamp of the blob it restores, like every other writer of the blob.
//   j. THE WRITER's STATEMENTS (the query log's "WritePreview" entries): one landed write is exactly four -
//      BEGIN IMMEDIATE, UPDATE, INSERT OR REPLACE, COMMIT - once each; a read beside it is counted (the control).
// The waits are on PUBLISHED state (a completion, a drop) with a cap that names the failure.

#include <algorithm>
#include <cstdio>
#include <functional>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QImage>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QRandomGenerator>
#include <QVariant>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "data/database/database.h"
#include "services/assetmigration.h"
#include "services/previewkey.h"
#include "services/previewwriter.h"
#include "services/jahlog.h"

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

/// Pumps the event loop until `done` holds; false (named by the caller) past a 20 s cap.
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

void addAsset(const QString &guid, const QByteArray &thumbnail)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("INSERT INTO assets (guid, type, name, thumbnail) VALUES (?, 1, ?, ?)"));
    q.addBindValue(guid);
    q.addBindValue(guid);
    q.addBindValue(thumbnail);
    q.exec();
}

QByteArray storedBytes(const QString &guid)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT thumbnail FROM assets WHERE guid = ?"));
    q.addBindValue(guid);
    return q.exec() && q.next() ? q.value(0).toByteArray() : QByteArray();
}

QString storedKey(const QString &guid, bool *hasRow = nullptr, bool *stamped = nullptr)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT hash, last_written FROM thumbnails WHERE guid = ?"));
    q.addBindValue(guid);
    const bool row = q.exec() && q.next();
    if (hasRow) *hasRow = row;
    if (stamped) *stamped = row && !q.value(1).isNull();
    return row ? q.value(0).toString() : QString();
}

QImage solid(QRgb colour)
{
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(colour);
    return image;
}

bool decodesTo(const QByteArray &png, QRgb colour)
{
    QImage image;
    return image.loadFromData(png, "PNG") && image.size() == QSize(8, 8)
           && image.pixel(4, 4) == (colour | 0xff000000u);
}

struct Probe
{
    QStringList landedKeys;
    QVector<previews::Writer::Landed> landed;
    previews::Writer::Done done()
    {
        return [this](const previews::Writer::Landed &l) { landed.append(l); landedKeys.append(l.key); };
    }
};

}   // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("JahLibrary.db"));
    Database db;
    check(db.initializeDatabase(path), "a temp library opens");
    db.createAllTables();

    QStringList announced;
    Database::setAssetThumbnailWritten([&announced](const QString &guid) { announced.append(guid); });

    const QByteArray old = QByteArrayLiteral("OLD-TILE-BYTES");
    for (const char *g : { "a", "b", "c", "d", "e", "e2", "f", "g", "h" }) addAsset(QString::fromLatin1(g), old);

    // f, first: the UI connection's busy timeout (Qt's QSQLITE default when no option is given).
    {
        QSqlQuery q;
        const int uiTimeout = q.exec(QStringLiteral("PRAGMA busy_timeout")) && q.next() ? q.value(0).toInt() : -1;
        std::printf("     the UI connection's busy timeout: %d ms (the writer's budget %d ms)\n", uiTimeout,
                    previews::Writer::kBudgetMs);
        check(uiTimeout == 5000, "f: the UI connection waits 5000 ms for a lock (Qt's QSQLITE default), never fails at once");
    }

    {
        previews::Writer writer(path);

        // a. One transaction, both halves, the hook told.
        Probe pa;
        check(writer.submit(QStringLiteral("a"), solid(qRgb(255, 0, 0)), QStringLiteral("00000000000000aa"), pa.done()),
              "a: submit accepts an image");
        check(waitFor([&] { return pa.landed.size() == 1; }), "a: the write completes");
        bool row = false, stamped = false;
        check(pa.landed.value(0).ok && pa.landed.value(0).drops == 0, "a: it landed, no drop");
        check(decodesTo(storedBytes(QStringLiteral("a")), qRgb(255, 0, 0)), "a: assets.thumbnail is the PNG of the image");
        check(storedKey(QStringLiteral("a"), &row, &stamped) == QStringLiteral("00000000000000aa") && stamped,
              "a: the thumbnails row holds the key and its last_written");
        check(announced.contains(QStringLiteral("a")), "a: the library's thumbnail hook is told (on the app's thread)");

        Probe pg;
        writer.submit(QStringLiteral("ghost"), solid(qRgb(1, 2, 3)), QStringLiteral("0000000000000001"), pg.done());
        check(waitFor([&] { return pg.landed.size() == 1; }), "a: a write for a missing row completes");
        storedKey(QStringLiteral("ghost"), &row);
        check(!pg.landed.value(0).ok && pg.landed.value(0).reason.contains(QStringLiteral("no asset")) && !row,
              "a: a missing row fails by name and leaves no thumbnails row (rolled back)");

        // b. Null image, empty PNG: nothing changes. The unguarded writer: the negative control.
        Probe pb;
        QString why;
        check(!writer.submit(QStringLiteral("a"), QImage(), QStringLiteral("00000000000000bb"), pb.done(), &why)
                  && !why.isEmpty(), "b: a null image is refused at submit, with its reason");
        const Database::PreviewWrite empty =
            Database::writePreview(QSqlDatabase::database(), QStringLiteral("a"), QByteArray(), QStringLiteral("00000000000000cc"));
        check(!empty.ok && !empty.reason.isEmpty(), "b: an empty PNG is refused at the write, with its reason");
        QCoreApplication::processEvents();
        check(pb.landed.isEmpty(), "b: the refused submit has no completion");
        check(decodesTo(storedBytes(QStringLiteral("a")), qRgb(255, 0, 0))
                  && storedKey(QStringLiteral("a")) == QStringLiteral("00000000000000aa"),
              "b: bytes and key are unchanged by both");
        db.updateAssetThumbnail(QStringLiteral("b"), QByteArray());
        check(storedBytes(QStringLiteral("b")).isEmpty(),
              "b: NEGATIVE CONTROL - the unguarded writer (updateAssetThumbnail, empty bytes) blanks the tile");

        // c. Submitted while the UI holds the write lock.
        QSqlQuery ui;
        check(ui.exec(QStringLiteral("BEGIN IMMEDIATE")), "c: the UI connection takes the write lock");
        ui.exec(QStringLiteral("UPDATE assets SET name = 'held' WHERE guid = 'c'"));
        Probe pc;
        const bool returned = writer.submit(QStringLiteral("c"), solid(qRgb(0, 255, 0)), QStringLiteral("00000000000000c1"), pc.done());
        check(returned, "c: submit RETURNS on the thread that holds the lock");
        QCoreApplication::processEvents();
        check(pc.landed.isEmpty() && storedKey(QStringLiteral("c")).isEmpty(), "c: ...and has not landed");
        check(ui.exec(QStringLiteral("COMMIT")), "c: the UI commits");
        check(waitFor([&] { return pc.landed.size() == 1; }), "c: the write completes after it");
        QString heldName;
        {
            // FINISHED once read: an active SELECT holds a WAL read snapshot on the UI connection, and a
            // write from a connection whose snapshot the writer has since moved past fails AT ONCE with
            // SQLITE_BUSY_SNAPSHOT (517) - no busy wait (measured here, the row's first run).
            QSqlQuery name;
            if (name.exec(QStringLiteral("SELECT name FROM assets WHERE guid = 'c'")) && name.next())
                heldName = name.value(0).toString();
        }
        check(pc.landed.value(0).ok && storedKey(QStringLiteral("c")) == QStringLiteral("00000000000000c1")
                  && heldName == QStringLiteral("held"),
              "c: it landed after the UI's write, both present");

        // d. Submit order for one guid.
        Probe pd;
        writer.submit(QStringLiteral("d"), solid(qRgb(0, 0, 255)), QStringLiteral("00000000000000d1"), pd.done());
        writer.submit(QStringLiteral("d"), solid(qRgb(255, 255, 0)), QStringLiteral("00000000000000d2"), pd.done());
        check(waitFor([&] { return pd.landed.size() == 2; }), "d: both writes complete");
        check(pd.landedKeys == QStringList({ QStringLiteral("00000000000000d1"), QStringLiteral("00000000000000d2") }),
              "d: they complete in submit order");
        check(pd.landed.value(0).ok || pd.landed.value(0).reason.contains(QStringLiteral("superseded")),
              "d: the first landed, or was superseded by name while it waited");
        check(storedKey(QStringLiteral("d")) == QStringLiteral("00000000000000d2")
                  && decodesTo(storedBytes(QStringLiteral("d")), qRgb(255, 255, 0)),
              "d: the second is stored, bytes and key together");

        // f. A UI write while 20 previews drain is never refused.
        Probe pf;
        for (int i = 0; i < 20; ++i)
            writer.submit(QStringLiteral("f"), solid(qRgb(i, i, i)), QStringLiteral("00000000000000%1").arg(i, 2, 10, QLatin1Char('0')), pf.done());
        QSqlQuery uiWrite;
        const bool uiWrote = uiWrite.exec(QStringLiteral("UPDATE assets SET name = 'ui-during-drain' WHERE guid = 'g'"));
        if (!uiWrote) std::printf("     the UI write's error: %s (native %s)\n", qUtf8Printable(uiWrite.lastError().text()),
                                  qUtf8Printable(uiWrite.lastError().nativeErrorCode()));
        check(uiWrote,
              "f: a UI write while 20 previews drain lands, unrefused");
        check(waitFor([&] { return pf.landed.size() == 20; }), "f: the 20 complete");
    }

    // e. Past the budget: dropped, told, re-queued at the head - quiet, in order, bounded per guid.
    {
        const int budgetMs = 100;
        previews::Writer writer(path, budgetMs);
        QStringList drops;
        writer.setDropHook([&drops](const QString &guid, const QString &reason) { drops.append(guid + QLatin1Char(':') + reason); });
        // THE WRITER's LINES, read from the session log's ring after the mark (JahLog::since: written on the writer's
        // thread under the log's own lock).
        const quint64 linesFrom = JahLog::mark(QStringLiteral("previews.writer: case e"));
        const auto previewLines = [linesFrom] { return JahLog::since(linesFrom).filter(QStringLiteral("legacy: previews:")); };

        // (i) one write, four drops at least: two lines.
        QSqlQuery ui;
        check(ui.exec(QStringLiteral("BEGIN IMMEDIATE")), "e: the UI holds the lock");
        Probe pe;
        writer.submit(QStringLiteral("e"), solid(qRgb(10, 20, 30)), QStringLiteral("00000000000000e1"), pe.done());
        check(waitFor([&] { return drops.size() >= 4; }), "e: past its budget the write is DROPPED, again and again, each told");
        check(drops.value(0).startsWith(QStringLiteral("e:")) && drops.value(0).contains(QStringLiteral("budget")),
              "e: the drop names its guid and its reason (the budget)");
        check(pe.landed.isEmpty(), "e: nothing landed while locked out");
        check(ui.exec(QStringLiteral("COMMIT")), "e: the UI lets go");
        check(waitFor([&] { return pe.landed.size() == 1; }), "e: the re-queued write completes");
        check(pe.landed.value(0).ok && pe.landed.value(0).drops >= 4 && storedKey(QStringLiteral("e")) == QStringLiteral("00000000000000e1"),
              "e: it landed, its drops counted");
        {
            const QStringList lines = previewLines();   // the writer is idle now: read the lines
            const int waits = int(std::count_if(lines.begin(), lines.end(), [](const QString &l) { return l.contains(QStringLiteral("waits behind")); }));
            const int landedLines = int(std::count_if(lines.begin(), lines.end(), [](const QString &l) { return l.contains(QStringLiteral("landed after")); }));
            std::printf("     %d drops -> %d 'waits' line(s), %d 'landed' line(s)\n", int(drops.size()), waits, landedLines);
            check(waits == 1 && landedLines == 1, "e: QUIET - one line when it first waits, one when it lands, whatever the drops");
            check(lines.filter(QStringLiteral("landed after")).value(0).contains(QStringLiteral("after %1 tries").arg(pe.landed.value(0).drops + 1)),
                  "e: the landing line counts its tries");
        }

        // (ii) two guids, both locked out: submit order.
        drops.clear();
        check(ui.exec(QStringLiteral("BEGIN IMMEDIATE")), "e: the UI holds the lock again");
        Probe pboth;
        writer.submit(QStringLiteral("e"), solid(qRgb(1, 1, 1)), QStringLiteral("00000000000000e2"), pboth.done());
        writer.submit(QStringLiteral("e2"), solid(qRgb(2, 2, 2)), QStringLiteral("00000000000000f2"), pboth.done());
        check(waitFor([&] { return !drops.isEmpty(); }), "e: the first is dropped");
        check(ui.exec(QStringLiteral("COMMIT")), "e: the UI lets go");
        check(waitFor([&] { return pboth.landed.size() == 2; }), "e: both complete");
        check(pboth.landedKeys == QStringList({ QStringLiteral("00000000000000e2"), QStringLiteral("00000000000000f2") }),
              "e: re-queued at the HEAD - two guids complete in submit order");

        // (iii) one guid twice while locked out: the newer supersedes the waiting one.
        drops.clear();
        check(ui.exec(QStringLiteral("BEGIN IMMEDIATE")), "e: the UI holds the lock a third time");
        Probe pnew;
        writer.submit(QStringLiteral("e"), solid(qRgb(3, 3, 3)), QStringLiteral("00000000000000e3"), pnew.done());
        check(waitFor([&] { return !drops.isEmpty(); }), "e: the older is waiting (dropped once)");
        writer.submit(QStringLiteral("e"), solid(qRgb(4, 4, 4)), QStringLiteral("00000000000000e4"), pnew.done());
        check(waitFor([&] { return pnew.landed.size() == 1; }), "e: the older completes at once, superseded");
        check(!pnew.landed.value(0).ok && pnew.landed.value(0).key == QStringLiteral("00000000000000e3")
                  && pnew.landed.value(0).reason.contains(QStringLiteral("superseded")) && pnew.landed.value(0).superseded,
              "e: BOUNDED PER GUID - the waiting write is superseded by name, never a second one waiting");
        check(ui.exec(QStringLiteral("COMMIT")), "e: the UI lets go");
        check(waitFor([&] { return pnew.landed.size() == 2; }), "e: the newer completes");
        check(pnew.landed.value(1).ok && storedKey(QStringLiteral("e")) == QStringLiteral("00000000000000e4")
                  && decodesTo(storedBytes(QStringLiteral("e")), qRgb(4, 4, 4)),
              "e: the newer is stored, bytes and key");
    }

    // h. The encode runs outside the writer's transaction: an ORDER of recorded events, never a race.
    {
        previews::Writer writer(path);
        QImage noise(512, 512, QImage::Format_RGB32);
        QRandomGenerator rng(7);
        for (int y = 0; y < noise.height(); ++y) {
            QRgb *row = reinterpret_cast<QRgb *>(noise.scanLine(y));
            for (int x = 0; x < noise.width(); ++x) row[x] = rng.generate() | 0xff000000u;
        }
        Probe ph;
        writer.submit(QStringLiteral("g"), noise, QStringLiteral("00000000000000a7"), ph.done());
        writer.submit(QStringLiteral("h"), solid(qRgb(5, 5, 5)), QStringLiteral("00000000000000a8"), ph.done());
        check(waitFor([&] { return ph.landed.size() == 2; }) && ph.landed.value(0).ok && ph.landed.value(1).ok,
              "h: both writes land");
        for (const auto &l : ph.landed)
            std::printf("     h: %s encoded at #%llu, its transaction began at #%llu\n", qUtf8Printable(l.guid),
                        static_cast<unsigned long long>(l.encodedAt), static_cast<unsigned long long>(l.begunAt));
        check(ph.landed.value(0).encodedAt > 0 && ph.landed.value(0).encodedAt < ph.landed.value(0).begunAt
                  && ph.landed.value(1).encodedAt < ph.landed.value(1).begunAt,
              "h: THE LOCK FOR THE ROW ONLY - each picture's encode ENDED before its transaction BEGAN");

        // i. The in-place edit race.
        const QString race = QStringLiteral("race");
        {
            QSqlQuery q;
            q.prepare(QStringLiteral("INSERT INTO assets (guid, type, name) VALUES (?, ?, ?)"));
            q.addBindValue(race);
            q.addBindValue(int(ModelTypes::Object));
            q.addBindValue(race);
            q.exec();
        }
        check(db.updateAssetAsset(race, QByteArrayLiteral("{\"node\": \"before the edit\"}")),
              "i: an Object row's blob is written through the edit door");
        const auto keyNow = [&](bool withStamp) {
            const QVector<Database::PreviewRow> rows = db.fetchPreviewRows(QString(), false, -1, { race });
            if (rows.isEmpty()) return QString();
            previews::KeySource source = previews::sourceOf(rows.first());
            if (!withStamp) source.blob.clear();
            return previews::key(source, 1);
        };
        const QString atRequest = keyNow(true), atRequestUnstamped = keyNow(false);
        Probe pi;
        {
            DbTransaction edit(db.connection());
            check(edit.isActive(), "i: the UI's edit takes the write lock");
            check(writer.submit(race, solid(qRgb(9, 9, 9)), atRequest, pi.done()),
                  "i: the preview drawn from the blob before the edit is submitted under it");
            QCoreApplication::processEvents();
            check(pi.landed.isEmpty(), "i: ...and waits behind the edit");
            check(db.updateAssetAsset(race, QByteArrayLiteral("{\"node\": \"after the edit\"}")),
                  "i: the edit rewrites the blob in place");
            check(edit.commit(), "i: the edit commits");
        }
        check(waitFor([&] { return pi.landed.size() == 1; }) && pi.landed.value(0).ok
                  && storedKey(race) == atRequest,
              "i: the preview landed AFTER the edit, under the key taken before it");
        check(!atRequest.isEmpty() && keyNow(true) != storedKey(race),
              "i: THE RACE: the row reads STALE (the blob's stamp moved its key), never ok with the old picture");
        check(keyNow(false) == atRequestUnstamped,
              "i: NEGATIVE CONTROL - without the stamp the key does not move across the edit (it would read ok)");
        {
            QSqlQuery q;
            q.prepare(QStringLiteral("SELECT hash FROM assets WHERE guid = ?"));
            q.addBindValue(race);
            check(q.exec() && q.next()
                      && q.value(0).toString() == Database::blobStamp(QByteArrayLiteral("{\"node\": \"after the edit\"}"))
                      && q.value(0).toString().size() == 16,
                  "i: assets.hash holds the stamp of the blob the row now has (16 hex digits)");
        }

        // i2. THE SIDECAR REBUILD WRITES THE STAMP TOO (the final read's E, leftover L1): an Object restored from
        //     its sidecar (AssetMigration::rebuildCatalog: a recovery, a Clear Database, a format bump) carries the
        //     stamp of the blob it restores in assets.hash, as every other writer of the blob does - else it keys
        //     on an empty stamp until its next in-place edit. Into a fresh catalog file (the rebuild refuses the
        //     live library).
        {
            QTemporaryDir store;
            QDir(store.path()).mkpath(QStringLiteral("sidecar"));
            const QJsonObject blob{ { QStringLiteral("node"), QStringLiteral("restored") } };
            const QJsonObject sidecar{ { QStringLiteral("formatVersion"), 3 },
                                       { QStringLiteral("guid"), QStringLiteral("rebuilt") },
                                       { QStringLiteral("name"), QStringLiteral("rebuilt") },
                                       { QStringLiteral("type"), int(ModelTypes::Object) },
                                       { QStringLiteral("home"), QStringLiteral("assets") },
                                       { QStringLiteral("origin"), QStringLiteral("import") },
                                       { QStringLiteral("asset"), blob },
                                       { QStringLiteral("files"), QJsonArray() } };
            QFile file(QDir(store.path()).filePath(QStringLiteral("sidecar/rebuilt.json")));
            const bool written = file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(sidecar).toJson()) > 0;
            file.close();
            const QString rebuiltPath = dir.filePath(QStringLiteral("Rebuilt.db"));
            const AssetMigration::RebuildReport report = AssetMigration::rebuildCatalog(rebuiltPath, store.path());
            check(written && report.error.isEmpty() && report.assets == 1, "i2: the sidecar rebuild restores the Object row");
            QString stamp;
            {
                QSqlDatabase conn = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("i2-read"));
                conn.setDatabaseName(rebuiltPath);
                QSqlQuery q(conn);
                if (conn.open() && q.exec(QStringLiteral("SELECT hash FROM assets WHERE guid = 'rebuilt'")) && q.next())
                    stamp = q.value(0).toString();
            }
            QSqlDatabase::removeDatabase(QStringLiteral("i2-read"));
            std::printf("     i2: the rebuilt row's assets.hash = '%s'\n", qUtf8Printable(stamp));
            check(stamp.size() == 16 && stamp == Database::blobStamp(QJsonDocument(blob).toJson(QJsonDocument::Compact)),
                  "i2: assets.hash holds the stamp of the blob the rebuild restored (16 hex digits)");
        }

        // j. THE WRITER's STATEMENTS, from the existing query log (its "WritePreview" entries, noted under the log's
        //    mutex on the worker): ONE landed write is exactly four - BEGIN IMMEDIATE, the thumbnail's UPDATE, the
        //    key's INSERT OR REPLACE, COMMIT - once each, and nothing else is logged while it lands. A writer that
        //    re-read the row (or read anything) would log a fifth: the instrument's own control below logs one
        //    read beside a write and must count five.
        struct Logged
        {
            bool landed = false;
            int total = 0;
            QString shape;   ///< name|first two words of the SQL|count, sorted
        };
        const auto statementsOf = [&](const std::function<void()> &beside) {
            Database::setQueryLog(true);
            Probe pj;
            writer.submit(QStringLiteral("a"), solid(qRgb(3, 4, 5)), QStringLiteral("00000000000000aj"), pj.done());
            const bool landed = waitFor([&] { return pj.landed.size() == 1; }) && pj.landed.value(0).ok;
            if (beside) beside();
            const int total = Database::queryLogStatements();
            QStringList shape;
            for (const Database::QueryLogEntry &e : Database::queryLogEntries())
                shape << QStringLiteral("%1|%2|%3").arg(e.name, e.sql.section(QLatin1Char(' '), 0, 1)).arg(e.count);
            shape.sort();
            Database::setQueryLog(false);
            return Logged{ landed, total, shape.join(QLatin1Char(',')) };
        };
        const Logged one = statementsOf({});
        std::printf("     j: one write logs %d statement(s): %s\n", one.total, qUtf8Printable(one.shape));
        check(one.landed && one.total == 4
                  && one.shape == QStringLiteral("WritePreview|BEGIN IMMEDIATE|1,WritePreview|COMMIT|1,"
                                              "WritePreview|INSERT OR|1,WritePreview|UPDATE assets|1"),
              "j: ONE landed write is FOUR statements - BEGIN IMMEDIATE, UPDATE, INSERT OR REPLACE, COMMIT - once each");
        const Logged withRead = statementsOf([&db] { db.fetchAsset(QStringLiteral("a")); });
        check(withRead.landed && withRead.total == 5, "j: CONTROL - a read of the row beside the write is counted (five)");
    }

    // g. The end drains.
    {
        previews::Writer writer(path);
        writer.submit(QStringLiteral("h"), solid(qRgb(7, 7, 7)), QStringLiteral("00000000000000h1"), {});
    }
    check(storedKey(QStringLiteral("h")) == QStringLiteral("00000000000000h1") && decodesTo(storedBytes(QStringLiteral("h")), qRgb(7, 7, 7)),
          "g: a write submitted just before the writer's end is stored (the destructor drains)");

    Database::setAssetThumbnailWritten(nullptr);
    db.closeDatabase();
    if (failures) std::printf("previews.writer: FAILED (%d)\n", failures);
    else std::printf("previews.writer: PASSED\n");
    return failures ? 1 : 0;
}
