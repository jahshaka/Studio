/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "bakedmembers.h"

#include <QFileInfo>

#include "data/assethomekind.h"
#include "data/database/database.h"
#include "data/project.h"
#include "data/database/dbqt.h"
#include "data/db/statement.h"
#include "irisgl/core/guid.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"

namespace materials {

BakedMembers bakedMemberRows(Database *db, const QString &materialGuid, const QMap<QString, QString> &files)
{
    BakedMembers out;
    if (!db || materialGuid.isEmpty()) {
        out.error = QStringLiteral("no material");
        return out;
    }
    QMap<QString, QString> written;   // slot -> file, the slots the bake wrote a file for
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        if (QFileInfo::exists(it.value())) written.insert(it.key(), it.value());
        else out.dropped.append(QStringLiteral("%1 <- bake (the bake wrote no file)").arg(it.key()));
    }
    if (written.isEmpty()) return out;
    db::Connection &conn = db->connection();

    // ---- THE READS, ALL BEFORE THE FIRST WRITE ----------
    // The material's home (its baked rows are born in it: a project's material bakes project rows; a guid that names
    // no row yet, the Materials storage - assethome::of's answer) ...
    assethome::Home home = assethome::materials();
    {
        db::Statement row(conn, "SELECT view_filter, project_guid FROM assets WHERE guid = ?");
        dbqt::bindText(row, 1, materialGuid);
        db::Error e;
        if (row.step(&e)) {
            const assethome::Home stored = assethome::fromStored(int(row.int64(0)), dbqt::text(row, 1));
            if (!(stored.isProject() && stored.projectGuid.isEmpty())) home = stored;
        } else if (e) {
            out.error = QStringLiteral("the home of '%1' could not be read (%2) - no baked map was written")
                            .arg(materialGuid, dbqt::describe(e));
            return out;
        }
    }
    // ... and every slot's existing row, in ONE statement (`<slot>.png` under the material).
    QHash<QString, QString> existing;   // row name -> guid
    {
        QString marks;
        for (int i = 0; i < written.size(); ++i) marks += i ? QStringLiteral(",?") : QStringLiteral("?");
        db::Statement rows(conn, dbqt::utf8(QStringLiteral("SELECT name, guid FROM assets WHERE parent = ? AND type = ? "
                                                           "AND name IN (%1)").arg(marks)));
        dbqt::bindText(rows, 1, materialGuid);
        rows.bindInt(2, static_cast<int>(ModelTypes::Texture));
        int at = 3;
        for (auto it = written.constBegin(); it != written.constEnd(); ++it)
            dbqt::bindText(rows, at++, it.key() + QStringLiteral(".png"));
        db::Error e;
        while (rows.step(&e)) existing.insert(dbqt::text(rows, 0), dbqt::text(rows, 1));
        if (e) {
            out.error = QStringLiteral("whether '%1' already has its baked maps could not be read (%2) - no baked map "
                                       "was written").arg(materialGuid, dbqt::describe(e));
            return out;
        }
    }

    // ---- THE WRITES, ONE UNIT ----------
    const QString root = AssetStorePaths::root();
    QHash<QString, QString> guids;
    {
        DbTransaction unit(conn);
        for (auto it = written.constBegin(); it != written.constEnd(); ++it) {
            const QString name = it.key() + QStringLiteral(".png");
            QString guid = existing.value(name);
            QString error;
            if (guid.isEmpty()) {
                guid = QString::fromStdString(iris::newGuid());
                if (db->createAssetEntry(guid, name, static_cast<int>(ModelTypes::Texture), materialGuid, home,
                                         assethome::bornInside(home), QString(), QString(), QByteArray(), QByteArray(),
                                         QByteArray(), QByteArray(), &error).isEmpty()) {
                    out.error = QStringLiteral("the %1 map's row could not be written (%2) - no baked map was written")
                                    .arg(it.key(), error);
                    return out;   // the unit rolls back at its end
                }
            }
            QString oid;
            // ingestFile's INSERT OR IGNORE on (guid, role, name) would leave the pointer on the old oid: it moves.
            if (!AssetCas::ingestFile(conn, root, it.value(), guid, QStringLiteral("source"), name, &oid, &error)
                || !AssetCas::moveSourcePointer(conn, guid, oid, name, &error)) {
                out.error = QStringLiteral("the %1 map could not be stored (%2) - no baked map was written")
                                .arg(it.key(), error.isEmpty() ? QStringLiteral("the store refused it") : error);
                return out;
            }
            guids.insert(it.key(), guid);
        }
        if (!unit.commit()) {
            out.error = QStringLiteral("the baked maps of '%1' could not be committed - none was written").arg(materialGuid);
            return out;
        }
    }
    for (auto it = guids.constBegin(); it != guids.constEnd(); ++it)
        AssetCas::writeSidecar(conn, root, it.value(), nullptr);
    out.guidForSlot = guids;
    return out;
}

} // namespace materials
