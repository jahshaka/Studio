/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/shippedassets.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "data/database/database.h"
#include "data/project.h"
#include "irisgl/core/irisutils.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/import/assetimportservice.h"
#include "services/projectassets.h"

namespace ShippedAssets
{

namespace {

/// The library Texture row whose SOURCE bytes are `oid`, or empty.
///
/// Identity is the content, not a name: the store is keyed on the sha256, so
/// "is the default tile already in this library" is one indexed lookup, and
/// it answers the same row whoever put those bytes there first — an earlier
/// project, the user importing tile.png by hand, or a sample archive that
/// carries the tile (four of the shipped samples do). Only a 'source' row of
/// a TEXTURE counts: an imported model's embedded copy of the same image is a
/// 'texture'-role file of an Object, not a texture asset a material may name.
///
/// Order: a row this project already pins (re-creating a scene in the same
/// project must not grow a second membership), then a LISTED row (a row the
/// user deleted from the library but a project still pins is legitimate
/// reuse, just not the first choice), then the guid, so two equal candidates
/// always answer the same way.
///
/// ONLY A ROW OF THE RIGHT HOME ANSWERS (ASSETS-SCOPE-1 F1): a library row
/// always may; a project's OWN row (Editor) only when `ownerProject` is that
/// project. A library material's picture must never be answered with some
/// project's private row — that project's remove would delete it under the
/// library definition naming it.
QString libraryTextureForOn(QSqlDatabase conn, const QString &oid, const QString &projectGuid,
                            const QString &ownerProject = QString())
{
    QSqlQuery query(conn);
    query.prepare(QStringLiteral(
                      "SELECT AF.asset_guid FROM asset_files AF "
                      "JOIN assets A ON A.guid = AF.asset_guid "
                      "LEFT JOIN project_assets PA ON PA.asset_guid = AF.asset_guid "
                      "                           AND PA.project_guid = ? "
                      "WHERE AF.oid = ? AND AF.role = 'source' AND A.type = ? "
                      "AND (A.view_filter IS NOT ?%1) "
                      "ORDER BY (PA.asset_guid IS NOT NULL) DESC, A.listed DESC, AF.asset_guid")
                      .arg(ownerProject.isEmpty() ? QString()
                                                  : QStringLiteral(" OR A.project_guid = ?")));
    query.addBindValue(projectGuid);
    query.addBindValue(oid);
    query.addBindValue(static_cast<int>(ModelTypes::Texture));
    query.addBindValue(static_cast<int>(AssetViewFilter::Editor));
    if (!ownerProject.isEmpty()) query.addBindValue(ownerProject);
    if (query.exec() && query.next()) return query.value(0).toString();
    return QString();
}

} // namespace

QString libraryTextureFor(const QString &oid, const QString &projectGuid)
{
    if (oid.isEmpty()) return QString();
    return libraryTextureForOn(QSqlDatabase::database(), oid, projectGuid);
}

namespace {

/// THE ONE CONTENT IMPORT, with the pin optional.
///
/// `pin` false is the LIBRARY import a material bundle's texture picker needs
/// (MATERIAL_BUNDLE_SPEC P-2): the image becomes a library Texture row by its
/// CONTENT — the same row a second pick of the same bytes answers, which is
/// what makes duplicate textures impossible — and the caller decides whether
/// anything pins it. Everything else about the route is identical, because it
/// IS the same route.
Pinned importTextureContent(const QString &sourcePath, const QString &displayName,
                            Database *db, Project *project, Ownership ownership, bool pin,
                            bool ownedByProject, bool shipped, const QString &knownOid = QString())
{
    Pinned out;
    if (sourcePath.isEmpty() || !QFileInfo(sourcePath).isFile()) {
        out.error = QStringLiteral("no such file '%1'").arg(sourcePath);
        return out;
    }
    // NO PROJECT, NO PIN. The startup placeholder (Project::createNew: no guid,
    // no folder) and a scripted session that never created a project render
    // the shipped file directly — there is no project to pin into, and neither
    // ever saves, so nothing will ever need the guid. A LIBRARY import (pin
    // false) has a row to make either way and does not take this door.
    const bool haveProject = db && project && !project->getProjectGuid().isEmpty();
    if (pin && !haveProject) {
        out.path = sourcePath;
        return out;
    }
    if (!db) {
        out.error = QStringLiteral("no library");
        return out;
    }

    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    const QString projectGuid = haveProject ? project->getProjectGuid() : QString();

    const QString oid = knownOid.isEmpty() ? AssetCas::hashFile(sourcePath) : knownOid;
    if (oid.isEmpty()) {
        out.error = QStringLiteral("could not read '%1'").arg(sourcePath);
        return out;
    }

    const QString ownerProject = (ownedByProject && haveProject) ? projectGuid : QString();
    QString guid = libraryTextureForOn(conn, oid, projectGuid, ownerProject);
    // A row whose object is no longer in the store (purged, a store moved
    // without its objects) cannot serve a scene; a fresh import brings the
    // bytes back under a new row rather than handing out a guid that
    // resolves to nothing.
    if (!guid.isEmpty() && AssetCas::resolveSource(conn, root, guid).isEmpty()) guid.clear();

    if (guid.isEmpty()) {
        // THE ONE PIPELINE. The image importer names the row after the file
        // it is given, so a display name that differs from the shipped file's
        // (tile.png -> "Tile.png", front.jpg -> "cove_front.jpg") is staged as
        // a copy under that name in a private temp dir. The bytes — and so
        // the object — are identical either way.
        QTemporaryDir staging;
        QString importPath = sourcePath;
        const QString name = displayName.trimmed();
        if (!name.isEmpty() && name != QFileInfo(sourcePath).fileName()) {
            if (!staging.isValid()) {
                out.error = QStringLiteral("cannot create a staging directory");
                return out;
            }
            importPath = QDir(staging.path()).filePath(name);
            if (!QFile::copy(sourcePath, importPath)) {
                out.error = QStringLiteral("cannot stage '%1'").arg(sourcePath);
                return out;
            }
        }
        ImportRequest request;
        request.sourcePath = importPath;
        request.typeHint = static_cast<int>(ModelTypes::Texture);
        // A MATERIAL (OR THE PLATFORM) ASKED, NEVER THE USER (IMPORT-INTENT-1).
        // Every door into this file is one of those: a material's texture
        // picker, a graph texture node, a preset's maps, the default floor's
        // checker, the emitter's particle image. So the import may not clear
        // the member stamp of a row it lands on — and it cannot reach that
        // code anyway, because by-content reuse is answered above and the
        // pipeline runs only when this call is MINTING a row. The intent is
        // still stated, because the next caller of this function will inherit
        // whatever it says.
        request.intent = ImportRequest::Intent::Material;
        // WHOSE ROW IT MINTS is the caller's answer (ASSETS-SCOPE-1): the
        // platform's furniture for the open project, or the picture of a
        // PROJECT'S material, is that project's own; a library material's
        // picture is a library row even with a project open.
        request.ownedByProject = ownedByProject && haveProject;
        request.shipped = shipped;
        AssetImportService importer(db, project);
        const ImportResult result = importer.import(request);
        if (!result.ok()) {
            out.error = result.error.isEmpty() ? QStringLiteral("import failed") : result.error;
            return out;
        }
        guid = result.assetGuid;
        out.minted = true;
    }

    // PLATFORM FURNITURE IS MARKED (owner, 2026-09-13: "the project asset tray
    // should only show assets and items added to the project"). The tray shows
    // what the user put in the project; the default floor's checker is pinned
    // by the editor itself, behind their back, so the row carries the same
    // `{"type": ...}` marker the editor writes on the rows it mints for scene
    // nodes, and the tray drops it by that same rule (services/assettray.h
    // rule 4).
    //
    // ON THE REUSED ROW TOO, not only on the one this call mints — measured,
    // and it is the whole point: the checker is identified by its CONTENT, so
    // in a library that already imported a sample scene (which carries the same
    // tile) the floor pins the SAMPLE's row, and a mint-only marker would leave
    // "Tile.png" in the tray of every project made on a real library. The cost
    // is that a user who imports this exact image by hand has that row marked
    // the first time a floor reuses it; they still get a tile for their add —
    // adding a texture to a project mints its companion material and THAT is
    // the tile (rule 5) — so nothing they added disappears from the tray.
    // One read, and a write only when the marker is missing: this runs when a
    // scene is built, not per frame.
    if (ownership == Ownership::Platform && !guid.isEmpty()) {
        const AssetRecord row = db->fetchAsset(guid);
        QJsonObject props = QJsonDocument::fromJson(row.properties).object();
        if (props.value(QStringLiteral("type")).toString().isEmpty()) {
            props.insert(QStringLiteral("type"), QStringLiteral("platform"));
            db->updateAssetProperties(guid, QJsonDocument(props).toJson());
        }
    }

    if (pin) {
        // A BINDING: the scene refers to the image (a material slot, an emitter,
        // a sky face), exactly like a decal's map or a light's IES profile — so
        // the pin and the session entry, and never a companion material.
        out.newlyPinned = !db->isAssetPinnedBy(projectGuid, guid);
        const ProjectAssets::Result pinned =
            ProjectAssets::addToProject(guid, db, project, ProjectAssets::AddKind::Binding);
        if (!pinned.ok()) {
            out.error = pinned.error;
            return out;
        }
        out.path = AssetCas::resolvePinned(conn, root, projectGuid, guid);
    } else {
        out.path = AssetCas::resolveSource(conn, root, guid);
    }
    if (out.path.isEmpty()) {
        out.error = QStringLiteral("asset '%1' has no stored bytes").arg(guid);
        return out;
    }
    out.guid = guid;
    return out;
}

} // namespace

Pinned pinTexture(const QString &sourcePath, const QString &displayName,
                  Database *db, Project *project, Ownership ownership)
{
    // The floor's checker, an emitter's image: FILES THE APP SHIPS — one
    // platform row per content, pinned by every project that uses it.
    return importTextureContent(sourcePath, displayName, db, project, ownership, /*pin=*/true,
                                /*ownedByProject=*/false, /*shipped=*/true);
}

Pinned importTexture(const QString &sourcePath, const QString &displayName,
                     Database *db, Project *project, const assethome::Home &home,
                     const QString &knownOid)
{
    // A LIBRARY row always; PINNED as well when a project is open. That is the
    // owner's answer to spec Q1 in one call: "into the library once, a member
    // of that material, and pinned into the open project".
    const bool haveProject = db && project && !project->getProjectGuid().isEmpty();
    const bool ownedByProject = home.isProject() && haveProject
                                && home.projectGuid == project->getProjectGuid();
    return importTextureContent(sourcePath, displayName, db, project,
                                Ownership::Project, /*pin=*/haveProject, ownedByProject,
                                /*shipped=*/false, knownOid);
}

QStringList SkyPreset::faces() const
{
    const QDir dir(directory);
    QStringList out;
    for (const char *face : { "front", "back", "left", "right", "top", "bottom" })
        out << dir.filePath(QStringLiteral("%1.%2").arg(QLatin1String(face), extension));
    return out;
}

QString SkyPreset::thumbnail() const
{
    return QDir(directory).filePath(QStringLiteral("front.%1").arg(extension));
}

QVector<SkyPreset> skyPresets()
{
    // The display names are the panel's, and they are not the folder names:
    // ame_desert has always been shown as "Hamarikyu" and yokohama as "Bay".
    struct Row { const char *name = nullptr; const char *dir = nullptr; const char *ext = nullptr; };
    static const Row rows[] = {
        { "Cove", "cove", "jpg" },
        { "Hamarikyu", "ame_desert", "png" },
        { "Bay", "yokohama", "jpg" },
        { "Field", "field", "jpg" },
        { "Creek", "creek", "jpg" },
        { "Space", "space", "png" },
    };
    QVector<SkyPreset> out;
    for (const Row &row : rows) {
        SkyPreset preset;
        preset.name = QLatin1String(row.name);
        preset.directory = IrisUtils::getAbsoluteAssetPath(
            QStringLiteral("app/content/skies/alternative/%1").arg(QLatin1String(row.dir)));
        preset.extension = QLatin1String(row.ext);
        out << preset;
    }
    return out;
}

QStringList pinSkyPreset(const QString &name, Database *db, Project *project,
                         QString *errorOut)
{
    auto fail = [errorOut](const QString &message) {
        if (errorOut) *errorOut = message;
        return QStringList();
    };
    if (!db || !project || project->getProjectGuid().isEmpty())
        return fail(QStringLiteral("no project is open to pin the sky's faces into"));

    const QVector<SkyPreset> presets = skyPresets();
    const SkyPreset *match = nullptr;
    for (const SkyPreset &preset : presets)
        if (preset.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0) match = &preset;
    if (!match) {
        QStringList names;
        for (const SkyPreset &preset : presets) names << preset.name;
        return fail(QStringLiteral("no sky preset '%1' — the presets are: %2")
                        .arg(name, names.join(QStringLiteral(", "))));
    }

    QStringList guids;
    const QString prefix = match->name.toLower();
    for (const QString &face : match->faces()) {
        const Pinned pinned = pinTexture(
            face, prefix + QLatin1Char('_') + QFileInfo(face).fileName(), db, project,
            Ownership::Project);
        if (!pinned.ok() || pinned.guid.isEmpty())
            return fail(QStringLiteral("sky preset '%1': face %2 — %3")
                            .arg(match->name, QFileInfo(face).fileName(),
                                 pinned.error.isEmpty() ? QStringLiteral("not pinned")
                                                        : pinned.error));
        guids << pinned.guid;
    }
    return guids;
}

} // namespace ShippedAssets
