/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// bundle.writer — THE SHARE FILE IS WRITTEN OFF THE UI THREAD, AND IT IS THE SAME FILE
// (EXPORT-THREAD-1).
//
// assetshare's export is two halves (services/bundlewriter.h): the UI thread STAGES the
// closure (rows, edges, store paths — queries), a worker WRITES it (reads the owed bytes,
// serialises the envelope, zips, renames into place). This suite drives the writer with a
// staged envelope over real files — no database, no engine — and asserts:
//
//   1. the worker's archive holds the SAME BYTES as the calling thread's, entry for entry
//      (the zip container itself carries per-entry wall-clock times, so the comparison is
//      the extracted manifest and payload — what an import reads);
//   2. the worker really is another thread, and the calling thread's event loop TURNED while
//      it wrote: a 1 ms timer on the calling thread ticks during BundleExport::wait (counted,
//      never timed);
//   3. a cancelled write leaves nothing at the destination — and a file already there
//      survives it (the write is beside, then renamed over);
//   4. the owed bytes land inline: the payload carries every file's content.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdio>
#include <limits>

#include "io/clipboardformat.h"
#include "io/ziphelper.h"
#include "services/bundlewriter.h"

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("ok:   %s\n", msg); \
    else { std::printf("FAIL: %s\n", msg); ++failures; } } while (0)

namespace {

/// `count` store objects of `size` bytes each, every one different.
QStringList writeObjects(const QString &dir, int count, int size)
{
    QStringList paths;
    for (int i = 0; i < count; ++i) {
        QByteArray bytes(size, '\0');
        quint32 x = 2463534242u + quint32(i) * 7919u;
        for (int b = 0; b < size; ++b) {    // xorshift: incompressible, reproducible
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            bytes[b] = char(x & 0xff);
        }
        const QString path = QDir(dir).filePath(QStringLiteral("obj%1.png").arg(i));
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) f.write(bytes);
        paths << path;
    }
    return paths;
}

/// A staged pack of `paths.size()` texture rows, every file owed.
assetshare::BundleStage stagePack(const QStringList &paths)
{
    assetshare::BundleStage stage;
    stage.kind = QStringLiteral("pack");
    stage.envelope.version = clipboardformat::kVersion;
    stage.envelope.created = QStringLiteral("2026-09-29T12:00:00Z");
    for (int i = 0; i < paths.size(); ++i) {
        clipboardformat::ClipAsset asset;
        asset.guid = QStringLiteral("{00000000-0000-0000-0000-%1}").arg(i, 12, 10, QLatin1Char('0'));
        asset.name = QStringLiteral("tex%1.png").arg(i);
        asset.type = QStringLiteral("texture");
        asset.typeId = 2;
        clipboardformat::ClipFile file;
        file.role = QStringLiteral("source");
        file.name = asset.name;
        file.ext = QStringLiteral("png");
        file.size = QFileInfo(paths.at(i)).size();
        file.oid = QStringLiteral("%1").arg(i, 64, 16, QLatin1Char('0'));
        asset.files.append(file);
        stage.envelope.assets.insert(asset.guid, asset);
        stage.reads.append(assetclosure::DeferredRead{ asset.guid, 0, paths.at(i) });
        clipboardformat::ClipItem item;
        item.kind = QLatin1String(clipboardformat::kind::asset());
        item.data = QJsonObject{ { QStringLiteral("guid"), asset.guid } };
        stage.envelope.items.append(item);
    }
    return stage;
}

QByteArray entryBytes(const QString &zip, const QString &entry)
{
    QTemporaryDir out;
    if (!ZipHelper::extract(zip, out.path())) return QByteArray();
    QFile f(QDir(out.path()).filePath(entry));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}   // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir work;
    CHECK(work.isValid(), "a scratch directory");
    const QString objects = QDir(work.path()).filePath(QStringLiteral("store"));
    QDir().mkpath(objects);
    // 200 entries (the pack the brief names) of 256 KiB: ~50 MiB of owed bytes, enough
    // that the write is many event-loop turns long on any box.
    const QStringList paths = writeObjects(objects, 200, 256 * 1024);
    const assetshare::BundleStage stage = stagePack(paths);

    // ---- 1 + 4: the calling thread's file, then the worker's --------------------
    const QString inlinePath = QDir(work.path()).filePath(QStringLiteral("inline.jbundle"));
    const assetshare::ExportResult inlineResult = assetshare::writeBundle(stage, inlinePath);
    CHECK(inlineResult.ok(), "the writer writes on the calling thread");
    CHECK(!inlineResult.offUiThread, "...and says so (offUiThread false)");
    CHECK(inlineResult.assets == 200 && inlineResult.kind == QLatin1String("pack"),
          "200 entries, kind pack");

    const QString workerPath = QDir(work.path()).filePath(QStringLiteral("worker.jbundle"));
    int ticks = 0;
    QTimer tick;
    QObject::connect(&tick, &QTimer::timeout, [&ticks]() { ++ticks; });
    tick.start(1);
    assetshare::BundleExport job(stage, workerPath);
    job.start();
    const assetshare::ExportResult workerResult = job.wait();
    tick.stop();
    std::printf("info: the calling thread's 1 ms timer ticked %d time(s) while the worker "
                "wrote %lld bytes\n", ticks, static_cast<long long>(workerResult.bytes));
    CHECK(workerResult.ok(), "the worker writes the same stage");
    CHECK(workerResult.offUiThread, "...on a thread that is not the application's");
    CHECK(ticks > 0, "the calling thread's event loop turned while the worker wrote");
    CHECK(job.done() == job.total() && job.total() == 202,
          "the progress counters reached the end (200 files + payload + archive)");

    for (const char *entry : { "jah.manifest.json", "payload.json" }) {
        const QByteArray a = entryBytes(inlinePath, QString::fromLatin1(entry));
        const QByteArray b = entryBytes(workerPath, QString::fromLatin1(entry));
        std::printf("info: %s: %lld bytes inline, %lld from the worker\n", entry,
                    static_cast<long long>(a.size()), static_cast<long long>(b.size()));
        CHECK(!a.isEmpty() && a == b, qPrintable(QStringLiteral("%1 is byte-identical on both threads")
                                                    .arg(QString::fromLatin1(entry))));
    }
    {
        QString error;
        const clipboardformat::Envelope back = clipboardformat::Envelope::fromText(
            entryBytes(workerPath, QStringLiteral("payload.json")), &error,
            std::numeric_limits<qint64>::max());   // a share file's payload is not the clipboard's
        std::printf("info: the worker's payload parses: %s (%lld assets)\n",
                    error.isEmpty() ? "yes" : qPrintable(error),
                    static_cast<long long>(back.assets.size()));
        int carried = 0;
        for (int i = 0; i < paths.size(); ++i) {
            QFile f(paths.at(i));
            f.open(QIODevice::ReadOnly);
            const auto asset = back.assets.value(stage.reads.at(i).guid);
            if (!asset.files.isEmpty() && asset.files.first().inlineData == f.readAll()) ++carried;
        }
        std::printf("info: %d of %lld files carried inline, byte for byte\n", carried,
                    static_cast<long long>(paths.size()));
        CHECK(carried == paths.size(), "every owed file travels inline, byte for byte");
    }

    // ---- 3: a cancelled write leaves the destination as it was -------------------
    const QString keep = QDir(work.path()).filePath(QStringLiteral("keep.jbundle"));
    {
        QFile f(keep);
        f.open(QIODevice::WriteOnly);
        f.write("the user's older file");
    }
    const assetshare::ExportResult stopped = assetshare::writeBundle(
        stage, keep, [](int done, int) { return done < 10; });
    CHECK(stopped.canceled && !stopped.ok(), "a write stopped by its progress hook is cancelled");
    {
        QFile f(keep);
        f.open(QIODevice::ReadOnly);
        CHECK(f.readAll() == QByteArray("the user's older file"),
              "the file already at the destination survives a cancelled write");
    }
    CHECK(!QFile::exists(keep + QStringLiteral(".partial")), "no .partial is left behind");

    const QString fresh = QDir(work.path()).filePath(QStringLiteral("fresh.jbundle"));
    assetshare::BundleExport cancelled(stage, fresh);
    cancelled.cancel();       // before the first step: the worker stops at once
    cancelled.start();
    const assetshare::ExportResult c = cancelled.wait();
    CHECK(c.canceled && !QFile::exists(fresh) && !QFile::exists(fresh + QStringLiteral(".partial")),
          "a cancelled job writes nothing at a fresh destination");

    std::printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
