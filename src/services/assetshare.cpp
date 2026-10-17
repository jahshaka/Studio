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
#include <QElapsedTimer>
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

namespace {

/// The envelope's header, the same for both exports.
clipboardformat::Envelope newEnvelope(Project *project)
{
    clipboardformat::Envelope envelope;
    envelope.version = clipboardformat::kVersion;
    envelope.app = Constants::CONTENT_VERSION;
    envelope.created = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    envelope.source.storeRoot = AssetStorePaths::root();
    AssetCas::readStoreInfo(AssetStorePaths::root(), &envelope.source.storeId, nullptr);
    if (project) envelope.source.projectGuid = project->getProjectGuid();
    return envelope;
}

/// THE WHOLE CLOSURE, WITH ITS BYTES. The budget is unbounded on purpose:
/// this is the owner's portability rule, not the clipboard's size policy — a
/// share file that referenced content by oid alone would open to nothing on a
/// machine that has never seen it (§5, "Import on an EMPTY library").
assetclosure::Options shareOptions(Project *project)
{
    assetclosure::Options options;
    options.inlineLimitBytes = std::numeric_limits<qint64>::max();
    options.includeRowBlobs = true;
    options.storeRoot = AssetStorePaths::root();
    if (project) options.projectGuid = project->getProjectGuid();
    return options;
}

/// THE CATALOG HALF IN SLICES, CUT BY WHAT IT COSTS (EXPORT-THREAD-1; MAIN-REDS-1 stage 5). The catalog reads
/// run on this (the UI) thread, a statement or a few per asset, and their cost is not a constant: in one Debug
/// run 16 pictures' describe cost 5.6 to 17 ms (0.35 to 1.07 ms an asset) and the 200 entries' rows in one
/// statement 11.9 ms — the fixed 16-asset slice and the one-statement fetch it replaces overran a frame's budget
/// (worst stretch 15.6 / 22.8 / 24.8 ms against 16.7). So every step is small (one asset), and the caller's
/// event loop turns (`yield`) before a step that, at the LAST step's measured cost, would carry the stretch past
/// HALF a frame's budget — the margin for a step that costs twice the last. A slicer without a `yield` (a caller
/// that does not turn its loop) slices nothing. The budget is unbounded here, so slicing changes nothing it
/// spends.
class StretchSlicer
{
public:
    static constexpr double kStretchMs = 1000.0 / 60.0 / 2.0;
    explicit StretchSlicer(const std::function<void()> &yield) : mYield(yield) {}
    /// Before a step: the loop turns if the step would end the stretch past the half frame.
    void before()
    {
        if (mYield && (!mStretch.isValid() || double(mStretch.nsecsElapsed()) / 1.0e6 + mLastMs > kStretchMs)) {
            mYield();
            mStretch.start();
        }
        mStep.start();
    }
    /// After it: its cost, the estimate of the next.
    void after() { mLastMs = double(mStep.nsecsElapsed()) / 1.0e6; }

private:
    const std::function<void()> &mYield;
    QElapsedTimer mStretch, mStep;
    double mLastMs = 0.0;
};

QMap<QString, clipboardformat::ClipAsset> describeInSlices(const QStringList &closure, Database *db,
                                                           Project *project,
                                                           QVector<assetclosure::DeferredRead> *reads,
                                                           StretchSlicer &slicer)
{
    QMap<QString, clipboardformat::ClipAsset> out;
    const assetclosure::Options options = shareOptions(project);
    for (const QString &guid : closure) {
        slicer.before();
        const auto described = assetclosure::describe({ guid }, db, options, nullptr, nullptr, reads);
        for (auto it = described.constBegin(); it != described.constEnd(); ++it) out.insert(it.key(), it.value());
        slicer.after();
    }
    return out;
}

}   // namespace

BundleStage stageBundle(Database *db, Project *project, const QStringList &guids,
                        const std::function<void()> &yield)
{
    BundleStage stage;
    const auto fail = [&stage](const QString &why) { stage.error = why; return stage; };
    if (!db) return fail(QStringLiteral("no library is open"));

    // THE ENTRY SET, in the caller's order, each once.
    QStringList entries;
    for (const QString &guid : guids)
        if (!guid.isEmpty() && !entries.contains(guid)) entries.append(guid);
    if (entries.isEmpty()) return fail(QStringLiteral("an asset is required"));

    clipboardformat::Envelope envelope = newEnvelope(project);
    QString singleKind;
    // The entries' own rows (no BLOB), one a step, sliced by their cost; in the caller's order.
    StretchSlicer slicer(yield);
    for (const QString &guid : entries) {
        slicer.before();
        const QVector<AssetRecord> fetched = db->fetchAssetHeaders({ guid });
        slicer.after();
        const AssetRecord row = fetched.isEmpty() ? AssetRecord() : fetched.first();
        if (row.guid.isEmpty()) return fail(QStringLiteral("no asset '%1'").arg(guid));
        // ONE item per entry: the assets the file is ABOUT. The rest of the
        // map is what they are made of; the resolver reads the items to know
        // which guids the import answers with.
        clipboardformat::ClipItem item;
        item.kind = QLatin1String(clipboardformat::kind::asset());
        item.data = QJsonObject{ { QStringLiteral("guid"), guid },
                                 { QStringLiteral("name"), row.name },
                                 { QStringLiteral("type"), scriptmod::assetTypeName(row.type) } };
        envelope.items.append(item);
        if (entries.size() == 1) singleKind = scriptmod::assetTypeName(row.type);
    }

    // The rows, the edges and the store paths — queries, on this thread. The
    // BYTES are owed (`reads`) and read by the writer, on a worker. The closure's walk is a
    // step of its own (a statement per level of the dependency graph).
    slicer.before();
    const QStringList closure = assetclosure::expand(entries, db);
    slicer.after();
    envelope.assets = describeInSlices(closure, db, project, &stage.reads, slicer);
    if (envelope.assets.isEmpty()) return fail(QStringLiteral("nothing to carry"));

    stage.envelope = envelope;
    // A single asset's manifest kind is its own type word; a set of them is a
    // PACK (the Assets tray's multi-selection export).
    stage.kind = entries.size() == 1 ? singleKind : QStringLiteral("pack");
    return stage;
}

BundleStage stageNode(Database *db, Project *project, const QJsonObject &nodeObject,
                      const QString &name, int typeId, const std::function<void()> &yield)
{
    BundleStage stage;
    const auto fail = [&stage](const QString &why) { stage.error = why; return stage; };
    if (!db) return fail(QStringLiteral("no library is open"));
    if (nodeObject.isEmpty()) return fail(QStringLiteral("a node is required"));

    // What the node is made of: the key-aware walk of its references (the
    // clipboard's own table, io/assetrefs.h) and everything THOSE depend on.
    StretchSlicer slicer(yield);
    slicer.before();
    const QStringList closure = assetclosure::forNodes({ nodeObject }, db);
    slicer.after();
    QMap<QString, clipboardformat::ClipAsset> assets =
        describeInSlices(closure, db, project, &stage.reads, slicer);

    // THE NODE BECOMES THE ROW THE FILE IS ABOUT: an Object (or ParticleSystem)
    // row whose `asset` blob is the node — the shape every Object row has and
    // `assets.addToScene` instantiates — minted here, under a fresh guid, so an
    // import is one new library tile however many times the file is opened.
    const QString rowGuid = IrisUtils::generateGUID();
    clipboardformat::ClipAsset row;
    row.guid = rowGuid;
    row.name = name.isEmpty() ? QStringLiteral("Node") : name;
    row.typeId = typeId;
    row.type = scriptmod::assetTypeName(typeId);
    row.blob = QJsonDocument(nodeObject).toJson(QJsonDocument::Compact);
    for (const QString &ref : assetrefs::collectAssetGuids(nodeObject))
        if (assets.contains(ref) && !row.dependencies.contains(ref)) row.dependencies.append(ref);
    // THE CLOSURE HANGS UNDER THE NEW ROW. A member row's `parent` names the
    // asset it belongs to — for a placed model, the library Object it was
    // imported as, which is not in the file. Left as it was, every member
    // would land under a guid the receiving library does not have; under the
    // new row it is that row's bundle, which is what it is.
    for (auto it = assets.begin(); it != assets.end(); ++it)
        if (!it->parent.isEmpty() && !assets.contains(it->parent)) it->parent = rowGuid;
    assets.insert(rowGuid, row);

    clipboardformat::Envelope envelope = newEnvelope(project);
    envelope.assets = assets;
    clipboardformat::ClipItem item;
    item.kind = QLatin1String(clipboardformat::kind::asset());
    item.data = QJsonObject{ { QStringLiteral("guid"), rowGuid },
                             { QStringLiteral("name"), row.name },
                             { QStringLiteral("type"), row.type } };
    envelope.items.append(item);
    stage.envelope = envelope;
    stage.kind = row.type;
    return stage;
}

void yieldToEventLoop()
{
    // Frames, repaints, timers; never user input — nothing a click could start
    // re-enters an export that is staging.
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

// THE VERBS MEASURE THEIR OWN BLOCKS (MAIN-REDS-1 stage 5): every stretch of the export's own work on this
// thread between two event-loop turns, from the verb's entry to its return (UiStepClock, bundlewriter.h).
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
    UiStepClock clock;
    BundleStage stage = stageNode(db, project, nodeObject, name, typeId, [&clock] { clock.turn(yieldToEventLoop); });
    return exportAndWait(std::move(stage), destPath, &clock);
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
