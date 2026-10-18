/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/assetshare.h"
// THE STAGING HALF OF AN EXPORT (assetshare.h: stageBundle, stageNode) — the catalog reads on the calling (UI)
// thread, sliced between its event-loop turns; the bytes are owed to the writer's worker (bundlewriter.h). Its own
// translation unit (MAIN-REDS-1's fix round) so the closure guard (archive.closure) counts the shipping path's
// statements, not a stand-in's.
#include "services/assetshare.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>

#include <limits>

#include "data/constants.h"
#include "data/database/database.h"
#include "data/project.h"
#include "io/assetrefs.h"
#include "io/clipboardformat.h"
#include "irisgl/core/irisutils.h"
#include "scripting/modules/moduleshared.h"
#include "services/assetcas.h"
#include "services/assetclosure.h"
#include "services/assetstorepaths.h"

namespace assetshare {

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

/// THE CATALOG HALF IN SLICES, CUT BY WHAT IT COSTS (EXPORT-THREAD-1; MAIN-REDS-1 stage 5 and its fix round).
/// The catalog reads run on this (the UI) thread, and their cost is not a constant: in one Debug run a 16-asset
/// describe cost 5.6 to 17 ms (0.35 to 1.07 ms an asset), so a slice of a fixed asset count overran a frame's
/// budget. The reads are the D11 shape (assetclosure::Describer and Expander: the walk, the set's headers and its
/// edges a CHUNK a statement (ChunkSizer), then a BLOB read and a files statement an asset), each a STEP here, and
/// the caller's event loop turns (`yield`) before a step that, at the LAST step's measured cost, would carry the
/// stretch past HALF a frame's budget — the margin for a step that costs twice the last. A slicer without a
/// `yield` slices nothing. The budget is unbounded here, so slicing changes nothing it spends.
class StretchSlicer
{
public:
    static constexpr double kStretchMs = 1000.0 / 60.0 / 2.0;
    explicit StretchSlicer(std::function<void()> yield) : mYield(std::move(yield)) {}
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
    double lastMs() const { return mLastMs; }

private:
    std::function<void()> mYield;
    QElapsedTimer mStretch, mStep;
    double mLastMs = 0.0;
};

/// A SET READ IN CHUNKS (MAIN-REDS-1's fix round, the lead's): one statement over the whole set grows with the set
/// (the 200 rows' headers 11-14 ms in Debug, over a frame by ~300 — and a pack can hold thousands), so a set read
/// is cut into chunks whose size the cost of the last chunk sets: as many rows as fit half a frame at the last
/// chunk's cost a row, and never fewer than kChunkMin (the first chunk is kChunkMin, the measurement). The
/// statement count stays D11's in kind: at most ceil(N / kChunkMin) a set read.
class ChunkSizer
{
public:
    static constexpr int kChunkMin = 32;
    int next() const { return mNext; }
    void measured(int rows, double ms)
    {
        if (rows <= 0) return;
        const double perRow = ms / double(rows);
        mNext = perRow > 0.0 ? int(qBound(double(kChunkMin), StretchSlicer::kStretchMs / perRow, 1.0e6))
                             : 1000000;
    }

private:
    int mNext = kChunkMin;
};

/// Runs `read(n)` — a set read returning the rows it fetched — until `done()`, a slicer step a chunk.
template <typename Read, typename Done>
void readInChunks(StretchSlicer &slicer, Read &&read, Done &&done)
{
    ChunkSizer sizer;
    while (!done()) {
        slicer.before();
        const int rows = read(sizer.next());
        slicer.after();
        sizer.measured(rows, slicer.lastMs());
    }
}

/// The closure's walk, a chunk of a BFS level a step.
QStringList expandSliced(const QStringList &seeds, Database *db, StretchSlicer &slicer)
{
    assetclosure::Expander walk(seeds, db);
    readInChunks(slicer, [&walk](int n) { return walk.step(n); }, [&walk] { return walk.done(); });
    return walk.result();
}

/// The set's two reads (the headers if not read yet: a stage reads them first, for its entries), then one step
/// an asset — every chunk and every asset a slicer step.
QMap<QString, clipboardformat::ClipAsset> describeSliced(assetclosure::Describer &describer, StretchSlicer &slicer,
                                                         bool headersRead = false)
{
    if (!headersRead)
        readInChunks(slicer, [&describer](int n) { return describer.readHeaders(n); },
                     [&describer] { return describer.headersDone(); });
    readInChunks(slicer, [&describer](int n) { return describer.readEdges(n); },
                 [&describer] { return describer.edgesDone(); });
    for (int i = 0; i < describer.steps(); ++i) {
        slicer.before();
        describer.step(i);
        slicer.after();
    }
    return describer.takeDescribed();
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

    // The closure (the entries first, then what they are made of: a statement per chunk of a level of the
    // dependency graph), then its rows and edges a chunk a statement and one step an asset — queries, on this
    // thread.
    // The BYTES are owed (`reads`) and read by the writer, on a worker.
    StretchSlicer slicer(yield);
    const QStringList closure = expandSliced(entries, db, slicer);
    assetclosure::Describer describer(closure, db, shareOptions(project), &stage.reads);
    readInChunks(slicer, [&describer](int n) { return describer.readHeaders(n); },
                 [&describer] { return describer.headersDone(); });

    clipboardformat::Envelope envelope = newEnvelope(project);
    QString singleKind;
    // The entries' own rows, from the closure's headers (an entry is in its own closure), in the caller's order.
    for (const QString &guid : entries) {
        const AssetRecord *row = describer.header(guid);
        if (!row) return fail(QStringLiteral("no asset '%1'").arg(guid));
        // ONE item per entry: the assets the file is ABOUT. The rest of the
        // map is what they are made of; the resolver reads the items to know
        // which guids the import answers with.
        clipboardformat::ClipItem item;
        item.kind = QLatin1String(clipboardformat::kind::asset());
        item.data = QJsonObject{ { QStringLiteral("guid"), guid },
                                 { QStringLiteral("name"), row->name },
                                 { QStringLiteral("type"), scriptmod::assetTypeName(row->type) } };
        envelope.items.append(item);
        if (entries.size() == 1) singleKind = scriptmod::assetTypeName(row->type);
    }

    envelope.assets = describeSliced(describer, slicer, true);
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
    const QStringList closure = expandSliced(assetclosure::nodeSeeds({ nodeObject }), db, slicer);
    assetclosure::Describer describer(closure, db, shareOptions(project), &stage.reads);
    QMap<QString, clipboardformat::ClipAsset> assets = describeSliced(describer, slicer);

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

}   // namespace assetshare
