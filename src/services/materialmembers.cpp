/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/materialmembers.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include "data/database/database.h"
#include "data/database/dbqt.h"
#include "data/db/statement.h"
#include "irisgl/core/guid.h"
#include "data/project.h"
#include "services/assetcas.h"
#include "services/assetdelete.h"
#include "services/assetstorepaths.h"
#include "services/materialbundle.h"
#include "services/memberstamp.h"
#include "services/jahlog.h"

namespace {

qint64 storedBytes(Database *db, Project *project, const QString &guid)
{
    Q_UNUSED(db);
    db::Connection &conn = db->connection();
    const QString root = AssetStorePaths::root();
    QString path;
    if (project && !project->getProjectGuid().isEmpty())
        path = AssetCas::resolvePinned(conn, root, project->getProjectGuid(), guid);
    if (path.isEmpty()) path = AssetCas::resolveSource(conn, root, guid);
    if (path.isEmpty()) return -1;
    const QFileInfo info(path);
    return info.exists() ? info.size() : -1;
}

}   // namespace

namespace materialmembers {

int usedBy(Database *db, const QString &guid)
{
    if (!db || guid.isEmpty()) return 0;
    // THE UNSCOPED LIST. `fetchDependers` is project-scoped and answers empty
    // outside a project, which is how the Members panel's one number read 0
    // for everything before phase 1's fix.
    return db->hasMultipleDependers(guid).size();
}

QSet<QString> hiddenAsMembers(Database *db, const QVector<AssetRecord> &records)
{
    QSet<QString> out;
    if (!db) return out;
    QStringList stamped;
    for (const AssetRecord &record : records)
        if (memberstamp::isStamped(record.properties))
            stamped << record.guid;   // the user's own image carries no stamp: a tile, always
    if (stamped.isEmpty()) return out;

    // Used by nothing: absent, a tile, so the user can SEE it and clean it up
    // (the row `unused` reports; a row nobody can see is a row nobody can
    // delete). Used by something that is not a material — a model, or a
    // depender the catalog does not know, which is a SCENE NODE's use (the
    // editor records use as a dependency with the node's id) — false: the
    // picture is on something the user can see, so it is a thing of its own.
    const QHash<QString, bool> onlyMaterials = db->fetchUsedOnlyByMaterials(stamped);
    for (const QString &guid : std::as_const(stamped))
        if (onlyMaterials.value(guid, false)) out.insert(guid);
    return out;
}

QVector<Member> describe(Database *db, Project *project, const QString &materialGuid)
{
    QVector<Member> out;
    if (!db || materialGuid.isEmpty()) return out;

    const QJsonObject definition = MaterialBundle::read(db, materialGuid, project);
    if (definition.isEmpty()) return out;

    // slot -> guid, and the reverse, from the definition's own values.
    const QJsonObject values = definition.value(QStringLiteral("values")).toObject();
    QHash<QString, QString> slotOf;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        if (MaterialBundle::textureSlots().contains(it.key()))
            slotOf.insert(it.value().toString(), it.key());

    // Which slots a BAKE produced (a map the graph baked, not a picture the
    // user picked) — the panel's "baked or source" column.
    const QJsonObject bakedMaps = definition.value(QStringLiteral("bake")).toObject()
                                            .value(QStringLiteral("maps")).toObject();
    QSet<QString> bakedGuids;
    for (auto it = bakedMaps.constBegin(); it != bakedMaps.constEnd(); ++it)
        bakedGuids.insert(it.value().toString());

    // And which GRAPH NODE holds it, for a graph material whose picture has
    // not reached a master slot (the node IS where the user put it).
    QHash<QString, QString> nodeOf;
    const QJsonArray nodes = definition.value(QStringLiteral("shadergraph")).toObject()
                                       .value(QStringLiteral("nodes")).toArray();
    for (const auto &entry : nodes) {
        const QJsonObject node = entry.toObject();
        if (node.value(QStringLiteral("type")).toString() != QLatin1String("texture")) continue;
        const QString value = node.value(QStringLiteral("value")).toString();
        if (value.isEmpty()) continue;
        if (!nodeOf.contains(value))
            nodeOf.insert(value, node.value(QStringLiteral("id")).toString());
    }

    const QString projectGuid = project ? project->getProjectGuid() : QString();
    QVector<AssetRecord> rows;
    for (const QString &guid : MaterialBundle::memberGuids(definition)) {
        const AssetRecord row = db->fetchAsset(guid);
        if (!row.guid.isEmpty()) rows.append(row);
    }
    const QSet<QString> folded = hiddenAsMembers(db, rows);
    for (const AssetRecord &row : std::as_const(rows)) {
        const QString &guid = row.guid;
        Member member;
        member.guid = guid;
        member.name = row.name;
        member.slot = slotOf.value(guid);
        member.node = nodeOf.value(guid);
        member.role = bakedGuids.contains(guid) ? QStringLiteral("baked")
                                                : QStringLiteral("source");
        member.bytes = storedBytes(db, project, guid);
        member.usedBy = usedBy(db, guid);
        member.pinned = !projectGuid.isEmpty() && db->isAssetPinnedBy(projectGuid, guid);
        member.member = memberstamp::isStamped(db, guid);
        member.hidden = folded.contains(guid);
        out.append(member);
    }
    return out;
}

QVector<Unused> unused(Database *db, Project *project, const QString &materialGuid)
{
    QVector<Unused> out;
    if (!db) return out;

    const auto describeRow = [&](const AssetRecord &row, const QString &scope) {
        Unused entry;
        entry.guid = row.guid;
        entry.name = row.name;
        entry.bytes = storedBytes(db, project, row.guid);
        entry.scope = scope;
        return entry;
    };

    // A FAILED READ NEVER LISTS A ROW AS UNUSED (NOQT-DATABASE-1 stage 2): the list is what cleanUnused
    // deletes, so a row whose users or pins could not be read is kept and said by name.
    const auto heldOrUnread = [db](const QString &guid) {
        bool read = true;
        const bool held = !db->hasMultipleDependers(guid, &read).isEmpty()
                          || !assetdelete::livePins(db, guid, &read).isEmpty();
        if (!read)
            JahLog::write(JahLog::legacy, JahLog::Level::Warning,
                QStringLiteral("members: '%1' is kept - who uses or pins it could not be read").arg(guid));
        return held || !read;
    };

    if (!materialGuid.isEmpty()) {
        // LIBRARY SCOPE: the rows that came in through THIS bundle and are
        // referenced by nothing. The origin stamp is the only link left once
        // the definition stopped naming them — the edges are derived from the
        // definition, so they went with the slot.
        // THE MATERIAL'S OWN HOME (ASSETS-HOME-1): its picked pictures live
        // where it does — the Materials storage, Assets for a saved one.
        const assethome::Home materialHome = assethome::of(db, materialGuid);
        const QVector<AssetRecord> homeRows =
            materialHome.isProject() ? QVector<AssetRecord>()
                                     : db->fetchAssetsInHome(materialHome.kind,
                                                             static_cast<int>(ModelTypes::Texture));
        for (const auto &row : homeRows) {
            if (row.type != static_cast<int>(ModelTypes::Texture)) continue;
            if (!memberstamp::isStamped(row.properties)) continue;
            if (memberstamp::originOf(row.properties) != materialGuid) continue;
            if (heldOrUnread(row.guid)) continue;   // somebody uses it, a project holds it, or neither is known
            out.append(describeRow(row, QStringLiteral("library")));
        }
        // AND THE BAKED MAPS THE DEFINITION NO LONGER NAMES (BUNDLE-P4, the
        // owed "cleanUnused at library scope cannot see baked members"). A
        // baked map is a member by the PARENT relation, invisible to every
        // library listing, so the walk above cannot reach it — and a re-bake
        // that dropped a slot left the old map behind for ever. The rows whose
        // parent is this material and whose guid the current bake block does
        // not name, referenced by nothing and pinned by no living project.
        QSet<QString> named;
        {
            const QJsonObject definition = MaterialBundle::read(db, materialGuid, nullptr);
            const QJsonObject maps = definition.value(QStringLiteral("bake")).toObject()
                                               .value(QStringLiteral("maps")).toObject();
            for (auto it = maps.constBegin(); it != maps.constEnd(); ++it)
                named.insert(it.value().toString());
            for (const QString &g : MaterialBundle::memberGuids(definition)) named.insert(g);
        }
        QStringList children;
        {
            db::Statement query(db->connection(), "SELECT guid FROM assets WHERE parent = ? AND type = ?");
            dbqt::bindText(query, 1, materialGuid);
            query.bindInt(2, static_cast<int>(ModelTypes::Texture));
            db::Error e;
            while (query.step(&e)) children << dbqt::text(query, 0);
        }
        for (const QString &child : children) {
            if (named.contains(child)) continue;                            // still a member
            if (heldOrUnread(child)) continue;   // somebody uses it, a project holds it, or neither is known
            const AssetRecord row = db->fetchAsset(child);
            if (row.guid.isEmpty()) continue;
            out.append(describeRow(row, QStringLiteral("library")));
        }
        return out;
    }

    // PROJECT SCOPE: the member pins this project holds that nothing in it
    // uses. The PIN goes, never the library row — another project may be
    // built on the same bundle.
    if (!project || project->getProjectGuid().isEmpty()) return out;
    const QString projectGuid = project->getProjectGuid();
    for (const auto &row : db->fetchProjectPinnedAssets(projectGuid)) {
        if (row.type != static_cast<int>(ModelTypes::Texture)) continue;
        if (!memberstamp::isStamped(row.properties)) continue;
        bool read = true;
        const bool used = !db->hasMultipleDependers(row.guid, &read).isEmpty();
        if (used || !read) {
            if (!read) JahLog::write(JahLog::legacy, JahLog::Level::Warning,
                QStringLiteral("members: '%1' is kept - who uses it could not be read").arg(row.guid));
            continue;
        }
        out.append(describeRow(row, QStringLiteral("project")));
    }
    return out;
}

QVector<Unused> cleanUnused(Database *db, Project *project, const QString &materialGuid,
                            QString *errorOut)
{
    QVector<Unused> removed;
    if (!db) {
        if (errorOut) *errorOut = QStringLiteral("no library");
        return removed;
    }
    if (materialGuid.isEmpty() && (!project || project->getProjectGuid().isEmpty())) {
        if (errorOut) *errorOut = QStringLiteral("a project-scope clean needs an open project");
        return removed;
    }

    const QVector<Unused> candidates = unused(db, project, materialGuid);
    DbBatch batch(db);
    for (const Unused &entry : candidates) {
        if (entry.scope == QLatin1String("library")) {
            // No pins by construction (`unused` checked), so this is the full
            // delete — of the ROW. The bytes stay for `assets.gc`, which is
            // the only thing in this app allowed to decide a shared object is
            // dead.
            if (assetdelete::remove(db, entry.guid).ok) removed.append(entry);
        } else {
            if (db->unpinAsset(project->getProjectGuid(), entry.guid)) removed.append(entry);
        }
    }
    return removed;
}

QString projectCopyName(Database *db, const QString &projectGuid, const QString &sourceName)
{
    const QString source = sourceName.trimmed();
    if (!db || projectGuid.isEmpty() || source.isEmpty()) return source;
    QSet<QString> taken;
    {
        db::Statement query(db->connection(),
            "SELECT a.name FROM assets a JOIN project_assets p ON p.asset_guid = a.guid "
            "WHERE p.project_guid = ? AND a.type = ?");
        dbqt::bindText(query, 1, projectGuid);
        query.bindInt(2, static_cast<int>(ModelTypes::Material));
        db::Error e;
        while (query.step(&e)) taken.insert(dbqt::text(query, 0).trimmed().toCaseFolded());
    }
    if (!taken.contains(source.toCaseFolded())) return source;
    // THE SUFFIX IS APPENDED, NEVER PARSED OUT OF THE NAME: "Brick 2024" is a
    // name, and its second copy is "Brick 2024 2", not "Brick 2025". The caller
    // passes the LIBRARY ENTRY's name as the base (the preset a copy was made
    // from, when there is one), so copies of copies still count on from it.
    for (int n = 2;; ++n) {
        const QString candidate = QStringLiteral("%1 %2").arg(source, QString::number(n));
        if (!taken.contains(candidate.toCaseFolded())) return candidate;
    }
}

QJsonObject copyDefinition(Database *db, Project *project, const QString &materialGuid,
                           bool pristine)
{
    if (!db || materialGuid.isEmpty()) return QJsonObject();
    QJsonObject definition = MaterialBundle::read(db, materialGuid, pristine ? nullptr : project);
    if (definition.isEmpty()) return definition;
    const QJsonObject bakedMaps = definition.value(QStringLiteral("bake")).toObject()
                                            .value(QStringLiteral("maps")).toObject();
    if (!bakedMaps.isEmpty()) {
        QJsonObject values = definition.value(QStringLiteral("values")).toObject();
        for (auto it = bakedMaps.constBegin(); it != bakedMaps.constEnd(); ++it)
            values.remove(it.key());
        definition[QStringLiteral("values")] = values;
    }
    definition.remove(QStringLiteral("bake"));
    return definition;
}

QString duplicate(Database *db, Project *project, const QString &materialGuid,
                  const QString &name, QString *errorOut)
{
    const auto fail = [errorOut](const QString &why) {
        if (errorOut) *errorOut = why;
        return QString();
    };
    if (!db) return fail(QStringLiteral("no library"));
    const AssetRecord row = db->fetchAsset(materialGuid);
    if (row.guid.isEmpty() || row.type != static_cast<int>(ModelTypes::Material))
        return fail(QStringLiteral("'%1' is not a material").arg(materialGuid));

    // THE BAKE DOES NOT COME WITH IT (copyDefinition). A baked map's member
    // row belongs to the ORIGINAL (its `parent`), so a copy naming it would
    // change appearance when the original is re-baked, and neither material's
    // Clean unused could reason about it. The slots it filled are cleared with
    // it; the copy's own save bakes its own maps into its own member rows.
    QJsonObject definition = copyDefinition(db, project, materialGuid, /*pristine*/ false);
    if (definition.isEmpty())
        return fail(QStringLiteral("'%1' has no definition to copy").arg(row.name));

    // A NAME NOBODY ELSE HAS, decided against EVERY material row — the
    // library's and every project's own (a drawer is a view of them, and with
    // a project open it does not even list every material).
    QSet<QString> taken;
    for (const QString &existing : MaterialBundle::materialNames(db)) taken.insert(existing);
    QString chosen = name.trimmed().isEmpty()
                         ? QStringLiteral("%1 copy").arg(row.name)
                         : name.trimmed();
    const QString base = chosen;
    for (int n = 2; taken.contains(chosen); ++n) chosen = QStringLiteral("%1 %2").arg(base).arg(n);
    definition[QStringLiteral("name")] = chosen;

    QString error;
    // THE COPY LIVES WHERE ITS ORIGINAL LIVES when that is a project (its own
    // row) or the Materials module's storage; a copy of anything else — an
    // Assets material, a platform row — is a NEW material, made in the
    // Materials storage (ASSETS-HOME-1: Assets only takes imports and saves).
    const assethome::Home source = assethome::of(db, materialGuid);
    const assethome::Home home = source.isProject() ? source : assethome::materials();
    const QString copy = MaterialBundle::create(db, chosen, definition, home,
                                                row.thumbnail, &error);
    if (copy.isEmpty()) return fail(error.isEmpty() ? QStringLiteral("the library refused the copy")
                                                    : error);
    return copy;
}

QString saveToAssets(Database *db, Project *project, const QString &materialGuid,
                     const QString &name, QString *errorOut)
{
    const auto fail = [errorOut](const QString &why) {
        if (errorOut) *errorOut = why;
        return QString();
    };
    if (!db) return fail(QStringLiteral("no library"));
    const AssetRecord row = db->fetchAsset(materialGuid);
    if (row.guid.isEmpty() || row.type != static_cast<int>(ModelTypes::Material))
        return fail(QStringLiteral("'%1' is not a material").arg(materialGuid));
    if (row.home().kind == assethome::Kind::Assets)
        return fail(QStringLiteral("'%1' is in Assets already").arg(row.name));
    if (!MaterialBundle::shippedPresetName(materialGuid).isEmpty())
        return fail(QStringLiteral("'%1' is a material the app ships — create a material from it "
                                   "first").arg(row.name));

    // THE WHOLE BUNDLE, BAKE INCLUDED: the version the user sees (pin-first in
    // the open project), and every member — picked pictures and baked maps
    // alike — copied into Assets as the new row's own by the bundle writer
    // (MaterialBundle::write, the member rule), so the saved copy is complete
    // on its own and outlives whatever it was saved from.
    QJsonObject definition = MaterialBundle::read(db, materialGuid, project);
    if (definition.isEmpty())
        return fail(QStringLiteral("'%1' has no definition to save").arg(row.name));
    const QString chosen = MaterialBundle::uniqueName(db, name.trimmed().isEmpty() ? row.name
                                                                                   : name.trimmed());
    definition[QStringLiteral("name")] = chosen;

    QString error;
    const QString saved = MaterialBundle::create(db, chosen, definition, assethome::assets(),
                                                 row.thumbnail, &error,
                                                 assethome::Origin::ExplicitSave);
    if (saved.isEmpty())
        return fail(error.isEmpty() ? QStringLiteral("Assets refused the copy") : error);
    return saved;
}

QVector<Unused> reapExclusiveMembers(Database *db, const QString &materialGuid)
{
    if (!db || materialGuid.isEmpty()) return {};

    // TWO KINDS OF MEMBER, and they are found two different ways.
    //
    // A PICKED PICTURE is an ordinary library row carrying this bundle's
    // ORIGIN STAMP — `unused` finds it, and refuses it if anything still
    // depends on it or any project pins it. (No project passed: this is the
    // library's clean-up, and `unused` weighs pins whatever project is open.)
    QVector<Unused> removed = cleanUnused(db, nullptr, materialGuid, nullptr);

    // A BAKED MAP is a member by the PARENT relation (M-A: born inside
    // exactly one material, never shared), and a parented row is invisible to
    // every library listing — so `unused`, which walks the library's own
    // listing, cannot see one. Deleting the material would otherwise leave
    // its baked maps behind for ever: unreachable from any browser, owned by
    // a row that no longer exists.
    // The rows whose PARENT is this material, read straight: `Database`
    // exposes no children-by-parent accessor, and `fetchAssetsFromParent`
    // despite its name answers the DEPENDENCY closure, which is a different
    // question (and one `unused` has already asked).
    QStringList children;
    {
        db::Statement query(db->connection(), "SELECT guid FROM assets WHERE parent = ? AND type = ?");
        dbqt::bindText(query, 1, materialGuid);
        query.bindInt(2, static_cast<int>(ModelTypes::Texture));
        db::Error e;
        while (query.step(&e)) children << dbqt::text(query, 0);
    }
    for (const QString &child : children) {
        // A FAILED READ KEEPS THE MAP (NOQT-DATABASE-1 stage 2): remove() below (keepShared) never asks who USES the
        // map itself, so "nobody uses it" from a failed read here would delete a map in use.
        bool read = true;
        if (!db->hasMultipleDependers(child, &read).isEmpty()) continue;   // somebody uses it
        if (!assetdelete::livePins(db, child, &read).isEmpty()) continue;  // a project holds it
        if (!read) {
            JahLog::write(JahLog::legacy, JahLog::Level::Warning,
                QStringLiteral("members: the baked map '%1' is kept - who uses or pins it could not be read").arg(child));
            continue;
        }
        const AssetRecord row = db->fetchAsset(child);
        Unused entry;
        entry.guid = child;
        entry.name = row.name;
        entry.bytes = storedBytes(db, nullptr, child);
        entry.scope = QStringLiteral("library");
        if (assetdelete::remove(db, child).ok) removed.append(entry);
    }
    return removed;
}

QString makeUnique(Database *db, Project *project, const QString &materialGuid,
                   const QString &textureGuid, QString *errorOut)
{
    const auto fail = [errorOut](const QString &why) {
        if (errorOut) *errorOut = why;
        return QString();
    };
    if (!db) return fail(QStringLiteral("no library"));
    if (materialGuid.isEmpty() || textureGuid.isEmpty())
        return fail(QStringLiteral("a material and a texture are required"));

    const AssetRecord texture = db->fetchAsset(textureGuid);
    if (texture.guid.isEmpty() || texture.type != static_cast<int>(ModelTypes::Texture))
        return fail(QStringLiteral("'%1' is not a texture").arg(textureGuid));

    QJsonObject definition = MaterialBundle::read(db, materialGuid, project);
    if (definition.isEmpty())
        return fail(QStringLiteral("no material '%1'").arg(materialGuid));
    if (!MaterialBundle::memberGuids(definition).contains(textureGuid))
        return fail(QStringLiteral("'%1' is not a member of this material").arg(texture.name));

    db::Connection &conn = db->connection();
    const QString root = AssetStorePaths::root();
    // THE BYTES THIS CALLER IS LOOKING AT, not the library's (fix round F16's
    // nit): with a project open the copy must be of the version that project
    // renders with — a texture it copied on write is precisely the case
    // somebody reaches for "make unique" in — so the source is pin-first,
    // exactly as `MaterialBundle::read` is. `resolvePinned` falls back to the
    // library source when there is no pin.
    const QString sourcePath =
        (project && !project->getProjectGuid().isEmpty())
            ? AssetCas::resolvePinned(conn, root, project->getProjectGuid(), textureGuid)
            : AssetCas::resolveSource(conn, root, textureGuid);
    if (sourcePath.isEmpty())
        return fail(QStringLiteral("'%1' has no stored bytes to copy").arg(texture.name));

    // THE NEW ROW OVER THE SAME BYTES. `ingestFile` is content-addressed and
    // idempotent, so re-ingesting the resolved object costs one asset_files
    // row and NO disk: the object is already there under its own hash, and
    // both rows now name it. That is the whole trick — "a second row on the
    // same bytes, zero disk" (spec §4).
    const QString newGuid = QString::fromStdString(iris::newGuid());
    // THE COPY IS THIS MATERIAL'S MEMBER, whatever the row it was copied from
    // carries: the origin of a row minted here is the material that asked for
    // it (memberstamp::stamped replaces, where the by-guid `stamp` never moves
    // an origin that already exists).
    const QByteArray props = memberstamp::stamped(texture.properties, materialGuid);

    // A COPY IS A ROW OF ITS OWN, never a member row of somebody else (fix
    // round F16's nit). `parent` is inherited only when it names a FOLDER —
    // filing, which the copy should keep. When it names an ASSET the row is
    // an import member or a BAKED map born inside another material, and
    // copying that parent produced a row hidden from every listing
    // (Database::memberSubquery drops it) and reachable by nothing.
    // "IS THE PARENT AN ASSET" UNREAD IS NO "NO" (NOQT-DATABASE-1 stage 2): the copy would be filed under an asset,
    // hidden from every listing - refused by name before the first write.
    QString parent = texture.parent;
    if (!parent.isEmpty()) {
        db::Statement asset(conn, "SELECT 1 FROM assets WHERE guid = ?");
        dbqt::bindText(asset, 1, parent);
        db::Error e;
        if (asset.step(&e)) parent.clear();
        else if (e) return fail(QStringLiteral("where '%1' is filed could not be read - nothing was made").arg(texture.name));
    }

    // THE COPY'S HOME (ASSETS-SCOPE-1): the open project's when that project
    // holds the material (the swap below is its copy-on-write), else the
    // material's own — never a library tile minted for a project's edit.
    // A READ THAT FAILED IS NOT "THE LIBRARY's" (NOQT-DATABASE-1 stage 2): it would mint the copy in the library and
    // write the project's swap over the LIBRARY's definition - refused by name before the first write.
    bool read = true;
    const bool projectHolds = project && !project->getProjectGuid().isEmpty()
                              && db->isAssetPinnedBy(project->getProjectGuid(), materialGuid, &read);
    if (!read) return fail(QStringLiteral("whether this project holds the material could not be read - nothing was made"));
    const assethome::Home home = projectHolds ? assethome::project(project->getProjectGuid())
                                              : assethome::of(db, materialGuid);
    DbBatch batch(db);
    db->createAssetEntry(newGuid, texture.name, static_cast<int>(ModelTypes::Texture),
                         parent, home, assethome::bornInside(home),
                         texture.license, texture.author, texture.thumbnail,
                         props, texture.tags, QByteArray());

    QString error;
    QString oid;
    if (!AssetCas::ingestFile(conn, root, sourcePath, newGuid, QStringLiteral("source"),
                              texture.name, &oid, &error)) {
        db->deleteAsset(newGuid, /*force=*/true);
        return fail(error.isEmpty() ? QStringLiteral("the store refused the copy") : error);
    }

    // THIS MATERIAL'S DEFINITION NOW NAMES THE NEW ROW — every place the old
    // guid appears in it: a master slot, a bake record, a graph texture node.
    QJsonObject values = definition.value(QStringLiteral("values")).toObject();
    for (const QString &key : values.keys())
        if (values.value(key).toString() == textureGuid) values[key] = newGuid;
    if (!values.isEmpty()) definition[QStringLiteral("values")] = values;

    QJsonObject bake = definition.value(QStringLiteral("bake")).toObject();
    QJsonObject maps = bake.value(QStringLiteral("maps")).toObject();
    bool movedBake = false;
    for (const QString &key : maps.keys())
        if (maps.value(key).toString() == textureGuid) { maps[key] = newGuid; movedBake = true; }
    if (movedBake) {
        bake[QStringLiteral("maps")] = maps;
        definition[QStringLiteral("bake")] = bake;
    }

    QJsonObject graph = definition.value(QStringLiteral("shadergraph")).toObject();
    if (!graph.isEmpty()) {
        QJsonArray nodes = graph.value(QStringLiteral("nodes")).toArray();
        bool movedNode = false;
        for (int i = 0; i < nodes.size(); ++i) {
            QJsonObject node = nodes.at(i).toObject();
            if (node.value(QStringLiteral("type")).toString() != QLatin1String("texture")) continue;
            if (node.value(QStringLiteral("value")).toString() != textureGuid) continue;
            node[QStringLiteral("value")] = newGuid;
            nodes.replace(i, node);
            movedNode = true;
        }
        if (movedNode) {
            graph[QStringLiteral("nodes")] = nodes;
            definition[QStringLiteral("shadergraph")] = graph;
        }
    }

    // A PROJECT'S EDIT STAYS THE PROJECT'S (the owner's model, §12 Q2): when
    // the open project holds this material, the swap is a copy-on-write and
    // the library original keeps the shared texture.
    // (The answer read above, before the first write: the same question.)
    const auto written = MaterialBundle::write(
        db, project, materialGuid, definition,
        projectHolds ? MaterialBundle::Scope::Project : MaterialBundle::Scope::Library);
    if (!written.ok) {
        db->deleteAsset(newGuid, /*force=*/true);
        return fail(written.error);
    }
    return newGuid;
}

}   // namespace materialmembers
