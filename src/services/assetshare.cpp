/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/assetshare.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QHash>
#include <QtConcurrent/QtConcurrentRun>

#include <limits>

#include "data/constants.h"
#include "data/database/database.h"
#include "data/project.h"
#include "io/clipboardformat.h"
#include "zip.h"
#include "scripting/modules/moduleshared.h"
#include "services/assetcas.h"
#include "services/assetclosure.h"
#include "io/assetrefs.h"
#include "irisgl/core/irisutils.h"
#include "services/assetstorepaths.h"
#include "services/clipboardresolver.h"
#include "services/meshbakestore.h"

namespace {

QString manifestName() { return QStringLiteral("jah.manifest.json"); }
QString payloadName()  { return QStringLiteral("payload.json"); }

}   // namespace

namespace assetshare {

QString fileFilter()
{
    return QStringLiteral("Jahshaka asset bundle (*.%1)").arg(QLatin1String(extension()));
}

void yieldToEventLoop()
{
    // Frames, repaints, timers; never user input — nothing a click could start
    // re-enters an export that is staging.
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

// assets.exportBundle MEASURES ITS OWN BLOCKS (MAIN-REDS-1 stage 5): every stretch of the export's own work on
// this thread between two event-loop turns, from the verb's entry to its return (UiStepClock, bundlewriter.h) —
// the verb returns it as worstUiStepMs.
ExportResult exportBundle(Database *db, Project *project, const QStringList &guids,
                          const QString &destPath)
{
    UiStepClock clock;
    BundleStage stage = stageBundle(db, project, guids, [&clock] { clock.turn(yieldToEventLoop); });
    return exportAndWait(std::move(stage), destPath, &clock);
}

ExportResult exportNode(Database *db, Project *project, const QJsonObject &nodeObject,
                        const QString &name, int typeId, const QString &destPath)
{
    return exportAndWait(stageNode(db, project, nodeObject, name, typeId, yieldToEventLoop), destPath);
}

bool looksLikeBundle(const QString &path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) return false;
    if (QFileInfo(path).suffix().compare(QLatin1String(extension()), Qt::CaseInsensitive) == 0)
        return true;

    // A zip that carries our two files IS one, whatever it is called — the
    // same tolerance every other reader here has for a renamed file. IT IS
    // READ FROM THE CENTRAL DIRECTORY, never extracted (fix round F9): this
    // runs on EVERY `assets.import`, including one aimed at a 2 GB model, and
    // it used to unpack whatever it was given into a QTemporaryDir — which on
    // this box is RAM (the 2026-09-08 crash was 26 GB of scratch in tmpfs).
    // The importers' size caps are downstream of this call and could not
    // help. Names and sizes only: nothing is decompressed.
    struct zip_t *zip = zip_open(path.toUtf8().constData(), 0, 'r');
    if (!zip) return false;
    bool haveManifest = false, havePayload = false;
    const ssize_t total = zip_entries_total(zip);
    for (ssize_t i = 0; i < total && !(haveManifest && havePayload); ++i) {
        if (zip_entry_openbyindex(zip, static_cast<size_t>(i)) != 0) continue;
        const char *name = zip_entry_name(zip);
        if (name) {
            const QString entry = QString::fromUtf8(name);
            if (entry == manifestName()) haveManifest = true;
            else if (entry == payloadName()) havePayload = true;
        }
        zip_entry_close(zip);
    }
    zip_close(zip);
    return haveManifest && havePayload;
}


ImportResult importBundle(Database *db, Project *project, const QString &path)
{
    ImportResult result;
    const auto fail = [&result](const QString &why) { result.error = why; return result; };
    if (!db) return fail(QStringLiteral("no library is open"));
    if (!QFileInfo::exists(path)) return fail(QStringLiteral("no such file '%1'").arg(path));

    // THE UNPACK IS A WORKER'S (EXPORT-THREAD-1): the extract and the parse of
    // an unbounded payload are file I/O, and the calling (UI) thread keeps its
    // event loop — frames, repaints, timers; user input held back — until the
    // envelope is in hand. The resolver below is the catalog's, on this thread.
    QFuture<UnpackedBundle> future = QtConcurrent::run([path]() { return unpackBundle(path); });
    if (!future.isFinished() && QCoreApplication::instance()) {
        QEventLoop loop;
        QFutureWatcher<UnpackedBundle> watcher;
        QObject::connect(&watcher, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
        watcher.setFuture(future);
        if (!future.isFinished()) loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    future.waitForFinished();
    const UnpackedBundle unpacked = future.result();
    if (!unpacked.error.isEmpty()) return fail(unpacked.error);
    const clipboardformat::Envelope &envelope = unpacked.envelope;

    // THE EXISTING IMPORT (spec §5): ingest by content, register the rows
    // with their blobs, write the intrinsic edges, pin into the open project.
    ClipboardResolver resolver(db, project);
    const ClipboardResolveReport report = resolver.apply(envelope);
    if (!report.error.isEmpty()) return fail(report.error);

    // THE IMPORT BAKES WHAT IT BRINGS (TC2-BUNDLE-NO-BAKE): a model row the file landed has its
    // source in the store and no bake — the bake is derived data, never carried — and the scene
    // only ever reads bakes. Started here on a worker and committed when it lands
    // (STAGE-CLOSE-TRIAGE-1 F3: it used to hold this thread's input for the whole bake); a row
    // placed before then shows its geometry when the bake lands, and a failure is logged by name.
    // (In a library that already held a bake of the same content under another row this finds
    // it fresh and builds nothing — which is how a pool hid the miss.)
    MeshBakeStore::bakeLandedAsync(report.imported);

    // EVERY ENTRY the file is about (a pack names several), in its order.
    for (const clipboardformat::ClipItem &item : envelope.items) {
        const QString wanted = item.data.value(QStringLiteral("guid")).toString();
        const QString landed = report.guidMap.value(wanted, wanted);
        if (landed.isEmpty() || db->fetchAsset(landed).guid.isEmpty()) {
            QString why = QStringLiteral("the asset could not be landed");
            if (!report.missing.isEmpty())
                why = QStringLiteral("the archive is missing content: %1")
                          .arg(report.missing.first().name);
            return fail(why);
        }
        result.guids.append(landed);
    }

    result.guid = result.guids.first();
    result.imported = report.imported;
    result.known = report.known;
    result.alreadyHad = report.known.contains(result.guid) && !report.imported.contains(result.guid);
    return result;
}

}   // namespace assetshare
