/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/memberstamp.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "data/database/database.h"

namespace {

const QLatin1String kMemberKey("member");
const QLatin1String kOriginKey("memberOf");

QJsonObject propertiesOf(Database *db, const QString &guid)
{
    if (!db) return QJsonObject();
    return QJsonDocument::fromJson(db->fetchAsset(guid).properties).object();
}

}   // namespace

namespace memberstamp {

bool isStamped(Database *db, const QString &guid)
{
    if (!db || guid.isEmpty()) return false;
    return propertiesOf(db, guid).value(kMemberKey).toBool();
}

bool isStamped(const QByteArray &properties)
{
    return QJsonDocument::fromJson(properties).object().value(kMemberKey).toBool();
}

QString originOf(const QByteArray &properties)
{
    return QJsonDocument::fromJson(properties).object().value(kOriginKey).toString();
}

QByteArray stamped(const QByteArray &properties, const QString &materialGuid)
{
    QJsonObject props = QJsonDocument::fromJson(properties).object();
    props.insert(kMemberKey, true);
    props.insert(kOriginKey, materialGuid);
    return QJsonDocument(props).toJson();
}

bool stamp(Database *db, const QString &textureGuid, const QString &materialGuid)
{
    if (!db || textureGuid.isEmpty()) return false;
    QJsonObject props = propertiesOf(db, textureGuid);
    if (props.value(kMemberKey).toBool()
        && !props.value(kOriginKey).toString().isEmpty())
        return true;   // already somebody's member; an origin never moves
    props.insert(kMemberKey, true);
    if (!materialGuid.isEmpty() && props.value(kOriginKey).toString().isEmpty())
        props.insert(kOriginKey, materialGuid);
    return db->updateAssetProperties(textureGuid, QJsonDocument(props).toJson());
}

bool unstamp(Database *db, const QString &guid)
{
    if (!db || guid.isEmpty()) return false;
    QJsonObject props = propertiesOf(db, guid);
    // BOTH KEYS, and nothing else in the row's properties: the reverse edit of
    // `stamp`. A row with neither key is already the user's — say yes and
    // write nothing (this runs on every import of content the library has).
    if (!props.contains(kMemberKey) && !props.contains(kOriginKey)) return true;
    props.remove(kMemberKey);
    props.remove(kOriginKey);
    return db->updateAssetProperties(guid, QJsonDocument(props).toJson());
}

QString stampedTextureFor(Database *db, const QString &oid)
{
    if (!db || oid.isEmpty()) return QString();
    QSqlDatabase conn = QSqlDatabase::database();
    if (!conn.isOpen()) return QString();

    // Only a 'source' row of a TEXTURE counts, the same content identity the
    // material picker's import uses (ShippedAssets::libraryTextureFor): an
    // imported model's embedded copy of the same image is a 'texture'-role
    // file of an Object, not a texture a material may name.
    QSqlQuery query(conn);
    query.prepare("SELECT AF.asset_guid FROM asset_files AF "
                  "JOIN assets A ON A.guid = AF.asset_guid "
                  "WHERE AF.oid = ? AND AF.role = 'source' AND A.type = ? "
                  "AND A.listed = 1 "
                  "AND " + Database::memberSubquery(QStringLiteral("A.guid")) + " "
                  // The row that FIRST brought the picture in: makeUnique re-ingests
                  // the same bytes under a second stamped row (a material's private
                  // copy), and the user's import must claim the original, not that.
                  "ORDER BY A.date_created ASC, A.rowid ASC");
    query.addBindValue(oid);
    query.addBindValue(static_cast<int>(ModelTypes::Texture));
    if (!query.exec()) return QString();
    while (query.next()) {
        const QString guid = query.value(0).toString();
        if (isStamped(db, guid)) return guid;
    }
    return QString();
}

}   // namespace memberstamp
