/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/presetrestamp.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVector>

#include "data/database/database.h"
#include "services/jahlog.h"
#include "services/materialbundle.h"
#include "services/memberstamp.h"

namespace {

/// A texture row as this pass needs it: its guid, the CONTENT ID of its source
/// object (identity), and the properties blob the stamp lives in (so the walk
/// asks the two keys without a query per row — the listing form
/// `memberstamp::isStamped(QByteArray)` exists for exactly this).
struct TextureRow
{
    QString guid;
    QString oid;
    QByteArray properties;
};

/// EVERY LIBRARY TEXTURE WITH ITS SOURCE OBJECT, in one query.
///
/// Only a 'source' row of a TEXTURE counts — the same content identity
/// `ShippedAssets::libraryTextureFor` and `memberstamp::stampedTextureFor`
/// use: an imported model's embedded copy of the same image is a
/// 'texture'-role file of an Object, not a texture a material may name.
/// UNLISTED rows are included: a row the user deleted from the library while a
/// project still pins it is still a preset's map, and the stamp is about where
/// the picture came from, not about whether a listing shows it today.
QVector<TextureRow> textureRows(QSqlDatabase conn)
{
    QVector<TextureRow> out;
    QSqlQuery query(conn);
    query.prepare("SELECT A.guid, AF.oid, A.properties FROM asset_files AF "
                  "JOIN assets A ON A.guid = AF.asset_guid "
                  "WHERE AF.role = 'source' AND A.type = ? "
                  "ORDER BY A.guid");
    query.addBindValue(static_cast<int>(ModelTypes::Texture));
    if (!query.exec()) return out;
    while (query.next()) {
        TextureRow row;
        row.guid = query.value(0).toString();
        row.oid = query.value(1).toString();
        row.properties = query.value(2).toByteArray();
        if (!row.guid.isEmpty() && !row.oid.isEmpty()) out.append(row);
    }
    return out;
}

/// ONE LINE PER PASS, and it says what the library looked like (the count is 0
/// on a library that needs nothing — the honest answer, and the one a log from
/// a user's machine must carry to tell "nothing to repair" from "never ran").
void logReport(const presetrestamp::Report &report)
{
    if (!report.error.isEmpty()) {
        irisLog(QStringLiteral("preset re-stamp: not run - %1").arg(report.error));
        return;
    }
    if (!report.ran) {
        irisLog(QStringLiteral("preset re-stamp: 0 map(s) — every one of the library's %1 "
                               "texture row(s) already carries its stamp (%2 ms)")
                    .arg(report.scanned).arg(report.ms));
        return;
    }
    irisLog(QStringLiteral("preset re-stamp: %1 shipped map(s) stamped as members across %2 "
                           "preset(s), %3 preset(s) left alone (a stamping seed minted them), "
                           "%4 texture row(s) scanned in %5 ms")
                .arg(report.stamped).arg(report.presets).arg(report.skipped).arg(report.scanned)
                .arg(report.ms));
}

}   // namespace

namespace presetrestamp {

namespace {

Report restampImpl(Database *db, const QStringList &presetGuids)
{
    Report out;
    if (!db || presetGuids.isEmpty()) return out;
    QSqlDatabase conn = QSqlDatabase::database();
    if (!conn.isOpen()) {
        out.error = QStringLiteral("no library");
        return out;
    }

    const QVector<TextureRow> rows = textureRows(conn);
    out.scanned = rows.size();

    // THE CHEAP PRE-GATE, and the answer on every launch of a library that has
    // been repaired (or was minted by a stamping seed in the first place): if
    // no texture row is unstamped there is nothing a repair could reach, and
    // not one definition needs to be read off the store.
    bool anyUnstamped = false;
    for (const TextureRow &row : rows) {
        if (!memberstamp::isStamped(row.properties)) { anyUnstamped = true; break; }
    }
    if (!anyUnstamped) return out;
    out.ran = true;

    // WHICH CONTENT IS WHOSE MAP. Read from the shipped bundles' own
    // definitions — a definition names its members by GUID, and a member's
    // guid resolves to the object it stores — so the key this pass matches on
    // is the sha256, never a name. A preset with no bundle in this library
    // (never seeded here) simply names nothing.
    //
    // FIRST NAMER WINS, in the shipped table's order: the same rule the seed
    // itself follows (`Prepared::mapOwners`) and the one `memberstamp::stamp`
    // enforces anyway — an origin never moves.
    QHash<QString, QString> oidOfRow;        // texture guid -> its source oid
    for (const TextureRow &row : rows) oidOfRow.insert(row.guid, row.oid);

    QHash<QString, QString> presetOfOid;     // map content -> the preset it came in through
    for (const QString &presetGuid : presetGuids) {
        const QJsonObject definition = MaterialBundle::read(db, presetGuid);
        if (definition.isEmpty()) continue;
        for (const QString &member : MaterialBundle::memberGuids(definition)) {
            const QString oid = oidOfRow.value(member);
            if (oid.isEmpty()) continue;                     // not a texture row here
            if (!presetOfOid.contains(oid)) presetOfOid.insert(oid, presetGuid);
        }
    }
    if (presetOfOid.isEmpty()) return out;

    // WAS A STAMPING SEED HERE? Asked per preset, of the maps themselves: a
    // seed stamps every map it mints, so one map of this preset carrying a
    // SHIPPED-PRESET origin proves this preset's batch was minted by a seed
    // that stamps — and then an unstamped map of it is a picture the USER
    // brought (V-2's other half, scripting.e2e.preset_seed_boot's arm 1), not
    // a row an old seed forgot. A stamp whose origin is the user's own
    // material is not that evidence and is not read as it.
    const QSet<QString> shipped(presetGuids.begin(), presetGuids.end());
    QSet<QString> claimed;
    for (const TextureRow &row : rows) {
        if (!memberstamp::isStamped(row.properties)) continue;
        const QString preset = presetOfOid.value(row.oid);
        if (preset.isEmpty()) continue;
        if (shipped.contains(memberstamp::originOf(row.properties))) claimed.insert(preset);
    }
    out.skipped = claimed.size();

    // ONE TRANSACTION for the whole repair: it is one fact about the library,
    // and a half-written one would leave a tray that folds some of a bundle's
    // pictures and not the others.
    DbTransaction tx(conn);
    QSet<QString> repaired;
    for (const TextureRow &row : rows) {
        if (memberstamp::isStamped(row.properties)) continue;
        const QString preset = presetOfOid.value(row.oid);
        if (preset.isEmpty()) continue;             // the user's own picture: never touched
        if (claimed.contains(preset)) continue;     // their own copy of a preset's map
        if (!memberstamp::stamp(db, row.guid, preset)) continue;
        ++out.stamped;
        repaired.insert(preset);
    }
    tx.commit();
    out.presets = repaired.size();
    return out;
}

}   // namespace

Report restamp(Database *db, const QStringList &presetGuids)
{
    QElapsedTimer timer;
    timer.start();
    Report report = restampImpl(db, presetGuids);
    report.ms = timer.elapsed();
    if (db && !presetGuids.isEmpty()) logReport(report);
    return report;
}

}   // namespace presetrestamp
