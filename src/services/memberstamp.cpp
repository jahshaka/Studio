/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/memberstamp.h"
#include "services/projectmembership.h"

#include <QJsonDocument>
#include <QJsonObject>
#include "data/database/dbqt.h"
#include "data/db/statement.h"

#include "data/database/database.h"

namespace {

const QLatin1String kMemberKey("member");
const QLatin1String kOriginKey("memberOf");

}   // namespace

namespace memberstamp {

bool isStamped(Database *db, const QString &guid)
{
    if (!db || guid.isEmpty()) return false;
    return db->fetchAssetProperties(guid).object.value(kMemberKey).toBool();
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
    // A MARKER's WRITE SKIPS unless the row's properties READ AS AN OBJECT: a failed read's {} or an unreadable blob's
    // {} written back would replace them whole (B4's sweep; the third answer).
    const Database::StoredProperties stored = db->fetchAssetProperties(textureGuid);
    if (!stored.isObject()) return false;
    QJsonObject props = stored.object;
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
    const Database::StoredProperties stored = db->fetchAssetProperties(guid);
    if (!stored.isObject()) return false;   // as `stamp`: nothing is written over properties that did not read
    QJsonObject props = stored.object;
    // BOTH KEYS, and nothing else in the row's properties: the reverse edit of
    // `stamp`. A row with neither key is already the user's — say yes and
    // write nothing (this runs on every import of content the library has).
    if (!props.contains(kMemberKey) && !props.contains(kOriginKey)) return true;
    props.remove(kMemberKey);
    props.remove(kOriginKey);
    if (!db->updateAssetProperties(guid, QJsonDocument(props).toJson())) return false;
    // THE PICTURE IS A TILE AGAIN (TRAY-REPOP-1's residual): a folded map the
    // user imported is theirs now, and the tray must show it without a click.
    // Not a pin change, so announced as "some project" — the tray repopulates
    // on it, coalesced, exactly as on a pin.
    ProjectMembership::instance()->announceAny(QString());
    return true;
}

QString stampedTextureFor(Database *db, const QString &oid, bool *readOk)
{
    if (!db || oid.isEmpty()) return QString();
    db::Connection &conn = db->connection();
    if (!conn.isOpen()) return QString();

    // Only a 'source' row of a TEXTURE counts, the same content identity the
    // material picker's import uses (ShippedAssets::libraryTextureFor): an
    // imported model's embedded copy of the same image is a 'texture'-role
    // file of an Object, not a texture a material may name.
    // EACH ROW's STAMP IS READ BY THE SAME STATEMENT (the second confirm's W3): a per-row read whose failure answered
    // "not a member" made the import mint the duplicate this function exists to prevent.
    db::Statement query(conn, dbqt::utf8("SELECT AF.asset_guid, A.properties FROM asset_files AF "
                  "JOIN assets A ON A.guid = AF.asset_guid "
                  "WHERE AF.oid = ? AND AF.role = 'source' AND A.type = ? "
                  "AND A.listed = 1 "
                  "AND " + Database::memberSubquery(QStringLiteral("A")) + " "
                  // The row that FIRST brought the picture in: makeUnique re-ingests
                  // the same bytes under a second stamped row (a material's private
                  // copy), and the user's import must claim the original, not that.
                  "ORDER BY A.date_created ASC, A.rowid ASC"));
    dbqt::bindText(query, 1, oid);
    query.bindInt(2, static_cast<int>(ModelTypes::Texture));
    db::Error e;
    while (query.step(&e))
        if (isStamped(dbqt::bytes(query, 1))) return dbqt::text(query, 0);
    if (readOk && e) *readOk = false;   // a failed read is no "no member holds it"
    return QString();
}

}   // namespace memberstamp
