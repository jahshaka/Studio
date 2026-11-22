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
//   d. two writes for one guid land in SUBMIT ORDER (completions in order, the second one stored);
//   e. PAST ITS BUDGET (a short one here; the product's is Writer::kBudgetMs) a write is DROPPED with
//      its reason, reported, RE-QUEUED AT THE HEAD (the lead's ruling A5): two locked-out writes for
//      one guid still land in submit order once the UI commits;
//   f. THE UI CONNECTION's busy timeout, read (PRAGMA busy_timeout; spec UNVERIFIED): a UI write that
//      meets the writer WAITS rather than failing, and the writer is one row a transaction, so a UI
//      write submitted while 20 previews drain lands at once, unrefused;
//   g. the writer's end DRAINS: a write submitted just before its destruction is stored.
// The waits are on PUBLISHED state (a completion, a drop) with a cap that names the failure.

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
#include <QVariant>

#include "data/database/database.h"
#include "services/previewwriter.h"

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
    for (const char *g : { "a", "b", "c", "d", "e", "f", "g", "h" }) addAsset(QString::fromLatin1(g), old);

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

    // e. Past the budget: dropped, said, re-queued at the head, order kept.
    {
        const int budgetMs = 100;
        previews::Writer writer(path, budgetMs);
        QStringList drops;
        writer.setDropHook([&drops](const QString &guid, const QString &reason) { drops.append(guid + QLatin1Char(':') + reason); });
        QSqlQuery ui;
        check(ui.exec(QStringLiteral("BEGIN IMMEDIATE")), "e: the UI holds the lock");
        Probe pe;
        writer.submit(QStringLiteral("e"), solid(qRgb(10, 20, 30)), QStringLiteral("00000000000000e1"), pe.done());
        writer.submit(QStringLiteral("e"), solid(qRgb(40, 50, 60)), QStringLiteral("00000000000000e2"), pe.done());
        check(waitFor([&] { return !drops.isEmpty(); }), "e: past its budget the write is DROPPED and reported");
        check(drops.value(0).startsWith(QStringLiteral("e:")) && drops.value(0).contains(QStringLiteral("budget")),
              "e: the drop names its guid and its reason (the budget)");
        check(pe.landed.isEmpty(), "e: nothing landed while locked out");
        check(ui.exec(QStringLiteral("COMMIT")), "e: the UI lets go");
        check(waitFor([&] { return pe.landed.size() == 2; }), "e: the re-queued writes complete");
        check(pe.landed.value(0).ok && pe.landed.value(0).drops >= 1, "e: the dropped write landed, its drops counted");
        check(pe.landedKeys == QStringList({ QStringLiteral("00000000000000e1"), QStringLiteral("00000000000000e2") })
                  && storedKey(QStringLiteral("e")) == QStringLiteral("00000000000000e2"),
              "e: re-queued at the HEAD - submit order kept, the second stored");
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
    std::printf("previews.writer: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
