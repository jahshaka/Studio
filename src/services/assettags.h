/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETTAGS_H
#define ASSETTAGS_H

// assettags — the library row's NAME and TAGS, as one service (2026-09-06
// verb-coverage audit F3).
//
// Both live in the same `assets` row and are written by the same statement
// (Database::updateAssetMetadata sets name AND tags together), so a verb that
// renames must carry the tags through and a verb that re-tags must carry the
// name through, or one of them silently wipes the other. That rule lived
// nowhere: the Assets page's Update button built the JSON blob inline and
// scripts could not rename an asset at all.
//
// The blob's shape is the one the Assets page has always written:
//
//     {"tags": ["kitchen", "wood"]}
//
// A bare JSON array is accepted on read too, because nothing ever validated
// what went in.

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "data/database/database.h"
#include "data/project.h"   // AssetRecord

namespace assettags {

/// The tag list inside a stored blob. Empty for anything unparseable — a tag
/// column has never had a schema, and a listing must not fail on old bytes.
inline QStringList parse(const QByteArray &blob)
{
    QStringList out;
    if (blob.isEmpty()) return out;
    const QJsonDocument doc = QJsonDocument::fromJson(blob);
    QJsonArray array;
    if (doc.isObject()) array = doc.object().value(QStringLiteral("tags")).toArray();
    else if (doc.isArray()) array = doc.array();
    for (const auto &value : array) {
        const QString tag = value.toString().trimmed();
        if (!tag.isEmpty()) out << tag;
    }
    return out;
}

/// The blob for a tag list. An EMPTY list stores empty bytes rather than
/// `{"tags":[]}` — that is what an untagged row has always looked like, and
/// the difference is visible to the sidecar diff.
inline QByteArray serialize(const QStringList &tags)
{
    QStringList cleaned;
    for (const QString &tag : tags) {
        const QString trimmed = tag.trimmed();
        if (!trimmed.isEmpty() && !cleaned.contains(trimmed, Qt::CaseInsensitive))
            cleaned << trimmed;
    }
    if (cleaned.isEmpty()) return QByteArray();
    QJsonArray array;
    for (const QString &tag : cleaned) array.append(tag);
    QJsonObject root;
    root[QStringLiteral("tags")] = array;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

/// The row's tags. Empty for an unknown guid (the caller checks existence).
inline QStringList tagsOf(Database *db, const QString &guid)
{
    if (!db) return QStringList();
    return parse(db->fetchAsset(guid).tags);
}

/// Writes name + tags together — the ONE path, used by assets.rename,
/// assets.setTags and the Assets page's Update button. `name` empty keeps the
/// stored name; `tags` null (a default-constructed optional-ish empty
/// QByteArray from `serialize`) is handled by the two wrappers below, which
/// read the row first so nothing is lost.
inline bool write(Database *db, const QString &guid, const QString &name,
                  const QStringList &tags)
{
    if (!db) return false;
    return db->updateAssetMetadata(guid, name, serialize(tags));
}

/// Renames the asset, keeping its tags. False when the row does not exist or
/// the write failed.
inline bool rename(Database *db, const QString &guid, const QString &name)
{
    if (!db) return false;
    const AssetRecord record = db->fetchAsset(guid);
    if (record.guid.isEmpty()) return false;
    return write(db, guid, name, parse(record.tags));
}

/// Replaces the asset's tags, keeping its name.
inline bool setTags(Database *db, const QString &guid, const QStringList &tags)
{
    if (!db) return false;
    const AssetRecord record = db->fetchAsset(guid);
    if (record.guid.isEmpty()) return false;
    return write(db, guid, record.name, tags);
}

} // namespace assettags

#endif // ASSETTAGS_H
