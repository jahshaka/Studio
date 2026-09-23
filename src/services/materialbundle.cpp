/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/materialbundle.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "data/constants.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"

namespace {

/// The definition's file name inside the store. One name per asset, so a
/// re-save of the SAME material rewrites its own asset_files row instead of
/// accumulating one row per version (the PK is (guid, role, name)).
QString definitionFileName()
{
    return QStringLiteral("material.json");
}

QJsonObject parseDefinition(const QByteArray &bytes)
{
    if (bytes.isEmpty()) return QJsonObject();
    return QJsonDocument::fromJson(bytes).object();
}

} // namespace

namespace MaterialBundle {

QString presetMasterOf(const QByteArray &rowProperties)
{
    if (rowProperties.isEmpty()) return QString();
    const QString recorded = QJsonDocument::fromJson(rowProperties).object()
                                 .value(QStringLiteral("presetMaster")).toString();
    return shippedPresetName(recorded).isEmpty() ? QString() : recorded;
}

QString foreignCopyRefusal(Database *db, const QString &guid, const QString &projectGuid)
{
    if (!db || guid.isEmpty()) return QString();
    const QString master = presetMasterOf(db->fetchAsset(guid).properties);
    if (master.isEmpty()) return QString();
    const QVector<AssetPinRecord> pins = db->fetchAssetPins(guid);
    if (pins.isEmpty()) return QString();
    QString owner;
    for (const AssetPinRecord &pin : pins) {
        if (pin.projectGuid == projectGuid) return QString();
        if (owner.isEmpty()) owner = pin.projectName;
    }
    return QObject::tr("'%1' here is project '%2''s own copy of the shipped preset. Add the "
                       "preset itself: this project makes its own copy on its first edit.")
        .arg(shippedPresetName(master), owner);
}

const QSet<QString> &textureSlots()
{
    // Built ONCE from the material's own property list — including the twelve
    // generated detail-layer rows, which is exactly what a hand-written list
    // would have missed.
    static const QSet<QString> slotNames = [] {
        QSet<QString> out;
        auto material = iris::PbrMaterial::create();
        for (auto *prop : material->properties)
            if (prop && prop->type == iris::PropertyType::Texture)
                out.insert(prop->name);
        return out;
    }();
    return slotNames;
}

bool looksLikePath(const QString &value)
{
    if (value.isEmpty()) return false;
    if (value.contains(QLatin1Char('/')) || value.contains(QLatin1Char('\\')))
        return true;
    // An asset guid has no extension; every image file does.
    return !QFileInfo(value).suffix().isEmpty();
}

QString offendingPath(const QJsonObject &definition, QString *slotOut)
{
    const auto report = [slotOut](const QString &slot, const QString &value) {
        if (slotOut) *slotOut = slot;
        return value;
    };

    const QJsonObject values = definition.value(QStringLiteral("values")).toObject();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (!textureSlots().contains(it.key())) continue;
        const QString value = it.value().toString();
        if (looksLikePath(value)) return report(it.key(), value);
    }

    // THE GRAPH IS WHERE THE PATH GOT IN (F3, §1.3): the evaluator is handed a
    // resolver that turns a guid into a path, and for a moment the RESOLVED
    // value was what got serialized. A texture node carries a guid or nothing.
    const QJsonArray nodes =
        definition.value(QStringLiteral("shadergraph")).toObject()
                  .value(QStringLiteral("nodes")).toArray();
    for (const auto &entry : nodes) {
        const QJsonObject node = entry.toObject();
        if (node.value(QStringLiteral("type")).toString() != QLatin1String("texture"))
            continue;
        const QString value = node.value(QStringLiteral("value")).toString();
        if (looksLikePath(value))
            return report(QStringLiteral("shadergraph.%1").arg(
                              node.value(QStringLiteral("id")).toString()),
                          value);
    }
    return QString();
}

QStringList memberGuids(const QJsonObject &definition)
{
    QStringList out;
    const auto add = [&out](const QString &guid) {
        if (guid.isEmpty() || looksLikePath(guid)) return;
        if (!out.contains(guid)) out.append(guid);
    };

    const QJsonObject values = definition.value(QStringLiteral("values")).toObject();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        if (textureSlots().contains(it.key())) add(it.value().toString());

    // Baked maps are member textures whose bytes live in the store; the slot
    // value in `values` already names them, so this only catches a bake record
    // for a slot the values no longer carry.
    const QJsonObject baked = definition.value(QStringLiteral("bake")).toObject()
                                        .value(QStringLiteral("maps")).toObject();
    for (auto it = baked.constBegin(); it != baked.constEnd(); ++it)
        add(it.value().toString());

    // A graph's texture nodes: an image a user picked inside the module is a
    // member of the material even before it reaches a master slot.
    const QJsonArray nodes =
        definition.value(QStringLiteral("shadergraph")).toObject()
                  .value(QStringLiteral("nodes")).toArray();
    for (const auto &entry : nodes) {
        const QJsonObject node = entry.toObject();
        if (node.value(QStringLiteral("type")).toString() != QLatin1String("texture"))
            continue;
        add(node.value(QStringLiteral("value")).toString());
    }
    return out;
}

bool reconcileEdges(Database *db, const QString &guid, const QJsonObject &definition,
                    const QString &projectGuid)
{
    if (!db || guid.isEmpty()) return false;
    QSqlDatabase conn = QSqlDatabase::database();
    if (!conn.isOpen()) return false;

    // ONE EDGE SET PER SCOPE, and the scope is the one whose definition this
    // is. With no project: the INTRINSIC edges (audit G2/G3), the ones with no
    // stamp — a bundle's own membership is a fact about the bundle, not about
    // whichever project happened to be open. With a project: THAT PROJECT's
    // edges for this material, and the intrinsic set is left exactly as it is.
    //
    // Why the project scope needs its own set at all (phase 2): a project-scope
    // save is a copy-on-write, so the library's version — and therefore the
    // library's membership — must not move. But the project's version DOES
    // have members, and with no edge at all nothing could see them: "used by"
    // read 0 for every picture picked into a material the project owns, the
    // V-2 fold never fired, and a closure walk over the project's rows found
    // a material that depended on nothing.
    //
    // An edge whose DEPENDER is somebody else — a node's USE of this material
    // — is not touched by either branch: this deletes only the edges FROM this
    // material in this one scope.
    //
    // WHO OWNS THE EDGES *FROM* A MATERIAL, stated because two writers reach
    // them (fix round F10). THIS function owns them, in both scopes: an edge
    // from a material to a texture means "this material is made of that
    // picture", and the definition is the only thing that knows. The other
    // writer is the preset apply (`SceneEditService::applyMaterialPreset`),
    // which writes the same project-stamped Material -> Texture rows for the
    // material row it mints per preset; the two AGREE by construction,
    // because it writes exactly the maps its definition names and skips an
    // edge that already exists. If they ever disagree, the definition is
    // right and this is the writer that says so — which is the whole reason
    // membership is DERIVED and never authored.
    QSqlQuery del(conn);
    if (projectGuid.isEmpty()) {
        del.prepare("DELETE FROM dependencies WHERE depender = ? AND project_guid IS NULL");
        del.addBindValue(guid);
    } else {
        del.prepare("DELETE FROM dependencies WHERE depender = ? AND project_guid = ?");
        del.addBindValue(guid);
        del.addBindValue(projectGuid);
    }
    if (!del.exec()) return false;

    bool ok = true;
    for (const QString &member : memberGuids(definition)) {
        const auto record = db->fetchAsset(member);
        // A guid the catalog does not know names nothing to depend on.
        if (record.guid.isEmpty()) continue;
        ok = db->createDependency(static_cast<int>(ModelTypes::Material),
                                  record.type, guid, member, projectGuid)
             && ok;
    }
    return ok;
}

QJsonObject normaliseUv(const QJsonObject &definition)
{
    // ONE SPELLING FOR THE UV TRANSFORM, and it is the DOCUMENT's — the same
    // rule as the colour above, and it was missing for the same slot shape.
    // The evaluator's folded transform is a two-element ARRAY
    // (`textureScale: [4, 4]`), and `MaterialReader::parsePbrMaterial` reads
    // `textureScale` as a FLOAT row: `QJsonValue::toDouble()` of an array is
    // ZERO, so a material saved with any non-identity tiling came back with
    // its UV scale at 0 — one texel stretched over the whole surface. The
    // document's rows are `textureScale`/`textureScaleV`,
    // `textureOffsetU`/`textureOffsetV` and `textureRotation`, and that is
    // what a definition holds.
    QJsonObject out = definition;
    QJsonObject values = out.value(QStringLiteral("values")).toObject();
    const auto split = [&values](const QString &key, const QString &uKey, const QString &vKey) {
        const QJsonValue value = values.value(key);
        if (!value.isArray()) return false;
        const QJsonArray pair = value.toArray();
        const double fallback = uKey == QLatin1String("textureScale") ? 1.0 : 0.0;
        const double u = pair.size() > 0 ? pair.at(0).toDouble(fallback) : fallback;
        values[uKey] = u;
        values[vKey] = pair.size() > 1 ? pair.at(1).toDouble(u) : u;
        if (key != uKey) values.remove(key);
        return true;
    };
    bool moved = split(QStringLiteral("textureScale"), QStringLiteral("textureScale"),
                       QStringLiteral("textureScaleV"));
    moved = split(QStringLiteral("textureOffset"), QStringLiteral("textureOffsetU"),
                  QStringLiteral("textureOffsetV")) || moved;
    if (moved) out[QStringLiteral("values")] = values;
    return out;
}

QJsonObject normaliseColours(const QJsonObject &definition)
{
    QJsonObject out = definition;
    QJsonObject values = out.value(QStringLiteral("values")).toObject();
    bool moved = false;
    for (const QString &key : values.keys()) {
        const QJsonValue value = values.value(key);
        if (!value.isObject()) continue;
        const QJsonObject rgba = value.toObject();
        if (!rgba.contains(QStringLiteral("r"))) continue;
        const QColor colour = QColor::fromRgbF(
            qBound(0.0, rgba.value(QStringLiteral("r")).toDouble(), 1.0),
            qBound(0.0, rgba.value(QStringLiteral("g")).toDouble(), 1.0),
            qBound(0.0, rgba.value(QStringLiteral("b")).toDouble(), 1.0),
            qBound(0.0, rgba.value(QStringLiteral("a")).toDouble(1.0), 1.0));
        values[key] = colour.name();
        moved = true;
    }
    if (moved) out[QStringLiteral("values")] = values;
    return out;
}

QJsonObject read(Database *db, const QString &guid, Project *project)
{
    if (!db || guid.isEmpty()) return QJsonObject();

    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();

    // D-2: the definition is a store object. PIN-FIRST in a project, so a
    // project renders the version it was built with (F11).
    QString path;
    if (project && !project->getProjectGuid().isEmpty())
        path = AssetCas::resolvePinned(conn, root, project->getProjectGuid(), guid);
    else
        path = AssetCas::resolveSource(conn, root, guid);

    if (!path.isEmpty()) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonObject definition = parseDefinition(file.readAll());
            if (!definition.isEmpty()) return definition;
        }
    }

    // No stored definition: the row's blob. An image companion minted before
    // this lane, and any row a reader reaches before its first save.
    return parseDefinition(db->fetchAssetData(guid));
}

namespace {

/// The one publish. `allowShipped` is true for exactly one caller — the
/// preset seeder — and false for the world; see `shippedPresetName`.
WriteResult writeImpl(Database *db, Project *project, const QString &guid,
                      const QJsonObject &definition, Scope scope, bool allowShipped);

} // namespace

WriteResult write(Database *db, Project *project, const QString &guid,
                  const QJsonObject &definition, Scope scope)
{
    return writeImpl(db, project, guid, definition, scope, false);
}

WriteResult writeShipped(Database *db, const QString &guid, const QJsonObject &definition)
{
    return writeImpl(db, nullptr, guid, definition, Scope::Library, true);
}

namespace {

WriteResult writeImpl(Database *db, Project *project, const QString &guid,
                      const QJsonObject &definition, Scope scope, bool allowShipped)
{
    WriteResult result;
    const auto fail = [&result](const QString &message) {
        result.ok = false;
        result.error = message;
        return result;
    };
    if (!db || guid.isEmpty()) return fail(QStringLiteral("no material"));
    if (definition.isEmpty()) return fail(QStringLiteral("an empty definition"));

    // A SHIPPED PRESET IS READ-ONLY IN FACT (phase 3, owner §12 Q2). The
    // drawer says so and offers no edit gesture, but a convention the writer
    // does not enforce is a convention an autosave breaks: the module saves
    // every 1.5 s while a user types, and a preset opened in any way at all
    // would have been republished under the guid the whole app treats as
    // immutable. Refused BY NAME, so the message the module puts in the scene
    // issue bar tells the user which material it is and what to do.
    const QString shipped = shippedPresetName(guid);
    if (!shipped.isEmpty() && !allowShipped)
        return fail(QStringLiteral("'%1' is a material the app ships and is read-only - "
                                   "Customise it to make your own copy").arg(shipped));

    // LOCK 3 (F3). A path in a definition is a reference that exists on one
    // machine; it is refused at the one place definitions are written, so no
    // caller can be the exception.
    QString slot;
    const QString path = offendingPath(definition, &slot);
    if (!path.isEmpty())
        return fail(QStringLiteral("'%1' holds a file path ('%2'), not an asset guid")
                        .arg(slot, path));

    // ONE SPELLING FOR A COLOUR, and it is the DOCUMENT's — beside the path
    // guard, for the same reason: a definition is refused or corrected HERE
    // so that no caller can be the exception. (The first attempt did this in
    // the graph's definition builder, and `MaterialsApi::create` — which
    // builds one without that function — shipped the evaluator's
    // object-valued colours straight into the store, so a scripted graph
    // material rendered BLACK.)
    QJsonObject stored = normaliseUv(normaliseColours(definition));
    stored[QStringLiteral("version")] = kDefinitionVersion;
    if (!stored.contains(QStringLiteral("materialType")))
        stored[QStringLiteral("materialType")] = QStringLiteral("pbr");

    // KNOWN, RECORDED, NOT FIXED IN PHASE 1: every save publishes a new store
    // object for the definition, and the superseded one waits for
    // `assets.gc` like any other superseded content. On the graph page's
    // 1.5 s autosave that is one small JSON per edit burst — the definitions
    // measured on the fixture are 0.5-4 KB — so it is a few hundred KB over a
    // long session, not a growth problem, and the content-addressed name
    // means an edit that changes nothing publishes nothing new at all.
    const QByteArray bytes = QJsonDocument(stored).toJson(QJsonDocument::Compact);

    // WHERE THE DEFINITION IS STAGED DECIDES WHETHER THIS CALL FSYNCS, and
    // for a SHIPPED PRESET it must not (FSYNC-2's law: no durable write on
    // the thread that draws). `AssetCas::storeObject` hardlinks when the
    // staging file and the store share a filesystem and COPIES + FSYNCS when
    // they do not — and the system temp dir is a different filesystem from
    // the store on every box that matters (here /tmp is tmpfs; on the owner's
    // the store is a USB volume). Staging inside the STORE ROOT takes the
    // link, so publishing a preset's definition is two renames and no device
    // wait.
    //
    // WHY ONLY FOR A PRESET. A link means the bytes this function wrote are
    // not flushed, and for a material the user authored that would be a
    // silent loss of durability: nothing can re-derive their graph. A
    // preset's definition IS re-derivable — it is a projection of a file the
    // app ships plus catalog rows — and `MaterialPresetAssets::ensureSeeded`
    // re-seeds a preset whose definition reads empty, so a torn write heals
    // itself on the next launch. That is exactly the rule FSYNC-2 wrote for
    // the sidecar (`Durability::Derived`), applied where it actually holds.
    const QString root = AssetStorePaths::root();
    QTemporaryDir staging(allowShipped
                              ? QDir(root).filePath(QStringLiteral("presetdef-XXXXXX"))
                              : QDir::tempPath() + QStringLiteral("/jahmatdef-XXXXXX"));
    if (!staging.isValid()) return fail(QStringLiteral("no staging directory"));
    const QString tmpPath = staging.filePath(definitionFileName());
    {
        QFile file(tmpPath);
        if (!file.open(QIODevice::WriteOnly)) return fail(QStringLiteral("could not stage the definition"));
        if (file.write(bytes) != bytes.size()) return fail(QStringLiteral("a short write"));
    }

    QSqlDatabase conn = QSqlDatabase::database();
    QString error;
    QString oid;

    // ONE TRANSACTION FOR THE CATALOG HALF (F19, phase 1's code review).
    // Publishing runs ingest → move the source pointer (or the pin) → blob →
    // edges → pins, and a failure between any two of them used to COMMIT the
    // steps before it: a definition object recorded against the asset that
    // nothing pointed at, edges describing a version the row does not have.
    // Every one of those writes is a catalog write, so one guard makes the
    // publish atomic — the guard rolls back at destruction unless `commit()`
    // is reached, and it degrades to a no-op inside somebody else's
    // transaction (an import slice, a gesture batch), which is the correct
    // nesting behaviour.
    //
    // THE GUARANTEE, stated rather than implied, because half of a publish is
    // NOT in the database: the BYTES are content-addressed and are written to
    // the store before the rows. A failure therefore leaves an object in the
    // store that no row names — which is precisely what `assets.gc` collects
    // (its dry-run-first law, assetgc.h), and precisely the harmless direction
    // of the two. The opposite order — a row naming bytes that are not there —
    // is the one that cannot be repaired, and this function never produces it.
    DbTransaction tx(conn);

    if (!AssetCas::ingestFile(conn, root, tmpPath, guid, QStringLiteral("source"),
                              definitionFileName(), &oid, &error))
        return fail(error.isEmpty() ? QStringLiteral("the store refused the definition") : error);
    result.oid = oid;

    if (scope == Scope::Project) {
        // Only THIS project's pin moves — the library original and every other
        // project keep theirs (the owner's model, §12 Q2).
        if (!project || project->getProjectGuid().isEmpty())
            return fail(QStringLiteral("a project-scope save needs an open project"));
        if (!AssetCas::writePin(conn, project->getProjectGuid(), guid, oid))
            return fail(QStringLiteral("could not move the project pin"));
    } else {
        // The LIBRARY publishes: the row's own source pointer moves. ingestFile
        // is an INSERT OR IGNORE on (guid, role, name), so on a re-save it left
        // the mapping on the previous oid — moveSourcePointer is the write that
        // publishes (assetcas.h says so in as many words).
        if (!AssetCas::moveSourcePointer(conn, guid, oid, definitionFileName(), &error))
            return fail(error.isEmpty() ? QStringLiteral("could not publish the definition") : error);
        // A project with no pin yet renders the library version; one that has a
        // pin keeps it. A library save is not an update of anybody's project.
    }

    // The row's `asset` blob is a CACHE of the LIBRARY's current definition —
    // what every listing, thumbnailer and search reads without touching disk.
    // A project-scope save must not move it: the library's version did not
    // change.
    if (scope == Scope::Library) db->updateAssetAsset(guid, bytes);

    // INTRINSIC EDGES ARE THE LIBRARY'S (F7). A PROJECT-scope save is a
    // copy-on-write: it moves THIS project's pin and must leave the library
    // version alone in every respect — including its membership. Rewriting
    // the NULL-project edges from a project's edited definition made the
    // library row's closure describe a version the library does not have, so
    // another project's add-to-project pinned the library oid and walked the
    // edited edges: the right definition with the wrong textures.
    //
    // A project's own membership needs no second edge set: `addToProject`
    // walks the closure at add time and the pins are what an archive reads,
    // and the pins are written below.
    reconcileEdges(db, guid, stored,
                   scope == Scope::Library || !project ? QString() : project->getProjectGuid());

    // EVERY MEMBER THE DEFINITION NAMES IS PINNED (F8). A baked map is minted
    // during the write itself, and the ordinary order is "add the material to
    // the project, THEN edit it" — so the member row is neither project-owned
    // nor pinned, and the archive manifest (project rows + pins) does not
    // carry it. The material then travels without the maps it is made of.
    // Pinning here is also what makes the closure right after a project-scope
    // save, where the intrinsic edges deliberately did not move.
    //
    // A MEMBER THE PROJECT ALREADY PINS IS LEFT WHERE IT IS (fix round F4 —
    // the twin of the loss fixed in `ProjectAssets::updatePinToLatest`, and
    // the more dangerous one, because THIS runs on the graph page's 1.5 s
    // AUTOSAVE). `writePin` is an upsert, so re-pinning every member to the
    // library's current oid on each save silently reset a texture the project
    // had copied on write — the user's own painted version — and did it on an
    // edit to the MATERIAL, which the user never connected to their texture.
    // What this loop is FOR is the member that nothing pinned yet (a baked map
    // minted during this very write); a member with a pin already has the
    // version this project chose. An empty source oid never overwrites a real
    // pin either: "empty" means a DB-only asset, and writing one over bytes is
    // how a pinned member silently became unpinned.
    if (project && !project->getProjectGuid().isEmpty()
        && db->isAssetPinnedBy(project->getProjectGuid(), guid)) {
        const QString projectGuid = project->getProjectGuid();
        for (const QString &member : memberGuids(stored)) {
            if (db->fetchAsset(member).guid.isEmpty()) continue;
            if (!AssetCas::pinnedOid(conn, projectGuid, member).isEmpty()) continue;
            const QString latest = AssetCas::sourceOid(conn, member);
            if (latest.isEmpty() && db->isAssetPinnedBy(projectGuid, member)) continue;
            AssetCas::writePin(conn, projectGuid, member, latest);
        }
    }

    if (!tx.commit()) return fail(QStringLiteral("the catalog refused the definition"));

    // AFTER the commit, deliberately: a sidecar is a PROJECTION of committed
    // rows (FSYNC-2's Durability::Derived rule). Writing it inside the guard
    // would describe rows a rollback then took away.
    QString sidecarError;
    if (!AssetCas::writeSidecar(conn, root, guid, &sidecarError))
        qWarning("MaterialBundle::write: could not refresh the sidecar for %s (%s)",
                 qUtf8Printable(guid), qUtf8Printable(sidecarError));

    result.ok = true;
    return result;
}

} // namespace

QString uniqueName(Database *db, const QString &wanted)
{
    QSet<QString> taken;
    if (db)
        for (const auto &row : db->fetchAssetsForAssetView())
            if (row.type == static_cast<int>(ModelTypes::Material))
                taken.insert(row.name.toCaseFolded());
    for (auto it = Constants::Reserved::DefaultMaterials.constBegin();
         it != Constants::Reserved::DefaultMaterials.constEnd(); ++it)
        taken.insert(it.value().toCaseFolded());
    const QString base = wanted.trimmed();
    if (base.isEmpty()) return base;
    QString chosen = base;
    for (int n = 1; taken.contains(chosen.toCaseFolded()); ++n)
        chosen = QStringLiteral("%1-%2").arg(base).arg(n);
    return chosen;
}

namespace {

/// THE ROW MINT both doors share: the catalog row, then the definition
/// through `write` — and the row is taken back when the write is refused, so
/// a refused mint leaves nothing behind.
QString mintRow(Database *db, const QString &guid, const QString &name,
                const QJsonObject &definition, const QByteArray &thumbnail,
                QString *errorOut)
{
    QJsonObject stored = definition;
    stored[QStringLiteral("name")] = name;

    db->createAssetEntry(guid, name, static_cast<int>(ModelTypes::Material),
                         QString(),          // a library row: no parent folder
                         QString(),          // a library row: no project guid
                         QString(), QString(), thumbnail,
                         QByteArray(), QByteArray(), QByteArray(),
                         AssetViewFilter::AssetsView);

    const WriteResult written = write(db, nullptr, guid, stored, Scope::Library);
    if (!written.ok) {
        if (errorOut) *errorOut = written.error;
        db->deleteAsset(guid);
        return QString();
    }
    return guid;
}

} // namespace

QString create(Database *db, const QString &name, const QJsonObject &definition,
               const QByteArray &thumbnail, QString *errorOut)
{
    if (!db) {
        if (errorOut) *errorOut = QStringLiteral("no database");
        return QString();
    }
    // A NEW MATERIAL MAY NOT TAKE A SHIPPED PRESET'S NAME (PRESET-UNIFY-1 fix
    // round). A preset is reached BY NAME from a script, a drag payload and
    // the tray, so a user material called "Gold PBR" is not a duplicate
    // label: it is a material nothing can ever address by name, while the
    // name goes on resolving to the preset. Refused here, at the one door
    // every new bundle comes through, and by `assettags::write` for a rename.
    if (!shippedPresetGuidForName(name).isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("'%1' is the name of a material the app ships — choose "
                                       "another. (A PROJECT'S own copy of a preset does keep the "
                                       "preset's name — MaterialBundle::createPresetCopy — "
                                       "because there the copy replaces the master.)").arg(name);
        return QString();
    }

    return mintRow(db, GUIDManager::generateGUID(), name, definition, thumbnail, errorOut);
}

QString createPresetCopy(Database *db, const QString &guid, const QString &name,
                         const QJsonObject &definition, const QByteArray &thumbnail,
                         QString *errorOut)
{
    if (!db) {
        if (errorOut) *errorOut = QStringLiteral("no database");
        return QString();
    }
    // NO NAME GUARD HERE, ON PURPOSE — see the header: this copy IS the
    // preset as far as the project is concerned, so it carries its name.
    return mintRow(db, guid.isEmpty() ? GUIDManager::generateGUID() : guid, name,
                   definition, thumbnail, errorOut);
}

} // namespace MaterialBundle
