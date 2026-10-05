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
        const QHash<QString, QStringList> edges = db->fetchDependencyEdges(level);
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

QMap<QString, clipboardformat::ClipAsset> describe(const QStringList &guids, Database *db,
                                                    const Options &options,
                                                    int *inlinedOut, int *referencedOut,
                                                    QVector<DeferredRead> *deferredOut)
{
    QMap<QString, clipboardformat::ClipAsset> out;
    int inlined = 0, referenced = 0;
    if (!db) {
        if (inlinedOut) *inlinedOut = 0;
        if (referencedOut) *referencedOut = 0;
        return out;
    }

    CasContentSource content(options.storeRoot, options.projectGuid);
    qint64 budget = options.inlineLimitBytes;

    // THE CLOSURE'S ROWS IN TWO STATEMENTS (D11-LIBRARY-SCALE §3.6): every
    // header without a BLOB, and every asset's direct edges, for the whole guid
    // set at once. A row's JSON BLOBs are read when that asset is written below
    // (and only when the item carries them) — never a thumbnail.
    QStringList wanted;
    for (const QString &guid : guids)
        if (!guid.isEmpty() && !assetrefs::isReservedGuid(guid)) wanted.append(guid);
    QHash<QString, AssetRecord> headers;
    for (const AssetRecord &row : db->fetchAssetHeaders(wanted)) headers.insert(row.guid, row);
    const QHash<QString, QStringList> edges = db->fetchDependencyEdges(wanted);

    for (const QString &guid : wanted) {
        const auto found = headers.constFind(guid);
        if (found == headers.constEnd()) continue;      // a guid this catalog does not know
        const AssetRecord &record = *found;

        clipboardformat::ClipAsset asset;
        asset.guid = guid;
        asset.name = record.name;
        asset.typeId = record.type;
        asset.type = scriptmod::assetTypeName(record.type);
        asset.parent = record.parent;
        asset.dependencies = edges.value(guid);
        if (options.includeRowBlobs) db->fetchAssetRowBlobs(guid, &asset.blob, &asset.properties);

        bool anyInlined = false, anyReferenced = false;
        for (const auto &entry : content.filesForAsset(guid, record.name)) {
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
            if (size >= 0 && size <= budget) {
                if (deferredOut) {
                    // DEFERRED (EXPORT-THREAD-1): the budget is spent now, the
                    // bytes are read by whoever writes the file — a worker.
                    deferredOut->append(DeferredRead{ guid, int(asset.files.size()), entry.path });
                    budget -= size;
                    anyInlined = owed = true;
                } else {
                    QFile source(entry.path);
                    if (source.open(QIODevice::ReadOnly)) {
                        file.inlineData = source.readAll();
                        budget -= file.inlineData.size();
                        anyInlined = true;
                    }
                }
            }
            if (file.inlineData.isEmpty() && !owed) anyReferenced = true;
            asset.files.append(file);
        }

        if (anyInlined) ++inlined;
        else if (anyReferenced) ++referenced;
        out.insert(guid, asset);
    }

    if (inlinedOut) *inlinedOut = inlined;
    if (referencedOut) *referencedOut = referenced;
    return out;
}

} // namespace assetclosure
