/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/assetclosure.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <utility>

#include "data/database/database.h"
#include "export/exportcontentsource.h"
#include "io/assetrefs.h"
#include "scripting/modules/moduleshared.h"

namespace assetclosure {

namespace {

/// The catalog's dependency edges from `seeds`, transitively, with a visited
/// set (the same contract AssetHelper::fetchAssetAndAllDependencies documents:
/// a dependency table is a graph a user can cycle through).
QStringList expandThroughCatalog(const QStringList &seeds, Database *db)
{
    QStringList out;
    if (!db) return seeds;
    QSet<QString> seen;
    QStringList frontier;
    const auto push = [&](const QString &guid) {
        if (guid.isEmpty() || assetrefs::isReservedGuid(guid) || seen.contains(guid)) return;
        seen.insert(guid);
        out.append(guid);
        frontier.append(guid);
    };
    for (const QString &seed : seeds) push(seed);
    // ONE QUERY PER BFS LEVEL (D11-LIBRARY-SCALE §3.6): the whole frontier's
    // edges at once, then walked in frontier order — the same visit order the
    // query-per-node walk had, at a statement per level instead of per asset.
    while (!frontier.isEmpty()) {
        const QStringList level = frontier;
        frontier.clear();
        // THE MEMBER-TO-OBJECT EDGE (STAGE-CLOSE-TRIAGE-1 reads 4-5): a model's Mesh member keeps its
        // import record on its OBJECT row (MeshBakeStore::importRecordForGuid walks `parent`), and a
        // node names the member; the dependency table points object -> members only, so a closure
        // seeded from a node lost the record and a paste re-baked the member at identity. The
        // Object rides — for a Mesh member ONLY: a model's texture rows hang under the Object too,
        // and a texture a material borrowed from model A must not drag model A into every copy.
        // In the level's one statement (fetchDependencyEdges' withMemberObjects).
        const QHash<QString, QStringList> edges = db->fetchDependencyEdges(level, true);
        for (const QString &guid : level) {
            // Self first, as fetchAssetGUIDAndDependencies(guid) answered.
            push(guid);
            for (const QString &dep : edges.value(guid)) push(dep);
        }
    }
    return out;
}

} // namespace

QStringList forNodes(const QVector<QJsonObject> &nodeObjects, Database *db)
{
    QStringList seeds;
    for (const QJsonObject &nodeObj : nodeObjects)
        for (const QString &guid : assetrefs::collectAssetGuids(nodeObj))
            if (!seeds.contains(guid)) seeds << guid;
    return expandThroughCatalog(seeds, db);
}

QStringList expand(const QStringList &seeds, Database *db)
{
    return expandThroughCatalog(seeds, db);
}

struct Describer::State
{
    Database *db = nullptr;
    Options options;
    QVector<DeferredRead> *deferredOut = nullptr;
    CasContentSource content;
    qint64 budget = 0;
    QStringList wanted, known;
    QHash<QString, AssetRecord> headers;
    QHash<QString, QStringList> edges;
    QMap<QString, clipboardformat::ClipAsset> out;
    int inlined = 0, referenced = 0;
    State(Database *d, const Options &o, QVector<DeferredRead> *def)
        : db(d), options(o), deferredOut(def), content(o.storeRoot, o.projectGuid), budget(o.inlineLimitBytes) {}
};

Describer::Describer(const QStringList &guids, Database *db, const Options &options,
                     QVector<DeferredRead> *deferredOut)
    : d(new State(db, options, deferredOut))
{
    for (const QString &guid : guids)
        if (!guid.isEmpty() && !assetrefs::isReservedGuid(guid)) d->wanted.append(guid);
}

Describer::~Describer() = default;

// THE CLOSURE'S ROWS IN TWO STATEMENTS (D11-LIBRARY-SCALE §3.6): every header without a BLOB, and every asset's
// direct edges, for the whole guid set at once. A row's JSON BLOBs are read when that asset is described (and
// only when the item carries them) — never a thumbnail.
void Describer::readHeaders()
{
    d->headers.clear();
    d->known.clear();
    if (!d->db) return;
    for (const AssetRecord &row : d->db->fetchAssetHeaders(d->wanted)) d->headers.insert(row.guid, row);
    for (const QString &guid : d->wanted)
        if (d->headers.contains(guid)) d->known.append(guid);   // a guid this catalog does not know is no step
}

void Describer::readEdges()
{
    if (d->db) d->edges = d->db->fetchDependencyEdges(d->wanted);
}

int Describer::steps() const { return int(d->known.size()); }

const AssetRecord *Describer::header(const QString &guid) const
{
    const auto found = d->headers.constFind(guid);
    return found == d->headers.constEnd() ? nullptr : &*found;
}

// ONE ASSET, in walk order. THE ROW'S BLOB IS THE ROW (round 8, the lead's rule): never budgeted, it always
// travels — a model of tens of thousands of parts copies like any other. The budget bounds the FILE payloads
// only; a file that does not fit goes by reference (its oid).
void Describer::step(int i)
{
    if (i < 0 || i >= int(d->known.size())) return;
    const QString &guid = d->known.at(i);
    const AssetRecord &record = d->headers[guid];

    clipboardformat::ClipAsset asset;
    asset.guid = guid;
    asset.name = record.name;
    asset.typeId = record.type;
    asset.type = scriptmod::assetTypeName(record.type);
    asset.parent = record.parent;
    asset.dependencies = d->edges.value(guid);
    bool anyInlined = false, anyReferenced = false;
    if (d->options.includeRowBlobs) d->db->fetchAssetRowBlobs(guid, &asset.blob, &asset.properties);

    for (const auto &entry : d->content.filesForAsset(guid, record.name)) {
        clipboardformat::ClipFile file;
        file.role = entry.role;
        file.name = entry.name;
        file.oid = entry.oid;
        file.size = entry.size;
        // The store object's file name IS `<oid>.<ext>`, so its suffix is
        // exactly the extension the catalog recorded — no second lookup.
        file.ext = QFileInfo(entry.path).suffix().toLower();

        const qint64 size = entry.size >= 0 ? entry.size : QFileInfo(entry.path).size();
        // IN WALK ORDER while the budget lasts (§2.3): a payload's total
        // inline size is what clipboard managers have to survive, so the
        // budget is spent, not applied per file. An asset that misses out
        // still travels — by oid, which finds the same content in any
        // store that has it.
        bool owed = false;
        if (size >= 0 && size <= d->budget) {
            if (d->deferredOut) {
                // DEFERRED (EXPORT-THREAD-1): the budget is spent now, the
                // bytes are read by whoever writes the file — a worker.
                d->deferredOut->append(DeferredRead{ guid, int(asset.files.size()), entry.path });
                d->budget -= size;
                anyInlined = owed = true;
            } else {
                QFile source(entry.path);
                if (source.open(QIODevice::ReadOnly)) {
                    file.inlineData = source.readAll();
                    d->budget -= file.inlineData.size();
                    anyInlined = true;
                }
            }
        }
        if (file.inlineData.isEmpty() && !owed) anyReferenced = true;
        asset.files.append(file);
    }

    if (anyInlined) ++d->inlined;
    else if (anyReferenced) ++d->referenced;
    d->out.insert(guid, asset);
}

QMap<QString, clipboardformat::ClipAsset> Describer::takeDescribed() { return std::exchange(d->out, {}); }
int Describer::inlined() const { return d->inlined; }
int Describer::referenced() const { return d->referenced; }

QMap<QString, clipboardformat::ClipAsset> describe(const QStringList &guids, Database *db,
                                                    const Options &options,
                                                    int *inlinedOut, int *referencedOut,
                                                    QVector<DeferredRead> *deferredOut)
{
    if (!db) {
        if (inlinedOut) *inlinedOut = 0;
        if (referencedOut) *referencedOut = 0;
        return {};
    }
    Describer describer(guids, db, options, deferredOut);
    describer.readHeaders();
    describer.readEdges();
    for (int i = 0; i < describer.steps(); ++i) describer.step(i);
    if (inlinedOut) *inlinedOut = describer.inlined();
    if (referencedOut) *referencedOut = describer.referenced();
    return describer.takeDescribed();
}

} // namespace assetclosure
