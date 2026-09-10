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
    while (!frontier.isEmpty()) {
        const QString guid = frontier.takeFirst();
        for (const QString &dep : db->fetchAssetGUIDAndDependencies(guid)) push(dep);
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
                                                    int *inlinedOut, int *referencedOut)
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

    for (const QString &guid : guids) {
        if (guid.isEmpty() || assetrefs::isReservedGuid(guid)) continue;
        const AssetRecord record = db->fetchAsset(guid);
        if (record.guid.isEmpty()) continue;      // a guid this catalog does not know

        clipboardformat::ClipAsset asset;
        asset.guid = guid;
        asset.name = record.name;
        asset.typeId = record.type;
        asset.type = scriptmod::assetTypeName(record.type);
        asset.parent = record.parent;
        asset.viewFilter = record.view_filter;
        asset.dependencies = db->fetchAssetGUIDAndDependencies(guid, false);
        if (options.includeRowBlobs) {
            asset.blob = record.asset;
            asset.properties = record.properties;
        }

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
            if (size >= 0 && size <= budget) {
                QFile source(entry.path);
                if (source.open(QIODevice::ReadOnly)) {
                    file.inlineData = source.readAll();
                    budget -= file.inlineData.size();
                    anyInlined = true;
                }
            }
            if (file.inlineData.isEmpty()) anyReferenced = true;
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
