/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/avatarassets.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QSet>
#include <QSqlDatabase>
#include <QTemporaryDir>

#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "services/assetcas.h"
#include "services/assetmetadata.h"
#include "services/assetstorepaths.h"
#include "services/projectassets.h"
#include "services/rigsignature.h"

namespace {

QString definitionFileName(const QString &name)
{
    QString base = name.trimmed();
    // The display name is user text; the file name rides the CAS as a display
    // string only (the object on disk is its sha256), but it still ends up in
    // archives and raw exports, so it must be a file name.
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9 _.-]")), QStringLiteral("_"));
    if (base.isEmpty()) base = QStringLiteral("avatar");
    return base + QStringLiteral(".avatar");
}

}   // namespace

bool AvatarAssets::scopeFromName(const QString &name, Scope &out)
{
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("library")) { out = Scope::Library; return true; }
    if (n == QLatin1String("project")) { out = Scope::Project; return true; }
    return false;
}

QString AvatarAssets::scopeName(Scope scope)
{
    return scope == Scope::Library ? QStringLiteral("library") : QStringLiteral("project");
}

bool AvatarAssets::isAvatarRow(const QString &guid, Database *db)
{
    if (!db || guid.isEmpty()) return false;
    return db->fetchAsset(guid).type == static_cast<int>(ModelTypes::Avatar);
}

QString AvatarAssets::libraryVersion(const QString &guid)
{
    return AssetCas::sourceOid(QSqlDatabase::database(), guid);
}

QString AvatarAssets::projectVersion(const QString &guid, Project *project)
{
    if (!project || project->getProjectGuid().isEmpty()) return QString();
    return AssetCas::pinnedOid(QSqlDatabase::database(), project->getProjectGuid(), guid);
}

bool AvatarAssets::isEdited(const QString &guid, Project *project)
{
    const QString pinned = projectVersion(guid, project);
    if (pinned.isEmpty()) return false;          // not in the project at all
    return pinned != libraryVersion(guid);
}

QString AvatarAssets::stageDefinition(const iris::AvatarDefinition &definition,
                                      const QString &fileName, QString *errorOut)
{
    // The staging file lives in the STORE's own staging area, not in /tmp: the
    // ingest hardlinks when it can, and a hardlink across filesystems degrades
    // to a full copy of every save.
    const QString stagingRoot = QDir(AssetStorePaths::root()).filePath(QStringLiteral("staging"));
    QDir().mkpath(stagingRoot);
    const QString dir = QDir(stagingRoot).filePath(GUIDManager::generateGUID());
    if (!QDir().mkpath(dir)) {
        if (errorOut) *errorOut = QStringLiteral("could not create a staging directory");
        return QString();
    }
    const QString path = QDir(dir).filePath(fileName);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) *errorOut = QStringLiteral("could not write %1").arg(path);
        return QString();
    }
    file.write(QJsonDocument(iris::avatarDefinitionToJson(definition)).toJson());
    file.close();
    return path;
}

void AvatarAssets::reconcileDependencies(const QString &guid,
                                         const iris::AvatarDefinition &definition, Database *db,
                                         Project *project)
{
    if (!db) return;
    const QString projectGuid = project ? project->getProjectGuid() : QString();

    // The definition IS the dependency list: whatever it names now is what the
    // closure (pins, archive walkers, gc) must see. Edges it no longer names go
    // — otherwise a clip removed in the module keeps riding every export
    // forever, and the removal looks like it did nothing.
    const QStringList wanted = definition.dependencyGuids();
    QSet<QString> keep(wanted.begin(), wanted.end());

    for (const QString &existing : db->fetchAssetGUIDAndDependencies(guid, false)) {
        if (!keep.contains(existing)) db->deleteDependency(guid, existing);
        else keep.remove(existing);   // already recorded
    }
    for (const QString &missing : wanted) {
        if (!keep.contains(missing)) continue;
        const auto record = db->fetchAsset(missing);
        if (record.guid.isEmpty()) continue;   // a guid nothing in the catalog knows
        db->createDependency(static_cast<int>(ModelTypes::Avatar), record.type, guid, missing,
                             projectGuid);
    }
}

QString AvatarAssets::create(const QString &objectGuid, Scope scope, Database *db, Project *project,
                             const QString &nameOverride, QString *errorOut)
{
    const auto refuse = [&](const QString &message) {
        if (errorOut) *errorOut = message;
        return QString();
    };
    if (!db) return refuse(QStringLiteral("no library is open"));
    if (scope == Scope::Project && (!project || project->getProjectGuid().isEmpty()))
        return refuse(QStringLiteral("a project-scope avatar needs an open project"));

    const auto record = db->fetchAsset(objectGuid);
    if (record.guid.isEmpty())
        return refuse(QStringLiteral("no asset with guid '%1'").arg(objectGuid));
    if (record.type != static_cast<int>(ModelTypes::Object))
        return refuse(QStringLiteral("'%1' is not a model — an avatar is made FROM a rigged "
                                     "model Object").arg(record.name));

    const QJsonObject meta = AssetMetadata::ensure(db, objectGuid);
    if (!meta.value(QStringLiteral("hasSkeleton")).toBool())
        return refuse(QStringLiteral("'%1' carries no skeleton (%2 bones) — only a rigged model "
                                     "can become an avatar")
                          .arg(record.name)
                          .arg(meta.value(QStringLiteral("bones")).toInt()));

    iris::AvatarDefinition definition;
    definition.name = nameOverride.trimmed().isEmpty() ? record.name : nameOverride.trimmed();
    definition.modelAsset = objectGuid;
    definition.rig.rigId = meta.value(QStringLiteral("rigId")).toString();
    definition.rig.bones = meta.value(QStringLiteral("bones")).toInt();
    for (const auto &v : meta.value(QStringLiteral("boneNames")).toArray())
        definition.rig.boneNames.append(v.toString());

    // The model's OWN clips, named by the one display rule: a Mixamo character
    // download ships a clip literally called "mixamo.com", and the avatar's
    // clip list is what the user reads, so it shows the file's base name.
    const QString base = QFileInfo(record.name).completeBaseName();
    QSet<QString> used;
    for (const auto &v : meta.value(QStringLiteral("animations")).toArray()) {
        const QJsonObject clip = v.toObject();
        const QString raw = clip.value(QStringLiteral("name")).toString();
        QString display = rig::displayNameFor(raw, base);
        QString unique = display;
        for (int suffix = 2; used.contains(unique); ++suffix)
            unique = display + QStringLiteral(" %1").arg(suffix);
        used.insert(unique);
        iris::AvatarClipEntry entry;
        entry.asset = objectGuid;       // an in-file clip references the model itself
        entry.rawName = raw;
        entry.name = unique;
        // A ZERO-LENGTH clip must not loop (Animation::getSampleTime is a fmod,
        // so length 0 samples at NaN) — Mixamo ships one in every character
        // download, and it is usually the FIRST one, i.e. the default.
        entry.looping = clip.value(QStringLiteral("length")).toDouble() > 0.0;
        definition.clips.append(entry);
    }
    if (!definition.clips.isEmpty()) definition.defaultClip = definition.clips.first().name;

    const QString guid = GUIDManager::generateGUID();
    const QString fileName = definitionFileName(definition.name);
    QString stagingPath = stageDefinition(definition, fileName, errorOut);
    if (stagingPath.isEmpty()) return QString();
    const QString stagingDir = QFileInfo(stagingPath).absolutePath();

    QSqlDatabase conn = QSqlDatabase::database();
    QString oid;
    QString ingestError;
    const bool ingested = AssetCas::ingestFile(conn, AssetStorePaths::root(), stagingPath, guid,
                                               QStringLiteral("source"), fileName, &oid,
                                               &ingestError);
    QDir(stagingDir).removeRecursively();
    if (!ingested)
        return refuse(QStringLiteral("the definition could not be stored: %1").arg(ingestError));

    // The tile: an avatar LOOKS like its character, so it inherits the model's
    // thumbnail rather than showing a generic placeholder for a JSON file.
    db->createAssetEntry(guid, definition.name, static_cast<int>(ModelTypes::Avatar), QString(),
                         scope == Scope::Project ? project->getProjectGuid() : QString(),
                         QString(), QString(), record.thumbnail, QByteArray(), QByteArray(),
                         QByteArray(), AssetViewFilter::AssetsView);
    reconcileDependencies(guid, definition, db, project);
    AssetCas::writeSidecar(conn, AssetStorePaths::root(), guid, nullptr);

    if (scope == Scope::Project) {
        // The project owns this row AND pins it — pinning is what gives the
        // project a VERSION it can copy-on-write later, and a project row with
        // no pin would have no version at all.
        ProjectAssets::addToProject(guid, db, project, ProjectAssets::AddKind::Direct);
    }
    return guid;
}

AvatarAssets::Loaded AvatarAssets::load(const QString &guid, Scope scope, Database *db,
                                        Project *project)
{
    Loaded out;
    if (!db) { out.error = QStringLiteral("no library is open"); return out; }
    const auto record = db->fetchAsset(guid);
    if (record.guid.isEmpty()) {
        out.error = QStringLiteral("no asset with guid '%1'").arg(guid);
        return out;
    }
    if (record.type != static_cast<int>(ModelTypes::Avatar)) {
        out.error = QStringLiteral("'%1' is not an avatar asset").arg(record.name);
        return out;
    }
    out.name = record.name;

    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    QString path;
    if (scope == Scope::Project) {
        if (!project || project->getProjectGuid().isEmpty()) {
            out.error = QStringLiteral("project scope needs an open project");
            return out;
        }
        out.oid = AssetCas::pinnedOid(conn, project->getProjectGuid(), guid);
        if (out.oid.isEmpty()) {
            out.error = QStringLiteral("'%1' is not in this project — add it first")
                            .arg(record.name);
            return out;
        }
        path = AssetCas::resolvePinned(conn, root, project->getProjectGuid(), guid);
    } else {
        out.oid = AssetCas::sourceOid(conn, guid);
        path = AssetCas::resolveSource(conn, root, guid);
    }
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        out.error = QStringLiteral("'%1' has no stored definition").arg(record.name);
        return out;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        out.error = QStringLiteral("could not read the definition of '%1'").arg(record.name);
        return out;
    }
    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    QString parseError;
    if (!iris::avatarDefinitionFromJson(obj, out.definition, &parseError)) {
        out.error = QStringLiteral("'%1': %2").arg(record.name, parseError);
        return out;
    }
    return out;
}

QString AvatarAssets::save(const QString &guid, Scope scope,
                           const iris::AvatarDefinition &definition, Database *db, Project *project,
                           QString *errorOut)
{
    const auto refuse = [&](const QString &message) {
        if (errorOut) *errorOut = message;
        return QString();
    };
    if (!db) return refuse(QStringLiteral("no library is open"));
    if (!definition.isValid())
        return refuse(QStringLiteral("the definition names no model asset"));
    const auto record = db->fetchAsset(guid);
    if (record.guid.isEmpty()) return refuse(QStringLiteral("no asset with guid '%1'").arg(guid));
    if (record.type != static_cast<int>(ModelTypes::Avatar))
        return refuse(QStringLiteral("'%1' is not an avatar asset").arg(record.name));

    QString name;
    AssetCas::resolveSource(QSqlDatabase::database(), AssetStorePaths::root(), guid, &name);
    if (name.isEmpty()) name = definitionFileName(definition.name);

    QString stagingPath = stageDefinition(definition, name, errorOut);
    if (stagingPath.isEmpty()) return QString();
    const QString stagingDir = QFileInfo(stagingPath).absolutePath();

    QString oid;
    QString error;
    if (scope == Scope::Project) {
        if (!project || project->getProjectGuid().isEmpty()) {
            QDir(stagingDir).removeRecursively();
            return refuse(QStringLiteral("project scope needs an open project"));
        }
        // COPY-ON-WRITE: new bytes, only THIS project's pin moves. The library
        // pointer and every other project are untouched — the owner's "project
        // edits go to the project", implemented by the pipeline itself.
        oid = ProjectAssets::copyOnWrite(guid, stagingPath, db, project, &error);
    } else {
        // A LIBRARY save PUBLISHES: the bytes become an object and the
        // library's own source pointer moves to them. No pin moves, so a
        // project that pinned the old version keeps rendering it (§1).
        QSqlDatabase conn = QSqlDatabase::database();
        if (AssetCas::ingestFile(conn, AssetStorePaths::root(), stagingPath, guid,
                                 QStringLiteral("source"), name, &oid, &error))
            if (!AssetCas::moveSourcePointer(conn, guid, oid, name, &error)) oid.clear();
    }
    QDir(stagingDir).removeRecursively();
    if (oid.isEmpty())
        return refuse(error.isEmpty() ? QStringLiteral("the definition could not be stored")
                                      : error);

    reconcileDependencies(guid, definition, db, project);
    // The row's name follows the definition's: the module renames an avatar by
    // renaming the thing, not by a second gesture on the tile.
    if (!definition.name.isEmpty() && definition.name != record.name)
        db->updateAssetMetadata(guid, definition.name, record.tags);
    // The metadata block describes the DEFINITION, so it is stale the moment
    // one is saved; drop it and let the lazy backfill recompute on demand.
    {
        QJsonObject props = QJsonDocument::fromJson(record.properties).object();
        props.remove(QStringLiteral("metadata"));
        db->updateAssetProperties(guid, QJsonDocument(props).toJson());
    }
    AssetCas::writeSidecar(QSqlDatabase::database(), AssetStorePaths::root(), guid, nullptr);
    return oid;
}

QString AvatarAssets::saveToLibrary(const QString &guid, Database *db, Project *project,
                                    QString *errorOut)
{
    const auto refuse = [&](const QString &message) {
        if (errorOut) *errorOut = message;
        return QString();
    };
    if (!db) return refuse(QStringLiteral("no library is open"));
    if (!project || project->getProjectGuid().isEmpty())
        return refuse(QStringLiteral("Save to Library needs an open project — it publishes the "
                                     "PROJECT's version"));
    const auto record = db->fetchAsset(guid);
    if (record.guid.isEmpty()) return refuse(QStringLiteral("no asset with guid '%1'").arg(guid));
    if (record.type != static_cast<int>(ModelTypes::Avatar))
        return refuse(QStringLiteral("'%1' is not an avatar asset").arg(record.name));

    QSqlDatabase conn = QSqlDatabase::database();
    const QString pinned = AssetCas::pinnedOid(conn, project->getProjectGuid(), guid);
    if (pinned.isEmpty())
        return refuse(QStringLiteral("'%1' is not in this project, so there is no project "
                                     "version to publish").arg(record.name));

    if (!record.projectGuid.isEmpty()) {
        // Created IN this project: PROMOTE IN PLACE (§4 D6-A). The row keeps
        // its guid — a fresh library row would orphan every instance that
        // already names this one, in scenes that may not even be open.
        if (!db->updateAssetProject(guid, QString()))
            return refuse(QStringLiteral("could not promote '%1' to the library").arg(record.name));
        // Its dependencies are project rows too when they were imported here;
        // they stay where they are (a library avatar may depend on a project
        // model, and the pin carries the bytes) — recorded, not silently
        // "fixed", because moving somebody else's rows is not this verb's job.
    }

    QString name;
    AssetCas::resolveSource(conn, AssetStorePaths::root(), guid, &name);
    QString error;
    if (!AssetCas::moveSourcePointer(conn, guid, pinned, name, &error))
        return refuse(error.isEmpty() ? QStringLiteral("could not move the library pointer")
                                      : error);
    AssetCas::writeSidecar(conn, AssetStorePaths::root(), guid, nullptr);
    return pinned;
}
